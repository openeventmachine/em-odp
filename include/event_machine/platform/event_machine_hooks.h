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

#ifndef EVENT_MACHINE_HOOKS_H_
#define EVENT_MACHINE_HOOKS_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_hooks API-hooks and Idle hooks
 *  Event Machine API-callback hooks and Idle hooks.
 * @{
 *
 * EM API-callback hook functions can be registered for a selected set of
 * EM APIs. The EM APIs in question are mostly fast path APIs, like em_send(),
 * em_alloc() and em_free(). Control APIs generally do not need hook support.
 * A registered user provided hook function will be called by EM each time the
 * corresponding API is called.
 * API-callback hooks enables the user to gather statistics, trace program and
 * event flow etc. API hooks should not change the state of the events etc.
 * they receive as arguments, nor should they call the same API from within the
 * hook to avoid hook recursion.
 * Hook support is only available when EM_API_HOOKS_ENABLE != 0.
 * Multiple API-callback hook functions (up to the number 'EM_CALLBACKS_MAX')
 * can be registered for a given EM API. The calling order of multiple
 * registered API hook functions is the order of registration. If the same
 * function is registered twice then it will be called twice.
 *
 * EM Idle callback hook functions can be registered for tracking the idle state
 * (ACTIVE/IDLE) of EM cores. Idle hooks can be used e.g. to gather application
 * load statistics. The idle hooks are called by the EM dispatcher depending on
 * whether the core gets events from scheduled or local queues. A core is in the
 * ACTIVE state when it gets events from these queues. A core is in the IDLE
 * state when it didn't get any events from these queues.
 * To_idle hooks are called when a core state changes from ACTIVE to IDLE.
 * To_active hooks are called when a core state changes from IDLE to ACTIVE.
 * While_idle hooks are called when a core is already in the IDLE state and it
 * doesn't get any events from scheduled or local queues. While_idle hooks can
 * be called several times when a core is in the IDLE state.
 * The user should not make any assumptions of the current idle state of the
 * core when registering new idle hooks.
 * The idle hook support is only available when EM_IDLE_HOOKS_ENABLE != 0.
 * Multiple idle hook functions (up to the number 'EM_CALLBACKS_MAX') can be
 * registered for each idle hook type. The calling order of multiple registered
 * idle hook functions is the order of registration. If the same function is
 * registered twice then it will be called twice.
 *
 * Do not include this file from the application, event_machine.h will
 * do it for you.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * API-callback hook for em_alloc(), em_alloc_multi() and em_event_clone()
 *
 * The hook will only be called for successful event allocations, passing also
 * the newly allocated 'events' to the hook.
 * The state and ownership of the events must not be changed by the hook, e.g.
 * the events must not be freed or sent etc. Calling em_alloc/_multi() within
 * the alloc hook leads to hook recursion and must be avoided.
 *
 * @note em_alloc(): hook is called with events[1] and num_act = num_req = 1.
 * @note em_alloc_multi(): hook is called with events[num_act] and
 *                         num_req >= num_act >= 1
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * @param[in] events[]  Array of newly allocated events: 'events[num_act]'.
 *                      Don't change the state of the array or the events!
 * @param num_act       The actual number of events allocated and written into
 *                      'events[]' (num_act <= num_req). This is the return val
 *                      of em_alloc_multi() if at least one event was allocated
 *                      (the hook is not called if no events were allocated).
 * @param num_req       The requested number of events to allocate,
 *                      from em_alloc/_multi('num')
 * @param size          Event size >0, from em_alloc/_multi('size')
 * @param type          Event type to allocate, from em_alloc/_multi('type')
 * @param pool          Event pool handle, from em_alloc/_multi('pool')
 *
 * @see em_alloc(), em_alloc_multi() and em_hooks_register_alloc()
 */
typedef void (*em_api_hook_alloc_t)(const em_event_t events[/*num_act*/],
				    int num_act, int num_req, uint32_t size,
				    em_event_type_t type, em_pool_t pool);

/**
 * API-callback hook for em_free() and em_free_multi().
 *
 * The hook will be called before freeing the actual events, after verifying
 * that the events given are valid, thus the hook does not 'see' if the actual
 * free-operation succeeds or fails.
 * The state and ownership of the events must not be changed by the hook, e.g.
 * the events must not be freed or sent etc. Calling em_free/_multi() within the
 * free hook leads to hook recursion and must be avoided.
 *
 * @note em_free(): hook is called with events[1] and num = 1.
 * @note em_free_multi(): hook is called with events[num] and num >= 1
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * @param[in] events[]  Array of events to be freed: 'events[num]'
 *                      Don't change the state of the array or the events!
 * @param num           The number of events in the array 'events[]'.
 *
 * @see em_free(), em_free_multi() and em_hooks_register_free()
 */
typedef void (*em_api_hook_free_t)(const em_event_t events[], int num);

/**
 * API-callback hook for em_send(), em_send_multi(), em_send_group() and
 * em_send_group_multi().
 *
 * Sending multiple events with an event group is the most generic
 * variant and thus one callback covers all.
 * The hook will be called just before sending the actual event(s), thus
 * the hook does not 'see' if the actual send operation succeeds or
 * fails.
 * The state and ownership of the events must not be changed by the
 * hook, e.g. the events can not be freed or sent etc.
 * Calling em_send...() within the send hook leads to hook recursion and
 * must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * @see
 */
typedef void (*em_api_hook_send_t)(const em_event_t events[], int num,
				   em_queue_t queue,
				   em_event_group_t event_group);

/**
 * To idle hook
 *
 * The to_idle hook will be called by the EM dispatcher when a core is entering
 * the IDLE state i.e. when the core doesn't get any new events to be processed.
 * The to_idle hook is called only when there previously has been events to
 * process and the state changes from active to idle.
 *
 * @param to_idle_delay_ns	The delay in nanoseconds that a core was waiting
 *				for scheduled events before calling to_idle hook
 */
typedef void (*em_idle_hook_to_idle_t)(uint64_t to_idle_delay_ns);

/**
 * To active hook
 *
 * The to_active hook will be called by the EM dispatcher when a core is
 * entering the ACTIVE state i.e. when the core gets events after being idle.
 * The to_active hook is called only when the core previously has been in the
 * IDLE state and the state changes to active. To_active hooks are called before
 * the EO processes the events.
 */
typedef void (*em_idle_hook_to_active_t)(void);

/**
 * While idle hook
 *
 * The while_idle hook will be called by the EM dispatcher when a core is
 * already in the IDLE state and stays in it i.e. the core doesn't get any
 * events. The while_idle hook can be called several times until the core state
 * changes to active i.e. the core again gets events for processing.
 */
typedef void (*em_idle_hook_while_idle_t)(void);

/**
 * API-callback hooks provided by the user at start-up (init)
 *
 * EM API functions will call an API hook if given by the user through this
 * struct to em_init(). E.g. em_alloc() will call api_hooks->alloc(...) if
 * api_hooks->alloc != NULL. Not all hooks need to be provided, use NULL for
 * unsused hooks.
 *
 * @note Not all EM API funcs have associated hooks, only the most used
 *       functions (in the fast path) are included.
 *       Notice that extensive usage or heavy processing in the hooks might
 *       significantly impact performance since each API call (that has a hook)
 *       will execute the extra code in the user provided hook.
 *
 * @note Only used if EM_API_HOOKS_ENABLE != 0
 */
typedef struct {
	/**
	 * API callback hook for _all_ alloc-variants:
	 * em_alloc() and em_alloc_multi()
	 * Initialize to NULL if unused.
	 */
	em_api_hook_alloc_t alloc_hook;

	/**
	 * API callback hook for all free-variants:
	 * em_free() and em_free_multi()
	 * Initialize to NULL if unused.
	 */
	em_api_hook_free_t free_hook;

	/**
	 * API callback hook used for _all_ send-variants:
	 * em_send(), em_send_multi(), em_send_group() and em_send_group_multi()
	 * Initialize to NULL if unused.
	 */
	em_api_hook_send_t send_hook;
} em_api_hooks_t;

/**
 * Idle hooks given by the user via this struct to the em_init() will be called
 * by the EM dispatcher on each core.
 *
 * The EM dispatcher will call:
 * -	to_idle_hook when a core doesn't get any more events from scheduled or
 *	local queues after the core has been active
 * -	to_active_hook when a core gets events after being idle
 * -	while_idle_hook when a core continues being idle
 *
 * Not all the idle hooks need to be provided, use NULL for unsused idle hooks.
 *
 * @note Notice that doing heavy processing in the hooks might significantly
 *	 impact performance.
 *
 * @note Only used if EM_IDLE_HOOKS_ENABLE != 0
 */
typedef struct {
	/**
	 * Idle hook called when entering the idle state
	 * Initialize to NULL if unused.
	 */
	em_idle_hook_to_idle_t to_idle_hook;
	/**
	 * Idle hook called when entering the active state
	 * Initialize to NULL if unused.
	 */
	em_idle_hook_to_active_t to_active_hook;
	/**
	 * Idle hook called while remaining in the idle state
	 * Initialize to NULL if unused.
	 */
	em_idle_hook_while_idle_t while_idle_hook;
} em_idle_hooks_t;

/**
 * Register an API-callback hook for em_alloc().
 *
 * A registered hook will be called at the end of em_alloc(), but only for
 * successful allocs, passing also the newly allocated 'event' to the hook.
 * The state and ownership of the event must not be changed by the hook, e.g.
 * the event must not be freed or sent etc. Calling em_alloc() within the
 * alloc hook leads to hook recursion and must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * Multiple API-callback hook functions (up to the number 'EM_CALLBACKS_MAX')
 * can be registered.
 * The order of calling multiple registered hook functions is the order of
 * registration. If same function is registered twice it will be called twice.
 *
 * @param func   API-callback hook function
 * @return EM_OK if callback hook registration succeeded
 */
em_status_t
em_hooks_register_alloc(em_api_hook_alloc_t func);

/**
 * Unregister a previously registered em_alloc() callback hook
 *
 * @param func   API-callback hook function
 * @return EM_OK if callback hook unregistration succeeded
 */
em_status_t
em_hooks_unregister_alloc(em_api_hook_alloc_t func);

/**
 * Register an API-callback hook for em_free().
 *
 * The hook will be called before freeing the actual event, after verifying that
 * the event given to em_free() is valid, thus the hook does not 'see' if the
 * actual free-operation succeeds or fails.
 * The state and ownership of the event must not be changed by the hook, e.g.
 * the event must not be freed or sent etc. Calling em_free() within the
 * free hook leads to hook recursion and must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * Multiple API-callback hook functions (up to the number 'EM_CALLBACKS_MAX')
 * can be registered.
 * The order of calling multiple registered hook functions is the order of
 * registration. If same function is registered twice it will be called twice.
 *
 * @param func   API-callback hook function
 * @return EM_OK if callback hook registration succeeded
 */
em_status_t
em_hooks_register_free(em_api_hook_free_t func);

/**
 * Unregister an em_free() callback hook
 *
 * @param func   API-callback hook function
 * @return EM_OK if callback hook unregistration succeeded
 */
em_status_t
em_hooks_unregister_free(em_api_hook_free_t func);

/**
 * Register an API-callback hook for em_send(), em_send_multi(), em_send_group()
 * and em_send_group_multi().
 *
 * Sending multiple events with an event group is the most generic
 * variant and thus one callback covers all.
 * The hook will be called just before sending the actual event(s), thus
 * the hook does not 'see' if the actual send operation succeeds or
 * fails.
 * The state and ownership of the events must not be changed by the
 * hook, e.g. the events can not be freed or sent etc.
 * Calling em_send...() within the send hook leads to hook recursion and
 * must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * Multiple API-callback hook functions (up to the number 'EM_CALLBACKS_MAX')
 * can be registered.
 * The order of calling multiple registered hook functions is the order of
 * registration. If same function is registered twice it will be called twice.
 *
 * @param func   API-callback hook function
 * @return EM_OK if callback hook registration succeeded
 */
em_status_t
em_hooks_register_send(em_api_hook_send_t func);

/**
 * Unregister an em_send_...() callback hook
 *
 * @param func   API-callback hook function
 * @return EM_OK if callback hook unregistration succeeded
 */
em_status_t
em_hooks_unregister_send(em_api_hook_send_t func);

/**
 * Register an idle hook that will be called when entering the idle state.
 *
 * To_idle hooks will be called by the EM dispatcher when a core enters the idle
 * state, i.e. when no further events are available from scheduled or local
 * queues for processing. The to_idle hooks will be called only if the core
 * previously was in the active state.
 *
 * Multiple to_idle hook functions (up to the number 'EM_CALLBACKS_MAX') can be
 * registered. The order of calling multiple registered hook functions is the
 * order of registration. If the same function is registered twice it will be
 * called twice.
 *
 * @param func	Idle hook function
 * @return EM_OK if idle hook registration succeeded
 */
em_status_t
em_hooks_register_to_idle(em_idle_hook_to_idle_t func);

/**
 * Unregister a to_idle hook.
 *
 * @param func   Idle hook function
 * @return EM_OK if idle hook unregistration succeeded
 */
em_status_t
em_hooks_unregister_to_idle(em_idle_hook_to_idle_t func);

/**
 * Register an idle hook that will be called when a core is entering the active
 * state.
 *
 * To_active hooks will be called by the EM dispatcher when a core enters the
 * active state, i.e. it received events from scheduled or local queues after
 * being in the idle state. The to_active hooks will be called before the actual
 * event processing is started and only if the core previously was in the idle
 * state.
 *
 * Multiple to_active hook functions (up to the number 'EM_CALLBACKS_MAX') can
 * be registered. The order of calling multiple registered hook functions is the
 * order of registration. If the same function is registered twice it will be
 * called twice.
 *
 * @param func	Idle hook function
 * @return EM_OK if idle hook registration succeeded
 */
em_status_t
em_hooks_register_to_active(em_idle_hook_to_active_t func);

/**
 * Unregister a to_active hook
 *
 * @param func   Idle hook function
 * @return EM_OK if idle hook unregistration succeeded
 */
em_status_t
em_hooks_unregister_to_active(em_idle_hook_to_active_t func);

/**
 * Register an idle hook that will be called while staying in the idle state.
 *
 * While_idle hooks will be called by the EM dispatcher while a core remains in
 * the idle state, i.e. the core didn't get any events from scheduled or local
 * queues for processing while already being in the idle state.
 *
 * Multiple while_idle hook functions (up to the number 'EM_CALLBACKS_MAX') can
 * be registered. The order of calling multiple registered hook functions is the
 * order of registration. If the same function is registered twice it will be
 * called twice.
 *
 * @param func	Idle hook function
 * @return EM_OK if idle hook registration succeeded
 */
em_status_t
em_hooks_register_while_idle(em_idle_hook_while_idle_t func);

/**
 * Unregister a while_idle hook
 *
 * @param func   Idle hook function
 * @return EM_OK if idle hook unregistration succeeded
 */
em_status_t
em_hooks_unregister_while_idle(em_idle_hook_while_idle_t func);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_HOOKS_H_ */
