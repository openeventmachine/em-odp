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

/**
 * em_queue_group_modify() triggers an internal 'Done'-notification event
 * that updates the queue group mask. This struct contains the callback args.
 */
typedef struct {
	queue_group_elem_t *qgrp_elem;
	em_core_mask_t new_mask;
} q_grp_done_callback_args_t;

static em_status_t core_queue_groups_create(void);
static em_status_t core_queue_group_join(void);
static em_queue_group_t default_queue_group_create(void);
static em_queue_group_t default_queue_group_join(void);

static em_queue_group_t
queue_group_create_escope(const char *name, const em_core_mask_t *mask,
			  int num_notif, const em_notif_t notif_tbl[],
			  em_queue_group_t requested_queue_group,
			  em_escope_t escope);

static void q_grp_add_core(const queue_group_elem_t *qgrp_elem);
static void q_grp_rem_core(const queue_group_elem_t *qgrp_elem);

static void q_grp_create_done_callback(void *arg_ptr);
static void q_grp_create_sync_done_callback(void *arg_ptr);
static void q_grp_create_done(const queue_group_elem_t *const qgrp_elem,
			      const em_core_mask_t *const new_mask);
static void q_grp_create_sync_done(const queue_group_elem_t *const qgrp_elem,
				   const em_core_mask_t *const new_mask);

static void q_grp_modify_done_callback(void *arg_ptr);
static void q_grp_modify_sync_done_callback(void *arg_ptr);
static void q_grp_modify_done(const queue_group_elem_t *const qgrp_elem,
			      const em_core_mask_t *const new_mask);

static void q_grp_delete_done_callback(void *arg_ptr);
static void q_grp_delete_sync_done_callback(void *arg_ptr);
static void q_grp_delete_done(queue_group_elem_t *const qgrp_elem,
			      const em_core_mask_t *const new_mask);

static em_status_t
send_qgrp_addrem_reqs(queue_group_elem_t *qgrp_elem,
		      const em_core_mask_t *new_mask,
		      const em_core_mask_t *add_mask,
		      const em_core_mask_t *rem_mask,
		      int num_notif, const em_notif_t notif_tbl[],
		      em_escope_t escope);

/**
 * Return the queue group elem that includes the given objpool_elem_t
 */
static inline queue_group_elem_t *
queue_group_poolelem2qgrpelem(objpool_elem_t *const queue_group_pool_elem)
{
	return (queue_group_elem_t *)((uintptr_t)queue_group_pool_elem -
			offsetof(queue_group_elem_t, queue_group_pool_elem));
}

static int
read_config_file(void)
{
	const char *conf_str;
	bool val_bool = false;
	int ret;

	EM_PRINT("EM queue group config:\n");

	/*
	 * Option: esv.enable - runtime enable/disable
	 */
	conf_str = "queue_group.create_core_queue_groups";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.queue_group.create_core_queue_groups = val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	em_shm->opt.queue_group.create_core_queue_groups = val_bool;

	return 0;
}

/**
 * Queue group inits done at global init (once at startup on one core)
 */
em_status_t queue_group_init(queue_group_tbl_t *const queue_group_tbl,
			     queue_group_pool_t *const queue_group_pool)
{
	const uint32_t objpool_subpools = MIN(4, OBJSUBPOOLS_MAX);
	queue_group_elem_t *queue_group_elem;
	int ret;

	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	memset(queue_group_tbl, 0, sizeof(queue_group_tbl_t));
	memset(queue_group_pool, 0, sizeof(queue_group_pool_t));
	env_atomic32_init(&em_shm->queue_group_count);

	for (int i = 0; i < EM_MAX_QUEUE_GROUPS; i++) {
		queue_group_elem = &queue_group_tbl->queue_group_elem[i];
		queue_group_elem->queue_group = qgrp_idx2hdl(i);
		/* Initialize empty queue list */
		env_spinlock_init(&queue_group_elem->lock);
		list_init(&queue_group_elem->queue_list);
	}

	ret = objpool_init(&queue_group_pool->objpool, objpool_subpools);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (uint32_t i = 0; i < EM_MAX_QUEUE_GROUPS; i++) {
		queue_group_elem = &queue_group_tbl->queue_group_elem[i];
		objpool_add(&queue_group_pool->objpool, i % objpool_subpools,
			    &queue_group_elem->queue_group_pool_elem);
	}

	/*
	 * Create the EM default queue group: EM_QUEUE_GROUP_DEFAULT, "default"
	 */
	em_queue_group_t default_queue_group = default_queue_group_create();

	if (default_queue_group != EM_QUEUE_GROUP_DEFAULT) {
		EM_LOG(EM_LOG_ERR, "default_queue_group_create() failed!\n");
		return EM_ERR_LIB_FAILED;
	}

	/*
	 * Create EM single-core queue groups if enabled by config.
	 */
	if (em_shm->opt.queue_group.create_core_queue_groups) {
		em_status_t stat = core_queue_groups_create();

		if (stat != EM_OK) {
			EM_LOG(EM_LOG_ERR, "core_queue_groups_create():%" PRI_STAT "\n", stat);
			return stat;
		}
	}

	return EM_OK;
}

em_status_t queue_group_init_local(void)
{
	/*
	 * Update the EM default queue group with this cores information
	 */
	em_queue_group_t def_qgrp = default_queue_group_join();

	if (def_qgrp != EM_QUEUE_GROUP_DEFAULT) {
		EM_LOG(EM_LOG_ERR, "default_queue_group_join() failed!\n");
		return EM_ERR_LIB_FAILED;
	}

	/*
	 * Update the single-core queue group with this core's information
	 * if enabled by config.
	 */
	if (em_shm->opt.queue_group.create_core_queue_groups) {
		em_status_t stat = core_queue_group_join();

		if (stat != EM_OK) {
			EM_LOG(EM_LOG_ERR, "core_queue_group_join():%" PRI_STAT "\n", stat);
			return EM_ERR_LIB_FAILED;
		}
	}

	return EM_OK;
}

/**
 * Allocate a new EM queue group
 *
 * @param queue_group  EM queue group handle if a specific EM queue group is
 *                     requested, EM_QUEUE_GROUP_UNDEF if any EM queue group
 *                     will do.
 *
 * @return EM queue group handle
 * @retval EM_QUEUE_GROUP_UNDEF on failure
 */
