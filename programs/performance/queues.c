/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2014, Nokia Solutions and Networks
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
 * Event Machine performance test.
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of queues and events in the system. The test increases
 * the number of queues[+events] for each measurement round and prints the
 * results. The test will stop if the maximum number of supported queues by the
 * system  is reached.
 *
 * Plot the cycles/event to get an idea of how the system scales with an
 * increasing number of queues.
 */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Test options:
 */

/* Alloc and free per event */
#define ALLOC_FREE_PER_EVENT 0 /* false=0 or true=1 */

/*
 * Create all EM queues at startup or create the queues during
 * the test in steps.
 */
#define CREATE_ALL_QUEUES_AT_STARTUP 0 /* false=0 or true=1 */

/*
 * Measure the send-enqueue-schedule-receive latency. Measured separately for
 * 'high priority and 'low priority' queues (ratio 1:4).
 */
#define MEASURE_LATENCY 1 /* false=0 or true=1 */

/*
 * Keep the number of events constant while increasing the number of queues.
 * Should be dividable by or factor of queue_step.
 */
#define CONST_NUM_EVENTS 4096 /* true>0 or false=0 */

/*
 * Test configuration:
 */

#define MAX_CORES  64

/* Number of EO's and queues in a loop */
#define NUM_EOS  4

/* Number of events per queue */
#define NUM_EVENTS  4

#if CONST_NUM_EVENTS > 0
/*
 * Total number of queues when using a constant number of events.
 * Make sure that all queues get 'NUM_EVENTS' events per queue.
 */
#define NUM_QUEUES  (CONST_NUM_EVENTS / NUM_EVENTS)
#else
/*
 * Total number of queues when increasing the total event count for each queue
 * step.
 */
#define NUM_QUEUES  (NUM_EOS * 16 * 1024)
#endif

/* Number of data bytes in an event */
#define DATA_SIZE  128

/* Samples before adding more queues */
#define NUM_SAMPLES  (1 + 8) /* setup(1) + measure(N) */

/* Num events a core processes between samples */
#define EVENTS_PER_SAMPLE  0x400000

/* EM queue type */
#define QUEUE_TYPE EM_QUEUE_TYPE_ATOMIC

/* Core states during test. */
#define CORE_STATE_MEASURE 0
#define CORE_STATE_IDLE    1

/* Result APPL_PRINT() format string */
#define RESULT_PRINTF_HDR  "Cycles/Event  Events/s  cpu-freq\n"
#define RESULT_PRINTF_FMT  "%12.0f %7.0f M %5.0f MHz  %" PRIu64 "\n"

/* Result APPL_PRINT() format string when MEASURE_LATENCY is used */
#define RESULT_PRINTF_LATENCY_HDR \
"Cycles/  Events/  Latency:\n" \
" Event     Sec     hi-ave  hi-max  lo-ave  lo-max  cpu-freq\n"
#define RESULT_PRINTF_LATENCY_FMT \
"%6.0f %7.2f M %8.0f %7" PRIu64 " %7.0f %7" PRIu64 " %5.0f MHz  %" PRIu64 "\n"

/*
 * The number of scheduled queues to use in each test step.
 *
 * NOTE: The max queue step is always 'NUM_QUEUES', even if the value of
 *       'NUM_QUEUES' would be smaller than a listed queue step (then just stop
 *       before reaching the end of the list).
 */
static const int queue_steps[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048,
				  4096, 8192, 16384, 32768, 65536, NUM_QUEUES};

/**
 * Test state,
 * cache line alignment and padding handled in 'perf_shm_t'
 */
typedef struct {
	int queues;
	int step;
	int samples;
	int num_cores;
	int reset_flag;
	double mhz;
	uint64_t print_count;
	env_atomic64_t ready_count;
	/*  if using CONST_NUM_EVENTS:*/
	int free_flag;
	env_atomic64_t freed_count;
} test_status_t;

/**
 * Performance test statistics (per core)
 */
typedef struct {
	uint64_t events;
	uint64_t begin_cycles;
	uint64_t end_cycles;
	uint64_t diff_cycles;
	struct {
		uint64_t events;
		uint64_t hi_prio_ave;
		uint64_t hi_prio_max;
		uint64_t lo_prio_ave;
		uint64_t lo_prio_max;
	} latency;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} core_stat_t;

