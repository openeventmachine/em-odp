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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_core.h"
#include "em_core_types.h"
#include "em_error.h"
#include "em_init.h"
#include "em_internal_event.h"
#include "em_internal_event_types.h"
#include "em_libconfig.h"
#include "em_mem.h"

em_status_t core_map_init(core_map_t *const core_map)
{
	memset(core_map, 0, sizeof(core_map_t));

	odp_rwlock_init(&core_map->rwlock);
	odp_atomic_init_u32(&core_map->current_core_count, 0);
	odp_atomic_init_u32(&core_map->worker_core_count, 0);
	odp_atomic_init_u32(&core_map->control_core_count, 0);
	odp_atomic_init_u32(&core_map->external_thread_count, 0);

	/* Check em_conf_t::core_count */
	uint32_t max_cores = em_shm->conf.core_count;
	uint32_t thr_max = MIN(odp_thread_count_max(), ODP_THREAD_COUNT_MAX);

	if (max_cores < 1 || max_cores > EM_MAX_CORES ||
	    thr_max < 1 || max_cores > thr_max) {
		EM_LOG(EM_LOG_ERR, "Invalid max core_count=%d! EM_MAX_CORES=%u, ODP-thr-max=%d\n",
		       max_cores, EM_MAX_CORES, thr_max);
		return EM_ERR_TOO_LARGE;
	}

	/* Check em_conf_t::phys_mask */
	const em_core_mask_t *allowed_mask = &em_shm->conf.phys_mask;
	uint32_t mask_count = em_core_mask_count(allowed_mask);

	if (mask_count < 1 || mask_count < max_cores) {
		char mask_str[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mask_str, sizeof(mask_str), allowed_mask);
		EM_LOG(EM_LOG_ERR, "Invalid phys_mask=%s! set bits=%d < max core_count=%d\n",
		       mask_str, mask_count, max_cores);
		return EM_ERR_TOO_SMALL;
	}

	return EM_OK;
}

/**
 * Verify the sched affinity for worker and control EM-cores
 *
 * Helper to core_map_init_local()
 */
static em_status_t verify_sched_affinity(em_core_type_t core_type, pid_t tid,
					 uint32_t max_cores, const em_core_mask_t *allowed_mask)
{
	cpu_set_t cpu_set;

	CPU_ZERO(&cpu_set);
	if (sched_getaffinity(tid, sizeof(cpu_set), &cpu_set) == -1) {
		EM_LOG(EM_LOG_ERR, "sched_getaffinity() failed: %s (%d)\n",
		       strerror(errno), errno);
		return EM_ERR_LIB_FAILED;
	}

	const uint32_t cpu_set_count = CPU_COUNT(&cpu_set);

	if (core_type == EM_CORE_TYPE_WORKER) {
		if (cpu_set_count != 1) {
			EM_LOG(EM_LOG_ERR,
			       "EM-core type is WORKER, but cpuset count=%d != 1\n",
			       cpu_set_count);
			return EM_ERR_BAD_STATE;
		}
	} else if (core_type == EM_CORE_TYPE_CONTROL) {
		if (cpu_set_count > max_cores) {
			EM_LOG(EM_LOG_ERR,
			       "EM-core type is CONTROL, but cpuset count=%d > max=%d\n",
			       cpu_set_count, max_cores);
			return EM_ERR_BAD_STATE;
		}

		em_core_mask_t cpuset_mask;
		em_core_mask_t or_mask;

		em_core_mask_zero(&cpuset_mask);

		for (int i = 0; i < CPU_SETSIZE; i++) {
			if (CPU_ISSET(i, &cpu_set))
				em_core_mask_set(i, &cpuset_mask);
		}

		/* the 'cpuset_mask' must be subset of em_conf_t::phys_mask */
		em_core_mask_or(&or_mask, &cpuset_mask, allowed_mask);

		if (!em_core_mask_equal(&or_mask, allowed_mask)) {
			char cpuset_str[EM_CORE_MASK_STRLEN];
			char allowed_str[EM_CORE_MASK_STRLEN];

			em_core_mask_tostr(cpuset_str, sizeof(cpuset_str), &cpuset_mask);
			em_core_mask_tostr(allowed_str, sizeof(allowed_str), allowed_mask);

			EM_LOG(EM_LOG_ERR,
			       "Cpuset of the control EM-core must be subset of conf.phys_mask\n"
			       "cpuset=%s conf.phys_mask=%s\n",
			       cpuset_str, allowed_str);
			return EM_ERR_BAD_STATE;
		}
	} else {
		EM_LOG(EM_LOG_ERR, "Unsupported EM-core type:%d\n", core_type);
		return EM_ERR_NOT_FOUND;
	}

	return EM_OK;
}

