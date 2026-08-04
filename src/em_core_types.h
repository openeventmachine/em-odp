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

#ifndef EM_CORE_TYPES_H_
#define EM_CORE_TYPES_H_

/**
 * @file
 * EM internal core-related types & definitions
 *
 */

#include <stdint.h>
#include <sys/types.h> /* pid_t */

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM mapping from physical core ids <-> logical core ids
 */
typedef struct {
	/**
	 * Number of EM-cores currently running (em_init_local/core() called).
	 * Includes EM worker and control cores, but not external threads.
	 */
	odp_atomic_u32_t current_core_count;
	/** Number of EM worker cores currently running */
	odp_atomic_u32_t worker_core_count;
	/** Number of EM control cores currently running */
	odp_atomic_u32_t control_core_count;
	/** Number of EM external threads currently running */
	odp_atomic_u32_t external_thread_count;

	/** Number of ongoing TERM_THREAD_REQ operations per thread */
	odp_atomic_u32_t ongoing_term_reqs[ODP_THREAD_COUNT_MAX];

	/**
	 * Read-write lock for the 'rwlocked'-struct below.
	 *
	 * Multiple readers of core-map data are allowed at the same time,
	 * but only one writer. The only two scenarios where the lock is taken
	 * for writes are adding an EM-core via em_init_local/core() and
	 * removing an EM-core via em_term_local/core() - thus taking the lock
	 * for reads in control operations is not time crtitical since add/rem
	 * core operations can afford to wait.
	 *
	 * This lock is also used to block EM-core adds or rems when an internal
	 * control operation is under way that needs info about the cores
	 * available and sends control events to them - cores can not be allowed
	 * to be removed in the middle of this.
	 */
	odp_rwlock_t rwlock;
	struct {
		/*
		 * ODP-thread-id <-> EM-core mapping available for EM-cores
		 * of type EM_CORE_TYPE_WORKER and EM_CORE_TYPE_CONTROL.
		 */
		struct {
			/** From ODP thread ids to logical EM core ids */
			uint8_t logic[ODP_THREAD_COUNT_MAX];
			/** From logical EM core ids to ODP thread ids */
			uint16_t odp_thr[EM_MAX_CORES];
		} thr_vs_logic;

		/** Mask of logic EM-core ids (worker and control EM-cores) */
		em_core_mask_t logic_mask;
		/** Mask of logic worker EM-core ids */
		em_core_mask_t worker_mask;
		/** Mask of logic control EM-core ids */
		em_core_mask_t control_mask;

		/** Mask of ODP-thread ids of worker _and_ control EM-cores */
		odp_thrmask_t thrmask_emcores;
		/** Mask of ODP-thread ids of EM external threads */
		odp_thrmask_t thrmask_extthrs;
		/** Mask of ODP-thread ids of all threads */
		odp_thrmask_t thrmask_all;

		/** Thread ids of EM-cores (worker and control) */
		pid_t tids[EM_MAX_CORES];
		/** Thread ids of external EM threads/processes */
		pid_t ext_tids[ODP_THREAD_COUNT_MAX];

		/** Type of each EM-core: worker or ctrl (not external) */
		em_core_type_t core_types[EM_MAX_CORES];

		/**
		 * Store file paths to each EM-core's /proc/[tid]/stat file
		 * so that the paths don't have to be generated each time.
		 */
		char proc_tid_stat_paths[EM_MAX_CORES][32];
	} rwlocked;

	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} core_map_t;

COMPILE_TIME_ASSERT((sizeof(core_map_t) % ENV_CACHE_LINE_SIZE) == 0,
		    CORE_MAP_T__SIZE_ERROR);
COMPILE_TIME_ASSERT(EM_MAX_CORES - 1 <= UINT8_MAX,
		    CORE_MAP_T__TYPE_ERROR1);
COMPILE_TIME_ASSERT(ODP_THREAD_COUNT_MAX - 1 <= UINT16_MAX,
		    CORE_MAP_T__TYPE_ERROR2);

#ifdef __cplusplus
}
#endif

#endif /* EM_CORE_TYPES_H_ */
