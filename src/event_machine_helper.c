/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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

#include <libgen.h> /* basename() */
#include <unistd.h> /* ssize_t */
#include "em_include.h"

int
em_error_format_string(char *str, size_t size, em_eo_t eo, em_status_t error,
		       em_escope_t escope, va_list args)
{
	int ret = -1;

	if (!EM_ESCOPE(escope) || (ssize_t)size <= 0)
		return 0;

	/*
	 * va_list contains: __FILE__, __func__, __LINE__, (format),
	 *  ## __VA_ARGS__ as reported by the INTERNAL_ERROR macro.
	 */
	char *file = va_arg(args, char*);
	const char *func = va_arg(args, const char*);
	const int line = va_arg(args, const int);
	const char *format = va_arg(args, const char*);
	const char *base = basename(file);
	char eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];
	const uint64_t loc_err_cnt = em_locm.error_count;
	const uint64_t glob_err_cnt = load_global_err_cnt();

	if (eo == EM_EO_UNDEF) {
		eo_str[0] = '\0';
	} else {
		char eo_name[EM_EO_NAME_LEN];
		size_t nlen;

		nlen = em_eo_get_name(eo, eo_name, sizeof(eo_name));
		nlen = nlen > 0 ? nlen + 1 : 0;
		eo_name[nlen] = '\0';

		snprintf(eo_str, sizeof(eo_str),
			 "EO:%" PRI_EO "-\"%s\"  ", eo, eo_name);
		eo_str[sizeof(eo_str) - 1] = '\0';
	}

	ret =
	snprintf(str, size, "\n"
		 "EM ERROR:0x%08X  ESCOPE:0x%08X  %s\n"
		 "core:%02i ecount:%" PRIu64 "(%" PRIu64 ") %s:%i %s()\n",
		 error, escope, eo_str, em_core_id(),
		 glob_err_cnt, loc_err_cnt, base, line, func);

	if (ret > 0 && ret < (int64_t)size) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
		ret += vsnprintf(str + ret, size - ret, format, args);
#pragma GCC diagnostic pop
		if (ret > 0 && ret < (int64_t)size)
			ret += snprintf(str + ret, size - ret, "\n");
	}

	str[size - 1] = '\0';

	return MIN((int64_t)size, ret + 1);
}

int
em_core_id_get_physical(int em_core_id)
{
	return logic_to_phys_core_id(em_core_id);
}

void
em_core_mask_get_physical(em_core_mask_t *phys, const em_core_mask_t *logic)
{
	if (em_core_mask_equal(logic, &em_shm->core_map.logic_mask)) {
		em_core_mask_copy(phys, &em_shm->core_map.phys_mask);
	} else {
		int i;

		em_core_mask_zero(phys);
		for (i = 0; i < EM_MAX_CORES; i++) {
			int phys_core;

			if (em_core_mask_isset(i, logic)) {
				phys_core = logic_to_phys_core_id(i);
				em_core_mask_set(phys_core, phys);
			}
		}
	}
}
