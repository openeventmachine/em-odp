/*
 *   Copyright (c) 2022, Nokia Solutions and Networks
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
#ifndef EVENT_MACHINE_DEBUG_H_
#define EVENT_MACHINE_DEBUG_H_

#pragma GCC visibility push(default)

/**
 * @file
 * Event Machine helper functions for debug support
 *
 * Not for normal application use, may lower performance or cause latency.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM dispatcher debug timestamp points
 * EM_DEBUG_TSP_SCHED_ENTRY: EM core local timestamp taken by the dispatcher
 *                           _before_ asking the scheduler for new events.
 * EM_DEBUG_TSP_SCHED_RETURN: EM core local timestamp taken by the dispatcher
 *                            _after_ returning from the scheduler.
 */
typedef enum {
	EM_DEBUG_TSP_SCHED_ENTRY,  /* timestamp at scheduler entry */
	EM_DEBUG_TSP_SCHED_RETURN, /* timestamp at scheduler return */
	EM_DEBUG_TSP_LAST
} em_debug_tsp_t;

/**
 * Returns a per core timestamp from the EM dispatcher.
 *
 * Not intended for normal application use!
 * These debug timestamps are disabled by default and must be enabled by the
 * user (see configure option '--enable-debug-timestamps=...' or the
 * EM_DEBUG_TIMESTAMP_ENABLE define).
 *
 * Timestamps are taken with odp_time_global/_strict() and converted to ns.
 * The timestamps can be used to e.g. measure the EM dispatcher overhead from
 * EM_DEBUG_TSP_SCHED_RETURN to the EO-receive() including all code and hooks
 * in between.
 *
 * If debug timestamps are disabled or the given timestamp point does not exist,
 * 0 will be returned.
 *
 * @param tsp  timestamp point, selects which EM internal timestamp to return
 *
 * @return timestamp in ns
 * @retval 0 if debug timestamps are disabled or the given timestamp point does not exist
 *
 * @see em_debug_tsp_t
 */
uint64_t em_debug_timestamp(em_debug_tsp_t tsp);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_DEBUG_H_ */
