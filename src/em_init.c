/* Copyright (c) 2020-2026 Nokia Solutions and Networks
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-3-Clause
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
#include <sys/types.h>
#include <unistd.h> /* getpid() */

#include <odp_api.h>
#include <odp/helper/odph_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_atomic_group.h"
#include "em_chaining.h"
#include "em_chaining_types.h"
#include "em_cli.h"
#include "em_core.h"
#include "em_dispatcher.h"
#include "em_eo.h"
#include "em_error.h"
#include "em_error_types.h"
#include "em_event.h"
#include "em_event_group.h"
#include "em_event_state.h"
#include "em_hooks.h"
#include "em_init.h"
#include "em_init_types.h"
#include "em_internal_event.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_pool.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_timer.h"

const em_locm_t locm_init_values = EM_LOCM_INIT_VALUES;

bool is_core_initialized(void)
{
	const em_locm_t *const locm = &em_locm;

	/*
	 * Compare the PID stored in TLS with the current PID.
	 * A mismatch means either:
	 *  - First use: init_pid is 0 (from EM_LOCM_INIT_VALUES) while
	 *    getpid() returns a non-zero PID.
	 *  - Post-fork child (process-per-core mode): the child inherited
	 *    the parent's TLS with init_pid == parent PID, but getpid()
	 *    now returns the child's PID.
	 * In both cases the core is not yet initialized.
	 *
	 * After a successful init, init_thr_mem() sets init_pid = getpid()
	 * and em_init_local()/em_init_core() set is_core_local_inited = true.
	 * Subsequent calls in the same process/thread will see a matching
	 * PID and return is_core_local_inited.
	 */
	if (locm->init_pid != getpid())
		return false;

	return locm->is_core_local_inited;
}

static em_status_t input_poll_init_local(const em_conf_local_t *conf_local)
{
	em_locm_t *const locm = &em_locm;

	locm->input_poll_fn = conf_local->input_poll_fn;

	return EM_OK;
}

static em_status_t output_drain_init_local(const em_conf_local_t *conf_local)
{
	em_locm_t *const locm = &em_locm;

	locm->output_drain_fn = conf_local->output_drain_fn;

	return EM_OK;
}

void core_log_fn_set(em_log_func_t func)
{
	em_locm_t *const locm = &em_locm;

	locm->log_fn = func;
}

void core_vlog_fn_set(em_vlog_func_t func)
{
	em_locm_t *const locm = &em_locm;

	locm->vlog_fn = func;
}

em_status_t init_cli_thread(void)
{
	em_locm_t *const locm = &em_locm;
	odp_shm_t shm;
	em_shm_t *shm_addr;
	em_status_t stat = EM_OK;

	/* Make sure that em_shm is available in this external thread */
	shm = odp_shm_lookup("em_shm");
	RETURN_ERROR_IF(shm == ODP_SHM_INVALID,
			EM_ERR_NOT_FOUND, EM_ESCOPE_INIT_CORE,
			"Shared memory lookup failed!");

	shm_addr = odp_shm_addr(shm);
	RETURN_ERROR_IF(shm_addr == NULL, EM_ERR_BAD_POINTER, EM_ESCOPE_INIT_CORE,
			"Shared memory ptr NULL");

	if (em_shm != shm_addr)
		em_shm = shm_addr;

	RETURN_ERROR_IF(shm_addr != em_shm, EM_ERR_BAD_POINTER, EM_ESCOPE_INIT_CORE,
			"Shared memory init fails: em_shm:%p != shm_addr:%p",
			em_shm, shm_addr);

	stat = emcli_init_local();
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_INIT_CORE,
			"emcli_init_local() fails: %" PRIxSTAT "", stat);

	/*
	 * Mark that this is an external thread, i.e. not an EM-core and thus
	 * will not participate in EM event dispatching.
	 */
	locm->is_external_thread = true;

	return EM_OK;
}

