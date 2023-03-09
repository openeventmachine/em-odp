/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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
 * Event Machine em_atomic_processing_end() example
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for an EO pair using atomic queues and alternating between calling
 * em_atomic_processing_end() and not calling it.
 * Each EO's receive function will do some dummy work for each received event.
 * The em_atomic_processing_end() is called before processing the dummy work to
 * allow another core to continue processing events from the queue. Not calling
 * em_atomic_processing_end() with a low number of queues and long per-event
 * processing times will limit the throughput and the cycles/event result.
 * For comparison, both results with em_atomic_processing_end()-calling enabled
 * and disabled is shown.
 * Note: Calling em_atomic_processing_end() will normally give worse
 * performance except in cases when atomic event processing becomes a
 * bottleneck by blocking other cores from doing their work (as this test
 * tries to demonstrate).
 */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Test configuration
 */

/**
 * Number of test EOs and queues. Must be an even number.
 * Create a BOTTLENECK for atomic processing in the system by
 * keeping the number smaller than the available cores.
 */
#define NUM_EO  2  /* BOTTLENECK */

/** Number of ping-pong events per EO pair. */
#define NUM_EVENT  512

/** Number of data bytes in the event */
#define DATA_SIZE  512

/** Max number of cores */
#define MAX_NBR_OF_CORES  256

/**
 * Number of "work" iterations to do per received event to show the
 * possible benefit of em_atomic_processing_end()
 */
#define WORK_LOOPS  40  /* cause long per-event processing time */

/* The number of events to be received per core before printing results */
#define PRINT_EVENT_COUNT 0x20000

/** Define how many events are sent per em_send_multi() call */
#define SEND_MULTI_MAX 32

/*
 * Per event processing options
 */

/* Check sequence numbers, works only with atomic queues */
#define CHECK_SEQ_PER_EVENT  1 /* 0=False or 1=True */

/**
 * Performance test statistics (per core)
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		uint64_t events;
		uint64_t begin_cycles;
		uint64_t end_cycles;
		uint64_t print_count;
		int atomic_processing_end;
		int rounds;
		int ready;
		double cycles_per_event;
	};
} perf_stat_t;

COMPILE_TIME_ASSERT(sizeof(perf_stat_t) == ENV_CACHE_LINE_SIZE,
		    PERF_STAT_T_SIZE_ERROR);

/**
 * Performance test EO context
 */
typedef struct {
	/* EO context id */
	em_eo_t id;
	/* Next sequence number (used with CHECK_SEQ_PER_EVENT) */
	int next_seq;
	/* at startup: EO-A should allocate and send the test events */
	int initialize_events;
} eo_context_t;

/**
 * EO context padded to cache line size
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	eo_context_t eo_ctx;
} eo_context_array_elem_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_array_elem_t) == ENV_CACHE_LINE_SIZE,
		    PERF_EO_CONTEXT_SIZE_ERROR);

/**
 * Performance test event
 */
typedef struct {
	/* Next destination queue */
	em_queue_t dest;
	/* Sequence number */
	int seq;
	/* Test data */
	uint8_t data[DATA_SIZE];
} perf_event_t;

/**
 * Perf test shared memory
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO context array */
	eo_context_array_elem_t perf_eo_context[NUM_EO] ENV_CACHE_LINE_ALIGNED;
	/* Array of core specific data accessed by using its core index */
	perf_stat_t core_stat[MAX_NBR_OF_CORES] ENV_CACHE_LINE_ALIGNED;
	/* Track the number of cores ready with the current measurement rounds*/
	env_atomic64_t ready_count ENV_CACHE_LINE_ALIGNED;
	/* Track the number of cores that have seen that all others are ready */
	env_atomic64_t seen_all_ready;
	/* Pad to size to a multiple of cache lines */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} perf_shm_t;

/** EM-core local pointer to shared memory */
static ENV_LOCAL perf_shm_t *perf_shm;

/*
 * Local function prototypes
 */
