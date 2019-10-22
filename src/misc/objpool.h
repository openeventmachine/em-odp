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

#ifndef OBJPOOL_H_
#define OBJPOOL_H_

/**
 * @file
 * object-pool types & definitions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/platform/env/environment.h>
#include <list.h>

#define OBJSUBPOOLS_MAX 32

typedef struct {
	list_node_t list_node;
	int subpool_idx;
	int in_pool;
} objpool_elem_t;

typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE];

	struct {
		env_spinlock_t lock;
		list_node_t list_head;
	};

} objsubpool_t ENV_CACHE_LINE_ALIGNED;

typedef struct {
	int nbr_subpools ENV_CACHE_LINE_ALIGNED;
	objsubpool_t subpool[OBJSUBPOOLS_MAX] ENV_CACHE_LINE_ALIGNED;
} objpool_t;

int
objpool_init(objpool_t *const objpool, int nbr_subpools);

void
objpool_add(objpool_t *const objpool, int subpool_idx,
	    objpool_elem_t *const elem);

objpool_elem_t *
objpool_rem(objpool_t *const objpool, int subpool_idx);

int
objpool_rem_elem(objpool_t *const objpool, objpool_elem_t *const elem);

static inline int
objpool_in_pool(objpool_elem_t *elem)
{
	return elem->in_pool;
}

#ifdef __cplusplus
}
#endif

#endif /* OBJPOOL_H_ */
