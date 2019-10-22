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

#define EM_Q_BASENAME  "EM_Q_"

static const em_queue_conf_t default_queue_conf = {
	.flags = EM_QUEUE_FLAG_DEFAULT,
	.min_events = 0, /* system default */
	.conf_len = 0, /* conf is ignored if this is 0 */
	.conf = NULL
};

static inline em_queue_t
queue_alloc(em_queue_t queue, const char **err_str);

static inline em_status_t
queue_free(em_queue_t queue);

static int
queue_setup_output(queue_elem_t *const q_elem, em_queue_prio_t prio,
		   const em_queue_conf_t *conf, const char **err_str);

static inline queue_elem_t *
queue_poolelem2queue(objpool_elem_t *const queue_pool_elem)
{
	return (queue_elem_t *)((uintptr_t)queue_pool_elem -
				offsetof(queue_elem_t, queue_pool_elem));
}

/**
 * Helper: initialize a queue pool (populate pool with q_elems)
 */
static int
queue_pool_init(queue_tbl_t *const queue_tbl,
		queue_pool_t *const queue_pool,
		int min_qidx, int max_qidx)
{
	const int cores = em_core_count();
	const int qs_per_pool = (max_qidx - min_qidx + 1);
	int qs_per_subpool = qs_per_pool / cores;
	int qs_leftover = qs_per_pool % cores;
	int subpool_idx = 0, add_cnt = 0;
	int i;

	if (objpool_init(&queue_pool->objpool, cores) != 0)
		return -1;

	for (i = min_qidx; i <= max_qidx; i++) {
		objpool_add(&queue_pool->objpool, subpool_idx,
			    &queue_tbl->queue_elem[i].queue_pool_elem);
		add_cnt++;
		if (add_cnt == qs_per_subpool + qs_leftover) {
			subpool_idx++; /* add to next subpool */
			qs_leftover = 0; /* added leftovers to subpool 0 */
			add_cnt = 0;
		}
	}

	return 0;
}

/**
 * Initialize the EM queues
 */
em_status_t
queue_init(queue_tbl_t *const queue_tbl,
	   queue_pool_t *const queue_pool,
	   queue_pool_t *const queue_pool_static)
{
	int i, min, max, ret;
	odp_queue_capability_t *const odp_queue_capa =
		&queue_tbl->odp_queue_capability;
	odp_schedule_capability_t *const odp_sched_capa =
		&queue_tbl->odp_schedule_capability;

	memset(queue_tbl, 0, sizeof(queue_tbl_t));
	memset(queue_pool, 0, sizeof(queue_pool_t));
	memset(queue_pool_static, 0, sizeof(queue_pool_t));
	env_atomic32_init(&em_shm->queue_count);

	/* Retieve and store the ODP queue capabilities into 'queue_tbl' */
	ret = odp_queue_capability(odp_queue_capa);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"odp_queue_capability():%d failed", ret);

	/* Retieve and store the ODP schedule capabilities into 'queue_tbl' */
	ret = odp_schedule_capability(odp_sched_capa);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"odp_schedule_capability():%d failed", ret);

	RETURN_ERROR_IF(odp_queue_capa->max_queues < EM_MAX_QUEUES,
			EM_ERR_TOO_LARGE, EM_ESCOPE_INIT,
			"EM_MAX_QUEUES:%i > odp-max-queues:%u",
			EM_MAX_QUEUES, odp_queue_capa->max_queues);

	/* Initialize the queue element table */
	for (i = 0; i < EM_MAX_QUEUES; i++)
		queue_tbl->queue_elem[i].queue = queue_idx2hdl(i);

	/* Initialize the static queue pool */
	min = queue_id2idx(EM_QUEUE_STATIC_MIN);
	max = queue_id2idx(LAST_INTERNAL_QUEUE);
	if (queue_pool_init(queue_tbl, queue_pool_static, min, max) != 0)
		return EM_ERR_LIB_FAILED;

	/* Initialize the dynamic queue pool */
	min = queue_id2idx(FIRST_DYN_QUEUE);
	max = queue_id2idx(LAST_DYN_QUEUE);
	if (queue_pool_init(queue_tbl, queue_pool, min, max) != 0)
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

/**
 * Queue inits done during EM core local init (once at startup on each core).
 *
 * Initialize event storage for queues of type 'EM_QUEUE_TYPE_LOCAL'.
 */
