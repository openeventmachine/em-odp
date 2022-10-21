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

#ifndef __clang__
COMPILE_TIME_ASSERT(EM_POOL_DEFAULT > (em_pool_t)0 &&
		    EM_POOL_DEFAULT < (em_pool_t)EM_CONFIG_POOLS,
		    EM_ODP_EM_DEFAULT_POOL_ERROR);
COMPILE_TIME_ASSERT(EM_POOL_UNDEF != EM_POOL_DEFAULT,
		    EM_ODP_EM_POOL_UNDEF_ERROR);
#endif
COMPILE_TIME_ASSERT(EM_EVENT_USER_AREA_MAX_SIZE < UINT16_MAX,
		    EM_ODP_EM_EVENT_USER_AREA_MAX_SIZE_ERROR);
/*
 * Max supported value for the config file option 'pool.align_offset'.
 *
 * The limitation is set by events based on odp-bufs that include the ev-hdr at
 * the beginning of the odp-buf payload - the alignment is adjusted into the end
 * of the ev-hdr.
 * Events based on odp-pkts do not have this restriction but the same limit is
 * used for all.
 */
#define ALIGN_OFFSET_MAX  ((int)(sizeof(event_hdr_t) - \
				 offsetof(event_hdr_t, end_hdr_data)))

static inline mpool_elem_t *
mpool_poolelem2pool(objpool_elem_t *const objpool_elem)
{
	return (mpool_elem_t *)((uintptr_t)objpool_elem -
				offsetof(mpool_elem_t, objpool_elem));
}

static int
read_config_file(void)
{
	const char *conf_str;
	bool val_bool = false;
	int val = 0;
	int ret;

	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;

	EM_PRINT("EM-pool config:\n");

	/*
	 * Option: pool.statistics_enable
	 */
	conf_str = "pool.statistics_enable";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}

	if (val_bool) {
		if (!capa->buf.stats.bit.available || !capa->pkt.stats.bit.available) {
			EM_LOG(EM_LOG_ERR, "! %s: NOT supported by ODP - disabling!\n",
			       conf_str);
			val_bool = false; /* disable pool statistics, no ODP support! */
		}

		if (!capa->buf.stats.bit.cache_available || !capa->pkt.stats.bit.cache_available) {
			EM_LOG(EM_LOG_ERR, "! %s: omit events in pool cache, no ODP support!\n",
			       conf_str);
		}
	}

	/* store & print the value */
	em_shm->opt.pool.statistics_enable = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	/*
	 * Option: pool.align_offset
	 */
	conf_str = "pool.align_offset";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}
	if (val < 0 || val > ALIGN_OFFSET_MAX || !POWEROF2(val)) {
		EM_LOG(EM_LOG_ERR,
		       "Bad config value '%s = %d' (max: %d and value must be power of 2)\n",
		       conf_str, val, ALIGN_OFFSET_MAX);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.pool.align_offset = val;
	EM_PRINT("  %s (default): %d (max: %d)\n",
		 conf_str, val, ALIGN_OFFSET_MAX);

	/*
	 * Option: pool.user_area_size
	 */
	conf_str = "pool.user_area_size";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}
	if (val < 0 || (unsigned int)val > capa->pkt.max_uarea_size ||
	    val > EM_EVENT_USER_AREA_MAX_SIZE) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n",
		       conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.pool.user_area_size = val;
	EM_PRINT("  %s (default): %d (max: %d)\n",
		 conf_str, val,
		 MIN(EM_EVENT_USER_AREA_MAX_SIZE, capa->pkt.max_uarea_size));

	/*
	 * Option: pool.pkt_headroom
	 */
	conf_str = "pool.pkt_headroom";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0 || (unsigned int)val > capa->pkt.max_headroom) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n",
		       conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.pool.pkt_headroom = val;
	EM_PRINT("  %s (default): %d (max: %u)\n",
		 conf_str, val, capa->pkt.max_headroom);

	return 0;
}

