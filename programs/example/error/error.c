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
 * Event Machine error handler example.
 *
 * Demonstrate and test the Event Machine error handling functionality,
 * see the API calls em_error(), em_register_error_handler(),
 * em_eo_register_error_handler() etc.
 *
 * Three application EOs are created, each with a dedicated queue.
 * An application specific global error handler is registered (thus replacing
 * the EM default). Additionally EO A will register an EO specific error
 * handler.
 * When the EOs receive events (error_receive) they will generate errors by
 * explicit calls to em_error() and by calling EM-API functions with invalid
 * arguments. The registered error handlers simply print the error information
 * on screen.
 *
 * Note: Lots of the API-call return values are left unchecked for errors
 * (especially in setup) since the error handler demonstrated in this example
 * is not designed to handle 'real' errors.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

#define APPL_ESCOPE_INIT       1
#define APPL_ESCOPE_OTHER      2
#define APPL_ESCOPE_STR        3
#define APPL_ESCOPE_STR_Q      4
#define APPL_ESCOPE_STR_Q_SEQ  5

#define DELAY_SPIN_COUNT       50000000

/**
 * Error test event
 */
typedef struct {
	/* Destination queue for the reply event */
	em_queue_t dest;
	/* Sequence number */
	unsigned int seq;
	/* Indicate whether to report a fatal error or not */
	int fatal;
} error_event_t;

/**
 * EO context of error test application
 */
typedef union {
	struct {
		/* EO Id */
		em_eo_t eo;
		/* EO name */
		char name[16];
	};
	/* Pad to cache line size */
	uint8_t u8[ENV_CACHE_LINE_SIZE];
} eo_context_t;

/**
 * Error test shared memory
 */
typedef struct {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO A context from shared memory region */
	eo_context_t eo_error_a ENV_CACHE_LINE_ALIGNED;
	/* EO B context from shared memory region */
	eo_context_t eo_error_b ENV_CACHE_LINE_ALIGNED;
	/* EO C context from shared memory region */
	eo_context_t eo_error_c ENV_CACHE_LINE_ALIGNED;
	/* Queue IDs - shared vars, test is NOT concerned with perf */
	em_queue_t queue_a ENV_CACHE_LINE_ALIGNED;
	em_queue_t queue_b;
	em_queue_t queue_c;
} error_shm_t;

static ENV_LOCAL error_shm_t *error_shm;

/*
 * Local function prototypes
 */
static em_status_t
error_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
error_stop(void *eo_context, em_eo_t eo);

static void
error_receive(void *eo_context, em_event_t event, em_event_type_t type,
	      em_queue_t queue, void *q_ctx);

static em_status_t
global_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		     va_list args);

static em_status_t
eo_specific_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
			  va_list args);

static em_status_t
combined_error_handler(const char *handler_name, em_eo_t eo, em_status_t error,
		       em_escope_t escope, va_list args);

static void
delay_spin(const uint64_t spin_count);

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
 * Init of the Error Handler test application.
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void
test_init(void)
{
	int core = em_core_id();

	if (core == 0)
		error_shm = env_shared_reserve("ErrorSharedMem",
					       sizeof(error_shm_t));
	else
		error_shm = env_shared_lookup("ErrorSharedMem");

	if (error_shm == NULL)
		em_error(EM_ERROR_SET_FATAL(0xec0de), APPL_ESCOPE_INIT,
			 "Error init failed on EM-core: %u\n", em_core_id());
	else if (core == 0)
		memset(error_shm, 0, sizeof(error_shm_t));
}

