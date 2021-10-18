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
} q_grp_modify_done_callback_args_t;

static em_queue_group_t
queue_group_create_escope(const char *name, const em_core_mask_t *mask,
			  int num_notif, const em_notif_t notif_tbl[],
			  em_queue_group_t requested_queue_group,
			  em_escope_t escope);

static void
q_grp_modify_done_callback(void *arg_ptr);
static void
q_grp_modify_sync_done_callback(void *arg_ptr);

static void
q_grp_modify_done(queue_group_elem_t *const qgrp_elem,
		  const em_core_mask_t *const new_mask);

static void
q_grp_delete_done_callback(void *arg_ptr);
static void
q_grp_delete_sync_done_callback(void *arg_ptr);

static void
q_grp_delete_done(queue_group_elem_t *const qgrp_elem,
		  const em_core_mask_t *const new_mask);

/**
 * Return the queue group elem that includes the given objpool_elem_t
 */
static inline queue_group_elem_t *
queue_group_poolelem2queue(objpool_elem_t *const queue_group_pool_elem)
{
	return (queue_group_elem_t *)((uintptr_t)queue_group_pool_elem -
			offsetof(queue_group_elem_t, queue_group_pool_elem));
}

/**
 * Queue group inits done at global init (once at startup on one core)
 */
em_status_t
queue_group_init(queue_group_tbl_t *const queue_group_tbl,
		 queue_group_pool_t *const queue_group_pool)
{
	const int cores = em_core_count();
	queue_group_elem_t *queue_group_elem;
	int ret;

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

	ret = objpool_init(&queue_group_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (int i = 0; i < EM_MAX_QUEUE_GROUPS; i++) {
		queue_group_elem = &queue_group_tbl->queue_group_elem[i];
		objpool_add(&queue_group_pool->objpool, i % cores,
			    &queue_group_elem->queue_group_pool_elem);
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

		qgrp_elem = queue_group_poolelem2queue(qgrp_pool_elem);
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
em_queue_group_t
default_queue_group_create(void)
{
	em_queue_group_t default_qgrp;
	queue_group_elem_t *default_qgrp_elem;
	em_core_mask_t *mask;
	odp_thrmask_t *odp_thrmask;

	default_qgrp = queue_group_alloc(EM_QUEUE_GROUP_DEFAULT);
	if (unlikely(default_qgrp != EM_QUEUE_GROUP_DEFAULT))
		return EM_QUEUE_GROUP_UNDEF; /* sanity check */

	default_qgrp_elem = queue_group_elem_get(EM_QUEUE_GROUP_DEFAULT);
	if (unlikely(default_qgrp_elem == NULL))
		return EM_QUEUE_GROUP_UNDEF; /* sanity check */

	mask = &default_qgrp_elem->core_mask;
	odp_thrmask = &default_qgrp_elem->odp_thrmask;

	/*
	 * Set all values of the default queue group to UNDEF/0 since a new
	 * odp schedule group / EM queue group cannot be created until all
	 * odp_init_local():s and em_init_core():s have been run on all cores.
	 * Call default_queue_group_update() during the last em_init_core() to
	 * create the actual group.
	 */
	default_qgrp_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	default_qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;
	em_core_mask_zero(mask);
	odp_thrmask_zero(odp_thrmask);

	return EM_QUEUE_GROUP_DEFAULT;
}

/**
 * Update the EM default queue group with valid group information after all the
 * core local inits have been run and both ODP and EM API-funcs are operational.
 */
em_queue_group_t
default_queue_group_update(void)
{
	queue_group_elem_t *default_qgrp_elem;
	em_core_mask_t *mask;
	odp_thrmask_t *odp_thrmask;

	default_qgrp_elem = queue_group_elem_get(EM_QUEUE_GROUP_DEFAULT);
	if (unlikely(default_qgrp_elem == NULL))
		return EM_QUEUE_GROUP_UNDEF;

	mask = &default_qgrp_elem->core_mask;
	em_core_mask_set_count(em_core_count(), mask);

	odp_thrmask = &default_qgrp_elem->odp_thrmask;
	/* Update the ODP thread mask for the default queue group */
	mask_em2odp(mask, odp_thrmask);

	/*
	 * Create a new odp schedule group for the EM default queue group.
	 * Don't use the ODP_SCHED_GROUP_WORKER or other predefined ODP groups
	 * since those groups can't be modified.
	 */
	default_qgrp_elem->odp_sched_group =
		odp_schedule_group_create(EM_QUEUE_GROUP_DEFAULT_NAME,
					  odp_thrmask);
	if (unlikely(default_qgrp_elem->odp_sched_group ==
		     ODP_SCHED_GROUP_INVALID))
		return EM_QUEUE_GROUP_UNDEF;

	default_qgrp_elem->queue_group = EM_QUEUE_GROUP_DEFAULT;

	return EM_QUEUE_GROUP_DEFAULT;
}

/**
 * Allow creating a queue group with a specific handle
 * if requested and available.
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
	odp_thrmask_t odp_thrmask;
	em_status_t stat;

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

	/* Map EM core mask to ODP thrmask */
	mask_em2odp(mask, &odp_thrmask);

	env_spinlock_lock(&qgrp_elem->lock);

	odp_sched_group = odp_schedule_group_create(name, &odp_thrmask);
	if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID)) {
		env_spinlock_unlock(&qgrp_elem->lock);
		queue_group_free(queue_group);
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, escope,
			       "ODP schedule group creation failed!");
		return EM_QUEUE_GROUP_UNDEF;
	}

	/* Initialize the data of the newly allocated queue group */
	qgrp_elem->odp_sched_group = odp_sched_group;
	em_core_mask_zero(&qgrp_elem->core_mask);
	list_init(&qgrp_elem->queue_list);
	env_atomic32_init(&qgrp_elem->num_queues);
	em_core_mask_copy(&qgrp_elem->core_mask, mask);
	odp_thrmask_copy(&qgrp_elem->odp_thrmask, &odp_thrmask);
	qgrp_elem->pending_modify = 0;

	env_spinlock_unlock(&qgrp_elem->lock);

	if (num_notif > 0) {
		stat = send_notifs(num_notif, notif_tbl);
		if (unlikely(stat != EM_OK))
			INTERNAL_ERROR(stat, escope, "Sending notifs failed!");
	}

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
 * queue_group_modify/_sync() helper:
 * Can only set core mask bits for running cores - verify this.
 */
