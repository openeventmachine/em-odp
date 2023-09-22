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

#ifndef EM_INIT_H_
#define EM_INIT_H_

/**
 * @file
 * EM internal initialization types & definitions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialization status info
 */
typedef struct {
	/** init lock */
	env_spinlock_t lock;
	/** Is em_init() completed? */
	int em_init_done;
	/** The number of EM cores that have run em_init_core() */
	int em_init_core_cnt;
} init_t;

/**
 * Pool configuration
 */
typedef struct {
	em_pool_t pool;
	char name[EM_POOL_NAME_LEN];
	em_pool_cfg_t cfg;
} startup_pool_conf_t;

/**
 * EM config options read from the config file.
 *
 * See the config/em-odp.conf file for description of the options.
 */
typedef struct {
	struct {
		em_pool_stats_opt_t statistics;
		unsigned int align_offset; /* bytes */
		unsigned int pkt_headroom; /* bytes */
		size_t user_area_size; /* bytes */
	} pool;

	struct {
		bool create_core_queue_groups;
	} queue_group;

	struct {
		unsigned int min_events_default; /* default min nbr of events */
		struct {
		int map_mode;
		int custom_map[EM_QUEUE_PRIO_NUM];
		} priority;
	} queue;

	struct {
		bool order_keep;
		unsigned int num_order_queues;
	} event_chaining;

	struct {
		int enable;
		int store_state;
		int store_first_u32;
		int prealloc_pools;
	} esv;

	struct {
		int enable;
		const char *ip_addr;
		int port;
	} cli;

	struct {
		unsigned int poll_ctrl_interval;
		uint64_t poll_ctrl_interval_ns;
		/** convert option 'poll_ctrl_interval_ns' to odp_time_t */
		odp_time_t poll_ctrl_interval_time;

		unsigned int poll_drain_interval;
		uint64_t poll_drain_interval_ns;
		odp_time_t poll_drain_interval_time;

		uint64_t sched_wait_ns;
		uint64_t sched_wait; /* odp_schedule_wait_time(sched_wait_ns) */
	} dispatch;

	struct {
		bool shared_tmo_pool_enable;
		uint32_t shared_tmo_pool_size;
		uint32_t tmo_pool_cache;
		struct {
			uint32_t timer_event_pool_size;
			uint32_t timer_event_pool_cache;
		} ring;
	} timer;

	struct {
		uint32_t num;
		startup_pool_conf_t conf[EM_CONFIG_POOLS];
	} startup_pools;
} opt_t;

em_status_t
poll_drain_mask_check(const em_core_mask_t *logic_mask,
		      const em_core_mask_t *poll_drain_mask);

em_status_t
input_poll_init(const em_core_mask_t *logic_mask, const em_conf_t *conf);

em_status_t
output_drain_init(const em_core_mask_t *logic_mask, const em_conf_t *conf);

em_status_t
poll_drain_mask_set_local(bool *const result /*out*/, int core_id,
			  const em_core_mask_t *mask);

em_status_t
input_poll_init_local(bool *const result /*out*/, int core_id,
		      const em_conf_t *conf);

em_status_t
output_drain_init_local(bool *const result /*out*/, int core_id,
			const em_conf_t *conf);

/**
 * Set EM core local log function.
 *
 * Called by EM-core (= process, thread or bare metal core) when a
 * different log function than EM internal log is needed.
 *
 */
void
core_log_fn_set(em_log_func_t func);

/**
 * Set EM core local log function with va_list.
 *
 * Called by EM-core (= process, thread or bare metal core) when a
 * different log function than EM internal log is needed.
 *
 */
void core_vlog_fn_set(em_vlog_func_t func);

/**
 * Initialize a thread external to EM.
 *
 * This function makes sure that EM shared memory has been setup properly before
 * an EM external thread is created.
 *
 * Must be called once by non EM core which wants to access EM shared memory or
 * use EM APIs.
 *
 * @return EM_OK if successful.
 */
em_status_t init_ext_thread(void);

em_status_t sync_api_init_local(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_INIT_H_ */
