/*
 *   Copyright (c) 2015-2026, Nokia Solutions and Networks
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_eo.h"
#include "em_eo_types.h"
#include "em_error.h"
#include "em_internal_event.h"
#include "em_internal_event_types.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"

/* per core (thread) state for queue_next() */
static ENV_LOCAL unsigned int _queue_tbl_iter_idx;

COMPILE_TIME_ASSERT(EM_QUEUE_NAME_LEN <= ODP_QUEUE_NAME_LEN,
		    EM_QUEUE_NAME_LEN_OVER_ODP_LIMIT);

em_queue_t
em_queue_create(const char *name, em_queue_type_t type, em_queue_prio_t prio,
		em_queue_group_t queue_group, const em_queue_conf_t *conf)
{
	const char *err_str = "";
	em_queue_t queue;
	em_queue_param_t param;

	if (unlikely(em_shm->opt.queue.num_dynamic == 0)) {
		INTERNAL_ERROR(EM_ERR_NOT_SUPPORTED, EM_ESCOPE_QUEUE_CREATE,
			       "Dynamic queues not configured!");
		return EM_QUEUE_UNDEF;
	}

	em_queue_param_init(&param);
	param.type = type;
	param.prio = prio;
	param.queue_group = queue_group;
	queue_param_apply_conf(&param, conf);

	queue = queue_create_param(name, &param, &err_str);

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
	em_queue_param_t param;
	internal_queue_t iq = {.queue = queue};

	RETURN_ERROR_IF(em_shm->opt.queue.num_static == 0,
			EM_ERR_NOT_SUPPORTED, EM_ESCOPE_QUEUE_CREATE_STATIC,
			"Static queues not configured!");

	RETURN_ERROR_IF(queue == EM_QUEUE_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_CREATE_STATIC,
			"Invalid static queue requested:%" PRI_QUEUE "",
			queue);

	RETURN_ERROR_IF(iq.queue_id < em_shm->queue_tbl.first_static_queue_id ||
			iq.queue_id > em_shm->queue_tbl.last_static_queue_id,
			EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_CREATE_STATIC,
			"Static queue id:%u out of range (%u-%u)",
			iq.queue_id,
			em_shm->queue_tbl.first_static_queue_id,
			em_shm->queue_tbl.last_static_queue_id);

	em_queue_param_init(&param);
	param.queue = queue;
	param.type = type;
	param.prio = prio;
	param.queue_group = queue_group;
	queue_param_apply_conf(&param, conf);

	queue_static = queue_create_param(name, &param, &err_str);
	RETURN_ERROR_IF(queue_static == EM_QUEUE_UNDEF,
			EM_ERR_NOT_FREE, EM_ESCOPE_QUEUE_CREATE_STATIC,
			"Static queue:%" PRI_QUEUE " creation failed: %s", queue, err_str);

	if (unlikely(queue_static != queue)) {
		const char *qdel_err = "queue cleanup ok";
		queue_elem_t *q_elem = queue_elem_get(queue_static);

		/* Fatal error if q_elem == NULL, should never happen if queue_static != UNDEF */
		RETURN_ERROR_IF(!q_elem, EM_FATAL(EM_ERR_BAD_POINTER),
				EM_ESCOPE_QUEUE_CREATE_STATIC,
				"Queue error - req:%" PRI_QUEUE " vs. %" PRI_QUEUE "(q_elem NULL)",
				queue, queue_static);

		(void)queue_delete(q_elem, &qdel_err);

		return INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_CREATE_STATIC,
				      "Queue req:%" PRI_QUEUE " vs. %" PRI_QUEUE " (deleted:%s)",
				      queue, queue_static, qdel_err);
	}

	return EM_OK;
}

void em_queue_aggr_conf_init(uint32_t num_aggr, em_queue_aggr_conf_t aggr_conf[])
{
	if (unlikely(num_aggr == 0 || num_aggr > EM_QUEUE_MAX_AGGR || !aggr_conf)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_ARG), EM_ESCOPE_QUEUE_AGGR_CONF_INIT,
			       "Invalid args: num_aggr=%u (valid: 1..%u), aggr_conf=%p",
			       num_aggr, EM_QUEUE_MAX_AGGR, aggr_conf);
		return;
	}

	/* Set whole aggr_conf[] first to zero */
	memset(aggr_conf, 0, sizeof(em_queue_aggr_conf_t) * num_aggr);

	for (uint32_t i = 0; i < num_aggr; i++) {
		aggr_conf[i].pool = EM_POOL_UNDEF;
		aggr_conf[i].event_type = EM_EVENT_TYPE_UNDEF;
		aggr_conf[i].__internal_check = EM_CHECK_INIT_CALLED;
	}
}

