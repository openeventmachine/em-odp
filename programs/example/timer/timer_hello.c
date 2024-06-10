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
 * Event Machine Timer hello world example.
 *
 * Timer hello world example to show basic event timer usage. Creates a
 * single EO that starts a periodic and a random one-shot timeout.
 *
 * Exception/error management is simplified to focus on basic timer usage.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/* test app defines */
#define APP_MAX_TEXT_LEN	128	/* string length limit */
#define APP_TIMEOUT_MODULO_MS	30000	/* max random timeout */
#define APP_TIMEOUT_MIN_MS	100	/* minimum random timeout */
#define APP_PERIOD_MS		1000	/* heartbeat tick period */

#define APP_EO_NAME "Control EO"

/**
 * Example application message event
 */
typedef enum app_cmd_t {
	APP_CMD_TMO,		/* periodic timeout */
	APP_CMD_HELLO		/* random timeout */
} app_cmd_t;

typedef struct app_msg_t {
	app_cmd_t command;
	uint64_t count;
	char text[APP_MAX_TEXT_LEN];
	/* for managing periodic timeouts */
	em_tmo_t tmo;
} app_msg_t;

/**
 * EO context
 */
typedef struct app_eo_ctx_t {
	em_tmo_t periodic_tmo;
	em_tmo_t random_tmo;
	em_queue_t my_q;
	uint64_t hz;
} app_eo_ctx_t;

/**
 * Timer hello world shared memory data
 */
typedef struct timer_app_shm_t {
	/* Event pool used by this application */
	em_pool_t pool;
	/* EO context data */
	app_eo_ctx_t eo_context;
	em_queue_t eo_q;
	em_timer_t tmr;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} timer_app_shm_t;

/* EM-core locals */
static ENV_LOCAL timer_app_shm_t *m_shm;
static ENV_LOCAL unsigned int m_randseed;

/* Local function prototypes */
static em_status_t app_eo_start(app_eo_ctx_t *eo_ctx, em_eo_t eo,
				const em_eo_conf_t *conf);
static em_status_t app_eo_start_local(app_eo_ctx_t *eo_ctx, em_eo_t eo);
static em_status_t app_eo_stop(app_eo_ctx_t *eo_ctx, em_eo_t eo);
static void app_eo_receive(app_eo_ctx_t *eo_ctx, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_ctx);
static void new_rand_timeout(app_eo_ctx_t *eo_ctx);

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
 * Before EM - Init of the test application.
 *
 * The shared memory is needed if EM instance runs on multiple processes.
 * Doing it like this makes it possible to run the app both as threads (-t)
 * as well as processes (-p).
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
		/* first core creates the ShMem */
		m_shm = env_shared_reserve("TimerAppShMem",
					   sizeof(timer_app_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		m_shm = env_shared_lookup("TimerAppShMem");
	}

	if (m_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "init failed on EM-core: %u",
			   em_core_id());
	} else if (core == 0) {
		/* initialize shared memory for EM app init */
		memset(m_shm, 0, sizeof(timer_app_shm_t));
	}
}

