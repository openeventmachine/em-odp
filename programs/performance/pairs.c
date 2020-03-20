/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2019, Nokia Solutions and Networks
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
 * Event Machine performance test example
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of EO pairs in the system. Test has a number of EO
 * pairs, that send ping-pong events. Depending on test dynamics (e.g. single
 * burst in atomic queue) only one EO of a pair might be active at a time.
 *
 * Uses three different queue priority levels that affect scheduling (might
 * starve low prio queues if using a strict prio scheduler).
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

/** Number of test EOs and queues. Must be an even number. */
#define NUM_EO  128

/** Number of events per queue */
#define NUM_EVENT_PER_QUEUE  32  /* Increase the value to tune performance */

/** sizeof data[DATA_SIZE] in bytes in the event payload */
#define DATA_SIZE  250

/** Max number of cores */
#define MAX_NBR_OF_CORES  256

/** The number of events to be received before printing a result */
#define PRINT_EVENT_COUNT  0xff0000

/** EM Queue type used */
#define QUEUE_TYPE  EM_QUEUE_TYPE_ATOMIC

/** Define how many events are sent per em_send_multi() call */
#define SEND_MULTI_MAX 32

/*
 * Options
 */

/** Use different priority levels for the queues created */
#define USE_DIFF_QUEUE_PRIO_LEVELS  0

/** Alloc and free per event */
#define ALLOC_FREE_PER_EVENT  0 /* 0=False or 1=True */

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
 * Performance test event
 */
typedef struct {
	uint8_t data[DATA_SIZE];
} perf_event_t;

/**
 * Perf test shared memory, read-only after start-up, allow cache-line sharing
 */
typedef struct {
	/* EO context table */
	eo_context_t eo_ctx_tbl[NUM_EO];
	/* EO table */
	em_eo_t eo_tbl[NUM_EO];
	/* Event pool used by this application */
	em_pool_t pool;
} perf_shm_t;

/** EM-core local pointer to shared memory */
static ENV_LOCAL perf_shm_t *perf_shm;
/**
 * Core specific test statistics.
 *
 * Allow for 'PRINT_EVENT_COUNT' warm-up rounds,
 * incremented per core during receive, measurement starts at 0.
 */
static ENV_LOCAL perf_stat_t core_stat = {.events = -PRINT_EVENT_COUNT};

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
print_result(perf_stat_t *const perf_stat);

