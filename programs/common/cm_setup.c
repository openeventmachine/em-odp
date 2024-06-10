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

#include "cm_setup.h"
#include "cm_pool_config.h"
#include "cm_pktio.h"
#include "cm_error_handler.h"

/**
 * @def USAGE_FMT
 * Usage help string
 */
#define USAGE_FMT \
"\n"										\
"Usage: %s APPL&EM-OPTIONS\n"							\
"  E.g. %s -c 0xfe -p\n"							\
"\n"										\
"Event Machine (EM) example application.\n"					\
"\n"										\
"Mandatory EM-OPTIONS:\n"							\
"  -c, --coremask <arg>    Select the cores to use, hexadecimal.\n"		\
"  -p, --process-per-core  Running EM with one process per core.\n"		\
"  -t, --thread-per-core   Running EM with one thread per core.\n"		\
"    Select EITHER -p OR -t, but not both!\n"					\
"\n"										\
"Optional [APPL & EM-OPTIONS]\n"								\
"  -d, --device-id <arg>        Device-id, hexadecimal (default: 0x0)\n"			\
"  -s, --startup-mode <arg>     Application startup mode:\n"					\
"                               0: Start-up & init all EM cores before appl-setup (default)\n"	\
"                               1: Start-up & init only one EM core before appl-setup,\n"	\
"                                  the rest of the EM-cores are init only after that.\n"	\
"\n"												\
"EM dispatch options (optional):\n"								\
"  a) using em_dispatch() - default: uses em_dispatch() unless options in b) are given.\n"	\
"    -r, --dispatch-rounds <arg>       Number of dispatch rounds (default: 0=forever).\n"	\
"  b) using em_dispatch_duration() - combinations of these options possible.\n"			\
"    --duration-rounds <arg>           Dispatch for the given number of rounds.\n"		\
"    --duration-ns <arg>               Dispatch for the given time in nanoseconds.\n"		\
"    --duration-events <arg>           Dispatch until the given number of events\n"		\
"                                      have been handled.\n"					\
"    --duration-noevents-rounds <arg>  Dispatch until no events have been received for the\n"	\
"                                      given number of rounds.\n"				\
"    --duration-noevents-ns <arg>      Dispatch until no events have been received for the\n"	\
"                                      given time in nanoseconds.\n"				\
"    --burst-size <arg>                The max number of events the dispatcher will request\n"	\
"                                      in one burst from the scheduler.\n"			\
"                                      (default: EM_SCHED_MULTI_MAX_BURST=%d)\n"		\
"    --wait-ns <arg>                   Scheduler wait-for-events timeout in nanoseconds.\n"	\
"                                      The scheduler will wait for events, if no immediately\n" \
"                                      available, for 'wait_ns' per dispatch round.\n"		\
"                                      (default: 0, do not wait)\n"				\
"    --sched-pause                     Pause the scheduler on the calling core when exiting the\n" \
"                                      EM dispatch function. If enabled, will also resume the\n"   \
"                                      scheduling when entering dispatch (default: don't pause).\n"\
"    Select EITHER a) OR b), but not both!\n"							\
"\n"												\
"Packet-IO (optional)\n"									\
"  -m, --pktin-mode <arg>        Select the packet-input mode to use:\n"			\
"                                0: Direct mode: PKTIN_MODE_DIRECT (default)\n"			\
"                                1: Plain queue mode: PKTIN_MODE_QUEUE\n"			\
"                                2: Scheduler mode with parallel queues:\n"			\
"                                   PKTIN_MODE_SCHED + SCHED_SYNC_PARALLEL\n"			\
"                                3: Scheduler mode with atomic queues:\n"			\
"                                   PKTIN_MODE_SCHED + SCHED_SYNC_ATOMIC\n"			\
"                                4: Scheduler mode with ordered queues:\n"			\
"                                   PKTIN_MODE_SCHED + SCHED_SYNC_ORDERED\n"			\
"  -v, --pktin-vector            Enable vector-mode for packet-input (default: disabled)\n"	\
"                                Supported with --pktin-mode:s 2, 3, 4\n"			\
"  -i, --eth-interface <arg(s)>  Select the ethernet interface(s) to use\n"			\
"  -e, --pktpool-em              Packet-io pool is an EM-pool (default)\n"			\
"  -o, --pktpool-odp             Packet-io pool is an ODP-pool\n"				\
"  -x, --vecpool-em              Packet-io vector pool is an EM-pool (default)\n"		\
"  -y, --vecpool-odp             Packet-io vector pool is an ODP-pool\n"			\
"    Select EITHER -e OR -o, but not both!\n" \
"Help\n" \
"  -h, --help              Display help and exit.\n" \
"\n"

static void
usage(char *progname)
{
	APPL_PRINT(USAGE_FMT, NO_PATH(progname), NO_PATH(progname),
		   EM_SCHED_MULTI_MAX_BURST);
}

/**
 * Stored command line arguments given at startup
 *
 * @see USAGE_FMT
 */
typedef struct {
	/** EM cmd line args */
	struct {
		/** EM device id */
		uint16_t device_id;
		/** RunMode: EM run with a thread per core */
		int thread_per_core;
		/** RunMode: EM run with a process per core */
		int process_per_core;
		/** Number of EM-cores (== nbr of EM-threads or EM-processes) */
		int core_count;
		/** Physical core mask, exact listing of cores for EM */
		em_core_mask_t phys_mask;
	} args_em;

	/** Application cmd line args */
	struct {
		/** Application name */
		char name[APPL_NAME_LEN];
		/** Start-up mode */
		startup_mode_t startup_mode;

		/** Dispatch rounds before returning: em_dispatch(rounds) */
		uint64_t dispatch_rounds;

		struct {
			/** Use em_dispatch_duration(duration, opt, ...) */
			bool in_use;

			/** Use 'rounds' as duration with em_dispatch_duration() */
			bool use_rounds;
			/** Dispatch for the given number of rounds */
			uint64_t rounds;

			/** Use 'ns' as duration with em_dispatch_duration() */
			bool use_ns;
			/** Dispatch for the given time of nanoseconds */
			uint64_t ns;

			/** Use 'events' as duration with em_dispatch_duration() */
			bool use_events;
			/** Dispatch until the given number of events have been handled */
			uint64_t events;

			/** Use 'noevents_rounds' as duration with em_dispatch_duration() */
			bool use_noevents_rounds;
			/** Dispatch until no events have been received for 'noevents_rounds' */
			uint64_t noevents_rounds;

			/** Use 'use_noevents_ns' as duration with em_dispatch_duration() */
			bool use_noevents_ns;
			/** Dispatch until no events have been received for 'noevents_ns' */
			uint64_t noevents_ns;

			/** Scheduler wait-for-events timeout in nanoseconds */
			uint64_t wait_ns;
			/** Scheduler event-burst size */
			unsigned int burst_size;
			/** Pause scheduler when returning form dispatch */
			bool sched_pause;
		} dispatch_duration;

		/** Packet I/O parameters */
		struct {
			/** Packet input mode */
			pktin_mode_t in_mode;
			/** Packet input vectors enabled (true/false) */
			bool pktin_vector;
			/** Interface count */
			int if_count;
			/** Interface names + placeholder for '\0' */
			char if_name[IF_MAX_NUM][IF_NAME_LEN + 1];
			/** Pktio is setup with an EM event-pool (true/false) */
			bool pktpool_em;
			/** Pktio is setup with an ODP pkt-pool (true/false) */
			bool pktpool_odp;
			/** Pktio is setup with an EM vector pool (if pkt-input vectors enabled) */
			bool vecpool_em;
			/** Pktio is setup with an ODP vector pool (if pkt-input vectors enabled) */
			bool vecpool_odp;
		} pktio;
	} args_appl;
} parse_args_t;

/**
 * CPU config to be used
 */
typedef struct {
	/** Number of CPUs to run EM-cores */
	int num_worker;
	/** Worker_mask specifying cores for EM */
	odp_cpumask_t worker_mask;
} cpu_conf_t;

/**
 * Dispatch rounds for em_dispatch() during start-up to properly sync the
 * cores to enter the main dispatch loop at roughly the same time.
 */
