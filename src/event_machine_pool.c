/*
 *   Copyright (c) 2018-2023, Nokia Solutions and Networks
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

void em_pool_cfg_init(em_pool_cfg_t *const pool_cfg)
{
	odp_pool_param_t odp_pool_defaults;
	uint32_t buf_cache_sz;
	uint32_t pkt_cache_sz;
	uint32_t vec_cache_sz;
	uint32_t cache_sz;

	if (unlikely(!pool_cfg)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_ARG),
			       EM_ESCOPE_POOL_CFG_INIT,
			       "pool_cfg pointer NULL!");
		return;
	}

	odp_pool_param_init(&odp_pool_defaults);
	memset(pool_cfg, 0, sizeof(*pool_cfg));

	pool_cfg->event_type = EM_EVENT_TYPE_UNDEF;

	buf_cache_sz = odp_pool_defaults.buf.cache_size;
	pkt_cache_sz = odp_pool_defaults.pkt.cache_size;
	vec_cache_sz = odp_pool_defaults.vector.cache_size;
	cache_sz = MIN(buf_cache_sz, pkt_cache_sz);
	cache_sz = MIN(cache_sz, vec_cache_sz);

	for (int i = 0; i < EM_MAX_SUBPOOLS; i++)
		pool_cfg->subpool[i].cache_size = cache_sz;

	pool_cfg->__internal_check = EM_CHECK_INIT_CALLED;
}

em_pool_t
em_pool_create(const char *name, em_pool_t pool, const em_pool_cfg_t *pool_cfg)
{
	em_pool_t pool_created;
	const char *err_str = "";

	/* Verify config */
	int err = invalid_pool_cfg(pool_cfg, &err_str);

	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_CREATE,
			       "Pool create: invalid pool-config(%d): %s",
			       err, err_str);
		return EM_POOL_UNDEF;
	}

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
em_pool_get_name(em_pool_t pool, char *name /*out*/, size_t maxlen)
{
	const mpool_elem_t *mpool_elem = pool_elem_get(pool);
	size_t len = 0;

	if (unlikely(name == NULL || maxlen == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_GET_NAME,
			       "Invalid args: name=0x%" PRIx64 ", maxlen=%zu",
			       name, maxlen);
		return 0;
	}

	if (unlikely(mpool_elem == NULL || !pool_allocated(mpool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_GET_NAME,
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
	const mpool_elem_t *const mpool_elem_tbl = em_shm->mpool_tbl.pool;
	const mpool_elem_t *mpool_elem = &mpool_elem_tbl[0];
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

	const mpool_elem_t *const mpool_elem_tbl = em_shm->mpool_tbl.pool;
	const mpool_elem_t *mpool_elem = &mpool_elem_tbl[_pool_tbl_iter_idx];

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
em_pool_info(em_pool_t pool, em_pool_info_t *pool_info /*out*/)
{
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(!pool_elem || !pool_info,
				EM_ERR_BAD_ARG, EM_ESCOPE_POOL_INFO,
				"Inv. args: pool:%" PRI_POOL " pool_info:%p",
				pool, pool_info);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!pool_allocated(pool_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_INFO,
				"EM-pool:%" PRI_POOL " not created", pool);

	memset(pool_info, 0, sizeof(*pool_info));
	/* copy pool info into the user provided 'pool_info' */
	strncpy(pool_info->name, pool_elem->name, sizeof(pool_info->name));
	pool_info->name[sizeof(pool_info->name) - 1] = '\0';
	pool_info->em_pool = pool_elem->em_pool;
	pool_info->event_type = pool_elem->event_type;
	pool_info->align_offset = pool_elem->align_offset;
	pool_info->user_area_size = pool_elem->user_area.size;
	pool_info->num_subpools = pool_elem->num_subpools;

	for (int i = 0; i < pool_elem->num_subpools; i++) {
		pool_info->subpool[i].size = pool_elem->size[i]; /*sorted sz*/
		pool_info->subpool[i].num = pool_elem->pool_cfg.subpool[i].num;
		pool_info->subpool[i].cache_size = pool_elem->pool_cfg.subpool[i].cache_size;
	}

	/*
	 * EM pool usage statistics only collected if the pool was created with
	 * 'available' or 'cache_available' statistics enabled either through
	 * EM config file: 'pool.statistics' or in 'em_pool_cfg_t::stats_opt'
	 * given to function em_pool_create(..., pool_cfg).
	 */
	if (!pool_elem->stats_opt.bit.available &&
	    !pool_elem->stats_opt.bit.cache_available)
		return EM_OK; /* no statistics, return */

	/* EM pool usage statistics _enabled_ - collect it: */
	for (int i = 0; i < pool_elem->num_subpools; i++) {
		const uint64_t num = pool_elem->pool_cfg.subpool[i].num;
		uint64_t used = 0;
		uint64_t free = 0;
		odp_pool_stats_t odp_stats;

		/* avoid LTO-error: 'odp_stats.thread.first/last' may be used uninitialized */
		odp_stats.thread.first = 0;
		odp_stats.thread.last = 0;

		int ret = odp_pool_stats(pool_elem->odp_pool[i], &odp_stats);

		RETURN_ERROR_IF(ret, EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_INFO,
				"EM-pool:%" PRI_POOL " subpool:%d stats failed:%d",
				pool, i, ret);
		/* ODP inactive counters are zero, it is safe to add both: */
		free = odp_stats.available + odp_stats.cache_available;
		if (free > num)
			free = num;
		used = num - free;

		pool_info->subpool[i].used = used;
		pool_info->subpool[i].free = free;
	}

	return EM_OK;
}

void
em_pool_info_print(em_pool_t pool)
{
	pool_info_print_hdr(1);
	pool_info_print(pool);
}

int em_pool_get_num_subpools(em_pool_t pool)
{
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0 && unlikely(!pool_elem)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_NUM_SUBPOOLS,
			       "Inv. args: pool:%" PRI_POOL, pool);
		return -1;
	}

	if (EM_CHECK_LEVEL >= 2 && unlikely(!pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_NUM_SUBPOOLS,
			       "EM-pool:%" PRI_POOL " not created", pool);
		return -1;
	}

	return pool_elem->num_subpools;
}

void
em_pool_info_print_all(void)
{
	em_pool_t pool;
	unsigned int num;

	pool = em_pool_get_first(&num);

	pool_info_print_hdr(num);
	while (pool != EM_POOL_UNDEF) {
		pool_info_print(pool);
		pool = em_pool_get_next();
	}
}

em_status_t em_pool_stats(em_pool_t pool, em_pool_stats_t *pool_stats/*out*/)
{
	int i;
	int ret;
	odp_pool_stats_t *odp_stats;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(!pool_elem || !pool_stats,
				EM_ERR_BAD_ARG, EM_ESCOPE_POOL_STATS,
				"Inv. args: pool:%" PRI_POOL " pool_stats:%p",
				pool, pool_stats);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!pool_allocated(pool_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_STATS,
				"EM-pool: %" PRI_POOL "not created", pool);

	i = 0;
	for (; i < pool_elem->num_subpools; i++) {
		odp_stats = (odp_pool_stats_t *)&pool_stats->subpool_stats[i];

		/* avoid LTO-error: 'odp_stats.thread.first/last' may be used uninitialized */
		odp_stats->thread.first = 0;
		odp_stats->thread.last = 0;

		ret = odp_pool_stats(pool_elem->odp_pool[i], odp_stats);

		RETURN_ERROR_IF(ret, EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_STATS,
				"EM-pool:%" PRI_POOL " subpool:%d stats failed:%d",
				pool, i, ret);
	}

	pool_stats->num_subpools = i;

	return EM_OK;
}

em_status_t em_pool_stats_reset(em_pool_t pool)
{
	int ret;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(pool_elem == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_POOL_STATS_RESET,
				"EM-pool:%" PRI_POOL " invalid", pool);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!pool_allocated(pool_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_STATS_RESET,
				"EM-pool:%" PRI_POOL " not created", pool);

	for (int i = 0; i < pool_elem->num_subpools; i++) {
		ret = odp_pool_stats_reset(pool_elem->odp_pool[i]);
		RETURN_ERROR_IF(ret, EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_STATS_RESET,
				"EM-pool:%" PRI_POOL " subpool:%d stats reset failed:%d",
				pool);
	}

	return EM_OK;
}

void em_pool_stats_print(em_pool_t pool)
{
	pool_stats_print(pool);
}

#define SUBPOOL_STATS_INV_ARG_FMT \
"Inv. args: pool:%" PRI_POOL " subpools:%p num_subpools:%d subpool_stats:%p"

int
em_pool_subpool_stats(em_pool_t pool, const int subpools[], int num_subpools,
		      em_pool_subpool_stats_t subpool_stats[]/*out*/)
{
	int ret;
	int num_stats = 0;
	odp_pool_stats_t *odp_stats;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!pool_elem || !subpools || !subpool_stats ||
		     num_subpools <= 0 || num_subpools > pool_elem->num_subpools)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_SUBPOOL_STATS,
			       SUBPOOL_STATS_INV_ARG_FMT, pool, subpools,
			       num_subpools, subpool_stats);
		return 0;
	}

	if (EM_CHECK_LEVEL >= 2 && unlikely(!pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_SUBPOOL_STATS,
			       "EM-pool: %" PRI_POOL "not allocated", pool);
		return 0;
	}

	for (int i = 0; i < num_subpools; i++) {
		if (EM_CHECK_LEVEL > 0 &&
		    unlikely(subpools[i] > pool_elem->num_subpools - 1)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_SUBPOOL_STATS,
				       "arg 'subpools[%d]: %d' out of range", i, subpools[i]);
			return num_stats;
		}

		odp_stats = (odp_pool_stats_t *)&subpool_stats[i];

		/* avoid LTO-error: 'odp_stats.thread.first/last' may be used uninitialized */
		odp_stats->thread.first = 0;
		odp_stats->thread.last = 0;

		ret = odp_pool_stats(pool_elem->odp_pool[subpools[i]], odp_stats);
		if (unlikely(ret < 0)) {
			INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_SUBPOOL_STATS,
				       "EM-pool:%" PRI_POOL " subpool:%d stats failed:%d",
				       pool, subpools[i], ret);
			return num_stats;
		}
		num_stats++;
	}

	return num_stats;
}