em_status_t
queue_init_local(void)
{
	int prio;
	int core;
	char name[20];
	odp_queue_param_t param;

	core = em_core_id();

	odp_queue_param_init(&param);
	param.type = ODP_QUEUE_TYPE_PLAIN;
	param.enq_mode = ODP_QUEUE_OP_MT_UNSAFE;
	param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;
	param.order = ODP_QUEUE_ORDER_IGNORE;
	param.size = 512;

	em_locm.local_queues.empty = 1;

	for (prio = 0; prio < EM_QUEUE_PRIO_NUM; prio++) {
		snprintf(name, sizeof(name),
			 "local-q:c%02d:prio%d", core, prio);
		name[sizeof(name) - 1] = '\0';

		em_locm.local_queues.prio[prio].empty_prio = 1;
		em_locm.local_queues.prio[prio].queue =
			odp_queue_create(name, &param);
		if (unlikely(em_locm.local_queues.prio[prio].queue ==
			     ODP_QUEUE_INVALID))
			return EM_ERR_ALLOC_FAILED;
	}

	memset(&em_locm.output_queue_track, 0,
	       sizeof(em_locm.output_queue_track));

	return EM_OK;
}

/**
 * Queue termination done during em_term_core().
 *
 * Flush & destroy event storage for queues of type 'EM_QUEUE_TYPE_LOCAL'.
 */
em_status_t
queue_term_local(void)
{
	int prio, ret;
	event_hdr_t *ev_hdr;
	em_status_t stat = EM_OK;

	/* flush all events */
	while ((ev_hdr = local_queue_dequeue()) != NULL)
		em_free(event_hdr_to_event(ev_hdr));

	for (prio = 0; prio < EM_QUEUE_PRIO_NUM; prio++) {
		ret = odp_queue_destroy(em_locm.local_queues.prio[prio].queue);
		if (unlikely(ret != 0))
			stat = EM_ERR_LIB_FAILED;
	}

	return stat;
}

/**
 * Allocate a new EM queue
 *
 * @param queue  EM queue handle if a specific EM queue is requested,
 *               EM_QUEUE_UNDEF if any EM queue will do.
 *
 * @return EM queue handle
 * @retval EM_QUEUE_UNDEF on failure
 */
static inline em_queue_t
queue_alloc(em_queue_t queue, const char **err_str)
{
	queue_elem_t *queue_elem;
	objpool_elem_t *queue_pool_elem;

	if (queue == EM_QUEUE_UNDEF) {
		/*
		 * Allocate a dynamic queue, i.e. take next available
		 */
		queue_pool_elem = objpool_rem(&em_shm->queue_pool.objpool,
					      em_core_id());
		if (unlikely(queue_pool_elem == NULL)) {
			*err_str = "queue pool element alloc failed!";
			return EM_QUEUE_UNDEF;
		}
		queue_elem = queue_poolelem2queue(queue_pool_elem);
	} else {
		/*
		 * Allocate a specific static-handle queue, handle given
		 */
		internal_queue_t iq;

		iq.queue = queue;
		if (iq.queue_id < EM_QUEUE_STATIC_MIN ||
		    iq.queue_id > LAST_INTERNAL_QUEUE) {
			*err_str = "queue handle not from static range!";
			return EM_QUEUE_UNDEF;
		}

		queue_elem = queue_elem_get(queue);
		if (unlikely(queue_elem == NULL)) {
			*err_str = "queue_elem ptr NULL!";
			return EM_QUEUE_UNDEF;
		}
		/* Verify that the queue is not allocated */
		if (queue_allocated(queue_elem)) {
			*err_str = "queue already allocated!";
			return EM_QUEUE_UNDEF;
		}
		/* Remove the queue from the pool */
		int ret = objpool_rem_elem(&em_shm->queue_pool_static.objpool,
					   &queue_elem->queue_pool_elem);
		if (unlikely(ret != 0)) {
			*err_str = "static queue pool element alloc failed!";
			return EM_QUEUE_UNDEF;
		}
	}

	env_atomic32_inc(&em_shm->queue_count);
	return queue_elem->queue;
}

