/*
 *   Copyright (c) 2015, Nokia Solutions and Networks
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *
 * Event Machine fractal (Mandelbrot set) drawer.
 * Generate Mandelbroth fractal images and for each image zoom deeper into the
 * fractal. Prints frames-per-second and the frame range. Modify defined values
 * for different resolution, zoom point, starting frame, ending frame and
 * the precision of the fractal calculation.
 *
 *
 * Creates three Execution Objects (EO) to form a pipeline:
 *  --> ](Pixel_handler) --> ](Worker) --> ](Imager) -==> IMAGE
 *  \_____________________________________________/
 *
 * The image is divided into horizontal pixel blocks which are processed as
 * events. All the data events are allocated once at startup and reused for
 * every image frame.
 *
 * Every image frame begins when the Pixel_handler EO receives a new frame
 * event from the Imager EO.
 * The Pixel_handler EO changes the frame value in the preallocated pixel block
 * events and sends them to the Worker EO's parallel queue.
 *
 * The worker calculates the iteration values for every pixel in the block
 * using the mandelbrot algorithm and forwards the events to the Imager EO's
 * parallel queue. The Imager writes color values into the image buffer for the
 * given pixel block. The red, blue and green values are calculated using the
 * iteration value and for green additionally the frame value is used for
 * increased visuals.
 *
 * The Worker EO sends events using an event group that eventually triggers a
 * notification event to be sent to the atomic queue of the Imager EO. Using
 * the EVENTS_PER_IMAGE value as the event group count, Imager receives the
 * notification event once all the data events received through Imager's
 * parallel queue have been processed - one fractal image is now complete.
 * The Imager EO switches the active frame buffer to a non-active buffer during
 * notification event handling and sends a new frame event to the Pixel_handler
 * EO to start the processing of a new frame.
 *
 * Having sent the new frame event, Imager writes the ready buffer into a tmp
 * file and renames it to 'IMAGE_NAME'. The 'IMAGE_NAME' file is constantly
 * being displayed by the 'feh' image viewer and renaming avoids tearing caused
 * by displaying an incomplete file.
 *
 * Note: The test creates and mounts a ramdisk for the images and starts the
 *       'feh' image viewer as a background process to refresh the constantly
 *       updated fractal image file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/**
 * Test configuration
 */

/** Images and paths */
#define IMAGE_NAME      "fractal.ppm"
#define TMP_IMAGE_NAME  "tmp.ppm"
#define IMAGE_DIR       "/tmp/em-frac_ramdisk/"
#define IMAGE_PATH       (IMAGE_DIR IMAGE_NAME)
#define TMP_IMAGE_PATH   (IMAGE_DIR TMP_IMAGE_NAME)
#define REMOUNT_RAMDISK_FMT \
	"(umount %s; mount -t tmpfs -o size=%dM tmpfs %s) 2> /dev/null"

/** Resolution of the image: width */
#define WIDTH  640
/** Resolution of the image: heigth */
#define HEIGHT 480

/** Coordinates where the image zooms in. */
#define ZOOM_POINT_X -1.401155
#define ZOOM_POINT_Y  0.00002

/** Greater the frame, deeper the image */
#define START_FROM_FRAME 0

/** Stop after amount of frames and print elapsed time. 0 for endless zoom. */
#define STOP_AFTER_FRAMES 0

/** Mandelbrot algorithm precision. Smaller value, less precise fractal. */
#define MAX_ITERATIONS 1000

/** Image viewer refresh rate in seconds. Match to FPS for smoother image. */
#define IMAGE_VIEW_RATE 0.05

/**
 * Amount of horizontal pixels per image data event
 * Max event size 2KB with image_block_size 1019
 */
#define IMAGE_BLOCK_SIZE 80

/**
 * How many events is needed for one full image
 * Depends on the image and block size
 */
#define EVENTS_PER_IMAGE ((ROUND_UP(WIDTH, IMAGE_BLOCK_SIZE) \
			  / IMAGE_BLOCK_SIZE) * HEIGHT)

#define ALLOC_EVENTS     ((WIDTH / IMAGE_BLOCK_SIZE + 1) * HEIGHT)

/**
 * Define how many events are sent per em_send_multi() call
 */
#define SEND_MULTI_MAX 32

/**
 * Single horizontal line of the image.
 * Every pixel requires red, green and blue (3) bytes
 */