em_status_t
pool_init(mpool_tbl_t *const mpool_tbl, mpool_pool_t *const mpool_pool,
	  const em_pool_cfg_t *default_pool_cfg)
{
	em_pool_t pool;
	int ret;
	const int cores = em_core_count();

	memset(mpool_tbl, 0, sizeof(mpool_tbl_t));
	memset(mpool_pool, 0, sizeof(mpool_pool_t));
	env_atomic32_init(&em_shm->pool_count);

	ret = objpool_init(&mpool_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_OPERATION_FAILED;

	for (int i = 0; i < EM_CONFIG_POOLS; i++) {
		pool = pool_idx2hdl(i);
		mpool_elem_t *mpool_elem = pool_elem_get(pool);

		if (unlikely(!mpool_elem))
			return EM_ERR_BAD_POINTER;

		mpool_elem->em_pool = pool;
		mpool_elem->event_type = EM_EVENT_TYPE_UNDEF;
		for (int j = 0; j < EM_MAX_SUBPOOLS; j++) {
			mpool_elem->odp_pool[j] = ODP_POOL_INVALID;
			mpool_elem->size[j] = 0;
		}

		objpool_add(&mpool_pool->objpool, i % cores,
			    &mpool_elem->objpool_elem);
	}

	/* Init the mapping tbl from odp-pool(=subpool) index to em-pool */
	if (odp_pool_max_index() >= POOL_ODP2EM_TBL_LEN)
		return EM_ERR_TOO_LARGE;
	for (int i = 0; i < POOL_ODP2EM_TBL_LEN; i++)
		mpool_tbl->pool_odp2em[i] = EM_POOL_UNDEF;

	/* Store common ODP pool capabilities in the mpool_tbl for easy access*/
	if (odp_pool_capability(&mpool_tbl->odp_pool_capability) != 0)
		return EM_ERR_LIB_FAILED;

	/* Read EM-pool related runtime config options */
	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	/* Create the 'EM_POOL_DEFAULT' pool */
	pool = em_pool_create(EM_POOL_DEFAULT_NAME, EM_POOL_DEFAULT,
			      default_pool_cfg);
	if (pool == EM_POOL_UNDEF || pool != EM_POOL_DEFAULT)
		return EM_ERR_ALLOC_FAILED;

	return EM_OK;
}

em_status_t
pool_term(const mpool_tbl_t *mpool_tbl)
{
	em_status_t stat = EM_OK;
	int i;

	(void)mpool_tbl;

	EM_PRINT("\n"
		 "Status before delete:\n");
	em_pool_info_print_all();

	for (i = 0; i < EM_CONFIG_POOLS; i++) {
		em_pool_t pool = pool_idx2hdl(i);
		const mpool_elem_t *mpool_elem = pool_elem_get(pool);
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

/* Helper func to invalid_pool_cfg() */
static int invalid_pool_cache_cfg(const em_pool_cfg_t *pool_cfg,
				  const char **err_str/*out*/)
{
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;
	uint32_t min_cache_size;
	uint32_t cache_size;

	if (pool_cfg->event_type == EM_EVENT_TYPE_SW)
		min_cache_size = capa->buf.min_cache_size;
	else
		min_cache_size = capa->pkt.min_cache_size;

	for (int i = 0; i < pool_cfg->num_subpools; i++) {
		if (pool_cfg->subpool[i].size <= 0 ||
		    pool_cfg->subpool[i].num <= 0) {
			*err_str = "Invalid subpool size/num";
			return -(9 * 10 + i); /* -90, -91, ... */
		}

		cache_size = pool_cfg->subpool[i].cache_size;
		if (unlikely(cache_size < min_cache_size)) {
			*err_str = "Requested cache size too small";
			return -(10 * 10 + i); /* -100, -101, ... */
		}
		/*
		 * If the given cache size is larger than odp-max,
		 * then use odp-max:
		 * if (cache_size > max_cache_size)
		 *         cache_size = max_cache_size;
		 * This is done later in pool_create();
		 */
	}

	return 0;
}

int invalid_pool_cfg(const em_pool_cfg_t *pool_cfg, const char **err_str/*out*/)
{
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;

	if (!pool_cfg) {
		*err_str = "Pool config NULL";
		return -1;
	}
	if (pool_cfg->__internal_check != EM_CHECK_INIT_CALLED) {
		*err_str = "Pool config not initialized";
		return -2;
	}

	if (pool_cfg->num_subpools <= 0 ||
	    pool_cfg->num_subpools > EM_MAX_SUBPOOLS) {
		*err_str = "Invalid number of subpools";
		return -3;
	}

	if (pool_cfg->event_type != EM_EVENT_TYPE_SW &&
	    pool_cfg->event_type != EM_EVENT_TYPE_PACKET) {
		*err_str = "Pool event type not supported, use _SW or _PACKET";
		return -4;
	}

	if (pool_cfg->align_offset.in_use &&
	    (pool_cfg->align_offset.value > ALIGN_OFFSET_MAX ||
	     !POWEROF2(pool_cfg->align_offset.value))) {
		*err_str = "Invalid align offset";
		return -5;
	}

	if (pool_cfg->user_area.in_use) {
		if (pool_cfg->user_area.size > EM_EVENT_USER_AREA_MAX_SIZE) {
			*err_str = "Event user area too large";
			return -6;
		}
		if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET) {
			size_t req_odp_uarea_sz = pool_cfg->user_area.size +
						  sizeof(event_hdr_t);
			if (req_odp_uarea_sz > capa->pkt.max_uarea_size) {
				*err_str = "ODP pkt max uarea not large enough";
				return -7;
			}
		}
	}

	if (pool_cfg->pkt.headroom.in_use &&
	    pool_cfg->pkt.headroom.value > capa->pkt.max_headroom) {
		*err_str = "Requested pkt headroom size too large";
		return -8;
	}

	int err = invalid_pool_cache_cfg(pool_cfg, err_str/*out*/);

	return err;
}

/*
 * Helper to pool_create() - preallocate all events in the pool for ESV to
 * maintain event state over multiple alloc- and free-operations.
 */
static void
pool_prealloc(const mpool_elem_t *pool_elem)
{
	em_event_t event;
	event_hdr_t *ev_hdr;
	uint64_t num_tot = 0;
	uint64_t num = 0;
	const uint32_t size = pool_elem->pool_cfg.subpool[0].size;
	list_node_t evlist;
	list_node_t *node;

	list_init(&evlist);

	for (int i = 0; i < pool_elem->num_subpools; i++)
		num_tot += pool_elem->pool_cfg.subpool[i].num;

	do {
		event = event_prealloc(pool_elem, size, pool_elem->event_type);
		if (likely(event != EM_EVENT_UNDEF)) {
			ev_hdr = event_to_hdr(event);
			list_add(&evlist, &ev_hdr->start_node);
			num++;
		}
	} while (event != EM_EVENT_UNDEF);

	if (unlikely(num < num_tot))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_TOO_SMALL),
			       EM_ESCOPE_POOL_CREATE,
			       "events expected:%" PRIu64 " actual:%" PRIu64 "",
			       num_tot, num);

	while (!list_is_empty(&evlist)) {
		node = list_rem_first(&evlist);
		ev_hdr = start_node_to_event_hdr(node);
		em_free(ev_hdr->event);
	}
}