static inline em_status_t
queue_free(em_queue_t queue)
{
	queue_elem_t *const queue_elem = queue_elem_get(queue);
	objpool_t *objpool;
	internal_queue_t iq;

	iq.queue = queue;

	if (unlikely(queue_elem == NULL))
		return EM_ERR_BAD_ID;

	if (iq.queue_id >= EM_QUEUE_STATIC_MIN &&
	    iq.queue_id <= LAST_INTERNAL_QUEUE)
		objpool = &em_shm->queue_pool_static.objpool;
	else
		objpool = &em_shm->queue_pool.objpool;

	queue_elem->state = EM_QUEUE_STATE_INVALID;

	objpool_add(objpool,
		    queue_elem->queue_pool_elem.subpool_idx,
		    &queue_elem->queue_pool_elem);

	env_atomic32_dec(&em_shm->queue_count);
	return EM_OK;
}

/**
 * Create an EM queue: alloc, setup and add to queue group list
 */
em_queue_t
queue_create(const char *name, em_queue_type_t type, em_queue_prio_t prio,
	     em_queue_group_t queue_group, em_queue_t queue_req,
	     em_atomic_group_t atomic_group, const em_queue_conf_t *conf,
	     const char **err_str)
{
	em_queue_t queue = EM_QUEUE_UNDEF;
	queue_elem_t *queue_elem = NULL;
	queue_group_elem_t *queue_group_elem = NULL;
	atomic_group_elem_t *ag_elem = NULL;
	int err;

	/* Use default EM queue conf if none given */
	if (conf == NULL)
		conf = &default_queue_conf;

	/* Argument checks per queue type & set queue_group_elem if used */
	switch (type) {
	case EM_QUEUE_TYPE_ATOMIC:
	case EM_QUEUE_TYPE_PARALLEL:
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
		/* EM scheduled queue */
		queue_group_elem = queue_group_elem_get(queue_group);
		/* scheduled queues are always associated with a queue group */
		if (unlikely(queue_group_elem == NULL ||
			     !queue_group_allocated(queue_group_elem))) {
			*err_str = "Invalid queue group!";
			return EM_QUEUE_UNDEF;
		}

		if (atomic_group != EM_ATOMIC_GROUP_UNDEF) {
			ag_elem = atomic_group_elem_get(atomic_group);
			if (unlikely(ag_elem == NULL ||
				     !atomic_group_allocated(ag_elem))) {
				*err_str = "Invalid atomic group!";
				return EM_QUEUE_UNDEF;
			}
		}

		break;

	case EM_QUEUE_TYPE_UNSCHEDULED:
		/* API arg checks for unscheduled queues */
		if (unlikely(prio != EM_QUEUE_PRIO_UNDEF)) {
			*err_str = "Invalid priority for unsched queue!";
			return EM_QUEUE_UNDEF;
		}
		if (unlikely(queue_group != EM_QUEUE_GROUP_UNDEF)) {
			*err_str = "Queue group not used with unsched queues!";
			return EM_QUEUE_UNDEF;
		}
		if (unlikely(atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
			*err_str = "Atomic group not used with unsched queues!";
			return EM_QUEUE_UNDEF;
		}
		break;

	case EM_QUEUE_TYPE_LOCAL:
		/* API arg checks for local queues */
		if (unlikely(queue_group != EM_QUEUE_GROUP_UNDEF)) {
			*err_str = "Invalid queue group for local queue!";
			return EM_QUEUE_UNDEF;
		}
		break;

	case EM_QUEUE_TYPE_OUTPUT:
		/* API arg checks for output queues */
		if (unlikely(queue_group != EM_QUEUE_GROUP_UNDEF)) {
			*err_str = "Invalid queue group for local queue!";
			return EM_QUEUE_UNDEF;
		}
		if (unlikely(conf == NULL ||
			     conf->conf_len < sizeof(em_output_queue_conf_t) ||
			     conf->conf == NULL)) {
			*err_str = "Invalid output queue conf";
			return EM_QUEUE_UNDEF;
		}
		break;

	default:
		*err_str = "Unknown queue type";
		return EM_QUEUE_UNDEF;
	}

	const char *alloc_err_str = "";

	queue = queue_alloc(queue_req, &alloc_err_str);
	if (unlikely(queue == EM_QUEUE_UNDEF)) {
		*err_str = alloc_err_str;
		return EM_QUEUE_UNDEF;
	}

	if (unlikely(queue_req != EM_QUEUE_UNDEF && queue_req != queue)) {
		queue_free(queue);
		*err_str = "Failed to allocate requested queue!";
		return EM_QUEUE_UNDEF;
	}

	queue_elem = queue_elem_get(queue);

	err = queue_setup(queue_elem, name, type, prio,
			  atomic_group, queue_group, conf, err_str);
	if (unlikely(err)) {
		queue_free(queue);
		/* 'err_str' set by queue_setup() */
		return EM_QUEUE_UNDEF;
	}

	/* scheduled queues only: add queue to a queue group list */
	if (queue_group_elem != NULL)
		queue_group_add_queue_list(queue_group_elem, queue_elem);

	return queue;
}

em_status_t
queue_delete(queue_elem_t *const queue_elem)
{
	queue_state_t old_state, new_state;
	em_status_t ret;
	em_queue_t queue = queue_elem->queue;
	em_queue_type_t type = queue_elem->type;

	if (unlikely(!queue_allocated(queue_elem)))
		return EM_ERR_BAD_STATE;

	old_state = queue_elem->state;
	new_state = EM_QUEUE_STATE_INVALID;

	if (type != EM_QUEUE_TYPE_UNSCHEDULED &&
	    type != EM_QUEUE_TYPE_OUTPUT) {
		/* verify scheduled queue state transition */
		ret = queue_state_change__check(old_state, new_state,
						0/*!is_setup*/);
		RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DELETE,
				"EM-Q:%" PRI_QUEUE " inv. state change:%d=>%d",
				queue, old_state, new_state);
	}

	if (type != EM_QUEUE_TYPE_UNSCHEDULED &&
	    type != EM_QUEUE_TYPE_LOCAL &&
	    type != EM_QUEUE_TYPE_OUTPUT) {
		queue_group_elem_t *const queue_group_elem =
			queue_group_elem_get(queue_elem->queue_group);
		/* Remove the queue from the queue group list */
		queue_group_rem_queue_list(queue_group_elem, queue_elem);
	}

	if (queue_elem->odp_queue != ODP_QUEUE_INVALID) {
		if (odp_queue_destroy(queue_elem->odp_queue) < 0)
			return EM_ERROR;
	}

	queue_elem->odp_queue = ODP_QUEUE_INVALID;

	/* output-queue: delete the fn-args storage if allocated in create */
	if (type == EM_QUEUE_TYPE_OUTPUT &&
	    queue_elem->output.output_fn_args_event != EM_EVENT_TYPE_UNDEF) {
		em_free(queue_elem->output.output_fn_args_event);
		queue_elem->output.output_fn_args_event = EM_EVENT_TYPE_UNDEF;
	}

	/* Zero queue name */
	em_shm->queue_tbl.name[queue_hdl2idx(queue)][0] = '\0';

	/* Remove the queue from the atomic group it belongs to, if any */
	atomic_group_remove_queue(queue_elem);

	return queue_free(queue);
}

