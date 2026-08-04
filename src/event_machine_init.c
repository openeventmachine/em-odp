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
 * Event Machine initialization and termination.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <odp_api.h>
#include <odp/helper/odph_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_atomic_group.h"
#include "em_chaining.h"
#include "em_chaining_types.h"
#include "em_cli.h"
#include "em_core.h"
#include "em_core_types.h"
#include "em_dispatcher.h"
#include "em_eo.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_group.h"
#include "em_event_state.h"
#include "em_hooks.h"
#include "em_info.h"
#include "em_init.h"
#include "em_internal_event.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_pool.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_timer.h"

/**
 * Check that the runtime ODP API version is supported.
 *
 * Compare the EM required ODP API version (EM_REQ_ODP_API_{GENERATION,MAJOR,MINOR})
 * against both the ODP API header version (ODP_VERSION_API_{GENERATION,MAJOR,MINOR})
 * and the runtime libodp version from odp_version_api_str().
 */
static em_status_t check_odp_version(void)
{
	const char *ver_str = odp_version_api_str();
	unsigned int odp_api_ver = ODP_VERSION_API_NUM(ODP_VERSION_API_GENERATION,
						       ODP_VERSION_API_MAJOR,
						       ODP_VERSION_API_MINOR);
	unsigned int gen = 0;
	unsigned int major = 0;
	unsigned int minor = 0;

	if (ver_str == NULL || sscanf(ver_str, "%u.%u.%u", &gen, &major, &minor) != 3) {
		EM_LOG(EM_LOG_ERR,
		       "Error: Unable to parse ODP runtime API version: \"%s\"\n",
		       ver_str ? ver_str : "NULL");
		return EM_ERR_NOT_SUPPORTED;
	}

	unsigned int odp_ver = ODP_VERSION_API_NUM(gen, major, minor);
	unsigned int min_ver = ODP_VERSION_API_NUM(EM_REQ_ODP_API_GENERATION,
						   EM_REQ_ODP_API_MAJOR,
						   EM_REQ_ODP_API_MINOR);

	if (odp_ver < min_ver || odp_api_ver < min_ver) {
		EM_LOG(EM_LOG_ERR,
		       "Error: Unsupported ODP API version:%s%s\n"
		       "  EM required ODP API version (min): %u.%u.%u\n"
		       "  ODP API version from headers:      %u.%u.%u\n"
		       "  ODP API version from libodp:       %u.%u.%u\n",
		       odp_api_ver < min_ver ? " headers too old" : "",
		       odp_ver < min_ver ? " libodp too old" : "",
		       EM_REQ_ODP_API_GENERATION,
		       EM_REQ_ODP_API_MAJOR,
		       EM_REQ_ODP_API_MINOR,
		       ODP_VERSION_API_GENERATION,
		       ODP_VERSION_API_MAJOR,
		       ODP_VERSION_API_MINOR,
		       gen, major, minor);
		return EM_ERR_NOT_SUPPORTED;
	}

	return EM_OK;
}

/** EM shared memory */
em_shm_t *em_shm;

ENV_LOCAL em_locm_t em_locm ENV_CACHE_LINE_ALIGNED = EM_LOCM_INIT_VALUES;

void em_conf_init(em_conf_t *conf)
{
	if (unlikely(!conf)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_POINTER),
			       EM_ESCOPE_CONF_INIT, "Conf pointer NULL!");
		return;
	}
	memset(conf, 0, sizeof(em_conf_t));
	em_pool_cfg_init(&conf->default_pool_cfg);
	conf->__internal_check = EM_CHECK_INIT_CALLED;
}

