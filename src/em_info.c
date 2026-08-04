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

/* Copyright (c) 2018, Linaro Limited
 * Copyright (c) 2020-2021, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/* sysinfo printouts from the ODP example: odp_sysinfo.c */
/* SW ISA detection from the <arch>/odp_sysinfo_parse.c files */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_atomic_group.h"
#include "em_chaining.h"
#include "em_core.h"
#include "em_dispatcher.h"
#include "em_eo.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_group.h"
#include "em_hooks.h"
#include "em_info.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_pool.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_timer.h"

typedef struct {
	/* EM-cores: */
	int logic_cores[EM_MAX_CORES];
	int phys_cores[EM_MAX_CORES];
	int thr_ids[EM_MAX_CORES];
	pid_t tids[EM_MAX_CORES];
	em_core_type_t core_types[EM_MAX_CORES];
	char proc_tid_stat_paths[EM_MAX_CORES][32];

	/* External EM threads/processes: */
	pid_t ext_tids[ODP_THREAD_COUNT_MAX];
	int ext_thr_ids[ODP_THREAD_COUNT_MAX];
} current_core_map_t;

static ENV_LOCAL current_core_map_t current_core_map;

static const char *cpu_arch_name(odp_cpu_arch_t cpu_arch)
{
	switch (cpu_arch) {
	case ODP_CPU_ARCH_ARM:
		return "ARM";
	case ODP_CPU_ARCH_MIPS:
		return "MIPS";
	case ODP_CPU_ARCH_PPC:
		return "PPC";
	case ODP_CPU_ARCH_RISCV:
		return "RISC-V";
	case ODP_CPU_ARCH_X86:
		return "x86";
	default:
		return "Unknown";
	}
}

static const char *arm_isa_name(odp_cpu_arch_arm_t isa)
{
	switch (isa) {
	case ODP_CPU_ARCH_ARMV6:
		return "ARMv6";
	case ODP_CPU_ARCH_ARMV7:
		return "ARMv7-A";
	case ODP_CPU_ARCH_ARMV8_0:
		return "ARMv8.0-A";
	case ODP_CPU_ARCH_ARMV8_1:
		return "ARMv8.1-A";
	case ODP_CPU_ARCH_ARMV8_2:
		return "ARMv8.2-A";
	case ODP_CPU_ARCH_ARMV8_3:
		return "ARMv8.3-A";
	case ODP_CPU_ARCH_ARMV8_4:
		return "ARMv8.4-A";
	case ODP_CPU_ARCH_ARMV8_5:
		return "ARMv8.5-A";
	case ODP_CPU_ARCH_ARMV8_6:
		return "ARMv8.6-A";
	case ODP_CPU_ARCH_ARMV8_7:
		return "ARMv8.7-A";
	case ODP_CPU_ARCH_ARMV9_0:
		return "ARMv9.0-A";
	case ODP_CPU_ARCH_ARMV9_1:
		return "ARMv9.1-A";
	case ODP_CPU_ARCH_ARMV9_2:
		return "ARMv9.2-A";
	case ODP_CPU_ARCH_ARMV8_8:
		return "ARMv8.8-A";
	case ODP_CPU_ARCH_ARMV8_9:
		return "ARMv8.9-A";
	case ODP_CPU_ARCH_ARMV9_3:
		return "ARMv9.3-A";
	default:
		return "Unknown";
	}
}

static const char *x86_isa_name(odp_cpu_arch_x86_t isa)
{
	switch (isa) {
	case ODP_CPU_ARCH_X86_I686:
		return "x86_i686";
	case ODP_CPU_ARCH_X86_64:
		return "x86_64";
	default:
		return "Unknown";
	}
}