/**
 * Setup an allocated/created queue before use.
 */
int
queue_setup(queue_elem_t *const q_elem, const char *name, em_queue_type_t type,
	    em_queue_prio_t prio, em_atomic_group_t atomic_group,
	    em_queue_group_t queue_group, const em_queue_conf_t *conf,
	    const char **err_str)
{
	const em_queue_t queue = q_elem->queue;
	em_queue_flag_t flags;
	char *const qname = &em_shm->queue_tbl.name[queue_hdl2idx(queue)][0];
	odp_queue_t odp_queue;
	odp_queue_param_t odp_queue_param;
	odp_queue_capability_t *odp_queue_capa;
	odp_schedule_capability_t *odp_sched_capa;
	int create_odp_queue = 1;
	int ret;

	q_elem->odp_queue = ODP_QUEUE_INVALID;
	q_elem->priority = prio;
	q_elem->type = type;
	q_elem->scheduled = EM_TRUE; /* override for local, output, unsched */
	q_elem->state = EM_QUEUE_STATE_INIT; /* override for unscheduled */
	q_elem->queue_group = queue_group;
	q_elem->atomic_group = atomic_group;
	q_elem->context = NULL;
	q_elem->eo = EM_EO_UNDEF;
	q_elem->eo_elem = NULL;
	q_elem->eo_ctx = NULL;
	q_elem->receive_func = NULL;

	if (name)
		strncpy(qname, name, EM_QUEUE_NAME_LEN);
	else /* default unique name: "EM_Q_" + Q-id = e.g. EM_Q_1234 */
		snprintf(qname, EM_QUEUE_NAME_LEN,
			 "%s%" PRI_QUEUE "", EM_Q_BASENAME, queue);
	qname[EM_QUEUE_NAME_LEN - 1] = '\0';

	odp_queue_capa = &em_shm->queue_tbl.odp_queue_capability;
	odp_sched_capa = &em_shm->queue_tbl.odp_schedule_capability;
	/* Init odp queue params to default values */
	odp_queue_param_init(&odp_queue_param);

	flags = conf->flags & EM_QUEUE_FLAG_MASK;
	if (unlikely(flags != EM_QUEUE_FLAG_DEFAULT)) {
		if (flags & EM_QUEUE_FLAG_NONBLOCKING_WF)
			odp_queue_param.nonblocking = ODP_NONBLOCKING_WF;
		else if (flags & EM_QUEUE_FLAG_NONBLOCKING_LF)
			odp_queue_param.nonblocking = ODP_NONBLOCKING_LF;

		if (flags & EM_QUEUE_FLAG_ENQ_NOT_MTSAFE)
			odp_queue_param.enq_mode = ODP_QUEUE_OP_MT_UNSAFE;
		if (flags & EM_QUEUE_FLAG_DEQ_NOT_MTSAFE)
			odp_queue_param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;
	}
	/* Set minimum queue size if other than 'default'(0) */
	if (conf->min_events != 0)
		odp_queue_param.size = conf->min_events;

	if (type == EM_QUEUE_TYPE_ATOMIC ||
	    type == EM_QUEUE_TYPE_PARALLEL ||
	    type == EM_QUEUE_TYPE_PARALLEL_ORDERED) {
		queue_group_elem_t *const qgrp_elem =
			queue_group_elem_get(queue_group);
		odp_schedule_sync_t odp_schedule_sync;
		odp_schedule_prio_t odp_prio;
		int err;

		err = scheduled_queue_type_em2odp(type, &odp_schedule_sync);
		if (unlikely(err != 0)) {
			*err_str = "Invalid queue type!";
			return -1;
		}

		err = prio_em2odp(prio, &odp_prio);
		if (unlikely(err != 0)) {
			*err_str = "Invalid queue priority!";
			return -2;
		}

		odp_queue_param.type = ODP_QUEUE_TYPE_SCHED;
		odp_queue_param.sched.prio = odp_prio;
		odp_queue_param.sched.sync = odp_schedule_sync;
		odp_queue_param.sched.group = qgrp_elem->odp_sched_group;
		/* set 'odp_queue_param.context = q_elem' after queue alloc */

		/* check nonblocking level against sched queue capabilities */
		if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
		    odp_sched_capa->lockfree_queues == ODP_SUPPORT_NO) {
			*err_str =
			"Non-blocking, lock-free sched queues unavailable";
			return -3;
		}
		if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
		    odp_sched_capa->waitfree_queues == ODP_SUPPORT_NO) {
			*err_str =
			"Non-blocking, wait-free sched queues unavailable";
			return -4;
		}
		if (odp_queue_param.enq_mode != ODP_QUEUE_OP_MT ||
		    odp_queue_param.deq_mode != ODP_QUEUE_OP_MT) {
			*err_str =
			"Invalid flag: scheduled queues must be MT-safe";
			return -5;
		}
	} else if (type == EM_QUEUE_TYPE_UNSCHEDULED) {
		odp_queue_param.type = ODP_QUEUE_TYPE_PLAIN;
		/* don't order events enqueued into unsched queues */
		odp_queue_param.order = ODP_QUEUE_ORDER_IGNORE;

		/* check nonblocking level against plain queue capabilities */
		if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
		    odp_queue_capa->plain.lockfree.max_num == 0) {
			*err_str =
			"Non-blocking, lock-free unsched queues unavailable";
			return -6;
		}
		if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
		    odp_queue_capa->plain.waitfree.max_num == 0) {
			*err_str =
			"Non-blocking, wait-free unsched queues unavailable";
			return -7;
		}
		/* Override: an unscheduled queue is not scheduled */
		q_elem->scheduled = EM_FALSE;
		/* Override the queue state for unscheduled queues */
		q_elem->state = EM_QUEUE_STATE_UNSCHEDULED;
	} else if (type == EM_QUEUE_TYPE_LOCAL) {
		/* odp queue NOT needed */
		create_odp_queue = 0;
		/* Override: a local queue is not scheduled */
		q_elem->scheduled = EM_FALSE;
	} else if (type == EM_QUEUE_TYPE_OUTPUT) {
		odp_queue_param.type = ODP_QUEUE_TYPE_PLAIN;

		/*
		 * Output-queues need an odp-queue to ensure re-ordering if
		 * events are sent into it from within an ordered context.
		 * Note: ODP_QUEUE_ORDER_KEEP is default for an odp queue.
		 */

		/* check nonblocking level against plain queue capabilities */
		if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
		    odp_queue_capa->plain.lockfree.max_num == 0) {
			*err_str =
			"Non-blocking, lock-free unsched queues unavailable";
			return -8;
		}
		if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
		    odp_queue_capa->plain.waitfree.max_num == 0) {
			*err_str =
			"Non-blocking, wait-free unsched queues unavailable";
			return -9;
		}

		/* output-queue dequeue protected by q_elem->output.lock */
		odp_queue_param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;

		ret = queue_setup_output(q_elem, prio, conf, err_str);
		if (unlikely(ret != 0))
			return -11;
	} else {
		*err_str = "Queue setup: unknown queue type";
		return -12;
	}

	if (create_odp_queue) {
		/*
		 * Note: The ODP queue context points to the EM queue elem.
		 * The EM queue context set by the user using the API function
		 * em_queue_set_context() is accessed through the EM queue elem:
		 * queue_elem->context.
		 */
		odp_queue_param.context = q_elem;

		odp_queue = odp_queue_create(qname, &odp_queue_param);
		if (unlikely(odp_queue == ODP_QUEUE_INVALID)) {
			queue_free(queue);
			*err_str = "odp queue creation failed!";
			return -13;
		}
		/* Store the corresponding ODP Queue */
		q_elem->odp_queue = odp_queue;
	}

	env_sync_mem();

	/* q_elem->static_allocated already set (for static queues) */
	return 0;
}

