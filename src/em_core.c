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

em_status_t
core_map_init(core_map_t *const core_map, int core_count,
	      const em_core_mask_t *phys_mask)
{
	int phys_id = 0;
	int logic_id = 0;

	if (core_count > EM_MAX_CORES || core_count > odp_thread_count_max())
		return EM_ERR_TOO_LARGE;

	memset(core_map, 0, sizeof(core_map_t));

	env_spinlock_init(&core_map->lock);
	core_map->count = core_count;

	em_core_mask_copy(&core_map->phys_mask, phys_mask);
	em_core_mask_set_count(core_count, &core_map->logic_mask);

	while (logic_id < core_count && phys_id < EM_MAX_CORES) {
		if (em_core_mask_isset(phys_id, &core_map->phys_mask)) {
			core_map->phys_vs_logic.logic[phys_id] = logic_id;
			core_map->phys_vs_logic.phys[logic_id] = phys_id;
			logic_id++;
		}
		phys_id++;
	}

	return EM_OK;
}

em_status_t
core_map_init_local(core_map_t *const core_map)
{
	int em_core = em_core_id();
	int odp_thr = odp_thread_id();

	if (odp_thr >= EM_MAX_CORES)
		return EM_ERR_TOO_LARGE;

	env_spinlock_lock(&core_map->lock);
	core_map->thr_vs_logic.logic[odp_thr] = em_core;
	core_map->thr_vs_logic.odp_thr[em_core] = odp_thr;
	env_spinlock_unlock(&core_map->lock);

	return EM_OK;
}

int
logic_to_thr_core_id(const int logic_core)
{
	if (unlikely(logic_core >= EM_MAX_CORES))
		return -1;

	return em_shm->core_map.thr_vs_logic.odp_thr[logic_core];
}

int
thr_to_logic_core_id(const int thr_id)
{
	if (unlikely(thr_id >= EM_MAX_CORES))
		return -1;

	return em_shm->core_map.thr_vs_logic.logic[thr_id];
}

void
mask_em2odp(const em_core_mask_t *const em_core_mask,
	    odp_thrmask_t *const odp_thrmask /*out*/)
{
	int core_count = em_core_count();
	int odp_thread_id;
	int i;

	odp_thrmask_zero(odp_thrmask);

	/* EM cores are consequtive 0 -> em_core_count()-1 */
	for (i = 0; i < core_count; i++) {
		if (em_core_mask_isset(i, em_core_mask)) {
			odp_thread_id = logic_to_thr_core_id(i);
			odp_thrmask_set(odp_thrmask, odp_thread_id);
		}
	}
}
