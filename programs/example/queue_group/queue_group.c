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
 * Event Machine queue group feature test.
 *
 * Creates an EO with two queues: a notification queue and a data event queue.
 * The notif queue belongs to the default queue group and can be processed on
 * any core while the data queue belongs to a newly created queue group called
 * "test_qgrp". The EO-receive function receives a number of data events and
 * then modifies the test queue group (i.e. changes the cores allowed to
 * process events from the data event queue). The test is restarted when the
 * queue group has been modified enough times to include each core at least
 * once.
 */

#include <string.h>
#include <stdio.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Defines & macros
 */
#define TEST_PRINT_COUNT    5
#define TEST_QGRP_NAME_LEN  EM_QUEUE_GROUP_NAME_LEN
#define TEST_QGRP_NAME_BASE "QGrp"  /* Usage: QGrp001, QGrp002 */

/** The maximum number of cores this test supports */
#define MAX_CORES  64

/**
 * The number of data events to allocate, these are sent many rounds through
 * the data test_queue for each core mask in the tested queue group
 */
#define EVENT_DATA_ALLOC_NBR  (MAX_CORES * 16)

/** Round 'val' to the next multiple of 'N' */
#define ROUND_UP(val, N)  ((((val) + ((N) - 1)) / (N)) * (N))

/**
 * EO context used by the application
 *
 * Cache line alignment and padding taken care of in 'qgrp_shm_t'
 */
typedef struct app_eo_ctx_t {
	em_eo_t eo;

	em_queue_t notif_queue;
	em_queue_group_t notif_qgrp;

	em_queue_t test_queue;
	em_queue_type_t test_queue_type;
	em_queue_group_t test_qgrp;
	em_event_group_t event_group;

	char test_qgrp_name[TEST_QGRP_NAME_LEN];
	int test_qgrp_name_nbr;

	em_core_mask_t core_mask_max;

	uint64_t qgrp_modify_count;
	uint64_t modify_threshold;
	uint64_t print_threshold;
	uint64_t tot_modify_count;
	uint64_t tot_modify_count_check;
} app_eo_ctx_t;

/**
 * Queue context for the test queue (receives data events, NOT notifications)
 *
 * Cache line alignment and padding taken care of in 'qgrp_shm_t'
 */
typedef struct app_q_ctx_t {
	/*
	 * Use atomic operations to suit any queue type.
	 * An atomic queue does not need this but parallel and
	 * parallel-ordered do so opt to always use.
	 */
	env_atomic64_t event_count;
} app_q_ctx_t;

/**
 * Application event
 */
typedef union app_event_t {
	#define EVENT_NOTIF 1 /**< Event id: notification*/
	#define EVENT_DATA  2 /**< Event id: data */
	/** Id is first in all events */
	uint32_t id;

	/** Event: notification */
	struct {
		uint32_t id;
		enum {
			NOTIF_START_DONE,
			NOTIF_RESTART,
			NOTIF_QUEUE_GROUP_MODIFY_DONE_FIRST,
			NOTIF_QUEUE_GROUP_MODIFY_DONE,
			NOTIF_EVENT_GROUP_DATA_DONE
		} type;

		em_queue_group_t used_group;
		em_core_mask_t core_mask;
	} notif;

	/** Event: data */
	struct {
		uint32_t id;
		em_queue_group_t used_group;
	} data;
} app_event_t;

/**
 * Statistics for each core, pad to cache line size
 */
typedef union core_stat_t {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		uint64_t event_count;
	};
} core_stat_t;

COMPILE_TIME_ASSERT(sizeof(core_stat_t) == ENV_CACHE_LINE_SIZE,
		    CORE_STAT_T__SIZE_ERROR);

/**
 * Queue Group test shared memory
 */
typedef struct qgrp_shm_t {
	em_pool_t pool ENV_CACHE_LINE_ALIGNED;

	app_eo_ctx_t app_eo_ctx ENV_CACHE_LINE_ALIGNED;

	app_q_ctx_t app_q_ctx ENV_CACHE_LINE_ALIGNED;

	core_stat_t core_stat[MAX_CORES] ENV_CACHE_LINE_ALIGNED;
} qgrp_shm_t;