#define STARTUP_DISPATCH_ROUNDS 16

/**
 * Dispatch rounds for em_dispatch() during program execution to regularly
 * return from dispatch and inspect the 'appl_shm->exit_flag' value. Program
 * termination will begin once a set 'appl_shm->exit_flags' has been noticed.
 */
#define EXIT_CHECK_DISPATCH_ROUNDS 20000

/**
 * Dispatch duration in nanoseconds for em_dispatch_duration() during program
 * execution to regularly return from dispatch and inspect the
 * 'appl_shm->exit_flag' value. Program termination will begin once a set
 * 'appl_shm->exit_flags' has been noticed.
 */
#define EXIT_CHECK_DISPATCH_DURATION_NS 1000000000 /* 1s */

static void
parse_args(int argc, char *argv[], parse_args_t *parse_args /* out */);

static void
verify_cpu_setup(const parse_args_t *parsed,
		 cpu_conf_t *cpu_conf /* out */);

static odp_instance_t
init_odp(const parse_args_t *parsed, const cpu_conf_t *cpu_conf);

static void
init_sync(sync_t *const sync, int num_cpus);

static void
init_em(const parse_args_t *parsed, em_conf_t *em_conf /* out */);

static void
init_appl_conf(const parse_args_t *parsed, appl_conf_t *appl_conf /* out */);

static void
create_pktio(appl_conf_t *appl_conf/*in/out*/, const cpu_conf_t *cpu_conf);
static void
term_pktio(const appl_conf_t *appl_conf);

static int
create_odp_threads(odp_instance_t instance,
		   const parse_args_t *parsed, const cpu_conf_t *cpu_conf,
		   int (*start_fn)(void *fn_arg), void *fn_arg,
		   odph_thread_t thread_tbl[/*out*/]);
static int
run_core_fn(void *arg);

static void
install_sig_handler(int signum, void (*sig_handler)(int), int flags);

static void
sigchld_handler(int sig ODP_UNUSED);
static void
sigint_handler(int signo ODP_UNUSED);

/**
 * Global pointer to common application shared memory
 */
appl_shm_t *appl_shm;

/**
 * Common setup function for em-odp example programs
 */
