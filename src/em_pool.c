/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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

/**
 * @def ALIGN_OFFSET_MAX
 *
 * Max supported value for the config file option 'pool.align_offset'.
 */
#define ALIGN_OFFSET_MAX  ((int)(16))

static inline mpool_elem_t *
mpool_poolelem2pool(objpool_elem_t *const objpool_elem)
{
	return (mpool_elem_t *)((uintptr_t)objpool_elem -
				offsetof(mpool_elem_t, objpool_elem));
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

static int event_type_from_string(const char *str, em_event_type_t *event_type /*out*/)
{
	if (strstr(str, "EM_EVENT_TYPE_SW")) {
		*event_type = EM_EVENT_TYPE_SW;
	} else if (strstr(str, "EM_EVENT_TYPE_PACKET")) {
		*event_type = EM_EVENT_TYPE_PACKET;
	} else if (strstr(str, "EM_EVENT_TYPE_VECTOR")) {
		*event_type = EM_EVENT_TYPE_VECTOR;
	} else {
		EM_LOG(EM_LOG_ERR, "Event type %s not supported.\n", str);
		return -1;
	}

	return 0;
}

/* Read option: startup_pools.conf[i].pool_cfg.subpools[j] from the EM config file */
static inline int read_config_subpool(const libconfig_list_t *subpool, int index,
				      const char *pool_cfg_str, em_pool_cfg_t *cfg/*out*/)
{
	int ret;
	/* Option: subpools[index].size */
	ret = em_libconfig_list_lookup_int(subpool, index, "size",
					   (int *)&cfg->subpool[index].size);
	if (unlikely(ret != 1)) {
		EM_LOG(EM_LOG_ERR,
		       "Option '%s.subpools[%d].size' not found or wrong type.\n",
		       pool_cfg_str, index);
		return -1;
	}

	if (cfg->subpool[index].size <= 0) {
		EM_LOG(EM_LOG_ERR, "Invalid '%s.subpools[%d].size'.\n",
		       pool_cfg_str, index);
		return -1;
	}

	/* Option: subpools[index].num */
	ret = em_libconfig_list_lookup_int(subpool, index, "num",
					   (int *)&cfg->subpool[index].num);
	if (unlikely(ret != 1)) {
		EM_LOG(EM_LOG_ERR,
		       "Option '%s.subpools[%d].num' not found or wrong type.\n",
		       pool_cfg_str, index);
		return -1;
	}

	if (cfg->subpool[index].num <= 0) {
		EM_LOG(EM_LOG_ERR, "Invalid '%s.subpools[%d].num'.\n",
		       pool_cfg_str, index);
		return -1;
	}

	/*
	 * Option: subpools[index].cache_size
	 * Not mandatory
	 */
	ret = em_libconfig_list_lookup_int(subpool, index, "cache_size",
					   (int *)&cfg->subpool[index].cache_size);

	/* If cache_size is given, check if it is valid */
	if (ret == 1) {
		uint32_t min_cache_size;
		const odp_pool_capability_t *capa;

		capa = &em_shm->mpool_tbl.odp_pool_capability;

		min_cache_size = (cfg->event_type == EM_EVENT_TYPE_SW) ?
			capa->buf.min_cache_size : capa->pkt.min_cache_size;

		if (unlikely(cfg->subpool[index].cache_size < min_cache_size)) {
			EM_LOG(EM_LOG_ERR,
			       "'%s.subpools[%d].cache_size' too small.\n",
			       pool_cfg_str, index);
			return -1;
		}
	} else if (ret == 0) {/*cache_size is given but with wrong data type*/
		EM_LOG(EM_LOG_ERR,
		       "'%s.subpools[%d].cache_size' wrong data type.\n",
		       pool_cfg_str, index);
		return -1;
	}

	/* No need to return fail -1 when cache_size not given (ret == -1) */
	return 0;
}

static int is_pool_type_supported(em_event_type_t type,
				  const char **err_str/*out*/)
{
	const odp_pool_capability_t *capa = &em_shm->mpool_tbl.odp_pool_capability;

	if (type == EM_EVENT_TYPE_SW) {
		if (capa->buf.max_pools == 0) {
			*err_str = "SW (buf) pool type unsupported";
			return -1;
		}
	} else if (type == EM_EVENT_TYPE_PACKET) {
		if (capa->pkt.max_pools == 0) {
			*err_str = "PACKET pool type unsupported";
			return -1;
		}
	} else if (type == EM_EVENT_TYPE_VECTOR) {
		if (capa->vector.max_pools == 0) {
			*err_str = "VECTOR pool type unsupported";
			return -1;
		}
	} else {
		*err_str = "Pool type unsupported, use _SW, _PACKET or _VECTOR";
		return -1;
	}

	return 0;
}

static inline bool is_align_offset_valid(const em_pool_cfg_t *pool_cfg)
{
	if (pool_cfg->align_offset.in_use &&
	    (pool_cfg->align_offset.value > ALIGN_OFFSET_MAX ||
	     !POWEROF2(pool_cfg->align_offset.value))) {
		return false;
	}

	return true;
}

static inline int is_user_area_valid(const em_pool_cfg_t *pool_cfg,
				     const odp_pool_capability_t *capa,
				     const char **err_str/*out*/)
{
	/* No need to check when pool specific value is not used */
	if (!pool_cfg->user_area.in_use)
		return 0;

	if (pool_cfg->user_area.size > EM_EVENT_USER_AREA_MAX_SIZE) {
		*err_str = "Event user area too large";
		return -1;
	}

	if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET) {
		size_t req_odp_uarea_sz = pool_cfg->user_area.size +
					  sizeof(event_hdr_t);
		if (req_odp_uarea_sz > capa->pkt.max_uarea_size) {
			*err_str = "ODP pkt max uarea not large enough";
			return -1;
		}
	}
	if (pool_cfg->event_type == EM_EVENT_TYPE_VECTOR) {
		size_t req_odp_uarea_sz = pool_cfg->user_area.size +
					  sizeof(event_hdr_t);
		if (req_odp_uarea_sz > capa->vector.max_uarea_size) {
			*err_str = "ODP pkt-vector max uarea not large enough";
			return -1;
		}
	}

	return 0;
}

/* Read option: startup_pools.conf[index].pool_cfg.align_offset from the EM config file */
static inline int read_config_align_offset(const libconfig_group_t *align_offset,
					   const char *pool_cfg_str,
					   em_pool_cfg_t *cfg/*out*/)
{
	int ret;

	/* Option: startup_pools.conf[index].pool_cfg.align_offset.in_use */
	ret = em_libconfig_group_lookup_bool(align_offset, "in_use",
					     &cfg->align_offset.in_use);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.align_offset.in_use' not found or wrong type\n",
		       pool_cfg_str);
		return -1;
	}

	/* Option: startup_pools.conf[index].pool_cfg.align_offset.value */
	ret = em_libconfig_group_lookup_int(align_offset, "value",
					    (int *)&cfg->align_offset.value);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.align_offset.value' not found or wront type\n",
		       pool_cfg_str);
		return -1;
	}

	/* Check whether the given value is valid or not */
	if (!is_align_offset_valid(cfg)) {
		EM_LOG(EM_LOG_ERR, "Invalid '%s.align_offset.value': %d\n"
		       "Max align_offset is %d and it must be power of 2\n",
		       pool_cfg_str, cfg->align_offset.value, ALIGN_OFFSET_MAX);
		return -1;
	}

	return 0;
}

/* Read option: startup_pools.conf[index].pool_cfg.user_area from the EM config file */
static inline int read_config_user_area(const libconfig_group_t *user_area,
					const char *pool_cfg_str,
					em_pool_cfg_t *cfg/*out*/)
{
	int ret;
	const odp_pool_capability_t *capa;
	const char *err_str = "";

	/* Option: startup_pools.conf[index].pool_cfg.user_area.in_use */
	ret = em_libconfig_group_lookup_bool(user_area, "in_use",
					     &cfg->user_area.in_use);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.user_area.in_use' not found or wrong type\n",
		       pool_cfg_str);
		return -1;
	}

	/* Option: startup_pools.conf[index].pool_cfg.user_area.size */
	ret = em_libconfig_group_lookup_int(user_area, "size",
					    (int *)&cfg->user_area.size);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.user_area.size' not found or wrong type\n",
		       pool_cfg_str);
		return -1;
	}

	capa = &em_shm->mpool_tbl.odp_pool_capability;
	/* Check whether the given value is valid or not */
	if (is_user_area_valid(cfg, capa, &err_str) < 0) {
		EM_LOG(EM_LOG_ERR, "%s: %ld\n", err_str, cfg->user_area.size);
		return -1;
	}

	return 0;
}

