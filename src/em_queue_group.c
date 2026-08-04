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
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#include "em_core.h"
#include "em_core_types.h"
#include "em_error.h"
#include "em_event_group.h"
#include "em_init.h"
#include "em_internal_event.h"
#include "em_internal_event_types.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_queue_group_types.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"
#include "misc/list.h"
#include "misc/objpool.h"

/**
 * em_queue_group_modify() triggers an internal 'Done'-notification event
 * that updates the queue group mask. This struct contains the callback args.
 */
typedef struct {
	queue_group_elem_t *qgrp_elem;
	em_core_mask_t new_mask;
} q_grp_done_callback_args_t;

/*
 * Assert that the value of EM_MAX_QUEUE_GROUPS is large enough to support
 * per-core queue groups (up to EM_MAX_CORES), the default queue group
 * as well as potential further queue groups the user might want to create.
 */
COMPILE_TIME_ASSERT(EM_MAX_QUEUE_GROUPS > EM_MAX_CORES,
		    EM_MAX_QUEUE_GROUPS__TOO_SMALL);

static em_status_t core_queue_group_create(void);
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
	 * Option: queue_group.create_core_queue_groups
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

	/*
	 * Option: queue_group.term_local_flush_orphans
	 */
	conf_str = "queue_group.term_local_flush_orphans";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.queue_group.term_local_flush_orphans = val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	return 0;
}

/**
 * Queue group inits done at global init (once at startup on one core)
 */
