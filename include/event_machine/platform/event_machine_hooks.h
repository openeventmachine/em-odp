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
 * @defgroup em_hooks API-hooks
 *  Event Machine API-callback hooks.
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
 *
 * Do not include this file from the application, event_machine.h will
 * do it for you.
 */

#ifdef __cplusplus
extern "C" {
#endif

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
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_HOOKS_H_ */
