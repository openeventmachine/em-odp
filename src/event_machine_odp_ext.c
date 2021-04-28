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
  * Event Machine ODP API extensions
  *
  */

#include <em_include.h>
#include <event_machine/platform/event_machine_odp_ext.h>

odp_queue_t
em_odp_queue_odp(const em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (unlikely(queue_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_ODP_EXT,
			       "queue_elem ptr NULL!");
		return ODP_QUEUE_INVALID;
	}

	return queue_elem->odp_queue;
}

em_queue_t
em_odp_queue_em(const odp_queue_t queue)
{
	const queue_elem_t *queue_elem = odp_queue_context(queue);

	if (unlikely(queue_elem == NULL))
		return EM_QUEUE_UNDEF;

	return queue_elem->queue;
}

uint32_t
em_odp_event_hdr_size(void)
{
	return sizeof(event_hdr_t);
}

odp_event_t
em_odp_event2odp(em_event_t event)
{
	return event_em2odp(event);
}

void
em_odp_events2odp(const em_event_t events[], odp_event_t odp_events[/*out*/],
		  const int num)
{
	if (unlikely(num <= 0))
		return;

	events_em2odp(events, odp_events/*out*/, num);
}

em_event_t em_odp_event2em(odp_event_t odp_event)
{
	em_event_t event = event_init_odp(odp_event, false/*!is_extev*/);

	return event;
}

void
em_odp_events2em(const odp_event_t odp_events[], em_event_t events[/*out*/],
		 const int num)
{
	if (unlikely(num <= 0))
		return;

	event_hdr_t *ev_hdrs[num];

	event_init_odp_multi(odp_events, events/*out*/, ev_hdrs/*out*/, num,
			     false/*!is_extev*/);
}

int pkt_enqueue(const odp_packet_t pkt_tbl[], const int num,
		const em_queue_t queue)
{
	if (unlikely(!pkt_tbl || num <= 0))
		return 0;

	queue_elem_t *const q_elem = queue_elem_get(queue);
	odp_event_t odp_event_tbl[num];
	int sent = 0;

	odp_packet_to_event_multi(pkt_tbl, odp_event_tbl/*out*/, num);

	if (q_elem != NULL && q_elem->scheduled) {
		/*
		 * Enqueue the events into a scheduled em-odp queue.
		 * No need to init the ev-hdrs - init is done in dispatch.
		 */
		sent = odp_queue_enq_multi(q_elem->odp_queue,
					   odp_event_tbl, num);
	} else {
		em_event_t event_tbl[num];
		event_hdr_t *evhdr_tbl[num];

		events_odp2em(odp_event_tbl, event_tbl/*out*/, num);

		/* Init the event-hdrs for incoming pkts */
		pkt_evhdr_init_multi(pkt_tbl, event_tbl/*in/out*/,
				     evhdr_tbl/*out*/, num);

		if (q_elem == NULL) {
			/* Send directly out via event chaining */
			if (likely(queue_external(queue)))
				sent = send_chaining_multi(event_tbl, evhdr_tbl,
							   num, queue);
		} else if (q_elem->type == EM_QUEUE_TYPE_UNSCHEDULED) {
			/* Enqueue into an unscheduled em-odp queue */
			sent = odp_queue_enq_multi(q_elem->odp_queue,
						   odp_event_tbl, num);
		} else if (q_elem->type ==  EM_QUEUE_TYPE_LOCAL) {
			/* Send into an local em-odp queue */
			sent = send_local_multi(event_tbl, evhdr_tbl,
						num, q_elem);
		} else if (q_elem->type ==  EM_QUEUE_TYPE_OUTPUT) {
			/* Send directly out via an output em-odp queue */
			sent = send_output_multi(event_tbl, evhdr_tbl,
						 num, q_elem);
		}
	}

	if (unlikely(sent < num)) {
		sent = unlikely(sent < 0) ? 0 : sent;
		odp_packet_free_multi(&pkt_tbl[sent], num - sent);
		/*
		 * Event state checking: No need to adjust the event state
		 * since the events were never enqueued into EM.
		 */
	}

	return sent;
}