int cm_setup(int argc, char *argv[])
{
	/* use unbuffered stdout */
	if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
		APPL_EXIT_FAILURE("setvbuf() fails (errno(%i)=%s)",
				  errno, strerror(errno));

	/*
	 * Parse the command line arguments
	 */
	parse_args_t parsed; /* filled during cmd line arg parsing */

	memset(&parsed, 0, sizeof(parsed));
	parse_args(argc, argv, &parsed/* out */);

	/*
	 * Verify the cpu setup and extract the cpu config
	 */
	cpu_conf_t cpu_conf;

	memset(&cpu_conf, 0, sizeof(cpu_conf));
	verify_cpu_setup(&parsed, &cpu_conf/* out */);

	/*
	 * Init ODP with given args and cpu setup
	 *
	 * Calls odp_init_global() and odp_init_local() for this thread
	 * before returning.
	 */
	odp_instance_t instance;

	instance = init_odp(&parsed, &cpu_conf);

	APPL_PRINT("\n"
		   "*********************************************************\n"
		   "Setting up EM on ODP-version:\n"
		   "%s\n"
		   "*********************************************************\n"
		   "\n",
		   odp_version_impl_str());

	/*
	 * Setup shared memory
	 *
	 * Reserve application shared memory in one chunk.
	 */
	uint32_t flags = 0;
	odp_shm_capability_t shm_capa;
	int err = odp_shm_capability(&shm_capa);

	if (unlikely(err))
		APPL_EXIT_FAILURE("shm capability error:%d", err);

	if (shm_capa.flags & ODP_SHM_SINGLE_VA)
		flags |= ODP_SHM_SINGLE_VA;

	odp_shm_t shm = odp_shm_reserve("appl_shm", sizeof(appl_shm_t),
					ODP_CACHE_LINE_SIZE, flags);
	if (unlikely(shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("appl shared mem reservation failed");
	appl_shm = odp_shm_addr(shm);
	if (unlikely(appl_shm == NULL))
		APPL_EXIT_FAILURE("obtaining shared mem addr failed");
	memset(appl_shm, 0, sizeof(appl_shm_t));

	/*
	 * Initialize application start-up & exit synchronization
	 */
	sync_t *const sync = &appl_shm->sync;

	init_sync(sync, cpu_conf.num_worker);

	/*
	 * Init EM with given args
	 *
	 * Calls em_init() before returning.
	 */
	em_conf_t *const em_conf = &appl_shm->em_conf;

	init_em(&parsed, em_conf);

	/*
	 * Set application conf based on parsed cmd line arguments
	 */
	appl_conf_t *const appl_conf = &appl_shm->appl_conf;

	init_appl_conf(&parsed, appl_conf);

	/*
	 * Create and start packet-I/O, if requested
	 */
	if (appl_conf->pktio.if_count > 0) {
		create_pktio(appl_conf/*in/out*/, &cpu_conf);
		pktio_start();
	}

	/*
	 * Signal handler for SIGCHLD in process-per-core mode
	 *
	 * Create a signal handler for the SIGCHLD signal that is sent
	 * to the parent process when a forked child process dies.
	 */
	if (em_conf->process_per_core)
		install_sig_handler(SIGCHLD, sigchld_handler, 0);

	/*
	 * Signal handler for SIGINT (Ctrl-C)
	 *
	 * Create a signal handler for the SIGINT (Ctrl-C) signal to flag
	 * program termination.
	 * Set the 'SA_RESETHAND'-flag to reset the SIGINT handler to its
	 * default disposition after the first handling to be able to stop
	 * execution if the application misbehaves.
	 */
	install_sig_handler(SIGINT, sigint_handler, SA_RESETHAND);

	/*
	 * Create the odp-threads to use as EM-cores
	 *
	 * Create the odp-threads / EM-cores. Each EM-core will run the
	 * 'run_core_fn(appl_shm)' function in a thread pinned to a single cpu
	 * as specified by 'cpu_conf'.
	 */
	odph_thread_t *const thread_tbl = appl_shm->thread_tbl;
	int ret = create_odp_threads(instance, &parsed, &cpu_conf,
				     run_core_fn /*fn*/, appl_shm /*fn_arg*/,
				     thread_tbl /*out*/);
	if (ret != cpu_conf.num_worker)
		APPL_EXIT_FAILURE("ODP thread creation failed:%d", ret);

	/*
	 * Wait for the created odp-threads / EM-cores to return
	 */
	ret = odph_thread_join(thread_tbl, cpu_conf.num_worker);
	if (ret != cpu_conf.num_worker)
		APPL_EXIT_FAILURE("ODP thread join failed:%d", ret);

	/*
	 * Teardown the application after all the odp-threads / EM-cores
	 * have ended:
	 */

	/*
	 * Terminate packet-I/O, if set up
	 */
	if (appl_conf->pktio.if_count > 0)
		term_pktio(appl_conf);

	/*
	 * Terminate EM
	 *
	 * All EM-cores have already run em_term_core()
	 */
	em_status_t stat = em_term(em_conf);

	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_term():%" PRI_STAT "", stat);

	/*
	 * Free shared memory
	 */
	ret = odp_shm_free(shm);
	if (ret != 0)
		APPL_EXIT_FAILURE("appl shared mem free failed:%d", ret);

	/**
	 * Terminate ODP
	 */
	ret = odp_term_local();
	if (ret != 0)
		APPL_EXIT_FAILURE("Last ODP local term failed:%d", ret);
	ret = odp_term_global(instance);
	if (ret != 0)
		APPL_EXIT_FAILURE("odp_term_global() failed:%d", ret);

	APPL_PRINT("\nDone - exit\n\n");

	return EXIT_SUCCESS;
}

static odp_instance_t
init_odp(const parse_args_t *parsed, const cpu_conf_t *cpu_conf)
{
	odp_init_t init_params;
	odp_instance_t instance;
	int ret;

	/* Initialize the odp init params with 'default' values */
	odp_init_param_init(&init_params);

	/* Restrict odp worker threads to cores set in the 'worker_mask' */
	init_params.num_worker = cpu_conf->num_worker;
	init_params.worker_cpus = &cpu_conf->worker_mask;

	/**
	 * Leave "init_params.control_cpus" unset to use odp default control
	 * cpus, which are the rest of installed cpus excluding worker cpus
	 * and CPU 0 when worker cpus don't have CPU 1 set. But if worker cpus
	 * have CPU 1 set, CPU 0 will be set as a control cpu.
	 */

	/*
	 * List odp features not to be used in the examples. This may optimize
	 * performance. Note that a real application might need to change this!
	 */
	init_params.not_used.feat.cls = 1; /* don't use the odp classifier */
	init_params.not_used.feat.compress = 1; /* don't use the odp compress */
	init_params.not_used.feat.crypto = 1; /* don't use odp crypto */
	init_params.not_used.feat.ipsec = 1; /* don't use odp ipsec */
	init_params.not_used.feat.tm = 1; /* don't use the odp traffic manager*/

	/*
	 * Set the memory model to use for odp: thread or process.
	 * parse_args() has verified .thread_per_core vs .process_per_core
	 */
	if (parsed->args_em.thread_per_core)
		init_params.mem_model = ODP_MEM_MODEL_THREAD;
	else
		init_params.mem_model = ODP_MEM_MODEL_PROCESS;

	ret = odp_init_global(&instance, &init_params, NULL);
	if (ret != 0)
		APPL_EXIT_FAILURE("ODP global init failed:%d", ret);

	ret = odp_init_local(instance, ODP_THREAD_CONTROL);
	if (ret != 0)
		APPL_EXIT_FAILURE("ODP local init failed:%d", ret);

	/* Configure the scheduler */
	odp_schedule_config_t sched_config;

	odp_schedule_config_init(&sched_config);
	/* EM does not need the ODP predefined scheduling groups */
	sched_config.sched_group.all = 0;
	sched_config.sched_group.control = 0;
	sched_config.sched_group.worker = 0;
	ret = odp_schedule_config(&sched_config);
	if (ret != 0)
		APPL_EXIT_FAILURE("ODP schedule config failed:%d", ret);

	/* Print ODP system info */
	odp_sys_info_print();

	return instance;
}

static void
init_sync(sync_t *const sync, int num_cpus)
{
	odp_barrier_init(&sync->start_barrier, num_cpus);
	odp_barrier_init(&sync->exit_barrier, num_cpus);
	env_atomic64_init(&sync->exit_count);
	env_atomic64_init(&sync->enter_count);
}

static void
init_em(const parse_args_t *parsed, em_conf_t *em_conf /* out */)
{
	em_status_t stat;

	em_conf_init(em_conf);

	/* Set EM conf based on parsed cmd line arguments */
	em_conf->device_id = parsed->args_em.device_id;
	em_conf->thread_per_core = parsed->args_em.thread_per_core;
	em_conf->process_per_core = parsed->args_em.process_per_core;
	em_conf->core_count = parsed->args_em.core_count;
	em_conf->phys_mask = parsed->args_em.phys_mask;

	/* Event-Timer: disable=0, enable=1 */
	em_conf->event_timer = 1;

	/*
	 * Set the default pool config in em_conf, needed internally by EM
	 * at startup. Note that if default pool configuration is provided
	 * in em-odp.conf at runtime through option 'startup_pools', this
	 * default pool config will be overridden and thus ignored.
	 */
	em_pool_cfg_t default_pool_cfg;

	em_pool_cfg_init(&default_pool_cfg); /* mandatory */
	default_pool_cfg.event_type = EM_EVENT_TYPE_SW;
	default_pool_cfg.align_offset.in_use = true; /* override config file */
	default_pool_cfg.align_offset.value = 0; /* set explicit '0 bytes' */
	default_pool_cfg.user_area.in_use = true; /* override config file */
	default_pool_cfg.user_area.size = 0; /* set explicit '0 bytes' */
	default_pool_cfg.num_subpools = 4;
	default_pool_cfg.subpool[0].size = 256;
	default_pool_cfg.subpool[0].num = 16384;
	default_pool_cfg.subpool[0].cache_size = 64;
	default_pool_cfg.subpool[1].size = 512;
	default_pool_cfg.subpool[1].num = 1024;
	default_pool_cfg.subpool[1].cache_size = 32;
	default_pool_cfg.subpool[2].size = 1024;
	default_pool_cfg.subpool[2].num =  1024;
	default_pool_cfg.subpool[2].cache_size = 16;
	default_pool_cfg.subpool[3].size = 2048;
	default_pool_cfg.subpool[3].num = 1024;
	default_pool_cfg.subpool[3].cache_size = 8;

	em_conf->default_pool_cfg = default_pool_cfg;

	/*
	 * User can override the EM default log functions by giving logging
	 * funcs of their own - here we just use the default (shown explicitly)
	 */
	em_conf->log.log_fn = NULL;
	em_conf->log.vlog_fn = NULL;

	/* Packet-I/O */
	if (parsed->args_appl.pktio.if_count > 0) {
		/*
		 * Request EM to poll input for pkts in the dispatch loop
		 */
		pktin_mode_t in_mode = parsed->args_appl.pktio.in_mode;

		if (in_mode == DIRECT_RECV)
			em_conf->input.input_poll_fn = pktin_pollfn_direct;
		else if (in_mode == PLAIN_QUEUE)
			em_conf->input.input_poll_fn = pktin_pollfn_plainqueue;
		/* in_mode: SCHED_... use no input_poll function! */

		/*
		 * Request EM to drain buffered output in the dispatch loop
		 */
		em_conf->output.output_drain_fn = pktout_drainfn;
	}

	/*
	 * Initialize the Event Machine. Every EM core still needs to call
	 * em_init_core() later.
	 * Note: the EM default pool config MUST be included in em_conf!
	 */
	stat = em_init(em_conf);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_init(), EM error:%" PRI_STAT "", stat);
}

static void
init_appl_conf(const parse_args_t *parsed, appl_conf_t *appl_conf /* out */)
{
	size_t len = sizeof(appl_conf->name);

	memcpy(appl_conf->name, parsed->args_appl.name, len);
	appl_conf->name[len - 1] = '\0';

	appl_conf->core_count = parsed->args_em.core_count;

	if (parsed->args_em.thread_per_core) {
		appl_conf->num_procs = 1;
		appl_conf->num_threads = parsed->args_em.core_count;
	} else {
		appl_conf->num_procs = parsed->args_em.core_count;
		appl_conf->num_threads = parsed->args_em.core_count;
	}

	appl_conf->startup_mode = parsed->args_appl.startup_mode;

	appl_conf->dispatch_rounds = parsed->args_appl.dispatch_rounds;

	appl_conf->dispatch_duration.in_use = parsed->args_appl.dispatch_duration.in_use;

	if (appl_conf->dispatch_duration.in_use) {
		em_dispatch_opt_init(&appl_conf->dispatch_duration.opt);

		if (parsed->args_appl.dispatch_duration.use_rounds) {
			appl_conf->dispatch_duration.duration.select |= EM_DISPATCH_DURATION_ROUNDS;
			appl_conf->dispatch_duration.duration.rounds =
				parsed->args_appl.dispatch_duration.rounds;
		}
		if (parsed->args_appl.dispatch_duration.use_ns) {
			appl_conf->dispatch_duration.duration.select |= EM_DISPATCH_DURATION_NS;
			appl_conf->dispatch_duration.duration.ns =
				parsed->args_appl.dispatch_duration.ns;
		}
		if (parsed->args_appl.dispatch_duration.use_events) {
			appl_conf->dispatch_duration.duration.select |= EM_DISPATCH_DURATION_EVENTS;
			appl_conf->dispatch_duration.duration.events =
				parsed->args_appl.dispatch_duration.events;
		}
		if (parsed->args_appl.dispatch_duration.use_noevents_rounds) {
			appl_conf->dispatch_duration.duration.select |=
				EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS;
			appl_conf->dispatch_duration.duration.no_events.rounds =
				parsed->args_appl.dispatch_duration.noevents_rounds;
		}
		if (parsed->args_appl.dispatch_duration.use_noevents_ns) {
			appl_conf->dispatch_duration.duration.select |=
				EM_DISPATCH_DURATION_NO_EVENTS_NS;
			appl_conf->dispatch_duration.duration.no_events.ns =
				parsed->args_appl.dispatch_duration.noevents_ns;
		}
		appl_conf->dispatch_duration.opt.wait_ns =
			parsed->args_appl.dispatch_duration.wait_ns;
		appl_conf->dispatch_duration.opt.burst_size =
			parsed->args_appl.dispatch_duration.burst_size;
		appl_conf->dispatch_duration.opt.sched_pause =
			parsed->args_appl.dispatch_duration.sched_pause;
	}

	/*
	 * Create the other event pools used by the application.
	 * Note that em_term() will delete all remaining pools during
	 * termination.
	 */
	em_pool_cfg_t appl_pool_1_cfg;

	em_pool_cfg_init(&appl_pool_1_cfg); /* mandatory */
	appl_pool_1_cfg.event_type = EM_EVENT_TYPE_PACKET;
	appl_pool_1_cfg.num_subpools = 4;

	appl_pool_1_cfg.subpool[0].size = 256;
	appl_pool_1_cfg.subpool[0].num = 16384;
	appl_pool_1_cfg.subpool[0].cache_size = 128;

	appl_pool_1_cfg.subpool[1].size = 512;
	appl_pool_1_cfg.subpool[1].num = 1024;
	appl_pool_1_cfg.subpool[1].cache_size = 64;

	appl_pool_1_cfg.subpool[2].size = 1024;
	appl_pool_1_cfg.subpool[2].num = 1024;
	appl_pool_1_cfg.subpool[2].cache_size = 32;

	appl_pool_1_cfg.subpool[3].size = 2048;
	appl_pool_1_cfg.subpool[3].num = 1024;
	appl_pool_1_cfg.subpool[3].cache_size = 16;

	em_pool_t appl_pool = em_pool_create(APPL_POOL_1_NAME, EM_POOL_UNDEF,
					     &appl_pool_1_cfg);
	if (appl_pool == EM_POOL_UNDEF)
		APPL_EXIT_FAILURE("appl pool:%s create failed", APPL_POOL_1_NAME);
	appl_conf->pools[0] = appl_pool;
	appl_conf->num_pools = 1;

	appl_conf->pktio.in_mode = parsed->args_appl.pktio.in_mode;
	appl_conf->pktio.if_count = parsed->args_appl.pktio.if_count;
	for (int i = 0; i < parsed->args_appl.pktio.if_count; i++) {
		memcpy(appl_conf->pktio.if_name[i],
		       parsed->args_appl.pktio.if_name[i], IF_NAME_LEN + 1);
	}

	appl_conf->pktio.pktpool_em = parsed->args_appl.pktio.pktpool_em;
	appl_conf->pktio.pktin_vector = parsed->args_appl.pktio.pktin_vector;
	appl_conf->pktio.vecpool_em = parsed->args_appl.pktio.vecpool_em;
}

static void
create_pktio(appl_conf_t *appl_conf/*in/out*/, const cpu_conf_t *cpu_conf)
{
	pktio_mem_reserve();
	pktio_pool_create(appl_conf->pktio.if_count,
			  appl_conf->pktio.pktpool_em,
			  appl_conf->pktio.pktin_vector,
			  appl_conf->pktio.vecpool_em);
	pktio_init(appl_conf);
	/* Create a pktio instance for each interface */
	for (int i = 0; i < appl_conf->pktio.if_count; i++) {
		int if_id = pktio_create(appl_conf->pktio.if_name[i],
					 appl_conf->pktio.in_mode,
					 appl_conf->pktio.pktin_vector,
					 appl_conf->pktio.if_count,
					 cpu_conf->num_worker);
		if (unlikely(if_id < 0))
			APPL_EXIT_FAILURE("Cannot create pktio if:%s",
					  appl_conf->pktio.if_name[i]);
		/* Store the interface id */
		appl_conf->pktio.if_ids[i] = if_id;
	}
}

static void
term_pktio(const appl_conf_t *appl_conf)
{
	/* Stop, close and free the pktio resources */
	pktio_stop();
	pktio_close();
	pktio_deinit(appl_conf);
	pktio_pool_destroy(appl_conf->pktio.pktpool_em,
			   appl_conf->pktio.pktin_vector,
			   appl_conf->pktio.vecpool_em);
	pktio_mem_free();
}

static int
create_odp_threads(odp_instance_t instance,
		   const parse_args_t *parsed, const cpu_conf_t *cpu_conf,
		   int (*start_fn)(void *fn_arg), void *fn_arg,
		   odph_thread_t thread_tbl[/*out*/])
{
	odph_thread_common_param_t thr_common;
	odph_thread_param_t thr_param; /* same for all thrs */
	int ret;

	/*
	 * Generate a thread summary for the user
	 */
	char cpumaskstr[ODP_CPUMASK_STR_SIZE];

	APPL_PRINT("num worker:  %i\n", cpu_conf->num_worker);

	odp_cpumask_to_str(&cpu_conf->worker_mask, cpumaskstr,
			   sizeof(cpumaskstr));
	APPL_PRINT("worker thread mask:  %s\n", cpumaskstr);

	odph_thread_common_param_init(&thr_common);
	thr_common.instance = instance;
	thr_common.cpumask = &cpu_conf->worker_mask;
	/*
	 * Select between pthreads and processes,
	 * parse_args() has verified .thread_per_core vs .process_per_core
	 */
	if (parsed->args_em.thread_per_core)
		thr_common.thread_model = 0; /* pthreads */
	else
		thr_common.thread_model = 1; /* processes */
	thr_common.sync = 1; /* Synchronize thread start up */
	thr_common.share_param = 1; /* same 'thr_param' for all threads */

	odph_thread_param_init(&thr_param);
	thr_param.start = start_fn;
	thr_param.arg = fn_arg;
	thr_param.thr_type = ODP_THREAD_WORKER;

	/*
	 * Create odp worker threads to run as EM-cores
	 */
	ret = odph_thread_create(thread_tbl /*out*/,
				 &thr_common, &thr_param,
				 cpu_conf->num_worker);
	return ret;
}

/**
 * @brief Helper to startup_core():
 *        Start-up and init all EM-cores before application setup
 *
 * @param sync       Application start-up and tear-down synchronization vars
 * @param appl_conf  Application configuration
 */
static void startup_all_cores(sync_t *sync, const appl_conf_t *appl_conf,
			      bool is_thread_per_core)
{
	/*
	 * Initialize this thread of execution (proc, thread), i.e. EM-core
	 */
	em_status_t stat = em_init_core();

	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_init_core():%" PRI_STAT ", EM-core:%02d",
				  stat, em_core_id());

	odp_barrier_wait(&sync->start_barrier);

	if (appl_conf->pktio.if_count > 0)
		pktio_mem_lookup(is_thread_per_core);

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
	int core_id = em_core_id();
	uint64_t cores = appl_conf->core_count;

	/* Ensure all EM cores can find the default event pool */
	if (em_pool_find(EM_POOL_DEFAULT_NAME) != EM_POOL_DEFAULT)
		APPL_EXIT_FAILURE("em_pool_find(%s) c:%d",
				  EM_POOL_DEFAULT_NAME, core_id);

	if (core_id == 0) {
		/*
		 * Initialize the application and allocate shared memory.
		 */
		test_init(appl_conf);
	}

	odp_barrier_wait(&sync->start_barrier);

	if (core_id != 0) {
		/* Look up the shared memory */
		test_init(appl_conf);
	}

	APPL_PRINT("Entering the event dispatch loop on EM-core:%02d\n", core_id);

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
	em_dispatch_duration_t start_duration;
	em_dispatch_opt_t start_opt;

	em_dispatch_opt_init(&start_opt);
	start_opt.skip_input_poll = true;
	start_opt.skip_output_drain = true;
	start_opt.sched_pause = false;

	start_duration.select = EM_DISPATCH_DURATION_ROUNDS;
	start_duration.rounds = STARTUP_DISPATCH_ROUNDS;

	env_atomic64_inc(&sync->enter_count);
	do {
		em_dispatch_duration(&start_duration, &start_opt, NULL);
		if (core_id == 0)
			env_atomic64_inc(&sync->enter_count);
	} while (env_atomic64_get(&sync->enter_count) <= cores);
}

/**
 * @brief Helper to startup_core():
 *        Start-up and init only one EM-core before application setup. The rest
 *        of the EM-cores are init only after that.
 *
 * @param sync       Application start-up and tear-down synchronization vars
 * @param appl_conf  Application configuration
 */
static void startup_one_core_first(sync_t *sync, const appl_conf_t *appl_conf,
				   bool is_thread_per_core)
{
	em_status_t stat;
	uint64_t enter_count = env_atomic64_return_add(&sync->enter_count, 1);

	if (enter_count == 0) {
		/*
		 * Initialize first EM-core
		 */
		stat = em_init_core();
		if (stat != EM_OK)
			APPL_EXIT_FAILURE("em_init_core():%" PRI_STAT ", EM-core:%02d",
					  stat, em_core_id());
		if (appl_conf->pktio.if_count > 0)
			pktio_mem_lookup(is_thread_per_core);

		/* Ensure all EM cores can find the default event pool */
		if (em_pool_find(EM_POOL_DEFAULT_NAME) != EM_POOL_DEFAULT)
			APPL_EXIT_FAILURE("em_pool_find(%s) c:%d",
					  EM_POOL_DEFAULT_NAME, em_core_id());

		/*
		 * Don't use barriers to sync the cores after this!
		 * EM synchronous API funcs (e.g. em_eo_start_sync()) blocks until the
		 * function has completed on all cores - a barrier might hinder a core
		 * from completing an operation.
		 */
	}

	odp_barrier_wait(&sync->start_barrier);

	if (enter_count == 0) {
		/*
		 * Initialize the application and allocate shared memory.
		 */
		test_init(appl_conf);
		/*
		 * Create and start application EOs, pass the appl_conf.
		 */
		test_start(appl_conf);
	} else {
		/*
		 * Sleep until the first EM-core has completed test_init() and
		 * test_start() to set up the application.
		 * Use sleep instead of barrier or lock etc. to avoid deadlock
		 * in case the first core is using sync-APIs and is waiting for
		 * completion by the other EM-cores, we need to go into dispatch
		 * (this is for testing only and NOT the most elegant start-up).
		 */
		sleep(1);

		/*
		 * Initialize this thread of execution (proc, thread), i.e. EM-core
		 */
		stat = em_init_core();
		if (stat != EM_OK)
			APPL_EXIT_FAILURE("em_init_core():%" PRI_STAT ", EM-core:%02d",
					  stat, em_core_id());

		if (appl_conf->pktio.if_count > 0)
			pktio_mem_lookup(is_thread_per_core);

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

		/* Ensure all EM cores can find the default event pool */
		if (em_pool_find(EM_POOL_DEFAULT_NAME) != EM_POOL_DEFAULT)
			APPL_EXIT_FAILURE("em_pool_find(%s) c:%d",
					  EM_POOL_DEFAULT_NAME, em_core_id());

		/* Look up the shared memory */
		test_init(appl_conf);
	}

	const int core_id = em_core_id();
	const uint64_t cores = appl_conf->core_count;

	APPL_PRINT("Entering the event dispatch loop on EM-core:%02d\n", core_id);

	/*
	 * Keep all cores dispatching until 'test_start()' has been
	 * completed in order to handle sync-API function calls and to enter
	 * the main dispatch loop almost at the same time.
	 */
	em_dispatch_duration_t start_duration;
	em_dispatch_opt_t start_opt;

	em_dispatch_opt_init(&start_opt);
	start_opt.skip_input_poll = true;
	start_opt.skip_output_drain = true;
	start_opt.sched_pause = false;

	start_duration.select = EM_DISPATCH_DURATION_ROUNDS;
	start_duration.rounds = STARTUP_DISPATCH_ROUNDS;

	env_atomic64_inc(&sync->enter_count);
	do {
		em_dispatch_duration(&start_duration, &start_opt, NULL);
		if (core_id == 0)
			env_atomic64_inc(&sync->enter_count);
	} while (env_atomic64_get(&sync->enter_count) <= 2 * cores);
}

/*
 * Startup EM-core.
 * Allow testing different startup scenarios.
 *
 * @param sync       Application start-up and tear-down synchronization vars
 * @param appl_conf  Application configuration
 */
static void startup_core(sync_t *sync, appl_conf_t *appl_conf)
{
	bool is_thread_per_core = appl_shm->em_conf.thread_per_core ? true : false;

	switch (appl_conf->startup_mode) {
	case STARTUP_ALL_CORES:
		/*
		 * All EM-cores start-up and init before application setup
		 */
		startup_all_cores(sync, appl_conf, is_thread_per_core);
		break;
	case STARTUP_ONE_CORE_FIRST:
		/*
		 * Only one EM-core start-up and init before application setup,
		 * the rest of the EM-cores are init after that.
		 */
		startup_one_core_first(sync, appl_conf, is_thread_per_core);
		break;
	default:
		APPL_EXIT_FAILURE("Unsupported startup-mode:%d",
				  appl_conf->startup_mode);
		break;
	}
}

/**
 * Dispatch-loop done for application, prepare for controlled shutdown
 */
static void terminate_core(sync_t *sync, appl_conf_t *appl_conf)
{	int core_id = em_core_id();
	uint64_t cores = appl_conf->core_count;

	em_dispatch_duration_t term_duration;
	em_dispatch_opt_t term_opt;
	em_dispatch_results_t results = {0};
	em_status_t stat;

	em_dispatch_opt_init(&term_opt);
	term_opt.skip_input_poll = true;
	term_opt.skip_output_drain = false;
	term_opt.sched_pause = true;

	term_duration.select = EM_DISPATCH_DURATION_NO_EVENTS_NS;
	term_duration.no_events.ns = 100000000; /* 100 ms*/

	/*
	 * Allow apps one more round with 'exit_flag' set to flush events from
	 * the sched queues etc.
	 */
	if (!appl_shm->exit_flag)
		appl_shm->exit_flag = 1; /* potential race with SIGINT-handler*/

	em_dispatch_duration(&term_duration, &term_opt, NULL);

	uint64_t exit_count = env_atomic64_return_add(&sync->exit_count, 1);

	/* First core to exit dispatch stops the application */
	if (exit_count == 0) {
		if (appl_conf->pktio.if_count > 0) {
			/* halt further pktio rx & tx */
			pktio_halt();
			/* dispatch with pktio stopped before test_stop()*/
			em_dispatch_duration(&term_duration, &term_opt, NULL);
		}
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
		em_dispatch_duration(&term_duration, &term_opt, NULL);
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
	stat = EM_OK;
	term_opt.skip_input_poll = true;
	term_opt.skip_output_drain = true;
	do {
		stat = em_dispatch_duration(&term_duration, &term_opt, &results);
		if (stat == EM_OK && results.events == 0)
			break;
	} while (stat == EM_OK);

	APPL_PRINT("Left the event dispatch loop on EM-core:%02d\n", core_id);

	odp_barrier_wait(&sync->exit_barrier);

	if (core_id == 0) {
		/*
		 * Free allocated test resources
		 */
		test_term(appl_conf);
	}

	odp_barrier_wait(&sync->exit_barrier);

	stat = em_term_core();
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_term_core(%d):%" PRI_STAT "",
				  core_id, stat);

	odp_barrier_wait(&sync->exit_barrier);

	/* depend on the odp helper to call odp_term_local */
}

/**
 * Legacy dispatch, i.e. use em_dispatch() without options
 */
static void run_core_dispatch(uint64_t dispatch_rounds)
{
	const uint64_t exit_check_rounds = EXIT_CHECK_DISPATCH_ROUNDS;

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
		uint64_t rounds = MIN(dispatch_rounds, exit_check_rounds);

		do {
			em_dispatch(rounds);
			dispatch_rounds -= rounds;
		} while (dispatch_rounds > rounds && !appl_shm->exit_flag);

		if (dispatch_rounds > 0) {
			rounds = MIN(dispatch_rounds, rounds);
			em_dispatch(rounds);
		}
	}
}

