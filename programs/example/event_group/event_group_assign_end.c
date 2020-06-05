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
 * Event Machine event group example using em_event_group_assign() and
 * em_event_group_processing_end().
 *
 * Test and measure the event group feature for fork-join type of operations
 * using events. See the event_machine_event_group.h file for the event group
 * API calls.
 *
 * Allocates and sends a number of data events to itself (using two event
 * groups) to trigger notification events to be sent when the configured event
 * count has been received. The cycles consumed until the notification is
 * received is measured and printed.
 *
 * Three event groups are used, two to track completion of a certain number
 * of data events and a final third chained event group to track completion
 * of the two other event groups. The test is restarted once the final third
 * notification is received. One of the two event groups used to track
 * completion of data events is a "normal" event group but the other event
 * group is assigned when a data event is received instead of normally sending
 * the event with an event group.
 *
 * Note: To keep things simple this testcase uses only a single queue into
 * which to receive all events, including the notification events. The event
 * group fork-join mechanism does not care about the used queues. However,
 * it's basically a counter of events sent using a certain event group id.
 * In a more complex example each data event could be send from different EO:s
 * to different queues and the final notification event sent yet to another
 * queue.
 */

#include <string.h>
#include <stdio.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Test configuration
 */

/** The number of data events to allocate and send */
#define DATA_EVENTS  256

/**
 * The number of times to call em_event_group_increment() per received
 * data event, results in '(7+1) * DATA_EVENTS' events before notification
 */
#define EVENT_GROUP_INCREMENT  7

/** Max number of cores */
#define MAX_NBR_OF_CORES  256

/** Spin value before restarting the test */
#define DELAY_SPIN_COUNT  50000000

/** Expected nbr of data events processed to trigger a notif event */
#define CFG_EV_CNT (DATA_EVENTS * (1 + EVENT_GROUP_INCREMENT))

/** Define how many events are sent per em_send_multi() call */
#define SEND_MULTI_MAX 32

/**
 * The event for this test case.
 */
typedef struct {
	#define MSG_START 1
	#define MSG_START_ASSIGN 2
	#define MSG_DATA 3
	/* notification event */
	#define MSG_DONE 4
	/* notification event for the assigned event group */
	#define MSG_DONE_ASSIGN 5
	/* chained notification event for when both 'done' above are handled */
	#define MSG_ALL_DONE_CHAINED 6
	/* Event/msg number */
	uint64_t msg;
	/* Start time stored at the beginning of each round */
	env_time_t start_time;
	/* The number of times to increment (per event) the event group count
	 * by calling em_event_group_increment(1).
	 */
	uint64_t increment;
} event_group_test_t;

/**
 * "Context" data for the event groups
 */
typedef struct {
	/* Event Group used by the EO */
	em_event_group_t event_group;
	/* Store the start event to restart the test */
	em_event_t event_start;
	/* Total test case running time. Updated when receiving
	 * a notification event.
	 */
	env_time_t total_time;
	/* The number of rounds, i.e. received notifications, during this
	 * test case
	 */
	uint64_t total_rounds;
} egrp_context_t;

/**
 * EO context used by the test
 */
typedef union {
	struct {
		/* This EO */
		em_eo_t eo;
		/* Queue owned by this EO */
		em_queue_t queue;
		/* The number of events to send using the event group before
		 * triggering a notification event.
		 */
		int event_count;
		/* Normal event group used to track completion of a set of
		 * MSG_DATA events
		 */
		egrp_context_t egrp;
		/* Assigned event group used to track completion of another
		 * set of MSG_DATA events
		 */
		egrp_context_t egrp_assign;
		/* Chained event group used to track when both of the above
		 * event groups have been completed
		 */
		em_event_group_t all_done_event_group;
	};
	/* Pad EO context to cache line size */
	uint8_t u8[2 * ENV_CACHE_LINE_SIZE];
} eo_context_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_t) % ENV_CACHE_LINE_SIZE == 0,
		    EVENT_GROUP_TEST_EO_CONTEXT_SIZE_ERROR);

/**
 * Core-specific data
 */
typedef union {
	struct {
		/* Counter of the received data events on a core */
		uint64_t rcv_ev_cnt;
	};
	/* Pad to cache line size to avoid cache line sharing */
	uint8_t u8[ENV_CACHE_LINE_SIZE];
} core_stat_t;