static int
queue_setup_output(queue_elem_t *const q_elem, em_queue_prio_t prio,
		   const em_queue_conf_t *conf, const char **err_str)
{
	em_output_queue_conf_t *const output_conf = conf->conf;

	/* Override: an output queue is not scheduled */
	q_elem->scheduled = EM_FALSE;
	/* Override the state for output queues, use unsched state */
	q_elem->state = EM_QUEUE_STATE_UNSCHEDULED;

	(void)prio; /* prio currently ignored for output */

	if (unlikely(output_conf->output_fn == NULL)) {
		*err_str = "output queue - invalid output function";
		return -1;
	}

	/* copy whole output conf */
	q_elem->output.output_conf = *output_conf;
	q_elem->output.output_fn_args_event = EM_EVENT_TYPE_UNDEF;
	if (output_conf->args_len == 0) {
		/* 'output_fn_args' is ignored, if 'args_len' is 0 */
		q_elem->output.output_conf.output_fn_args = NULL;
	} else {
		em_event_t args_event;
		void *args_storage;

		/* alloc an event to copy the given fn-args into */
		args_event = em_alloc(output_conf->args_len, EM_EVENT_TYPE_SW,
				      EM_POOL_DEFAULT);
		if (unlikely(args_event == EM_EVENT_TYPE_UNDEF)) {
			*err_str = "output queue - alloc output_fn_args fails";
			return -2;
		}
		/* store the event handle for em_free() later */
		q_elem->output.output_fn_args_event = args_event;
		args_storage = em_event_pointer(args_event);
		memcpy(args_storage, output_conf->output_fn_args,
		       output_conf->args_len);
		/* update the args ptr to point to the copied content */
		q_elem->output.output_conf.output_fn_args = args_storage;
	}
	env_spinlock_init(&q_elem->output.lock);

	return 0;
}

