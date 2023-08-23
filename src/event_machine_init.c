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

#include "em_include.h"
#include "add-ons/event_timer/em_timer.h"

/** EM shared memory */
em_shm_t *em_shm;

/** Core local variables */
ENV_LOCAL em_locm_t em_locm ENV_CACHE_LINE_ALIGNED = {
		.current.egrp = EM_EVENT_GROUP_UNDEF,
		.current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE,
		.local_queues.empty = 1,
		.do_input_poll = false,
		.do_output_drain = false,
		.sync_api.in_progress = false
		/* other members initialized to 0 or NULL as per C standard */
};

void em_conf_init(em_conf_t *conf)
{
	if (unlikely(!conf)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_POINTER),
			       EM_ESCOPE_CONF_INIT, "Conf pointer NULL!");
		return;
	}
	memset(conf, 0, sizeof(em_conf_t));
	em_pool_cfg_init(&conf->default_pool_cfg);
}

em_status_t
em_init(const em_conf_t *conf)
{
	em_status_t stat;
	int ret;

	RETURN_ERROR_IF(!conf, EM_FATAL(EM_ERR_BAD_ARG), EM_ESCOPE_INIT,
			"Conf pointer NULL!");

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

#if ODP_VERSION_API_NUM(1, 33, 0) > ODP_VERSION_API
	flags |= ODP_SHM_SINGLE_VA;
#else
	odp_shm_capability_t shm_capa;

	ret = odp_shm_capability(&shm_capa);
	RETURN_ERROR_IF(ret, EM_ERR_OPERATION_FAILED, EM_ESCOPE_INIT,
			"shm capability error:%d", ret);

	if (shm_capa.flags & ODP_SHM_SINGLE_VA)
		flags |= ODP_SHM_SINGLE_VA;
#endif
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

	env_spinlock_init(&em_shm->init.lock);

	/* Initialize the log & error handling */
	log_init();
	error_init();

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
	stat = core_map_init(&em_shm->core_map, conf->core_count,
			     &conf->phys_mask);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"core_map_init() failed:%" PRI_STAT "", stat);

	/* Initialize the EM event dispatcher */
	stat = dispatch_init();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"dispatch_init() failed:%" PRI_STAT "", stat);

	/*
	 * Check validity of core masks for input_poll_fn and output_drain_fn.
	 *
	 * Masks must be a subset of logical EM core mask. Zero mask means
	 * that input_poll_fn and output_drain_fn are run on all EM cores.
	 */
	stat = input_poll_init(&em_shm->core_map.logic_mask, conf);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"input_poll_init() failed:%" PRI_STAT "", stat);
	stat = output_drain_init(&em_shm->core_map.logic_mask, conf);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"output_drain_init() failed:%" PRI_STAT "", stat);

	/*
	 * Initialize Event State Verification (ESV), if enabled at compile time
	 */
	if (EM_ESV_ENABLE) {
		stat = esv_init();
		RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
				"esv_init() failed:%" PRI_STAT "", stat);
	}

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
				&em_shm->event_group_pool);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"event_group_init() failed:%" PRI_STAT "", stat);

	stat = queue_init(&em_shm->queue_tbl, &em_shm->queue_pool,
			  &em_shm->queue_pool_static);
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

	stat = create_ctrl_queues();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"create_ctrl_queues() failed:%" PRI_STAT "", stat);

	/* timer add-on */
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

	return EM_OK;
}

