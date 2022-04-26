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

	locm->dispatch_cnt = em_shm->opt.dispatch.poll_ctrl_interval;
	locm->dispatch_last_run = ODP_TIME_NULL;

	return EM_OK;
}
