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

#ifndef EM_EO_TYPES_H_
#define EM_EO_TYPES_H_

/**
 * @file
 * EM internal EO types & definitions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM EO element
 */
typedef struct {
	/** EO name */
	char name[EM_EO_NAME_LEN] ENV_CACHE_LINE_ALIGNED;
	/** EO state */
	em_eo_state_t state;
	/** EO start function */
	em_start_func_t start_func;
	/** EO core-local start function */
	em_start_local_func_t start_local_func;
	/** EO stop function */
	em_stop_func_t stop_func;
	/** EO core-local stop function */
	em_stop_local_func_t stop_local_func;

	int use_multi_rcv; /* true:receive_multi_func(), false:receive_func() */
	int max_events;
	/** EO event receive function */
	em_receive_func_t receive_func;
	/** EO multi-event receive function */
	em_receive_multi_func_t receive_multi_func;

	/** EO specific error handler function */
	em_error_handler_t error_handler_func;
	/** EO context data pointer */
	void *eo_ctx;
	/** EO elem lock */
	env_spinlock_t lock;
	/** EO queue list */
	list_node_t queue_list;
	/** Number of queues */
	env_atomic32_t num_queues;
	/** Buffered events sent during the EO start-function */
	odp_stash_t stash;
	/** EO handle */
	em_eo_t eo;
	/** EO pool elem for linking free EOs for EO-alloc */
	objpool_elem_t eo_pool_elem;
} eo_elem_t ENV_CACHE_LINE_ALIGNED;

/**
 * EO EO element table
 */
typedef struct {
	/** EO element table */
	eo_elem_t eo_elem[EM_MAX_EOS];
} eo_tbl_t;

typedef struct {
	objpool_t objpool;
} eo_pool_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_EO_TYPES_H_ */