/* Read option: startup_pools.conf[index].pool_cfg.pkt.headroom from the EM config file */
static inline int read_config_pkt_headroom(const libconfig_group_t *pkt_headroom,
					   const char *pool_cfg_str,
					   em_pool_cfg_t *cfg/*out*/)
{
	int ret;
	const odp_pool_capability_t *capa;

	/*Option: startup_pools.conf[index].pool_cfg.pkt.headroom.in_use*/
	ret = em_libconfig_group_lookup_bool(pkt_headroom, "in_use",
					     &cfg->pkt.headroom.in_use);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.pkt.headroom.in_use' not found or wrong type\n",
		       pool_cfg_str);
		return -1;
	}

	/*Option: startup_pools.conf[index].pool_cfg.pkt.headroom.value*/
	ret = em_libconfig_group_lookup_int(pkt_headroom, "value",
					    (int *)&cfg->pkt.headroom.value);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.pkt.headroom.value' not found or wront type\n",
		       pool_cfg_str);
		return -1;
	}

	/* Check whether the given value is valid or not */
	capa = &em_shm->mpool_tbl.odp_pool_capability;
	if (cfg->pkt.headroom.in_use &&
	    cfg->pkt.headroom.value > capa->pkt.max_headroom) {
		EM_LOG(EM_LOG_ERR,
		       "'%s.pkt.headroom.value' %d too large (max=%d)\n",
		       pool_cfg_str, cfg->pkt.headroom.value,
		       capa->pkt.max_headroom);
		return -1;
	}

	return 0;
}

/* Read option: startup_pools.conf[index] from the EM config file */
static int read_config_startup_pools_conf(const libconfig_list_t *list, int index)
{
	int ret;
	int pool;
	int ret_pool;
	int num_subpools;
	const char *pool_name;
	const char *event_type;
	char pool_cfg_str[40];
	libconfig_group_t *pool_cfg;
	const libconfig_list_t *subpool;
	const libconfig_group_t *headroom;
	const libconfig_group_t *user_area;
	const libconfig_group_t *align_offset;
	startup_pool_conf_t *conf = &em_shm->opt.startup_pools.conf[index];
	em_pool_cfg_t *cfg = &conf->cfg;
	const char *err_str = "";

	snprintf(pool_cfg_str, sizeof(pool_cfg_str),
		 "startup_pools.conf[%d].pool_cfg", index);

	pool_cfg = em_libconfig_list_lookup_group(list, index, "pool_cfg");
	if (!pool_cfg) {
		EM_LOG(EM_LOG_ERR, "Conf option '%s' not found\n", pool_cfg_str);
		return -1;
	}

	em_pool_cfg_init(cfg);

	/*
	 * Read mandatory fields first, in case they are not provided, no need
	 * to proceed to read optional fields.
	 */

	/* Option: startup_pools.conf[index].pool_cfg.event_type */
	ret = em_libconfig_group_lookup_string(pool_cfg, "event_type", &event_type);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "'%s.event_type' not found.\n", pool_cfg_str);
		return -1;
	}

	ret = event_type_from_string(event_type, &cfg->event_type/*out*/);
	if (unlikely(ret < 0))
		return -1;

	ret = is_pool_type_supported(cfg->event_type, &err_str/*out*/);
	if (unlikely(ret)) {
		EM_LOG(EM_LOG_ERR, "%s", err_str);
		return -1;
	}

	/* Option: startup_pools.conf[index].pool_cfg.num_subpools */
	ret = em_libconfig_group_lookup_int(pool_cfg, "num_subpools",
					    &cfg->num_subpools);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "'%s.num_subpools' not found.\n", pool_cfg_str);
		return -1;
	}

	if (cfg->num_subpools <= 0 || cfg->num_subpools > EM_MAX_SUBPOOLS) {
		EM_LOG(EM_LOG_ERR, "Invalid '%s.num_subpools'\n"
		       "Valid value range is [1, %d]\n", pool_cfg_str,
		       EM_MAX_SUBPOOLS);
		return -1;
	}

	/* Option: startup_pools.conf[index].pool_cfg.subpools */
	subpool = em_libconfig_group_lookup_list(pool_cfg, "subpools");
	if (unlikely(!subpool)) {
		EM_LOG(EM_LOG_ERR, "'%s.subpools' not found.\n", pool_cfg_str);
		return -1;
	}

	num_subpools = em_libconfig_list_length(subpool);
	if (unlikely(num_subpools != cfg->num_subpools)) {
		EM_LOG(EM_LOG_ERR, "The number of subpool configuration given\n"
		       "in '%s.subpools' does not match '%s.num_subpools'.\n",
		       pool_cfg_str, pool_cfg_str);
		return -1;
	}

	for (int j = 0; j < num_subpools; j++) {
		ret = read_config_subpool(subpool, j, pool_cfg_str, cfg);

		if (unlikely(ret < 0))
			return -1;
	}

	/* Following are optional configurations */

	/* Option: startup_pools.conf[index].pool */
	ret_pool = em_libconfig_list_lookup_int(list, index, "pool", &pool);
	if (unlikely(ret_pool == 0)) {
		EM_LOG(EM_LOG_ERR,
		       "'startup_pools.conf[%d].pool' has wrong data type(expect int)\n",
		       index);
		return -1;
	}

	/* startup_pools.conf[index].pool is provided */
	if (ret_pool == 1) {
		if (pool < 0 || pool > EM_CONFIG_POOLS) {
			EM_LOG(EM_LOG_ERR, "Invalid pool ID %d, valid IDs are within [0, %d]\n",
			       pool, EM_CONFIG_POOLS);
			return -1;
		}

		conf->pool = (em_pool_t)(uintptr_t)pool;
	}

	/* Option: startup_pools.conf[index].name */
	ret = em_libconfig_list_lookup_string(list, index, "name", &pool_name);
	if (unlikely(ret == 0)) {
		EM_LOG(EM_LOG_ERR,
		       "'startup_pools.conf[%d].name' has wrong data type(expect string)\n",
		       index);
		return -1;
	}

	if (ret_pool == 1 && ret == 1) { /*Both pool and name have been given*/
		const char *is_default_name = strstr(pool_name, EM_POOL_DEFAULT_NAME);
		bool is_default_id = (conf->pool == EM_POOL_DEFAULT);

		if (is_default_name && !is_default_id) {
			EM_LOG(EM_LOG_ERR,
			       "Default name \"%s\" with non-default ID %d\n",
			       EM_POOL_DEFAULT_NAME, (int)(uintptr_t)conf->pool);
			return -1;
		}

		if (is_default_id && !is_default_name) {
			EM_LOG(EM_LOG_ERR,
			       "Default pool ID 1 with non-default name \"%s\"\n",
			       pool_name);
			return -1;
		}
	}

	if (ret == 1) { /* Pool name is given and no conflict with pool ID */
		strncpy(conf->name, pool_name, EM_POOL_NAME_LEN - 1);
		conf->name[EM_POOL_NAME_LEN - 1] = '\0';
	}

	align_offset = em_libconfig_group_lookup_group(pool_cfg, "align_offset");
	/*align_offset is provided*/
	if (align_offset && read_config_align_offset(align_offset, pool_cfg_str, cfg))
		return -1;

	user_area = em_libconfig_group_lookup_group(pool_cfg, "user_area");
	if (user_area && read_config_user_area(user_area, pool_cfg_str, cfg))
		return -1;

	headroom = em_libconfig_group_lookup_group(pool_cfg, "pkt.headroom");
	if (headroom) {
		if (read_config_pkt_headroom(headroom, pool_cfg_str, cfg))
			return -1;

		/* Ignore the given pkt.headroom for non packet event type */
		if (conf->cfg.event_type != EM_EVENT_TYPE_PACKET)
			EM_PRINT("pkt.headroom will be ignored for non packet type!\n");
	}

	return 0;
}

