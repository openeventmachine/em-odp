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

/**
 * @file
 * EM internal POOL types & definitions
 *
 */

#ifndef EM_POOL_TYPES_H_
#define EM_POOL_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM event/memory pool
 */
typedef struct {
	/** EM pool handle */
	em_pool_t em_pool;
	/** Event type of events allocated from the pool */
	em_event_type_t event_type;
	/** Subpool alignment requested from ODP */
	uint32_t align_offset;
	/** Number of subpools within one EM pool, max=EM_MAX_SUBPOOLS */
	int num_subpools;
	/** ODP (sub)pool buffer (event) payload sizes */
	uint32_t size[EM_MAX_SUBPOOLS];
	/** ODP buffer handles for the subpools  */
	odp_pool_t odp_pool[EM_MAX_SUBPOOLS];
	/** for linking free pool-entries together */
	objpool_elem_t objpool_elem;
	/** Pool Configuration given during create */
	em_pool_cfg_t pool_cfg;
	/* Pool name */
	char name[EM_POOL_NAME_LEN];
} mpool_elem_t;

typedef struct {
	struct {
		uint64_t alloc;
		uint64_t free;
	} stat[EM_CONFIG_POOLS][EM_MAX_SUBPOOLS];
} mpool_statistics_t ENV_CACHE_LINE_ALIGNED;

/**
 * @def POOL_ODP2EM_TBL_LEN
 * Length of the mpool_tbl_t::pool_odp2em[] array
 */
#define POOL_ODP2EM_TBL_LEN  256
/*
 * Verify at compile time that the mpool_tbl_t::pool_odp2em[] mapping table
 * is large enough.
 * Verified also at runtime that: POOL_ODP2EM_TBL_LEN > odp_pool_max_index()
 */
COMPILE_TIME_ASSERT(EM_CONFIG_POOLS * EM_MAX_SUBPOOLS <= POOL_ODP2EM_TBL_LEN,
		    "MPOOL_TBL_T__POOL_ODP2EM__LEN_ERR");

/**
 * EM pool element table
 */
typedef struct {
	/** event/memory pool elem table */
	mpool_elem_t pool[EM_CONFIG_POOLS];

	/**
	 * Mapping from odp_pool_index(odp_pool) to em-pool handle.
	 * Verified at runtime that: POOL_ODP2EM_TBL_LEN > odp_pool_max_index()
	 */
	em_pool_t pool_odp2em[POOL_ODP2EM_TBL_LEN] ENV_CACHE_LINE_ALIGNED;

	/** Pool usage statistics - updated per EM-core */
	mpool_statistics_t pool_stat_core[EM_MAX_CORES] ENV_CACHE_LINE_ALIGNED;

	/** ODP pool capabilities common for all pools */
	odp_pool_capability_t odp_pool_capability ENV_CACHE_LINE_ALIGNED;
} mpool_tbl_t;

/**
 * Pool of free mempools
 */
typedef struct {
	objpool_t objpool;
} mpool_pool_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_POOL_TYPES_H_ */
