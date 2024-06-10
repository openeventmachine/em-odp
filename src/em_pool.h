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

#ifndef EM_POOL_H_
#define EM_POOL_H_

/**
 * @file
 * EM internal event pool functions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#define valid_pool(pool)   ((unsigned int)pool_hdl2idx((pool)) < \
			    EM_CONFIG_POOLS)
#define invalid_pool(pool) ((unsigned int)pool_hdl2idx((pool)) > \
			    EM_CONFIG_POOLS - 1)

int invalid_pool_cfg(const em_pool_cfg_t *pool_cfg,
		     const char **err_str/*out*/);

int check_pool_uarea_persistence(const em_pool_cfg_t *pool_cfg,
				 const char **err_str/*out*/);

em_status_t
pool_init(mpool_tbl_t *const mpool_tbl, mpool_pool_t *const mpool_pool,
	  const em_pool_cfg_t *default_pool_cfg);

em_status_t
pool_term(const mpool_tbl_t *pool_tbl);

em_pool_t
pool_create(const char *name, em_pool_t req_pool, const em_pool_cfg_t *pool_cfg);

em_status_t
pool_delete(em_pool_t pool);

em_pool_t
pool_find(const char *name);

void pool_info_print_hdr(unsigned int num_pools);
void pool_info_print(em_pool_t pool);

void pool_stats_print(em_pool_t pool);

void subpools_stats_print(em_pool_t pool, const int subpools[], int num_subpools);
void pool_stats_selected_print(em_pool_t pool, const em_pool_stats_opt_t *opt);
void subpools_stats_selected_print(em_pool_t pool, const int subpools[],
				   int num_subpools, const em_pool_stats_opt_t *opt);

void print_pool_elem_info(void);

/** Convert pool handle to pool index */
static inline int
pool_hdl2idx(em_pool_t pool)
{
	return (int)(uintptr_t)pool - 1;
}

/** Convert pool index to pool handle */
static inline em_pool_t
pool_idx2hdl(int pool_idx)
{
	return (em_pool_t)(uintptr_t)(pool_idx + 1);
}

/** Returns pool element associated with pool handle */
static inline mpool_elem_t *
pool_elem_get(em_pool_t pool)
{
	const int pool_idx = pool_hdl2idx(pool);
	mpool_elem_t *mpool_elem;

	if (unlikely((unsigned int)pool_idx > EM_CONFIG_POOLS - 1))
		return NULL;

	mpool_elem = &em_shm->mpool_tbl.pool[pool_idx];

	return mpool_elem;
}

static inline int
pool_allocated(const mpool_elem_t *const mpool_elem)
{
	return !objpool_in_pool(&mpool_elem->objpool_elem);
}

static inline int
pool_find_subpool(const mpool_elem_t *const pool_elem, uint32_t size)
{
	int subpool;

	/* Find the optimal subpool to allocate the event from */
	for (subpool = 0; subpool < pool_elem->num_subpools &&
	     size > pool_elem->size[subpool]; subpool++)
		;

	if (unlikely(subpool >= pool_elem->num_subpools))
		return -1;

	return subpool;
}

unsigned int
pool_count(void);

/**
 * Get the EM event-pool that an odp-pool belongs to.
 *
 * An EM event-pool consists of up to EM_MAX_SUBPOOLS subpools (that are
 * odp-pools) - a table (em_shm->mpool_tbl.pool_subpool_odp2em[]) contains the
 * mapping and is populated during em_pool_create() calls.
 */
static inline em_pool_t
pool_odp2em(odp_pool_t odp_pool)
{
	/*
	 * 'idx' is in the range: 0 to odp_pool_max_index(), which is smaller
	 * than the length of the em_shm->mpool_tbl.pool_subpool_odp2em[] array
	 * (verified at startup in pool_init()).
	 */
	int idx = odp_pool_index(odp_pool);

	if (unlikely(idx < 0))
		return EM_POOL_UNDEF;

	return (em_pool_t)(uintptr_t)em_shm->mpool_tbl.pool_subpool_odp2em[idx].pool;
}

/**
 * Get the EM event-pool and subpool that an odp-pool belongs to.
 *
 * An EM event-pool consists of up to EM_MAX_SUBPOOLS subpools (that are
 * odp-pools) - a table (em_shm->mpool_tbl.pool_subpool_odp2em[]) contains the
 * mapping and is populated during em_pool_create() calls.
 */
static inline pool_subpool_t
pool_subpool_odp2em(odp_pool_t odp_pool)
{
	/*
	 * 'idx' is in the range: 0 to odp_pool_max_index(), which is smaller
	 * than the length of the em_shm->mpool_tbl.pool_subpool_odp2em[] array
	 * (verified at startup in pool_init()).
	 */
	int idx = odp_pool_index(odp_pool);

	if (unlikely(idx < 0))
		return pool_subpool_undef; /* .pool=EM_POOL_UNDEF, .subpool=0 */

	return em_shm->mpool_tbl.pool_subpool_odp2em[idx];
}

#ifdef __cplusplus
}
#endif

#endif /* EM_POOL_H_ */