typedef struct {
	char col[WIDTH * 3];
} h_line_t;

/**
 * Image buffer consisting of 'HEIGHT' horizontal lines
 */
typedef struct {
	h_line_t line[HEIGHT];
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} buffer_t;

/*
 * EVENTS:
 */

/** Pixel handler event */
typedef struct {
	uint32_t frame;
} pixel_handler_event_t;

/** Data events contain a block of horizontal pixels */
typedef struct {
	uint16_t y;
	uint16_t x_start;
	uint16_t x_len;
	uint16_t pix_iter_array[IMAGE_BLOCK_SIZE];
	uint32_t frame;
} imager_data_event_t;

/** Notification event */
typedef struct {
	uint64_t seq;
} notif_event_t;

/** Union of all events for easy alloc&reuse */
typedef union {
	pixel_handler_event_t pixel_handler_event;
	imager_data_event_t imager_data_event;
	notif_event_t notif_event;
} fractal_event_t;

/*
 * EO contexts:
 */

/** My EO context */
typedef struct {
	char name[16];
	em_eo_t eo;
} my_eo_context_t;

/** Imager EO context holds the buffer which limits very large images */
typedef struct {
	char name[16];
	em_eo_t eo;
	struct timespec start_time;
	struct timespec next_print_time;
	uint32_t prev_print_frame;
	uint32_t frame;

	int buf_idx; /**< Index of active image buffer: buf[buf_idx] */
	buffer_t buf[2] ENV_CACHE_LINE_ALIGNED; /**< Fractal image buffer */
} imager_eo_context_t;

/* Verify that the image buffers don't share cache lines */
COMPILE_TIME_ASSERT((offsetof(imager_eo_context_t, buf[1]) -
		     offsetof(imager_eo_context_t, buf[0])) %
		    ENV_CACHE_LINE_SIZE == 0,
		    IMAGER_EO_CONTEXT_T__SIZE_ERROR);

/**
 * Queue context data
 */
typedef struct {
	/* Queue identifications */
	#define DATA_QUEUE  0
	#define NOTIF_QUEUE  1
	uint8_t id;
} imager_queue_context_t;

/**
 * Fractal shared memory
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* Allocate pixel handler EO context from shared memory region */
	my_eo_context_t eo_context_pixel_handler ENV_CACHE_LINE_ALIGNED;
	/* Allocate worker EO context from shared memory region */
	my_eo_context_t eo_context_worker ENV_CACHE_LINE_ALIGNED;
	/* Allocate imager EO context from shared memory region */
	imager_eo_context_t eo_context_imager ENV_CACHE_LINE_ALIGNED;
	/* Imager data queue context */
	imager_queue_context_t imager_data_queue_ctx ENV_CACHE_LINE_ALIGNED;
	/* Imager notification queue context */
	imager_queue_context_t imager_notif_queue_ctx ENV_CACHE_LINE_ALIGNED;

	em_event_t data_event_tbl[ALLOC_EVENTS] ENV_CACHE_LINE_ALIGNED;

	struct {
		/* EO queues */
		em_queue_t queue_pixel;
		em_queue_t queue_worker;
		em_queue_t queue_data_imager;
		em_queue_t queue_notif_imager;
		/* Event group for tracking the nbr of completed data events */
		em_event_group_t egrp_imager;
	} ENV_CACHE_LINE_ALIGNED;
} fractal_shm_t;

/* EM-core local pointer to shared memory */
static ENV_LOCAL fractal_shm_t *fractal_shm;

static em_status_t
pixel_handler_start(my_eo_context_t *eo_ctx, em_eo_t eo,
		    const em_eo_conf_t *conf);

static em_status_t
pixel_handler_stop(my_eo_context_t *eo_ctx, em_eo_t eo);

static void
pixel_handler_receive_event(my_eo_context_t *eo_ctx, em_event_t event,
			    em_event_type_t type, em_queue_t queue,
			    void *q_ctx);
