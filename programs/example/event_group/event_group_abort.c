/*
 *   Copyright (c) 2016, Nokia Solutions and Networks
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
 * Event Machine event group example/test using em_event_group_abort().
 *
 * Aborting an ongoing event group means that events sent with that group no
 * longer belong to a valid event group. Same is valid for excess events.
 * If more group events are sent than the applied count, the excess events,
 * once received don't belong to a valid event group.
 *
 * This example creates one EO with a parallel queue and allocates a defined
 * amount of event groups at startup that are reused. At round start the EO
 * allocates a defined amount of data events per event group and sends these to
 * the parallel queue. Events loop until the group is used as many times as
 * defined by the round. Event count and round stop count is set randomly.
 *
 * Counters track received valid and non-valid event group events and
 * statistics on event group API calls. Events that don't belong to a valid
 * group are removed from the loop and freed.  Once all event groups are
 * aborted, or notification events are received, a starting event is sent that
 * begins the next round.
 *
 * During the round everything possible is done to misuse the event group to
 * try and break it by getting the test or the groups in an undefined state.
 * Groups are incremented, assigned and ended at random.
 *
 * Every group has an associated lock and a generation variable that should
 * track the internal generation count. This example will exit with failure if
 * it receives en event from a valid event group that is sent during the
 * previous round. When an event is received, sent or em_event_group_apply()
 * for that group is called during round start, the lock is used to provide
 * atomicity to prevent the internal and external generation count to go out
 * of sync.
 *
 * To abort an event group first the notification events are requested with
 * em_event_group_get_notif() and then the group is aborted with
 * em_event_group_abort(). If the abort call succeeds, then the notifications
 * events can be freed and the group is ready for reuse. If the abort call
 * fails, then the notification events are already sent and not to be touched.
 */

#include <event_machine.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Test configuration
 */

/* Number of event groups. */
#define EVENT_GROUPS 30

/* Events per group */
#define EVENTS_PER_GROUP 128

/* Random count used for group event count and when to abort */
#define RANDOM_COUNT (rand() % 128000 + 1)

/**
 * The event for this test case.
 */
typedef struct {
	#define MSG_START 1
	#define MSG_DATA  2
	#define MSG_NOTIF 3
	uint64_t msg;
	uint64_t egrp_id;
	uint64_t egrp_gen;
} egrp_test_t;

/**
 * Every group has an associated lock and generation value
 */
typedef struct {
	em_event_group_t grp;
	uint64_t gen;
	env_spinlock_t lock;
} egrp_data_tbl;

/**
 * EO context used by the test
 */
typedef struct {
	em_eo_t eo;
	em_queue_t paral_queue;
	/* Table of event groups used in the test */
	egrp_data_tbl egrp_tbl[EVENT_GROUPS];
	/* Counters used to track groups and events */
	env_atomic64_t rcvd_group_events;
	env_atomic64_t rcvd_expired_events;
	env_atomic64_t rcvd_notif_events;
	env_atomic64_t increments;
	env_atomic64_t failed_increments;
	env_atomic64_t aborted_egrps;
	env_atomic64_t failed_aborts;
	env_atomic64_t assigns;
	env_atomic64_t failed_assigns;
	env_atomic64_t del_notifs;
	env_atomic64_t groups_left;
	env_atomic64_t group_counter[EVENT_GROUPS];
} eo_context_t;

/**
 * Event Group test shared data
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* Amount of received events per group before it is aborted */
	uint64_t stop_count;
	/* Group count that is applied to all groups */
	uint64_t target_count;
	/* Current test round/generation  */
	uint64_t round;
	/* EO context */
	eo_context_t test_eo_ctx ENV_CACHE_LINE_ALIGNED;
	/* pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
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

static void
send_start_event(void);

static void
abort_event_group(em_event_group_t event_group);

static void
print_round_start_info(void);

static void
print_round_end_info(void);

static void
send_test_events(void);

static void
init_counters(void);

static void
update_group_count(void);

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
 * Application error handler.
 * Suppress expected errors
 */