static em_queue_prio_t
get_queue_priority(const int index);

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

	if (perf_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Perf init failed on EM-core:%u", em_core_id());
	else if (core == 0)
		memset(perf_shm, 0, sizeof(perf_shm_t));
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
	 * Create and start application pairs.
	 * Send initial test events to the queues.
	 */
	em_queue_t queues_a[NUM_EO / 2];
	em_queue_t queues_b[NUM_EO / 2];

	for (int i = 0; i < NUM_EO / 2; i++) {
		em_queue_t queue_a, queue_b;
		eo_context_t *eo_ctx_a, *eo_ctx_b;
		em_eo_t eo;
		em_status_t ret, start_ret = EM_OK;

		/* Create both queues for the pair */
		queue_a = em_queue_create("queue-A", QUEUE_TYPE,
					  get_queue_priority(i),
					  EM_QUEUE_GROUP_DEFAULT, NULL);
		queue_b = em_queue_create("queue-B", QUEUE_TYPE,
					  get_queue_priority(i),
					  EM_QUEUE_GROUP_DEFAULT, NULL);
		test_fatal_if(queue_a == EM_QUEUE_UNDEF ||
			      queue_b == EM_QUEUE_UNDEF,
			      "Queue creation failed, round:%d", i);
		queues_a[i] = queue_a;
		queues_b[i] = queue_b;

		/* Create EO "A" */
		eo_ctx_a = &perf_shm->eo_ctx_tbl[2 * i];

		eo = em_eo_create("pairs-eo-a", perf_start, NULL, perf_stop,
				  NULL, perf_receive, eo_ctx_a);
		test_fatal_if(eo == EM_EO_UNDEF,
			      "EO(%d) creation failed!", 2 * i);
		perf_shm->eo_tbl[2 * i] = eo;
		eo_ctx_a->dest = queue_b;

		ret = em_eo_add_queue_sync(eo, queue_a);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      ret, eo, queue_a);

		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO start:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);

		/* Create EO "B" */
		eo_ctx_b = &perf_shm->eo_ctx_tbl[2 * i + 1];

		eo = em_eo_create("pairs-eo-b", perf_start, NULL, perf_stop,
				  NULL, perf_receive, eo_ctx_b);
		test_fatal_if(eo == EM_EO_UNDEF,
			      "EO(%d) creation failed!", 2 * i + 1);
		perf_shm->eo_tbl[2 * i + 1] = eo;
		eo_ctx_b->dest = queue_a;

		ret = em_eo_add_queue_sync(eo, queue_b);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
			      ret, eo, queue_b);

		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO start:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);
	}

	for (int i = 0; i < NUM_EO / 2; i++) {
		em_queue_t queue_a = queues_a[i];
		em_queue_t queue_b = queues_b[i];
		em_event_t events_a[NUM_EVENT_PER_QUEUE];
		em_event_t events_b[NUM_EVENT_PER_QUEUE];

		/* Alloc and send test events */
		for (int j = 0; j < NUM_EVENT_PER_QUEUE; j++) {
			em_event_t ev_a, ev_b;

			ev_a = em_alloc(sizeof(perf_event_t),
					EM_EVENT_TYPE_SW, perf_shm->pool);
			ev_b = em_alloc(sizeof(perf_event_t),
					EM_EVENT_TYPE_SW, perf_shm->pool);
			test_fatal_if(ev_a == EM_EVENT_UNDEF ||
				      ev_b == EM_EVENT_UNDEF,
				      "Event allocation failed (%d, %d)", i, j);
			events_a[j] = ev_a;
			events_b[j] = ev_b;
		}

		/* Send in bursts of 'SEND_MULTI_MAX' events */
		const int send_rounds = NUM_EVENT_PER_QUEUE / SEND_MULTI_MAX;
		const int left_over = NUM_EVENT_PER_QUEUE % SEND_MULTI_MAX;
		int num_sent = 0;
		int m, n;

		for (m = 0, n = 0; m < send_rounds; m++, n += SEND_MULTI_MAX) {
			num_sent += em_send_multi(&events_a[n], SEND_MULTI_MAX,
						  queue_a);
		}
		if (left_over) {
			num_sent += em_send_multi(&events_a[n], left_over,
					  queue_a);
		}
		test_fatal_if(num_sent != NUM_EVENT_PER_QUEUE,
			      "Event send multi failed:%d (%d)\n"
			      "Q:%" PRI_QUEUE "",
			      num_sent, NUM_EVENT_PER_QUEUE, queue_a);

		num_sent = 0;
		for (m = 0, n = 0; m < send_rounds; m++, n += SEND_MULTI_MAX) {
			num_sent += em_send_multi(&events_b[n], SEND_MULTI_MAX,
						  queue_b);
		}
		if (left_over) {
			num_sent += em_send_multi(&events_b[n], left_over,
					  queue_b);
		}
		test_fatal_if(num_sent != NUM_EVENT_PER_QUEUE,
			      "Event send multi failed:%d (%d)\n"
			      "Q:%" PRI_QUEUE "",
			      num_sent, NUM_EVENT_PER_QUEUE, queue_b);
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

	for (i = 0; i < NUM_EO; i++) {
		/* Stop & delete EO */
		eo = perf_shm->eo_tbl[i];

		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);

		ret = em_eo_delete(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
	}
}

void
test_term(void)
{
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
 *
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
 *
 */
static em_status_t
perf_stop(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);
	return ret;
}

/**
 * @private
 *
 * EO receive function for EO A.
 *
 * Loops back events and calculates the event rate.
 */
static void
perf_receive(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *queue_context)
{
	int64_t events = core_stat.events;
	eo_context_t *const eo_ctx = eo_context;
	const em_queue_t dst_queue = eo_ctx->dest;
	em_status_t ret;

	(void)type;
	(void)queue;
	(void)queue_context;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(events == 0)) {
		/* Start the measurement */
		core_stat.begin_cycles = env_get_cycle();
	} else if (unlikely(events == PRINT_EVENT_COUNT)) {
		/* End the measurement */
		core_stat.end_cycles = env_get_cycle();
		/* Print results and restart */
		core_stat.print_count += 1;
		print_result(&core_stat);
		/* Restart the measurement next round */
		events = -1; /* +1 below => 0 */
	}

	if (ALLOC_FREE_PER_EVENT) {
		em_free(event);
		event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW,
				 perf_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF, "Event alloc fails");
	}

	/* Send the event into the next queue */
	ret = em_send(event, dst_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, dst_queue);
	}

	events++;
	core_stat.events = events;
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

	if (USE_DIFF_QUEUE_PRIO_LEVELS) {
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

	if (perf_stat->end_cycles > perf_stat->begin_cycles)
		diff = perf_stat->end_cycles - perf_stat->begin_cycles;
	else
		diff = UINT64_MAX - perf_stat->begin_cycles +
			perf_stat->end_cycles + 1;

	print_count = perf_stat->print_count;
	cycles_per_event = ((double)diff) / ((double)perf_stat->events);
	events_per_sec = mhz / cycles_per_event; /* Million events/s */

	APPL_PRINT(RESULT_PRINTF_FMT, cycles_per_event, events_per_sec,
		   mhz, em_core_id(), print_count);
}