COMPILE_TIME_ASSERT(sizeof(core_stat_t) % ENV_CACHE_LINE_SIZE == 0,
		    CORE_STAT_SIZE_ERROR);

/**
 * EO context data
 */
typedef struct {
	em_eo_t eo_id;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} eo_context_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_t) % ENV_CACHE_LINE_SIZE == 0,
		    EO_CONTEXT_T__SIZE_ERROR);

/**
 * Queue context data
 */
typedef struct {
	/** This queue */
	em_queue_t this_queue;
	/** Next queue */
	em_queue_t next_queue;
	/** Priority of 'this_queue' */
	em_queue_prio_t prio;
	/** Type of 'this_queue' */
	em_queue_type_t type;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} queue_context_t;

COMPILE_TIME_ASSERT(sizeof(queue_context_t) % ENV_CACHE_LINE_SIZE == 0,
		    QUEUE_CONTEXT_SIZE_ERROR);

/**
 * Performance test event
 */
typedef struct {
	/* Send time stamp */
	uint64_t send_time;
	/* Sequence number */
	int seq;
	/* Test data */
	uint8_t data[DATA_SIZE];
} perf_event_t;

/**
 * Test shared memory
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;

	test_status_t test_status ENV_CACHE_LINE_ALIGNED;

	core_stat_t core_stat[MAX_CORES] ENV_CACHE_LINE_ALIGNED;

	eo_context_t eo_context_tbl[NUM_EOS] ENV_CACHE_LINE_ALIGNED;

	queue_context_t queue_context_tbl[NUM_QUEUES] ENV_CACHE_LINE_ALIGNED;
	/* EO ID's */
	em_eo_t eo[NUM_EOS] ENV_CACHE_LINE_ALIGNED;
} perf_shm_t;

COMPILE_TIME_ASSERT(sizeof(perf_shm_t) % ENV_CACHE_LINE_SIZE == 0,
		    PERF_SHM_T__SIZE_ERROR);

/* EM-core local pointer to shared memory */
static ENV_LOCAL perf_shm_t *perf_shm;

/* EM-core local state */
static ENV_LOCAL int core_state = CORE_STATE_MEASURE;

static em_status_t
error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);

static void
queue_step(void);

static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
stop(void *eo_context, em_eo_t eo);

static void
receive_func(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_context);

static int
update_test_state(em_event_t event);

static void
create_and_link_queues(int start_queue, int num_queues);

static void
print_test_statistics(test_status_t *test_status, int print_header,
		      core_stat_t core_stat[]);

static inline em_event_t
alloc_free_per_event(em_event_t event);

static inline void
measure_latency(perf_event_t *const perf_event, queue_context_t *const q_ctx,
		uint64_t recv_time);

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
 * Test error handler
 *
 * @param eo            Execution object id
 * @param error         The error code
 * @param escope        Error scope
 * @param args          List of arguments (__FILE__, __func__, __LINE__,
 *                                         (format), ## __VA_ARGS__)
 *
 * @return The original error code.
 */
static em_status_t
error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
	if (unlikely(escope == EM_ESCOPE_QUEUE_CREATE)) {
		APPL_PRINT("\nUnable to create more queues\n\n"
			   "Test finished\n");
		raise(SIGINT);
	} else {
		error = test_error_handler(eo, error, escope, args);
	}

	return error;
}

