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
  * EM internal event functions
  *
  */

#ifndef EM_EVENT_INLINE_H_
#define EM_EVENT_INLINE_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_event_state.h"
#include "em_event_types.h"
#include "em_mem.h"
#include "em_pool_inline.h"
#include "em_pool_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Convert an EM-event into an ODP-event */
static inline odp_event_t
event_em2odp(em_event_t event)
{
	/* Valid for both ESV enabled and disabled */
	evhdl_t evhdl = {.event = event};

	return (odp_event_t)(uintptr_t)evhdl.evptr;
}

/**
 * Convert an ODP-event into an EM-event
 *
 * @note The returned EM-event does NOT contain the ESV event-generation-count
 *       evhdl_t::evgen! This must be set separately when using ESV.
 */
static inline em_event_t
event_odp2em(odp_event_t odp_event)
{
	/* Valid for both ESV enabled and disabled */

	/*
	 * Setting 'evhdl.event = odp_event' is equal to
	 *         'evhdl.evptr = odp_event, evhdl.evgen = 0'
	 * (evhdl.evgen still needs to be set when using ESV)
	 */
	evhdl_t evhdl = {.event = (em_event_t)(uintptr_t)odp_event};

	return evhdl.event;
}

/** Convert an array of EM-events into an array ODP-events */
static inline void
events_em2odp(const em_event_t events[/*in*/],
	      odp_event_t odp_events[/*out*/], const unsigned int num)
{
	/* Valid for both ESV enabled and disabled */
	const evhdl_t *const evhdls = (const evhdl_t *)events;

	for (unsigned int i = 0; i < num; i++)
		odp_events[i] = (odp_event_t)(uintptr_t)evhdls[i].evptr;
}

/**
 * Convert an array of ODP-events into an array of EM-events
 *
 * @note The output EM-events do NOT contain the ESV event-generation-count
 *       evhdl_t::evgen! This must be set separately when using ESV.
 */
static inline void
events_odp2em(const odp_event_t odp_events[/*in*/],
	      em_event_t events[/*out*/], const unsigned int num)
{
	/*
	 * Valid for both ESV enabled and disabled.
	 *
	 * Bit-cast odp_event_t -> em_event_t. Copying the full 64-bit value
	 * into events[i] is equivalent to setting evhdl_t::evptr to the
	 * odp_event_t value and evhdl_t::evgen to 0 (evhdl_t::evgen still
	 * needs to be set separately when using ESV).
	 *
	 * sizeof(em_event_t) == sizeof(odp_event_t) guaranteed by a COMPILE_TIME_ASSERT(),
	 * so the raw byte copy is equivalent to the per-element bit-cast.
	 * Copy for(each i): events[i] = (em_event_t)(uintptr_t)odp_events[i]
	 */
	memcpy(events, odp_events, num * sizeof(em_event_t));
}

/**
 * Convert an array of EM-events into an array of ODP-packets.
 * The content must be known to be packets.
 */
static inline void
events_em2pkt(const em_event_t events[/*in*/],
	      odp_packet_t odp_pkts[/*out*/], const unsigned int num)
{
	/* Valid for both ESV enabled and disabled */
	const evhdl_t *const evhdls = (const evhdl_t *)events;

	for (unsigned int i = 0; i < num; i++)
		odp_pkts[i] = odp_packet_from_event((odp_event_t)(uintptr_t)evhdls[i].evptr);
}

/**
 * Convert an array of EM-events into an array of ODP-events in-place (i.e.
 * convert using the same memory area for output). Be careful!
 *
 * @return Pointer to odp event table: odp_event_t odp_events[num]
 *         Uses the same memory area for the output of 'odp_events[num]' as for
 *         the input 'events[num]' (thus overwrites 'events[num]' with
 *         'odp_events[num]').
 */
static inline odp_event_t *
events_em2odp_inplace(em_event_t events[/*in(/out)*/], const unsigned int num)
{
	/* Valid for both ESV enabled and disabled */
	const evhdl_t *const evhdls = (evhdl_t *)events;
	odp_event_t *const odp_events = (odp_event_t *)events; /* careful! */

	/* Careful! Overwrites events[num] with odp_events[num] */
	for (unsigned int i = 0; i < num; i++)
		odp_events[i] = (odp_event_t)(uintptr_t)evhdls[i].evptr;

	return odp_events;
}

/**
 * Convert an array of EM-events into an array of ODP-packets in-place (i.e.
 * convert using the same memory area for output) when the event type is known
 * for sure to be packets. Be careful!
 *
 * @return Pointer to odp packet table: odp_packet_t pkts[num]
 *         Uses the same memory area for the output of 'pkts[num]' as for
 *         the input 'events[num]' (thus overwrites 'events[num]' with
 *         'pkts[num]').
 */
static inline odp_packet_t *
events_em2pkt_inplace(em_event_t events[/*in(/out)*/], const unsigned int num)
{
	/* Valid for both ESV enabled and disabled */
	evhdl_t *const evhdls = (evhdl_t *)events;
	odp_packet_t *const pkts = (odp_packet_t *)events; /* careful! */

	/* Careful! Overwrites events[num] with pkts[num] */
	for (unsigned int i = 0; i < num; i++)
		pkts[i] = odp_packet_from_event((odp_event_t)(uintptr_t)evhdls[i].evptr);

	return pkts;
}

/**
 * @brief Convert event vector table content to odp events in-place.
 *
 * Convert an EM event vector table, containing em_event_t:s with
 * esv-info (evgen), to a table of odp events (remove handles' evgen in-place).
 *
 * Done before enqueuing a vector to a scheduled or unscheduled queue so the
 * inner-event table is in odp-form when later iterated on dispatch/dequeue.
 */
static inline void
evvec_tbl2odp(odp_event_t odp_event_vec)
{
	odp_event_vector_t odp_evv = odp_event_vector_from_event(odp_event_vec);
	odp_event_t *odp_evtbl = NULL;
	const int num = odp_event_vector_tbl(odp_evv, &odp_evtbl/*out*/);

	if (likely(num > 0)) {
		/* Careful! Points to same table (aliasing) */
		em_event_t *event_tbl = (em_event_t *)odp_evtbl;

		/* Drop ESV event generation (evgen) from event handle */
		(void)events_em2odp_inplace(event_tbl, num);
	}
}

/**
 * @brief Convert event vector table content to odp packets in-place.
 *
 * Convert an EM event vector table, containing em_event_t:s with
 * esv-info (evgen), to a table of odp packets (remove handles' evgen in-place).
 */
static inline void
pktvec_tbl2odp(odp_event_t odp_event_pktvec)
{
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event_pktvec);
	odp_packet_t *pkt_tbl = NULL;
	const int pkts = odp_packet_vector_tbl(pkt_vec, &pkt_tbl/*out*/);

	if (likely(pkts > 0)) {
		/* Careful! Points to same table (aliasing) */
		em_event_t *event_tbl = (em_event_t *)pkt_tbl;

		/* Drop ESV event generation (evgen) from event handle */
		(void)events_em2pkt_inplace(event_tbl, pkts);
	}
}

/**
 * @brief Convert from a packet-event to event header
 *
 * It has to be known that the event is a packet before calling this function,
 * otherwise use event_to_hdr().
 *
 * @param pkt_event       EM event based on odp_packet_t
 * @return event_hdr_t*   Pointer to the event header
 *
 * Does NOT initialize the event header.
 */
