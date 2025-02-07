/*
 *   Copyright (c) 2024, Nokia Solutions and Networks
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
 * Event Machine performance test example combining together loop, loop_vector,
 * loop_multircv, loop_refs and pairs test applications with parameters.
 *
 * loops (loop, loop_vector, loop_multircv):
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of EOs in the system. The test has a number of EOs, each
 * with one queue. Each EO receives events through its dedicated queue and
 * sends them right back into the same queue, thus looping the events.
 *
 * For vector loop-type instead of separate the events is used event-vectors.
 *
 * For multircv loop-type is used a multi-event EO-receive function.
 *
 * loop_refs:
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of EOs in the system. The test has a number of EOs, each
 * with one queue. Each EO receives events (references) through its dedicated
 * queue and sends them right back into the same queue, thus looping the events.
 * Each sent event is a reference in the example.
 *
 * pairs:
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of EO pairs in the system. Test has a number of EOs
 * arranged to pairs, which ping-pong looping the events between the EOs defined
 * for the pair. Depending on test dynamics (e.g. single burst in atomic queue)
 * only one EO of a pair might be active at a time.
 *
 * From the pairs test application is inherited option to set portion of queues
 * to different priorities. It uses three different queue priority levels that
 * affect scheduling (might starve low prio queues if using a strict prio
 * scheduler).
 *
 * The --priorities option can be also used now for other loops as well.
 */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

#define USAGE_FMT \
"\n"									\
"Usage: %s EM-ODP options -- APP SPECIFIC OPTIONS\n"			\
"  E.g. %s -c 0xfe -t -- -l l -e 10\n"					\
"\n"									\
"Open Event Machine example application.\n"				\
"\n"									\
"Note to get EM-ODP options:\n"						\
"  -h, --help	 before/without -- option.\n"				\
"\n"									\
"APP specific options\n"						\
"  -l, --loop           <loop_type)\n"                                  \
"                       run tests for loop type, (default: loop)\n" \
"                       l: loop\n" \
"                       r: refs\n" \
"                       v: vectors\n" \
"                       m: multircv\n" \
"                       p: pairs\n" \
"  -f, --free           Free each received event and allocate it a new\n" \
"                       (default: not freed)\n" \
"  -u, --use-prio       Use different queue priority levels.\n" \
"                       (default: single priority level used)\n" \
"  -d, --data-size      Event payload data size in bytes. (default: 250 B)\n" \
"                       (maximum: 256 B)\n" \
"  -e, --eo-count       Number of test EOs and queues. Must be an even number.\n" \
"                       (default: 128) (maximum: 128)\n" \
"  -n, --event-amount	Number of events per queue.\n" \
"                       (default: 32 and 128 for refs and multircv)\n" \
"                       (maximum: 128, for vectors max is 32)\n" \
"  -q, --queue-type     <queue_type>\n" \
"                       Queue type. (default: atomic, but for refs parallel)\n" \
"                       a: atomic\n" \
"                       p: parallel\n" \
"  -c, --count-to-print The number of events to be received before printing\n" \
"                       a result.\n" \
"                       (default is 0xff0000 and 0x3f0000 for refs)\n" \
"                       (maximum: 0x7fffffff)\n" \
"  -m, --multi-send     The maximum number of events to be sent in multi event\n" \
"                       sending. Valid only for multircv.\n" \
"                       (default: 32) (maximum: 32)\n" \
"  -v, --vector-size    The size of vector to be sent in vector loop.\n" \
"                       Valid only for vectors. (default: 8) (maximum: 8)\n" \
"  -h, --help           Display help and exit.\n" \
"\n"

/*
 * Test configuration
 */

/** Define maximum amount events are sent per em_send_multi() call */
#define SEND_MULTI_MAX 32

/** Define maximum amount events are in vector */
#define MAX_VECTOR_SIZE 8

/** Define maximum data size in event. Note: reserved maximum amount from memory */
#define MAX_DATA_SIZE 256

/** Define maximum amount of EOs and queues */
#define MAX_NUM_EO 128

/** Define maximum amount of events */
#define MAX_NUM_EVENTS 128

/** Define maximum count of events to be waited before print */
#define MAX_PRINT_EVENT_COUNT 0x7fffffff

typedef enum loop_type_t {
	LOOP_TYPE_UNDEF = 0,
	LOOP_TYPE_LOOP = 1,
	LOOP_TYPE_VECTOR = 2,
	LOOP_TYPE_MULTIRCV = 3,
	LOOP_TYPE_REFS = 4,
	LOOP_TYPE_PAIRS = 5
} loop_type_t;

/* Result APPL_PRINT() format string */
#define RESULT_PRINTF_FMT \
"cycles/event:% -8.2f  Mevents/s/core: %-6.2f %5.0f MHz  core%02d %" PRIu64 "\n"

