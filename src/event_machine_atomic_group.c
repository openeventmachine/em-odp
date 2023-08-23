/*
 *   Copyright (c) 2014-2023, Nokia Solutions and Networks
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

/* per core (thread) state for em_atomic_group_get_next() */
static ENV_LOCAL unsigned int _agrp_tbl_iter_idx;
/* Per core (thread) state of em_atomic_group_queue_get_next() */
static ENV_LOCAL unsigned int _agrp_q_iter_idx;
static ENV_LOCAL em_atomic_group_t _agrp_q_iter_agrp;

em_atomic_group_t
em_atomic_group_create(const char *name, em_queue_group_t queue_group)
{
	em_atomic_group_t atomic_group = EM_ATOMIC_GROUP_UNDEF;
	atomic_group_elem_t *ag_elem = NULL;
	const char *err_str = "";
	em_status_t error = EM_OK;
	int ret = 0;

	if (unlikely(invalid_qgrp(queue_group))) {
		error = EM_ERR_BAD_ARG;
		err_str = "Invalid queue group!";
		goto error;
	}

	/* New Atomic group */
	atomic_group = atomic_group_alloc();

	if (unlikely(atomic_group == EM_ATOMIC_GROUP_UNDEF)) {
		error = EM_ERR_ALLOC_FAILED;
		err_str = "Atomic group allocation failed!";
		goto error;
	}

	/* Initialize the atomic group */
	ag_elem = atomic_group_elem_get(atomic_group);
	if (unlikely(!ag_elem)) {
		/* Fatal since atomic_group_alloc() returned 'ok', should never happen */
		error = EM_FATAL(EM_ERR_BAD_ID);
		err_str = "Atomic group allocation failed: ag_elem NULL!";
		goto error;
	}

	env_atomic32_init(&ag_elem->num_queues);

	/* Store the related queue group */
	ag_elem->queue_group = queue_group;

	if (name != NULL) {
		strncpy(ag_elem->name, name, sizeof(ag_elem->name));
		ag_elem->name[sizeof(ag_elem->name) - 1] = '\0';
	} else {
		ag_elem->name[0] = '\0';
	}

	/*
	 * Create the AG internal stashes
	 */
	unsigned int num_obj = 0;
	odp_stash_capability_t stash_capa;
	odp_stash_param_t stash_param;

	ret = odp_stash_capability(&stash_capa, ODP_STASH_TYPE_FIFO);
	if (ret != 0) {
		error = EM_ERR_LIB_FAILED;
		err_str = "odp_stash_capability() failed!";
		goto error;
	}

	odp_stash_param_init(&stash_param);

	stash_param.type = ODP_STASH_TYPE_FIFO;
	stash_param.put_mode = ODP_STASH_OP_MT;
	/* 'get' protected by ag_elem->lock */
	stash_param.get_mode = ODP_STASH_OP_ST;

	/* Stash size: use EM default queue size value from config file: */
	num_obj = em_shm->opt.queue.min_events_default;
	if (num_obj != 0)
		stash_param.num_obj = num_obj;
	/* else: use odp default as set by odp_stash_param_init() */

	if (stash_param.num_obj > stash_capa.max_num_obj) {
		EM_LOG(EM_LOG_PRINT,
		       "%s(): req stash.num_obj(%" PRIu64 ") > capa.max_num_obj(%" PRIu64 ").\n"
		       "      ==> using max value:%" PRIu64 "\n", __func__,
		       stash_param.num_obj, stash_capa.max_num_obj, stash_capa.max_num_obj);
		stash_param.num_obj = stash_capa.max_num_obj;
	}

	stash_param.obj_size = sizeof(uint64_t);
	stash_param.cache_size = 0; /* No core local caching */

	ag_elem->stashes.hi_prio = odp_stash_create(ag_elem->name, &stash_param);
	ag_elem->stashes.lo_prio = odp_stash_create(ag_elem->name, &stash_param);
	if (unlikely(ag_elem->stashes.hi_prio == ODP_STASH_INVALID ||
		     ag_elem->stashes.lo_prio == ODP_STASH_INVALID)) {
		error = EM_ERR_LIB_FAILED;
		err_str = "odp_stash_create() failed!";
		goto error;
	}

	return atomic_group;

error:
	INTERNAL_ERROR(error, EM_ESCOPE_ATOMIC_GROUP_CREATE, err_str);
	if (atomic_group != EM_ATOMIC_GROUP_UNDEF)
		em_atomic_group_delete(atomic_group);

	return EM_ATOMIC_GROUP_UNDEF;
}

