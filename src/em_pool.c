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

COMPILE_TIME_ASSERT(EM_POOL_DEFAULT > (em_pool_t)0 &&
		    EM_POOL_DEFAULT < (em_pool_t)EM_CONFIG_POOLS,
		    EM_ODP_EM_DEFAULT_POOL_ERROR);
COMPILE_TIME_ASSERT(EM_POOL_UNDEF != EM_POOL_DEFAULT,
		    EM_ODP_EM_POOL_UNDEF_ERROR);

static inline mpool_elem_t *
mpool_poolelem2pool(objpool_elem_t *const objpool_elem)
{
	return (mpool_elem_t *)((uintptr_t)objpool_elem -
				offsetof(mpool_elem_t, objpool_elem));
}

em_status_t
pool_init(mpool_tbl_t *const mpool_tbl, mpool_pool_t *const mpool_pool,
	  em_pool_cfg_t *const default_pool_cfg)
{
	em_pool_t pool;
	int i, j, ret;
	const int cores = em_core_count();

	memset(mpool_tbl, 0, sizeof(mpool_tbl_t));
	memset(mpool_pool, 0, sizeof(mpool_pool_t));
	env_atomic32_init(&em_shm->pool_count);

	ret = objpool_init(&mpool_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_OPERATION_FAILED;

	for (i = 0; i < EM_CONFIG_POOLS; i++) {
		em_pool_t pool = pool_idx2hdl(i);
		mpool_elem_t *mpool_elem = pool_elem_get(pool);

		mpool_elem->em_pool = pool;
		mpool_elem->event_type = EM_EVENT_TYPE_UNDEF;
		for (j = 0; j < EM_MAX_SUBPOOLS; j++) {
			mpool_elem->odp_pool[j] = ODP_POOL_INVALID;
			mpool_elem->size[j] = 0;
		}

		objpool_add(&mpool_pool->objpool, i % cores,
			    &mpool_elem->objpool_elem);
	}

	/* Store common ODP pool capabilities in the mpool_tbl for easy access*/
	if (odp_pool_capability(&mpool_tbl->odp_pool_capability) != 0)
		return EM_ERR_LIB_FAILED;

	/* Create the 'EM_POOL_DEFAULT' pool */
	pool = pool_create(EM_POOL_DEFAULT_NAME, EM_POOL_DEFAULT,
			   default_pool_cfg);
	if (pool == EM_POOL_UNDEF || pool != EM_POOL_DEFAULT)
		return EM_ERR_ALLOC_FAILED;

	return EM_OK;
}

em_status_t
pool_term(mpool_tbl_t *const mpool_tbl)
{
	em_status_t stat = EM_OK;
	int i;

	(void)mpool_tbl;

	EM_PRINT("\n"
		 "Status before delete:\n");
	em_pool_info_print_all();

	for (i = 0; i < EM_CONFIG_POOLS; i++) {
		em_pool_t pool = pool_idx2hdl(i);
		mpool_elem_t *mpool_elem = pool_elem_get(pool);
		em_status_t ret;

		if (pool_allocated(mpool_elem)) {
			ret = pool_delete(pool);
			if (ret != EM_OK)
				stat = ret; /* save last error as return val */
		}
	}

	return stat;
}

static em_pool_t
pool_alloc(em_pool_t pool)
{
	mpool_elem_t *mpool_elem;

	if (pool == EM_POOL_UNDEF) {
		objpool_elem_t *objpool_elem =
			objpool_rem(&em_shm->mpool_pool.objpool, em_core_id());

		if (unlikely(objpool_elem == NULL))
			return EM_POOL_UNDEF;

		mpool_elem = mpool_poolelem2pool(objpool_elem);
	} else {
		int ret;

		mpool_elem = pool_elem_get(pool);
		if (unlikely(mpool_elem == NULL))
			return EM_POOL_UNDEF;

		ret = objpool_rem_elem(&em_shm->mpool_pool.objpool,
				       &mpool_elem->objpool_elem);
		if (unlikely(ret != 0))
			return EM_POOL_UNDEF;
	}

	env_atomic32_inc(&em_shm->pool_count);
	return mpool_elem->em_pool;
}

static em_status_t
pool_free(em_pool_t pool)
{
	mpool_elem_t *mpool_elem = pool_elem_get(pool);

	if (unlikely(mpool_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(&em_shm->mpool_pool.objpool,
		    mpool_elem->objpool_elem.subpool_idx,
		    &mpool_elem->objpool_elem);

	env_atomic32_dec(&em_shm->pool_count);
	return EM_OK;
}

static int
invalid_pool_cfg(em_pool_cfg_t *const pool_cfg)
{
	if (unlikely(pool_cfg == NULL ||
		     pool_cfg->num_subpools > EM_MAX_SUBPOOLS ||
		     (pool_cfg->event_type != EM_EVENT_TYPE_SW &&
		      pool_cfg->event_type != EM_EVENT_TYPE_PACKET)))
		return -1;

	return 0;
}

em_pool_t
pool_create(const char *name, em_pool_t pool, em_pool_cfg_t *const pool_cfg)
{
	em_pool_t allocated_pool;
	em_pool_cfg_t sorted_cfg;
	odp_pool_param_t pool_params;
	odp_pool_t odp_pool;
	char pool_name[ODP_POOL_NAME_LEN];
	int i, j, n;
	uint32_t size, num;
	mpool_elem_t *mpool_elem;

	/* Verify config */
	if (unlikely(invalid_pool_cfg(pool_cfg)))
		return EM_POOL_UNDEF;

	/* Allocate a free EM pool */
	allocated_pool = pool_alloc(pool /* requested pool or 'undef'*/);
	if (unlikely(allocated_pool == EM_POOL_UNDEF))
		return EM_POOL_UNDEF;

	pool = allocated_pool;
	mpool_elem = pool_elem_get(pool);

	/* Sanity check */
	if (mpool_elem->em_pool != allocated_pool)
		return EM_POOL_UNDEF;

	mpool_elem->event_type = pool_cfg->event_type;
	mpool_elem->num_subpools = pool_cfg->num_subpools;
	/* Store the event pool name, if given */
	if (name && *name) {
		strncpy(mpool_elem->name, name, sizeof(mpool_elem->name));
		mpool_elem->name[sizeof(mpool_elem->name) - 1] = '\0';
	} else {
		mpool_elem->name[0] = '\0';
	}

	sorted_cfg = *pool_cfg;
	/* Sort the subpools in ascending order based on the buffer size */
	n = sorted_cfg.num_subpools;
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (sorted_cfg.subpool[i].size >
			    sorted_cfg.subpool[j].size) {
				size = sorted_cfg.subpool[i].size;
				num = sorted_cfg.subpool[i].num;
				sorted_cfg.subpool[i] = sorted_cfg.subpool[j];
				sorted_cfg.subpool[j].size = size;
				sorted_cfg.subpool[j].num = num;
			}
		}
	}

	/* store the sorted config */
	mpool_elem->pool_cfg = sorted_cfg;

	int cores = em_core_count();
	int pool_idx = pool_hdl2idx(pool);
	mpool_statistics_t *pstat_core = em_shm->mpool_tbl.pool_stat_core;

	for (i = 0; i < cores; i++) {
		for (j = 0; j < EM_MAX_SUBPOOLS; j++) {
			pstat_core[i].stat[pool_idx][j].alloc = 0;
			pstat_core[i].stat[pool_idx][j].free = 0;
		}
	}

	for (i = 0; i < sorted_cfg.num_subpools; i++) {
		odp_pool_param_init(&pool_params);

		if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET) {
			pool_params.type = ODP_POOL_PACKET;
			/* num == max_num */
			pool_params.pkt.num = sorted_cfg.subpool[i].num;
			pool_params.pkt.max_num = sorted_cfg.subpool[i].num;
			/* len == max_len */
			pool_params.pkt.len = sorted_cfg.subpool[i].size;
			pool_params.pkt.max_len = sorted_cfg.subpool[i].size;
			pool_params.pkt.seg_len = sorted_cfg.subpool[i].size;
			/*
			 * Reserve space for the event header in each packet's
			 * user area:
			 */
			pool_params.pkt.uarea_size = sizeof(event_hdr_t);
		} else { /* pool_cfg->event_type == EM_EVENT_TYPE_SW */
			pool_params.type = ODP_POOL_BUFFER;
			pool_params.buf.num = sorted_cfg.subpool[i].num;
			pool_params.buf.size = sorted_cfg.subpool[i].size +
					       sizeof(event_hdr_t);
			pool_params.buf.align = ODP_CACHE_LINE_SIZE;
		}

		snprintf(pool_name, sizeof(pool_name),
			 "%" PRI_POOL ":%d-%s", pool, i, mpool_elem->name);
		pool_name[sizeof(pool_name) - 1] = '\0';

		odp_pool = odp_pool_create(pool_name, &pool_params);
		if (unlikely(odp_pool == ODP_POOL_INVALID)) {
			INTERNAL_ERROR(EM_FATAL(EM_ERR_ALLOC_FAILED),
				       EM_ESCOPE_POOL_CREATE,
				       "EM-subpool:\"%s\" create fails",
				       pool_name);
			return EM_POOL_UNDEF;
		}

		/*odp_pool_print(odp_pool);*/

		mpool_elem->odp_pool[i] = odp_pool;
		mpool_elem->size[i] = sorted_cfg.subpool[i].size;
	}

	return mpool_elem->em_pool;
}

