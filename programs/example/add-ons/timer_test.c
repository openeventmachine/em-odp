/*
 *   Copyright (c) 2017, Nokia Solutions and Networks
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
 * Event Machine timer add-on basic test.
 *
 * Simple test for timer (does not test everything). Creates and deletes random
 * timers and checks how accurate the timeout indications are against timer
 * itself and also linux time (clock_gettime). Single EO, but receiving queue
 * is parallel so multiple threads can process timeouts concurrently.
 *
 * Exception/error management is simplified and aborts on any error.
 *
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#include <event_machine.h>
#include <event_machine/add-ons/event_machine_timer.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Test app defines.
 * Be careful, conflicting values may not be checked!
 */
#define APP_TIMER_RESOLUTION_US	1000 /* requested em-timer resolution */
#define APP_TIMEOUT_MAX_US	(15000ULL * 1000ULL) /* max random timeout */
#define APP_TIMEOUT_MIN_US	5000 /* minimum random timeout */
#define APP_MAX_TMOS		1000 /* simultaneous oneshots */
#define APP_MAX_PERIODIC	300  /* simultaneous periodic */
#define APP_PRINT_EACH_TMO	0 /* 0 to only print summary */
#define APP_PRINT_DOTS		1 /* visual progress dots */
#define APP_VISUAL_DEBUG	0 /* 0|1, for testing only. Slow, but visual */

#define APP_SHMEM_NAME		"TimerTestShMem"
#define APP_HEARTBEAT_MS	2000 /* heartbeat tick period */
#define APP_CHECK_COUNT		(APP_TIMEOUT_MAX_US / 1000 / APP_HEARTBEAT_MS)
#define APP_CHECK_LIMIT		(3 * (APP_CHECK_COUNT + 1)) /* num HB */
#define APP_CHECK_GUARD		3 /* num HB */

#define APP_CANCEL_MODULO_P	(APP_MAX_PERIODIC * 50) /* cancel propability*/
#define APP_CANCEL_MODULO	(APP_MAX_TMOS * 2) /* cancel propability */
#define APP_LINUX_CLOCK_SRC	CLOCK_MONOTONIC /* for clock_gettime */
#define APP_INCREASING_DLY	9 /* if not 0, add this to increasing
				   * delay before calling periodic timer ack
				   */
#define APP_INC_DLY_MODULO	15 /* apply increasing delay to every Nth tmo*/

#define COMPILER_BARRIER()    ({ __asm__ volatile("" ::: "memory"); (void)0; })

#if APP_VISUAL_DEBUG
#define VISUAL_DBG(x)		APPL_PRINT(x)
#else
#define VISUAL_DBG(x)		do {} while (0)
#endif

#define APP_EO_NAME		"Control EO"

/**
 * Example test application message event
 */
typedef enum app_cmd_t {
	APP_CMD_HEARTBEAT,
	APP_CMD_TMO_SINGLE,
	APP_CMD_TMO_PERIODIC
} app_cmd_t;

typedef struct app_msg_t {
	app_cmd_t command;
	int index;
	int dummy_delay;
} app_msg_t;

typedef struct app_tmo_data_t {
	em_tmo_t tmo ENV_CACHE_LINE_ALIGNED;
	em_event_t event;
	em_timer_tick_t	when;
	em_timer_tick_t	howmuch;
	em_timer_tick_t	appeared;
	struct timespec	linux_when;
	struct timespec	linux_appeared;
	int canceled;
	unsigned int max_dummy;
} app_tmo_data_t;

typedef enum app_test_state_t {
	APP_STATE_IDLE = 0,
	APP_STATE_RUNNING,
	APP_STATE_STOPPING,
	APP_STATE_CHECKING
} app_test_state_t;

const char *dot_marks = " .-#"; /* per state above */

/**
 * EO context
 */
typedef struct app_eo_ctx_t {
	em_tmo_t heartbeat_tmo;
	uint64_t heartbeat_count;
	uint64_t heartbeat_target;
	em_queue_t my_q;
	em_queue_t my_prio_q;
	uint64_t hz;
	uint64_t linux_hz;
	app_test_state_t state;
	uint64_t rounds;
	int nocancel;

	int64_t min_diff;
	int64_t max_diff;
	int64_t min_diff_l;
	int64_t max_diff_l;
	unsigned int errors;
	unsigned int ack_errors;
	unsigned int max_dummy;

	struct {
		app_tmo_data_t tmo[APP_MAX_TMOS];

		uint64_t received ENV_CACHE_LINE_ALIGNED;
		uint64_t cancelled;
		uint64_t cancel_fail;
	} oneshot;

	struct {
		app_tmo_data_t tmo[APP_MAX_PERIODIC];

		uint64_t received ENV_CACHE_LINE_ALIGNED;
		uint64_t cancelled;
		uint64_t cancel_fail;
	} periodic;
} app_eo_ctx_t;

/**
 * Timer test shared memory data
 */
typedef struct timer_app_shm_t {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO context data */
	app_eo_ctx_t eo_context;
	/* Event timer handle */
	em_timer_t tmr;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} timer_app_shm_t;

/* EM-thread locals */
static ENV_LOCAL timer_app_shm_t *m_shm;
static ENV_LOCAL unsigned int m_randseed;

/* Local function prototypes */
static em_status_t app_eo_start(app_eo_ctx_t *eo_ctx, em_eo_t eo,
				const em_eo_conf_t *conf);