static inline event_hdr_t *
eventpkt_to_hdr(em_event_t pkt_event)
{
	odp_event_t odp_event = event_em2odp(pkt_event);
	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	event_hdr_t *ev_hdr = odp_packet_user_area(odp_pkt);

	return ev_hdr;
}

/**
 * @brief Convert from a buffer-event to event header
 *
 * It has to be known that the event is a buffer before calling this function,
 * otherwise use event_to_hdr().
 *
 * @param buf_event       EM event based on odp_buffer_t
 * @return event_hdr_t*   Pointer to the event header
 *
 * Does NOT initialize the event header.
 */
static inline event_hdr_t *
eventbuf_to_hdr(em_event_t buf_event)
{
	odp_event_t odp_event = event_em2odp(buf_event);
	odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);
	event_hdr_t *ev_hdr = odp_buffer_user_area(odp_buf);

	return ev_hdr;
}

/**
 * @brief Convert from an event vector to event header
 *
 * It has to be known that the event is a vector before calling this function,
 * otherwise use event_to_hdr().
 *
 * @param vector_event    EM event of major type EM_EVENT_TYPE_VECTOR
 * @return event_hdr_t*   Pointer to the event header
 *
 * Does NOT initialize the event header.
 */
static inline event_hdr_t *
eventvec_to_hdr(em_event_t vector_event)
{
	odp_event_t odp_event = event_em2odp(vector_event);
	odp_event_type_t odp_etype = odp_event_type(odp_event);
	event_hdr_t *ev_hdr;

	if (odp_etype == ODP_EVENT_VECTOR) {
		odp_event_vector_t odp_evvec = odp_event_vector_from_event(odp_event);

		ev_hdr = odp_event_vector_user_area(odp_evvec);
	} else /* ODP_EVENT_PACKET_VECTOR */ {
		odp_packet_vector_t odp_pktvec = odp_packet_vector_from_event(odp_event);

		ev_hdr = odp_packet_vector_user_area(odp_pktvec);
	}

	return ev_hdr;
}

/**
 * Convert from EM event to event header.
 *
 * Does NOT initialize the event header.
 */
static inline event_hdr_t *
event_to_hdr(em_event_t event)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_packet_t odp_pkt;
	odp_buffer_t odp_buf;
	odp_event_vector_t odp_evvec;
	odp_packet_vector_t odp_pktvec;
	odp_timeout_t odp_tmo;
	event_hdr_t *ev_hdr;

	odp_event_type_t evtype = odp_event_type(odp_event);

	switch (evtype) {
	case ODP_EVENT_PACKET:
		odp_pkt = odp_packet_from_event(odp_event);
		ev_hdr = odp_packet_user_area(odp_pkt);
		break;
	case ODP_EVENT_BUFFER:
		odp_buf = odp_buffer_from_event(odp_event);
		ev_hdr = odp_buffer_user_area(odp_buf);
		break;
	case ODP_EVENT_VECTOR:
		odp_evvec = odp_event_vector_from_event(odp_event);
		ev_hdr = odp_event_vector_user_area(odp_evvec);
		break;
	case ODP_EVENT_PACKET_VECTOR:
		odp_pktvec = odp_packet_vector_from_event(odp_event);
		ev_hdr = odp_packet_vector_user_area(odp_pktvec);
		break;
	case ODP_EVENT_TIMEOUT:
		odp_tmo = odp_timeout_from_event(odp_event);
		ev_hdr = odp_timeout_user_area(odp_tmo);
		break;
	default:
		/*
		 * Non-EM events don't have an event header of their own,
		 * use a temporary core-local event header instead.
		 */
		ev_hdr = &em_locm.evhdr_nonem;
		break;
	}

	return ev_hdr;
}

/**
 * Convert from EM events to event headers.
 *
 * Does NOT initialize the event headers.
 *
 * @param[in]   events   Input array of 'num' valid events
 * @param[out]  ev_hdrs  Output array with room to store 'num' pointers to the
 *                       corresponding event headers
 * @param       num      Number of entries in 'events[]' and 'ev_hdrs[]'
 */
static inline void
event_to_hdr_multi(const em_event_t events[], event_hdr_t *ev_hdrs[/*out*/],
		   const int num)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = event_to_hdr(events[i]);
}

/** Convert from event header to EM event */
static inline em_event_t
event_hdr_to_event(const event_hdr_t *const event_hdr)
{
	return event_hdr->event;
}

/**
 * Initialize the event header of a packet allocated outside of EM.
 */
static inline em_event_t
evhdr_init_pkt(event_hdr_t *ev_hdr, em_event_t event,
	       odp_packet_t odp_pkt, bool is_extev)
{
	const int user_flag_set = odp_packet_user_flag(odp_pkt);
	const bool esv_ena = esv_enabled();

	if (user_flag_set) {
		/* Event already initialized by EM */
		if (esv_ena) {
			event = ev_hdr->event;
			if (is_extev)
				event = evstate_em2usr(event, ev_hdr, EVSTATE__DISPATCH);
		}

		return event;
	}

	/*
	 * ODP pkt from outside of EM - not allocated by EM & needs init
	 */
	odp_packet_user_flag_set(odp_pkt, USER_FLAG_SET);
	ev_hdr->event_type = EM_EVENT_TYPE_PACKET;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	ev_hdr->user_area.all = 0; /* uarea fields init when used */

	if (!esv_ena) {
		ev_hdr->flags.all = 0;
		ev_hdr->event = event;
		return event;
	}

	/*
	 * ESV enabled:
	 */
	if (!em_shm->opt.esv.prealloc_pools || ev_hdr->flags.refs_used) {
		/* No prealloc OR pkt was a ref before being freed into the pool */
		event = evstate_init(event, ev_hdr, is_extev);
	} else {
		/* esv.prealloc_pools == true: */
		const odp_pool_t odp_pool = odp_packet_pool(odp_pkt);
		const em_pool_t pool = pool_odp2em(odp_pool);

		if (pool == EM_POOL_UNDEF) {
			/* External odp pkt originates from an ODP-pool */
			event = evstate_init(event, ev_hdr, is_extev);
		} else {
			/* External odp pkt originates from an EM-pool */
			event = evstate_update(event, ev_hdr, is_extev);
		}
	}
	ev_hdr->flags.all = 0; /* clear only after evstate_...() */

	return event;
}

/**
 * Initialize the event headers of packets allocated outside of EM.
 *
 * Note that events[] and odp_pkts[] may refer to the same memory locations,
 * i.e. an in-place update is possible (they may alias each other).
 */