em_status_t queue_group_init(queue_group_tbl_t *const queue_group_tbl,
			     queue_group_pool_t *const queue_group_pool)
{
	const uint32_t num_subpools = MIN(4, OBJSUBPOOLS_MAX);
	queue_group_elem_t *queue_group_elem;
	int ret;

	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	memset(queue_group_tbl, 0, sizeof(queue_group_tbl_t));
	memset(queue_group_pool, 0, sizeof(queue_group_pool_t));
	odp_atomic_init_u32(&em_shm->queue_group_count, 0);

	for (int i = 0; i < EM_MAX_QUEUE_GROUPS; i++) {
		queue_group_elem = &queue_group_tbl->queue_group_elem[i];
		queue_group_elem->queue_group = qgrp_idx2hdl(i);
		/* Initialize empty queue list */
		odp_ticketlock_init(&queue_group_elem->lock);
		list_init(&queue_group_elem->queue_list);
	}

	/*
	 * objpool for core specific queue group elems (1 -> EM_MAX_CORES)
	 */
	ret = objpool_init(&queue_group_pool->objpool_rsvd, num_subpools);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (uint32_t i = 0; i < EM_MAX_CORES; i++) {
		queue_group_elem = &queue_group_tbl->queue_group_elem[i];
		objpool_add(&queue_group_pool->objpool_rsvd, i % num_subpools,
			    &queue_group_elem->queue_group_pool_elem);
	}

	/*
	 * objpool for the rest of the queue group elems
	 * (EM_MAX_CORES + 1 -> EM_MAX_QUEUE_GROUPS)
	 */
	ret = objpool_init(&queue_group_pool->objpool_dyn, num_subpools);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (uint32_t i = EM_MAX_CORES; i < EM_MAX_QUEUE_GROUPS; i++) {
		queue_group_elem = &queue_group_tbl->queue_group_elem[i];
		objpool_add(&queue_group_pool->objpool_dyn, i % num_subpools,
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
	 * Create EM single-core queue group if enabled by config.
	 */
	if (em_shm->opt.queue_group.create_core_queue_groups) {
		em_status_t stat = core_queue_group_create();

		if (stat != EM_OK) {
			EM_LOG(EM_LOG_ERR, "core_queue_group_create():%" PRI_STAT "\n", stat);
			return stat;
		}

		/*
		 * Update the single-core queue group with this core's information
		 */
		stat = core_queue_group_join();

		if (stat != EM_OK) {
			EM_LOG(EM_LOG_ERR, "core_queue_group_join():%" PRI_STAT "\n", stat);
			return stat;
		}
	}

	return EM_OK;
}

em_status_t queue_group_term(void)
{
	em_status_t stat = EM_OK;
	em_queue_group_t qgrp = em_queue_group_first(NULL);

	/*
	 * Destroy all ODP schedule groups to prevent resource leaks
	 * and allow proper re-initialization.
	 */
	while (qgrp != EM_QUEUE_GROUP_UNDEF) {
		queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

		if (unlikely(!qgrp_elem)) {
			EM_LOG(EM_LOG_ERR, "%s(): qgrp_elem NULL\n", __func__);
			continue;
		}

		if (unlikely(qgrp_elem->ongoing_delete)) {
			stat = EM_ERR_BAD_STATE;
			EM_LOG(EM_LOG_ERR,
			       "Bad state qgrp:%" PRI_QGRP " delete is ongoing\n", qgrp);
			/* Continue cleanup even if delete is ongoing */
		}

		if (unlikely(odp_ticketlock_is_locked(&qgrp_elem->lock))) {
			stat = EM_ERR_BAD_STATE;
			EM_LOG(EM_LOG_ERR, "Bad state qgrp:%" PRI_QGRP " - lock is locked\n", qgrp);
			/* Continue cleanup even if lock is locked */
		}

		if (qgrp_elem->odp_sched_group != ODP_SCHED_GROUP_INVALID) {
			int ret = odp_schedule_group_destroy(qgrp_elem->odp_sched_group);

			if (unlikely(ret != 0)) {
				EM_LOG(EM_LOG_ERR,
				       "ODP schedule group destroy failed qgrp:%"
				       PRI_QGRP " ret:%d\n", qgrp, ret);
				stat = EM_ERR_LIB_FAILED;
				/* Continue cleanup even if destroy fails */
			}

			qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;
		}

		qgrp = em_queue_group_next();
	}

	return stat;
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
	queue_group_pool_t *const qgrp_pools = &em_shm->queue_group_pool;

	if (queue_group == EM_QUEUE_GROUP_UNDEF) {
		/*
		 * Allocate any queue group, i.e. take next available
		 */
		int core = em_core_id();

		qgrp_pool_elem = objpool_rem(&qgrp_pools->objpool_dyn, core);

		if (unlikely(qgrp_pool_elem == NULL)) {
			/* try the core specific queue group pool next */
			qgrp_pool_elem = objpool_rem(&qgrp_pools->objpool_rsvd, core);
			if (unlikely(qgrp_pool_elem == NULL))
				return EM_QUEUE_GROUP_UNDEF;
		}

		qgrp_elem = queue_group_poolelem2qgrpelem(qgrp_pool_elem);
	} else {
		/*
		 * Allocate a specific queue group, handle given as argument
		 */
		objpool_t *const objpool = qgrp_hdl2idx(queue_group) < EM_MAX_CORES ?
					   &qgrp_pools->objpool_rsvd : &qgrp_pools->objpool_dyn;

		qgrp_elem = queue_group_elem_get(queue_group);
		if (unlikely(qgrp_elem == NULL))
			return EM_QUEUE_GROUP_UNDEF;

		odp_ticketlock_lock(&qgrp_elem->lock);
		/* Verify that the queue group is not allocated */
		if (queue_group_allocated(qgrp_elem)) {
			odp_ticketlock_unlock(&qgrp_elem->lock);
			return EM_QUEUE_GROUP_UNDEF;
		}

		/* Remove the queue group from the pool */
		int ret = objpool_rem_elem(objpool, &qgrp_elem->queue_group_pool_elem);

		odp_ticketlock_unlock(&qgrp_elem->lock);
		if (unlikely(ret != 0))
			return EM_QUEUE_GROUP_UNDEF;
	}

	odp_atomic_inc_u32(&em_shm->queue_group_count);
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
	objpool_t *const objpool = qgrp_hdl2idx(queue_group) < EM_MAX_CORES ?
				   &em_shm->queue_group_pool.objpool_rsvd :
				   &em_shm->queue_group_pool.objpool_dyn;

	if (unlikely(queue_group_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(objpool,
		    queue_group_elem->queue_group_pool_elem.subpool_idx,
		    &queue_group_elem->queue_group_pool_elem);

	odp_atomic_dec_u32(&em_shm->queue_group_count);
	return EM_OK;
}

/**
 * Create a new ODP schedule group
 *
 * @return ODP schedule group handle
 */
static odp_schedule_group_t
create_odp_schedule_group(const char *name, const odp_thrmask_t *thrmask)
{
	odp_schedule_group_t odp_sched_group = ODP_SCHED_GROUP_INVALID;

	odp_schedule_group_param_t param;

	odp_schedule_group_param_init(&param);

	/* Optimize cache stashing hints for EM */
	param.cache_stash_hints.common.regions.event_data_l2 = 1;      /* EM event data */
	param.cache_stash_hints.common.regions.event_user_area_l2 = 1; /* EM evhdr (+ EM uarea) */
	param.cache_stash_hints.common.regions.queue_context_l2 = 1;   /* EM queue elem */

	/* EM event data: */
	param.cache_stash_hints.common.event_data.l2.offset = 0;
	param.cache_stash_hints.common.event_data.l2.len = ODP_CACHE_LINE_SIZE;
	/* EM evhdr + uarea: */
	param.cache_stash_hints.common.event_user_area.l2.offset = 0;
	param.cache_stash_hints.common.event_user_area.l2.len = sizeof(event_hdr_t);
	/* EM queue elem: */
	param.cache_stash_hints.common.queue_context.l2.offset = 0;
	param.cache_stash_hints.common.queue_context.l2.len = MIN(sizeof(queue_elem_t),
								  ODP_CACHE_LINE_SIZE);
	odp_sched_group = odp_schedule_group_create_2(name, thrmask, &param);
	return odp_sched_group;
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

	odp_thrmask_zero(&zero_thrmask);

	/*
	 * Create a new odp schedule group for the EM default queue group.
	 * Don't use the ODP_SCHED_GROUP_WORKER or other predefined ODP groups
	 * since those groups can't be modified.
	 * Create the group without any cores/threads and update it during
	 * em_init_local/core() -> default_queue_group_join() calls for each
	 * EM core.
	 */
	default_qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;

	odp_sched_group = create_odp_schedule_group(EM_QUEUE_GROUP_DEFAULT_NAME, &zero_thrmask);
	if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID)) {
		DBG_PRINT("ODP schedule group creation failed for 'EM_QUEUE_GROUP_DEFAULT'\n");
		return EM_QUEUE_GROUP_UNDEF;
	}

	/* Store the created odp sched group as the EM default queue group */
	default_qgrp_elem->odp_sched_group = odp_sched_group;

	return EM_QUEUE_GROUP_DEFAULT;
}

/**
 * Update the EM default queue group with valid group information for each
 * core local init and add the ODP thread-id to the scheduling mask.
 * Run by each call to em_init_local/core() for EM worker and control cores.
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

	odp_ticketlock_lock(&default_qgrp_elem->lock);
	em_core_mask_set(core_id, &default_qgrp_elem->core_mask);
	/* Join this thread into the "EM default" schedule group */
	ret = odp_schedule_group_join(default_qgrp_elem->odp_sched_group,
				      &odp_joinmask);
	odp_ticketlock_unlock(&default_qgrp_elem->lock);

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
	em_queue_group_t qgrp = em_queue_group_first(NULL);
	const int core_id = em_locm.core_id;

	while (qgrp != EM_QUEUE_GROUP_UNDEF) {
		queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

		odp_ticketlock_lock(&qgrp_elem->lock);

		int allocated = queue_group_allocated(qgrp_elem);
		bool ongoing_delete = qgrp_elem->ongoing_delete;

		if (allocated && !ongoing_delete &&
		    !em_core_mask_isset(core_id, &qgrp_elem->core_mask)) {
			em_core_mask_set(core_id, &qgrp_elem->core_mask);
			q_grp_add_core(qgrp_elem);
		}
		odp_ticketlock_unlock(&qgrp_elem->lock);

		qgrp = em_queue_group_next();
	}
}

static inline queue_group_elem_t *
list_node_to_qgrp_elem(const list_node_t *const list_node)
{
	queue_group_elem_t *qgrp_elem = (queue_group_elem_t *)((uintptr_t)list_node
					- offsetof(queue_group_elem_t, qgrp_list_node));

	return likely(list_node != NULL) ? qgrp_elem : NULL;
}

/**
 * @brief The calling core leaves all queue groups it is part of
 *
 * Also flushes the scheduler from events from queues that belong to
 * queue groups that would only be services by the terminating core.
 *
 * Main use case for em_term_local()/em_term_core()
 *
 * @param flush_orphan_qgrps  Flush the scheduler from events from queues that
 *                            belong to queue groups that only this core is
 *                            handling queues for.
 */
void queue_group_leave_all(bool flush_orphan_qgrps)
{
	em_queue_group_t qgrp = em_queue_group_first(NULL);

	if (qgrp == EM_QUEUE_GROUP_UNDEF)
		return;

	const int core_id = em_locm.core_id;
	queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);
	em_core_mask_t this_core_only;
	list_node_t qgrp_list;

	em_core_mask_zero(&this_core_only);
	em_core_mask_set(core_id, &this_core_only);
	list_init(&qgrp_list);

	while (qgrp_elem) {
		odp_ticketlock_lock(&qgrp_elem->lock);

		int allocated = queue_group_allocated(qgrp_elem);
		bool ongoing_delete = qgrp_elem->ongoing_delete;
		bool do_unlock = true;

		if (allocated && !ongoing_delete &&
		    em_core_mask_isset(core_id, &qgrp_elem->core_mask)) {
			if (flush_orphan_qgrps &&
			    em_core_mask_equal(&this_core_only, &qgrp_elem->core_mask) &&
			    !list_is_empty(&qgrp_elem->queue_list)) {
				/* No other cores handling queues in this qgrp - add to a list */
				list_add(&qgrp_list, &qgrp_elem->qgrp_list_node);
				/* keep the qgrp elems in the list locked */
				do_unlock = false;
			} else {
				/* Remove the EM-core from the queue group */
				q_grp_rem_core(qgrp_elem);
				em_core_mask_clr(core_id, &qgrp_elem->core_mask);
			}
		}
		if (do_unlock)
			odp_ticketlock_unlock(&qgrp_elem->lock);

		qgrp = em_queue_group_next();
		/* qgrp_elem is NULL for EM_QUEUE_GROUP_UNDEF */
		qgrp_elem = queue_group_elem_get(qgrp);
	}

	/* can contain qgrps only if 'flush_orphan_qgrps = true' */
	if (!list_is_empty(&qgrp_list)) {
		em_locm_t *const locm = &em_locm;
		/*
		 * Flush the scheduler from events from queues that belong to
		 * queue groups that only this core is handling queues for.
		 * This is needed to avoid events being stuck in the scheduler
		 * when an EM-core is terminating.
		 */
		odp_schedule_resume();
		locm->is_sched_paused = false;
		flush_scheduler_events();
		/*
		 * Flush the scheduler from locally stashed events.
		 */
		odp_schedule_pause();
		locm->is_sched_paused = true;
		flush_scheduler_events();

		/*
		 * Drain the list of qgrp-elems that only this core is
		 * handling: remove this core from them and unlock them.
		 *
		 * Use list_rem_first() to properly unlink each node from
		 * the stack-local 'qgrp_list', ensuring no global
		 * qgrp_list_node retains a back-pointer to the stack.
		 */
		const list_node_t *list_node;

		while ((list_node = list_rem_first(&qgrp_list)) != NULL) {
			qgrp_elem = list_node_to_qgrp_elem(list_node);
			/* Remove the EM-core from the queue group */
			q_grp_rem_core(qgrp_elem);
			em_core_mask_clr(core_id, &qgrp_elem->core_mask);
			/* unlock the qgrp-elem that was locked earlier */
			odp_ticketlock_unlock(&qgrp_elem->lock);
		}
	}
}