/* Print option: startup_pools from the EM config file */
static void print_config_startup_pools(void)
{
	startup_pool_conf_t *conf;
	char str_conf[32];
	const char *str = "";

	EM_PRINT("  startup_pools.num: %u\n", em_shm->opt.startup_pools.num);

	for (uint32_t i = 0; i < em_shm->opt.startup_pools.num; i++) {
		conf = &em_shm->opt.startup_pools.conf[i];

		snprintf(str_conf, sizeof(str_conf), "  startup_pools.conf[%d]", i);

		if (*conf->name)
			EM_PRINT("%s.name: %s\n", str_conf, conf->name);

		if (conf->pool)
			EM_PRINT("%s.pool: %d\n", str_conf, (int)(uintptr_t)conf->pool);

		/*event type*/
		if (conf->cfg.event_type == EM_EVENT_TYPE_SW)
			str = "EM_EVENT_TYPE_SW";
		else if (conf->cfg.event_type == EM_EVENT_TYPE_PACKET)
			str = "EM_EVENT_TYPE_PACKET";
		else if (conf->cfg.event_type == EM_EVENT_TYPE_VECTOR)
			str = "EM_EVENT_TYPE_VECTOR";
		EM_PRINT("%s.pool_cfg.event_type: %s\n", str_conf, str);

		/*align_offset*/
		str = conf->cfg.align_offset.in_use ? "true" : "false";
		EM_PRINT("%s.pool_cfg.align_offset.in_use: %s\n", str_conf, str);
		EM_PRINT("%s.pool_cfg.align_offset.value: %d\n", str_conf,
			 conf->cfg.align_offset.value);

		/*user area*/
		str = conf->cfg.user_area.in_use ? "true" : "false";
		EM_PRINT("%s.pool_cfg.user_area.in_use: %s\n", str_conf, str);
		EM_PRINT("%s.pool_cfg.user_area.size: %ld\n", str_conf,
			 conf->cfg.user_area.size);

		/*pkt headroom*/
		str = conf->cfg.pkt.headroom.in_use ? "true" : "false";
		EM_PRINT("%s.pool_cfg.pkt.headroom.in_use: %s\n", str_conf, str);
		EM_PRINT("%s.pool_cfg.pkt.headroom.value: %d\n", str_conf,
			 conf->cfg.pkt.headroom.value);

		/*number of subpools*/
		EM_PRINT("%s.pool_cfg.num_subpools: %u\n", str_conf,
			 conf->cfg.num_subpools);

		/*subpools*/
		for (int j = 0; j < conf->cfg.num_subpools; j++) {
			EM_PRINT("%s.pool_cfg.subpools[%d].size: %u\n", str_conf,
				 j, conf->cfg.subpool[j].size);

			EM_PRINT("%s.pool_cfg.subpools[%d].num: %u\n", str_conf,
				 j, conf->cfg.subpool[j].num);

			EM_PRINT("%s.pool_cfg.subpools[%d].cache_size: %u\n",
				 str_conf, j, conf->cfg.subpool[j].cache_size);
		}
	}
}

/* Read option: startup_pools from the EM config file */
static int read_config_startup_pools(void)
{
	int ret;
	int list_len;
	int num_startup_pools;
	const libconfig_list_t *conf_list;
	libconfig_setting_t *default_setting;
	libconfig_setting_t *runtime_setting;
	libconfig_setting_t *startup_pools_setting;

	em_libconfig_lookup(&em_shm->libconfig, "startup_pools",
			    &default_setting, &runtime_setting);

	/*
	 * Option: startup_pools
	 *
	 * Optional. Thus, when runtime configuration is provided, and option
	 * "startup_pools" is given, use it. However, when option "startup_pools"
	 * is not specified in the given runtime configuration file, returns
	 * without giving error, which means no startup pools will be created.
	 * Note that it does not fall back to use the option "startup_pools"
	 * specified in the default configuration file.
	 */
	if (em_shm->libconfig.has_cfg_runtime) {
		if (runtime_setting)
			startup_pools_setting = runtime_setting;
		else
			return 0;
	} else {
		if (default_setting)
			startup_pools_setting = default_setting;
		else
			return 0;
	}

	EM_PRINT("EM-startup_pools config:\n");
	/*
	 * Option: startup_pools.num
	 * Mandatory when startup_pools option is given
	 */
	ret = em_libconfig_setting_lookup_int(startup_pools_setting, "num",
					      &num_startup_pools);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Option 'startup_pools.num' not found\n");
		return -1;
	}

	if (num_startup_pools <= 0 || num_startup_pools > EM_CONFIG_POOLS - 1) {
		EM_LOG(EM_LOG_ERR,
		       "Number of startup_pools %d is too large or too small\n"
		       "Valid value range is [1, %d]\n",
		       num_startup_pools, EM_CONFIG_POOLS - 1);
		return -1;
	}

	conf_list = em_libconfig_setting_get_list(startup_pools_setting, "conf");
	if (!conf_list) {
		EM_LOG(EM_LOG_ERR, "Conf option 'startup_pools.conf' not found\n");
		return -1;
	}

	list_len = em_libconfig_list_length(conf_list);
	if (list_len != num_startup_pools) {
		EM_LOG(EM_LOG_ERR,
		       "The number of pool configuration(s) given in\n"
		       "'startup_pools.conf':%d does not match number of\n"
		       "startup_pools specified in 'startup_pools.num': %d\n",
		       list_len, num_startup_pools);
		return -1;
	}

	for (int i = 0; i < list_len; i++) {
		if (read_config_startup_pools_conf(conf_list, i) < 0)
			return -1;
	}

	em_shm->opt.startup_pools.num = num_startup_pools;

	print_config_startup_pools();
	return 0;
}