static inline void
evhdr_init_pkt_multi(event_hdr_t *const ev_hdrs[],
		     em_event_t events[/*in,out*/],
		     const odp_packet_t odp_pkts[/*in*/] /* may alias events[] */,
		     const int num, bool is_extev)
{
	const bool esv_ena = esv_enabled();
	int user_flag_set;

	int needs_init_idx[num];
	int needs_init_num = 0;
	int idx;

	for (int i = 0; i < num; i++) {
		user_flag_set = odp_packet_user_flag(odp_pkts[i]);
		if (!user_flag_set) {
			/* ODP pkt not yet initialized by EM - defer init below */
			odp_packet_user_flag_set(odp_pkts[i], USER_FLAG_SET);
			needs_init_idx[needs_init_num++] = i;
			continue;
		}
		/* Event already initialized by EM */
		if (esv_ena) {
			events[i] = ev_hdrs[i]->event;
			if (is_extev)
				events[i] = evstate_em2usr(events[i], ev_hdrs[i],
							   EVSTATE__DISPATCH_MULTI);
		}
		/* else: events[i] = events[i], no update needed */
	}

	if (needs_init_num == 0)
		return;

	/*
	 * ODP pkt from outside of EM - not allocated by EM & needs init
	 */

	if (!esv_ena) {
		for (int i = 0; i < needs_init_num; i++) {
			idx = needs_init_idx[i];
			ev_hdrs[idx]->flags.all = 0;
			ev_hdrs[idx]->event_type = EM_EVENT_TYPE_PACKET;
			ev_hdrs[idx]->event = events[idx];
			ev_hdrs[idx]->egrp = EM_EVENT_GROUP_UNDEF;
			ev_hdrs[idx]->user_area.all = 0; /* uarea fields init when used */
		}

		return;
	}

	/*
	 * ESV enabled:
	 */
	if (!em_shm->opt.esv.prealloc_pools) {
		for (int i = 0; i < needs_init_num; i++) {
			idx = needs_init_idx[i];
			events[idx] = evstate_init(events[idx], ev_hdrs[idx], is_extev);
		}
	} else {
		/* em_shm->opt.esv.prealloc_pools == true */
		for (int i = 0; i < needs_init_num; i++) {
			idx = needs_init_idx[i];

			odp_pool_t odp_pool = odp_packet_pool(odp_pkts[idx]);
			em_pool_t pool = pool_odp2em(odp_pool);

			if (pool == EM_POOL_UNDEF || ev_hdrs[idx]->flags.refs_used) {
				/*
				 * External odp pkt originates from an ODP-pool,
				 * or pkt was a ref before being freed into the pool.
				 */
				events[idx] = evstate_init(events[idx], ev_hdrs[idx], is_extev);
			} else {
				/* External odp pkt originates from an EM-pool */
				events[idx] = evstate_update(events[idx], ev_hdrs[idx], is_extev);
			}
		}
	}

	for (int i = 0; i < needs_init_num; i++) {
		idx = needs_init_idx[i];
		ev_hdrs[idx]->flags.all = 0; /* clear only after evstate_...() */
		ev_hdrs[idx]->event_type = EM_EVENT_TYPE_PACKET;
		ev_hdrs[idx]->egrp = EM_EVENT_GROUP_UNDEF;
		ev_hdrs[idx]->user_area.all = 0; /* uarea fields init when used */
	}
}

/**
 * Apply the missing evstate_em2usr() for one inner event from an
 * aggregation-produced vector: non-ref sends set aggr_sent in send_aggr();
 * ref sends keep aggr_sent clear but set refs_used — still enter here.
 * Skip inner slots that are neither (e.g. user-built vectors).
 */
static inline void
esv_aggr_inner_em2usr_one(odp_event_t odp_ev)
{
	event_hdr_t *inner_hdr = event_to_hdr(event_odp2em(odp_ev));

	/*
	 * Non-ref aggregator sends set aggr_sent in send_aggr[_multi]().
	 * Ref sends must not touch aggr_sent (single flags byte vs refs);
	 * identify them via refs_used and still apply em2usr here.
	 * Skip when neither applies — inner slot is not from send_aggr (e.g.
	 * user-built vector).
	 */
	if (!inner_hdr->flags.refs_used && !inner_hdr->flags.aggr_sent)
		return;
	/* Use the stored handle (correct evgen from send). */
	(void)evstate_em2usr(inner_hdr->event, inner_hdr,
			     EVSTATE__DISPATCH_AGGR);
	if (!inner_hdr->flags.refs_used)
		inner_hdr->flags.aggr_sent = 0;
}

/**
 * Apply the missing em2usr ESV-transition for inner events of an
 * aggregation-produced vector.
 *
 * Events sent to EM_QUEUE_TYPE_AGGR go through evstate_usr2em() at send time
 * but never receive evstate_em2usr() at dispatch because ODP bundles them into
 * a vector transparently. Non-ref sends set aggr_sent in send_aggr(); ref
 * sends do not touch aggr_sent but refs_used is set — esv_aggr_inner_em2usr_one()
 * handles both. User-constructed vectors have neither bit set so inner slots
 * are skipped.
 *
 * Pktio-originated packets (user_flag=0) have never been seen by EM and may
 * have uninitialized user area - skip them before reading aggr_sent.
 * ODP buffers are always EM-allocated and need no such guard.
 *
 * Called only when ESV is enabled and is_extev=true.
 *
 * @param disp_q_elem  Queue the vector is being dispatched from, or NULL when
 *                     the caller is not a normal dispatch (e.g. freeing dropped
 *                     events). See the in-body note for the skip condition.
 */
static inline void
esv_aggr_inner_em2usr(odp_event_vector_t odp_evvec,
		      const queue_elem_t *disp_q_elem)
{
	/*
	 * Aggregation-produced vectors are only ever delivered on the parent
	 * queue that owns the event aggregators (flags.has_aggr==1), so when
	 * dispatching from a queue without aggregators no inner slot can carry
	 * ev_hdr->flags.aggr_sent = 1 and the per-inner scan is unnecessary.
	 * disp_q_elem==NULL marks a non-dispatch caller (e.g. freeing dropped
	 * events): keep the scan so inner events are moved to user-side state
	 * before em_free().
	 */
	if (disp_q_elem && !disp_q_elem->flags.has_aggr)
		return;

	odp_event_t *odp_evtbl = NULL;
	uint32_t sz = odp_event_vector_tbl(odp_evvec, &odp_evtbl);
	odp_event_type_t odp_vtype = odp_event_vector_type(odp_evvec);

	/*
	 * Type-dispatched once per vector (ODP guarantees the vector type
	 * reflects all inner events or is ODP_EVENT_ANY when mixed/unknown).
	 */
	if (odp_vtype == ODP_EVENT_PACKET) {
		for (uint32_t i = 0; i < sz; i++) {
			/* Skip pktio packets not yet initialized by EM */
			if (!odp_packet_user_flag(odp_packet_from_event(odp_evtbl[i])))
				continue;
			esv_aggr_inner_em2usr_one(odp_evtbl[i]);
		}
	} else if (odp_vtype == ODP_EVENT_BUFFER) {
		/* All EM-allocated buffers, no pktio guard needed */
		for (uint32_t i = 0; i < sz; i++)
			esv_aggr_inner_em2usr_one(odp_evtbl[i]);
	} else {
		/* ODP_EVENT_ANY: mixed or unknown, per-element type check */
		for (uint32_t i = 0; i < sz; i++) {
			/* Skip pktio packets not yet initialized by EM */
			if (odp_event_type(odp_evtbl[i]) == ODP_EVENT_PACKET &&
			    !odp_packet_user_flag(odp_packet_from_event(odp_evtbl[i])))
				continue;
			esv_aggr_inner_em2usr_one(odp_evtbl[i]);
		}
	}
}

/**
 * Initialize the event header of an event vector allocated outside of EM.
 */
