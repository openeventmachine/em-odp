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

#include "em_include.h"

static hook_tbl_t **
get_hook_tbl(const uint8_t hook_type, hook_storage_t **hook_storage /*out*/);

/**
 * Pack a hook table after removing one item.
 *
 * Move pointers following the given index back towards the beginning of the
 * table so that there are no NULL pointers in the middle.
 */
static inline int
pack_hook_tbl(hook_tbl_t hook_tbl[], unsigned int idx);

em_status_t
hooks_init(const em_api_hooks_t *api_hooks)
{
	em_status_t stat = EM_OK;

	EM_PRINT("EM callbacks init\n");

	memset(&em_shm->dispatch_enter_cb_storage, 0, sizeof(hook_storage_t));
	memset(&em_shm->dispatch_exit_cb_storage, 0, sizeof(hook_storage_t));
	memset(&em_shm->alloc_hook_storage, 0, sizeof(hook_storage_t));
	memset(&em_shm->free_hook_storage, 0, sizeof(hook_storage_t));
	memset(&em_shm->send_hook_storage, 0, sizeof(hook_storage_t));

	em_shm->dispatch_enter_cb_tbl =
		&em_shm->dispatch_enter_cb_storage.hook_tbl_storage[0];
	em_shm->dispatch_exit_cb_tbl =
		&em_shm->dispatch_exit_cb_storage.hook_tbl_storage[0];
	em_shm->alloc_hook_tbl =
		&em_shm->alloc_hook_storage.hook_tbl_storage[0];
	em_shm->free_hook_tbl =
		&em_shm->free_hook_storage.hook_tbl_storage[0];
	em_shm->send_hook_tbl =
		&em_shm->send_hook_storage.hook_tbl_storage[0];

	em_shm->dispatch_enter_cb_storage.idx = 0;
	em_shm->dispatch_exit_cb_storage.idx = 0;
	em_shm->alloc_hook_storage.idx = 0;
	em_shm->free_hook_storage.idx = 0;
	em_shm->send_hook_storage.idx = 0;

	env_spinlock_init(&em_shm->dispatch_enter_cb_storage.lock);
	env_spinlock_init(&em_shm->dispatch_exit_cb_storage.lock);
	env_spinlock_init(&em_shm->alloc_hook_storage.lock);
	env_spinlock_init(&em_shm->free_hook_storage.lock);
	env_spinlock_init(&em_shm->send_hook_storage.lock);

	if (EM_API_HOOKS_ENABLE) {
		if (api_hooks->alloc_hook) {
			stat = em_hooks_register_alloc(api_hooks->alloc_hook);
			if (unlikely(stat != EM_OK))
				return stat;
		}
		if (api_hooks->free_hook) {
			stat = em_hooks_register_free(api_hooks->free_hook);
			if (unlikely(stat != EM_OK))
				return stat;
		}
		if (api_hooks->send_hook) {
			stat = em_hooks_register_send(api_hooks->send_hook);
			if (unlikely(stat != EM_OK))
				return stat;
		}
	}

	return EM_OK;
}

em_status_t
hook_register(uint8_t hook_type, hook_fn_t hook_fn)
{
	int i, idx, next_idx;
	hook_storage_t *hook_storage;
	hook_tbl_t *hook_tbl, *next_tbl;
	hook_tbl_t **active_tbl_ptr;

	/* Get the em_shm hook table and hook storage to update */
	active_tbl_ptr = get_hook_tbl(hook_type, &hook_storage/*out*/);
	if (unlikely(active_tbl_ptr == NULL))
		return EM_ERR_BAD_ID;

	env_spinlock_lock(&hook_storage->lock);

	/*TODO: Check that no thread is still using the new memory area. */

	idx = hook_storage->idx;
	next_idx = idx + 1;
	if (next_idx >= API_HOOKS_MAX_TBL_SIZE)
		next_idx = 0;
	hook_tbl = &hook_storage->hook_tbl_storage[idx];
	next_tbl = &hook_storage->hook_tbl_storage[next_idx];

	/*
	 * Copy old callback functions and find the index
	 * of the new function pointer.
	 */
	memset(next_tbl, 0, sizeof(hook_tbl_t));
	memcpy(next_tbl, hook_tbl, sizeof(hook_tbl_t));

	for (i = 0; i < EM_CALLBACKS_MAX; i++) {
		if (next_tbl->tbl[i].void_hook == NULL)
			break;
	}
	if (unlikely(i == EM_CALLBACKS_MAX)) {
		env_spinlock_unlock(&hook_storage->lock);
		return EM_ERR_ALLOC_FAILED;
	}
	/* Add new callback */
	next_tbl->tbl[i] = hook_fn;

	/* move the active hook tbl to the new tbl */
	*active_tbl_ptr = next_tbl; /* em_shm->..._hook_tbl = next_tbl */

	hook_storage->idx = next_idx;

	env_spinlock_unlock(&hook_storage->lock);

	return EM_OK;
}

