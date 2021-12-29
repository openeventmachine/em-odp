/*
 *   Copyright (c) 2015-2021, Nokia Solutions and Networks
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

/**
 * Queue create-params passed to queue_setup...()
 */
typedef struct {
	const char *name;
	em_queue_type_t type;
	em_queue_prio_t prio;
	em_atomic_group_t atomic_group;
	em_queue_group_t queue_group;
	const em_queue_conf_t *conf;
} queue_setup_t;

/**
 * Default queue create conf to use if not provided by the user
 */
static const em_queue_conf_t default_queue_conf = {
	.flags = EM_QUEUE_FLAG_DEFAULT,
	.min_events = 0, /* use EM default value */
	.conf_len = 0, /* .conf is ignored if this is 0 */
	.conf = NULL
};

static int
queue_init_prio_map(int minp, int maxp, int nump);
static int
queue_init_prio_legacy(int minp, int maxp);
static void
queue_init_prio_adaptive(int minp, int maxp, int nump);
static int
queue_init_prio_custom(int minp, int maxp);

static inline int
queue_create_check_sched(const queue_setup_t *setup, const char **err_str);

static inline em_queue_t
queue_alloc(em_queue_t queue, const char **err_str);

static inline em_status_t
queue_free(em_queue_t queue);

static int
queue_setup(queue_elem_t *q_elem, const queue_setup_t *setup,
	    const char **err_str);
static void
queue_setup_common(queue_elem_t *q_elem, const queue_setup_t *setup);
static void
queue_setup_odp_common(const queue_setup_t *setup,
		       odp_queue_param_t *odp_queue_param);
static int
queue_setup_scheduled(queue_elem_t *q_elem, const queue_setup_t *setup,
		      const char **err_str);
static int
queue_setup_unscheduled(queue_elem_t *q_elem, const queue_setup_t *setup,
			const char **err_str);
static int
queue_setup_local(queue_elem_t *q_elem, const queue_setup_t *setup,
		  const char **err_str);
static int
queue_setup_output(queue_elem_t *q_elem, const queue_setup_t *setup,
		   const char **err_str);

static inline queue_elem_t *
queue_poolelem2queue(objpool_elem_t *const queue_pool_elem)
{
	return (queue_elem_t *)((uintptr_t)queue_pool_elem -
				offsetof(queue_elem_t, queue_pool_elem));
}