/* Read option: pool from the EM config file */
static int read_config_pool(void)
{
	const char *conf_str;
	bool val_bool = false;
	int val = 0;
	int ret;

	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;

	EM_PRINT("EM-pool config:\n");

	/*
	 * Option: pool.statistics.available
	 */
	conf_str = "pool.statistics.available";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.available = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.alloc_ops
	 */
	conf_str = "pool.statistics.alloc_ops";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.alloc_ops = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.alloc_fails
	 */
	conf_str = "pool.statistics.alloc_fails";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.alloc_fails = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.free_ops
	 */
	conf_str = "pool.statistics.free_ops";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.free_ops = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.total_ops
	 */
	conf_str = "pool.statistics.total_ops";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.total_ops = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.cache_available
	 */
	conf_str = "pool.statistics.cache_available";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.cache_available = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.cache_alloc_ops
	 */
	conf_str = "pool.statistics.cache_alloc_ops";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.cache_alloc_ops = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.cache_free_ops
	 */
	conf_str = "pool.statistics.cache_free_ops";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.cache_free_ops = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	/*
	 * Option: pool.statistics.core_cache_available
	 */
	conf_str = "pool.statistics.core_cache_available";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	em_shm->opt.pool.statistics.core_cache_available = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

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

static int
read_config_file(void)
{
	/* Option: pool */
	if (read_config_pool() < 0)
		return -1;

	/* Option: startup_pools */
	if (read_config_startup_pools() < 0)
		return -1;

	return 0;
}

/* We use following static asserts and function check_em_pool_subpool_stats()
 * to verify at both compile time and runtime that, em_pool_subpool_stats_t is
 * exactly the same as odp_pool_stats_t except the last struct member, namely,
 * 'em_pool_subpool_stats_t::__internal_use', whose size must also be bigger
 * than that of 'odp_pool_stats_t::thread'. This allows us to avoid exposing ODP
 * type in EM-ODP API (at event_machine_pool.h in this case) and allows us to
 * type cast 'em_pool_subpool_stats_t' to 'odp_pool_stats_t', ensuring high
 * performance (see em_pool_stats() and em_pool_subpool_stats()).
 */

ODP_STATIC_ASSERT(sizeof(odp_pool_stats_t) <= sizeof(em_pool_subpool_stats_t),
		  "Size of odp_pool_stats_t must be smaller than that of em_pool_subpool_stats_t");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, available) ==
		  offsetof(em_pool_subpool_stats_t, available) &&
		  sizeof_field(odp_pool_stats_t, available) ==
		  sizeof_field(em_pool_subpool_stats_t, available),
		  "em_pool_subpool_stats_t.available differs from odp_pool_stats_t.available!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, alloc_ops) ==
		  offsetof(em_pool_subpool_stats_t, alloc_ops) &&
		  sizeof_field(odp_pool_stats_t, alloc_ops) ==
		  sizeof_field(em_pool_subpool_stats_t, alloc_ops),
		  "em_pool_subpool_stats_t.alloc_ops differs from odp_pool_stats_t.alloc_ops!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, alloc_fails) ==
		  offsetof(em_pool_subpool_stats_t, alloc_fails) &&
		  sizeof_field(odp_pool_stats_t, alloc_fails) ==
		  sizeof_field(em_pool_subpool_stats_t, alloc_fails),
		  "em_pool_subpool_stats_t.alloc_fails differs from odp_pool_stats_t.alloc_fails!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, free_ops) ==
		  offsetof(em_pool_subpool_stats_t, free_ops) &&
		  sizeof_field(odp_pool_stats_t, free_ops) ==
		  sizeof_field(em_pool_subpool_stats_t, free_ops),
		  "em_pool_subpool_stats_t.free_ops differs from odp_pool_stats_t.free_ops!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, total_ops) ==
		  offsetof(em_pool_subpool_stats_t, total_ops) &&
		  sizeof_field(odp_pool_stats_t, total_ops) ==
		  sizeof_field(em_pool_subpool_stats_t, total_ops),
		  "em_pool_subpool_stats_t.total_ops differs from odp_pool_stats_t.total_ops!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, cache_available) ==
		  offsetof(em_pool_subpool_stats_t, cache_available) &&
		  sizeof_field(odp_pool_stats_t, cache_available) ==
		  sizeof_field(em_pool_subpool_stats_t, cache_available),
		  "em_pool_subpool_stats_t.cache_available differs from that of odp_pool_stats_t!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, cache_alloc_ops) ==
		  offsetof(em_pool_subpool_stats_t, cache_alloc_ops) &&
		  sizeof_field(odp_pool_stats_t, cache_alloc_ops) ==
		  sizeof_field(em_pool_subpool_stats_t, cache_alloc_ops),
		  "em_pool_subpool_stats_t.cache_alloc_ops differs from that of odp_pool_stats_t!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, cache_free_ops) ==
		  offsetof(em_pool_subpool_stats_t, cache_free_ops) &&
		  sizeof_field(odp_pool_stats_t, cache_free_ops) ==
		  sizeof_field(em_pool_subpool_stats_t, cache_free_ops),
		  "em_pool_subpool_stats_t.cache_free_ops differs from that of odp_pool_stats_t!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_t, thread) ==
		  offsetof(em_pool_subpool_stats_t, __internal_use) &&
		  sizeof_field(odp_pool_stats_t, thread) <=
		  sizeof_field(em_pool_subpool_stats_t, __internal_use),
		  "em_pool_subpool_stats_t.__internal_use differs from odp_pool_stats_t.thread");

#define STRUCT_ERR_STR \
"em_pool_subpool_stats_t.%s differs from odp_pool_stats_t.%s either in size or in offset!\n"

static int check_em_pool_subpool_stats(void)
{
	if (sizeof(odp_pool_stats_t) > sizeof(em_pool_subpool_stats_t)) {
		EM_LOG(EM_LOG_ERR,
		       "Size of odp_pool_stats_t bigger than that of em_pool_subpool_stats_t\n");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, available) !=
	    offsetof(em_pool_subpool_stats_t, available) ||
	    sizeof_field(odp_pool_stats_t, available) !=
	    sizeof_field(em_pool_subpool_stats_t, available)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "available", "available");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, alloc_ops) !=
	    offsetof(em_pool_subpool_stats_t, alloc_ops) ||
	    sizeof_field(odp_pool_stats_t, alloc_ops) !=
	    sizeof_field(em_pool_subpool_stats_t, alloc_ops)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "alloc_ops", "alloc_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, alloc_fails) !=
	    offsetof(em_pool_subpool_stats_t, alloc_fails) ||
	    sizeof_field(odp_pool_stats_t, alloc_fails) !=
	    sizeof_field(em_pool_subpool_stats_t, alloc_fails)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "alloc_fails", "alloc_fails");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, free_ops) !=
	    offsetof(em_pool_subpool_stats_t, free_ops) ||
	    sizeof_field(odp_pool_stats_t, free_ops) !=
	    sizeof_field(em_pool_subpool_stats_t, free_ops)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "free_ops", "free_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, total_ops) !=
	    offsetof(em_pool_subpool_stats_t, total_ops) ||
	    sizeof_field(odp_pool_stats_t, total_ops) !=
	    sizeof_field(em_pool_subpool_stats_t, total_ops)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "total_ops", "total_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, cache_available) !=
	    offsetof(em_pool_subpool_stats_t, cache_available) ||
	    sizeof_field(odp_pool_stats_t, cache_available) !=
	    sizeof_field(em_pool_subpool_stats_t, cache_available)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "cache_available", "cache_available");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, cache_alloc_ops) !=
	    offsetof(em_pool_subpool_stats_t, cache_alloc_ops) ||
	    sizeof_field(odp_pool_stats_t, cache_alloc_ops) !=
	    sizeof_field(em_pool_subpool_stats_t, cache_alloc_ops)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "cache_alloc_ops", "cache_alloc_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, cache_free_ops) !=
	    offsetof(em_pool_subpool_stats_t, cache_free_ops) ||
	    sizeof_field(odp_pool_stats_t, cache_free_ops) !=
	    sizeof_field(em_pool_subpool_stats_t, cache_free_ops)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "cache_free_ops", "cache_free_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_t, thread) !=
	    offsetof(em_pool_subpool_stats_t, __internal_use) ||
	    sizeof_field(odp_pool_stats_t, thread) >
	    sizeof_field(em_pool_subpool_stats_t, __internal_use)) {
		EM_LOG(EM_LOG_ERR, STRUCT_ERR_STR, "__internal_use", "thread");
		return -1;
	}

	return 0;
}

/* We use following static asserts and function check_em_pool_subpool_stats_selected()
 * to verify at both compile time and runtime that, em_pool_subpool_stats_selected_t
 * is exactly the same as odp_pool_stats_selected_t This allows us to avoid exposing
 * ODP type in EM-ODP API (at event_machine_pool.h in this case) and allows us to
 * type cast 'em_pool_subpool_stats_selected_t' to 'odp_pool_stats_selected_t', ensuring
 * high performance (see em_pool_stats_selected() and em_pool_subpool_stats_selected()).
 */

#define SIZE_NOT_EQUAL_ERR_STR \
"Size of odp_pool_stats_selected_t must equal to that of em_pool_subpool_stats_selected_t\n"

ODP_STATIC_ASSERT(sizeof(odp_pool_stats_selected_t) == sizeof(em_pool_subpool_stats_selected_t),
		  SIZE_NOT_EQUAL_ERR_STR);

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, available) ==
		  offsetof(em_pool_subpool_stats_selected_t, available) &&
		  sizeof_field(odp_pool_stats_selected_t, available) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, available),
		  "available in em_pool_subpool_stats_selected_t and odp_pool_stats_selected_t differs!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, alloc_ops) ==
		  offsetof(em_pool_subpool_stats_selected_t, alloc_ops) &&
		  sizeof_field(odp_pool_stats_selected_t, alloc_ops) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, alloc_ops),
		  "em_pool_subpool_stats_selected_t.alloc_ops differs from odp_pool_stats_selected_t.alloc_ops!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, alloc_fails) ==
		  offsetof(em_pool_subpool_stats_t, alloc_fails) &&
		  sizeof_field(odp_pool_stats_selected_t, alloc_fails) ==
		  sizeof_field(em_pool_subpool_stats_t, alloc_fails),
		  "em_pool_subpool_stats_selected_t.alloc_fails differs from odp_pool_stats_selected_t.alloc_fails!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, free_ops) ==
		  offsetof(em_pool_subpool_stats_selected_t, free_ops) &&
		  sizeof_field(odp_pool_stats_selected_t, free_ops) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, free_ops),
		  "em_pool_subpool_stats_selected_t.free_ops differs from odp_pool_stats_selected_t.free_ops!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, total_ops) ==
		  offsetof(em_pool_subpool_stats_selected_t, total_ops) &&
		  sizeof_field(odp_pool_stats_selected_t, total_ops) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, total_ops),
		  "em_pool_subpool_stats_selected_t.total_ops differs from odp_pool_stats_selected_t.total_ops!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, cache_available) ==
		  offsetof(em_pool_subpool_stats_selected_t, cache_available) &&
		  sizeof_field(odp_pool_stats_selected_t, cache_available) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, cache_available),
		  "em_pool_subpool_stats_selected_t.cache_available differs from that of odp_pool_stats_selected_t!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, cache_alloc_ops) ==
		  offsetof(em_pool_subpool_stats_selected_t, cache_alloc_ops) &&
		  sizeof_field(odp_pool_stats_selected_t, cache_alloc_ops) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, cache_alloc_ops),
		  "em_pool_subpool_stats_selected_t.cache_alloc_ops differs from that of odp_pool_stats_selected_t!");