/*
 * pool_create() helper: sort subpool cfg in ascending order based on buf size
 */
static void
sort_pool_cfg(const em_pool_cfg_t *pool_cfg, em_pool_cfg_t *sorted_cfg /*out*/)
{
	const int num_subpools = pool_cfg->num_subpools;

	*sorted_cfg = *pool_cfg;

	for (int i = 0; i < num_subpools - 1; i++) {
		int idx = i; /* array index containing smallest size */

		for (int j = i + 1; j < num_subpools; j++) {
			if (sorted_cfg->subpool[j].size <
			    sorted_cfg->subpool[idx].size)
				idx = j; /* store idx to smallest */
		}

		/* min size at [idx], swap with [i] */
		if (idx != i) {
			uint32_t size = sorted_cfg->subpool[i].size;
			uint32_t num = sorted_cfg->subpool[i].num;
			uint32_t cache_size = sorted_cfg->subpool[i].cache_size;

			sorted_cfg->subpool[i] = sorted_cfg->subpool[idx];

			sorted_cfg->subpool[idx].size = size;
			sorted_cfg->subpool[idx].num = num;
			sorted_cfg->subpool[idx].cache_size = cache_size;
		}
	}
}

/*
 * pool_create() helper: set pool event-cache size.
 *
 * Set the requested subpool cache-size based on user provided value and
 * limit set by odp-pool-capability.
 * Requested value can be larger than odp-max, use odp--max in this
 * case.
 * Verification against odp-min value done in invalid_pool_cfg().
 */