void em_queue_param_init(em_queue_param_t *param)
{
	if (unlikely(!param)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_ARG), EM_ESCOPE_QUEUE_PARAM_INIT,
			       "Invalid arg: param=%p", param);
		return;
	}

	memset(param, 0, sizeof(em_queue_param_t));
	param->queue = EM_QUEUE_UNDEF;
	param->type = EM_QUEUE_TYPE_UNDEF;
	param->prio = EM_QUEUE_PRIO_UNDEF;
	param->queue_group = EM_QUEUE_GROUP_UNDEF;
	param->atomic_group = EM_ATOMIC_GROUP_UNDEF;
	param->flags = EM_QUEUE_FLAG_DEFAULT;
	param->__internal_check = EM_CHECK_INIT_CALLED;
}

em_queue_t em_queue_create_param(const char *name,
				 const em_queue_param_t *param)
{
	if (unlikely(!param || param->__internal_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_QUEUE_CREATE_PARAM,
			       "Invalid queue param: Use em_queue_param_init() before create");
		return EM_QUEUE_UNDEF;
	}

	const char *err_str = "";
	em_queue_t queue = queue_create_param(name, param, &err_str);

	if (unlikely(queue == EM_QUEUE_UNDEF))
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_QUEUE_CREATE_PARAM, err_str);

	return queue;
}

em_status_t em_queue_delete(em_queue_t queue)
{
	queue_elem_t *const q_elem = queue_elem_get(queue);
	em_status_t status;
	const char *del_err = "unknown error";

	RETURN_ERROR_IF(q_elem == NULL || !queue_allocated(q_elem),
			EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_DELETE,
			"Invalid queue:%" PRI_QUEUE "", queue);

	status = queue_delete(q_elem, &del_err);

	RETURN_ERROR_IF(status != EM_OK, status, EM_ESCOPE_QUEUE_DELETE,
			"%s", del_err);

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

void *em_queue_context(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 && unlikely(queue_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_CONTEXT,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return NULL;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_QUEUE_CONTEXT,
			       "Queue:%" PRI_QUEUE " not created!", queue);
		return NULL;
	}

	return queue_elem->context;
}

size_t em_queue_name(em_queue_t queue, char *name, size_t maxlen)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(name == NULL || maxlen == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_NAME,
			       "Invalid ptr or maxlen (name=0x%" PRIx64 ", maxlen=%zu)",
			       name, maxlen);
		return 0;
	}

	name[0] = '\0';

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(queue_elem == NULL || !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_NAME,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return 0;
	}

	return queue_name(queue_elem, name, maxlen);
}

em_queue_t em_queue_find(const char *name)
{
	if (name && *name) {
		/* this might be worth optimizing if maaany queues */
		for (unsigned int i = 0; i < em_shm->queue_tbl.max_queue_num; i++) {
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

em_queue_prio_t em_queue_priority(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(queue_elem == NULL || !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_PRIORITY,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return EM_QUEUE_PRIO_UNDEF;
	}

	return queue_elem->priority;
}

em_queue_type_t em_queue_type(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(queue_elem == NULL || !queue_allocated(queue_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_TYPE,
			       "Invalid queue-id:%" PRI_QUEUE "", queue);
		return EM_QUEUE_TYPE_UNDEF;
	}

	return queue_elem->type;
}

em_queue_t em_queue_static_handle(uint16_t static_offset)
{
	if (unlikely(em_shm->opt.queue.num_static == 0)) {
		INTERNAL_ERROR(EM_ERR_NOT_SUPPORTED,
			       EM_ESCOPE_QUEUE_STATIC_HANDLE,
			       "Static queues not configured!");
		return EM_QUEUE_UNDEF;
	}

	if (unlikely(static_offset >= em_shm->opt.queue.num_static)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG,
			       EM_ESCOPE_QUEUE_STATIC_HANDLE,
			       "Static offset %u out of range (0-%u)!",
			       static_offset,
			       em_shm->opt.queue.num_static - 1);
		return EM_QUEUE_UNDEF;
	}

	return queue_id2hdl(em_shm->queue_tbl.first_static_queue_id + static_offset);
}

em_queue_group_t em_queue_qgroup(em_queue_t queue)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_QGROUP,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return EM_QUEUE_GROUP_UNDEF;
	}

	if (unlikely(q_elem->state == EM_QUEUE_STATE_INVALID))
		return EM_QUEUE_GROUP_UNDEF;
	else
		return q_elem->queue_group;
}