/*
 * Helper for em_atomic_group_delete()
 * Flush the atomic group's internal queues and then destroy them.
 */
static int
ag_stash_destroy(odp_stash_t stash)
{
	stash_entry_t entry_tbl[EM_SCHED_AG_MULTI_MAX_BURST];
	odp_event_t odp_evtbl[EM_SCHED_AG_MULTI_MAX_BURST];
	em_event_t ev_tbl[EM_SCHED_AG_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_SCHED_AG_MULTI_MAX_BURST];
	int32_t cnt = 0;
	bool esv_ena = esv_enabled();

	if (stash == ODP_STASH_INVALID)
		return -1;

	do {
		cnt = odp_stash_get_u64(stash, &entry_tbl[0].u64 /*[out]*/,
					EM_SCHED_AG_MULTI_MAX_BURST);
		if (cnt <= 0)
			break;
		for (int32_t i = 0; i < cnt; i++)
			odp_evtbl[i] = (odp_event_t)(uintptr_t)entry_tbl[i].evptr;

		events_odp2em(odp_evtbl, ev_tbl/*out*/, cnt);

		if (esv_ena) {
			event_to_hdr_multi(ev_tbl, ev_hdr_tbl/*out*/, cnt);
			evstate_em2usr_multi(ev_tbl/*in/out*/, ev_hdr_tbl,
					     cnt, EVSTATE__AG_DELETE);
		}

		em_free_multi(ev_tbl, cnt);
	} while (cnt > 0);

	return odp_stash_destroy(stash);
}

em_status_t
em_atomic_group_delete(em_atomic_group_t atomic_group)
{
	atomic_group_elem_t *const ag_elem =
		atomic_group_elem_get(atomic_group);
	em_status_t error = EM_OK;
	int err = 0;

	RETURN_ERROR_IF(ag_elem == NULL,
			EM_ERR_BAD_ARG, EM_ESCOPE_ATOMIC_GROUP_DELETE,
			"Invalid atomic group - cannot delete!");

	env_spinlock_lock(&ag_elem->lock);

	/* Error checks */
	err  = !list_is_empty(&ag_elem->qlist_head);
	err |= !atomic_group_allocated(ag_elem);

	if (unlikely(err)) {
		env_spinlock_unlock(&ag_elem->lock);
		return INTERNAL_ERROR(EM_ERR_BAD_STATE,
				      EM_ESCOPE_ATOMIC_GROUP_DELETE,
				      "Atomic group in bad state - cannot delete!");
	}

	/* Flush the atomic group's internal queues and destroy them */
	err  = ag_stash_destroy(ag_elem->stashes.hi_prio);
	err |= ag_stash_destroy(ag_elem->stashes.lo_prio);

	ag_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	ag_elem->name[0] = '\0';

	env_spinlock_unlock(&ag_elem->lock);

	/* Free the atomic group (elem) back into the AG-pool */
	error = atomic_group_free(atomic_group);
	RETURN_ERROR_IF(error != EM_OK || err != 0,
			error, EM_ESCOPE_ATOMIC_GROUP_DELETE,
			"Atomic group free failed(%d)!", err);

	return EM_OK;
}

