/*
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
 * Event Machine queue group performance test.
 *
 * Measures the average cycles consumed during an event send-sched-receive loop
 * for a certain number of queue groups in the system. The test increases the
 * number of groups for each measurement round and prints the results. The test
 * runs until the maximum number of supported queue groups is reached.
 *
 * Plot the cycles/event to get an idea of how the system scales with an
 * increasing number of queue groups.
 *
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

/** Number of queue groups to start test with */
#define MIN_NUM_QUEUE_GROUPS  1

/** Number of groups to add on every test step */
#define QUEUE_GROUP_STEP_SIZE  1

/** Number of test queues  */
#define NUM_QUEUES  256

/** Total number of test events */
#define NUM_EVENTS  (NUM_QUEUES * 10)

/** Maximum number of supported cores */
#define MAX_CORES  64

/** Number of data bytes in an event */
#define DATA_SIZE  128

/** Samples before adding more groups */
#define NUM_SAMPLES  8

/** Number of events a core processes between samples */
#define EVENTS_PER_SAMPLE  0x400000

/** Number of events to recv before checking the 'ready'-status of the test-step */
#define EVENTS_CHECK_READY_MASK  0xffff

/**
 * Measure the send-enqueue-schedule-receive latency. Measured separately
 * for 'high priority' and 'low priority' queues (ratio 1:4).
 */
#define MEASURE_LATENCY  1 /* 0=False or 1=True */

/** Core states during test. */
#define CORE_STATE_MEASURE  0
#define CORE_STATE_IDLE     1

typedef union {
	uint8_t u8[2 * ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		int num_qgrp; /* Number of queue groups created */
		int samples;
		int num_cores;
		int free_flag;
		int reset_flag;
		double mhz;
		uint64_t cpu_hz;
		uint64_t print_count;
		env_atomic64_t ready_count;
		env_atomic64_t freed_count;
	};
} test_status_t;

COMPILE_TIME_ASSERT((sizeof(test_status_t) % ENV_CACHE_LINE_SIZE) == 0,
		    TEST_STATUS_T__SIZE_ERROR);

/**
 * Performance test statistics (per core)
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		uint64_t num_events;
		env_time_t begin_time;
		env_time_t end_time;
		env_time_t diff_time;
		env_time_t latency_hi;
		env_time_t latency_lo;
	};
} core_stat_t;

COMPILE_TIME_ASSERT((sizeof(core_stat_t) % ENV_CACHE_LINE_SIZE) == 0,
		    CORE_STAT_SIZE_ERROR);

/**
 * EO context data
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		em_eo_t eo_id;
		em_event_group_t event_group;
	};
} eo_context_t;

COMPILE_TIME_ASSERT((sizeof(eo_context_t) % ENV_CACHE_LINE_SIZE) == 0,
		    EO_CONTEXT_T__SIZE_ERROR);

/**
 * Queue context data
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		em_queue_t this_queue;
		em_queue_t next_queue;
		em_queue_prio_t prio;
	};
} queue_context_t;

COMPILE_TIME_ASSERT(sizeof(queue_context_t) == ENV_CACHE_LINE_SIZE,
		    QUEUE_CONTEXT_SIZE_ERROR);

/**
 * Performance test event
 */
typedef struct {
	env_time_t send_time;
	enum {
		PERF_EVENT,
		NOTIF_QUEUE_GROUP_CREATED,
		NOTIF_ALL_QUEUE_GROUPS_CREATED
	} type;
	/* Sequence number */
	int seq;
	union {
		/* Queue Group name for NOTIF_QUEUE_GROUP_CREATED */
		char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
		/* Test data */
		uint8_t data[DATA_SIZE];
	};
} perf_event_t;

/**
 * Test shared memory
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO id */
	em_eo_t eo;

	test_status_t test_status ENV_CACHE_LINE_ALIGNED;

	core_stat_t core_stat[MAX_CORES] ENV_CACHE_LINE_ALIGNED;

	eo_context_t eo_context ENV_CACHE_LINE_ALIGNED;

	em_queue_t notif_queue ENV_CACHE_LINE_ALIGNED;

	em_queue_t queue_tbl[NUM_QUEUES] ENV_CACHE_LINE_ALIGNED;

	queue_context_t queue_context_tbl[NUM_QUEUES] ENV_CACHE_LINE_ALIGNED;

	em_queue_group_t queue_group_tbl[EM_MAX_QUEUE_GROUPS]
				ENV_CACHE_LINE_ALIGNED;
} perf_shm_t;

