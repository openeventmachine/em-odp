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

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>

#include "event_machine.h"
#include "event_machine/helper/event_machine_helper.h"
#include "event_machine/platform/env/environment.h"

#include "cm_error_handler.h"
#include "cm_setup.h"

/**
 * Common default error handler for example applications
 *
 * @param eo            Execution object id
 * @param error         The error code
 * @param escope        Error scope
 * @param args          List of arguments (__FILE__, __func__, __LINE__,
 *                                         (format), ## __VA_ARGS__)
 *
 * @return The original error code. Fatal error causes abort.
 */
em_status_t
test_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		   va_list args)
{
	const int core_id = em_core_id();

	if (EM_ESCOPE(escope)) {
		char error_str[512];

		if (unlikely(appl_shm->exit_flag &&
			     !EM_ERROR_IS_FATAL(error))) {
			/*
			 * Suppress non-fatal error logs during
			 * application tear-down.
			 */
			switch (escope) {
			case EM_ESCOPE_SEND: /* fallthrough */
			case EM_ESCOPE_SEND_MULTI: /* fallthrough */
			case EM_ESCOPE_SEND_GROUP: /* fallthrough */
			case EM_ESCOPE_SEND_GROUP_MULTI:
				/* Sending to a disabled queue */
				return error;
			case EM_ESCOPE_DISPATCH:
				/*
				 * Dispatch event from disabled or nonexisting
				 * queue
				 */
				if (error == EM_ERR_BAD_STATE ||
				    error == EM_ERR_BAD_POINTER)
					return  error;
				break;
			default:
				break;
			}
		}

		em_error_format_string(error_str, sizeof(error_str), eo,
				       error, escope, args);
		fprintf(stderr, "\n%s\n", error_str);
	} else {
		/* Application error */
		char *file = va_arg(args, char*);
		const char *func = va_arg(args, const char*);
		const int line = va_arg(args, const int);
		const char *format = va_arg(args, const char*);
		const char *base = basename(file);
		char eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];

		/* Find out name of the EO */
		if (eo == EM_EO_UNDEF) {
			eo_str[0] = '\0';
		} else {
			char eo_name[EM_EO_NAME_LEN];
			size_t nlen;

			nlen = em_eo_get_name(eo, eo_name, sizeof(eo_name));
			eo_name[nlen] = '\0';

			snprintf(eo_str, sizeof(eo_str),
				 "EO:%" PRI_EO "-\"%s\"  ", eo, eo_name);
			eo_str[sizeof(eo_str) - 1] = '\0';
		}

		fprintf(stderr, "\nAPP ERROR:0x%08X  ESCOPE:0x%08X  %s\n",
			error, escope, eo_str);
		fprintf(stderr, "core:%02i %s:%i %s()\n",
			core_id, base, line, func);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
		vfprintf(stderr, format, args);
#pragma GCC diagnostic pop
		fprintf(stderr, "\n\n");
	}

	if (unlikely(EM_ERROR_IS_FATAL(error))) {
		/*
		 * Abort process, flush all open streams, dump stack,
		 * generate core dump, never return.
		 */
		fprintf(stderr,
			"\nFATAL EM ERROR:0x%08X on core:%02i - ABORT!\n\n",
			error, core_id);
		abort();
	}
	return error;
}