em_status_t em_init(const em_conf_t *conf)
{
	em_status_t stat;
	int ret;

	RETURN_ERROR_IF(!conf, EM_FATAL(EM_ERR_BAD_ARG), EM_ESCOPE_INIT,
			"Conf pointer NULL!");

	RETURN_ERROR_IF(conf->__internal_check != EM_CHECK_INIT_CALLED,
			EM_ERR_NOT_INITIALIZED, EM_ESCOPE_INIT,
			"Not initialized: em_conf_init(conf) not called");

	stat = early_log_init(conf->log.log_fn, conf->log.vlog_fn);
	RETURN_ERROR_IF(stat != EM_OK, EM_FATAL(stat),
			EM_ESCOPE_INIT, "User provided log funcs invalid!");

	/* Sanity check: em_shm should not be set yet */
	RETURN_ERROR_IF(em_shm != NULL,
			EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_INIT,
			"EM shared memory ptr set - already initialized?");
	/* Sanity check: either process- or thread-per-core, but not both */
	RETURN_ERROR_IF(!(conf->process_per_core ^ conf->thread_per_core),
			EM_FATAL(EM_ERR_BAD_ARG), EM_ESCOPE_INIT,
			"Select EITHER process-per-core OR thread-per-core!");

	/*
	 * Reserve the EM shared memory once at start-up.
	 */
	uint32_t flags = 0;
	odp_shm_capability_t shm_capa;

	ret = odp_shm_capability(&shm_capa);
	RETURN_ERROR_IF(ret, EM_ERR_OPERATION_FAILED, EM_ESCOPE_INIT,
			"shm capability error:%d", ret);

	if (shm_capa.flags & ODP_SHM_SINGLE_VA)
		flags |= ODP_SHM_SINGLE_VA;

	odp_shm_t shm = odp_shm_reserve("em_shm", sizeof(em_shm_t),
					ODP_CACHE_LINE_SIZE, flags);

	RETURN_ERROR_IF(shm == ODP_SHM_INVALID, EM_ERR_ALLOC_FAILED,
			EM_ESCOPE_INIT, "Shared memory reservation failed!");

	em_shm = odp_shm_addr(shm);

	RETURN_ERROR_IF(em_shm == NULL, EM_ERR_NOT_FOUND, EM_ESCOPE_INIT,
			"Shared memory ptr NULL!");

	memset(em_shm, 0, sizeof(em_shm_t));

	/* Store shm handle, can be used in em_term() to free the memory */
	em_shm->this_shm = shm;

	/* Store the given EM configuration */
	em_shm->conf = *conf;

	if (!EM_API_HOOKS_ENABLE) {
		memset(&em_shm->conf.api_hooks, 0,
		       sizeof(em_shm->conf.api_hooks));
	}

	/* Initialize the log & error handling */
	log_init();
	error_init();

	/* Check that the ODP API version (headers) is supported */
	stat = check_odp_version();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_NOT_SUPPORTED, EM_ESCOPE_INIT,
			"ODP API version not supported!");

	/* Initialize libconfig */
	ret = em_libconfig_init_global(&em_shm->libconfig);
	RETURN_ERROR_IF(ret != 0, EM_ERR_OPERATION_FAILED, EM_ESCOPE_INIT,
			"libconfig initialization failed:%d", ret);

	/*
	 * Initialize the physical-core <-> EM-core mapping
	 *
	 * EM-core <-> ODP-thread id mappings cannot be set up yet,
	 * the ODP thread id is assigned only when that thread is initialized.
	 * Set this mapping in core_map_init_local()
	 */
	stat = core_map_init(&em_shm->core_map);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"core_map_init() failed:%" PRI_STAT "", stat);

	/* Initialize the EM event dispatcher */
	stat = dispatch_init();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"dispatch_init() failed:%" PRI_STAT "", stat);

	/*
	 * Initialize Event State Verification (ESV), if enabled at compile time
	 */
	stat = esv_init();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"esv_init() failed:%" PRI_STAT "", stat);

	/* Initialize EM callbacks/hooks */
	stat = hooks_init(&conf->api_hooks, &conf->idle_hooks);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"hooks_init() failed:%" PRI_STAT "", stat);

	/*
	 * Initialize the EM buffer pools and create the EM_DEFAULT_POOL.
	 * Create also startup pools if configured in the runtime config
	 * file through option 'startup_pools'.
	 */
	stat = pool_init(&em_shm->mpool_tbl, &em_shm->mpool_pool,
			 &conf->default_pool_cfg);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"pool_init() failed:%" PRI_STAT "", stat);

	stat = event_init();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"event_init() failed:%" PRI_STAT "", stat);

	stat = event_group_init(&em_shm->event_group_tbl,
				&em_shm->event_group_stash);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"event_group_init() failed:%" PRI_STAT "", stat);

	stat = queue_init(&em_shm->queue_tbl, &em_shm->queue_pool,
			  &em_shm->queue_pool_static, &em_shm->queue_pool_aggr);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"queue_init() failed:%" PRI_STAT "", stat);

	stat = queue_group_init(&em_shm->queue_group_tbl,
				&em_shm->queue_group_pool);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"queue_group_init() failed:%" PRI_STAT "", stat);

	stat = atomic_group_init(&em_shm->atomic_group_tbl,
				 &em_shm->atomic_group_pool);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"atomic_group_init() failed:%" PRI_STAT "", stat);

	stat = eo_init(&em_shm->eo_tbl, &em_shm->eo_pool);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"eo_init() failed:%" PRI_STAT "", stat);

	stat = create_shared_ctrl_queue();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"create_shared_ctrl_queue() failed:%" PRI_STAT "", stat);

	/* Initialize EM Timer */
	if (conf->event_timer) {
		stat = timer_init(&em_shm->timers);
		RETURN_ERROR_IF(stat != EM_OK,
				EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
				"timer_init() failed:%" PRI_STAT "",
				stat);
	}

	/* Initialize basic Event Chaining support */
	stat = chaining_init(&em_shm->event_chaining);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"chaining_init() failed:%" PRI_STAT "", stat);

	/* Initialize em_cli */
	stat = emcli_init();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"emcli_init() failed:%" PRI_STAT "", stat);

	/*
	 * Print EM and ODP version information
	 */
	print_version_info();

	em_shm->is_init = true;
	return EM_OK;
}

