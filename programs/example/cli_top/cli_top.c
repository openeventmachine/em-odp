/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2024-2025 Nokia
 */

/**
 * @file
 *
 * EM application for testing EM CLI top command.
 *
 * The application sets up a periodic timeout for every core and waits for a given
 * time upon receiving an event. The wait time simulates how long a core handles
 * an event and the periodic timeout sets the frequency at which a core receives
 * events. The wait-time/timeout ratio can roughly represent the percentage a core
 * is in an active state.
 *
 * This application provides a reference for the CLI top command. For example,
 * with cli_top -w 200000 -t 1000000 the CLI top command should see %CPU at
 * around 20.
 *
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

#define APP_EO_NAME "Control EO"

typedef struct app_msg_t {
	/* for managing periodic timeouts */
	em_tmo_t tmo;
} app_msg_t;

/**
 * CLI top ref shared memory data
 */
typedef struct app_shm_t {
	/* Event pool used by this application */
	em_pool_t pool;
	em_queue_t eo_q;
	em_timer_t tmr;
	unsigned int core_count;
	em_queue_group_t qgrp[EM_MAX_CORES];
	em_tmo_t periodic_tmo[EM_MAX_CORES];
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} app_shm_t;

/* EM-core locals */
static ENV_LOCAL app_shm_t *app_shm;

#define SECOND_IN_NS 1000000000

/* Period in ns for a timeout, default 50ms */
static uint64_t timeout = 50000000;

/* Wait_time in ns used at EO receive function, the wait-time decides
 * the percentage (wait_time / timeout) a core is in active state
 *
 * Default: 25ms
 */
static uint64_t wait_time = 25000000;

/* Timer resolution in ns */
static uint64_t resolution = 1000;

/* No event mode, no events are sent to the EO, so while_idle hook
 * will be called, cpu usage should be 0%
 */
static bool no_event_mode;

const struct option longopts[] = {
	{"timeout",		required_argument, NULL, 't'},
	{"wait",		required_argument, NULL, 'w'},
	{"resolution",		required_argument, NULL, 'r'},
	{"no-event",		no_argument, NULL, 'n'},
	{"help",		no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

const char *shortopts = "t:w:r:nh";

/* Descriptions for above options, keep in sync! */
const char *descopts[] = {
	"Period for a timeout in ns, default 50ms",
	"Wait time in ns, default 25ms",
	"Timer resolution in ns, default 1000ns",
	"No event mode, no events are sent to the EO",
	"Print usage and exit",
	NULL
};

const char *instructions =
"The application sets up a periodic timeout for every core and\n"
"waits for a given time upon receiving an event. The wait time\n"
"simulates how long a core handles an event, and the periodic timeout\n"
"sets the frequency at which a core receives events. The wait-time/timeout\n"
"ratio can roughly represent the percentage of time a core is in an active state.\n\n"
"This application provides a reference for the CLI top command.\n"
"\n";

/* Local function prototypes */
static em_status_t app_eo_start(void *eo_ctx, em_eo_t eo,
				const em_eo_conf_t *conf);
static em_status_t app_eo_start_local(void *eo_ctx, em_eo_t eo);
static em_status_t app_eo_stop(void *eo_ctx, em_eo_t eo);
static void app_eo_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
			   em_queue_t queue, void *q_ctx);

static void usage(void)
{
	APPL_PRINT("cli_top: simple application with fixed core load\n\n%s", instructions);

	for (int i = 0; ; i++) {
		if (longopts[i].name == NULL)
			break;
		APPL_PRINT("-%c or --%-10s %s\n", longopts[i].val, longopts[i].name, descopts[i]);
	}
	APPL_PRINT("\n");
}

static int parse_args(int first, int argc, char *argv[])
{
	optind = first + 1; /* skip '--' */
	while (1) {
		int opt;
		int long_index;
		char *endptr;
		int64_t num;

		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 't': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			timeout = (uint64_t)num;
		}
		break;

		case 'w': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			wait_time = (uint64_t)num;
		}
		break;

		case 'r': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			resolution = (uint64_t)num;
		}
		break;

		case 'n':
			no_event_mode = true;
			break;

		case 'h':
		default:
			opterr = 0;
			usage();
			return 0;
		}
	}

	optind = 1; /* cm_setup() to parse again */
	return 1;
}

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
	/* Pick app-specific arguments after '--' */
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			break;
	}

	if (i < argc) {
		if (!parse_args(i, argc, argv))
			return 1;
	}

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
		/* First core creates the ShMem */
		app_shm = env_shared_reserve("TimerAppShMem", sizeof(app_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		app_shm = env_shared_lookup("TimerAppShMem");
	}

	if (app_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "init failed on EM-core: %u",
			   em_core_id());
	} else if (core == 0) {
		/* Initialize shared memory for EM app init */
		memset(app_shm, 0, sizeof(app_shm_t));
	}
}

