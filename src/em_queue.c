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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_atomic_group.h"
#include "em_atomic_group_types.h"
#include "em_chaining.h"
#include "em_eo.h"
#include "em_eo_types.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_internal_event.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_queue_group_types.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"
#include "misc/list.h"
#include "misc/objpool.h"

#define EM_Q_BASENAME  "EM_Q_"

static int queue_init_prio_map(int minp, int maxp, int nump);
static void queue_init_prio_legacy(int minp, int maxp);
static void queue_init_prio_adaptive(int minp, int maxp, int nump);
static int queue_init_prio_custom(int minp, int maxp);

static inline int
queue_create_check_sched(const em_queue_param_t *param, const char **err_str);

static int queue_setup(const char *name, const em_queue_param_t *param,
		       queue_elem_t *q_elem /*in/out*/,
		       const char **err_str /*out*/);
static void queue_setup_odp_common(const em_queue_param_t *param,
				   odp_queue_param_t *odp_queue_param /*out*/);
static int queue_setup_scheduled(const em_queue_param_t *param,
				 queue_elem_t *q_elem /*out*/,
				 const char **err_str /*out*/);
static int queue_setup_unscheduled(const em_queue_param_t *param,
				   queue_elem_t *q_elem /*out*/,
				   const char **err_str /*out*/);
static int queue_setup_local(const em_queue_param_t *param,
			     queue_elem_t *q_elem /*out*/,
			     const char **err_str /*out*/);
static int queue_setup_output(const em_queue_param_t *param,
			      queue_elem_t *q_elem /*out*/,
			      const char **err_str /*out*/);

static inline queue_elem_t *
queue_poolelem2queue(objpool_elem_t *const queue_pool_elem)
{
	return (queue_elem_t *)((uintptr_t)queue_pool_elem -
				offsetof(queue_elem_t, queue_pool_elem));
}

/*
 * Helper for read_config_file(): read and validate queue.num_aggr.
 *
 * The option is only applicable when vector.backend == EVENT; with the packet
 * backend num_aggr is forced to 0, and any other backend value is an error.
 * Assumes opt.vector.backend has already been parsed.
 */
static int read_num_aggr_config(void)
{
	const unsigned int max_config_queues = UINT16_MAX - MAX_INTERNAL_QUEUES;
	const char *conf_str;
	int val;
	int ret;

	if (em_shm->opt.vector.backend == EM_VECTOR_BACKEND_EVENT) {
		conf_str = "queue.num_aggr";
		ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
		if (unlikely(!ret)) {
			EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
			return -1;
		}
		if (val < 0 || (unsigned int)val > max_config_queues) {
			EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d', value must be\n"
				">= 0 and <= %u\n", conf_str, val, max_config_queues);
			return -1;
		}
		em_shm->opt.queue.num_aggr = (unsigned int)val;
		EM_PRINT("  %s: %d\n", conf_str, val);
	} else if (em_shm->opt.vector.backend == EM_VECTOR_BACKEND_PACKET) {
		/* Aggregated queues are not supported with packet vectors */
		em_shm->opt.queue.num_aggr = 0;
	} else {
		EM_LOG(EM_LOG_ERR, "Invalid vector backend %d\n", em_shm->opt.vector.backend);
		return -1;
	}

	return 0;
}

static int read_config_file(void)
{
	const char *conf_str;
	int val = 0;
	int ret;
	unsigned int max_queues;
	const unsigned int max_config_queues = UINT16_MAX - MAX_INTERNAL_QUEUES;

	EM_PRINT("EM queue config:\n");

	/*
	 * Option: queue.num_static
	 */
	conf_str = "queue.num_static";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0 || (unsigned int)val > max_config_queues) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d', value must be\n"
			">= 0 and <= %u\n", conf_str, val, max_config_queues);
		return -1;
	}
	em_shm->opt.queue.num_static = (unsigned int)val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	/*
	 * Option: queue.num_dynamic
	 */
	conf_str = "queue.num_dynamic";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0 || (unsigned int)val > max_config_queues) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d', value must be\n"
			">= 0 and <= %u\n", conf_str, val, max_config_queues);
		return -1;
	}
	em_shm->opt.queue.num_dynamic = (unsigned int)val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	if (em_shm->opt.queue.num_static == 0 && em_shm->opt.queue.num_dynamic == 0) {
		EM_LOG(EM_LOG_ERR, "At least one of num_static or num_dynamic must be > 0\n");
		return -1;
	}

	/*
	 * Option: queue.num_aggr
	 * Only applicable when 'vector.backend = event', otherwise interpreted
	 * as '0' (no aggregator queues).
	 * Assumes the option 'vector.backend' has been parsed before this option.
	 */
	if (read_num_aggr_config() != 0)
		return -1;

	max_queues = em_shm->opt.queue.num_static +
		     em_shm->opt.queue.num_dynamic +
		     em_shm->opt.queue.num_aggr + MAX_INTERNAL_QUEUES;

	if (max_queues > UINT16_MAX) {
		EM_LOG(EM_LOG_ERR,
		       "Bad config value: num_static(%u) + num_dynamic(%u) +\n"
		       "num_aggr(%u) + MAX_INTERNAL_QUEUES(%u) = %u > UINT16_MAX(%u)\n",
		       em_shm->opt.queue.num_static, em_shm->opt.queue.num_dynamic,
		       em_shm->opt.queue.num_aggr, MAX_INTERNAL_QUEUES, max_queues, UINT16_MAX);
		return -1;
	}

	if (max_queues > em_shm->queue_tbl.odp_queue_capability.max_queues) {
		EM_LOG(EM_LOG_ERR,
		       "Bad config values: num_static(%u) + num_dynamic(%u) +\n"
		       "num_aggr(%u) + MAX_INTERNAL_QUEUES(%u) = %u > odp-max-queues:%u\n",
		       em_shm->opt.queue.num_static, em_shm->opt.queue.num_dynamic,
		       em_shm->opt.queue.num_aggr, MAX_INTERNAL_QUEUES, max_queues,
		       em_shm->queue_tbl.odp_queue_capability.max_queues);
		return -1;
	}

	/* store the maximum number of EM queues that can be created */
	em_shm->queue_tbl.max_queue_num = (uint16_t)max_queues;

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
 * Helper: initialize a queue pool (set as empty)
 */
static int queue_pool_init(queue_pool_t *const queue_pool)
{
	const uint32_t num_subpools = MIN(4, OBJSUBPOOLS_MAX);

	if (objpool_init(&queue_pool->objpool, num_subpools) != 0)
		return -1;

	return 0;
}

/**
 * Helper: populate a queue pool with queue elements after init.
 */
