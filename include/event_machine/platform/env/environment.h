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

#ifndef _ENVIRONMENT_H_
#define _ENVIRONMENT_H_

#pragma GCC visibility push(default)

/**
 * @file
 *
 * Environment header file
 */
#include <stdlib.h>
#include <odp_api.h>

/*
 * env helper include files - don't include these files directly,
 * instead #include <environment.h>
 *
 * env_conf.h provides ENV_DEPRECATED_API (derived from EM_DEPRECATED_API)
 * and must be included before the conditional env_atomic.h below.
 */
#include <event_machine/platform/env/env_conf.h>
#include <event_machine/platform/env/env_macros.h>
#if ENV_DEPRECATED_API
#include <event_machine/platform/env/env_atomic.h>
#endif
#include <event_machine/platform/env/env_bitmask.h>
#include <event_machine/platform/env/env_barrier.h>
#include <event_machine/platform/env/env_sharedmem.h>
#include <event_machine/platform/env/env_spinlock.h>
#include <event_machine/platform/env/env_time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the current cpu cycle count
 *
 * The cycle count frequency follows the cpu frequency and may thus change at
 * any time. The cycle count should not be used for time measurements due to
 * the possibility of frequency variation.
 * The count may advance in steps larger than one.
 *
 * @return Current cycle count on the cpu the caller is running on
 * @retval 0 if the cpu cycle counter is not supported
 */
static inline uint64_t env_get_cycle(void)
{
	return odp_cpu_cycles();
}

/**
 * Returns difference of cpu cycles (cycles2 - cycles1).
 *
 * @param cycles2  Second cycle count
 * @param cycles1  First cycle count
 *
 * @return Difference between given cpu cycles
 */
static inline uint64_t env_cycles_diff(uint64_t cycles2, uint64_t cycles1)
{
	return odp_cpu_cycles_diff(cycles2, cycles1);
}

static inline void env_sync_mem(void)
{
	odp_mb_full();
}

static inline uint64_t env_core_hz(void)
{
	return odp_cpu_hz();
}

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* _ENVIRONMENT_H_ */