COMPILE_TIME_ASSERT(sizeof(core_stat_t) == ENV_CACHE_LINE_SIZE,
		    CORE_STAT_T_SIZE_ERROR);

/**
 * Event Group test shared data
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO context */
	eo_context_t eo_context ENV_CACHE_LINE_ALIGNED;
	/* Array of core specific data accessed by a core using its core index.
	 * No serialization mechanisms needed to protect the data even when
	 * using parallel queues.
	 * Core stats for the normal event group
	 */
	core_stat_t core_stats[MAX_NBR_OF_CORES] ENV_CACHE_LINE_ALIGNED;
	/* Core stats for the assigned event group */
	core_stat_t core_stats_assign[MAX_NBR_OF_CORES] ENV_CACHE_LINE_ALIGNED;

	uint64_t cpu_hz;
} egrp_shm_t;

static ENV_LOCAL egrp_shm_t *egrp_shm;

/*
 * Local function prototypes
 */
static em_status_t
egroup_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
egroup_stop(void *eo_context, em_eo_t eo);

static void
egroup_receive(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx);

static uint64_t
sum_received_events(core_stat_t core_stats[], int len);

static void
update_test_time(env_time_t diff, egrp_context_t *const egrp_context);

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
 * Init of the Event Group test application.
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
		egrp_shm = env_shared_reserve("EGrpSharedMem",
					      sizeof(egrp_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		egrp_shm = env_shared_lookup("EGrpSharedMem");
	}

	if (egrp_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "EventGroup test init failed on EM-core: %u\n",
			   em_core_id());
	} else if (core == 0) {
		memset(egrp_shm, 0, sizeof(egrp_shm_t));
	}
}