ODP_STATIC_ASSERT(offsetof(odp_pool_stats_selected_t, cache_free_ops) ==
		  offsetof(em_pool_subpool_stats_selected_t, cache_free_ops) &&
		  sizeof_field(odp_pool_stats_selected_t, cache_free_ops) ==
		  sizeof_field(em_pool_subpool_stats_selected_t, cache_free_ops),
		  "em_pool_subpool_stats_selected_t.cache_free_ops differs from that of odp_pool_stats_selected_t!");

#define SELECTED_TYPE_ERR_FMT \
"em_pool_subpool_stats_selected_t.%s differs from odp_pool_stats_selected_t.%s\n"

static int check_em_pool_subpool_stats_selected(void)
{
	if (sizeof(odp_pool_stats_selected_t) != sizeof(em_pool_subpool_stats_selected_t)) {
		EM_LOG(EM_LOG_ERR,
		       "odp_pool_stats_selected_t vs em_pool_subpool_stats_selected_t size diff\n");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, available) !=
	    offsetof(em_pool_subpool_stats_selected_t, available) ||
	    sizeof_field(odp_pool_stats_selected_t, available) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, available)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "available", "available");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, alloc_ops) !=
	    offsetof(em_pool_subpool_stats_selected_t, alloc_ops) ||
	    sizeof_field(odp_pool_stats_selected_t, alloc_ops) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, alloc_ops)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "alloc_ops", "alloc_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, alloc_fails) !=
	    offsetof(em_pool_subpool_stats_selected_t, alloc_fails) ||
	    sizeof_field(odp_pool_stats_selected_t, alloc_fails) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, alloc_fails)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "alloc_fails", "alloc_fails");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, free_ops) !=
	    offsetof(em_pool_subpool_stats_selected_t, free_ops) ||
	    sizeof_field(odp_pool_stats_selected_t, free_ops) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, free_ops)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "free_ops", "free_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, total_ops) !=
	    offsetof(em_pool_subpool_stats_selected_t, total_ops) ||
	    sizeof_field(odp_pool_stats_selected_t, total_ops) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, total_ops)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "total_ops", "total_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, cache_available) !=
	    offsetof(em_pool_subpool_stats_selected_t, cache_available) ||
	    sizeof_field(odp_pool_stats_selected_t, cache_available) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, cache_available)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "cache_available", "cache_available");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, cache_alloc_ops) !=
	    offsetof(em_pool_subpool_stats_selected_t, cache_alloc_ops) ||
	    sizeof_field(odp_pool_stats_selected_t, cache_alloc_ops) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, cache_alloc_ops)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "cache_alloc_ops", "cache_alloc_ops");
		return -1;
	}

	if (offsetof(odp_pool_stats_selected_t, cache_free_ops) !=
	    offsetof(em_pool_subpool_stats_selected_t, cache_free_ops) ||
	    sizeof_field(odp_pool_stats_selected_t, cache_free_ops) !=
	    sizeof_field(em_pool_subpool_stats_selected_t, cache_free_ops)) {
		EM_LOG(EM_LOG_ERR, SELECTED_TYPE_ERR_FMT, "cache_free_ops", "cache_free_ops");
		return -1;
	}

	return 0;
}

ODP_STATIC_ASSERT(sizeof(odp_pool_stats_opt_t) == sizeof(em_pool_stats_opt_t),
		  "Size of odp_pool_stats_opt_t differs from that of em_pool_stats_opt_t\n");

em_status_t
pool_init(mpool_tbl_t *const mpool_tbl, mpool_pool_t *const mpool_pool,
	  const em_pool_cfg_t *default_pool_cfg)
{
	int ret;
	em_pool_t pool;
	em_pool_t pool_default;
	startup_pool_conf_t *startup_pool_conf;
	bool default_pool_set = false;
	const int cores = em_core_count();

	/* Return error if em_pool_subpool_stats_t differs from odp_pool_stats_t */
	if (check_em_pool_subpool_stats())
		return EM_ERR;

	/*Return error if em_pool_subpool_stats_selected_t differs from odp_pool_stats_selected_t*/
	if (check_em_pool_subpool_stats_selected())
		return EM_ERR;

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

	/* Read EM-pool and EM-startup_pools related runtime config options */
	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	/*
	 * Create default and startup pools.
	 *
	 * If default pool configuration is given through 'startup_pools.conf'
	 * in em-odp.conf, use that instead. Otherwise use default_pool_cfg.
	 *
	 * Allocate/reserve default pool first here so when creating startup
	 * pools whose configuration does not provide pool handle, default pool
	 * handle EM_POOL_DEFAULT(1) won't be allocated to them.
	 */
	pool_default = pool_alloc(EM_POOL_DEFAULT);

	if (unlikely(pool_default == EM_POOL_UNDEF ||
		     pool_default != EM_POOL_DEFAULT))
		return EM_ERR_ALLOC_FAILED;

	/* Create startup pools whose configuration is provided by the EM config file */
	for (uint32_t i = 0; i < em_shm->opt.startup_pools.num; i++) {
		startup_pool_conf = &em_shm->opt.startup_pools.conf[i];

		/* Default pool is provided by the EM config file */
		if (strstr(startup_pool_conf->name, EM_POOL_DEFAULT_NAME) ||
		    startup_pool_conf->pool == EM_POOL_DEFAULT) {
			default_pool_set = true;
			pool_free(EM_POOL_DEFAULT);
			pool = em_pool_create(EM_POOL_DEFAULT_NAME,
					      EM_POOL_DEFAULT,
					      &startup_pool_conf->cfg);
		} else {
			pool = em_pool_create(startup_pool_conf->name,
					      startup_pool_conf->pool,
					      &startup_pool_conf->cfg);
		}

		if (pool == EM_POOL_UNDEF)
			return EM_ERR_ALLOC_FAILED;
	}

	/* Create the default pool if it is not provided by the EM config file */
	if (!default_pool_set) {
		pool_free(EM_POOL_DEFAULT);
		pool = em_pool_create(EM_POOL_DEFAULT_NAME, EM_POOL_DEFAULT,
				      default_pool_cfg);
		if (pool == EM_POOL_UNDEF || pool != EM_POOL_DEFAULT)
			return EM_ERR_ALLOC_FAILED;
	}

	return EM_OK;
}