static em_status_t app_eo_start_local(app_eo_ctx_t *eo_ctx, em_eo_t eo);
static em_status_t app_eo_stop(app_eo_ctx_t *eo_ctx, em_eo_t eo);
static void app_eo_receive(app_eo_ctx_t *eo_ctx, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_ctx);

static em_timer_tick_t rand_timeout(unsigned int *seed, app_eo_ctx_t *eo_ctx,
				    unsigned int fixed);
static int set_timeouts(app_eo_ctx_t *eo_ctx);
static void start_test(app_eo_ctx_t *eo_ctx);
static void check_test(app_eo_ctx_t *eo_ctx);
static void stop_test(app_eo_ctx_t *eo_ctx);
static void cleanup_test(app_eo_ctx_t *eo_ctx);
static int64_t ts_diff_ns(struct timespec *ts1, struct timespec *ts2);
static int64_t tick_diff_ns(em_timer_tick_t t1, em_timer_tick_t t2,
			    uint64_t hz);
static void random_cancel(app_eo_ctx_t *eo_ctx);
static em_event_t random_cancel_periodic(app_eo_ctx_t *eo_ctx);
static unsigned int check_single(app_eo_ctx_t *eo_ctx);
static unsigned int check_periodic(app_eo_ctx_t *eo_ctx);
static void dummy_processing(unsigned int us);
static int handle_periodic_event(app_eo_ctx_t *eo_ctx, em_event_t event,
				 app_msg_t *msgin);
static void handle_single_event(app_eo_ctx_t *eo_ctx, em_event_t event,
				app_msg_t *msgin);
static void handle_heartbeat(app_eo_ctx_t *eo_ctx, em_queue_t queue);

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
 * Local EO error handler. Prevents error when ack() is done after cancel()
 * since it's normal here.
 *
 * @param eo            Execution object id
 * @param error         The error code
 * @param escope        Error scope
 * @param args          List of arguments (__FILE__, __func__, __LINE__,
 *                                         (format), ## __VA_ARGS__)
 *
 * @return The original error code.
 */
static em_status_t eo_error_handler(em_eo_t eo, em_status_t error,
				    em_escope_t escope, va_list args)
{
	VISUAL_DBG("E");

	/* BAD_STATE in ack() means timer was canceled */
	if (error == EM_ERR_BAD_STATE && escope == EM_ESCOPE_TMO_ACK)
		APPL_PRINT("\n%s: ack() with cancelled tmo (ok)\n", __func__);
	else
		error = test_error_handler(eo, error, escope, args);

	return error;
}

/**
 * Before EM - Init of the test application.
 *
 * The shared memory is needed if EM instance runs on multiple processes.
 * Doing it like this makes it possible to run the app both as threads (-t)
 * as well as processes (-p).
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_init(void)
{
	int core = em_core_id();

	/* first core creates ShMem */
	if (core == 0) {
		m_shm = env_shared_reserve(APP_SHMEM_NAME,
					   sizeof(timer_app_shm_t));
		/* initialize it */
		if (m_shm)
			memset(m_shm, 0, sizeof(timer_app_shm_t));

		em_register_error_handler(test_error_handler);
		APPL_PRINT("%ldk shared memory for app context\n",
			   sizeof(timer_app_shm_t) / 1000);

	} else {
		m_shm = env_shared_lookup(APP_SHMEM_NAME);
	}

	if (m_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "ShMem init failed on EM-core: %u",
			   em_core_id());
	}

	APPL_PRINT("core %d: %s done\n", core, __func__);
}