/**
 * @brief Determine the EM-core id to use for this EM-core
 *
 * Helper to core_map_init_local() and must be called with the core_map
 * RW-lock held: odp_rwlock_write_lock(&core_map->rwlock);
 *
 * @param req_core_id  Requested core-id or -1 to let EM decide
 * @param max_cores    Max number of allowed EM-cores
 * @param core_map     Core map structure
 *
 * @return EM-core id to use or -1 on error
 */
static int determine_core_id(int req_core_id, uint32_t max_cores, const core_map_t *core_map)
{
	if (unlikely(req_core_id < -1)) {
		EM_LOG(EM_LOG_ERR,
		       "Requested core-id:%d invalid:\n"
		       "  -1: Let EM decide, [0 - %u]: req id range\n",
		       req_core_id, max_cores - 1);
		return -1;
	}

	/*
	 * -1: let EM assign the core-id
	 */
	if (req_core_id == -1) {
		uint32_t core_id = 0;

		for (; core_id < max_cores; core_id++) {
			/* Use the first unset bit in the coremask of EM-cores => core-id */
			if (!em_core_mask_isset(core_id, &core_map->rwlocked.logic_mask))
				break;
		}
		if (unlikely(core_id == max_cores)) {
			EM_LOG(EM_LOG_ERR, "No free EM-core ids found in range [0 - %u] (max:%u)\n",
			       max_cores - 1, max_cores);
			return -1;
		}

		return (int)core_id;
	}

	/*
	 * Use the requested EM-core id if available (unused).
	 */
	if ((uint32_t)req_core_id >= max_cores) {
		EM_LOG(EM_LOG_ERR, "Given core-id:%d too large (max:%u)\n",
		       req_core_id, max_cores - 1);
		return -1;
	}

	int unavailable = em_core_mask_isset(req_core_id, &core_map->rwlocked.logic_mask);

	if (unlikely(unavailable)) {
		EM_LOG(EM_LOG_ERR, "Given core-id:%d already in use\n", req_core_id);
		return -1;
	}

	/* The requested EM-core is available, return it. */
	return req_core_id;
}

/**
 * Local init of EM-cores of type EM_CORE_TYPE_WORKER or EM_CORE_TYPE_CONTROL
 */