COMPILE_TIME_ASSERT(sizeof(qgrp_shm_t) % ENV_CACHE_LINE_SIZE == 0,
		    QGRP_SHM_T__SIZE_ERROR);
COMPILE_TIME_ASSERT(offsetof(qgrp_shm_t, app_eo_ctx) % ENV_CACHE_LINE_SIZE
		    == 0, OFFSETOF_EO_CTX_ERROR);
COMPILE_TIME_ASSERT(offsetof(qgrp_shm_t, app_q_ctx) % ENV_CACHE_LINE_SIZE
		    == 0, OFFSETOF_Q_CTX_ERROR);
COMPILE_TIME_ASSERT(offsetof(qgrp_shm_t, core_stat) % ENV_CACHE_LINE_SIZE
		    == 0, OFFSETOF_CORE_STAT_ERROR);

/** EM-core local pointer to shared memory */
static ENV_LOCAL qgrp_shm_t *qgrp_shm;

static void
receive(void *eo_context, em_event_t event, em_event_type_t type,
	em_queue_t queue, void *queue_context);

static inline void
receive_event_notif(app_eo_ctx_t *const eo_ctx, em_event_t event,
		    em_queue_t queue, app_q_ctx_t *const q_ctx);

static void
notif_start_done(app_eo_ctx_t *eo_ctx, em_event_t event, em_queue_t queue);
static void
notif_queue_group_modify_done(app_eo_ctx_t *eo_ctx, em_event_t event,
			      em_queue_t queue);
static void
notif_event_group_data_done(app_eo_ctx_t *eo_ctx, em_event_t event,
			    em_queue_t queue);

static inline void
receive_event_data(app_eo_ctx_t *const eo_ctx, em_event_t event,
		   em_queue_t queue, app_q_ctx_t *const q_ctx);

static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
stop(void *eo_context, em_eo_t eo);

static em_status_t
start_local(void *eo_context, em_eo_t eo);

static em_status_t
stop_local(void *eo_context, em_eo_t eo);

static void
next_core_mask(em_core_mask_t *new_mask, em_core_mask_t *max_mask, int count);

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
 * Init of the Queue Group test application.
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
		qgrp_shm = env_shared_reserve("QueueGroupSharedMem",
					      sizeof(qgrp_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		qgrp_shm = env_shared_lookup("QueueGroupSharedMem");
	}

	if (qgrp_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Queue Group test init failed on EM-core: %u\n",
			   em_core_id());
	} else if (core == 0) {
		memset(qgrp_shm, 0, sizeof(qgrp_shm_t));
	}
}