static inline em_event_t
evhdr_init_evvec(event_hdr_t *ev_hdr, em_event_t event,
		 odp_event_vector_t odp_evvec, bool is_extev,
		 const queue_elem_t *disp_q_elem)
{
	const int user_flag = odp_event_vector_user_flag(odp_evvec);
	const bool esv_ena = esv_enabled();

	if (user_flag == USER_FLAG_SET) {
		/* Event already initialized by EM */
		if (esv_ena) {
			event = ev_hdr->event;
			if (is_extev) {
				event = evstate_em2usr(event, ev_hdr,
						       EVSTATE__DISPATCH);
				esv_aggr_inner_em2usr(odp_evvec, disp_q_elem);
			}
		}

		return event;
	}

	/*
	 * ODP event vector from outside of EM - not allocated by EM & needs init
	 *
	 * Note: ODP resets the user_flag when allocating from a pool even if the
	 * pool was pre-initialized by EM (prealloc_pools=true). Hence aggregation-
	 * produced vectors from an EM pool also enter this path.
	 */
	odp_event_vector_user_flag_set(odp_evvec, USER_FLAG_SET);
	/* can clear flags before evstate_...(), flags.refs_used not set for vecs */
	ev_hdr->flags.all = 0;
	ev_hdr->event_type = EM_EVENT_TYPE_VECTOR;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	ev_hdr->user_area.all = 0; /* uarea fields init when used */

	if (!esv_ena) {
		ev_hdr->event = event;
		return event;
	}

	/*
	 * ESV enabled:
	 */
	if (!em_shm->opt.esv.prealloc_pools) {
		event = evstate_init(event, ev_hdr, is_extev);
	} else {
		/* esv.prealloc_pools == true: */
		odp_pool_t odp_pool = odp_event_vector_pool(odp_evvec);
		em_pool_t pool = pool_odp2em(odp_pool);

		if (pool == EM_POOL_UNDEF) {
			/* External odp event vector originates from an ODP-pool */
			event = evstate_init(event, ev_hdr, is_extev);
		} else {
			/* External odp event vector originates from an EM-pool */
			event = evstate_update(event, ev_hdr, is_extev);
		}
	}

	if (!is_extev)
		return event;

	/* is_extev=true: apply em2usr for aggr-sent inner events */
	esv_aggr_inner_em2usr(odp_evvec, disp_q_elem);
	return event;
}

/**
 * Initialize the event header of a packet vector allocated outside of EM.
 */
static inline em_event_t
evhdr_init_pktvec(event_hdr_t *ev_hdr, em_event_t event,
		  odp_packet_vector_t odp_pktvec, bool is_extev)
{
	const int user_flag = odp_packet_vector_user_flag(odp_pktvec);
	const bool esv_ena = esv_enabled();

	if (user_flag == USER_FLAG_SET) {
		/* Event already initialized by EM */
		if (esv_ena) {
			event = ev_hdr->event;
			if (is_extev)
				event = evstate_em2usr(event, ev_hdr, EVSTATE__DISPATCH);
		}

		return event;
	}

	/*
	 * ODP pkt vector from outside of EM - not allocated by EM & needs init
	 */
	odp_packet_vector_user_flag_set(odp_pktvec, USER_FLAG_SET);
	/* can clear flags before evstate_...(), flags.refs_used not set for vecs */
	ev_hdr->flags.all = 0;
	ev_hdr->event_type = EM_EVENT_TYPE_VECTOR;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	ev_hdr->user_area.all = 0; /* uarea fields init when used */

	if (!esv_ena) {
		ev_hdr->event = event;
		return event;
	}

	/*
	 * ESV enabled:
	 */
	if (!em_shm->opt.esv.prealloc_pools) {
		event = evstate_init(event, ev_hdr, is_extev);
	} else {
		/* esv.prealloc_pools == true: */
		odp_pool_t odp_pool = odp_packet_vector_pool(odp_pktvec);
		em_pool_t pool = pool_odp2em(odp_pool);

		if (pool == EM_POOL_UNDEF) {
			/* External odp pkt originates from an ODP-pool */
			event = evstate_init(event, ev_hdr, is_extev);
		} else {
			/* External odp pkt originates from an EM-pool */
			event = evstate_update(event, ev_hdr, is_extev);
		}
	}

	return event;
}

/**
 * Initialize an external ODP event that have been input into EM.
 *
 * Initialize the event header if needed, i.e. if event originated from outside
 * of EM from pktio or other input and was not allocated by EM via em_alloc().
 * The odp pkt-user-ptr is used to determine whether the header has been
 * initialized or not.
 *
 * @param disp_q_elem  Queue the event is being dispatched from, or NULL for
 *                     non-dispatch callers. Only used to gate the inner-event
 *                     ESV pass for aggregation-produced vectors, see
 *                     esv_aggr_inner_em2usr().
 */
static inline em_event_t
event_init_odp(odp_event_t odp_event, bool is_extev,
	       const queue_elem_t *disp_q_elem, event_hdr_t **ev_hdr__out)
{
	const odp_event_type_t odp_type = odp_event_type(odp_event);
	em_event_t event = event_odp2em(odp_event); /* return value, updated by ESV */

	switch (odp_type) {
	case ODP_EVENT_PACKET: {
		odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
		event_hdr_t *ev_hdr = odp_packet_user_area(odp_pkt);

		/* init event-hdr if needed (also ESV-state if used) */
		event = evhdr_init_pkt(ev_hdr, event, odp_pkt, is_extev);
		if (ev_hdr__out)
			*ev_hdr__out = ev_hdr;
		return event;
	}
	case ODP_EVENT_BUFFER: {
		const bool esv_ena = esv_enabled();

		if (!ev_hdr__out && !esv_ena)
			return event;

		odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);
		event_hdr_t *ev_hdr = odp_buffer_user_area(odp_buf);

		if (esv_ena) {
			/* event_odp2em() zeroed .evgen; refresh it.
			 * is_extev: state machine (re)writes .evgen for
			 *           !refs_used; refs ignore evgen.
			 * else:     load EM-tracked handle from ev_hdr.
			 */
			if (is_extev)
				event = evstate_em2usr(event, ev_hdr, EVSTATE__DISPATCH);
			else
				event = ev_hdr->event;
		}
		if (ev_hdr__out)
			*ev_hdr__out = ev_hdr;
		return event;
	}
	case ODP_EVENT_VECTOR: {
		odp_event_vector_t odp_evvec = odp_event_vector_from_event(odp_event);
		event_hdr_t *ev_hdr = odp_event_vector_user_area(odp_evvec);

		/* init event-hdr if needed (also ESV-state if used) */
		event = evhdr_init_evvec(ev_hdr, event, odp_evvec, is_extev,
					 disp_q_elem);
		if (ev_hdr__out)
			*ev_hdr__out = ev_hdr;
		return event;
	}
	case ODP_EVENT_PACKET_VECTOR: {
		odp_packet_vector_t odp_pktvec = odp_packet_vector_from_event(odp_event);
		event_hdr_t *ev_hdr = odp_packet_vector_user_area(odp_pktvec);

		/* init event-hdr if needed (also ESV-state if used) */
		event = evhdr_init_pktvec(ev_hdr, event, odp_pktvec, is_extev);
		if (ev_hdr__out)
			*ev_hdr__out = ev_hdr;
		return event;
	}
	case ODP_EVENT_TIMEOUT: {
		odp_timeout_t odp_tmo = odp_timeout_from_event(odp_event);
		event_hdr_t *ev_hdr = odp_timeout_user_area(odp_tmo);
		const bool esv_ena = esv_enabled();

		if (esv_ena) {
			/*
			 * Update event handle, no other ESV checks done.
			 * Some timers might send a copy of the original event
			 * in tear-down, thus keep ptr but update evgen.
			 */
			evhdl_t evhdl = {.event = event}; /* .evptr from here */
			evhdl_t evhdr_hdl = {.event = ev_hdr->event}; /* .evgen from here */

			evhdl.evgen = evhdr_hdl.evgen; /* update .evgen */
			ev_hdr->event = evhdl.event; /* store updated hdl in hdr */
			event = evhdl.event; /* return updated event */
		}

		if (ev_hdr__out)
			*ev_hdr__out = ev_hdr;
		return event;
	}
	default:
		/*
		 * Non-EM events don't have an event header of their own,
		 * use a temporary core-local event header instead.
		 */
		if (ev_hdr__out)
			*ev_hdr__out = &em_locm.evhdr_nonem;
		return event;
	}
}