static em_status_t
error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
	      va_list args)
{
	switch (escope) {
	case EM_ESCOPE_EVENT_GROUP_UPDATE:
		break;

	case EM_ESCOPE_EVENT_GROUP_ABORT:
		break;

	case EM_ESCOPE_EVENT_GROUP_ASSIGN:
		break;

	case EM_ESCOPE_EVENT_GROUP_INCREMENT:
		break;

	default:
		error = test_error_handler(eo, error, escope, args);
	};

	return error;
}

void
send_test_events(void)
{
	em_status_t ret;
	em_notif_t notif_tbl[1];
	em_event_t test_event;
	egrp_test_t *egrp_event;
	egrp_data_tbl *egrp_elem;
	const em_queue_t paral_queue = egrp_shm->test_eo_ctx.paral_queue;
	int i, j;

	for (i = 0; i < EVENT_GROUPS; i++) {
		egrp_elem = &egrp_shm->test_eo_ctx.egrp_tbl[i];

		test_event = em_alloc(sizeof(egrp_test_t), EM_EVENT_TYPE_SW,
				      egrp_shm->pool);
		test_fatal_if(test_event == EM_EVENT_UNDEF,
			      "Event allocation failed!");
		egrp_event = em_event_pointer(test_event);
		egrp_event->msg = MSG_NOTIF;
		egrp_event->egrp_id = i;
		/* Set notif gen before lock section */
		egrp_event->egrp_gen = egrp_elem->gen + 1;
		notif_tbl[0].event = test_event;
		notif_tbl[0].queue = paral_queue;
		notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

		/* During lock em_event_group_apply() is called and group
		 * generation count is incremented to make sure that these
		 * two are always in sync.
		 */
		env_spinlock_lock(&egrp_elem->lock);

		ret = em_event_group_apply(egrp_elem->grp,
					   egrp_shm->target_count, 1,
					   notif_tbl);
		test_fatal_if(ret != EM_OK,
			      "em_event_group_apply():%" PRI_STAT "", ret);

		egrp_elem->gen++;
		env_atomic64_init(&egrp_shm->test_eo_ctx.group_counter[i]);

		env_spinlock_unlock(&egrp_elem->lock);

		/* Create data events */
		for (j = 0; j < EVENTS_PER_GROUP; j++) {
			test_event = em_alloc(sizeof(egrp_test_t),
					      EM_EVENT_TYPE_SW, egrp_shm->pool);
			test_fatal_if(test_event == EM_EVENT_UNDEF,
				      "Event allocation failed!");

			egrp_event = em_event_pointer(test_event);
			egrp_event->msg = MSG_DATA;
			egrp_event->egrp_id = i;
			egrp_event->egrp_gen = egrp_elem->gen;

			ret = em_send_group(test_event, paral_queue,
					    egrp_elem->grp);
			if (likely(ret == EM_OK))
				continue;
			/* error: */
			em_free(test_event);
			test_fatal_if(!appl_shm->exit_flag,
				      "Send:%" PRI_STAT " Q:%" PRI_QUEUE "",
				      ret, paral_queue);
			return; /* appl_shm->exit_flag set */
		}
	}
}