em_status_t em_conf_opts(em_conf_t *conf_opts)
{
	RETURN_ERROR_IF(!conf_opts, EM_ERR_BAD_POINTER, EM_ESCOPE_CONF_OPTS,
			"Conf pointer NULL!");

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_ERR_BAD_STATE,
			EM_ESCOPE_CONF_OPTS,
			"EM not initialized - call em_init() first!");

	*conf_opts = em_shm->conf;
	return EM_OK;
}

void em_conf_opts_print(void)
{
	conf_opts_print();
}

void em_conf_local_init(em_conf_local_t *conf_local)
{
	if (unlikely(!conf_local)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_POINTER),
			       EM_ESCOPE_CONF_LOCAL_INIT, "conf_local pointer NULL!");
		return;
	}
	memset(conf_local, 0, sizeof(*conf_local));
	conf_local->core_id = -1; /* let EM assign core-id */
	conf_local->__internal_check = EM_CHECK_INIT_CALLED;
}

em_status_t em_init_local(const em_conf_local_t *conf_local)
{
	em_status_t status = EM_OK;
	em_locm_t *const locm = &em_locm;
	odp_thread_type_t thrtype_odp = odp_thread_type();

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_FATAL(EM_ERR_BAD_STATE),
			EM_ESCOPE_INIT_LOCAL,
			"Must call em_init() first before %s!", __func__);

	RETURN_ERROR_IF(!conf_local, EM_FATAL(EM_ERR_BAD_ARG), EM_ESCOPE_INIT_LOCAL,
			"conf_local pointer NULL!");

	RETURN_ERROR_IF(conf_local->__internal_check != EM_CHECK_INIT_CALLED,
			EM_ERR_NOT_INITIALIZED, EM_ESCOPE_INIT_LOCAL,
			"Not initialized: em_conf_local_init(conf_local) not called");

	RETURN_ERROR_IF(conf_local->core_type <= EM_CORE_TYPE_UNDEF ||
			conf_local->core_type >= EM_CORE_TYPE_LAST,
			EM_ERR_BAD_TYPE, EM_ESCOPE_INIT_LOCAL,
			"Unsupported core_type=%d", conf_local->core_type);

	RETURN_ERROR_IF(conf_local->core_id < -1, EM_ERR_BAD_ARG, EM_ESCOPE_INIT_LOCAL,
			"Invalid core id: %d!", conf_local->core_id);

	RETURN_ERROR_IF(is_core_initialized(), EM_ERR_BAD_STATE, EM_ESCOPE_INIT_LOCAL,
			"Core already initialized! core_id:%d", locm->core_id);

	switch (conf_local->core_type) {
	case EM_CORE_TYPE_WORKER:
		RETURN_ERROR_IF(thrtype_odp != ODP_THREAD_WORKER,
				EM_ERR_BAD_TYPE, EM_ESCOPE_INIT_LOCAL,
				"ODP thread-type:%d not 'worker' for EM worker core!",
				thrtype_odp);
		status = init_local(conf_local, EM_ESCOPE_INIT_LOCAL);
		break;

	case EM_CORE_TYPE_CONTROL:
		RETURN_ERROR_IF(thrtype_odp != ODP_THREAD_CONTROL,
				EM_ERR_BAD_TYPE, EM_ESCOPE_INIT_LOCAL,
				"ODP thread-type:%d not 'control' for EM control core!",
				thrtype_odp);
		status = init_local(conf_local, EM_ESCOPE_INIT_LOCAL);
		break;

	case EM_CORE_TYPE_EXTERNAL:
		RETURN_ERROR_IF(thrtype_odp != ODP_THREAD_CONTROL,
				EM_ERR_BAD_TYPE, EM_ESCOPE_INIT_LOCAL,
				"ODP thread-type:%d not 'control' for EM external thread!",
				thrtype_odp);
		status = init_extthr(conf_local, EM_ESCOPE_INIT_LOCAL);
		break;

	default:
		return INTERNAL_ERROR(EM_ERR_BAD_TYPE, EM_ESCOPE_INIT_LOCAL,
				      "Unsupported core_type=%d", conf_local->core_type);
	}

	if (status == EM_OK)
		locm->is_core_local_inited = true;

	return status;
}