static em_queue_group_t
queue_group_alloc(em_queue_group_t queue_group)
{
	queue_group_elem_t *qgrp_elem;
	objpool_elem_t *qgrp_pool_elem;

	if (queue_group == EM_QUEUE_GROUP_UNDEF) {
		/*
		 * Allocate any queue group, i.e. take next available
		 */
		qgrp_pool_elem = objpool_rem(&em_shm->queue_group_pool.objpool,
					     em_core_id());
		if (unlikely(qgrp_pool_elem == NULL))
			return EM_QUEUE_GROUP_UNDEF;

		qgrp_elem = queue_group_poolelem2qgrpelem(qgrp_pool_elem);
	} else {
		/*
		 * Allocate a specific queue group, handle given as argument
		 */
		qgrp_elem = queue_group_elem_get(queue_group);
		if (unlikely(qgrp_elem == NULL))
			return EM_QUEUE_GROUP_UNDEF;

		env_spinlock_lock(&qgrp_elem->lock);
		/* Verify that the queue group is not allocated */
		if (queue_group_allocated(qgrp_elem)) {
			env_spinlock_unlock(&qgrp_elem->lock);
			return EM_QUEUE_GROUP_UNDEF;
		}

		/* Remove the queue group from the pool */
		int ret = objpool_rem_elem(&em_shm->queue_group_pool.objpool,
					   &qgrp_elem->queue_group_pool_elem);
		env_spinlock_unlock(&qgrp_elem->lock);
		if (unlikely(ret != 0))
			return EM_QUEUE_GROUP_UNDEF;
	}

	env_atomic32_inc(&em_shm->queue_group_count);
	return qgrp_elem->queue_group;
}

/**
 * Free an EM queue group
 *
 * @param queue_group  EM queue group handle
 *
 * @return EM status
 * @retval EM_QUEUE_GROUP_UNDEF on failure
 */
static em_status_t
queue_group_free(em_queue_group_t queue_group)
{
	queue_group_elem_t *const queue_group_elem =
		queue_group_elem_get(queue_group);

	if (unlikely(queue_group_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(&em_shm->queue_group_pool.objpool,
		    queue_group_elem->queue_group_pool_elem.subpool_idx,
		    &queue_group_elem->queue_group_pool_elem);

	env_atomic32_dec(&em_shm->queue_group_count);
	return EM_OK;
}

/**
 * Create the EM default queue group 'EM_QUEUE_GROUP_DEFAULT'
 */
static em_queue_group_t default_queue_group_create(void)
{
	em_queue_group_t default_qgrp;
	queue_group_elem_t *default_qgrp_elem;
	em_core_mask_t *mask;
	odp_thrmask_t zero_thrmask;
	odp_schedule_group_t odp_sched_group;

	default_qgrp = queue_group_alloc(EM_QUEUE_GROUP_DEFAULT);
	if (unlikely(default_qgrp != EM_QUEUE_GROUP_DEFAULT))
		return EM_QUEUE_GROUP_UNDEF; /* sanity check */

	default_qgrp_elem = queue_group_elem_get(EM_QUEUE_GROUP_DEFAULT);
	if (unlikely(default_qgrp_elem == NULL))
		return EM_QUEUE_GROUP_UNDEF; /* sanity check */

	mask = &default_qgrp_elem->core_mask;
	em_core_mask_zero(mask);
	em_core_mask_set_count(em_core_count(), mask);

	odp_thrmask_zero(&zero_thrmask);

	/*
	 * Create a new odp schedule group for the EM default queue group.
	 * Don't use the ODP_SCHED_GROUP_WORKER or other predefined ODP groups
	 * since those groups can't be modified.
	 * Create the group without any cores/threads and update it during
	 * em_init_core() -> default_queue_group_join() calls for each
	 * EM core.
	 */
	default_qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;
	odp_sched_group = odp_schedule_group_create(EM_QUEUE_GROUP_DEFAULT_NAME,
						    &zero_thrmask);
	if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID))
		return EM_QUEUE_GROUP_UNDEF;
	/* Store the created odp sched group as the EM default queue group */
	default_qgrp_elem->odp_sched_group = odp_sched_group;

	return EM_QUEUE_GROUP_DEFAULT;
}

/**
 * Update the EM default queue group with valid group information for each
 * core local init and add the ODP thread-id to the scheduling mask.
 * Run by each call to em_init_core().
 */
static em_queue_group_t default_queue_group_join(void)
{
	queue_group_elem_t *default_qgrp_elem;
	odp_thrmask_t odp_joinmask;
	const int core_id = em_locm.core_id;
	const int odp_thr = odp_thread_id();
	int ret;

	default_qgrp_elem = queue_group_elem_get(EM_QUEUE_GROUP_DEFAULT);
	if (unlikely(!default_qgrp_elem))
		return EM_QUEUE_GROUP_UNDEF;

	/* Set this thread in the odp schedule group join-mask */
	odp_thrmask_zero(&odp_joinmask);
	odp_thrmask_set(&odp_joinmask, odp_thr);

	env_spinlock_lock(&default_qgrp_elem->lock);
	em_core_mask_set(core_id, &default_qgrp_elem->core_mask);
	/* Join this thread into the "EM default" schedule group */
	ret = odp_schedule_group_join(default_qgrp_elem->odp_sched_group,
				      &odp_joinmask);
	env_spinlock_unlock(&default_qgrp_elem->lock);

	if (unlikely(ret))
		return EM_QUEUE_GROUP_UNDEF;

	return EM_QUEUE_GROUP_DEFAULT;
}

/**
 * @brief The calling core joins all available queue groups
 *
 * Main use case for em_term(): to be able to flush the scheduler with only the
 * last EM-core running we need to modify all queue groups to include this last
 * core in the queue groups' core masks
 */
void queue_group_join_all(void)
{
	em_queue_group_t qgrp = em_queue_group_get_first(NULL);
	const int core_id = em_locm.core_id;

	while (qgrp != EM_QUEUE_GROUP_UNDEF) {
		queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

		env_spinlock_lock(&qgrp_elem->lock);

		int allocated = queue_group_allocated(qgrp_elem);
		bool ongoing_delete = qgrp_elem->ongoing_delete;

		if (allocated && !ongoing_delete &&
		    !em_core_mask_isset(core_id, &qgrp_elem->core_mask)) {
			em_core_mask_set(core_id, &qgrp_elem->core_mask);
			q_grp_add_core(qgrp_elem);
		}
		env_spinlock_unlock(&qgrp_elem->lock);

		qgrp = em_queue_group_get_next();
	}
}

static em_status_t core_queue_groups_create(void)
{
	em_queue_group_t qgrp;
	em_queue_group_t qgrp_req;
	queue_group_elem_t *qgrp_elem;
	em_core_mask_t *mask;
	odp_thrmask_t zero_thrmask;
	odp_schedule_group_t odp_sched_group;
	const int num_cores = em_core_count();
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];

	for (int i = 0; i < num_cores; i++) {
		qgrp_req = qgrp_idx2hdl(i);
		qgrp = queue_group_alloc(qgrp_req);
		if (unlikely(qgrp == EM_QUEUE_GROUP_UNDEF || qgrp != qgrp_req)) {
			EM_DBG("queue_group_alloc() fails for core-qgrp:%d\n", i);
			return EM_ERR_ALLOC_FAILED;
		}

		qgrp_elem = queue_group_elem_get(qgrp);
		if (unlikely(qgrp_elem == NULL)) {
			EM_DBG("qgrp_elem NULL for core-qgrp:%d\n", i);
			return EM_ERR_BAD_POINTER;
		}

		mask = &qgrp_elem->core_mask;
		em_core_mask_zero(mask);
		em_core_mask_set(i, mask);

		odp_thrmask_zero(&zero_thrmask);

		/*
		 * Create a new odp schedule group for each EM core.
		 * Create the group without the core/thread set and update it
		 * during em_init_core() -> core_queue_group_join()
		 * calls for each EM core.
		 */
		qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;
		core_queue_grp_name(i/*core*/, qgrp_name/*out*/,
				    sizeof(qgrp_name));
		odp_sched_group = odp_schedule_group_create(qgrp_name,
							    &zero_thrmask);
		if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID)) {
			EM_DBG("odp_schedule_group_create() fails for core-qgrp:%d\n", i);
			return EM_ERR_LIB_FAILED;
		}
		/* Store the created odp sched group for this EM queue group */
		qgrp_elem->odp_sched_group = odp_sched_group;
	}

	return EM_OK;
}

