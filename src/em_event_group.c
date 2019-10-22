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

#include "em_include.h"

static inline event_group_elem_t *
egrp_poolelem2egrp(objpool_elem_t *const event_group_pool_elem)
{
	return (event_group_elem_t *)((uintptr_t)event_group_pool_elem -
			offsetof(event_group_elem_t, event_group_pool_elem));
}

em_status_t
event_group_init(event_group_tbl_t *const event_group_tbl,
		 event_group_pool_t *const event_group_pool)
{
	int i, ret;
	event_group_elem_t *egrp_elem;
	const int cores = em_core_count();

	memset(event_group_tbl, 0, sizeof(event_group_tbl_t));
	memset(event_group_pool, 0, sizeof(event_group_pool_t));
	env_atomic32_init(&em_shm->event_group_count);

	for (i = 0; i < EM_MAX_EVENT_GROUPS; i++) {
		em_event_group_t egrp = egrp_idx2hdl(i);
		event_group_elem_t *const egrp_elem =
			event_group_elem_get(egrp);

		egrp_elem->event_group = egrp; /* store handle */
		egrp_elem->all = 0;
		env_atomic64_set(&egrp_elem->post.atomic, 0);
		env_atomic64_set(&egrp_elem->pre.atomic, 0);
	}

	ret = objpool_init(&event_group_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (i = 0; i < EM_MAX_EVENT_GROUPS; i++) {
		egrp_elem = &event_group_tbl->egrp_elem[i];
		objpool_add(&event_group_pool->objpool, i % cores,
			    &egrp_elem->event_group_pool_elem);
	}

	return EM_OK;
}

em_event_group_t
event_group_alloc(void)
{
	event_group_elem_t *egrp_elem;
	objpool_elem_t *egrp_pool_elem;

	egrp_pool_elem = objpool_rem(&em_shm->event_group_pool.objpool,
				     em_core_id());
	if (unlikely(egrp_pool_elem == NULL))
		return EM_EVENT_GROUP_UNDEF;

	egrp_elem = egrp_poolelem2egrp(egrp_pool_elem);

	env_atomic32_inc(&em_shm->event_group_count);
	return egrp_elem->event_group;
}

em_status_t
event_group_free(em_event_group_t event_group)
{
	event_group_elem_t *egrp_elem = event_group_elem_get(event_group);

	if (unlikely(egrp_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(&em_shm->event_group_pool.objpool,
		    egrp_elem->event_group_pool_elem.subpool_idx,
		    &egrp_elem->event_group_pool_elem);

	env_atomic32_dec(&em_shm->event_group_count);
	return EM_OK;
}

unsigned int
event_group_count(void)
{
	return env_atomic32_get(&em_shm->event_group_count);
}
