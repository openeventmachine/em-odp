/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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

#include <errno.h>
#include <libgen.h> /* basename() */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> /* ssize_t */

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_debug.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#include "em_core.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_inline.h"
#include "em_event_types.h"
#include "em_info.h"
#include "em_mem.h"
#include "em_pool.h"
#include "em_pool_types.h"

int em_error_format_string(char err_msg[/*out*/], size_t errmsg_sz, em_eo_t eo,
			   em_status_t error, em_escope_t escope, va_list args)
{
	if (!EM_ESCOPE(escope) || !err_msg || (ssize_t)errmsg_sz <= 0)
		return 0;

	char eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];

	eo_str[0] = '\0';

	if (eo != EM_EO_UNDEF) {
		char eo_name[EM_EO_NAME_LEN];

		em_eo_name(eo, eo_name, sizeof(eo_name));
		snprintf(eo_str, sizeof(eo_str),
			 "EO:%" PRI_EO "-\"%s\"  ", eo, eo_name);
		/* Both eo_name[] and eo_str[] always '\0'-terminated */
	}

	size_t len = format_errmsg(error, escope, eo_str, args, false,
				   err_msg /*out*/, errmsg_sz);

	return (int)len + 1;
}

void em_version_print(void)
{
	print_version_info();
}

void em_info_print(void)
{
	print_em_info();
}

int em_core_id_physical(int em_core_id)
{
	const em_locm_t *const locm = &em_locm;
	int phys_core;

	if (unlikely((const unsigned int)em_core_id >= EM_MAX_CORES))
		return -1;

	if (locm->core_id == em_core_id)
		return odp_cpu_id();

	core_map_t *const core_map = &em_shm->core_map;
	char path[32]; /* path to /proc/[tid]/stat */

	odp_rwlock_read_lock(&core_map->rwlock);
	memcpy(path, core_map->rwlocked.proc_tid_stat_paths[em_core_id], sizeof(path));
	odp_rwlock_read_unlock(&core_map->rwlock);

	if (unlikely(path[0] == '\0'))
		return -1;

	FILE *file = fopen(path, "r");

	if (!file) {
		EM_LOG(EM_LOG_ERR, "fopen(%s) failed: %s (%d)\n",
		       path, strerror(errno), errno);
		return -1;
	}

	phys_core = read_proc_cpuid(file);
	fclose(file);

	return phys_core;
}

void em_core_mask_physical(em_core_mask_t *mask_phys /*out*/,
			   const em_core_mask_t *mask_emcore)
{
	core_map_t *const core_map = &em_shm->core_map;
	const int mask_cnt = em_core_mask_count(mask_emcore);
	int core_em = em_core_mask_first(mask_emcore);

	em_core_mask_zero(mask_phys);

	if (unlikely(mask_cnt <= 0 || mask_cnt > EM_MAX_CORES || core_em < 0))
		return;

	char paths[mask_cnt][32]; /* paths to /proc/[tid]/stat */

	odp_rwlock_read_lock(&core_map->rwlock);

	int f_cnt = 0;

	while (core_em >= 0 && f_cnt < mask_cnt) {
		memcpy(paths[f_cnt], core_map->rwlocked.proc_tid_stat_paths[core_em],
		       sizeof(paths[f_cnt]));

		if (unlikely(paths[f_cnt][0] == '\0')) {
			odp_rwlock_read_unlock(&core_map->rwlock);
			return;
		}
		core_em = em_core_mask_next(mask_emcore, core_em);
		f_cnt++;
	}

	odp_rwlock_read_unlock(&core_map->rwlock);

	for (int i = 0; i < f_cnt; i++) {
		FILE *file = fopen(paths[i], "r");
		int phys_core;

		if (!file) {
			EM_LOG(EM_LOG_ERR, "fopen(%s) failed: %s (%d)\n",
			       paths[i], strerror(errno), errno);
			return;
		}
		phys_core = read_proc_cpuid(file);
		fclose(file);

		if (phys_core >= 0)
			em_core_mask_set(phys_core, mask_phys);
	}
}

uint64_t em_debug_timestamp(em_debug_tsp_t tsp)
{
	if (EM_DEBUG_TIMESTAMP_ENABLE == 0)
		return 0;

	if (unlikely(tsp >= EM_DEBUG_TSP_LAST || tsp < 0))
		return 0;
	else
		return em_locm.debug_ts[tsp];
}
