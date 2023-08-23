/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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
#include <event_machine/platform/env/environment.h>

/* per core (thread) state for queue_get_next() */
static ENV_LOCAL unsigned int _queue_tbl_iter_idx;

COMPILE_TIME_ASSERT(EM_QUEUE_NAME_LEN <= ODP_QUEUE_NAME_LEN,
		    EM_QUEUE_NAME_LEN_OVER_ODP_LIMIT);

em_queue_t
em_queue_create(const char *name, em_queue_type_t type, em_queue_prio_t prio,
		em_queue_group_t queue_group, const em_queue_conf_t *conf)
{
	const char *err_str = "";
	em_queue_t queue;

	queue = queue_create(name, type, prio, queue_group,
			     EM_QUEUE_UNDEF, EM_ATOMIC_GROUP_UNDEF,
			     conf, &err_str);

	if (unlikely(queue == EM_QUEUE_UNDEF))
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_QUEUE_CREATE,
			       err_str);
	return queue;
}

em_status_t
em_queue_create_static(const char *name, em_queue_type_t type,
		       em_queue_prio_t prio, em_queue_group_t queue_group,
		       em_queue_t queue, const em_queue_conf_t *conf)
{
	const char *err_str = "";
	em_queue_t queue_static;
	internal_queue_t iq;

	iq.queue = queue;

	RETURN_ERROR_IF(iq.device_id != em_shm->conf.device_id ||
			iq.queue_id < EM_QUEUE_STATIC_MIN ||
			iq.queue_id > EM_QUEUE_STATIC_MAX,
			EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_CREATE_STATIC,
			"Invalid static queue requested:%" PRI_QUEUE "",
			queue);

	queue_static = queue_create(name, type, prio, queue_group,
				    queue, EM_ATOMIC_GROUP_UNDEF,
				    conf, &err_str);

	RETURN_ERROR_IF(queue_static == EM_QUEUE_UNDEF ||
			queue_static != queue,
			EM_ERR_NOT_FREE, EM_ESCOPE_QUEUE_CREATE_STATIC,
			err_str);
	return EM_OK;
}

em_status_t em_queue_delete(em_queue_t queue)
{
	queue_elem_t *const q_elem = queue_elem_get(queue);
	em_status_t status;

	RETURN_ERROR_IF(q_elem == NULL || !queue_allocated(q_elem),
			EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_DELETE,
			"Invalid queue:%" PRI_QUEUE "", queue);

	status = queue_delete(q_elem);

	RETURN_ERROR_IF(status != EM_OK, status, EM_ESCOPE_QUEUE_DELETE,
			"queue delete failed!");

	return status;
}

em_status_t em_queue_set_context(em_queue_t queue, const void *context)
{
	queue_elem_t *const queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(queue_elem == NULL || !queue_allocated(queue_elem),
				EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_SET_CONTEXT,
				"Invalid queue:%" PRI_QUEUE "", queue);

	queue_elem->context = (void *)(uintptr_t)context;

	return EM_OK;
}

void *em_queue_get_context(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 && unlikely(queue_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_CONTEXT,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return NULL;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_QUEUE_GET_CONTEXT,
			       "Queue:%" PRI_QUEUE " not created!", queue);
		return NULL;
	}

	return queue_elem->context;
}

size_t em_queue_get_name(em_queue_t queue, char *name, size_t maxlen)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(name == NULL || maxlen == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_NAME,
			       "Invalid ptr or maxlen (name=0x%" PRIx64 ", maxlen=%zu)",
			       name, maxlen);
		return 0;
	}

	name[0] = '\0';

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(queue_elem == NULL || !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_NAME,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return 0;
	}

	return queue_get_name(queue_elem, name, maxlen);
}

em_queue_t em_queue_find(const char *name)
{
	if (name && *name) {
		/* this might be worth optimizing if maaany queues */
		for (int i = 0; i < EM_MAX_QUEUES; i++) {
			const queue_elem_t *q_elem =
				&em_shm->queue_tbl.queue_elem[i];

			if (queue_allocated(q_elem) &&
			    !strncmp(name, em_shm->queue_tbl.name[i], EM_QUEUE_NAME_LEN)) {
				return (em_queue_t)(uintptr_t)
					em_shm->queue_tbl.queue_elem[i].queue;
			}
		}
	}
	return EM_QUEUE_UNDEF;
}

em_queue_prio_t em_queue_get_priority(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(queue_elem == NULL || !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_PRIORITY,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return EM_QUEUE_PRIO_UNDEF;
	}

	return queue_elem->priority;
}

em_queue_type_t em_queue_get_type(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(queue_elem == NULL || !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_TYPE,
			       "Invalid queue-id:%" PRI_QUEUE "", queue);
		return EM_QUEUE_TYPE_UNDEF;
	}

	return queue_elem->type;
}

