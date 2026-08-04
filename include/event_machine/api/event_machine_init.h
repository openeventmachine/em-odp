/*
 *   Copyright (c) 2018-2026, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_INIT_H_
#define EVENT_MACHINE_INIT_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup init Initialization and termination
 *  Event Machine initialization and termination
 * @{
 *
 * The Event Machine must be initialized before use. One thread (or process)
 * that will be part of EM needs to call em_init(). Additionally, after the user
 * has set up the participating threads and pinned them to HW-cores, each thread
 * needs to run em_init_local() to become EM-cores. Only now is an EM-core ready
 * to use the other EM API functions and can finally enter the dispatch-loop on
 * each EM-core via one of the em_dispatch...() APIs to start handling events.
 *
 * The EM termination sequence runs in the opposite order: each EM-core needs to
 * call em_term_local() before one last call to em_term().
 */

#include <event_machine/api/event_machine_config.h>
#include <event_machine/api/event_machine_types.h>
#include <event_machine/api/event_machine_error.h>
#include <event_machine/api/event_machine_core_mask.h>
#include <event_machine/api/event_machine_core.h>
#include <event_machine/api/event_machine_pool.h>
#include <event_machine/api/event_machine_hooks.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Event Machine run-time configuration options given at startup to em_init()
 *
 * The 'em_conf_t' struct should be initialized with em_conf_init() before use.
 * This initialization provides better backwards compatibility since all options
 * will be set to default values.
 * The user must further set the needed configuration and call em_init():
 *
 * @code
 *	em_conf_t conf;
 *	em_conf_init(&conf); // init with default values
 *	conf.thread_per_core = 1;
 *	...
 *	conf.core_count = N; // max number of EM cores for this instance
 *	conf.phys_mask = set N bits; // allowed CPUs for this instance
 *	...
 *	ret = em_init(&conf); // on one core
 *	...
 *	em_conf_local_t conf_local;
 *	em_conf_local_init(&conf_local); // init with default values
 *	conf_local.core_type = select EM-core type;
 *	...
 *	ret = em_init_local(&conf_local); // on each core added to EM
 *	...
 * @endcode
 *
 * Content is copied into EM by em_init().
 *
 * @note Several EM options are configured through compile-time defines.
 *       Run-time options allow using the same EM-lib with different configs.
 *       Also see the overridable EM runtime config file values,
 *       default file: config/em-odp.config
 *
 * @see em_conf_init(), em_init()
 */
typedef struct {
	/**
	 * EM device id - use different device ids for each EM instance or
	 * remote EM device that need to communicate with each other.
	 * Default value is 0.
	 */
	uint16_t device_id;

	/**
	 * Event Timer: enable=1, disable=0.
	 * Default value is 0 (disable).
	 */
	int event_timer;

	/**
	 * RunMode: EM run with one thread per core.
	 * Set 'true' to select thread-per-core mode.
	 * This is the recommended mode, but the user must explicitly set it to
	 * enable. Default value is 0.
	 * @note The user must set either 'thread_per_core' or
	 *       'process_per_core' but not both.
	 */
	int thread_per_core;

	/**
	 * RunMode: EM run with one process per core.
	 * Set 'true' to select process-per-core mode. Default value is 0.
	 * @note The user must set either 'thread_per_core' or
	 *       'process_per_core' but not both.
	 */
	int process_per_core;

	/**
	 * Maximum number of concurrently running EM-cores allowed for this
	 * EM instance.
	 *
	 * The max number of EM-cores is concerned with concurrently running
	 * EM-cores of the type EM_CORE_TYPE_CONTROL or EM_CORE_TYPE_WORKER.
	 * Additionally the user may create threads/processes of type
	 * EM_CORE_TYPE_EXTERNAL that are not included in this max count,
	 * see em_core_type_t.
	 *
	 * EM-core ids will be assigned from the range of 0 to 'core_count-1'
	 * during local initialization. Based on the options given to
	 * em_init_local(), EM-cores will either be assigned in startup/init
	 * order regardless of the actual physical CPU ids, or, based on the
	 * user provided 'core_id'.
	 * Note that the range may be discountiguous if the user removes
	 * EM-cores from the middle of the range (by calling em_term_local()) or
	 * if the user requests specific core-ids with gaps in between.
	 *
	 * The largest value that can be used for 'core_count' is the compile-
	 * time constant EM_MAX_CORES.
	 *
	 * Default value is 0 and needs to be changed by the user.
	 */
	uint32_t core_count;

	/**
	 * Core mask of physical CPUs allowed for EM-cores on this EM instance
	 * (this is a physical mask even though the 'em_core_mask_t' type is used).
	 *
	 * Similarly to '.core_count', the physical CPU mask is concerned with
	 * concurrently running EM-cores of the type EM_CORE_TYPE_CONTROL or
	 * EM_CORE_TYPE_WORKER.
	 * Additionally the user may create threads/processes of type
	 * EM_CORE_TYPE_EXTERNAL that are not included in this mask,
	 * see em_core_type_t.
	 *
	 * Default value is all-0 and needs to be changed by the user.
	 */
	em_core_mask_t phys_mask;

	/**
	 * Pool configuration for the EM default pool (EM_POOL_DEFAULT).
	 * Default value is set by em_pool_cfg_init() and needs to be changed
	 * by the user.
	 *
	 * Note that if the default pool configuration is also given in the
	 * config file through option 'startup_pools', it will override this
	 * default pool configuration.
	 */
	em_pool_cfg_t default_pool_cfg;

	/**
	 * EM log functions.
	 * Default values are NULL and causes EM to use internal default
	 * log-functions.
	 */
	struct {
		/** EM log function, user overridable, variable number of args*/
		em_log_func_t log_fn;
		/** EM log function, user overridable, va_list */
		em_vlog_func_t vlog_fn;
	} log;

	/**
	 * User provided API callback hooks.
	 * Set only the needed hooks to avoid performance degradation.
	 * Only used if EM_API_HOOKS_ENABLE != 0
	 */
	em_api_hooks_t api_hooks;

	/**
	 * User provided idle callback hooks.
	 * Set only the needed hooks to avoid performance degradation.
	 * Only used if EM_IDLE_HOOKS_ENABLE != 0
	 */
	em_idle_hooks_t idle_hooks;

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_conf_init() has been called
	 * before calling em_init().
	 */
	uint32_t __internal_check;
} em_conf_t;

