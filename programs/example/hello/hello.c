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
 * Event Machine hello world example.
 *
 * Creates two Execution Objects (EOs), each with a dedicated queue for
 * incoming events, and ping-pongs an event between the EOs while printing
 * "Hello world" each time the event is received.
 *
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
 * The hello world event
 */
typedef struct {
	/* Destination queue for the reply event */
	em_queue_t dest;
	/* Sequence number */
	unsigned int seq;
} hello_event_t;

/**
 * EO context in the hello world test
 */
typedef struct {
	/* Init before start */
	em_eo_t this_eo;
	em_eo_t other_eo;
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
 * Hello World shared memory
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
	/* Pad to cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} hello_shm_t;

COMPILE_TIME_ASSERT((sizeof(hello_shm_t) % ENV_CACHE_LINE_SIZE) == 0,
		    HELLO_SHM_T__SIZE_ERROR);

/* EM-core local pointer to shared memory */
static ENV_LOCAL hello_shm_t *hello_shm;

/*
 * Local function prototypes
 */
static em_status_t
hello_start(my_eo_context_t *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
hello_stop(my_eo_context_t *eo_ctx, em_eo_t eo);

static void
hello_receive_event(my_eo_context_t *eo_ctx, em_event_t event,
		    em_event_type_t type, em_queue_t queue,
		    my_queue_context_t *q_ctx);

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
 * Init of the Hello World test application.
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
		hello_shm = env_shared_reserve("HelloSharedMem",
					       sizeof(hello_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		hello_shm = env_shared_lookup("HelloSharedMem");
	}

	if (hello_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Hello init failed on EM-core: %u",
			   em_core_id());
	} else if (core == 0) {
		memset(hello_shm, 0, sizeof(hello_shm_t));
	}
}

/**
 * Startup of the Hello World test application.
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
	em_status_t ret, start_ret = EM_ERROR;
	char pool_name[EM_POOL_NAME_LEN];

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		hello_shm->pool = appl_conf->pools[0];
	else
		hello_shm->pool = EM_POOL_DEFAULT;

	em_pool_get_name(hello_shm->pool, pool_name, sizeof(pool_name));

	APPL_PRINT("\n"
		   "***********************************************************\n"
		   "EM APPLICATION: '%s' initializing:\n"
		   "  %s: %s() - EM-core:%i\n"
		   "  Application running on %d EM-cores (procs:%d, threads:%d)\n"
		   "  using event pool:%" PRI_POOL " - %s\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads,
		   hello_shm->pool, pool_name);

	test_fatal_if(hello_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/* Create both EOs */
	eo_a = em_eo_create("EO A",
			    (em_start_func_t)hello_start, NULL,
			    (em_stop_func_t)hello_stop, NULL,
			    (em_receive_func_t)hello_receive_event,
			    &hello_shm->eo_context_a);

	test_fatal_if(eo_a == EM_EO_UNDEF, "EO A creation failed!");

	eo_b = em_eo_create("EO B",
			    (em_start_func_t)hello_start, NULL,
			    (em_stop_func_t)hello_stop, NULL,
			    (em_receive_func_t)hello_receive_event,
			    &hello_shm->eo_context_b);

	test_fatal_if(eo_b == EM_EO_UNDEF, "EO B creation failed!");

	/* Init EO contexts */
	hello_shm->eo_context_a.this_eo = eo_a;
	hello_shm->eo_context_a.other_eo = eo_b;
	hello_shm->eo_context_a.is_a = 1;

	hello_shm->eo_context_b.this_eo = eo_b;
	hello_shm->eo_context_b.other_eo = eo_a;
	hello_shm->eo_context_b.is_a = 0;

	/* Start EO A */
	ret = em_eo_start_sync(eo_a, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO A start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);

	/* Start EO B */
	ret = em_eo_start_sync(eo_b, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO B start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	const em_eo_t eo_a = hello_shm->eo_context_a.this_eo;
	const em_eo_t eo_b = hello_shm->eo_context_b.this_eo;
	em_status_t stat;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	/*
	 * Stop both EOs, this will also disable all added queues.
	 * The EO-stop function 'hello_stop()' will be called for each EO once
	 * the asynchronous em_eo_stop() operation has been completed.
	 */
	stat = em_eo_stop_sync(eo_a);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO A stop failed!");

	stat = em_eo_stop_sync(eo_b);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO B stop failed!");
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(hello_shm);
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
hello_start(my_eo_context_t *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_queue_t queue;
	em_status_t status;
	my_queue_context_t *q_ctx;
	const char *queue_name;

	(void)conf;

	/* Copy EO name */
	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	if (eo_ctx->is_a) {
		queue_name = "queue A";
		q_ctx = &hello_shm->queue_context_a;
	} else {
		queue_name = "queue B";
		q_ctx = &hello_shm->queue_context_b;
	}

	queue = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);

	test_fatal_if(queue == EM_QUEUE_UNDEF, "%s creation failed!",
		      queue_name);

	q_ctx->queue = queue;
	status = em_queue_set_context(queue, q_ctx);

	test_fatal_if(status != EM_OK,
		      "Set queue context:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue);

	status = em_eo_add_queue_sync(eo, queue);

	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue);

	APPL_PRINT("Hello world started %s.\t"
		   "I'm EO %" PRI_EO ". My queue is %" PRI_QUEUE ".\n",
		   eo_ctx->name, eo, queue);

	if (eo_ctx->is_a) {
		/* Save queue ID for EO B. */
		hello_shm->queue_a = queue;
	} else {
		em_event_t event;
		hello_event_t *hello;

		/*
		 * Send the first event to EO A.
		 * Store queue ID as the destination queue for EO A.
		 */
		event = em_alloc(sizeof(hello_event_t), EM_EVENT_TYPE_SW,
				 hello_shm->pool);

		test_fatal_if(event == EM_EVENT_UNDEF,
			      "Event allocation failed!");

		hello = em_event_pointer(event);
		hello->dest = queue;
		hello->seq = 0;

		status = em_send(event, hello_shm->queue_a);

		test_fatal_if(status != EM_OK, "em_send():%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      status, eo, hello_shm->queue_a);
	}
	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t
hello_stop(my_eo_context_t *eo_ctx, em_eo_t eo)
{
	my_queue_context_t *q_ctx;
	em_status_t stat = EM_OK;

	APPL_PRINT("Hello world stop on EM-core %d (%s, eo id %" PRI_EO ")\n",
		   em_core_id(), eo_ctx->name, eo);

	if (eo_ctx->is_a)
		q_ctx = &hello_shm->queue_context_a;
	else
		q_ctx = &hello_shm->queue_context_b;

	stat = em_eo_remove_queue_sync(eo, q_ctx->queue);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO remove queue failed!");

	stat = em_queue_delete(q_ctx->queue);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Queue delete failed!");

	stat = em_eo_delete(eo);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO delete failed!");

	return stat;
}

/**
 * @private
 *
 * EO receive function.
 *
 * Print "Hello world" and send back to the sender of the event.
 *
 */
static void
hello_receive_event(my_eo_context_t *eo_ctx, em_event_t event,
		    em_event_type_t type, em_queue_t queue,
		    my_queue_context_t *q_ctx)
{
	em_queue_t dest;
	em_status_t status;
	hello_event_t *hello;
	(void)type;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	hello = em_event_pointer(event);

	dest = hello->dest;
	hello->dest = queue;

	APPL_PRINT("Hello world from %s!  My queue is %" PRI_QUEUE ".\t"
		   "I'm on core %02i.  Event seq is %u.\n",
		   eo_ctx->name, q_ctx->queue, em_core_id(), hello->seq++);

	delay_spin(SPIN_COUNT);

	status = em_send(event, dest);
	if (unlikely(status != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag,
			      "em_send():%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      status, eo_ctx->this_eo, hello_shm->queue_a);
	}
}
