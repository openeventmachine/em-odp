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

/* per core (thread) state for em_queue_group_get_next() */
static ENV_LOCAL unsigned int _qgrp_tbl_iter_idx;
/* Per core (thread) state of em_queue_group_queue_get_next() */
static ENV_LOCAL unsigned int _qgrp_q_iter_idx;
static ENV_LOCAL em_queue_group_t _qgrp_q_iter_qgrp;

em_queue_group_t
em_queue_group_create(const char *name, const em_core_mask_t *mask,
		      int num_notif, const em_notif_t *notif_tbl)
{
	em_queue_group_t queue_group;
	em_status_t err;

	if (unlikely(mask == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_QUEUE_GROUP_CREATE,
			       "Core mask NULL");
		return EM_QUEUE_GROUP_UNDEF;
	}

	err = check_notif_tbl(num_notif, notif_tbl);
	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_QUEUE_GROUP_CREATE,
			       "Invalid notif cfg given!");
		return EM_QUEUE_GROUP_UNDEF;
	}

	queue_group = queue_group_create(name, mask, num_notif, notif_tbl,
					 EM_QUEUE_GROUP_UNDEF/*any free qgrp*/);
	return queue_group;
}

em_queue_group_t
em_queue_group_create_sync(const char *name, const em_core_mask_t *mask)
{
	em_queue_group_t queue_group;

	if (unlikely(mask == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER,
			       EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC,
			       "Core mask NULL");
		return EM_QUEUE_GROUP_UNDEF;
	}

	queue_group = queue_group_create_sync(name, mask, EM_QUEUE_GROUP_UNDEF
					      /* any free queue group */);
	return queue_group;
}

em_status_t
em_queue_group_delete(em_queue_group_t queue_group,
		      int num_notif, const em_notif_t *notif_tbl)
{
	em_core_mask_t zero_mask;
	em_status_t err;

	queue_group_elem_t *const qgrp_elem =
		queue_group_elem_get(queue_group);

	RETURN_ERROR_IF(qgrp_elem == NULL || !queue_group_allocated(qgrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_DELETE,
			"Invalid queue group: %" PRI_QGRP "", queue_group);

	err = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_DELETE,
			"Invalid notif cfg given!");

	em_core_mask_zero(&zero_mask);

	/* Use modify and the notif mechanism to set the core mask to zero */
	err = queue_group_modify(qgrp_elem, &zero_mask, num_notif, notif_tbl,
				 1 /* is_delete=1 */);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_DELETE,
			"Queue group:%" PRI_QGRP " modify for delete failed!",
			queue_group);

	return EM_OK;
}

em_status_t
em_queue_group_delete_sync(em_queue_group_t queue_group)
{
	em_core_mask_t zero_mask;
	em_status_t err;

	queue_group_elem_t *const qgrp_elem =
		queue_group_elem_get(queue_group);

	RETURN_ERROR_IF(qgrp_elem == NULL || !queue_group_allocated(qgrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC,
			"Invalid queue group: %" PRI_QGRP "", queue_group);

	em_core_mask_zero(&zero_mask);

	/* Use modify and the notif mechanism to set the core mask to zero */
	err = queue_group_modify_sync(qgrp_elem, &zero_mask,
				      1 /* is_delete=1 */);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC,
			"Queue group:%" PRI_QGRP " modify for delete failed!",
			queue_group);

	return EM_OK;
}

em_status_t
em_queue_group_modify(em_queue_group_t queue_group,
		      const em_core_mask_t *new_mask,
		      int num_notif, const em_notif_t *notif_tbl)
{
	queue_group_elem_t *const qgrp_elem =
		queue_group_elem_get(queue_group);
	em_status_t err;

	RETURN_ERROR_IF(qgrp_elem == NULL || !queue_group_allocated(qgrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_MODIFY,
			"Invalid queue group: %" PRI_QGRP "", queue_group);

	RETURN_ERROR_IF(new_mask == NULL,
			EM_ERR_BAD_POINTER, EM_ESCOPE_QUEUE_GROUP_MODIFY,
			"Queue group mask NULL! Queue group:%" PRI_QGRP "",
			queue_group);

	err = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_MODIFY,
			"Invalid notif cfg given!");

	err = queue_group_modify(qgrp_elem, new_mask, num_notif, notif_tbl,
				 0 /* is_delete=0 */);
	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_MODIFY,
			"Queue group:%" PRI_QGRP " modify failed!",
			queue_group);
	return EM_OK;
}