em_status_t sync_api_init_local(void)
{
	em_locm_t *const locm = &em_locm;
	int thr = odp_thread_id();
	em_queue_t unsched_queue;
	queue_elem_t *q_elem;

	unsched_queue = queue_id2hdl(em_shm->queue_tbl.first_internal_queue_id + (uint16_t)thr);
	if (unlikely(unsched_queue == EM_QUEUE_UNDEF))
		return EM_ERR_NOT_FOUND;
	q_elem = queue_elem_get(unsched_queue);
	if (unlikely(!q_elem))
		return EM_ERR_BAD_POINTER;
	locm->sync_api.ctrl_poll.thr_unsched_queue = unsched_queue;
	locm->sync_api.ctrl_poll.thr_unsched_qelem = q_elem;
	locm->sync_api.ctrl_poll.thr_odp_plain_queue = q_elem->odp_queue;

	unsched_queue = queue_id2hdl(em_shm->queue_tbl.shared_internal_queue_id);
	if (unlikely(unsched_queue == EM_QUEUE_UNDEF))
		return EM_ERR_NOT_FOUND;
	q_elem = queue_elem_get(unsched_queue);
	if (unlikely(!q_elem))
		return EM_ERR_BAD_POINTER;
	locm->sync_api.ctrl_poll.shared_unsched_queue = unsched_queue;
	locm->sync_api.ctrl_poll.shared_unsched_qelem = q_elem;
	locm->sync_api.ctrl_poll.shared_odp_plain_queue = q_elem->odp_queue;

	locm->sync_api.in_progress = false;

	return EM_OK;
}

static em_status_t init_thr_mem(void)
{
	em_locm_t *const locm = &em_locm;
	odp_shm_t shm;
	em_shm_t *shm_addr;

	memset(locm, 0, sizeof(em_locm_t));
	*locm = locm_init_values;
	locm->init_pid = getpid();

	/* Lookup the EM shared memory on each EM-core */
	shm = odp_shm_lookup("em_shm");
	if (unlikely(shm == ODP_SHM_INVALID)) {
		EM_LOG(EM_LOG_ERR, "Shared memory lookup failed!");
		return EM_ERR_NOT_FOUND;
	}

	shm_addr = odp_shm_addr(shm);
	if (unlikely(shm_addr == NULL)) {
		EM_LOG(EM_LOG_ERR, "Shared memory ptr NULL");
		return EM_ERR_BAD_POINTER;
	}

	/* Process mode or EM external processes might need to update em_shm */
	if (em_shm != shm_addr)
		em_shm = shm_addr;

	if (unlikely(shm_addr != em_shm)) {
		EM_LOG(EM_LOG_ERR, "Shared memory init fails: em_shm:%p != shm_addr:%p",
		       em_shm, shm_addr);
		return EM_ERR_BAD_POINTER;
	}

	return EM_OK;
}

em_status_t init_local(const em_conf_local_t *conf_local, em_escope_t escope)
{
	em_status_t stat;

	stat = init_thr_mem();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"init_thr_mem() failed:%" PRIxSTAT "", stat);

	em_locm_t *const locm = &em_locm;

	/* store the given local conf */
	locm->conf_local = *conf_local;

	/* locm->evhdr_nonem all-zero here: set only non-zero fields */
	locm->evhdr_nonem.event_type = EM_EVENT_TYPE_ODP;
	locm->evhdr_nonem.user_area.isinit = 1;

	/*
	 * Create the core specific unsched control queue before local core-map
	 * init to avoid having another core targeting the queue before it's
	 * available.
	 */
	stat = create_ctrl_queue();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"create_ctrl_queue() failed:%" PRIxSTAT "", stat);

	/* Initialize core mappings not known yet in core_map_init() */
	stat = core_map_init_local(&em_shm->core_map, conf_local);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"core_map_init_local() failed:%" PRIxSTAT "", stat);

	stat = queue_group_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"queue_group_init_local() failed:%" PRIxSTAT "", stat);

	stat = dispatch_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"dispatch_init_local() failed:%" PRIxSTAT "", stat);

	/* Check if input_poll_fn should be executed on this core */
	stat = input_poll_init_local(conf_local);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"input_poll_init_local() failed:%" PRIxSTAT "", stat);

	/* Check if output_drain_fn should be executed on this core */
	stat = output_drain_init_local(conf_local);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"output_drain_init_local() failed:%" PRIxSTAT "", stat);

	stat = queue_init_local();
	RETURN_ERROR_IF(stat != EM_OK, stat, escope,
			"queue_init_local() failed:%" PRIxSTAT "", stat);

	/*
	 * Initialize EM timer. If global init was not done (config),
	 * this is just a NOP
	 */
	stat = timer_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"timer_init_local() failed:%" PRIxSTAT "", stat);

	stat = sync_api_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"sync_api_init_local() failed:%" PRIxSTAT "", stat);

	/* Init the EM CLI locally on this core (only if enabled) */
	stat = emcli_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"emcli_init_local() failed:%" PRIxSTAT "", stat);

	/* Initialize debug timestamps to 1 if enabled to differentiate from disabled */
	if (EM_DEBUG_TIMESTAMP_ENABLE)
		for (int i = 0; i < EM_DEBUG_TSP_LAST; i++)
			locm->debug_ts[i] = 1;

	/* Mark this thread as an EM-core that has a core-id and calls dispatch */
	locm->is_em_core = true;
	locm->is_external_thread = false;

	/* Now OK to call EM APIs */

	env_sync_mem();

	/* Run EO local-start funcs for all EOs on a new core (if configured) */
	eo_start_local_fn_at_init();

	return EM_OK;
}