static int queue_pool_populate(queue_tbl_t *const queue_tbl,
			       queue_pool_t *const queue_pool,
			       int min_qidx, int max_qidx)
{
	const uint32_t num_subpools = objpool_subpools(&queue_pool->objpool);
	const int qs_per_pool = (max_qidx - min_qidx + 1);
	int qs_per_subpool = qs_per_pool / num_subpools;
	int qs_leftover = qs_per_pool % num_subpools;
	uint32_t subpool_idx = 0;
	int add_cnt = 0;

	for (int i = min_qidx; i <= max_qidx; i++) {
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
em_status_t queue_init(queue_tbl_t *const queue_tbl,
		       queue_pool_t *const queue_pool,
		       queue_pool_t *const queue_pool_static,
		       queue_pool_t *const queue_pool_aggr)
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
	memset(queue_pool_aggr, 0, sizeof(queue_pool_t));
	odp_atomic_init_u32(&em_shm->queue_count, 0);
	odp_atomic_init_u32(&queue_tbl->output_queue_count, 0);

	/* init the queue-pools as empty */
	if (queue_pool_init(queue_pool) ||
	    queue_pool_init(queue_pool_static) ||
	    queue_pool_init(queue_pool_aggr))
		return EM_ERR_LIB_FAILED;

	/* Retrieve and store the ODP queue capabilities into 'queue_tbl' */
	ret = odp_queue_capability(odp_queue_capa);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"odp_queue_capability():%d failed", ret);

	/* Retrieve and store the ODP schedule capabilities into 'queue_tbl' */
	ret = odp_schedule_capability(odp_sched_capa);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"odp_schedule_capability():%d failed", ret);

	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	const unsigned int max_queues = em_shm->queue_tbl.max_queue_num;

	if (em_shm->opt.queue.num_static) {
		queue_tbl->first_static_queue_id = queue_idx2id(0);
		queue_tbl->last_static_queue_id = queue_tbl->first_static_queue_id +
						  (uint16_t)em_shm->opt.queue.num_static - 1;
		queue_tbl->first_internal_queue_id = queue_tbl->last_static_queue_id + 1;
	} else {
		queue_tbl->first_static_queue_id = INVALID_QUEUE_ID;
		queue_tbl->last_static_queue_id = INVALID_QUEUE_ID;
		queue_tbl->first_internal_queue_id = queue_idx2id(0);
	}

	queue_tbl->last_internal_queue_id = queue_tbl->first_internal_queue_id +
					    MAX_INTERNAL_QUEUES - 1;
	queue_tbl->shared_internal_queue_id = queue_tbl->last_internal_queue_id;

	if (em_shm->opt.queue.num_dynamic) {
		queue_tbl->first_dyn_queue_id = queue_tbl->last_internal_queue_id + 1;
		queue_tbl->last_dyn_queue_id = queue_tbl->first_dyn_queue_id +
					       (uint16_t)em_shm->opt.queue.num_dynamic - 1;
	} else {
		queue_tbl->first_dyn_queue_id = INVALID_QUEUE_ID;
		queue_tbl->last_dyn_queue_id = INVALID_QUEUE_ID;
	}

	if (em_shm->opt.queue.num_aggr) {
		if (em_shm->opt.queue.num_dynamic) {
			queue_tbl->first_aggr_queue_id = queue_tbl->last_dyn_queue_id + 1;
			queue_tbl->last_aggr_queue_id = queue_tbl->first_aggr_queue_id +
							(uint16_t)em_shm->opt.queue.num_aggr - 1;
		} else {
			queue_tbl->first_aggr_queue_id = queue_tbl->last_internal_queue_id + 1;
			queue_tbl->last_aggr_queue_id = queue_tbl->first_aggr_queue_id +
							(uint16_t)em_shm->opt.queue.num_aggr - 1;
		}
	} else {
		queue_tbl->first_aggr_queue_id = INVALID_QUEUE_ID;
		queue_tbl->last_aggr_queue_id = INVALID_QUEUE_ID;
	}

	size_t qelem_tbl_sz = sizeof(queue_elem_t) * max_queues;
	size_t qname_tbl_sz = sizeof(char) * EM_QUEUE_NAME_LEN * max_queues;
	size_t shm_sz = qelem_tbl_sz + qname_tbl_sz;

	void *shm_tbl = env_shared_reserve("EM q_elem tbl and names", shm_sz);

	RETURN_ERROR_IF(!shm_tbl, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"env_shared_reserve() failed when reserving \"EM q_elem tbl and names\"");
	memset(shm_tbl, 0, shm_sz);

	/* Store the q_elem tbl and q_name tbl pointers, points into the allocated shared mem */
	queue_tbl->queue_elem = shm_tbl;
	queue_tbl->name = (char(*)[EM_QUEUE_NAME_LEN])((uintptr_t)shm_tbl + qelem_tbl_sz);

	/* Initialize the queue element table */
	for (unsigned int i = 0; i < max_queues; i++)
		queue_tbl->queue_elem[i].queue = (uint32_t)(uintptr_t)queue_idx2hdl(i);

	/* Initialize the static queue pool */
	if (em_shm->opt.queue.num_static)
		min = queue_id2idx(queue_tbl->first_static_queue_id);
	else
		min = queue_id2idx(queue_tbl->first_internal_queue_id);

	max = queue_id2idx(queue_tbl->last_internal_queue_id);
	if (queue_pool_populate(queue_tbl, queue_pool_static, min, max) != 0)
		return EM_ERR_LIB_FAILED;

	/* Initialize the dynamic queue pool */
	if (em_shm->opt.queue.num_dynamic) {
		min = queue_id2idx(queue_tbl->first_dyn_queue_id);
		max = queue_id2idx(queue_tbl->last_dyn_queue_id);
		if (queue_pool_populate(queue_tbl, queue_pool, min, max) != 0)
			return EM_ERR_LIB_FAILED;
	}

	/* Initialize the aggregator queue pool */
	if (em_shm->opt.queue.num_aggr) {
		min = queue_id2idx(queue_tbl->first_aggr_queue_id);
		max = queue_id2idx(queue_tbl->last_aggr_queue_id);
		if (queue_pool_populate(queue_tbl, queue_pool_aggr, min, max) != 0)
			return EM_ERR_LIB_FAILED;
	}

	/* Initialize priority mapping, adapt to values from ODP */
	min = odp_schedule_min_prio();
	max = odp_schedule_max_prio();
	em_shm->queue_prio.num_runtime = max - min + 1;
	ret = queue_init_prio_map(min, max, em_shm->queue_prio.num_runtime);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"mapping odp priorities failed: %d", ret);

	/* Initialize output queue free indexes */
	for (unsigned int i = 0; i < EM_MAX_OUTPUT_QUEUES; i++)
		queue_tbl->output_queue_idx_free[i] = true;

	return EM_OK;
}

/**
 * Queue inits done during EM core local init (once at startup on each core).
 *
 * Initialize event storage for queues of type 'EM_QUEUE_TYPE_LOCAL'.
 */