static em_status_t core_queue_group_create(void)
{
	em_queue_group_t qgrp;
	em_queue_group_t qgrp_req;
	queue_group_elem_t *qgrp_elem;
	em_core_mask_t *mask;
	odp_thrmask_t zero_thrmask;
	odp_schedule_group_t odp_sched_group;
	const int core = em_locm.core_id;
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];

	core_queue_grp_name(core, qgrp_name/*out*/, sizeof(qgrp_name));

	qgrp = em_queue_group_find(qgrp_name);
	if (qgrp != EM_QUEUE_GROUP_UNDEF) {
		DBG_PRINT("%s(): core:%d, %s already created - reusing\n",
			  __func__, core, qgrp_name);

		qgrp_elem = queue_group_elem_get(qgrp);

		if (unlikely(!qgrp_elem || !queue_group_allocated(qgrp_elem))) {
			DBG_PRINT("%s(): qgrp_elem invalid for core-qgrp:%d\n",
				  __func__, core);
			return EM_ERR_BAD_POINTER;
		}

		return EM_OK;
	}

	qgrp_req = qgrp_idx2hdl(core);
	qgrp = queue_group_alloc(qgrp_req);
	if (unlikely(qgrp == EM_QUEUE_GROUP_UNDEF || qgrp != qgrp_req)) {
		DBG_PRINT("queue_group_alloc() fails for core-qgrp:%d\n", core);
		return EM_ERR_ALLOC_FAILED;
	}

	qgrp_elem = queue_group_elem_get(qgrp);
	if (unlikely(qgrp_elem == NULL)) {
		DBG_PRINT("qgrp_elem NULL for core-qgrp:%d\n", core);
		return EM_ERR_BAD_POINTER;
	}

	mask = &qgrp_elem->core_mask;
	em_core_mask_zero(mask);
	em_core_mask_set(core, mask);

	odp_thrmask_zero(&zero_thrmask);

	/*
	 * Create a new odp schedule group for each EM core.
	 * Create the group without the core/thread set and update it
	 * during em_init_local/core() -> core_queue_group_join()
	 * calls for each EM core.
	 */
	qgrp_elem->odp_sched_group = ODP_SCHED_GROUP_INVALID;

	odp_sched_group = create_odp_schedule_group(qgrp_name, &zero_thrmask);
	if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID)) {
		DBG_PRINT("ODP schedule group creation failed for core-qgrp:%d\n", core);
		return EM_ERR_LIB_FAILED;
	}
	/* Store the created odp sched group for this EM queue group */
	qgrp_elem->odp_sched_group = odp_sched_group;

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
		DBG_PRINT("%s(): core:%d, %s not found\n", __func__, core, qgrp_name);
		return EM_ERR_NOT_FOUND;
	}

	queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

	if (unlikely(!qgrp_elem)) {
		DBG_PRINT("%s(): qgrp_elem NULL for core-qgrp:%d\n",
			  __func__, core);
		return EM_ERR_BAD_POINTER;
	}

	/* Set this thread in the odp schedule group join-mask */
	odp_thrmask_t odp_joinmask;

	odp_thrmask_zero(&odp_joinmask);
	odp_thrmask_set(&odp_joinmask, odp_thr);

	odp_ticketlock_lock(&qgrp_elem->lock);
	/* Join this thread into the core-local schedule group */
	int ret = odp_schedule_group_join(qgrp_elem->odp_sched_group,
					  &odp_joinmask);
	odp_ticketlock_unlock(&qgrp_elem->lock);

	if (unlikely(ret)) {
		DBG_PRINT("%s(): odp_schedule_group_join():%d, core-qgrp:%d\n",
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
	core_map_t *const core_map = &em_shm->core_map;
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
	odp_sched_group = create_odp_schedule_group(name, &zero_thrmask);
	if (unlikely(odp_sched_group == ODP_SCHED_GROUP_INVALID)) {
		queue_group_free(queue_group);
		(void)INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, escope,
				     "ODP sched group creation for EM queue group (\"%s\") failed!",
				     name);
		return EM_QUEUE_GROUP_UNDEF;
	}

	/*
	 * Take the core_map rwlock for reading - can afford to keep it for a
	 * "long" time since blocking write-access is only needed in core
	 * add/rem scenarios (via em_init/term_local() and the older
	 * em_init/term_core()). The core ctrl events should be sent before
	 * releasing the lock to prevent add/rem-core actions while sending
	 * these ctrl events.
	 */
	odp_rwlock_read_lock(&core_map->rwlock);

	stat = queue_group_check_mask(mask);
	if (unlikely(stat != EM_OK)) {
		odp_rwlock_read_unlock(&core_map->rwlock);
		odp_schedule_group_destroy(odp_sched_group);
		queue_group_free(queue_group);

		/* use mstr len > EM_CORE_MASK_STRLEN on mask-error */
		char mstr[ODP_CPUMASK_STR_SIZE];

		em_core_mask_tostr(mstr, sizeof(mstr), mask);
		INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
			       "Invalid mask given:%s", mstr);

		return EM_QUEUE_GROUP_UNDEF;
	}

	em_core_mask_copy(&add_mask, mask);

	odp_ticketlock_lock(&qgrp_elem->lock);

	/* Initialize the data of the newly allocated queue group */
	qgrp_elem->odp_sched_group = odp_sched_group;
	em_core_mask_copy(&qgrp_elem->core_mask, mask); /* set new mask */
	list_init(&qgrp_elem->queue_list);
	odp_atomic_init_u32(&qgrp_elem->num_queues, 0);
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

		odp_ticketlock_unlock(&qgrp_elem->lock);
		odp_rwlock_read_unlock(&core_map->rwlock);

		stat = send_notifs(num_notif, notif_tbl);
		if (unlikely(stat != EM_OK))
			INTERNAL_ERROR(stat, escope, "Sending notifs failed!");

		return queue_group;
	}

	odp_ticketlock_unlock(&qgrp_elem->lock);

	stat = send_qgrp_addrem_reqs(qgrp_elem, mask, &add_mask, &rem_zero_mask,
				     num_notif, notif_tbl, escope);

	odp_rwlock_read_unlock(&core_map->rwlock);

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
 * Must be called with em_shm->core_map.rwlock locked for reads.
 */