em_status_t core_map_init_local(core_map_t *const core_map, const em_conf_local_t *conf_local)
{
	em_locm_t *const locm = &em_locm;
	const em_core_mask_t *allowed_mask = &em_shm->conf.phys_mask;
	const uint32_t max_cores = em_shm->conf.core_count; /* <= EM_MAX_CORES */
	const int phys_core = odp_cpu_id();
	const pid_t tid = gettid();
	em_status_t err;
	int core_id;

	/* Verify that the current physical core/cpu is in the list of allowed ones */
	if (!em_core_mask_isset(phys_core, allowed_mask)) {
		char mask_str[EM_CORE_MASK_STRLEN];

		em_core_mask_tostr(mask_str, sizeof(mask_str), allowed_mask);
		EM_LOG(EM_LOG_ERR, "The used phys_core:%d not allowed by the phys_mask:%s\n",
		       phys_core, mask_str);
		return EM_ERR_BAD_ID;
	}

	/* Verify the sched affinity for worker and control EM-cores */
	err = verify_sched_affinity(conf_local->core_type, tid,
				    max_cores, allowed_mask);
	if (unlikely(err != EM_OK))
		return err;

	const int odp_thr = odp_thread_id();
	const int thr_max = MIN(odp_thread_count_max(), ODP_THREAD_COUNT_MAX);

	if (odp_thr < 0 || odp_thr >= thr_max) {
		EM_LOG(EM_LOG_ERR, "Invalid ODP thread-id:%d, id range is 0->%d\n",
		       odp_thr, thr_max);
		return EM_ERR_LIB_FAILED;
	}

	/* Generate the path to the /proc/[tid]/stat file, store path inside RW-lock */
	char path[32];
	size_t path_len;

	snprintf(path, sizeof(path), "/proc/%d/stat", tid);
	path[sizeof(path) - 1] = '\0';
	path_len = strnlen(path, sizeof(path));

	if (path_len >= sizeof(path)) {
		EM_LOG(EM_LOG_ERR, "strnlen(%s)=%zu >= sizeof(path)=%zu\n",
		       path, path_len, sizeof(path));
		return EM_ERR_LIB_FAILED;
	}
	path_len++; /* include the '\0' */

	/*
	 * Lock RW-lock for writing
	 */
	odp_rwlock_write_lock(&core_map->rwlock);

	/* Set EM-core id */
	core_id = determine_core_id(conf_local->core_id /* requested */, max_cores, core_map);
	if (unlikely(core_id < 0)) {
		/* Unlock RW-lock on error, writing done */
		odp_rwlock_write_unlock(&core_map->rwlock);
		return EM_ERR_TOO_SMALL;
	}
	locm->core_id = core_id;

	uint32_t current_core_count = odp_atomic_fetch_inc_u32(&core_map->current_core_count) + 1;

	if (unlikely(current_core_count > max_cores)) {
		/* Roll back core count on error*/
		odp_atomic_dec_u32(&core_map->current_core_count);
		/* Unlock RW-lock on error, writing done */
		odp_rwlock_write_unlock(&core_map->rwlock);
		EM_LOG(EM_LOG_ERR, "Current core count:%d too large (max:%d)\n",
		       current_core_count, max_cores);
		return EM_ERR_TOO_LARGE;
	}

	core_map->rwlocked.thr_vs_logic.logic[odp_thr] = (uint8_t)core_id;
	core_map->rwlocked.thr_vs_logic.odp_thr[core_id] = (uint16_t)odp_thr;
	core_map->rwlocked.tids[core_id] = tid;
	core_map->rwlocked.core_types[core_id] = conf_local->core_type;

	em_core_mask_set(core_id, &core_map->rwlocked.logic_mask);
	odp_thrmask_set(&core_map->rwlocked.thrmask_emcores, odp_thr);
	odp_thrmask_set(&core_map->rwlocked.thrmask_all, odp_thr);

	if (conf_local->core_type == EM_CORE_TYPE_WORKER) {
		odp_atomic_inc_u32(&core_map->worker_core_count);
		em_core_mask_set(core_id, &core_map->rwlocked.worker_mask);
	} else {
		odp_atomic_inc_u32(&core_map->control_core_count);
		em_core_mask_set(core_id, &core_map->rwlocked.control_mask);
	}

	/* store the /proc/[tid]/stat file path */
	memcpy(core_map->rwlocked.proc_tid_stat_paths[core_id], path, path_len);

	/*
	 * Unlock RW-lock, writing done
	 */
	odp_rwlock_write_unlock(&core_map->rwlock);

	DBG_PRINT("EM-core%02d (%s): %s() - EM-cores:%d (workers:%u ctrl:%u), EM-ext-thrs:%u\n",
		  core_id, __func__, conf_local->core_type == EM_CORE_TYPE_WORKER ? "WRK" : "CTL",
		  current_core_count, odp_atomic_load_u32(&core_map->worker_core_count),
		  odp_atomic_load_u32(&core_map->control_core_count),
		  odp_atomic_load_u32(&core_map->external_thread_count));

	return EM_OK;
}

/**
 * Local init of an EM external thread/process, EM_CORE_TYPE_EXT
 */