/**
 * Init of the  Queues performance test application.
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
		perf_shm = env_shared_reserve("PerfQueuesSharedMem",
					      sizeof(perf_shm_t));
		em_register_error_handler(error_handler);
	} else {
		perf_shm = env_shared_lookup("PerfQueuesSharedMem");
	}

	if (perf_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Perf test queues init failed on EM-core: %u\n",
			   em_core_id());
	else if (core == 0)
		memset(perf_shm, 0, sizeof(perf_shm_t));
}

/**
 * Startup of the Queues performance test application.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void
test_start(appl_conf_t *const appl_conf)
{
	eo_context_t *eo_ctx;
	em_status_t ret, start_ret = EM_ERROR;
	uint32_t hz;
	const int q_ctx_size = sizeof(perf_shm->queue_context_tbl);
	int i;

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
		   "  %s: %s() - EM-core:%i\n"
		   "  Application running on %d EM-cores (procs:%d, threads:%d)\n"
		   "  using event pool:%" PRI_POOL "\n"
		   "    Max. NUM_QUEUES:          %i\n"
		   "    sizeof queue_context_tbl: %i kB\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads,
		   perf_shm->pool, NUM_QUEUES, q_ctx_size / 1024);

	test_fatal_if(perf_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	hz = env_core_hz();
	perf_shm->test_status.mhz = ((double)hz) / 1000000.0;
	perf_shm->test_status.reset_flag = 0;
	perf_shm->test_status.num_cores = em_core_count();
	perf_shm->test_status.free_flag = 0;

	env_atomic64_init(&perf_shm->test_status.ready_count);
	env_atomic64_init(&perf_shm->test_status.freed_count);

	/* Create EOs */
	for (i = 0; i < NUM_EOS; i++) {
		eo_ctx = &perf_shm->eo_context_tbl[i];
		perf_shm->eo[i] = em_eo_create("perf test eo", start, NULL,
					       stop, NULL, receive_func,
					       eo_ctx);
		test_fatal_if(perf_shm->eo[i] == EM_EO_UNDEF,
			      "EO create failed:%d", i, NUM_EOS);
	}

	APPL_PRINT("  EOs created\n");

	/*
	 * Create and link queues
	 */
	if (CREATE_ALL_QUEUES_AT_STARTUP) /* Create ALL queues at once */
		create_and_link_queues(0, NUM_QUEUES);
	else /* Create queues for the first step, then more before each step */
		create_and_link_queues(0, queue_steps[0]);

	/* Start EOs */
	for (i = 0; i < NUM_EOS; i++) {
		ret = em_eo_start_sync(perf_shm->eo[i], &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO start(%d):%" PRI_STAT " %" PRI_STAT "",
			      i, ret, start_ret);
	}

	queue_step();
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo;
	em_status_t ret;
	int i;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	/* Stop & delete EOs */
	for (i = 0; i < NUM_EOS; i++) {
		eo = perf_shm->eo[i];

		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " stop:%" PRI_STAT "",
			      eo, ret);

		ret = em_eo_delete(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " delete:%" PRI_STAT "",
			      eo, ret);
	}
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(perf_shm);
		em_unregister_error_handler();
	}
}

/**
 * Allocate, initialize and send test step events.
 */
static void
queue_step(void)
{
	queue_context_t *q_ctx;
	em_event_t event;
	perf_event_t *perf_event;
	em_status_t ret;
	const int first = perf_shm->test_status.queues;
	const int step = perf_shm->test_status.step;
	const int queue_count = queue_steps[step];
	int i, j;

	/* Allocate and send test events for the queues in the first step */
	if (CONST_NUM_EVENTS) {
		for (i = 0; i < CONST_NUM_EVENTS; i++) {
			event = em_alloc(sizeof(perf_event_t),
					 EM_EVENT_TYPE_SW, perf_shm->pool);
			test_fatal_if(event == EM_EVENT_UNDEF,
				      "EM alloc failed (%i)", i);
			perf_event = em_event_pointer(event);
			perf_event->seq = i;
			perf_event->send_time = env_get_cycle();

			/* Allocate events evenly to the queues */
			q_ctx = &perf_shm->queue_context_tbl[i % queue_count];
			ret = em_send(event, q_ctx->this_queue);
			test_fatal_if(ret != EM_OK,
				      "EM send:%" PRI_STAT "\n"
				      "Queue:%" PRI_QUEUE "",
				      ret, q_ctx->this_queue);
		}
	} else {
		for (i = first; i < queue_count; i++) {
			q_ctx = &perf_shm->queue_context_tbl[i];

			for (j = 0; j < NUM_EVENTS; j++) {
				event = em_alloc(sizeof(perf_event_t),
						 EM_EVENT_TYPE_SW,
						 perf_shm->pool);
				test_fatal_if(event == EM_EVENT_UNDEF,
					      "EM alloc failed (%d)", i);

				perf_event = em_event_pointer(event);
				perf_event->seq = i * NUM_EVENTS + j;
				perf_event->send_time = env_get_cycle();

				ret = em_send(event, q_ctx->this_queue);
				test_fatal_if(ret != EM_OK,
					      "EM send:%" PRI_STAT "\n"
					      "Queue:%" PRI_QUEUE "",
					      ret, q_ctx->this_queue);
			}
		}
	}

	perf_shm->test_status.queues = queue_count;
	perf_shm->test_status.step++;

	APPL_PRINT("\nNumber of queues: %d\n", perf_shm->test_status.queues);
	if (CONST_NUM_EVENTS)
		APPL_PRINT("Number of events: %d\n", CONST_NUM_EVENTS);
	else
		APPL_PRINT("Number of events: %d\n",
			   perf_shm->test_status.queues * NUM_EVENTS);
}