em_status_t
pool_term(const mpool_tbl_t *mpool_tbl)
{
	em_status_t stat = EM_OK;

	(void)mpool_tbl;

	EM_PRINT("\n"
		 "Status before delete:\n");
	em_pool_info_print_all();

	for (int i = 0; i < EM_CONFIG_POOLS; i++) {
		em_pool_t pool = pool_idx2hdl(i);
		const mpool_elem_t *mpool_elem = pool_elem_get(pool);
		em_status_t ret;

		if (mpool_elem && pool_allocated(mpool_elem)) {
			ret = pool_delete(pool);
			if (ret != EM_OK)
				stat = ret; /* save last error as return val */
		}
	}

	return stat;
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
	else if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET)
		min_cache_size = capa->pkt.min_cache_size;
	else if (pool_cfg->event_type == EM_EVENT_TYPE_VECTOR)
		min_cache_size = capa->vector.min_cache_size;
	else
		return -9;

	for (int i = 0; i < pool_cfg->num_subpools; i++) {
		if (pool_cfg->subpool[i].size <= 0 ||
		    pool_cfg->subpool[i].num <= 0) {
			*err_str = "Invalid subpool size/num";
			return -(1 * 10 + i); /* -10, -11, ... */
		}

		cache_size = pool_cfg->subpool[i].cache_size;
		if (unlikely(cache_size < min_cache_size)) {
			*err_str = "Requested cache size too small";
			return -(2 * 10 + i); /* -20, -21, ... */
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
	int ret = 0;
	const odp_pool_capability_t *capa = &em_shm->mpool_tbl.odp_pool_capability;

	if (!pool_cfg) {
		*err_str = "Pool config NULL";
		return -1;
	}
	if (pool_cfg->__internal_check != EM_CHECK_INIT_CALLED) {
		*err_str = "Pool config not initialized";
		return -1;
	}

	if (pool_cfg->num_subpools <= 0 ||
	    pool_cfg->num_subpools > EM_MAX_SUBPOOLS) {
		*err_str = "Invalid number of subpools";
		return -1;
	}

	ret = is_pool_type_supported(pool_cfg->event_type, err_str/*out*/);
	if (ret)
		return ret;

	if (!is_align_offset_valid(pool_cfg)) {
		*err_str = "Invalid align offset";
		return -1;
	}

	ret = is_user_area_valid(pool_cfg, capa, err_str/*out*/);
	if (ret)
		return ret;

	if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET &&
	    pool_cfg->pkt.headroom.in_use &&
	    pool_cfg->pkt.headroom.value > capa->pkt.max_headroom) {
		*err_str = "Requested pkt headroom size too large";
		return -1;
	}

	ret = invalid_pool_cache_cfg(pool_cfg, err_str/*out*/);

	return ret; /* 0: success, <0: error */
}

/*
 * Helper to pool_create() - preallocate all events in the pool for ESV to
 * maintain event state over multiple alloc- and free-operations.
 */
static void
pool_prealloc(const mpool_elem_t *pool_elem)
{
	event_prealloc_hdr_t *prealloc_hdr = NULL;
	uint64_t num_tot = 0;
	uint64_t num = 0;
	uint64_t num_free = 0;
	const uint32_t size = pool_elem->pool_cfg.subpool[0].size;
	list_node_t evlist;
	list_node_t *node;

	list_init(&evlist);

	for (int i = 0; i < pool_elem->num_subpools; i++)
		num_tot += pool_elem->pool_cfg.subpool[i].num;

	do {
		prealloc_hdr = event_prealloc(pool_elem, size);
		if (likely(prealloc_hdr)) {
			list_add(&evlist, &prealloc_hdr->list_node);
			num++;
		}
	} while (prealloc_hdr);

	if (unlikely(num < num_tot))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_TOO_SMALL),
			       EM_ESCOPE_POOL_CREATE,
			       "alloc: events expected:%" PRIu64 " actual:%" PRIu64 "",
			       num_tot, num);

	while (!list_is_empty(&evlist)) {
		node = list_rem_first(&evlist);
		prealloc_hdr = list_node_to_prealloc_hdr(node);
		em_free(prealloc_hdr->ev_hdr.event);
		num_free++;
	}

	if (unlikely(num_free > num))
		INTERNAL_ERROR(EM_FATAL(EM_ERR_TOO_LARGE),
			       EM_ESCOPE_POOL_CREATE,
			       "free: events expected:%" PRIu64 " actual:%" PRIu64 "",
			       num, num_free);
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
	else if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET)
		max_cache_size = capa->pkt.max_cache_size;
	else /* EM_EVENT_TYPE_VECTOR */
		max_cache_size = capa->vector.max_cache_size;

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
set_uarea_size(const em_pool_cfg_t *pool_cfg, size_t *uarea_size/*out*/)
{
	size_t size = 0;
	size_t max_size = 0;
	const odp_pool_capability_t *capa =
		&em_shm->mpool_tbl.odp_pool_capability;

	if (pool_cfg->user_area.in_use) /* use pool-cfg */
		size = pool_cfg->user_area.size;
	else /* use cfg-file */
		size = em_shm->opt.pool.user_area_size;

	if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET)
		max_size = MIN(capa->pkt.max_uarea_size, EM_EVENT_USER_AREA_MAX_SIZE);
	else if (pool_cfg->event_type == EM_EVENT_TYPE_VECTOR)
		max_size = MIN(capa->vector.max_uarea_size, EM_EVENT_USER_AREA_MAX_SIZE);
	else if (size > 0) /* EM_EVENT_TYPE_SW: bufs */
		max_size = MIN(capa->buf.max_uarea_size, EM_EVENT_USER_AREA_MAX_SIZE);

	if (size > max_size)
		return -1;

	*uarea_size = size;

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
static void set_pool_params_stats(odp_pool_stats_opt_t *param_stats /*out*/,
				  const odp_pool_stats_opt_t *capa_stats,
				  const em_pool_stats_opt_t *stats_opt)
{
	param_stats->all = 0;

	if (capa_stats->bit.available)
		param_stats->bit.available = stats_opt->available;

	if (capa_stats->bit.alloc_ops)
		param_stats->bit.alloc_ops = stats_opt->alloc_ops;

	if (capa_stats->bit.alloc_fails)
		param_stats->bit.alloc_fails = stats_opt->alloc_fails;

	if (capa_stats->bit.free_ops)
		param_stats->bit.free_ops = stats_opt->free_ops;

	if (capa_stats->bit.total_ops)
		param_stats->bit.total_ops = stats_opt->total_ops;

	if (capa_stats->bit.cache_alloc_ops)
		param_stats->bit.cache_alloc_ops = stats_opt->cache_alloc_ops;

	if (capa_stats->bit.cache_available)
		param_stats->bit.cache_available = stats_opt->cache_available;

	if (capa_stats->bit.cache_free_ops)
		param_stats->bit.cache_free_ops = stats_opt->cache_free_ops;

	if (capa_stats->bit.thread_cache_available)
		param_stats->bit.thread_cache_available = stats_opt->core_cache_available;
}

/** Helper to create_subpools() */
static void set_pool_params_pkt(odp_pool_param_t *pool_params /* out */,
				const em_pool_cfg_t *pool_cfg,
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
	if (pool_cfg->stats_opt.in_use) {
		set_pool_params_stats(&pool_params->stats, &capa->pkt.stats,
				      &pool_cfg->stats_opt.opt);
	} else {
		set_pool_params_stats(&pool_params->stats, &capa->pkt.stats,
				      &em_shm->opt.pool.statistics);/*from cnf file*/
	}
}

static void set_pool_params_vector(odp_pool_param_t *pool_params /* out */,
				   const em_pool_cfg_t *pool_cfg,
				   uint32_t size, uint32_t num,
				   uint32_t cache_size, uint32_t uarea_size)
{
	const odp_pool_capability_t *capa = &em_shm->mpool_tbl.odp_pool_capability;

	odp_pool_param_init(pool_params);

	pool_params->type = ODP_POOL_VECTOR;
	pool_params->vector.num = num;
	pool_params->vector.max_size = size;
	/* Reserve space for the EM event header in the vector's ODP-user-area */
	pool_params->vector.uarea_size = sizeof(event_hdr_t) + uarea_size;
	pool_params->vector.cache_size = cache_size;

	/* Vector pool statistics */
	if (pool_cfg->stats_opt.in_use)
		set_pool_params_stats(&pool_params->stats, &capa->vector.stats,
				      &pool_cfg->stats_opt.opt);
	else
		set_pool_params_stats(&pool_params->stats, &capa->vector.stats,
				      &em_shm->opt.pool.statistics);
}

