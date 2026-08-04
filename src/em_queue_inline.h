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

/**
 * @file
 * EM internal queue functions
 */

#ifndef EM_QUEUE_INLINE_H_
#define EM_QUEUE_INLINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_mem.h"
#include "em_queue_types.h"
#include "misc/list.h"
#include "misc/objpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Is the queue allocated? */
static inline int
queue_allocated(const queue_elem_t *const queue_elem)
{
	return !objpool_in_pool(&queue_elem->queue_pool_elem);
}

/** Convert EM queue handle to queue index */
static inline int
queue_hdl2idx(em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};
	int queue_idx;

	queue_idx = iq.queue_id - EM_QUEUE_RANGE_OFFSET;

	return queue_idx;
}

/** Convert queue index to EM queue handle */
static inline em_queue_t
queue_idx2hdl(int queue_idx)
{
	internal_queue_t iq = {.queue = 0};

	iq.queue_id = queue_idx + EM_QUEUE_RANGE_OFFSET;
	iq.device_id = em_shm->conf.device_id;

	return iq.queue;
}

/** Convert queue ID (internal_queue_t:queue_id) to queue index */
static inline int
queue_id2idx(uint16_t queue_id)
{
	return (int)queue_id - EM_QUEUE_RANGE_OFFSET;
}

/** Convert queue index to queue ID (internal_queue_t:queue_id) */
static inline uint16_t
queue_idx2id(int queue_idx)
{
	return (uint16_t)queue_idx + EM_QUEUE_RANGE_OFFSET;
}

/** Convert queue ID (internal_queue_t:queue_id) handle to EM queue handle */
static inline em_queue_t
queue_id2hdl(uint16_t queue_id)
{
	internal_queue_t iq = {.queue = 0};

	iq.queue_id = queue_id;
	iq.device_id = em_shm->conf.device_id;

	return iq.queue;
}

/**
 * Return 'true' if the EM queue handle belongs to another EM instance.
 *
 * Sending to external queues will cause EM to call the user provided
 * functions 'event_send_device' or 'event_send_device_multi'
 */
static inline bool
queue_external(em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	if (unlikely(queue == EM_QUEUE_UNDEF))
		return 0;

	return iq.device_id != em_shm->conf.device_id ? true : false;
}

/** Returns queue element associated with queued id 'queue' */
static inline queue_elem_t *
queue_elem_get(const em_queue_t queue)
{
	int queue_idx;
	internal_queue_t iq;
	queue_elem_t *queue_elem;

	iq.queue = queue;
	queue_idx = queue_id2idx(iq.queue_id);

	if (unlikely(iq.device_id != em_shm->conf.device_id ||
		     (uint16_t)queue_idx > em_shm->queue_tbl.max_queue_num - 1))
		return NULL;

	queue_elem = &em_shm->queue_tbl.queue_elem[queue_idx];

	return queue_elem;
}

static inline em_queue_t
queue_current(void)
{
	const queue_elem_t *const q_elem = em_locm.current.q_elem;

	if (unlikely(q_elem == NULL))
		return EM_QUEUE_UNDEF;

	return (em_queue_t)(uintptr_t)q_elem->queue;
}

/**
 * Convert an EO queue list node to its containing queue element.
 *
 * @param list_node  Pointer to the 'eo_queue_node' member of a queue_elem_t
 *
 * @return Pointer to the queue element, or NULL if list_node is NULL
 */
static inline queue_elem_t *
eo_queue_node_to_queue_elem(const list_node_t *const list_node)
{
	queue_elem_t *const q_elem = (queue_elem_t *)((uintptr_t)list_node
				     - offsetof(queue_elem_t, eo_queue_node));

	return likely(list_node != NULL) ? q_elem : NULL;
}

static inline int
prio_em2odp(em_queue_prio_t em_prio, odp_schedule_prio_t *odp_prio /*out*/)
{
	if (em_prio < EM_QUEUE_PRIO_NUM) {
		*odp_prio = em_shm->queue_prio.map[em_prio];
		return 0;
	}
	return -1;
}

static inline int
scheduled_queue_type_em2odp(em_queue_type_t em_queue_type,
			    odp_schedule_sync_t *odp_schedule_sync /* out */)
{
	switch (em_queue_type) {
	case EM_QUEUE_TYPE_ATOMIC:
		*odp_schedule_sync = ODP_SCHED_SYNC_ATOMIC;
		return 0;
	case EM_QUEUE_TYPE_PARALLEL:
		*odp_schedule_sync = ODP_SCHED_SYNC_PARALLEL;
		return 0;
	case EM_QUEUE_TYPE_ORDERED:
		*odp_schedule_sync = ODP_SCHED_SYNC_ORDERED;
		return 0;
	default:
		return -1;
	}
}

