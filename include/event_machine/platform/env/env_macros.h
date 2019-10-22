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

/*
 * env generic macros - don't include this file directly,
 * instead #include <environment.h>
 */

#ifndef _ENV_MACROS_H_
#define _ENV_MACROS_H_

/**
 * Branch Prediction macros
 */
#undef likely
#undef unlikely
#define likely(x)    odp_likely(!!(x))
#define unlikely(x)  odp_unlikely(!!(x))

/**
 * Compile time assertion-macro - fail compilation if cond is false.
 */
#define COMPILE_TIME_ASSERT(cond, msg)  ODP_STATIC_ASSERT(cond, #msg)

/** Round up 'val' to next multiple of 'N' */
#define ROUND_UP(val, N)  ((((val) + ((N) - 1)) / (N)) * (N))
/** True if x is a power of 2 */
#define POWEROF2(x)  ((((x) - 1) & (x)) == 0)
/** Return min of 'a' vs. 'b' */
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
/** Return max of 'a' vs. 'b' */
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
/** Get the lowest bit on 'n' */
#define ENV_LOWEST_BIT(n)  ((n) & (~((n) - 1)))
/** Clear the lowest bit of 'n' */
#define ENV_CLEAR_LOWEST_BIT(n)  ((n) & ((n) - 1))

#endif
