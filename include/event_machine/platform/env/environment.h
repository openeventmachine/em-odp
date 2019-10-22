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

#ifndef _ENVIRONMENT_H_
#define _ENVIRONMENT_H_

/**
 * @file
 *
 * Environment header file
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <odp_api.h>
/* env generic macros */
#include <event_machine/platform/env/env_macros.h>
/* env configuration affecting other env files */
#include <event_machine/platform/env/env_conf.h>

/**
 * Thread local vars
 */
#define ENV_LOCAL __thread

/**
 * Cache line size
 */
#define ENV_CACHE_LINE_SIZE           ODP_CACHE_LINE_SIZE

/**
 * Cache line size round up
 */
#define ENV_CACHE_LINE_SIZE_ROUNDUP(x) \
	((((x) + ENV_CACHE_LINE_SIZE - 1) / ENV_CACHE_LINE_SIZE) \
	 * ENV_CACHE_LINE_SIZE)

#define ENV_ALIGNED(x)  ODP_ALIGNED(x)

/**
 * Cache line alignment
 */
#define ENV_CACHE_LINE_ALIGNED  ODP_ALIGNED_CACHE

/*
 * Cache Prefetch-macros
 */
/** Prefetch into all cache levels */
#define ENV_PREFETCH(addr) odp_prefetch((addr))

/*
 * env helper include files - don't include these files directly,
 * instead #include <environment.h>
 */
#include <event_machine/platform/env/env_atomic.h>
#include <event_machine/platform/env/env_bitmask.h>
#include <event_machine/platform/env/env_barrier.h>
#include <event_machine/platform/env/env_sharedmem.h>
#include <event_machine/platform/env/env_spinlock.h>

/**
 * Panic
 */
#define env_panic(...) abort()

static inline uint64_t env_get_cycle(void)
{
	return odp_cpu_cycles();
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

#endif /* _ENVIRONMENT_H_ */
