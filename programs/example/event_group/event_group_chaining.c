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
 * Event Machine event group chaining example.
 *
 * Test and measure the event group chaining feature for fork-join type of
 * operations using events. See the event_machine_event_group.h file for the
 * event group API calls.
 *
 * Allocates and sends a number of data events to itself using an event group.
 * When the configured event count has been reached, notification events are
 * triggered. The cycles consumed until each notification is received is
 * measured and printed. The notification events are sent using a second event
 * group which will trigger a final done event after all the notifications have
 * been processed.
 *
 * Note: To keep things simple this test case uses only a single queue to
 * receive all events, including the notification events. The event group
 * fork-join mechanism does not care about the used queues. However, it's
 * basically a counter of events sent using a certain event group id. In
 * a more complex example each data event could be send from different EOs to
 * different queues and the final notification event sent yet to another queue.
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

/* The number of data events to allocate and send */
#define DATA_EVENTS  128

/*
 * The number of notification events (max EM_EVENT_GROUP_MAX_NOTIF) to
 * allocate and send to the chained event group
 */
#define NOTIF_EVENTS  4

/* em_event_group_increment() calls per received data event */
#define EVENT_GROUP_INCREMENT  1

/* Max number of cores */
#define MAX_NBR_OF_CORES  256

/**
 * The event for this test case.
 */
typedef struct {
	#define MSG_START 1
	#define MSG_DATA  2
	#define MSG_CHAIN 3
	#define MSG_DONE  4
	/* Event/msg number */
	uint64_t msg;
	/* Start time stored at the beginning of each round */
	env_time_t start_time;
	/* Number of times to increment (per event) the event group count */
	uint64_t increment;
} event_group_test_t;

/**
 * EO context used by the test
 */
typedef struct {
	/* This EO */
	em_eo_t eo;
	/* Event Group Id used by the EO */
	em_event_group_t event_group;
	/* Chained Event Group Id used by the EO */
	em_event_group_t chained_event_group;
	/* Number of events to send before triggering a notif event */
	int event_count;
	/* Total running time updated when receiving a notif event*/
	env_time_t total_time;
	/* Number of rounds, i.e. received notifications */
	uint64_t total_rounds;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} eo_context_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_t) == ENV_CACHE_LINE_SIZE,
		    EVENT_GROUP_TEST_EO_CONTEXT_SIZE_ERROR);

/**
 * Core-specific data
 */
typedef struct {
	/* Counter of the received data events on a core */
	uint64_t rcv_ev_cnt;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} core_stat_t;

COMPILE_TIME_ASSERT(sizeof(core_stat_t) == ENV_CACHE_LINE_SIZE,
		    CORE_STAT_T_SIZE_ERROR);

/**
 * Shared data
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO context */
	eo_context_t eo_context ENV_CACHE_LINE_ALIGNED;
	/*
	 * Array of core specific data accessed by a core using its core index.
	 * No serialization mechanisms needed to protect the data even when
	 * using parallel queues.
	 */
	core_stat_t core_stats[MAX_NBR_OF_CORES] ENV_CACHE_LINE_ALIGNED;

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
 * Init of the Event Group Chaining test application.
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
			   "EventGroupChaining test init failed\n");
	} else if (core == 0) {
		memset(egrp_shm, 0, sizeof(egrp_shm_t));
	}
}