/**
 * Helper to event_init_odp_multi()
 * Initialize the event headers of packets allocated outside of EM.
 *
 * Note that events[] and odp_pkts[] may refer to the same memory locations,
 * i.e. an in-place update is possible (they may alias each other).
 *
 * The implementation minimizes copies and preserves correctness even when
 * events[] and odp_pkts[] alias, by sequencing reads before writes and only
 * touching entries that need init.
 * The only practical risk is strict aliasing but that has been disabled by
 * compiler flags in this project (-fno-strict-aliasing).
 */
static inline void
event_init_pkt_multi(const odp_packet_t odp_pkts[/*in*/] /* may alias events[] */,
		     em_event_t events[/*in,out*/],
		     event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_packet_user_area(odp_pkts[i]);

	evhdr_init_pkt_multi(ev_hdrs, events, odp_pkts, num, is_extev);
}

/**
 * Helper to event_init_odp_multi()
 * Initialize the event headers of a batch of ODP buffers.
 *
 * ODP buffers reaching EM are always EM-allocated, so unlike the packet
 * helper there is no needs-init split: every entry is "already initialized".
 *
 * Caller must pre-populate events[] with events_odp2em() bit-casts; this
 * supplies the correct .evptr (and is the final value for the ESV-off path).
 */
static inline void
event_init_buf_multi(const odp_buffer_t odp_bufs[/*in*/],
		     em_event_t events[/*in,out*/],
		     event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_buffer_user_area(odp_bufs[i]);

	if (!esv_enabled())
		return; /* bit-cast events[] already correct */

	if (is_extev) {
		/* evstate_em2usr_multi() sets events[i].evgen for !refs_used;
		 * refs ignore evgen. .evptr already correct from bit-cast.
		 */
		evstate_em2usr_multi(events, ev_hdrs, num, EVSTATE__DISPATCH_MULTI);
		return;
	}

	/* !is_extev: no state update -> load EM-tracked handle for the EO. */
	for (int i = 0; i < num; i++)
		events[i] = ev_hdrs[i]->event;
}

/**
 * Convert from EM events to event headers and initialize the headers as needed
 * (e.g. for events entering EM from pktio that were not allocated via em_alloc).
 *
 * Fast path: a leading run of same-type ODP_EVENT_PACKET or ODP_EVENT_BUFFER
 * events is dispatched to a batched helper, avoiding the per-event
 * odp_event_type() switch in event_init_odp(). Any tail (different type) and
 * non-packet/non-buffer leading types fall back to the per-event loop.
 *
 * @param disp_q_elem  Queue the events are being dispatched from, or NULL for
 *                     non-dispatch callers. Forwarded to event_init_odp() to
 *                     gate the inner-event ESV pass for aggregation-produced
 *                     vectors, see esv_aggr_inner_em2usr().
 */
static inline void
event_init_odp_multi(const odp_event_t odp_events[/*in*/],
		     em_event_t events[/*out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev,
		     const queue_elem_t *disp_q_elem)
{
	if (unlikely(num <= 0))
		return;

	odp_event_type_t type;
	const int same = odp_event_type_multi(odp_events, num, &type);
	int handled_num = 0;

	switch (type) {
	case ODP_EVENT_PACKET: {
		odp_packet_t odp_pkts[same];

		odp_packet_from_event_multi(odp_pkts, odp_events, same);
		/* Pre-populate events[] so evhdr_init_pkt_multi() can use it
		 * for needs-init slots (evstate_init/update inputs and the
		 * !esv_ena 'ev_hdr->event = events[i]' assignment).
		 */
		events_odp2em(odp_events, events, same);
		event_init_pkt_multi(odp_pkts, events, ev_hdrs, same, is_extev);
		handled_num = same;
		break;
	}
	case ODP_EVENT_BUFFER: {
		odp_buffer_t odp_bufs[same];

		odp_buffer_from_event_multi(odp_bufs, odp_events, same);
		/* Pre-populate events[] so the !esv_ena branch of
		 * event_init_buf_multi() can keep the bit-cast handle.
		 */
		events_odp2em(odp_events, events, same);
		event_init_buf_multi(odp_bufs, events, ev_hdrs, same, is_extev);
		handled_num = same;
		break;
	}
	default:
		/* No batched fast path for vectors / packet-vectors / timeouts
		 * / non-EM events: keep the per-event init below.
		 */
		break;
	}

	if (likely(handled_num == num))
		return;

	for (int i = handled_num; i < num; i++)
		events[i] = event_init_odp(odp_events[i], is_extev, disp_q_elem,
					   &ev_hdrs[i]);
}

/**
 * @brief Revert conversion of event vector table content to odp events in-place.
 *
 * Revert operation done by evvec_tbl2odp().
 * Convert an ODP event vector table to a table of EM events.
 * The content must be known to be odp events.
 *
 * For recovery purposes only (e.g. a vector returned to the user after a
 * failed enqueue, see send_sched() / queue_unsched_enqueue()).
 */
static inline void
evvec_tbl2odp_revert(odp_event_t odp_event_vec)
{
	odp_event_vector_t odp_evv = odp_event_vector_from_event(odp_event_vec);
	odp_event_t *odp_evtbl = NULL;
	const int num = odp_event_vector_tbl(odp_evv, &odp_evtbl/*out*/);

	if (likely(num > 0)) {
		/* Careful! Points to same table (aliasing) */
		em_event_t *const ev_tbl = (em_event_t *const)odp_evtbl;
		event_hdr_t *ev_hdr_tbl[num];

		event_init_odp_multi(odp_evtbl /*in*/, ev_tbl /*in,out*/,
				     ev_hdr_tbl /*out*/, num, false,
				     NULL/*disp_q_elem*/);
	}
}

/**
 * @brief Revert conversion of event vector table content to odp packets in-place.
 *
 * Revert operation done by pktvec_tbl2odp().
 * Convert an ODP packet vector table to a table of EM events.
 * The content must be known to be raw odp packets.
 *
 * For recovery purposes only (e.g. a vector returned to the user after a
 * failed enqueue, see send_sched() / queue_unsched_enqueue()).
 */
static inline void
pktvec_tbl2odp_revert(odp_event_t odp_event_pktvec)
{
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event_pktvec);
	odp_packet_t *pkt_tbl = NULL;
	const int pkts = odp_packet_vector_tbl(pkt_vec, &pkt_tbl/*out*/);

	if (likely(pkts > 0)) {
		/* Careful! Points to same table (aliasing) */
		em_event_t *const ev_tbl = (em_event_t *const)pkt_tbl;
		event_hdr_t *ev_hdr_tbl[pkts];

		event_init_pkt_multi(pkt_tbl /*in*/, ev_tbl /*in,out*/,
				     ev_hdr_tbl /*out*/, pkts, false);
	}
}

/**
 * Allocate an event based on an odp-buf.
 */