/**
 * Startup of the Queue Group test application.
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
	app_event_t *app_event;
	em_event_t event;
	em_queue_group_t default_group;
	em_queue_t notif_queue;
	em_event_group_t event_group;
	em_status_t err, start_err;
	em_eo_t eo;
	em_notif_t notif_tbl[1];
	int core_count = em_core_count();

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		qgrp_shm->pool = appl_conf->pools[0];
	else
		qgrp_shm->pool = EM_POOL_DEFAULT;

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
		   qgrp_shm->pool);

	test_fatal_if(qgrp_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	test_fatal_if(core_count > MAX_CORES,
		      "Test started on too many cores(%i)!\n"
		      "Max supported core count for this test is: %u\n",
		      core_count, MAX_CORES);

	eo = em_eo_create("test_appl_queue_group",
			  start, start_local, stop, stop_local,
			  receive, &qgrp_shm->app_eo_ctx);

	default_group = em_queue_group_find("default");
	/* Verify that the find-func worked correctly. */
	test_fatal_if(default_group != EM_QUEUE_GROUP_DEFAULT,
		      "Default queue group(%" PRI_QGRP ") not found!",
		      default_group);

	notif_queue = em_queue_create("notif_queue", EM_QUEUE_TYPE_ATOMIC,
				      EM_QUEUE_PRIO_HIGH, default_group, NULL);
	test_fatal_if(notif_queue == EM_QUEUE_UNDEF,
		      "Notification queue creation failed!");

	err = em_eo_add_queue_sync(eo, notif_queue);
	test_fatal_if(err != EM_OK,
		      "Notification queue add to EO failed:%" PRI_STAT "", err);

	event_group = em_event_group_create();
	test_fatal_if(event_group == EM_EVENT_GROUP_UNDEF,
		      "Event group creation failed!");

	qgrp_shm->app_eo_ctx.eo = eo;
	qgrp_shm->app_eo_ctx.notif_queue = notif_queue;
	qgrp_shm->app_eo_ctx.notif_qgrp = default_group;
	qgrp_shm->app_eo_ctx.event_group = event_group;

	APPL_PRINT("Starting EO:%" PRI_EO "\t"
		   "- Notification Queue=%" PRI_QUEUE "\n", eo, notif_queue);

	event = em_alloc(sizeof(app_event_t), EM_EVENT_TYPE_SW,
			 qgrp_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF,
		      "Notification event allocation failed");
	app_event = em_event_pointer(event);
	memset(app_event, 0, sizeof(*app_event));
	app_event->notif.id = EVENT_NOTIF;
	app_event->notif.type = NOTIF_START_DONE;
	/* Verify group when receiving */
	app_event->notif.used_group = default_group;

	notif_tbl[0].event = event;
	notif_tbl[0].queue = notif_queue;
	notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

	err = em_eo_start(eo, &start_err, NULL, 1, notif_tbl);
	test_fatal_if(err != EM_OK,
		      "em_eo_start(%" PRI_EO "):%" PRI_STAT "", eo, err);
	test_fatal_if(start_err != EM_OK,
		      "EO start function:%" PRI_STAT "",
		      start_err);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo = qgrp_shm->app_eo_ctx.eo;
	em_event_group_t egrp;
	em_notif_t notif_tbl[1] = { {.event = EM_EVENT_UNDEF} };
	int num_notifs;
	em_status_t err;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	err = em_eo_stop_sync(eo);
	test_fatal_if(err != EM_OK,
		      "EO stop:%" PRI_STAT " EO:%" PRI_EO "", err, eo);

	/* No more dispatching of the EO's events, egrp can be freed */

	egrp = qgrp_shm->app_eo_ctx.event_group;
	if (!em_event_group_is_ready(egrp)) {
		num_notifs = em_event_group_get_notif(egrp, 1, notif_tbl);
		err = em_event_group_abort(egrp);
		if (err == EM_OK && num_notifs == 1)
			em_free(notif_tbl[0].event);
	}
	err = em_event_group_delete(egrp);
	test_fatal_if(err != EM_OK,
		      "egrp:%" PRI_EGRP " delete:%" PRI_STAT " EO:%" PRI_EO "",
		      egrp, err, eo);
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(qgrp_shm);
		em_unregister_error_handler();
	}
}

/**
 * Receive function for the test EO
 */
static void
receive(void *eo_context, em_event_t event, em_event_type_t type,
	em_queue_t queue, void *queue_context)
{
	app_eo_ctx_t *const eo_ctx = eo_context;
	app_event_t *const app_event = em_event_pointer(event);
	/* Only set for the test_queue */
	app_q_ctx_t *const q_ctx = queue_context;

	test_fatal_if(em_get_type_major(type) != EM_EVENT_TYPE_SW,
		      "Unexpected event type: 0x%x", type);

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	switch (app_event->id) {
	case EVENT_NOTIF:
		receive_event_notif(eo_ctx, event, queue, q_ctx);
		break;
	case EVENT_DATA:
		receive_event_data(eo_ctx, event, queue, q_ctx);
		break;
	default:
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Unknown event id(%u)!", app_event->id);
		break;
	}
}

/**
 * Handle the notification events received through the notif_queue
 */