em_queue_t
em_queue_create_ag(const char *name, em_queue_prio_t prio,
		   em_atomic_group_t atomic_group, const em_queue_conf_t *conf)
{
	em_queue_t queue;
	queue_elem_t *q_elem;
	em_queue_group_t queue_group;
	atomic_group_elem_t *const ag_elem =
		atomic_group_elem_get(atomic_group);
	const char *err_str = "";

	if (unlikely(ag_elem == NULL || !atomic_group_allocated(ag_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_CREATE_AG,
			       "Invalid Atomic Group:%" PRI_AGRP "",
			       atomic_group);
		return EM_QUEUE_UNDEF;
	}

	queue_group = ag_elem->queue_group;

	queue = queue_create(name, EM_QUEUE_TYPE_ATOMIC, prio, queue_group,
			     EM_QUEUE_UNDEF, atomic_group, conf, &err_str);

	if (unlikely(queue == EM_QUEUE_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_QUEUE_CREATE_AG,
			       "Atomic Group queue creation failed! (%s)",
			       err_str);
		return EM_QUEUE_UNDEF;
	}

	q_elem = queue_elem_get(queue);
	/* Add queue to atomic group list */
	atomic_group_add_queue_list(ag_elem, q_elem);

	return queue;
}

em_status_t
em_queue_create_static_ag(const char *name, em_queue_prio_t prio,
			  em_atomic_group_t atomic_group, em_queue_t queue,
			  const em_queue_conf_t *conf)
{
	em_queue_t queue_static;
	queue_elem_t *q_elem;
	em_queue_group_t queue_group;
	atomic_group_elem_t *const ag_elem =
		atomic_group_elem_get(atomic_group);
	const char *err_str = "";

	RETURN_ERROR_IF(ag_elem == NULL || !atomic_group_allocated(ag_elem),
			EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_CREATE_STATIC_AG,
			"Invalid Atomic Group:%" PRI_AGRP "", atomic_group);

	queue_group = ag_elem->queue_group;

	queue_static = queue_create(name, EM_QUEUE_TYPE_ATOMIC, prio,
				    queue_group, queue, atomic_group, conf,
				    &err_str);

	RETURN_ERROR_IF(queue_static == EM_QUEUE_UNDEF ||
			queue_static != queue,
			EM_ERR_NOT_FREE, EM_ESCOPE_QUEUE_CREATE_STATIC_AG,
			"Atomic Group static queue creation failed! (%s)",
			err_str);

	q_elem = queue_elem_get(queue);
	/* Add queue to atomic group list */
	atomic_group_add_queue_list(ag_elem, q_elem);

	return EM_OK;
}

em_atomic_group_t
em_atomic_group_get(em_queue_t queue)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);
	em_atomic_group_t atomic_group = EM_ATOMIC_GROUP_UNDEF;

	if (unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ATOMIC_GROUP_GET,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return EM_ATOMIC_GROUP_UNDEF;
	}

	if (q_elem->flags.in_atomic_group)
		atomic_group = q_elem->agrp.atomic_group;

	return atomic_group;
}