/**
 * Performance test statistics (per core)
 */
typedef struct {
	int64_t events;
	uint64_t begin_cycles;
	uint64_t end_cycles;
	uint64_t print_count;
} perf_stat_t;

/**
 * Performance test EO context
 */
typedef struct {
	/* Next destination queue */
	em_queue_t dest;
} eo_context_t;

/**
 * Perf test shared memory, read-only after start-up, allow cache-line sharing
 */
typedef struct {
	/* EO context table */
	eo_context_t eo_ctx_tbl[MAX_NUM_EO];
	/* EO table */
	em_eo_t eo_tbl[MAX_NUM_EO];
	/* Event pool used by this application */
	em_pool_t pool;
	/* Vector pool used by this application */
	em_pool_t vec_pool;
} perf_shm_t;

/** EM-core local pointer to shared memory */
static ENV_LOCAL perf_shm_t *perf_shm;

/* Command line arguments specific to this application */
static struct {
	/* The queue type */
	uint8_t queue_type;
	/* The loop type */
	uint32_t loop_type;
	/* The print event count */
	int64_t print_event_count;
	/* The number of events in queue */
	int number_event_per_queue;
	/* Data size for events */
	int data_size;
	/* Number of E0s and queues */
	int number_eo;
	/* Number of events sent with send_multi */
	int num_multi_events;
	/* Number of events per vector */
	int vector_size;
	/* Free event allocations in every round */
	bool free_event;
	/* Use priority for pairs */
	bool use_prio;
} perf_args;

/**
 * Core specific test statistics.
 *
 */
static ENV_LOCAL perf_stat_t core_stat;

/*
 * Local function prototypes
 */

static em_status_t
perf_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
perf_stop(void *eo_context, em_eo_t eo);

static void
perf_receive(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_ctx);

static void
perf_receive_free(void *eo_context, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *q_ctx);

static void
perf_receive_pairs(void *eo_context, em_event_t event, em_event_type_t type,
		   em_queue_t queue, void *q_ctx);

static void
perf_receive_pairs_free(void *eo_context, em_event_t event, em_event_type_t type,
			em_queue_t queue, void *q_ctx);

static void
perf_receive_multi(void *eo_context, em_event_t event_tbl[], int num,
		   em_queue_t queue, void *queue_context);

static void
perf_receive_multi_free(void *eo_context, em_event_t event_tbl[], int num,
			em_queue_t queue, void *queue_context);

static void
print_result(perf_stat_t *const perf_stat);

static em_queue_prio_t
get_queue_priority(const int index);

static void
set_default_opts(void);

static void
update_default_opts(bool queue_type_set, bool count_to_print_set, bool number_event_per_queue_set);

static void
print_loop_options(void);

static const char
*get_loop_type_str(uint32_t loop_type);

static void
create_vector_pool(void);

static void
send_burst(em_queue_t queue, em_event_t *events);

static void
alloc_send_events_ref(em_queue_t *queues);

static void
alloc_send_events_multircv(em_queue_t *queues);

static void
alloc_send_events(em_queue_t *queues, uint32_t num_eos);

static void
alloc_send_events_vector(em_queue_t *queues);

static void
create_queues_eos(uint32_t num_of_eos, em_queue_t *queues,
		  const char *eo_name, const char *queue_name,
		  uint8_t shift_eo_ctx_tbl, em_eo_t *eos);

static void
sync_to_queues_and_start_eos(em_queue_t *queues, em_queue_t *target_queues,
			     uint32_t offset, uint32_t num_of_eos, em_eo_t *eos);

static inline void send_event(em_event_t event, em_queue_t queue);

static inline em_event_t alloc_event(int data_size, em_event_type_t type);

static inline void start_measurement(void);

static inline void end_measurement(void);

static inline void restart_measurement(int64_t *events);

static void
usage(char *progname)
{
	APPL_PRINT(USAGE_FMT, NO_PATH(progname), NO_PATH(progname));
}