em_atomic_group_t em_queue_agroup(em_queue_t queue)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);
	em_atomic_group_t atomic_group = EM_ATOMIC_GROUP_UNDEF;

	if (unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_AGROUP,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return EM_ATOMIC_GROUP_UNDEF;
	}

	if (q_elem->flags.in_atomic_group)
		atomic_group = q_elem->agrp.atomic_group;

	return atomic_group;
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

em_queue_t em_queue_first(unsigned int *num)
{
	const queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;
	const unsigned int queue_cnt = queue_count();
	const unsigned int max_queues = em_shm->queue_tbl.max_queue_num;

	_queue_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = queue_cnt;

	if (queue_cnt == 0) {
		_queue_tbl_iter_idx = max_queues; /* UNDEF = _next() */
		return EM_QUEUE_UNDEF;
	}

	/* find first */
	while (!queue_allocated(&queue_tbl->queue_elem[_queue_tbl_iter_idx])) {
		_queue_tbl_iter_idx++;
		if (_queue_tbl_iter_idx >= max_queues)
			return EM_QUEUE_UNDEF;
	}

	return queue_idx2hdl(_queue_tbl_iter_idx);
}

em_queue_t em_queue_next(void)
{
	const unsigned int max_queues = em_shm->queue_tbl.max_queue_num;

	if (_queue_tbl_iter_idx >= max_queues - 1)
		return EM_QUEUE_UNDEF;

	_queue_tbl_iter_idx++;

	const queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;

	/* find next */
	while (!queue_allocated(&queue_tbl->queue_elem[_queue_tbl_iter_idx])) {
		_queue_tbl_iter_idx++;
		if (_queue_tbl_iter_idx >= max_queues)
			return EM_QUEUE_UNDEF;
	}

	return queue_idx2hdl(_queue_tbl_iter_idx);
}

int em_queue_index(em_queue_t queue)
{
	const internal_queue_t iq = {.queue = queue};
	const int queue_idx = queue_id2idx(iq.queue_id); /* return value */

	if (unlikely((uint16_t)queue_idx > em_shm->queue_tbl.max_queue_num - 1))
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
	INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_INDEX,
		       "Bad arg, invalid queue:%" PRI_QUEUE ":\n"
		       "  Q.device-id:0x%" PRIx16 " Q.id:0x%" PRIx16 "",
		       queue, iq.device_id, iq.queue_id);
	return queue_idx % em_shm->queue_tbl.max_queue_num;
}

int em_queue_num_prio(int *num_runtime)
{
	if (EM_CHECK_LEVEL > 1 && unlikely(em_shm == NULL)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_QUEUE_NUM_PRIO,
			       "EM not initialized!");
		return 0;
	}
	if (num_runtime != NULL)
		*num_runtime = em_shm->queue_prio.num_runtime;

	return EM_QUEUE_PRIO_NUM;
}

int em_queue_max_num(void)
{
	return em_shm->queue_tbl.max_queue_num;
}

int em_queue_internal_max_num(void)
{
	return MAX_INTERNAL_QUEUES;
}

int em_queue_static_max_num(void)
{
	return em_shm->opt.queue.num_static;
}

int em_queue_dynamic_max_num(void)
{
	return em_shm->opt.queue.num_dynamic;
}

int em_queue_aggr_max_num(void)
{
	return em_shm->opt.queue.num_aggr;
}

uint32_t em_queue_aggr_num(em_queue_t parent_queue)
{
	const queue_elem_t *q_elem = queue_elem_get(parent_queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!q_elem || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_AGGR_NUM,
			       "Invalid arg: parent_queue=%" PRI_QUEUE "", parent_queue);
		return 0;
	}

	if (unlikely(!q_elem->flags.has_aggr))
		return 0;

	return q_elem->aggr_parent.num_aggr;
}

em_queue_t em_queue_aggr(em_queue_t parent_queue, uint32_t aggr_index)
{
	const queue_elem_t *q_elem = queue_elem_get(parent_queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!q_elem || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_AGGR,
			       "Invalid arg: parent_queue=%" PRI_QUEUE "", parent_queue);
		return EM_QUEUE_UNDEF;
	}

	if (unlikely(!q_elem->flags.has_aggr ||
		     aggr_index >= q_elem->aggr_parent.num_aggr))
		return EM_QUEUE_UNDEF;

	return (em_queue_t)(uintptr_t)q_elem->aggr_parent.aggr_queues[aggr_index];
}