em_status_t core_map_init_extthr(core_map_t *const core_map)
{
	em_locm_t *const locm = &em_locm;
	const int odp_thr = odp_thread_id();
	const int thr_max = MIN(odp_thread_count_max(), ODP_THREAD_COUNT_MAX);
	const pid_t tid = gettid();

	locm->core_id = -1;

	if (odp_thr < 0 || odp_thr >= thr_max) {
		EM_LOG(EM_LOG_ERR,
		       "Ext-thread: Invalid ODP thread-id:%d, id range is 0->%d\n",
		       odp_thr, thr_max);
		return EM_ERR_LIB_FAILED;
	}

	/*
	 * Lock RW-lock for writing
	 */
	odp_rwlock_write_lock(&core_map->rwlock);

	odp_atomic_inc_u32(&core_map->external_thread_count);
	odp_thrmask_set(&core_map->rwlocked.thrmask_extthrs, odp_thr);
	odp_thrmask_set(&core_map->rwlocked.thrmask_all, odp_thr);
	core_map->rwlocked.ext_tids[odp_thr] = tid;

	/*
	 * Unlock RW-lock, writing done
	 */
	odp_rwlock_write_unlock(&core_map->rwlock);

	DBG_PRINT("EM-extthr%02d (EXT): %s() - EM-cores:%d (workers:%u ctrl:%u), EM-ext-thrs:%u\n",
		  odp_thr, __func__, odp_atomic_load_u32(&core_map->current_core_count),
		  odp_atomic_load_u32(&core_map->worker_core_count),
		  odp_atomic_load_u32(&core_map->control_core_count),
		  odp_atomic_load_u32(&core_map->external_thread_count));

	return EM_OK;
}

/**
 * EM internal event handler (see em_internal_event.c&h)
 * Handle the internal event notifying the EM-cores about a thread termination
 * (internal event id: TERM_THREAD_REQ).
 */
void i_event__term_thread_req(const internal_event_t *i_ev)
{
	const int thr = odp_thread_id();
	int32_t cnt = odp_atomic_fetch_dec_u32(&em_shm->core_map.ongoing_term_reqs[thr]);

	if (em_locm.is_em_core)
		DBG_PRINT("EM-core%02d (thr%02d): TERM_THREAD_REQ from thr%02d - ongoing-term-reqs=%d\n",
			  em_core_id(), thr, i_ev->term_thread.thr, cnt);
	else
		DBG_PRINT("EM-extthr%02d: TERM_THREAD_REQ from thr%02d - ongoing-term-reqs=%d\n",
			  thr, i_ev->term_thread.thr, cnt);

	if (unlikely(cnt < 0)) {
		INTERNAL_ERROR(EM_ERR_TOO_SMALL, EM_ESCOPE_INTERNAL_EVENT_RECV_FUNC,
			       "TERM_THREAD_REQ - ongoing-term-reqs=%d", i_ev->id, cnt);
	}
}

/**
 * Done-callback for the internal TERM_THREAD_REQ
 *
 * Function run when all receivers of the TERM_THREAD_REQ-events
 * have processed the requests.
 */
static void term_thread_req_done_callback(void *arg)
{
	(void)arg;

	em_locm.is_term_thread_done = true;

	if (em_locm.is_em_core)
		DBG_PRINT("EM-core%02d (thr%02d): TERM_THREAD_REQ DONE\n",
			  em_core_id(), odp_thread_id());
	else
		DBG_PRINT("EM-extthr%02d: TERM_THREAD_REQ DONE\n",
			  odp_thread_id());
}

/**
 * Allocate and send TERM_THREAD_REQ events to all remaining EM-cores
 */
static em_status_t
term_thread_req(int thread_id, const odp_thrmask_t *remain_thrmask_emcores, em_escope_t escope)
{
	em_event_t event = em_alloc(sizeof(internal_event_t),
				    EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
			EM_ERR_ALLOC_FAILED, escope,
			"Internal event (TERM_THREAD_REQ) allocation failed");

	internal_event_t *i_event = em_event_pointer(event);

	i_event->id = TERM_THREAD_REQ;
	i_event->term_thread.thr = thread_id;

	int err = send_core_ctrl_events(remain_thrmask_emcores, event,
					term_thread_req_done_callback, NULL,
					0, NULL, true);
	if (unlikely(err)) {
		char mask_str[ODP_THRMASK_STR_SIZE];

		odp_thrmask_to_str(remain_thrmask_emcores, mask_str, ODP_THRMASK_STR_SIZE);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, escope,
				      "send_core_ctrl_events(mask=%s) failed", mask_str);
	}

	return EM_OK;
}