em_status_t
em_queue_group_modify_sync(em_queue_group_t queue_group,
			   const em_core_mask_t *new_mask)
{
	queue_group_elem_t *const qgrp_elem =
		queue_group_elem_get(queue_group);
	em_status_t err;

	RETURN_ERROR_IF(qgrp_elem == NULL || !queue_group_allocated(qgrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC,
			"Invalid queue group: %" PRI_QGRP "", queue_group);

	RETURN_ERROR_IF(new_mask == NULL,
			EM_ERR_BAD_POINTER, EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC,
			"Queue group mask NULL! Queue group:%" PRI_QGRP "",
			queue_group);

	err = queue_group_modify_sync(qgrp_elem, new_mask, 0 /* is_delete=0 */);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC,
			"Queue group:%" PRI_QGRP " modify sync failed!",
			queue_group);
	return EM_OK;
}

em_queue_group_t
em_queue_group_find(const char *name)
{
	odp_schedule_group_t odp_group;

	if (name == NULL || name[0] == '\0')
		return EM_QUEUE_GROUP_UNDEF;

	odp_group = odp_schedule_group_lookup(name);
	if (odp_group == ODP_SCHED_GROUP_INVALID)
		return EM_QUEUE_GROUP_UNDEF;

	for (int i = 0; i < EM_MAX_QUEUE_GROUPS; i++) {
		queue_group_elem_t *const qgrp_elem =
			&em_shm->queue_group_tbl.queue_group_elem[i];

		if (qgrp_elem->odp_sched_group == odp_group &&
		    queue_group_allocated(qgrp_elem))
			return qgrp_idx2hdl(i);
	}

	return EM_QUEUE_GROUP_UNDEF;
}

em_status_t
em_queue_group_get_mask(em_queue_group_t queue_group, em_core_mask_t *mask)
{
	int allocated, pending_modify;
	queue_group_elem_t *const qg_elem = queue_group_elem_get(queue_group);

	RETURN_ERROR_IF(qg_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_MASK,
			"Invalid queue group:%" PRI_QGRP "", queue_group);

	env_spinlock_lock(&qg_elem->lock);

	allocated = queue_group_allocated(qg_elem);
	pending_modify = qg_elem->pending_modify;
	em_core_mask_copy(mask, &qg_elem->core_mask);

	env_spinlock_unlock(&qg_elem->lock);

	RETURN_ERROR_IF(!allocated || pending_modify,
			EM_ERR_BAD_STATE, EM_ESCOPE_QUEUE_GROUP_MASK,
			"Queue group:%" PRI_QGRP " in bad state:\t"
			"allocated=%i, pending_modify=%i",
			queue_group, allocated, pending_modify);

	return EM_OK;
}

size_t
em_queue_group_get_name(em_queue_group_t queue_group,
			char *name, size_t maxlen)
{
	queue_group_elem_t *const qg_elem = queue_group_elem_get(queue_group);
	odp_schedule_group_info_t info;
	size_t len;
	int ret;

	if (unlikely(name == NULL || maxlen == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER,
			       EM_ESCOPE_QUEUE_GROUP_GET_NAME,
			       "Invalid name=0x%" PRIx64 " or maxlen=%zu",
			       name, maxlen);
		return 0;
	}

	name[0] = '\0';

	if (unlikely(qg_elem == NULL || !queue_group_allocated(qg_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER,
			       EM_ESCOPE_QUEUE_GROUP_GET_NAME,
			       "Invalid queue group:%" PRI_QGRP "",
			       queue_group);
		return 0;
	}

	ret = odp_schedule_group_info(qg_elem->odp_sched_group, &info);
	if (unlikely(ret != 0)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED,
			       EM_ESCOPE_QUEUE_GROUP_GET_NAME,
			       "Failed to retrieve queue group info");
		return 0;
	}

	if (unlikely(info.name == NULL))
		return 0;

	len = strnlen(info.name, ODP_SCHED_GROUP_NAME_LEN - 1);
	if (maxlen - 1 < len)
		len = maxlen - 1;

	memcpy(name, info.name, len);
	name[len] = '\0';

	return len;
}

