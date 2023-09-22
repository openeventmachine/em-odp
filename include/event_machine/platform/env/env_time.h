/*
 *   Copyright (c) 2020, Nokia Solutions and Networks
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

/**
 * @file
 * Env time functions - don't include this file directly,
 * instead include "environment.h"
 */

#ifndef _ENV_TIME_H_
#define _ENV_TIME_H_

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

/* type for time stamp */
typedef odp_time_t env_time_t;

/* Zero time stamp */
#define ENV_TIME_NULL ODP_TIME_NULL

#define ENV_TIME_MSEC_IN_NS ODP_TIME_MSEC_IN_NS

/**
 * Returns current local time stamp value.
 * Local time is thread specific and must not be shared between threads.
 *
 * @return Local time stamp
 */
static inline env_time_t env_time_local(void)
{
	return odp_time_local();
}

/**
 * Returns current global time stamp value.
 * Global time can be shared between threads.
 *
 * @return Global time stamp
 */
static inline env_time_t env_time_global(void)
{
	return odp_time_global();
}

/**
 * Returns difference of time stamps (time2 - time1).
 *
 * @param time2    Second time stamp
 * @param time1    First time stamp
 *
 * @return Difference between given time stamps
 */
static inline env_time_t env_time_diff(env_time_t time2, env_time_t time1)
{
	return odp_time_diff(time2, time1);
}

/**
 * Returns difference of time stamps (time2 - time1) in nanoseconds.
 *
 * @param time2    Second time stamp
 * @param time1    First time stamp
 *
 * @return Difference between given time stamps in nanoseconds
 */
static inline uint64_t env_time_diff_ns(env_time_t time2, env_time_t time1)
{
	return odp_time_diff_ns(time2, time1);
}

/**
 * Sum of time stamps
 *
 * @param time1    First time stamp
 * @param time2    Second time stamp
 *
 * @return Sum of given time stamps
 */
static inline env_time_t env_time_sum(env_time_t time1, env_time_t time2)
{
	return odp_time_sum(time1, time2);
}

/**
 * Convert nanoseconds to local time.
 *
 * @param ns    Time in nanoseconds
 *
 * @return Local time stamp
 */
static inline env_time_t env_time_local_from_ns(uint64_t ns)
{
	return odp_time_local_from_ns(ns);
}

/**
 * Convert nanoseconds to global time
 *
 * @param ns    Time in nanoseconds
 *
 * @return Global time stamp
 */
static inline env_time_t env_time_global_from_ns(uint64_t ns)
{
	return odp_time_global_from_ns(ns);
}

/**
 * Compare time stamps.
 *
 * @param time2    Second time stamp
 * @param time1    First time stamp
 *
 * @retval <0 when time2 < time1
 * @retval  0 when time2 == time1
 * @retval >0 when time2 > time1
 */
static inline int env_time_cmp(env_time_t time2, env_time_t time1)
{
	return odp_time_cmp(time2, time1);
}

 /**
  * Convert time stamp to nanoseconds
  *
  * @param time    Time stamp
  *
  * @return time in nanoseconds
  */
static inline uint64_t env_time_to_ns(env_time_t time)
{
	return odp_time_to_ns(time);
}

/**
 * Convert time stamp to cpu cycles
 *
 * @param time    Time stamp
 * @param hz      CPU Hz
 *
 * @return cpu cycles
 */
#ifdef __SIZEOF_INT128__

static inline uint64_t env_time_to_cycles(env_time_t time, uint64_t hz)
{
	__uint128_t cycles;

	cycles = (__uint128_t)odp_time_to_ns(time) * hz / 1000000000;
	return (uint64_t)cycles;
}
#else
static inline uint64_t env_time_to_cycles(env_time_t time, uint64_t hz)
{
	return odp_time_to_ns(time) * (hz / 1000000) / 1000;
}
#endif

static inline void env_time_wait_ns(uint64_t ns)
{
	odp_time_wait_ns(ns);
}

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* _ENV_TIME_H_ */