/**
 * Helper func for queue_state_change() - check that state change is valid
 *
 * Valid state transitions:
 * ---------------------------------
 * |         |new-state|new-state  |
 * |old_state|is_setup |is_teardown|
 * |---------|---------|-----------|
 * |INVALID  | INIT    | (NULL)    |
 * |INIT     | BIND    | INVALID   |
 * |BIND     | READY   | INIT      |
 * |READY    | (NULL)  | BIND      |
 * ---------------------------------
 * State change check is made easy because the following condition is true
 * for valid state transitions: abs(old-new)=1
 */
em_status_t
queue_state_change__check(queue_state_t old_state, queue_state_t new_state,
			  int is_setup /* vs. is_teardown */)
{
	uint32_t state_diff;

	if (is_setup)
		state_diff = new_state - old_state;
	else
		state_diff = old_state - new_state;

	return (state_diff == 1) ? EM_OK : EM_ERR_BAD_STATE;
}

static inline em_status_t
_queue_state_change(queue_elem_t *const q_elem, queue_state_t new_state)
{
	const queue_state_t old_state = q_elem->state;
	const int is_setup = (new_state == EM_QUEUE_STATE_READY);
	em_status_t err;

	/* allow multiple queue_enable/disable() calls */
	if (new_state == old_state &&
	    (new_state == EM_QUEUE_STATE_READY ||
	     new_state == EM_QUEUE_STATE_BIND))
		return EM_OK;

	err = queue_state_change__check(old_state, new_state, is_setup);
	if (unlikely(err != EM_OK))
		return err;

	q_elem->state = new_state;
	return EM_OK;
}