/**
 * Startup of the timer hello EM application.
 *
 * At this point EM is up, but no EOs exist. EM API can be used to create
 * queues, EOs etc.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	em_timer_attr_t attr;
	em_queue_t queue;
	em_status_t stat;
	app_eo_ctx_t *eo_ctx;
	em_event_t event;
	app_msg_t *msg;
	struct timespec ts;
	uint64_t period;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		m_shm->pool = appl_conf->pools[0];
	else
		m_shm->pool = EM_POOL_DEFAULT;

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
		   m_shm->pool);

	test_fatal_if(m_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/* Create EO */
	eo = em_eo_create(APP_EO_NAME,
			  (em_start_func_t)app_eo_start,
			  (em_start_local_func_t)app_eo_start_local,
			  (em_stop_func_t)app_eo_stop, NULL,
			  (em_receive_func_t)app_eo_receive,
			  &m_shm->eo_context);
	test_fatal_if(eo == EM_EO_UNDEF, "Failed to create EO!");
	eo_ctx = &m_shm->eo_context;

	/* atomic queue for control */
	queue = em_queue_create("Control Q",
				EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create queue!");

	m_shm->eo_context.my_q = queue;
	/* another parallel high priority for timeout handling*/
	queue = em_queue_create("Tmo Q",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_HIGHEST,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create queue!");

	m_shm->eo_context.my_prio_q = queue;

	stat = em_eo_register_error_handler(eo, eo_error_handler);
	test_fatal_if(stat != EM_OK, "Failed to register EO error handler");

	/* create shared timer and store handle in
	 * shared memory. Require the configured app values
	 */
	memset(&attr, 0, sizeof(em_timer_attr_t));
	strncpy(attr.name, "TestTimer", EM_TIMER_NAME_LEN);
	attr.num_tmo = MAX(APP_MAX_TMOS + APP_MAX_PERIODIC + 1,
			   em_core_count() * 512 + 1024); /* core stashing */
	attr.resolution = APP_TIMER_RESOLUTION_US * 1000ULL;
	m_shm->tmr = em_timer_create(&attr);
	test_fatal_if(m_shm->tmr == EM_TIMER_UNDEF, "Failed to create timer!");

	/* Start EO */
	stat = em_eo_start_sync(eo, NULL, NULL);
	test_fatal_if(stat != EM_OK, "Failed to start EO!");

	/* create periodic timer for heartbeat */
	eo_ctx->heartbeat_tmo = em_tmo_create(m_shm->tmr, EM_TMO_FLAG_PERIODIC,
					      eo_ctx->my_q);
	test_fatal_if(eo_ctx->heartbeat_tmo == EM_TMO_UNDEF,
		      "Can't allocate heartbeat_tmo!\n");

	event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, m_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate event (%ldB)!\n",
		      sizeof(app_msg_t));

	msg = em_event_pointer(event);
	msg->command = APP_CMD_HEARTBEAT;
	eo_ctx->hz = em_timer_get_freq(m_shm->tmr);
	if (eo_ctx->hz < 100)
		APPL_ERROR("WARNING - timer hz very low!\n");

	/* linux time check */
	test_fatal_if(clock_getres(APP_LINUX_CLOCK_SRC, &ts) != 0,
		      "clock_getres() failed!\n");

	period = ts.tv_nsec + (ts.tv_sec * 1000000000ULL);
	eo_ctx->linux_hz = 1000000000ULL / period;
	APPL_PRINT("Linux reports clock running at %" PRIu64 " hz\n",
		   eo_ctx->linux_hz);

	/* start heartbeat */
	period = (APP_HEARTBEAT_MS * eo_ctx->hz) / 1000;
	test_fatal_if(period < 1, "timer resolution is too low!\n");

	stat = em_tmo_set_rel(eo_ctx->heartbeat_tmo, period, event);
	test_fatal_if(stat != EM_OK, "Can't activate heartbeat tmo!\n");

	APPL_PRINT("%s done, test repetition interval %ds\n\n", __func__,
		   (int)((APP_HEARTBEAT_MS * APP_CHECK_LIMIT) / 1000));
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_status_t ret;
	em_eo_t eo;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	eo = em_eo_find(APP_EO_NAME);
	test_fatal_if(eo == EM_EO_UNDEF,
		      "Could not find EO:%s", APP_EO_NAME);

	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);

	ret = em_timer_delete(m_shm->tmr);
	test_fatal_if(ret != EM_OK,
		      "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      m_shm->tmr, ret);
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (m_shm != NULL) {
		env_shared_free(m_shm);
		m_shm = NULL;
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t app_eo_start(app_eo_ctx_t *eo_ctx, em_eo_t eo,
				const em_eo_conf_t *conf)
{
	em_timer_attr_t attr;
	em_timer_t tmr;
	int num_timers;

	(void)eo;
	(void)conf;

	APPL_PRINT("EO start\n");

	num_timers = em_timer_get_all(&tmr, 1);
	APPL_PRINT("System has %d timer(s)\n", num_timers);

	if (em_timer_get_attr(m_shm->tmr, &attr) != EM_OK) {
		APPL_ERROR("Can't get timer info\n");
		return EM_ERR_BAD_ID;
	}
	APPL_PRINT("Timer \"%s\" info:\n", attr.name);
	APPL_PRINT("  -resolution: %" PRIu64 " ns\n", attr.resolution);
	APPL_PRINT("  -max_tmo: %" PRIu64 " ms\n", attr.max_tmo / 1000);
	APPL_PRINT("  -num_tmo: %d\n", attr.num_tmo);
	APPL_PRINT("  -tick Hz: %" PRIu64 " hz\n",
		   em_timer_get_freq(m_shm->tmr));

	if (APP_INCREASING_DLY) {
		APPL_PRINT("Using increasing processing delay (%d, 1/%d)\n",
			   APP_INCREASING_DLY, APP_INC_DLY_MODULO);
	}

	/* init other local EO context */
	eo_ctx->min_diff = INT64_MAX;
	eo_ctx->max_diff = 0;
	eo_ctx->min_diff_l = INT64_MAX;
	eo_ctx->max_diff_l = 0;

	return EM_OK;
}

/**
 * @private
 *
 * EO per thread start function.
 *
 */
static em_status_t app_eo_start_local(app_eo_ctx_t *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;
	(void)eo;

	APPL_PRINT("EO local start\n");

	/* per-thread random seed */
	m_randseed = time(NULL);

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t app_eo_stop(app_eo_ctx_t *eo_ctx, em_eo_t eo)
{
	em_event_t event = EM_EVENT_UNDEF;
	em_status_t ret;
	int i;

	APPL_PRINT("EO stop\n");

	if (eo_ctx->heartbeat_tmo != EM_TMO_UNDEF) {
		em_tmo_delete(eo_ctx->heartbeat_tmo, &event);
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	}

	for (i = 0; i < APP_MAX_TMOS; i++) {
		if (eo_ctx->oneshot.tmo[i].tmo != EM_TMO_UNDEF) {
			em_tmo_delete(eo_ctx->oneshot.tmo[i].tmo, &event);
			if (event != EM_EVENT_UNDEF)
				em_free(event);
		}
	}

	for (i = 0; i < APP_MAX_PERIODIC; i++) {
		if (eo_ctx->periodic.tmo[i].tmo != EM_TMO_UNDEF) {
			em_tmo_delete(eo_ctx->periodic.tmo[i].tmo, &event);
			if (event != EM_EVENT_UNDEF)
				em_free(event);
		}
	}

	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);
	return EM_OK;
}

/**
 * @private
 *
 * EO receive function. Runs the example test app after initialization.
 *
 */
static void app_eo_receive(app_eo_ctx_t *eo_ctx, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_ctx)
{
	int reuse = 0;

	(void)q_ctx;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	VISUAL_DBG("e");

	if (type == EM_EVENT_TYPE_SW) {
		app_msg_t *msgin = (app_msg_t *)em_event_pointer(event);

		switch (msgin->command) {
		case APP_CMD_HEARTBEAT: /* uses atomic queue */
			VISUAL_DBG("H");
			handle_heartbeat(eo_ctx, queue);
			if (em_tmo_ack(eo_ctx->heartbeat_tmo, event) != EM_OK)
				test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
					   "Heartbeat ack() failed!\n");
			reuse = 1;
			break;

		case APP_CMD_TMO_SINGLE: /* parallel queue */
			VISUAL_DBG("s");
			if (queue != eo_ctx->my_prio_q)
				test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
					   "tmo from wrong queue!\n");

			handle_single_event(eo_ctx, event, msgin);
			break;

		case APP_CMD_TMO_PERIODIC: /* parallel queue */
			VISUAL_DBG("p");
			if (queue != eo_ctx->my_prio_q)
				test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
					   "tmo from wrong queue!\n");
			reuse = handle_periodic_event(eo_ctx, event, msgin);
			break;

		default:
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Invalid event!\n");
		}
	} else {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Invalid event type!\n");
	}

	if (!reuse)
		em_free(event);
}