em_status_t queue_group_check_mask(const em_core_mask_t *mask)
{
	const core_map_t *const core_map = &em_shm->core_map;
	em_core_mask_t check_mask;

	em_core_mask_and(&check_mask, mask, &core_map->rwlocked.logic_mask);

	if (unlikely(!em_core_mask_equal(&check_mask, mask)))
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
 * queue_group_modify/_sync() helper: determine cores to be added to the queue group
 */
static int count_qgrp_adds(const em_core_mask_t *old_mask,
			   const em_core_mask_t *new_mask,
			   em_core_mask_t *add_mask /*out*/)
{
	em_core_mask_t changed_mask;

	/* get changed cores (added and removed ones) */
	em_core_mask_xor(&changed_mask, old_mask, new_mask);
	/* cores that were added in the new mask */
	em_core_mask_and(add_mask, &changed_mask, new_mask);

	int adds = em_core_mask_count(add_mask);

	return adds;
}

/*
 * queue_group_modify/_sync() helper: determine cores to be removed from the queue group
 */
static int count_qgrp_rems(const em_core_mask_t *old_mask,
			   const em_core_mask_t *new_mask,
			   em_core_mask_t *rem_mask /*out*/)
{
	em_core_mask_t changed_mask;

	/* get changed cores (added and removed ones) */
	em_core_mask_xor(&changed_mask, old_mask, new_mask);
	/* cores that were removed from the old mask */
	em_core_mask_and(rem_mask, &changed_mask, old_mask);

	int rems = em_core_mask_count(rem_mask);

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
 *
 * Converts the given EM core mask to an ODP thread mask and thus requires that
 * this function has been called with the core map rwlock taken:
 *         odp_rwlock_read_lock(&em_shm->core_map.rwlock)
 */
static em_status_t send_addrem_events(em_event_t addrem_events[], int count,
				      const em_core_mask_t *mask,
				      em_event_group_t event_group)
{
	const int first_qidx = queue_id2idx(em_shm->queue_tbl.first_internal_queue_id);
	int ev_idx = 0;
	em_status_t err;

	odp_thrmask_t thr_mask;
	int thr;

	/*
	 * Assumes lock is already taken:
	 * odp_rwlock_read_lock(&em_shm->core_map.rwlock)
	 * Caller locks and unlocks.
	 */
	mask_em2odp__rwlocked(mask, &thr_mask);

	thr = odp_thrmask_first(&thr_mask);
	while (thr >= 0 && ev_idx < count) {
		/*
		 * Send an add/rem-req to each core-specific unscheduled ctrl
		 * queue, track completion using an event group.
		 */
		err = send_ctrl_queue(addrem_events[ev_idx],
				      queue_idx2hdl(first_qidx + thr),
				      event_group);
		if (unlikely(err != EM_OK))
			return err;
		addrem_events[ev_idx] = EM_EVENT_UNDEF;
		ev_idx++;

		thr = odp_thrmask_next(&thr_mask, thr);
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
	em_event_t callback_args_event =
		em_alloc(sizeof(q_grp_done_callback_args_t),
			 EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	if (unlikely(callback_args_event == EM_EVENT_UNDEF))
		return EM_ERR_ALLOC_FAILED;

	/*
	 * Set the 'qgrp operation done'-callback func based on given
	 * escope (identifies operation).
	 *    f_done_callback(f_done_arg_ptr)
	 */
	void (*f_done_callback)(void *arg_ptr);
	void  *f_done_arg_ptr = callback_args_event;
	bool sync_operation = false;

	int ret = set_qgrp_done_func(escope, &f_done_callback/*out*/,
				     &sync_operation/*out*/);
	if (unlikely(ret)) {
		em_free(callback_args_event);
		return EM_ERR_NOT_FOUND;
	}

	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	const int add_count = em_core_mask_count(add_mask);
	const int rem_count = em_core_mask_count(rem_mask);
	const int addrem_count = add_count + rem_count; /* Subset of cores*/
	em_event_t add_events[add_count];
	em_event_t rem_events[rem_count];
	em_event_group_t event_group;
	em_status_t err;
	int cnt;

	/* Init the 'done'-callback function arguments */
	q_grp_done_callback_args_t *callback_args =
		em_event_pointer(callback_args_event);
	callback_args->qgrp_elem = qgrp_elem;
	em_core_mask_copy(&callback_args->new_mask, new_mask);

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
	err = send_addrem_events(rem_events, rem_count, rem_mask, event_group);
	if (unlikely(err != EM_OK))
		goto err_free_resources;
	/*
	 * Send add-req events to the concerned cores
	 */
	err = send_addrem_events(add_events, add_count, add_mask, event_group);

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
	core_map_t *const core_map = &em_shm->core_map;
	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	em_status_t err;
	const char *err_str = "";
	const em_escope_t escope = is_delete ? EM_ESCOPE_QUEUE_GROUP_DELETE :
					       EM_ESCOPE_QUEUE_GROUP_MODIFY;
	const int core = em_core_id();

	/*
	 * Take the core_map rwlock for reading - can afford to keep it for a
	 * "long" time since blocking write-access is only needed in core
	 * add/rem scenarios (via em_init/term_local() and the older
	 * em_init/term_core()). The core ctrl events should be sent before
	 * releasing the lock to prevent add/rem-core actions while sending
	 * these ctrl events.
	 */
	odp_rwlock_read_lock(&core_map->rwlock);

	err = queue_group_check_mask(new_mask);
	if (unlikely(err != EM_OK)) {
		odp_rwlock_read_unlock(&core_map->rwlock);

		/* use mstr len > EM_CORE_MASK_STRLEN on mask-error */
		char mstr[ODP_CPUMASK_STR_SIZE];

		em_core_mask_tostr(mstr, sizeof(mstr), new_mask);
		return INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
				      "Queue group:%" PRI_QGRP ", invalid mask given:%s",
				      queue_group, mstr);
	}

	odp_ticketlock_lock(&qgrp_elem->lock);

	/* Check Queue Group state */
	err = check_qgrp_state(qgrp_elem, is_delete, &err_str/*out*/);
	if (unlikely(err != EM_OK)) {
		odp_ticketlock_unlock(&qgrp_elem->lock);
		odp_rwlock_read_unlock(&core_map->rwlock);
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

		odp_ticketlock_unlock(&qgrp_elem->lock);
		odp_rwlock_read_unlock(&core_map->rwlock);

		err = send_notifs(num_notif, notif_tbl);
		RETURN_ERROR_IF(err != EM_OK, err, escope,
				"notif sending failed");
		return EM_OK;
	}

	/* Catch contending queue group operations while delete is ongoing */
	if (is_delete)
		qgrp_elem->ongoing_delete = true;

	odp_ticketlock_unlock(&qgrp_elem->lock);

	/*
	 * Send add/rem-req events to all other concerned cores.
	 * Note: if .ongoing_delete = true:
	 *       Treat errors as EM_FATAL because failures will leave
	 *       .ongoing_delete = true for the group until restart of EM.
	 */
	err = send_qgrp_addrem_reqs(qgrp_elem, new_mask, &add_mask, &rem_mask,
				    num_notif, notif_tbl, escope);

	odp_rwlock_read_unlock(&core_map->rwlock);

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
	core_map_t *const core_map = &em_shm->core_map;
	em_locm_t *const locm = &em_locm;
	const em_queue_group_t queue_group = qgrp_elem->queue_group;
	em_status_t err = EM_OK;
	const char *err_str = "";
	const em_escope_t escope = is_delete ? EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC
					: EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC;
	const int core = em_core_id();

	/*
	 * Take the core_map rwlock for reading - can afford to keep it for a
	 * "long" time since blocking write-access is only needed in core
	 * add/rem scenarios (via em_init/term_local() and the older
	 * em_init/term_core()). The core ctrl events should be sent before
	 * releasing the lock to prevent add/rem-core actions while sending
	 * these ctrl events.
	 */
	odp_rwlock_read_lock(&core_map->rwlock);

	err = queue_group_check_mask(new_mask);
	if (unlikely(err != EM_OK)) {
		odp_rwlock_read_unlock(&core_map->rwlock);

		/* use mstr len > EM_CORE_MASK_STRLEN on mask-error */
		char mstr[ODP_CPUMASK_STR_SIZE];

		em_core_mask_tostr(mstr, sizeof(mstr), new_mask);
		return INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
				      "Queue group:%" PRI_QGRP ", invalid core mask given:%s",
				      queue_group, mstr);
	}

	/* Mark that a sync-API call is in progress */
	locm->sync_api.in_progress = true;

	odp_ticketlock_lock(&qgrp_elem->lock);

	/* Check Queue Group state */
	err = check_qgrp_state(qgrp_elem, is_delete, &err_str/*out*/);
	if (unlikely(err != EM_OK)) {
		odp_ticketlock_unlock(&qgrp_elem->lock);
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

		odp_ticketlock_unlock(&qgrp_elem->lock);

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

		odp_ticketlock_unlock(&qgrp_elem->lock);
		err = EM_OK;
		goto queue_group_modify_sync_error; /* no error, just return  */
	}

	odp_ticketlock_unlock(&qgrp_elem->lock);

	/*
	 * Send add/rem-req events to all other concerned cores.
	 * Note: if .ongoing_delete = true:
	 *       Treat errors as EM_FATAL because failures will leave
	 *       .ongoing_delete = true for the group until restart of EM.
	 */
	err = send_qgrp_addrem_reqs(qgrp_elem, new_mask, &add_mask, &rem_mask,
				    0, NULL, escope);

	odp_rwlock_read_unlock(&core_map->rwlock);

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
	odp_rwlock_read_unlock(&core_map->rwlock);
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

	odp_ticketlock_lock(&qgrp_elem->lock);
	q_grp_add_core(qgrp_elem);
	odp_ticketlock_unlock(&qgrp_elem->lock);
}

void i_event__qgrp_rem_core_req(const internal_event_t *i_ev)
{
	em_queue_group_t qgrp = i_ev->q_grp.queue_group;
	queue_group_elem_t *qgrp_elem = queue_group_elem_get(qgrp);

	if (unlikely(!qgrp_elem))
		return;

	odp_ticketlock_lock(&qgrp_elem->lock);
	q_grp_rem_core(qgrp_elem);
	odp_ticketlock_unlock(&qgrp_elem->lock);
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

	odp_ticketlock_lock(&qgrp_elem->lock);
	q_grp_create_done(qgrp_elem, &args->new_mask);
	odp_ticketlock_unlock(&qgrp_elem->lock);

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

	odp_ticketlock_lock(&qgrp_elem->lock);
	q_grp_create_sync_done(qgrp_elem, &args->new_mask);
	odp_ticketlock_unlock(&qgrp_elem->lock);

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

	odp_ticketlock_lock(&qgrp_elem->lock);
	q_grp_modify_done(qgrp_elem, &args->new_mask);
	odp_ticketlock_unlock(&qgrp_elem->lock);

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

	odp_ticketlock_lock(&qgrp_elem->lock);
	q_grp_delete_done(qgrp_elem, &args->new_mask);
	odp_ticketlock_unlock(&qgrp_elem->lock);

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
	const unsigned int num_queues = odp_atomic_load_u32(&qgrp_elem->num_queues);
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
	odp_ticketlock_lock(&queue_group_elem->lock);
	list_add(&queue_group_elem->queue_list, &queue_elem->qgrp_node);
	odp_atomic_inc_u32(&queue_group_elem->num_queues);
	odp_ticketlock_unlock(&queue_group_elem->lock);
}

void queue_group_rem_queue_list(queue_group_elem_t *const queue_group_elem,
				queue_elem_t *const queue_elem)
{
	odp_ticketlock_lock(&queue_group_elem->lock);
	if (!list_is_empty(&queue_group_elem->queue_list)) {
		list_rem(&queue_group_elem->queue_list, &queue_elem->qgrp_node);
		odp_atomic_dec_u32(&queue_group_elem->num_queues);
	}
	odp_ticketlock_unlock(&queue_group_elem->lock);
}

unsigned int queue_group_count(void)
{
	return odp_atomic_load_u32(&em_shm->queue_group_count);
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
	em_core_mask_t phys_mask;
	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
	char em_mask_str[EM_CORE_MASK_STRLEN];
	char odp_thrmask_str[ODP_THRMASK_STR_SIZE];
	char phys_mask_str[EM_CORE_MASK_STRLEN];
	em_status_t err;
	int ret;
	int len = 0;

	queue_group_elem_t *qgrp_elem = queue_group_elem_get(queue_group);

	if (unlikely(!qgrp_elem || !queue_group_allocated(qgrp_elem)))
		goto info_print_err;

	em_queue_group_name(queue_group, qgrp_name, sizeof(qgrp_name));
	err = em_queue_group_mask(queue_group, &core_mask);
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
	em_core_mask_physical(&phys_mask /*out*/, &core_mask);
	em_core_mask_tostr(phys_mask_str, sizeof(phys_mask_str), &phys_mask);
	phys_mask_str[EM_CORE_MASK_STRLEN - 1] = '\0';

	len = snprintf(qgrp_info_str, QGRP_INFO_LEN, QGRP_INFO_FMT,
		       queue_group, qgrp_name, em_mask_str,
		       phys_mask_str, odp_thrmask_str,
		       odp_atomic_load_u32(&qgrp_elem->num_queues));

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

	qgrp = em_queue_group_first(&qgrp_num);

	/*
	 * qgrp_num may not match the amount of queue groups actually returned
	 * by iterating using em_queue_group_next() if queue groups are added
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
		qgrp = em_queue_group_next();
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

	em_queue_group_name(qgrp, qgrp_name, sizeof(qgrp_name));
	qgrp_queue = em_queue_group_queue_first(&q_num, qgrp);

	/*
	 * q_num may not match the amount of queues actually returned by iterating
	 * using em_queue_group_queue_next() if queues are added or removed
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
			qgrp_queue = em_queue_group_queue_next();
			continue;
		}

		queue_name(q_elem, q_name, EM_QUEUE_NAME_LEN - 1);

		n_print = snprintf(q_info_str + len, q_info_len - len,
				   QGRP_Q_INFO_FMT, qgrp_queue, q_name,
				   q_elem->priority,
				   queue_type_str(q_elem->type),
				   queue_state_str(q_elem->state),
				   q_elem->context ? 'Y' : 'N');

		/* Not enough space to hold more queue info */
		if (n_print >= q_info_len - len)
			break;

		len += n_print;
		qgrp_queue = em_queue_group_queue_next();
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