em_status_t init_extthr(const em_conf_local_t *conf_local, em_escope_t escope)
{
	em_status_t stat;

	stat = init_thr_mem();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"init_thr_mem() failed:%" PRIxSTAT "", stat);

	em_locm_t *const locm = &em_locm;

	/* store the given local conf */
	locm->conf_local = *conf_local;

	stat = create_ctrl_queue();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"create_ctrl_queue() failed:%" PRIxSTAT "", stat);

	stat = core_map_init_extthr(&em_shm->core_map);
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"core_map_init_extthr() failed:%" PRIxSTAT "", stat);

	stat = sync_api_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"sync_api_init_local() failed:%" PRIxSTAT "", stat);

	/* Init the EM CLI locally on this core (only if enabled) */
	stat = emcli_init_local();
	RETURN_ERROR_IF(stat != EM_OK, EM_ERR_LIB_FAILED, escope,
			"emcli_init_local() failed:%" PRIxSTAT "", stat);

	/* Mark this thread as an EM-control thread */
	locm->is_em_core = false;
	locm->is_external_thread = true;

	return EM_OK;
}

void flush_scheduler_events(void)
{
	odp_event_t odp_ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	em_event_t em_ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	int num_events;

	do {
		num_events = odp_schedule_multi_no_wait(NULL, odp_ev_tbl, EM_SCHED_MULTI_MAX_BURST);
		/* the check 'num_events > EM_SCHED_MULTI_MAX_BURST' avoids a gcc warning */
		if (num_events <= 0 || num_events > EM_SCHED_MULTI_MAX_BURST)
			break;
		/*
		 * Events might originate from outside of EM and need init.
		 */
		event_init_odp_multi(odp_ev_tbl, em_ev_tbl/*out*/, ev_hdr_tbl/*out*/,
				     num_events, true/*is_extev*/,
				     NULL/*not a dispatch: always scan*/);
		em_free_multi(em_ev_tbl, num_events);
	} while (num_events > 0);
}

em_status_t term_core(em_escope_t escope)
{
	em_status_t stat = EM_OK;
	em_status_t ret_stat = EM_OK;
	em_locm_t *const locm = &em_locm;
	const int thr = odp_thread_id();

	/* Run EO local-stop funcs for all EOs during term-core (if configured) */
	eo_stop_local_fn_at_term();

	/* continue core termination: */

	int current_core_count = core_map_term_local(&em_shm->core_map, escope);

	RETURN_ERROR_IF(current_core_count < 0, EM_ERR_LIB_FAILED, escope,
			"core_map_term_local(): current_core_count=%d", current_core_count);

	/*
	 * Poll internal unscheduled ctrl queues to complete ctrl actions
	 * and flush them.
	 */
	int32_t cnt = 0;

	do {
		poll_unsched_ctrl_queue();

		cnt = odp_atomic_load_u32(&em_shm->core_map.ongoing_term_reqs[thr]);
		RETURN_ERROR_IF(cnt < 0, EM_ERR_TOO_SMALL, escope,
				"%s(): ongoing_term_reqs[%02d]=%d", __func__, thr, cnt);
	} while (!locm->is_term_thread_done || cnt);

	/*
	 * Flush the scheduler from locally stashed events.
	 */
	if (!locm->is_sched_paused) {
		locm->is_sched_paused = true;
		odp_schedule_pause();
	}
	flush_scheduler_events();

	/* Stop EM Timer. Just a NOP if timer was not enabled (config) */
	stat = timer_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, escope,
			       "timer_term_local() fails: %" PRIxSTAT "", stat);
	}

	/* Term the EM CLI locally (if enabled) */
	stat = emcli_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, escope,
			       "emcli_term_local() fails: %" PRIxSTAT "", stat);
	}

	/* Delete the local queues */
	stat = queue_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, escope,
			       "queue_term_local() fails: %" PRIxSTAT "", stat);
	}

	bool flush_orphan_qgrps = em_shm->opt.queue_group.term_local_flush_orphans;

	queue_group_leave_all(flush_orphan_qgrps);

	locm->is_em_core = false;
	locm->is_external_thread = false;

	return ret_stat == EM_OK ? EM_OK : EM_ERR;
}