/**
 * Initialize configuration parameters for em_init()
 *
 * Initialize em_conf_t to default values for all fields.
 * After initialization, the user further needs to set the mandatory fields of
 * 'em_conf_t' before calling em_init().
 * Always initialize 'conf' first with em_conf_init(&conf) to
 * ensure backwards compatibility with potentially added new options.
 *
 * @param conf  Address of the em_conf_t to be initialized
 *
 * @see em_init()
 */
void em_conf_init(em_conf_t *conf);

/**
 * Initialize the Event Machine.
 *
 * Must be called once at startup. Additionally an EM-core needs to call the
 * em_init_local() (or the older em_init_core()) function before using any
 * further EM API functions/resources.
 *
 * @param conf   EM global config options
 *
 * @return EM_OK if successful.
 *
 * @see em_init_local() for EM-core specific init after em_init().
 */
em_status_t em_init(const em_conf_t *conf);

/**
 * Retrieve EM configuration options given to em_init()
 *
 * This function returns the configuration options that were provided to
 * em_init() at initialization time. The function can be called only after
 * a successful call to em_init().
 *
 * See the documentation of each option in em_conf_t for more details.
 *
 * @param[out] conf_opts  Filled with EM configuration options given to
 *                        em_init()
 *
 * @return EM_OK if the configuration is written to conf_opts successfully
 */
em_status_t em_conf_opts(em_conf_t *conf_opts/*out*/);

/** Print EM configuration options given to em_init(). */
void em_conf_opts_print(void);

/**
 * Input poll function - poll various input sources for pkts/events and enqueue
 * into EM.
 *
 * User provided function - EM calls this, if not NULL, in the dispatch loop on
 * each core - set via 'em_conf_local_t::input_poll_fn'
 *
 * @return number of pkts/events received from input and enqueued into EM
 */
typedef int (*em_input_poll_func_t)(void);

/**
 * 'Periodical' draining of output from EM, if needed.
 *
 * User provided function - EM calls this, if not NULL, in the dispatch loop on
 * each core - set via 'em_conf_local_t::output_drain_fn'
 *
 * Draining of output events/pkts: EM will every once in a while call this
 * user provided function to ensure that low rate buffered output is eventually
 * sent out. Not needed if your EM output queues (EM_QUEUE_TYPE_OUTPUT) always
 * sends all events out. Useful in situations where output is buffered and sent
 * out in bursts when enough output has been gathered - single events or low
 * rate flows may, without this function, never be sent out (or too late) if the
 * buffering threshold has not been reached.
 *
 * @return number of events successfully drained and sent for output
 */
