/*
 *   Copyright (c) 2014, Nokia Solutions and Networks
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
 * EM internal atomic group functions
 *
 */

#ifndef EM_ATOMIC_GROUP_H_
#define EM_ATOMIC_GROUP_H_

#ifdef __cplusplus
extern "C" {
#endif

#define invalid_atomic_group(atomic_group) \
	((unsigned int)agrp_hdl2idx((atomic_group)) >= EM_MAX_ATOMIC_GROUPS)

em_status_t
atomic_group_init(atomic_group_tbl_t *const atomic_group_tbl,
		  atomic_group_pool_t *const atomic_group_pool);

em_atomic_group_t
atomic_group_alloc(void);

em_status_t
atomic_group_free(em_atomic_group_t atomic_group);

void atomic_group_remove_queue(queue_elem_t *const q_elem);

void atomic_group_dispatch(odp_event_t odp_evtbl[], const int num_events,
			   const queue_elem_t *q_elem);

static inline int
atomic_group_allocated(const atomic_group_elem_t *agrp_elem)
{
	return !objpool_in_pool(&agrp_elem->atomic_group_pool_elem);
}

static inline int
agrp_hdl2idx(const em_atomic_group_t atomic_group)
{
	return (int)((uintptr_t)atomic_group - 1);
}

static inline em_atomic_group_t
agrp_idx2hdl(const int atomic_group_index)
{
	return (em_atomic_group_t)(uintptr_t)(atomic_group_index + 1);
}

static inline atomic_group_elem_t *
atomic_group_elem_get(const em_atomic_group_t atomic_group)
{
	const int ag_idx = agrp_hdl2idx(atomic_group);
	atomic_group_elem_t *ag_elem;

	if (unlikely((unsigned int)ag_idx > EM_MAX_ATOMIC_GROUPS - 1))
		return NULL;

	ag_elem = &em_shm->atomic_group_tbl.ag_elem[ag_idx];

	return ag_elem;
}

static inline void
atomic_group_add_queue_list(atomic_group_elem_t *const ag_elem,
			    queue_elem_t *const q_elem)
{
	env_spinlock_lock(&ag_elem->lock);
	list_add(&ag_elem->qlist_head, &q_elem->agrp.agrp_node);
	env_atomic32_inc(&ag_elem->num_queues);
	env_spinlock_unlock(&ag_elem->lock);
}

static inline void
atomic_group_rem_queue_list(atomic_group_elem_t *const ag_elem,
			    queue_elem_t *const q_elem)
{
	env_spinlock_lock(&ag_elem->lock);
	if (!list_is_empty(&ag_elem->qlist_head)) {
		list_rem(&ag_elem->qlist_head, &q_elem->agrp.agrp_node);
		env_atomic32_dec(&ag_elem->num_queues);
	}
	env_spinlock_unlock(&ag_elem->lock);
}

static inline void
atomic_group_release(void)
{
	em_locm_t *const locm = &em_locm;
	const queue_elem_t *q_elem = locm->current.sched_q_elem;
	em_atomic_group_t atomic_group = q_elem->agrp.atomic_group;
	atomic_group_elem_t *const agrp_elem = atomic_group_elem_get(atomic_group);

	locm->atomic_group_released = true;
	env_spinlock_unlock(&agrp_elem->lock);
}

unsigned int
atomic_group_count(void);

/** Print information about all atomic groups */
void print_atomic_group_info(void);

/** Print information about all queues of the given atomic group */
void print_atomic_group_queues(em_atomic_group_t ag);

void print_ag_elem_info(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_ATOMIC_GROUP_H_ */
