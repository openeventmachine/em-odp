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

em_status_t
pool_init(mpool_tbl_t *const mpool_tbl, mpool_pool_t *const mpool_pool,
	  em_pool_cfg_t *const default_pool_cfg);

em_status_t
pool_term(mpool_tbl_t *const pool_tbl);

em_pool_t
pool_create(const char *name, em_pool_t pool, em_pool_cfg_t *const pool_cfg);

em_status_t
pool_delete(em_pool_t pool);

em_pool_t
pool_find(const char *name);

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
pool_allocated(mpool_elem_t *const mpool_elem)
{
	return !objpool_in_pool(&mpool_elem->objpool_elem);
}

static inline int
pool_find_subpool(mpool_elem_t *const pool_elem, size_t size)
{
	int subpool = 0;

	if (EM_MAX_SUBPOOLS > 1) { /* Compile time option */
		int i;
		/* Find the optimal subpool to allocate the event from */
		for (i = 0; i < pool_elem->num_subpools &&
		     size > pool_elem->size[i]; i++)
			;

		if (unlikely(i >= pool_elem->num_subpools))
			return -1;

		subpool = i;
	}

	return subpool;
}

unsigned int
pool_count(void);

static inline void
pool_stat_increment(em_pool_t pool, int subpool)
{
	const int pool_idx = pool_hdl2idx(pool);
	mpool_statistics_t *const pstat =
		&em_shm->mpool_tbl.pool_stat_core[em_core_id()];

	pstat->stat[pool_idx][subpool].alloc++;
}

static inline void
pool_stat_decrement(em_pool_t pool, int subpool)
{
	const int pool_idx = pool_hdl2idx(pool);
	mpool_statistics_t *const pstat =
		&em_shm->mpool_tbl.pool_stat_core[em_core_id()];

	pstat->stat[pool_idx][subpool].free++;
}

void
pool_info_print(em_pool_t pool);

#ifdef __cplusplus
}
#endif

#endif /* EM_POOL_H_ */