static inline void
receive_event_notif(app_eo_ctx_t *const eo_ctx, em_event_t event,
		    em_queue_t queue, app_q_ctx_t *const q_ctx)
{
	app_event_t *const app_event = em_event_pointer(event);
	em_status_t err;
	(void)q_ctx;

	switch (app_event->notif.type) {
	case NOTIF_RESTART:
		APPL_PRINT("\n"
			   "***********************************************\n"
			   "!!! Restarting test !!!\n"
			   "***********************************************\n"
			   "\n\n\n");
		eo_ctx->tot_modify_count_check = 0;
		notif_start_done(eo_ctx, event, queue);
		break;

	case NOTIF_START_DONE:
		notif_start_done(eo_ctx, event, queue);
		break;

	case NOTIF_QUEUE_GROUP_MODIFY_DONE_FIRST:
		err = em_eo_add_queue_sync(eo_ctx->eo, eo_ctx->test_queue);
		test_fatal_if(err != EM_OK,
			      "EO add queue:%" PRI_STAT "", err);
		notif_queue_group_modify_done(eo_ctx, event, queue);
		break;

	case NOTIF_QUEUE_GROUP_MODIFY_DONE:
		notif_queue_group_modify_done(eo_ctx, event, queue);
		break;

	case NOTIF_EVENT_GROUP_DATA_DONE:
		notif_event_group_data_done(eo_ctx, event, queue);
		break;

	default:
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Unknown notification type:%i!",
			   app_event->notif.type);
		break;
	}
}

/** Helper for receive_event_notif() */
static void
notif_start_done(app_eo_ctx_t *eo_ctx, em_event_t event, em_queue_t queue)
{
	em_queue_group_t new_qgrp;
	em_queue_type_t new_qtype;
	const char *new_qtype_str;
	em_core_mask_t core_mask;
	em_notif_t notif_tbl;
	em_status_t err;
	const em_queue_group_t qgrp_curr = em_queue_get_group(queue);
	app_event_t *const app_event = em_event_pointer(event);

	test_fatal_if(app_event->notif.used_group != qgrp_curr,
		      "Qgrp mismatch: %" PRI_QGRP "!=%" PRI_QGRP "!",
		      app_event->notif.used_group, qgrp_curr);

	/* Create a test queue group */
	snprintf(&eo_ctx->test_qgrp_name[0],
		 sizeof(eo_ctx->test_qgrp_name), "%s%03i",
		 TEST_QGRP_NAME_BASE, eo_ctx->test_qgrp_name_nbr);

	eo_ctx->test_qgrp_name[TEST_QGRP_NAME_LEN - 1] = '\0';
	eo_ctx->test_qgrp_name_nbr = (eo_ctx->test_qgrp_name_nbr + 1)
					% 1000; /* Range 0-999 */

	/* Start with EM core-0 (it's always running) */
	em_core_mask_zero(&core_mask);
	em_core_mask_set(0, &core_mask);

	/* Re-use event */
	app_event->notif.type = NOTIF_QUEUE_GROUP_MODIFY_DONE_FIRST;
	app_event->notif.used_group = eo_ctx->notif_qgrp;

	notif_tbl.event = event; /* = app_event->notif */
	notif_tbl.queue = queue;
	notif_tbl.egroup = EM_EVENT_GROUP_UNDEF;

	em_core_mask_copy(&app_event->notif.core_mask, &core_mask);

	/*
	 * Create the queue group!
	 */
	new_qgrp = em_queue_group_create(eo_ctx->test_qgrp_name, &core_mask,
					 1, &notif_tbl);
	test_fatal_if(new_qgrp == EM_QUEUE_GROUP_UNDEF,
		      "Queue group creation failed!");

	if (eo_ctx->test_qgrp != EM_QUEUE_GROUP_UNDEF) {
		/*
		 * Delete group - no need for notifs since 'modify to zero
		 * core mask' already done & queue deleted from group. Do the
		 * delete after the create to force creation of another
		 * queue group -> avoids always running the test with the same
		 * queue group.
		 */
		err = em_queue_group_delete(eo_ctx->test_qgrp, 0, NULL);
		test_fatal_if(err != EM_OK,
			      "Qgrp delete:%" PRI_STAT "", err);
	}
	/* Store the new queue group to use for this test round */
	eo_ctx->test_qgrp = new_qgrp;

	/*
	 * Create a test queue for data events. The queue belongs to
	 * the test queue group. Change the queue type for every new
	 * test run.
	 */
	switch (eo_ctx->test_queue_type) {
	case EM_QUEUE_TYPE_ATOMIC:
		new_qtype = EM_QUEUE_TYPE_PARALLEL;
		new_qtype_str = "PARALLEL";
		break;
	case EM_QUEUE_TYPE_PARALLEL:
		new_qtype = EM_QUEUE_TYPE_PARALLEL_ORDERED;
		new_qtype_str = "PARALLEL_ORDERED";
		break;
	default:
		new_qtype = EM_QUEUE_TYPE_ATOMIC;
		new_qtype_str = "ATOMIC";
		break;
	}
	eo_ctx->test_queue_type = new_qtype;
	eo_ctx->test_queue = em_queue_create("test_queue",
					     eo_ctx->test_queue_type,
					     EM_QUEUE_PRIO_NORMAL,
					     eo_ctx->test_qgrp, NULL);
	test_fatal_if(eo_ctx->test_queue == EM_QUEUE_UNDEF,
		      "Test queue creation failed!");

	APPL_PRINT("\n"
		   "Created test queue:%" PRI_QUEUE " type:%s(%u)\t"
		   "queue group:%" PRI_QGRP " (name:\"%s\")\n",
		   eo_ctx->test_queue, new_qtype_str, eo_ctx->test_queue_type,
		   eo_ctx->test_qgrp, eo_ctx->test_qgrp_name);

	memset(&qgrp_shm->app_q_ctx, 0, sizeof(qgrp_shm->app_q_ctx));
	env_atomic64_init(&qgrp_shm->app_q_ctx.event_count);

	err = em_queue_set_context(eo_ctx->test_queue, &qgrp_shm->app_q_ctx);
	test_fatal_if(err != EM_OK, "Set queue context:%" PRI_STAT "", err);
	/*
	 * Synchronize EO context. Event is sent through notification,
	 * which might have happened before we write the eo_ctx.
	 */
	env_sync_mem();
}