static const char *cpu_arch_isa_name(odp_cpu_arch_t cpu_arch,
				     odp_cpu_arch_isa_t cpu_arch_isa)
{
	switch (cpu_arch) {
	case ODP_CPU_ARCH_ARM:
		return arm_isa_name(cpu_arch_isa.arm);
	case ODP_CPU_ARCH_MIPS:
		return "Unknown";
	case ODP_CPU_ARCH_PPC:
		return "Unknown";
	case ODP_CPU_ARCH_RISCV:
		return "Unknown";
	case ODP_CPU_ARCH_X86:
		return x86_isa_name(cpu_arch_isa.x86);
	default:
		return "Unknown";
	}
}

/*
 * Detect the ARM ISA used when compiling the em-odp library.
 * (based on the <arch>/odp_sysinfo_parse.c files)
 */
static odp_cpu_arch_arm_t detect_sw_isa_arm(void)
{
#if defined(__ARM_ARCH)
	if (__ARM_ARCH == 8) {
	#ifdef __ARM_FEATURE_QRDMX
		/* v8.1 or higher */
		return ODP_CPU_ARCH_ARMV8_1;
	#else
		return ODP_CPU_ARCH_ARMV8_0;
	#endif
	}

	if (__ARM_ARCH == 9) {
		/* v9.0 or higher */
		return ODP_CPU_ARCH_ARMV9_0;
	}

	if (__ARM_ARCH >= 800) {
		/*
		 * ACLE 2018 defines that from v8.1 onwards the value includes
		 * the minor version number: __ARM_ARCH = X * 100 + Y
		 * E.g. for Armv8.1 __ARM_ARCH = 801
		 */
		int major = __ARM_ARCH / 100;
		int minor = __ARM_ARCH - (major * 100);

		if (major == 8) {
			switch (minor) {
			case 0:
				return ODP_CPU_ARCH_ARMV8_0;
			case 1:
				return ODP_CPU_ARCH_ARMV8_1;
			case 2:
				return ODP_CPU_ARCH_ARMV8_2;
			case 3:
				return ODP_CPU_ARCH_ARMV8_3;
			case 4:
				return ODP_CPU_ARCH_ARMV8_4;
			case 5:
				return ODP_CPU_ARCH_ARMV8_5;
			case 6:
				return ODP_CPU_ARCH_ARMV8_6;
			case 7:
				return ODP_CPU_ARCH_ARMV8_7;
			case 8:
				return ODP_CPU_ARCH_ARMV8_8;
			case 9:
				return ODP_CPU_ARCH_ARMV8_9;
			default:
				return ODP_CPU_ARCH_ARM_UNKNOWN;
			}
		} else if (major == 9) {
			switch (minor) {
			case 0:
				return ODP_CPU_ARCH_ARMV9_0;
			case 1:
				return ODP_CPU_ARCH_ARMV9_1;
			case 2:
				return ODP_CPU_ARCH_ARMV9_2;
			case 3:
				return ODP_CPU_ARCH_ARMV9_3;
			default:
				return ODP_CPU_ARCH_ARM_UNKNOWN;
			}
		}
	}
#endif
	return ODP_CPU_ARCH_ARM_UNKNOWN;
}

/*
 * Detect the x86 ISA used when compiling the em-odp library.
 * (based on the <arch>/odp_sysinfo_parse.c files)
 */
static odp_cpu_arch_x86_t detect_sw_isa_x86(void)
{
	odp_cpu_arch_x86_t isa_x86 = ODP_CPU_ARCH_X86_UNKNOWN;

#if defined __x86_64 || defined __x86_64__
	isa_x86 = ODP_CPU_ARCH_X86_64;
#elif defined __i686 || defined __i686__
	isa_x86 = ODP_CPU_ARCH_X86_I686;
#endif
	return isa_x86;
}

static odp_cpu_arch_mips_t detect_sw_isa_mips(void)
{
	return ODP_CPU_ARCH_MIPS_UNKNOWN;
}

static odp_cpu_arch_ppc_t detect_sw_isa_ppc(void)
{
	return ODP_CPU_ARCH_PPC_UNKNOWN;
}