em_status_t em_conf_local_opts(em_conf_local_t *conf_local_opts)
{
	const em_locm_t *const locm = &em_locm;

	RETURN_ERROR_IF(!conf_local_opts, EM_ERR_BAD_POINTER,
			EM_ESCOPE_CONF_LOCAL_OPTS, "Conf pointer NULL!");

	RETURN_ERROR_IF(!is_core_initialized(), EM_ERR_BAD_STATE, EM_ESCOPE_CONF_LOCAL_OPTS,
			"Not initialized locally: call em_init_local() or em_init_core() first!");

	*conf_local_opts = locm->conf_local;

	return EM_OK;
}

void em_conf_local_opts_print(void)
{
	if (!is_core_initialized()) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_CONF_LOCAL_OPTS,
			       "Not initialized locally, call em_init_local/core() first!");
		return;
	}

	conf_local_opts_print();
}

em_status_t em_init_core(void)
{
	em_conf_local_t conf_local;
	em_status_t status = EM_OK;
	em_locm_t *const locm = &em_locm;
	odp_thread_type_t thrtype_odp = odp_thread_type();

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_FATAL(EM_ERR_BAD_STATE),
			EM_ESCOPE_INIT_CORE,
			"Must call em_init() first before %s!", __func__);

	RETURN_ERROR_IF(is_core_initialized(), EM_ERR_BAD_STATE, EM_ESCOPE_INIT_CORE,
			"Core already initialized! core_id:%d", locm->core_id);

	RETURN_ERROR_IF(thrtype_odp != ODP_THREAD_WORKER,
			EM_ERR_BAD_TYPE, EM_ESCOPE_INIT_CORE,
			"ODP thread-type:%d not 'worker' for EM worker core!",
			thrtype_odp);

	em_conf_local_init(&conf_local);
	conf_local.core_type = EM_CORE_TYPE_WORKER;

	status = init_local(&conf_local, EM_ESCOPE_INIT_CORE);
	if (status == EM_OK)
		locm->is_core_local_inited = true;

	return status;
}

void em_term_local_init(em_term_local_t *term_local)
{
	if (unlikely(!term_local)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_POINTER),
			       EM_ESCOPE_TERM_LOCAL_INIT,
			       "term_local pointer NULL!");
		return;
	}
	memset(term_local, 0, sizeof(*term_local));
	term_local->__internal_check = EM_CHECK_INIT_CALLED;
}

em_status_t em_term_local(const em_term_local_t *term_local)
{
	em_locm_t *const locm = &em_locm;
	const em_conf_local_t *conf_local = &locm->conf_local;
	em_status_t status = EM_OK;

	RETURN_ERROR_IF(!term_local, EM_FATAL(EM_ERR_BAD_ARG),
			EM_ESCOPE_TERM_LOCAL, "term_local pointer NULL!");

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_ERR_BAD_STATE,
			EM_ESCOPE_TERM_LOCAL,
			"EM not initialized - call em_init() first!");

	RETURN_ERROR_IF(!is_core_initialized(), EM_ERR_BAD_STATE, EM_ESCOPE_TERM_LOCAL,
			"Not initialized: em_init_local() or em_init_core() not called");

	RETURN_ERROR_IF(term_local->__internal_check != EM_CHECK_INIT_CALLED,
			EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TERM_LOCAL,
			"Not initialized: em_term_local_init() not called");

	switch (conf_local->core_type) {
	case EM_CORE_TYPE_WORKER: /* fallthrough */
	case EM_CORE_TYPE_CONTROL:
		status = term_core(EM_ESCOPE_TERM_LOCAL);
		break;

	case EM_CORE_TYPE_EXTERNAL:
		status = term_extthr(EM_ESCOPE_TERM_LOCAL);
		break;

	default:
		return INTERNAL_ERROR(EM_ERR_BAD_TYPE, EM_ESCOPE_TERM_LOCAL,
				      "Unsupported core_type=%d", conf_local->core_type);
	}

	*locm = locm_init_values;

	return status;
}