em_status_t
em_init_core(void)
{
	em_locm_t *const locm = &em_locm;
	odp_shm_t shm;
	em_shm_t *shm_addr;
	em_status_t stat;
	int init_count;

	/* Lookup the EM shared memory on each EM-core */
	shm = odp_shm_lookup("em_shm");
	RETURN_ERROR_IF(shm == ODP_SHM_INVALID,
			EM_ERR_NOT_FOUND, EM_ESCOPE_INIT_CORE,
			"Shared memory lookup failed!");

	shm_addr = odp_shm_addr(shm);
	RETURN_ERROR_IF(shm_addr == NULL, EM_ERR_BAD_POINTER, EM_ESCOPE_INIT_CORE,
			"Shared memory ptr NULL");

	if (shm_addr->conf.process_per_core && em_shm == NULL)
		em_shm = shm_addr;

	RETURN_ERROR_IF(shm_addr != em_shm, EM_ERR_BAD_POINTER, EM_ESCOPE_INIT_CORE,
			"Shared memory init fails: em_shm:%p != shm_addr:%p",
			em_shm, shm_addr);

	/* Initialize core mappings not known yet in core_map_init() */
	stat = core_map_init_local(&em_shm->core_map);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"core_map_init_local() failed:%" PRI_STAT "", stat);

	stat = queue_group_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"queue_group_init_local() failed:%" PRI_STAT "", stat);

	stat = dispatch_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"dispatch_init_local() failed:%" PRI_STAT "", stat);

	/* Check if input_poll_fn should be executed on this core */
	stat = input_poll_init_local(&locm->do_input_poll,
				     locm->core_id, &em_shm->conf);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"input_poll_init_local() failed:%" PRI_STAT "", stat);

	/* Check if output_drain_fn should be executed on this core */
	stat = output_drain_init_local(&locm->do_output_drain,
				       locm->core_id, &em_shm->conf);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"output_drain_init_local() failed:%" PRI_STAT "", stat);

	stat = queue_init_local();
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_INIT_CORE,
			"queue_init_local() failed:%" PRI_STAT "", stat);

	/*
	 * Initialize timer add-on. If global init was not done (config),
	 * this is just a NOP
	 */
	stat = timer_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"timer_init_local() failed:%" PRI_STAT "", stat);

	stat = sync_api_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"sync_api_init_local() failed:%" PRI_STAT "", stat);

	/* Init the EM CLI locally on this core (only if enabled) */
	stat = emcli_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"emcli_init_local() failed:%" PRI_STAT "", stat);

	/* This is an EM-core that will participate in EM event dispatching */
	locm->is_external_thr = false;

	/* Initialize debug timestamps to 1 if enabled to differentiate from disabled */
	if (EM_DEBUG_TIMESTAMP_ENABLE)
		for (int i = 0; i < EM_DEBUG_TSP_LAST; i++)
			locm->debug_ts[i] = 1;

	env_spinlock_lock(&em_shm->init.lock);
	init_count = ++em_shm->init.em_init_core_cnt;
	env_spinlock_unlock(&em_shm->init.lock);

	/* Now OK to call EM APIs */

	/* Print info about the Env&HW when the last core has initialized */
	if (init_count == em_core_count()) {
		print_em_info();
		/* Last */
		em_shm->init.em_init_done = 1;
	}

	env_sync_mem();

	return EM_OK;
}

em_status_t
em_term(const em_conf_t *conf)
{
	em_status_t stat;
	int ret;

	(void)conf;

	if (em_shm->conf.event_timer)
		timer_term();

	stat = emcli_term();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"emcli_term() failed:%" PRI_STAT "", stat);

	stat = chaining_term(&em_shm->event_chaining);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"chaining_term() failed:%" PRI_STAT "", stat);

	ret = em_libconfig_term_global(&em_shm->libconfig);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"EM config term failed:%d");

	stat = pool_term(&em_shm->mpool_tbl);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"pool_term() failed:%" PRI_STAT "", stat);

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

em_status_t
em_term_core(void)
{
	em_status_t stat = EM_OK;
	em_status_t ret_stat = EM_OK;

	if (em_core_id() == 0)
		delete_ctrl_queues();

	/* Stop timer add-on. Just a NOP if timer was not enabled (config) */
	stat = timer_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, EM_ESCOPE_TERM_CORE,
			       "timer_term_local() fails: %" PRI_STAT "", stat);
	}

	/* Term the EM CLI locally (if enabled) */
	stat = emcli_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, EM_ESCOPE_TERM_CORE,
			       "emcli_term_local() fails: %" PRI_STAT "", stat);
	}

	/* Delete the local queues */
	stat = queue_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, EM_ESCOPE_TERM_CORE,
			       "queue_term_local() fails: %" PRI_STAT "", stat);
	}

	/*
	 * Flush all events in the scheduler.
	 * Scheduler paused during return from em_dispatch()
	 */
	odp_schedule_resume();

	odp_event_t odp_ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	em_event_t em_ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	odp_queue_t odp_queue;
	int num_events;

	/* run loop twice: first with sched enabled and then paused */
	for (int i = 0; i < 2; i++) {
		do {
			num_events =
			odp_schedule_multi_no_wait(&odp_queue, odp_ev_tbl,
						   EM_SCHED_MULTI_MAX_BURST);
			/* the check 'num_events > EM_SCHED_MULTI_MAX_BURST' avoids a gcc warning */
			if (num_events <= 0 || num_events > EM_SCHED_MULTI_MAX_BURST)
				break;
			/*
			 * Events might originate from outside of EM and need init.
			 */
			event_init_odp_multi(odp_ev_tbl, em_ev_tbl/*out*/,
					     ev_hdr_tbl/*out*/, num_events,
					     true/*is_extev*/);
			em_free_multi(em_ev_tbl, num_events);
		} while (num_events > 0);

		odp_schedule_pause();
	}

	return ret_stat == EM_OK ? EM_OK : EM_ERR;
}
