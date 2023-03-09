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

#include <event_machine/platform/env/environment.h>
#include <objpool.h>
#include <list.h>

static inline objpool_elem_t *
objpool_node2elem(list_node_t *const list_node);

int
objpool_init(objpool_t *const objpool, int nbr_subpools)
{
	if (nbr_subpools > OBJSUBPOOLS_MAX)
		nbr_subpools = OBJSUBPOOLS_MAX;

	objpool->nbr_subpools = nbr_subpools;

	for (int i = 0; i < nbr_subpools; i++) {
		objsubpool_t *const subpool = &objpool->subpool[i];

		env_spinlock_init(&subpool->lock);
		list_init(&subpool->list_head);
	}

	return 0;
}

void
objpool_add(objpool_t *const objpool, int subpool_idx,
	    objpool_elem_t *const elem)
{
	const int idx = subpool_idx % objpool->nbr_subpools;
	objsubpool_t *const subpool = &objpool->subpool[idx];

	elem->subpool_idx = idx;

	env_spinlock_lock(&subpool->lock);
	list_add(&subpool->list_head, &elem->list_node);
	elem->in_pool = 1;
	env_spinlock_unlock(&subpool->lock);
}

objpool_elem_t *
objpool_rem(objpool_t *const objpool, int subpool_idx)
{
	objpool_elem_t *elem = NULL;

	for (int i = 0; i < objpool->nbr_subpools; i++) {
		const int idx = (subpool_idx + i) % objpool->nbr_subpools;
		objsubpool_t *const subpool = &objpool->subpool[idx];

		env_spinlock_lock(&subpool->lock);

		list_node_t *const node = list_rem_first(&subpool->list_head);

		if (node != NULL) {
			elem = objpool_node2elem(node);
			elem->in_pool = 0;
		}

		env_spinlock_unlock(&subpool->lock);

		if (node != NULL)
			return elem;
	}

	return NULL;
}

int
objpool_rem_elem(objpool_t *const objpool, objpool_elem_t *const elem)
{
	const int idx = elem->subpool_idx;
	objsubpool_t *const subpool = &objpool->subpool[idx];
	int ret = -1;

	env_spinlock_lock(&subpool->lock);
	if (elem->in_pool) {
		list_rem(&subpool->list_head, &elem->list_node);
		elem->in_pool = 0;
		ret = 0;
	}
	env_spinlock_unlock(&subpool->lock);

	return ret;
}

static inline objpool_elem_t *
objpool_node2elem(list_node_t *const list_node)
{
	return (objpool_elem_t *)((uintptr_t)list_node -
				  offsetof(objpool_elem_t, list_node));
}
