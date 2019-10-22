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
 * EM internal dispatcher types & definitions
 *
 */

#ifndef EM_HOOK_TYPES_H_
#define EM_HOOK_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Max number of API-callback hook arrays */
#define API_HOOKS_MAX_TBL_SIZE 1000

/* EM API hook function types */
#define ALLOC_HOOK 1
#define FREE_HOOK  2
#define SEND_HOOK  3
/* Dispatcher callback function types */
#define DISPATCH_CALLBACK_ENTER 4
#define DISPATCH_CALLBACK_EXIT  5

typedef void (*void_hook_t)(void);

typedef union {
	em_api_hook_alloc_t alloc;
	em_api_hook_send_t send;
	em_api_hook_free_t free;
	em_dispatch_enter_func_t disp_enter;
	em_dispatch_exit_func_t disp_exit;
	void_hook_t void_hook;
} hook_fn_t;

/**
 * Table for storing API-callback hook function pointers.
 */
typedef struct {
	/* Hook function table */
	hook_fn_t tbl[EM_CALLBACKS_MAX];
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} hook_tbl_t;

COMPILE_TIME_ASSERT(sizeof(hook_tbl_t) % ENV_CACHE_LINE_SIZE == 0,
		    HOOK_ALIGNMENT_ERROR);

/**
 * API-callback hook functions (table of tables)
 */
typedef struct {
	/** Storage for multiple hook function tables */
	hook_tbl_t hook_tbl_storage[API_HOOKS_MAX_TBL_SIZE];
	/** Callback table edit lock */
	env_spinlock_t lock;
	/** Index of the current active callback table */
	int idx;
} hook_storage_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_HOOK_TYPES_H_ */