void handle_single_event(app_eo_ctx_t *eo_ctx, em_event_t event,
			 app_msg_t *msgin)
{
	(void)event;

	if (eo_ctx->state != APP_STATE_RUNNING) {
		APPL_PRINT("Tmo received after test finish\n");
		__atomic_fetch_add(&eo_ctx->errors, 1,
				   __ATOMIC_RELAXED);
	}
	eo_ctx->oneshot.tmo[msgin->index].appeared =
		em_timer_current_tick(m_shm->tmr);
	clock_gettime(APP_LINUX_CLOCK_SRC,
		      &eo_ctx->oneshot.tmo[msgin->index].linux_appeared);
	__atomic_fetch_add(&eo_ctx->oneshot.received, 1,
			   __ATOMIC_RELAXED);

	if (!eo_ctx->nocancel)
		random_cancel(eo_ctx);
}

int handle_periodic_event(app_eo_ctx_t *eo_ctx, em_event_t event,
			  app_msg_t *msgin)
{
	int reuse = 0;

	if (eo_ctx->state != APP_STATE_RUNNING &&
	    eo_ctx->state != APP_STATE_STOPPING) {
		APPL_PRINT("Periodic tmo received after test finish\n");
		__atomic_fetch_add(&eo_ctx->errors, 1,
				   __ATOMIC_RELAXED);
	} else {
		eo_ctx->periodic.tmo[msgin->index].appeared =
			em_timer_current_tick(m_shm->tmr);
		__atomic_fetch_add(&eo_ctx->periodic.received, 1,
				   __ATOMIC_RELAXED);

		if (eo_ctx->state == APP_STATE_STOPPING)
			return 0; /* ignore when STOPPING */

		reuse = 1;

		if (APP_INCREASING_DLY && msgin->dummy_delay) {
			/* add delay before ack() */
			/* printf("%d-%d", msgin->index, msgin->dummy_delay); */
			dummy_processing(msgin->dummy_delay);
			msgin->dummy_delay += APP_INCREASING_DLY;
			eo_ctx->periodic.tmo[msgin->index].max_dummy =
				msgin->dummy_delay;
		}
		em_status_t ret =
		em_tmo_ack(eo_ctx->periodic.tmo[msgin->index].tmo,
			   event);
		if (ret != EM_OK) {
			APPL_PRINT("em_tmo_ack error:%" PRI_STAT "\n", ret);
			__atomic_fetch_add(&eo_ctx->ack_errors, 1,
					   __ATOMIC_RELAXED);
			reuse = 0;
		}

		if (!eo_ctx->nocancel) {
			if (random_cancel_periodic(eo_ctx) == event)
				reuse = 1;
		}
	}
	return reuse;
}

/* handle beartbeat, i.e. run state machine */
void handle_heartbeat(app_eo_ctx_t *eo_ctx, em_queue_t queue)
{
	if (queue != eo_ctx->my_q) {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "heartbeat from wrong queue!\n");
	}

	eo_ctx->heartbeat_count++;

	if (APP_PRINT_DOTS) {
		char ch = dot_marks[eo_ctx->state];

		if (ch != ' ')
			APPL_PRINT("%c", ch);
	}

	/* reached next state change */
	if (eo_ctx->heartbeat_count >= eo_ctx->heartbeat_target) {
		switch (eo_ctx->state) {
		case APP_STATE_IDLE:
			start_test(eo_ctx);
			eo_ctx->heartbeat_target = eo_ctx->heartbeat_count +
						APP_CHECK_LIMIT;
			break;
		case APP_STATE_RUNNING:
			stop_test(eo_ctx);
			eo_ctx->heartbeat_target = eo_ctx->heartbeat_count +
						APP_CHECK_GUARD;
			break;
		case APP_STATE_STOPPING:
			check_test(eo_ctx);
			eo_ctx->heartbeat_target = eo_ctx->heartbeat_count +
						APP_CHECK_GUARD;
			break;
		case APP_STATE_CHECKING:
			cleanup_test(eo_ctx);
			eo_ctx->heartbeat_target = eo_ctx->heartbeat_count +
						APP_CHECK_GUARD;
			break;
		default:
			break;
		}
	}
}