typedef int (*em_output_drain_func_t)(void);

/**
 * Event Machine thread-local configuration options for em_init_local()
 */
typedef struct {
	/**
	 * Worker/control EM-core or external thread (mandatory)
	 */
	em_core_type_t core_type;

	/**
	 * Requested core-id for this EM-core - or let EM assign.
	 *
	 * Use '-1' to let EM assign the core-id (default & recommended):
	 *  - EM-cores will be assigned an id in startup/init order regardless
	 *    of the actual physical CPU ids.
	 *
	 * Use '0' to 'core_count - 1' to request a specific core-id:
	 *  - An id will be assigned based on the user provided 'core_id'.
	 *  - The user must ensure that the requested 'core_id' is not already
	 *    in use or EM local initialization will fail!
	 *
	 * The max value is set by em_conf_t::core_count, thus the allowed range
	 * is from 0 to 'core_count - 1' (where 'core_count' <= EM_MAX_CORES).
	 *
	 * Note that the range may be discountiguous if the user removes
	 * EM-cores from the middle of the range (by calling em_term_local()) or
	 * if the user requests specific core-ids with gaps in between.
	 *
	 * Only valid for EM-cores of the type EM_CORE_TYPE_WORKER and
	 * EM_CORE_TYPE_CONTROL.
	 * Ignored for EM external threads/processes: EM_CORE_TYPE_EXTERNAL.
	 */
	int core_id; /* -1: let EM assign (default)*/

	/**
	 * User provided function, called from within the EM-dispatch
	 * loop, mainly for polling various input sources for events or
	 * pkts and then enqueue them into EM.
	 * Set to 'NULL' if not needed (default).
	 *
	 * Only valid for EM-cores of the type EM_CORE_TYPE_WORKER and
	 * EM_CORE_TYPE_CONTROL. External threads do not dispatch and can thus
	 * not utilize this functionality.
	 */
	em_input_poll_func_t input_poll_fn;

	/**
	 * User provided function, called from within the EM-dispatch
	 * loop, mainly for 'periodical' draining of buffered output to
	 * make sure events/pkts are eventually sent out even if the
	 * rate is low or stops for a while.
	 * Set to 'NULL' if not needed (default).
	 *
	 * Only valid for EM-cores of the type EM_CORE_TYPE_WORKER and
	 * EM_CORE_TYPE_CONTROL. External threads do not dispatch and can thus
	 * not utilize this functionality.
	 */
	em_output_drain_func_t output_drain_fn;

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_conf_local_init() has been called
	 * before calling em_init_local().
	 */
	uint32_t __internal_check;
} em_conf_local_t;

/**
 * Initialize configuration parameters for em_init_local()
 *
 * Initialize em_conf_local_t to default values for all fields.
 * After initialization, the user further needs to set the mandatory fields of
 * 'em_conf_local_t' before calling em_init_local().
 * Always initialize 'conf_local' first with em_conf_local_init(&conf_local) to
 * ensure backwards compatibility with potentially added new options.
 *
 * @param conf_local  Address of the em_conf_local_t to be initialized
 *
 * @see em_init_local()
 */
void em_conf_local_init(em_conf_local_t *conf_local);

/**
 * Initialize the current thread/process as a worker, control or external EM-core.
 *
 * Must be called once by each thread/process that needs access to EM resources
 * within the instance.
 *
 * @note
 * Pin the thread or process to the desired physical cpu(s)/core(s) before
 * calling this function.
 * The pinning depends on the EM-core type set in 'em_conf_local_t::core_type':
 * - EM_CORE_TYPE_WORKER:   pin to a unique physical cpu/core allowed by
 *                          'em_conf_t::phys_mask' as given to em_init().
 * - EM_CORE_TYPE_CONTROL:  pin to a set of cpus/cores allowed by
 *                          'em_conf_t::phys_mask' as given to em_init().
 * - EM_CORE_TYPE_EXTERNAL: no restrictions on pinning.
 *
 * @note
 * em_conf_local_init(&conf_local) must be called before setting the mandatory
 * fields of 'em_conf_local_t' and calling em_init_local().
 *
 * @param conf_local
 *
 * @return EM_OK if successful.
 *
 * @see em_core_type_t for a description of the core types.
 */
em_status_t em_init_local(const em_conf_local_t *conf_local);