/**
 * Startup of the timer hello EM application.
 *
 * At this point EM is up, but no EOs exist. EM API can be used to create
 * queues, EOs etc.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_start(const appl_conf_t *appl_conf)
{
	em_eo_t eo;
	em_timer_attr_t attr;
	em_queue_t queue;
	em_status_t stat;
	em_event_t event;
	app_msg_t *msg;
	app_eo_ctx_t *eo_ctx;
	uint64_t period;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		m_shm->pool = appl_conf->pools[0];
	else
		m_shm->pool = EM_POOL_DEFAULT;

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
		   m_shm->pool);

	test_fatal_if(m_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/* Create EO */
	eo = em_eo_create(APP_EO_NAME,
			  (em_start_func_t)app_eo_start,
			  (em_start_local_func_t)app_eo_start_local,
			  (em_stop_func_t)app_eo_stop, NULL,
			  (em_receive_func_t)app_eo_receive,
			  &m_shm->eo_context);
	test_fatal_if(eo == EM_EO_UNDEF, "Failed to create EO!");
	eo_ctx = &m_shm->eo_context;

	/* one basic queue */
	queue = em_queue_create("Timer hello Q",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create queue!");
	m_shm->eo_q = queue;

	/*
	 * Create shared timer and store handle in shared memory.
	 * Accept all defaults.
	 */
	em_timer_attr_init(&attr);
	strncpy(attr.name, "ExampleTimer", EM_TIMER_NAME_LEN);
	m_shm->tmr = em_timer_create(&attr);
	test_fatal_if(m_shm->tmr == EM_TIMER_UNDEF, "Failed to create timer!");

	/* Start EO */
	stat = em_eo_start_sync(eo, NULL, NULL);
	test_fatal_if(stat != EM_OK, "Failed to start EO!");

	/* create periodic timer */
	eo_ctx->periodic_tmo = em_tmo_create(m_shm->tmr, EM_TMO_FLAG_PERIODIC,
					     eo_ctx->my_q);
	test_fatal_if(eo_ctx->periodic_tmo == EM_TMO_UNDEF, "Can't allocate tmo!\n");

	/* allocate timeout event */
	event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, m_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate event!\n");

	msg = em_event_pointer(event);
	msg->command = APP_CMD_TMO;
	msg->tmo = eo_ctx->periodic_tmo;
	msg->count = 0;
	eo_ctx->hz = em_timer_get_freq(m_shm->tmr); /* save for later */
	if (eo_ctx->hz < 1000) { /* sanity check */
		APPL_ERROR("WARNING - timer hz very low!\n");
	}

	/* pre-allocate random timeout */
	eo_ctx->random_tmo = em_tmo_create(m_shm->tmr, EM_TMO_FLAG_ONESHOT,
					   eo_ctx->my_q);
	test_fatal_if(eo_ctx->random_tmo == EM_TMO_UNDEF, "Can't allocate tmo!\n");

	/* setup periodic timeout (the tick) */
	period = eo_ctx->hz / 1000; /* ticks for 1 ms */
	period *= APP_PERIOD_MS;
	stat = em_tmo_set_periodic(eo_ctx->periodic_tmo, 0, period, event);
	test_fatal_if(stat != EM_OK, "Can't activate tmo!\n");
}

void test_stop(const appl_conf_t *appl_conf)
{
	const int core = em_core_id();
	em_status_t ret;
	em_eo_t eo;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	eo = em_eo_find(APP_EO_NAME);
	test_fatal_if(eo == EM_EO_UNDEF, "Could not find EO:%s", APP_EO_NAME);

	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);

	ret = em_timer_delete(m_shm->tmr);
	test_fatal_if(ret != EM_OK,
		      "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      m_shm->tmr, ret);
}