static int
read_config_file(void)
{
	const char *conf_str;
	int val = 0;
	int ret;

	EM_PRINT("EM-queue config:\n");

	/*
	 * Option: queue.min_events_default
	 */
	conf_str = "queue.min_events_default";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}
	if (val < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n",
		       conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.queue.min_events_default = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	/*
	 * Option: queue.prio_map_mode
	 */
	conf_str = "queue.priority.map_mode";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	if (val < 0 || val > 2) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n", conf_str, val);
		return -1;
	}
	em_shm->opt.queue.priority.map_mode = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	if (val == 2) { /* custom map */
		conf_str = "queue.priority.custom_map";
		ret = em_libconfig_lookup_array(&em_shm->libconfig, conf_str,
						em_shm->opt.queue.priority.custom_map,
						EM_QUEUE_PRIO_NUM);
		if (unlikely(!ret)) {
			EM_LOG(EM_LOG_ERR, "Config option '%s' not found or invalid\n", conf_str);
			return -1;
		}
		EM_PRINT("  %s: [", conf_str);
		for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
			EM_PRINT("%d", em_shm->opt.queue.priority.custom_map[i]);
			if (i < (EM_QUEUE_PRIO_NUM - 1))
				EM_PRINT(",");
		}
		EM_PRINT("]\n");
	}
	return 0;
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
	int subpool_idx = 0;
	int add_cnt = 0;
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
	odp_queue_capability_t *const odp_queue_capa =
		&queue_tbl->odp_queue_capability;
	odp_schedule_capability_t *const odp_sched_capa =
		&queue_tbl->odp_schedule_capability;
	int min;
	int max;
	int ret;

	memset(queue_tbl, 0, sizeof(queue_tbl_t));
	memset(queue_pool, 0, sizeof(queue_pool_t));
	memset(queue_pool_static, 0, sizeof(queue_pool_t));
	env_atomic32_init(&em_shm->queue_count);

	if (read_config_file())
		return EM_ERR_LIB_FAILED;

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
	for (int i = 0; i < EM_MAX_QUEUES; i++)
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

	/* Initialize priority mapping, adapt to values from ODP */
	min = odp_schedule_min_prio();
	max = odp_schedule_max_prio();
	em_shm->queue_prio.num_runtime = max - min + 1;
	ret = queue_init_prio_map(min, max, em_shm->queue_prio.num_runtime);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"mapping odp priorities failed: %d", ret);
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
	em_locm_t *const locm = &em_locm;

	core = em_core_id();

	odp_queue_param_init(&param);
	param.type = ODP_QUEUE_TYPE_PLAIN;
	param.enq_mode = ODP_QUEUE_OP_MT_UNSAFE;
	param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;
	param.order = ODP_QUEUE_ORDER_IGNORE;
	param.size = 512;

	locm->local_queues.empty = 1;

	for (prio = 0; prio < EM_QUEUE_PRIO_NUM; prio++) {
		snprintf(name, sizeof(name),
			 "local-q:c%02d:prio%d", core, prio);
		name[sizeof(name) - 1] = '\0';

		locm->local_queues.prio[prio].empty_prio = 1;
		locm->local_queues.prio[prio].queue =
			odp_queue_create(name, &param);
		if (unlikely(locm->local_queues.prio[prio].queue ==
			     ODP_QUEUE_INVALID))
			return EM_ERR_ALLOC_FAILED;
	}

	memset(&locm->output_queue_track, 0,
	       sizeof(locm->output_queue_track));

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
	em_event_t event;
	event_hdr_t *ev_hdr;
	em_status_t stat = EM_OK;
	int ret;
	bool esv_ena = esv_enabled();

	/* flush all events */
	while ((ev_hdr = local_queue_dequeue()) != NULL) {
		event = event_hdr_to_event(ev_hdr);
		if (esv_ena)
			event = evstate_em2usr(event, ev_hdr,
					       EVSTATE__TERM_CORE__QUEUE_LOCAL);
		em_free(event);
	}

	for (int prio = 0; prio < EM_QUEUE_PRIO_NUM; prio++) {
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

static int
queue_create_check_sched(const queue_setup_t *setup, const char **err_str)
{
	const queue_group_elem_t *queue_group_elem = NULL;
	const atomic_group_elem_t *ag_elem = NULL;

	queue_group_elem = queue_group_elem_get(setup->queue_group);
	/* scheduled queues are always associated with a queue group */
	if (unlikely(queue_group_elem == NULL || !queue_group_allocated(queue_group_elem))) {
		*err_str = "Invalid queue group!";
		return -1;
	}

	if (setup->atomic_group != EM_ATOMIC_GROUP_UNDEF) {
		ag_elem = atomic_group_elem_get(setup->atomic_group);
		if (unlikely(ag_elem == NULL || !atomic_group_allocated(ag_elem))) {
			*err_str = "Invalid atomic group!";
			return -1;
		}
	}

	if (unlikely(setup->prio >= EM_QUEUE_PRIO_NUM)) {
		*err_str = "Invalid queue priority!";
		return -1;
	}
	return 0;
}

static int
queue_create_check_args(const queue_setup_t *setup, const char **err_str)
{
	/* scheduled queue */
	if (setup->type == EM_QUEUE_TYPE_ATOMIC   ||
	    setup->type == EM_QUEUE_TYPE_PARALLEL ||
	    setup->type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
		return queue_create_check_sched(setup, err_str);

	/* other queue types */
	switch (setup->type) {
	case EM_QUEUE_TYPE_UNSCHEDULED:
		/* API arg checks for unscheduled queues */
		if (unlikely(setup->prio != EM_QUEUE_PRIO_UNDEF)) {
			*err_str = "Invalid priority for unsched queue!";
			return -1;
		}
		if (unlikely(setup->queue_group != EM_QUEUE_GROUP_UNDEF)) {
			*err_str = "Queue group not used with unsched queues!";
			return -1;
		}
		if (unlikely(setup->atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
			*err_str = "Atomic group not used with unsched queues!";
			return -1;
		}
		break;

	case EM_QUEUE_TYPE_LOCAL:
		/* API arg checks for local queues */
		if (unlikely(setup->queue_group != EM_QUEUE_GROUP_UNDEF)) {
			*err_str = "Queue group not used with local queues!";
			return -1;
		}
		if (unlikely(setup->atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
			*err_str = "Atomic group not used with local queues!";
			return -1;
		}
		if (unlikely(setup->prio >= EM_QUEUE_PRIO_NUM)) {
			*err_str = "Invalid queue priority!";
			return -1;
		}
		break;

	case EM_QUEUE_TYPE_OUTPUT:
		/* API arg checks for output queues */
		if (unlikely(setup->queue_group != EM_QUEUE_GROUP_UNDEF)) {
			*err_str = "Queue group not used with output queues!";
			return -1;
		}
		if (unlikely(setup->atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
			*err_str = "Atomic group not used with output queues!";
			return -1;
		}
		if (unlikely(setup->conf == NULL ||
			     setup->conf->conf_len < sizeof(em_output_queue_conf_t) ||
			     setup->conf->conf == NULL)) {
			*err_str = "Invalid output queue conf";
			return -1;
		}
		break;

	default:
		*err_str = "Unknown queue type";
		return -1;
	}

	return 0;
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
	int err;

	/* Use default EM queue conf if none given */
	if (conf == NULL)
		conf = &default_queue_conf;

	queue_setup_t setup = {.name = name, .type = type, .prio = prio,
			       .atomic_group = atomic_group,
			       .queue_group = queue_group, .conf = conf};

	err = queue_create_check_args(&setup, err_str);
	if (err) {
		/* 'err_str' set by queue_create_check_args() */
		return EM_QUEUE_UNDEF;
	}

	/*
	 * Allocate the queue handle and obtain the corresponding queue-element
	 */
	const char *alloc_err_str = "";

	em_queue_t queue = queue_alloc(queue_req, &alloc_err_str);

	if (unlikely(queue == EM_QUEUE_UNDEF)) {
		*err_str = alloc_err_str;
		return EM_QUEUE_UNDEF;
	}
	if (unlikely(queue_req != EM_QUEUE_UNDEF && queue_req != queue)) {
		queue_free(queue);
		*err_str = "Failed to allocate the requested queue!";
		return EM_QUEUE_UNDEF;
	}

	queue_elem_t *queue_elem = queue_elem_get(queue);

	if (unlikely(!queue_elem)) {
		queue_free(queue);
		*err_str = "Queue elem NULL!";
		return EM_QUEUE_UNDEF;
	}

	/*
	 * Setup/configure the queue
	 */
	err = queue_setup(queue_elem, &setup, err_str);
	if (unlikely(err)) {
		queue_free(queue);
		/* 'err_str' set by queue_setup() */
		return EM_QUEUE_UNDEF;
	}

	return queue;
}

em_status_t
queue_delete(queue_elem_t *const queue_elem)
{
	queue_state_t old_state;
	queue_state_t new_state;
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

		RETURN_ERROR_IF(queue_group_elem == NULL ||
				!queue_group_allocated(queue_group_elem),
				EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_DELETE,
				"Invalid queue group: %" PRI_QGRP "",
				queue_elem->queue_group);

		/* Remove the queue from the queue group list */
		queue_group_rem_queue_list(queue_group_elem, queue_elem);
	}

	if (type == EM_QUEUE_TYPE_OUTPUT) {
		env_spinlock_t *const lock = &queue_elem->output.lock;
		q_elem_output_t *const q_out = &queue_elem->output;

		env_spinlock_lock(lock);
		/* Drain any remaining events from the output queue */
		output_queue_drain(queue_elem);
		env_spinlock_unlock(lock);

		/* delete the fn-args storage if allocated in create */
		if (q_out->output_fn_args_event != EM_EVENT_UNDEF) {
			em_free(q_out->output_fn_args_event);
			q_out->output_fn_args_event = EM_EVENT_UNDEF;
		}
	}

	if (queue_elem->odp_queue != ODP_QUEUE_INVALID) {
		int err = odp_queue_destroy(queue_elem->odp_queue);

		RETURN_ERROR_IF(err, EM_ERR_LIB_FAILED, EM_ESCOPE_QUEUE_DELETE,
				"EM-Q:%" PRI_QUEUE ":odp_queue_destroy(" PRIu64 "):%d",
				queue, odp_queue_to_u64(queue_elem->odp_queue),
				err);
	}

	queue_elem->odp_queue = ODP_QUEUE_INVALID;

	/* Zero queue name */
	em_shm->queue_tbl.name[queue_hdl2idx(queue)][0] = '\0';

	/* Remove the queue from the atomic group it belongs to, if any */
	atomic_group_remove_queue(queue_elem);

	return queue_free(queue);
}

/**
 * Setup an allocated/created queue before use.
 */
static int
queue_setup(queue_elem_t *q_elem, const queue_setup_t *setup,
	    const char **err_str)
{
	int ret;

	/* Set common queue-elem fields based on setup */
	queue_setup_common(q_elem, setup);

	switch (setup->type) {
	case EM_QUEUE_TYPE_ATOMIC: /* fallthrough */
	case EM_QUEUE_TYPE_PARALLEL: /* fallthrough */
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
		ret = queue_setup_scheduled(q_elem, setup, err_str);
		break;
	case EM_QUEUE_TYPE_UNSCHEDULED:
		ret = queue_setup_unscheduled(q_elem, setup, err_str);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		ret = queue_setup_local(q_elem, setup, err_str);
		break;
	case EM_QUEUE_TYPE_OUTPUT:
		ret = queue_setup_output(q_elem, setup, err_str);
		break;
	default:
		*err_str = "Queue setup: unknown queue type";
		ret = -1;
		break;
	}

	if (unlikely(ret))
		return -1;

	env_sync_mem();
	return 0;
}

/**
 * Helper function to queue_setup()
 *
 * Set EM queue params common to all EM queues based on EM config
 */
static void
queue_setup_common(queue_elem_t *q_elem /*out*/, const queue_setup_t *setup)
{
	const em_queue_t queue = q_elem->queue;
	char *const qname = &em_shm->queue_tbl.name[queue_hdl2idx(queue)][0];

	/* Store queue name */
	if (setup->name)
		strncpy(qname, setup->name, EM_QUEUE_NAME_LEN);
	else /* default unique name: "EM_Q_" + Q-id = e.g. EM_Q_1234 */
		snprintf(qname, EM_QUEUE_NAME_LEN,
			 "%s%" PRI_QUEUE "", EM_Q_BASENAME, queue);
	qname[EM_QUEUE_NAME_LEN - 1] = '\0';

	/* Init q_elem fields based on setup params and clear the rest */
	q_elem->type = setup->type;
	q_elem->priority = setup->prio;
	q_elem->queue_group = setup->queue_group;
	q_elem->atomic_group = setup->atomic_group;
	/* q_elem->conf = not stored, configured later */

	/* Clear the rest */
	q_elem->odp_queue = ODP_QUEUE_INVALID;
	q_elem->scheduled = EM_FALSE;
	q_elem->state = EM_QUEUE_STATE_INVALID;
	q_elem->context = NULL;
	q_elem->eo = EM_EO_UNDEF;
	q_elem->eo_elem = NULL;
	q_elem->eo_ctx = NULL;
	q_elem->use_multi_rcv = 0;
	q_elem->max_events = 0;
	q_elem->receive_func = NULL;
	q_elem->receive_multi_func = NULL;
}

/**
 * Helper function to queue_setup_...()
 *
 * Set common ODP queue params based on EM config
 */
static void
queue_setup_odp_common(const queue_setup_t *setup,
		       odp_queue_param_t *odp_queue_param /*out*/)
{
	/*
	 * Set ODP queue params according to EM queue conf flags
	 */
	const em_queue_conf_t *conf = setup->conf;
	em_queue_flag_t flags = conf->flags & EM_QUEUE_FLAG_MASK;

	if (flags != EM_QUEUE_FLAG_DEFAULT) {
		if (flags & EM_QUEUE_FLAG_NONBLOCKING_WF)
			odp_queue_param->nonblocking = ODP_NONBLOCKING_WF;
		else if (flags & EM_QUEUE_FLAG_NONBLOCKING_LF)
			odp_queue_param->nonblocking = ODP_NONBLOCKING_LF;

		if (flags & EM_QUEUE_FLAG_ENQ_NOT_MTSAFE)
			odp_queue_param->enq_mode = ODP_QUEUE_OP_MT_UNSAFE;
		if (flags & EM_QUEUE_FLAG_DEQ_NOT_MTSAFE)
			odp_queue_param->deq_mode = ODP_QUEUE_OP_MT_UNSAFE;
	}

	/*
	 * Set minimum queue size if other than 'default'(0)
	 */
	if (conf->min_events == 0) {
		/* use EM default value from config file: */
		unsigned int size = em_shm->opt.queue.min_events_default;

		if (size != 0)
			odp_queue_param->size = size;
		/* else: use odp default as set by odp_queue_param_init() */
	} else {
		/* use user provided value: */
		odp_queue_param->size = conf->min_events;
	}
}

/**
 * Create an ODP queue for the newly created EM queue
 */
static int create_odp_queue(queue_elem_t *q_elem,
			    const odp_queue_param_t *odp_queue_param)
{
	char odp_name[ODP_QUEUE_NAME_LEN];
	odp_queue_t odp_queue;

	(void)queue_get_name(q_elem, odp_name/*out*/, sizeof(odp_name));

	odp_queue = odp_queue_create(odp_name, odp_queue_param);
	if (unlikely(odp_queue == ODP_QUEUE_INVALID))
		return -1;

	/* Store the corresponding ODP Queue */
	q_elem->odp_queue = odp_queue;

	return 0;
}

/**
 * Helper function to queue_setup()
 *
 * Set EM and ODP queue params for scheduled queues
 */
static int
queue_setup_scheduled(queue_elem_t *q_elem /*in,out*/,
		      const queue_setup_t *setup, const char **err_str)
{
	/* validity checks done earlier for queue_group */
	queue_group_elem_t *qgrp_elem = queue_group_elem_get(setup->queue_group);
	int err;

	if (unlikely(qgrp_elem == NULL)) {
		*err_str = "Q-setup-sched: invalid queue group!";
		return -1;
	}

	q_elem->priority = setup->prio;
	q_elem->type = setup->type;
	q_elem->queue_group = setup->queue_group;
	q_elem->atomic_group = setup->atomic_group;

	q_elem->scheduled = EM_TRUE;
	q_elem->state = EM_QUEUE_STATE_INIT;

	/*
	 * Set up a scheduled ODP queue for the EM scheduled queue
	 */
	odp_queue_param_t odp_queue_param;
	odp_schedule_sync_t odp_schedule_sync;
	odp_schedule_prio_t odp_prio;

	/* Init odp queue params to default values */
	odp_queue_param_init(&odp_queue_param);
	/* Set common ODP queue params based on the EM Queue config */
	queue_setup_odp_common(setup, &odp_queue_param /*out*/);

	err = scheduled_queue_type_em2odp(setup->type,
					  &odp_schedule_sync /*out*/);
	if (unlikely(err)) {
		*err_str = "Q-setup-sched: invalid queue type!";
		return -2;
	}

	err = prio_em2odp(setup->prio, &odp_prio /*out*/);
	if (unlikely(err)) {
		*err_str = "Q-setup-sched: invalid queue priority!";
		return -3;
	}

	odp_queue_param.type = ODP_QUEUE_TYPE_SCHED;
	odp_queue_param.sched.prio = odp_prio;
	odp_queue_param.sched.sync = odp_schedule_sync;
	odp_queue_param.sched.group = qgrp_elem->odp_sched_group;

	/* Retrieve previously stored ODP scheduler capabilities */
	const odp_schedule_capability_t *odp_sched_capa =
		&em_shm->queue_tbl.odp_schedule_capability;

	/*
	 * Check nonblocking level against sched queue capabilities.
	 * Related ODP queue params set earlier in queue_setup_common().
	 */
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
	    odp_sched_capa->lockfree_queues == ODP_SUPPORT_NO) {
		*err_str = "Q-setup-sched: non-blocking, lock-free sched queues unavailable";
		return -4;
	}
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
	    odp_sched_capa->waitfree_queues == ODP_SUPPORT_NO) {
		*err_str = "Q-setup-sched: non-blocking, wait-free sched queues unavailable";
		return -5;
	}
	if (odp_queue_param.enq_mode != ODP_QUEUE_OP_MT ||
	    odp_queue_param.deq_mode != ODP_QUEUE_OP_MT) {
		*err_str = "Q-setup-sched: invalid flag: scheduled queues must be MT-safe";
		return -6;
	}

	/*
	 * Note: The ODP queue context points to the EM queue elem.
	 * The EM queue context set by the user using the API function
	 * em_queue_set_context() is accessed through the queue_elem_t::context
	 * and retrieved with em_queue_get_context() or passed by EM to the
	 * EO-receive function for scheduled queues.
	 */
	odp_queue_param.context = q_elem;
	/*
	 * Set the context data length (in bytes) for potential prefetching.
	 * The ODP implementation may use this value as a hint for the number
	 * of context data bytes to prefetch.
	 */
	odp_queue_param.context_len = sizeof(*q_elem);

	err = create_odp_queue(q_elem, &odp_queue_param);
	if (unlikely(err)) {
		*err_str = "Q-setup-sched: scheduled odp queue creation failed!";
		return -7;
	}

	/*
	 * Add the scheduled queue to the queue group
	 */
	queue_group_add_queue_list(qgrp_elem, q_elem);

	return 0;
}

/*
 * Helper function to queue_setup()
 *
 * Set EM and ODP queue params for unscheduled queues
 */
static int
queue_setup_unscheduled(queue_elem_t *q_elem /*in,out*/,
			const queue_setup_t *setup, const char **err_str)
{
	q_elem->priority = EM_QUEUE_PRIO_UNDEF;
	q_elem->type = EM_QUEUE_TYPE_UNSCHEDULED;
	q_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	q_elem->atomic_group = EM_ATOMIC_GROUP_UNDEF;
	/* unscheduled queues are not scheduled */
	q_elem->scheduled = EM_FALSE;
	q_elem->state = EM_QUEUE_STATE_UNSCHEDULED;

	/*
	 * Set up a plain ODP queue for the EM unscheduled queue.
	 */
	odp_queue_param_t odp_queue_param;
	/* Retrieve previously stored ODP queue capabilities */
	const odp_queue_capability_t *odp_queue_capa =
		&em_shm->queue_tbl.odp_queue_capability;

	/* Init odp queue params to default values */
	odp_queue_param_init(&odp_queue_param);
	/* Set common ODP queue params based on the EM Queue config */
	queue_setup_odp_common(setup, &odp_queue_param);

	odp_queue_param.type = ODP_QUEUE_TYPE_PLAIN;
	/* don't order events enqueued into unsched queues */
	odp_queue_param.order = ODP_QUEUE_ORDER_IGNORE;

	/*
	 * Check nonblocking level against plain queue capabilities.
	 * Related ODP queue params set earlier in queue_setup_common().
	 */
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
	    odp_queue_capa->plain.lockfree.max_num == 0) {
		*err_str = "Q-setup-unsched: non-blocking, lock-free unsched queues unavailable";
		return -1;
	}
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
	    odp_queue_capa->plain.waitfree.max_num == 0) {
		*err_str = "Q-setup-unsched: non-blocking, wait-free unsched queues unavailable";
		return -2;
	}

	/*
	 * Note: The ODP queue context points to the EM queue elem.
	 * The EM queue context set by the user using the API function
	 * em_queue_set_context() is accessed through the queue_elem_t::context
	 * and retrieved with em_queue_get_context().
	 */
	odp_queue_param.context = q_elem;

	int err = create_odp_queue(q_elem, &odp_queue_param);

	if (unlikely(err)) {
		*err_str = "Q-setup-unsched: plain odp queue creation failed!";
		return -3;
	}

	return 0;
}