/**
 * Startup of the Error Handler test application.
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
	em_event_t event;
	em_queue_t queue;
	em_status_t ret, start_ret;
	error_event_t *error;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		error_shm->pool = appl_conf->pools[0];
	else
		error_shm->pool = EM_POOL_DEFAULT;

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
		   error_shm->pool);

	/*
	 * Register the application specifig global error handler
	 * This replaces the EM internal default error handler
	 */
	ret = em_register_error_handler(global_error_handler);
	test_fatal_if(ret != EM_OK,
		      "Register global error handler:%" PRI_STAT "", ret);

	/* Create and start EO "A" */
	eo = em_eo_create("EO A", error_start, NULL, error_stop, NULL,
			  error_receive, &error_shm->eo_error_a);

	queue = em_queue_create("queue A", EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);

	ret = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(ret != EM_OK,
		      "EO add queue:%" PRI_STAT ".\n"
		      "EO:%" PRI_EO ", queue:%" PRI_QUEUE "",
		      ret, eo, queue);

	error_shm->queue_a = queue;

	/* Register an application 'EO A'-specific error handler */
	ret = em_eo_register_error_handler(eo, eo_specific_error_handler);
	test_fatal_if(ret != EM_OK,
		      "Register EO error handler:%" PRI_STAT "", ret);

	ret = em_eo_start_sync(eo, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO A start:%" PRI_STAT " %" PRI_STAT "");

	/* Create and start EO "B" */
	eo = em_eo_create("EO B", error_start, NULL, error_stop, NULL,
			  error_receive, &error_shm->eo_error_b);

	queue = em_queue_create("queue B", EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT,
				NULL);

	ret = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(ret != EM_OK,
		      "EO add queue:%" PRI_STAT ".\n"
		      "EO:%" PRI_EO ", queue:%" PRI_QUEUE "",
		      ret, eo, queue);

	error_shm->queue_b = queue;

	/*
	 * Note: No 'EO B' specific error handler. Use the application specific
	 * global error handler instead.
	 */

	ret = em_eo_start_sync(eo, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO B start:%" PRI_STAT " %" PRI_STAT "");

	/* Create and start EO "C" */
	eo = em_eo_create("EO C", error_start, NULL, error_stop, NULL,
			  error_receive, &error_shm->eo_error_c);
	queue = em_queue_create("queue C", EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_HIGH, EM_QUEUE_GROUP_DEFAULT,
				NULL);

	ret = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(ret != EM_OK,
		      "EO add queue:%" PRI_STAT ".\n"
		      "EO:%" PRI_EO ", queue:%" PRI_QUEUE "",
		      ret, eo, queue);

	error_shm->queue_c = queue;

	/*
	 * Note: No 'EO C' specific error handler. Use the application specific
	 * global error handler instead.
	 */

	ret = em_eo_start_sync(eo, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO C start:%" PRI_STAT " %" PRI_STAT "");

	/*
	 * Send an event to EO A.
	 * Store EO B's queue as the destination queue for EO A.
	 */
	event = em_alloc(sizeof(error_event_t), EM_EVENT_TYPE_SW,
			 error_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Alloc failed");

	error = em_event_pointer(event);
	error->dest = error_shm->queue_b;
	error->seq = 0;
	error->fatal = 0;

	ret = em_send(event, error_shm->queue_a);
	test_fatal_if(ret != EM_OK, "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
		      ret, error_shm->queue_a);

	/* Send event to EO C. No dest queue stored since fatal flag is set */
	event = em_alloc(sizeof(error_event_t), EM_EVENT_TYPE_SW,
			 error_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Alloc failed");

	error = em_event_pointer(event);
	error->dest = 0; /* Don't care, never resent */
	error->seq = 0;
	error->fatal = 1; /* Generate a fatal error when received */

	ret = em_send(event, error_shm->queue_c);
	test_fatal_if(ret != EM_OK, "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
		      ret, error_shm->queue_c);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	const em_eo_t eo_a = error_shm->eo_error_a.eo;
	const em_eo_t eo_b = error_shm->eo_error_b.eo;
	const em_eo_t eo_c = error_shm->eo_error_c.eo;
	em_status_t stat;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	stat = em_eo_stop_sync(eo_a);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO A stop failed!");
	stat = em_eo_delete(eo_a);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO A delete failed!");

	stat = em_eo_stop_sync(eo_b);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO B stop failed!");
	stat = em_eo_delete(eo_b);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO B delete failed!");

	stat = em_eo_stop_sync(eo_c);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO C stop failed!");
	stat = em_eo_delete(eo_c);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO C delete failed!");
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0)
		env_shared_free(error_shm);
}

/**
 * @private
 *
 * EO specific error handler.
 *
 * @return The function may not return depending on implementation/error
 * code/error scope. If it returns, the return value is the original
 * (or modified) error code from the caller.
 */
static em_status_t
eo_specific_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
			  va_list args)
{
	return combined_error_handler("Appl EO specific error handler", eo,
				      error, escope, args);
}

/**
 * @private
 *
 * Global error handler.
 *
 * @return The function may not return depending on implementation/error
 * code/error scope. If it returns, the return value is the original
 * (or modified) error code from the caller.
 */
static em_status_t
global_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		     va_list args)
{
	return combined_error_handler("Appl Global error handler     ", eo,
				      error, escope, args);
}

/**
 * @private
 *
 * Error handler implementation for both global and EO specific handlers
 * registered by the application.
 *
 * @return The function may not return depending on implementation/error
 * code/error scope. If it returns, the return value is the original
 * (or modified) error code from the caller.
 */
static em_status_t
combined_error_handler(const char *handler_name, em_eo_t eo, em_status_t error,
		       em_escope_t escope, va_list args)
{
	em_queue_t queue;
	const char *str;
	unsigned int seq;

	if (EM_ERROR_IS_FATAL(error)) {
		/*
		 * Application registered handling of FATAL errors.
		 * Just print it and return since it's a fake fatal error.
		 */
		APPL_PRINT("THIS IS A FATAL ERROR!!\n"
			   "%s: EO %" PRI_EO "  error 0x%" PRIxSTAT "  escope 0x%X\n"
			   "Return from fatal.\n\n",
			   handler_name, eo, error, escope);

		return error;
	}

	if (EM_ESCOPE_API(escope)) {
		/* EM API error: call em_error_format_string() */
		char error_str[256];

		em_error_format_string(error_str, sizeof(error_str), eo,
				       error, escope, args);

		APPL_PRINT("%s: EO %" PRI_EO "  error 0x%" PRIxSTAT "  escope 0x%X\n"
			   "- EM info: %s", handler_name, eo, error,
			   escope, error_str);
	} else {
		/* Application specific error handling. */
		switch (escope) {
		case APPL_ESCOPE_STR:
			str = va_arg(args, const char*);
			APPL_PRINT("%s: EO %" PRI_EO " error 0x%" PRIxSTAT " escope 0x%X\t"
				   "ARGS: %s\n", handler_name, eo, error,
				   escope, str);
			break;

		case APPL_ESCOPE_STR_Q:
			str = va_arg(args, const char*);
			queue = va_arg(args, em_queue_t);
			APPL_PRINT("%s: EO %" PRI_EO " error 0x%" PRIxSTAT " escope 0x%X\t"
				   "ARGS: %s %" PRI_QUEUE "\n", handler_name,
				   eo, error, escope, str, queue);
			break;

		case APPL_ESCOPE_STR_Q_SEQ:
			str = va_arg(args, const char*);
			queue = va_arg(args, em_queue_t);
			seq = va_arg(args, unsigned int);
			APPL_PRINT("%s: EO %" PRI_EO " error 0x%" PRIxSTAT " escope 0x%X\t"
				   "ARGS: %s %" PRI_QUEUE " %u\n", handler_name,
				   eo, error, escope, str, queue, seq);
			break;

		default:
			APPL_PRINT("%s: EO %" PRI_EO " error 0x%" PRIxSTAT " escope 0x%X\n",
				   handler_name, eo, error, escope);
		};
	}
	return error;
}

/**
 * @private
 *
 * EO receive function.
 *
 * Report various kinds of errors to demonstrate the EM error handling API.
 *
 */
static void
error_receive(void *eo_context, em_event_t event, em_event_type_t type,
	      em_queue_t queue, void *q_ctx)
{
	em_queue_t dest;
	eo_context_t *eo_ctx = eo_context;
	error_event_t *error;
	em_status_t ret;

	(void)type;
	(void)q_ctx;

	error = em_event_pointer(event);
	dest = error->dest;
	error->dest = queue;

	if (error->fatal) {
		APPL_PRINT("\nError log from %s [%u] on core %i!\n",
			   eo_ctx->name, error->seq, em_core_id());
		em_free(event);
		/* Report a fatal error */
		em_error(EM_ERROR_SET_FATAL(0xdead), 0);
		return;
	}

	APPL_PRINT("Error log from %s [%u] on core %i!\n", eo_ctx->name,
		   error->seq, em_core_id());

	/*       error   escope                 args  */
	em_error(0x1111, APPL_ESCOPE_OTHER);
	em_error(0x2222, APPL_ESCOPE_STR, "Second error");
	em_error(0x3333, APPL_ESCOPE_STR_Q, "Third  error", queue);
	em_error(0x4444, APPL_ESCOPE_STR_Q_SEQ, "Fourth error", queue,
		 error->seq);

	/* Example of an API call error - generates an EM API error */
	em_free(EM_EVENT_UNDEF);

	error->seq++;

	delay_spin(DELAY_SPIN_COUNT);

	ret = em_send(event, dest);
	test_fatal_if(ret != EM_OK, "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
		      ret, dest);

	/* Request a fatal error to be generated every 8th event by 'EO C' */
	if ((error->seq & 0x7) == 0x7) {
		/* Send a new event to EO 'C' to cause a fatal error */
		event = em_alloc(sizeof(error_event_t), EM_EVENT_TYPE_SW,
				 error_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF, "Alloc failed");

		error = em_event_pointer(event);
		error->dest = 0; /* Don't care, never resent */
		error->seq = 0;
		error->fatal = 1;

		ret = em_send(event, error_shm->queue_c);
		test_fatal_if(ret != EM_OK,
			      "Send:%" PRI_STAT " Queue:%" PRI_QUEUE "",
			      ret, error_shm->queue_c);
	}
}

/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
error_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_context_t *eo_ctx = eo_context;

	(void)conf;

	memset(eo_ctx, 0, sizeof(eo_context_t));

	eo_ctx->eo = eo;

	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	APPL_PRINT("Error test start (%s, eo id %" PRI_EO ")\n",
		   eo_ctx->name, eo);

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t
error_stop(void *eo_context, em_eo_t eo)
{
	em_status_t stat;

	(void)eo_context;

	APPL_PRINT("Error test stop function (EO:%" PRI_EO ")\n", eo);

	stat = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO:%" PRI_EO " rem all queues failed!", eo);

	return EM_OK;
}

/**
 * Delay spinloop
 */
static void
delay_spin(const uint64_t spin_count)
{
	env_atomic64_t dummy; /* use atomic to avoid optimization */
	uint64_t i;

	env_atomic64_init(&dummy);

	for (i = 0; i < spin_count; i++)
		env_atomic64_inc(&dummy);
}