/** Helper to create_subpools() */
static void set_pool_params_buf(odp_pool_param_t *pool_params /* out */,
				const em_pool_cfg_t *pool_cfg,
				uint32_t size, uint32_t num, uint32_t cache_size,
				uint32_t align_offset, uint32_t odp_align,
				uint32_t uarea_size)
{
	const odp_pool_capability_t *capa = &em_shm->mpool_tbl.odp_pool_capability;

	odp_pool_param_init(pool_params);

	pool_params->type = ODP_POOL_BUFFER;
	pool_params->buf.num = num;
	pool_params->buf.size = size;
	if (align_offset)
		pool_params->buf.size += 32 - align_offset;
	pool_params->buf.align = odp_align;
	pool_params->buf.uarea_size = sizeof(event_hdr_t) + uarea_size;
	pool_params->buf.cache_size = cache_size;

	/* Buf pool statistics */
	if (pool_cfg->stats_opt.in_use)
		set_pool_params_stats(&pool_params->stats, &capa->buf.stats,
				      &pool_cfg->stats_opt.opt);
	else
		set_pool_params_stats(&pool_params->stats, &capa->buf.stats,
				      &em_shm->opt.pool.statistics);
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
			set_pool_params_pkt(&pool_params /* out */, pool_cfg,
					    size, num, cache_size,
					    align_offset, odp_align,
					    uarea_size, pkt_headroom);
		} else if (pool_cfg->event_type == EM_EVENT_TYPE_VECTOR) {
			set_pool_params_vector(&pool_params /* out */, pool_cfg,
					       size, num, cache_size,
					       uarea_size);
		} else { /* pool_cfg->event_type == EM_EVENT_TYPE_SW */
			set_pool_params_buf(&pool_params /* out */, pool_cfg,
					    size, num, cache_size,
					    align_offset, odp_align, uarea_size);
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
		mpool_elem->stats_opt = pool_params.stats;

		/* odp_pool_print(odp_pool); */
	}

	return 0;
}

em_pool_t
pool_create(const char *name, em_pool_t req_pool, const em_pool_cfg_t *pool_cfg)
{
	const em_event_type_t pool_evtype = pool_cfg->event_type;
	int err = 0;

	/* Allocate a free EM pool */
	const em_pool_t pool = pool_alloc(req_pool/* requested or undef*/);

	if (unlikely(pool == EM_POOL_UNDEF))
		return EM_POOL_UNDEF;

	mpool_elem_t *mpool_elem = pool_elem_get(pool);

	/* Sanity check */
	if (!mpool_elem || mpool_elem->em_pool != pool)
		return EM_POOL_UNDEF;

	mpool_elem->event_type = pool_evtype;
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

	/* align only valid for bufs and pkts */
	if (pool_evtype == EM_EVENT_TYPE_SW ||
	    pool_evtype == EM_EVENT_TYPE_PACKET) {
		err = set_align(&sorted_cfg, &align_offset/*out*/,
				&odp_align/*out*/);
		if (unlikely(err)) {
			INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_POOL_CREATE,
				       "EM-pool:\"%s\" align mismatch:\n"
				       "align:%u cfg:align_offset:%u",
				       name, odp_align, align_offset);
			goto error;
		}
	}
	/* store the align offset, needed in pkt-alloc */
	mpool_elem->align_offset = align_offset;

	/*
	 * Event user area size.
	 * Pool-specific param overrides config file 'user_area_size' value
	 */
	size_t uarea_size = 0;

	err = set_uarea_size(&sorted_cfg, &uarea_size/*out*/);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_POOL_CREATE,
			       "EM-pool:\"%s\" invalid uarea config: req.size:%zu",
			       name, uarea_size);
		goto error;
	}

	/* store the user_area sizes, needed in alloc */
	mpool_elem->user_area.size = uarea_size & UINT16_MAX;

	/*
	 * Set the headroom for events in EM packet pools
	 */
	uint32_t pkt_headroom = 0;
	uint32_t max_headroom = 0;

	if (pool_evtype == EM_EVENT_TYPE_PACKET) {
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
			      (uint32_t)uarea_size, pkt_headroom, mpool_elem /*out*/);
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

	if (unlikely(mpool_elem == NULL || !pool_allocated(mpool_elem)))
		return EM_ERR_BAD_ARG;

	for (int i = 0; i < mpool_elem->num_subpools; i++) {
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

	if (pool_info.event_type == EM_EVENT_TYPE_VECTOR)
		pool_type = "vec";
	else if (pool_info.event_type == EM_EVENT_TYPE_PACKET)
		pool_type = "pkt";
	else
		pool_type = "buf";

	EM_PRINT("  %-6" PRI_POOL " %-16s %4s   %02u    %02zu    %02u   ",
		 pool, pool_info.name, pool_type,
		 pool_info.align_offset, pool_info.user_area_size,
		 pool_info.num_subpools);

	for (int i = 0; i < pool_info.num_subpools; i++) {
		char subpool_str[42];

		if (pool_info.subpool[i].used || pool_info.subpool[i].free) {
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

#define POOL_STATS_HDR_STR \
"EM pool statistics for pool %" PRI_POOL ":\n\n"\
"Subpool Available Alloc_ops Alloc_fails Free_ops Total_ops Cache_available" \
" Cache_alloc_ops Cache_free_ops\n"\
"--------------------------------------------------------------------------" \
"-------------------------------\n%s"

#define POOL_STATS_LEN 107
#define POOL_STATS_FMT "%-8u%-10lu%-10lu%-12lu%-9lu%-10lu%-16lu%-16lu%-15lu\n"

void pool_stats_print(em_pool_t pool)
{
	em_status_t stat;
	em_pool_stats_t pool_stats;
	const em_pool_subpool_stats_t *subpool_stats;
	int len = 0;
	int n_print = 0;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);
	const int stats_str_len = EM_MAX_SUBPOOLS * POOL_STATS_LEN + 1;
	char stats_str[stats_str_len];

	if (pool_elem == NULL || !pool_allocated(pool_elem)) {
		EM_LOG(EM_LOG_ERR, "EM-pool:%" PRI_POOL " invalid\n", pool);
		return;
	}

	stat = em_pool_stats(pool, &pool_stats);
	if (unlikely(stat != EM_OK)) {
		EM_PRINT("Failed to fetch EM pool statistics\n");
		return;
	}

	for (uint32_t i = 0; i < pool_stats.num_subpools; i++) {
		subpool_stats = &pool_stats.subpool_stats[i];
		n_print = snprintf(stats_str + len, stats_str_len - len,
				   POOL_STATS_FMT,
				   i, subpool_stats->available,
				   subpool_stats->alloc_ops,
				   subpool_stats->alloc_fails,
				   subpool_stats->free_ops,
				   subpool_stats->total_ops,
				   subpool_stats->cache_available,
				   subpool_stats->cache_alloc_ops,
				   subpool_stats->cache_free_ops);

		/* Not enough space to hold more subpool stats */
		if (n_print >= stats_str_len - len)
			break;

		len += n_print;
	}

	stats_str[len] = '\0';
	EM_PRINT(POOL_STATS_HDR_STR, pool, stats_str);
}

#define POOL_STATS_SELECTED_HDR_STR \
"Selected EM pool statistics for pool %" PRI_POOL ":\n\n"\
"Selected statistic counters: %s\n\n"\
"Subpool Available Alloc_ops Alloc_fails Free_ops Total_ops Cache_available" \
" Cache_alloc_ops Cache_free_ops\n"\
"--------------------------------------------------------------------------" \
"-------------------------------\n%s"

#define OPT_STR_LEN 150

static void fill_opt_str(char *opt_str, const em_pool_stats_opt_t *opt)
{
	int n_print;
	int len = 0;

	if (opt->available) {
		n_print = snprintf(opt_str + len, 12, "%s", "available");
		len += n_print;
	}

	if (opt->alloc_ops) {
		n_print = snprintf(opt_str + len, 12, "%s", len ? ", alloc_ops" : "alloc_ops");
		len += n_print;
	}

	if (opt->alloc_fails) {
		n_print = snprintf(opt_str + len, 14, "%s", len ? ", alloc_fails" : "alloc_fails");
		len += n_print;
	}

	if (opt->free_ops) {
		n_print = snprintf(opt_str + len, 11, "%s", len ? ", free_ops" : "free_ops");
		len += n_print;
	}

	if (opt->total_ops) {
		n_print = snprintf(opt_str + len, 12, "%s", len ? ", total_ops" : "total_ops");
		len += n_print;
	}

	if (opt->cache_available) {
		n_print = snprintf(opt_str + len, 18, "%s",
				   len ? ", cache_available" : "cache_available");
		len += n_print;
	}

	if (opt->cache_alloc_ops) {
		n_print = snprintf(opt_str + len, 18, "%s",
				   len ? ", cache_alloc_ops" : "cache_alloc_ops");
		len += n_print;
	}

	if (opt->cache_free_ops)
		snprintf(opt_str + len, 17, "%s", len ? ", cache_free_ops" : "cache_free_ops");
}

void pool_stats_selected_print(em_pool_t pool, const em_pool_stats_opt_t *opt)
{
	em_status_t stat;
	em_pool_stats_selected_t pool_stats = {0};
	const em_pool_subpool_stats_selected_t *subpool_stats;
	int len = 0;
	int n_print = 0;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);
	char opt_str[OPT_STR_LEN];
	const int stats_str_len = EM_MAX_SUBPOOLS * POOL_STATS_LEN + 1;
	char stats_str[stats_str_len];

	if (pool_elem == NULL || !pool_allocated(pool_elem)) {
		EM_LOG(EM_LOG_ERR, "EM-pool:%" PRI_POOL " invalid\n", pool);
		return;
	}

	stat = em_pool_stats_selected(pool, &pool_stats, opt);
	if (unlikely(stat != EM_OK)) {
		EM_PRINT("Failed to fetch EM selected pool statistics\n");
		return;
	}

	for (uint32_t i = 0; i < pool_stats.num_subpools; i++) {
		subpool_stats = &pool_stats.subpool_stats[i];

		n_print = snprintf(stats_str + len, stats_str_len - len,
				   POOL_STATS_FMT,
				   i,
				   subpool_stats->available,
				   subpool_stats->alloc_ops,
				   subpool_stats->alloc_fails,
				   subpool_stats->free_ops,
				   subpool_stats->total_ops,
				   subpool_stats->cache_available,
				   subpool_stats->cache_alloc_ops,
				   subpool_stats->cache_free_ops);

		/* Not enough space to hold more subpool stats */
		if (n_print >= stats_str_len - len)
			break;

		len += n_print;
	}
	stats_str[len] = '\0';

	/* Fill selected statistic counters */
	fill_opt_str(opt_str, opt);

	EM_PRINT(POOL_STATS_SELECTED_HDR_STR, pool, opt_str, stats_str);
}

#define SUBPOOL_STATS_HDR_STR \
"EM subpool statistics for pool %" PRI_POOL ":\n\n"\
"Subpool Available Alloc_ops Alloc_fails Free_ops Total_ops Cache_available" \
" Cache_alloc_ops Cache_free_ops\n"\
"--------------------------------------------------------------------------" \
"-------------------------------\n%s"

void subpools_stats_print(em_pool_t pool, const int subpools[], int num_subpools)
{
	int num_stats;
	em_pool_subpool_stats_t stats[num_subpools];
	int len = 0;
	int n_print = 0;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);
	const int stats_str_len = num_subpools * POOL_STATS_LEN + 1;
	char stats_str[stats_str_len];

	if (pool_elem == NULL || !pool_allocated(pool_elem)) {
		EM_LOG(EM_LOG_ERR, "EM-pool:%" PRI_POOL " invalid\n", pool);
		return;
	}

	num_stats = em_pool_subpool_stats(pool, subpools, num_subpools, stats);
	if (unlikely(!num_stats || num_stats > num_subpools)) {
		EM_LOG(EM_LOG_ERR, "Failed to fetch subpool statistics\n");
		return;
	}

	/* Print subpool stats */
	for (int i = 0; i < num_stats; i++) {
		n_print = snprintf(stats_str + len, stats_str_len - len,
				   POOL_STATS_FMT,
				   subpools[i], stats[i].available, stats[i].alloc_ops,
				   stats[i].alloc_fails, stats[i].free_ops,
				   stats[i].total_ops, stats[i].cache_available,
				   stats[i].cache_alloc_ops, stats[i].cache_free_ops);

		/* Not enough space to hold more subpool stats */
		if (n_print >= stats_str_len - len)
			break;

		len += n_print;
	}

	stats_str[len] = '\0';
	EM_PRINT(SUBPOOL_STATS_HDR_STR, pool, stats_str);
}