/**
 * Retrieve EM core-local configuration options given to em_init_local()
 * or used by em_init_core().
 *
 * This function returns the configurations of the calling EM-core and can
 * be called only after a successful call to em_init_local()/em_init_core().
 *
 * @note Retrieving configurations for cores other than the calling core is
 *       not supported.
 *
 * @param[out] conf_local_opts  Filled with EM core-local configuration
 *                              options given to em_init_local() or used
 *                              by em_init_core().
 *
 * @return EM_OK if the configuration is written to conf_local_opts successfully
 */
em_status_t em_conf_local_opts(em_conf_local_t *conf_local_opts/*out*/);

/**
 * Print EM core-local configuration options given to em_init_local() or used
 * by em_init_core().
 *
 * @note This function prints configurations of the calling EM-core and can
 *       be called only after a successful call to em_init_local() or
 *       em_init_core(). Printing configurations for cores other than the
 *       calling core is not supported.
 */
void em_conf_local_opts_print(void);

/**
 * Initialize the current thread/process as a worker EM-core.
 *
 * EM resources can be created after a successful call to this function and the
 * worker EM-core can start dispatching events.
 *
 * @note
 * The thread or process must be pinned to a unique physical core before
 * running em_init_core().
 *
 * @note
 * Prefer using the newer em_init_local()/em_term_local() sequence over the
 * older em_init_core()/em_term_core().
 *
 * Using this function is similar to
 *   em_init_local(em_conf_local_t::core_type=EM_CORE_TYPE_WORKER)
 * without the extra options provided by 'em_conf_local_t'.
 *
 * @return EM_OK if successful.
 *
 * @see em_init()
 */
em_status_t em_init_core(void);

/**
 * Event Machine thread-local termination configuration options for
 * em_term_local().
 */
typedef struct {
	struct {
		/**
		 * Select: Use the option 'flush_orphans' below or use the
		 *         global default value from the EM config file.
		 * false: Use 'queue_group.term_local_flush_orphans' from the
		 *        EM config file (default).
		 *  true: Override the EM config file setting for this EM-core's
		 *        termination and use the option '.flush_orphans' below.
		 *
		 * Only valid for EM-cores of the type EM_CORE_TYPE_WORKER and
		 * EM_CORE_TYPE_CONTROL. External threads do not dispatch and
		 * can thus not utilize this functionality.
		 */
		bool in_use;
		/**
		 * Flush events from orphaned queue groups during EM-core local
		 * termination (true/false) - only evaluated if '.in_use=true'.
		 *
		 * During EM-core local termination: flush the scheduler from
		 * events from queues that belong to queue groups that only this
		 * terminating EM-core is handling.
		 * This may be needed to avoid events being stuck in the
		 * scheduler when an EM-core is terminating and orphaned queue
		 * groups exist with queues that still have events in them.
		 * Alternatively, the application may modify the orphan
		 * queue group(s) to have another EM-core handle the events,
		 * or create a new EM-core after EM-core termination on another
		 * cpu - then no flushing is needed (use 'false').
		 *
		 * Overrides the config file value
		 * 'queue_group.term_local_flush_orphans' for the termination
		 * of this EM-core.
		 * false: don't flush orphaned queue groups (default).
		 *  true: flush orphaned queue groups.
		 *
		 * Only valid for EM-cores of the type EM_CORE_TYPE_WORKER and
		 * EM_CORE_TYPE_CONTROL. External threads do not dispatch and
		 * can thus not utilize this functionality.
		 */
		bool flush_orphans;
	} queue_group_flush;

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_term_local_init() has been called
	 * before calling em_term_local().
	 */
	uint32_t __internal_check;
} em_term_local_t;

/**
 * Initialize configuration parameters for em_term_local()
 *
 * Initialize em_term_local_t to default values for all fields.
 * After initialization, the user further needs to set the mandatory fields of
 * 'em_term_local_t' before calling em_term_local().
 * Always initialize 'term_local' first with em_term_local_init(&term_local)
 * to ensure backwards compatibility with potentially added new options.
 *
 * @param term_local  Address of the em_term_local_t to be initialized
 *
 * @see em_term_local()
 */
void em_term_local_init(em_term_local_t *term_local);

/**
 * Local terminatation of a thread set up to run EM
 *
 * Local termination is required by each worker, control and external EM-core
 * before one call to em_term().
 *
 * The EM worker and control cores will leave all queue groups before returning.
 *
 * @note
 * em_term_local_init(&term_local) must be called before setting the
 * fields of 'em_term_local_t' and calling em_term_local().
 *
 * @return EM_OK if successful
 */
em_status_t em_term_local(const em_term_local_t *term_local);