em_status_t em_term_core(void)
{
	em_status_t status = EM_OK;
	em_locm_t *const locm = &em_locm;
	odp_thread_type_t thrtype_odp = odp_thread_type();

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_ERR_BAD_STATE,
			EM_ESCOPE_TERM_CORE,
			"EM not initialized - call em_init() first!");

	RETURN_ERROR_IF(!is_core_initialized(), EM_ERR_BAD_STATE, EM_ESCOPE_TERM_CORE,
			"Not initialized: em_init_local() or em_init_core() not called");

	RETURN_ERROR_IF(thrtype_odp != ODP_THREAD_WORKER,
			EM_ERR_BAD_TYPE, EM_ESCOPE_TERM_CORE,
			"ODP thread-type:%d not 'worker' for EM worker core!",
			thrtype_odp);

	status = term_core(EM_ESCOPE_TERM_CORE);
	*locm = locm_init_values;
	return status;
}

em_status_t em_term(void)
{
	em_locm_t *const locm = &em_locm;
	em_status_t stat;
	int ret;

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_ERR_BAD_STATE,
			EM_ESCOPE_TERM, "EM not initialized or already terminated!");

	/*
	 * Join all queue groups to be able to flush all events
	 * from the scheduler from this core.
	 */
	queue_group_join_all();

	/*
	 * Flush all events in the scheduler.
	 * Run loop twice: first with sched enabled and then paused.
	 */
	if (locm->is_sched_paused) {
		locm->is_sched_paused = false;
		odp_schedule_resume();
	}
	for (int i = 0; i < 2; i++) {
		flush_scheduler_events();
		locm->is_sched_paused = true;
		odp_schedule_pause();
	}

	stat = delete_ctrl_queues();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"delete_ctrl_queues() failed:%" PRI_STAT "", stat);

	if (em_shm->conf.event_timer)
		timer_term(&em_shm->timers);

	stat = emcli_term();
	if (unlikely(stat != EM_OK)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			       "emcli_term() failed:%" PRI_STAT "", stat);
		/* Continue teardown after CLI error */
	}

	stat = chaining_term(&em_shm->event_chaining);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"chaining_term() failed:%" PRI_STAT "", stat);

	ret = em_libconfig_term_global(&em_shm->libconfig);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"EM config term failed:%d");

	stat = pool_term(&em_shm->mpool_tbl);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"pool_term() failed:%" PRI_STAT "", stat);

	env_shared_free(em_shm->queue_tbl.queue_elem);

	stat = event_group_term();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"event_group_term() failed.");

	/* Destroy all ODP schedule groups before freeing shared memory */
	stat = queue_group_term();
	if (unlikely(stat != EM_OK)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			       "queue_group_term() failed:" PRI_STAT "", stat);
		/* Continue teardown even if queue group term fails */
	}

	/*
	 * Free the EM shared memory
	 */
	ret = odp_shm_free(em_shm->this_shm);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"odp_shm_free() failed:%d", ret);
	/* Set em_shm = NULL to allow a new call to em_init() */
	em_shm = NULL;

	return EM_OK;
}

uint16_t em_device_id(void)
{
	return em_shm->conf.device_id;
}

em_status_t em_cfgfile_opts(em_cfgfile_opts_t *cfgfile_opts)
{
	RETURN_ERROR_IF(!cfgfile_opts, EM_ERR_BAD_ARG, EM_ESCOPE_CFGFILE_OPTS,
			"cfgfile_opts pointer cannot be NULL!");

	RETURN_ERROR_IF(!em_shm || !em_shm->is_init, EM_ERR_BAD_STATE,
			EM_ESCOPE_CFGFILE_OPTS,
			"EM not initialized - call em_init() first!");

	*cfgfile_opts = em_shm->opt;
	return EM_OK;
}

void em_cfgfile_opts_print(void)
{
	cfgfile_opts_print();
}