static em_status_t
worker_start(my_eo_context_t *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
worker_stop(my_eo_context_t *eo_ctx, em_eo_t eo);

static void
worker_receive_event(my_eo_context_t *eo_ctx, em_event_t event,
		     em_event_type_t type, em_queue_t queue, void *q_ctx);

static em_status_t
imager_start(imager_eo_context_t *eo_ctx, em_eo_t eo,
	     const em_eo_conf_t *conf);

static em_status_t
imager_stop(imager_eo_context_t *eo_ctx, em_eo_t eo);

static void
imager_receive_event(imager_eo_context_t *eo_ctx, em_event_t event,
		     em_event_type_t type, em_queue_t queue,
		     imager_queue_context_t *q_ctx);
static void
snprintf_check(int cmd_size, int ret);

static void
run_system_cmds(void);

static void
create_data_events(void);

static void
free_data_events(void);

/**
 * Main function
 *
 * Call cm_setup() to perform test & EM setup common for all the
 * test applications.
 *
 * cm_setup() will call test_init() and test_start() and launch
 * the EM dispatch loop on every EM-core.
 */
int main(int argc, char *argv[])
{
	return cm_setup(argc, argv);
}

/**
 * Init of the Fractal test application.
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void
test_init(void)
{
	int core = em_core_id();

	if (core == 0) {
		fractal_shm = env_shared_reserve("FractalShMem",
						 sizeof(fractal_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		fractal_shm = env_shared_lookup("FractalShMem");
	}

	if (fractal_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Fractal init failed on EM-core: %u",
			   em_core_id());
	else if (core == 0)
		memset(fractal_shm, 0, sizeof(fractal_shm_t));
}

/**
 * Startup of the fractal test application.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void
test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo_pixel_handler, eo_worker, eo_imager;
	em_status_t ret, start_ret = EM_ERROR;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		fractal_shm->pool = appl_conf->pools[0];
	else
		fractal_shm->pool = EM_POOL_DEFAULT;

	APPL_PRINT("\n"
		   "***********************************************************\n"
		   "EM APPLICATION: '%s' initializing:\n"
		   "  %s: %s() - EM-core:%i\n"
		   "  Application running on %d EM-cores (procs:%d, threads:%d)\n"
		   "  using event pool:%" PRI_POOL "\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads,
		   fractal_shm->pool);

	test_fatal_if(fractal_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/* Create EOs */
	eo_pixel_handler =
		em_eo_create("Pixel handler",
			     (em_start_func_t)pixel_handler_start, NULL,
			     (em_stop_func_t)pixel_handler_stop, NULL,
			     (em_receive_func_t)pixel_handler_receive_event,
			     &fractal_shm->eo_context_pixel_handler);

	test_fatal_if(eo_pixel_handler == EM_EO_UNDEF,
		      "pixel handler creation failed!");

	eo_worker = em_eo_create("Worker",
				 (em_start_func_t)worker_start, NULL,
				 (em_stop_func_t)worker_stop, NULL,
				 (em_receive_func_t)worker_receive_event,
				 &fractal_shm->eo_context_worker);

	test_fatal_if(eo_worker == EM_EO_UNDEF, "Worker creation failed!");

	eo_imager = em_eo_create("Imager",
				 (em_start_func_t)imager_start, NULL,
				 (em_stop_func_t)imager_stop, NULL,
				 (em_receive_func_t)imager_receive_event,
				 &fractal_shm->eo_context_imager);

	test_fatal_if(eo_imager == EM_EO_UNDEF, "Imager creation failed!");

	fractal_shm->eo_context_pixel_handler.eo = eo_pixel_handler;
	fractal_shm->eo_context_worker.eo = eo_worker;
	fractal_shm->eo_context_imager.eo = eo_imager;

	/* Create event group to use with the imager data queue */
	fractal_shm->egrp_imager = em_event_group_create();
	test_fatal_if(fractal_shm->egrp_imager == EM_EVENT_GROUP_UNDEF,
		      "Event group creation failed!");

	/* Run system commands */
	run_system_cmds();

	/* Allocates all data events */
	create_data_events();

	/* Start EO pixel_handler */
	ret = em_eo_start_sync(eo_pixel_handler, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO pixel handler start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);

	/* Start EO worker */
	ret = em_eo_start_sync(eo_worker, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO worker start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);

	/* Start EO imager */
	ret = em_eo_start_sync(eo_imager, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO imager start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	const em_eo_t eo_pixel_handler =
	      fractal_shm->eo_context_pixel_handler.eo;
	const em_eo_t eo_worker = fractal_shm->eo_context_worker.eo;
	const em_eo_t eo_imager = fractal_shm->eo_context_imager.eo;
	em_status_t stat;
	em_event_group_t egrp;
	em_notif_t notif_tbl[1] = { {.event = EM_EVENT_UNDEF} };
	int num_notifs;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	stat = em_eo_stop_sync(eo_pixel_handler);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO pixel handler stop failed!");
	stat = em_eo_delete(eo_pixel_handler);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO pixel handler delete failed!");

	stat = em_eo_stop_sync(eo_worker);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO worker stop failed!");
	stat = em_eo_delete(eo_worker);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO worker delete failed!");

	stat = em_eo_stop_sync(eo_imager);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO imager stop failed!");
	stat = em_eo_delete(eo_imager);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO imager delete failed!");

	/* No more dispatching of the EO's events, egrp can be freed */

	egrp = fractal_shm->egrp_imager;
	if (!em_event_group_is_ready(egrp)) {
		num_notifs = em_event_group_get_notif(egrp, 1, notif_tbl);
		stat = em_event_group_abort(egrp);
		if (stat == EM_OK && num_notifs == 1)
			em_free(notif_tbl[0].event);
	}
	stat = em_event_group_delete(egrp);
	test_fatal_if(stat != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT "",
		      egrp, stat);

	free_data_events();
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(fractal_shm);
		em_unregister_error_handler();
	}
}

