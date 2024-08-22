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
 * Event Machine performance test example using event vectors.
 *
 * Test based on the 'loop' test but changed to use event vectors instead of
 * separate events.
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of EOs in the system. The test has a number of EOs, each
 * with one queue. Each EO receives events through its dedicated queue and
 * sends them right back into the same queue, thus looping the events.
 *
 * Based on the 'pairs' performance test, but instead of forwarding events
 * between queues, here we loop them back into the same queue (which is usually
 * faster). Also 'loop' only uses one queue priority level.
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

#define MAX_VECTOR_SIZE 8

/*
 * Options
 */

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
 * Performance test event
 */
typedef struct {
	uint8_t data[DATA_SIZE];
} perf_event_t;

/**
 * Perf test shared memory, read-only after start-up, allow cache-line sharing
 */
typedef struct {
	/* EO table */
	em_eo_t eo_tbl[NUM_EO];
	/* Event pool used by this application */
	em_pool_t pool;
	/* Vector pool used by this application */
	em_pool_t vec_pool;
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

	em_pool_cfg_t vec_pool_cfg;
	em_pool_t vec_pool = EM_POOL_UNDEF;

	em_pool_cfg_init(&vec_pool_cfg);
	vec_pool_cfg.event_type = EM_EVENT_TYPE_VECTOR;
	vec_pool_cfg.num_subpools = 1;
	if (ALLOC_FREE_PER_EVENT)
		vec_pool_cfg.subpool[0].cache_size = 32;
	else
		vec_pool_cfg.subpool[0].cache_size = 0; /* all allocated in startup */
	vec_pool_cfg.subpool[0].num = NUM_EO * NUM_EVENT_PER_QUEUE * 2;
	vec_pool_cfg.subpool[0].size = MAX_VECTOR_SIZE;

	vec_pool = em_pool_create("vector-pool", EM_POOL_UNDEF, &vec_pool_cfg);
	test_fatal_if(vec_pool == EM_POOL_UNDEF, "vector pool create failed!");

	perf_shm->vec_pool = vec_pool;

	/*
	 * Create and start application EOs
	 * Send initial test events to the EOs' queues
	 */
	em_queue_t queues[NUM_EO];

	for (int i = 0; i < NUM_EO; i++) {
		em_queue_t queue;
		em_eo_t eo;
		em_status_t ret, start_ret = EM_ERROR;

		/* Create the EO's loop queue */
		queue = em_queue_create("queue A", QUEUE_TYPE,
					EM_QUEUE_PRIO_NORMAL,
					EM_QUEUE_GROUP_DEFAULT, NULL);
		test_fatal_if(queue == EM_QUEUE_UNDEF,
			      "Queue creation failed, round:%d", i);
		queues[i] = queue;

		/* Create the EO */
		eo = em_eo_create("loop-eo", perf_start, NULL, perf_stop, NULL,
				  perf_receive, NULL);
		test_fatal_if(eo == EM_EO_UNDEF,
			      "EO(%d) creation failed!", i);
		perf_shm->eo_tbl[i] = eo;

		ret = em_eo_add_queue_sync(eo, queue);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      ret, eo, queue);

		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO start:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);
	}

	for (int i = 0; i < NUM_EO; i++) {
		em_queue_t queue = queues[i];
		em_event_t vectors[NUM_EVENT_PER_QUEUE];

		/* Alloc and send test vectors */
		for (int j = 0; j < NUM_EVENT_PER_QUEUE; j++) {
			uint32_t vec_sz = (j % MAX_VECTOR_SIZE) + 1;
			em_event_t vec = em_alloc(vec_sz, EM_EVENT_TYPE_VECTOR,
						  vec_pool);
			test_fatal_if(vec == EM_EVENT_UNDEF,
				      "Vector allocation failed (%d, %d)", i, j);
			em_event_t *vectbl = NULL;
			uint32_t curr_sz = em_event_vector_tbl(vec, &vectbl);

			test_fatal_if(curr_sz || !vectbl,
				      "Vector table invalid: sz=%d vectbl=%p)",
				      curr_sz, vectbl);

			for (uint32_t k = 0; k < vec_sz; k++) {
				em_event_t ev = em_alloc(sizeof(perf_event_t),
							 EM_EVENT_TYPE_SW, perf_shm->pool);
				test_fatal_if(ev == EM_EVENT_UNDEF,
					      "Event allocation failed (%d, %d)", i, j);
				vectbl[k] = ev;
			}
			em_event_vector_size_set(vec, vec_sz);

			vectors[j] = vec;
		}

		/* Send in bursts of 'SEND_MULTI_MAX' vectors */
		const int send_rounds = NUM_EVENT_PER_QUEUE / SEND_MULTI_MAX;
		const int left_over = NUM_EVENT_PER_QUEUE % SEND_MULTI_MAX;
		int num_sent = 0;
		int m, n;

		for (m = 0, n = 0; m < send_rounds; m++, n += SEND_MULTI_MAX) {
			num_sent += em_send_multi(&vectors[n], SEND_MULTI_MAX,
						  queue);
		}
		if (left_over) {
			num_sent += em_send_multi(&vectors[n], left_over,
					  queue);
		}
		test_fatal_if(num_sent != NUM_EVENT_PER_QUEUE,
			      "Event send multi failed:%d (%d)\n"
			      "Q:%" PRI_QUEUE "",
			      num_sent, NUM_EVENT_PER_QUEUE, queue);
	}

	env_sync_mem();
}

void test_stop(const appl_conf_t *appl_conf)
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
	em_status_t ret;

	(void)eo_context;
	(void)type;
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
		if (em_event_type_major(type) == EM_EVENT_TYPE_VECTOR) {
			em_pool_t vec_pool = perf_shm->vec_pool;

			em_event_t *vectbl = NULL;
			uint32_t vec_sz = em_event_vector_tbl(event, &vectbl);

			test_fatal_if(!vec_sz || !vectbl,
				      "Vector table invalid: sz=%d vectbl=%p)",
				      vec_sz, vectbl);

			em_event_t vec = em_alloc(vec_sz, EM_EVENT_TYPE_VECTOR, vec_pool);

			test_fatal_if(vec == EM_EVENT_UNDEF,
				      "Vector allocation failed, sz=%u, events=%" PRIi64 "",
				      vec_sz, events + 1);

			em_event_t *vectbl_new = NULL;
			uint32_t sz_new = em_event_vector_tbl(vec, &vectbl_new);

			test_fatal_if(sz_new || !vectbl_new,
				      "Vector table invalid: sz=%d vectbl=%p)",
				      sz_new, vectbl_new);

			for (uint32_t i = 0; i < vec_sz; i++)
				vectbl_new[i] = vectbl[i];

			em_event_vector_size_set(vec, vec_sz);

			em_event_vector_free(event);
			event = vec;
		} else {
			em_pool_t pool = perf_shm->pool;

			em_free(event);
			event = em_alloc(sizeof(perf_event_t), type, pool);
			test_fatal_if(event == EM_EVENT_UNDEF, "Event alloc fails");
		}
	}

	/* Send the event back into the queue it originated from, i.e. loop */
	ret = em_send(event, queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, queue);
	}

	events++;
	core_stat.events = events;
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