/** Helper for receive_event_notif() */
static void
notif_queue_group_modify_done(app_eo_ctx_t *eo_ctx, em_event_t event,
			      em_queue_t queue)
{
	em_status_t err;
	const em_queue_group_t qgrp_curr = em_queue_get_group(queue);
	app_event_t *const app_event = em_event_pointer(event);

	test_fatal_if(app_event->notif.used_group != qgrp_curr,
		      "Qgrp mismatch: %" PRI_QGRP "!=%" PRI_QGRP "!",
		      app_event->notif.used_group, qgrp_curr);

	if (unlikely(em_core_mask_iszero(&app_event->notif.core_mask))) {
		APPL_PRINT("\n"
			   "*************************************\n"
			   "All cores removed from QueueGroup!\n"
			   "*************************************\n");

		test_fatal_if(eo_ctx->tot_modify_count !=
			      eo_ctx->tot_modify_count_check,
			      "Modify count != actual count:\t"
			      "%" PRIu64 " vs %" PRIu64 "",
			      eo_ctx->tot_modify_count,
			      eo_ctx->tot_modify_count_check);

		err = em_eo_remove_queue_sync(eo_ctx->eo,
					      eo_ctx->test_queue);
		test_fatal_if(err != EM_OK,
			      "Remove test queue:%" PRI_STAT "", err);

		APPL_PRINT("Deleting test queue:%" PRI_QUEUE ",\t"
			   "Qgrp ID:%" PRI_QGRP " (name:\"%s\")\n",
			   eo_ctx->test_queue, eo_ctx->test_qgrp,
			   eo_ctx->test_qgrp_name);

		err = em_queue_delete(eo_ctx->test_queue);
		test_fatal_if(err != EM_OK,
			      "Delete test queue:%" PRI_STAT "", err);

		/*
		 * Delete the queue group later in restart after the
		 * creation of a new group. This forces the creation
		 * and usage of at least two different queue groups.
		 */
		app_event->notif.id = EVENT_NOTIF;
		app_event->notif.type = NOTIF_RESTART;
		app_event->notif.used_group = eo_ctx->notif_qgrp;
		err = em_send(event, eo_ctx->notif_queue);
		if (unlikely(err != EM_OK)) {
			em_free(event);
			test_fatal_if(!appl_shm->exit_flag,
				      "Send to notif queue:%" PRI_STAT "", err);
		}
	} else {
		em_notif_t egroup_notif_tbl[1];
		int i;

		/* Reuse the event */
		app_event->notif.id = EVENT_NOTIF;
		app_event->notif.type = NOTIF_EVENT_GROUP_DATA_DONE;
		app_event->notif.used_group = eo_ctx->notif_qgrp;

		egroup_notif_tbl[0].event = event;
		egroup_notif_tbl[0].queue = eo_ctx->notif_queue;
		egroup_notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

		err = em_event_group_apply(eo_ctx->event_group,
					   eo_ctx->modify_threshold, 1,
					   egroup_notif_tbl);
		test_fatal_if(err != EM_OK,
			      "em_event_group_apply():%" PRI_STAT "", err);

		for (i = 0; i < EVENT_DATA_ALLOC_NBR; i++) {
			em_event_t ev_data = em_alloc(sizeof(app_event_t),
						      EM_EVENT_TYPE_SW,
						      qgrp_shm->pool);
			test_fatal_if(ev_data == EM_EVENT_UNDEF,
				      "Event alloc failed!");

			app_event_t *app_event = em_event_pointer(ev_data);

			app_event->id = EVENT_DATA;
			app_event->data.used_group = eo_ctx->test_qgrp;

			err = em_send_group(ev_data, eo_ctx->test_queue,
					    eo_ctx->event_group);
			if (unlikely(err != EM_OK)) {
				em_free(ev_data);
				test_fatal_if(!appl_shm->exit_flag,
					      "Send to test queue:%" PRI_STAT "",
					      err);
			}
		}
	}
}

