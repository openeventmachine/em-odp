/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_DISPATCHER_H_
#define EVENT_MACHINE_DISPATCHER_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_dispatcher Dispatcher
 *  Event Machine dispatcher related services.
 * @{
 *
 * The EM dispatcher contains the main loop of processing on each EM-core and
 * interfaces with the scheduler to obtain events for processing.
 * Further, the EM dispatcher is responsible for passing the events received
 * on a core, from the scheduler, to the correct EO-receive function along with
 * information about which queue the events originated from, what their types
 * are etc.
 *
 * EM provides APIs to register, or unregister, dispatch callback hooks, i.e.
 * user provided callback functions that will be run just before EM calls the
 * EO-receive function or after returning from it. These callbacks are referred
 * to as enter- and exit-callbacks respectively.
 * The dispatch callbacks can be used to collect debug information, statistics
 * or implement new functionality. The enter-callback is called before entering
 * the EO-receive function on each core separately. The callback gets all the
 * same arguments as the EO-receive function and can additionally modify them.
 * The exit-callback works in a similar way, but is instead called after the
 * EO-receive function returns and has no arguments except for the EO handle.
 * Multiple callbacks can be registered. The calling order of multiple
 * registered functions is the order of registration. If the same function is
 * registered twice then it will be called twice. The max amount of simultaneous
 * callbacks is set by the define 'EM_CALLBACKS_MAX'.
 * If an enter-callback changes the event handle to UNDEF, the next callback
 * will still be called with event as UNDEF, but the EO-receive function won't
 * be called with an UNDEF event.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * @brief EM dispatch duration selection flags
 *
 * Combining (bitwise OR) several DURATION flags will instruct the EM dispatcher
 * to dispatch until the first 'duration' condition is met, whichever happens
 * first.
 */
typedef enum {
	/** Select: dispatch forever, never return */
	EM_DISPATCH_DURATION_FOREVER = 0,
	/** Select: dispatch until em_dispatch_opt_t::duration.rounds reached */
	EM_DISPATCH_DURATION_ROUNDS = 1,
	/** Select: dispatch until em_dispatch_opt_t::duration.ns reached */
	EM_DISPATCH_DURATION_NS = 2,
	/** Select: dispatch until em_dispatch_opt_t::duration.events reached */
	EM_DISPATCH_DURATION_EVENTS = 4,

	/** Select: dispatch until em_dispatch_opt_t::duration.no_events.rounds reached */
	EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS = 8,
	/** Select: dispatch until em_dispatch_opt_t::duration.no_events.ns reached */
	EM_DISPATCH_DURATION_NO_EVENTS_NS = 16,

	/* Keep last, for error checking */
	EM_DISPATCH_DURATION_LAST
} em_dispatch_duration_select_t;

/**
 * Dispatch duration.
 *
 * Select which dispatch duration, or combination, is to be used with the
 * em_dispatch_duration() function.
 * Bitwise OR .select-flags for a combination.
 * Dispatch will end when one of the selected 'duration' options is
 * reached, whichever is hit first.
 */
typedef struct {
	/**
	 * Select which 'duration'-fields that should be taken into account
	 * when evaluating the em_dispatch_duration() run time.
	 *
	 * Only the duration fields that correspond to set .select-flags
	 * will be used.
	 */
	em_dispatch_duration_select_t select;

	/*
	 * Duration fields / values below considered according to .select-flags:
	 */

	/**
	 * Dispatch for the given number of rounds, if used must be > 0.
	 * Only considered if .select contains EM_DISPATCH_DURATION_ROUNDS.
	 */
	uint64_t rounds;

	/**
	 * Dispatch (at least) for the given time in nanoseconds,
	 * if used must be > 0.
	 * Only considered if .select contains EM_DISPATCH_DURATION_NS.
	 *
	 * Using a large value for the option 'wait_ns' relative to .ns
	 * might delay the return from dispatch.
	 *
	 * The runtime of the EO-receive function for the last batch of events
	 * is not covered by .ns.
	 * EM will request new events to dispatch while the
	 * elapsed dispatch time is < .ns.
	 */
	uint64_t ns;

	/**
	 * Dispatch until (at least) the given number of events have been
	 * handled, if used must be > 0.
	 * Only considered if .select contains EM_DISPATCH_DURATION_EVENTS.
	 *
	 * Note that the option 'burst_size' affects the number of events
	 * dispatched. EM will request new events to dispatch while the number
	 * of dispatched events is < .events and then handle the whole burst.
	 *
	 * The option 'sched_pause=true' might also increase the number of
	 * events dispatched since the EM dispatcher needs to fetch and handle
	 * any leftover events held locally by the scheduler before returning.
	 */
	uint64_t events;

	struct {
		/**
		 * Dispatch until no events have been received for the
		 * given number of rounds, if used must be > 0.
		 * Only considered if .select contains
		 * EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS.
		 */
		uint64_t rounds;

		/**
		 * Dispatch until no events have been received for the
		 * given time in nanoseconds, if used must be > 0.
		 * Only considered if .select contains
		 * EM_DISPATCH_DURATION_NO_EVENTS_NS.
		 */
		uint64_t ns;
	} no_events;
} em_dispatch_duration_t;

/**
 * @brief EM dispatch options
 *
 * The options must be initialized once with em_dispatch_opt_init() before
 * using them with other em_dispatch_...() calls for the first time. Further
 * calls to em_dispatch_...() with the same options structure do not need
 * initialization and the user is allowed to modify the options between calls
 * to change the dispatch behaviour.
 *
 * @see em_dispatch_opt_init(), em_dispatch_duration() etc.
 */
typedef struct {
	/**
	 * Scheduler wait-for-events timeout in nanoseconds, might save power.
	 * The scheduler will wait for events, if no immediately available, for
	 * 'wait_ns' nanoseconds per scheduling / dispatch round.
	 *
	 * Note that using a large 'wait_ns' value relative to a
	 * dispatch duration in 'ns' might delay the return from dispatch.
	 *
	 * 0: do not wait for events (default)
	 */
	uint64_t wait_ns;

	/**
	 * Scheduler burst size.
	 * The max number of events the dispatcher will request in one burst
	 * from the scheduler.
	 *
	 * default: EM_SCHED_MULTI_MAX_BURST
	 */
	uint16_t burst_size;

	/**
	 * Override the possibly configured dispatcher input-polling callback
	 * (set via em_conf_t::input.input_poll_fn).
	 *
	 * false: Do not skip the input-poll callback if configured (default).
	 * true:  Skip the input-poll callback in the dispatcher.
	 */
	bool skip_input_poll; /* override em_conf_t configuration */

	/**
	 * Override the possibly configured dispatcher output-drain callback
	 * (set via em_conf_t::output.output_drain_fn).
	 *
	 * false: Do not skip the output-drain callback if configured (default).
	 * true:  Skip the output-drain callback in the dispatcher.
	 */
	bool skip_output_drain; /* override em_conf_t configuration */

	/**
	 * Pause the scheduler on the calling core when exiting the EM dispatch
	 * function. If enabled, will also resume the scheduling when entering
	 * dispatch. Pausing also implicitly causes the dispatcher to fetch and
	 * handle any leftover events held locally by the scheduler before
	 * returning.
	 *
	 * false: Do not pause and resume the scheduler when entering and
	 *        exiting dispatch (default).
	 * true:  Pause scheduling when exiting dispatch and resume scheduling
	 *        when entering. EM will further empty and dispatch any remaining
	 *        events locally stashed in the scheduler before returning
	 *        causing some extra dispatch 'rounds' to be run.
	 */
	bool sched_pause;

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_dispatch_opt_init(opt) has been called
	 * before use with dispatch functions.
	 */
	uint32_t __internal_check;
} em_dispatch_opt_t;

/**
 * @brief Dispatch results
 *
 * Output struct for returning the results of the em_dispatch_...() functions
 * in. Usage of 'em_dispatch_results_t *results' with dispatch functions is
 * optional and 'NULL' can be used if not interested in the results.
 */
typedef struct {
	/**
	 * The number of dispatch rounds that were run.
	 */
	uint64_t rounds;

	/**
	 * The time in nanoseconds that dispatch was run.
	 * Only filled if requesting EM to dispatch for a certain amount of
	 * time, i.e. if EM_DISPATCH_DURATION_NS or
	 * EM_DISPATCH_DURATION_NO_EVENTS_NS duration selection flags were set
	 * in em_dispatch_duration_t::select when using em_dispatch_duration().
	 * Also set when used with em_dispatch_ns().
	 */
	uint64_t ns;

	/**
	 * The number of events that were dispatched.
	 */
	uint64_t events;
} em_dispatch_results_t;

/**
 * @brief Initialize the EM dispatch options.
 *
 * The options passed to em_dispatch_...() need to be initialized once before
 * first use. Further calls to em_dispatch_...() with the same options structure
 * do not need initialization and the user is allowed to modify the options
 * between calls to change dispatch behaviour.
 *
 * This function may be called before em_init() or em_init_core() since it only
 * sets the default values for the 'em_dispatch_opt_t *opt' argument.
 *
 * @param opt
 */
void em_dispatch_opt_init(em_dispatch_opt_t *opt);

/**
 * @brief Run the EM dispatcher for a certain duration with options.
 *
 * Called by an EM-core to dispatch (with options) events for EM processing.
 * The EM dispatcher internally queries the scheduler for events for the
 * calling EM-core and then dispatches them for processing, i.e. passes the
 * events to the application EO's receive-function based on the queue the events
 * were received / dequeued from.
 *
 * Combining (bitwise OR) several DURATION selection flags
 * (see em_dispatch_duration_select_t) will dispatch until the first
 * duration-condition is met, whichever happens first.
 *
 * Example usage:
 * @code
 *	em_dispatch_duration_t duration;
 *	em_dispatch_opt_t opt;
 *	em_dispatch_results_t results;
 *	em_status_t status;
 *
 *	em_dispatch_opt_init(&opt); // Mandatory once before first use!
 *	opt.wait_ns = 10000;        // Wait max 10 us for events from scheduler
 *	opt.sched_pause = false;    // Don't pause scheduling on return
 *
 *	// Dispatch for 1000 rounds, 200 us or until 300 events have been
 *	// handled. Return when the first of these conditions is met.
 *	duration.select = EM_DISPATCH_DURATION_ROUNDS |
 *			  EM_DISPATCH_DURATION_NS |
 *			  EM_DISPATCH_DURATION_EVENTS;
 *	duration.rounds = 1000;
 *	duration.ns = 200000; // 200 us
 *	duration.events = 300;
 *	...
 *	do {
		// Dispatch until '.rounds' or '.ns' or '.events' reached
 *		status = em_dispatch_duration(&duration, &opt, &results);
 *		...
 *		// Update 'duration' and 'opt' based on 'results'
 *		// and/or runtime conditions
 *	} while (do_dispatch(&results, ...));
 *
 *	// Prepare to leave EM dispatching
 *	duration.select = EM_DISPATCH_DURATION_NO_EVENTS_NS;
 *	duration.no_events.ns = 100000;
 *	opt.wait_ns = 0;              // No waiting for events
 *	opt.skip_input_poll = true;   // No callbacks
 *	opt.skip_output_drain = true; // -"-
 *	opt.sched_pause = true;       // Pause scheduling on this EM-core
 *
 *	status = em_dispatch_duration(&duration, &opt, &results);
 *	// Leave EM dispatching for a while
 * @endcode
 *
 * @param      duration Dispatch duration.
 * @param      opt      Dispatch options (optional, can be NULL).
 *                      If used, must have been initialized with
 *                      em_dispatch_opt_init(). One initialization is enough,
 *                      later calls to em_dispatch_...(...opt) can reuse (the
 *                      possibly modified) 'opt'.
 *                      Using NULL is the same as passing 'opt' initialized
 *                      with em_dispatch_opt_init(&opt) without further changes.
 * @param[out] results  Dispatch results (optional, can be NULL).
 *                      Filled for successful dispatch scenarios, i.e. when the
 *                      return value is EM_OK.
 *
 * @return Error status code
 * @retval EM_OK when dispatch was successful, 'result' is filled (if provided)
 * @retval other than EM_OK on error, 'result' is untouched
 */
em_status_t em_dispatch_duration(const em_dispatch_duration_t *duration,
				 const em_dispatch_opt_t *opt,
				 em_dispatch_results_t *results /*out*/);
/**
 * @brief Run the EM dispatcher for a given amount of time (in nanoseconds).
 *
 * Similar to em_dispatch_duration(), but with a simplified dispatch duration:
 * here only the number of nanoseconds to dispatch is provided.
 *
 * Using a large value for 'opt.wait_ns' relative to 'ns' might delay the
 * return from dispatch.
 *
 * The runtime of the EO-receive function for the last batch of events
 * is not covered by 'ns'.
 * EM will request new events to dispatch while the elapsed time is < 'ns'.
 *
 * @see em_dispatch_duration() for documentation and usage.
 *
 * @param      ns       Dispatch duration in nanoseconds.
 *                      Note that 'ns=0' is not allowed!
 * @param      opt      Dispatch options (optional, can be NULL).
 *                      If used, must have been initialized with
 *                      em_dispatch_opt_init(). One initialization is enough,
 *                      later calls to em_dispatch_...(...opt) can reuse (the
 *                      possibly modified) 'opt'.
 *                      Using NULL is the same as passing 'opt' initialized
 *                      with em_dispatch_opt_init(&opt) without further changes.
 * @param[out] results  Dispatch results (optional, can be NULL).
 *                      Filled for successful dispatch scenarios, i.e. when the
 *                      return value is EM_OK.
 *
 * @return Error status code
 * @retval EM_OK when dispatch was successful, 'result' is filled (if provided)
 * @retval other than EM_OK on error, 'result' is untouched
 */
em_status_t em_dispatch_ns(uint64_t ns,
			   const em_dispatch_opt_t *opt,
			   em_dispatch_results_t *results /*out*/);

/**
 * @brief Run the EM dispatcher until a given number of events have been
 *        dispatched.
 *
 * Similar to em_dispatch_duration(), but with a simplified dispatch duration:
 * here only the number of events to dispatch is provided.
 *
 * Note that 'opt.burst_size' affects the number of events dispatched.
 * EM will request new events to dispatch while the number of dispatched
 * events is < .events and then handle the whole burst.
 *
 * The option 'opt.sched_pause=true' might also increase the number of
 * events dispatched since the EM dispatcher needs to fetch and handle
 * any leftover events held locally by the scheduler before returning.
 *
 * @see em_dispatch_duration() for documentation and usage.
 *
 * @param      events   Dispatch duration events. Dispatch until the given
 *                      number of events have been dispatched.
 *                      Note that 'events=0' is not allowed!
 * @param      opt      Dispatch options (optional, can be NULL).
 *                      If used, must have been initialized with
 *                      em_dispatch_opt_init(). One initialization is enough,
 *                      later calls to em_dispatch_...(...opt) can reuse (the
 *                      possibly modified) 'opt'.
 *                      Using NULL is the same as passing 'opt' initialized
 *                      with em_dispatch_opt_init(&opt) without further changes.
 * @param[out] results  Dispatch results (optional, can be NULL).
 *                      Filled for successful dispatch scenarios, i.e. when the
 *                      return value is EM_OK.
 *
 * @return Error status code
 * @retval EM_OK when dispatch was successful, 'result' is filled (if provided)
 * @retval other than EM_OK on error, 'result' is untouched
 */
em_status_t em_dispatch_events(uint64_t events,
			       const em_dispatch_opt_t *opt,
			       em_dispatch_results_t *results /*out*/);

/**
 * @brief Run the EM dispatcher for a given number of dispatch-rounds.
 *
 * Similar to em_dispatch_duration(), but with a simplified dispatch duration:
 * here only the number of rounds to dispatch is provided.
 *
 * @see em_dispatch_duration() for documentation and usage.
 *
 * @param      rounds   Dispatch duration rounds. Dispatch for the given number
 *                      of rounds.
 *                      Note that 'rounds=0' is not allowed!
 * @param      opt      Dispatch options (optional, can be NULL).
 *                      If used, must have been initialized with
 *                      em_dispatch_opt_init(). One initialization is enough,
 *                      later calls to em_dispatch_...(...opt) can reuse (the
 *                      possibly modified) 'opt'.
 *                      Using NULL is the same as passing 'opt' initialized
 *                      with em_dispatch_opt_init(&opt) without further changes.
 * @param[out] results  Dispatch results (optional, can be NULL).
 *                      Filled for successful dispatch scenarios, i.e. when the
 *                      return value is EM_OK.
 *
 * @return Error status code
 * @retval EM_OK when dispatch was successful, 'result' is filled (if provided)
 * @retval other than EM_OK on error, 'result' is untouched
 */
em_status_t em_dispatch_rounds(uint64_t rounds,
			       const em_dispatch_opt_t *opt,
			       em_dispatch_results_t *results /*out*/);

/**
 * EM event dispatch
 *
 * Called by an EM-core to dispatch events for EM processing.
 * The EM dispatcher internally queries the scheduler for events for the
 * calling EM-core and then dispatches them for processing, i.e. passes the
 * events to the application EO's receive-function based on the queue the events
 * were received / dequeued from.
 *
 * See the EM config file for options controlling the global behaviour of
 * em_dispatch().
 *
 * @param rounds  Dispatch rounds before returning,
 *                0 means 'never return from dispatch'
 *
 * @return The number of events dispatched on this core.
 *         Only makes sense if 'rounds > 0'
 *
 * @see em_dispatch_duration() for a function that enables dispatching
 *      with more options.
 */
uint64_t em_dispatch(uint64_t rounds);

/**
 * Dispatcher global EO-receive enter-callback.
 *
 * Common dispatch callback run before EO-receive functions of both the
 * em_receive_func_t and em_receive_multi_func_t types (i.e. for EOs created
 * with either em_eo_create() or em_eo_create_multircv()).
 *
 * Enter-callbacks are run just before entering EO-receive functions, they can
 * be useful for debugging, collecting statistics, manipulating events before
 * they reach the EO or implementing new services needing synchronization
 * between cores.
 * Arguments common for both types of EO receive functions are passed as
 * references to the enter-callback (the event-type passed to the single-event
 * receive function case is not passed, use em_event_get/set_type() instead).
 * Arguments are references, i.e. the callback can optionally modify them.
 * If modified, the new values will go to the next callback and eventually to
 * the multi-event EO-receive function.
 *
 * Events can be dropped by changing the event-entries in the events[num]-array
 * to EM_EVENT_UNDEF. Neither EO-receive nor any further enter-callbacks will
 * be called if all events have been dropped by the callbacks already run, i.e.
 * no callback will be called with 'num=0'.
 * The callback itself needs to handle the events it drops, e.g. free them.
 * Note: EM will remove entries of EM_EVENT_UNDEF from the events[]-array before
 *       calling the next enter-callback (if several registered) or the
 *       receive function and adjust 'num' accordingly for the call.
 *
 * The EO handle can be used to separate callback functionality per EO and the
 * core id can be obtained for core specific functionality.
 *
 * Callback functions can be called concurrently from different cores.
 *
 * @see em_dispatch_register_enter_cb()
 */
typedef void (*em_dispatch_enter_func_t)(em_eo_t eo, void **eo_ctx,
					 em_event_t events[/*in/out*/], int num,
					 em_queue_t *queue, void **q_ctx);

/**
 * Dispatcher global EO-receive exit-callback.
 *
 * The exit-callbacks are run after EO-receive returns.
 * Some arguments given to EO-receive might not be valid afterwards, thus
 * the only argument given to the exit callback is the EO handle.
 *
 * Callback functions can be called concurrently from different cores.
 *
 * @see em_dispatch_register_exit_cb()
 */
typedef void (*em_dispatch_exit_func_t)(em_eo_t eo);

/**
 * Register an EO-enter callback
 *
 * Register a global function to be called by the dispatcher just before calling
 * an EO-receive function. This can be useful for debugging, collecting
 * statistics, manipulating events before they reach the EO or implementing new
 * services needing synchronization between cores.
 *
 * The function registered should be kept short since it will be run each time
 * just before calling EO-receive. All registered callbacks will further
 * increase the processing time.
 *
 * Multiple callbacks can be registered.
 * The order of calling multiple registered functions is the order of
 * registration. If same function is registered twice it will be called twice.
 * The maximum number of simultaneous callbacks is system specific
 * (EM_CALLBACKS_MAX).
 *
 * @param func          Callback function
 *
 * @return EM_OK if callback registration succeeded
 *
 * @see em_dispatch_enter_func_t
 */
em_status_t
em_dispatch_register_enter_cb(em_dispatch_enter_func_t func);

/**
 * Unregister an EO-enter callback
 *
 * This can be used to unregister a previously registered enter-function.
 *
 * The given function is searched for and if found removed from the call list.
 * If the same function has been registered multiple times, only one reference
 * is removed per unregister call.
 * Note that when this function returns, no new calls are made to the removed
 * callback function, but it is still possible that another core could be
 * executing the function, so care must be taken before removing anything it may
 * still use.
 *
 * @param func          Callback function
 *
 * @return EM_OK if the given function was found and removed.
 */
em_status_t
em_dispatch_unregister_enter_cb(em_dispatch_enter_func_t func);

/**
 * Register an EO-exit callback
 *
 * Register a global function to be called by the dispatcher just after return
 * from an EO-receive function.
 *
 * The function registered should be kept short since it will be run each time
 * just after EO-receive returns. All registered callbacks will further increase
 * the processing time.
 *
 * Multiple callbacks can be registered.
 * The order of calling multiple registered functions is the order of
 * registration. If same function is registered twice it will be called twice.
 * The maximum number of simultaneous callbacks is system specific
 * (EM_CALLBACKS_MAX).
 *
 * @param func          Callback function
 *
 * @return EM_OK if callback registration succeeded
 *
 * @see em_dispatch_register_enter_cb(), em_dispatch_unregister_exit_cb()
 */
em_status_t
em_dispatch_register_exit_cb(em_dispatch_exit_func_t func);

/**
 * Unregister an EO-exit callback
 *
 * This can be used to unregister a previously registered exit-function.
 *
 * Given function pointer is searched and if found removed from the call list.
 * If one function is registered multiple times only one reference is removed.
 *
 * The given function is searched for and if found removed from the call list.
 * If the same function has been registered multiple times, only one reference
 * is removed per unregister call.
 * Note that when this function returns, no new calls are made to the removed
 * callback function, but it is still possible that another core could be
 * executing the function, so care must be taken before removing anything it may
 * still use.
 *
 * @param func          Callback function
 *
 * @return EM_OK if the given function was found and removed.
 *
 * @see em_dispatch_exit_func_t
 */
em_status_t
em_dispatch_unregister_exit_cb(em_dispatch_exit_func_t func);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_DISPATCHER_H_ */