em_queue_group_t em_queue_get_group(em_queue_t queue)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_GROUP,
			       "Invalid queue-id:%" PRI_QUEUE "", queue);
		return EM_QUEUE_GROUP_UNDEF;
	}

	if (unlikely(q_elem->state == EM_QUEUE_STATE_INVALID))
		return EM_QUEUE_GROUP_UNDEF;
	else
		return q_elem->queue_group;
}

em_event_t em_queue_dequeue(em_queue_t queue)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);
	em_event_t event;

	if (unlikely(EM_CHECK_LEVEL > 0 && !q_elem)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_DEQUEUE,
			       "Invalid EM queue:%" PRI_QUEUE "", queue);
		return EM_EVENT_UNDEF;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_QUEUE_DEQUEUE,
			       "Queue:%" PRI_QUEUE " not created", queue);
		return EM_EVENT_UNDEF;
	}

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(q_elem->type != EM_QUEUE_TYPE_UNSCHEDULED)) {
		INTERNAL_ERROR(EM_ERR_BAD_CONTEXT, EM_ESCOPE_QUEUE_DEQUEUE,
			       "Queue is not unscheduled, cannot dequeue!");
		return EM_EVENT_UNDEF;
	}

	event = queue_dequeue(q_elem);
	return event;
}

int em_queue_dequeue_multi(em_queue_t queue,
			   em_event_t events[/*out*/], int num)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);
	int ret;

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!q_elem || !events || num < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_DEQUEUE_MULTI,
			       "Inv.args: Q:%" PRI_QUEUE " events[]:%p num:%d",
			       queue, events, num);
		return 0;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_QUEUE_DEQUEUE,
			       "Queue:%" PRI_QUEUE " not created", queue);
		return 0;
	}

	if (unlikely(num == 0))
		return 0;

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(q_elem->type != EM_QUEUE_TYPE_UNSCHEDULED)) {
		INTERNAL_ERROR(EM_ERR_BAD_CONTEXT,
			       EM_ESCOPE_QUEUE_DEQUEUE_MULTI,
			       "Queue is not unscheduled, cannot dequeue!");
		return 0;
	}

	ret = queue_dequeue_multi(q_elem, events /*out*/, num);
	if (unlikely(ret < 0)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED,
			       EM_ESCOPE_QUEUE_DEQUEUE_MULTI,
			       "odp_queue_deq_multi(%d):%d", num, ret);
		return 0;
	}

	return ret;
}

em_queue_t em_queue_current(void)
{
	return queue_current();
}

em_queue_t em_queue_get_first(unsigned int *num)
{
	const queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;
	const unsigned int queue_cnt = queue_count();

	_queue_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = queue_cnt;

	if (queue_cnt == 0) {
		_queue_tbl_iter_idx = EM_MAX_QUEUES; /* UNDEF = _get_next() */
		return EM_QUEUE_UNDEF;
	}

	/* find first */
	while (!queue_allocated(&queue_tbl->queue_elem[_queue_tbl_iter_idx])) {
		_queue_tbl_iter_idx++;
		if (_queue_tbl_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
	}

	return queue_idx2hdl(_queue_tbl_iter_idx);
}

em_queue_t em_queue_get_next(void)
{
	if (_queue_tbl_iter_idx >= EM_MAX_QUEUES - 1)
		return EM_QUEUE_UNDEF;

	_queue_tbl_iter_idx++;

	const queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;

	/* find next */
	while (!queue_allocated(&queue_tbl->queue_elem[_queue_tbl_iter_idx])) {
		_queue_tbl_iter_idx++;
		if (_queue_tbl_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
	}

	return queue_idx2hdl(_queue_tbl_iter_idx);
}

int em_queue_get_index(em_queue_t queue)
{
	const internal_queue_t iq = {.queue = queue};
	const int queue_idx = queue_id2idx(iq.queue_id); /* return value */

	if (unlikely((unsigned int)queue_idx > EM_MAX_QUEUES - 1))
		goto error;

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(iq.device_id != em_shm->conf.device_id))
		goto error;

	if (EM_CHECK_LEVEL >= 3) {
		const queue_elem_t *q_elem =
			&em_shm->queue_tbl.queue_elem[queue_idx];
		if (unlikely(q_elem == NULL || !queue_allocated(q_elem)))
			goto error;
	}

	return queue_idx;

error:
	INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_GET_INDEX,
		       "Bad arg, invalid queue:%" PRI_QUEUE ":\n"
		       "  Q.device-id:0x%" PRIx16 " Q.id:0x%" PRIx16 "",
		       queue, iq.device_id, iq.queue_id);
	return queue_idx % EM_MAX_QUEUES;
}

int em_queue_get_num_prio(int *num_runtime)
{
	if (EM_CHECK_LEVEL > 1 && unlikely(em_shm == NULL)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED,
			       EM_ESCOPE_QUEUE_GET_NUM_PRIO,
			       "EM not initialized!");
		return 0;
	}
	if (num_runtime != NULL)
		*num_runtime = em_shm->queue_prio.num_runtime;

	return EM_QUEUE_PRIO_NUM;
}
