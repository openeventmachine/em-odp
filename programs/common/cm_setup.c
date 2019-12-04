/*
 *   Copyright (c) 2015, Nokia Solutions and Networks
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
 * Event Machine common initialization functions
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_pool_config.h"
#include "cm_pktio.h"

#define USAGE_FMT \
"\n"									\
"Usage: %s APPL&EM-OPTIONS\n"						\
"  E.g. %s -c 0xfe -p\n"						\
"\n"									\
"Open Event Machine example application.\n"				\
"\n"									\
"Mandatory EM-OPTIONS:\n"						\
"  -c, --coremask          Select the cores to use, hexadecimal\n"	\
"  -p, --process-per-core  Running OpenEM with one process per core.\n"	\
"  -t, --thread-per-core   Running OpenEM with one thread per core.\n"	\
"    Select EITHER -p OR -t, but not both!\n"				\
"\n"									\
"Optional [APPL&EM-OPTIONS]\n"						\
"  -d, --device-id         Set the device-id, hexadecimal (defaults to 0)\n" \
"  -i, --eth-interface     Select the ethernet interface(s) to use\n"      \
"  -r, --dispatch-rounds   Set number of dispatch rounds (testing mostly)\n" \
"  -h, --help              Display help and exit.\n" \
"\n"

/**
 * Dispatch rounds for em_dispatch() during start-up to properly sync the
 * cores to enter the main dispatch loop at roughly the same time.
 */
#define STARTUP_DISPATCH_ROUNDS 16

/**
 * Dispatch rounds for em_dispatch() during program execution to regularly
 * return from dipatch and inspect the 'appl_shm->exit_flag' value. Program
 * termination will begin once a set 'appl_shm->exit_flags' has been noticed.
 */
#define EXIT_CHECK_DISPATCH_ROUNDS 20000

/**
 * Dispatch rounds for em_dispatch() during termination to properly sync the
 * cores and shutdown actions and allow for a graceful shutdown.
 */
#define TERM_DISPATCH_ROUNDS 16

/**
 * EM pool configuration, see cm_pool_config.h
 */
static em_pool_cfg_t default_pool_cfg = DEFAULT_POOL_CFG;
static em_pool_cfg_t appl_pool_1_cfg = APPL_POOL_1_CFG;
/* static em_pool_cfg_t appl_pool_2_cfg = APPL_POOL_2_CFG; */

static void
parse_args(int argc, char *argv[],
	   em_conf_t *em_conf /* out param */,
	   appl_conf_t *appl_conf /* out param */);
static void
verify_cpu_setup(int num_cpus, odp_cpumask_t *const cpu_mask);

static void
init_sync(sync_t *const sync, int num_cpus);

static int
create_odp_threads(odp_instance_t instance,
		   odp_cpumask_t *const cpu_mask, const int num_cpus,
		   odp_cpumask_t *const control_mask, const int num_control,
		   odp_cpumask_t *const worker_mask, const int num_worker,
		   int (*start_fn)(void *fn_arg), void *fn_arg,
		   odph_thread_t thread_tbl[/*out*/]);

static int
run_core_fn(void *arg);

static odp_platform_init_t *
set_platform_params(odp_platform_init_t *params);

static void
install_sig_handler(int signum, void (*sig_handler)(int), int flags);

static void
sigchld_handler(int sig ODP_UNUSED);
static void
sigint_handler(int signo ODP_UNUSED);

static void
usage(char *progname)
{
	APPL_PRINT(USAGE_FMT, NO_PATH(progname), NO_PATH(progname));
}

/**
 * Global pointer to common application shared memory
 */
appl_shm_t *appl_shm;

/**
 * ODP-thread table.
 * Allocate from shared memory so that also process-per-core mode works.
 */
typedef struct {
	odph_thread_t thread_tbl[MAX_THREADS];
} thread_tbl_shm_t;