/**
 * Change the queue state
 */
em_status_t
queue_state_change(queue_elem_t *const q_elem, queue_state_t new_state)
{
	em_status_t err = _queue_state_change(q_elem, new_state);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_STATE_CHANGE,
			"EM-Q:%" PRI_QUEUE " inv. state: %d=>%d",
			q_elem->queue, q_elem->state, new_state);
	return EM_OK;
}

/**
 * Change the queue state for all queues associated with the given EO
 */
em_status_t
queue_state_change_all(eo_elem_t *const eo_elem, queue_state_t new_state)
{
	em_status_t err = EM_OK;
	queue_elem_t *q_elem;
	list_node_t *pos;
	list_node_t *list_node;

	/*
	 * Loop through all queues associated with the EO, no need for
	 * eo_elem-lock since this is called only on single core at the
	 * end of em_eo_start()
	 */
	env_spinlock_lock(&eo_elem->lock);

	list_for_each(&eo_elem->queue_list, pos, list_node) {
		q_elem = list_node_to_queue_elem(list_node);
		err = _queue_state_change(q_elem, new_state);
		if (unlikely(err != EM_OK))
			break;
	} /* end loop */

	env_spinlock_unlock(&eo_elem->lock);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_STATE_CHANGE,
			"EM-Q:%" PRI_QUEUE " inv. state: %d=>%d",
			q_elem->queue, q_elem->state, new_state);
	return EM_OK;
}

/**
 * Enable event reception of an EM queue
 */
em_status_t
queue_enable(queue_elem_t *const q_elem)
{
	em_status_t ret;

	RETURN_ERROR_IF(q_elem == NULL || !queue_allocated(q_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_ENABLE,
			"Invalid queue");

	ret = queue_state_change(q_elem, EM_QUEUE_STATE_READY);

	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_ENABLE,
			"queue_state_change()->READY fails EM-Q:%" PRI_QUEUE "",
			q_elem->queue);

	return EM_OK;
}

/**
 * Enable event reception of ALL queues belonging to an EO
 */
em_status_t
queue_enable_all(eo_elem_t *const eo_elem)
{
	em_status_t ret;

	RETURN_ERROR_IF(eo_elem == NULL || !eo_allocated(eo_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_ENABLE_ALL,
			"Invalid EO");

	ret = queue_state_change_all(eo_elem, EM_QUEUE_STATE_READY);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_ENABLE_ALL,
			"queue_state_change_all()->READY fails EO:%" PRI_EO "",
			eo_elem->eo);

	return EM_OK;
}

/**
 * Disable event reception of an EM queue
 */
em_status_t
queue_disable(queue_elem_t *const q_elem)
{
	em_status_t ret;

	RETURN_ERROR_IF(q_elem == NULL || !queue_allocated(q_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_DISABLE,
			"Invalid queue");

	/* Change the state of the queue */
	ret = queue_state_change(q_elem, EM_QUEUE_STATE_BIND);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE,
			"queue_state_change()->BIND fails, Q:%" PRI_QUEUE "",
			q_elem->queue);

	return EM_OK;
}

/**
 * Disable event reception of ALL queues belonging to an EO
 */
em_status_t
queue_disable_all(eo_elem_t *const eo_elem)
{
	em_status_t ret;

	RETURN_ERROR_IF(eo_elem == NULL || !eo_allocated(eo_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_DISABLE_ALL,
			"Invalid EO");

	ret = queue_state_change_all(eo_elem, EM_QUEUE_STATE_BIND);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE_ALL,
			"queue_state_change_all()->BIND: EO:%" PRI_EO "",
			eo_elem->eo);

	return EM_OK;
}

