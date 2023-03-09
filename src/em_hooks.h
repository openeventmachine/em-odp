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

#ifndef EM_HOOKS_H_
#define EM_HOOKS_H_

/**
 * @file
 * EM internal API callback hook functions
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM API-callback and idle hooks init function (called once at startup).
 */
em_status_t
hooks_init(const em_api_hooks_t *api_hooks, const em_idle_hooks_t *idle_hooks);

/**
 * Helper function for registering callback hook functions.
 *
 * @return EM_OK if there was room left to register a new callback
 */
em_status_t
hook_register(uint8_t type, hook_fn_t hook_fn);

/**
 * Helper function for unregistering dispatcher callback functions.
 *
 * @return EM_OK if there was room left to register a new callback
 */
em_status_t
hook_unregister(uint8_t type, hook_fn_t hook_fn);

static inline void
call_api_hooks_alloc(const em_event_t events[], const int num_act,
		     const int num_req, uint32_t size, em_event_type_t type,
		     em_pool_t pool)
{
	const hook_tbl_t *alloc_hook_tbl = em_shm->alloc_hook_tbl;
	em_api_hook_alloc_t alloc_hook_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		alloc_hook_fn = alloc_hook_tbl->tbl[i].alloc;
		if (alloc_hook_fn == NULL)
			return;
		alloc_hook_fn(events, num_act, num_req, size, type, pool);
	}
}

static inline void
call_api_hooks_free(const em_event_t events[], const int num)
{
	const hook_tbl_t *free_hook_tbl = em_shm->free_hook_tbl;
	em_api_hook_free_t free_hook_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		free_hook_fn = free_hook_tbl->tbl[i].free;
		if (free_hook_fn == NULL)
			return;
		free_hook_fn(events, num);
	}
}

static inline void
call_api_hooks_send(const em_event_t events[], const int num,
		    em_queue_t queue, em_event_group_t event_group)
{
	const hook_tbl_t *send_hook_tbl = em_shm->send_hook_tbl;
	em_api_hook_send_t send_hook_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		send_hook_fn = send_hook_tbl->tbl[i].send;
		if (send_hook_fn == NULL)
			return;
		send_hook_fn(events, num, queue, event_group);
	}
}

static inline void call_idle_hooks_to_idle(uint64_t to_idle_delay_ns)
{
	const hook_tbl_t *to_idle_hook_tbl = em_shm->to_idle_hook_tbl;
	em_idle_hook_to_idle_t to_idle_hook_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		to_idle_hook_fn = to_idle_hook_tbl->tbl[i].to_idle;
		if (to_idle_hook_fn == NULL)
			return;
		to_idle_hook_fn(to_idle_delay_ns);
	}
}

static inline void call_idle_hooks_to_active(void)
{
	const hook_tbl_t *to_active_hook_tbl = em_shm->to_active_hook_tbl;
	em_idle_hook_to_active_t to_active_hook_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		to_active_hook_fn = to_active_hook_tbl->tbl[i].to_active;
		if (to_active_hook_fn == NULL)
			return;
		to_active_hook_fn();
	}
}

static inline void call_idle_hooks_while_idle(void)
{
	const hook_tbl_t *while_idle_hook_tbl = em_shm->while_idle_hook_tbl;
	em_idle_hook_while_idle_t while_idle_hook_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		while_idle_hook_fn = while_idle_hook_tbl->tbl[i].while_idle;
		if (while_idle_hook_fn == NULL)
			return;
		while_idle_hook_fn();
	}
}

#ifdef __cplusplus
}
#endif

#endif /* EM_HOOKS_H_ */
