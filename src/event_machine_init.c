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
 * Event Machine initialization and termination.
 *
 */

#include "em_include.h"
#include "add-ons/event_timer/em_timer.h"

/** Thread-local pointer to EM shared memory */
ENV_LOCAL em_shm_t *em_shm;

/** Core local variables */
ENV_LOCAL em_locm_t em_locm ENV_CACHE_LINE_ALIGNED = {
		.current.egrp = EM_EVENT_GROUP_UNDEF,
		.current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE,
		.local_queues.empty = 1
		/* other members initialized to 0 or NULL as per C standard */
};

em_status_t
em_init(em_conf_t *conf)
{
	em_status_t stat;
	int ret;

	if (!EM_API_HOOKS_ENABLE)
		memset(&conf->api_hooks, 0, sizeof(conf->api_hooks));

	stat = early_log_init(conf->log.log_fn, conf->log.vlog_fn);
	RETURN_ERROR_IF(stat != EM_OK, EM_FATAL(stat),
			EM_ESCOPE_INIT, "User provided log funcs invalid!");

	/*
	 * Reserve the EM shared memory once at start-up.
	 */
	odp_shm_t shm = odp_shm_reserve("em_shm", sizeof(em_shm_t),
					ODP_CACHE_LINE_SIZE,
					ODP_SHM_SINGLE_VA);

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

	env_spinlock_init(&em_shm->init.lock);

	/* Initialize the log & error handling */
	log_init();
	error_init();

	/* Initialize libconfig */
	ret = libconfig_init_global(&em_shm->libconfig);
	RETURN_ERROR_IF(ret != 0, EM_ERR_OPERATION_FAILED, EM_ESCOPE_INIT,
			"libconfig initialization failed:%d", ret);

	/* Initialize the synchronous API locks */
	env_spinlock_init(&em_shm->sync_api.lock_global);
	env_spinlock_init(&em_shm->sync_api.lock_caller);

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

	/*
	 * Initialize the EM buffer pools and create the EM_DEFAULT_POOL based
	 * on config.
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

	em_queue_group_t default_queue_group = default_queue_group_create();

	RETURN_ERROR_IF(default_queue_group != EM_QUEUE_GROUP_DEFAULT,
			EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"default_queue_group_create() failed!");

	/* timer add-on */
	if (conf->event_timer) {
		stat = timer_init(&em_shm->timers);
		RETURN_ERROR_IF(stat != EM_OK,
				EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
				"timer_init() failed:%" PRI_STAT "",
				stat);
	}

	/* Initialize EM callbacks/hooks */
	stat = hooks_init(&conf->api_hooks);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"hooks_init() failed:%" PRI_STAT "", stat);

	/* Initialize basic Event Chaining support */
	stat = chaining_init(&em_shm->event_chaining);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"chaining_init() failed:%" PRI_STAT "", stat);

	return EM_OK;
}

em_status_t
em_init_core(void)
{
	odp_shm_t shm;
	int init_count;
	em_status_t stat;

	/* Lookup the EM shared memory on each EM-core */
	shm = odp_shm_lookup("em_shm");
	RETURN_ERROR_IF(shm == ODP_SHM_INVALID, EM_ERR_NOT_FOUND,
			EM_ESCOPE_INIT_CORE, "Shared memory lookup failed!");

	/* Store the EM-core local pointer to EM shared memory */
	em_shm = odp_shm_addr(shm);
	RETURN_ERROR_IF(em_shm == NULL, EM_ERR_BAD_POINTER,
			EM_ESCOPE_INIT_CORE, "Shared memory ptr NULL!");

	/* Initialize core mappings not known yet in core_map_init() */
	stat = core_map_init_local(&em_shm->core_map);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
			"core_map_init_local() failed:%" PRI_STAT "", stat);

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

	env_spinlock_lock(&em_shm->init.lock);
	init_count = ++em_shm->init.em_init_core_cnt;
	env_spinlock_unlock(&em_shm->init.lock);

	/* Now OK to call EM APIs */

	/* Print info about the Env&HW when the last core has initialized */
	if (init_count == em_core_count()) {
		em_queue_group_t def_qgrp;

		def_qgrp = default_queue_group_update();
		RETURN_ERROR_IF(def_qgrp != EM_QUEUE_GROUP_DEFAULT,
				EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_CORE,
				"default_queue_group_update() failed!");

		daemon_eo_create();
		print_em_info();

		/* Last */
		em_shm->init.em_init_done = 1;
	}

	env_sync_mem();

	return EM_OK;
}

em_status_t
em_term(em_conf_t *conf)
{
	odp_event_t odp_ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	em_event_t *em_ev_tbl;
	odp_queue_t odp_queue;
	em_status_t stat;
	int num_events;
	int ret, i;

	(void)conf;

	if (em_shm->conf.event_timer)
		timer_term();

	/*
	 * Flush all events in the scheduler.
	 * Scheduler paused during return from em_dispatch()
	 */
	odp_schedule_resume();
	/* run loop twice: first with sched enabled and then paused */
	for (i = 0; i < 2; i++) {
		do {
			num_events =
			odp_schedule_multi_no_wait(&odp_queue, odp_ev_tbl,
						   EM_SCHED_MULTI_MAX_BURST);
			if (num_events > 0) {
				em_ev_tbl = events_odp2em(odp_ev_tbl);
				/*
				 * Events might originate from outside of EM
				 * and need hdr-init.
				 */
				event_to_hdr_init_multi(em_ev_tbl, ev_hdr_tbl,
							num_events);
				event_free_multi(em_ev_tbl, num_events);
			}
		} while (num_events > 0);

		odp_schedule_pause();
	}

	stat = chaining_term(&em_shm->event_chaining);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
			"chaining_term() failed:%" PRI_STAT "", stat);

	ret = libconfig_term_global(&em_shm->libconfig);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"EM config term failed:%d");

	stat = pool_term(&em_shm->mpool_tbl);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"pool_term() failed:%" PRI_STAT "", stat);

	ret = odp_shm_free(em_shm->this_shm);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TERM,
			"odp_shm_free() failed:%d", ret);

	return EM_OK;
}

em_status_t
em_term_core(void)
{
	em_status_t stat = EM_OK;

	if (em_core_id() == 0)
		daemon_eo_shutdown();

	/* Stop timer add-on. Just a NOP if timer was not enabled (config) */
	stat |= timer_term_local();

	/* Delete the local queues */
	stat |= queue_term_local();

	return stat == EM_OK ? EM_OK : EM_ERR;
}