COMPILE_TIME_ASSERT((sizeof(perf_shm_t) % ENV_CACHE_LINE_SIZE) == 0,
		    PERF_SHM_T__SIZE_ERROR);

/* EM-core local pointer to shared memory */
static ENV_LOCAL perf_shm_t *perf_shm;

/* EM-core local state */
static ENV_LOCAL int core_state = CORE_STATE_MEASURE;

/*
 * Local Function Prototypes
 */

static em_status_t
error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);

static void
init_queue_groups(int count);

static void
test_step(void);

static void
next_test_step(void);

static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
stop(void *eo_context, em_eo_t eo);

static void
receive_func(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_context);

static void
receive_notif_queue_group_created(perf_event_t *perf, em_event_t event);

static void
receive_notif_all_queue_groups_created(em_event_t event);

static void
receive_perf_event(env_time_t recv_time, em_event_t event,
		   perf_event_t *perf, em_queue_t queue, void *q_context);

static inline void
update_latency(env_time_t recv_time, queue_context_t *q_ctx, perf_event_t *perf,
	       core_stat_t *const cstat);

static void
print_sample_statistics(test_status_t *const tstat);

static void
create_and_link_queues(int num_queues);

static int
unschedule_and_delete_queues(int num_queues);

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
 *                                        (format), ## __VA_ARGS__)
 *
 * @return The original error code.
 */
em_status_t
error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
	if (error == EM_ERR_ALLOC_FAILED &&
	    escope == EM_ESCOPE_QUEUE_GROUP_CREATE) {
		APPL_PRINT("\nNo more free queue groups left\n"
			   "\nTest finished\n\n");
		return error;
	}

	if (appl_shm->exit_flag && EM_ESCOPE(escope) &&
	    !EM_ERROR_IS_FATAL(error)) {
		/* Suppress non-fatal EM-error logs during tear-down */
		if (escope == EM_ESCOPE_EO_ADD_QUEUE_SYNC ||
		    escope == EM_ESCOPE_EO_REMOVE_QUEUE_SYNC ||
		    escope == EM_ESCOPE_EO_REMOVE_QUEUE_SYNC_DONE_CB ||
		    escope == EM_ESCOPE_QUEUE_DELETE) {
			APPL_PRINT("\nExit: suppress queue setup error\n\n");
			return error;
		}
	}

	return test_error_handler(eo, error, escope, args);
}