em_status_t
em_pool_subpool_stats_reset(em_pool_t pool, const int subpools[], int num_subpools)
{
	int ret;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(!pool_elem || !subpools || num_subpools <= 0 ||
				num_subpools > pool_elem->num_subpools,
				EM_ERR_BAD_ARG, EM_ESCOPE_POOL_SUBPOOL_STATS_RESET,
				"Inv. args: pool:%" PRI_POOL " subpools:%p num:%d",
				pool, subpools, num_subpools);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!pool_allocated(pool_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_SUBPOOL_STATS_RESET,
				"EM-pool:%" PRI_POOL " not created", pool);

	for (int i = 0; i < num_subpools; i++) {
		RETURN_ERROR_IF(subpools[i] > pool_elem->num_subpools - 1,
				EM_ERR_BAD_ARG, EM_ESCOPE_POOL_SUBPOOL_STATS_RESET,
				"arg 'subpools[%d]: %d' out of range", i, subpools[i]);

		ret = odp_pool_stats_reset(pool_elem->odp_pool[subpools[i]]);
		RETURN_ERROR_IF(ret, EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_SUBPOOL_STATS_RESET,
				"EM-pool:%" PRI_POOL " subpool:%d stats reset failed:%d",
				pool, subpools[i], ret);
	}

	return EM_OK;
}