/* new random timeout APP_TIMEOUT_MIN_US ... APP_TIMEOUT_MAX_US in ticks */
em_timer_tick_t rand_timeout(unsigned int *seed, app_eo_ctx_t *eo_ctx,
			     unsigned int fixed)
{
	uint64_t us;
	double tick_ns = 1000000000.0 / (double)eo_ctx->hz;

	if (fixed) {
		us = fixed;
	} else {
		do {
			us = (uint64_t)rand_r(seed);
			us %= APP_TIMEOUT_MAX_US;
		} while (us < APP_TIMEOUT_MIN_US);
	}

	return (em_timer_tick_t)((double)us * 1000.0 / tick_ns);
}

/* start new batch of random timeouts */
int set_timeouts(app_eo_ctx_t *eo_ctx)
{
	app_msg_t *msg;
	int i;
	uint64_t t1, t2;
	struct timespec ts1, ts2;

	/* timeouts allocate new events every time (could re-use old ones).
	 * Do this first so we can time just the tmo creation
	 */
	for (i = 0; i < APP_MAX_TMOS; i++) {
		em_event_t event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW,
					    m_shm->pool);
		if (event == EM_EVENT_UNDEF)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't allocate event nr %d!", i + 1);

		/* prepare as timeout */
		msg = em_event_pointer(event);
		msg->command = APP_CMD_TMO_SINGLE;
		msg->index = i;
		msg->dummy_delay = 0;
		memset(&eo_ctx->oneshot.tmo[i], 0, sizeof(app_tmo_data_t));
		eo_ctx->oneshot.tmo[i].event = event;
	}
	/* barriers to make sure we're timing the right code. Some may be
	 * unnecessary if the time functions not get inlined
	 */
	COMPILER_BARRIER();
	t1 = em_timer_current_tick(m_shm->tmr);
	clock_gettime(APP_LINUX_CLOCK_SRC, &ts1);
	COMPILER_BARRIER();
	/* allocate new tmos every time (could re-use) */
	for (i = 0; i < APP_MAX_TMOS; i++) {
		em_tmo_t tmo = em_tmo_create(m_shm->tmr, EM_TMO_FLAG_ONESHOT,
					     eo_ctx->my_prio_q);

		if (unlikely(tmo == EM_TMO_UNDEF))
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't allocate tmo nr %d!", i);

		eo_ctx->oneshot.tmo[i].tmo = tmo;
	}
	COMPILER_BARRIER();
	t2 = em_timer_current_tick(m_shm->tmr);
	clock_gettime(APP_LINUX_CLOCK_SRC, &ts2);
	APPL_PRINT("Timer: Creating %d timeouts took %" PRIu64 " ns (%" PRIu64
		   " ns each)\n", i,
		   tick_diff_ns(t1, t2, eo_ctx->hz),
		   tick_diff_ns(t1, t2, eo_ctx->hz) / APP_MAX_TMOS);
	APPL_PRINT("Linux: Creating %d timeouts took %" PRIu64 " ns (%" PRIu64
		   " ns each)\n", i, ts_diff_ns(&ts1, &ts2),
		   ts_diff_ns(&ts1, &ts2) / APP_MAX_TMOS);

	/* start them all. Some might be served before this loop ends! */
	for (i = 0; i < APP_MAX_TMOS; i++) {
		unsigned int fixed = 0;

		/* always test min and max tmo */
		if (i == 0)
			fixed = APP_TIMEOUT_MAX_US;
		else if (i == 1)
			fixed = APP_TIMEOUT_MIN_US;

		eo_ctx->oneshot.tmo[i].howmuch = rand_timeout(&m_randseed,
							      eo_ctx, fixed);
		eo_ctx->oneshot.tmo[i].when = em_timer_current_tick(m_shm->tmr);
		clock_gettime(APP_LINUX_CLOCK_SRC,
			      &eo_ctx->oneshot.tmo[i].linux_when);
		COMPILER_BARRIER();
		if (em_tmo_set_rel(eo_ctx->oneshot.tmo[i].tmo,
				   eo_ctx->oneshot.tmo[i].howmuch,
				   eo_ctx->oneshot.tmo[i].event) != EM_OK)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't activate tmo!\n");
	}
	if (APP_MAX_TMOS)
		APPL_PRINT("Started single shots\n");

	for (i = 0; i < APP_MAX_PERIODIC; i++) {
		unsigned int fixed = 0;

		em_event_t event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW,
					    m_shm->pool);
		if (event == EM_EVENT_UNDEF)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't allocate event!");

		msg = em_event_pointer(event);
		msg->command = APP_CMD_TMO_PERIODIC;
		msg->index = i;
		msg->dummy_delay = (i % APP_INC_DLY_MODULO) ?
				    0 : APP_INCREASING_DLY;
		memset(&eo_ctx->periodic.tmo[i], 0, sizeof(app_tmo_data_t));
		eo_ctx->periodic.tmo[i].event = event;

		em_tmo_t tmo = em_tmo_create(m_shm->tmr, EM_TMO_FLAG_PERIODIC,
					     eo_ctx->my_prio_q);
		if (unlikely(tmo == EM_TMO_UNDEF))
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't allocate periodic tmo nr %d!", i + 1);
		eo_ctx->periodic.tmo[i].tmo = tmo;

		/* always test min and max tmo */
		if (i == 0)
			fixed = APP_TIMEOUT_MAX_US;
		else if (i == 1)
			fixed = APP_TIMEOUT_MIN_US;
		eo_ctx->periodic.tmo[i].howmuch = rand_timeout(&m_randseed,
							       eo_ctx, fixed);
		eo_ctx->periodic.tmo[i].when =
				em_timer_current_tick(m_shm->tmr);
		if (em_tmo_set_rel(eo_ctx->periodic.tmo[i].tmo,
				   eo_ctx->periodic.tmo[i].howmuch,
				   eo_ctx->periodic.tmo[i].event) != EM_OK)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't activate periodic tmo nr %d!\n",
				   i + 1);
	}

	if (APP_MAX_PERIODIC)
		APPL_PRINT("Started periodic\n");

	return 0;
}

