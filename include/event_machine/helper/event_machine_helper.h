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

#ifndef EVENT_MACHINE_HELPER_H_
#define EVENT_MACHINE_HELPER_H_

#pragma GCC visibility push(default)

/**
 * @file
 * Event Machine helper functions and macros
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Format error string
 *
 * Creates an implementation dependent error report string from EM
 * internal errors.
 *
 * @param str           Output string pointer
 * @param size          Maximum string length in characters
 * @param eo            EO id
 * @param error         Error code (EM internal)
 * @param escope        Error scope (EM internal)
 * @param args          Variable arguments
 *
 * @return Output string length.
 */
int
em_error_format_string(char *str, size_t size, em_eo_t eo, em_status_t error,
		       em_escope_t escope, va_list args);

/*
 * Physical core ids
 ***************************************
 */

/**
 * Converts a logical core id to a physical core id
 *
 * Mainly needed when interfacing HW specific APIs.
 *
 * @param core          Logical (Event Machine) core id
 *
 * @return Physical core id or -1 on error.
 */
int
em_core_id_get_physical(int core);

/**
 * Converts a logical core mask to a physical core mask
 *
 * Mainly needed when interfacing HW specific APIs.
 *
 * @param phys          Core mask of physical core ids
 * @param logic         Core mask of logical (Event Machine) core ids
 */
void
em_core_mask_get_physical(em_core_mask_t *phys, const em_core_mask_t *logic);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_HELPER_H_ */