/** Helper for receive_event_notif() */
static void
notif_event_group_data_done(app_eo_ctx_t *eo_ctx, em_event_t event,
			    em_queue_t queue)
{
	em_core_mask_t core_mask, used_mask;
	em_notif_t notif_tbl;
	em_status_t err;
	int core_count;
	int i;
	const em_queue_group_t qgrp_curr = em_queue_get_group(queue);
	app_event_t *const app_event = em_event_pointer(event);

	test_fatal_if(app_event->notif.used_group != qgrp_curr,
		      "Qgrp mismatch: %" PRI_QGRP "!=%" PRI_QGRP "!",
		      app_event->notif.used_group, qgrp_curr);

	uint64_t mod_cnt = ++eo_ctx->qgrp_modify_count;

	eo_ctx->tot_modify_count_check++;

	err = em_queue_group_get_mask(eo_ctx->test_qgrp, &used_mask);
	test_fatal_if(err != EM_OK,
		      "Get queue group mask:%" PRI_STAT "", err);

	/* Get the next core mask for the test group */
	next_core_mask(/*New*/ &core_mask, /*Max*/ &eo_ctx->core_mask_max,
		       eo_ctx->tot_modify_count_check);

	if (mod_cnt >= eo_ctx->print_threshold ||
	    em_core_mask_iszero(&core_mask)) {
		char used_mask_str[EM_CORE_MASK_STRLEN];
		char core_mask_str[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(used_mask_str, EM_CORE_MASK_STRLEN,
				   &used_mask);
		em_core_mask_tostr(core_mask_str, EM_CORE_MASK_STRLEN,
				   &core_mask);
		APPL_PRINT("\n"
			   "****************************************\n"
			   "Received %" PRIu64 " events on Q:%" PRI_QUEUE ":\n"
			   "    QueueGroup:%" PRI_QGRP ", Curr Coremask:%s\n"
			   "Now Modifying:\n"
			   "    QueueGroup:%" PRI_QGRP ",  New Coremask:%s\n"
			   "****************************************\n",
			   env_atomic64_get(&qgrp_shm->app_q_ctx.event_count),
			   eo_ctx->test_queue, eo_ctx->test_qgrp,
			   used_mask_str, eo_ctx->test_qgrp, core_mask_str);

		eo_ctx->qgrp_modify_count = 0;
	}

	/*
	 * Sanity check: verify that all cores that process the queue
	 * group actually received events and that other cores do not
	 * get any events.
	 */
	core_count = em_core_count();
	for (i = 0; i < core_count; i++) {
		const uint64_t ev_count = qgrp_shm->core_stat[i].event_count;
		char mstr[EM_CORE_MASK_STRLEN];

		if (em_core_mask_isset(i, &used_mask)) {
			if (unlikely(ev_count == 0)) {
				em_core_mask_tostr(mstr, EM_CORE_MASK_STRLEN,
						   &used_mask);
				test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
					   "No events on core%i, mask:%s",
					   i, mstr);
			}
		} else if (unlikely(ev_count > 0)) {
			em_core_mask_tostr(mstr, EM_CORE_MASK_STRLEN,
					   &used_mask);
			test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
				   "Events:%" PRIu64 " on inv.core%i, mask:%s",
				   ev_count, i, mstr);
		}
	}

	memset(qgrp_shm->core_stat, 0, sizeof(qgrp_shm->core_stat));
	env_atomic64_set(&qgrp_shm->app_q_ctx.event_count, 0);

	/* Reuse the event */
	app_event->id = EVENT_NOTIF;
	app_event->notif.type = NOTIF_QUEUE_GROUP_MODIFY_DONE;
	app_event->notif.used_group = eo_ctx->notif_qgrp;
	em_core_mask_copy(&app_event->notif.core_mask, &core_mask);

	notif_tbl.event = event;
	notif_tbl.queue = eo_ctx->notif_queue;
	notif_tbl.egroup = EM_EVENT_GROUP_UNDEF;

	err = em_queue_group_modify(eo_ctx->test_qgrp, &core_mask,
				    1, &notif_tbl);
	test_fatal_if(err != EM_OK,
		      "em_queue_group_modify():%" PRI_STAT "", err);
}

