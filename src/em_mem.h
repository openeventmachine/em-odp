/*
 *   Copyright (c) 2015-2026, Nokia Solutions and Networks
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
 * EM Shared & Local Memory data
 *
 */

#ifndef EM_MEM_H_
#define EM_MEM_H_

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h> /* pid_t */

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_debug.h>
#include <event_machine/platform/env/environment.h>

/*
 * Bring in every internal type referenced by em_shm_t / em_locm_t.
 * Most are embedded by value; even the pointer-typed fields (e.g. hook_tbl_t*,
 * event_group_elem_t*) require their containing type for sizeof in callers,
 * so include the full definitions.
 */
#include "em_atomic_group_types.h"
#include "em_chaining_types.h"
#include "em_core_types.h"
#include "em_dispatcher_types.h"
#include "em_eo_types.h"
#include "em_error_types.h"
#include "em_event_group_types.h"
#include "em_event_types.h"
#include "em_hook_types.h"
#include "em_init_types.h"
#include "em_internal_event_types.h"
#include "em_libconfig_types.h"
#include "em_pool_types.h"
#include "em_queue_group_types.h"
#include "em_queue_types.h"
#include "em_sync_api_types.h"
#include "em_timer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM shared memory data
 *
 * Struct contains data that is shared between all EM-cores,
 * i.e. shared between all EM-processes or EM-threads depending on the setup.
 */
