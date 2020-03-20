/*
 *   Copyright (c) 2018, Nokia Solutions and Networks
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
 *
 * Event Machine event pool functions.
 *
 */

#include "em_include.h"

/* per core (thread) state for em_atomic_group_get_next() */
static ENV_LOCAL unsigned int _pool_tbl_iter_idx;

em_pool_t
em_pool_create(const char *name, em_pool_t pool, em_pool_cfg_t *const pool_cfg)
{
	em_pool_t pool_created;

	pool_created = pool_create(name, pool, pool_cfg);

	if (unlikely(pool_created == EM_POOL_UNDEF ||
		     (pool != EM_POOL_UNDEF && pool != pool_created))) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_CREATE,
			       "Pool create failed,\n"
			       "requested:%" PRI_POOL " created:%" PRI_POOL "",
			       pool, pool_created);
		return EM_POOL_UNDEF;
	}

	return pool_created;
}

em_status_t
em_pool_delete(em_pool_t pool)
{
	em_status_t stat;

	stat = pool_delete(pool);
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_POOL_DELETE,
			"Pool delete failed");

	return EM_OK;
}

em_pool_t
em_pool_find(const char *name)
{
	if (name && *name)
		return pool_find(name);

	return EM_POOL_UNDEF;
}

size_t
em_pool_get_name(em_pool_t pool, char *name, size_t maxlen)
{
	mpool_elem_t *const mpool_elem = pool_elem_get(pool);
	size_t len = 0;

	if (unlikely(name == NULL || maxlen == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_POOL_GET_NAME,
			       "Invalid args: name=0x%" PRIx64 ", maxlen=%zu",
			       name, maxlen);
		return 0;
	}

	if (unlikely(mpool_elem == NULL || !pool_allocated(mpool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_POOL_GET_NAME,
			       "Invalid Pool:%" PRI_POOL "", pool);
		name[0] = '\0';
		return 0;
	}

	len = strnlen(mpool_elem->name, sizeof(mpool_elem->name) - 1);
	if (maxlen - 1 < len)
		len = maxlen - 1;

	memcpy(name, mpool_elem->name, len);
	name[len] = '\0';

	return len;
}

em_pool_t
em_pool_get_first(unsigned int *num)
{
	mpool_elem_t *const mpool_elem_tbl = em_shm->mpool_tbl.pool;
	mpool_elem_t *mpool_elem = &mpool_elem_tbl[0];
	const unsigned int pool_cnt = pool_count();

	_pool_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = pool_cnt;

	if (pool_cnt == 0) {
		_pool_tbl_iter_idx = EM_CONFIG_POOLS; /* UNDEF = _get_next() */
		return EM_POOL_UNDEF;
	}

	/* find first */
	while (!pool_allocated(mpool_elem)) {
		_pool_tbl_iter_idx++;
		if (_pool_tbl_iter_idx >= EM_CONFIG_POOLS)
			return EM_POOL_UNDEF;
		mpool_elem = &mpool_elem_tbl[_pool_tbl_iter_idx];
	}

	return pool_idx2hdl(_pool_tbl_iter_idx);
}

em_pool_t
em_pool_get_next(void)
{
	if (_pool_tbl_iter_idx >= EM_CONFIG_POOLS - 1)
		return EM_POOL_UNDEF;

	_pool_tbl_iter_idx++;

	mpool_elem_t *const mpool_elem_tbl = em_shm->mpool_tbl.pool;
	mpool_elem_t *mpool_elem = &mpool_elem_tbl[_pool_tbl_iter_idx];

	/* find next */
	while (!pool_allocated(mpool_elem)) {
		_pool_tbl_iter_idx++;
		if (_pool_tbl_iter_idx >= EM_CONFIG_POOLS)
			return EM_POOL_UNDEF;
		mpool_elem = &mpool_elem_tbl[_pool_tbl_iter_idx];
	}

	return pool_idx2hdl(_pool_tbl_iter_idx);
}

em_status_t
em_pool_info(em_pool_t pool, em_pool_info_t *const pool_info /*out*/)
{
	mpool_elem_t *pool_elem;

	RETURN_ERROR_IF(pool_info == NULL,
			EM_ERR_BAD_POINTER, EM_ESCOPE_POOL_INFO,
			"arg 'pool_info' invalid");

	pool_elem = pool_elem_get(pool);
	RETURN_ERROR_IF(pool_elem == NULL || !pool_allocated(pool_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_POOL_INFO,
			"EM-pool:%" PRI_POOL " invalid", pool);

	memset(pool_info, 0, sizeof(*pool_info));
	/* copy pool info into the user provided 'pool_info' */
	strncpy(pool_info->name, pool_elem->name, sizeof(pool_info->name));
	pool_info->name[sizeof(pool_info->name) - 1] = '\0';
	pool_info->em_pool = pool_elem->em_pool;
	pool_info->event_type = pool_elem->event_type;
	pool_info->num_subpools = pool_elem->num_subpools;
	for (int i = 0; i < pool_elem->num_subpools; i++) {
		uint32_t num = pool_elem->pool_cfg.subpool[i].num;

		pool_info->subpool[i].size = pool_elem->size[i]; /*sorted sz*/
		pool_info->subpool[i].num = num;
		/*
		 * EM pool usage statistics only collected if
		 * EM config file: pool.statistics_enable=true.
		 */
		if (em_shm->opt.pool.statistics_enable) {
			int cores = em_core_count();
			int pool_idx = pool_hdl2idx(pool);
			mpool_statistics_t *pstat_core =
				em_shm->mpool_tbl.pool_stat_core;
			uint64_t alloc = 0, free = 0, used;

			for (int j = 0; j < cores; j++) {
				alloc += pstat_core[j].stat[pool_idx][i].alloc;
				free += pstat_core[j].stat[pool_idx][i].free;
			}
			if (likely(alloc >= free))
				used = alloc - free;
			else /* wrap, should not happen very soon... */
				used = UINT64_MAX - free + alloc + 1;

			pool_info->subpool[i].used = used;
			pool_info->subpool[i].free = num - used;
		}
	}

	return EM_OK;
}

#define POOL_INFO_HDR_STR \
"  id     name             type        sizes                 (used/free)\n"

void
em_pool_info_print(em_pool_t pool)
{
	EM_PRINT("EM Event Pool\n"
		 "-------------\n"
		 POOL_INFO_HDR_STR);
	pool_info_print(pool);
}

void
em_pool_info_print_all(void)
{
	em_pool_t pool;
	unsigned int num;

	pool = em_pool_get_first(&num);

	EM_PRINT("EM Event Pools:%2u\n"
		 "-----------------\n"
		 POOL_INFO_HDR_STR, num);

	while (pool != EM_POOL_UNDEF) {
		pool_info_print(pool);
		pool = em_pool_get_next();
	}
}