static void
set_poolcache_size(em_pool_cfg_t *pool_cfg)
{
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;
	int num_subpools = pool_cfg->num_subpools;
	uint32_t max_cache_size;

	if (pool_cfg->event_type == EM_EVENT_TYPE_SW)
		max_cache_size = capa->buf.max_cache_size;
	else
		max_cache_size = capa->pkt.max_cache_size;

	for (int i = 0; i < num_subpools; i++) {
		if (max_cache_size < pool_cfg->subpool[i].cache_size)
			pool_cfg->subpool[i].cache_size = max_cache_size;
	}
}

/*
 * pool_create() helper: determine payload alignment.
 */
static int
set_align(const em_pool_cfg_t *pool_cfg,
	  uint32_t *align_offset /*out*/, uint32_t *odp_align /*out*/)
{
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;
	uint32_t offset = 0;
	uint32_t align = ODP_CACHE_LINE_SIZE;

	/* Pool-specific param overrides config file 'align_offset' value */
	if (pool_cfg->align_offset.in_use)
		offset = pool_cfg->align_offset.value; /* pool cfg */
	else
		offset = em_shm->opt.pool.align_offset; /* cfg file */

	/* Set subpool minimum alignment */
	if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET) {
		if (align > capa->pkt.max_align)
			align = capa->pkt.max_align;
	} else {
		if (align > capa->buf.max_align)
			align = capa->buf.max_align;
	}

	*align_offset = offset;
	*odp_align = align;

	/* verify alignment requirements */
	if (!POWEROF2(align) || align <= offset)
		return -1;

	return 0;
}

/*
 * pool_create() helper: determine user area size.
 */
static int
set_uarea_size(const em_pool_cfg_t *pool_cfg, uint32_t align_offset,
	       size_t *uarea_req_size/*out*/, size_t *uarea_pad_size/*out*/)
{
	size_t req_size = 0;
	size_t pad_size = 0;
	size_t max_size = 0;
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;

	if (pool_cfg->user_area.in_use) /* use pool-cfg */
		req_size = pool_cfg->user_area.size;
	else /* use cfg-file */
		req_size = em_shm->opt.pool.user_area_size;

	if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET) {
		pad_size = req_size;
		max_size = MIN(capa->pkt.max_uarea_size,
			       EM_EVENT_USER_AREA_MAX_SIZE);
	} else if (req_size > 0) {
		/* EM_EVENT_TYPE_SW: bufs */
		/* Note: contains align_offset extra space for adjustment */
		pad_size = ROUND_UP(req_size + align_offset, 32);
		max_size = EM_EVENT_USER_AREA_MAX_SIZE;
	}

	if (req_size > max_size)
		return -1;

	*uarea_req_size = req_size;
	*uarea_pad_size = pad_size;
	return 0;
}

/*
 * pool_create() helper: set the pkt headroom
 */
static int
set_pkt_headroom(const em_pool_cfg_t *pool_cfg,
		 uint32_t *pkt_headroom /*out*/,
		 uint32_t *max_headroom /*out, for err print only*/)
{
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;
	/* default value from cfg file */
	uint32_t headroom = em_shm->opt.pool.pkt_headroom;

	/* Pool-specific param overrides config file value */
	if (pool_cfg->pkt.headroom.in_use)
		headroom = pool_cfg->pkt.headroom.value;

	*pkt_headroom = headroom;
	*max_headroom = capa->pkt.max_headroom;

	if (unlikely(headroom > capa->pkt.max_headroom))
		return -1;

	return 0;
}