/**
 * Startup of the Event Group test application.
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
	em_event_t event;
	em_eo_t eo;
	em_queue_t queue;
	em_status_t ret;
	/* return value from the EO's start function 'group_start' */
	em_status_t eo_start_ret = EM_ERROR;
	eo_context_t *eo_ctx;
	event_group_test_t *egroup_test;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		egrp_shm->pool = appl_conf->pools[0];
	else
		egrp_shm->pool = EM_POOL_DEFAULT;

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
		   egrp_shm->pool);

	test_fatal_if(egrp_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	egrp_shm->cpu_hz = env_core_hz();

	/*
	 * Create the event group test EO and a parallel queue, add the queue
	 * to the EO
	 */
	eo_ctx = &egrp_shm->eo_context;
	memset(eo_ctx, 0, sizeof(eo_context_t));

	eo = em_eo_create("group test appl", egroup_start, NULL, egroup_stop,
			  NULL, egroup_receive, eo_ctx);
	eo_ctx->eo = eo;
	queue = em_queue_create("group test parallelQ", EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);
	eo_ctx->queue = queue;

	ret = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(ret != EM_OK, "EO add queue:%" PRI_STAT ".\n"
		      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
		      ret, eo, queue);

	/* Start the EO (triggers the EO's start function 'egroup_start') */
	ret = em_eo_start_sync(eo, &eo_start_ret, NULL);
	test_fatal_if(ret != EM_OK || eo_start_ret != EM_OK,
		      "em_eo_start() failed! EO:%" PRI_EO "\n"
		      "ret:%" PRI_STAT " EO-start-ret:%" PRI_STAT "",
		      eo, ret, eo_start_ret);

	/*
	 * Allocate and send the test start events, one for each of the two
	 * event groups used
	 */
	event = em_alloc(sizeof(event_group_test_t), EM_EVENT_TYPE_SW,
			 egrp_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");

	egroup_test = em_event_pointer(event);
	egroup_test->msg = MSG_START;
	ret = em_send(event, queue);
	test_fatal_if(ret != EM_OK,
		      "Event send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
		      ret, queue);

	event = em_alloc(sizeof(event_group_test_t), EM_EVENT_TYPE_SW,
			 egrp_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");
	egroup_test = em_event_pointer(event);
	egroup_test->msg = MSG_START_ASSIGN;
	ret = em_send(event, queue);
	test_fatal_if(ret != EM_OK,
		      "Event send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
		      ret, queue);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	eo_context_t *const eo_ctx = &egrp_shm->eo_context;
	em_eo_t eo;
	em_status_t ret;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	eo = eo_ctx->eo;

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
		env_shared_free(egrp_shm);
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 *
 * Creates the event groups used in this test case.
 */
static em_status_t
egroup_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_status_t ret;
	em_event_t event;
	event_group_test_t *egroup_test;
	em_notif_t egroup_notif_tbl[1];
	eo_context_t *eo_ctx = eo_context;

	(void)conf;

	/* First normal event group */
	eo_ctx->egrp.event_group = em_event_group_create();
	/* Event group assigned to events sent without any event group */
	eo_ctx->egrp_assign.event_group = em_event_group_create();
	/* Event group used to track completion of the two event groups above*/
	eo_ctx->all_done_event_group = em_event_group_create();

	if (eo_ctx->egrp.event_group == EM_EVENT_GROUP_UNDEF ||
	    eo_ctx->egrp_assign.event_group == EM_EVENT_GROUP_UNDEF ||
	    eo_ctx->all_done_event_group == EM_EVENT_GROUP_UNDEF) {
		return EM_ERR_ALLOC_FAILED;
	}

	memset(egrp_shm->core_stats, 0, sizeof(egrp_shm->core_stats));
	memset(egrp_shm->core_stats_assign, 0,
	       sizeof(egrp_shm->core_stats_assign));

	APPL_PRINT("EO:%" PRI_EO " event group:%" PRI_EGRP "\n",
		   eo, eo_ctx->egrp.event_group);
	APPL_PRINT("EO:%" PRI_EO " assigned event group:%" PRI_EGRP "\n\n",
		   eo, eo_ctx->egrp_assign.event_group);

	/*
	 * Require 'DATA_EVENTS' sent using the event_groups before
	 * a notification, see em_event_group_apply() later.
	 */
	eo_ctx->event_count = DATA_EVENTS;

	/*
	 * Allocate the notification event for the chained test completion
	 * event group
	 */
	event = em_alloc(sizeof(event_group_test_t), EM_EVENT_TYPE_SW,
			 egrp_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");
	egroup_test = em_event_pointer(event);
	egroup_test->msg = MSG_ALL_DONE_CHAINED;

	egroup_notif_tbl[0].event = event;
	egroup_notif_tbl[0].queue = eo_ctx->queue;
	egroup_notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

	/*
	 * Apply the chained 'all done' event group, chained notification
	 * sent when both data notifications events have been completed.
	 * Used for restarting the test.
	 */
	ret = em_event_group_apply(eo_ctx->all_done_event_group, 2, 1,
				   egroup_notif_tbl);
	test_fatal_if(ret != EM_OK, "Event group apply:%" PRI_STAT "", ret);

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t
egroup_stop(void *eo_context, em_eo_t eo)
{
	eo_context_t *eo_ctx = eo_context;
	em_event_group_t egrp;
	em_status_t ret;
	em_notif_t notif_tbl[1] = { {.event = EM_EVENT_UNDEF} };
	int num_notifs;

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* No more dispatching of the EO's events, egrps can be freed */

	/* First normal event group */
	egrp = eo_ctx->egrp.event_group;
	if (!em_event_group_is_ready(egrp)) {
		num_notifs = em_event_group_get_notif(egrp, 1, notif_tbl);
		ret = em_event_group_abort(egrp);
		if (ret == EM_OK && num_notifs == 1)
			em_free(notif_tbl[0].event);
	}
	ret = em_event_group_delete(egrp);
	test_fatal_if(ret != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT " EO:%" PRI_EO "",
		      egrp, ret, eo);

	/* Event group assigned to events sent without any event group */
	egrp = eo_ctx->egrp_assign.event_group;
	if (!em_event_group_is_ready(egrp)) {
		num_notifs = em_event_group_get_notif(egrp, 1, notif_tbl);
		ret = em_event_group_abort(egrp);
		if (ret == EM_OK && num_notifs == 1)
			em_free(notif_tbl[0].event);
	}
	ret = em_event_group_delete(egrp);
	test_fatal_if(ret != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT " EO:%" PRI_EO "",
		      egrp, ret, eo);

	/* Event group used to track completion of the two event groups above*/
	egrp = eo_ctx->all_done_event_group;
	if (!em_event_group_is_ready(egrp)) {
		num_notifs = em_event_group_get_notif(egrp, 1, notif_tbl);
		ret = em_event_group_abort(egrp);
		if (ret == EM_OK && num_notifs == 1)
			em_free(notif_tbl[0].event);
	}
	ret = em_event_group_delete(egrp);
	test_fatal_if(ret != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT " EO:%" PRI_EO "",
		      egrp, ret, eo);

	return EM_OK;
}

/**
 * @private
 *
 * EO receive function.
 *
 * Handles all events received during this test: test start, data and
 * completion notification events.
 */
static void
egroup_receive(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx)
{
	em_status_t ret;
	eo_context_t *eo_ctx = eo_context;
	event_group_test_t *egroup_test;
	env_time_t diff_time, start_time, end_time;
	int i, j;

	em_notif_t egroup_notif_tbl[1];
	em_event_group_t apply_event_group;

	uint64_t rcv_ev_cnt;
	egrp_context_t *egrp_context;
	core_stat_t *core_stats;
	const char *egrp_str;

	(void)type;
	(void)q_ctx;

	egroup_test = em_event_pointer(event);

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	switch (egroup_test->msg) {
	case MSG_START:
	case MSG_START_ASSIGN: {
		/*
		 * (Re)start the test by configuring the event group,
		 * allocating all data events and then sending all events
		 * back to itself. MSG_DATA events are sent using an event
		 * group and MSG_DATA_ASSIGN events are sent without an event
		 * group (instead assign the event group when receiving the
		 * event to test both aspects).
		 */
		const int ev_cnt = eo_ctx->event_count;
		em_event_t ev_tbl[ev_cnt];
		int num_sent = 0;

		/* Reuse the start event as the notification event */
		if (egroup_test->msg == MSG_START) {
			APPL_PRINT("--- Start event group ---\n");
			/* event group notification */
			egroup_test->msg = MSG_DONE;
			apply_event_group = eo_ctx->egrp.event_group;
		} else {
			APPL_PRINT("--- Start assigned event group ---\n");
			/* event group notification */
			egroup_test->msg = MSG_DONE_ASSIGN;
			apply_event_group = eo_ctx->egrp_assign.event_group;
		}

		egroup_test->start_time = env_time_global();
		egroup_test->increment = 0;

		/* Notification 'event' should be sent to 'queue' when done */
		egroup_notif_tbl[0].event = event;
		egroup_notif_tbl[0].queue = queue;
		/*
		 * Send final notification using a chained event group to
		 * trigger the last notif when both event groups are done
		 */
		egroup_notif_tbl[0].egroup = eo_ctx->all_done_event_group;

		/*
		 * Request one notification event when 'eo_ctx->event_count'
		 * events from the event_group have been received.
		 *
		 * Note that the em_event_group_increment() functionality is
		 * used in the event:MSG_DATA processing so the total number
		 * of events received will be larger than 'eo_ctx->event_count'
		 * before the notification 'MSG_DONE' is received.
		 */
		ret = em_event_group_apply(apply_event_group, ev_cnt, 1,
					   egroup_notif_tbl);
		test_fatal_if(ret != EM_OK,
			      "Event group apply:%" PRI_STAT "", ret);

		/*
		 * Allocate 'eo_ctx->event_count' number of DATA events
		 * and send them using an event group to trigger the
		 * notification event configured above with
		 * em_event_group_apply()
		 */
		for (i = 0; i < ev_cnt; i++) {
			event_group_test_t *egroup_test_2;

			ev_tbl[i] = em_alloc(sizeof(event_group_test_t),
					     EM_EVENT_TYPE_SW, egrp_shm->pool);
			test_fatal_if(ev_tbl[i] == EM_EVENT_UNDEF,
				      "Event allocation failed!");

			egroup_test_2 = em_event_pointer(ev_tbl[i]);
			egroup_test_2->msg = MSG_DATA;
			egroup_test_2->start_time = ENV_TIME_NULL;
			/* How many times to increment and resend */
			egroup_test_2->increment = EVENT_GROUP_INCREMENT;
		}

		/* Send in bursts of 'SEND_MULTI_MAX' events */
		const int send_rounds = ev_cnt / SEND_MULTI_MAX;
		const int left_over = ev_cnt % SEND_MULTI_MAX;
		em_event_group_t send_event_group;

		if (apply_event_group == eo_ctx->egrp.event_group) {
			/* Send events using the event group */
			send_event_group = apply_event_group;
		} else {
			/* Assign the event group later */
			send_event_group = EM_EVENT_GROUP_UNDEF;
		}

		for (i = 0, j = 0; i < send_rounds; i++, j += SEND_MULTI_MAX) {
			num_sent +=
			em_send_group_multi(&ev_tbl[j], SEND_MULTI_MAX,
					    queue, send_event_group);
		}
		if (left_over) {
			num_sent +=
			em_send_group_multi(&ev_tbl[j], left_over,
					    queue, send_event_group);
		}
		if (unlikely(num_sent != ev_cnt)) {
			for (i = num_sent; i < ev_cnt; i++)
				em_free(ev_tbl[i]);
			test_fatal_if(!appl_shm->exit_flag,
				      "Event send multi failed:%d (%d)\n"
				      "Q:%" PRI_QUEUE "",
				      num_sent, ev_cnt, queue);
		}
		break;
	} /* end case MSG_START & MSG_START_ASSIGN */

	case MSG_DATA: {
		int core = em_core_id();

		/*
		 * Handle the test data events. If the received event was
		 * sent without an event group then assign it instead.
		 */
		if (em_event_group_current() == EM_EVENT_GROUP_UNDEF) {
			/* Assign event to an event group if sent without */
			ret =
			em_event_group_assign(eo_ctx->egrp_assign.event_group);
			test_fatal_if(ret != EM_OK,
				      "Event group assign:%" PRI_STAT "", ret);
			/* Update the count of assigned data events received */
			egrp_shm->core_stats_assign[core].rcv_ev_cnt += 1;
		} else {
			/* Update the count of data events received */
			egrp_shm->core_stats[core].rcv_ev_cnt += 1;
		}

		/*
		 * Test the em_event_group_processing_end() and
		 * em_event_group_assign() functionalities.
		 *
		 * Note that the total number of events received will become
		 * larger than 'eo_ctx->event_count' before the notification
		 * 'MSG_DONE' is received.
		 */
		if (egroup_test->increment) {
			egroup_test->increment--;

			/*
			 * Increment the event count in the event group, after
			 * this the count is >= 2
			 */
			ret = em_event_group_increment(1);
			test_fatal_if(ret != EM_OK,
				      "Event group incr:%" PRI_STAT "", ret);

			/* Get the current event group */
			const em_event_group_t event_group =
					       em_event_group_current();

			/*
			 * End event group processing, effectively also
			 * decrements count to >= 1
			 */
			em_event_group_processing_end();

			/*
			 * Assign the same group back (for testing purposes),
			 * this is still valid due to the increment & end,
			 * i.e. event group count is >= 1
			 */
			ret = em_event_group_assign(event_group);
			test_fatal_if(ret != EM_OK,
				      "Event group assign:%" PRI_STAT "", ret);

			/*
			 * Increment the count once more to include
			 * resending + decrement after return
			 */
			ret = em_event_group_increment(1);
			test_fatal_if(ret != EM_OK,
				      "Event group incr:%" PRI_STAT "", ret);

			if (em_event_group_current() ==
			    eo_ctx->egrp.event_group) {
				/* Resend event using the event group */
				ret = em_send_group(event, queue, event_group);
			} else {
				/*
				 * Resend the event without an event group,
				 * instead assign it when received again
				 */
				ret = em_send(event, queue);
			}
			if (unlikely(ret != EM_OK)) {
				em_free(event);
				test_fatal_if(!appl_shm->exit_flag,
					      "Send:%" PRI_STAT "\n"
					      "Q:%" PRI_QUEUE "", ret, queue);
			}
		} else {
			em_event_group_processing_end();
			em_free(event);
		}
		break;
	} /* end case MSG_DATA */

	case MSG_DONE:
	case MSG_DONE_ASSIGN:
		/*
		 * Notification event received!
		 * Calculate the number of cycles spent.
		 * A new notification event 'MSG_ALL_DONE_CHAINED' will be
		 * triggered when both 'MSG_DONE' and 'MSG_DONE_ASSIGN' have
		 * been processed - this last notif event will restart the
		 * test.
		 */
		if (egroup_test->msg == MSG_DONE) {
			egrp_context = &eo_ctx->egrp;
			core_stats = egrp_shm->core_stats;
			egrp_str = "\"Normal\"";
			/* Store the start event */
			egroup_test->msg = MSG_START;
		} else { /* MSG_DONE_ASSIGN */
			egrp_context = &eo_ctx->egrp_assign;
			core_stats = egrp_shm->core_stats_assign;
			egrp_str = "Assigned";
			/* Store the start event */
			egroup_test->msg = MSG_START_ASSIGN;
		}

		/* Calculate the time and received data events */
		end_time = env_time_global();
		start_time = egroup_test->start_time;
		diff_time = env_time_diff(end_time, start_time);
		update_test_time(diff_time, egrp_context);
		rcv_ev_cnt = sum_received_events(core_stats, em_core_count());

		/* OK, print results */
		APPL_PRINT("%s event group notification event received after\t"
			   "%" PRIu64 " data events.\n"
			   "Cycles curr:%" PRIu64 ", ave:%" PRIu64 "\n",
			   egrp_str, rcv_ev_cnt,
			   env_time_to_cycles(diff_time, egrp_shm->cpu_hz),
			   env_time_to_cycles(egrp_context->total_time,
					      egrp_shm->cpu_hz) /
			   egrp_context->total_rounds);

		/*
		 * Verify that the amount of received data events prior to
		 * this notification event is correct.
		 */
		test_fatal_if(rcv_ev_cnt != CFG_EV_CNT,
			      "Incorrect nbr of data events before notif:\t"
			      "%" PRIu64 " != %" PRIu64 "!",
			      rcv_ev_cnt, CFG_EV_CNT);

		egrp_context->event_start = event;
		break;

	case MSG_ALL_DONE_CHAINED:
		/*
		 * Final chained notification event to indicate end of test,
		 * i.e. both data event completion notification events have
		 * been received and processed. Restart the test by sending
		 * the 'start' events.
		 */
		APPL_PRINT("--- Chained event group done ---\n\n");

		/*
		 * Re-apply the 'all done' event group for a new test round,
		 * i.e. request a final notif to be sent when the test is
		 * completed.
		 */
		egroup_notif_tbl[0].event = event;
		egroup_notif_tbl[0].queue = eo_ctx->queue;
		egroup_notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

		ret = em_event_group_apply(eo_ctx->all_done_event_group, 2, 1,
					   egroup_notif_tbl);
		test_fatal_if(ret != EM_OK,
			      "Event group apply:%" PRI_STAT "!", ret);

		/* Restart the test after "some cycles" of delay */
		delay_spin(DELAY_SPIN_COUNT);

		ret = em_send(eo_ctx->egrp.event_start, queue);
		if (unlikely(ret != EM_OK)) {
			em_free(eo_ctx->egrp.event_start);
			test_fatal_if(!appl_shm->exit_flag,
				      "Send:%" PRI_STAT " Q:%" PRI_QUEUE "",
				      ret, queue);
		}

		ret = em_send(eo_ctx->egrp_assign.event_start, queue);
		if (unlikely(ret != EM_OK)) {
			em_free(eo_ctx->egrp_assign.event_start);
			test_fatal_if(!appl_shm->exit_flag,
				      "Send:%" PRI_STAT " Q:%" PRI_QUEUE "",
				      ret, queue);
		}
		break;

	default:
		test_fatal_if(1, "Bad msg (%" PRIu64 ")!", egroup_test->msg);
		break;
	};
}

static uint64_t
sum_received_events(core_stat_t core_stats[], int len)
{
	uint64_t rcv_ev_cnt = 0;
	int i;

	/* Sum up the amount of data events processed on each core */
	for (i = 0; i < len; i++) {
		rcv_ev_cnt += core_stats[i].rcv_ev_cnt;
		core_stats[i].rcv_ev_cnt = 0;
	}

	return rcv_ev_cnt;
}

static void
update_test_time(env_time_t diff_time, egrp_context_t *const egrp_context)
{
	/* Ignore the first round because of cold caches. */
	if (egrp_context->total_rounds == 1) {
		egrp_context->total_time =
		env_time_sum(egrp_context->total_time, diff_time);
		egrp_context->total_time =
		env_time_sum(egrp_context->total_time, diff_time);
	} else if (egrp_context->total_rounds > 1) {
		egrp_context->total_time =
		env_time_sum(egrp_context->total_time, diff_time);
	}
	egrp_context->total_rounds++;
}
