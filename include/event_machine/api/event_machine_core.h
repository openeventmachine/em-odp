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

#ifndef EVENT_MACHINE_CORE_H_
#define EVENT_MACHINE_CORE_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_core Core related
 *  core (thread) specific APIs.
 * @{
 *
 * Currently OpenEM enumerates cores (threads) within an EM instance as
 * contiguous integers starting from 0. Numbers may or may not match those
 * provided by underlying HW or operating system.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Logical core id.
 *
 * Returns the logical id of the current core.
 * EM enumerates cores (or HW threads) to start from 0 and be contiguous,
 * i.e. valid core identifiers are 0...em_core_count()-1
 *
 * @return Current logical core id.
 *
 * @see em_core_count()
 */
int
em_core_id(void);

/**
 * The number of cores running within the same EM instance
 * (sharing the EM state).
 *
 * @return Number of EM cores (or HW threads).
 *
 * @see em_core_id()
 */
int
em_core_count(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_CORE_H_ */
