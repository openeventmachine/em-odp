/*
 *   Copyright (c) 2015-2021, Nokia Solutions and Networks
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
 *
 * Event Machine ODP API extensions
 */

#ifndef EVENT_MACHINE_ODP_EXT_H
#define EVENT_MACHINE_ODP_EXT_H

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

#include <odp_api.h>
#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Get the associated ODP queue.
 *
 * @param queue  EM queue
 *
 * @return odp queue if successful, ODP_QUEUE_INVALID on error
 */
odp_queue_t em_odp_queue_odp(em_queue_t queue);

/**
 * Get the associated EM queue.
 *
 * @param queue  ODP queue
 *
 * @return em queue if successful, EM_QUEUE_UNDEF on error
 */
em_queue_t em_odp_queue_em(odp_queue_t queue);

/**
 * Get EM event header size.
 *
 * Needed when user has to configure separate pool for packet I/O and allocate
 * EM events from there.
 *
 * @return em event header size.
 */
uint32_t em_odp_event_hdr_size(void);

/**
 * Convert EM event handle to ODP event handle.
 *
 * @param event  EM-event handle
 *
 * @return ODP event handle.
 */
odp_event_t em_odp_event2odp(em_event_t event);

/**
 * Convert EM event handles to ODP event handles
 *
 * @param      events      Array of EM-events to convert to ODP-events.
 *                         The 'events[]' array must contain 'num' valid
 *                         event handles.
 * @param[out] odp_events  Output array into which the ocrresponding ODP-event
 *                         handles are written. Array must fit 'num' entries.
 * @param      num         Number of entries in 'events[]' and 'odp_events[]'.
 */
void em_odp_events2odp(const em_event_t events[/*num*/],
		       odp_event_t odp_events[/*out:num*/], int num);

/**
 * Convert ODP event handle to EM event handle.
 *
 * The event must have been allocated by EM originally.
 *
 * @param odp_event  ODP-event handle
 *
 * @return EM event handle.
 */
em_event_t em_odp_event2em(odp_event_t odp_event);

/**
 * Convert EM event handles to ODP event handles
 *
 * @param      odp_events  Array of ODP-events to convert to EM-events.
 *                         The 'odp_events[]' array must contain 'num' valid
 *                         ODP-event handles.
 * @param[out] events      Output array into which the ocrresponding EM-event
 *                         handles are written. Array must fit 'num' entries.
 * @param      num         Number of entries in 'odp_events[]' and 'events[]'.
 */
void em_odp_events2em(const odp_event_t odp_events[/*num*/],
		      em_event_t events[/*out:num*/], int num);

/**
 * @brief Get the ODP pools used as subpools in a given EM event pool.
 *
 * An EM event pool consists of 1 to 'EM_MAX_SUBPOOLS' subpools. Each subpool
 * is an ODP pool. This function outputs the ODP pool handles of these subpools
 * into a user-provided array and returns the number of handles written.
 *
 * The obtained ODP pools must not be deleted or alterede outside of EM,
 * e.g. these ODP pools must only be deleted as part of an EM event pool
 * using em_pool_delete().
 *
 * ODP pool handles obtained through this function can be used to
 *  - configure ODP pktio to use an ODP pool created via EM (allows for
 *    better ESV tracking)
 *  - print ODP-level pool statistics with ODP APIs etc.
 *
 * Note that direct allocations and free:s via ODP APIs will bypass
 * EM checks (e.g. ESV) and might cause errors unless properely handled:
 *  - use em_odp_event2em() to initialize as an EM event
 *  - use em_event_mark_free() before ODP-free operations (SW- or HW-free)
 *
 * @param      pool       EM event pool handle.
 * @param[out] odp_pools  Output array to be filled with the ODP pools used as
 *                        subpools in the given EM event pool. The array must
 *                        fit 'num' entries.
 * @param      num        Number of entries in the 'odp_pools[]' array.
 *                        Using 'num=EM_MAX_SUBPOOLS' will always be large
 *                        enough to fit all subpools in the EM event pool.
 *
 * @return The number of ODP pools filled into 'odp_pools[]'
 */
int em_odp_pool2odp(em_pool_t pool, odp_pool_t odp_pools[/*out*/], int num);

/**
 * @brief Get the EM event pool that the given ODP pool belongs to
 *
 * An EM event pool consists of 1 to 'EM_MAX_SUBPOOLS' subpools. Each subpool
 * is an ODP pool. This function returns the EM event pool that contains the
 * given ODP pool as a subpool.
 *
 * @param odp_pool
 *
 * @return The EM event pool that contains the subpool 'odp_pool' or
 *         EM_POOL_UNDEF if 'odp_pool' is not part of any EM event pool.
 */
em_pool_t em_odp_pool2em(odp_pool_t odp_pool);

/**
 * Enqueue external packets into EM (packets are from outside of EM, i.e not
 * allocated by EM using em_alloc/_multi())
 *
 * @param pkt_tbl  Array of external ODP-packets to enqueue into EM as events.
 *                 The 'pkt_tbl[]' array must contain 'num' valid ODP packet
 *                 handles.
 * @param num      The number of packets in the 'pkt_tbl[]' array, must be >0.
 * @param queue    EM queue into which to sen/enqueue the packets as EM-events.
 *
 * @return The number of ODP packets successfully send/enqueued as EM-events
 */
int pkt_enqueue(const odp_packet_t pkt_tbl[/*num*/], int num, em_queue_t queue);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_ODP_EXT_H */