static void
snprintf_check(int cmd_size, int ret)
{
	if (ret < 0 || ret >= cmd_size)
		APPL_EXIT_FAILURE("snprintf(): ret=%d, errno(%i)=%s",
				  ret, errno, strerror(errno));
}

/**
 * Mounts ramdisk and opens feh as a background process.
 */
static void
run_system_cmds(void)
{
	int ret;
	char cmd[256];
	int ramdisk_size = (WIDTH * HEIGHT * 6) / 1000000 + 1;
	struct sigaction sa, old_sa;

	/* Change to default signal handler for SIGCHLD during system calls */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_DFL;
	if (sigaction(SIGCHLD, &sa, &old_sa) == -1)
		APPL_EXIT_FAILURE("sigaction(): errno(%i)=%s", errno,
				  strerror(errno));

	/* Create output directory */
	ret = snprintf(cmd, sizeof(cmd), "mkdir -p %s", IMAGE_DIR);
	snprintf_check(sizeof(cmd), ret);

	ret = system(cmd);
	if (ret != 0)
		APPL_EXIT_FAILURE("Failed to create output dir: %s\n", cmd);

	/* Remount ramdisk if it already exists*/
	ret = snprintf(cmd, sizeof(cmd), REMOUNT_RAMDISK_FMT,
		       IMAGE_DIR, ramdisk_size, IMAGE_DIR);

	snprintf_check(sizeof(cmd), ret);

	ret = system(cmd);
	if (ret != 0)
		APPL_PRINT("Ramdisk mount failed.\n"
			   "Run as root/sudo for better performance.\n");

	/* Check and start image viewer  */
	ret = system("feh -v > /dev/null");
	if (ret != 0) {
		APPL_PRINT("NOTE: Install 'feh' or open %s\n", IMAGE_PATH);
	} else {
		ret = snprintf(cmd, sizeof(cmd),
			       "echo \"P6\n1 1\n255\n\" > %s; feh %s -R %f &",
			       IMAGE_PATH, IMAGE_PATH, IMAGE_VIEW_RATE);

		snprintf_check(sizeof(cmd), ret);

		ret = system(cmd);
		if (ret != 0)
			APPL_PRINT("NOTE: 'feh' unable to display %s\n",
				   IMAGE_PATH);
	}

	if (sigaction(SIGCHLD, &old_sa, NULL) == -1)
		APPL_EXIT_FAILURE("sigaction(): errno(%i)=%s", errno,
				  strerror(errno));
}

/**
 * Creates all imager data events and calculates block values
 */