void start_test(app_eo_ctx_t *eo_ctx)
{
	eo_ctx->oneshot.received = 0;
	eo_ctx->oneshot.cancelled = 0;
	eo_ctx->oneshot.cancel_fail = 0;

	eo_ctx->periodic.received = 0;
	eo_ctx->periodic.cancelled = 0;
	eo_ctx->periodic.cancel_fail = 0;

	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char s[40];

	strftime(s, sizeof(s), "%b-%d %H:%M:%S", tm);
	eo_ctx->rounds++;
	APPL_PRINT("\n\n%s ROUND %" PRIu64 " ************\n",
		   s, eo_ctx->rounds);

	eo_ctx->nocancel = 1;
	eo_ctx->state = APP_STATE_RUNNING; /* do this _before_ starting tmo */
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	set_timeouts(eo_ctx);	/* timeouts start coming */
	APPL_PRINT("Running\n");
	eo_ctx->nocancel = 0;	/* after all timeouts are completely created */
}

void stop_test(app_eo_ctx_t *eo_ctx)
{
	em_event_t event;

	/* cancel periodic */
	for (int i = 0; i < APP_MAX_PERIODIC; i++) {
		if (!eo_ctx->periodic.tmo[i].canceled) {
			em_tmo_cancel(eo_ctx->periodic.tmo[i].tmo, &event);
			if (event != EM_EVENT_UNDEF)
				em_free(event);
		}
	}
	eo_ctx->state = APP_STATE_STOPPING;
}

void cleanup_test(app_eo_ctx_t *eo_ctx)
{
	int i;
	uint64_t t1, t2;
	struct timespec ts1, ts2;

	APPL_PRINT("\nCleaning up\n");

	t1 = em_timer_current_tick(m_shm->tmr);
	clock_gettime(APP_LINUX_CLOCK_SRC, &ts1);
	COMPILER_BARRIER();
	for (i = 0; i < APP_MAX_TMOS; i++) {
		em_event_t evt = EM_EVENT_UNDEF;

		if (em_tmo_delete(eo_ctx->oneshot.tmo[i].tmo, &evt) != EM_OK)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't delete tmo!\n");

		if (evt != EM_EVENT_UNDEF) {
			APPL_PRINT("WARN - tmo_delete returned event,\n"
				   "       should be received or canceled!\n");
			em_free(evt);
		}
	}
	COMPILER_BARRIER();
	t2 = em_timer_current_tick(m_shm->tmr);
	clock_gettime(APP_LINUX_CLOCK_SRC, &ts2);
	APPL_PRINT("Timer: Deleting %d timeouts took %" PRIu64
		   " ns (%" PRIu64 " ns each)\n", i,
		   tick_diff_ns(t1, t2, eo_ctx->hz),
		   tick_diff_ns(t1, t2, eo_ctx->hz) / APP_MAX_TMOS);
	APPL_PRINT("Linux: Deleting %d timeouts took %" PRIu64 " ns (%" PRIu64
		   " ns each)\n", i, ts_diff_ns(&ts1, &ts2),
		   ts_diff_ns(&ts1, &ts2) / APP_MAX_TMOS);

	for (i = 0; i < APP_MAX_PERIODIC; i++) {
		em_event_t evt = EM_EVENT_UNDEF;

		if (em_tmo_delete(eo_ctx->periodic.tmo[i].tmo, &evt) != EM_OK)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Can't delete periodic tmo!\n");

		if (evt != EM_EVENT_UNDEF)
			em_free(evt);
	}
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	eo_ctx->state = APP_STATE_IDLE;
}

void check_test(app_eo_ctx_t *eo_ctx)
{
	unsigned int errors;

	eo_ctx->state = APP_STATE_CHECKING;
	eo_ctx->nocancel = 1;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	APPL_PRINT("\nHeartbeat count %" PRIu64 "\n", eo_ctx->heartbeat_count);

	errors = check_single(eo_ctx);
	errors += check_periodic(eo_ctx);
	eo_ctx->errors += errors;
	APPL_PRINT("Errors: %u\n\n", errors);

	APPL_PRINT("TOTAL RUNTIME/US: min %" PRIi64 ", max %" PRIi64 "\n",
		   tick_diff_ns(0, eo_ctx->min_diff, eo_ctx->hz) / 1000,
		   tick_diff_ns(0, eo_ctx->max_diff, eo_ctx->hz) / 1000);
	APPL_PRINT("TOTAL RUNTIME LINUX/US: min %" PRIi64 ", max %" PRIi64 "\n",
		   eo_ctx->min_diff_l / 1000, eo_ctx->max_diff_l / 1000);
	APPL_PRINT("TOTAL ERRORS: %u\n", eo_ctx->errors);
	APPL_PRINT("TOTAL ACK FAILS: %u\n", eo_ctx->ack_errors);
	if (APP_INCREASING_DLY)
		APPL_PRINT("TOTAL MAX DUMMY PROCESSING/US: %u\n",
			   eo_ctx->max_dummy);
}