static odp_cpu_arch_riscv_t detect_sw_isa_riscv(void)
{
	return ODP_CPU_ARCH_RISCV_UNKNOWN;
}

/*
 * Detect the SW CPU ISA used when compiling the em-odp library.
 * (based on the <arch>/odp_sysinfo_parse.c files)
 */
static odp_cpu_arch_isa_t detect_sw_isa(odp_cpu_arch_t cpu_arch)
{
	odp_cpu_arch_isa_t sw_isa;

	switch (cpu_arch) {
	case ODP_CPU_ARCH_ARM:
		sw_isa.arm = detect_sw_isa_arm();
		break;
	case ODP_CPU_ARCH_MIPS:
		sw_isa.mips = detect_sw_isa_mips();
		break;
	case ODP_CPU_ARCH_PPC:
		sw_isa.ppc = detect_sw_isa_ppc();
		break;
	case ODP_CPU_ARCH_RISCV:
		sw_isa.riscv = detect_sw_isa_riscv();
		break;
	case ODP_CPU_ARCH_X86:
		sw_isa.x86 = detect_sw_isa_x86();
		break;
	default:
		sw_isa.arm = ODP_CPU_ARCH_ARM_UNKNOWN;
		break;
	}

	return sw_isa;
}

/* Helper to print_core_map_info(): print EM-cores stored in 'cur-map' */
static void print_em_cores(const current_core_map_t *const cur_map, int cnt)
{
	if (unlikely(cnt <= 0))
		return;

	/* cnt >= 1 up to core_count */
	const int logic_last = cur_map->logic_cores[cnt - 1];
	int idx = 0;

	for (int i = 0; i <= logic_last; i++) {
		if (i == cur_map->logic_cores[idx]) {
			int phys_core = cur_map->phys_cores[idx];
			char phys_core_str[20];

			if (phys_core >= 0)
				snprintf(phys_core_str, sizeof(phys_core_str), "%2d", phys_core);
			else
				snprintf(phys_core_str, sizeof(phys_core_str), " ?");
			phys_core_str[sizeof(phys_core_str) - 1] = '\0';

			EM_PRINT(" %2d            %s           %2d            %s     %" PRIu64 "\n",
				 cur_map->logic_cores[idx], phys_core_str, cur_map->thr_ids[idx],
				 cur_map->core_types[idx] == EM_CORE_TYPE_WORKER ? "WRK" : "CTL",
				 (uint64_t)cur_map->tids[idx]);
			idx++;
		} else {
			EM_PRINT("  -             -            -             -       -\n");
		}
	}
}

/* Helper to print_core_map_info(): print EM external threads/processes stored in 'cur-map' */
static void print_ext_threads(const current_core_map_t *const cur_map, int ext_cnt)
{
	if (unlikely(ext_cnt <= 0))
		return;

	for (int i = 0; i < ext_cnt; i++) {
		EM_PRINT("  -             -           %2d            EXT     %" PRIu64 "\n",
			 cur_map->ext_thr_ids[i], (uint64_t)cur_map->ext_tids[i]);
	}
}