void test_term(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (m_shm != NULL) {
		env_shared_free(m_shm);
		m_shm = NULL;
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t app_eo_start(app_eo_ctx_t *eo_ctx, em_eo_t eo,
				const em_eo_conf_t *conf)
{
	em_timer_attr_t attr;
	em_timer_t tmr;
	int num_timers;

	(void)eo;
	(void)conf;

	APPL_PRINT("EO start\n");

	/* print timer info */
	num_timers = em_timer_get_all(&tmr, 1);
	APPL_PRINT("System has %d timer(s)\n", num_timers);

	if (em_timer_get_attr(m_shm->tmr, &attr) != EM_OK) {
		APPL_ERROR("Can't get timer info!\n");
		return EM_ERR_BAD_ID;
	}

	APPL_PRINT("Timer \"%s\" info:\n", attr.name);
	APPL_PRINT("  -resolution: %" PRIu64 " ns\n", attr.resparam.res_ns);
	APPL_PRINT("  -max_tmo: %" PRIu64 " us\n", attr.resparam.max_tmo / 1000);
	APPL_PRINT("  -min_tmo: %" PRIu64 " us\n", attr.resparam.min_tmo / 1000);
	APPL_PRINT("  -num_tmo: %d\n", attr.num_tmo);
	APPL_PRINT("  -clk_src: %d\n", attr.resparam.clk_src);
	APPL_PRINT("  -tick Hz: %" PRIu64 " hz\n",
		   em_timer_get_freq(m_shm->tmr));

	/* init local EO context */
	eo_ctx->my_q = m_shm->eo_q;

	return EM_OK;
}

/**
 * @private
 *
 * EO per thread start function.
 *
 */
static em_status_t app_eo_start_local(app_eo_ctx_t *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;
	(void)eo;

	APPL_PRINT("EO local start\n");

	/* per-thread random seed */
	m_randseed = (unsigned int)em_timer_current_tick(m_shm->tmr);

	/* with a low frequency timer we actually get the same seed! */

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t app_eo_stop(app_eo_ctx_t *eo_ctx, em_eo_t eo)
{
	em_event_t event = EM_EVENT_UNDEF;
	em_status_t ret;

	APPL_PRINT("EO stop\n");

	/* cancel and delete ongoing timeouts */
	if (eo_ctx->periodic_tmo != EM_TMO_UNDEF) {
		em_tmo_delete(eo_ctx->periodic_tmo, &event);
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	}
	if (eo_ctx->random_tmo != EM_TMO_UNDEF) {
		event = EM_EVENT_UNDEF;
		em_tmo_delete(eo_ctx->random_tmo, &event);
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	}

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

/**
 * @private
 *
 * EO receive function. This runs the example app after initialization.
 *
 * Prints tick-tock at every periodic timeout and in parallel runs random
 * timeouts that trigger printing of a random quote.
 *
 */
static void app_eo_receive(app_eo_ctx_t *eo_ctx, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_ctx)
{
	int reuse = 0;
	em_status_t ret;

	(void)queue;
	(void)q_ctx;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (type == EM_EVENT_TYPE_SW) {
		app_msg_t *msgin = (app_msg_t *)em_event_pointer(event);

		switch (msgin->command) {
		case APP_CMD_TMO:
			/* print tick-tock */
			msgin->count++;
			if (msgin->count & 1)
				APPL_PRINT("%" PRIu64 ". ",
					   (msgin->count / 2) + 1);
			APPL_PRINT((msgin->count & 1) ? "tick\n" : "tock\n");

			/* ack periodic timeout, re-use the same event */
			ret = em_tmo_ack(msgin->tmo, event);
			test_fatal_if(ret != EM_OK,
				      "em_tmo_ack():%" PRI_STAT, ret);

			reuse = 1; /* do not free this event */

			/* get random timeouts going after 10th message */
			if (msgin->count == 10)
				new_rand_timeout(eo_ctx);
			break;

		case APP_CMD_HELLO:
			APPL_PRINT("%s\n\n", msgin->text);
			/* set next timeout */
			new_rand_timeout(eo_ctx);
			break;
		default:
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Invalid event!\n");
		}
	} else {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Invalid event type!\n");
	}

	/* normally free the received event */
	if (!reuse)
		em_free(event);
}

/* sets a new random timeout */
void new_rand_timeout(app_eo_ctx_t *eo_ctx)
{
	int rnd;
	app_msg_t *msg;
	uint64_t period;
	em_status_t stat;

	/* random timeouts allocate new event every time (could re-use) */
	em_event_t event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW,
				    m_shm->pool);
	if (!event) {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Can't allocate event!");
	}

	msg = em_event_pointer(event);
	msg->command = APP_CMD_HELLO;

	/* new timeout period APP_TIMEOUT_MIN_MS ... APP_TIMEOUT_MODULO */
	do {
		rnd = rand_r(&m_randseed);
		rnd %= APP_TIMEOUT_MODULO_MS;

	} while (rnd < APP_TIMEOUT_MIN_MS);

	snprintf(msg->text, APP_MAX_TEXT_LEN, "%d ms gone!\n", rnd);
	msg->text[APP_MAX_TEXT_LEN - 1] = 0;

	APPL_PRINT("Meditation time: what can you do in %d ms?\n", rnd);

	period = eo_ctx->hz / 1000;
	period *= rnd; /* rnd x ms */

	/* Alternate between set_rel() and set_abs(), roughly half of each */
	if (rnd > (APP_TIMEOUT_MODULO_MS + APP_TIMEOUT_MIN_MS) / 2)
		stat = em_tmo_set_rel(eo_ctx->random_tmo, period, event);
	else
		stat = em_tmo_set_abs(eo_ctx->random_tmo,
				      em_timer_current_tick(m_shm->tmr) +
				      period, event);
	if (stat != EM_OK)
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Can't activate tmo!\n");
}