/* timespec diff to ns */
int64_t ts_diff_ns(struct timespec *ts1, struct timespec *ts2)
{
	uint64_t t1 = ts1->tv_nsec + (ts1->tv_sec * 1000000000ULL);
	uint64_t t2 = ts2->tv_nsec + (ts2->tv_sec * 1000000000ULL);

	return (t2 - t1);
}

/* timer tick diff to ns */
int64_t tick_diff_ns(em_timer_tick_t t1, em_timer_tick_t t2, uint64_t hz)
{
	uint64_t ticks = (uint64_t)t2 - (uint64_t)t1;

	double tick_ns = 1000000000.0 / (double)hz;

	return (int64_t)((double)ticks * tick_ns);
}

void random_cancel(app_eo_ctx_t *eo_ctx)
{
	unsigned int idx = (unsigned int)rand_r(&m_randseed) %
			   APP_CANCEL_MODULO ? APP_CANCEL_MODULO : 1;

	if (idx >= APP_MAX_TMOS || idx == 0)
		return;

	/* no full synchronization, so double cancel is possible. It will
	 * cause 2nd one to fail
	 */
	if (!eo_ctx->oneshot.tmo[idx].canceled &&
	    eo_ctx->oneshot.tmo[idx].tmo != EM_TMO_UNDEF) {
		/* try to cancel (Tmo might have been fired already) */
		em_event_t evt = EM_EVENT_UNDEF;

		if (em_tmo_cancel(eo_ctx->oneshot.tmo[idx].tmo, &evt) ==
				  EM_OK) {
			eo_ctx->oneshot.tmo[idx].canceled = 1;
			__atomic_fetch_add(&eo_ctx->oneshot.cancelled, 1,
					   __ATOMIC_RELAXED);
		} else {
			__atomic_fetch_add(&eo_ctx->oneshot.cancel_fail, 1,
					   __ATOMIC_RELAXED);
		}

		if (evt != EM_EVENT_UNDEF) /* cancelled in time, free event */
			em_free(evt);

		VISUAL_DBG("c");
	}
}

em_event_t random_cancel_periodic(app_eo_ctx_t *eo_ctx)
{
	unsigned int idx = ((unsigned int)rand_r(&m_randseed)) %
			   (APP_CANCEL_MODULO_P ? APP_CANCEL_MODULO_P : 1);

	if (idx >= APP_MAX_PERIODIC || idx == 0)
		return EM_EVENT_UNDEF;

	if (!eo_ctx->periodic.tmo[idx].canceled &&
	    eo_ctx->periodic.tmo[idx].tmo != EM_TMO_UNDEF) {
		/* try to cancel (Tmo might have been fired already) */
		em_event_t evt = EM_EVENT_UNDEF;

		if (em_tmo_cancel(eo_ctx->periodic.tmo[idx].tmo, &evt) ==
				  EM_OK) {
			eo_ctx->periodic.tmo[idx].canceled = 1;
			eo_ctx->periodic.tmo[idx].appeared = 0; /* for chk */
			__atomic_fetch_add(&eo_ctx->periodic.cancelled, 1,
					   __ATOMIC_RELAXED);
		} else {
			__atomic_fetch_add(&eo_ctx->periodic.cancel_fail, 1,
					   __ATOMIC_RELAXED);
		}

		if (evt != EM_EVENT_UNDEF) {
			em_free(evt);
			return evt; /* for check only */
		}
		VISUAL_DBG("C");
	}

	return EM_EVENT_UNDEF;
}

