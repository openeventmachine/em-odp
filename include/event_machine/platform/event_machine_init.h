/*
 *   Copyright (c) 2018, Nokia Solutions and Networks
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
 * The Event Machine must be initialized before use. One core that will be part
 * of EM calls em_init(). Additionally, after the user has set up the threads,
 * or processes and pinned those to HW-cores, each participating core, i.e.
 * EM-core, needs to run em_init_core(). Only now is an EM-core ready to use the
 * other EM API functions and can finally enter the dispatch-loop via
 * em_dispath() on each core that should handle events.
 *
 * The EM termination sequence runs in the opposite order: each core needs to
 * call em_term_core() before one last call to em_term().
 *
 * The 'em_conf_t' type given to em_init() and em_term() is HW/platform specific
 * and is defined in event_machine_hw_types.h
 *
 * Do not include this from the application, event_machine.h will
 * do it for you.
 */

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
 *	conf.core_count = N;
 *	conf.phys_mask = set N bits; // use em_core_mask_...() functions to set
 *	...
 *	ret = em_init(&conf); // on one core
 *	...
 *	ret = em_init_core(); // on each of the 'conf.core_count' cores
 * @endcode
 *
 * Content is copied into EM by em_init().
 *
 * @note Several EM options are configured through compile-time defines.
 *       Run-time options allow using the same EM-lib with different configs.
 *       Also see the overrideable EM runtime config file values,
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
	 * Number of EM-cores (== number of EM-threads or EM-processes).
	 * The 'core_count' must match the number of bits set in 'phys_mask'.
	 * EM-cores will be enumerated from 0 to 'core_count-1' regardless of
	 * the actual physical core ids.
	 * Default value is 0 and needs to be changed by the user.
	 */
	int core_count;

	/**
	 * Physical core mask, exactly listing the physical CPU cores to be used
	 * by EM (this is a physical core mask even though the 'em_core_mask_t'
	 * type is used).
	 * Default value is all-0 and needs to be changed by the user.
	 * @note EM otherwise operates on logical cores, i.e. enumerated
	 *       contiguously from 0 to 'core_count-1' and a logical
	 *       EM core mask has 'core_count' consequtively set bits.
	 *       Example - physical mask vs. corresponding EM core mask:
	 *          .core_count = 8
	 *          .physmask: 0xf0f0 (binary: 1111 0000 1111 0000 - 8 set bits)
	 *                     = 8 phys-cores (phys-cores 4-7,12-15)
	 *       ==> EM-mask:  0x00ff (0000 0000 1111 1111 binary) - 8 EM cores
	 *                     = 8 EM-cores (EM-cores 0-7)
	 */
	em_core_mask_t phys_mask;

	/**
	 * Pool configuration for the EM default pool (EM_POOL_DEFAULT).
	 * Default value is all-0 and needs to be changed by the user.
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

	/** EM event/pkt input related functions and config */
	struct {
		/**
		 * User provided function, called from within the EM-dispatch
		 * loop, mainly for polling various input sources for events or
		 * pkts and then enqueue them into EM.
		 * Set to 'NULL' if not needed (default).
		 */
		em_input_poll_func_t input_poll_fn;
		/**
		 * EM core mask to control which EM-cores (0 to 'core_count-1')
		 * input_poll_fn() will be called on.
		 * The provided mask has to be equal or a subset of the
		 * EM core mask with all 'core_count' bits set.
		 * A zero mask means execution on _all_ EM cores (default).
		 */
		em_core_mask_t input_poll_mask;
	} input;

	/** EM event/pkt output related functions and config */
	struct {
		/**
		 * User provided function, called from within the EM-dispatch
		 * loop, mainly for 'periodical' draining of buffered output to
		 * make sure events/pkts are eventually sent out even if the
		 * rate is low or stops for a while.
		 * Set to 'NULL' if not needed (default).
		 */
		em_output_drain_func_t output_drain_fn;
		/**
		 * EM core mask to control which EM-cores (0 to 'core_count-1')
		 * output_drain_fn() will be called on.
		 * The provided mask has to be equal or a subset of the
		 * EM core mask with all 'core_count' bits set.
		 * A zero mask means execution on _all_ EM cores (default).
		 */
		em_core_mask_t output_drain_mask;
	} output;

	/**
	 * User provided API callback hooks.
	 * Set only the needed hooks to avoid performance degradation.
	 * Only used if EM_API_HOOKS_ENABLE != 0
	 */
	em_api_hooks_t api_hooks;

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
 * @param param   Address of the em_conf_t to be initialized
 *
 * @see em_init()
 */
void em_conf_init(em_conf_t *conf);

/**
 * Initialize the Event Machine.
 *
 * Must be called once at startup. Additionally each EM-core needs to call the
 * em_init_core() function before using any further EM API functions/resources.
 *
 * @param conf   EM runtime config options,
 *               HW/platform specific: see event_machine_hw_types.h
 *
 * @return EM_OK if successful.
 *
 * @see em_init_core() for EM-core specific init after em_init().
 */
em_status_t
em_init(em_conf_t *conf);

/**
 * Initialize an EM-core.
 *
 * Must be called once by each EM-core (= process, thread or bare metal core).
 * EM queues, EOs, queue groups etc. can be created after a successful return
 * from this function.
 *
 * @return EM_OK if successful.
 *
 * @see em_init()
 */
em_status_t
em_init_core(void);

/**
 * Terminate the Event Machine.
 *
 * Called once at exit. Additionally, before the one call to em_term(),
 * each EM-core needs to call the em_term_core() function to free up local
 * resources.
 *
 * @param conf          EM runtime config options
 *
 * @return EM_OK if successful.
 *
 * @see em_term_core() for EM-core specific termination before em_term().
 */
em_status_t
em_term(em_conf_t *conf);

/**
 * Terminate an EM-core.
 *
 * Called by each EM-core (= process, thread or bare metal core) before
 * the one call call to em_term();
 *
 * @return EM_OK if successful.
 *
 * @see em_term()
 */
em_status_t
em_term_core(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_INIT_H_ */