/**
 * Local terminatation of a worker EM-core.
 *
 * Local termination on a worker EM-core initialized by em_init_core()
 * (or by em_init_local(em_conf_local_t::core_type=EM_CORE_TYPE_WORKER)).
 * Local termination is required on all EM-cores before one call to em_term().
 *
 * @note
 * Prefer using the newer em_init_local()/em_term_local() sequence over the
 * older em_init_core()/em_term_core().
 *
 * The EM-core will leave all queue groups before returning.
 *
 * @return EM_OK if successful.
 *
 * @see em_term()
 */
em_status_t em_term_core(void);

/**
 * Terminate the Event Machine.
 *
 * Called once at exit. Additionally, before the one final call to em_term(),
 * each EM-core (and EM external thread) needs to call the em_term_local()
 * function to free up local resources.
 *
 * @return EM_OK if successful.
 *
 * @see em_term_core() for EM-core specific termination before em_term().
 */
em_status_t em_term(void);

/**
 * Return the EM device-id for this instance.
 *
 * This is a convenience function that returns the EM device-id given by the
 * user to em_init() via the em_conf_t::device_id field.
 *
 * The function should only be called after a successful EM initialization.
 *
 * @return the device-id of this EM instance.
 */
uint16_t em_device_id(void);

/**
 * EM startup pool configuration
 */
typedef struct {
	em_pool_t pool;
	char name[EM_POOL_NAME_LEN];
	em_pool_cfg_t cfg;
} em_startup_pool_conf_t;

/**
 * EM config options read from the config file
 *
 * See the config/em-odp.conf file for description of the options.
 */
typedef struct {
	struct {
		int enable;
		const char *ip_addr;
		int port;
	} cli;

	struct {
		unsigned int poll_ctrl_interval;
		uint64_t poll_ctrl_interval_ns;

		unsigned int poll_drain_interval;
		uint64_t poll_drain_interval_ns;

		uint64_t sched_wait_ns;
		bool sched_pause;
	} dispatch;

	struct {
		bool start_local_fn_at_init;
		bool stop_local_fn_at_term;
	} eo;

	struct {
		int enable;
		int store_state;
		int store_first_u32;
		int prealloc_pools;
	} esv;

	struct {
		bool order_keep;
		unsigned int num_order_queues;
	} event_chaining;

	struct {
		em_pool_stats_opt_t statistics;
		unsigned int align_offset; /* bytes */
		size_t user_area_size; /* bytes */
		unsigned int pkt_headroom; /* bytes */
	} pool;

	struct {
		unsigned int num_static;
		unsigned int num_dynamic;
		unsigned int num_aggr;
		unsigned int min_events_default;
		struct {
			int map_mode;
			int custom_map[EM_QUEUE_PRIO_NUM];
		} priority;
	} queue;

	struct {
		bool create_core_queue_groups;
		bool term_local_flush_orphans;
	} queue_group;

	struct {
		bool shared_tmo_pool_enable;
		uint32_t shared_tmo_pool_size;
		uint32_t tmo_pool_cache;
		struct {
			uint32_t timer_event_pool_size;
			uint32_t timer_event_pool_cache;
		} ring;
	} timer;

	struct {
		enum {
			/* Using ODP packet vectors*/
			EM_VECTOR_BACKEND_PACKET = 1,
			/* Using ODP event vectors */
			EM_VECTOR_BACKEND_EVENT  = 2
		} backend;

		const char *backend_str;
	} vector;

	struct {
		uint32_t num;
		em_startup_pool_conf_t conf[EM_CONFIG_POOLS];
	} startup_pools;
} em_cfgfile_opts_t;

/**
 * Retrieve current EM config file options
 *
 * This function populates cfgfile_opts with the configuration options read
 * from the config file specified by the EM_CONFIG_FILE environment variable.
 * If an option is not set in the config file, the default value defined in
 * config/em-odp.conf is used. The function can be called only after a
 * successful call to em_init().
 *
 * See the documentation of each option for more details in config/em-odp.conf.
 *
 * @param[out] cfgfile_opts  Filled with the combined options from the default
 *                           config file and the config file specified by
 *                           EM_CONFIG_FILE
 *
 * @return EM_OK if the configuration is written to cfgfile_opts successfully
 */
em_status_t em_cfgfile_opts(em_cfgfile_opts_t *cfgfile_opts/*out*/);

/** Print EM config options set in the config file. */
void em_cfgfile_opts_print(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_INIT_H_ */