void em_pool_subpool_stats_print(em_pool_t pool, const int subpools[], int num_subpools)
{
	subpools_stats_print(pool, subpools, num_subpools);
}

static inline void assign_odp_stats_opt(odp_pool_stats_opt_t *opt_odp/*out*/,
					const em_pool_stats_opt_t *opt_em)
{
	opt_odp->all = 0;
	opt_odp->bit.available = opt_em->available;
	opt_odp->bit.alloc_ops = opt_em->alloc_ops;
	opt_odp->bit.alloc_fails = opt_em->alloc_fails;
	opt_odp->bit.free_ops = opt_em->free_ops;
	opt_odp->bit.total_ops = opt_em->total_ops;
	opt_odp->bit.cache_available = opt_em->cache_available;
	opt_odp->bit.cache_alloc_ops = opt_em->cache_alloc_ops;
	opt_odp->bit.cache_free_ops = opt_em->cache_free_ops;
}

em_status_t
em_pool_stats_selected(em_pool_t pool, em_pool_stats_selected_t *pool_stats/*out*/,
		       const em_pool_stats_opt_t *opt)
{
	int i;
	int ret;
	odp_pool_stats_opt_t opt_odp;
	odp_pool_stats_selected_t *odp_stats;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(!pool_elem || !pool_stats || !opt,
				EM_ERR_BAD_ARG, EM_ESCOPE_POOL_STATS_SELECTED,
				"Inv. args: pool:%" PRI_POOL " pool_stats:%p opt: %p",
				pool, pool_stats, opt);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!pool_allocated(pool_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_STATS_SELECTED,
				"EM-pool: %" PRI_POOL "not created", pool);

	assign_odp_stats_opt(&opt_odp, opt);

	for (i = 0; i < pool_elem->num_subpools; i++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[i];

		if (EM_CHECK_LEVEL >= 3)
			RETURN_ERROR_IF(odp_pool == ODP_POOL_INVALID,
					EM_ERR_BAD_ID, EM_ESCOPE_POOL_STATS_SELECTED,
					"EM-pool:%" PRI_POOL " invalid subpool:%d",
					pool, i);

		odp_stats = (odp_pool_stats_selected_t *)&pool_stats->subpool_stats[i];

		ret = odp_pool_stats_selected(odp_pool, odp_stats, &opt_odp);

		RETURN_ERROR_IF(ret, EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_STATS_SELECTED,
				"EM-pool:%" PRI_POOL " subpool:%d stats selected failed:%d",
				pool, i, ret);
	}

	pool_stats->num_subpools = i;

	return EM_OK;
}