static void parse_app_specific_args(int argc, char *argv[])
{
	int pca_argv_idx = 0;

	/* Find the index of -- where getopt_long at cm_setup.c would stop */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			pca_argv_idx = i;
			break;
		}
	}

	set_default_opts();
	bool queue_type_set = false;
	bool count_to_print_set = false;
	bool number_event_per_queue_set = false;

	/* No app specific argument is given, skip parsing */
	if (!pca_argv_idx)
		return;

	optind = pca_argv_idx + 1; /* start from '--' + 1 */

	static const struct option longopts[] = {
		{"loop",		required_argument,	NULL, 'l'},
		{"free",		no_argument,		NULL, 'f'},
		{"use-prio",		no_argument,		NULL, 'u'},
		{"data-size",		required_argument,	NULL, 'd'},
		{"eo-count",		required_argument,	NULL, 'e'},
		{"event-amount",	required_argument,	NULL, 'n'},
		{"queue-type",		required_argument,	NULL, 'q'},
		{"count-to-print",	required_argument,	NULL, 'c'},
		{"multi-send",		required_argument,	NULL, 'm'},
		{"vector-size",		required_argument,	NULL, 'v'},
		{"help",		no_argument,		NULL, 'h'},
		{NULL, 0, NULL, 0}
	};
	static const char *shortopts = "l:fud:e:n:q:c:m:v:h";

	while (1) {
		int opt;
		int long_idx;

		opt = getopt_long(argc, argv, shortopts, longopts, &long_idx);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 'l': {
			/* Sanity check */
			if (*optarg == 'l') {
				perf_args.loop_type = LOOP_TYPE_LOOP;
			} else if (*optarg == 'v') {
				perf_args.loop_type = LOOP_TYPE_VECTOR;
			} else if (*optarg == 'm') {
				perf_args.loop_type = LOOP_TYPE_MULTIRCV;
			} else if (*optarg == 'r') {
				perf_args.loop_type = LOOP_TYPE_REFS;
			} else if (*optarg == 'p') {
				perf_args.loop_type = LOOP_TYPE_PAIRS;
			} else {
				APPL_PRINT("Loop type must be l, v, m, r or p\n");
				APPL_EXIT_FAILURE("Invalid type of loop: %s", optarg);
			}
		}
		break;

		case 'f': {
			perf_args.free_event = true;
		}
		break;

		case 'u': {
			perf_args.use_prio = true;
		}
		break;

		case 'd': {
			perf_args.data_size = atoi(optarg);
			if (perf_args.data_size <= 0 ||
			    perf_args.data_size > MAX_DATA_SIZE)
				APPL_EXIT_FAILURE("Invalid data size: %s",
						  optarg);
		}
		break;

		case 'e': {
			perf_args.number_eo = atoi(optarg);
			if (perf_args.number_eo <= 0 ||
			    perf_args.number_eo > MAX_NUM_EO ||
			    (perf_args.number_eo % 2))
				APPL_EXIT_FAILURE("Invalid EO amount: %s",
						  optarg);
		}
		break;

		case 'n': {
			perf_args.number_event_per_queue = atoi(optarg);
			number_event_per_queue_set = true;
			if (perf_args.number_event_per_queue <= 0 ||
			    perf_args.number_event_per_queue > MAX_NUM_EVENTS)
				APPL_EXIT_FAILURE("Invalid events amount: %s",
						  optarg);
		}
		break;

		case 'q': {
			/* Sanity check */
			if (*optarg == 'a') {
				perf_args.queue_type = EM_QUEUE_TYPE_ATOMIC;
			} else if (*optarg == 'p') {
				perf_args.queue_type = EM_QUEUE_TYPE_PARALLEL;
			} else {
				APPL_PRINT("Queue type must be a (atomic) or p (parallel)\n");
				APPL_EXIT_FAILURE("Invalid type of queue: %s", optarg);
			}
			queue_type_set = true;
		}
		break;

		case 'c': {
			char *end_ptr;

			perf_args.print_event_count = strtol(optarg, &end_ptr, 16);
			count_to_print_set = true;
			if (perf_args.print_event_count <= 0 ||
			    perf_args.print_event_count > MAX_PRINT_EVENT_COUNT)
				APPL_EXIT_FAILURE("Invalid print event count: %s",
						  optarg);
		}
		break;

		case 'm': {
			perf_args.num_multi_events = atoi(optarg);
			if (perf_args.num_multi_events <= 0 ||
			    perf_args.num_multi_events > SEND_MULTI_MAX)
				APPL_EXIT_FAILURE("Invalid multi-events count: %s",
						  optarg);
		}
		break;

		case 'v': {
			perf_args.vector_size = atoi(optarg);
			if (perf_args.vector_size <= 0 ||
			    perf_args.vector_size > MAX_VECTOR_SIZE)
				APPL_EXIT_FAILURE("Invalid vector size: %s",
						  optarg);
		}
		break;

		/* Note: must specify -h after -- to print usage info
		 * specific to this app. Otherwise, general EM-ODP usage
		 * will be displayed.
		 */
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		case ':':
			usage(argv[0]);
			APPL_EXIT_FAILURE("Missing arguments!\n");
			break;

		default:
			usage(argv[0]);
			APPL_EXIT_FAILURE("Unknown option!\n");
			break;
		}
	}

	update_default_opts(queue_type_set, count_to_print_set, number_event_per_queue_set);

	print_loop_options();

	/* Reset 'extern optind' to restart scanning in cm_setup() */
	optind = 1;
}