static em_status_t
check_qgrp_mask(em_queue_group_t queue_group, const em_core_mask_t *new_mask,
		em_escope_t escope)
{
	const int core_count = em_core_count();
	em_core_mask_t max_mask;
	em_core_mask_t tmp_mask;

	/*
	 * 'new_mask' can contain set bits only for cores running EM,
	 * 'max_mask' contains all allowed set bits. Check that new_mask
	 * only contains set bits that are also found in max_mask.
	 */
	em_core_mask_zero(&max_mask);
	em_core_mask_set_count(core_count, &max_mask);

	em_core_mask_or(&tmp_mask, new_mask, &max_mask);
	if (unlikely(!em_core_mask_equal(&tmp_mask, &max_mask))) {
		char new_mstr[EM_CORE_MASK_STRLEN];
		char max_mstr[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(new_mstr, EM_CORE_MASK_STRLEN, new_mask);
		em_core_mask_tostr(max_mstr, EM_CORE_MASK_STRLEN, &max_mask);
		return INTERNAL_ERROR(EM_ERR_TOO_LARGE, escope,
				      "Queue grp:%" PRI_QGRP "- Inv mask:%s, max valid:%s",
				      queue_group, new_mstr, max_mstr);
	}

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
	if (unlikely(qgrp_elem->pending_modify)) {
		*err_str = "Contending queue group modify ongoing";
		return EM_ERR_BAD_STATE;
	}
	if (unlikely(is_delete && !list_is_empty(&qgrp_elem->queue_list))) {
		*err_str = "Queue group contains queues in delete";
		return EM_ERR_NOT_FREE;
	}

	return EM_OK;
}

/*
 * queue_group_modify/_sync() helper: modify the qgrp's odp schedule group
 */
static int
modify_odp_schedgrp(odp_schedule_group_t odp_sched_group,
		    const odp_thrmask_t *odp_new_mask, int adds, int rems)
{
	int ret = 0;

	if (rems > 0) {
		odp_thrmask_t odp_leave_mask;
		odp_thrmask_t odp_all_mask;

		odp_thrmask_setall(&odp_all_mask);
		odp_thrmask_xor(&odp_leave_mask, &odp_all_mask, odp_new_mask);
		ret = odp_schedule_group_leave(odp_sched_group,
					       &odp_leave_mask);
	}

	if (adds > 0)
		ret |= odp_schedule_group_join(odp_sched_group, odp_new_mask);

	return ret;
}

/*
 * queue_group_modify/_sync() helper: count cores to be added to the queue group
 */
static int count_qgrp_adds(const em_core_mask_t *old_mask,
			   const em_core_mask_t *new_mask)
{
	int core_count = em_core_count();
	int adds = 0;

	/* Count added cores */
	for (int i = 0; i < core_count; i++) {
		if (!em_core_mask_isset(i, old_mask) &&
		    em_core_mask_isset(i, new_mask))
			adds++;
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

/*
 * queue_group_modify/_sync() helper: send qgrp rem-req events to cores
 */
static em_status_t
send_qgrp_rem_reqs(queue_group_elem_t *qgrp_elem,
		   const em_core_mask_t *new_mask, const em_core_mask_t *rem_mask,
		   int num_notif, const em_notif_t notif_tbl[],
		   em_escope_t escope)
{
	em_event_t callback_args_event =
		em_alloc(sizeof(q_grp_modify_done_callback_args_t),
			 EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	em_event_t ctrl_event =
		em_alloc(sizeof(internal_event_t),
			 EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	if (unlikely(callback_args_event == EM_EVENT_UNDEF ||
		     ctrl_event == EM_EVENT_UNDEF))
		return EM_ERR_ALLOC_FAILED;

	/* Init the 'done'-callback function arguments */
	q_grp_modify_done_callback_args_t *callback_args =
		em_event_pointer(callback_args_event);
	callback_args->qgrp_elem = qgrp_elem;
	em_core_mask_copy(&callback_args->new_mask, new_mask);

	/* f_done_callback(f_done_arg_ptr): */
	void (*f_done_callback)(void *arg_ptr);
	void  *f_done_arg_ptr = callback_args_event;

	switch (escope) {
	case EM_ESCOPE_QUEUE_GROUP_MODIFY:
		f_done_callback = q_grp_modify_done_callback;
		break;
	case EM_ESCOPE_QUEUE_GROUP_DELETE:
		f_done_callback = q_grp_delete_done_callback;
		break;
	case EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC:
		f_done_callback = q_grp_modify_sync_done_callback;
		break;
	case EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC:
		f_done_callback = q_grp_delete_sync_done_callback;
		break;
	default:
		return EM_ERR_NOT_FOUND;
	}

	/* Init the QUEUE_GROUP_REM_REQ internal ctrl event(s) */
	internal_event_t *i_event = em_event_pointer(ctrl_event);

	i_event->id = QUEUE_GROUP_REM_REQ;
	i_event->q_grp.queue_group = qgrp_elem->queue_group;

	int ret = send_core_ctrl_events(rem_mask, ctrl_event /*copied to all*/,
					f_done_callback, f_done_arg_ptr,
					num_notif, notif_tbl);
	if (unlikely(ret))
		return EM_ERR_OPERATION_FAILED;

	return EM_OK;
}

/**
 * Called by em_queue_group_modify with flag is_delete=0 and by
 * em_queue_group_delete() with flag is_delete=1
 *
 * @param is_delete  Modify triggered by em_queue_group_delete()? 1=Yes, 0=No
 */
em_status_t
queue_group_modify(queue_group_elem_t *const qgrp_elem,
		   const em_core_mask_t *new_mask,
		   int num_notif, const em_notif_t notif_tbl[],
		   bool is_delete)
{
	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	em_status_t err;
	const char *err_str = "";
	const em_escope_t escope = is_delete ? EM_ESCOPE_QUEUE_GROUP_DELETE :
					       EM_ESCOPE_QUEUE_GROUP_MODIFY;

	/* Can only set core mask bits for running cores - verify this */
	err = check_qgrp_mask(queue_group, new_mask, escope);
	if (unlikely(err != EM_OK))
		return err;

	env_spinlock_lock(&qgrp_elem->lock);

	/* Check Queue Group state */
	err = check_qgrp_state(qgrp_elem, is_delete, &err_str/*out*/);
	if (unlikely(err != EM_OK)) {
		env_spinlock_unlock(&qgrp_elem->lock);
		return INTERNAL_ERROR(err, escope, err_str);
	}

	/*
	 * If the new mask is equal to the one in use:
	 * send notifs immediately and return.
	 */
	em_core_mask_t old_mask;

	em_core_mask_copy(&old_mask, &qgrp_elem->core_mask);

	if (em_core_mask_equal(&old_mask, new_mask)) {
		/* New mask == curr mask, or both zero, send notifs & return */
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);

		env_spinlock_unlock(&qgrp_elem->lock);

		err = send_notifs(num_notif, notif_tbl);
		RETURN_ERROR_IF(err != EM_OK, err, escope,
				"notif sending failed");
		return EM_OK;
	}

	/* Catch contending modifies */
	qgrp_elem->pending_modify = 1;

	/* Count added & removed cores */
	em_core_mask_t rem_mask;
	int adds = count_qgrp_adds(&old_mask, new_mask);
	int rems = count_qgrp_rems(&old_mask, new_mask, &rem_mask /*out*/);

	/*
	 * Modify the EM queue group's corresponding ODP schedule group
	 */
	odp_thrmask_t odp_new_mask;
	int ret;

	mask_em2odp(new_mask, &odp_new_mask);

	ret = modify_odp_schedgrp(qgrp_elem->odp_sched_group, &odp_new_mask,
				  adds, rems);
	if (unlikely(ret != 0)) {
		env_spinlock_unlock(&qgrp_elem->lock);
		return INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED), escope,
				      "ODP sched grp mod failed(%d)", ret);
	}

	if (rems == 0) {
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);
		else
			q_grp_modify_done(qgrp_elem, new_mask);

		env_spinlock_unlock(&qgrp_elem->lock);

		err = send_notifs(num_notif, notif_tbl);
		RETURN_ERROR_IF(err != EM_OK, err, escope,
				"notif sending failed");
		return EM_OK; /* return: no cores to remove */
	}

	env_spinlock_unlock(&qgrp_elem->lock);

	/*
	 * Note: .pending_modify = 1:
	 *       Threat all errors as EM_FATAL because failures will leave
	 *       .pending_modify = 1 for the group until restart.
	 */

	err = send_qgrp_rem_reqs(qgrp_elem, new_mask, &rem_mask,
				 num_notif, notif_tbl, escope);
	RETURN_ERROR_IF(err != EM_OK, EM_FATAL(err), escope,
			"qgrp rem req(s) sending failed");

	return EM_OK;
}

/**
 * Called by em_queue_group_modify_sync with flag is_delete=0 and by
 * em_queue_group_delete_sync() with flag is_delete=1
 *
 * @param is_delete  Modify triggered by em_queue_group_delete()? 1=Yes, 0=No
 */
em_status_t
queue_group_modify_sync(queue_group_elem_t *const qgrp_elem,
			const em_core_mask_t *new_mask, bool is_delete)
{
	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	em_status_t err = EM_OK;
	const char *err_str = "";
	int lock_taken;
	const em_escope_t escope = is_delete ? EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC
					: EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC;

	/* Can only set core mask bits for running cores - verify this */
	err = check_qgrp_mask(queue_group, new_mask, escope);
	if (unlikely(err != EM_OK))
		return err;

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(!lock_taken, EM_ERR_NOT_FREE, escope,
			"Another sync API function in progress");

	/* Sync APIs locked: */

	/* Take the API-caller lock */
	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_caller);
	if (unlikely(!lock_taken)) {
		env_spinlock_unlock(&em_shm->sync_api.lock_global);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, escope,
				      "Sync API-caller lock taken");
	}

	env_spinlock_lock(&qgrp_elem->lock);

	/* Check Queue Group state */
	err = check_qgrp_state(qgrp_elem, is_delete, &err_str/*out*/);
	if (unlikely(err != EM_OK)) {
		env_spinlock_unlock(&qgrp_elem->lock);
		goto queue_group_modify_sync_error;
	}

	/*
	 * If the new mask is equal to the one in use.
	 */
	em_core_mask_t old_mask;

	em_core_mask_copy(&old_mask, &qgrp_elem->core_mask);

	if (em_core_mask_equal(&old_mask, new_mask)) {
		/* New mask == curr mask, or both zero */
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);

		env_spinlock_unlock(&qgrp_elem->lock);

		err = EM_OK;
		goto queue_group_modify_sync_error; /* no error, just return */
	}

	/* Catch contending modifies */
	qgrp_elem->pending_modify = 1;

	env_spinlock_unlock(&qgrp_elem->lock);

	/* Count added & removed cores */
	em_core_mask_t rem_mask;
	int adds = count_qgrp_adds(&old_mask, new_mask);
	int rems = count_qgrp_rems(&old_mask, new_mask, &rem_mask /*out*/);

	/* Modify the ODP schedule group */
	odp_thrmask_t odp_new_mask;
	int ret;

	mask_em2odp(new_mask, &odp_new_mask);
	ret = modify_odp_schedgrp(qgrp_elem->odp_sched_group, &odp_new_mask,
				  adds, rems);
	if (unlikely(ret != 0)) {
		err = EM_FATAL(EM_ERR_LIB_FAILED);
		goto queue_group_modify_sync_error;
	}

	/*
	 * Remove the calling core from the remove-mask and -count since no
	 * rem-req event should be sent to it during this _sync operation.
	 */
	int core = em_core_id();

	if (rems > 0 && em_core_mask_isset(core, &rem_mask)) {
		em_core_mask_clr(core, &rem_mask);
		rems--;
	}

	/* No cores to send rem-reqs to, mark operation done and return */
	if (rems == 0) {
		if (is_delete)
			q_grp_delete_done(qgrp_elem, new_mask);
		else
			q_grp_modify_done(qgrp_elem, new_mask);
		err = EM_OK;
		goto queue_group_modify_sync_error; /* no error, just return  */
	}

	/*
	 * Send rem-req events to all other conserned cores.
	 * Note: .pending_modify = 1:
	 *       Threat all errors as EM_FATAL because failures will leave
	 *       .pending_modify = 1 for the group until restart.
	 */

	err = send_qgrp_rem_reqs(qgrp_elem, new_mask, &rem_mask,
				 0, NULL, escope);
	if (unlikely(err != EM_OK)) {
		err = EM_FATAL(err);
		goto queue_group_modify_sync_error;
	}

	/*
	 * Spin on the lock until q_grp_modify_sync_done_callback()
	 * unlocks when the operation has completed.
	 */
	env_spinlock_lock(&em_shm->sync_api.lock_caller);