void em_pool_stats_selected_print(em_pool_t pool, const em_pool_stats_opt_t *opt)
{
	pool_stats_selected_print(pool, opt);
}

#define SUBPOOL_STATS_SELECTED_INV_ARG_FMT \
"Inv. args: pool:%" PRI_POOL " subpools:%p num_subpools:%d subpool_stats:%p opt: %p"

int em_pool_subpool_stats_selected(em_pool_t pool, const int subpools[], int num_subpools,
				   em_pool_subpool_stats_selected_t subpool_stats[]/*out*/,
				   const em_pool_stats_opt_t *opt)
{
	int ret;
	int num_stats = 0;
	odp_pool_stats_opt_t opt_odp;
	odp_pool_stats_selected_t *odp_stats;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!pool_elem || !subpools || !subpool_stats || !opt ||
		     num_subpools <= 0 || num_subpools > pool_elem->num_subpools)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED,
			       SUBPOOL_STATS_SELECTED_INV_ARG_FMT, pool, subpools,
			       num_subpools, subpool_stats, opt);
		return 0;
	}

	if (EM_CHECK_LEVEL >= 2 && unlikely(!pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED,
			       "EM-pool: %" PRI_POOL "not allocated", pool);
		return 0;
	}

	assign_odp_stats_opt(&opt_odp, opt);

	for (int i = 0; i < num_subpools; i++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpools[i]];

		if (EM_CHECK_LEVEL > 0 &&
		    unlikely(subpools[i] > pool_elem->num_subpools - 1)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED,
				       "arg 'subpools[%d]: %d' out of range", i, subpools[i]);
			return num_stats;
		}

		if (EM_CHECK_LEVEL >= 3 && unlikely(odp_pool == ODP_POOL_INVALID)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED,
				       "EM-pool:%" PRI_POOL " invalid subpool:%d", pool, i);
			return num_stats;
		}

		odp_stats = (odp_pool_stats_selected_t *)&subpool_stats[i];

		ret = odp_pool_stats_selected(odp_pool, odp_stats, &opt_odp);
		if (unlikely(ret < 0)) {
			INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED,
				       "EM-pool:%" PRI_POOL " subpool:%d stats failed:%d",
				       pool, subpools[i], ret);
			return num_stats;
		}
		num_stats++;
	}

	return num_stats;
}

void em_pool_subpool_stats_selected_print(em_pool_t pool, const int subpools[],
					  int num_subpools,
					  const em_pool_stats_opt_t *opt)
{
	subpools_stats_selected_print(pool, subpools, num_subpools, opt);
}

uint64_t em_pool_to_u64(em_pool_t pool)
{
	return (uint64_t)pool;
}