/**
 * Startup of the CLI top ref EM application.
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
	em_tmo_t tmo;
	app_msg_t *msg;
	em_queue_t queue;
	em_status_t stat;
	em_event_t event;
	em_timer_attr_t attr;
	em_core_mask_t core_mask;
	em_timer_tick_t tick_now;
	em_timer_tick_t start_abs;
	em_timer_tick_t period_ticks;
	em_timer_res_param_t resparam;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		app_shm->pool = appl_conf->pools[0];
	else
		app_shm->pool = EM_POOL_DEFAULT;

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
		   app_shm->pool);

	APPL_PRINT("In order to get accurate result, the update rate for cli\n"
		   "top command must be at least %lu ns\n\n", timeout);

	test_fatal_if(app_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	test_fatal_if(appl_conf->core_count > EM_MAX_QUEUE_GROUPS,
		      "Not enough queue group (max: %d) to support given number of cores: %u",
		       EM_MAX_QUEUE_GROUPS, appl_conf->core_count);

	app_shm->core_count = appl_conf->core_count;

	/* Create EO */
	eo = em_eo_create(APP_EO_NAME,
			  (em_start_func_t)app_eo_start,
			  (em_start_local_func_t)app_eo_start_local,
			  (em_stop_func_t)app_eo_stop, NULL,
			  (em_receive_func_t)app_eo_receive, NULL);
	test_fatal_if(eo == EM_EO_UNDEF, "Failed to create EO!");

	/* No event mode, skip the creation of timers and queues */
	if (no_event_mode) {
		stat = em_eo_start_sync(eo, NULL, NULL);
		test_fatal_if(stat != EM_OK, "Failed to start EO!");
		return;
	}

	/*
	 * Create shared timer and store handle in shared memory.
	 * Accept all defaults.
	 */
	em_timer_attr_init(&attr);

	/* Going to change resolution, so need to check limits */
	memset(&resparam, 0, sizeof(em_timer_res_param_t));
	resparam.res_ns = resolution;
	stat = em_timer_res_capability(&resparam, EM_TIMER_CLKSRC_DEFAULT);
	test_fatal_if(stat != EM_OK, "Timer does not support resolution %u ns", resolution);

	strncpy(attr.name, "ExampleTimer", EM_TIMER_NAME_LEN);
	attr.resparam = resparam;
	attr.resparam.res_hz = 0;
	app_shm->tmr = em_timer_create(&attr);
	test_fatal_if(app_shm->tmr == EM_TIMER_UNDEF, "Failed to create timer!");

	period_ticks =  em_timer_ns_to_tick(app_shm->tmr, timeout);
	test_fatal_if(period_ticks < 1, "timer resolution is too low!\n");

	/* Ticks of 1 second */
	start_abs = em_timer_ns_to_tick(app_shm->tmr, SECOND_IN_NS);

	/* Start EO */
	stat = em_eo_start_sync(eo, NULL, NULL);
	test_fatal_if(stat != EM_OK, "Failed to start EO!");

	for (uint32_t i = 0; i < appl_conf->core_count; i++) {
		/* Start with EM core-0 (it's always running) */
		em_core_mask_zero(&core_mask);
		em_core_mask_set(i, &core_mask);

		app_shm->qgrp[i] = em_queue_group_create_sync(NULL, &core_mask);
		test_fatal_if(app_shm->qgrp[i] == EM_QUEUE_GROUP_UNDEF,
			      "Failed to create queue group!");

		queue = em_queue_create(NULL, EM_QUEUE_TYPE_PARALLEL,
					EM_QUEUE_PRIO_NORMAL, app_shm->qgrp[i], NULL);
		stat = em_eo_add_queue_sync(eo, queue);
		test_fatal_if(stat != EM_OK, "Failed to create queue!");

		/* Create periodic timer */
		tmo = em_tmo_create(app_shm->tmr, EM_TMO_FLAG_PERIODIC | EM_TMO_FLAG_NOSKIP, queue);
		test_fatal_if(tmo == EM_TMO_UNDEF, "Can't allocate tmo!\n");
		app_shm->periodic_tmo[i] = tmo;

		/* Allocate timeout event */
		event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, app_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate event!\n");

		msg = em_event_pointer(event);
		msg->tmo = app_shm->periodic_tmo[i];

		/* Setup periodic timeout (the ticks for 1ns) */
		tick_now = em_timer_current_tick(app_shm->tmr);
		stat = em_tmo_set_periodic(app_shm->periodic_tmo[i],
					   tick_now + start_abs, period_ticks, event);
		test_fatal_if(stat != EM_OK, "Can't activate tmo!\n");
	}
}