static em_status_t core_queue_group_join(void)
{
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
	int core = em_core_id();
	const int odp_thr = odp_thread_id();

	core_queue_grp_name(core, qgrp_name/*out*/, sizeof(qgrp_name));

	em_queue_group_t qgrp = em_queue_group_find(qgrp_name);

	if (unlikely(qgrp == EM_QUEUE_GROUP_UNDEF)) {
		EM_DBG("%s(): core:%d, %s not found", __func__, core, qgrp_name);
		return EM_ERR_NOT_FOUND;
	}

	queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

	if (unlikely(!qgrp_elem)) {
		EM_DBG("%s(): qgrp_elem NULL for core-qgrp:%d\n",
		       __func__, core);
		return EM_ERR_BAD_POINTER;
	}

	/* Set this thread in the odp schedule group join-mask */
	odp_thrmask_t odp_joinmask;

	odp_thrmask_zero(&odp_joinmask);
	odp_thrmask_set(&odp_joinmask, odp_thr);

	env_spinlock_lock(&qgrp_elem->lock);
	/* Join this thread into the core-local schedule group */
	int ret = odp_schedule_group_join(qgrp_elem->odp_sched_group,
					  &odp_joinmask);
	env_spinlock_unlock(&qgrp_elem->lock);

	if (unlikely(ret)) {
		EM_DBG("%s(): odp_schedule_group_join():%d, core-qgrp:%d\n",
		       __func__, ret, core);
		return EM_ERR_LIB_FAILED;
	}

	return EM_OK;
}

/**
 * Allow creating a queue group with a specific handle if requested and
 * available, use EM_QUEUE_GROUP_UNDEF to take any free handle.
 * Called from queue_group_create() and queue_group_create_sync() with an
 * appropriate escope.
 */
static em_queue_group_t
queue_group_create_escope(const char *name, const em_core_mask_t *mask,
			  int num_notif, const em_notif_t notif_tbl[],
			  em_queue_group_t requested_queue_group,
			  em_escope_t escope)
{
	em_queue_group_t queue_group;
	queue_group_elem_t *qgrp_elem;
	odp_schedule_group_t odp_sched_group;
	odp_thrmask_t zero_thrmask;
	em_status_t stat;
	em_core_mask_t add_mask;
	em_core_mask_t rem_zero_mask;
	const int core = em_core_id();

	odp_thrmask_zero(&zero_thrmask);
	em_core_mask_zero(&rem_zero_mask);
	em_core_mask_zero(&add_mask);
	em_core_mask_copy(&add_mask, mask);

	/*
	 * Allocate the queue group element,
	 * if 'requested_queue_group' == EM_QUEUE_GROUP_UNDEF take any handle.
	 */
	queue_group = queue_group_alloc(requested_queue_group);
	qgrp_elem = queue_group_elem_get(queue_group);
	if (unlikely(qgrp_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, escope,
			       "Queue group alloc failed!");
		/* No free queue group found */
		return EM_QUEUE_GROUP_UNDEF;
	}

	/* Create empty schedule group, each core adds itself via an add-req */
	odp_sched_group = odp_schedule_group_create(name, &zero_thrmask);
	if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID)) {
		queue_group_free(queue_group);
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, escope,
			       "ODP schedule group creation failed!");
		return EM_QUEUE_GROUP_UNDEF;
	}

	env_spinlock_lock(&qgrp_elem->lock);

	/* Initialize the data of the newly allocated queue group */
	qgrp_elem->odp_sched_group = odp_sched_group;
	em_core_mask_copy(&qgrp_elem->core_mask, mask); /* set new mask */
	list_init(&qgrp_elem->queue_list);
	env_atomic32_init(&qgrp_elem->num_queues);
	qgrp_elem->ongoing_delete = false;

	if (em_core_mask_isset(core, &add_mask)) {
		em_core_mask_clr(core, &add_mask);
		q_grp_add_core(qgrp_elem);
	}

	if (em_core_mask_iszero(&add_mask)) {
		if (escope == EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC)
			q_grp_create_sync_done(qgrp_elem, mask);
		else
			q_grp_create_done(qgrp_elem, mask);

		env_spinlock_unlock(&qgrp_elem->lock);

		stat = send_notifs(num_notif, notif_tbl);
		if (unlikely(stat != EM_OK))
			INTERNAL_ERROR(stat, escope, "Sending notifs failed!");

		return queue_group;
	}

	env_spinlock_unlock(&qgrp_elem->lock);

	stat = send_qgrp_addrem_reqs(qgrp_elem, mask, &add_mask, &rem_zero_mask,
				     num_notif, notif_tbl, escope);
	if (unlikely(stat != EM_OK))
		INTERNAL_ERROR(stat, escope, "qgrp add/rem-req(s) send failed");

	return queue_group;
}

/**
 * Allow creating a queue group with a specific handle
 * if requested and available.
 */
em_queue_group_t
queue_group_create(const char *name, const em_core_mask_t *mask,
		   int num_notif, const em_notif_t notif_tbl[],
		   em_queue_group_t requested_queue_group)
{
	return queue_group_create_escope(name, mask, num_notif, notif_tbl,
					 requested_queue_group,
					 EM_ESCOPE_QUEUE_GROUP_CREATE);
}

/**
 * Allow creating a queue group synchronously with a specific handle
 * if requested and available.
 * No need for sync blocking when creating a new queue group.
 */
em_queue_group_t
queue_group_create_sync(const char *name, const em_core_mask_t *mask,
			em_queue_group_t requested_queue_group)
{
	return queue_group_create_escope(name, mask, 0, NULL,
					 requested_queue_group,
					 EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC);
}

/*
 * queue_group_create/modify/_sync() helper:
 * Can only set core mask bits for running cores - verify this.
 */
em_status_t queue_group_check_mask(const em_core_mask_t *mask)
{
	const int core_count = em_core_count();
	em_core_mask_t max_mask;
	em_core_mask_t check_mask;

	/*
	 * 'mask' can contain set bits only for cores running EM,
	 * 'max_mask' contains all allowed set bits. Check that mask
	 * only contains set bits that are also found in max_mask.
	 */
	em_core_mask_zero(&max_mask);
	em_core_mask_set_count(core_count, &max_mask);
	em_core_mask_or(&check_mask, mask, &max_mask);

	if (unlikely(!em_core_mask_equal(&check_mask, &max_mask)))
		return EM_ERR_TOO_LARGE;

	return EM_OK;
}