/*
 * Helper function to queue_setup()
 *
 * Set EM queue params for (core-)local queues
 */
static int
queue_setup_local(queue_elem_t *q_elem, const queue_setup_t *setup,
		  const char **err_str)
{
	(void)err_str;

	q_elem->priority = setup->prio;
	q_elem->type = EM_QUEUE_TYPE_LOCAL;
	q_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	q_elem->atomic_group = EM_ATOMIC_GROUP_UNDEF;
	/* local queues are not scheduled */
	q_elem->scheduled = EM_FALSE;
	q_elem->state = EM_QUEUE_STATE_INIT;

	return 0;
}

/*
 * Helper function to queue_setup()
 *
 * Set EM queue params for output queues
 */
static int
queue_setup_output(queue_elem_t *q_elem, const queue_setup_t *setup,
		   const char **err_str)
{
	const em_queue_conf_t *qconf = setup->conf;
	const em_output_queue_conf_t *output_conf = qconf->conf;

	q_elem->priority = EM_QUEUE_PRIO_UNDEF;
	q_elem->type = EM_QUEUE_TYPE_OUTPUT;
	q_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	q_elem->atomic_group = EM_ATOMIC_GROUP_UNDEF;
	/* output queues are not scheduled */
	q_elem->scheduled = EM_FALSE;
	/* use unsched state for output queues  */
	q_elem->state = EM_QUEUE_STATE_UNSCHEDULED;

	if (unlikely(output_conf->output_fn == NULL)) {
		*err_str = "Q-setup-output: invalid output function";
		return -1;
	}

	/* copy whole output conf */
	q_elem->output.output_conf = *output_conf;
	q_elem->output.output_fn_args_event = EM_EVENT_UNDEF;
	if (output_conf->args_len == 0) {
		/* 'output_fn_args' is ignored, if 'args_len' is 0 */
		q_elem->output.output_conf.output_fn_args = NULL;
	} else {
		em_event_t args_event;
		void *args_storage;

		/* alloc an event to copy the given fn-args into */
		args_event = em_alloc(output_conf->args_len, EM_EVENT_TYPE_SW,
				      EM_POOL_DEFAULT);
		if (unlikely(args_event == EM_EVENT_UNDEF)) {
			*err_str = "Q-setup-output: alloc output_fn_args fails";
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

	/*
	 * Set up a plain ODP queue for EM output queue (re-)ordering.
	 *
	 * EM output-queues need an odp-queue to ensure re-ordering if
	 * events are sent into it from within an ordered context.
	 */
	odp_queue_param_t odp_queue_param;
	/* Retrieve previously stored ODP queue capabilities */
	const odp_queue_capability_t *odp_queue_capa =
		&em_shm->queue_tbl.odp_queue_capability;

	/* Init odp queue params to default values */
	odp_queue_param_init(&odp_queue_param);
	/* Set common ODP queue params based on the EM Queue config */
	queue_setup_odp_common(setup, &odp_queue_param);

	odp_queue_param.type = ODP_QUEUE_TYPE_PLAIN;
	odp_queue_param.order = ODP_QUEUE_ORDER_KEEP;

	/* check nonblocking level against plain queue capabilities */
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
	    odp_queue_capa->plain.lockfree.max_num == 0) {
		*err_str = "Q-setup-output: non-blocking, lock-free unsched queues unavailable";
		return -3;
	}
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
	    odp_queue_capa->plain.waitfree.max_num == 0) {
		*err_str = "Q-setup-output: non-blocking, wait-free unsched queues unavailable";
		return -4;
	}

	/* output-queue dequeue protected by q_elem->output.lock */
	odp_queue_param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;

	/* explicitly show here that output queues should not set odp-context */
	odp_queue_param.context = NULL;

	int err = create_odp_queue(q_elem, &odp_queue_param);

	if (unlikely(err)) {
		*err_str = "Q-setup-output: plain odp queue creation failed!";
		return -5;
	}

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
queue_state_set(queue_elem_t *const q_elem, queue_state_t new_state)
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
	em_status_t err = queue_state_set(q_elem, new_state);

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
	const list_node_t *list_node;

	/*
	 * Loop through all queues associated with the EO, no need for
	 * eo_elem-lock since this is called only on single core at the
	 * end of em_eo_start()
	 */
	env_spinlock_lock(&eo_elem->lock);

	list_for_each(&eo_elem->queue_list, pos, list_node) {
		q_elem = list_node_to_queue_elem(list_node);
		err = queue_state_set(q_elem, new_state);
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
	const odp_queue_capability_t *queue_capa =
		&em_shm->queue_tbl.odp_queue_capability;
	const odp_schedule_capability_t *sched_capa =
		&em_shm->queue_tbl.odp_schedule_capability;
	char plain_sz[24] = "n/a";
	char plain_lf_sz[24] = "n/a";
	char plain_wf_sz[24] = "n/a";
	char sched_sz[24] = "nolimit";

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

void
print_queue_prio_info(void)
{
	#define MAXPRIOBUF 127
	char buf[MAXPRIOBUF + 1];
	int pos = snprintf(buf, MAXPRIOBUF, "  Current queue priority map: [");

	if (pos > 0 && pos < MAXPRIOBUF) {
		for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
			int num = snprintf(buf + pos, MAXPRIOBUF - pos, "%d",
					   em_shm->queue_prio.map[i]);

			if (num < 0 || num >= (MAXPRIOBUF - pos))
				break;
			pos += num;

			if (i < EM_QUEUE_PRIO_NUM - 1) {
				num = snprintf(buf + pos, MAXPRIOBUF - pos, ",");
				if (num < 0 || num >= (MAXPRIOBUF - pos))
					break;
				pos += num;
			}
		}
	}
	buf[MAXPRIOBUF] = 0;
	EM_PRINT("%s]\n", buf);
}

unsigned int
queue_count(void)
{
	return env_atomic32_get(&em_shm->queue_count);
}

size_t queue_get_name(const queue_elem_t *const q_elem,
		      char name[/*out*/], const size_t maxlen)
{
	em_queue_t queue = q_elem->queue;
	const char *queue_name = &em_shm->queue_tbl.name[queue_hdl2idx(queue)][0];
	size_t len = strnlen(queue_name, EM_QUEUE_NAME_LEN - 1);

	if (maxlen - 1 < len)
		len = maxlen - 1;

	if (len)
		memcpy(name, queue_name, len);
	name[len] = '\0';

	return len;
}

static int queue_init_prio_legacy(int minp, int maxp)
{
	/* legacy mode - match the previous simple 3-level implementation */

	int def = odp_schedule_default_prio();

	/* needs to be synced with queue_prio_e values. Due to enum this can't be #if */
	COMPILE_TIME_ASSERT(EM_QUEUE_PRIO_HIGHEST < EM_QUEUE_PRIO_NUM,
			    "queue_prio_e values / EM_QUEUE_PRIO_NUM mismatch!\n");

	/* init both ends first */
	for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++)
		em_shm->queue_prio.map[i] = i < (EM_QUEUE_PRIO_NUM / 2) ? minp : maxp;

	/* then add NORMAL in the middle */
	em_shm->queue_prio.map[EM_QUEUE_PRIO_NORMAL] = def;
	/* if room: widen the normal range a bit */
	if (EM_QUEUE_PRIO_NORMAL - EM_QUEUE_PRIO_LOW > 1) /* legacy 4-2 */
		em_shm->queue_prio.map[EM_QUEUE_PRIO_NORMAL - 1] = def;
	if (EM_QUEUE_PRIO_HIGH - EM_QUEUE_PRIO_NORMAL > 1) /* legacy 6-4 */
		em_shm->queue_prio.map[EM_QUEUE_PRIO_NORMAL + 1] = def;

	return 0;
}