em_queue_group_t
em_queue_group_get_first(unsigned int *num)
{
	queue_group_elem_t *const qgrp_elem_tbl =
		em_shm->queue_group_tbl.queue_group_elem;
	queue_group_elem_t *qgrp_elem = &qgrp_elem_tbl[0];
	const unsigned int max_qgrps = EM_MAX_QUEUE_GROUPS;
	const unsigned int qgrp_cnt = queue_group_count();

	_qgrp_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = qgrp_cnt;

	if (qgrp_cnt == 0) {
		_qgrp_tbl_iter_idx = max_qgrps; /* UNDEF = _get_next() */
		return EM_QUEUE_GROUP_UNDEF;
	}

	/* find first */
	while (!queue_group_allocated(qgrp_elem)) {
		_qgrp_tbl_iter_idx++;
		if (_qgrp_tbl_iter_idx >= max_qgrps)
			return EM_QUEUE_GROUP_UNDEF;
		qgrp_elem = &qgrp_elem_tbl[_qgrp_tbl_iter_idx];
	}

	return qgrp_idx2hdl(_qgrp_tbl_iter_idx);
}

em_queue_group_t
em_queue_group_get_next(void)
{
	const unsigned int max_qgrps = EM_MAX_QUEUE_GROUPS;

	if (_qgrp_tbl_iter_idx >= max_qgrps - 1)
		return EM_QUEUE_GROUP_UNDEF;

	_qgrp_tbl_iter_idx++;

	queue_group_elem_t *const qgrp_elem_tbl =
		em_shm->queue_group_tbl.queue_group_elem;
	queue_group_elem_t *qgrp_elem = &qgrp_elem_tbl[_qgrp_tbl_iter_idx];

	/* find next */
	while (!queue_group_allocated(qgrp_elem)) {
		_qgrp_tbl_iter_idx++;
		if (_qgrp_tbl_iter_idx >= max_qgrps)
			return EM_QUEUE_GROUP_UNDEF;
		qgrp_elem = &qgrp_elem_tbl[_qgrp_tbl_iter_idx];
	}

	return qgrp_idx2hdl(_qgrp_tbl_iter_idx);
}

em_queue_t
em_queue_group_queue_get_first(unsigned int *num, em_queue_group_t queue_group)
{
	queue_group_elem_t *const qgrp_elem = queue_group_elem_get(queue_group);

	if (unlikely(qgrp_elem == NULL || !queue_group_allocated(qgrp_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID,
			       EM_ESCOPE_QUEUE_GROUP_QUEUE_GET_FIRST,
			       "Invalid queue group:%" PRI_QGRP "",
			       queue_group);
		if (num)
			*num = 0;
		return EM_QUEUE_UNDEF;
	}

	const unsigned int num_queues =
		env_atomic32_get(&qgrp_elem->num_queues);

	if (num)
		*num = num_queues;

	if (num_queues == 0) {
		_qgrp_q_iter_idx = EM_MAX_QUEUES; /* UNDEF = _get_next() */
		return EM_QUEUE_UNDEF;
	}

	/*
	 * A 'qgrp_elem' contains a linked list with all it's queues. That list
	 * might be modified while processing this iteration, so instead we just
	 * go through the whole queue table.
	 * This is potentially a slow implementation and perhaps worth
	 * re-thinking?
	 */
	queue_elem_t *const q_elem_tbl = em_shm->queue_tbl.queue_elem;
	queue_elem_t *q_elem = &q_elem_tbl[0];

	_qgrp_q_iter_idx = 0; /* reset list */
	_qgrp_q_iter_qgrp = queue_group;

	/* find first */
	while (!queue_allocated(q_elem) ||
	       q_elem->queue_group != _qgrp_q_iter_qgrp) {
		_qgrp_q_iter_idx++;
		if (_qgrp_q_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
		q_elem = &q_elem_tbl[_qgrp_q_iter_idx];
	}

	return queue_idx2hdl(_qgrp_q_iter_idx);
}

em_queue_t
em_queue_group_queue_get_next(void)
{
	if (_qgrp_q_iter_idx >= EM_MAX_QUEUES - 1)
		return EM_QUEUE_UNDEF;

	_qgrp_q_iter_idx++;

	queue_elem_t *const q_elem_tbl = em_shm->queue_tbl.queue_elem;
	queue_elem_t *q_elem = &q_elem_tbl[_qgrp_q_iter_idx];

	/* find next */
	while (!queue_allocated(q_elem) ||
	       q_elem->queue_group != _qgrp_q_iter_qgrp) {
		_qgrp_q_iter_idx++;
		if (_qgrp_q_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
		q_elem = &q_elem_tbl[_qgrp_q_iter_idx];
	}

	return queue_idx2hdl(_qgrp_q_iter_idx);
}