/*
 * queue_group_modify/_sync() helper: check Queue Group state
 */
static em_status_t
check_qgrp_state(const queue_group_elem_t *qgrp_elem, bool is_delete,
		 const char **err_str/*out*/)
{
	if (unlikely(!queue_group_allocated(qgrp_elem))) {
		*err_str = "Queue group not allocated";
		return EM_ERR_BAD_ID;
	}
	if (unlikely(qgrp_elem->ongoing_delete)) {
		*err_str = "Contending queue group delete ongoing";
		return EM_ERR_BAD_STATE;
	}
	if (unlikely(is_delete && !list_is_empty(&qgrp_elem->queue_list))) {
		*err_str = "Queue group contains queues, cannot delete group";
		return EM_ERR_NOT_FREE;
	}

	return EM_OK;
}

/*
 * queue_group_modify/_sync() helper: count cores to be added to the queue group
 */
static int count_qgrp_adds(const em_core_mask_t *old_mask,
			   const em_core_mask_t *new_mask,
			   em_core_mask_t *add_mask /*out*/)
{
	int core_count = em_core_count();
	int adds = 0;

	em_core_mask_zero(add_mask);

	/* Count added cores */
	for (int i = 0; i < core_count; i++) {
		if (!em_core_mask_isset(i, old_mask) &&
		    em_core_mask_isset(i, new_mask)) {
			em_core_mask_set(i, add_mask);
			adds++;
		}
	}

	return adds;
}

/*
 * queue_group_modify/_sync() helper: count cores to be removed from the queue group
 */
static int count_qgrp_rems(const em_core_mask_t *old_mask,
			   const em_core_mask_t *new_mask,
			   em_core_mask_t *rem_mask /*out*/)
{
	int core_count = em_core_count();
	int rems = 0;

	em_core_mask_zero(rem_mask);

	/* Count removed cores */
	for (int i = 0; i < core_count; i++) {
		if (em_core_mask_isset(i, old_mask) &&
		    !em_core_mask_isset(i, new_mask)) {
			em_core_mask_set(i, rem_mask);
			rems++;
		}
	}

	return rems;
}

/**
 * @brief send_qgrp_addrem_reqs() helper: free unsent add/rem req events
 */
static void addrem_events_free(em_event_t add_events[], int add_count,
			       em_event_t rem_events[], int rem_count)
{
	for (int i = 0; i < add_count; i++) {
		if (add_events[i] != EM_EVENT_UNDEF)
			em_free(add_events[i]);
	}
	for (int i = 0; i < rem_count; i++) {
		if (rem_events[i] != EM_EVENT_UNDEF)
			em_free(rem_events[i]);
	}
}

/**
 * @brief send_qgrp_addrem_reqs() helper: send add or rem req events to cores
 *        in 'mask', send with an event group to trigger 'done' notification.
 *
 * Mark each sent event in the array as 'undef' to help detect sent vs. unsent
 */
static em_status_t send_addrem_events(em_event_t addrem_events[],
				      const em_core_mask_t *mask,
				      em_event_group_t event_group)
{
	const int core_count = em_core_count();
	const int first_qidx = queue_id2idx(FIRST_INTERNAL_UNSCHED_QUEUE);
	int ev_idx = 0;
	em_status_t err;

	for (int i = 0; i < core_count; i++) {
		if (em_core_mask_isset(i, mask)) {
			/*
			 * Send a add/rem-req to each core-specific queue,
			 * track completion using an event group.
			 */
			err = em_send_group(addrem_events[ev_idx],
					    queue_idx2hdl(first_qidx + i),
					    event_group);
			if (unlikely(err != EM_OK))
				return err;
			addrem_events[ev_idx] = EM_EVENT_UNDEF;
			ev_idx++;
		}
	}

	return EM_OK;
}

/**
 * @brief send_qgrp_addrem_reqs() helper: create the add/rem req events
 */
