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
		 "EM API version: v%i.%i, "
#ifdef EM_64_BIT
		 "64 bit "
#else
		 "32 bit "
#endif
		"(EM_CHECK_LEVEL: %d)\n"
		"ODP version: %s\n",
		 EM_TARGET_STR, EM_API_VERSION_MAJOR, EM_API_VERSION_MINOR,
		 EM_CHECK_LEVEL, odp_version_impl_str());

	print_core_map_info();
	print_queue_info();
	print_queue_group_info();
	em_pool_info_print_all();
}