static inline event_hdr_t *
event_alloc_buf(const mpool_elem_t *const pool_elem, uint32_t size)
{
	odp_buffer_t odp_buf = ODP_BUFFER_INVALID;
	int subpool;

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has buffers available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return NULL;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return NULL;

		odp_buf = odp_buffer_alloc(odp_pool);
		if (likely(odp_buf != ODP_BUFFER_INVALID))
			break;
	}

	if (unlikely(odp_buf == ODP_BUFFER_INVALID))
		return NULL;

	/*
	 * odp buffer now allocated - init the EM event header
	 * in the odp user area.
	 */
	event_hdr_t *const ev_hdr = odp_buffer_user_area(odp_buf);
	odp_event_t odp_event = odp_buffer_to_event(odp_buf);
	em_event_t event = event_odp2em(odp_event);

	ev_hdr->event = event;  /* store this event handle */
	ev_hdr->align_offset = (uint8_t)pool_elem->align_offset;

	/* init common ev_hdr fields in the caller */

	return ev_hdr;
}

/**
 * Allocate & initialize multiple events based on odp-bufs.
 */
static inline int
event_alloc_buf_multi(em_event_t events[/*out*/], const int num,
		      const mpool_elem_t *pool_elem, uint32_t size,
		      em_event_type_t type)
{
	odp_buffer_t odp_bufs[num];
	odp_event_t odp_event;
	event_hdr_t *ev_hdrs[num];
	int subpool;
	const bool esv_ena = esv_enabled();

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has buffers available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return 0;

	int num_req = num;
	int num_bufs = 0;
	int i;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return 0;

		int ret = odp_buffer_alloc_multi(odp_pool, &odp_bufs[num_bufs],
						 num_req);
		if (unlikely(ret <= 0))
			continue; /* try next subpool */

		/* store the allocated events[] */
		for (i = num_bufs; i < num_bufs + ret; i++) {
			odp_event = odp_buffer_to_event(odp_bufs[i]);
			events[i] = event_odp2em(odp_event);
		}

		/* Init 'ret' ev-hdrs from this 'subpool'=='odp-pool' */
		for (i = num_bufs; i < num_bufs + ret; i++)
			ev_hdrs[i] = odp_buffer_user_area(odp_bufs[i]);

		if (esv_ena) {
			/* reads ev_hdrs[i]->flags if prealloc_pools used */
			evstate_alloc_multi(&events[num_bufs] /*in/out*/,
					    &ev_hdrs[num_bufs], ret);
		}

		for (i = num_bufs; i < num_bufs + ret; i++) {
			ev_hdrs[i]->flags.all = 0;
			ev_hdrs[i]->event_type = type;
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size;
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

			ev_hdrs[i]->user_area.all = 0;
			ev_hdrs[i]->user_area.size = pool_elem->user_area.size;
			ev_hdrs[i]->user_area.isinit = 1;

			ev_hdrs[i]->align_offset = (uint8_t)pool_elem->align_offset;
		}

		num_bufs += ret;
		if (likely(num_bufs == num))
			break; /* all allocated */
		num_req -= ret;
	}

	return num_bufs; /* number of allocated bufs (0 ... num) */
}

/**
 * Allocate & initialize an event based on an odp-pkt.
 */
static inline event_hdr_t *
event_alloc_pkt(const mpool_elem_t *pool_elem, uint32_t size)
{
	const uint32_t push_len = pool_elem->align_offset;
	uint32_t pull_len;
	uint32_t alloc_size;
	odp_packet_t odp_pkt = ODP_PACKET_INVALID;
	int subpool;

	if (size > push_len) {
		alloc_size = size - push_len;
		pull_len = 0;
	} else {
		alloc_size = 1; /* min allowed */
		pull_len = push_len + 1 - size;
	}

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return NULL;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return NULL;

		odp_pkt = odp_packet_alloc(odp_pool, alloc_size);
		if (likely(odp_pkt != ODP_PACKET_INVALID))
			break;
	}

	if (unlikely(odp_pkt == ODP_PACKET_INVALID))
		return NULL;

	/*
	 * odp packet now allocated - adjust the payload start address and
	 * init the EM event header in the odp-pkt user-area
	 */

	/* Adjust event payload start-address based on alignment config */
	const void *ptr;

	if (push_len) {
		ptr = odp_packet_push_head(odp_pkt, push_len);
		if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr))
			goto err_pktalloc;
	}
	if (pull_len) {
		ptr = odp_packet_pull_tail(odp_pkt, pull_len);
		if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr))
			goto err_pktalloc;
	}

	/*
	 * Set the pkt user ptr to be able to recognize pkt-events that
	 * EM has created vs pkts from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	odp_packet_user_flag_set(odp_pkt, USER_FLAG_SET);

	event_hdr_t *const ev_hdr = odp_packet_user_area(odp_pkt);
	odp_event_t odp_event = odp_packet_to_event(odp_pkt);
	em_event_t event = event_odp2em(odp_event);

	if (EM_CHECK_LEVEL >= 3 && unlikely(ev_hdr == NULL))
		goto err_pktalloc;

	/* store this event handle */
	ev_hdr->event = event;

	/* init common ev_hdr fields in the caller */

	return ev_hdr;

err_pktalloc:
	odp_packet_free(odp_pkt);
	return NULL;
}

/*
 * Helper for event_alloc_pkt_multi()
 */
static inline int
pktalloc_multi(odp_packet_t odp_pkts[/*out*/], int num,
	       odp_pool_t odp_pool, uint32_t size,
	       uint32_t push_len, uint32_t pull_len)
{
	int ret = odp_packet_alloc_multi(odp_pool, size, odp_pkts, num);

	if (unlikely(ret <= 0))
		return 0;

	const int num_pkts = ret; /* return value > 0 */
	const void *ptr = NULL;
	int i;

	/* Adjust payload start-address based on alignment config */
	if (push_len) {
		for (i = 0; i < num_pkts; i++) {
			ptr = odp_packet_push_head(odp_pkts[i], push_len);
			if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr))
				goto err_pktalloc_multi;
		}
	}
	if (pull_len) {
		for (i = 0; i < num_pkts; i++) {
			ptr = odp_packet_pull_tail(odp_pkts[i], pull_len);
			if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr))
				goto err_pktalloc_multi; /* only before esv */
		}
	}

	/*
	 * Set the pkt user ptr to be able to recognize pkt-events that
	 * EM has created vs pkts from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	for (i = 0; i < num_pkts; i++)
		odp_packet_user_flag_set(odp_pkts[i], USER_FLAG_SET);

	return num_pkts;

err_pktalloc_multi:
	odp_packet_free_sp(odp_pkts, num_pkts);
	return 0;
}

/**
 * Allocate & initialize multiple events based on odp-pkts.
 */