em_status_t queue_init_local(void)
{
	em_locm_t *const locm = &em_locm;
	odp_stash_capability_t stash_capa;
	odp_stash_param_t stash_param;
	unsigned int num_obj = 0;
	int core = em_core_id();
	char name[ODP_STASH_NAME_LEN];

	int ret = odp_stash_capability(&stash_capa, ODP_STASH_TYPE_FIFO);

	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	odp_stash_param_init(&stash_param);

	stash_param.type = ODP_STASH_TYPE_FIFO;
	/*
	 * Each local-queue stash lives in core-local memory and is only ever
	 * accessed (put and get) by the owning core's own thread, so the
	 * strongest single-thread mode applies.
	 */
	stash_param.put_mode = ODP_STASH_OP_LOCAL;
	stash_param.get_mode = ODP_STASH_OP_LOCAL;

	/* Stash size: use EM default queue size value from config file: */
	num_obj = em_shm->opt.queue.min_events_default;
	if (num_obj != 0)
		stash_param.num_obj = num_obj;
	/* else: use odp default as set by odp_stash_param_init() */

	stash_param.obj_size = sizeof(uint64_t);
	if (stash_param.num_obj > stash_capa.max_num.u64) {
		EM_LOG(EM_LOG_PRINT,
		       "%s(): req stash.num_obj(%" PRIu64 ") > capa.max_num.u64(%" PRIu64 ").\n"
		       "      ==> using max value:%" PRIu64 "\n", __func__,
		       stash_param.num_obj, stash_capa.max_num.u64, stash_capa.max_num.u64);
		stash_param.num_obj = stash_capa.max_num.u64;
	}

	stash_param.cache_size = 0; /* No core local caching */

	locm->local_queues.empty = 1;

	for (int prio = 0; prio < EM_QUEUE_PRIO_NUM; prio++) {
		snprintf(name, sizeof(name),
			 "local-q:c%02d:prio%d", core, prio);
		name[sizeof(name) - 1] = '\0';

		locm->local_queues.prio[prio].empty_prio = 1;
		locm->local_queues.prio[prio].stash =
			odp_stash_create(name, &stash_param);
		if (unlikely(locm->local_queues.prio[prio].stash ==
			     ODP_STASH_INVALID))
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
em_status_t queue_term_local(void)
{
	stash_entry_t entry_tbl[EM_QUEUE_LOCAL_MULTI_MAX_BURST];
	em_event_t ev_tbl[EM_QUEUE_LOCAL_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_QUEUE_LOCAL_MULTI_MAX_BURST];
	em_status_t stat = EM_OK;

	for (;;) {
		int num = next_local_queue_events(entry_tbl /*[out]*/,
						  EM_QUEUE_LOCAL_MULTI_MAX_BURST);
		if (num <= 0)
			break;

		for (int i = 0; i < num; i++)
			ev_tbl[i] = (em_event_t)(uintptr_t)entry_tbl[i].evptr;

		event_to_hdr_multi(ev_tbl, ev_hdr_tbl, num);

		if (esv_enabled())
			evstate_em2usr_multi(ev_tbl, ev_hdr_tbl, num,
					     EVSTATE__TERM_CORE__QUEUE_LOCAL);
		em_free_multi(ev_tbl, num);
	}

	for (int prio = 0; prio < EM_QUEUE_PRIO_NUM; prio++) {
		int ret = odp_stash_destroy(em_locm.local_queues.prio[prio].stash);

		if (unlikely(ret != 0))
			stat = EM_ERR_LIB_FAILED;
	}

	return stat;
}

static inline bool id_in_queue_pool_static(uint16_t qid)
{
	if (em_shm->opt.queue.num_static == 0)
		return qid >= em_shm->queue_tbl.first_internal_queue_id &&
		       qid <= em_shm->queue_tbl.last_internal_queue_id;
	else
		return qid >= em_shm->queue_tbl.first_static_queue_id &&
		       qid <= em_shm->queue_tbl.last_internal_queue_id;
}

/**
 * Allocate a new static or dynamic EM queue
 *
 * Aggregator queue allocation uses queue_aggr_alloc() instead.
 *
 * @param queue         EM queue handle if a specific EM queue is requested,
 *                      EM_QUEUE_UNDEF if any EM queue will do.
 * @param[out] err_str  Output var for error message in case of failure.
 *
 * @return EM queue handle
 * @retval EM_QUEUE_UNDEF on failure
 */
em_queue_t queue_alloc(em_queue_t queue, const char **err_str /*out*/)
{
	queue_elem_t *queue_elem;
	objpool_elem_t *queue_pool_elem;

	if (queue == EM_QUEUE_UNDEF) {
		/*
		 * Allocate a dynamic queue, i.e. take next available
		 */
		queue_pool_elem = objpool_rem(&em_shm->queue_pool.objpool,
					      odp_thread_id());
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
		if (iq.device_id != em_shm->conf.device_id ||
		    !id_in_queue_pool_static(iq.queue_id)) {
			*err_str = "Invalid queue requested or handle not from static range!";
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

	odp_atomic_inc_u32(&em_shm->queue_count);
	return (em_queue_t)(uintptr_t)queue_elem->queue;
}

em_status_t queue_free(em_queue_t queue)
{
	queue_elem_t *const queue_elem = queue_elem_get(queue);
	objpool_t *objpool;
	internal_queue_t iq;

	iq.queue = queue;

	if (unlikely(queue_elem == NULL))
		return EM_ERR_BAD_ID;

	if (id_in_queue_pool_static(iq.queue_id))
		objpool = &em_shm->queue_pool_static.objpool;
	else
		objpool = &em_shm->queue_pool.objpool;

	queue_elem->state = EM_QUEUE_STATE_INVALID;

	objpool_add(objpool,
		    queue_elem->queue_pool_elem.subpool_idx,
		    &queue_elem->queue_pool_elem);

	odp_atomic_dec_u32(&em_shm->queue_count);
	return EM_OK;
}

static em_queue_t queue_aggr_alloc(const char **err_str /*out*/)
{
	const queue_elem_t *queue_elem;
	objpool_elem_t *queue_pool_elem;

	/*
	 * Allocate an aggregator queue, i.e. take next available
	 */
	queue_pool_elem = objpool_rem(&em_shm->queue_pool_aggr.objpool,
				      odp_thread_id());
	if (unlikely(queue_pool_elem == NULL)) {
		*err_str = "aggregator queue pool element alloc failed!";
		return EM_QUEUE_UNDEF;
	}
	queue_elem = queue_poolelem2queue(queue_pool_elem);

	odp_atomic_inc_u32(&em_shm->queue_count);
	return (em_queue_t)(uintptr_t)queue_elem->queue;
}

static void queue_aggr_free(queue_elem_t *aggr_qelem)
{
	aggr_qelem->state = EM_QUEUE_STATE_INVALID;
	aggr_qelem->aggr.parent_queue = EM_QUEUE_UNDEF;
	aggr_qelem->aggr.pool = EM_POOL_UNDEF;
	aggr_qelem->aggr.max_tmo_ns = 0;
	aggr_qelem->aggr.max_size = 0;
	aggr_qelem->aggr.event_type = EM_EVENT_TYPE_UNDEF;

	objpool_t *objpool = &em_shm->queue_pool_aggr.objpool;

	objpool_add(objpool, aggr_qelem->queue_pool_elem.subpool_idx,
		    &aggr_qelem->queue_pool_elem);

	odp_atomic_dec_u32(&em_shm->queue_count);
}

static int queue_free_aggr_all(queue_elem_t *q_elem)
{
	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	uint32_t num_aggr = q_elem->aggr_parent.num_aggr;
	const uint32_t aggr_undef = (uint32_t)(uintptr_t)EM_QUEUE_UNDEF;
	bool err_occurred = false;

	for (uint32_t i = 0; i < num_aggr; i++) {
		em_queue_t aggr_queue =
			(em_queue_t)(uintptr_t)q_elem->aggr_parent.aggr_queues[i];
		queue_elem_t *aggr_qelem = queue_elem_get(aggr_queue);

		q_elem->aggr_parent.aggr_queues[i] = aggr_undef;

		if (unlikely(!aggr_qelem || !queue_allocated(aggr_qelem))) {
			err_occurred = true;
			EM_LOG(EM_LOG_ERR,
			       "Q:%" PRI_QUEUE " - Invalid aggr-Q:%" PRI_QUEUE " at idx:%u\n",
			       queue, aggr_queue, i);
			continue;
		}

		if (unlikely(aggr_qelem->type != EM_QUEUE_TYPE_AGGR ||
			     aggr_qelem->aggr.parent_queue != queue)) {
			err_occurred = true;
			EM_LOG(EM_LOG_ERR,
			       "Q:%" PRI_QUEUE " - Invalid aggr-Q:%" PRI_QUEUE " at idx:%u\n"
			       "aggr-Q:{type=%u, parent-Q=%" PRI_QUEUE "}\n",
			       queue, aggr_queue, i,
			       aggr_qelem->type, aggr_qelem->aggr.parent_queue);
			continue;
		}

		/* Free the aggregator queue */
		queue_aggr_free(aggr_qelem);
	}

	q_elem->aggr_parent.num_aggr = 0;
	q_elem->flags.has_aggr = 0;

	if (unlikely(err_occurred))
		return -1;

	return 0;
}

static int queue_create_check_aggr(const em_queue_param_t *param,
				   const odp_event_aggr_capability_t *odp_aggr_capa,
				   const char **err_str)
{
	if (param->num_aggr == 0)
		return 0; /* No event aggregation, no need to check further */

	/* param->num_aggr > 0: */
	if (unlikely(em_shm->opt.vector.backend == EM_VECTOR_BACKEND_PACKET)) {
		*err_str = "Event aggregation not supported with packet vector backend!";
		return -1;
	}

	/* Check maximum number of event aggregators */
	if (unlikely(param->num_aggr > MIN(odp_aggr_capa->max_num,
					   odp_aggr_capa->max_num_per_queue) ||
		     param->num_aggr > EM_QUEUE_MAX_AGGR)) {
		*err_str = "Invalid number of event aggregators, check limits!";
		return -1;
	}

	if (unlikely(!param->aggr_conf)) {
		*err_str = "Event aggregator config missing!";
		return -1;
	}

	for (uint32_t i = 0; i < param->num_aggr; i++) {
		const em_queue_aggr_conf_t *aggr_conf = &param->aggr_conf[i];

		if (unlikely(aggr_conf->__internal_check != EM_CHECK_INIT_CALLED)) {
			*err_str = "Use em_queue_aggr_conf_init() before create";
			return -1;
		}

		if (unlikely(aggr_conf->pool == EM_POOL_UNDEF)) {
			*err_str = "Event aggregator pool not defined!";
			return -1;
		}

		const mpool_elem_t *pool_elem = pool_elem_get(aggr_conf->pool);

		if (unlikely(!pool_elem || !pool_allocated(pool_elem) ||
			     pool_elem->event_type != EM_EVENT_TYPE_VECTOR)) {
			*err_str = "Invalid pool for event aggregator!";
			return -1;
		}

		/* Check max & min number of events that can be aggregated into an event vector */
		if (unlikely(aggr_conf->max_size > odp_aggr_capa->max_size ||
			     aggr_conf->max_size < odp_aggr_capa->min_size)) {
			*err_str = "Invalid max_size for event aggregator!";
			return -1;
		}

		/* Check max & min allowed value of max_tmo_ns */
		if (aggr_conf->max_tmo_ns > 0 /* 0 == no limit */ &&
		    (aggr_conf->max_tmo_ns > odp_aggr_capa->max_tmo_ns ||
		     aggr_conf->max_tmo_ns < odp_aggr_capa->min_tmo_ns)) {
			*err_str = "Invalid max_tmo_ns for event aggregator!";
			return -1;
		}
	}

	return 0;
}

static int queue_setup_odp_aggr(const em_queue_aggr_conf_t aggr_conf[/*in:num_aggr*/],
				odp_event_aggr_config_t odp_aggr_config[/*out:num_aggr*/],
				uint32_t num_aggr, const char **err_str)
{
	/*
	 * Set queue params for event aggregation,
	 * values already checked in queue_create_check_aggr().
	 */
	for (uint32_t i = 0; i < num_aggr; i++) {
		const mpool_elem_t *pool_elem = pool_elem_get(aggr_conf[i].pool);

		if (unlikely(!pool_elem || !pool_allocated(pool_elem) ||
			     pool_elem->event_type != EM_EVENT_TYPE_VECTOR)) {
			*err_str = "Invalid pool for event aggregator!";
			return -1;
		}

		int subpool = pool_find_subpool(pool_elem, aggr_conf[i].max_size);

		if (unlikely(subpool < 0)) {
			*err_str = "No suitable subpool found for event aggregator!";
			return -1;
		}

		odp_aggr_config[i].pool = pool_elem->odp_pool[subpool];
		odp_aggr_config[i].max_tmo_ns = aggr_conf[i].max_tmo_ns;
		odp_aggr_config[i].max_size = aggr_conf[i].max_size;

		/* Convert EM event type to ODP event type */
		em_event_type_t evtype_major = em_event_type_major(aggr_conf[i].event_type);

		switch (evtype_major) {
		case EM_EVENT_TYPE_SW:
			odp_aggr_config[i].event_type = ODP_EVENT_BUFFER;
			break;
		case EM_EVENT_TYPE_PACKET:
			odp_aggr_config[i].event_type = ODP_EVENT_PACKET;
			break;
		case EM_EVENT_TYPE_TIMER_IND:
			odp_aggr_config[i].event_type = ODP_EVENT_TIMEOUT;
			break;
		case EM_EVENT_TYPE_TIMER:  /* fallthrough */
		case EM_EVENT_TYPE_CRYPTO: /* fallthrough */
		case EM_EVENT_TYPE_ODP:    /* fallthrough */
		case EM_EVENT_TYPE_ANY:
			odp_aggr_config[i].event_type = ODP_EVENT_ANY;
			break;
		default:
			/* also EM_EVENT_TYPE_VECTOR */
			*err_str = "Unsupported event type for event aggregator!";
			return -1;
		}
	}

	return 0;
}

static int queue_create_check_sched(const em_queue_param_t *param, const char **err_str)
{
	const queue_group_elem_t *queue_group_elem = queue_group_elem_get(param->queue_group);

	/* scheduled queues are always associated with a queue group */
	if (unlikely(queue_group_elem == NULL || !queue_group_allocated(queue_group_elem))) {
		*err_str = "Invalid queue group!";
		return -1;
	}

	if (param->atomic_group != EM_ATOMIC_GROUP_UNDEF) {
		const atomic_group_elem_t *ag_elem = atomic_group_elem_get(param->atomic_group);

		if (unlikely(!ag_elem || !atomic_group_allocated(ag_elem))) {
			*err_str = "Invalid atomic group!";
			return -1;
		}
	}

	if (unlikely(param->prio >= EM_QUEUE_PRIO_NUM)) {
		*err_str = "Invalid queue priority!";
		return -1;
	}

	const odp_event_aggr_capability_t *odp_aggr_capa =
		&em_shm->queue_tbl.odp_schedule_capability.aggr;

	int err = queue_create_check_aggr(param, odp_aggr_capa, err_str /*out*/);

	if (unlikely(err)) /* 'err_str' set by queue_create_check_aggr() */
		return -1;

	return 0;
}

static int queue_create_check_unsched(const em_queue_param_t *param, const char **err_str)
{
	/* API arg checks for unscheduled queues */
	if (unlikely(param->prio != EM_QUEUE_PRIO_UNDEF)) {
		*err_str = "Invalid priority for unsched queue!";
		return -1;
	}
	if (unlikely(param->queue_group != EM_QUEUE_GROUP_UNDEF)) {
		*err_str = "Queue group not used with unsched queues!";
		return -1;
	}
	if (unlikely(param->atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
		*err_str = "Atomic group not used with unsched queues!";
		return -1;
	}

	const odp_event_aggr_capability_t *odp_aggr_capa =
		&em_shm->queue_tbl.odp_queue_capability.plain.aggr;

	int err = queue_create_check_aggr(param, odp_aggr_capa, err_str /*out*/);

	if (unlikely(err)) /* 'err_str' set by queue_create_check_aggr() */
		return -1;

	return 0;
}

static int queue_create_check_local(const em_queue_param_t *param, const char **err_str)
{
	/* API arg checks for local queues */
	if (unlikely(param->queue_group != EM_QUEUE_GROUP_UNDEF)) {
		*err_str = "Queue group not used with local queues!";
		return -1;
	}
	if (unlikely(param->atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
		*err_str = "Atomic group not used with local queues!";
		return -1;
	}
	if (unlikely(param->prio >= EM_QUEUE_PRIO_NUM)) {
		*err_str = "Invalid queue priority!";
		return -1;
	}
	if (unlikely(param->num_aggr > 0)) {
		*err_str = "Event aggregation not supported with local queues!";
		return -1;
	}

	return 0;
}

static int queue_create_check_output(const em_queue_param_t *param, const char **err_str)
{
	/* API arg checks for output queues */
	if (unlikely(param->queue_group != EM_QUEUE_GROUP_UNDEF)) {
		*err_str = "Queue group not used with output queues!";
		return -1;
	}
	if (unlikely(param->atomic_group != EM_ATOMIC_GROUP_UNDEF)) {
		*err_str = "Atomic group not used with output queues!";
		return -1;
	}
	if (unlikely(param->num_aggr > 0)) {
		*err_str = "Event aggregation not supported with output queues!";
		return -1;
	}
	if (unlikely(param->output_conf.output_fn == NULL)) {
		*err_str = "Invalid output queue conf: output function missing!";
		return -1;
	}

	return 0;
}

static int queue_create_check_args(const em_queue_param_t *param, const char **err_str)
{
	if (param->atomic_group != EM_ATOMIC_GROUP_UNDEF &&
	    param->type != EM_QUEUE_TYPE_ATOMIC) {
		*err_str = "Atomic group can only be used with atomic queues!";
		return -1;
	}

	/* scheduled queue */
	if (param->type == EM_QUEUE_TYPE_ATOMIC   ||
	    param->type == EM_QUEUE_TYPE_PARALLEL ||
	    param->type == EM_QUEUE_TYPE_ORDERED)
		return queue_create_check_sched(param, err_str/*out*/);

	/* other queue types */
	switch (param->type) {
	case EM_QUEUE_TYPE_UNSCHEDULED:
		return queue_create_check_unsched(param, err_str/*out*/);
	case EM_QUEUE_TYPE_LOCAL:
		return queue_create_check_local(param, err_str/*out*/);
	case EM_QUEUE_TYPE_OUTPUT:
		return queue_create_check_output(param, err_str/*out*/);
	default:
		*err_str = "Unknown queue type";
		return -1;
	}

	return 0;
}

void queue_param_apply_conf(em_queue_param_t *param /*in,out*/,
			    const em_queue_conf_t *conf)
{
	if (conf == NULL)
		return;

	param->flags = conf->flags;
	param->min_events = conf->min_events;
	if (conf->conf &&
	    conf->conf_len == sizeof(em_output_queue_conf_t) &&
	    param->type == EM_QUEUE_TYPE_OUTPUT) {
		param->output_conf = *((const em_output_queue_conf_t *)conf->conf);
	}
}

em_queue_t queue_create_param(const char *name, const em_queue_param_t *param,
			      const char **err_str /*out*/)
{
	int err = queue_create_check_args(param, err_str /*out*/);

	if (unlikely(err)) /* 'err_str' set by queue_create_check_args() */
		return EM_QUEUE_UNDEF;

	/*
	 * Allocate the queue handle and obtain the corresponding queue-element
	 */
	em_queue_t queue_req = param->queue;
	em_queue_t queue = queue_alloc(queue_req, err_str /*out*/);

	if (unlikely(queue == EM_QUEUE_UNDEF)) {
		/* 'err_str' set by queue_alloc() */
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
	err = queue_setup(name, param, queue_elem, err_str);
	if (unlikely(err)) {
		queue_free(queue);
		/* 'err_str' set by queue_setup() */
		return EM_QUEUE_UNDEF;
	}

	return queue;
}

em_status_t queue_delete(queue_elem_t *const queue_elem, const char **err_str/*out*/)
{
	queue_state_t old_state;
	queue_state_t new_state;
	em_status_t ret;
	em_queue_t queue = (em_queue_t)(uintptr_t)queue_elem->queue;
	em_queue_type_t type = queue_elem->type;

	if (unlikely(!queue_allocated(queue_elem))) {
		*err_str = "Invalid queue (not allocated)";
		return EM_ERR_BAD_STATE;
	}
	/*
	 * Cannot explicitly delete aggregator queues,
	 * they are deleted as part of the parent queue deletion.
	 */
	if (unlikely(type == EM_QUEUE_TYPE_AGGR)) {
		*err_str = "Cannot delete aggregator queue, delete parent queue instead";
		return EM_ERR_NOT_SUPPORTED;
	}

	old_state = queue_elem->state;
	new_state = EM_QUEUE_STATE_INVALID;

	if (type != EM_QUEUE_TYPE_UNSCHEDULED &&
	    type != EM_QUEUE_TYPE_OUTPUT) {
		/* verify scheduled queue state transition */
		ret = queue_state_change__check(old_state, new_state,
						0/*!is_setup*/);
		if (unlikely(ret != EM_OK)) {
			*err_str = "Invalid queue state transition for delete";
			return ret;
		}
	}

	if (type != EM_QUEUE_TYPE_UNSCHEDULED &&
	    type != EM_QUEUE_TYPE_LOCAL &&
	    type != EM_QUEUE_TYPE_OUTPUT) {
		queue_group_elem_t *const queue_group_elem =
			queue_group_elem_get(queue_elem->queue_group);

		if (unlikely(queue_group_elem == NULL ||
			     !queue_group_allocated(queue_group_elem))) {
			*err_str = "Invalid queue group";
			return EM_ERR_BAD_ID;
		}

		/* Remove the queue from the queue group list */
		queue_group_rem_queue_list(queue_group_elem, queue_elem);
	}

	if (type == EM_QUEUE_TYPE_OUTPUT) {
		odp_ticketlock_t *const lock = &queue_elem->output.lock;
		q_elem_output_t *const q_out = &queue_elem->output;

		odp_ticketlock_lock(lock);
		/* Drain any remaining events from the output queue */
		output_queue_drain(queue_elem);
		odp_ticketlock_unlock(lock);

		/* delete the fn-args storage if allocated in create */
		if (q_out->output_fn_args_event != EM_EVENT_UNDEF) {
			em_free(q_out->output_fn_args_event);
			q_out->output_fn_args_event = EM_EVENT_UNDEF;
		}

		odp_ticketlock_lock(&em_shm->queue_tbl.output_queue_lock);
		em_shm->queue_tbl.output_queue_idx_free[q_out->idx] = true;
		odp_atomic_dec_u32(&em_shm->queue_tbl.output_queue_count);
		odp_ticketlock_unlock(&em_shm->queue_tbl.output_queue_lock);
	}

	if (queue_elem->odp_queue != ODP_QUEUE_INVALID &&
	    !queue_elem->flags.is_pktin) {
		/*
		 * Destroy the underlying ODP queue.
		 * Also destroys all ODP event aggregators used by this queue
		 * (for scheduled and unscheduled queues only).
		 */
		int err = odp_queue_destroy(queue_elem->odp_queue);

		if (unlikely(err)) {
			*err_str = "odp_queue_destroy() failed";
			return EM_ERR_LIB_FAILED;
		}
	}

	queue_elem->odp_queue = ODP_QUEUE_INVALID;

	if (queue_elem->flags.has_aggr) {
		/* Free all aggregator queue elems linked to this queue (if any) */
		int err = queue_free_aggr_all(queue_elem);

		if (unlikely(err)) {
			/* Log error, but continue with queue deletion */
			EM_LOG(EM_LOG_ERR,
			       "Failed to free aggregator queues of EM-Q:%" PRI_QUEUE " err:%d",
			       queue, err);
		}
	}

	/* Zero queue name */
	em_shm->queue_tbl.name[queue_hdl2idx(queue)][0] = '\0';

	/* Remove the queue from the atomic group it belongs to, if any */
	atomic_group_remove_queue(queue_elem);

	ret = queue_free(queue);
	if (unlikely(ret != EM_OK)) {
		*err_str = "queue_free() failed";
		return ret;
	}

	return EM_OK;
}

/**
 * Setup an allocated/created queue before use.
 */
static int queue_setup(const char *name, const em_queue_param_t *param,
		       queue_elem_t *q_elem /*in/out*/,
		       const char **err_str /*out*/)
{
	int ret;

	/* Set common queue-elem fields based on setup */
	queue_setup_common(name, param, q_elem);

	switch (param->type) {
	case EM_QUEUE_TYPE_ATOMIC: /* fallthrough */
	case EM_QUEUE_TYPE_PARALLEL: /* fallthrough */
	case EM_QUEUE_TYPE_ORDERED:
		ret = queue_setup_scheduled(param, q_elem, err_str);
		break;
	case EM_QUEUE_TYPE_UNSCHEDULED:
		ret = queue_setup_unscheduled(param, q_elem, err_str);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		ret = queue_setup_local(param, q_elem, err_str);
		break;
	case EM_QUEUE_TYPE_OUTPUT:
		ret = queue_setup_output(param, q_elem, err_str);
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
void queue_setup_common(const char *name, const em_queue_param_t *param,
			queue_elem_t *q_elem /*in/out*/)
{
	const em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	char *const qname = &em_shm->queue_tbl.name[queue_hdl2idx(queue)][0];

	/* checks that the odp queue context points to an EM queue elem */
	q_elem->valid_check = QUEUE_ELEM_VALID;

	/* Store queue name */
	if (name)
		strncpy(qname, name, EM_QUEUE_NAME_LEN - 1);
	else /* default unique name: "EM_Q_" + Q-id = e.g. EM_Q_1234 */
		snprintf(qname, EM_QUEUE_NAME_LEN,
			 "%s%" PRI_QUEUE "", EM_Q_BASENAME, queue);
	qname[EM_QUEUE_NAME_LEN - 1] = '\0';

	q_elem->flags.all = 0;
	/* Init q_elem fields based on setup params and clear the rest */
	q_elem->type = (uint8_t)param->type;
	q_elem->priority = (uint8_t)param->prio;
	q_elem->queue_group = param->queue_group;

	/* Does this queue belong to an EM Atomic Group? */
	if (param->atomic_group == EM_ATOMIC_GROUP_UNDEF) {
		q_elem->flags.in_atomic_group = false;
		q_elem->agrp.atomic_group = EM_ATOMIC_GROUP_UNDEF;
	} else {
		q_elem->flags.in_atomic_group = true;
		q_elem->agrp.atomic_group = param->atomic_group;
	}

	/* Clear the rest */
	q_elem->odp_queue = ODP_QUEUE_INVALID;
	q_elem->state = EM_QUEUE_STATE_INVALID;
	q_elem->context = NULL;
	q_elem->eo = (uint16_t)(uintptr_t)EM_EO_UNDEF;
	q_elem->eo_elem = NULL;
	q_elem->eo_ctx = NULL;
	q_elem->max_events = 0;
	q_elem->receive_func = NULL;
	q_elem->receive_multi_func = NULL; /* union */
	q_elem->aggr_parent.num_aggr = 0;
}

/**
 * Helper function to queue_setup_...()
 *
 * Set common ODP queue params based on EM config
 */
static void queue_setup_odp_common(const em_queue_param_t *param,
				   odp_queue_param_t *odp_queue_param /*out*/)
{
	/*
	 * Set ODP queue params according to EM queue conf flags
	 */
	em_queue_flag_t flags = param->flags & EM_QUEUE_FLAG_MASK;

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
	if (param->min_events == 0) {
		/* use EM default value from config file: */
		unsigned int size = em_shm->opt.queue.min_events_default;

		if (size != 0)
			odp_queue_param->size = size;
		/* else: use odp default as set by odp_queue_param_init() */
	} else {
		/* use user provided value: */
		odp_queue_param->size = param->min_events;
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

	(void)queue_name(q_elem, odp_name/*out*/, sizeof(odp_name));

	odp_queue = odp_queue_create(odp_name, odp_queue_param);
	if (unlikely(odp_queue == ODP_QUEUE_INVALID))
		return -1;

	/* Store the corresponding ODP Queue */
	q_elem->odp_queue = odp_queue;

	return 0;
}

/**
 * Helper function to queue_setup_scheduled() and queue_setup_unscheduled().
 *
 * Set up (child) aggregator queues for the parent queue. Retrieves the
 * ODP aggregator queue handles, allocates EM aggregator queue elements and
 * initializes them with names derived from the parent queue name.
 *
 * @param q_elem    Parent queue element, updated with aggregator parent data.
 * @param num_aggr  Number of event aggregators to set up.
 * @param err_str   [out] Error message string on failure.
 *
 * @return 0 on success, -1 on failure.
 */
static int queue_setup_aggr(queue_elem_t *q_elem /*in,out*/,
			    const em_queue_aggr_conf_t aggr_conf[/*in:num_aggr*/],
			    uint32_t num_aggr, const char **err_str /*out*/)
{
	odp_queue_t odp_aggr_queues[num_aggr];
	em_queue_t aggr_queues[num_aggr];
	queue_elem_t *aggr_qelems[num_aggr];

	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	char *const qname = &em_shm->queue_tbl.name[queue_hdl2idx(queue)][0];

	/* Get the ODP queue handles for the event aggregators */
	for (uint32_t i = 0; i < num_aggr; i++) {
		odp_aggr_queues[i] = odp_queue_aggr(q_elem->odp_queue, i);
		if (unlikely(odp_aggr_queues[i] == ODP_QUEUE_INVALID)) {
			*err_str = "Q-setup-aggr: failed to get ODP event aggregator!";
			return -1;
		}
	}

	/* Allocate queue elems for the EM queue aggregators */
	for (uint32_t i = 0; i < num_aggr; i++) {
		aggr_queues[i] = queue_aggr_alloc(err_str /*out*/);
		aggr_qelems[i] = queue_elem_get(aggr_queues[i]);

		if (unlikely(aggr_queues[i] == EM_QUEUE_UNDEF ||
			     aggr_qelems[i] == NULL)) {
			*err_str = "Q-setup-aggr: failed to allocate aggregator queue!";
			for (uint32_t j = 0; j < i; j++)
				queue_aggr_free(aggr_qelems[j]);
			return -1;
		}
	}

	q_elem->flags.has_aggr = 1;
	q_elem->aggr_parent.num_aggr = num_aggr;

	/* Setup the EM aggregator queue elems */
	for (uint32_t i = 0; i < num_aggr; i++) {
		const em_queue_t aggr_queue = aggr_queues[i];
		queue_elem_t *aggr_qelem = aggr_qelems[i];
		int aggr_idx = queue_hdl2idx(aggr_queue);
		char *const aggr_name = &em_shm->queue_tbl.name[aggr_idx][0];

		/*
		 * Store aggregator queue name:
		 * "AGGR01-<parent_qname>" e.g. "AGGR01-EM_Q_1234"
		 * "AGGRnn-" prefix (4+2+1) + '\0' = 8 bytes overhead
		 */
		const int aggr_name_maxlen = EM_QUEUE_NAME_LEN - 8;

		snprintf(aggr_name, EM_QUEUE_NAME_LEN,
			 "AGGR%02u-%.*s",
			 (i + 1) % (EM_QUEUE_MAX_AGGR + 1) /* modulo informs compiler: fits %02u */,
			 aggr_name_maxlen, qname);
		aggr_name[EM_QUEUE_NAME_LEN - 1] = '\0';

		/* checks that the odp queue context points to an EM queue elem */
		aggr_qelem->valid_check = QUEUE_ELEM_VALID;
		/* Set the queue type */
		aggr_qelem->type = EM_QUEUE_TYPE_AGGR;
		/* Store the corresponding ODP aggregator queue */
		aggr_qelem->odp_queue = odp_aggr_queues[i];

		/* Clear the rest */
		aggr_qelem->flags.all = 0;
		aggr_qelem->flags.scheduled = q_elem->flags.scheduled;
		aggr_qelem->state = q_elem->state; /* same as parent queue state */
		aggr_qelem->priority = EM_QUEUE_PRIO_UNDEF;
		aggr_qelem->max_events = 0;
		aggr_qelem->eo = (uint16_t)(uintptr_t)EM_EO_UNDEF;
		aggr_qelem->context = NULL;
		aggr_qelem->receive_func = NULL;
		aggr_qelem->receive_multi_func = NULL; /* union */
		aggr_qelem->eo_ctx = NULL;
		aggr_qelem->eo_elem = NULL;
		aggr_qelem->queue_group = EM_QUEUE_GROUP_UNDEF;

		/* store aggregator specific data */
		aggr_qelem->aggr.parent_queue = queue;
		aggr_qelem->aggr.pool = aggr_conf[i].pool;
		aggr_qelem->aggr.max_tmo_ns = aggr_conf[i].max_tmo_ns;
		aggr_qelem->aggr.max_size = aggr_conf[i].max_size;
		aggr_qelem->aggr.event_type = aggr_conf[i].event_type;
	}

	/* store the aggregator queue handles in the parent's queue elem */
	for (uint32_t i = 0; i < num_aggr; i++)
		q_elem->aggr_parent.aggr_queues[i] = (uint32_t)(uintptr_t)aggr_queues[i];

	return 0;
}

/**
 * Helper function to queue_setup()
 *
 * Set EM and ODP queue params for scheduled queues
 */
static int queue_setup_scheduled(const em_queue_param_t *param,
				 queue_elem_t *q_elem /*out*/,
				 const char **err_str /*out*/)
{
	/* validity checks done earlier for queue_group */
	queue_group_elem_t *qgrp_elem = queue_group_elem_get(param->queue_group);
	int err;

	if (unlikely(qgrp_elem == NULL)) {
		*err_str = "Q-setup-sched: invalid queue group!";
		return -1;
	}

	/* q_elem common fields already set by queue_setup_common() */
	q_elem->flags.scheduled = true;
	q_elem->state = EM_QUEUE_STATE_INIT;

	/*
	 * Set up a scheduled ODP queue for the EM scheduled queue
	 */
	odp_queue_param_t odp_queue_param;
	/* Default values. Always changed unless error: */
	odp_schedule_sync_t odp_schedule_sync = ODP_SCHED_SYNC_PARALLEL;
	odp_schedule_prio_t odp_prio = odp_schedule_min_prio();

	/* Init odp queue params to default values */
	odp_queue_param_init(&odp_queue_param);
	/* Set common ODP queue params based on the EM Queue config */
	queue_setup_odp_common(param, &odp_queue_param /*out*/);

	err = scheduled_queue_type_em2odp(param->type, &odp_schedule_sync /*out*/);
	if (unlikely(err)) {
		*err_str = "Q-setup-sched: invalid queue type!";
		return -1;
	}

	err = prio_em2odp(param->prio, &odp_prio /*out*/);
	if (unlikely(err)) {
		*err_str = "Q-setup-sched: invalid queue priority!";
		return -1;
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
		return -1;
	}
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
	    odp_sched_capa->waitfree_queues == ODP_SUPPORT_NO) {
		*err_str = "Q-setup-sched: non-blocking, wait-free sched queues unavailable";
		return -1;
	}
	if (odp_queue_param.enq_mode != ODP_QUEUE_OP_MT ||
	    odp_queue_param.deq_mode != ODP_QUEUE_OP_MT) {
		*err_str = "Q-setup-sched: invalid flag: scheduled queues must be MT-safe";
		return -1;
	}

	/*
	 * Note: The ODP queue context points to the EM queue elem.
	 * The EM queue context set by the user using the API function
	 * em_queue_set_context() is accessed through the queue_elem_t::context
	 * and retrieved with em_queue_context() or passed by EM to the
	 * EO-receive function for scheduled queues.
	 */
	odp_queue_param.context = q_elem;
	/*
	 * Set the context data length (in bytes) for potential prefetching.
	 * The ODP implementation may use this value as a hint for the number
	 * of context data bytes to prefetch.
	 */
	odp_queue_param.context_len = sizeof(*q_elem);

	/*
	 * Set event aggregation params for the queue if any,
	 * values already checked in queue_create_check_aggr().
	 */
	const uint32_t num_aggr = MIN(param->num_aggr, EM_QUEUE_MAX_AGGR);
	odp_event_aggr_config_t odp_aggr_config[EM_QUEUE_MAX_AGGR];

	if (num_aggr > 0) {
		err = queue_setup_odp_aggr(param->aggr_conf /* [ in:num_aggr] */,
					   odp_aggr_config  /* [out:num_aggr] */,
					   num_aggr, err_str /*out*/);
		if (unlikely(err)) /* err_str set */
			return -1;
		odp_queue_param.num_aggr = num_aggr;
		odp_queue_param.aggr = &odp_aggr_config[0];
	}

	/* Create the underlying ODP queue, stored in q_elem->odp_queue when successful */
	err = create_odp_queue(q_elem, &odp_queue_param);
	if (unlikely(err)) {
		*err_str = "Q-setup-sched: scheduled odp queue creation failed!";
		return -1;
	}

	if (num_aggr > 0) {
		err = queue_setup_aggr(q_elem, param->aggr_conf, num_aggr, err_str /*out*/);
		if (unlikely(err)) { /* err_str set by queue_setup_aggr() */
			err = odp_queue_destroy(q_elem->odp_queue);
			if (unlikely(err))
				EM_LOG(EM_LOG_ERR, "ODP queue destroy failed:%d", err);
			q_elem->odp_queue = ODP_QUEUE_INVALID;
			return -1;
		}
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
static int queue_setup_unscheduled(const em_queue_param_t *param,
				   queue_elem_t *q_elem /*out*/,
				   const char **err_str /*out*/)
{
	int err;

	q_elem->priority = EM_QUEUE_PRIO_UNDEF;
	q_elem->type = EM_QUEUE_TYPE_UNSCHEDULED;
	q_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	/* unscheduled queues are not scheduled */
	q_elem->flags.scheduled = false;
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
	queue_setup_odp_common(param, &odp_queue_param);

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
	 * and retrieved with em_queue_context().
	 */
	odp_queue_param.context = q_elem;
	/*
	 * Set the context data length (in bytes) for potential prefetching.
	 * The ODP implementation may use this value as a hint for the number
	 * of context data bytes to prefetch.
	 */
	odp_queue_param.context_len = sizeof(*q_elem);

	/*
	 * Set event aggregation params for the queue if any,
	 * values already checked in queue_create_check_aggr().
	 */
	const uint32_t num_aggr = MIN(param->num_aggr, EM_QUEUE_MAX_AGGR);
	odp_event_aggr_config_t odp_aggr_config[EM_QUEUE_MAX_AGGR];

	if (num_aggr > 0) {
		err = queue_setup_odp_aggr(param->aggr_conf /* [ in:num_aggr] */,
					   odp_aggr_config  /* [out:num_aggr] */,
					   num_aggr, err_str /*out*/);
		if (unlikely(err)) /* err_str set */
			return -1;
		odp_queue_param.num_aggr = num_aggr;
		odp_queue_param.aggr = &odp_aggr_config[0];
	}

	/* Create the underlying ODP queue, stored in q_elem->odp_queue when successful */
	err = create_odp_queue(q_elem, &odp_queue_param);
	if (unlikely(err)) {
		*err_str = "Q-setup-unsched: plain odp queue creation failed!";
		return -1;
	}

	if (num_aggr > 0) {
		err = queue_setup_aggr(q_elem, param->aggr_conf, num_aggr, err_str /*out*/);
		if (unlikely(err)) { /* err_str set by queue_setup_aggr() */
			err = odp_queue_destroy(q_elem->odp_queue);
			if (unlikely(err))
				EM_LOG(EM_LOG_ERR, "ODP queue destroy failed:%d", err);
			q_elem->odp_queue = ODP_QUEUE_INVALID;
			return -1;
		}
	}

	return 0;
}

/*
 * Helper function to queue_setup()
 *
 * Set EM queue params for (core-)local queues
 */
static int queue_setup_local(const em_queue_param_t *param,
			     queue_elem_t *q_elem /*out*/,
			     const char **err_str /*out*/)
{
	(void)err_str;

	q_elem->priority = (uint8_t)param->prio;
	q_elem->type = EM_QUEUE_TYPE_LOCAL;
	q_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	/* local queues are not scheduled */
	q_elem->flags.scheduled = false;
	q_elem->state = EM_QUEUE_STATE_INIT;

	return 0;
}

/*
 * Helper function to queue_setup()
 *
 * Set EM queue params for output queues
 */
static int queue_setup_output(const em_queue_param_t *param,
			      queue_elem_t *q_elem /*out*/,
			      const char **err_str /*out*/)
{
	queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;
	const em_output_queue_conf_t *output_conf = &param->output_conf;
	uint32_t nbr_output_queues;

	/* Double check */
	nbr_output_queues = odp_atomic_fetch_add_u32(&queue_tbl->output_queue_count, 1) + 1;

	if (unlikely(nbr_output_queues > EM_MAX_OUTPUT_QUEUES)) {
		*err_str = "Q-setup-output: too many output queues";
		goto error_dec_cnt;
	}

	q_elem->priority = EM_QUEUE_PRIO_UNDEF;
	q_elem->type = EM_QUEUE_TYPE_OUTPUT;
	q_elem->queue_group = EM_QUEUE_GROUP_UNDEF;
	/* output queues are not scheduled */
	q_elem->flags.scheduled = false;
	/* use unsched state for output queues  */
	q_elem->state = EM_QUEUE_STATE_UNSCHEDULED;

	if (unlikely(output_conf->output_fn == NULL)) {
		*err_str = "Q-setup-output: invalid output function";
		goto error_dec_cnt;
	}

	odp_ticketlock_lock(&em_shm->queue_tbl.output_queue_lock);
	for (unsigned int i = 0; i < EM_MAX_OUTPUT_QUEUES; i++) {
		if (em_shm->queue_tbl.output_queue_idx_free[i]) {
			em_shm->queue_tbl.output_queue_idx_free[i] = false;
			q_elem->output.idx = i;
			break;
		}
	}
	odp_ticketlock_unlock(&em_shm->queue_tbl.output_queue_lock);

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
		args_event = em_alloc((uint32_t)output_conf->args_len,
				      EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
		if (unlikely(args_event == EM_EVENT_UNDEF)) {
			*err_str = "Q-setup-output: alloc output_fn_args fails";
			goto error_dec_cnt;
		}
		/* store the event handle for em_free() later */
		q_elem->output.output_fn_args_event = args_event;
		args_storage = em_event_pointer(args_event);
		memcpy(args_storage, output_conf->output_fn_args,
		       output_conf->args_len);
		/* update the args ptr to point to the copied content */
		q_elem->output.output_conf.output_fn_args = args_storage;
	}
	odp_ticketlock_init(&q_elem->output.lock);

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
	queue_setup_odp_common(param, &odp_queue_param);

	odp_queue_param.type = ODP_QUEUE_TYPE_PLAIN;
	odp_queue_param.order = ODP_QUEUE_ORDER_KEEP;

	/* check nonblocking level against plain queue capabilities */
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_LF &&
	    odp_queue_capa->plain.lockfree.max_num == 0) {
		*err_str = "Q-setup-output: non-blocking, lock-free unsched queues unavailable";
		goto error_dec_cnt;
	}
	if (odp_queue_param.nonblocking == ODP_NONBLOCKING_WF &&
	    odp_queue_capa->plain.waitfree.max_num == 0) {
		*err_str = "Q-setup-output: non-blocking, wait-free unsched queues unavailable";
		goto error_dec_cnt;
	}

	/* output-queue dequeue protected by q_elem->output.lock */
	odp_queue_param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;

	/* explicitly show here that output queues should not set odp-context */
	odp_queue_param.context = NULL;

	int err = create_odp_queue(q_elem, &odp_queue_param);

	if (unlikely(err)) {
		*err_str = "Q-setup-output: plain odp queue creation failed!";
		goto error_dec_cnt;
	}

	return 0;

error_dec_cnt:
	/*
	 * Setup error: decrement the output queue count if it was incremented
	 * before the error was detected.
	 */
	odp_atomic_dec_u32(&queue_tbl->output_queue_count);
	return -1;
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
em_status_t queue_state_change__check(queue_state_t old_state,
				      queue_state_t new_state,
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

	/* Propagate state change to aggregator child queues (if any) */
	if (q_elem->flags.has_aggr) {
		uint32_t num = q_elem->aggr_parent.num_aggr;

		for (uint32_t i = 0; i < num; i++) {
			em_queue_t aq = (em_queue_t)(uintptr_t)q_elem->aggr_parent.aggr_queues[i];
			queue_elem_t *aggr_qelem = queue_elem_get(aq);

			if (likely(aggr_qelem != NULL))
				aggr_qelem->state = new_state;
		}
	}

	return EM_OK;
}

/**
 * Change the queue state
 */
em_status_t queue_state_change(queue_elem_t *const q_elem, queue_state_t new_state)
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
em_status_t queue_state_change_all(eo_elem_t *const eo_elem, queue_state_t new_state)
{
	em_status_t err = EM_OK;
	queue_elem_t *q_elem = NULL;
	const list_node_t *list_node;

	/*
	 * Loop through all queues associated with the EO
	 */
	odp_ticketlock_lock(&eo_elem->lock);

	list_for_each(&eo_elem->queue_list, list_node) {
		q_elem = eo_queue_node_to_queue_elem(list_node);
		err = queue_state_set(q_elem, new_state);
		if (unlikely(err != EM_OK))
			break;
	} /* end loop */

	odp_ticketlock_unlock(&eo_elem->lock);

	if (unlikely(err != EM_OK)) {
		uint32_t queue_u32 = q_elem ? q_elem->queue : 0;
		queue_state_t state = q_elem ? q_elem->state : EM_QUEUE_STATE_INVALID;

		return INTERNAL_ERROR(err, EM_ESCOPE_QUEUE_STATE_CHANGE,
				      "EM-Q:%" PRIx32 " inv. state: %d=>%d",
				      queue_u32, state, new_state);
	}

	return EM_OK;
}

/**
 * Enable event reception of an EM queue
 */
em_status_t queue_enable(queue_elem_t *const q_elem)
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
em_status_t queue_enable_all(eo_elem_t *const eo_elem)
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
em_status_t queue_disable(queue_elem_t *const q_elem)
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
em_status_t queue_disable_all(eo_elem_t *const eo_elem)
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

void print_queue_elem_info(void)
{
	EM_PRINT("queue-elem size: %zu B\n",
		 sizeof(queue_elem_t));

	DBG_PRINT("\t\t\toffset\tsize\n"
		  "\t\t\t------\t----\n"
		  "valid_check:\t\t%3zu B\t%2zu B\n"
		  "flags:\t\t\t%3zu B\t%2zu B\n"
		  "state:\t\t\t%3zu B\t%2zu B\n"
		  "priority:\t\t%3zu B\t%2zu B\n"
		  "type:\t\t\t%3zu B\t%2zu B\n"
		  "max_events:\t\t%3zu B\t%2zu B\n"
		  "eo:\t\t\t%3zu B\t%2zu B\n"
		  "queue:\t\t\t%3zu B\t%2zu B\n"
		  "odp_queue:\t\t%3zu B\t%2zu B\n"
		  "context:\t\t%3zu B\t%2zu B\n"
		  "union {\n"
		  "  rcv_fn:\t\t%3zu B\t%2zu B\n"
		  "  rcv_multi_fn:\t\t%3zu B\t%2zu B\n"
		  "}\n"
		  "eo_ctx:\t\t\t%3zu B\t%2zu B\n"
		  "agrp {\t\t\t%3zu B\t%2zu B\n"
		  "  .atomic_grp:\t\t%3zu B\t%2zu B\n"
		  "  .agrp_node:\t\t%3zu B\t%2zu B\n"
		  "}\n"
		  "union {\n"
		  "  aggr_parent {\t\t%3zu B\t%2zu B\n"
		  "    .num_aggr:\t\t%3zu B\t%2zu B\n"
		  "    .aggr_queues[]:\t%3zu B\t%2zu B\n"
		  "  }\n"
		  "  aggr {\t\t%3zu B\t%2zu B\n"
		  "    .parent_queue:\t%3zu B\t%2zu B\n"
		  "    .pool:\t\t%3zu B\t%2zu B\n"
		  "    .max_tmo_ns:\t%3zu B\t%2zu B\n"
		  "    .max_size:\t\t%3zu B\t%2zu B\n"
		  "    .event_type:\t%3zu B\t%2zu B\n"
		  "  }\n"
		  "  output {\t\t%3zu B\t%2zu B\n"
		  "    .conf:\t\t%3zu B\t%2zu B\n"
		  "    .args_event:\t%3zu B\t%2zu B\n"
		  "    .idx:\t\t%3zu B\t%2zu B\n"
		  "    .lock:\t\t%3zu B\t%2zu B\n"
		  "  }\n"
		  "}\n"
		  "eo_elem:\t\t%3zu B\t%2zu B\n"
		  "queue_group:\t\t%3zu B\t%2zu B\n"
		  "eo_queue_node:\t\t%3zu B\t%2zu B\n"
		  "qgrp_node:\t\t%3zu B\t%2zu B\n"
		  "queue_pool_elem:\t%3zu B\t%2zu B\n"
		  "  <pad>\t\t\t%3zu B\n"
		  "end:\t\t\t%3zu B\t%2zu B\n",
		  offsetof(queue_elem_t, valid_check), sizeof_field(queue_elem_t, valid_check),
		  offsetof(queue_elem_t, flags), sizeof_field(queue_elem_t, flags),
		  offsetof(queue_elem_t, state), sizeof_field(queue_elem_t, state),
		  offsetof(queue_elem_t, priority), sizeof_field(queue_elem_t, priority),
		  offsetof(queue_elem_t, type), sizeof_field(queue_elem_t, type),
		  offsetof(queue_elem_t, max_events), sizeof_field(queue_elem_t, max_events),
		  offsetof(queue_elem_t, eo), sizeof_field(queue_elem_t, eo),
		  offsetof(queue_elem_t, queue), sizeof_field(queue_elem_t, queue),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  offsetof(queue_elem_t, odp_queue), sizeof_field(queue_elem_t, odp_queue),
		  offsetof(queue_elem_t, context), sizeof_field(queue_elem_t, context),
		  offsetof(queue_elem_t, receive_func),
		  sizeof_field(queue_elem_t, receive_func),
		  offsetof(queue_elem_t, receive_multi_func),
		  sizeof_field(queue_elem_t, receive_multi_func),
		  offsetof(queue_elem_t, eo_ctx), sizeof_field(queue_elem_t, eo_ctx),
		  offsetof(queue_elem_t, agrp), sizeof_field(queue_elem_t, agrp),
		  offsetof(queue_elem_t, agrp.atomic_group),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  sizeof_field(queue_elem_t, agrp.atomic_group),
		  offsetof(queue_elem_t, agrp.agrp_node),
		  sizeof_field(queue_elem_t, agrp.agrp_node),
		  offsetof(queue_elem_t, aggr_parent), sizeof_field(queue_elem_t, aggr_parent),
		  offsetof(queue_elem_t, aggr_parent.num_aggr),
		  sizeof_field(queue_elem_t, aggr_parent.num_aggr),
		  offsetof(queue_elem_t, aggr_parent.aggr_queues),
		  sizeof_field(queue_elem_t, aggr_parent.aggr_queues),
		  offsetof(queue_elem_t, aggr), sizeof_field(queue_elem_t, aggr),
		  offsetof(queue_elem_t, aggr.parent_queue),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  sizeof_field(queue_elem_t, aggr.parent_queue),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  offsetof(queue_elem_t, aggr.pool), sizeof_field(queue_elem_t, aggr.pool),
		  offsetof(queue_elem_t, aggr.max_tmo_ns),
		  sizeof_field(queue_elem_t, aggr.max_tmo_ns),
		  offsetof(queue_elem_t, aggr.max_size), sizeof_field(queue_elem_t, aggr.max_size),
		  offsetof(queue_elem_t, aggr.event_type),
		  sizeof_field(queue_elem_t, aggr.event_type),
		  offsetof(queue_elem_t, output), sizeof_field(queue_elem_t, output),
		  offsetof(queue_elem_t, output.output_conf),
		  sizeof_field(queue_elem_t, output.output_conf),
		  offsetof(queue_elem_t, output.output_fn_args_event),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  sizeof_field(queue_elem_t, output.output_fn_args_event),
		  offsetof(queue_elem_t, output.idx), sizeof_field(queue_elem_t, output.idx),
		  offsetof(queue_elem_t, output.lock), sizeof_field(queue_elem_t, output.lock),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  offsetof(queue_elem_t, eo_elem), sizeof_field(queue_elem_t, eo_elem),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  offsetof(queue_elem_t, queue_group), sizeof_field(queue_elem_t, queue_group),
		  offsetof(queue_elem_t, eo_queue_node), sizeof_field(queue_elem_t, eo_queue_node),
		  offsetof(queue_elem_t, qgrp_node), sizeof_field(queue_elem_t, qgrp_node),
		  offsetof(queue_elem_t, queue_pool_elem),
		  sizeof_field(queue_elem_t, queue_pool_elem),
		  offsetof(queue_elem_t, end) - offsetof(queue_elem_t, end_data),
		  offsetof(queue_elem_t, end), sizeof_field(queue_elem_t, end));

	EM_PRINT("\n");
}

/*
 * Format a queue-ID range as decimal and hex strings for printing.
 * Buffers are left unchanged (caller pre-fills with "n/a") when the range
 * is not configured (num_queues == 0) or the ID equals INVALID_QUEUE_ID.
 */
static void
format_qid_range(unsigned int num_queues, uint16_t first_id, uint16_t last_id,
		 char first_dec[24], char last_dec[24],
		 char first_hex[24], char last_hex[24])
{
	if (!num_queues)
		return;
	if (first_id != INVALID_QUEUE_ID) {
		snprintf(first_dec, 24, "%u", first_id);
		snprintf(first_hex, 24, "0x%x", first_id);
	}
	if (last_id != INVALID_QUEUE_ID) {
		snprintf(last_dec, 24, "%u", last_id);
		snprintf(last_hex, 24, "0x%x", last_id);
	}
}

void print_queue_capa(void)
{
	const odp_queue_capability_t *queue_capa =
		&em_shm->queue_tbl.odp_queue_capability;
	const odp_schedule_capability_t *sched_capa =
		&em_shm->queue_tbl.odp_schedule_capability;
	const queue_tbl_t *queue_tbl = &em_shm->queue_tbl;
	char plain_sz[24] = "n/a";
	char plain_lf_sz[24] = "n/a";
	char plain_wf_sz[24] = "n/a";
	char sched_sz[24] = "nolimit";

	char first_static_qid[24] = "n/a";
	char last_static_qid[24] = "n/a";
	char first_dyn_qid[24] = "n/a";
	char last_dyn_qid[24] = "n/a";
	char first_aggr_qid[24] = "n/a";
	char last_aggr_qid[24] = "n/a";
	char first_static_qid_hex[24] = "n/a";
	char last_static_qid_hex[24] = "n/a";
	char first_dyn_qid_hex[24] = "n/a";
	char last_dyn_qid_hex[24] = "n/a";
	char first_aggr_qid_hex[24] = "n/a";
	char last_aggr_qid_hex[24] = "n/a";

	const unsigned int max_queues = em_shm->queue_tbl.max_queue_num;

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

	format_qid_range(em_shm->opt.queue.num_static,
			 queue_tbl->first_static_queue_id,
			 queue_tbl->last_static_queue_id,
			 first_static_qid, last_static_qid,
			 first_static_qid_hex, last_static_qid_hex);

	format_qid_range(em_shm->opt.queue.num_dynamic,
			 queue_tbl->first_dyn_queue_id,
			 queue_tbl->last_dyn_queue_id,
			 first_dyn_qid, last_dyn_qid,
			 first_dyn_qid_hex, last_dyn_qid_hex);

	format_qid_range(em_shm->opt.queue.num_aggr,
			 queue_tbl->first_aggr_queue_id,
			 queue_tbl->last_aggr_queue_id,
			 first_aggr_qid, last_aggr_qid,
			 first_aggr_qid_hex, last_aggr_qid_hex);
	/* snprintf() has '\0'-terminated all strings above */

	/* The first EM queue id depends if static queues are configured or not */
	uint16_t queue_range_first = em_shm->opt.queue.num_static ?
				     queue_tbl->first_static_queue_id :
				     queue_tbl->first_internal_queue_id;

	/* The last EM queue id depends on which queue ranges are configured */
	uint16_t queue_range_last = queue_tbl->last_internal_queue_id;

	if (em_shm->opt.queue.num_dynamic)
		queue_range_last = queue_tbl->last_dyn_queue_id;
	if (em_shm->opt.queue.num_aggr)
		queue_range_last = queue_tbl->last_aggr_queue_id;

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
		 "  EM queue range:   [%u - %u] ([0x%x - 0x%x])\n"
		 "    static range:   [%s - %s] ([%s - %s])\n"
		 "    internal range: [%d - %d] ([0x%x - 0x%x])\n"
		 "    dynamic range:  [%s - %s] ([%s - %s])\n"
		 "    aggreg. range:  [%s - %s] ([%s - %s])\n"
		 "\n",
		 max_queues, max_queues,
		 EM_QUEUE_RANGE_OFFSET, EM_QUEUE_RANGE_OFFSET,
		 queue_range_first, queue_range_last,
		 queue_range_first, queue_range_last,
		 first_static_qid, last_static_qid,
		 first_static_qid_hex, last_static_qid_hex,
		 queue_tbl->first_internal_queue_id, queue_tbl->last_internal_queue_id,
		 queue_tbl->first_internal_queue_id, queue_tbl->last_internal_queue_id,
		 first_dyn_qid, last_dyn_qid,
		 first_dyn_qid_hex, last_dyn_qid_hex,
		 first_aggr_qid, last_aggr_qid,
		 first_aggr_qid_hex, last_aggr_qid_hex);
}

void print_queue_prio_info(void)
{
	#define MAXPRIOBUF 128
	char buf[MAXPRIOBUF];
	int pos = 0;

	for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
		/* comma separated list of priorities */
		int num = snprintf(&buf[pos], MAXPRIOBUF - pos, "%d%c",
				   em_shm->queue_prio.map[i],
				   i < (EM_QUEUE_PRIO_NUM - 1) ? ',' : '\0');
		if (num < 0 || num >= (MAXPRIOBUF - pos))
			break;
		pos += num;
	}

	buf[MAXPRIOBUF - 1] = 0;
	EM_PRINT("  Current queue priority map: [%s]\n", buf);
}

unsigned int queue_count(void)
{
	return odp_atomic_load_u32(&em_shm->queue_count);
}

size_t queue_name(const queue_elem_t *const q_elem,
		  char name[/*out*/], const size_t maxlen)
{
	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	const char *queue_name = &em_shm->queue_tbl.name[queue_hdl2idx(queue)][0];
	size_t len = strnlen(queue_name, EM_QUEUE_NAME_LEN - 1);

	if (maxlen - 1 < len)
		len = maxlen - 1;

	if (len)
		memcpy(name, queue_name, len);
	name[len] = '\0';

	return len;
}

static void queue_init_prio_legacy(int minp, int maxp)
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
		queue_init_prio_legacy(minp, maxp);
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

const char *queue_state_str(queue_state_t state)
{
	const char *str;

	switch (state) {
	case EM_QUEUE_STATE_INVALID:
		str = "INVALID";
		break;
	case EM_QUEUE_STATE_INIT:
		str = "INIT";
		break;
	case EM_QUEUE_STATE_BIND:
		str = "BIND";
		break;
	case EM_QUEUE_STATE_READY:
		str = "READY";
		break;
	case EM_QUEUE_STATE_UNSCHEDULED:
		str = "UNSCH";
		break;
	default:
		str = "UNKNOWN";
		break;
	}

	return str;
}

const char *queue_type_str(em_queue_type_t type)
{
	const char *type_str;

	switch (type) {
	case EM_QUEUE_TYPE_UNDEF:
		type_str = "UNDEF";
		break;
	case EM_QUEUE_TYPE_ATOMIC:
		type_str = "ATOMIC";
		break;
	case EM_QUEUE_TYPE_PARALLEL:
		type_str = "PARALLEL";
		break;
	case EM_QUEUE_TYPE_ORDERED:
		type_str = "ORDERED";
		break;
	case EM_QUEUE_TYPE_UNSCHEDULED:
		type_str = "UNSCH";
		break;
	case EM_QUEUE_TYPE_LOCAL:
		type_str = "LOCAL";
		break;
	case EM_QUEUE_TYPE_OUTPUT:
		type_str = "OUTPUT";
		break;
	default:
		type_str = "UNKNOWN";
		break;
	}

	return type_str;
}

#define QUEUE_INFO_HDR_STR \
"Number of queues: %d\n\n" \
"Handle    Name                            Priority  Type      State    Qgrp" \
"      Agrp      EO        Multi-rcv  Max-events  Ctx\n" \
"---------------------------------------------------------------------------" \
"----------------------------------------------------\n" \
"%s\n"

#define QUEUE_INFO_LEN 128

#define QUEUE_INFO_FMT \
"%-10" PRI_QUEUE "%-32s%-10" PRI_QPRIO "%-10s%-9s%-10" PRI_QGRP "%-10" PRI_AGRP \
"%-10" PRI_EO "%-11c%-12d%-3c\n" /*128 bytes per queue*/

void print_queue_info(void)
{
	unsigned int q_num;
	const queue_elem_t *q_elem;
	char q_name[EM_QUEUE_NAME_LEN];
	int len = 0;
	int n_print = 0;

	em_queue_t q = em_queue_first(&q_num);

	/* q_num may not match the amount of queues actually returned by iterating
	 * using em_queue_next() if queues are added or removed in parallel
	 * by another core. Thus space for 10 extra queues is reserved. If more
	 * than 10 queues are added by other cores in parallel, we print only info
	 * of the (q_num + 10) queues.
	 */
	const int q_info_buf_len = (q_num + 10) * QUEUE_INFO_LEN + 1/*Terminating null byte*/;
	char q_info_buf[q_info_buf_len];

	while (q != EM_QUEUE_UNDEF) {
		q_elem = queue_elem_get(q);

		if (unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
			q = em_queue_next();
			continue;
		}

		em_atomic_group_t atomic_group = EM_ATOMIC_GROUP_UNDEF;
		em_eo_t eo = (em_eo_t)(uintptr_t)q_elem->eo;

		if (q_elem->flags.in_atomic_group)
			atomic_group = q_elem->agrp.atomic_group;

		queue_name(q_elem, q_name, EM_QUEUE_NAME_LEN - 1);
		n_print = snprintf(q_info_buf + len,
				   q_info_buf_len - len,
				   QUEUE_INFO_FMT,
				   q, q_name, q_elem->priority,
				   queue_type_str(q_elem->type),
				   queue_state_str(q_elem->state),
				   q_elem->queue_group, atomic_group, eo,
				   q_elem->flags.use_multi_rcv ? 'Y' : 'N',
				   q_elem->max_events,
				   q_elem->context ? 'Y' : 'N');

		/* Not enough space to hold more queue info */
		if (n_print >= q_info_buf_len - len)
			break;

		len += n_print;
		q = em_queue_next();
	}

	/* No queue */
	if (len == 0) {
		EM_PRINT("No EM queue!\n");
		return;
	}

	/*
	 * To prevent printing incomplete information of the last queue when
	 * there is not enough space to hold all queue info.
	 */
	q_info_buf[len] = '\0';
	EM_PRINT(QUEUE_INFO_HDR_STR, q_num, q_info_buf);
}