void print_core_map_info(void)
{
	core_map_t *const core_map = &em_shm->core_map;
	current_core_map_t *const cur_map = &current_core_map;
	const odp_cpumask_t *const logic_mask = &core_map->rwlocked.logic_mask.odp_cpumask;
	const odp_thrmask_t *const thrmask_extthrs = &core_map->rwlocked.thrmask_extthrs;

	odp_rwlock_read_lock(&core_map->rwlock);

	/* Read the current mappings for EM_CORE_TYPE_WORKER & _CONTROL */
	uint32_t cnt = 0;
	int logic_core = odp_cpumask_first(logic_mask);

	while (logic_core >= 0 && cnt < EM_MAX_CORES) {
		cur_map->logic_cores[cnt] = logic_core;
		/* set later: cur_map->phys_cores[cnt] */
		cur_map->thr_ids[cnt] = core_map->rwlocked.thr_vs_logic.odp_thr[logic_core];
		cur_map->tids[cnt] = core_map->rwlocked.tids[logic_core];
		cur_map->core_types[cnt] = core_map->rwlocked.core_types[logic_core];
		memcpy(cur_map->proc_tid_stat_paths[cnt],
		       core_map->rwlocked.proc_tid_stat_paths[logic_core],
		       sizeof(cur_map->proc_tid_stat_paths[cnt]));
		logic_core = odp_cpumask_next(logic_mask, logic_core);
		cnt++;
	}

	/* Ext threads/processes: EM_CORE_TYPE_EXTERNAL */
	int ext_cnt = 0;
	int ext_thr = odp_thrmask_first(thrmask_extthrs);

	while (ext_thr >= 0 && ext_cnt < ODP_THREAD_COUNT_MAX) {
		cur_map->ext_thr_ids[ext_cnt] = ext_thr;
		cur_map->ext_tids[ext_cnt] = core_map->rwlocked.ext_tids[ext_thr];
		ext_thr = odp_thrmask_next(thrmask_extthrs, ext_thr);
		ext_cnt++;
	}

	odp_rwlock_read_unlock(&core_map->rwlock);

	if (unlikely(cnt == 0 && ext_cnt == 0))
		goto core_map_error;

	/* read the phys core from the stored /proc/[tid]/stat files */
	for (uint32_t i = 0; i < cnt; i++) {
		const char *path = cur_map->proc_tid_stat_paths[i];

		if (unlikely(path[0] == '\0')) {
			cur_map->phys_cores[i] = -1; /* prints '?' */
			continue;
		}

		FILE *file = fopen(path, "r");

		if (!file) {
			EM_LOG(EM_LOG_ERR, "fopen(%s) failed: %s (%d)\n",
			       path, strerror(errno), errno);
			return;
		}
		cur_map->phys_cores[i] = read_proc_cpuid(file);
		fclose(file);
	}

	/* Print outside of rwlock */
	EM_PRINT("Core mapping:\n"
		 "EM-core <-> phys-core <-> ODP-thread <-> type <-> pid\n");

	/* Print EM worker and control cores */
	print_em_cores(cur_map, cnt);

	/* Print EM external threads/processes: */
	print_ext_threads(cur_map, ext_cnt);

	EM_PRINT("\n");
	return;

core_map_error:
	EM_PRINT("Core mapping:\n"
		 "EM-core <-> phys-core <-> ODP-thread <-> type <-> pid\n"
		 "  -             -            -             -       -\n\n");
}

void print_cpu_arch_info(void)
{
	odp_system_info_t sysinfo;
	int err;

	err = odp_system_info(&sysinfo);
	if (err) {
		EM_PRINT("%s(): odp_system_info() call failed:%d\n",
			 __func__, err);
		return;
	}

	/* detect & print EM SW ISA info here also */
	odp_cpu_arch_isa_t isa_em = detect_sw_isa(sysinfo.cpu_arch);

	const char *cpu_arch = cpu_arch_name(sysinfo.cpu_arch);
	const char *hw_isa = cpu_arch_isa_name(sysinfo.cpu_arch,
					       sysinfo.cpu_isa_hw);
	const char *sw_isa_odp = cpu_arch_isa_name(sysinfo.cpu_arch,
						   sysinfo.cpu_isa_sw);
	const char *sw_isa_em = cpu_arch_isa_name(sysinfo.cpu_arch, isa_em);

	EM_PRINT("CPU model:              %s\n"
		 "CPU arch:               %s\n"
		 "CPU ISA version:        %s\n"
		 " SW ISA version (ODP):  %s\n"
		 " SW ISA version (EM):   %s\n"
		 "\n",
		 odp_cpu_model_str(), cpu_arch,
		 hw_isa, sw_isa_odp, sw_isa_em);
}