em_status_t
pool_delete(em_pool_t pool)
{
	mpool_elem_t *const mpool_elem = pool_elem_get(pool);
	int i;

	if (unlikely(mpool_elem == NULL || !pool_allocated(mpool_elem)))
		return EM_ERR_BAD_ID;

	for (i = 0; i < mpool_elem->num_subpools; i++) {
		odp_pool_t odp_pool = mpool_elem->odp_pool[i];
		int ret;

		if (odp_pool == ODP_POOL_INVALID)
			return EM_ERR_NOT_FOUND;

		ret = odp_pool_destroy(odp_pool);
		if (unlikely(ret))
			return EM_ERR_LIB_FAILED;

		mpool_elem->odp_pool[i] = ODP_POOL_INVALID;
		mpool_elem->size[i] = 0;
	}

	mpool_elem->name[0] = '\0';
	mpool_elem->event_type = EM_EVENT_TYPE_UNDEF;
	mpool_elem->num_subpools = 0;

	return pool_free(pool);
}

em_pool_t
pool_find(const char *name)
{
	if (name && *name) {
		for (int i = 0; i < EM_CONFIG_POOLS; i++) {
			mpool_elem_t *mpool_elem = &em_shm->mpool_tbl.pool[i];

			if (pool_allocated(mpool_elem) &&
			    !strncmp(name, mpool_elem->name, EM_POOL_NAME_LEN))
				return mpool_elem->em_pool;
		}
	}

	return EM_POOL_UNDEF;
}