static inline int
scheduled_queue_type_odp2em(odp_schedule_sync_t odp_schedule_sync,
			    em_queue_type_t *em_queue_type /* out */)
{
	switch (odp_schedule_sync) {
	case ODP_SCHED_SYNC_ATOMIC:
		*em_queue_type = EM_QUEUE_TYPE_ATOMIC;
		return 0;
	case ODP_SCHED_SYNC_PARALLEL:
		*em_queue_type = EM_QUEUE_TYPE_PARALLEL;
		return 0;
	case ODP_SCHED_SYNC_ORDERED:
		*em_queue_type = EM_QUEUE_TYPE_ORDERED;
		return 0;
	default:
		*em_queue_type = EM_QUEUE_TYPE_UNDEF;
		return 0;
	}
}

static inline em_event_t
queue_dequeue(const queue_elem_t *q_elem)
{
	odp_queue_t odp_queue;
	odp_event_t odp_event;
	em_event_t event;

	odp_queue = q_elem->odp_queue;
	odp_event = odp_queue_deq(odp_queue);
	if (odp_event == ODP_EVENT_INVALID)
		return EM_EVENT_UNDEF;

	event = event_odp2em(odp_event);

	/*
	 * Only aggregator parents (flags.has_aggr) can receive aggregation-
	 * produced vectors, which need full vector-header init plus, with ESV,
	 * the inner-event em2usr transition - event_init_odp() applies it. A
	 * plain queue only ever yields scalars or user-built vectors, both of
	 * which need at most the same em2usr as a scalar, so keep them on the
	 * fast path below (no per-event odp_event_type() probe).
	 */
	if (unlikely(q_elem->flags.has_aggr &&
		     odp_event_type(odp_event) == ODP_EVENT_VECTOR)) {
		/*
		 * .has_aggr implies the event-vector backend, so a vector here is
		 * always an ODP event vector - init it directly instead of via
		 * event_init_odp(). evhdr_init_evvec() inits the header and, with
		 * ESV, scans the inner table; the per-inner aggr_sent/refs_used
		 * flags pick aggregator-delivered slots needing em2usr and skip
		 * user-built ones.
		 */
		odp_event_vector_t odp_evvec = odp_event_vector_from_event(odp_event);
		event_hdr_t *ev_hdr = odp_event_vector_user_area(odp_evvec);

		return evhdr_init_evvec(ev_hdr, event, odp_evvec,
					true/*is_extev*/, q_elem);
	}

	if (esv_enabled()) {
		event_hdr_t *ev_hdr = event_to_hdr(event);

		event = evstate_em2usr(event, ev_hdr, EVSTATE__DEQUEUE);
	}

	return event;
}

static inline int
queue_dequeue_multi(const queue_elem_t *q_elem,
		    em_event_t events[/*out*/], int num)
{
	odp_queue_t odp_queue;
	int ret;

	/* use same output-array for dequeue: odp_events[] = events[] */
	odp_event_t *const odp_events = (odp_event_t *)events;

	odp_queue = q_elem->odp_queue;
	ret = odp_queue_deq_multi(odp_queue, odp_events /*out*/, num);
	if (ret <= 0)
		return ret;

	/* now events[] = odp_events[], events[].evgen missing, set below: */

	/*
	 * Only aggregator parents (flags.has_aggr) can receive aggregation-
	 * produced vectors that need full init + inner-event em2usr; handle
	 * those per-event. A plain queue never yields such vectors, so use the
	 * batched fast path below - and skip all per-event work when ESV is
	 * disabled.
	 */
	if (unlikely(q_elem->flags.has_aggr)) {
		/*
		 * .has_aggr implies the event-vector backend - a vector here is
		 * always an ODP event vector, init directly (see queue_dequeue()).
		 */
		for (int i = 0; i < ret; i++) {
			if (odp_event_type(odp_events[i]) == ODP_EVENT_VECTOR) {
				odp_event_vector_t evvec =
					odp_event_vector_from_event(odp_events[i]);
				event_hdr_t *ev_hdr =
					odp_event_vector_user_area(evvec);

				events[i] = evhdr_init_evvec(ev_hdr,
							     event_odp2em(odp_events[i]),
							     evvec, true/*is_extev*/,
							     q_elem);
			} else if (esv_enabled()) {
				event_hdr_t *ev_hdr = event_to_hdr(events[i]);

				events[i] = evstate_em2usr(events[i], ev_hdr,
							   EVSTATE__DEQUEUE_MULTI);
			}
		}
		return ret;
	}

	if (esv_enabled()) {
		event_hdr_t *ev_hdrs[ret];

		event_to_hdr_multi(events, ev_hdrs, ret);
		evstate_em2usr_multi(events, ev_hdrs, ret,
				     EVSTATE__DEQUEUE_MULTI);
	}

	return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_INLINE_H_ */