unsigned int check_single(app_eo_ctx_t *eo_ctx)
{
	int i;
	unsigned int errors = 0;
	int64_t min_diff = INT64_MAX;
	int64_t max_diff = 0;
	int64_t avg_diff = 0;
	int64_t min_linux = INT64_MAX;
	int64_t max_linux = 0;
	int64_t avg_linux = 0;
	struct timespec zerot;

	memset(&zerot, 0, sizeof(zerot)); /* 0 to use diff*/
	APPL_PRINT("ONESHOT:\n");
	APPL_PRINT(" Received: %" PRIu64 ", expected %lu\n",
		   eo_ctx->oneshot.received,
		   APP_MAX_TMOS - eo_ctx->oneshot.cancelled);
	APPL_PRINT(" Cancelled OK: %" PRIu64 "\n", eo_ctx->oneshot.cancelled);
	APPL_PRINT(" Cancel failed (too late): %" PRIu64 "\n",
		   eo_ctx->oneshot.cancel_fail);

	for (i = 0; i < APP_MAX_TMOS; i++) {
		/* missing any? */
		if (!eo_ctx->oneshot.tmo[i].canceled &&
		    !eo_ctx->oneshot.tmo[i].appeared) {
			APPL_PRINT(" ERR: TMO %d event missing!\n", i);
			APPL_PRINT("    - to %lu ticks\n",
				   eo_ctx->oneshot.tmo[i].howmuch);
			errors++;
		}

		/* calculate timing */
		if (eo_ctx->oneshot.tmo[i].appeared) {
			/* timer ticks */
			uint64_t target = eo_ctx->oneshot.tmo[i].when +
					eo_ctx->oneshot.tmo[i].howmuch;
			int64_t diff = (int64_t)eo_ctx->oneshot.tmo[i].appeared
					- (int64_t)target;
			if (APP_PRINT_EACH_TMO)
				APPL_PRINT("Timeout #%u: diff %" PRIi64
						" ticks\n", i + 1, diff);
			if (min_diff > diff)
				min_diff = diff;
			if (max_diff < diff)
				max_diff = diff;
			avg_diff += diff;

			/* linux time in ns*/
			int64_t ldiff;

			ldiff = tick_diff_ns(0,
					     eo_ctx->oneshot.tmo[i].howmuch,
					     eo_ctx->hz);

			target = ts_diff_ns(&zerot,
					    &eo_ctx->oneshot.tmo[i].linux_when)
					     + ldiff;
			diff = (int64_t)ts_diff_ns(&zerot,
					&eo_ctx->oneshot.tmo[i].linux_appeared)
					- (int64_t)target;
			if (APP_PRINT_EACH_TMO)
				APPL_PRINT("Timeout #%d: diff %" PRIi64
						" linux ns\n", i + 1, diff);
			if (min_linux > diff)
				min_linux = diff;
			if (max_linux < diff)
				max_linux = diff;
			avg_linux += diff;
		}

		if (eo_ctx->oneshot.tmo[i].canceled &&
		    eo_ctx->oneshot.tmo[i].appeared) {
			APPL_PRINT(" ERR: TMO %d cancel ok but event appeared!\n",
				   i);
			errors++;
		}
	}

	avg_diff /= (int64_t)eo_ctx->oneshot.received;
	avg_linux /= (int64_t)eo_ctx->oneshot.received;
	APPL_PRINT(" SUMMARY/TICKS: min %" PRIi64 ", max %" PRIi64
			", avg %" PRIi64 "\n", min_diff, max_diff,
			avg_diff);
	APPL_PRINT("        /US: min %" PRIi64 ", max %" PRIi64
			", avg %" PRIi64 "\n",
			tick_diff_ns(0, min_diff, eo_ctx->hz) / 1000,
			tick_diff_ns(0, max_diff, eo_ctx->hz) / 1000,
			tick_diff_ns(0, avg_diff, eo_ctx->hz) / 1000);
	APPL_PRINT(" SUMMARY/LINUX US: min %" PRIi64 ", max %" PRIi64
		", avg %" PRIi64 "\n", min_linux / 1000, max_linux / 1000,
		avg_linux / 1000);

	/* over total runtime */
	if (eo_ctx->min_diff > min_diff)
		eo_ctx->min_diff = min_diff;
	if (eo_ctx->max_diff < max_diff)
		eo_ctx->max_diff = max_diff;
	if (eo_ctx->min_diff_l > min_linux)
		eo_ctx->min_diff_l = min_linux;
	if (eo_ctx->max_diff_l < max_linux)
		eo_ctx->max_diff_l = max_linux;

	return errors;
}

unsigned int check_periodic(app_eo_ctx_t *eo_ctx)
{
	int i;
	unsigned int errors = 0;
	unsigned int max_dummy = 0;

	APPL_PRINT("PERIODIC:\n");
	APPL_PRINT(" Received: %" PRIu64 "\n", eo_ctx->periodic.received);
	APPL_PRINT(" Cancelled: %" PRIu64 "\n", eo_ctx->periodic.cancelled);
	APPL_PRINT(" Cancel failed (too late): %" PRIu64 "\n",
		   eo_ctx->periodic.cancel_fail);

	for (i = 0; i < APP_MAX_PERIODIC; i++) {
		/* missing? */
		if (!eo_ctx->periodic.tmo[i].canceled &&
		    !eo_ctx->periodic.tmo[i].appeared) {
			APPL_PRINT(" ERR: No periodic TMO %d event(s)!\n", i);
			errors++;
		}
		/* appeared after successful cancel? */
		if (eo_ctx->periodic.tmo[i].canceled &&
		    eo_ctx->periodic.tmo[i].appeared) {
			APPL_PRINT(" ERR: periodic TMO %d event(s) after successful cancel!\n",
				   i);
			errors++;
		}
		if (max_dummy < eo_ctx->periodic.tmo[i].max_dummy)
			max_dummy = eo_ctx->periodic.tmo[i].max_dummy;
	}

	if (max_dummy) {
		APPL_PRINT(" Max extra processing delay (us): %u\n",
			   max_dummy);
		if (eo_ctx->max_dummy < max_dummy)
			eo_ctx->max_dummy = max_dummy;
	}

	return errors;
}

/* emulate processing delay */
static void dummy_processing(unsigned int us)
{
	struct timespec now, sample;

	VISUAL_DBG("D");

	clock_gettime(APP_LINUX_CLOCK_SRC, &now);
	/* uint64_t t1 = env_get_cycle(); */
	do {
		clock_gettime(APP_LINUX_CLOCK_SRC, &sample);
	} while (ts_diff_ns(&now, &sample) / 1000ULL < us);
	/* uint64_t t2 = env_get_cycle();
	 * printf("dummy delay: %u\n", us);
	 * printf("%ld ticks\n", t2-t1);
	 */
	VISUAL_DBG("d");
}