static void
set_default_opts(void)
{
	/* Set default values */
	perf_args.loop_type = LOOP_TYPE_LOOP;
	perf_args.free_event = false;
	perf_args.use_prio = false;
	perf_args.data_size = 250;
	perf_args.number_eo = 128;
	perf_args.number_event_per_queue = 32;
	perf_args.queue_type = EM_QUEUE_TYPE_ATOMIC;
	perf_args.print_event_count = 0xff0000;
	perf_args.num_multi_events = 32;
	perf_args.vector_size = 8;
}

static void
update_default_opts(bool queue_type_set, bool count_to_print_set, bool number_event_per_queue_set)
{
	/* Update values which depends of LOOP_TYPE when parameters were not explicitly set */
	if (!number_event_per_queue_set) {
		if (perf_args.loop_type == LOOP_TYPE_REFS ||
		    perf_args.loop_type == LOOP_TYPE_MULTIRCV) {
			perf_args.number_event_per_queue = 128;
		}
	}
	if (!queue_type_set) {
		if (perf_args.loop_type == LOOP_TYPE_REFS)
			perf_args.queue_type = EM_QUEUE_TYPE_PARALLEL;
	}
	if (!count_to_print_set) {
		if (perf_args.loop_type == LOOP_TYPE_REFS)
			perf_args.print_event_count = 0x3f0000;
	}
	/* If loop type is vector, no free-event functionality supported */
	if (perf_args.loop_type == LOOP_TYPE_VECTOR) {
		perf_args.free_event = false;
		/* If loop type is vector, max number of events per queue is 32 */
		if (perf_args.number_event_per_queue > 32)
			APPL_EXIT_FAILURE("With vector loop max nbr of events per queue is 32\n");
	}
}

static void
print_loop_options(void)
{
	APPL_PRINT("EM loop options:\n");
	APPL_PRINT("  Loop type:    %s\n", get_loop_type_str(perf_args.loop_type));
	APPL_PRINT("  Free:         %s\n", perf_args.free_event ? "on" : "off");
	APPL_PRINT("  Priorities:   %s\n", perf_args.use_prio ? "on" : "off");
	APPL_PRINT("  Data size:    %d\n", perf_args.data_size);
	APPL_PRINT("  EO count:     %d\n", perf_args.number_eo);
	APPL_PRINT("  Event amount: %d\n", perf_args.number_event_per_queue);
	APPL_PRINT("  Queue type:   %s\n", (perf_args.queue_type == EM_QUEUE_TYPE_ATOMIC)
		   ? "ATOMIC" : "PARALLEL");
	APPL_PRINT("  print count:  0x%lx / (%ld)\n", perf_args.print_event_count,
		   perf_args.print_event_count);
	if (perf_args.loop_type == LOOP_TYPE_MULTIRCV)
		APPL_PRINT("  multi send:   %d\n", perf_args.num_multi_events);
	if (perf_args.loop_type == LOOP_TYPE_VECTOR)
		APPL_PRINT("  vector size:  %d\n", perf_args.vector_size);
}

static const char
*get_loop_type_str(uint32_t loop_type)
{
	const char *type_str;

	switch (loop_type) {
	case LOOP_TYPE_UNDEF:
		type_str = "UNDEF";
		break;
	case LOOP_TYPE_LOOP:
		type_str = "LOOP";
		break;
	case LOOP_TYPE_VECTOR:
		type_str = "VECTOR";
		break;
	case LOOP_TYPE_REFS:
		type_str = "REFS";
		break;
	case LOOP_TYPE_MULTIRCV:
		type_str = "MULTIRCV";
		break;
	case LOOP_TYPE_PAIRS:
		type_str = "PAIRS";
		break;
	default:
		type_str = "UNKNOWN";
		break;
	}

	return type_str;
}

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
	/* Parse arguments specific to this app */
	parse_app_specific_args(argc, argv);

	/* This shall be updated before threads are created */
	if (perf_args.loop_type == LOOP_TYPE_MULTIRCV)
		core_stat.events = 0;
	else
		core_stat.events = -perf_args.print_event_count;

	return cm_setup(argc, argv);
}