static int create_addrem_events(em_event_t addrem_events[/*out*/], int count,
				uint64_t ev_id, em_queue_group_t queue_group)
{
	internal_event_t *i_event;

	if (unlikely(count < 1))
		return 0;

	addrem_events[0] = em_alloc(sizeof(internal_event_t),
				    EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	if (unlikely(addrem_events[0] == EM_EVENT_UNDEF))
		return 0;

	/* Init the QUEUE_GROUP_ADD_REQ internal ctrl event(s) */
	i_event = em_event_pointer(addrem_events[0]);
	i_event->id = ev_id;
	i_event->q_grp.queue_group = queue_group;

	for (int i = 1; i < count; i++) {
		addrem_events[i] = em_event_clone(addrem_events[0],
						  EM_POOL_UNDEF);
		if (unlikely(addrem_events[i] == EM_EVENT_UNDEF))
			return i;
	}

	return count;
}

/**
 * @brief send_qgrp_addrem_reqs() helper: set callback based on err-scope (=id)
 */
static int
set_qgrp_done_func(em_escope_t escope,
		   void (**f_done_callback)(void *arg_ptr) /*out*/,
		   bool *sync_operation /*out*/)
{
	*sync_operation = false;

	switch (escope) {
	case EM_ESCOPE_QUEUE_GROUP_CREATE:
		*f_done_callback = q_grp_create_done_callback;
		break;
	case EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC:
		*f_done_callback = q_grp_create_sync_done_callback;
		*sync_operation = true;
		break;
	case EM_ESCOPE_QUEUE_GROUP_MODIFY:
		*f_done_callback = q_grp_modify_done_callback;
		break;
	case EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC:
		*f_done_callback = q_grp_modify_sync_done_callback;
		*sync_operation = true;
		break;
	case EM_ESCOPE_QUEUE_GROUP_DELETE:
		*f_done_callback = q_grp_delete_done_callback;
		break;
	case EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC:
		*f_done_callback = q_grp_delete_sync_done_callback;
		*sync_operation = true;
		break;
	default:
		*f_done_callback = NULL;
		return -1;
	}

	return 0;
}

/**
 * @brief queue_group_create/modify/_sync() helper: send qgrp addrem-req events to cores
 */
static em_status_t
send_qgrp_addrem_reqs(queue_group_elem_t *qgrp_elem,
		      const em_core_mask_t *new_mask,
		      const em_core_mask_t *add_mask,
		      const em_core_mask_t *rem_mask,
		      int num_notif, const em_notif_t notif_tbl[],
		      em_escope_t escope)
{
	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	const int add_count = em_core_mask_count(add_mask);
	const int rem_count = em_core_mask_count(rem_mask);
	const int addrem_count = add_count + rem_count; /* Subset of cores*/
	em_event_t add_events[add_count];
	em_event_t rem_events[rem_count];
	em_event_group_t event_group;
	em_status_t err;
	int cnt;
	int ret;

	em_event_t callback_args_event =
		em_alloc(sizeof(q_grp_done_callback_args_t),
			 EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	if (unlikely(callback_args_event == EM_EVENT_UNDEF))
		return EM_ERR_ALLOC_FAILED;

	/* Init the 'done'-callback function arguments */
	q_grp_done_callback_args_t *callback_args =
		em_event_pointer(callback_args_event);
	callback_args->qgrp_elem = qgrp_elem;
	em_core_mask_copy(&callback_args->new_mask, new_mask);

	/*
	 * Set the 'qgrp operation done'-callback func based on given
	 * escope (identifies operation).
	 *    f_done_callback(f_done_arg_ptr)
	 */
	void (*f_done_callback)(void *arg_ptr);
	void  *f_done_arg_ptr = callback_args_event;
	bool sync_operation = false;

	ret = set_qgrp_done_func(escope, &f_done_callback/*out*/,
				 &sync_operation/*out*/);
	if (unlikely(ret)) {
		em_free(callback_args_event);
		return EM_ERR_NOT_FOUND;
	}

	/*
	 * Create an event group to track completion of all sent add/rem-reqs.
	 * Set up notifications to be sent when all cores are done handling the
	 * queue group add/rem-reqs.
	 */
	event_group = internal_done_w_notif_req(addrem_count,
						f_done_callback, f_done_arg_ptr,
						num_notif, notif_tbl,
						sync_operation);
	if (unlikely(event_group == EM_EVENT_GROUP_UNDEF)) {
		em_free(callback_args_event);
		return EM_ERR_NOT_FREE;
	}

	for (int i = 0; i < add_count; i++)
		add_events[i] = EM_EVENT_UNDEF;
	for (int i = 0; i < rem_count; i++)
		rem_events[i] = EM_EVENT_UNDEF;

	/* Create internal events for queue group add-reqs */
	if (add_count) {
		cnt = create_addrem_events(add_events /*out*/, add_count,
					   QUEUE_GROUP_ADD_REQ, queue_group);
		if (unlikely(cnt != add_count))
			goto err_free_resources;
	}
	/* Create internal events for queue group rem-reqs */
	if (rem_count) {
		cnt = create_addrem_events(rem_events /*out*/, rem_count,
					   QUEUE_GROUP_REM_REQ, queue_group);
		if (unlikely(cnt != rem_count))
			goto err_free_resources;
	}

	/*
	 * Send rem-req events to the concerned cores
	 */
	err = send_addrem_events(rem_events, rem_mask, event_group);
	if (unlikely(err != EM_OK))
		goto err_free_resources;
	/*
	 * Send add-req events to the concerned cores
	 */
	err = send_addrem_events(add_events, add_mask, event_group);
	if (unlikely(err != EM_OK))
		goto err_free_resources;

	return EM_OK;

err_free_resources:
	addrem_events_free(add_events, add_count,
			   rem_events, rem_count);
	evgrp_abort_delete(event_group);
	em_free(callback_args_event);

	return EM_ERR_OPERATION_FAILED;
}

/**
 * Called by em_queue_group_modify with flag is_delete=0 and by
 * em_queue_group_delete() with flag is_delete=1
 *
 * @param qgrp_elem  Queue group element
 * @param new_mask   New core mask
 * @param num_notif  Number of entries in notif_tbl (0 for no notification)
 * @param notif_tbl  Array of notifications to send as the operation completes
 * @param is_delete  Is this modify triggered by em_queue_group_delete()?
 */
em_status_t
queue_group_modify(queue_group_elem_t *const qgrp_elem,
		   const em_core_mask_t *new_mask,
		   int num_notif, const em_notif_t notif_tbl[],
		   bool is_delete)
{
	em_status_t err;
	const char *err_str = "";
	const em_escope_t escope = is_delete ? EM_ESCOPE_QUEUE_GROUP_DELETE :
					       EM_ESCOPE_QUEUE_GROUP_MODIFY;
	const int core = em_core_id();

	env_spinlock_lock(&qgrp_elem->lock);

	/* Check Queue Group state */
	err = check_qgrp_state(qgrp_elem, is_delete, &err_str/*out*/);
	if (unlikely(err != EM_OK)) {
		env_spinlock_unlock(&qgrp_elem->lock);
		return INTERNAL_ERROR(err, escope, err_str);
	}

	em_core_mask_t old_mask;

	/* store previous mask */
	em_core_mask_copy(&old_mask, &qgrp_elem->core_mask);
	/* update with new_mask */
	em_core_mask_copy(&qgrp_elem->core_mask, new_mask);

	/* Count added & removed cores */
	em_core_mask_t add_mask;
	em_core_mask_t rem_mask;
	int adds = count_qgrp_adds(&old_mask, new_mask, &add_mask /*out*/);
	int rems = count_qgrp_rems(&old_mask, new_mask, &rem_mask /*out*/);
	/*
	 * Remove the calling core from the add-mask. The core adds itself.
	 * Don't do the same for the rem-mask: we want to send a rem-event to
	 * this core to ensure the core is not currently processing from that
	 * queue-group.
	 */
	if (adds > 0 && em_core_mask_isset(core, &add_mask)) {
		em_core_mask_clr(core, &add_mask);
		adds--;
		q_grp_add_core(qgrp_elem);
	}

	/*
	 * If the new mask is equal to the one in use:
	 * send notifs immediately and return.
	 */
	if (em_core_mask_equal(&old_mask, new_mask) || (adds == 0 && rems == 0)) {
		/* New mask == curr mask, or both zero, send notifs & return */
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);
		else
			q_grp_modify_done(qgrp_elem, new_mask);

		env_spinlock_unlock(&qgrp_elem->lock);

		err = send_notifs(num_notif, notif_tbl);
		RETURN_ERROR_IF(err != EM_OK, err, escope,
				"notif sending failed");
		return EM_OK;
	}

	/* Catch contending queue group operations while delete is ongoing */
	if (is_delete)
		qgrp_elem->ongoing_delete = true;

	env_spinlock_unlock(&qgrp_elem->lock);

	/*
	 * Send add/rem-req events to all other concerned cores.
	 * Note: if .ongoing_delete = true:
	 *       Treat errors as EM_FATAL because failures will leave
	 *       .ongoing_delete = true for the group until restart of EM.
	 */
	err = send_qgrp_addrem_reqs(qgrp_elem, new_mask, &add_mask, &rem_mask,
				    num_notif, notif_tbl, escope);
	RETURN_ERROR_IF(err != EM_OK, is_delete ? EM_FATAL(err) : err, escope,
			"qgrp rem req(s) sending failed");

	return EM_OK;
}

/**
 * Called by em_queue_group_modify_sync with flag is_delete=0 and by
 * em_queue_group_delete_sync() with flag is_delete=1
 *
 * @param qgrp_elem  Queue group element
 * @param new_mask   New core mask
 * @param is_delete  Is this modify triggered by em_queue_group_delete_sync()?
 */
em_status_t
queue_group_modify_sync(queue_group_elem_t *const qgrp_elem,
			const em_core_mask_t *new_mask, bool is_delete)
{
	em_locm_t *const locm = &em_locm;
	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	em_status_t err = EM_OK;
	const char *err_str = "";
	const em_escope_t escope = is_delete ? EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC
					: EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC;
	const int core = em_core_id();

	/* Mark that a sync-API call is in progress */
	locm->sync_api.in_progress = true;

	env_spinlock_lock(&qgrp_elem->lock);

	/* Check Queue Group state */
	err = check_qgrp_state(qgrp_elem, is_delete, &err_str/*out*/);
	if (unlikely(err != EM_OK)) {
		env_spinlock_unlock(&qgrp_elem->lock);
		goto queue_group_modify_sync_error;
	}

	em_core_mask_t old_mask;

	/* store previous mask */
	em_core_mask_copy(&old_mask, &qgrp_elem->core_mask);
	/* update with new_mask */
	em_core_mask_copy(&qgrp_elem->core_mask, new_mask);

	if (em_core_mask_equal(&old_mask, new_mask)) {
		/* New mask == curr mask, or both zero */
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);

		env_spinlock_unlock(&qgrp_elem->lock);

		err = EM_OK;
		goto queue_group_modify_sync_error; /* no error, just return */
	}

	/* Catch contending queue group operations while delete is ongoing */
	if (is_delete)
		qgrp_elem->ongoing_delete = true;

	/* Count added & removed cores */
	em_core_mask_t add_mask;
	em_core_mask_t rem_mask;
	int adds = count_qgrp_adds(&old_mask, new_mask, &add_mask /*out*/);
	int rems = count_qgrp_rems(&old_mask, new_mask, &rem_mask /*out*/);

	/*
	 * Remove the calling core from the add/rem-mask and -count since no
	 * add/rem-req event should be sent to it during this _sync operation.
	 */
	if (adds > 0 && em_core_mask_isset(core, &add_mask)) {
		em_core_mask_clr(core, &add_mask);
		adds--;
		q_grp_add_core(qgrp_elem);
	}
	if (rems > 0 && em_core_mask_isset(core, &rem_mask)) {
		em_core_mask_clr(core, &rem_mask);
		rems--;
		q_grp_rem_core(qgrp_elem);
	}

	/* No cores to send rem-reqs to, mark operation done and return */
	if (adds == 0 && rems == 0) {
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);
		else
			q_grp_modify_done(qgrp_elem, new_mask);

		env_spinlock_unlock(&qgrp_elem->lock);
		err = EM_OK;
		goto queue_group_modify_sync_error; /* no error, just return  */
	}

	env_spinlock_unlock(&qgrp_elem->lock);

	/*
	 * Send add/rem-req events to all other concerned cores.
	 * Note: if .ongoing_delete = true:
	 *       Treat errors as EM_FATAL because failures will leave
	 *       .ongoing_delete = true for the group until restart of EM.
	 */
	err = send_qgrp_addrem_reqs(qgrp_elem, new_mask, &add_mask, &rem_mask,
				    0, NULL, escope);
	if (unlikely(err != EM_OK)) {
		if (is_delete)
			err = EM_FATAL(err);
		goto queue_group_modify_sync_error;
	}

	/*
	 * Poll the core-local unscheduled control-queue for events.
	 * These events request the core to do a core-local operation (or not).
	 * Poll and handle events until 'locm->sync_api.in_progress == false'
	 * indicating that this sync-API is 'done' on all concerned cores.
	 */
	while (locm->sync_api.in_progress)
		poll_unsched_ctrl_queue();

	return EM_OK;