/**
 * Init of the Queue Groups performance test application.
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
		perf_shm = env_shared_reserve("PerfGroupsSharedMem",
					      sizeof(perf_shm_t));
		em_register_error_handler(error_handler);
	} else {
		perf_shm = env_shared_lookup("PerfGroupsSharedMem");
	}

	if (perf_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Perf test groups init failed on EM-core: %u",
			   em_core_id());
	else if (core == 0)
		memset(perf_shm, 0, sizeof(perf_shm_t));
}

/**
 * Startup of the Queue Groups performance test application.
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
	em_status_t ret, start_fn_ret = EM_ERROR;
	em_queue_t notif_queue;
	int q_ctx_size = NUM_QUEUES * sizeof(queue_context_t);

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
		   "    Max. test queue groups:   %i\n"
		   "    sizeof queue_context_tbl: %i kB\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads,
		   perf_shm->pool, EM_MAX_QUEUE_GROUPS - 1,
		   q_ctx_size / 1024);

	test_fatal_if(perf_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	perf_shm->test_status.cpu_hz = env_core_hz();
	perf_shm->test_status.mhz = ((double)perf_shm->test_status.cpu_hz) /
				    1000000.0;
	perf_shm->test_status.reset_flag = 0;
	perf_shm->test_status.num_cores = em_core_count();
	perf_shm->test_status.num_qgrp = 0;

	env_atomic64_init(&perf_shm->test_status.ready_count);
	env_atomic64_init(&perf_shm->test_status.freed_count);

	/* Create EO */
	eo_ctx = &perf_shm->eo_context;
	perf_shm->eo = em_eo_create("perf test eo", start, NULL, stop, NULL,
				    receive_func, eo_ctx);

	/* Create and link notification queue */
	notif_queue = em_queue_create("notif_queue", EM_QUEUE_TYPE_ATOMIC,
				      EM_QUEUE_PRIO_NORMAL,
				      EM_QUEUE_GROUP_DEFAULT, NULL);
	ret = em_eo_add_queue_sync(perf_shm->eo, notif_queue);
	test_fatal_if(ret != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      ret, perf_shm->eo, notif_queue);

	perf_shm->notif_queue = notif_queue;

	/* Start EO */
	ret = em_eo_start_sync(perf_shm->eo, &start_fn_ret, NULL);
	test_fatal_if(ret != EM_OK || start_fn_ret != EM_OK,
		      "EO start:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, perf_shm->eo);

	env_sync_mem();

	init_queue_groups(MIN_NUM_QUEUE_GROUPS);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo = perf_shm->eo;
	em_status_t ret;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
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
 * Initialize queue groups for the test.
 *
 * @param count  Queue group count
 */
static void
init_queue_groups(int count)
{
	em_core_mask_t mask;
	em_event_t event;
	em_notif_t notif_tbl[1];
	em_queue_group_t queue_group;
	em_status_t stat;
	perf_event_t *notif_event;
	int i, ret, num_qgrp;
	size_t nlen;

	num_qgrp = perf_shm->test_status.num_qgrp;

	APPL_PRINT("\nCreating %d new queue group(s)\n", count);

	em_core_mask_zero(&mask);
	em_core_mask_set_count(perf_shm->test_status.num_cores, &mask);

	/* Event group to detect when all queue groups have been created */
	perf_shm->eo_context.event_group = em_event_group_create();
	test_fatal_if(perf_shm->eo_context.event_group ==
		      EM_EVENT_GROUP_UNDEF,
		      "Creating event group failed!");

	/* Event for notifying that all queue groups have been created */
	event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW,
			 perf_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF,
		      "Event allocation failed!");
	notif_event = em_event_pointer(event);
	notif_event->type = NOTIF_ALL_QUEUE_GROUPS_CREATED;
	notif_event->seq = 0;
	notif_event->send_time = env_time_global();

	notif_tbl[0].event = event;
	notif_tbl[0].queue = perf_shm->notif_queue;
	notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

	/*
	 * Request one notification event when 'count' events from
	 * 'eo_ctx->event_group' have been received.
	 */
	stat = em_event_group_apply(perf_shm->eo_context.event_group, count,
				    1, notif_tbl);
	test_fatal_if(stat != EM_OK,
		      "em_event_group_apply():%" PRI_STAT "", stat);

	/* Create new queue groups */
	for (i = num_qgrp; i < (num_qgrp + count); i++) {
		em_notif_t completed_notif_tbl[1];

		/*
		 * Alloc event for notifying that the asynchronous
		 * em_queue_group_create() operation has been completed.
		 */
		event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW,
				 perf_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF,
			      "Notification event allocation failed!");

		notif_event = em_event_pointer(event);
		notif_event->type = NOTIF_QUEUE_GROUP_CREATED;
		notif_event->seq = i;
		notif_event->send_time = env_time_global();
		/* write the name into the notif for lookup when create done */
		nlen = sizeof(notif_event->qgrp_name);
		ret = snprintf(notif_event->qgrp_name, nlen, "grp_%d", i);
		test_fatal_if(ret >= (int)nlen, "Too long queue group name");
		notif_event->qgrp_name[nlen - 1] = '\0';

		completed_notif_tbl[0].event = event;
		completed_notif_tbl[0].queue = perf_shm->notif_queue;
		completed_notif_tbl[0].egroup =
			perf_shm->eo_context.event_group;

		queue_group = em_queue_group_create(notif_event->qgrp_name,
						    &mask, 1,
						    completed_notif_tbl);
		if (unlikely(queue_group == EM_QUEUE_GROUP_UNDEF)) {
			APPL_PRINT("Cannot create more queue groups - exit.\n");
			exit(EXIT_SUCCESS);
		}
		/*
		 * Update perf_shm->test_status.num_qgrp count and
		 * perf_shm->queue_group_tbl[] with new qgrp when receiving
		 * notif event.
		 */
	}
}

/**
 * Allocate, initialize, and send test step events evenly to queues.
 */
