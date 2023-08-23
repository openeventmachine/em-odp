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

 /**
  * @file
  * EM internal event functions
  *
  */

#ifndef EM_EVENT_INLINE_H_
#define EM_EVENT_INLINE_H_

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
	/* Valid for both ESV enabled and disabled */
	evhdl_t *const evhdls = (evhdl_t *)events;

	/*
	 * Setting 'evhdls[i].event = odp_events[i]' is equal to
	 *         'evhdls[i].evptr = odp_events[i], evhdl[i].evgen = 0'
	 * (evhdls[i].evgen still needs to be set when using ESV)
	 */
	for (unsigned int i = 0; i < num; i++)
		evhdls[i].event = (em_event_t)(uintptr_t)odp_events[i];
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
events_em2pkt_inplace(em_event_t events[/*in*/], const unsigned int num)
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
	odp_packet_vector_t odp_pktvec = odp_packet_vector_from_event(odp_event);
	event_hdr_t *ev_hdr = odp_packet_vector_user_area(odp_pktvec);

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
	odp_timeout_t odp_tmo;
	odp_packet_vector_t odp_pktvec;
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
	case ODP_EVENT_PACKET_VECTOR:
		odp_pktvec = odp_packet_vector_from_event(odp_event);
		ev_hdr = odp_packet_vector_user_area(odp_pktvec);
		break;
	case ODP_EVENT_TIMEOUT:
		odp_tmo = odp_timeout_from_event(odp_event);
		ev_hdr = odp_timeout_user_area(odp_tmo);
		break;
	default:
		INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_IMPLEMENTED),
			       EM_ESCOPE_EVENT_TO_HDR,
			       "Unexpected odp event type:%u", evtype);
		/* avoids: "error: 'ev_hdr' may be used uninitialized" */
		__builtin_unreachable();
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

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_INLINE_H_ */