static void
create_data_events(void)
{
	imager_data_event_t *image_data_event;
	em_event_t event;
	int y, x_start, x_len;
	int index;

	index = 0;

	/* Start filling event array */
	for (y = 0; y < HEIGHT; y++) {
		/* Pixels in curr horizontal line divided into smaller blocks*/
		x_start = 0;
		while (x_start < WIDTH) {
			if (x_start + IMAGE_BLOCK_SIZE > WIDTH - 1)
				x_len = WIDTH - x_start;
			else
				x_len = IMAGE_BLOCK_SIZE;

			event = em_alloc(sizeof(fractal_event_t),
					 EM_EVENT_TYPE_SW,
					 fractal_shm->pool);
			test_fatal_if(event == EM_EVENT_UNDEF,
				      "Event alloc failed!");
			image_data_event = em_event_pointer(event);

			image_data_event->y = y;
			image_data_event->x_len = x_len;
			image_data_event->x_start = x_start;
			image_data_event->frame = 0;
			/* Add to array */
			test_fatal_if(index >= ALLOC_EVENTS,
				      "Event-tbl too small");
			fractal_shm->data_event_tbl[index++] = event;
			image_data_event = NULL;
			x_start += IMAGE_BLOCK_SIZE;
		}
	}
}

/**
 * Frees all imager data events
 */
static void
free_data_events(void)
{
	em_event_t event;
	int y, x_start;
	int index = 0;

	/* Free event array */
	for (y = 0; y < HEIGHT; y++) {
		/* Pixels in curr horizontal line divided into smaller blocks*/
		for (x_start = 0; x_start < WIDTH;
		     x_start += IMAGE_BLOCK_SIZE) {
			test_fatal_if(index >= ALLOC_EVENTS,
				      "Event-tbl too small");
			event = fractal_shm->data_event_tbl[index++];
			em_free(event);
		}
	}
}

/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
pixel_handler_start(my_eo_context_t *eo_ctx, em_eo_t eo,
		    const em_eo_conf_t *conf)
{
	em_queue_t queue;
	em_status_t status;
	const char *queue_name;

	(void)conf;

	/* Copy EO name */
	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	queue_name = "queue Pixel handler";

	queue = em_queue_create(queue_name, EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);

	test_fatal_if(queue == EM_QUEUE_UNDEF, "%s creation failed!",
		      queue_name);

	/* Store the queue handle */
	fractal_shm->queue_pixel = queue;

	status = em_eo_add_queue_sync(eo, queue);

	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
		      status, eo, queue);

	APPL_PRINT("Started %s - EO:%" PRI_EO ". Q:%" PRI_QUEUE ".\n",
		   eo_ctx->name, eo, queue);

	return EM_OK;
}

/**
 * @private
 *
 * Pixel Handler EO receive function.
 *
 * Receives a new frame event from Imager EO.
 * Changes the frame value in the preallocated events and sends them to Worker
 * EO for processing.
 */
static void
pixel_handler_receive_event(my_eo_context_t *eo_ctx, em_event_t event,
			    em_event_type_t type, em_queue_t queue,
			    void *q_ctx)
{
	pixel_handler_event_t *const pixel = em_event_pointer(event);
	const uint32_t frame = pixel->frame;
	int num_sent = 0;
	int i, j;

	(void)eo_ctx;
	(void)type;
	(void)queue;
	(void)q_ctx;

	em_free(event);
	if (unlikely(appl_shm->exit_flag))
		return;

	/* Start filling worker queue */
	for (i = 0; i < EVENTS_PER_IMAGE; i++) {
		imager_data_event_t *data;

		data = em_event_pointer(fractal_shm->data_event_tbl[i]);
		data->frame = frame;
	}

	const int send_rounds = EVENTS_PER_IMAGE / SEND_MULTI_MAX;
	const int left_over = EVENTS_PER_IMAGE % SEND_MULTI_MAX;

	for (i = 0, j = 0; i < send_rounds; i++, j += SEND_MULTI_MAX) {
		num_sent += em_send_multi(&fractal_shm->data_event_tbl[j],
					  SEND_MULTI_MAX,
					  fractal_shm->queue_worker);
	}
	if (left_over)
		num_sent += em_send_multi(&fractal_shm->data_event_tbl[j],
					  left_over,
					  fractal_shm->queue_worker);
	if (unlikely(num_sent != EVENTS_PER_IMAGE)) {
		for (i = num_sent; i < EVENTS_PER_IMAGE; i++)
			em_free(fractal_shm->data_event_tbl[i]);
		test_fatal_if(!appl_shm->exit_flag,
			      "em_send_multi(): num_sent:%d Q:%" PRI_QUEUE "",
			      num_sent, fractal_shm->queue_worker);
	}
}

/**
 * @private
 *
 * EO stop functions.
 *
 */