int core_map_term_local(core_map_t *const core_map, em_escope_t escope)
{
	const em_locm_t *const locm = &em_locm;
	const int32_t core_id = locm->core_id;
	const int odp_thr = odp_thread_id();
	odp_thrmask_t remain_thrmask_emcores;
	em_status_t stat = EM_OK;

	/*
	 * Lock RW-lock for writing
	 */
	odp_rwlock_write_lock(&core_map->rwlock);

	int32_t current_core_count = odp_atomic_fetch_dec_u32(&core_map->current_core_count) - 1;

	core_map->rwlocked.thr_vs_logic.logic[odp_thr] = 0; /* EM core-id */
	core_map->rwlocked.thr_vs_logic.odp_thr[core_id] = 0; /* odp thread-id */

	em_core_mask_clr(core_id, &core_map->rwlocked.logic_mask);
	if (locm->conf_local.core_type == EM_CORE_TYPE_WORKER) {
		odp_atomic_dec_u32(&core_map->worker_core_count);
		em_core_mask_clr(core_id, &core_map->rwlocked.worker_mask);
	} else {
		odp_atomic_dec_u32(&core_map->control_core_count);
		em_core_mask_clr(core_id, &core_map->rwlocked.control_mask);
	}
	odp_thrmask_clr(&core_map->rwlocked.thrmask_emcores, odp_thr);
	odp_thrmask_clr(&core_map->rwlocked.thrmask_all, odp_thr);

	odp_thrmask_copy(&remain_thrmask_emcores, &core_map->rwlocked.thrmask_emcores);

	if (current_core_count > 0) {
		int thr = odp_thrmask_first(&remain_thrmask_emcores);

		while (thr >= 0) {
			odp_atomic_inc_u32(&core_map->ongoing_term_reqs[thr]);
			thr = odp_thrmask_next(&remain_thrmask_emcores, thr);
		}
	}

	core_map->rwlocked.tids[core_id] = 0;
	core_map->rwlocked.core_types[core_id] = EM_CORE_TYPE_UNDEF;
	core_map->rwlocked.proc_tid_stat_paths[core_id][0] = '\0';

	if (current_core_count >= 1)
		stat = term_thread_req(odp_thr, &remain_thrmask_emcores, escope);

	/*
	 * Unlock RW-lock, writing done
	 */
	odp_rwlock_write_unlock(&core_map->rwlock);

	if (stat != EM_OK)
		(void)INTERNAL_ERROR(stat, escope, "term_thread_req() failed");

	if (current_core_count < 1) {
		/* Last core manually calls the term-done function */
		term_thread_req_done_callback(NULL);
	}

	if (unlikely(current_core_count < 0))
		EM_LOG(EM_LOG_ERR, "Current core count invalid: %d", current_core_count);

	DBG_PRINT("EM-core%02d (thr%02d): %s() - current core count:%d\n",
		  locm->core_id, odp_thr, __func__, current_core_count);

	return current_core_count;
}

int core_map_term_extthr(core_map_t *const core_map, em_escope_t escope)
{
	const int odp_thr = odp_thread_id();
	odp_thrmask_t remain_thrmask_emcores;
	em_status_t stat = EM_OK;

	/*
	 * Lock RW-lock for writing
	 */
	odp_rwlock_write_lock(&core_map->rwlock);

	int32_t current_core_count = odp_atomic_load_u32(&core_map->current_core_count);

	odp_atomic_dec_u32(&core_map->external_thread_count);
	odp_thrmask_clr(&core_map->rwlocked.thrmask_extthrs, odp_thr);
	odp_thrmask_clr(&core_map->rwlocked.thrmask_all, odp_thr);

	odp_thrmask_copy(&remain_thrmask_emcores, &core_map->rwlocked.thrmask_emcores);

	if (current_core_count > 0) {
		int thr = odp_thrmask_first(&remain_thrmask_emcores);

		while (thr >= 0) {
			odp_atomic_inc_u32(&core_map->ongoing_term_reqs[thr]);
			thr = odp_thrmask_next(&remain_thrmask_emcores, thr);
		}
	}
	odp_atomic_inc_u32(&core_map->ongoing_term_reqs[odp_thr]);

	core_map->rwlocked.ext_tids[odp_thr] = 0;

	if (current_core_count >= 1)
		stat = term_thread_req(odp_thr, &remain_thrmask_emcores, escope);

	/*
	 * Unlock RW-lock, writing done
	 */
	odp_rwlock_write_unlock(&core_map->rwlock);

	if (stat != EM_OK)
		(void)INTERNAL_ERROR(stat, escope, "term_thread_req() failed");

	if (current_core_count < 1) {
		/* No EM-cores running (worker or control), call the term-done function */
		term_thread_req_done_callback(NULL);
	}

	if (unlikely(current_core_count < 0)) {
		EM_LOG(EM_LOG_ERR, "Current core count invalid: %d", current_core_count);
		return -1;
	}

	DBG_PRINT("EM-extthr%02d: %s() - current EM-core count:%d\n",
		  odp_thr, __func__, current_core_count);

	return current_core_count;
}

