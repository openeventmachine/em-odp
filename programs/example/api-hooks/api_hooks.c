/*
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
 * Event Machine API callback hooks example.
 *
 * Based on the dispatcher callback example.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

#define SPIN_COUNT  50000000

/**
 * Test ping event
 */
typedef struct {
	/* Destination queue for the reply event */
	em_queue_t dest;
	/* Sequence number */
	unsigned int seq;
} ping_event_t;

/**
 * EO context in the dispatcher callback test
 */
typedef struct {
	/* Init before start */
	em_eo_t this_eo;
	em_eo_t other_eo;
	em_queue_t my_queue;
	int is_a;
	/* Init in start */
	char name[16];
} my_eo_context_t;

/**
 * Queue context data
 */
typedef struct {
	em_queue_t queue;
} my_queue_context_t;

/**
 * Test shared memory
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* Allocate EO contexts from shared memory region */
	my_eo_context_t eo_context_a;
	my_eo_context_t eo_context_b;
	/* Queue context */
	my_queue_context_t queue_context_a;
	my_queue_context_t queue_context_b;
	/* EO A's queue */
	em_queue_t queue_a;
	/* EO B's queue */
	em_queue_t queue_b;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} test_shm_t;

COMPILE_TIME_ASSERT((sizeof(test_shm_t) % ENV_CACHE_LINE_SIZE) == 0,
		    TEST_SHM_T__SIZE_ERROR);

/* EM-core local pointer to shared memory */
static ENV_LOCAL test_shm_t *test_shm;

static em_status_t
ping_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t
ping_stop(void *eo_ctx, em_eo_t eo);
static void
ping_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_ctx);

/* Callback & hook functions */
static void
enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
	 em_queue_t *queue, void **q_ctx);
static void
exit_cb(em_eo_t eo);

static void
alloc_hook(const em_event_t events[/*num_act*/], int num_act, int num_req,
	   size_t size, em_event_type_t type, em_pool_t pool);
static void
free_hook(const em_event_t events[], int num);
static void
send_hook(const em_event_t events[], int num,
	  em_queue_t queue, em_event_group_t event_group);

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
 * Init of the Dispatcher Callback test application.
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
		test_shm = env_shared_reserve("TestSharedMem",
					      sizeof(test_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		test_shm = env_shared_lookup("TestSharedMem");
	}

	if (test_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Dispatcher callback init failed on EM-core: %u\n",
			   em_core_id());
	} else if (core == 0) {
		memset(test_shm, 0, sizeof(test_shm_t));
	}
}