void
print_queue_info(void)
{
	odp_queue_capability_t *const queue_capa =
		&em_shm->queue_tbl.odp_queue_capability;
	odp_schedule_capability_t *const sched_capa =
		&em_shm->queue_tbl.odp_schedule_capability;
	char plain_sz[8] = "n/a";
	char plain_lf_sz[8] = "n/a", plain_wf_sz[8] = "n/a";
	char sched_sz[8] = "nolimit";

	if (queue_capa->plain.max_size > 0)
		snprintf(plain_sz, sizeof(plain_sz), "%u",
			 queue_capa->plain.max_size);
	if (queue_capa->plain.lockfree.max_size > 0)
		snprintf(plain_lf_sz, sizeof(plain_lf_sz), "%u",
			 queue_capa->plain.lockfree.max_size);
	if (queue_capa->plain.waitfree.max_size > 0)
		snprintf(plain_wf_sz, sizeof(plain_wf_sz), "%u",
			 queue_capa->plain.waitfree.max_size);

	if (sched_capa->max_queue_size > 0)
		snprintf(sched_sz, sizeof(sched_sz), "%u",
			 sched_capa->max_queue_size);

	plain_sz[sizeof(plain_sz) - 1] = '\0';
	plain_lf_sz[sizeof(plain_lf_sz) - 1] = '\0';
	plain_wf_sz[sizeof(plain_wf_sz) - 1] = '\0';
	sched_sz[sizeof(sched_sz) - 1] = '\0';

	EM_PRINT("ODP Queue Capabilities\n"
		 "----------------------\n"
		 "  Max number of ODP queues: %u\n"
		 "  Max number of ODP ordered locks per queue: %u\n"
		 "  Max number of ODP scheduling groups: %u\n"
		 "  Max number of ODP scheduling priorities: %u\n"
		 "    PLAIN queues:\n"
		 "        blocking:       count: %6u   size: %6s\n"
		 "        nonblocking-lf: count: %6u   size: %6s\n"
		 "        nonblocking-wf: count: %6u   size: %6s\n"
		 "    SCHED queues:\n"
		 "        blocking:       count: %6u   size: %6s\n"
		 "        nonblocking-lf: %ssupported\n"
		 "        nonblocking-wf: %ssupported\n\n",
		 queue_capa->max_queues, sched_capa->max_ordered_locks,
		 sched_capa->max_groups, sched_capa->max_prios,
		 queue_capa->plain.max_num, plain_sz,
		 queue_capa->plain.lockfree.max_num, plain_lf_sz,
		 queue_capa->plain.waitfree.max_num, plain_wf_sz,
		 sched_capa->max_queues, sched_sz,
		 sched_capa->lockfree_queues == ODP_SUPPORT_NO ? "not " : "",
		 sched_capa->waitfree_queues == ODP_SUPPORT_NO ? "not " : "");

	EM_PRINT("EM Queues\n"
		 "---------\n"
		 "  Max number of EM queues: %d (0x%x)\n"
		 "  EM queue handle offset: %d (0x%x)\n"
		 "  EM queue range:   [%d - %d] ([0x%x - 0x%x])\n"
		 "    static range:   [%d - %d] ([0x%x - 0x%x])\n"
		 "    internal range: [%d - %d] ([0x%x - 0x%x])\n"
		 "    dynamic range:  [%d - %d] ([0x%x - 0x%x])\n"
		 "\n",
		 EM_MAX_QUEUES, EM_MAX_QUEUES,
		 EM_QUEUE_RANGE_OFFSET, EM_QUEUE_RANGE_OFFSET,
		 EM_QUEUE_STATIC_MIN, LAST_DYN_QUEUE,
		 EM_QUEUE_STATIC_MIN, LAST_DYN_QUEUE,
		 EM_QUEUE_STATIC_MIN, EM_QUEUE_STATIC_MAX,
		 EM_QUEUE_STATIC_MIN, EM_QUEUE_STATIC_MAX,
		 FIRST_INTERNAL_QUEUE, LAST_INTERNAL_QUEUE,
		 FIRST_INTERNAL_QUEUE, LAST_INTERNAL_QUEUE,
		 FIRST_DYN_QUEUE, LAST_DYN_QUEUE,
		 FIRST_DYN_QUEUE, LAST_DYN_QUEUE);
}

unsigned int
queue_count(void)
{
	return env_atomic32_get(&em_shm->queue_count);
}