/**
 * Init of the Loop performance test application.
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_init(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	int core = em_core_id();

	if (core == 0) {
		perf_shm = env_shared_reserve("PerfSharedMem",
					      sizeof(perf_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		perf_shm = env_shared_lookup("PerfSharedMem");
	}

	if (perf_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Perf init failed on EM-core:%u", em_core_id());
	else if (core == 0)
		memset(perf_shm, 0, sizeof(perf_shm_t));
}

/**
 * Startup of the Loop performance test application.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_start(const appl_conf_t *appl_conf)
{
	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		perf_shm->pool = appl_conf->pools[0];
	else
		perf_shm->pool = EM_POOL_DEFAULT;

	APPL_PRINT("\n"
		   "***********************************************************\n"
		   "EM APPLICATION: '%s' initializing:\n"
		   "  %s: %s() - EM-core:%d\n"
		   "  Application running on %u EM-cores (procs:%u, threads:%u)\n"
		   "  using event pool:%" PRI_POOL "\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   appl_conf->core_count, appl_conf->num_procs, appl_conf->num_threads,
		   perf_shm->pool);

	test_fatal_if(perf_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/*
	 * Create and start application EOs
	 * Send initial test events to the EOs' queues
	 */

	switch (perf_args.loop_type) {
	case LOOP_TYPE_LOOP: {
		em_queue_t queues[perf_args.number_eo];
		em_eo_t eos[perf_args.number_eo];

		create_queues_eos(perf_args.number_eo, queues, "loop-eo",
				  "queue A", 0, eos);
		sync_to_queues_and_start_eos(queues, queues, 0, perf_args.number_eo, eos);
		alloc_send_events(queues, perf_args.number_eo);
		break;
	}

	case LOOP_TYPE_VECTOR: {
		em_queue_t queues[perf_args.number_eo];
		em_eo_t eos[perf_args.number_eo];

		create_vector_pool();
		create_queues_eos(perf_args.number_eo, queues, "loop-eo",
				  "queue A", 0, eos);
		sync_to_queues_and_start_eos(queues, queues, 0, perf_args.number_eo, eos);
		alloc_send_events_vector(queues);
		break;
	}

	case LOOP_TYPE_MULTIRCV: {
		em_queue_t queues[perf_args.number_eo];
		em_eo_t eos[perf_args.number_eo];

		create_queues_eos(perf_args.number_eo, queues, "loop-eo",
				  "queue A", 0, eos); /*multircv create EOs!!!!!*/
		sync_to_queues_and_start_eos(queues, queues, 0, perf_args.number_eo, eos);
		alloc_send_events_multircv(queues);
		break;
	}

	case LOOP_TYPE_REFS: {
		em_queue_t queues[perf_args.number_eo];
		em_eo_t eos[perf_args.number_eo];

		create_queues_eos(perf_args.number_eo, queues, "loop-eo",
				  "queue A", 0, eos);
		sync_to_queues_and_start_eos(queues, queues, 0, perf_args.number_eo, eos);
		alloc_send_events_ref(queues);
		break;
	}

	case LOOP_TYPE_PAIRS: {
		int half_num_eo = perf_args.number_eo / 2;
		em_queue_t queues_a[half_num_eo];
		em_queue_t queues_b[half_num_eo];
		em_eo_t eos_a[half_num_eo];
		em_eo_t eos_b[half_num_eo];

		create_queues_eos(half_num_eo, queues_a, "pairs-eo-a",
				  "queue-A", 0, eos_a);
		create_queues_eos(half_num_eo, queues_b, "pairs-eo-b",
				  "queue-B", half_num_eo, eos_b);
		sync_to_queues_and_start_eos(queues_a, queues_b, 0, half_num_eo, eos_a);
		sync_to_queues_and_start_eos(queues_b, queues_a, half_num_eo,
					     half_num_eo, eos_b);
		alloc_send_events(queues_a, half_num_eo);
		alloc_send_events(queues_b, half_num_eo);
		break;
	}

	default:

		break;
	}
}

static void
send_burst(em_queue_t queue, em_event_t *events)
{
	/* Send in bursts of  events/vectors */
	const int send_rounds = perf_args.number_event_per_queue
				/ perf_args.num_multi_events;
	const int left_over = perf_args.number_event_per_queue
			      % perf_args.num_multi_events;
	int num_sent = 0;
	int m, n;

	for (m = 0, n = 0; m < send_rounds; m++, n += perf_args.num_multi_events) {
		num_sent += em_send_multi(&events[n], perf_args.num_multi_events,
					  queue);
	}
	if (left_over) {
		num_sent += em_send_multi(&events[n], left_over,
					  queue);
	}
	test_fatal_if(num_sent != perf_args.number_event_per_queue,
		      "Event send multi failed:%d (%d)\n"
		      "Q:%" PRI_QUEUE "",
		      num_sent, perf_args.number_event_per_queue, queue);
}

static void
alloc_send_events_ref(em_queue_t *queues)
{
	em_event_t ev = em_alloc(perf_args.data_size,
		      EM_EVENT_TYPE_SW, perf_shm->pool);
	test_fatal_if(ev == EM_EVENT_UNDEF,
		      "Event allocation failed");
	for (int i = 0; i < perf_args.number_eo; i++) {
		em_queue_t queue = queues[i];
		em_event_t events[perf_args.number_event_per_queue];

		/* LOOP_TYPE_REFS */
		for (int j = 0; j < perf_args.number_event_per_queue; j++) {
			em_event_t ref = em_event_ref(ev);

			test_fatal_if(ref == EM_EVENT_UNDEF,
				      "Event ref creation failed (%d, %d)", i, j);
			events[j] = ref;
		}
		send_burst(queue, events);
	}
	/* Free the original event */
	em_free(ev);
	env_sync_mem();
}