/**
 * Startup of the Dispatcher Callback test application.
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
	em_eo_t eo_a, eo_b;
	em_status_t ret;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		test_shm->pool = appl_conf->pools[0];
	else
		test_shm->pool = EM_POOL_DEFAULT;

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
		   test_shm->pool);

	test_fatal_if(test_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/* Create both EOs */
	eo_a = em_eo_create("EO A", ping_start, NULL, ping_stop, NULL,
			    ping_receive, &test_shm->eo_context_a);
	test_fatal_if(eo_a == EM_EO_UNDEF, "EO A creation failed!");

	eo_b = em_eo_create("EO B", ping_start, NULL, ping_stop, NULL,
			    ping_receive, &test_shm->eo_context_b);
	test_fatal_if(eo_b == EM_EO_UNDEF, "EO B creation failed!");

	/* Init EO contexts */
	test_shm->eo_context_a.this_eo = eo_a;
	test_shm->eo_context_a.other_eo = eo_b;
	test_shm->eo_context_a.is_a = 1;

	test_shm->eo_context_b.this_eo = eo_b;
	test_shm->eo_context_b.other_eo = eo_a;
	test_shm->eo_context_b.is_a = 0;

	/* Register/unregister callback functions.
	 *
	 * Callback functions may be registered multiple times and unregister
	 * function removes only the first matching callback.
	 *
	 * Register each callback twice and then remove one  - for testing
	 * purposes only.
	 */
	ret = em_dispatch_register_enter_cb(enter_cb);
	test_fatal_if(ret != EM_OK, "enter_cb() register failed!");
	ret = em_dispatch_register_enter_cb(enter_cb);
	test_fatal_if(ret != EM_OK, "enter_cb() register failed!");
	ret = em_dispatch_unregister_enter_cb(enter_cb);
	test_fatal_if(ret != EM_OK, "enter_cb() unregister failed!");

	ret = em_dispatch_register_exit_cb(exit_cb);
	test_fatal_if(ret != EM_OK, "exit_cb() register failed!");
	ret = em_dispatch_register_exit_cb(exit_cb);
	test_fatal_if(ret != EM_OK, "exit_cb() register failed!");
	ret = em_dispatch_unregister_exit_cb(exit_cb);
	test_fatal_if(ret != EM_OK, "exit_cb() unregister failed!");

	/*
	 * Register EM API hooks.
	 * Register each hook twice and then remove one - for testing
	 * purposes only.
	 */
	ret = em_hooks_register_alloc(alloc_hook);
	test_fatal_if(ret != EM_OK, "alloc_hook() register failed!");
	ret = em_hooks_register_alloc(alloc_hook);
	test_fatal_if(ret != EM_OK, "alloc_hook() register failed!");
	ret = em_hooks_unregister_alloc(alloc_hook);
	test_fatal_if(ret != EM_OK, "alloc_hook() unregister failed!");

	ret = em_hooks_register_free(free_hook);
	test_fatal_if(ret != EM_OK, "free_hook() register failed!");
	ret = em_hooks_register_free(free_hook);
	test_fatal_if(ret != EM_OK, "free_hook() register failed!");
	ret = em_hooks_unregister_free(free_hook);

	ret = em_hooks_register_send(send_hook);
	test_fatal_if(ret != EM_OK, "send_hook() register failed!");
	ret = em_hooks_register_send(send_hook);
	test_fatal_if(ret != EM_OK, "send_hook() register failed!");
	ret = em_hooks_unregister_send(send_hook);
	test_fatal_if(ret != EM_OK, "send_hook() unregister failed!");

	/* Start EO A */
	ret = em_eo_start_sync(eo_a, NULL, NULL);
	test_fatal_if(ret != EM_OK, "em_eo_start_sync(eo_a) failed!");

	/* Start EO B */
	ret = em_eo_start_sync(eo_b, NULL, NULL);
	test_fatal_if(ret != EM_OK, "em_eo_start_sync(eo_b) failed!");

	/*
	 * Send the first event to EO A's queue.
	 * Store the following destination queue into the event.
	 */
	em_event_t event;
	ping_event_t *ping;

	event = em_alloc(sizeof(ping_event_t), EM_EVENT_TYPE_SW,
			 test_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");

	ping = em_event_pointer(event);
	ping->dest = test_shm->queue_b;
	ping->seq = 0;

	ret = em_send(event, test_shm->queue_a);
	test_fatal_if(ret != EM_OK,
		      "em_send():%" PRI_STAT " Queue:%" PRI_QUEUE "",
		      ret, test_shm->queue_a);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	const em_eo_t eo_a = test_shm->eo_context_a.this_eo;
	const em_eo_t eo_b = test_shm->eo_context_b.this_eo;
	em_status_t stat;
	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	stat = em_dispatch_unregister_enter_cb(enter_cb);
	test_fatal_if(stat != EM_OK, "enter_cb() unregister failed!");

	stat = em_dispatch_unregister_exit_cb(exit_cb);
	test_fatal_if(stat != EM_OK, "exit_cb() unregister failed!");

	stat = em_hooks_unregister_alloc(alloc_hook);
	test_fatal_if(stat != EM_OK, "alloc_hook() unregister failed!");

	stat = em_hooks_unregister_free(free_hook);
	test_fatal_if(stat != EM_OK, "free_hook() unregister failed!");

	stat = em_hooks_unregister_send(send_hook);
	test_fatal_if(stat != EM_OK, "send_hook() unregister failed!");

	stat = em_eo_stop_sync(eo_a);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO A stop failed!");
	stat = em_eo_stop_sync(eo_b);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO B stop failed!");

	stat = em_eo_delete(eo_a);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO A delete failed!");
	stat = em_eo_delete(eo_b);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO B delete failed!");
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(test_shm);
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
ping_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	my_eo_context_t *const my_eo_ctx = eo_ctx;
	em_queue_t queue;
	em_status_t status;
	my_queue_context_t *my_q_ctx;
	const char *queue_name;

	(void)conf;

	/* Copy EO name */
	em_eo_get_name(eo, my_eo_ctx->name, sizeof(my_eo_ctx->name));

	if (my_eo_ctx->is_a) {
		queue_name = "queue A";
		my_q_ctx = &test_shm->queue_context_a;
	} else {
		queue_name = "queue B";
		my_q_ctx = &test_shm->queue_context_b;
	}

	queue = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);
	test_fatal_if(queue == EM_QUEUE_UNDEF,
		      "%s creation failed!", queue_name);

	my_eo_ctx->my_queue = queue; /* for ping_stop() */
	my_q_ctx->queue = queue;

	status = em_queue_set_context(queue, my_q_ctx);
	test_fatal_if(status != EM_OK,
		      "Set queue context:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " queue:%" PRI_QUEUE "", status, eo, queue);

	status = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(status != EM_OK,
		      "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue);

	APPL_PRINT("Test start %s: EO %" PRI_EO ", queue:%" PRI_QUEUE ".\n",
		   my_eo_ctx->name, eo, queue);

	if (my_eo_ctx->is_a)
		test_shm->queue_a = queue;
	else
		test_shm->queue_b = queue;

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t
ping_stop(void *eo_ctx, em_eo_t eo)
{
	my_eo_context_t *const my_eo_ctx = eo_ctx;
	em_queue_t queue = my_eo_ctx->my_queue;
	em_status_t status;

	APPL_PRINT("Dispatcher callback example stop (%s, eo id %" PRI_EO ")\n",
		   my_eo_ctx->name, eo);

	status = em_eo_remove_queue_sync(eo, queue);
	if (status != EM_OK)
		return status;

	status = em_queue_delete(queue);
	if (status != EM_OK)
		return status;

	return EM_OK;
}

/**
 * @private
 *
 * EO receive function.
 *
 * Print "Event received" and send back to the sender of the event.
 *
 */
static void
ping_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_ctx)
{
	my_eo_context_t *const my_eo_ctx = eo_ctx;
	em_queue_t dest;
	em_status_t status;
	ping_event_t *ping, *new_ping;
	em_event_t new_event;
	(void)type;
	(void)q_ctx;

	ping = em_event_pointer(event);

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	dest = ping->dest;
	ping->dest = queue;

	APPL_PRINT("** EO-rcv: Ping from EO:'%s'(%" PRI_EO ") on core%02d!\t"
		   "Queue:%" PRI_QUEUE "\t\t"
		   "Event:%" PRI_EVENT " Event-seq:%u\n",
		   my_eo_ctx->name, my_eo_ctx->this_eo, em_core_id(),
		   queue, event, ping->seq++);

	new_event = em_alloc(sizeof(ping_event_t), EM_EVENT_TYPE_SW,
			     test_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");
	new_ping = em_event_pointer(new_event);
	memcpy(new_ping, ping, sizeof(ping_event_t));
	em_free(event);

	delay_spin(SPIN_COUNT);

	status = em_send(new_event, dest);
	if (unlikely(status != EM_OK)) {
		em_free(new_event);
		test_fatal_if(!appl_shm->exit_flag,
			      "em_send():%" PRI_STAT "EO:%" PRI_EO "\n"
			      "Rcv-Q:%" PRI_QUEUE " Dst-Q:%" PRI_QUEUE "",
			      status, my_eo_ctx->this_eo, queue, dest);
	}
}

/**
 * Callback functions
 */

static void
enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
	 em_queue_t *queue, void **q_ctx)
{
	my_eo_context_t *my_eo_ctx = *eo_ctx;
	my_queue_context_t *my_q_ctx = *q_ctx;
	ping_event_t *ping;
	em_event_t event = events[0];

	(void)num; /* 1 event at a time here */
	(void)queue;

	ping = em_event_pointer(event);

	APPL_PRINT("\n"
		   "+  Dispatch enter callback  EO:'%s'(%" PRI_EO ")\t"
		   "Queue:%" PRI_QUEUE " on core%02i\t"
		   "Event:%" PRI_EVENT " Event-seq:%u\n",
		   my_eo_ctx->name, eo, my_q_ctx->queue, em_core_id(),
		   event, ping->seq);
}

static void
exit_cb(em_eo_t eo)
{
	my_eo_context_t *my_eo_ctx = em_eo_get_context(eo);

	APPL_PRINT("-  Dispatch exit callback  EO:'%s'(%" PRI_EO ")\n",
		   my_eo_ctx->name, eo);
}

static void
alloc_hook(const em_event_t events[/*num_act*/], int num_act, int num_req,
	   size_t size, em_event_type_t type, em_pool_t pool)
{
	em_eo_t eo, eo_a, eo_b;
	void *eo_ctx;
	my_eo_context_t *my_eo_ctx;

	(void)num_req;

	eo = em_eo_current();
	if (unlikely(eo == EM_EO_UNDEF))
		return;
	eo_ctx = em_eo_get_context(eo);
	if (unlikely(eo_ctx == NULL))
		return;

	/* Only print stuff for this test's EOs */
	if (unlikely(test_shm == NULL))
		return;
	eo_a = test_shm->eo_context_a.this_eo;
	eo_b = test_shm->eo_context_a.other_eo;
	if (eo != eo_a && eo != eo_b)
		return;

	my_eo_ctx = eo_ctx;

	APPL_PRINT("     Alloc-hook  EO:'%s'(%" PRI_EO ")\t"
		   "sz:%zu type:0x%x pool:%" PRI_POOL "\t\t"
		   "Events[%d]:",
		   my_eo_ctx->name, eo, size, type, pool,
		   num_act);
	for (int i = 0; i < num_act; i++)
		APPL_PRINT(" %" PRI_EVENT "", events[i]);
	APPL_PRINT("\n");
}

static void
free_hook(const em_event_t events[], int num)
{
	em_eo_t eo, eo_a, eo_b;
	void *eo_ctx;
	my_eo_context_t *my_eo_ctx;

	eo = em_eo_current();
	if (unlikely(eo == EM_EO_UNDEF))
		return;
	eo_ctx = em_eo_get_context(eo);
	if (unlikely(eo_ctx == NULL))
		return;

	/* Only print stuff for this test's EOs */
	if (unlikely(test_shm == NULL))
		return;
	eo_a = test_shm->eo_context_a.this_eo;
	eo_b = test_shm->eo_context_a.other_eo;
	if (eo != eo_a && eo != eo_b)
		return;

	my_eo_ctx = eo_ctx;

	APPL_PRINT("     Free-hook   EO:'%s'(%" PRI_EO ")\t\t\t\t\t\t"
		   "Events[%d]:", my_eo_ctx->name, eo, num);
	for (int i = 0; i < num; i++)
		APPL_PRINT(" %" PRI_EVENT "", events[i]);
	APPL_PRINT("\n");
}

static void
send_hook(const em_event_t events[], int num,
	  em_queue_t queue, em_event_group_t event_group)
{
	em_eo_t eo, eo_a, eo_b;
	void *eo_ctx;
	my_eo_context_t *my_eo_ctx;

	(void)events;
	(void)event_group;

	eo = em_eo_current();
	if (unlikely(eo == EM_EO_UNDEF))
		return;
	eo_ctx = em_eo_get_context(eo);
	if (unlikely(eo_ctx == NULL))
		return;

	/* Only print stuff for this test's EOs */
	if (unlikely(test_shm == NULL))
		return;
	eo_a = test_shm->eo_context_a.this_eo;
	eo_b = test_shm->eo_context_a.other_eo;
	if (eo != eo_a && eo != eo_b)
		return;

	my_eo_ctx = eo_ctx;

	APPL_PRINT("     Send-hook   EO:'%s'(%" PRI_EO ")\t"
		   "%d event(s)\tQueue:%" PRI_QUEUE " ==> %" PRI_QUEUE "\t"
		   "Events[%d]:",
		   my_eo_ctx->name, eo, num, em_queue_current(), queue, num);
	for (int i = 0; i < num; i++)
		APPL_PRINT(" %" PRI_EVENT "", events[i]);
	APPL_PRINT("\n");
}