queue_group_modify_sync_error:
	locm->sync_api.in_progress = false;
	RETURN_ERROR_IF(err != EM_OK, err, escope,
			"Failure: Modify sync QGrp:%" PRI_QGRP ":%s",
			queue_group, err_str);
	return EM_OK;
}

/**
 * @brief Add the calling core to the odp schedule group that is used by
 *        the given EM queue group.
 *
 * @param qgrp_elem Queue group element
 */
static void q_grp_add_core(const queue_group_elem_t *qgrp_elem)
{
	int odp_thr = odp_thread_id();
	odp_thrmask_t odp_joinmask;

	odp_thrmask_zero(&odp_joinmask);
	odp_thrmask_set(&odp_joinmask, odp_thr);

	/* Join this thread into the core-local schedule group */
	int ret = odp_schedule_group_join(qgrp_elem->odp_sched_group,
					  &odp_joinmask);
	if (unlikely(ret)) {
		char mask_str[EM_CORE_MASK_STRLEN];
		em_queue_group_t queue_group = qgrp_elem->queue_group;

		em_core_mask_tostr(mask_str, EM_CORE_MASK_STRLEN,
				   &qgrp_elem->core_mask);
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_QUEUE_GROUP_ADD_CORE,
			       "QGrp ADD core%02d: odp_schedule_group_join(thr:%d):%d\n"
			       "QueueGroup:%" PRI_QGRP " core-mask:%s",
			       em_core_id(), odp_thr, ret, queue_group, mask_str);
	}
}

/**
 * @brief Remove the calling core from the odp schedule group that is used by
 *        the given EM queue group.
 *
 * @param qgrp_elem Queue group element
 */
static void q_grp_rem_core(const queue_group_elem_t *qgrp_elem)
{
	int odp_thr = odp_thread_id();
	odp_thrmask_t odp_leavemask;

	odp_thrmask_zero(&odp_leavemask);
	odp_thrmask_set(&odp_leavemask, odp_thr);

	/* Join this thread into the core-local schedule group */
	int ret = odp_schedule_group_leave(qgrp_elem->odp_sched_group,
					   &odp_leavemask);
	if (unlikely(ret)) {
		char mask_str[EM_CORE_MASK_STRLEN];
		em_queue_group_t queue_group = qgrp_elem->queue_group;

		em_core_mask_tostr(mask_str, EM_CORE_MASK_STRLEN,
				   &qgrp_elem->core_mask);
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_QUEUE_GROUP_REM_CORE,
			       "QGrp REM core%02d: odp_schedule_group_leave(thr:%d):%d\n"
			       "QueueGroup:%" PRI_QGRP " core-mask:%s",
			       em_core_id(), odp_thr, ret, queue_group, mask_str);
	}
}

void i_event__qgrp_add_core_req(const internal_event_t *i_ev)
{
	em_queue_group_t qgrp = i_ev->q_grp.queue_group;
	queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

	if (unlikely(!qgrp_elem))
		return;

	env_spinlock_lock(&qgrp_elem->lock);
	q_grp_add_core(qgrp_elem);
	env_spinlock_unlock(&qgrp_elem->lock);
}

void i_event__qgrp_rem_core_req(const internal_event_t *i_ev)
{
	em_queue_group_t qgrp = i_ev->q_grp.queue_group;
	queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

	if (unlikely(!qgrp_elem))
		return;

	env_spinlock_lock(&qgrp_elem->lock);
	q_grp_rem_core(qgrp_elem);
	env_spinlock_unlock(&qgrp_elem->lock);
}

/**
 * Callback function when a em_queue_group_create()
 * completes with the internal DONE-event
 */
static void q_grp_create_done_callback(void *arg_ptr)
{
	em_event_t event = (em_event_t)arg_ptr;
	const q_grp_done_callback_args_t *args = em_event_pointer(event);
	queue_group_elem_t *const qgrp_elem = args->qgrp_elem;

	env_spinlock_lock(&qgrp_elem->lock);
	q_grp_create_done(qgrp_elem, &args->new_mask);
	env_spinlock_unlock(&qgrp_elem->lock);

	em_free(event);
}