queue_group_modify_sync_error:
	env_spinlock_unlock(&em_shm->sync_api.lock_caller);
	env_spinlock_unlock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(err != EM_OK, err, escope,
			"Failure: Modify sync QGrp:%" PRI_QGRP ":%s",
			queue_group, err_str);

	return EM_OK;
}

/**
 * Callback function when a em_queue_group_modify()
 * completes with the internal DONE-event
 */
static void
q_grp_modify_done_callback(void *arg_ptr)
{
	em_event_t event = (em_event_t)arg_ptr;
	const q_grp_modify_done_callback_args_t *args = em_event_pointer(event);
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
static void
q_grp_modify_sync_done_callback(void *arg_ptr)
{
	q_grp_modify_done_callback(arg_ptr);

	/* Enable the caller of the sync API func to proceed */
	env_spinlock_unlock(&em_shm->sync_api.lock_caller);
}

static void
q_grp_modify_done(queue_group_elem_t *const qgrp_elem,
		  const em_core_mask_t *const new_mask)
{
	/* Now modify is complete, update the mask */
	em_core_mask_copy(&qgrp_elem->core_mask, new_mask);
	qgrp_elem->pending_modify = 0;
}

/**
 * Callback function when a em_queue_group_modify(delete flag set)
 * completes with the internal DONE-event
 */
static void
q_grp_delete_done_callback(void *arg_ptr)
{
	em_event_t event = (em_event_t)arg_ptr;
	const q_grp_modify_done_callback_args_t *args = em_event_pointer(event);
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
static void
q_grp_delete_sync_done_callback(void *arg_ptr)
{
	q_grp_delete_done_callback(arg_ptr);

	/* Enable the caller of the sync API func to proceed */
	env_spinlock_unlock(&em_shm->sync_api.lock_caller);
}

static void
q_grp_delete_done(queue_group_elem_t *const qgrp_elem,
		  const em_core_mask_t *const new_mask)
{
	/* Sanity check: new core mask for delete is always zero */
	if (unlikely(!em_core_mask_iszero(new_mask))) {
		char mstr[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mstr, EM_CORE_MASK_STRLEN, new_mask);
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE),
			       EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "Queue group mask not zero in delete:%s", mstr);
	}

	if (unlikely(!list_is_empty(&qgrp_elem->queue_list)))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_FREE),
			       EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "Queue group contains queues, cannot delete!");

	int ret = odp_schedule_group_destroy(qgrp_elem->odp_sched_group);

	if (unlikely(ret != 0))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED),
			       EM_ESCOPE_QUEUE_GROUP_DELETE,
			       "ODP sched group destroy fail, cannot delete!");

	qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;

	/* Now modify/delete is complete, zero the mask */
	em_core_mask_zero(&qgrp_elem->core_mask);
	qgrp_elem->pending_modify = 0;

	/* Free the queue group */
	queue_group_free(qgrp_elem->queue_group);
}

