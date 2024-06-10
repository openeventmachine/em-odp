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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM mapping from physical core ids <-> logical core ids
 */
typedef struct {
	/** core map lock */
	env_spinlock_t lock;

	/** Number of EM cores currently running (em_init_core() called) */
	odp_atomic_u32_t current_core_count;

	/** Number of EM cores */
	int count;
	struct {
		/** From physical core ids to logical EM core ids */
		uint8_t logic[EM_MAX_CORES];
		/** From logical EM core ids to physical core ids */
		uint8_t phys[EM_MAX_CORES];
	} phys_vs_logic;

	struct {
		/** From ODP thread ids to logical EM core ids */
		uint16_t logic[ODP_THREAD_COUNT_MAX];
		/** From logical EM core ids to ODP thread ids */
		uint8_t odp_thr[EM_MAX_CORES];
	} thr_vs_logic;

	/** Mask of logic core IDs */
	em_core_mask_t logic_mask;
	/** Mask of phys core IDs */
	em_core_mask_t phys_mask;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} core_map_t;

COMPILE_TIME_ASSERT((sizeof(core_map_t) % ENV_CACHE_LINE_SIZE) == 0,
		    CORE_MAP_T__SIZE_ERROR);
COMPILE_TIME_ASSERT(EM_MAX_CORES - 1 <= UINT8_MAX,
		    CORE_MAP_T__TYPE_ERROR1);
COMPILE_TIME_ASSERT(ODP_THREAD_COUNT_MAX - 1 <= UINT16_MAX,
		    CORE_MAP_T__TYPE_ERROR2);

/**
 * EM spinlock - Cache line sized & aligned
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE];

	struct {
		env_spinlock_t lock;
	};
} em_spinlock_t  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(em_spinlock_t) == ENV_CACHE_LINE_SIZE,
		    EM_STATIC_QUEUE_LOCK_T__SIZE_ERROR);

#ifdef __cplusplus
}
#endif

#endif /* EM_CORE_TYPES_H_ */
