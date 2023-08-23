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

#ifndef EM_CORE_H_
#define EM_CORE_H_

/**
 * @file
 * EM internal core-related functions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

em_status_t
core_map_init(core_map_t *const core_map, int core_count,
	      const em_core_mask_t *phys_mask);

em_status_t
core_map_init_local(core_map_t *const core_map);

static inline int
logic_to_phys_core_id(const int logic_core)
{
	if (unlikely(logic_core >= EM_MAX_CORES))
		return -1;

	return em_shm->core_map.phys_vs_logic.phys[logic_core];
}

static inline int
phys_to_logic_core_id(const int phys_core)
{
	if (unlikely(phys_core >= EM_MAX_CORES))
		return -1;

	return em_shm->core_map.phys_vs_logic.logic[phys_core];
}

int logic_to_thr_core_id(const int logic_core);

int thr_to_logic_core_id(const int thr_id);

void mask_em2odp(const em_core_mask_t *const em_core_mask,
		 odp_thrmask_t *const odp_thrmask /*out*/);

void mask_em2phys(const em_core_mask_t *const em_core_mask,
		  odp_cpumask_t *const odp_cpumask /*out*/);

#ifdef __cplusplus
}
#endif

#endif /* EM_CORE_H_ */