static em_status_t
perf_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
perf_stop(void *eo_context, em_eo_t eo);

static void
initialize_events(em_queue_t queue_a, em_queue_t queue_b);

static void
perf_receive_a(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx);
static void
perf_receive_b(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx);
static void
calc_result(perf_stat_t *const perf_stat, const uint64_t events);

static void
print_result(perf_stat_t *const perf_stat);

static int
get_queue_priority(const int index);

static void
check_seq_per_event(eo_context_t *const eo_ctx, perf_event_t *const perf,
		    em_queue_t queue);
static void
do_dummy_work(unsigned int work_loops);

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
 * Init of the Pairs performance test application.
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
		perf_shm = env_shared_reserve("PerfSharedMem",
					      sizeof(perf_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		perf_shm = env_shared_lookup("PerfSharedMem");
	}

	if (perf_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Perf init failed on EM-core:%u\n", em_core_id());
	} else if (core == 0) {
		memset(perf_shm, 0, sizeof(perf_shm_t));
		env_atomic64_init(&perf_shm->ready_count);
		env_atomic64_init(&perf_shm->seen_all_ready);
	}
}

/**
 * Startup of the Pairs performance test application.
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
	em_eo_t eo;
	em_queue_t queue_a, queue_b;
	em_status_t ret, start_ret = EM_ERROR;
	eo_context_t *eo_ctx;
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
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads,
		   perf_shm->pool);

	test_fatal_if(perf_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/*
	 * Create and start application pairs
	 * Send initial test events to the queues
	 */
	for (i = 0; i < NUM_EO / 2; i++) {
		char eo_name[EM_EO_NAME_LEN];
		char queue_name[EM_QUEUE_NAME_LEN];
		em_event_t start_event;
		perf_event_t *perf;

		/* Create EO "A" */
		eo_ctx = &perf_shm->perf_eo_context[2 * i].eo_ctx;
		eo_ctx->initialize_events = 1; /* EO to init events at start */
		eo_ctx->next_seq = 0;

		snprintf(eo_name, sizeof(eo_name), "EO-A%i", i);
		eo_name[sizeof(eo_name) - 1] = '\0';
		eo = em_eo_create(eo_name, perf_start, NULL, perf_stop, NULL,
				  perf_receive_a, eo_ctx);

		snprintf(queue_name, sizeof(queue_name), "Q-A%i", i);
		queue_name[sizeof(queue_name) - 1] = '\0';
		queue_a = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC,
					  get_queue_priority(i),
					  EM_QUEUE_GROUP_DEFAULT, NULL);

		ret = em_eo_add_queue_sync(eo, queue_a);
		test_fatal_if(ret != EM_OK,
			      "EO or Q creation failed:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
			      ret, eo, queue_a);

		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO start failed:%" PRI_STAT " %" PRI_STAT "\n"
			      "EO:%" PRI_EO "", ret, start_ret, eo);

		/* Create EO "B" */
		eo_ctx = &perf_shm->perf_eo_context[2 * i + 1].eo_ctx;
		eo_ctx->next_seq = 0;

		snprintf(eo_name, sizeof(eo_name), "EO-B%i", i);
		eo_name[sizeof(eo_name) - 1] = '\0';
		eo = em_eo_create(eo_name, perf_start, NULL, perf_stop, NULL,
				  perf_receive_b, eo_ctx);

		snprintf(queue_name, sizeof(queue_name), "Q-B%i", i);
		queue_name[sizeof(queue_name) - 1] = '\0';
		queue_b = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC,
					  get_queue_priority(i),
					  EM_QUEUE_GROUP_DEFAULT, NULL);

		ret = em_eo_add_queue_sync(eo, queue_b);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
			      ret, eo, queue_b);

		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK  || start_ret != EM_OK,
			      "EO start failed:%" PRI_STAT " %" PRI_STAT "\n"
			      "EO: %" PRI_EO "", ret, start_ret, eo);

		start_event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW,
				       perf_shm->pool);
		test_fatal_if(start_event == EM_EVENT_UNDEF,
			      "Start event alloc failed");
		perf = em_event_pointer(start_event);
		perf->seq = 0;
		perf->dest = queue_b; /* EO-A sends to queue-B */

		ret = em_send(start_event, queue_a);
		test_fatal_if(ret != EM_OK,
			      "Start event send:%" PRI_STAT "\n"
			      "Queue:%" PRI_QUEUE "",
			      ret, queue_a);
	}
	env_sync_mem();
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

	/* Stop all EOs to disable dipatch from all the EOs' queues */
	for (i = 0; i < NUM_EO; i++) {
		eo = perf_shm->perf_eo_context[i].eo_ctx.id;

		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	}

	for (i = 0; i < NUM_EO; i++) {
		eo = perf_shm->perf_eo_context[i].eo_ctx.id;

		/* remove and delete all of the EO's queues */
		ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
		test_fatal_if(ret != EM_OK,
			      "EO rem-Q-all-sync:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo);

		ret = em_eo_delete(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
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
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
perf_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_context_t *eo_ctx = eo_context;
	char eo_name[EM_EO_NAME_LEN];

	(void)conf;

	em_eo_get_name(eo, eo_name, sizeof(eo_name));
	APPL_PRINT("%s (id:%" PRI_EO ") starting.\n", eo_name, eo);

	eo_ctx->id = eo;

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t
perf_stop(void *eo_context, em_eo_t eo)
{
	char eo_name[EM_EO_NAME_LEN];

	(void)eo_context;

	em_eo_get_name(eo, eo_name, sizeof(eo_name));
	APPL_PRINT("%s (id:%" PRI_EO ") stopping.\n", eo_name, eo);

	return EM_OK;
}

static void
initialize_events(em_queue_t queue_a, em_queue_t queue_b)
{
	/* tmp storage for allocated events to send */
	em_event_t events[NUM_EVENT];
	int i;

	for (i = 0; i < NUM_EVENT; i++) {
		perf_event_t *perf;

		events[i] = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW,
				     perf_shm->pool);
		test_fatal_if(events[i] == EM_EVENT_UNDEF,
			      "Event alloc failed (%d)", i);
		perf = em_event_pointer(events[i]);
		perf->seq = i;
		perf->dest = queue_b; /* EO-A sends to queue-B */
	}

	/*
	 * Send the test events first to EO-A's queue-A.
	 * Send in bursts of 'SEND_MULTI_MAX' events.
	 */
	const int send_rounds = NUM_EVENT / SEND_MULTI_MAX;
	const int left_over = NUM_EVENT % SEND_MULTI_MAX;
	int num_sent = 0;

	for (i = 0; i < send_rounds; i++) {
		num_sent += em_send_multi(&events[num_sent], SEND_MULTI_MAX,
					  queue_a);
	}
	if (left_over) {
		num_sent += em_send_multi(&events[num_sent], left_over,
					  queue_a);
	}
	if (unlikely(num_sent != NUM_EVENT)) {
		test_fatal_if(!appl_shm->exit_flag,
			      "Event send multi failed:%d (%d)\n"
			      "Q:%" PRI_QUEUE "",
			      num_sent, NUM_EVENT, queue_a);
		for (i = num_sent; i < NUM_EVENT; i++)
			em_free(events[i]);
	}
}

/**
 * @private
 *
 * EO receive function for EO A.
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive_a(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx)
{
	const int core = em_core_id();
	perf_event_t *const perf = em_event_pointer(event);
	uint64_t events = perf_shm->core_stat[core].events;
	int call_atomic_processing_end =
		perf_shm->core_stat[core].atomic_processing_end;
	int ready = perf_shm->core_stat[core].ready;
	uint64_t ready_count;
	em_queue_t dest_queue;
	em_status_t ret;

	(void)type;
	(void)q_ctx;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		eo_context_t *const eo_ctx = eo_context;

		if (unlikely(eo_ctx->initialize_events)) {
			/* start-up: initialize the perf event sending */
			eo_ctx->initialize_events = 0;
			initialize_events(queue, perf->dest);
			em_free(event);
			return;
		}
		perf_shm->core_stat[core].begin_cycles = env_get_cycle();
	} else if (unlikely(!ready && events > PRINT_EVENT_COUNT)) {
		/* Measurement done, collect cycle count */
		perf_shm->core_stat[core].end_cycles = env_get_cycle();
		/*
		 * Three measurement rounds: calculate results only for the
		 * middle round.
		 * Trigger core-sync after the last round to have all cores
		 * in the same mode for the next three rounds.
		 */
		int rounds = perf_shm->core_stat[core].rounds++;

		if (rounds % 3 == 1) {
			/* Calculate results for middle round */
			calc_result(&perf_shm->core_stat[core], events);
		} else if (rounds % 3 == 2) {
			/* Print earlier calculated results after last round */
			print_result(&perf_shm->core_stat[core]);
			/* Mark that the core is ready with all rounds */
			ready = 1;
			perf_shm->core_stat[core].ready = 1;
			env_atomic64_inc(&perf_shm->ready_count);
		}
	}

	events++;

	if (CHECK_SEQ_PER_EVENT)
		check_seq_per_event(eo_context, perf, queue);

	dest_queue = perf->dest;
	perf->dest = queue;

	perf_shm->core_stat[core].events = events;

	ret = em_send(event, dest_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, dest_queue);
		return;
	}

	if (call_atomic_processing_end)
		em_atomic_processing_end();

	if (unlikely(ready)) {
		/* core ready with rounds, check if other cores are also ready*/
		ready_count = env_atomic64_get(&perf_shm->ready_count);

		if (ready_count == (uint64_t)em_core_count()) {
			/* Change mode after last round */
			perf_shm->core_stat[core].atomic_processing_end =
				!call_atomic_processing_end;
			perf_shm->core_stat[core].ready = 0;
			events = 0;
			perf_shm->core_stat[core].events = 0;

			/* Track that all cores have seen that all are ready */
			uint64_t seen_all_ready =
			env_atomic64_add_return(&perf_shm->seen_all_ready, 1);

			/* Last core to see 'all ready' resets the counters */
			if (seen_all_ready == (uint64_t)em_core_count()) {
				env_atomic64_set(&perf_shm->ready_count, 0);
				env_atomic64_set(&perf_shm->seen_all_ready, 0);
			}
		}
	}

	/*
	 * Give a hint to the scheduler indicating that event
	 * processing on this core will soon be finished and the
	 * scheduler could start preparing the next event for this
	 * core already now to reduce latency etc. The em_preschedule()
	 * call might only be meaningful with HW schedulers.
	 */
	em_preschedule();

	/* Do some dummy processing */
	do_dummy_work(WORK_LOOPS);
}