/** Helper to create_subpools() */
static void set_pool_params_pkt(odp_pool_param_t *pool_params /* out */,
				uint32_t size, uint32_t num, uint32_t cache_size,
				uint32_t align_offset, uint32_t odp_align,
				uint32_t uarea_size, uint32_t pkt_headroom)
{
	const odp_pool_capability_t *capa = &em_shm->mpool_tbl.odp_pool_capability;

	odp_pool_param_init(pool_params);

	pool_params->type = ODP_POOL_PACKET;
	/* num == max_num, helps pool-info stats calculation */
	pool_params->pkt.num = num;
	pool_params->pkt.max_num = num;

	if (size > align_offset)
		size = size - align_offset;
	else
		size = 1; /* 0:default, can be big => use 1 */
	/* len == max_len */
	pool_params->pkt.len = size;
	pool_params->pkt.max_len = size;
	pool_params->pkt.seg_len = size;
	pool_params->pkt.align = odp_align;
	/*
	 * Reserve space for the event header in each packet's
	 * ODP-user-area:
	 */
	pool_params->pkt.uarea_size = sizeof(event_hdr_t) + uarea_size;
	/*
	 * Set the pkt headroom.
	 * Make sure the alloc-alignment fits into the headroom.
	 */
	pool_params->pkt.headroom = pkt_headroom;
	if (pkt_headroom < align_offset)
		pool_params->pkt.headroom = align_offset;

	pool_params->pkt.cache_size = cache_size;

	/* Pkt pool statistics */
	pool_params->stats.all = 0;
	if (em_shm->opt.pool.statistics_enable) {
		if (capa->pkt.stats.bit.available)
			pool_params->stats.bit.available = 1;
		if (capa->pkt.stats.bit.cache_available)
			pool_params->stats.bit.cache_available = 1;
	}
}

/** Helper to create_subpools() */
static void set_pool_params_buf(odp_pool_param_t *pool_params /* out */,
				uint32_t size, uint32_t num, uint32_t cache_size,
				uint32_t odp_align, uint32_t uarea_size)
{
	const odp_pool_capability_t *capa = &em_shm->mpool_tbl.odp_pool_capability;

	odp_pool_param_init(pool_params);

	pool_params->type = ODP_POOL_BUFFER;
	pool_params->buf.num = num;
	pool_params->buf.size = size + sizeof(event_hdr_t) + uarea_size;
	pool_params->buf.align = odp_align;
	pool_params->buf.cache_size = cache_size;

	/* Buf pool statistics */
	pool_params->stats.all = 0;
	if (em_shm->opt.pool.statistics_enable) {
		if (capa->buf.stats.bit.available)
			pool_params->stats.bit.available = 1;
		if (capa->buf.stats.bit.cache_available)
			pool_params->stats.bit.cache_available = 1;
	}
}

static int
create_subpools(const em_pool_cfg_t *pool_cfg,
		uint32_t align_offset, uint32_t odp_align,
		uint32_t uarea_size, uint32_t pkt_headroom,
		mpool_elem_t *mpool_elem /*out*/)
{
	const int num_subpools = pool_cfg->num_subpools;
	mpool_tbl_t *const mpool_tbl = &em_shm->mpool_tbl;

	for (int i = 0; i < num_subpools; i++) {
		char pool_name[ODP_POOL_NAME_LEN];
		odp_pool_param_t pool_params;
		uint32_t size = pool_cfg->subpool[i].size;
		uint32_t num = pool_cfg->subpool[i].num;
		uint32_t cache_size = pool_cfg->subpool[i].cache_size;

		if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET) {
			set_pool_params_pkt(&pool_params /* out */,
					    size, num, cache_size,
					    align_offset, odp_align,
					    uarea_size, pkt_headroom);
		} else { /* pool_cfg->event_type == EM_EVENT_TYPE_SW */
			set_pool_params_buf(&pool_params /* out */,
					    size, num, cache_size,
					    odp_align, uarea_size);
		}

		snprintf(pool_name, sizeof(pool_name), "%" PRI_POOL ":%d-%s",
			 mpool_elem->em_pool, i, mpool_elem->name);
		pool_name[sizeof(pool_name) - 1] = '\0';

		odp_pool_t odp_pool = odp_pool_create(pool_name, &pool_params);

		if (unlikely(odp_pool == ODP_POOL_INVALID))
			return -1;

		int odp_pool_idx = odp_pool_index(odp_pool);

		if (unlikely(odp_pool_idx < 0))
			return -2;

		/* Store mapping from odp-pool (idx) to em-pool */
		mpool_tbl->pool_odp2em[odp_pool_idx] = mpool_elem->em_pool;

		mpool_elem->odp_pool[i] = odp_pool;
		mpool_elem->size[i] = pool_cfg->subpool[i].size;
		mpool_elem->num_subpools++; /* created subpools for delete */

		/*odp_pool_print(odp_pool);*/
	}

	return 0;
}

