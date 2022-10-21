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
 *
 * EM internal queue group functions
 *
 */

#ifndef EM_QUEUE_GROUP_H_
#define EM_QUEUE_GROUP_H_

#ifdef __cplusplus
extern "C" {
#endif

#define invalid_qgrp(queue_group) \
	((unsigned int)qgrp_hdl2idx((queue_group)) >= EM_MAX_QUEUE_GROUPS)

em_status_t queue_group_init(queue_group_tbl_t *const queue_group_tbl,
			     queue_group_pool_t *const queue_group_pool);
em_status_t queue_group_init_local(void);

em_queue_group_t
queue_group_create(const char *name, const em_core_mask_t *mask,
		   int num_notif, const em_notif_t notif_tbl[],
		   em_queue_group_t queue_group);
em_queue_group_t
queue_group_create_sync(const char *name, const em_core_mask_t *mask,
			em_queue_group_t requested_queue_group);

em_status_t
queue_group_modify(queue_group_elem_t *const qgrp_elem,
		   const em_core_mask_t *new_mask,
		   int num_notif, const em_notif_t notif_tbl[],
		   bool is_delete);
em_status_t
queue_group_modify_sync(queue_group_elem_t *const qgrp_elem,
			const em_core_mask_t *new_mask, bool is_delete);

void queue_group_add_queue_list(queue_group_elem_t *const queue_group_elem,
				queue_elem_t *const queue_elem);
void queue_group_rem_queue_list(queue_group_elem_t *const queue_group_elem,
				queue_elem_t *const queue_elem);
unsigned int queue_group_count(void);

/**
 * @brief Check that only running EM cores are set in mask
 *
 * @param mask Queue group core mask
 * @return EM_OK if mask is valid
 */
em_status_t queue_group_check_mask(const em_core_mask_t *mask);

/**
 * @brief Print EM queue group info
 */
void queue_group_info_print_all(void);

/**
 * @brief Print info about all queues belonging to the given queue group
 */
void queue_group_queues_print(em_queue_group_t qgrp);

/**
 * @brief EM internal event handler, add core to an EM queue group.
 *        (see em_internal_event.c&h)
 *
 * Handle the internal event requesting to add the core to an
 * ODP schedule group that is related to an EM queue group.
 */
void i_event__qgrp_add_core_req(const internal_event_t *i_ev);

/**
 * @brief EM internal event handler, remove core from an EM queue group.
 *        (see em_internal_event.c&h)
 *
 * Handle the internal event requesting to remove the core from an
 * ODP schedule group that is related to an EM queue group.
 */
void i_event__qgrp_rem_core_req(const internal_event_t *i_ev);

/**
 * Convert queue group handle <-> index
 */
static inline int
qgrp_hdl2idx(const em_queue_group_t queue_group)
{
	return (int)((uintptr_t)queue_group - 1);
}

/**
 * Convert queue group index <-> handle
 */
static inline em_queue_group_t
qgrp_idx2hdl(const int queue_group_idx)
{
	return (em_queue_group_t)(uintptr_t)(queue_group_idx + 1);
}

/**
 * Return the queue group element ptr or NULL if no such element
 */
static inline queue_group_elem_t *
queue_group_elem_get(em_queue_group_t queue_group)
{
	const int qgrp_idx = qgrp_hdl2idx(queue_group);
	queue_group_elem_t *qgrp_elem;

	if (unlikely((unsigned int)qgrp_idx > EM_MAX_QUEUE_GROUPS - 1))
		return NULL;

	qgrp_elem = &em_shm->queue_group_tbl.queue_group_elem[qgrp_idx];

	return qgrp_elem;
}

static inline int
queue_group_allocated(const queue_group_elem_t *queue_group_elem)
{
	return !objpool_in_pool(&queue_group_elem->queue_group_pool_elem);
}

/**
 * Write the EM single-core queue group name for a given core.
 */
static inline void
core_queue_grp_name(int core, char qgrp_name[/*out:len*/], size_t len)
{
	const size_t maxlen = len < EM_QUEUE_GROUP_NAME_LEN ?
				len : EM_QUEUE_GROUP_NAME_LEN;

	if (unlikely(maxlen == 0))
		return;

	/* write "core#" into qgrp_name[] */
	snprintf(qgrp_name, maxlen, "%s%d",
		 EM_QUEUE_GROUP_CORE_BASE_NAME, core);
	qgrp_name[maxlen - 1] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_GROUP_H_ */