em_status_t term_extthr(em_escope_t escope)
{
	em_status_t stat = EM_OK;
	em_status_t ret_stat = EM_OK;
	em_locm_t *const locm = &em_locm;

	int current_core_count = core_map_term_extthr(&em_shm->core_map, escope);

	RETURN_ERROR_IF(current_core_count < 0, EM_ERR_LIB_FAILED, escope,
			"core_map_term_extthr(): current_core_count=%d", current_core_count);

	/*
	 * Poll internal unscheduled ctrl queues to complete ctrl actions
	 * and flush them.
	 */
	do {
		poll_unsched_ctrl_queue();
	} while (!locm->is_term_thread_done);

	/* Term the EM CLI locally (if enabled) */
	stat = emcli_term_local();
	if (stat != EM_OK) {
		ret_stat = stat;
		INTERNAL_ERROR(stat, escope,
			       "emcli_term_local() fails: %" PRIxSTAT "", stat);
	}

	locm->is_em_core = false;
	locm->is_external_thread = false;

	return ret_stat == EM_OK ? EM_OK : EM_ERR;
}

static void fill_startup_pools_str(char *startup_pools_str,
				   size_t startup_pools_str_size,
				   uint32_t num_startup_pools)
{
	size_t offset = 0;

	for (uint32_t i = 0; i < num_startup_pools; i++) {
		const em_startup_pool_conf_t *conf = &em_shm->opt.startup_pools.conf[i];

		/* event_type */
		const char *event_type_str = "UNKNOWN";

		if (conf->cfg.event_type == EM_EVENT_TYPE_SW)
			event_type_str = "EM_EVENT_TYPE_SW";
		else if (conf->cfg.event_type == EM_EVENT_TYPE_PACKET)
			event_type_str = "EM_EVENT_TYPE_PACKET";
		else if (conf->cfg.event_type == EM_EVENT_TYPE_VECTOR)
			event_type_str = "EM_EVENT_TYPE_VECTOR";

		offset += snprintf(startup_pools_str + offset,
				   startup_pools_str_size - offset,
				   "  conf[%u]:\n"
				   "    name: %s\n"
				   "    pool: %" PRI_POOL "\n"
				   "    pool_cfg.event_type: %s\n"
				   "    pool_cfg.align_offset.in_use: %s\n"
				   "    pool_cfg.align_offset.value: %u\n"
				   "    pool_cfg.user_area.in_use: %s\n"
				   "    pool_cfg.user_area.size: %zu\n"
				   "    pool_cfg.pkt.headroom.in_use: %s\n"
				   "    pool_cfg.pkt.headroom.value: %u\n"
				   "    pool_cfg.num_subpools: %u\n", i,
				   conf->name[0] ? conf->name : "(not set)",
				   conf->pool, event_type_str,
				   conf->cfg.align_offset.in_use ? "true" : "false",
				   conf->cfg.align_offset.value,
				   conf->cfg.user_area.in_use ? "true" : "false",
				   conf->cfg.user_area.size,
				   conf->cfg.pkt.headroom.in_use ? "true" : "false",
				   conf->cfg.pkt.headroom.value,
				   conf->cfg.num_subpools);

		for (int j = 0; j < conf->cfg.num_subpools; j++) {
			offset += snprintf(startup_pools_str + offset,
					   startup_pools_str_size - offset,
					   "    pool_cfg.subpool[%d].size: %u\n"
					   "    pool_cfg.subpool[%d].num: %u\n"
					   "    pool_cfg.subpool[%d].cache_size: %u\n",
					   j, conf->cfg.subpool[j].size,
					   j, conf->cfg.subpool[j].num,
					   j, conf->cfg.subpool[j].cache_size);
		}
	}
}