/**
 * @private
 *
 * EO receive function for EO B.
 *
 * Loops back events.
 */
static void
perf_receive_b(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx)
{
	const int core = em_core_id();
	perf_event_t *const perf = em_event_pointer(event);
	const int call_atomic_processing_end =
		perf_shm->core_stat[core].atomic_processing_end;
	uint64_t events = perf_shm->core_stat[core].events;
	em_queue_t dest_queue;
	em_status_t ret;
	(void)type;
	(void)q_ctx;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		/* Restart the measurement */
		perf_shm->core_stat[core].begin_cycles = env_get_cycle();
	}

	events++;

	if (CHECK_SEQ_PER_EVENT)
		check_seq_per_event(eo_context, perf, queue);

	dest_queue = perf->dest;
	perf->dest = queue;

	perf_shm->core_stat[core].events = events;

	ret = em_send(event, dest_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, dest_queue);
		return;
	}

	if (call_atomic_processing_end)
		em_atomic_processing_end();

	/*
	 * Give a hint to the scheduler indicating that event processing on
	 * this core will soon be finished and the scheduler could start
	 * preparing the next event for this core already now to reduce
	 * latency etc. The em_preschedule() call might only be meaningful
	 * with HW schedulers.
	 */
	em_preschedule();

	/* Do some dummy processing */
	do_dummy_work(WORK_LOOPS);
}