static void
alloc_send_events_multircv(em_queue_t *queues)
{
	for (int i = 0; i < perf_args.number_eo; i++) {
		em_queue_t queue = queues[i];
		em_event_t events[perf_args.number_event_per_queue];

		int num, tot = 0;

		/* Alloc and send test events */
		do {
			num = em_alloc_multi(events,
					     perf_args.number_event_per_queue - tot,
					     perf_args.data_size,
					     EM_EVENT_TYPE_SW, perf_shm->pool);
			tot += num;
		} while (tot < num && num > 0);
		test_fatal_if(tot != perf_args.number_event_per_queue,
			      "Allocated:%d of requested:%d events",
			      tot, perf_args.number_event_per_queue);
		send_burst(queue, events);
	}
	env_sync_mem();
}

static void
alloc_send_events(em_queue_t *queues, uint32_t num_eos)
{
	for (uint32_t i = 0; i < num_eos; i++) {
		em_event_t ev;
		em_queue_t queue = queues[i];
		em_event_t events[perf_args.number_event_per_queue];

		/* Alloc and send test events */
		for (int j = 0; j < perf_args.number_event_per_queue; j++) {
			ev = em_alloc(perf_args.data_size,
				      EM_EVENT_TYPE_SW, perf_shm->pool);
			test_fatal_if(ev == EM_EVENT_UNDEF,
				      "Event allocation failed (%d, %d)", i, j);
			events[j] = ev;
		}
		send_burst(queue, events);
	}
	env_sync_mem();
}

static void
alloc_send_events_vector(em_queue_t *queues)
{
	for (int i = 0; i < perf_args.number_eo; i++) {
		em_event_t ev;
		em_queue_t queue = queues[i];
		em_event_t events[perf_args.number_event_per_queue];

		/* Alloc and send test vectors */
		for (int j = 0; j < perf_args.number_event_per_queue; j++) {
			uint32_t vec_sz = (j % perf_args.vector_size) + 1;
			em_event_t vec = em_alloc(vec_sz, EM_EVENT_TYPE_VECTOR,
						  perf_shm->vec_pool);
			test_fatal_if(vec == EM_EVENT_UNDEF,
				      "Vector allocation failed (%d, %d)", i, j);
			em_event_t *vectbl = NULL;
			uint32_t curr_sz = em_event_vector_tbl(vec, &vectbl);

			test_fatal_if(curr_sz || !vectbl,
				      "Vector table invalid: sz=%d vectbl=%p)",
				      curr_sz, vectbl);

			for (uint32_t k = 0; k < vec_sz; k++) {
				ev = em_alloc(perf_args.data_size,
					      EM_EVENT_TYPE_SW, perf_shm->pool);
				test_fatal_if(ev == EM_EVENT_UNDEF,
					      "Event allocation failed (%d, %d)", i, j);
				vectbl[k] = ev;
			}
			em_event_vector_size_set(vec, vec_sz);

			events[j] = vec;
		}
		send_burst(queue, events);
	}
	env_sync_mem();
}

static void
create_vector_pool(void)
{
	em_pool_cfg_t vec_pool_cfg;
	em_pool_t vec_pool = EM_POOL_UNDEF;

	/* For vectors will be initiated vector pool, not needed for other loops but doesn't harm */
	em_pool_cfg_init(&vec_pool_cfg);
	vec_pool_cfg.event_type = EM_EVENT_TYPE_VECTOR;
	vec_pool_cfg.num_subpools = 1;
	vec_pool_cfg.subpool[0].cache_size = 0; /* all allocated in startup */
	vec_pool_cfg.subpool[0].num = perf_args.number_eo * perf_args.number_event_per_queue;
	vec_pool_cfg.subpool[0].size = perf_args.vector_size;

	vec_pool = em_pool_create("vector-pool", EM_POOL_UNDEF, &vec_pool_cfg);
	test_fatal_if(vec_pool == EM_POOL_UNDEF, "vector pool create failed!");

	perf_shm->vec_pool = vec_pool;
}