void cfgfile_opts_print(void)
{
	size_t offset = 0;
	char *startup_pools_str = NULL;
	const uint32_t custom_map_size = 64;
	char queue_priority_custom_map[custom_map_size];
	uint32_t num_startup_pools = em_shm->opt.startup_pools.num;
	size_t startup_pools_str_size = 1000 * num_startup_pools;

	/* Allocate buffer based on actual number of pools: ~1000 bytes per pool */
	if (num_startup_pools) {
		startup_pools_str = malloc(startup_pools_str_size);
		if (!startup_pools_str) {
			EM_LOG(EM_LOG_ERR, "Failed to malloc(%zu) for startup_pools_str\n",
			       startup_pools_str_size);
			return;
		}
		fill_startup_pools_str(startup_pools_str, startup_pools_str_size,
				       num_startup_pools);
	}

	if (em_shm->opt.queue.priority.map_mode != 2) {
		queue_priority_custom_map[0] = '\0';
	} else {
		offset += snprintf(queue_priority_custom_map + offset,
				   custom_map_size - offset,
				   "  priority.custom_map: [");

		for (int i = 0; i < EM_QUEUE_PRIO_NUM - 1; i++) {
			offset += snprintf(queue_priority_custom_map + offset,
					   custom_map_size - offset, "%d, ",
					   em_shm->opt.queue.priority.custom_map[i]);
		}

		snprintf(queue_priority_custom_map + offset,
			 custom_map_size - offset, "%d]\n",
			 em_shm->opt.queue.priority.custom_map[EM_QUEUE_PRIO_NUM - 1]);
	}

	EM_PRINT("\n"
		 "======================\n"
		 "EM Config File Options\n"
		 "======================\n"
		 "\ncli:\n"
		 "  enable: %s\n"
		 "  ip_addr: %s\n"
		 "  port: %d\n"
		 "\ndispatch:\n"
		 "  poll_ctrl_interval: %u\n"
		 "  poll_ctrl_interval_ns: %" PRIu64 " ns\n"
		 "  poll_drain_interval: %u\n"
		 "  poll_drain_interval_ns: %" PRIu64 " ns\n"
		 "  sched_wait_ns: %" PRIu64 " ns\n"
		 "  sched_pause: %s\n"
		 "\neo:\n"
		 "  start_local_fn_at_init: %s\n"
		 "  stop_local_fn_at_term: %s\n"
		 "\nesv:\n"
		 "  enable: %s\n"
		 "  store_state: %s\n"
		 "  store_payload_first_u32: %s\n"
		 "  prealloc_pools: %s\n"
		 "\nevent_chaining:\n"
		 "  order_keep: %s\n"
		 "  num_order_queues: %u\n"
		 "\npool:\n"
		 "  statistics.available: %u\n"
		 "  statistics.alloc_ops: %u\n"
		 "  statistics.alloc_fails: %u\n"
		 "  statistics.free_ops: %u\n"
		 "  statistics.total_ops: %u\n"
		 "  statistics.cache_available: %u\n"
		 "  statistics.cache_alloc_ops: %u\n"
		 "  statistics.cache_free_ops: %u\n"
		 "  statistics.core_cache_available: %u\n"
		 "  align_offset: %u bytes\n"
		 "  user_area_size: %zu bytes\n"
		 "  pkt_headroom: %u bytes\n"
		 "\nqueue:\n"
		 "  num_static: %u\n"
		 "  num_dynamic: %u\n"
		 "  min_events_default: %u\n"
		 "  priority.map_mode: %d\n%s\n"
		 "\nqueue_group:\n"
		 "  create_core_queue_groups: %s\n"
		 "  term_local_flush_orphans: %s\n"
		 "\ntimer:\n"
		 "  shared_tmo_pool_enable: %s\n"
		 "  shared_tmo_pool_size: %u\n"
		 "  tmo_pool_cache: %u\n"
		 "  ring.timer_event_pool_size: %u\n"
		 "  ring.timer_event_pool_cache: %u\n"
		 "\nvector:\n"
		 "  backend: %s\n"
		 "\nstartup_pools:\n"
		 "  num: %u\n%s\n",
		 em_shm->opt.cli.enable ? "true" : "false",
		 em_shm->opt.cli.ip_addr ? em_shm->opt.cli.ip_addr : "(null)",
		 em_shm->opt.cli.port,
		 em_shm->opt.dispatch.poll_ctrl_interval,
		 em_shm->opt.dispatch.poll_ctrl_interval_ns,
		 em_shm->opt.dispatch.poll_drain_interval,
		 em_shm->opt.dispatch.poll_drain_interval_ns,
		 em_shm->opt.dispatch.sched_wait_ns,
		 em_shm->opt.dispatch.sched_pause ? "true" : "false",
		 em_shm->opt.eo.start_local_fn_at_init ? "true" : "false",
		 em_shm->opt.eo.stop_local_fn_at_term ? "true" : "false",
		 em_shm->opt.esv.enable ? "true" : "false",
		 em_shm->opt.esv.store_state ? "true" : "false",
		 em_shm->opt.esv.store_first_u32 ? "true" : "false",
		 em_shm->opt.esv.prealloc_pools ? "true" : "false",
		 em_shm->opt.event_chaining.order_keep ? "true" : "false",
		 em_shm->opt.event_chaining.num_order_queues,
		 em_shm->opt.pool.statistics.available,
		 em_shm->opt.pool.statistics.alloc_ops,
		 em_shm->opt.pool.statistics.alloc_fails,
		 em_shm->opt.pool.statistics.free_ops,
		 em_shm->opt.pool.statistics.total_ops,
		 em_shm->opt.pool.statistics.cache_available,
		 em_shm->opt.pool.statistics.cache_alloc_ops,
		 em_shm->opt.pool.statistics.cache_free_ops,
		 em_shm->opt.pool.statistics.core_cache_available,
		 em_shm->opt.pool.align_offset,
		 em_shm->opt.pool.user_area_size,
		 em_shm->opt.pool.pkt_headroom,
		 em_shm->opt.queue.num_static,
		 em_shm->opt.queue.num_dynamic,
		 em_shm->opt.queue.min_events_default,
		 em_shm->opt.queue.priority.map_mode,
		 queue_priority_custom_map,
		 em_shm->opt.queue_group.create_core_queue_groups ? "true" : "false",
		 em_shm->opt.queue_group.term_local_flush_orphans ? "true" : "false",
		 em_shm->opt.timer.shared_tmo_pool_enable ? "true" : "false",
		 em_shm->opt.timer.shared_tmo_pool_size,
		 em_shm->opt.timer.tmo_pool_cache,
		 em_shm->opt.timer.ring.timer_event_pool_size,
		 em_shm->opt.timer.ring.timer_event_pool_cache,
		 em_shm->opt.vector.backend_str,
		 num_startup_pools,
		 startup_pools_str ? startup_pools_str : "");

	if (startup_pools_str)
		free(startup_pools_str);
}