static void
check_seq_per_event(eo_context_t *const eo_ctx, perf_event_t *const perf,
		    em_queue_t queue)
{
	int seq = perf->seq;

	if (unlikely(seq != eo_ctx->next_seq)) {
		char eo_name[EM_EO_NAME_LEN];
		char queue_name[EM_QUEUE_NAME_LEN];

		em_eo_get_name(eo_ctx->id, eo_name, sizeof(eo_name));
		em_queue_get_name(queue, queue_name, sizeof(queue_name));

		APPL_PRINT("Bad sequence number. %s(id:%" PRI_EO "),\t"
			   "%s(id:%" PRI_QUEUE ") expected seq %i, event seq %i\n",
			   eo_name, eo_ctx->id, queue_name, queue,
			   eo_ctx->next_seq, seq);
	}

	if (likely(eo_ctx->next_seq < (NUM_EVENT - 1)))
		eo_ctx->next_seq++;
	else
		eo_ctx->next_seq = 0;
}

static void
do_dummy_work(unsigned int work_loops)
{
	em_event_t workbuf_event;
	perf_event_t *workbuf;
	uint8_t *from, *to;
	unsigned int i;

	for (i = 0; i < work_loops && !appl_shm->exit_flag; i++) {
		/* Dummy workload after releasing atomic context */
		workbuf_event = em_alloc(sizeof(perf_event_t),
					 EM_EVENT_TYPE_SW, perf_shm->pool);
		test_fatal_if(workbuf_event == EM_EVENT_UNDEF,
			      "em_alloc(pool:%" PRI_POOL ") of buf:%u of tot:%u failed!",
			      perf_shm->pool, i, work_loops);
		workbuf = em_event_pointer(workbuf_event);
		from = &workbuf->data[DATA_SIZE / 2];
		to = &workbuf->data[0];
		memcpy(to, from, DATA_SIZE / 2);
		em_free(workbuf_event);
	}
}