static inline int
event_alloc_pkt_multi(em_event_t events[/*out*/], const int num,
		      const mpool_elem_t *pool_elem, uint32_t size,
		      em_event_type_t type)
{
	const uint32_t push_len = pool_elem->align_offset;
	uint32_t pull_len;
	odp_packet_t odp_pkts[num];
	/* use same output-array: odp_events[] = events[] */
	odp_event_t *const odp_events = (odp_event_t *)events;
	event_hdr_t *ev_hdrs[num];
	uint32_t alloc_size;
	int subpool;
	const bool esv_ena = esv_enabled();

	if (size > push_len) {
		alloc_size = size - push_len;
		pull_len = 0;
	} else {
		alloc_size = 1; /* min allowed */
		pull_len = push_len + 1 - size;
	}

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return 0;

	int num_req = num;
	int num_pkts = 0;
	int i;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return 0;

		int ret = pktalloc_multi(&odp_pkts[num_pkts], num_req,
					 odp_pool, alloc_size,
					 push_len, pull_len);
		if (unlikely(ret <= 0))
			continue; /* try next subpool */

		/*
		 * Init 'ret' ev-hdrs from this 'subpool'=='odp-pool'.
		 * Note: odp_events[] points&writes into events[out]
		 */
		odp_packet_to_event_multi(&odp_pkts[num_pkts],
					  &odp_events[num_pkts], ret);

		for (i = num_pkts; i < num_pkts + ret; i++)
			ev_hdrs[i] = odp_packet_user_area(odp_pkts[i]);

		/*
		 * Note: events[] == odp_events[] before ESV init.
		 * Don't touch odp_events[] during this loop-round anymore.
		 */
		if (esv_ena) {
			/* reads ev_hdrs[i]->flags if prealloc_pools used */
			evstate_alloc_multi(&events[num_pkts] /*in/out*/,
					    &ev_hdrs[num_pkts], ret);
		}

		for (i = num_pkts; i < num_pkts + ret; i++) {
			ev_hdrs[i]->flags.all = 0;
			ev_hdrs[i]->event_type = type;
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size; /* original size */
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

			ev_hdrs[i]->user_area.all = 0;
			ev_hdrs[i]->user_area.size = pool_elem->user_area.size;
			ev_hdrs[i]->user_area.isinit = 1;
			/*ev_hdrs[i]->align_offset = needed by odp bufs only*/
		}

		num_pkts += ret;
		if (likely(num_pkts == num))
			break; /* all allocated */
		num_req -= ret;
	}

	return num_pkts; /* number of allocated pkts */
}

static inline event_hdr_t *
event_alloc_vector__odp_pktvec(const mpool_elem_t *pool_elem, uint32_t size)
{
	odp_packet_vector_t odp_pktvec = ODP_PACKET_VECTOR_INVALID;
	int subpool;

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return NULL;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return NULL;

		odp_pktvec = odp_packet_vector_alloc(odp_pool);
		if (likely(odp_pktvec != ODP_PACKET_VECTOR_INVALID))
			break;
	}

	if (unlikely(odp_pktvec == ODP_PACKET_VECTOR_INVALID))
		return NULL;

	/*
	 * Packet vector now allocated:
	 * Init the EM event header in the odp-pkt-vector user-area.
	 */

	/*
	 * Set the pktvec user flag to be able to recognize vectors that
	 * EM has created vs. vectors from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	odp_packet_vector_user_flag_set(odp_pktvec, USER_FLAG_SET);

	event_hdr_t *const ev_hdr = odp_packet_vector_user_area(odp_pktvec);
	odp_event_t odp_event = odp_packet_vector_to_event(odp_pktvec);
	em_event_t event = event_odp2em(odp_event);

	if (EM_CHECK_LEVEL >= 3 && unlikely(ev_hdr == NULL))
		goto err_vecalloc;

	ev_hdr->event = event;  /* store this event handle */
	ev_hdr->vector_type = EM_EVENT_TYPE_PACKET;
	/* init common ev_hdr fields in the caller */

	return ev_hdr;

err_vecalloc:
	odp_packet_vector_free(odp_pktvec);
	return NULL;
}

static inline event_hdr_t *
event_alloc_vector__odp_evvec(const mpool_elem_t *pool_elem, uint32_t size)
{
	odp_event_vector_t odp_evvec = ODP_EVENT_VECTOR_INVALID;
	int subpool;

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return NULL;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return NULL;

		odp_evvec = odp_event_vector_alloc(odp_pool);
		if (likely(odp_evvec != ODP_EVENT_VECTOR_INVALID))
			break;
	}

	if (unlikely(odp_evvec == ODP_EVENT_VECTOR_INVALID))
		return NULL;

	/*
	 * ODP Event vector now allocated:
	 * Init the EM event header in the odp-event-vector user-area.
	 */

	/*
	 * Set the event vector user flag to be able to recognize vectors that
	 * EM has created vs. vectors that needs their ev-hdrs to be initialized.
	 */
	odp_event_vector_user_flag_set(odp_evvec, USER_FLAG_SET);

	event_hdr_t *const ev_hdr = odp_event_vector_user_area(odp_evvec);
	odp_event_t odp_event = odp_event_vector_to_event(odp_evvec);
	em_event_t event = event_odp2em(odp_event);

	if (EM_CHECK_LEVEL >= 3 && unlikely(ev_hdr == NULL))
		goto err_vecalloc;

	ev_hdr->event = event;  /* store this event handle */
	ev_hdr->vector_type = EM_EVENT_TYPE_ANY;
	/* init common ev_hdr fields in the caller */

	return ev_hdr;

err_vecalloc:
	odp_event_vector_free(odp_evvec);
	return NULL;
}

static inline event_hdr_t *
event_alloc_vector(const mpool_elem_t *pool_elem, uint32_t size)
{
	if (em_shm->opt.vector.backend == EM_VECTOR_BACKEND_PACKET)
		return event_alloc_vector__odp_pktvec(pool_elem, size);
	else
		return event_alloc_vector__odp_evvec(pool_elem, size);
}

/*
 * Helper for event_alloc_vector_multi__odp_pktvec()
 */
static inline int
pktvec_alloc_multi(odp_packet_vector_t odp_pktvecs[/*out*/], int num,
		   odp_pool_t odp_pool)
{
	int i;

	for (i = 0; i < num; i++) {
		odp_pktvecs[i] = odp_packet_vector_alloc(odp_pool);
		if (unlikely(odp_pktvecs[i] == ODP_PACKET_VECTOR_INVALID))
			break;
	}

	const int num_vecs = i;

	if (unlikely(num_vecs == 0))
		return 0;

	/*
	 * Set the pkt vector user flag to be able to recognize vector-events
	 * that EM has created vs. vectors from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	for (i = 0; i < num_vecs; i++)
		odp_packet_vector_user_flag_set(odp_pktvecs[i], USER_FLAG_SET);

	return num_vecs;
}

/*
 * Helper for event_alloc_vector_multi__odp_evvec()
 */
static inline int
evvec_alloc_multi(odp_event_vector_t odp_evvecs[/*out*/], int num,
		  odp_pool_t odp_pool)
{
	int i;

	for (i = 0; i < num; i++) {
		odp_evvecs[i] = odp_event_vector_alloc(odp_pool);
		if (unlikely(odp_evvecs[i] == ODP_EVENT_VECTOR_INVALID))
			break;
	}

	const int num_vecs = i;

	if (unlikely(num_vecs == 0))
		return 0;

	/*
	 * Set the event vector user flag to be able to recognize vector-events
	 * that EM has created vs. vectors that need their ev-hdrs to be initialized.
	 */
	for (i = 0; i < num_vecs; i++)
		odp_event_vector_user_flag_set(odp_evvecs[i], USER_FLAG_SET);

	return num_vecs;
}

static inline int
event_alloc_vector_multi__odp_pktvec(em_event_t events[/*out*/], const int num,
				     const mpool_elem_t *pool_elem, uint32_t size,
				     em_event_type_t type)
{
	odp_packet_vector_t odp_pktvecs[num];
	/* use same output-array: odp_events[] = events[] */
	odp_event_t *const odp_events = (odp_event_t *)events;
	event_hdr_t *ev_hdrs[num];
	int subpool;
	const bool esv_ena = esv_enabled();

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return 0;

	int num_req = num;
	int num_vecs = 0;
	int i;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return 0;

		int ret = pktvec_alloc_multi(&odp_pktvecs[num_vecs], num_req, odp_pool);

		if (unlikely(ret <= 0))
			continue; /* try next subpool */

		/*
		 * Init 'ret' ev-hdrs from this 'subpool'=='odp-pool'.
		 * Note: odp_events[] points&writes into events[out]
		 */
		for (i = num_vecs; i < num_vecs + ret; i++) {
			odp_events[i] = odp_packet_vector_to_event(odp_pktvecs[i]);
			ev_hdrs[i] = odp_packet_vector_user_area(odp_pktvecs[i]);
		}

		/*
		 * Note: events[] == odp_events[] before ESV init.
		 * Don't touch odp_events[] during this loop-round anymore.
		 */
		if (esv_ena) {
			/* reads ev_hdrs[i]->flags if prealloc_pools used */
			evstate_alloc_multi(&events[num_vecs] /*in/out*/,
					    &ev_hdrs[num_vecs], ret);
		}

		for (i = num_vecs; i < num_vecs + ret; i++) {
			ev_hdrs[i]->flags.all = 0;
			/* ev_hdrs[i]->align_offset = needed by odp bufs only */
			ev_hdrs[i]->event_type = type;
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size; /* original vec size */
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

			ev_hdrs[i]->user_area.all = 0;
			ev_hdrs[i]->user_area.size = pool_elem->user_area.size;
			ev_hdrs[i]->user_area.isinit = 1;
			ev_hdrs[i]->vector_type = EM_EVENT_TYPE_PACKET;
		}

		num_vecs += ret;
		if (likely(num_vecs == num))
			break; /* all allocated */
		num_req -= ret;
	}

	return num_vecs; /* number of allocated pkts */
}