static void
test_step(void)
{
	em_status_t ret;
	em_event_t event;
	perf_event_t *perf;
	queue_context_t *q_ctx;
	int i;

	for (i = 0; i < NUM_EVENTS; i++) {
		event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW,
				 perf_shm->pool);
		perf = em_event_pointer(event);

		test_fatal_if(event == EM_EVENT_UNDEF,
			      "EM alloc failed (%i)", i);

		perf->type = PERF_EVENT;
		perf->seq = i;
		perf->send_time = env_time_global();

		/* Send events evenly to the queues. */
		q_ctx = &perf_shm->queue_context_tbl[i % NUM_QUEUES];
		ret = em_send(event, q_ctx->this_queue);

		if (unlikely(ret != EM_OK)) {
			test_fatal_if(!appl_shm->exit_flag,
				      "EM send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
				      ret, q_ctx->this_queue);
			em_free(event);
			return;
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
 *
 */
static em_status_t
stop(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	APPL_PRINT("EO %" PRI_EO " stopping.\n", eo);

	/* Remove and delete all of the EO's queues */
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
	env_time_t recv_time;

	if (MEASURE_LATENCY)
		recv_time = env_time_global();

	perf_event_t *perf = em_event_pointer(event);

	(void)eo_context;
	(void)type;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(perf->type == NOTIF_QUEUE_GROUP_CREATED)) {
		receive_notif_queue_group_created(perf, event);
		return;
	}

	if (unlikely(perf->type == NOTIF_ALL_QUEUE_GROUPS_CREATED)) {
		receive_notif_all_queue_groups_created(event);
		return;
	}

	/* perf->type == PERF_EVENT */
	receive_perf_event(recv_time, event, perf, queue, q_context);
}

/*
 * Verify the receive queue group and update perf_shm->test_status.num_qgrp
 * and perf_shm->queue_group_tbl[] with new qgrp.
 */
static void receive_notif_queue_group_created(perf_event_t *perf, em_event_t event)
{
	int cmp;
	char name[EM_QUEUE_GROUP_NAME_LEN];

	test_status_t *const tstat = &perf_shm->test_status;
	const int tbl_idx = tstat->num_qgrp;
	em_queue_group_t queue_group = em_queue_group_find(perf->qgrp_name);

	test_fatal_if(queue_group == EM_QUEUE_GROUP_UNDEF,
		      "QGrp:%s not found!", perf->qgrp_name);

	em_queue_group_get_name(queue_group, name, sizeof(name));
	cmp = strncmp(perf->qgrp_name, name, sizeof(name));
	test_fatal_if(cmp != 0, "Qgrp name check fails! %s != %s",
		      perf->qgrp_name, name);

	perf_shm->queue_group_tbl[tbl_idx] = queue_group;
	tstat->num_qgrp++;

	APPL_PRINT("New group name: %s\n"
		   "Queue group created - total num:%d\n",
		   name, tstat->num_qgrp);

	em_free(event);
}

static void receive_notif_all_queue_groups_created(em_event_t event)
{
	em_status_t ret;

	APPL_PRINT("New queue group(s) ready\n");

	ret = em_event_group_delete(perf_shm->eo_context.event_group);
	test_fatal_if(ret != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT "",
		      perf_shm->eo_context.event_group, ret);

	em_free(event);

	/* Create queues, link them to the created queue groups, send step
	 * events evenly to the created queues.
	 */
	next_test_step();
}

static void receive_perf_event(env_time_t recv_time, em_event_t event,
			       perf_event_t *perf, em_queue_t queue,
			       void *q_context)
{
	em_status_t ret;
	em_queue_t dest_queue;
	uint64_t freed_count;
	uint64_t ready_count;
	queue_context_t *q_ctx;
	const int core = em_core_id();
	test_status_t *const tstat = &perf_shm->test_status;
	core_stat_t *const cstat = &perf_shm->core_stat[core];
	uint64_t num_events = cstat->num_events;

	q_ctx = q_context;
	num_events++;

	if (unlikely(tstat->reset_flag)) {
		num_events = 0;

		/* Free all old events before allocating new ones. */
		if (unlikely(tstat->free_flag)) {
			em_free(event);

			freed_count =
			env_atomic64_add_return(&tstat->freed_count, 1);

			/* Last event. Only one core goes here. */
			if (freed_count == NUM_EVENTS) {
				env_atomic64_init(&tstat->freed_count);
				tstat->reset_flag = 0;
				tstat->free_flag = 0;
				init_queue_groups(QUEUE_GROUP_STEP_SIZE);
			}
			return;
		}

		if (unlikely(core_state != CORE_STATE_IDLE)) {
			core_state = CORE_STATE_IDLE;
			cstat->begin_time = ENV_TIME_NULL;

			ready_count =
			env_atomic64_add_return(&tstat->ready_count, 1);

			/* Only one core goes here. */
			if ((int)ready_count == tstat->num_cores) {
				env_atomic64_init(&tstat->ready_count);
				if (tstat->samples == 0)
					tstat->free_flag = 1;
				else
					tstat->reset_flag = 0;
			}
		}
	/* First event resets counters. */
	} else if (unlikely(num_events == 1)) {
		cstat->begin_time = env_time_global();
		cstat->latency_hi = ENV_TIME_NULL;
		cstat->latency_lo = ENV_TIME_NULL;
		core_state = CORE_STATE_MEASURE;
	} else if (unlikely(num_events == EVENTS_PER_SAMPLE)) {
		/*
		 * Measurement done for this step. Store results and
		 * continue receiving events until all cores are done.
		 */
		cstat->end_time = env_time_global();
		cstat->diff_time = env_time_diff(cstat->end_time, cstat->begin_time);

		/*
		 * Update the latency for the last measurement round.
		 */
		update_latency(recv_time, q_ctx, perf, cstat);

		ready_count = env_atomic64_add_return(&tstat->ready_count, 1);

		/*
		 * Check whether all cores are done with the step, and if done
		 * proceed to the next sample step.
		 */
		if (unlikely((int)ready_count == tstat->num_cores)) {
			print_sample_statistics(tstat);

			tstat->samples++;
			if (tstat->samples == NUM_SAMPLES)
				tstat->samples = 0;

			tstat->reset_flag = 1;
		}
	}
	cstat->num_events = num_events;

	dest_queue = q_ctx->next_queue;
	test_fatal_if(queue != q_ctx->this_queue, "Queue config error");

	if (MEASURE_LATENCY) {
		if (likely(num_events < EVENTS_PER_SAMPLE) &&
		    likely(!tstat->reset_flag))
			update_latency(recv_time, q_ctx, perf, cstat);

		perf->send_time = env_time_global();
	}

	ret = em_send(event, dest_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "EM send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, dest_queue);
	}
}

/* Update the send-enqueue-schedule-receive latency */
static inline void update_latency(env_time_t recv_time, queue_context_t *q_ctx,
				  perf_event_t *perf, core_stat_t *const cstat)
{
	env_time_t diff_time;

	diff_time = env_time_diff(recv_time, perf->send_time);

	if (q_ctx->prio == EM_QUEUE_PRIO_HIGH)
		cstat->latency_hi = env_time_sum(cstat->latency_hi, diff_time);
	else
		cstat->latency_lo = env_time_sum(cstat->latency_lo, diff_time);
}

/* Print statistics for one sample step */
static void print_sample_statistics(test_status_t *const tstat)
{
	env_time_t latency_hi;
	env_time_t latency_lo;
	double cycles_per_event, events_per_sec;
	env_time_t total_time = ENV_TIME_NULL;
	const uint64_t total_events = (uint64_t)tstat->num_cores * EVENTS_PER_SAMPLE;

	/* No real need for atomicity, only ran on last core */
	env_atomic64_init(&tstat->ready_count);

	if (MEASURE_LATENCY) {
		latency_hi = ENV_TIME_NULL;
		latency_lo = ENV_TIME_NULL;
	}

	for (int i = 0; i < tstat->num_cores; i++) {
		core_stat_t *cstat = &perf_shm->core_stat[i];

		total_time = env_time_sum(total_time, cstat->diff_time);
		if (MEASURE_LATENCY) {
			latency_hi = env_time_sum(latency_hi, cstat->latency_hi);
			latency_lo = env_time_sum(latency_lo, cstat->latency_lo);
		}
	}

	cycles_per_event =
	(double)env_time_to_cycles(total_time, tstat->cpu_hz) / (double)total_events;
	events_per_sec = tstat->mhz * tstat->num_cores / cycles_per_event;
	if (MEASURE_LATENCY) {
		const double latency_per_hi =
			(double)env_time_to_cycles(latency_hi, tstat->cpu_hz) /
			(double)total_events;
		const double latency_per_lo =
			(double)env_time_to_cycles(latency_lo, tstat->cpu_hz) /
			(double)total_events;
		APPL_PRINT("Cycles/Event: %.0f  Events/s: %.2f M\t"
			   "Latency: Hi-prio=%.0f Lo-prio=%.0f \t"
			   "@%.0f MHz(%" PRIu64 ")\n",
			   cycles_per_event, events_per_sec,
			   latency_per_hi, latency_per_lo,
			   tstat->mhz, tstat->print_count++);
	} else {
		APPL_PRINT("Cycles/Event: %.0f  Events/s: %.2f M\t"
			   "@%.0f MHz(%" PRIu64 ")\n",
			   cycles_per_event, events_per_sec,
			   tstat->mhz, tstat->print_count++);
	}
}

/**
 * Remap queues and move to the next test step.
 */
static void
next_test_step(void)
{
	if (perf_shm->test_status.num_qgrp > MIN_NUM_QUEUE_GROUPS)
		if (unschedule_and_delete_queues(NUM_QUEUES))
			return; /* appl_shm->exit_flag == 1 */

	create_and_link_queues(NUM_QUEUES);

	if (unlikely(appl_shm->exit_flag))
		return;

	test_step();
}

/**
 * Create a given number of EM queues, associate them with the test EO, and
 * link them.
 *
 * @param num_queues  Number of queues to be created
 */
static void
create_and_link_queues(int num_queues)
{
	em_queue_group_t queue_group;
	em_queue_prio_t prio;
	em_queue_t queue, prev_queue;
	queue_context_t *q_ctx;
	em_status_t ret;
	int i;

	prev_queue = EM_QUEUE_UNDEF;

	for (i = 0; i < num_queues; i++) {
		prio = EM_QUEUE_PRIO_NORMAL;

		if (MEASURE_LATENCY && (i % 4 == 0))
			prio = EM_QUEUE_PRIO_HIGH;

		queue_group = perf_shm->queue_group_tbl[i %
					  perf_shm->test_status.num_qgrp];

		queue = em_queue_create("queue", EM_QUEUE_TYPE_ATOMIC,
					prio, queue_group, NULL);
		test_fatal_if(queue == EM_QUEUE_UNDEF,
			      "em_queue_create failed.\t"
			      "Index:%i queue group:%" PRI_QGRP "",
			      i, queue_group);

		perf_shm->queue_tbl[i] = queue;

		q_ctx = &perf_shm->queue_context_tbl[i];

		test_fatal_if(em_queue_set_context(queue, q_ctx) !=
			      EM_OK, "em_queue_set_context failed.\t"
			      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
			      perf_shm->eo, queue);

		ret = em_eo_add_queue_sync(perf_shm->eo, queue);
		if (unlikely(ret != EM_OK)) {
			test_fatal_if(!appl_shm->exit_flag,
				      "em_eo_add_queue_sync():%" PRI_STAT "\n"
				      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
				      ret, perf_shm->eo, queue);

			/* appl_shm->exit_flag == 1 */
			em_queue_delete(queue);
			return;
		}
		/* Link queues */
		q_ctx->this_queue = queue;
		q_ctx->next_queue = prev_queue;
		q_ctx->prio = prio;

		prev_queue = queue;
	}

	/* Connect first queue to the last */
	q_ctx = &perf_shm->queue_context_tbl[0];
	q_ctx->next_queue = prev_queue;

	APPL_PRINT("\nQueues: %i, Queue groups: %i\n",
		   num_queues, perf_shm->test_status.num_qgrp);
}

/**
 * Unschedule and delete a given number of EM queues. Assume queues are already
 * empty.
 *
 * @param num_queues  Number of queues to be unscheduled and deleted
 * @return 0 on success, -1 if appl_shm->exit_flag is set
 */
static int
unschedule_and_delete_queues(int num_queues)
{
	em_status_t ret;
	em_queue_t queue;
	int i;

	for (i = 0; i < num_queues; i++) {
		queue = perf_shm->queue_tbl[i];

		ret = em_eo_remove_queue_sync(perf_shm->eo, queue);
		if (unlikely(ret != EM_OK)) {
			/* When appl_shm->exit_flag == 1, the queue might have
			 * been removed in em_eo_remove_queue_all_sync() from
			 * eo stop() executed by another core.
			 */
			test_fatal_if(!appl_shm->exit_flag,
				      "em_eo_remove_queue_sync failed:\t"
				      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
				      perf_shm->eo, queue);
			return -1; /* appl_shm->exit_flag == 1 */
		}

		ret = em_queue_delete(queue);
		if (unlikely(ret != EM_OK)) {
			/* When appl_shm->exit_flag == 1, the queue might have
			 * been deleted already in em_eo_delete() from test_stop()
			 * executed by another core.
			 */
			test_fatal_if(!appl_shm->exit_flag,
				      "em_queue_delete():%" PRI_STAT "\t"
				      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
				      ret, perf_shm->eo, queue);
			return -1; /* appl_shm->exit_flag == 1 */
		}
	}

	return 0;
}