/**
 * Handle the test data events received through the test_queue
 *
 * Check that the queue group is valid and send the data back to the same
 * queue for another round.
 * The last event should trigger a notification event to be sent to the
 * notif_queue to begin the queue group modification sequence.
 */
static inline void
receive_event_data(app_eo_ctx_t *const eo_ctx, em_event_t event,
		   em_queue_t queue, app_q_ctx_t *const q_ctx)
{
	int core_id = em_core_id();
	app_event_t *const app_event = em_event_pointer(event);
	em_queue_group_t qgrp_curr = em_queue_get_group(queue);
	em_core_mask_t used_mask;
	em_status_t err;
	const uint64_t event_count =
		env_atomic64_add_return(&q_ctx->event_count, 1);
	qgrp_shm->core_stat[core_id].event_count++;

	/* Verify that the queue group is correct & expected */
	test_fatal_if(app_event->data.used_group != qgrp_curr,
		      "Queue grp mismatch:%" PRI_QGRP "!=%" PRI_QGRP "",
		      app_event->data.used_group, qgrp_curr);

	/* Verify that this core is a valid receiver of events in this group */
	err = em_queue_group_get_mask(qgrp_curr, &used_mask);
	test_fatal_if(err != EM_OK,
		      "Get queue group mask:%" PRI_STAT "", err);

	if (unlikely(!em_core_mask_isset(core_id, &used_mask))) {
		char mask_str[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mask_str, EM_CORE_MASK_STRLEN, &used_mask);
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Core bit not set in core mask! core:%02i mask:%s",
			   core_id, mask_str);
	}

	/*
	 * Handle the test data event
	 */
	if (event_count <= eo_ctx->modify_threshold - EVENT_DATA_ALLOC_NBR) {
		/* Send the data event for another round */
		err = em_send_group(event, eo_ctx->test_queue,
				    eo_ctx->event_group);
		if (unlikely(err != EM_OK)) {
			em_free(event);
			test_fatal_if(!appl_shm->exit_flag,
				      "Send to test queue:%" PRI_STAT "", err);
		}
	} else if (event_count <= eo_ctx->modify_threshold) {
		/*
		 * Free the events for the last round, an event group
		 * notification event should be triggered when the last event
		 * has been processed
		 */
		em_free(event);
	} else {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xacdc,
			   "Invalid event count(%u)!", event_count);
	}
}

/**
 * Global start function for the test EO
 */