static void print_mem_info(void)
{
	EM_PRINT("EM shared mem size:     %zu B\n"
		 "EM local mem size:       %zu B (per core)\n",
		 sizeof(em_shm_t), sizeof(em_locm_t));

	DBG_PRINT("\nem_locm_t:\t\t\t offset\t   size\n"
		  "\t\t\t\t ------\t   ----\n"
		  "current:\t\t\t%5zu B\t%5zu B\n"
		  "idle_state:\t\t\t%5zu B\t%5zu B\n"
		  "dispatch_cnt:\t\t\t%5zu B\t%5zu B\n"
		  "poll_drain_dispatch_cnt:\t%5zu B\t%5zu B\n"
		  "event_burst_cnt:\t\t%5zu B\t%5zu B\n"
		  "core_id:\t\t\t%5zu B\t%5zu B\n"
		  "local_queues:\t\t\t%5zu B\t%5zu B\n"
		  "local_event_cnt:\t\t%5zu B\t%5zu B\n"
		  "input_poll_fn:\t\t\t%5zu B\t%5zu B\n"
		  "output_drain_fn:\t\t%5zu B\t%5zu B\n"
		  "atomic_group_released:\t\t%5zu B\t%5zu B\n"
		  "is_sched_paused:\t\t%5zu B\t%5zu B\n"
		  "is_em_core:\t\t\t%5zu B\t%5zu B\n"
		  "is_external_thread:\t\t%5zu B\t%5zu B\n"
		  "is_term_thread_done:\t\t%5zu B\t%5zu B\n"
		  "is_core_local_inited:\t\t%5zu B\t%5zu B\n"
		  "dispatch_last_run:\t\t%5zu B\t%5zu B\n"
		  "poll_ctrl_interval_time:\t%5zu B\t%5zu B\n"
		  "poll_drain_dispatch_last_run:\t%5zu B\t%5zu B\n"
		  "poll_drain_interval_time:\t%5zu B\t%5zu B\n"
		  "init_pid:\t\t\t%5zu B\t%5zu B\n"
		  "start_eo_elem:\t\t\t%5zu B\t%5zu B\n"
		  "error_count:\t\t\t%5zu B\t%5zu B\n"
		  "log_fn:\t\t\t\t%5zu B\t%5zu B\n"
		  "vlog_fn:\t\t\t%5zu B\t%5zu B\n"
		  "sync_api:\t\t\t%5zu B\t%5zu B\n"
		  "debug_ts[]:\t\t\t%5zu B\t%5zu B\n"
		  "output_queue_track:\t\t%5zu B\t%5zu B\n"
		  "conf_local:\t\t\t%5zu B\t%5zu B\n"
		  "evhdr_nonem:\t\t\t%5zu B\t%5zu B\n"
		  "end:\t\t\t\t%5zu B\t%5zu B\n\n",
		  offsetof(em_locm_t, current),
		  sizeof_field(em_locm_t, current),
		  offsetof(em_locm_t, idle_state),
		  sizeof_field(em_locm_t, idle_state),
		  offsetof(em_locm_t, dispatch_cnt),
		  sizeof_field(em_locm_t, dispatch_cnt),
		  offsetof(em_locm_t, poll_drain_dispatch_cnt),
		  sizeof_field(em_locm_t, poll_drain_dispatch_cnt),
		  offsetof(em_locm_t, event_burst_cnt),
		  sizeof_field(em_locm_t, event_burst_cnt),
		  offsetof(em_locm_t, core_id),
		  sizeof_field(em_locm_t, core_id),
		  offsetof(em_locm_t, local_queues),
		  sizeof_field(em_locm_t, local_queues),
		  offsetof(em_locm_t, local_event_cnt),
		  sizeof_field(em_locm_t, local_event_cnt),
		  offsetof(em_locm_t, input_poll_fn),
		  sizeof_field(em_locm_t, input_poll_fn),
		  offsetof(em_locm_t, output_drain_fn),
		  sizeof_field(em_locm_t, output_drain_fn),
		  offsetof(em_locm_t, atomic_group_released),
		  sizeof_field(em_locm_t, atomic_group_released),
		  offsetof(em_locm_t, is_sched_paused),
		  sizeof_field(em_locm_t, is_sched_paused),
		  offsetof(em_locm_t, is_em_core),
		  sizeof_field(em_locm_t, is_em_core),
		  offsetof(em_locm_t, is_external_thread),
		  sizeof_field(em_locm_t, is_external_thread),
		  offsetof(em_locm_t, is_term_thread_done),
		  sizeof_field(em_locm_t, is_term_thread_done),
		  offsetof(em_locm_t, is_core_local_inited),
		  sizeof_field(em_locm_t, is_core_local_inited),
		  offsetof(em_locm_t, dispatch_last_run),
		  sizeof_field(em_locm_t, dispatch_last_run),
		  offsetof(em_locm_t, poll_ctrl_interval_time),
		  sizeof_field(em_locm_t, poll_ctrl_interval_time),
		  offsetof(em_locm_t, poll_drain_dispatch_last_run),
		  sizeof_field(em_locm_t, poll_drain_dispatch_last_run),
		  offsetof(em_locm_t, poll_drain_interval_time),
		  sizeof_field(em_locm_t, poll_drain_interval_time),
		  offsetof(em_locm_t, init_pid),
		  sizeof_field(em_locm_t, init_pid),
		  offsetof(em_locm_t, start_eo_elem),
		  sizeof_field(em_locm_t, start_eo_elem),
		  offsetof(em_locm_t, error_count),
		  sizeof_field(em_locm_t, error_count),
		  offsetof(em_locm_t, log_fn),
		  sizeof_field(em_locm_t, log_fn),
		  offsetof(em_locm_t, vlog_fn),
		  sizeof_field(em_locm_t, vlog_fn),
		  offsetof(em_locm_t, sync_api),
		  sizeof_field(em_locm_t, sync_api),
		  offsetof(em_locm_t, debug_ts),
		  sizeof_field(em_locm_t, debug_ts),
		  offsetof(em_locm_t, output_queue_track),
		  sizeof_field(em_locm_t, output_queue_track),
		  offsetof(em_locm_t, conf_local),
		  sizeof_field(em_locm_t, conf_local),
		  offsetof(em_locm_t, evhdr_nonem),
		  sizeof_field(em_locm_t, evhdr_nonem),
		  offsetof(em_locm_t, end),
		  sizeof_field(em_locm_t, end)
	);
}

