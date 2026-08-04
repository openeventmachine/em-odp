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

#ifndef EM_INIT_TYPES_H_
#define EM_INIT_TYPES_H_

/**
 * @file
 * EM internal initialization types
 */

#include <stdint.h>

#include <odp_api.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def EM_CHECK_INIT_CALLED
 *
 * Magic number used detect whether an initialization function has been called
 * prior to a create-function.
 *
 * An error will be reported and the creation will fail if the call to _init()
 * has been omitted.
 */
#define EM_CHECK_INIT_CALLED ((uint32_t)0xACDCBEEF)

/** EM config file options cached for optimal performance
 *
 * Some EM config file options need to be computed or converted to
 * ODP types before being used in fast paths. The caching here avoids
 * redundant computations and keeps em_cfgfile_opts_t independent of
 * ODP types.
 */
typedef struct {
	struct {
		uint64_t sched_wait; /* odp_schedule_wait_time(sched_wait_ns) */
	} dispatch;
} cfgfile_opt_cache_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_INIT_TYPES_H_ */