/**
 * Dispatch with options (via cmd line arguments), i.e. use em_dispatch_duration()
 */
static void run_core_dispatch_duration(const em_dispatch_duration_t *duration,
				       const em_dispatch_opt_t *opt)
{
	const em_dispatch_duration_select_t select = duration->select;
	em_dispatch_duration_t exit_check_duration = *duration;
	em_dispatch_results_t results = {0};
	em_dispatch_results_t results_tot = {0};
	em_status_t status;

	int64_t rounds_left = 0;
	int64_t ns_left = 0;
	int64_t events_left = 0;
	int64_t noevents_rounds_left = 0;
	int64_t noevents_ns_left = 0;

	if (select & EM_DISPATCH_DURATION_ROUNDS)
		rounds_left = duration->rounds;
	if (select & EM_DISPATCH_DURATION_NS)
		ns_left = duration->ns;
	if (select & EM_DISPATCH_DURATION_EVENTS)
		events_left = duration->events;
	if (select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS)
		noevents_rounds_left = duration->no_events.rounds;
	if (select & EM_DISPATCH_DURATION_NO_EVENTS_NS)
		noevents_ns_left = duration->no_events.ns;

	/*
	 * Dispatch in chunks of 1s (to check the exit_flag)
	 */
	exit_check_duration.select |= EM_DISPATCH_DURATION_NS;
	exit_check_duration.ns = EXIT_CHECK_DISPATCH_DURATION_NS;
	if (select & EM_DISPATCH_DURATION_NS)
		exit_check_duration.ns = MIN(exit_check_duration.ns, duration->ns);

	odp_time_t t1 = odp_time_local();

	do {
		status = em_dispatch_duration(&exit_check_duration, opt, &results);
		if (unlikely(status != EM_OK))
			break;

		results_tot.rounds += results.rounds;
		results_tot.ns += results.ns;
		results_tot.events += results.events;

		if (select & EM_DISPATCH_DURATION_ROUNDS) {
			rounds_left -= results.rounds;
			if (unlikely(rounds_left <= 0))
				break;
			exit_check_duration.rounds = rounds_left;
		}
		if (select & EM_DISPATCH_DURATION_NS) {
			ns_left -= results.ns;
			if (unlikely(ns_left <= 0))
				break;
			if (ns_left < EXIT_CHECK_DISPATCH_DURATION_NS)
				exit_check_duration.ns = ns_left;
		}
		if (select & EM_DISPATCH_DURATION_EVENTS) {
			events_left -= results.events;
			if (unlikely(events_left <= 0))
				break;
			exit_check_duration.events = events_left;
		}

		/*
		 * The 'no-events' updates to '.rounds' and '.ns' are
		 * approximations only since it is not known if 'no-events'
		 * could have started in the middle of the last dispatch.
		 * Can only check against events == 0 here.
		 */
		if (select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS) {
			if (results.events == 0) {
				noevents_rounds_left -= results.rounds;
				if (unlikely(noevents_rounds_left <= 0))
					break;
				exit_check_duration.no_events.rounds = noevents_rounds_left;
			}
		}
		if (select & EM_DISPATCH_DURATION_NO_EVENTS_NS) {
			if (results.events == 0) {
				noevents_ns_left -= results.ns;
				if (unlikely(noevents_ns_left <= 0))
					break;
				exit_check_duration.no_events.ns = noevents_ns_left;
			}
		}
	} while (status == EM_OK && !appl_shm->exit_flag);

	odp_time_t t2 = odp_time_local();
	uint64_t diff_ns = odp_time_diff_ns(t2, t1);
	double diff_sec = (double)diff_ns / 1.0e9;

	APPL_PRINT("EM-core:%02d dispatched for %g s (%" PRIu64 " ns)\n"
		   "           total: rounds=%" PRIu64 " ns=%" PRIu64 " events=%" PRIu64 "\n",
		   em_core_id(), diff_sec, diff_ns,
		   results_tot.rounds, results_tot.ns, results_tot.events);
}