void print_version_info(void)
{
	EM_PRINT("\n"
		 "===========================================================\n"
		 "EM version information: %s\n"
		 "===========================================================\n"
		 "EM API version:         %s\n"
		 "EM version:             %s, "
#ifdef EM_64_BIT
		 "64 bit "
#else
		 "32 bit "
#endif
		 "(EM_CHECK_LEVEL:%d, EM_ESV_ENABLE:%d)\n"
		 "EM build info:          %s\n"
		 "ODP API version:        %s\n"
		 "ODP impl name:          %s\n"
		 "ODP impl details:       %s\n",
		 EM_TARGET_STR, EM_VERSION_API_STR, EM_VERSION_STR,
		 EM_CHECK_LEVEL, EM_ESV_ENABLE,
		 EM_BUILD_INFO_STR,
		 odp_version_api_str(),
		 odp_version_impl_name(),
		 odp_version_impl_str());

	print_cpu_arch_info();
}

/**
 * Print information about EM & the environment
 */
void print_em_info(void)
{
	EM_PRINT("\n"
		 "===========================================================\n"
		 "EM Info on target: %s\n"
		 "===========================================================\n",
		 EM_TARGET_STR);
	print_mem_info();
	print_core_map_info();
	print_queue_capa();
	print_queue_elem_info();
	queue_group_info_print_all();
	em_pool_info_print_all();
	print_pool_elem_info();
	print_ag_elem_info();
	print_event_info();
}