typedef struct {
	/** Handle for this shared memory */
	odp_shm_t this_shm;
	/** EM internal log function, overridable via em_conf, var args */
	em_log_func_t log_fn;
	/** EM internal log function, overridable via em_conf, va_list */
	em_vlog_func_t vlog_fn;
	/** EM configuration as given to em_init() */
	em_conf_t conf;
	/** Indicates if EM has been initialized successfully with em_init() */
	bool is_init;
	/** Cache EM config file options */
	cfgfile_opt_cache_t opt_cache;
	/** EM config file options */
	em_cfgfile_opts_t opt;
	/** Mapping between physical core id <-> EM core id */
	core_map_t core_map ENV_CACHE_LINE_ALIGNED;
	/** Table of buffer/packet/event pools used by EM */
	mpool_tbl_t mpool_tbl ENV_CACHE_LINE_ALIGNED;
	/** Pool of free event/mempools */
	mpool_pool_t mpool_pool ENV_CACHE_LINE_ALIGNED;
	/** EO table */
	eo_tbl_t eo_tbl ENV_CACHE_LINE_ALIGNED;
	/** EO pool of free/unused EOs */
	eo_pool_t eo_pool ENV_CACHE_LINE_ALIGNED;
	/** Event Chaining resources */
	event_chaining_t event_chaining ENV_CACHE_LINE_ALIGNED;
	/** Queue table */
	queue_tbl_t queue_tbl ENV_CACHE_LINE_ALIGNED;
	/** Queue pool of free/unused dynamic queues */
	queue_pool_t queue_pool ENV_CACHE_LINE_ALIGNED;
	/** Queue pool of free/unused static queues */
	queue_pool_t queue_pool_static ENV_CACHE_LINE_ALIGNED;
	/** Queue pool of free/unused aggregator queues */
	queue_pool_t queue_pool_aggr ENV_CACHE_LINE_ALIGNED;
	/** Queue group table */
	queue_group_tbl_t queue_group_tbl ENV_CACHE_LINE_ALIGNED;
	/** Queue group pool of free/unused queue groups */
	queue_group_pool_t queue_group_pool ENV_CACHE_LINE_ALIGNED;
	/** Atomic group table */
	atomic_group_tbl_t atomic_group_tbl ENV_CACHE_LINE_ALIGNED;
	/** Dynamic atomic group pool */
	atomic_group_pool_t atomic_group_pool ENV_CACHE_LINE_ALIGNED;
	/** Event group table */
	event_group_tbl_t event_group_tbl ENV_CACHE_LINE_ALIGNED;
	/** Event group stash of free/unused queue groups */
	odp_stash_t event_group_stash ENV_CACHE_LINE_ALIGNED;
	/** Error handler structure */
	error_handler_t error_handler ENV_CACHE_LINE_ALIGNED;

	/** Dispatcher enter callback functions currently in use */
	hook_tbl_t *dispatch_enter_cb_tbl ENV_CACHE_LINE_ALIGNED;
	/** Dispatcher exit callback functions currently in use */
	hook_tbl_t *dispatch_exit_cb_tbl;
	/** Alloc-hook functions currently in use */
	hook_tbl_t *alloc_hook_tbl;
	/** Free-hook functions currently in use */
	hook_tbl_t *free_hook_tbl;
	/** Send-hook functions currently in use */
	hook_tbl_t *send_hook_tbl;
	/** To_idle hook functions currently in use */
	hook_tbl_t *to_idle_hook_tbl;
	/** To_active hook functions currently in use */
	hook_tbl_t *to_active_hook_tbl;
	/** While_idle hook functions currently in use */
	hook_tbl_t *while_idle_hook_tbl;

	/** Dispatch enter callback storage, many sets of callback-tables */
	hook_storage_t dispatch_enter_cb_storage ENV_CACHE_LINE_ALIGNED;
	/** Dispatch exit callback storage, many sets of callback-tables */
	hook_storage_t dispatch_exit_cb_storage ENV_CACHE_LINE_ALIGNED;
	/** Alloc-hook function storage, many sets of hook-tables */
	hook_storage_t alloc_hook_storage ENV_CACHE_LINE_ALIGNED;
	/** Free-hook function storage, many sets of hook-tables */
	hook_storage_t free_hook_storage ENV_CACHE_LINE_ALIGNED;
	/** Send-hook function storage, many sets of hook-tables */
	hook_storage_t send_hook_storage ENV_CACHE_LINE_ALIGNED;
	/** To_idle hook functions storage, many sets of hook-tables */
	hook_storage_t to_idle_hook_storage;
	/** To_active hook functions storage, many sets of hook-tables */
	hook_storage_t to_active_hook_storage;
	/** While_idle hook functions storage, many sets of hook-tables */
	hook_storage_t while_idle_hook_storage;

	/** Timer resources */
	timer_storage_t timers ENV_CACHE_LINE_ALIGNED;

	/** Current number of allocated EOs */
	odp_atomic_u32_t eo_count ENV_CACHE_LINE_ALIGNED;
	/** Current number of allocated queues */
	odp_atomic_u32_t queue_count;
	/** Current number of allocated queue groups */
	odp_atomic_u32_t queue_group_count;
	/** Current number of allocated event groups */
	odp_atomic_u32_t event_group_count;
	/** Current number of allocated atomic groups */
	odp_atomic_u32_t atomic_group_count;
	/** Current number of allocated event pools */
	odp_atomic_u32_t pool_count;

	/** libconfig setting, default (compiled) and runtime (from file) */
	libconfig_t libconfig ENV_CACHE_LINE_ALIGNED;
	/** priority mapping */
	struct {
		/** mapping table */
		int map[EM_QUEUE_PRIO_NUM];
		int num_runtime;
	} queue_prio;

	/** Guarantee that size is a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} em_shm_t;

COMPILE_TIME_ASSERT(sizeof(em_shm_t) % ENV_CACHE_LINE_SIZE == 0,
		    EM_SHM_SIZE_ERROR);

/**
 * EM core/local current state
 *
 * Contains information about the current EO, queue, event group etc. when
 * running in an EO context (e.g. in an EO-receive function),
 * undef/NULL otherwise.
 */
typedef struct ODP_PACKED {
	/** Current scheduling context type */
	em_sched_context_type_t sched_context_type;
	/** EO-receive function burst count */
	int rcv_multi_cnt;
	/** Current queue element during a receive call */
	queue_elem_t *q_elem;
	/** Current scheduled queue element that set the sched context*/
	queue_elem_t *sched_q_elem;
	/** Current event group element (for the current event group) */
	event_group_elem_t *egrp_elem;
	/** Current event group generation count */
	uint32_t egrp_gen;
} em_locm_current_t;

/**
 * EM core local data
 */
typedef struct {
	/** EM core/local current state */
	em_locm_current_t current;

	/** Idle state of the core, used when calling idle hooks */
	idle_state_t idle_state;
	/** Number of dispatch rounds since previous polling of ctrl queues */
	unsigned int dispatch_cnt;
	/** Number of dispatch rounds since previous call of poll/drain functions */
	unsigned int poll_drain_dispatch_cnt;
	/** The number of events from the scheduler to dispatch */
	int event_burst_cnt;
	/** EM core id for this core */
	int core_id;
	/** Local queues, i.e. storage for events to local queues */
	local_queues_t local_queues;
	/** The number of local queue events dispatched during the em_dispatch...() call */
	uint64_t local_event_cnt;
	/** Dispatch callback: input_poll_fn() for this core */
	em_input_poll_func_t input_poll_fn;
	/** Dispatch callback: output_drain_fn() for this core */
	em_output_drain_func_t output_drain_fn;

	/** em_atomic_processing_end() called during event dispatch */
	bool atomic_group_released;
	/** Is the scheduler paused on this core (for odp_sched_pause/resume()) */
	bool is_sched_paused;
	/** Is this an EM worker or control core? Not an external EM thread */
	bool is_em_core;
	/** Is thread external to EM (doesn't participate in event dispatching) */
	bool is_external_thread;
	/** Is thread termination completed? */
	bool is_term_thread_done;
	/** Has core-local initialization completed on this core? */
	bool is_core_local_inited;

	/** Time when polling of ctrl queues where last done */
	odp_time_t dispatch_last_run;
	/** Ctrl queue poll interval as core-local time (converted per-core) */
	odp_time_t poll_ctrl_interval_time;
	/** Time when poll and drain functions were last called */
	odp_time_t poll_drain_dispatch_last_run;
	/** Poll/drain interval as core-local time (converted per-core) */
	odp_time_t poll_drain_interval_time;

	/** PID when TLS was initialized (for detecting forked children) */
	pid_t init_pid;

	/** EO start-function ongoing, buffer all events and send after start */
	eo_elem_t *start_eo_elem;
	/** The number of errors on a core */
	uint64_t error_count;

	/** EM-core local log function */
	em_log_func_t log_fn;

	/** EM-core local log function with va_list */
	em_vlog_func_t vlog_fn;

	/** Synchronous API */
	sync_api_t sync_api;

	/** dispatcher debug timestamps (ns) */
	uint64_t debug_ts[EM_DEBUG_TSP_LAST];

	/** Track output-queues used during this dispatch round (burst) */
	output_queue_track_t output_queue_track;

	/** EM local configuration as passed by em_init_local(conf_local) */
	em_conf_local_t conf_local;

	/**
	 * Non-EM events don't have an event header of their own,
	 * use a temporary core-local event header instead.
	 * Initialized to all-zero with .event_type = EM_EVENT_TYPE_ODP to
	 * avoid having ev_hdr == NULL in the code and checks for it.
	 * Using a tmp event header, instead of NULL checks, speeds up the code.
	 *
	 * em_locm_t::evhdr_nonem.event_type = EM_EVENT_TYPE_ODP;
	 */
	event_hdr_t evhdr_nonem;

	/** Guarantee that size is a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} em_locm_t;

COMPILE_TIME_ASSERT((sizeof(em_locm_t) % ENV_CACHE_LINE_SIZE) == 0,
		    EM_LOCM_SIZE_ERROR);

/** EM shared memory pointer */
extern em_shm_t *em_shm;
/** EM core local memory */
extern ENV_LOCAL em_locm_t em_locm;

#ifdef __cplusplus
}
#endif

#endif /* EM_MEM_H_ */