/**
 * Prints test measurement result
 */
static void
calc_result(perf_stat_t *const perf_stat, const uint64_t events)
{
	uint64_t diff;
	double cycles_per_event;

	diff = env_cycles_diff(perf_stat->end_cycles, perf_stat->begin_cycles);

	cycles_per_event = ((double)diff) / ((double)events);

	perf_stat->cycles_per_event = cycles_per_event;
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
static int
get_queue_priority(const int queue_index)
{
	int remainder = queue_index % 5;

	if (remainder <= 1)
		return EM_QUEUE_PRIO_LOW;
	else if (remainder <= 3)
		return EM_QUEUE_PRIO_NORMAL;
	else
		return EM_QUEUE_PRIO_HIGH;
}

static void
print_result(perf_stat_t *const perf_stat)
{
	const uint32_t hz = env_core_hz();
	const double mhz = ((double)hz) / 1000000.0;
	const double cycles_per_event = perf_stat->cycles_per_event;
	const double events_per_sec = mhz * em_core_count() /
				      cycles_per_event; /* Million events/s*/
	const uint64_t print_count = perf_stat->print_count++;

	if (perf_stat->atomic_processing_end) {
		APPL_PRINT("em_atomic_processing_end():%10.0f cycles/event\t"
			   "events/s:%.2f M  @%.2f MHz (core-%02i %" PRIu64 ")\n",
			   cycles_per_event, events_per_sec, mhz,
			   em_core_id(), print_count);
	} else {
		APPL_PRINT("normal atomic processing:%12.0f cycles/event\t"
			   "events/s:%.2f M  @%.2f MHz (core-%02i %" PRIu64 ")\n",
			   cycles_per_event, events_per_sec,
			   mhz, em_core_id(), print_count);
	}
}