#define SUBPOOL_STATS_SELECTED_HDR_STR \
"Selected EM subpool statistics for pool %" PRI_POOL ":\n\n"\
"Selected statistic counters: %s\n\n"\
"Subpool Available Alloc_ops Alloc_fails Free_ops Total_ops Cache_available" \
" Cache_alloc_ops Cache_free_ops\n"\
"--------------------------------------------------------------------------" \
"-------------------------------\n%s"

void subpools_stats_selected_print(em_pool_t pool, const int subpools[],
				   int num_subpools, const em_pool_stats_opt_t *opt)
{
	int num_stats;
	char opt_str[OPT_STR_LEN];
	em_pool_subpool_stats_selected_t stats[num_subpools];
	int len = 0;
	int n_print = 0;
	const mpool_elem_t *pool_elem = pool_elem_get(pool);
	const int stats_str_len = num_subpools * POOL_STATS_LEN + 1;
	char stats_str[stats_str_len];

	if (pool_elem == NULL || !pool_allocated(pool_elem)) {
		EM_LOG(EM_LOG_ERR, "EM-pool:%" PRI_POOL " invalid\n", pool);
		return;
	}

	memset(stats, 0, sizeof(stats));
	num_stats = em_pool_subpool_stats_selected(pool, subpools, num_subpools, stats, opt);
	if (unlikely(!num_stats || num_stats > num_subpools)) {
		EM_LOG(EM_LOG_ERR, "Failed to fetch selected subpool statistics\n");
		return;
	}

	/* Print subpool stats */
	for (int i = 0; i < num_stats; i++) {
		n_print = snprintf(stats_str + len, stats_str_len - len,
				   POOL_STATS_FMT,
				   subpools[i], stats[i].available, stats[i].alloc_ops,
				   stats[i].alloc_fails, stats[i].free_ops,
				   stats[i].total_ops, stats[i].cache_available,
				   stats[i].cache_alloc_ops, stats[i].cache_free_ops);

		/* Not enough space to hold more subpool stats */
		if (n_print >= stats_str_len - len)
			break;

		len += n_print;
	}
	stats_str[len] = '\0';

	/* Fill selected statistic counters */
	fill_opt_str(opt_str, opt);
	EM_PRINT(SUBPOOL_STATS_SELECTED_HDR_STR, pool, opt_str, stats_str);
}

void print_pool_elem_info(void)
{
	EM_PRINT("\n"
		 "pool-elem size: %zu B\n",
		 sizeof(mpool_elem_t));

	EM_DBG("\t\toffset\tsize\n"
	       "\t\t------\t-----\n"
	       "event_type:\t%3zu B\t%3zu B\n"
	       "align_offset:\t%3zu B\t%3zu B\n"
	       "user_area info:\t%3zu B\t%3zu B\n"
	       "num_subpools:\t%3zu B\t%3zu B\n"
	       "size[]:\t\t%3zu B\t%3zu B\n"
	       "odp_pool[]:\t%3zu B\t%3zu B\n"
	       "em_pool:\t%3zu B\t%3zu B\n"
	       "objpool_elem:\t%3zu B\t%3zu B\n"
	       "stats_opt:\t%3zu B\t%3zu B\n"
	       "pool_cfg:\t%3zu B\t%3zu B\n"
	       "name[]:\t\t%3zu B\t%3zu B\n",
	       offsetof(mpool_elem_t, event_type), sizeof_field(mpool_elem_t, event_type),
	       offsetof(mpool_elem_t, align_offset), sizeof_field(mpool_elem_t, align_offset),
	       offsetof(mpool_elem_t, user_area), sizeof_field(mpool_elem_t, user_area),
	       offsetof(mpool_elem_t, num_subpools), sizeof_field(mpool_elem_t, num_subpools),
	       offsetof(mpool_elem_t, size), sizeof_field(mpool_elem_t, size),
	       offsetof(mpool_elem_t, odp_pool), sizeof_field(mpool_elem_t, odp_pool),
	       offsetof(mpool_elem_t, em_pool), sizeof_field(mpool_elem_t, em_pool),
	       offsetof(mpool_elem_t, objpool_elem), sizeof_field(mpool_elem_t, objpool_elem),
	       offsetof(mpool_elem_t, stats_opt), sizeof_field(mpool_elem_t, stats_opt),
	       offsetof(mpool_elem_t, pool_cfg), sizeof_field(mpool_elem_t, pool_cfg),
	       offsetof(mpool_elem_t, name), sizeof_field(mpool_elem_t, name));

	       EM_PRINT("\n");
}