void
init_counters(void)
{
	env_atomic64_init(&egrp_shm->test_eo_ctx.rcvd_group_events);
	env_atomic64_init(&egrp_shm->test_eo_ctx.rcvd_expired_events);
	env_atomic64_init(&egrp_shm->test_eo_ctx.rcvd_notif_events);
	env_atomic64_init(&egrp_shm->test_eo_ctx.increments);
	env_atomic64_init(&egrp_shm->test_eo_ctx.failed_increments);
	env_atomic64_init(&egrp_shm->test_eo_ctx.failed_aborts);
	env_atomic64_init(&egrp_shm->test_eo_ctx.assigns);
	env_atomic64_init(&egrp_shm->test_eo_ctx.failed_assigns);
	env_atomic64_init(&egrp_shm->test_eo_ctx.aborted_egrps);
	env_atomic64_init(&egrp_shm->test_eo_ctx.del_notifs);
	env_atomic64_set(&egrp_shm->test_eo_ctx.groups_left, EVENT_GROUPS);

	/* Set random group count to be applied */
	egrp_shm->target_count = RANDOM_COUNT;
	/* Set random group stop count */
	egrp_shm->stop_count = RANDOM_COUNT;
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
	em_eo_t eo;
	em_queue_t queue;
	em_status_t ret, eo_start_ret = EM_ERROR;
	eo_context_t *eo_ctx;

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

	/*
	 * Create the event group test EO and a parallel queue, add the queue
	 * to the EO
	 */
	eo_ctx = &egrp_shm->test_eo_ctx;

	eo = em_eo_create("Evgrp-abort-test", egroup_start, NULL, egroup_stop,
			  NULL, egroup_receive, eo_ctx);
	test_fatal_if(eo == EM_EO_UNDEF, "EO creation failed!");
	eo_ctx->eo = eo;

	/* Create parallel queue for data */
	queue = em_queue_create("group test parallelQ",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);

	ret = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(ret != EM_OK, "EO add queue failed:%" PRI_STAT "\n"
		      "EO:%" PRI_EO ", queue:%" PRI_QUEUE "", ret, eo, queue);

	eo_ctx->paral_queue = queue;

	/* Specify EO error handler */
	em_eo_register_error_handler(eo, error_handler);

	int i;
	/* Alloc event groups used in the test */
	for (i = 0; i < EVENT_GROUPS; i++) {
		em_event_group_t egrp = em_event_group_create();

		test_fatal_if(egrp == EM_EVENT_GROUP_UNDEF,
			      "Event group creation failed!");
		eo_ctx->egrp_tbl[i].grp = egrp;
		/* Init group specific lock and gen */
		env_spinlock_init(&eo_ctx->egrp_tbl[i].lock);
		eo_ctx->egrp_tbl[i].gen = 0;
	}

	egrp_shm->round = 0;

	/* Start the EO (triggers the EO's start function 'egroup_start') */
	ret = em_eo_start_sync(eo, &eo_start_ret, NULL);

	test_fatal_if(ret != EM_OK || eo_start_ret != EM_OK,
		      "em_eo_start() failed! EO:%" PRI_EO "\n"
		      "ret:%" PRI_STAT ", EO-start-ret:%" PRI_STAT "",
		      eo, ret, eo_start_ret);
}

