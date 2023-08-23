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
 * @defgroup em_odp_ext Conversions & extensions
 *  Event Machine ODP API extensions and conversion functions between EM and ODP
 * @{
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
#include <event_machine/add-ons/event_machine_timer.h>

/**
 * Get the associated ODP queue.
 *
 * The given EM queue must have been created with em_queue_create...() APIs.
 *
 * @param queue  EM queue
 *
 * @return odp queue if successful, ODP_QUEUE_INVALID on error
 */
odp_queue_t em_odp_queue_odp(em_queue_t queue);

/**
 * Get the associated EM queue.
 *
 * The associated EM queue must have been created with em_queue_create...() APIs
 *
 * @param queue  ODP queue
 *
 * @return em queue if successful, EM_QUEUE_UNDEF on error
 */
em_queue_t em_odp_queue_em(odp_queue_t queue);

/**
 * @brief Map the given scheduled ODP pktin event queues to new EM queues.
 *
 * Creates new EM queues and maps them to use the given scheduled ODP pktin
 * event queues.
 * Enables direct scheduling of packets as EM events via EM queues.
 * EM queues based on scheduled ODP pktin queues are a bit special in how they
 * are created and how they are deleted:
 *   - creation is done via this function by providing the already set up
 *     scheduled ODP pktin event queues to use.
 *   - deletion of one of the returned EM queues will not delete the underlying
 *     ODP pktin event queue. The ODP queues in question are deleted when
 *     the ODP pktio is terminated.
 * The scheduled ODP pktin event queues must have been set up with an
 * ODP schedule group that belongs to an existing EM queue group. Also the used
 * priority must mappable to an EM priority.
 *
 * Setup example:
 * @code
 *	// Configure ODP pktin queues
 *	odp_pktin_queue_param_t pktin_queue_param;
 *	odp_pktin_queue_param_init(&pktin_queue_param);
 *	pktin_queue_param.num_queues = num;
 *	pktin_queue_param.queue_param.type = ODP_QUEUE_TYPE_SCHED;
 *	pktin_queue_param.queue_param.sched.prio = ODP prio mappable to EM prio
 *	pktin_queue_param.queue_param.sched.sync = PARALLEL | ATOMIC | ORDERED;
 *	pktin_queue_param.queue_param.sched.group = em_odp_qgrp2odp(EM qgroup);
 *	...
 *	ret = odp_pktin_queue_config(pktio, &pktin_queue_param);
 *	if (ret < 0)
 *		error(...);
 *
 *	// Obtain ODP pktin event queues used for scheduled packet input
 *	odp_queue_t pktin_sched_queues[num];
 *	ret = odp_pktin_event_queue(pktio, pktin_sched_queues['out'], num);
 *	if (ret != num)
 *		error(...);
 *
 *	// Create EM queues mapped to the scheduled ODP pktin event queues
 *	em_queue_t queues_em[num];
 *	ret = em_odp_pktin_event_queues2em(pktin_sched_queues['in'],
 *					   queues_em['out'], num);
 *	if (ret != num)
 *		error(...);
 *
 *	// Add the EM queues to an EM EO and once the EO has been started it
 *	// will receive pktio events directly from the scheduler.
 *	for (int i = 0; i < num; i++)
 *		err = em_eo_add_queue_sync(eo, queues_em);
 * @endcode
 *
 * @param[in]  odp_pktin_evqueues  Array of ODP pktin event queues to convert to
 *                                 EM-queues. The array must contain 'num' valid
 *                                 ODP-queue handles (as returned by the
 *                                 odp_pktin_event_queue() function).
 * @param[out] queues              Output array into which the corresponding
 *                                 EM-queue handles are written.
 *                                 Array must fit 'num' entries.
 * @param      num                 Number of entries in 'odp_pktin_evqueues[]'
 *                                 and 'queues[]'.
 * @return int  Number of EM queues created that correspond to the given
 *              ODP pktin event queues
 * @retval <0 on failure
 */
int em_odp_pktin_event_queues2em(const odp_queue_t odp_pktin_evqueues[/*num*/],
				 em_queue_t queues[/*out:num*/], int num);

/**
 * Get the EM event header size.
 *
 * Needed e.g. when configuring a separate ODP packet pool and have pktio
 * allocate events usable by EM from there:
 * @code
 *	odp_pool_param_t::pkt.uarea_size = em_odp_event_hdr_size();
 * @endcode
 *
 * @return EM event header size.
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
 * @param[out] odp_events  Output array into which the corresponding ODP-event
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
 * @param[out] events      Output array into which the corresponding EM-event
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
 * @param odp_pool  ODP pool
 *
 * @return The EM event pool that contains the subpool 'odp_pool' or
 *         EM_POOL_UNDEF if 'odp_pool' is not part of any EM event pool.
 */
em_pool_t em_odp_pool2em(odp_pool_t odp_pool);

/**
 * @brief Get the ODP schedule group that corresponds to the given EM queue gruop
 *
 * @param queue_group
 *
 * @return ODP schedule group handle
 * @retval ODP_SCHED_GROUP_INVALID on error
 */
odp_schedule_group_t em_odp_qgrp2odp(em_queue_group_t queue_group);

/**
 * Enqueue external packets into EM
 *
 * Enqueue packets from outside of EM into EM queues for processing.
 * This function will initialize the odp packets properly as EM events before
 * enqueueing them into EM.
 * The odp packets might be polled from pktio or some other external source,
 * e.g. the em_conf_t::input.input_poll_fn() function (see em_init()) can use
 * this API to enqueue polled packets into EM queues.
 * Inside EM, the application must use em_send...() instead to send/enqueue
 * events into EM queues.
 *
 * @param pkt_tbl  Array of external ODP-packets to enqueue into EM as events.
 *                 The 'pkt_tbl[]' array must contain 'num' valid ODP packet
 *                 handles.
 * @param num      The number of packets in the 'pkt_tbl[]' array, must be >0.
 * @param queue    EM queue into which to send/enqueue the packets as EM-events.
 *
 * @return The number of ODP packets successfully send/enqueued as EM-events
 */
int em_odp_pkt_enqueue(const odp_packet_t pkt_tbl[/*num*/], int num,
		       em_queue_t queue);

/**
 * @brief Get the odp timer_pool from EM timer handle
 *
 * Returns the corresponding odp timer_pool from a valid EM timer handle.
 * This can be used for e.g. debugging.
 *
 * DO NOT use any odp apis directly to modify the odp timer_pool created by EM.
 *
 * @param tmr	em timer handle
 *
 * @return odp timer_pool or ODP_TIMER_POOL_INVALID on failure
 */
odp_timer_pool_t em_odp_timer2odp(em_timer_t tmr);

/**
 * @brief Get the odp timer from EM timeout handle
 *
 * Returns the corresponding odp timer from a valid EM tmo handle.
 * This can be used for e.g. debugging.
 *
 * DO NOT use any odp apis directly to modify the odp timer created by EM.
 *
 * @param tmo	em timeout handle
 *
 * @return odp timer or ODP_TIMER_INVALID on failure
 */
odp_timer_t em_odp_tmo2odp(em_tmo_t tmo);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_ODP_EXT_H */