/**
 * Core runner function - application entry on each EM-core
 *
 * Application setup and event dispatch loop run by each EM-core.
 * A call to em_init_core() MUST be made on each EM-core before using other
 * EM API functions to create EOs, queues etc. or calling em_dispatch().
 *
 * @param arg  passed arg actually of type 'appl_shm_t *', i.e. appl shared mem
 */
static int run_core_fn(void *arg)
{
	odp_shm_t shm;
	appl_shm_t *appl_shm;
	void *shm_addr;
	appl_conf_t *appl_conf;
	sync_t *sync;

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

	appl_conf = &appl_shm->appl_conf;
	sync = &appl_shm->sync;

	/*
	 * Startup EM-core.
	 * Allow testing different startup scenarios:
	 */
	startup_core(sync, appl_conf);

	APPL_PRINT("%s() on EM-core:%02d\n", __func__, em_core_id());

	/*
	 * Dispatch on EM-core.
	 * Enter the EM event dispatch loop on this EM-core.
	 */
	if (appl_conf->dispatch_duration.in_use) {
		/*
		 * Dispatch with options (via cmd line arguments),
		 * i.e. use em_dispatch_duration(...)
		 */
		run_core_dispatch_duration(&appl_conf->dispatch_duration.duration,
					   &appl_conf->dispatch_duration.opt);
	} else {
		/*
		 * Legacy dispatch, i.e. use em_dispatch(rounds)
		 * without other options
		 */
		run_core_dispatch(appl_conf->dispatch_rounds);
	}

	/*
	 * Terminate EM-core.
	 * Dispatch-loop done for application, prepare for controlled shutdown
	 */
	terminate_core(sync, appl_conf);

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
parse_args(int argc, char *argv[], parse_args_t *parsed /* out param */)
{
#define DURATION_ROUNDS          0x1001
#define DURATION_NS              0x1002
#define DURATION_EVENTS          0x1003
#define DURATION_NOEVENTS_ROUNDS 0x1004
#define DURATION_NOEVENTS_NS     0x1005
#define OPT_BURST_SIZE           0x1006
#define OPT_WAIT_NS              0x1007
#define OPT_SCHED_PAUSE          0x1008

	static const struct option longopts[] = {
		{"coremask",         required_argument, NULL, 'c'},
		{"process-per-core", no_argument,       NULL, 'p'},
		{"thread-per-core",  no_argument,       NULL, 't'},
		{"device-id",        required_argument, NULL, 'd'},
		{"eth-interface",    required_argument, NULL, 'i'},
		{"pktpool-em",       no_argument,       NULL, 'e'},
		{"pktpool-odp",      no_argument,       NULL, 'o'},
		{"pktin-mode",       required_argument, NULL, 'm'},
		{"pktin-vector",     no_argument,       NULL, 'v'},
		{"startup-mode",     required_argument, NULL, 's'},
		{"vecpool-em",       no_argument,       NULL, 'x'},
		{"vecpool-odp",      no_argument,       NULL, 'y'},
		{"help",             no_argument,       NULL, 'h'},
		/* em_dispatch(rounds): */
		{"dispatch-rounds",  required_argument, NULL, 'r'},
		/* em_dispatch_duration(duration, opt, ...): */
		{"duration-rounds",  required_argument, NULL, DURATION_ROUNDS},
		{"duration-ns",  required_argument, NULL, DURATION_NS},
		{"duration-events", required_argument, NULL, DURATION_EVENTS},
		{"duration-noevents-rounds", required_argument, NULL, DURATION_NOEVENTS_ROUNDS},
		{"duration-noevents-ns", required_argument, NULL, DURATION_NOEVENTS_NS},
		{"burst-size", required_argument, NULL, OPT_BURST_SIZE},
		{"wait-ns", required_argument, NULL, OPT_WAIT_NS},
		{"sched-pause", no_argument, NULL, OPT_SCHED_PAUSE},
		{NULL, 0, NULL, 0}
	};
	static const char *shortopts = "+c:ptd:r:i:oem:vs:xyh";

	/* set defaults: */
	parsed->args_em.device_id = 0;
	parsed->args_appl.pktio.in_mode = DIRECT_RECV;
	parsed->args_appl.startup_mode = STARTUP_ALL_CORES;
	parsed->args_appl.dispatch_duration.in_use = false;
	parsed->args_appl.dispatch_duration.burst_size = EM_SCHED_MULTI_MAX_BURST;

	opterr = 0; /* don't complain about unknown options here */

	APPL_PRINT("EM application options:\n");

	/*
	 * Parse the application & EM arguments and save core mask.
	 * Note: Use '+' at the beginning of optstring:
	 *       - don't permute the contents of argv[].
	 * Note: Stops at "--"
	 */
	while (1) {
		int opt;
		int long_index;

		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 'c': { /* --coremask */
			char *mask_str = optarg;
			char tmp_str[EM_CORE_MASK_STRLEN];
			int err;

			/*
			 * Store the core mask for EM - usage depends on the
			 * process-per-core or thread-per-core mode selected.
			 */
			em_core_mask_zero(&parsed->args_em.phys_mask);
			err = em_core_mask_set_str(mask_str,
						   &parsed->args_em.phys_mask);
			if (err)
				APPL_EXIT_FAILURE("Invalid coremask(%s) given",
						  mask_str);

			parsed->args_em.core_count =
			em_core_mask_count(&parsed->args_em.phys_mask);

			em_core_mask_tostr(tmp_str, sizeof(tmp_str),
					   &parsed->args_em.phys_mask);
			APPL_PRINT("  Coremask:     %s\n"
				   "  Core Count:   %i\n",
				   tmp_str, parsed->args_em.core_count);
		}
		break;

		case 'p': /* --process-per-core */
			parsed->args_em.process_per_core = 1;
			break;

		case 't': /* --thread-per-core */
			parsed->args_em.thread_per_core = 1;
			break;

		case 'd': { /* --device-id */
			char *endptr;

			long device_id = strtol(optarg, &endptr, 0);

			if (*endptr != '\0' || (uint64_t)device_id > UINT16_MAX)
				APPL_EXIT_FAILURE("Invalid device-id:%s", optarg);

			parsed->args_em.device_id = (uint16_t)(device_id & 0xffff);
		}
		break;

		case 'r': { /* --dispatch-rounds: em_dispatch(rounds) */
			long long dispatch_rounds = atoll(optarg);

			if (dispatch_rounds < 0)
				APPL_EXIT_FAILURE("Invalid dispatch-rounds:%s", optarg);
			parsed->args_appl.dispatch_rounds = dispatch_rounds;
		}
		break;

		case 'i': { /* --eth-interface */
			int i;
			size_t len, max;
			char *name;

			name = strtok(optarg, ",");
			for (i = 0; name != NULL; i++) {
				if (i > IF_MAX_NUM - 1)
					APPL_EXIT_FAILURE("Too many if's:%d",
							  i + 1);
				max = sizeof(parsed->args_appl.pktio.if_name[i]);
				len = strnlen(name, max);
				if (len + 1 > max)
					APPL_EXIT_FAILURE("Invalid if name:%s",
							  name);

				strncpy(parsed->args_appl.pktio.if_name[i], name, len);
				parsed->args_appl.pktio.if_name[i][len + 1] = '\0';

				name = strtok(NULL, ",");
			}
			parsed->args_appl.pktio.if_count = i;
		}
		break;

		case 'e': /* --pktpool-em */
			parsed->args_appl.pktio.pktpool_em = true;
			break;

		case 'o': /* --pktpool-odp */
			parsed->args_appl.pktio.pktpool_odp = true;
			break;

		case 'm': { /* --pktin-mode */
			int mode = atoi(optarg);

			if (mode == 0) {
				parsed->args_appl.pktio.in_mode = DIRECT_RECV;
			} else if (mode == 1) {
				parsed->args_appl.pktio.in_mode = PLAIN_QUEUE;
			} else if (mode == 2) {
				parsed->args_appl.pktio.in_mode = SCHED_PARALLEL;
			} else if (mode == 3) {
				parsed->args_appl.pktio.in_mode = SCHED_ATOMIC;
			} else if (mode == 4) {
				parsed->args_appl.pktio.in_mode = SCHED_ORDERED;
			} else {
				usage(argv[0]);
				APPL_EXIT_FAILURE("Unknown value: -m, --pktin-mode = %d", mode);
			}
		}
		break;

		case 'v': { /* --pktin-vector */
			parsed->args_appl.pktio.pktin_vector = true;
		}
		break;

		case 's': { /* --startup-mode */
			int mode = atoi(optarg);

			if (mode == 0) {
				parsed->args_appl.startup_mode = STARTUP_ALL_CORES;
			} else if (mode == 1) {
				parsed->args_appl.startup_mode = STARTUP_ONE_CORE_FIRST;
			} else {
				usage(argv[0]);
				APPL_EXIT_FAILURE("Unknown value: -s, --startup-mode = %d", mode);
			}
		}
		break;

		case 'x': /* --vecpool-em, only used if --pktin-vector given */
			parsed->args_appl.pktio.vecpool_em = true;
			break;

		case 'y': /* --vecpool-odp, only used if --pktin-vector given */
			parsed->args_appl.pktio.vecpool_odp = true;
			break;

		case 'h': /* --help */
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		case DURATION_ROUNDS: {
			long long rounds = atoll(optarg);

			if (unlikely(rounds <= 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.use_rounds = true;
			parsed->args_appl.dispatch_duration.rounds = rounds;
		}
		break;

		case DURATION_NS: {
			long long ns = atoll(optarg);

			if (unlikely(ns <= 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.use_ns = true;
			parsed->args_appl.dispatch_duration.ns = ns;
		}
		break;

		case DURATION_EVENTS: {
			long long events = atoll(optarg);

			if (unlikely(events <= 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.use_events = true;
			parsed->args_appl.dispatch_duration.events = events;
		}
		break;

		case DURATION_NOEVENTS_ROUNDS: {
			long long noevents_rounds = atoll(optarg);

			if (unlikely(noevents_rounds <= 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.use_noevents_rounds = true;
			parsed->args_appl.dispatch_duration.noevents_rounds = noevents_rounds;
		}
		break;

		case DURATION_NOEVENTS_NS: {
			long long noevents_ns = atoll(optarg);

			if (unlikely(noevents_ns <= 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.use_noevents_ns = true;
			parsed->args_appl.dispatch_duration.noevents_ns = noevents_ns;
		}
		break;

		case OPT_WAIT_NS: {
			long long wait_ns = atoll(optarg);

			if (unlikely(wait_ns < 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.wait_ns = wait_ns;
		}
		break;

		case OPT_BURST_SIZE: {
			int burst_size = atoi(optarg);

			if (unlikely(burst_size <= 0))
				APPL_EXIT_FAILURE("Invalid option: '--%s=%s'",
						  longopts[long_index].name, optarg);
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.burst_size = burst_size;
		}
		break;

		case OPT_SCHED_PAUSE:
			parsed->args_appl.dispatch_duration.in_use = true;
			parsed->args_appl.dispatch_duration.sched_pause = true;
			break;

		default:
			usage(argv[0]);
			APPL_EXIT_FAILURE("Unknown option: '%s'!", argv[optind - 1]);
			break;
		}
	}

	optind = 1; /* reset 'extern optind' from the getopt lib */

	/* Sanity check: */
	if (!parsed->args_em.core_count) {
		usage(argv[0]);
		APPL_EXIT_FAILURE("Give mandatory coremask!");
	}

	/* Print the device-id, use the default '0' if not given. */
	APPL_PRINT("  Device-id:    0x%" PRIX16 "\n", parsed->args_em.device_id);

	/* Sanity checks: */
	if (!(parsed->args_em.process_per_core ^ parsed->args_em.thread_per_core)) {
		usage(argv[0]);
		APPL_EXIT_FAILURE("Select EITHER:\n"
				  "process-per-core(-p) OR thread-per-core(-t)!");
	}
	if (parsed->args_em.thread_per_core)
		APPL_PRINT("  EM mode:      Thread-per-core\n");
	else
		APPL_PRINT("  EM mode:      Process-per-core\n");

	const char *startup_mode_str = "";

	/* Startup-mode */
	if (parsed->args_appl.startup_mode == STARTUP_ALL_CORES)
		startup_mode_str = "All EM-cores before application";
	else if (parsed->args_appl.startup_mode == STARTUP_ONE_CORE_FIRST)
		startup_mode_str = "One EM-core before application (then the rest)";
	/* other values are reported as errors earlier in parsing */

	APPL_PRINT("  Startup-mode: %s\n", startup_mode_str);

	/* Store the application name */
	size_t len = sizeof(parsed->args_appl.name);

	strncpy(parsed->args_appl.name, NO_PATH(argv[0]), len);
	parsed->args_appl.name[len - 1] = '\0';

	/* Packet I/O */
	if (parsed->args_appl.pktio.if_count > 0) {
		if (parsed->args_appl.pktio.pktpool_em && parsed->args_appl.pktio.pktpool_odp) {
			usage(argv[0]);
			APPL_EXIT_FAILURE("Select EITHER:\n"
					  "pktpool-em(-e) OR pktpool-odp(-o)!");
		}
		if (!parsed->args_appl.pktio.pktpool_em && !parsed->args_appl.pktio.pktpool_odp)
			parsed->args_appl.pktio.pktpool_em = true; /* default if none given */

		if (parsed->args_appl.pktio.pktpool_em)
			APPL_PRINT("  Pktio pool:   EM event-pool\n");
		else
			APPL_PRINT("  Pktio pool:   ODP pkt-pool\n");

		APPL_PRINT("  Pktin-mode:   %s\n",
			   pktin_mode_str(parsed->args_appl.pktio.in_mode));

		if (parsed->args_appl.pktio.pktin_vector) {
			APPL_PRINT("  Pktin-vector: Enabled\n");
			if (parsed->args_appl.pktio.vecpool_em &&
			    parsed->args_appl.pktio.vecpool_odp) {
				usage(argv[0]);
				APPL_EXIT_FAILURE("Select EITHER:\n"
						  "vecpool-em(-x) OR vecpool-odp(-y)!");
			}
			if (!parsed->args_appl.pktio.vecpool_em &&
			    !parsed->args_appl.pktio.vecpool_odp)
				parsed->args_appl.pktio.vecpool_em = true; /* default */

			if (parsed->args_appl.pktio.vecpool_em)
				APPL_PRINT("  Vector pool:  EM vector-pool\n");
			else
				APPL_PRINT("  Vector pool:  ODP vector-pool\n");
		} else {
			APPL_PRINT("  Pktin-vector: Disabled\n");
			parsed->args_appl.pktio.vecpool_em = false;
			parsed->args_appl.pktio.vecpool_odp = false;
		}
	} else {
		APPL_PRINT("  Pktio:        Not used\n");
		parsed->args_appl.pktio.pktpool_em = false;
		parsed->args_appl.pktio.pktpool_odp = false;
		parsed->args_appl.pktio.vecpool_em = false;
		parsed->args_appl.pktio.vecpool_odp = false;
	}

	/*
	 * Dispatch duration options - em_dispatch_duration() used.
	 */
	if (parsed->args_appl.dispatch_duration.in_use) {
		/* Don't allow a mix of em_dispatch() and em_dispatch_duration() options */
		if (unlikely(parsed->args_appl.dispatch_rounds)) {
			usage(argv[0]);
			APPL_EXIT_FAILURE("Don't mix em_dispatch() and em_dispatch_duration() options!\n"
					  "See usage above.");
		}

		if (parsed->args_appl.dispatch_duration.use_rounds)
			APPL_PRINT("  Dispatch-duration: rounds=%" PRIu64 "\n",
				   parsed->args_appl.dispatch_duration.rounds);

		if (parsed->args_appl.dispatch_duration.use_ns) {
			double sec = (double)parsed->args_appl.dispatch_duration.ns / 1.0e9;

			APPL_PRINT("  Dispatch-duration: ns=%" PRIu64 " (%g s)\n",
				   parsed->args_appl.dispatch_duration.ns, sec);
		}

		if (parsed->args_appl.dispatch_duration.use_events)
			APPL_PRINT("  Dispatch-duration: events=%" PRIu64 "\n",
				   parsed->args_appl.dispatch_duration.events);

		if (parsed->args_appl.dispatch_duration.use_noevents_rounds)
			APPL_PRINT("  Dispatch-duration: no-events rounds=%" PRIu64 "\n",
				   parsed->args_appl.dispatch_duration.noevents_rounds);

		if (parsed->args_appl.dispatch_duration.use_noevents_ns)
			APPL_PRINT("  Dispatch-duration: no-events ns=%" PRIu64 " (%g ns)\n",
				   parsed->args_appl.dispatch_duration.noevents_ns,
				   (double)parsed->args_appl.dispatch_duration.noevents_ns);

		APPL_PRINT("  Dispatch-options:  wait-ns=%" PRIu64 " burst-size=%" PRIu16 " sched-pause=%s\n",
			   parsed->args_appl.dispatch_duration.wait_ns,
			   parsed->args_appl.dispatch_duration.burst_size,
			   parsed->args_appl.dispatch_duration.sched_pause ? "true" : "false");
	} else {
		/* Dispatch rounds - em_dispatch_duration() used */
		const char *str = parsed->args_appl.dispatch_rounds == 0 ? "(=forever)" : "rounds";

		APPL_PRINT("  Dispatch:     %" PRIu64 " %s using em_dispatch()\n",
			   parsed->args_appl.dispatch_rounds, str);
	}
}

/**
 * Verify the cpu setup - sanity check and store cpus to use
 *
 * Verify the cpu count and mask against system values
 */
static void
verify_cpu_setup(const parse_args_t *parsed,
		 cpu_conf_t *cpu_conf /* out */)
{
	odp_cpumask_t invalid_mask;
	odp_cpumask_t check_mask;
	odp_cpumask_t zero_mask;
	int usable_cpus;
	cpu_set_t cpuset;
	int ret;

	const odp_cpumask_t *cpu_mask = &parsed->args_em.phys_mask.odp_cpumask;
	int num_cpus = parsed->args_em.core_count;

	/*
	 * Verify cpu setup
	 */
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
	 * Make sure no cpu in the cpu_mask is set in the invalid_mask.
	 * For a valid setup check_mask will be all-zero, otherwise it
	 * will contain the invalid cpus.
	 */
	odp_cpumask_and(&check_mask, &invalid_mask, cpu_mask);
	if (!odp_cpumask_equal(&zero_mask, &check_mask) ||
	    num_cpus > usable_cpus) {
		char cpus_str[ODP_CPUMASK_STR_SIZE];
		char check_str[ODP_CPUMASK_STR_SIZE];

		memset(cpus_str, '\0', sizeof(cpus_str));
		memset(check_str, '\0', sizeof(check_str));
		odp_cpumask_to_str(cpu_mask, cpus_str, sizeof(cpus_str));
		odp_cpumask_to_str(&check_mask, check_str, sizeof(check_str));

		APPL_EXIT_FAILURE("Invalid cpus - requested:%d available:%d\n"
				  "cpu_mask:%s of which invalid-cpus:%s",
				  num_cpus, usable_cpus, cpus_str, check_str);
	}

	/*
	 * Store the cpu conf to be set up for ODP
	 */
	odp_cpumask_copy(&cpu_conf->worker_mask,
			 &parsed->args_em.phys_mask.odp_cpumask);
	cpu_conf->num_worker = parsed->args_em.core_count;
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

	if (likely(appl_shm)) {
		for (i = 0; i < spin_count && !appl_shm->exit_flag; i++)
			env_atomic64_inc(&dummy);
	} else {
		for (i = 0; i < spin_count; i++)
			env_atomic64_inc(&dummy);
	}
}