/**
 * Callback function when a em_queue_group_create_sync()
 * completes with the internal DONE-event
 */
static void q_grp_create_sync_done_callback(void *arg_ptr)
{
	em_event_t event = (em_event_t)arg_ptr;
	const q_grp_done_callback_args_t *args = em_event_pointer(event);
	queue_group_elem_t *const qgrp_elem = args->qgrp_elem;

	env_spinlock_lock(&qgrp_elem->lock);
	q_grp_create_sync_done(qgrp_elem, &args->new_mask);
	env_spinlock_unlock(&qgrp_elem->lock);

	em_free(event);
}

static void q_grp_create_done(const queue_group_elem_t *const qgrp_elem,
			      const em_core_mask_t *const new_mask)
{
	(void)qgrp_elem;
	(void)new_mask;
}

static void q_grp_create_sync_done(const queue_group_elem_t *const qgrp_elem,
				   const em_core_mask_t *const new_mask)
{
	(void)qgrp_elem;
	(void)new_mask;
}

/**
 * Callback function when a em_queue_group_modify()
 * completes with the internal DONE-event
 */
static void q_grp_modify_done_callback(void *arg_ptr)
{
	em_event_t event = (em_event_t)arg_ptr;
	const q_grp_done_callback_args_t *args = em_event_pointer(event);
	queue_group_elem_t *const qgrp_elem = args->qgrp_elem;

	env_spinlock_lock(&qgrp_elem->lock);
	q_grp_modify_done(qgrp_elem, &args->new_mask);
	env_spinlock_unlock(&qgrp_elem->lock);

	em_free(event);
}

/**
 * Callback function when a em_queue_group_modify_sync()
 * completes with the internal DONE-event
 */
static void q_grp_modify_sync_done_callback(void *arg_ptr)
{
	em_locm_t *const locm = &em_locm;

	q_grp_modify_done_callback(arg_ptr);

	/* Enable the caller of the sync API func to proceed (on this core) */
	locm->sync_api.in_progress = false;
}

static void q_grp_modify_done(const queue_group_elem_t *const qgrp_elem,
			      const em_core_mask_t *const new_mask)
{
	(void)qgrp_elem;
	(void)new_mask;
}

/**
 * Callback function when a em_queue_group_modify(delete flag set)
 * completes with the internal DONE-event
 */
static void q_grp_delete_done_callback(void *arg_ptr)
{
	em_event_t event = (em_event_t)arg_ptr;
	const q_grp_done_callback_args_t *args = em_event_pointer(event);
	queue_group_elem_t *const qgrp_elem = args->qgrp_elem;

	env_spinlock_lock(&qgrp_elem->lock);
	q_grp_delete_done(qgrp_elem, &args->new_mask);
	env_spinlock_unlock(&qgrp_elem->lock);

	em_free(event);
}

/**
 * Callback function when a em_queue_group_modify_sync(delete flag set)
 * completes with the internal DONE-event
 */
static void q_grp_delete_sync_done_callback(void *arg_ptr)
{
	em_locm_t *const locm = &em_locm;

	q_grp_delete_done_callback(arg_ptr);

	/* Enable the caller of the sync API func to proceed (on this core) */
	locm->sync_api.in_progress = false;
}

static void q_grp_delete_done(queue_group_elem_t *const qgrp_elem,
			      const em_core_mask_t *const new_mask)
{
	const unsigned int num_queues = env_atomic32_get(&qgrp_elem->num_queues);
	const em_queue_group_t queue_group = qgrp_elem->queue_group;

	/* Sanity check: new core mask for delete is always zero */
	if (unlikely(!em_core_mask_iszero(new_mask))) {
		char mstr[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mstr, EM_CORE_MASK_STRLEN, new_mask);
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "Delete QGrp:%" PRI_QGRP " mask not zero:%s",
			       queue_group, mstr);
	}
	/* Sanity check: grp must not have been modified since start of delete */
	if (unlikely(!em_core_mask_equal(&qgrp_elem->core_mask, new_mask))) {
		char mstr1[EM_CORE_MASK_STRLEN];
		char mstr2[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mstr1, EM_CORE_MASK_STRLEN, &qgrp_elem->core_mask);
		em_core_mask_tostr(mstr2, EM_CORE_MASK_STRLEN, new_mask);
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "Delete QGrp:%" PRI_QGRP ", masks modified during delete:%s vs. %s",
			       queue_group, mstr1, mstr2);
	}

	if (unlikely(!list_is_empty(&qgrp_elem->queue_list) || num_queues))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_FREE), EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "Delete QGrp:%" PRI_QGRP ", contains %u queues, cannot delete!",
			       queue_group, num_queues);

	int ret = odp_schedule_group_destroy(qgrp_elem->odp_sched_group);

	if (unlikely(ret != 0))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "Delete QGrp:%" PRI_QGRP ", ODP sched grp destroy fails:%d",
			       queue_group, ret);

	qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;
	qgrp_elem->ongoing_delete = false;

	/* Free the queue group */
	queue_group_free(qgrp_elem->queue_group);
}

void queue_group_add_queue_list(queue_group_elem_t *const queue_group_elem,
				queue_elem_t *const queue_elem)
{
	env_spinlock_lock(&queue_group_elem->lock);
	list_add(&queue_group_elem->queue_list, &queue_elem->qgrp_node);
	env_atomic32_inc(&queue_group_elem->num_queues);
	env_spinlock_unlock(&queue_group_elem->lock);
}

void queue_group_rem_queue_list(queue_group_elem_t *const queue_group_elem,
				queue_elem_t *const queue_elem)
{
	env_spinlock_lock(&queue_group_elem->lock);
	if (!list_is_empty(&queue_group_elem->queue_list)) {
		list_rem(&queue_group_elem->queue_list, &queue_elem->qgrp_node);
		env_atomic32_dec(&queue_group_elem->num_queues);
	}
	env_spinlock_unlock(&queue_group_elem->lock);
}

unsigned int queue_group_count(void)
{
	return env_atomic32_get(&em_shm->queue_group_count);
}

#define QGRP_INFO_HDR_STR \
"EM Queue group(s):%2u\n" \
"ID        Name                            EM-mask             Cpumask         " \
"    ODP-mask            Q-num\n" \
"------------------------------------------------------------------------------" \
"------------------------------\n" \
"%s\n"

/* Info len (in bytes) per queue group, calculated from QGRP_INFO_FMT */
#define QGRP_INFO_LEN (108 + 1 /* Terminating null byte */)
#define QGRP_INFO_FMT "%-10" PRI_QGRP "%-32s%-20s%-20s%-20s%-5d\n" /*108 bytes*/