uint32_t em_queue_aggr_list(em_queue_t parent_queue,
			    em_queue_t aggr_list[/*out:max*/], uint32_t max,
			    uint32_t *num_aggr /*out*/)
{
	const queue_elem_t *q_elem = queue_elem_get(parent_queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!q_elem || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_AGGR_LIST,
			       "Invalid arg: parent_queue=%" PRI_QUEUE "", parent_queue);
		return 0;
	}

	if (unlikely(!q_elem->flags.has_aggr)) {
		if (num_aggr)
			*num_aggr = 0;
		return 0;
	}

	const uint32_t num = q_elem->aggr_parent.num_aggr; /* <= EM_QUEUE_MAX_AGGR */
	const uint32_t num_out = MIN(num, max);

	if (num_aggr)
		*num_aggr = num;
	if (!aggr_list || num_out == 0)
		return 0;

	for (uint32_t i = 0; i < num_out; i++)
		aggr_list[i] = (em_queue_t)(uintptr_t)q_elem->aggr_parent.aggr_queues[i];

	return num_out;
}

uint32_t em_queue_aggr_info(em_queue_t parent_queue,
			    em_queue_aggr_info_t aggr_info[/*out*/],
			    uint32_t max, uint32_t *num_aggr /*out*/)
{
	const queue_elem_t *q_elem = queue_elem_get(parent_queue);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!q_elem || !queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_QUEUE_AGGR_INFO,
			       "Invalid args: parent_queue=%" PRI_QUEUE "", parent_queue);
		return 0;
	}

	if (unlikely(!q_elem->flags.has_aggr)) {
		if (num_aggr)
			*num_aggr = 0;
		return 0;
	}

	const uint32_t num = q_elem->aggr_parent.num_aggr; /* <= EM_QUEUE_MAX_AGGR */
	const uint32_t num_out = MIN(num, max);

	if (num_aggr)
		*num_aggr = num;
	if (!aggr_info || num_out == 0)
		return 0;

	for (uint32_t i = 0; i < num_out; i++) {
		em_queue_t aggr_queue = (em_queue_t)(uintptr_t)q_elem->aggr_parent.aggr_queues[i];
		queue_elem_t *aggr_qelem = queue_elem_get(aggr_queue);

		if (unlikely(!aggr_qelem)) {
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_QUEUE_AGGR_INFO,
				       "Q:%" PRI_QUEUE " - Invalid aggr-Q:%" PRI_QUEUE ", idx:%u",
				       parent_queue, aggr_queue, i);
			return i;
		}

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(!queue_allocated(aggr_qelem) ||
			     aggr_qelem->type != EM_QUEUE_TYPE_AGGR ||
			     aggr_qelem->aggr.parent_queue != parent_queue)) {
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_QUEUE_AGGR_INFO,
				       "Q:%" PRI_QUEUE " - Invalid aggr-Q:%" PRI_QUEUE ", idx:%u\n"
				       "  aggr-Q:{type=%u, parent-Q=%" PRI_QUEUE "}",
				       parent_queue, aggr_queue, i,
				       aggr_qelem->type, aggr_qelem->aggr.parent_queue);
			return i;
		}

		aggr_info[i].queue = aggr_queue;
		aggr_info[i].pool = aggr_qelem->aggr.pool;
		aggr_info[i].max_tmo_ns = aggr_qelem->aggr.max_tmo_ns;
		aggr_info[i].max_size = aggr_qelem->aggr.max_size;
		aggr_info[i].event_type = aggr_qelem->aggr.event_type;
	}

	return num_out;
}

uint16_t em_queue_device_id(em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	return iq.device_id;
}

uint16_t em_queue_qid(em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	return iq.queue_id;
}

void em_queue_ids(em_queue_t queue, uint16_t *device_id /*out*/, uint16_t *qid /*out*/)
{
	internal_queue_t iq = {.queue = queue};

	if (likely(device_id))
		*device_id = iq.device_id;
	if (likely(qid))
		*qid = iq.queue_id;
}

em_queue_t em_queue_handle_raw(uint16_t device_id, uint16_t qid)
{
	internal_queue_t iq = {.device_id = device_id, .queue_id = qid};

	return iq.queue;
}

uint32_t em_queue_to_u32(em_queue_t queue)
{
	uint64_t queue_uptr = (uintptr_t)queue;
	uint32_t queue_u32 = (uint32_t)(queue_uptr & UINT32_MAX);

	return queue_u32;
}