/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_context_t *eo_ctx = eo_context;

	(void)conf;

	APPL_PRINT("EO %" PRI_EO " starting.\n", eo);

	eo_ctx->eo_id = eo;

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t
stop(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	APPL_PRINT("EO %" PRI_EO " stopping.\n", eo);

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
 * EO receive function.
 *
 * Loops back events and calculates the event rate.
 */
static void
receive_func(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_context)
{
	uint64_t recv_time;
	perf_event_t *perf_event;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (MEASURE_LATENCY) {
		recv_time = env_get_cycle();
		perf_event = em_event_pointer(event);
	}

	queue_context_t *q_ctx;
	em_queue_t dst_queue;
	em_status_t ret;
	int do_return;

	(void)eo_context;
	(void)type;

	q_ctx = q_context;

	/*
	 * Helper: Update the test state, count recv events,
	 * calc & print stats, prepare for next step
	 */
	do_return = update_test_state(event);
	if (unlikely(do_return))
		return;

	if (ALLOC_FREE_PER_EVENT)
		event = alloc_free_per_event(event);

	dst_queue = q_ctx->next_queue;
	test_fatal_if(queue != q_ctx->this_queue, "Queue config error");

	if (MEASURE_LATENCY) {
		measure_latency(perf_event, q_ctx, recv_time);
		perf_event->send_time = env_get_cycle();
	}
	/* Send the event to the next queue */
	ret = em_send(event, dst_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "EM send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, dst_queue);
	}
}

/**
 * Receive function helper: Update the test state
 *
 * Calculates the number of received events, maintains & prints test statistics
 * and restarts/reconfigures the test for the next queue/event-setup
 *
 * @return  '1' if the caller receive function should immediately return,
 *          '0' otherwise
 */
static inline int
update_test_state(em_event_t event)
{
	uint64_t events;
	uint64_t freed_count;
	uint64_t ready_count;
	const int core = em_core_id();
	test_status_t *const tstat = &perf_shm->test_status;
	core_stat_t *const cstat = &perf_shm->core_stat[core];

	events = cstat->events;
	events++;

	if (unlikely(tstat->reset_flag)) {
		events = 0;
		if (CONST_NUM_EVENTS) {
			/* Free all old events before allocating new ones. */
			if (unlikely(tstat->free_flag)) {
				em_free(event);
				freed_count =
				env_atomic64_add_return(&tstat->freed_count, 1);
				if (freed_count == CONST_NUM_EVENTS) {
					/* Last event */
					env_atomic64_set(&tstat->freed_count,
							 0);
					tstat->reset_flag = 0;
					tstat->free_flag = 0;
					queue_step();
				}
				/* Req caller receive-func to return */
				return 1;
			}
		}

		if (unlikely(core_state != CORE_STATE_IDLE)) {
			core_state = CORE_STATE_IDLE;
			cstat->begin_cycles = 0;

			ready_count =
			env_atomic64_add_return(&tstat->ready_count, 1);

			if (ready_count == (uint64_t)tstat->num_cores) {
				env_atomic64_set(&tstat->ready_count, 0);

				if (CONST_NUM_EVENTS) {
					int sample = tstat->samples;
					int queues = tstat->queues;

					if (sample == 0 && queues < NUM_QUEUES)
						tstat->free_flag = 1;
					else
						tstat->reset_flag = 0;
				} else {
					tstat->reset_flag = 0;
				}
			}
		}
	} else if (unlikely(events == 1)) {
		cstat->begin_cycles = env_get_cycle();
		cstat->latency.events = 0;
		cstat->latency.hi_prio_ave = 0;
		cstat->latency.hi_prio_max = 0;
		cstat->latency.lo_prio_ave = 0;
		cstat->latency.lo_prio_max = 0;

		core_state = CORE_STATE_MEASURE;
	} else if (unlikely(events == EVENTS_PER_SAMPLE)) {
		/*
		 * Measurements done for this step. Store results and continue
		 * receiving events until all cores are done.
		 */
		uint64_t diff, begin_cycles, end_cycles;

		cstat->end_cycles = env_get_cycle();

		end_cycles = cstat->end_cycles;
		begin_cycles = cstat->begin_cycles;
		if (likely(end_cycles > begin_cycles))
			diff = end_cycles - begin_cycles;
		else
			diff = UINT64_MAX - begin_cycles + end_cycles + 1;

		cstat->diff_cycles = diff;

		ready_count = env_atomic64_add_return(&tstat->ready_count, 1);

		/*
		 * Check whether all cores are done with the step,
		 * and if done proceed to the next step
		 */
		if (unlikely((int)ready_count == tstat->num_cores)) {
			/* No real need for atomicity here, ran on last core*/
			env_atomic64_set(&tstat->ready_count, 0);

			tstat->reset_flag = 1;
			tstat->samples++;

			/*
			 * Print statistics.
			 * Omit prints for the first sample round to allow the
			 * test to stabilize after setups and teardowns.
			 */
			if (tstat->samples > 1) {
				int print_header = tstat->samples == 2 ? 1 : 0;

				print_test_statistics(tstat, print_header,
						      perf_shm->core_stat);
			}

			/*
			 * Start next test step - setup new queues
			 */
			if (tstat->samples == NUM_SAMPLES &&
			    tstat->queues < NUM_QUEUES) {
				if (!CREATE_ALL_QUEUES_AT_STARTUP) {
					int step = tstat->step;
					int first_q = tstat->queues;
					int num_qs = queue_steps[step] -
						     queue_steps[step - 1];

					create_and_link_queues(first_q, num_qs);
				}

				if (!CONST_NUM_EVENTS)
					queue_step();

				tstat->samples = 0;
			}
		}
	}

	cstat->events = events;

	return 0;
}

/**
 * Creates a number of EM queues, associates them with EOs, and links them.
 */
static void
create_and_link_queues(int start_queue, int num_queues)
{
	int i, j;
	em_queue_t queue, prev_queue;
	em_queue_prio_t prio;
	em_queue_type_t type;
	em_status_t ret;
	queue_context_t *q_ctx;

	APPL_PRINT("\nCreate new queues: %d\n", num_queues);

	if (num_queues % NUM_EOS != 0) {
		APPL_PRINT("%s() arg 'num_queues'=%d not multiple of NUM_EOS=%d\n",
			   __func__, num_queues, NUM_EOS);
		return;
	}

	for (i = start_queue; i < (start_queue + num_queues); i += NUM_EOS) {
		prev_queue = EM_QUEUE_UNDEF;

		for (j = 0; j < NUM_EOS; j++) {
			prio = EM_QUEUE_PRIO_NORMAL;

			if (MEASURE_LATENCY) {
				if (j == 0)
					prio = EM_QUEUE_PRIO_HIGH;
			}

			type = QUEUE_TYPE;
			queue = em_queue_create("queue", type, prio,
						EM_QUEUE_GROUP_DEFAULT, NULL);
			if (queue == EM_QUEUE_UNDEF) {
				APPL_PRINT("Max nbr of supported queues: %d\n",
					   i);
				return;
			}

			q_ctx = &perf_shm->queue_context_tbl[i + j];

			ret = em_queue_set_context(queue, q_ctx);
			test_fatal_if(ret != EM_OK,
				      "em_queue_set_context():%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
				      ret, perf_shm->eo[j], queue);

			ret = em_eo_add_queue_sync(perf_shm->eo[j], queue);
			test_fatal_if(ret != EM_OK,
				      "em_eo_add_queue_sync():%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
				      ret, perf_shm->eo[j], queue);
			/* Link queues */
			q_ctx->this_queue = queue;
			q_ctx->next_queue = prev_queue;
			q_ctx->prio = prio;
			q_ctx->type = type;
			prev_queue = queue;
		}

		/* Connect first queue to the last */
		q_ctx = &perf_shm->queue_context_tbl[i + 0];
		q_ctx->next_queue = prev_queue;
	}

	APPL_PRINT("New Qs created:%d First:%" PRI_QUEUE " Last:%" PRI_QUEUE "\n",
		   num_queues,
		   perf_shm->queue_context_tbl[start_queue].this_queue,
		   perf_shm->queue_context_tbl[start_queue +
					       num_queues - 1].this_queue);
}

/**
 * Print test statistics
 */
static void
print_test_statistics(test_status_t *test_status, int print_header,
		      core_stat_t core_stat[])
{
	uint64_t total_cycles = 0;
	uint64_t total_events = test_status->num_cores * EVENTS_PER_SAMPLE;
	uint64_t latency_events = 0;
	uint64_t latency_hi_ave = 0;
	uint64_t latency_hi_max = 0;
	uint64_t latency_lo_ave = 0;
	uint64_t latency_lo_max = 0;
	double latency_per_hi_ave;
	double latency_per_lo_ave;
	double cycles_per_event, events_per_sec;
	int i;

	for (i = 0; i < test_status->num_cores; i++) {
		total_cycles += core_stat[i].diff_cycles;
		latency_events += core_stat[i].latency.events;
		latency_hi_ave += core_stat[i].latency.hi_prio_ave;
		latency_lo_ave += core_stat[i].latency.lo_prio_ave;
		if (core_stat[i].latency.hi_prio_max > latency_hi_max)
			latency_hi_max = core_stat[i].latency.hi_prio_max;
		if (core_stat[i].latency.lo_prio_max > latency_lo_max)
			latency_lo_max = core_stat[i].latency.lo_prio_max;
	}

	cycles_per_event = (double)total_cycles / (double)total_events;
	events_per_sec = test_status->mhz * test_status->num_cores /
			 cycles_per_event; /* Million events/s */
	latency_per_hi_ave = (double)latency_hi_ave / (double)latency_events;
	latency_per_lo_ave = (double)latency_lo_ave / (double)latency_events;

	if (MEASURE_LATENCY) {
		if (print_header)
			APPL_PRINT(RESULT_PRINTF_LATENCY_HDR);
		APPL_PRINT(RESULT_PRINTF_LATENCY_FMT,
			   cycles_per_event, events_per_sec,
			   latency_per_hi_ave, latency_hi_max,
			   latency_per_lo_ave, latency_lo_max,
			   test_status->mhz, test_status->print_count++);
	} else {
		if (print_header)
			APPL_PRINT(RESULT_PRINTF_HDR);
		APPL_PRINT(RESULT_PRINTF_FMT,
			   cycles_per_event, events_per_sec,
			   test_status->mhz, test_status->print_count++);
	}
}

/**
 * Free the input event and allocate a new one instead
 */
static inline em_event_t
alloc_free_per_event(em_event_t event)
{
	perf_event_t *perf_event = em_event_pointer(event);
	uint64_t send_time = perf_event->send_time;
	int seq = perf_event->seq;
	size_t event_size = em_event_get_size(event);

	em_free(event);

	event = em_alloc(event_size, EM_EVENT_TYPE_SW, perf_shm->pool);

	perf_event = em_event_pointer(event);

	perf_event->send_time = send_time;
	perf_event->seq = seq;

	return event;
}

/**
 * Measure the scheduling latency per event
 */
static inline void
measure_latency(perf_event_t *const perf_event, queue_context_t *const q_ctx,
		uint64_t recv_time)
{
	const int core = em_core_id();
	core_stat_t *const cstat = &perf_shm->core_stat[core];
	const uint64_t send_time = perf_event->send_time;
	uint64_t latency;

	if (perf_shm->test_status.reset_flag ||
	    cstat->events == 0 || cstat->events >= EVENTS_PER_SAMPLE)
		return;

	cstat->latency.events++;

	if (likely(recv_time > send_time))
		latency = recv_time - send_time;
	else
		latency = UINT64_MAX - send_time + recv_time + 1;

	if (q_ctx->prio == EM_QUEUE_PRIO_HIGH) {
		cstat->latency.hi_prio_ave += latency;
		if (latency > cstat->latency.hi_prio_max)
			cstat->latency.hi_prio_max = latency;
	} else {
		cstat->latency.lo_prio_ave += latency;
		if (latency > cstat->latency.lo_prio_max)
			cstat->latency.lo_prio_max = latency;
	}
}