static void queue_group_info_str(em_queue_group_t queue_group,
				 char qgrp_info_str[/*out*/])
{
	em_core_mask_t core_mask;
	odp_thrmask_t odp_thrmask;
	odp_cpumask_t odp_cpumask;
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
	char em_mask_str[EM_CORE_MASK_STRLEN];
	char odp_thrmask_str[ODP_THRMASK_STR_SIZE];
	char odp_cpumask_str[ODP_CPUMASK_STR_SIZE];
	em_status_t err;
	int ret;
	int len = 0;

	const queue_group_elem_t *qgrp_elem = queue_group_elem_get(queue_group);

	if (unlikely(!qgrp_elem || !queue_group_allocated(qgrp_elem)))
		goto info_print_err;

	em_queue_group_get_name(queue_group, qgrp_name, sizeof(qgrp_name));
	err = em_queue_group_get_mask(queue_group, &core_mask);
	if (unlikely(err != EM_OK))
		goto info_print_err;
	em_core_mask_tostr(em_mask_str, sizeof(em_mask_str), &core_mask);

	/* ODP thread mask */
	ret = odp_schedule_group_thrmask(qgrp_elem->odp_sched_group,
					 &odp_thrmask /*out*/);
	if (unlikely(ret))
		goto info_print_err;
	ret = odp_thrmask_to_str(&odp_thrmask, odp_thrmask_str,
				 sizeof(odp_thrmask_str));
	if (unlikely(ret <= 0))
		goto info_print_err;
	odp_thrmask_str[ret - 1] = '\0';

	/* Physical mask */
	mask_em2phys(&core_mask, &odp_cpumask /*out*/);
	ret = odp_cpumask_to_str(&odp_cpumask, odp_cpumask_str,
				 sizeof(odp_cpumask_str));
	if (unlikely(ret <= 0))
		goto info_print_err;
	odp_cpumask_str[ret - 1] = '\0';

	len = snprintf(qgrp_info_str, QGRP_INFO_LEN, QGRP_INFO_FMT,
		       queue_group, qgrp_name, em_mask_str,
		       odp_cpumask_str, odp_thrmask_str,
		       env_atomic32_get(&qgrp_elem->num_queues));

	qgrp_info_str[len] = '\0';
	return;

info_print_err:
	len = snprintf(qgrp_info_str, QGRP_INFO_LEN, QGRP_INFO_FMT,
		       queue_group, "err:n/a", "n/a", "n/a", "n/a", 0);
	qgrp_info_str[len] = '\0';
}

void queue_group_info_print_all(void)
{
	em_queue_group_t qgrp;
	unsigned int qgrp_num;
	char single_qgrp_info_str[QGRP_INFO_LEN];
	int len = 0;
	int n_print = 0;

	qgrp = em_queue_group_get_first(&qgrp_num);

	/*
	 * qgrp_num may not match the amount of queue groups actually returned
	 * by iterating using em_queue_group_get_next() if queue groups are added
	 * or removed in parallel by another core. Thus space for 10 extra queue
	 * groups is reserved. If more than 10 queue groups are added by other
	 * cores in parallel, we print only information of the (qgrp_num + 10)
	 * queue groups.
	 *
	 * The extra 1 byte is reserved for the terminating null byte.
	 */
	const int all_qgrp_info_str_len = (qgrp_num + 10) * QGRP_INFO_LEN + 1;
	char all_qgrp_info_str[all_qgrp_info_str_len];

	while (qgrp != EM_QUEUE_GROUP_UNDEF) {
		queue_group_info_str(qgrp, single_qgrp_info_str);

		n_print = snprintf(all_qgrp_info_str + len,
				   all_qgrp_info_str_len - len,
				   "%s", single_qgrp_info_str);

		/* Not enough space to hold more queue group info */
		if (n_print >= all_qgrp_info_str_len - len)
			break;

		len += n_print;
		qgrp = em_queue_group_get_next();
	}

	/* No EM queue group */
	if (len == 0) {
		EM_PRINT("No EM queue group!\n");
		return;
	}

	/*
	 * To prevent printing incomplete information of the last queue group
	 * when there is not enough space to hold all queue group info.
	 */
	all_qgrp_info_str[len] = '\0';
	EM_PRINT(QGRP_INFO_HDR_STR, qgrp_num, all_qgrp_info_str);
}

#define QGRO_QUEUE_INFO_HDR_STR \
"Queue group %" PRI_QGRP "(%s) has %d queue(s):\n\n" \
"Id        Name                            Priority  Type      State    Ctx\n" \
"--------------------------------------------------------------------------\n" \
"%s\n"

/* Info len (in bytes) per queue group queue, calculated from QGRP_Q_INFO_FMT */
#define QGRP_Q_LEN 75
#define QGRP_Q_INFO_FMT "%-10" PRI_QUEUE "%-32s%-10d%-10s%-9s%-3c\n" /*75 bytes*/

void queue_group_queues_print(em_queue_group_t qgrp)
{
	unsigned int q_num;
	em_queue_t qgrp_queue;
	const queue_elem_t *q_elem;
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
	char q_name[EM_QUEUE_NAME_LEN];
	int len = 0;
	int n_print = 0;

	const queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

	if (unlikely(!qgrp_elem || !queue_group_allocated(qgrp_elem))) {
		EM_PRINT("Queue group %" PRI_QGRP " is not created!\n", qgrp);
		return;
	}

	em_queue_group_get_name(qgrp, qgrp_name, sizeof(qgrp_name));
	qgrp_queue = em_queue_group_queue_get_first(&q_num, qgrp);

	/*
	 * q_num may not match the amount of queues actually returned by iterating
	 * using em_queue_group_queue_get_next() if queues are added or removed
	 * in parallel by another core. Thus space for 10 extra queues is reserved.
	 * If more than 10 extra queues are added to this queue group by other
	 * cores in parallel, we print only information of the (q_num + 10) queues.
	 *
	 * The extra 1 byte is reserved for the terminating null byte.
	 */
	const int q_info_len = (q_num + 10) * QGRP_Q_LEN + 1;
	char q_info_str[q_info_len];

	while (qgrp_queue != EM_QUEUE_UNDEF) {
		q_elem = queue_elem_get(qgrp_queue);

		if (unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
			qgrp_queue = em_queue_group_queue_get_next();
			continue;
		}

		queue_get_name(q_elem, q_name, EM_QUEUE_NAME_LEN - 1);

		n_print = snprintf(q_info_str + len, q_info_len - len,
				   QGRP_Q_INFO_FMT, qgrp_queue, q_name,
				   q_elem->priority,
				   queue_get_type_str(q_elem->type),
				   queue_get_state_str(q_elem->state),
				   q_elem->context ? 'Y' : 'N');

		/* Not enough space to hold more queue info */
		if (n_print >= q_info_len - len)
			break;

		len += n_print;
		qgrp_queue = em_queue_group_queue_get_next();
	}

	/* No queue belonging to the queue group */
	if (!len) {
		EM_PRINT("Queue group %" PRI_QGRP "(%s) has no queue!\n",
			 qgrp, qgrp_name);
		return;
	}

	/*
	 * To prevent printing incomplete information of the last queue when
	 * there is not enough space to hold all queue info.
	 */
	q_info_str[len] = '\0';
	EM_PRINT(QGRO_QUEUE_INFO_HDR_STR, qgrp, qgrp_name, q_num, q_info_str);
}