static void
create_queues_eos(uint32_t num_of_eos, em_queue_t *queues,
		  const char *eo_name, const char *queue_name,
		  uint8_t shift_eo_ctx_tbl, em_eo_t *eos)
{
	em_queue_t queue;
	eo_context_t *eo_ctx;
	em_eo_t eo;
	em_eo_multircv_param_t eo_param;
	em_receive_func_t recv_fn;

	for (uint32_t i = 0; i < num_of_eos; i++) {
		/* Create the EO's loop queue */
		queue = em_queue_create(queue_name, perf_args.queue_type,
					get_queue_priority(i),
					EM_QUEUE_GROUP_DEFAULT, NULL);

		test_fatal_if(queue == EM_QUEUE_UNDEF,
			      "Queue creation failed, round:%d", i);
		queues[i] = queue;

		/* Create the EO */
		switch (perf_args.loop_type) {
		case LOOP_TYPE_MULTIRCV:
			/* Init & create the EO */
			em_eo_multircv_param_init(&eo_param);
			/* Set EO params needed by this application */
			eo_param.start = perf_start;
			eo_param.stop = perf_stop;
			if (perf_args.free_event)
				eo_param.receive_multi = perf_receive_multi_free;
			else
				eo_param.receive_multi = perf_receive_multi;
			/* eo_param.max_events = use default; */
			eo = em_eo_create_multircv("loop-eo", &eo_param);
			break;
		case LOOP_TYPE_PAIRS:
			if (perf_args.free_event)
				recv_fn = perf_receive_pairs_free;
			else
				recv_fn = perf_receive_pairs;
			eo_ctx = &perf_shm->eo_ctx_tbl[i + shift_eo_ctx_tbl];
			eo = em_eo_create(eo_name, perf_start, NULL, perf_stop, NULL,
					  recv_fn, eo_ctx);
			break;
		case LOOP_TYPE_LOOP:
		case LOOP_TYPE_VECTOR:
		case LOOP_TYPE_REFS:
			if (perf_args.free_event)
				recv_fn = perf_receive_free;
			else
				recv_fn = perf_receive;
			eo_ctx = &perf_shm->eo_ctx_tbl[i + shift_eo_ctx_tbl];
			eo = em_eo_create(eo_name, perf_start, NULL, perf_stop, NULL,
					  recv_fn, eo_ctx);
			break;
		default:
			eo = EM_EO_UNDEF;
		}
		test_fatal_if(eo == EM_EO_UNDEF,
			      "EO(%d) creation failed!", i);
		eos[i] = eo;
	}
}

static void
sync_to_queues_and_start_eos(em_queue_t *queues, em_queue_t *target_queues,
			     uint32_t offset, uint32_t num_of_eos, em_eo_t *eos)
{
	em_status_t ret, start_ret = EM_ERROR;

	for (uint32_t i = 0; i < num_of_eos; i++) {
		eo_context_t *eo_ctx = &perf_shm->eo_ctx_tbl[i + offset];

		eo_ctx->dest = target_queues[i];
		perf_shm->eo_tbl[i + offset] = eos[i];
		ret = em_eo_add_queue_sync(eos[i], queues[i]);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      ret, eos[i], queues[i]);

		ret = em_eo_start_sync(eos[i], &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO start:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);
	}
}

void test_stop(const appl_conf_t *appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo;
	em_status_t ret;
	int i;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	for (i = 0; i < perf_args.number_eo; i++) {
		/* Stop & delete EO */
		eo = perf_shm->eo_tbl[i];

		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);

		ret = em_eo_delete(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
	}

	if (perf_args.loop_type == LOOP_TYPE_VECTOR)
		em_pool_delete(perf_shm->vec_pool);
}