int
cm_setup(int argc, char *argv[])
{
	em_conf_t em_conf_parse; /* tmp var filled in during arg parsing */
	em_conf_t *em_conf;
	appl_conf_t appl_conf_parse; /* tmp var filled during arg parsing */
	appl_conf_t *appl_conf;
	sync_t *sync;
	odp_shm_t shm;
	odp_init_t init_params;
	odp_platform_init_t plat_params[PLAT_PARAM_SIZE];
	odp_platform_init_t *plat_params_ptr;
	odph_helper_options_t helper_options;
	odp_cpumask_t *cpu_mask;
	odp_cpumask_t worker_mask;
	odp_cpumask_t control_mask;
	int num_cpus, num_worker, num_control;
	em_status_t stat;
	em_pool_t em_pool;
	int ret;
	odp_instance_t instance;

	/* use unbuffered stdout */
	if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
		APPL_EXIT_FAILURE("setvbuf() fails (errno(%i)=%s)",
				  errno, strerror(errno));

	/* Parse the command line arguments */
	memset(&em_conf_parse, 0, sizeof(em_conf_parse));
	memset(&appl_conf_parse, 0, sizeof(appl_conf_parse));
	parse_args(argc, argv, &em_conf_parse, &appl_conf_parse);

	memset(plat_params, 0, sizeof(plat_params));
	plat_params_ptr = set_platform_params(&plat_params[0]);

	/* Initialize the odp init params with 'default' values */
	odp_init_param_init(&init_params);

	/*
	 * The physical cpus & mask to use for the odp threads (i.e. em cores),
	 * contains both odp worker and control threads.
	 */
	cpu_mask = &em_conf_parse.phys_mask.odp_cpumask;
	num_cpus = em_conf_parse.core_count;
	verify_cpu_setup(num_cpus, cpu_mask);

	/* Initial setting: worker-cores=all and control-cores=none */
	odp_cpumask_copy(&worker_mask, cpu_mask);
	odp_cpumask_zero(&control_mask);
	/*
	 * Physical core-0, if used, is set as a control-core if more than
	 * one core is started. If just one core is started then the core is
	 * set as a worker-core regardless which physical core it is.
	 */
	if (num_cpus > 1 && odp_cpumask_isset(cpu_mask, 0)) {
		odp_cpumask_clr(&worker_mask, 0);
		odp_cpumask_set(&control_mask, 0);
	}

	/* Restrict odp worker threads to cores set in the 'worker_mask' */
	num_worker = odp_cpumask_count(&worker_mask);
	init_params.num_worker = num_worker;
	init_params.worker_cpus = &worker_mask;
	/* Restrict odp control threads to cores set in the 'control_mask' */
	num_control = odp_cpumask_count(&control_mask);
	init_params.num_control = num_control;
	init_params.control_cpus = &control_mask;
	/* Sanity check for core counts  */
	if (num_worker + num_control != num_cpus)
		APPL_EXIT_FAILURE("Inconsistent core count\n"
				  "worker:%d + control:%d != cpus:%d",
				  num_worker, num_control, num_cpus);
	/*
	 * List odp features not to be used in the examples. This may optimize
	 * performance. Note that a real application might need to change this!
	 */
	init_params.not_used.feat.cls = 1; /* don't use the odp classifier */
	init_params.not_used.feat.crypto = 1; /* don't use odp crypto */
	init_params.not_used.feat.ipsec = 1; /* don't use odp ipsec */
	init_params.not_used.feat.tm = 1; /* don't use the odp traffic manager*/
	/* Get the odp-helper options, initialized with odph_parse_options() */
	ret = odph_options(&helper_options /*out*/);
	if (ret != 0)
		APPL_EXIT_FAILURE("odph_options failed: %d", ret);
	/* Set the memory model to use for odp: thread or process */
	init_params.mem_model = helper_options.mem_model;

	ret = odp_init_global(&instance, &init_params, plat_params_ptr);
	if (ret != 0)
		APPL_EXIT_FAILURE("ODP global init failed:%d", ret);

	ret = odp_init_local(instance, ODP_THREAD_CONTROL);
	if (ret != 0)
		APPL_EXIT_FAILURE("ODP local init failed:%d", ret);

	/* Configure the scheduler */
	ret = odp_schedule_config(NULL);
	if (ret != 0)
		APPL_EXIT_FAILURE("ODP schedule config failed:%d", ret);

	APPL_PRINT("\n"
		   "*********************************************************\n"
		   "Setting up EM on ODP-version:\n"
		   "%s\n"
		   "*********************************************************\n"
		   "\n",
		   odp_version_impl_str());

	/* Reserve application shared memory in one chunk */
	shm = odp_shm_reserve("appl_shm", sizeof(appl_shm_t),
			      ODP_CACHE_LINE_SIZE, ODP_SHM_SINGLE_VA);
	if (unlikely(shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("appl shared mem reservation failed");
	appl_shm = odp_shm_addr(shm);
	if (unlikely(appl_shm == NULL))
		APPL_EXIT_FAILURE("obtaining shared mem addr failed");
	memset(appl_shm, 0, sizeof(appl_shm_t));

	em_conf = &appl_shm->em_conf;
	appl_conf = &appl_shm->appl_conf;
	sync = &appl_shm->sync;

	/* Copy the parsed conf into the shm conf */
	*em_conf = em_conf_parse;
	*appl_conf = appl_conf_parse;

	/* Initialize application start-up & exit synchronization */
	init_sync(sync, num_cpus);

	/* Event-Timer: disable=0, enable=1 */
	em_conf->event_timer = 1;

	/*
	 * Set the default pool config in em_conf, needed internally by EM
	 * at startup.
	 */
	em_conf->default_pool_cfg = default_pool_cfg;

	/*
	 * User can override the EM default log functions by giving logging
	 * funcs of their own - here we just use the default (shown explicitly)
	 */
	em_conf->log.log_fn = NULL;
	em_conf->log.vlog_fn = NULL;

	/* Packet-I/O */
	if (appl_conf->pktio.if_count > 0) {
		/*
		 * Request EM to poll input for pkts in the dispatch loop
		 */
		em_conf->input.input_poll_fn = input_poll; /* user fn */
		/*
		 * Request EM to drain buffered output in the dispatch loop
		 */
		em_conf->output.output_drain_fn = output_drain; /* user fn*/
	}

	/*
	 * Initialize the Event Machine. Every EM core still needs to call
	 * em_init_core() later.
	 * Note: the EM default pool config MUST be included in em_conf!
	 */
	stat = em_init(em_conf);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_init(), EM error:%" PRI_STAT "", stat);

	/* Create packet io */
	if (appl_conf->pktio.if_count > 0) {
		pktio_mem_reserve();
		pktio_pool_create(appl_conf->pktio.if_count);
		pktio_init(appl_conf);
		/* Create a pktio instance for each interface */
		for (int i = 0; i < appl_conf->pktio.if_count; i++) {
			int if_id = pktio_create(appl_conf->pktio.if_name[i],
						 num_cpus);
			if (unlikely(if_id < 0))
				APPL_EXIT_FAILURE("Cannot create pktio if:%s",
						  appl_conf->pktio.if_name[i]);
			/* Store the interface id */
			appl_conf->pktio.if_ids[i] = if_id;
		}
	}

	/*
	 * Create the other event pools used by the application.
	 * Note that em_term() will delete all remaining pools during
	 * termination.
	 */
	em_pool = em_pool_create(APPL_POOL_1_NAME, APPL_POOL_1,
				 &appl_pool_1_cfg);
	if (em_pool == EM_POOL_UNDEF || em_pool != APPL_POOL_1)
		APPL_EXIT_FAILURE("appl pool:%s(%" PRI_POOL ") create failed",
				  APPL_POOL_1_NAME, APPL_POOL_1);
	appl_conf->pools[0] = em_pool;
	appl_conf->num_pools = 1;

	if (em_conf->process_per_core) {
		/*
		 * Create a signal handler for the SIGCHLD signal that is sent
		 * to the parent process when a forked child process dies.
		 */
		install_sig_handler(SIGCHLD, sigchld_handler, 0);
	}

	/*
	 * Create a signal handler for the SIGINT (Ctrl-C) signal to flag
	 * program termination.
	 * Set the 'SA_RESETHAND'-flag to reset the SIGINT handler to its
	 * default disposition after the first handling to be able to stop
	 * execution if the application misbehaves.
	 */
	install_sig_handler(SIGINT, sigint_handler, SA_RESETHAND);

	/*
	 * Reserve the odp thread table from shared memory so that
	 * process-per-core mode also works.
	 */
	odp_shm_t thr_shm = odp_shm_reserve("thr_shm", sizeof(thread_tbl_shm_t),
					    ODP_CACHE_LINE_SIZE,
					    ODP_SHM_SINGLE_VA);

	if (unlikely(thr_shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("thread-tbl shared mem reservation failed");

	thread_tbl_shm_t *const thread_tbl_shm = odp_shm_addr(thr_shm);

	if (unlikely(thread_tbl_shm == NULL))
		APPL_EXIT_FAILURE("obtaining shared mem addr failed");
	memset(thread_tbl_shm, 0, sizeof(thread_tbl_shm_t));

	odph_thread_t *const thread_tbl = thread_tbl_shm->thread_tbl;

	/*
	 * Create the odp threads to use as EM-cores
	 */
	ret = create_odp_threads(instance, cpu_mask, num_cpus,
				 &control_mask, num_control,
				 &worker_mask, num_worker,
				 run_core_fn /*fn*/, appl_shm /*fn_arg*/,
				 thread_tbl /*out*/);
	if (ret != num_cpus)
		APPL_EXIT_FAILURE("ODP thread creation failed:%d", ret);

	/* Wait for the odp threads to return */
	ret = odph_thread_join(thread_tbl, num_cpus);
	if (ret != num_cpus)
		APPL_EXIT_FAILURE("ODP thread join failed:%d", ret);

	ret = odp_shm_free(thr_shm);
	if (ret != 0)
		APPL_EXIT_FAILURE("thread-tbl shared mem free failed:%d", ret);

	/* Free packet io resources */
	if (appl_conf->pktio.if_count > 0) {
		/* Stop, close and free the pktio resources */
		pktio_stop();
		pktio_close();
		pktio_deinit(appl_conf);
		pktio_pool_destroy();
		pktio_mem_free();
	}

	ret = odp_shm_free(shm);
	if (ret != 0)
		APPL_EXIT_FAILURE("appl shared mem free failed:%d", ret);

	ret = odp_term_local();
	if (ret != 0)
		APPL_EXIT_FAILURE("Last ODP local term failed:%d", ret);

	ret = odp_term_global(instance);
	if (ret != 0)
		APPL_EXIT_FAILURE("odp_term_global() failed:%d", ret);

	APPL_PRINT("\nDone - exit\n\n");

	return EXIT_SUCCESS;
}

static void
init_sync(sync_t *const sync, int num_cpus)
{
	odp_barrier_init(&sync->start_barrier, num_cpus);
	odp_barrier_init(&sync->exit_barrier, num_cpus);
	env_atomic64_init(&sync->exit_count);
	env_atomic64_init(&sync->enter_count);
}

static int
create_odp_threads(odp_instance_t instance,
		   odp_cpumask_t *const cpu_mask, const int num_cpus,
		   odp_cpumask_t *const control_mask, const int num_control,
		   odp_cpumask_t *const worker_mask, const int num_worker,
		   int (*start_fn)(void *fn_arg), void *fn_arg,
		   odph_thread_t thread_tbl[/*out*/])
{
	odph_thread_common_param_t thread_common_param;
	odph_thread_param_t thread_param_tbl[MAX_THREADS];
	int ret;

	/*
	 * Generate a thread summary for the user
	 */
	char cpumaskstr[ODP_CPUMASK_STR_SIZE];

	APPL_PRINT("num threads: %i\n"
		   "num worker:  %i\n"
		   "num control: %i\n",
		   num_cpus, num_worker, num_control);
	odp_cpumask_to_str(cpu_mask, cpumaskstr, sizeof(cpumaskstr));
	APPL_PRINT("cpu mask:            %s\n", cpumaskstr);
	odp_cpumask_to_str(worker_mask, cpumaskstr, sizeof(cpumaskstr));
	APPL_PRINT("worker thread mask:  %s\n", cpumaskstr);
	odp_cpumask_to_str(control_mask, cpumaskstr, sizeof(cpumaskstr));
	APPL_PRINT("control thread mask: %s\n", cpumaskstr);

	memset(&thread_common_param, 0, sizeof(thread_common_param));
	memset(thread_param_tbl, 0, sizeof(thread_param_tbl));

	thread_common_param.instance = instance;
	thread_common_param.cpumask = cpu_mask;
	thread_common_param.sync = 1; /* Synchronize thread start up */
	thread_common_param.share_param = 0;

	for (int i = 0; i < num_cpus; i++) {
		thread_param_tbl[i].start = start_fn;
		thread_param_tbl[i].arg = fn_arg;
		thread_param_tbl[i].thr_type = ODP_THREAD_WORKER;
	}

	/*
	 * Configure odp control threads, if any, to run as EM-cores
	 */
	int cpu = odp_cpumask_first(control_mask);
	int last = odp_cpumask_last(cpu_mask);

	while (cpu >= 0) {
		if (cpu > last)
			APPL_EXIT_FAILURE("Invalid control-cpu:%d", cpu);
		thread_param_tbl[cpu].thr_type = ODP_THREAD_CONTROL;
		cpu = odp_cpumask_next(control_mask, cpu);
	}

	/*
	 * Create odp worker threads to run as EM-cores
	 */
	ret = odph_thread_create(thread_tbl /*out*/,
				 &thread_common_param, thread_param_tbl,
				 num_cpus);
	return ret;
}

/**
 * Core runner - application entry on each EM-core
 *
 * Application setup and event dispatch loop run by each EM-core.
 * A call to em_init_core() MUST be made on each EM-core before using other
 * EM API functions to create EOs, queues etc. or calling em_dispatch().
 *
 * @param arg  passed arg actually of type 'appl_shm_t *', i.e. appl shared mem
 */
static int
run_core_fn(void *arg)
{
	odp_shm_t shm;
	appl_shm_t *appl_shm;
	void *shm_addr;
	em_conf_t *em_conf;
	appl_conf_t *appl_conf;
	sync_t *sync;
	em_status_t stat;
	int core_id;
	uint64_t cores, exit_count;

	/* thread: depend on the odp helper to call odp_init_local */
	/* process: parent called odp_init_local, fork creates copy for child */

	appl_shm = (appl_shm_t *)arg;

	/* Look up the appl shared memory - sanity check */
	shm = odp_shm_lookup("appl_shm");
	if (unlikely(shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("appl_shm lookup failed");
	shm_addr = odp_shm_addr(shm);
	if (unlikely(shm_addr == NULL || shm_addr != (void *)appl_shm))
		APPL_EXIT_FAILURE("obtaining shared mem addr failed:\n"
				  "shm_addr:%p appl_shm:%p",
				  shm_addr, appl_shm);

	em_conf = &appl_shm->em_conf;
	appl_conf = &appl_shm->appl_conf;
	sync = &appl_shm->sync;

	/*
	 * Initialize this thread of execution (proc, thread), i.e. EM-core
	 */
	stat = em_init_core();
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_init_core():%" PRI_STAT ", EM-core:%02d",
				  stat, em_core_id());

	APPL_PRINT("%s() on EM-core:%02d\n", __func__, em_core_id());

	odp_barrier_wait(&sync->start_barrier);

	if (appl_conf->pktio.if_count > 0)
		pktio_mem_lookup();

	odp_barrier_wait(&sync->start_barrier);

	/*
	 * EM is ready on this EM-core (= proc, thread or core)
	 * It is now OK to start creating EOs, queues etc.
	 *
	 * Note that only one core needs to create the shared memory, EO's,
	 * queues etc. needed by the application, all other cores need only
	 * look up the shared mem and go directly into the em_dispatch()-loop,
	 * where they are ready to process events as soon as the EOs have been
	 * started and queues enabled.
	 */
	core_id = em_core_id();
	cores = (uint64_t)em_core_count();

	/* Ensure all EM cores can find the default event pool */
	if (em_pool_find(EM_POOL_DEFAULT_NAME) != EM_POOL_DEFAULT)
		APPL_EXIT_FAILURE("em_pool_find(%s) c:%d",
				  EM_POOL_DEFAULT_NAME, core_id);

	if (core_id == 0) {
		/*
		 * Initialize the application and allocate shared memory.
		 */
		test_init();
	}

	odp_barrier_wait(&sync->start_barrier);

	if (core_id != 0) {
		/* Look up the shared memory */
		test_init();
	}

	const char *str = appl_conf->dispatch_rounds == 0 ?
				"forever" : "rounds";

	APPL_PRINT("Entering the event dispatch loop(%s=%d) on EM-core %d\n",
		   str, appl_conf->dispatch_rounds, core_id);

	odp_barrier_wait(&sync->start_barrier); /* to print pretty */

	/*
	 * Don't use barriers to sync the cores after this!
	 * EM synchronous API funcs (e.g. em_eo_start_sync()) blocks until the
	 * function has completed on all cores - a barrier might hinder a core
	 * from completing an operation.
	 */

	if (core_id == 0) {
		/*
		 * Create and start application EOs, pass the appl_conf.
		 */
		test_start(appl_conf);
	}

	/*
	 * Keep all cores dispatching until 'test_start()' has been
	 * completed in order to handle sync-API function calls and to enter
	 * the main dispatch loop almost at the same time.
	 */
	env_atomic64_inc(&sync->enter_count);
	do {
		em_dispatch(STARTUP_DISPATCH_ROUNDS);
		if (core_id == 0) {
			/* Start pktio if configured */
			if (appl_conf->pktio.if_count > 0)
				pktio_start();
			env_atomic64_inc(&sync->enter_count);
		}
	} while (env_atomic64_get(&sync->enter_count) <= cores);

	/*
	 * Enter the EM event dispatch loop (0==forever) on this EM-core.
	 */
	uint32_t dispatch_rounds = appl_conf->dispatch_rounds;
	uint32_t exit_check_rounds = EXIT_CHECK_DISPATCH_ROUNDS;
	uint32_t rounds;

	if (dispatch_rounds == 0) {
		/*
		 * Dispatch forever, in chunks of 'exit_check_rounds',
		 * or until 'exit_flag' is set by SIGINT (CTRL-C).
		 */
		while (!appl_shm->exit_flag)
			em_dispatch(exit_check_rounds);
	} else {
		/*
		 * Dispatch for 'dispatch_rounds' in chunks of 'rounds',
		 * or until 'exit_flag' is set by SIGINT (CTRL-C).
		 */
		rounds = MIN(dispatch_rounds, exit_check_rounds);
		do {
			em_dispatch(rounds);
			dispatch_rounds -= rounds;
		} while (dispatch_rounds > rounds && !appl_shm->exit_flag);

		if (dispatch_rounds > 0) {
			rounds = MIN(dispatch_rounds, rounds);
			em_dispatch(rounds);
		}
	}
	/*
	 * Allow apps one more round with 'exit_flag' set to flush events from
	 * the sched queues etc.
	 */
	if (!appl_shm->exit_flag)
		appl_shm->exit_flag = 1; /* potential race with SIGINT-handler*/
	em_dispatch(exit_check_rounds);

	/*
	 * Dispatch-loop done for application, prepare for controlled shutdown
	 */

	exit_count = env_atomic64_return_add(&sync->exit_count, 1);

	/* First core to exit dispatch stops the application */
	if (exit_count == 0) {
		if (appl_conf->pktio.if_count > 0)
			pktio_halt(); /* halt further pktio rx & tx */
		/*
		 * Stop and delete created application EOs
		 */
		test_stop(appl_conf);
	}

	/*
	 * Continue dispatching until all cores have exited the dispatch loop
	 * and until 'test_stop()' has been completed, the cores might have to
	 * react to teardown related events such as EM function completion
	 * events & notifs.
	 */
	do {
		em_dispatch(TERM_DISPATCH_ROUNDS);
		if (exit_count == 0) {
			/*
			 * First core to exit increments 'exit_count' twice -
			 * this ensures that all other cores will stay in this
			 * dispatch loop until the first core reaches the loop.
			 */
			env_atomic64_inc(&sync->exit_count);
		}
		exit_count = env_atomic64_get(&sync->exit_count);
	} while (exit_count <= cores);

	/*
	 * Proper application teardown should have been completed on all cores,
	 * still do some 'empty' dispatch rounds to drain all possibly
	 * remaining events in the system.
	 */
	while (em_dispatch(TERM_DISPATCH_ROUNDS) > 0)
		;

	APPL_PRINT("Left the event dispatch loop on EM-core %d\n", core_id);

	odp_barrier_wait(&sync->exit_barrier);

	if (core_id == 0) {
		/*
		 * Free allocated test resources
		 */
		test_term();
	}

	odp_barrier_wait(&sync->exit_barrier);

	stat = em_term_core();
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_term_core(%d):%" PRI_STAT "",
				  core_id, stat);

	odp_barrier_wait(&sync->exit_barrier);

	if (core_id == 0) {
		stat = em_term(em_conf);
		if (stat != EM_OK)
			APPL_EXIT_FAILURE("em_term(%d):%" PRI_STAT "",
					  core_id, stat);
	}

	odp_barrier_wait(&sync->exit_barrier);

	/* depend on the odp helper to call odp_term_local */

	return 0;
}

/**
 * Parse and store relevant command line arguments. Set config options for both
 * application and EM.
 *
 * EM options are stored into em_conf and application specific options into
 * appl_conf. Note that both application and EM parsing is done here since EM
 * should not, by design, be concerned with the parsing of options, instead
 * em_conf_t specifies the options needed by the EM-implementation (HW, device
 * and env specific).
 *
 * @param argc       Command line argument count
 * @param argv[]     Command line arguments
 * @param em_conf    EM config options parsed from argv[]
 * @param appl_conf  Application config options parsed from argv[]
 */
static void
parse_args(int argc, char *argv[],
	   em_conf_t *em_conf   /* out param */,
	   appl_conf_t *appl_conf /* out param */)
{
	static const struct option longopts[] = {
		{"coremask",         required_argument, NULL, 'c'},
		{"process-per-core", no_argument,       NULL, 'p'},
		{"thread-per-core",  no_argument,       NULL, 't'},
		{"device-id",        required_argument, NULL, 'd'},
		{"dispatch-rounds",  required_argument, NULL, 'r'},
		{"eth-interface",    required_argument, NULL, 'i'},
		{"help",             no_argument,       NULL, 'h'},
		{NULL, 0, NULL, 0}
	};
	static const char *shortopts = "+c:ptd:r:i:h";
	long device_id = -1;

	opterr = 0; /* don't complain about unknown options here */

	/*
	 * Parse the application & EM arguments and save core mask.
	 * Note:   Use '+' at the beginning of optstring - don't permute the
	 *	 contents of argv[].
	 * Note 2: Stops at "--"
	 */
	while (1) {
		int opt;
		int long_index;

		opt = getopt_long(argc, argv, shortopts,
				  longopts, &long_index);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 'c': {
			char *mask_str = optarg;
			char tmp_str[EM_CORE_MASK_STRLEN];

			/*
			 * Store the core mask for EM - usage depends on the
			 * process-per-core or thread-per-core mode selected.
			 */
			em_core_mask_zero(&em_conf->phys_mask);
			int err = em_core_mask_set_str(mask_str,
						       &em_conf->phys_mask);
			if (err)
				APPL_EXIT_FAILURE("Invalid coremask(%s) given",
						  mask_str);

			em_conf->core_count =
				em_core_mask_count(&em_conf->phys_mask);

			em_core_mask_tostr(tmp_str, sizeof(tmp_str),
					   &em_conf->phys_mask);
			APPL_PRINT("Coremask:   %s\n"
			       "Core Count: %i\n",
			       tmp_str, em_conf->core_count);
		}
		break;

		case 'p':
			em_conf->process_per_core = 1;
			break;

		case 't':
			em_conf->thread_per_core = 1;
			break;

		case 'd': {
			char *endptr;

			device_id = strtol(optarg, &endptr, 0);

			if (*endptr != '\0' ||
			    (uint64_t)device_id > UINT16_MAX)
				APPL_EXIT_FAILURE("Invalid device-id:%s",
						  optarg);

			em_conf->device_id = (uint16_t)(device_id & 0xffff);
		}
		break;

		case 'r':
			appl_conf->dispatch_rounds = atoi(optarg);
			if (atoi(optarg) < 0)
				APPL_EXIT_FAILURE("Invalid dispatch-rounds:%s",
						  optarg);
			break;

		case 'i': {
			int i;
			uint16_t len;
			char *name;

			for (name = strtok(optarg, ","), i = 0;
			     name != NULL; name = strtok(NULL, ","), i++) {
				if (i > IF_MAX_NUM - 1)
					APPL_EXIT_FAILURE("Too many if's:%d",
							  i + 1);

				len =
				strnlen(name,
					sizeof(appl_conf->pktio.if_name[i]));
				if (len + 1 > (uint16_t)IF_NAME_LEN)
					APPL_EXIT_FAILURE("Invalid if name:%s",
							  name);

				strncpy(appl_conf->pktio.if_name[i], name,
					len);
				appl_conf->pktio.if_name[i][len + 1] = '\0';
			}
			appl_conf->pktio.if_count = i;
		}
		break;

		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		default:
			usage(argv[0]);
			APPL_EXIT_FAILURE("Unknown option!");
			break;
		}
	}

	optind = 1; /* reset 'extern optind' from the getopt lib */

	/* Sanity check: */
	if (!em_conf->core_count) {
		usage(argv[0]);
		APPL_EXIT_FAILURE("Give mandatory coremask!");
	}

	/* Check if a device-id was given, if not use the default '0' */
	if (device_id == -1) /* not set */
		em_conf->device_id = 0;
	APPL_PRINT("Device-id:  0x%" PRIX16 "\n", em_conf->device_id);

	/* Sanity check: */
	if (!(em_conf->process_per_core ^ em_conf->thread_per_core)) {
		usage(argv[0]);
		APPL_EXIT_FAILURE("Select EITHER process-per-core OR thread-per-core!");
	}

	/*
	 * Initialize the odp helper options by calling odph_parse_options().
	 * In process-per-core mode additionally pass the '--odph_proc' option.
	 * This will correctly set 'helper_options.mem_model' in odp.
	 */
	odph_helper_options_t helper_options;
	char odp_proc_arg[] = "--odph_proc"; /* only used if '-p' given */
	int odp_argc = 1;
	char *odp_argv[2] = {argv[0], NULL};
	int res;

	if (em_conf->process_per_core) {
		odp_argc = 2;
		odp_argv[1] = odp_proc_arg;
	}

	/* Initialize the odp helper options */
	odph_parse_options(odp_argc, odp_argv);

	/* Get the odp-helper options for sanity checks */
	res = odph_options(&helper_options);
	if (res != 0)
		APPL_EXIT_FAILURE("odph_options():%d", res);
	if ((helper_options.mem_model == ODP_MEM_MODEL_THREAD &&
	     em_conf->process_per_core) ||
	    (helper_options.mem_model == ODP_MEM_MODEL_PROCESS &&
	     em_conf->thread_per_core))
		APPL_EXIT_FAILURE("EM vs ODP thread/proc option mismatch!");

	if (em_conf->thread_per_core)
		APPL_PRINT("Thread-per-core mode selected!\n");
	else
		APPL_PRINT("Process-per-core mode selected!\n");

	/*
	 * Set application specific config
	 */
	strncpy(appl_conf->name, NO_PATH(argv[0]), APPL_NAME_LEN);
	appl_conf->name[APPL_NAME_LEN - 1] = '\0'; /* '\0'-terminate str */

	if (em_conf->thread_per_core) {
		appl_conf->num_procs   = 1;
		appl_conf->num_threads = em_conf->core_count;
	} else {
		appl_conf->num_procs   = em_conf->core_count;
		appl_conf->num_threads = appl_conf->num_procs;
	}
}

/**
 * Verify the cpu setup - sanity check
 *
 * Verify the cpu count and mask against system values
 */
static void
verify_cpu_setup(int num_cpus, odp_cpumask_t *const cpumask)
{
	odp_cpumask_t invalid_mask;
	odp_cpumask_t check_mask;
	odp_cpumask_t zero_mask;
	int usable_cpus;
	cpu_set_t cpuset;
	int ret;

	if (num_cpus > MAX_THREADS)
		APPL_EXIT_FAILURE("Setup configured for max %d cores, not %d",
				  MAX_THREADS, num_cpus);

	odp_cpumask_zero(&invalid_mask);
	odp_cpumask_zero(&check_mask);
	odp_cpumask_zero(&zero_mask);
	usable_cpus = 0;

	CPU_ZERO(&cpuset);
	/* get the cpus/cores available to this application */
	ret = sched_getaffinity(0, sizeof(cpuset), &cpuset);
	if (ret < 0)
		APPL_EXIT_FAILURE("sched_getaffinity:%d errno(%d):%s",
				  ret, errno, strerror(errno));

	/* count the usable cpus and also record the invalid cpus */
	for (int i = 0; i < CPU_SETSIZE - 1; i++) {
		if (CPU_ISSET(i, &cpuset))
			usable_cpus++;
		else
			odp_cpumask_set(&invalid_mask, i);
	}

	/*
	 * Make sure no cpu in the cpumask is set in the invalid_mask.
	 * For a valid setup check_mask will be all-zero, otherwise it
	 * will contain the invalid cpus.
	 */
	odp_cpumask_and(&check_mask, &invalid_mask, cpumask);
	if (!odp_cpumask_equal(&zero_mask, &check_mask) ||
	    num_cpus > usable_cpus) {
		char cpus_str[ODP_CPUMASK_STR_SIZE];
		char check_str[ODP_CPUMASK_STR_SIZE];

		memset(cpus_str, '\0', sizeof(cpus_str));
		memset(check_str, '\0', sizeof(check_str));
		odp_cpumask_to_str(cpumask, cpus_str, sizeof(cpus_str));
		odp_cpumask_to_str(&check_mask, check_str, sizeof(check_str));

		APPL_EXIT_FAILURE("Invalid cpus - requested:%d available:%d\n"
				  "cpumask:%s of which invalid-cpus:%s",
				  num_cpus, usable_cpus, cpus_str, check_str);
	}
}

/* Used for passing cmd line arguments for odp_init_global() */
static odp_platform_init_t *
set_platform_params(odp_platform_init_t *params)
{
	/* DPDK is used */
	if (strstr(odp_version_impl_str(), "dpdk")) {
		int ret;
		int params_size = sizeof(odp_platform_init_t) * PLAT_PARAM_SIZE;

		ret = snprintf((char *)params, params_size, "-n 4");

		if (ret < 0 || ret > (params_size - 1))
			APPL_EXIT_FAILURE("snprintf(): ret=%d, errno(%i)=%s",
					  ret, errno, strerror(errno));
		return params;
	} else {
		return NULL;
	}
}

/**
 * Install a signal handler
 */
static void
install_sig_handler(int signum, void (*sig_handler)(int), int flags)
{
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);

	sa.sa_flags = SA_RESTART; /* restart interrupted system calls */
	sa.sa_flags |= flags;
	sa.sa_handler = sig_handler;

	if (sigaction(signum, &sa, NULL) == -1)
		APPL_EXIT_FAILURE("sigaction() fails (errno(%i)=%s)",
				  errno, strerror(errno));
}

/**
 * Signal handler for SIGINT (e.g. Ctrl-C to stop the program)
 */
static void
sigint_handler(int signo ODP_UNUSED)
{
	if (appl_shm == NULL)
		return;
	appl_shm->exit_flag = 1;
}

/**
 * Signal handler for SIGCHLD (parent receives when child process dies).
 */
static void
sigchld_handler(int sig ODP_UNUSED)
{
	int status;
	pid_t child;

	/* Child-process termination requested, normal tear-down, just return */
	if (appl_shm->exit_flag)
		return;

	/* Nonblocking waits until no more dead children are found */
	do {
		child = waitpid(-1, &status, WNOHANG);
	} while (child > 0);

	if (child == -1 && errno != ECHILD)
		_exit(EXIT_FAILURE);

	/*
	 * Exit the parent process - triggers SIGTERM in the remaining children
	 * (set by prctl(PR_SET_PDEATHSIG, SIGTERM)).
	 */
	_exit(EXIT_SUCCESS);
}

__attribute__((format(printf, 2, 0)))
int appl_vlog(em_log_level_t level, const char *fmt, va_list args)
{
	int r;
	FILE *logfd;

	switch (level) {
	case EM_LOG_DBG:
	case EM_LOG_PRINT:
		logfd = stdout;
		break;
	case EM_LOG_ERR:
	default:
		logfd = stderr;
		break;
	}

	r = vfprintf(logfd, fmt, args);
	return r;
}

__attribute__((format(printf, 2, 3)))
int appl_log(em_log_level_t level, const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = appl_vlog(level, fmt, args);
	va_end(args);

	return r;
}

/**
 * Delay spinloop
 */
void delay_spin(const uint64_t spin_count)
{
	env_atomic64_t dummy; /* use atomic to avoid optimization */
	uint64_t i;

	env_atomic64_init(&dummy);

	for (i = 0; i < spin_count; i++)
		env_atomic64_inc(&dummy);
}
