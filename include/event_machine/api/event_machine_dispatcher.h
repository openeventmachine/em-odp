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

#ifndef EVENT_MACHINE_DISPATCHER_H_
#define EVENT_MACHINE_DISPATCHER_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_dispatcher Dispatcher
 *  Event Machine dispatcher related services (new to API 1.2).
 * @{
 *
 * The implementation of scheduling and the dispatcher is system specific,
 * but this file provides a placeholder for common dispatcher specific services.
 * The dispatcher is commonly the main loop of each core (thread) that
 * interfaces with the scheduler and does the mapping of queue->EO.
 *
 * API1.2 introduces EO enter-exit callback function hooks. These can be used
 * to collect debugging information, statistics or implement new functionality.
 * The global enter-callback is called before EO-receive on each core
 * separately. The callback gets all the arguments the EO-receive would get and
 * can additionally modify them. The exit-callback works in a similar way, but
 * is instead called after the EO-receive returns and has no arguments except
 * for the EO handle.
 * Multiple callbacks can be registered. The calling order of multiple
 * registered functions is the order of registration. If the same function is
 * registered twice then it will be called twice. The amount of simultaneous
 * callbacks is system specific (EM_CALLBACKS_MAX).
 * If an enter-callback changes the event handle to UNDEF, the next callback
 * will still be called with event as UNDEF, but the EO-receive won't be called
 * with an UNDEF event.
 *
 * Do not include this from the application, event_machine.h will
 * do it for you.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * EM event dispatch.
 *
 * Called by each EM-core to dispatch events for EM processing.
 *
 * @param rounds  Dispatch rounds before returning,
 *                0 means 'never return from dispatch'
 *
 * @return The number of events dispatched on this core.
 *         Only makes sense if 'rounds > 0'
 */
uint64_t
em_dispatch(uint64_t rounds);

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