void
queue_group_add_queue_list(queue_group_elem_t *const queue_group_elem,
			   queue_elem_t *const queue_elem)
{
	env_spinlock_lock(&queue_group_elem->lock);
	list_add(&queue_group_elem->queue_list, &queue_elem->qgrp_node);
	env_atomic32_inc(&queue_group_elem->num_queues);
	env_spinlock_unlock(&queue_group_elem->lock);
}

void
queue_group_rem_queue_list(queue_group_elem_t *const queue_group_elem,
			   queue_elem_t *const queue_elem)
{
	env_spinlock_lock(&queue_group_elem->lock);
	if (!list_is_empty(&queue_group_elem->queue_list)) {
		list_rem(&queue_group_elem->queue_list, &queue_elem->qgrp_node);
		env_atomic32_dec(&queue_group_elem->num_queues);
	}
	env_spinlock_unlock(&queue_group_elem->lock);
}

unsigned int
queue_group_count(void)
{
	return env_atomic32_get(&em_shm->queue_group_count);
}

void
print_queue_group_info(void)
{
	em_queue_group_t queue_group;
	em_core_mask_t core_mask;
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
	char mask_str[EM_CORE_MASK_STRLEN];
	unsigned int num;

	EM_PRINT("EM Queue groups\n"
		 "---------------\n"
		 "  id      name   mask\n");

	queue_group = em_queue_group_get_first(&num);

	while (queue_group != EM_QUEUE_GROUP_UNDEF) {
		em_queue_group_get_name(queue_group, qgrp_name,
					sizeof(qgrp_name));
		em_queue_group_get_mask(queue_group, &core_mask);
		em_core_mask_tostr(mask_str, sizeof(mask_str), &core_mask);

		EM_PRINT("  %-6" PRI_QGRP "%8s %s\n",
			 queue_group, qgrp_name, mask_str);

		/* next queue group */
		queue_group = em_queue_group_get_next();
	}

	EM_PRINT("\n");
}