void
send_start_event(void)
{
	em_status_t ret;
	em_event_t start_event;
	egrp_test_t *start_event_ptr;
	const em_queue_t paral_queue = egrp_shm->test_eo_ctx.paral_queue;

	start_event = em_alloc(sizeof(egrp_test_t), EM_EVENT_TYPE_SW,
			       egrp_shm->pool);
	test_fatal_if(start_event == EM_EVENT_UNDEF,
		      "Event allocation failed!");

	start_event_ptr = em_event_pointer(start_event);

	start_event_ptr->msg = MSG_START;

	ret = em_send(start_event, paral_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(start_event);
		test_fatal_if(!appl_shm->exit_flag,
			      "Event send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, paral_queue);
	}
}

void
print_round_start_info(void)
{
	APPL_PRINT("-----------------------------------------\n");
	APPL_PRINT("\n--- Round %" PRIu64 "\n", egrp_shm->round);
	APPL_PRINT("\nCreated %i event group(s) with count of %" PRIu64 "\n",
		   EVENT_GROUPS, egrp_shm->target_count);
	APPL_PRINT("Abort group when received %" PRIu64 " events\n\n",
		   egrp_shm->stop_count);
}

void
print_round_end_info(void)
{
	APPL_PRINT("Evgrp events:\t\tValid:%" PRIu64 "\tExpired:%" PRIu64 "\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.rcvd_group_events),
		   env_atomic64_get(&egrp_shm->test_eo_ctx.rcvd_expired_events)
		   );

	APPL_PRINT("Evgrp increments:\tValid:%" PRIu64 "\tFailed:%" PRIu64 "\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.increments),
		   env_atomic64_get(&egrp_shm->test_eo_ctx.failed_increments));

	APPL_PRINT("Evgrp assigns:\t\tValid:%" PRIu64 "\tFailed:%" PRIu64 "\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.assigns),
		   env_atomic64_get(&egrp_shm->test_eo_ctx.failed_assigns));

	APPL_PRINT("Aborted %" PRIu64 " event groups\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.aborted_egrps));

	APPL_PRINT("Failed to abort %" PRIu64 " times\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.failed_aborts));

	APPL_PRINT("Received %" PRIu64 " notification events\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.rcvd_notif_events));

	APPL_PRINT("Freed %" PRIu64 " notification events\n",
		   env_atomic64_get(&egrp_shm->test_eo_ctx.del_notifs));
}

/**
 * Requests notif events, tries to abort the group and updates counters
 */
void
abort_event_group(em_event_group_t event_group)
{
	em_status_t ret;
	int returned_notifs, ready, i;
	em_notif_t notif_tbl[1] = { {.event = EM_EVENT_UNDEF} };

	/* Get notification events */
	returned_notifs = em_event_group_get_notif(event_group, 1, notif_tbl);

	/* Try to abort event group */
	ret = em_event_group_abort(event_group);

	if (ret == EM_OK) {
		/* Delete notif events */
		for (i = 0; i < returned_notifs; i++) {
			em_free(notif_tbl[i].event);
			env_atomic64_inc(&egrp_shm->test_eo_ctx.del_notifs);
		}
		/* Inc aborted counter */
		env_atomic64_inc(&egrp_shm->test_eo_ctx.aborted_egrps);

		/* Keeps track of aborted groups in a round */
		update_group_count();

		ready = em_event_group_is_ready(event_group);
		if (ready != EM_TRUE) {
			APPL_ERROR("em_event_group_is_ready():\n"
				   "should succeed after event group abort\n");
			exit(EXIT_FAILURE);
		}

	} else {
		env_atomic64_inc(&egrp_shm->test_eo_ctx.failed_aborts);
	}
}

/* Update current group count and start new round when last group is done */
void update_group_count(void)
{
	/* Check if this is the last group and hence the end of the round */
	int groups_left =
	env_atomic64_sub_return(&egrp_shm->test_eo_ctx.groups_left, 1);

	if (groups_left == 0) {
		print_round_end_info();
		/* Start new round */
		send_start_event();
	}
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo;
	em_status_t ret;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	/*
	 * Allow the other cores to run the dispatch loop with the 'exit_flag'
	 * set for a while to free the scheduled events as they are received.
	 */
	if (em_core_count() > 1)
		delay_spin(env_core_hz() / 100);

	eo = egrp_shm->test_eo_ctx.eo;

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
 */
static em_status_t
egroup_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	(void)eo;
	(void)eo_context;
	(void)conf;

	/* Start test by sending starting event */
	send_start_event();

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
	em_status_t ret;
	eo_context_t *eo_ctx = eo_context;
	em_event_group_t egrp;
	em_notif_t notif_tbl[1] = { {.event = EM_EVENT_UNDEF} };
	int num_notifs;

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* No more dispatching of the EO's events, egrps can be freed */

	for (int i = 0; i < EVENT_GROUPS; i++) {
		egrp = eo_ctx->egrp_tbl[i].grp;
		if (!em_event_group_is_ready(egrp)) {
			num_notifs = em_event_group_get_notif(egrp, 1,
							      notif_tbl);
			ret = em_event_group_abort(egrp);
			if (ret == EM_OK && num_notifs == 1)
				em_free(notif_tbl[0].event);
		}
		ret = em_event_group_delete(egrp);
		test_fatal_if(ret != EM_OK,
			      "egrp:%" PRI_EGRP "\n"
			      "delete:%" PRI_STAT " EO:%" PRI_EO "",
			      egrp, ret, eo);
	}

	return EM_OK;
}

/**
 * @private
 *
 * EO receive function.
 *
	1. Start test round by changing round type, setting up event groups and
	   sending events to parallel queue for looping.
	2. Keep sending events until defined count is reached and the group is
	   aborted. Discard events that don't belong to a valid group. If
	   notification event is sent, then that tries to abort the group, just
	   to decrement the counters to see when all groups are done.
	3. Go to step 1
 *
 */
static void
egroup_receive(void *eo_context, em_event_t event, em_event_type_t type,
	       em_queue_t queue, void *q_ctx)
{
	em_status_t ret;
	eo_context_t *eo_ctx = eo_context;
	egrp_test_t *rcvd_event = em_event_pointer(event);
	em_event_group_t current_egrp;
	egrp_data_tbl *egrp_data;
	uint64_t egrp_id;
	uint64_t current_count;

	(void)type;
	(void)q_ctx;
	(void)queue;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	switch (rcvd_event->msg) {
	case MSG_START:
		/* Free the start event */
		em_free(event);

		init_counters();

		/* Next round begins */
		egrp_shm->round++;

		print_round_start_info();

		/* Alloc and send all event group events  */
		send_test_events();

		break;

	case MSG_DATA:
		egrp_id = rcvd_event->egrp_id;
		egrp_data = &eo_ctx->egrp_tbl[egrp_id];

		/* Acquire lock when checking event validity. */
		env_spinlock_lock(&egrp_data->lock);

		current_egrp = em_event_group_current();
		/* Undef group events are from expired groups and discarded */
		if (current_egrp == EM_EVENT_GROUP_UNDEF) {
			env_spinlock_unlock(&egrp_data->lock);
			env_atomic64_inc(&eo_ctx->rcvd_expired_events);
			em_free(event);
			return;
		}

		/*
		 * If TRUE, then the event is from a valid event group but from
		 * a previous round, which should not be possible.
		 */
		test_fatal_if(rcvd_event->egrp_gen != egrp_data->gen,
			      "Current gen: %" PRIu64 ", received gen\t"
			      "%" PRIu64 " event: %p. Event group:\t"
			      "%" PRI_EGRP "\n", egrp_data->gen,
			      rcvd_event->egrp_gen, (void *)rcvd_event,
			      egrp_data->grp);

		env_spinlock_unlock(&egrp_data->lock);

		/* Total received valid group events */
		env_atomic64_inc(&eo_ctx->rcvd_group_events);

		/* Random Event group operations */
		if (rand() % 2) {
			ret = em_event_group_increment(1);
			if (ret == EM_OK)
				env_atomic64_inc(&eo_ctx->increments);
			else
				env_atomic64_inc(&eo_ctx->failed_increments);
		}
		/*
		 * Randomly select event group from the list and assign it.
		 * Assign may fail if the group is waiting to be applied again
		 * after it has completed or aborted.
		 */
		if (rand() % 2) {
			em_event_group_processing_end();

			em_event_group_t rand_egrp =
				eo_ctx->egrp_tbl[rand()	% EVENT_GROUPS].grp;
			ret = em_event_group_assign(rand_egrp);
			if (ret == EM_OK)
				env_atomic64_inc(&eo_ctx->assigns);
			else
				env_atomic64_inc(&eo_ctx->failed_assigns);
		}

		/* Increment the original event group received event counter */
		current_count =
		env_atomic64_add_return(&eo_ctx->group_counter[egrp_id], 1);

		/* Events loop until stop count */
		if (current_count != egrp_shm->stop_count) {
			/* Lock makes it possible to check that this event is
			 * not sent if the next round happens to begin on a
			 * another core. Same lock is used to change the group
			 * generation count when new round begins.
			 */
			env_spinlock_lock(&egrp_data->lock);

			/* Dont send old events forward */
			if (rcvd_event->egrp_gen != egrp_data->gen) {
				em_free(event);
			} else {
				ret = em_send_group(event, eo_ctx->paral_queue,
						    current_egrp);
				if (unlikely(ret != EM_OK)) {
					em_free(event);
					test_fatal_if(!appl_shm->exit_flag,
						      "Send:%" PRI_STAT "\t"
						      "Q:%" PRI_QUEUE "",
						      ret, eo_ctx->paral_queue);
				}
			}

			env_spinlock_unlock(&egrp_data->lock);
		} else {
			em_free(event);
			abort_event_group(current_egrp);
		}

		break;

	case MSG_NOTIF:
		egrp_data = &eo_ctx->egrp_tbl[rcvd_event->egrp_id];

		if (rcvd_event->egrp_gen != egrp_data->gen) {
			APPL_ERROR("Receiving notification events from\n"
				   "previous rounds should not be possible");
			exit(EXIT_FAILURE);
		}

		env_atomic64_inc(&eo_ctx->rcvd_notif_events);
		update_group_count();
		em_free(event);
		break;

	default:
		test_fatal_if(EM_TRUE, "Bad msg (%" PRIu64 ")!",
			      rcvd_event->msg);
		break;
	}
}
