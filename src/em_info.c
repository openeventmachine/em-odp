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

/* ODP info printouts taken from the ODP example: odp_sysinfo.c */

/* Copyright (c) 2018, Linaro Limited
 * Copyright (c) 2020, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include "em_include.h"

#define XSTR(x) #x
#define STR(x) XSTR(x)

static const char *cpu_arch_name(const odp_system_info_t *sysinfo)
{
	odp_cpu_arch_t cpu_arch = sysinfo->cpu_arch;

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

static const char *arm_isa(odp_cpu_arch_arm_t isa)
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
	default:
		return "Unknown";
	}
}

static const char *x86_isa(odp_cpu_arch_x86_t isa)
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

static const char *cpu_arch_isa(const odp_system_info_t *sysinfo, int isa_sw)
{
	odp_cpu_arch_t cpu_arch = sysinfo->cpu_arch;

	switch (cpu_arch) {
	case ODP_CPU_ARCH_ARM:
		if (isa_sw)
			return arm_isa(sysinfo->cpu_isa_sw.arm);
		else
			return arm_isa(sysinfo->cpu_isa_hw.arm);
	case ODP_CPU_ARCH_MIPS:
		return "Unknown";
	case ODP_CPU_ARCH_PPC:
		return "Unknown";
	case ODP_CPU_ARCH_RISCV:
		return "Unknown";
	case ODP_CPU_ARCH_X86:
		if (isa_sw)
			return x86_isa(sysinfo->cpu_isa_sw.x86);
		else
			return x86_isa(sysinfo->cpu_isa_hw.x86);
	default:
		return "Unknown";
	}
}

static void
print_core_map_info(void)
{
	int logic_core;

	EM_PRINT("Core mapping: EM-core <-> phys-core <-> ODP-thread\n");

	for (logic_core = 0; logic_core < em_core_count(); logic_core++) {
		EM_PRINT("                %2i           %2i           %2i\n",
			 logic_core,
			 em_core_id_get_physical(logic_core),
			 logic_to_thr_core_id(logic_core));
	}

	EM_PRINT("\n");
}

static void
print_odp_info(void)
{
	odp_system_info_t sysinfo;
	int err;

	err = odp_system_info(&sysinfo);
	if (err) {
		EM_PRINT("%s(): odp_system_info() call failed:%d\n",
			 __func__, err);
		return;
	}

	EM_PRINT("ODP API version:        %s\n"
		 "ODP impl name:          %s\n"
		 "ODP impl details:       %s\n"
		 "CPU model:              %s\n"
		 "CPU arch:               %s\n"
		 "CPU ISA version:        %s\n"
		 "SW ISA version:         %s\n",
		 odp_version_api_str(),
		 odp_version_impl_name(),
		 odp_version_impl_str(),
		 odp_cpu_model_str(),
		 cpu_arch_name(&sysinfo),
		 cpu_arch_isa(&sysinfo, 0),
		 cpu_arch_isa(&sysinfo, 1));
}

/**
 * Print information about EM & the environment
 */
void
print_em_info(void)
{
	EM_PRINT("\n"
		 "===========================================================\n"
		 "EM Info on target: %s\n"
		 "===========================================================\n"
		 "EM API version:         v%i.%i, "
#ifdef EM_64_BIT
		 "64 bit "
#else
		 "32 bit "
#endif
		 "(EM_CHECK_LEVEL:%d, EM_ESV_ENABLE:%d)\n"
		 "EM build info:          %s\n",
		 EM_TARGET_STR, EM_API_VERSION_MAJOR, EM_API_VERSION_MINOR,
		 EM_CHECK_LEVEL, EM_ESV_ENABLE,
		 STR(EM_BUILD_INFO));

	print_odp_info();
	print_core_map_info();
	print_queue_info();
	print_queue_group_info();
	em_pool_info_print_all();
	print_event_info();
}
