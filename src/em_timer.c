/*
 *   Copyright (c) 2017, Nokia Solutions and Networks
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

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_chaining.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"
#include "em_timer.h"
#include "em_timer_types.h"

static int read_config_file(void);

em_status_t timer_init(timer_storage_t *const tmrs)
{
	for (int i = 0; i < EM_ODP_MAX_TIMERS; i++) {
		memset(&tmrs->timer[i], 0, sizeof(event_timer_t));
		tmrs->timer[i].idx = i; /* for fast reverse lookup */
		tmrs->timer[i].tmo_pool = ODP_POOL_INVALID;
		tmrs->timer[i].odp_tmr_pool = ODP_TIMER_POOL_INVALID;
	}
	tmrs->ring_tmo_pool = ODP_POOL_INVALID;
	tmrs->shared_tmo_pool = ODP_POOL_INVALID;
	tmrs->ring_reserved = 0;
	tmrs->reserved = 0;
	tmrs->num_rings = 0;
	tmrs->num_timers = 0;
	tmrs->num_ring_create_calls = 0;
	tmrs->init_check = EM_CHECK_INIT_CALLED;
	odp_ticketlock_init(&tmrs->timer_lock);

	if (read_config_file() < 0)
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

em_status_t timer_init_local(void)
{
	return EM_OK;
}

em_status_t timer_term_local(void)
{
	return EM_OK;
}

em_status_t timer_term(timer_storage_t *const tmrs)
{
#if PRE_1_50_ODP_TIMER_API
	/* ring_tmo_pool is managed by em-odp only on old ODP API.
	 * New ODP API creates/destroys it as part of periodic timer pool lifecycle.
	 */
	if (tmrs && tmrs->ring_tmo_pool != ODP_POOL_INVALID) {
		if (odp_pool_destroy(tmrs->ring_tmo_pool) != 0)
			return EM_ERR_LIB_FAILED;
		tmrs->ring_tmo_pool = ODP_POOL_INVALID;
	}
#else
	(void)tmrs;
#endif
	return EM_OK;
}

static int read_config_file(void)
{
	const char *conf_str;
	int val = 0;
	int ret;
	bool bval;

	EM_PRINT("EM timer config:\n");

	/*
	 * Option: timer.shared_tmo_pool_enable
	 */
	conf_str = "timer.shared_tmo_pool_enable";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &bval);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.timer.shared_tmo_pool_enable = bval;
	EM_PRINT("  %s: %s\n", conf_str, bval ? "true" : "false");

	/*
	 * Option: timer.shared_tmo_pool_size
	 */
	if (em_shm->opt.timer.shared_tmo_pool_enable) {
		conf_str = "timer.shared_tmo_pool_size";
		ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
		if (unlikely(!ret)) {
			EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
			return -1;
		}

		if (val < 1) {
			EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n", conf_str, val);
			return -1;
		}
		/* store & print the value */
		em_shm->opt.timer.shared_tmo_pool_size = val;
		EM_PRINT("  %s: %d\n", conf_str, val);
	}

	/*
	 * Option: timer.tmo_pool_cache
	 */
	conf_str = "timer.tmo_pool_cache";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n", conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.timer.tmo_pool_cache = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	/*
	 * Option: timer.ring.timer_event_pool_size
	 */
	conf_str = "timer.ring.timer_event_pool_size";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n", conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.timer.ring.timer_event_pool_size = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	/*
	 * Option: timer.ring.timer_event_pool_cache
	 */
	conf_str = "timer.ring.timer_event_pool_cache";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n", conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.timer.ring.timer_event_pool_cache = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	return 0;
}