em_pool_t
pool_create(const char *name, em_pool_t req_pool, const em_pool_cfg_t *pool_cfg)
{
	/* Allocate a free EM pool */
	const em_pool_t pool = pool_alloc(req_pool/* requested or undef*/);

	if (unlikely(pool == EM_POOL_UNDEF))
		return EM_POOL_UNDEF;

	mpool_elem_t *mpool_elem = pool_elem_get(pool);

	/* Sanity check */
	if (!mpool_elem || mpool_elem->em_pool != pool)
		return EM_POOL_UNDEF;

	mpool_elem->event_type = pool_cfg->event_type;
	/* Store successfully created subpools later */
	mpool_elem->num_subpools = 0;
	/* Store the event pool name, if given */
	if (name && *name) {
		strncpy(mpool_elem->name, name, sizeof(mpool_elem->name));
		mpool_elem->name[sizeof(mpool_elem->name) - 1] = '\0';
	} else {
		mpool_elem->name[0] = '\0';
	}

	em_pool_cfg_t sorted_cfg;

	/*
	 * Sort the subpool cfg in ascending order based on the buffer size
	 */
	sort_pool_cfg(pool_cfg, &sorted_cfg/*out*/);
	/* Use sorted_cfg instead of pool_cfg from here on */

	/*
	 * Set the cache-size of each subpool in the EM-pool
	 */
	set_poolcache_size(&sorted_cfg);

	/* Store the sorted config */
	mpool_elem->pool_cfg = sorted_cfg;

	/*
	 * Event payload alignment requirement for the pool
	 */
	uint32_t align_offset = 0;
	uint32_t odp_align = 0;
	int err = set_align(&sorted_cfg, &align_offset/*out*/,
			    &odp_align/*out*/);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_POOL_CREATE,
			       "EM-pool:\"%s\" align mismatch:\n"
			       "align:%u cfg:align_offset:%u",
			       name, odp_align, align_offset);
		goto error;
	}
	/* store the align offset, needed in pkt-alloc */
	mpool_elem->align_offset = align_offset;

	/*
	 * Event user area size.
	 * Pool-specific param overrides config file 'user_area_size' value
	 */
	size_t uarea_req_size = 0;
	size_t uarea_pad_size = 0;

	err = set_uarea_size(&sorted_cfg, align_offset,
			     &uarea_req_size/*out*/, &uarea_pad_size/*out*/);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_POOL_CREATE,
			       "EM-pool:\"%s\" invalid uarea config:\n"
			       "req.size:%zu => padded uarea size:%zu",
			       name, uarea_req_size, uarea_pad_size);
		goto error;
	}

	/* store the user_area sizes, needed in alloc */
	mpool_elem->user_area.req_size = uarea_req_size & UINT16_MAX;
	mpool_elem->user_area.pad_size = uarea_pad_size & UINT16_MAX;

	EM_DBG("EM-pool:\"%s\":\n"
	       "  user_area: .req_size=%zu .pad_size=%zu align_offset=%u\n",
	       name, uarea_req_size, uarea_pad_size, align_offset);

	/*
	 * Set the headroom for events in EM packet pools
	 */
	uint32_t pkt_headroom = 0;
	uint32_t max_headroom = 0;

	if (sorted_cfg.event_type == EM_EVENT_TYPE_PACKET) {
		err = set_pkt_headroom(&sorted_cfg, &pkt_headroom/*out*/,
				       &max_headroom/*out*/);
		if (unlikely(err)) {
			INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_POOL_CREATE,
				       "EM-pool:\"%s\" invalid pkt headroom:\n"
				       "headroom:%u vs. max:headroom:%u",
				       name, pkt_headroom, max_headroom);
			goto error;
		}
	}

	/*
	 * Create the subpools for the EM event-pool.
	 * Each EM subpool is an ODP pool.
	 */
	err = create_subpools(&sorted_cfg, align_offset, odp_align,
			      uarea_pad_size, pkt_headroom, mpool_elem /*out*/);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_ALLOC_FAILED),
			       EM_ESCOPE_POOL_CREATE,
			       "EM-pool:\"%s\" create fails:%d\n"
			       "subpools req:%d vs. subpools created:%d",
			       name, err, sorted_cfg.num_subpools,
			       mpool_elem->num_subpools);
			goto error;
	}

	/*
	 * ESV: preallocate all events in the pool
	 */
	if (esv_enabled() && em_shm->opt.esv.prealloc_pools)
		pool_prealloc(mpool_elem);

	/* Success! */
	return mpool_elem->em_pool;

