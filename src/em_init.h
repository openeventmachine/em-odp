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
 * EM config options read from the config file.
 *
 * See the config/em-odp.conf file for description of the options.
 */
typedef struct {
	struct {
		int statistics_enable; /* true/false */
		unsigned int align_offset; /* bytes */
		unsigned int pkt_headroom; /* bytes */
	} pool;

	struct {
		unsigned int min_events_default; /* default min nbr of events */
	} queue;

	struct {
		unsigned int num_order_queues;
	} event_chaining;

	struct {
		int enable;
		int store_first_u32;
		int prealloc_pools;
	} esv;
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

#ifdef __cplusplus
}
#endif

#endif /* EM_INIT_H_ */