static inline int
event_alloc_vector_multi__odp_evvec(em_event_t events[/*out*/], const int num,
				    const mpool_elem_t *pool_elem, uint32_t size,
				    em_event_type_t type)
{
	odp_event_vector_t odp_evvecs[num];
	/* use same output-array: odp_events[] = events[] */
	odp_event_t *const odp_events = (odp_event_t *)events;
	event_hdr_t *ev_hdrs[num];
	int subpool;
	const bool esv_ena = esv_enabled();

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return 0;

	int num_req = num;
	int num_vecs = 0;
	int i;

	for (; subpool < pool_elem->num_subpools; subpool++) {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL >= 3 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return 0;

		int ret = evvec_alloc_multi(&odp_evvecs[num_vecs], num_req, odp_pool);

		if (unlikely(ret <= 0))
			continue; /* try next subpool */

		/*
		 * Init 'ret' ev-hdrs from this 'subpool'=='odp-pool'.
		 * Note: odp_events[] points&writes into events[out]
		 */
		for (i = num_vecs; i < num_vecs + ret; i++) {
			odp_events[i] = odp_event_vector_to_event(odp_evvecs[i]);
			ev_hdrs[i] = odp_event_vector_user_area(odp_evvecs[i]);
		}

		/*
		 * Note: events[] == odp_events[] before ESV init.
		 * Don't touch odp_events[] during this loop-round anymore.
		 */
		if (esv_ena) {
			/* reads ev_hdrs[i]->flags if prealloc_pools used */
			evstate_alloc_multi(&events[num_vecs] /*in/out*/,
					    &ev_hdrs[num_vecs], ret);
		}

		for (i = num_vecs; i < num_vecs + ret; i++) {
			ev_hdrs[i]->flags.all = 0;
			/* ev_hdrs[i]->align_offset = needed by odp bufs only */
			ev_hdrs[i]->event_type = type;
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size; /* original vec size */
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

			ev_hdrs[i]->user_area.all = 0;
			ev_hdrs[i]->user_area.size = pool_elem->user_area.size;
			ev_hdrs[i]->user_area.isinit = 1;
			ev_hdrs[i]->vector_type = EM_EVENT_TYPE_ANY;
		}

		num_vecs += ret;
		if (likely(num_vecs == num))
			break; /* all allocated */
		num_req -= ret;
	}

	return num_vecs; /* number of allocated events */
}

/**
 * Allocate & initialize multiple events based on odp-pkt-vectors.
 */
static inline int
event_alloc_vector_multi(em_event_t events[/*out*/], const int num,
			 const mpool_elem_t *pool_elem, uint32_t size,
			 em_event_type_t type)
{
	if (em_shm->opt.vector.backend == EM_VECTOR_BACKEND_PACKET)
		return event_alloc_vector_multi__odp_pktvec(events /*out*/, num,
							    pool_elem, size, type);
	else
		return event_alloc_vector_multi__odp_evvec(events /*out*/, num,
							   pool_elem, size, type);
}

/**
 * Helper for em_alloc() and em_event_clone()
 */
static inline em_event_t
event_alloc(const mpool_elem_t *pool_elem, uint32_t size, em_event_type_t type,
	    const uint16_t api_op)
{
	/*
	 * EM event pools created with type=PKT can support:
	 *   - SW events (bufs)
	 *   - pkt events.
	 *
	 * EM event pools created with type=SW can support:
	 *   - SW events (bufs) only
	 */
	event_hdr_t *ev_hdr = NULL;

	if (pool_elem->event_type == EM_EVENT_TYPE_PACKET)
		ev_hdr = event_alloc_pkt(pool_elem, size);
	else if (pool_elem->event_type == EM_EVENT_TYPE_SW)
		ev_hdr = event_alloc_buf(pool_elem, size);
	else if (pool_elem->event_type == EM_EVENT_TYPE_VECTOR)
		ev_hdr = event_alloc_vector(pool_elem, size);

	if (unlikely(!ev_hdr))
		return EM_EVENT_UNDEF;

	/*
	 * event now allocated:
	 * ev_hdr->event = stored by event_alloc_pkt/buf/vector()
	 */
	/* Update event ESV state for alloc */
	if (esv_enabled())
		(void)evstate_alloc(ev_hdr->event, ev_hdr, api_op);

	ev_hdr->flags.all = 0; /* clear only after evstate_alloc() */
	ev_hdr->event_type = type; /* store the event type */
	ev_hdr->event_size = size; /* store requested size */
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	ev_hdr->user_area.all = 0;
	ev_hdr->user_area.size = pool_elem->user_area.size;
	ev_hdr->user_area.isinit = 1;
	/* ev_hdr->align_offset = init by event_alloc_buf() when needed */

	return ev_hdr->event;
}

/**
 * Return a pointer to the EM event user payload.
 * Helper to e.g. EM API em_event_pointer()
 */
static inline void *
event_pointer(em_event_t event)
{
	const odp_event_t odp_event = event_em2odp(event);
	const odp_event_type_t odp_etype = odp_event_type(odp_event);
	void *ev_ptr = NULL; /* return value */

	if (odp_etype == ODP_EVENT_PACKET) {
		const odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

		ev_ptr = odp_packet_data(odp_pkt);
	} else if (odp_etype == ODP_EVENT_BUFFER) {
		const odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);
		const event_hdr_t *ev_hdr = odp_buffer_user_area(odp_buf);
		const uint32_t align_offset = ev_hdr->align_offset;

		ev_ptr = odp_buffer_addr(odp_buf);

		if (align_offset)
			ev_ptr = (void *)((uintptr_t)ev_ptr + 32 - align_offset);
	}

	return ev_ptr; /* NULL for unrecognized odp_etype, also for vectors */
}

static inline bool
event_has_ref(em_event_t event)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t odp_etype = odp_event_type(odp_event);

	if (odp_etype != ODP_EVENT_PACKET)
		return false;

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

	return odp_packet_has_ref(odp_pkt) ? true : false;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_INLINE_H_ */