error:
	(void)pool_delete(pool);
	return EM_POOL_UNDEF;
}

em_status_t
pool_delete(em_pool_t pool)
{
	mpool_tbl_t *const mpool_tbl = &em_shm->mpool_tbl;
	mpool_elem_t *const mpool_elem = pool_elem_get(pool);
	int i;

	if (unlikely(mpool_elem == NULL || !pool_allocated(mpool_elem)))
		return EM_ERR_BAD_STATE;

	for (i = 0; i < mpool_elem->num_subpools; i++) {
		odp_pool_t odp_pool = mpool_elem->odp_pool[i];
		int odp_pool_idx;
		int ret;

		if (odp_pool == ODP_POOL_INVALID)
			return EM_ERR_NOT_FOUND;

		odp_pool_idx = odp_pool_index(odp_pool);
		if (unlikely(odp_pool_idx < 0))
			return EM_ERR_BAD_ID;

		ret = odp_pool_destroy(odp_pool);
		if (unlikely(ret))
			return EM_ERR_LIB_FAILED;

		/* Clear mapping from odp-pool (idx) to em-pool */
		mpool_tbl->pool_odp2em[odp_pool_idx] = EM_POOL_UNDEF;

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
			const mpool_elem_t *mpool_elem =
				&em_shm->mpool_tbl.pool[i];

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

#define POOL_INFO_HDR_STR \
"  id     name             type offset uarea sizes    [size count(used/free) cache]\n"

#define POOL_INFO_SUBSTR_FMT \
"%d:[sz=%" PRIu32 " n=%" PRIu32 "(%" PRIu32 "/%" PRIu32 ") $=%" PRIu32 "]"

#define POOL_INFO_SUBSTR_NO_STATS_FMT \
"%d:[sz=%" PRIu32 " n=%" PRIu32 "(-/-) cache=%" PRIu32 "]"

void pool_info_print_hdr(unsigned int num_pools)
{
	if (num_pools == 1) {
		EM_PRINT("EM Event Pool\n"
			 "-------------\n"
			 POOL_INFO_HDR_STR);
	} else {
		EM_PRINT("EM Event Pools:%2u\n"
			 "-----------------\n"
			 POOL_INFO_HDR_STR, num_pools);
	}
}

void pool_info_print(em_pool_t pool)
{
	em_pool_info_t pool_info;
	em_status_t stat;
	const char *pool_type;

	stat = em_pool_info(pool, &pool_info/*out*/);
	if (unlikely(stat != EM_OK)) {
		EM_PRINT("  %-6" PRI_POOL " %-16s  n/a   n/a   n/a   n/a     [n/a]\n",
			 pool, "err:n/a");
		return;
	}

	pool_type = pool_info.event_type == EM_EVENT_TYPE_SW ? "buf" : "pkt";
	EM_PRINT("  %-6" PRI_POOL " %-16s %4s   %02u    %02zu    %02u   ",
		 pool, pool_info.name, pool_type,
		 pool_info.align_offset, pool_info.user_area_size,
		 pool_info.num_subpools);

	for (int i = 0; i < pool_info.num_subpools; i++) {
		char subpool_str[42];

		if (em_shm->opt.pool.statistics_enable) {
			snprintf(subpool_str, sizeof(subpool_str),
				 POOL_INFO_SUBSTR_FMT, i,
				 pool_info.subpool[i].size,
				 pool_info.subpool[i].num,
				 pool_info.subpool[i].used,
				 pool_info.subpool[i].free,
				 pool_info.subpool[i].cache_size);
		} else {
			snprintf(subpool_str, sizeof(subpool_str),
				 POOL_INFO_SUBSTR_NO_STATS_FMT, i,
				 pool_info.subpool[i].size,
				 pool_info.subpool[i].num,
				 pool_info.subpool[i].cache_size);
		}
		subpool_str[sizeof(subpool_str) - 1] = '\0';
		EM_PRINT(" %-42s", subpool_str);
	}

	EM_PRINT("\n");
}