static em_status_t
pixel_handler_stop(my_eo_context_t *eo_ctx, em_eo_t eo)
{
	em_status_t ret;

	APPL_PRINT("pixel handler stop (%s, eo id %" PRI_EO ")\n",
		   eo_ctx->name, eo);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

static em_status_t
worker_stop(my_eo_context_t *eo_ctx, em_eo_t eo)
{
	em_status_t ret;

	APPL_PRINT("worker stop (%s, eo id %" PRI_EO ")\n",
		   eo_ctx->name, eo);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

static em_status_t
imager_stop(imager_eo_context_t *eo_ctx, em_eo_t eo)
{
	em_status_t ret;

	APPL_PRINT("imager stop (%s, eo id %" PRI_EO ")\n",
		   eo_ctx->name, eo);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

/**
 * @private
 *
 * Worker EO receive function.
 *
 * Calculates color values for the pixels in the given block.
 * Fills the values in the received event and forwards the event to Imager EO.
 *
 */
static void
worker_receive_event(my_eo_context_t *eo_ctx, em_event_t event,
		     em_event_type_t type, em_queue_t queue, void *q_ctx)
{
	em_status_t status;
	em_event_group_t event_group;
	imager_data_event_t *const block = em_event_pointer(event);
	double frame = block->frame;

	(void)eo_ctx;
	(void)type;
	(void)queue;
	(void)q_ctx;

	if (unlikely(appl_shm->exit_flag))
		return;

	/*
	 * Each iteration, it calculates: newz = oldz*oldz + p, where p is the
	 * current pixel, and oldz stars at the origin
	 */
	/* real and imaginary part of the pixel p */
	double pr, pi;
	/* real and imag parts of new and old z */
	double new_re, new_im, old_re, old_im;
	/* you can change the following to zoom and change position */
	double zoom = pow(frame, 2) / 1000;
	double move_x = ZOOM_POINT_X;
	double move_y = -ZOOM_POINT_Y;
	int x;

	for (x = block->x_start; x < (block->x_len + block->x_start); x++) {
		/**
		 * Calculate the initial real and imaginary part of z, based
		 * on the pixel location and zoom and position values
		 */
		pr = 1.5 * (x - WIDTH / 2) / (0.5 * zoom * WIDTH) + move_x;
		pi = (block->y - HEIGHT / 2) / (0.5 * zoom * HEIGHT) + move_y;
		/* these should start at 0,0 */
		new_re = 0, new_im = 0, old_re = 0, old_im = 0;
		/* "i" will represent the number of iterations */
		int i;
		/* Start the iteration process */
		for (i = 0; i < MAX_ITERATIONS; i++) {
			/* Remember value of previous iteration */
			old_re = new_re;
			old_im = new_im;
			/* Calculate the real and imaginary part */
			new_re = old_re * old_re - old_im * old_im + pr;
			new_im = 2 * old_re * old_im + pi;
			/* Stop if point is outside of circle with radius 2 */
			if (new_re * new_re + new_im * new_im > 4)
				break;
		}
		block->pix_iter_array[x - block->x_start] = i;
	}

	event_group = fractal_shm->egrp_imager;
	status = em_send_group(event, fractal_shm->queue_data_imager,
			       event_group);
	if (unlikely(status != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "em_send_group():%" PRI_STAT " Q:%" PRI_QUEUE "",
			      status, fractal_shm->queue_data_imager);
	}
}

/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
worker_start(my_eo_context_t *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_queue_t queue;
	em_status_t status;
	const char *queue_name;

	(void)conf;

	/* Copy EO name */
	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	queue_name = "queue Worker";

	queue = em_queue_create(queue_name, EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);

	test_fatal_if(queue == EM_QUEUE_UNDEF, "%s creation failed!",
		      queue_name);
	/* Store the queue handle */
	fractal_shm->queue_worker = queue;

	status = em_eo_add_queue_sync(eo, queue);

	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      status, eo, queue);

	APPL_PRINT("Started %s - EO:%" PRI_EO ". Q:%" PRI_QUEUE ".\n",
		   eo_ctx->name, eo, queue);

	return EM_OK;
}

/**
 * @private
 *
 * Imager EO receive function.
 *
 * Data queue: Writes pixel blocks to buffer in memory.
 * Notif queue: Image is now ready so writes buffer to file and sends a new
 * frame event to Pixel Handler EO.
 *
 */
static void
imager_receive_event(imager_eo_context_t *eo_ctx, em_event_t event,
		     em_event_type_t type, em_queue_t queue,
		     imager_queue_context_t *q_ctx)
{
	em_status_t status;

	(void)type;
	(void)queue;

	switch (q_ctx->id) {
	case DATA_QUEUE: {
		if (unlikely(appl_shm->exit_flag))
			return;

		imager_data_event_t *const block = em_event_pointer(event);
		const unsigned int y = block->y;
		const unsigned int x_start = block->x_start;
		const unsigned int x_len = block->x_len;
		buffer_t *const buf = &eo_ctx->buf[eo_ctx->buf_idx];
		unsigned int i;

		for (i = 0; i < x_len; i++) {
			/* 3 => x, x+1, x+2 */
			const unsigned int x = (x_start + i) * 3;
			const uint16_t pix_iter = block->pix_iter_array[i];
			/* RED */
			buf->line[y].col[x] = pix_iter % 256;
			/* GREEN */
			buf->line[y].col[x + 1] =
				(eo_ctx->frame * pix_iter) % 256;
			/* BLUE */
			buf->line[y].col[x + 2] =
				255 * (pix_iter < MAX_ITERATIONS);
		}
	}
		break;

		/* Notification: Current frame is ready */
	case NOTIF_QUEUE: {
		if (unlikely(appl_shm->exit_flag)) {
			em_free(event);
			return;
		}

		em_event_group_t event_group;

		/* store the image buffer index to write into file */
		const int buf_idx = eo_ctx->buf_idx;

		/* Reapply event group used by data queue and reuse event */
		em_notif_t notif_tbl[1];

		notif_tbl[0].event = event; /* == imager_notif */
		notif_tbl[0].queue = fractal_shm->queue_notif_imager;
		notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

		event_group = fractal_shm->egrp_imager;
		status = em_event_group_apply(event_group, EVENTS_PER_IMAGE,
					      1, notif_tbl);
		test_fatal_if(status != EM_OK,
			      "em_event_group_apply():%" PRI_STAT "",
			      status);

		/* Start new frame by sending event to Pixel handler */
		eo_ctx->frame++;
		em_event_t new_pixel_event = em_alloc(sizeof(fractal_event_t),
						      EM_EVENT_TYPE_SW,
						      fractal_shm->pool);
		test_fatal_if(new_pixel_event == EM_EVENT_UNDEF,
			      "Event allocation failed!");

		pixel_handler_event_t *const new_pixel =
				      em_event_pointer(new_pixel_event);
		new_pixel->frame = eo_ctx->frame;

		/* Modify image buffer index for the next round */
		eo_ctx->buf_idx = (eo_ctx->buf_idx + 1) % 2; /* 0,1,0,1,...*/

		status = em_send(new_pixel_event, fractal_shm->queue_pixel);
		if (unlikely(status != EM_OK)) {
			em_free(new_pixel_event);
			test_fatal_if(!appl_shm->exit_flag,
				      "em_send:%" PRI_STAT " Q:%" PRI_QUEUE "",
				      status, fractal_shm->queue_pixel);
		}

		/*
		 * After requesting a new frame, write the currently ready
		 * one from the buffer into the ramdisk file at
		 * 'TMP_IMAGE_PATH' and then rename it to 'IMAGE_PATH'
		 */
		FILE * fp = fopen(TMP_IMAGE_PATH, "wb+");

		test_fatal_if(fp == NULL, "Can't open output file %s",
			      TMP_IMAGE_PATH);

		/* Write image header */
		fprintf(fp, "P6\n%d %d\n255\n", WIDTH, HEIGHT);

		/* Write image buffer to file */
		fwrite(&eo_ctx->buf[buf_idx].line[0].col[0],
		       sizeof(char), WIDTH * HEIGHT * 3, fp);
		fclose(fp);

		/* Rename file to avoid tearing in the image */
		test_fatal_if(rename(TMP_IMAGE_PATH, IMAGE_PATH) != 0,
			      "Failed to rename file %s to %s.",
			      TMP_IMAGE_PATH, IMAGE_PATH);

		/* Print time taken for frame */
		struct timespec cur_time;

		clock_gettime(CLOCK_MONOTONIC, &cur_time);
		if (eo_ctx->next_print_time.tv_sec <= cur_time.tv_sec) {
			int fps = eo_ctx->frame - eo_ctx->prev_print_frame;

			fprintf(stdout,
				"Frames per second: %02d | frames %d - %d\n",
				fps, eo_ctx->prev_print_frame,
				eo_ctx->frame - 1);
			eo_ctx->prev_print_frame = eo_ctx->frame;
			eo_ctx->next_print_time.tv_sec = cur_time.tv_sec + 1;
		}

		/*
		 * Prints the time taken and ends if STOP_AFTER_FRAMES is
		 * greater than 0.
		 */
		if (START_FROM_FRAME + STOP_AFTER_FRAMES == eo_ctx->frame) {
			double ms = 0;

			ms += (cur_time.tv_sec - eo_ctx->start_time.tv_sec) *
			      1000;
			ms += (cur_time.tv_nsec - eo_ctx->start_time.tv_nsec) /
			      1000000;
			APPL_PRINT("%d frames in %.3f seconds.\n",
				   STOP_AFTER_FRAMES, ms / 1000);
			exit(EXIT_SUCCESS);
		}
	}
		break;

	default:
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Unknown queue id(%u)!", q_ctx->id);
		break;
	}
}

/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
imager_start(imager_eo_context_t *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_queue_t queue;
	em_status_t status;
	imager_queue_context_t *q_ctx;
	const char *queue_name;
	em_notif_t notif_tbl[1];
	em_event_group_t event_group;
	em_event_t event;

	(void)conf;

	/* Copy EO name */
	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	/* Create data queue */
	queue_name = "data queue imager";
	queue = em_queue_create(queue_name, EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);
	test_fatal_if(queue == EM_QUEUE_UNDEF, "%s creation failed!",
		      queue_name);

	status = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue);

	fractal_shm->queue_data_imager = queue;

	q_ctx = &fractal_shm->imager_data_queue_ctx;
	q_ctx->id = DATA_QUEUE;
	status = em_queue_set_context(queue, q_ctx);
	test_fatal_if(status != EM_OK,
		      "EO set queue context:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue);

	/* Create notification queue */
	queue_name = "notif queue imager";
	queue = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);
	test_fatal_if(queue == EM_QUEUE_UNDEF,
		      "%s creation failed!", queue_name);

	q_ctx = &fractal_shm->imager_notif_queue_ctx;
	q_ctx->id = NOTIF_QUEUE;
	status = em_queue_set_context(queue, q_ctx);
	test_fatal_if(status != EM_OK,
		      "Set queue context:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      status, eo, queue);

	status = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(status != EM_OK,
		      "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue);

	fractal_shm->queue_notif_imager = queue;

	APPL_PRINT("Started %s - EO:%" PRI_EO ". Q:%" PRI_QUEUE ".\n",
		   eo_ctx->name, eo, queue);

	/* Set event group notification */
	event = em_alloc(sizeof(fractal_event_t), EM_EVENT_TYPE_SW,
			 fractal_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF,
		      "Event allocation failed!");

	notif_tbl[0].event = event;
	notif_tbl[0].queue = fractal_shm->queue_notif_imager;
	notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

	event_group = fractal_shm->egrp_imager;
	status = em_event_group_apply(event_group, EVENTS_PER_IMAGE, 1,
				      notif_tbl);
	test_fatal_if(status != EM_OK,
		      "em_event_group_apply():%" PRI_STAT "", status);

	/* Init EO context */
	clock_gettime(CLOCK_MONOTONIC, &eo_ctx->start_time);
	eo_ctx->frame = START_FROM_FRAME;
	eo_ctx->next_print_time = eo_ctx->start_time;
	eo_ctx->next_print_time.tv_sec += 1;
	eo_ctx->prev_print_frame = eo_ctx->frame;

	/* Send the first event to pixel_handler to start */
	pixel_handler_event_t *pixel;

	event = em_alloc(sizeof(fractal_event_t), EM_EVENT_TYPE_SW,
			 fractal_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF,
		      "Event allocation failed!");

	pixel = em_event_pointer(event);
	pixel->frame  = eo_ctx->frame;

	status = em_send(event, fractal_shm->queue_pixel);
	test_fatal_if(status != EM_OK, "em_send():%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      status, eo, fractal_shm->queue_pixel);
	return EM_OK;
}