unsigned int
pool_count(void)
{
	return env_atomic32_get(&em_shm->pool_count);
}

#define SUBSTR_FMT \
"%d:[sz=%" PRIu32 " n=%" PRIu32 "(%" PRIu32 "/%" PRIu32 ")]"

void
pool_info_print(em_pool_t pool)
{
	em_pool_info_t pool_info;
	em_status_t stat;
	int i;

	stat = em_pool_info(pool, &pool_info/*out*/);
	if (unlikely(stat != EM_OK)) {
		EM_PRINT("  %-6" PRI_POOL " %-16s %-10s  %3s\n",
			 pool, "err:n/a", "n/a", "n/a");
		return;
	}

	EM_PRINT("  %-6" PRI_POOL " %-16s 0x%08x    %02d   ",
		 pool, pool_info.name, pool_info.event_type,
		 pool_info.num_subpools);

	for (i = 0; i < pool_info.num_subpools; i++) {
		char subpool_str[40];

		snprintf(subpool_str, sizeof(subpool_str),
			 SUBSTR_FMT, i,
			 pool_info.subpool[i].size,
			 pool_info.subpool[i].num,
			 pool_info.subpool[i].used,
			 pool_info.subpool[i].free);
		subpool_str[sizeof(subpool_str) - 1] = '\0';
		EM_PRINT(" %-40s", subpool_str);
	}

	EM_PRINT("\n");
}