static void queue_init_prio_adaptive(int minp, int maxp, int nump)
{
	double step = (double)nump / EM_QUEUE_PRIO_NUM;
	double cur = (double)minp;

	/* simple linear fit to available levels */

	for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
		em_shm->queue_prio.map[i] = (int)cur;
		cur += step;
	}

	/* last EM prio always highest ODP level */
	if (em_shm->queue_prio.map[EM_QUEUE_PRIO_NUM - 1] != maxp)
		em_shm->queue_prio.map[EM_QUEUE_PRIO_NUM - 1] = maxp;
}

static int queue_init_prio_custom(int minp, int maxp)
{
	for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
		em_shm->queue_prio.map[i] = minp + em_shm->opt.queue.priority.custom_map[i];
		if (em_shm->queue_prio.map[i] > maxp || em_shm->queue_prio.map[i] < minp) {
			EM_PRINT("Invalid odp priority %d!\n", em_shm->queue_prio.map[i]);
			return -1;
		}
	}
	return 0;
}

static int queue_init_prio_map(int minp, int maxp, int nump)
{
	/* EM normally uses 8 priority levels (EM_QUEUE_PRIO_NUM).
	 * These are mapped to ODP runtime values depending on selected map mode
	 */

	switch (em_shm->opt.queue.priority.map_mode) {
	case 0: /* legacy mode, use only 3 levels */
		if (queue_init_prio_legacy(minp, maxp) != 0)
			return -1;
		break;
	case 1: /* adapt to runtime (full spread) */
		queue_init_prio_adaptive(minp, maxp, nump);
		break;
	case 2: /** custom */
		if (queue_init_prio_custom(minp, maxp) != 0)
			return -1;
		break;
	default:
		EM_PRINT("Unknown map_mode %d!\n", em_shm->opt.queue.priority.map_mode);
		return -1;
	}

	EM_PRINT("  EM uses %d priorities, runtime %d (%d-%d)\n",
		 EM_QUEUE_PRIO_NUM, nump, minp, nump - minp - 1);
	print_queue_prio_info();
	return 0;
}