size_t
em_atomic_group_get_name(em_atomic_group_t atomic_group,
			 char *name, size_t maxlen)
{
	const atomic_group_elem_t *ag_elem =
		atomic_group_elem_get(atomic_group);
	size_t len = 0;

	if (unlikely(name == NULL || maxlen == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ATOMIC_GROUP_GET_NAME,
			       "Invalid args: name=0x%" PRIx64 ", maxlen=%zu",
			       name, maxlen);
		return 0;
	}

	if (unlikely(ag_elem == NULL || !atomic_group_allocated(ag_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ATOMIC_GROUP_GET_NAME,
			       "Invalid Atomic Group:%" PRI_AGRP "",
			       atomic_group);
		name[0] = '\0';
		return 0;
	}

	len = strnlen(ag_elem->name, sizeof(ag_elem->name) - 1);
	if (maxlen - 1 < len)
		len = maxlen - 1;

	memcpy(name, ag_elem->name, len);
	name[len] = '\0';

	return len;
}

em_atomic_group_t
em_atomic_group_find(const char *name)
{
	if (name && *name) {
		for (int i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
			const atomic_group_elem_t *ag_elem =
				&em_shm->atomic_group_tbl.ag_elem[i];

			if (atomic_group_allocated(ag_elem) &&
			    !strncmp(name, ag_elem->name,
				     EM_ATOMIC_GROUP_NAME_LEN))
				return ag_elem->atomic_group;
		}
	}
	return EM_ATOMIC_GROUP_UNDEF;
}

em_atomic_group_t
em_atomic_group_get_first(unsigned int *num)
{
	const atomic_group_elem_t *const agrp_elem_tbl =
		em_shm->atomic_group_tbl.ag_elem;
	const atomic_group_elem_t *ag_elem = &agrp_elem_tbl[0];
	const unsigned int agrp_count = atomic_group_count();

	_agrp_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = agrp_count;

	if (agrp_count == 0) {
		_agrp_tbl_iter_idx = EM_MAX_ATOMIC_GROUPS; /*UNDEF=_get_next()*/
		return EM_ATOMIC_GROUP_UNDEF;
	}

	/* find first */
	while (!atomic_group_allocated(ag_elem)) {
		_agrp_tbl_iter_idx++;
		if (_agrp_tbl_iter_idx >= EM_MAX_ATOMIC_GROUPS)
			return EM_ATOMIC_GROUP_UNDEF;
		ag_elem = &agrp_elem_tbl[_agrp_tbl_iter_idx];
	}

	return agrp_idx2hdl(_agrp_tbl_iter_idx);
}

em_atomic_group_t
em_atomic_group_get_next(void)
{
	if (_agrp_tbl_iter_idx >= EM_MAX_ATOMIC_GROUPS - 1)
		return EM_ATOMIC_GROUP_UNDEF;

	_agrp_tbl_iter_idx++;

	const atomic_group_elem_t *const agrp_elem_tbl =
		em_shm->atomic_group_tbl.ag_elem;
	const atomic_group_elem_t *ag_elem = &agrp_elem_tbl[_agrp_tbl_iter_idx];

	/* find next */
	while (!atomic_group_allocated(ag_elem)) {
		_agrp_tbl_iter_idx++;
		if (_agrp_tbl_iter_idx >= EM_MAX_ATOMIC_GROUPS)
			return EM_ATOMIC_GROUP_UNDEF;
		ag_elem = &agrp_elem_tbl[_agrp_tbl_iter_idx];
	}

	return agrp_idx2hdl(_agrp_tbl_iter_idx);
}

em_queue_t
em_atomic_group_queue_get_first(unsigned int *num,
				em_atomic_group_t atomic_group)
{
	const atomic_group_elem_t *const agrp_elem =
		atomic_group_elem_get(atomic_group);

	if (unlikely(agrp_elem == NULL || !atomic_group_allocated(agrp_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG,
			       EM_ESCOPE_ATOMIC_GROUP_QUEUE_GET_FIRST,
			       "Invalid atomic group:%" PRI_AGRP "",
			       atomic_group);
		if (num)
			*num = 0;
		return EM_QUEUE_UNDEF;
	}

	const unsigned int num_queues =
		env_atomic32_get(&agrp_elem->num_queues);

	if (num)
		*num = num_queues;

	if (num_queues == 0) {
		_agrp_q_iter_idx = EM_MAX_QUEUES; /* UNDEF = _get_next() */
		return EM_QUEUE_UNDEF;
	}

	/*
	 * A 'agrp_elem' contains a linked list with all it's queues. That list
	 * might be modified while processing this iteration, so instead we just
	 * go through the whole queue table.
	 * This is potentially a slow implementation and perhaps worth
	 * re-thinking?
	 */
	const queue_elem_t *const q_elem_tbl = em_shm->queue_tbl.queue_elem;
	const queue_elem_t *q_elem = &q_elem_tbl[0];

	_agrp_q_iter_idx = 0; /* reset list */
	_agrp_q_iter_agrp = atomic_group;

	/* find first */
	while (!queue_allocated(q_elem) ||
	       !q_elem->flags.in_atomic_group ||
	       q_elem->agrp.atomic_group != _agrp_q_iter_agrp) {
		_agrp_q_iter_idx++;
		if (_agrp_q_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
		q_elem = &q_elem_tbl[_agrp_q_iter_idx];
	}

	return queue_idx2hdl(_agrp_q_iter_idx);
}

em_queue_t
em_atomic_group_queue_get_next(void)
{
	if (_agrp_q_iter_idx >= EM_MAX_QUEUES - 1)
		return EM_QUEUE_UNDEF;

	_agrp_q_iter_idx++;

	const queue_elem_t *const q_elem_tbl = em_shm->queue_tbl.queue_elem;
	const queue_elem_t *q_elem = &q_elem_tbl[_agrp_q_iter_idx];

	/* find next */
	while (!queue_allocated(q_elem) ||
	       !q_elem->flags.in_atomic_group ||
	       q_elem->agrp.atomic_group != _agrp_q_iter_agrp) {
		_agrp_q_iter_idx++;
		if (_agrp_q_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
		q_elem = &q_elem_tbl[_agrp_q_iter_idx];
	}

	return queue_idx2hdl(_agrp_q_iter_idx);
}