/**
 * Startup of the Event Group Chaining test application.
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
	 * Create the event group chaining test EO and a parallel queue, add
	 * the queue to the EO
	 */
	eo_ctx = &egrp_shm->eo_context;

	eo = em_eo_create("evgrp-chaining-test-eo", egroup_start, NULL,
			  egroup_stop, NULL, egroup_receive, eo_ctx);
	test_fatal_if(eo == EM_EO_UNDEF, "EO create failed\n");
	eo_ctx->eo = eo;

	queue = em_queue_create("evgrp-chaining-test-parallelQ",
				EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);

	ret = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(ret != EM_OK, "EO add queue:%" PRI_STAT ".\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", ret, eo, queue);

	/* Start the EO (triggers the EO's start function 'egroup_start') */
	ret = em_eo_start_sync(eo, &eo_start_ret, NULL);
	test_fatal_if(ret != EM_OK || eo_start_ret != EM_OK,
		      "EO start failed, EO:%" PRI_EO "\n"
		      "ret:%" PRI_STAT " EO-start-ret:%" PRI_STAT "",
		      eo, ret, eo_start_ret);

	/* Print the used event groups */
	unsigned int num, i = 1;
	em_event_group_t event_group;

	event_group = em_event_group_get_first(&num);
	if (event_group != EM_EVENT_GROUP_UNDEF)
		APPL_PRINT("%s is using %u event groups:\n",
			   appl_conf->name, num);
	while (event_group != EM_EVENT_GROUP_UNDEF) {
		APPL_PRINT("  evgrp-%d:%" PRI_EGRP "\n", i++, event_group);
		event_group = em_event_group_get_next();
	}

	event = em_alloc(sizeof(event_group_test_t), EM_EVENT_TYPE_SW,
			 egrp_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");

	egroup_test = em_event_pointer(event);
	egroup_test->msg = MSG_START;

	ret = em_send(event, queue);
	test_fatal_if(ret != EM_OK,
		      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "", ret, queue);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_status_t ret;
	em_eo_t eo;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	eo = egrp_shm->eo_context.eo;

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
	eo_context_t *eo_ctx = eo_context;
	int ready;

	(void)conf;

	/* First event group */
	eo_ctx->event_group = em_event_group_create();
	eo_ctx->total_time = ENV_TIME_NULL;

	if (eo_ctx->event_group == EM_EVENT_GROUP_UNDEF)
		return EM_ERR_ALLOC_FAILED;

	test_fatal_if(em_event_group_is_ready(eo_ctx->event_group) == EM_FALSE,
		      "'event_group' should be ready to be applied");

	APPL_PRINT("EO:%" PRI_EO " - event group %" PRI_EGRP " created\n",
		   eo, eo_ctx->event_group);

	/* Chained event group */
	eo_ctx->chained_event_group = em_event_group_create();

	if (eo_ctx->chained_event_group == EM_EVENT_GROUP_UNDEF)
		return EM_ERR_ALLOC_FAILED;

	ready = em_event_group_is_ready(eo_ctx->chained_event_group);
	test_fatal_if(ready == EM_FALSE,
		      "'chained_event_group' should be ready to be applied");

	APPL_PRINT("EO:%" PRI_EO " - chained event group %" PRI_EGRP " created\n",
		   eo, eo_ctx->chained_event_group);

	/*
	 * Require 'DATA_EVENTS' sent using the event_group before
	 * a notification, see em_event_group_apply() later.
	 */
	eo_ctx->event_count = DATA_EVENTS;

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
	em_notif_t notif_tbl[NOTIF_EVENTS];
	int num_notifs;
	em_status_t ret;

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* No more dispatching of the EO's events, egrps can be freed */

	/* First event group */
	egrp = eo_ctx->event_group;
	if (!em_event_group_is_ready(egrp)) {
		num_notifs = em_event_group_get_notif(egrp, NOTIF_EVENTS,
						      notif_tbl);
		ret = em_event_group_abort(egrp);
		if (ret == EM_OK && num_notifs > 0)
			for (int i = 0; i < num_notifs; i++)
				em_free(notif_tbl[i].event);
	}
	ret = em_event_group_delete(egrp);
	test_fatal_if(ret != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT " EO:%" PRI_EO "",
		      egrp, ret, eo);

	/* Chained event group */
	egrp = eo_ctx->chained_event_group;
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
 */
static void
egroup_receive(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx)
{
	em_event_t chained_event;
	em_notif_t notif_tbl[NOTIF_EVENTS];
	em_notif_t notif_tbl_chained[1];
	em_status_t ret;
	eo_context_t *const eo_ctx = eo_context;
	event_group_test_t *egroup_chained;
	event_group_test_t *egroup_test;
	env_time_t diff_time, start_time, end_time;
	uint64_t rcv_ev_cnt;
	uint64_t cfg_ev_cnt;
	int i;
	int ready;

	(void)type;
	(void)q_ctx;

	egroup_test = em_event_pointer(event);

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	switch (egroup_test->msg) {
	case MSG_START:
		/*
		 * (Re)start the test by configuring the event groups,
		 * allocating all data events and then sending (with event
		 * group) all events back to itself.
		 */
		APPL_PRINT("\n--- Start event group ---\n");

		/* Reuse the start event as the final notification event */
		egroup_test->msg = MSG_DONE;

		/* The notif 'event' should be sent to 'queue' when done */
		notif_tbl_chained[0].event = event;
		notif_tbl_chained[0].queue = queue;
		notif_tbl_chained[0].egroup = EM_EVENT_GROUP_UNDEF;

		/* Allocate new events for the chained event group */
		for (i = 0; i < NOTIF_EVENTS; i++) {
			chained_event = em_alloc(sizeof(event_group_test_t),
						 EM_EVENT_TYPE_SW,
						 egrp_shm->pool);
			test_fatal_if(chained_event == EM_EVENT_UNDEF,
				      "Event allocation failed!");

			egroup_chained = em_event_pointer(chained_event);
			egroup_chained->msg = MSG_CHAIN;
			egroup_chained->start_time = env_time_global();
			egroup_chained->increment = 0;
			/*
			 * The notification 'chained_event' should be sent
			 * to 'queue' after the original event group has
			 * finished
			 */
			notif_tbl[i].event = chained_event;
			notif_tbl[i].queue = queue;
			notif_tbl[i].egroup = eo_ctx->chained_event_group;
		}

		/*
		 * Request one notification event when NOTIF_EVENTS events from
		 * 'eo_ctx->chained_event_group' have been received.
		 */
		ret = em_event_group_apply(eo_ctx->chained_event_group,
					   NOTIF_EVENTS, 1, notif_tbl_chained);
		test_fatal_if(ret != EM_OK,
			      "em_event_group_apply():%" PRI_STAT "", ret);

		ready = em_event_group_is_ready(eo_ctx->chained_event_group);
		test_fatal_if(ready == EM_TRUE,
			      "chained ev-grp shouldn't be ready yet");

		/*
		 * Request NOTIF_EVENTS notification events when
		 * 'eo_ctx->event_count' events from 'eo_ctx->event_group'
		 * have been received.
		 *
		 * Note that the em_event_group_increment() functionality is
		 * used in the event:MSG_DATA processing so the total number
		 * of events received will be larger than 'eo_ctx->event_count'
		 * before the notification event:MSG_CHAIN is received.
		 */
		ret = em_event_group_apply(eo_ctx->event_group,
					   eo_ctx->event_count, NOTIF_EVENTS,
					   notif_tbl);
		test_fatal_if(ret != EM_OK,
			      "em_event_group_apply():%" PRI_STAT "", ret);

		ready = em_event_group_is_ready(eo_ctx->event_group);
		test_fatal_if(ready == EM_TRUE,
			      "'event_group' should NOT be ready yet");

		/*
		 * Allocate 'eo_ctx->event_count' number of DATA events and
		 * send them using the event group to trigger the notification
		 * events configured above with em_event_group_apply().
		 */
		for (i = 0; i < eo_ctx->event_count; i++) {
			em_event_t ev_data;
			event_group_test_t *egrp_test_data;

			if (unlikely(appl_shm->exit_flag))
				break;

			ev_data = em_alloc(sizeof(event_group_test_t),
					   EM_EVENT_TYPE_SW, egrp_shm->pool);
			test_fatal_if(ev_data == EM_EVENT_UNDEF,
				      "Event allocation failed!");

			egrp_test_data = em_event_pointer(ev_data);
			egrp_test_data->msg = MSG_DATA;
			egrp_test_data->start_time = ENV_TIME_NULL;
			/* How many times to increment and resend */
			egrp_test_data->increment = EVENT_GROUP_INCREMENT;
			/* Send events using the event group. */
			ret = em_send_group(ev_data, queue,
					    eo_ctx->event_group);
			if (unlikely(ret != EM_OK)) {
				em_free(ev_data);
				test_fatal_if(!appl_shm->exit_flag,
					      "Send grp:%" PRI_STAT "\n"
					      "Queue:%" PRI_QUEUE "",
					      ret, queue);
			}
		}
		break;

	case MSG_DATA:
		ready = em_event_group_is_ready(eo_ctx->event_group);
		test_fatal_if(ready == EM_TRUE,
			      "'event_group' should not be ready");

		ready = em_event_group_is_ready(eo_ctx->chained_event_group);
		test_fatal_if(ready == EM_TRUE,
			      "chained ev-grp shouldn't be ready yet");

		/* Update the count of data events received by this core */
		egrp_shm->core_stats[em_core_id()].rcv_ev_cnt += 1;

		/*
		 * Test the em_event_group_increment() functionality.
		 *
		 * Note that the total number of events received will become
		 * larger than 'eo_ctx->event_count' before the notification
		 * 'MSG_DONE' is received.
		 */
		if (egroup_test->increment) {
			em_event_group_t event_group;

			egroup_test->increment--;

			/* Increment event count in group */
			ret = em_event_group_increment(1);
			test_fatal_if(ret != EM_OK,
				      "Event group incr:%" PRI_STAT "", ret);

			/* Get the current event group */
			event_group = em_event_group_current();

			/* Resend event using the event group */
			ret = em_send_group(event, queue, event_group);
			if (unlikely(ret != EM_OK)) {
				em_free(event);
				test_fatal_if(!appl_shm->exit_flag,
					      "Send grp:%" PRI_STAT "\n"
					      "Queue:%" PRI_QUEUE "",
					      ret, queue);
			}
		} else {
			em_free(event);
		}
		break;

	case MSG_CHAIN:
		ready = em_event_group_is_ready(eo_ctx->event_group);
		test_fatal_if(ready != EM_TRUE, "event_group should be ready");

		ready = em_event_group_is_ready(eo_ctx->chained_event_group);
		test_fatal_if(ready == EM_TRUE,
			      "chained ev-grp shouldn't be ready yet");

		/* Calculate the number of processing time */
		end_time = env_time_global();
		start_time = egroup_test->start_time;
		diff_time = env_time_diff(end_time, start_time);

		/* Ignore the first round because of cold caches. */
		if (eo_ctx->total_rounds == 1) {
			eo_ctx->total_time = env_time_sum(eo_ctx->total_time,
							  diff_time);
			eo_ctx->total_time = env_time_sum(eo_ctx->total_time,
							  diff_time);
		} else if (eo_ctx->total_rounds > 1) {
			eo_ctx->total_time = env_time_sum(eo_ctx->total_time,
							  diff_time);
		}
		eo_ctx->total_rounds++;

		/* Sum up the amount of data events processed on each core */
		rcv_ev_cnt = 0;
		for (i = 0; i < em_core_count(); i++)
			rcv_ev_cnt += egrp_shm->core_stats[i].rcv_ev_cnt;

		/*
		 * The expected number of data events processed to trigger
		 * a notification event
		 */
		cfg_ev_cnt = (DATA_EVENTS * (1 + EVENT_GROUP_INCREMENT));

		/*
		 * Verify that the amount of received data events prior to
		 * this notification event is correct.
		 */
		test_fatal_if(rcv_ev_cnt != cfg_ev_cnt,
			      "Incorrect nbr of data events before notif:\t"
			      "%" PRIu64 " != %" PRIu64 "!",
			      rcv_ev_cnt, cfg_ev_cnt);

		APPL_PRINT("Event group notification event received after\t"
			   "%" PRIu64 " data events.\n"
			   "Cycles curr:%" PRIu64 ", ave:%" PRIu64 "\n",
			   rcv_ev_cnt,
			   env_time_to_cycles(diff_time, egrp_shm->cpu_hz),
			   env_time_to_cycles(eo_ctx->total_time,
					      egrp_shm->cpu_hz) /
			   eo_ctx->total_rounds);

		em_free(event);
		break;

	case MSG_DONE:
		ready = em_event_group_is_ready(eo_ctx->event_group);
		test_fatal_if(ready != EM_TRUE, "event_group should be ready");

		ready = em_event_group_is_ready(eo_ctx->chained_event_group);
		test_fatal_if(ready != EM_TRUE,
			      "chained ev-grp should be ready");

		APPL_PRINT("--- Chained event group done ---\n");

		/* Restart after some of delay to slow down result printouts */
		delay_spin(env_core_hz() / 100);

		memset(egrp_shm->core_stats, 0, sizeof(egrp_shm->core_stats));

		egroup_test->msg = MSG_START;

		ret = em_send(event, queue);
		if (unlikely(ret != EM_OK)) {
			em_free(event);
			test_fatal_if(!appl_shm->exit_flag,
				      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
				      ret, queue);
		}
		break;

	default:
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Bad msg (%" PRIu64 ")!", egroup_test->msg);
	};
}