static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	app_eo_ctx_t *const eo_ctx = eo_context;
	uint64_t tot_modify_count = 0;
	uint64_t tmp;
	int ret;

	(void)eo;
	(void)conf;

	APPL_PRINT("Queue Group Test - Global EO Start\n");

	snprintf(&eo_ctx->test_qgrp_name[0],
		 sizeof(eo_ctx->test_qgrp_name),
		 "%s%03i", TEST_QGRP_NAME_BASE, 0);

	em_core_mask_zero(&eo_ctx->core_mask_max);
	em_core_mask_set_count(em_core_count(), &eo_ctx->core_mask_max);

	/*
	 * The values used below in calculations are derived from the way the
	 * next_core_mask() function calculates the next core mask to use.
	 */
	ret = em_core_mask_get_bits(&tmp, 1, &eo_ctx->core_mask_max);
	if (unlikely(ret != 1)) {
		char mask_str[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mask_str, EM_CORE_MASK_STRLEN,
				   &eo_ctx->core_mask_max);
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "em_core_mask_get_bits(coremask=%s), ret=%i",
			   mask_str, ret);
	}

	do {
		tot_modify_count += (tmp & 0xFF) + 1;
		tmp = (tmp >> 4);
		if (tmp < 0x10)
			break;
	} while (tmp);

	tot_modify_count -= 1;

	eo_ctx->tot_modify_count = tot_modify_count;
	eo_ctx->tot_modify_count_check = 0;

	eo_ctx->print_threshold = tot_modify_count / TEST_PRINT_COUNT;

	if (eo_ctx->print_threshold == 0)
		eo_ctx->print_threshold = 1;

	/*
	 *  256*15 - 1 is the maximum number of core masks tested when 64
	 * cores (max) are running this test.
	 */
	eo_ctx->modify_threshold =
		((256 * 15 * 0x1000) - 1) / tot_modify_count;
	eo_ctx->modify_threshold = ROUND_UP(eo_ctx->modify_threshold,
					    EVENT_DATA_ALLOC_NBR);

	APPL_PRINT("\n"
		   "*******************************************************\n"
		   "Test threshold values set:\n"
		   "  Tot group modifies:                     %" PRIu64 "\n"
		   "  Events received on group before modify: %" PRIu64 "\n"
		   "  Group modify print threshold:           %" PRIu64 "\n"
		   "*******************************************************\n"
		   "\n",
		   tot_modify_count, eo_ctx->modify_threshold,
		   eo_ctx->print_threshold);

	return EM_OK;
}

/**
 * Global stop function for the test EO
 */
static em_status_t
stop(void *eo_context, em_eo_t eo)
{
	em_status_t err;

	(void)eo_context;

	/* remove and delete all of the EO's queues */
	err = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(err != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      err, eo);

	/* delete the EO at the end of the stop-function */
	err = em_eo_delete(eo);
	test_fatal_if(err != EM_OK,
		      "EO delete:%" PRI_STAT " EO:%" PRI_EO "",
		      err, eo);
	APPL_PRINT("Queue Group Test - Global EO Stop\n");

	return EM_OK;
}

/**
 * Local start function for the test EO
 */
static em_status_t
start_local(void *eo_context, em_eo_t eo)
{
	(void)eo_context;
	(void)eo;

	APPL_PRINT("Queue Group Test - Local EO Start: core%02d\n",
		   em_core_id());
	return EM_OK;
}

/**
 * Local stop function for the test EO
 */
static em_status_t
stop_local(void *eo_context, em_eo_t eo)
{
	(void)eo_context;
	(void)eo;

	APPL_PRINT("Queue Group Test - Local EO Stop: core%02d\n",
		   em_core_id());
	return EM_OK;
}

/**
 * Update the core mask:
 * E.g. if max_mask is 0xFFFF: 0x0001-0x0100 (256 masks),
 *      0x0010->0x1000 (256 masks), 0x0100-0x0000 (255 masks)
 */
static void
next_core_mask(em_core_mask_t *new_mask, em_core_mask_t *max_mask, int count)
{
	uint64_t mask64 = ((uint64_t)(count % 256) + 1) << (4 * (count / 256));

	em_core_mask_zero(new_mask);
	em_core_mask_set_bits(&mask64, 1, new_mask);
	em_core_mask_and(new_mask, new_mask, max_mask);
}