void test_term(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	const int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(perf_shm);
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t
perf_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	(void)eo_context;
	(void)eo;
	(void)conf;

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t
perf_stop(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	/* Remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);
	return ret;
}

static inline void send_event(em_event_t event, em_queue_t queue)
{
	em_status_t ret = em_send(event, queue);

	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, queue);
	}
}

static inline em_event_t alloc_event(int data_size, em_event_type_t type)
{
	em_event_t event = em_alloc(data_size, type, perf_shm->pool);

	test_fatal_if(event == EM_EVENT_UNDEF, "Event alloc fails");
	return event;
}

static inline void start_measurement(void)
{
	core_stat.begin_cycles = env_get_cycle();
}

static inline void end_measurement(void)
{
	core_stat.end_cycles = env_get_cycle();
	core_stat.print_count += 1;
	print_result(&core_stat);
}

static inline void restart_measurement(int64_t *events)
{
	*events = -1; /* +1 below => 0 */
}

/**
 * @private
 *
 * EO receive function.
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *queue_context)
{
	int64_t events = core_stat.events;

	(void)eo_context;
	(void)type;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		start_measurement();
	} else if (unlikely(events == perf_args.print_event_count)) {
		end_measurement();
		restart_measurement(&events);
	}

	send_event(event, queue);

	events++;
	core_stat.events = events;
}

/**
 * @private
 *
 * EO receive function. Freeing and recreating event every round
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive_free(void *eo_context, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *queue_context)
{
	int64_t events = core_stat.events;

	(void)eo_context;
	(void)type;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		start_measurement();
	} else if (unlikely(events == perf_args.print_event_count)) {
		end_measurement();
		restart_measurement(&events);
	}

	em_free(event);
	event = alloc_event(perf_args.data_size, type);

	send_event(event, queue);

	events++;
	core_stat.events = events;
}

/**
 * @private
 *
 * EO pairs receive function.
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive_pairs(void *eo_context, em_event_t event, em_event_type_t type,
		   em_queue_t queue, void *queue_context)
{
	int64_t events = core_stat.events;
	eo_context_t *const eo_ctx = eo_context;
	const em_queue_t dst_queue = eo_ctx->dest;

	(void)type;
	(void)queue;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		start_measurement();
	} else if (unlikely(events == perf_args.print_event_count)) {
		end_measurement();
		restart_measurement(&events);
	}

	send_event(event, dst_queue);

	events++;
	core_stat.events = events;
}

/**
 * @private
 *
 * EO pairs receive function.
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive_pairs_free(void *eo_context, em_event_t event, em_event_type_t type,
			em_queue_t queue, void *queue_context)
{
	int64_t events = core_stat.events;
	eo_context_t *const eo_ctx = eo_context;
	const em_queue_t dst_queue = eo_ctx->dest;

	(void)type;
	(void)queue;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		start_measurement();
	} else if (unlikely(events == perf_args.print_event_count)) {
		end_measurement();
		restart_measurement(&events);
	}

	em_free(event);
	event = alloc_event(perf_args.data_size, type);

	send_event(event, dst_queue);

	events++;
	core_stat.events = events;
}

/**
 * @private
 *
 * EO receive function for Multircv loops
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive_multi(void *eo_context, em_event_t event_tbl[], int num,
		   em_queue_t queue, void *queue_context)
{
	int64_t event_count = core_stat.events;
	int ret;

	(void)eo_context;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free_multi(event_tbl, num);
		return;
	}

	if (unlikely(event_count == 0)) {
		start_measurement();
	} else if (unlikely(event_count >= perf_args.print_event_count)) {
		end_measurement();
		event_count = -num; /* +num below => 0 */
	}

	ret = em_send_multi(event_tbl, num, queue);
	if (unlikely(ret != num)) {
		em_free_multi(&event_tbl[ret], num - ret);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send-multi:%d Num:%d Queue:%" PRI_QUEUE "",
			      ret, num, queue);
	}

	event_count += num;
	core_stat.events = event_count;
}

/**
 * @private
 *
 * EO receive function for Multircv loops freeing and recreating event every round
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive_multi_free(void *eo_context, em_event_t event_tbl[], int num,
			em_queue_t queue, void *queue_context)
{
	int64_t event_count = core_stat.events;
	int ret;

	(void)eo_context;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free_multi(event_tbl, num);
		return;
	}

	if (unlikely(event_count == 0)) {
		start_measurement();
	} else if (unlikely(event_count >= perf_args.print_event_count)) {
		end_measurement();
		event_count = -num; /* +num below => 0 */
	}

	em_free_multi(event_tbl, num);
	ret = em_alloc_multi(event_tbl, num, perf_args.data_size,
			     EM_EVENT_TYPE_SW, perf_shm->pool);
	test_fatal_if(ret != num, "Allocated %d of num:%d events",
		      ret, num);

	ret = em_send_multi(event_tbl, num, queue);
	if (unlikely(ret != num)) {
		em_free_multi(&event_tbl[ret], num - ret);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send-multi:%d Num:%d Queue:%" PRI_QUEUE "",
			      ret, num, queue);
	}

	event_count += num;
	core_stat.events = event_count;
}

/**
 * Get queue priority value based on the index number.
 *
 * @param Queue index
 *
 * @return Queue priority value
 *
 * @note Priority distribution: 40% LOW, 40% NORMAL, 20% HIGH
 */
static em_queue_prio_t
get_queue_priority(const int queue_index)
{
	em_queue_prio_t prio;

	if (perf_args.use_prio) {
		int remainder = queue_index % 5;

		if (remainder <= 1)
			prio = EM_QUEUE_PRIO_LOW;
		else if (remainder <= 3)
			prio = EM_QUEUE_PRIO_NORMAL;
		else
			prio = EM_QUEUE_PRIO_HIGH;
	} else {
		prio = EM_QUEUE_PRIO_NORMAL;
	}

	return prio;
}

/**
 * Prints test measurement result
 */
static void
print_result(perf_stat_t *const perf_stat)
{
	uint64_t diff;
	uint32_t hz;
	double mhz;
	double cycles_per_event, events_per_sec;
	uint64_t print_count;

	hz = env_core_hz();
	mhz = ((double)hz) / 1000000.0;

	diff = env_cycles_diff(perf_stat->end_cycles, perf_stat->begin_cycles);

	print_count = perf_stat->print_count;
	cycles_per_event = ((double)diff) / ((double)perf_stat->events);
	events_per_sec = mhz / cycles_per_event; /* Million events/s */

	APPL_PRINT(RESULT_PRINTF_FMT, cycles_per_event, events_per_sec,
		   mhz, em_core_id(), print_count);
}