void test_stop(const appl_conf_t *appl_conf)
{
	em_eo_t eo;
	em_status_t ret;
	const int core = em_core_id();

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

	if (no_event_mode)
		return;

	ret = em_timer_delete(app_shm->tmr);
	test_fatal_if(ret != EM_OK,
		      "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      app_shm->tmr, ret);
}

void test_term(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (app_shm != NULL) {
		env_shared_free(app_shm);
		app_shm = NULL;
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t app_eo_start(void *eo_ctx, em_eo_t eo,
				const em_eo_conf_t *conf)
{
	em_timer_attr_t attr;
	em_timer_t tmr;
	int num_timers;

	(void)eo;
	(void)conf;
	(void)eo_ctx;

	APPL_PRINT("EO start\n");

	if (no_event_mode) {
		APPL_PRINT("No event mode enabled\n");
		return EM_OK;
	}

	/* Print timer info */
	num_timers = em_timer_list(&tmr, 1);
	APPL_PRINT("System has %d timer(s)\n", num_timers);

	if (em_timer_attr(app_shm->tmr, &attr) != EM_OK) {
		APPL_ERROR("Can't get timer info!\n");
		return EM_ERR_BAD_ID;
	}

	APPL_PRINT("Timer \"%s\" info:\n", attr.name);
	APPL_PRINT("  -resolution: %" PRIu64 " ns\n", attr.resparam.res_ns);
	APPL_PRINT("  -max_tmo: %" PRIu64 " us\n", attr.resparam.max_tmo / 1000);
	APPL_PRINT("  -min_tmo: %" PRIu64 " us\n", attr.resparam.min_tmo / 1000);
	APPL_PRINT("  -num_tmo: %d\n", attr.num_tmo);
	APPL_PRINT("  -clk_src: %d\n", attr.resparam.clk_src);
	APPL_PRINT("  -tick Hz: %" PRIu64 " hz\n", em_timer_freq(app_shm->tmr));

	return EM_OK;
}

/**
 * @private
 *
 * EO per thread start function.
 *
 */
static em_status_t app_eo_start_local(void *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;
	(void)eo;

	APPL_PRINT("EO local start\n");

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t app_eo_stop(void *eo_ctx, em_eo_t eo)
{
	em_event_t event = EM_EVENT_UNDEF;

	(void)eo_ctx;

	APPL_PRINT("EO stop\n");
	/* Remove and delete all of the EO's queues */
	em_status_t ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);

	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* Cancel and delete ongoing timeouts */
	for (uint32_t i  = 0; i < app_shm->core_count; i++) {
		if (app_shm->periodic_tmo[i] != EM_TMO_UNDEF) {
			if (em_tmo_state(app_shm->periodic_tmo[i]) == EM_TMO_STATE_ACTIVE)
				em_tmo_cancel(app_shm->periodic_tmo[i], &event);

			em_tmo_delete(app_shm->periodic_tmo[i]);
			if (event != EM_EVENT_UNDEF)
				em_free(event);
		}

		/* Delete queue groups */
		ret = em_queue_group_delete(app_shm->qgrp[i], 0, NULL);
		test_fatal_if(ret != EM_OK, "Remove queue group:%" PRI_STAT "", ret);
	}

	return EM_OK;
}

/**
 * @private
 *
 * EO receive function. This runs the example app after initialization.
 *
 * Wait 500ms
 *
 */
static void app_eo_receive(void *eo_ctx, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_ctx)
{
	(void)eo_ctx;
	(void)queue;
	(void)q_ctx;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(type != EM_EVENT_TYPE_SW))
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Invalid event type!\n");

	app_msg_t *msgin = (app_msg_t *)em_event_pointer(event);

	odp_time_wait_ns(wait_time);

	/* Ack periodic timeout, reuse the same event */
	em_status_t ret = em_tmo_ack(msgin->tmo, event);

	test_fatal_if(ret != EM_OK, "em_tmo_ack():%" PRI_STAT, ret);
}