em_status_t
hook_unregister(uint8_t hook_type, hook_fn_t hook_fn)
{
	int i, idx, next_idx, ret;

	hook_storage_t *hook_storage;
	hook_tbl_t *hook_tbl, *next_tbl;
	hook_tbl_t **active_tbl_ptr;

	active_tbl_ptr = get_hook_tbl(hook_type, &hook_storage/*out*/);
	if (unlikely(active_tbl_ptr == NULL))
		return EM_ERR_BAD_ID;

	env_spinlock_lock(&hook_storage->lock);

	/*TODO: Check that no thread is still using the new memory area. */

	idx = hook_storage->idx;
	next_idx = idx + 1;
	if (next_idx >= API_HOOKS_MAX_TBL_SIZE)
		next_idx = 0;
	hook_tbl = &hook_storage->hook_tbl_storage[idx];
	next_tbl = &hook_storage->hook_tbl_storage[next_idx];

	/*
	 * Copy old callback functions and try to find matching
	 * function pointer.
	 */
	memset(next_tbl, 0, sizeof(hook_tbl_t));
	memcpy(next_tbl, hook_tbl, sizeof(hook_tbl_t));

	for (i = 0; i < EM_CALLBACKS_MAX; i++)
		if (next_tbl->tbl[i].void_hook == hook_fn.void_hook)
			break;
	if (unlikely(i == EM_CALLBACKS_MAX)) {
		env_spinlock_unlock(&hook_storage->lock);
		return EM_ERR_NOT_FOUND;
	}

	/*
	 * Remove a pointer and move the following array entries backwards
	 * and set callback array pointer to the beginning of the new array.
	 */
	next_tbl->tbl[i].void_hook = NULL;
	ret = pack_hook_tbl(next_tbl, i);
	if (unlikely(ret != 0)) {
		env_spinlock_unlock(&hook_storage->lock);
		return EM_ERR_BAD_POINTER;
	}

	/* move the active hook tbl to the new tbl */
	*active_tbl_ptr = next_tbl; /* em_shm->..._hook_tbl = next_tbl */

	hook_storage->idx = next_idx;

	env_spinlock_unlock(&hook_storage->lock);

	return EM_OK;
}

static hook_tbl_t **
get_hook_tbl(const uint8_t hook_type, hook_storage_t **hook_storage /*out*/)
{
	hook_tbl_t **active_tbl_ptr;

	switch (hook_type) {
	case ALLOC_HOOK:
		*hook_storage = &em_shm->alloc_hook_storage;
		active_tbl_ptr = &em_shm->alloc_hook_tbl;
		break;
	case FREE_HOOK:
		*hook_storage = &em_shm->free_hook_storage;
		active_tbl_ptr = &em_shm->free_hook_tbl;
		break;
	case SEND_HOOK:
		*hook_storage = &em_shm->send_hook_storage;
		active_tbl_ptr = &em_shm->send_hook_tbl;
		break;
	case DISPATCH_CALLBACK_ENTER:
		*hook_storage = &em_shm->dispatch_enter_cb_storage;
		active_tbl_ptr = &em_shm->dispatch_enter_cb_tbl;
		break;
	case DISPATCH_CALLBACK_EXIT:
		*hook_storage = &em_shm->dispatch_exit_cb_storage;
		active_tbl_ptr = &em_shm->dispatch_exit_cb_tbl;
		break;
	default:
		return NULL;
	}

	return active_tbl_ptr;
}

static inline int
pack_hook_tbl(hook_tbl_t *const hook_tbl, unsigned int idx)
{
	hook_fn_t *const fn_tbl = hook_tbl->tbl;

	if (unlikely(idx >= EM_CALLBACKS_MAX ||
		     fn_tbl[idx].void_hook != NULL))
		return -1;

	for (; idx < EM_CALLBACKS_MAX - 1; idx++) {
		if (fn_tbl[idx + 1].void_hook != NULL) {
			fn_tbl[idx].void_hook = fn_tbl[idx + 1].void_hook;
			fn_tbl[idx + 1].void_hook = NULL;
		} else {
			break;
		}
	}

	return 0;
}