void conf_local_opts_print(void)
{
	const char *core_type_str;
	const em_locm_t *const locm = &em_locm;
	const em_conf_local_t *conf_local = &locm->conf_local;

	/* Format core type string */
	switch (conf_local->core_type) {
	case EM_CORE_TYPE_WORKER:
		core_type_str = "EM_CORE_TYPE_WORKER";
		break;
	case EM_CORE_TYPE_CONTROL:
		core_type_str = "EM_CORE_TYPE_CONTROL";
		break;
	case EM_CORE_TYPE_EXTERNAL:
		core_type_str = "EM_CORE_TYPE_EXTERNAL";
		break;
	default:
		core_type_str = "UNKNOWN";
		break;
	}

	EM_PRINT("\n"
		 "============================================\n"
		 "EM core-local config given to em_init_local()\n"
		 "or used by em_init_core()\n"
		 "============================================\n"
		 "\ncore_type: %s\n"
		 "core_id: %d\n"
		 "input_poll_fn: %s\n"
		 "output_drain_fn: %s\n"
		 "\n",
		 core_type_str,
		 conf_local->core_id,
		 conf_local->input_poll_fn ? "(set)" : "(not set)",
		 conf_local->output_drain_fn ? "(set)" : "(not set)");
}

void conf_opts_print(void)
{
	const em_conf_t *conf = &em_shm->conf;
	char phys_mask_str[EM_CORE_MASK_STRLEN];
	char default_pool_str[1024];
	size_t offset = 0;

	/* Format physical core mask */
	em_core_mask_tostr(phys_mask_str, sizeof(phys_mask_str), &conf->phys_mask);

	/* Format default pool config */
	const em_pool_cfg_t *pool_cfg = &conf->default_pool_cfg;
	const char *event_type_str = "UNKNOWN";

	if (pool_cfg->event_type == EM_EVENT_TYPE_SW)
		event_type_str = "EM_EVENT_TYPE_SW";
	else if (pool_cfg->event_type == EM_EVENT_TYPE_PACKET)
		event_type_str = "EM_EVENT_TYPE_PACKET";
	else if (pool_cfg->event_type == EM_EVENT_TYPE_VECTOR)
		event_type_str = "EM_EVENT_TYPE_VECTOR";

	offset += snprintf(default_pool_str + offset,
			   sizeof(default_pool_str) - offset,
			   "  event_type: %s\n"
			   "  align_offset.in_use: %s\n"
			   "  align_offset.value: %u\n"
			   "  user_area.in_use: %s\n"
			   "  user_area.size: %zu\n"
			   "  pkt.headroom.in_use: %s\n"
			   "  pkt.headroom.value: %u\n"
			   "  num_subpools: %u\n",
			   event_type_str,
			   pool_cfg->align_offset.in_use ? "true" : "false",
			   pool_cfg->align_offset.value,
			   pool_cfg->user_area.in_use ? "true" : "false",
			   pool_cfg->user_area.size,
			   pool_cfg->pkt.headroom.in_use ? "true" : "false",
			   pool_cfg->pkt.headroom.value,
			   pool_cfg->num_subpools);

	for (int i = 0; i < pool_cfg->num_subpools; i++) {
		offset += snprintf(default_pool_str + offset,
				   sizeof(default_pool_str) - offset,
				   "  subpool[%d].size: %u\n"
				   "  subpool[%d].num: %u\n"
				   "  subpool[%d].cache_size: %u\n",
				   i, pool_cfg->subpool[i].size,
				   i, pool_cfg->subpool[i].num,
				   i, pool_cfg->subpool[i].cache_size);
	}

	EM_PRINT("\n"
		 "====================================\n"
		 "EM runtime config given to em_init()\n"
		 "====================================\n"
		 "\ndevice_id: %u\n"
		 "event_timer: %s\n"
		 "thread_per_core: %s\n"
		 "process_per_core: %s\n"
		 "core_count: %u\n"
		 "phys_mask: %s\n"
		 "\ndefault_pool_cfg:\n%s\n"
		 "log.log_fn: %s\n"
		 "log.vlog_fn: %s\n"
		 "\napi_hooks.alloc_hook: %s\n"
		 "api_hooks.free_hook: %s\n"
		 "api_hooks.send_hook: %s\n"
		 "\nidle_hooks.to_idle_hook: %s\n"
		 "idle_hooks.to_active_hook: %s\n"
		 "idle_hooks.while_idle_hook: %s\n"
		 "\n",
		 conf->device_id,
		 conf->event_timer ? "true" : "false",
		 conf->thread_per_core ? "true" : "false",
		 conf->process_per_core ? "true" : "false",
		 conf->core_count,
		 phys_mask_str,
		 default_pool_str,
		 conf->log.log_fn ? "(set)" : "(not set)",
		 conf->log.vlog_fn ? "(set)" : "(not set)",
		 conf->api_hooks.alloc_hook ? "(set)" : "(not set)",
		 conf->api_hooks.free_hook ? "(set)" : "(not set)",
		 conf->api_hooks.send_hook ? "(set)" : "(not set)",
		 conf->idle_hooks.to_idle_hook ? "(set)" : "(not set)",
		 conf->idle_hooks.to_active_hook ? "(set)" : "(not set)",
		 conf->idle_hooks.while_idle_hook ? "(set)" : "(not set)");
}
