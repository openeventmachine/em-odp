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

static int read_config_file(void)
{
	const char *conf_str;
	int val = 0;
	int64_t val64 = 0;
	int ret;

	/*
	 * Option: dispatch.poll_ctrl_interval
	 */
	conf_str = "dispatch.poll_ctrl_interval";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n",
		       conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.dispatch.poll_ctrl_interval = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	/*
	 * Option: dispatch.poll_ctrl_interval_ns
	 */
	conf_str = "dispatch.poll_ctrl_interval_ns";
	ret = em_libconfig_lookup_int64(&em_shm->libconfig, conf_str, &val64);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val64 < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %" PRId64 "'\n",
		       conf_str, val64);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.dispatch.poll_ctrl_interval_ns = val64;
	long double sec = (long double)val64 / 1000000000.0;

	EM_PRINT("  %s: %" PRId64 "ns (%Lfs)\n", conf_str, val64, sec);

	/* Store ns value as odp_time_t */
	em_shm->opt.dispatch.poll_ctrl_interval_time = odp_time_global_from_ns(val64);

	/*
	 * Option: dispatch.poll_drain_interval
	 */
	conf_str = "dispatch.poll_drain_interval";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d'\n",
		       conf_str, val);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.dispatch.poll_drain_interval = val;
	EM_PRINT("  %s: %d\n", conf_str, val);

	/*
	 * Option: dispatch.poll_drain_interval_ns
	 */
	conf_str = "dispatch.poll_drain_interval_ns";
	ret = em_libconfig_lookup_int64(&em_shm->libconfig, conf_str, &val64);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}

	if (val64 < 0) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %" PRId64 "'\n",
		       conf_str, val64);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.dispatch.poll_drain_interval_ns = val64;
	sec = (long double)val64 / 1000000000.0;

	EM_PRINT("  %s: %" PRId64 "ns (%Lfs)\n", conf_str, val64, sec);

	/* Store ns value as odp_time_t */
	em_shm->opt.dispatch.poll_drain_interval_time = odp_time_global_from_ns(val64);

	/*
	 * Option: dispatch.sched_wait_ns
	 */
	if (EM_SCHED_WAIT_ENABLE) {
		conf_str = "dispatch.sched_wait_ns";
		ret = em_libconfig_lookup_int64(&em_shm->libconfig, conf_str, &val64);
		if (unlikely(!ret)) {
			EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
			return -1;
		}

		if (val64 < -1) {
			EM_LOG(EM_LOG_ERR, "Bad config value '%s = %" PRId64 "'\n",
			       conf_str, val64);
			return -1;
		}

		/* store & print the value */
		em_shm->opt.dispatch.sched_wait_ns = val64;

		/* Store ns value as odp sched-wait-time */
		if (val64 == 0) {
			em_shm->opt.dispatch.sched_wait = ODP_SCHED_NO_WAIT;
			EM_PRINT("  %s: no-wait (0 ns)\n", conf_str);
		} else if (val64 == -1) {
			em_shm->opt.dispatch.sched_wait = ODP_SCHED_WAIT;
			EM_PRINT("  %s: wait indefinitely (inf. ns)\n", conf_str);
		} else {
			em_shm->opt.dispatch.sched_wait = odp_schedule_wait_time(val64);
			sec = (long double)val64 / 1000000000.0;
			EM_PRINT("  %s: wait %" PRId64 "ns (%Lfs)\n", conf_str, val64, sec);
		}
	}

	return 0;
}

em_status_t dispatch_init(void)
{
	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

em_status_t dispatch_init_local(void)
{
	em_locm_t *const locm = &em_locm;
	odp_time_t poll_period = em_shm->opt.dispatch.poll_ctrl_interval_time;
	odp_time_t now = odp_time_global();

	/*
	 * Initialize values so that the _first_ call to em_dispatch() on each
	 * core will trigger a poll of the unscheduled ctrl queues.
	 */
	locm->dispatch_cnt = 1;
	locm->dispatch_last_run = odp_time_diff(now, poll_period); /* wrap OK */

	/*
	 * Sanity check:
	 * Perform the same calculation as in dispatch_poll_ctrl_queue()
	 * to verify that ctrl queue polling is triggered by the first dispatch.
	 */
	odp_time_t period = odp_time_diff(now, locm->dispatch_last_run);

	if (odp_time_cmp(period, poll_period) != 0) /* 0: periods equal */
		return EM_ERR_TOONEAR;

	locm->poll_drain_dispatch_cnt = em_shm->opt.dispatch.poll_drain_interval;
	locm->poll_drain_dispatch_last_run = now;

	locm->idle_state = IDLE_STATE_ACTIVE;

	return EM_OK;
}