void mask_em2odp__rwlocked(const em_core_mask_t *const core_mask,
			   odp_thrmask_t *const odp_thrmask /*out*/)
{
	const core_map_t *const core_map = &em_shm->core_map;

	odp_thrmask_zero(odp_thrmask);

	/* Assumes lock is already taken: odp_rwlock_read_lock(&em_shm->core_map.rwlock) */
	int core = em_core_mask_first(core_mask);

	while (core >= 0) {
		int odp_thread_id = core_map->rwlocked.thr_vs_logic.odp_thr[core];

		odp_thrmask_set(odp_thrmask, odp_thread_id);
		core = em_core_mask_next(core_mask, core);
	}

	/* Assumes lock is unlocked by caller: odp_rwlock_read_unlock(&em_shm->core_map.rwlock); */
}

void mask_em2odp(const em_core_mask_t *const core_mask,
		 odp_thrmask_t *const odp_thrmask /*out*/)
{
	core_map_t *const core_map = &em_shm->core_map;

	odp_thrmask_zero(odp_thrmask);

	odp_rwlock_read_lock(&core_map->rwlock);

	int core = em_core_mask_first(core_mask);

	while (core >= 0) {
		int odp_thread_id = core_map->rwlocked.thr_vs_logic.odp_thr[core];

		odp_thrmask_set(odp_thrmask, odp_thread_id);
		core = em_core_mask_next(core_mask, core);
	}

	odp_rwlock_read_unlock(&core_map->rwlock);
}

/**
 * @brief Get the CPU ID from the /proc/[tid]/stat file.
 *
 * This function reads the CPU ID from the specified file, which is expected to
 * be in the format of /proc/[tid]/stat. The CPU ID is the 39th field in the
 * file and indicates the CPU number the thread was last executed on.
 *
 * @param proc_tid_stat_file A file pointer to the /proc/[tid]/stat file.
 * @return The CPU ID the thread was last executed on, or -1 if an error occurs.
 */
int read_proc_cpuid(FILE *proc_tid_stat_file)
{
	if (unlikely(!proc_tid_stat_file))
		return -1;

	/* field 39 in the /proc/[tid]/stat file contains the CPU ID */
	const int cpu_field_idx = 39;
	int cpu_id = -1;
	char buffer[512];

	if (likely(fgets(buffer, sizeof(buffer), proc_tid_stat_file) != NULL)) {
		const char *stat_token;
		char *saveptr;

		stat_token = strtok_r(buffer, " ", &saveptr);

		/* Scan through to the 'cpu_field_idx' field */
		for (int i = 1; i < cpu_field_idx; i++) {
			if (unlikely(!stat_token))
				break;
			stat_token = strtok_r(NULL, " ", &saveptr);
		}

		/* The 'cpu_field_idx':th field contains the CPU number last executed on */
		if (likely(stat_token)) {
			char *endptr;
			long value = strtol(stat_token, &endptr, 10); /* Read CPU ID (39th field) */

			/* Check for conversion errors */
			if (endptr != stat_token && *endptr == '\0' &&
			    value >= 0 && value <= INT_MAX)
				cpu_id = (int)value;

			/* Conversion error or out-of-range value keeps cpu_id = -1 */
		}
	}

	return cpu_id;
}
