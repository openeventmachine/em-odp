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

#ifndef EM_EVENT_H_
#define EM_EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif

COMPILE_TIME_ASSERT((uintptr_t)EM_EVENT_UNDEF == (uintptr_t)ODP_EVENT_INVALID,
		    EM_EVENT_NOT_EQUAL_TO_ODP_EVENT);

em_status_t event_init(void);
void print_event_info(void);
em_event_t pkt_clone_odp(odp_packet_t pkt, odp_pool_t pkt_pool);
void output_queue_track(queue_elem_t *const output_q_elem);
void output_queue_drain(const queue_elem_t *output_q_elem);
void output_queue_buffering_drain(void);

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
 * Initialize the event header of a packet allocated outside of EM.
 */
static inline em_event_t
evhdr_init_pkt(event_hdr_t *ev_hdr, em_event_t event,
	       odp_packet_t odp_pkt, bool is_extev)
{
	const void *user_ptr = odp_packet_user_ptr(odp_pkt);
	const bool esv_ena = esv_enabled();

	if (user_ptr == PKT_USERPTR_MAGIC_NBR) {
		/* Event already initialized by EM */
		if (esv_ena)
			return ev_hdr->event;
		else
			return event;
	}

	/*
	 * ODP pkt from outside of EM - not allocated by EM & needs init
	 */
	odp_packet_user_ptr_set(odp_pkt, PKT_USERPTR_MAGIC_NBR);
	ev_hdr->event_type = EM_EVENT_TYPE_PACKET;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	if (em_shm->opt.pool.statistics_enable)
		ev_hdr->pool = EM_POOL_UNDEF; /* ev_hdr->subpool = don't care */

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
		odp_pool_t odp_pool = odp_packet_pool(odp_pkt);
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
 * Initialize external ODP events that have been input into EM.
 *
 * Initialize the event header if needed, i.e. if event originated from outside
 * of EM from pktio or other input and was not allocated by EM via em_alloc().
 * The odp pkt-user-ptr is used to determine whether the header has been
 * initialized or not.
 */
static inline em_event_t
event_init_odp(odp_event_t odp_event, bool is_extev)
{
	odp_event_type_t odp_type;
	odp_packet_t odp_pkt;
	odp_buffer_t odp_buf;
	em_event_t event; /* return value */
	event_hdr_t *ev_hdr;

	odp_type = odp_event_type(odp_event);
	event = event_odp2em(odp_event);

	switch (odp_type) {
	case ODP_EVENT_PACKET:
		odp_pkt = odp_packet_from_event(odp_event);
		ev_hdr = odp_packet_user_area(odp_pkt);
		/* init event-hdr if needed (also ESV-state if used) */
		event = evhdr_init_pkt(ev_hdr, event, odp_pkt, is_extev);
		break;
	case ODP_EVENT_BUFFER:
		if (esv_enabled()) {
			/* update event handle (ESV) */
			odp_buf = odp_buffer_from_event(odp_event);
			ev_hdr = odp_buffer_addr(odp_buf);
			event = ev_hdr->event;
		}
		break;
	default:
		INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_IMPLEMENTED),
			       EM_ESCOPE_EVENT_INIT_ODP,
			       "Unexpected odp event type:%u", odp_type);
		break;
	}

	return event;
}

/* Helper to event_init_odp_multi() */
static inline void
event_init_pkt_multi(const odp_packet_t odp_pkts[/*in*/],
		     em_event_t events[/*in,out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_packet_user_area(odp_pkts[i]);

	for (int i = 0; i < num; i++) {
		/* init event-hdrs if needed (also ESV-state if used) */
		events[i] = evhdr_init_pkt(ev_hdrs[i], events[i],
					   odp_pkts[i], is_extev);
	}
}

/* Helper to event_init_odp_multi() */
static inline void
event_init_buf_multi(const odp_buffer_t odp_bufs[/*in*/],
		     em_event_t events[/*in,out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_buffer_addr(odp_bufs[i]);

	if (esv_enabled()) {
		/* update event handle (ESV) */
		for (int i = 0; i < num; i++)
			events[i] = ev_hdrs[i]->event;
	}
}

/**
 * Convert from EM events to event headers and initialize the headers as needed.
 *
 * Initialize the event header if needed, i.e. if event originated from outside
 * of EM from pktio or other input and was not allocated by EM via em_alloc().
 * The odp pkt-user-ptr is used to determine whether the header has been
 * initialized or not.
 */
static inline void
event_init_odp_multi(const odp_event_t odp_events[/*in*/],
		     em_event_t events[/*out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev)
{
	odp_event_type_t odp_type;
	int ev = 0; /* event & ev_hdr tbl index*/

	events_odp2em(odp_events, events/*out*/, num);

	do {
		int num_type = odp_event_type_multi(&odp_events[ev], num - ev,
						    &odp_type /*out*/);
		if (likely(odp_type == ODP_EVENT_PACKET)) {
			odp_packet_t odp_pkts[num];

			odp_packet_from_event_multi(odp_pkts /*out*/,
						    &odp_events[ev],
						    num_type);
			event_init_pkt_multi(odp_pkts /*in*/,
					     &events[ev] /*in,out*/,
					     &ev_hdrs[ev] /*out*/,
					     num_type, is_extev);
		} else if (likely(odp_type == ODP_EVENT_BUFFER)) {
			odp_buffer_t odp_bufs[num];

			for (int i = 0; i < num_type; i++)
				odp_bufs[i] = odp_buffer_from_event(odp_events[ev + i]);

			event_init_buf_multi(odp_bufs /*in*/,
					     &events[ev] /*in,out*/,
					     &ev_hdrs[ev] /*out*/,
					     num_type);
		} else {
			INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_IMPLEMENTED),
				       EM_ESCOPE_EVENT_INIT_ODP_MULTI,
				       "Unexpected odp event type:%u (%d events)",
				       odp_type, num_type);
			__builtin_unreachable();
		}

		ev += num_type;
	} while (ev < num);
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
	event_hdr_t *ev_hdr;

	odp_event_type_t evtype = odp_event_type(odp_event);

	switch (evtype) {
	case ODP_EVENT_PACKET:
		odp_pkt = odp_packet_from_event(odp_event);
		ev_hdr = odp_packet_user_area(odp_pkt);
		break;
	case ODP_EVENT_BUFFER:
		odp_buf = odp_buffer_from_event(odp_event);
		ev_hdr = odp_buffer_addr(odp_buf);
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
	odp_event_t odp_events[num];
	odp_packet_t odp_pkts[num];
	odp_buffer_t odp_buf;
	odp_event_type_t evtype;
	int num_type;
	int ev = 0; /* event & ev_hdr tbl index*/
	int i;

	events_em2odp(events, odp_events/*out*/, num);

	do {
		num_type =
		odp_event_type_multi(&odp_events[ev], num - ev, &evtype/*out*/);

		switch (evtype) {
		case ODP_EVENT_PACKET:
			odp_packet_from_event_multi(odp_pkts, &odp_events[ev],
						    num_type);
			for (i = 0; i < num_type; i++) {
				ev_hdrs[ev + i] =
					odp_packet_user_area(odp_pkts[i]);
			}
			break;

		case ODP_EVENT_BUFFER:
			for (i = 0; i < num_type; i++) {
				odp_buf =
				odp_buffer_from_event(odp_events[ev + i]);
				ev_hdrs[ev + i] = odp_buffer_addr(odp_buf);
			}
			break;
		default:
			INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_IMPLEMENTED),
				       EM_ESCOPE_EVENT_TO_HDR_MULTI,
				       "Unexpected odp event type:%u", evtype);
			/* unreachable */
			__builtin_unreachable();
			break;
		}

		ev += num_type;
	} while (ev < num);
}

/** Convert from event header to EM event */
static inline em_event_t
event_hdr_to_event(const event_hdr_t *const event_hdr)
{
	return event_hdr->event;
}

/**
 * Allocate & initialize an event based on an odp-buf.
 */
static inline event_hdr_t *
event_alloc_buf(const mpool_elem_t *const pool_elem,
		size_t size, em_event_type_t type)
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

		if (EM_CHECK_LEVEL > 1 &&
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
	 * at the beginning of the buffer.
	 */
	event_hdr_t *const ev_hdr = odp_buffer_addr(odp_buf);
	odp_event_t odp_event = odp_buffer_to_event(odp_buf);
	em_event_t event = event_odp2em(odp_event);

	ev_hdr->event = event;  /* store this event handle */
	/* For optimization, no initialization for feature variables */
	ev_hdr->event_size = size; /* store requested size */
	ev_hdr->align_offset = pool_elem->align_offset;
	ev_hdr->event_type = type; /* store the event type */
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	/* Pool usage statistics */
	if (em_shm->opt.pool.statistics_enable) {
		em_pool_t pool = pool_elem->em_pool;

		ev_hdr->subpool = subpool;
		ev_hdr->pool = pool;

		poolstat_inc(pool, subpool, 1);
	}

	return ev_hdr;
}

/**
 * Allocate & initialize multiple events based on odp-bufs.
 */
static inline int
event_alloc_buf_multi(em_event_t events[/*out*/], const int num,
		      const mpool_elem_t *pool_elem, size_t size,
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

		if (EM_CHECK_LEVEL > 1 &&
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
			ev_hdrs[i] = odp_buffer_addr(odp_bufs[i]);

		if (esv_ena)
			evstate_alloc_multi(&events[num_bufs] /*in/out*/,
					    &ev_hdrs[num_bufs], ret);

		for (i = num_bufs; i < num_bufs + ret; i++) {
			/* For optimization, no init for feature vars */
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size;
			ev_hdrs[i]->align_offset = pool_elem->align_offset;
			ev_hdrs[i]->event_type = type;
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;
			/* Pool usage statistics */
			if (em_shm->opt.pool.statistics_enable) {
				ev_hdrs[i]->subpool = subpool;
				ev_hdrs[i]->pool = pool_elem->em_pool;
			}
		}

		if (em_shm->opt.pool.statistics_enable)
			poolstat_inc(pool_elem->em_pool, subpool, ret);

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
event_alloc_pkt(const mpool_elem_t *pool_elem,
		size_t size, em_event_type_t type)
{
	const uint32_t push_len = pool_elem->align_offset;
	uint32_t pull_len;
	size_t alloc_size;
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

		if (EM_CHECK_LEVEL > 1 &&
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
		if (unlikely(!ptr))
			goto err_pktalloc;
	}
	if (pull_len) {
		ptr = odp_packet_pull_tail(odp_pkt, pull_len);
		if (unlikely(!ptr))
			goto err_pktalloc;
	}

	/*
	 * Set the pkt user ptr to be able to recognize pkt-events that
	 * EM has created vs pkts from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	odp_packet_user_ptr_set(odp_pkt, PKT_USERPTR_MAGIC_NBR);

	event_hdr_t *const ev_hdr = odp_packet_user_area(odp_pkt);
	odp_event_t odp_event = odp_packet_to_event(odp_pkt);
	em_event_t event = event_odp2em(odp_event);

	if (unlikely(ev_hdr == NULL))
		goto err_pktalloc;
	ev_hdr->event = event;  /* store this event handle */
	ev_hdr->event_size = size; /* store requested size */
	/* ev_hdr->align_offset = needed by odp bufs only */
	ev_hdr->event_type = type; /* store the event type */
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	/* Pool usage statistics */
	if (em_shm->opt.pool.statistics_enable) {
		em_pool_t pool = pool_elem->em_pool;

		ev_hdr->subpool = subpool;
		ev_hdr->pool = pool;

		poolstat_inc(pool, subpool, 1);
	}

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
	       odp_pool_t odp_pool, size_t size,
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
			if (unlikely(!ptr))
				goto err_pktalloc_multi;
		}
	}
	if (pull_len) {
		for (i = 0; i < num_pkts; i++) {
			ptr = odp_packet_pull_tail(odp_pkts[i], pull_len);
			if (unlikely(!ptr))
				goto err_pktalloc_multi; /* only before esv */
		}
	}

	/*
	 * Set the pkt user ptr to be able to recognize pkt-events that
	 * EM has created vs pkts from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	for (i = 0; i < num_pkts; i++)
		odp_packet_user_ptr_set(odp_pkts[i], PKT_USERPTR_MAGIC_NBR);

	return num_pkts;

err_pktalloc_multi:
	odp_packet_free_multi(odp_pkts, num_pkts);
	return 0;
}

/**
 * Allocate & initialize multiple events based on odp-pkts.
 */
static inline int
event_alloc_pkt_multi(em_event_t events[/*out*/], const int num,
		      const mpool_elem_t *pool_elem, size_t size,
		      em_event_type_t type)
{
	const uint32_t push_len = pool_elem->align_offset;
	uint32_t pull_len;
	odp_packet_t odp_pkts[num];
	/* use same output-array: odp_events[] = events[] */
	odp_event_t *const odp_events = (odp_event_t *)events;
	event_hdr_t *ev_hdrs[num];
	size_t alloc_size;
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

		if (EM_CHECK_LEVEL > 1 &&
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
		if (esv_ena)
			evstate_alloc_multi(&events[num_pkts] /*in/out*/,
					    &ev_hdrs[num_pkts], ret);

		for (i = num_pkts; i < num_pkts + ret; i++) {
			/* For optimization, no init for feature vars */
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size;
			/* ev_hdr->align_offset = needed by odp bufs only */
			ev_hdrs[i]->event_type = type;
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;
			/* Pool usage statistics */
			if (em_shm->opt.pool.statistics_enable) {
				ev_hdrs[i]->subpool = subpool;
				ev_hdrs[i]->pool = pool_elem->em_pool;
			}
		}
		if (em_shm->opt.pool.statistics_enable)
			poolstat_inc(pool_elem->em_pool, subpool, ret);

		num_pkts += ret;
		if (likely(num_pkts == num))
			break; /* all allocated */
		num_req -= ret;
	}

	return num_pkts; /* number of allocated pkts */
}

/**
 * Helper for em_alloc() and em_event_clone()
 */
static inline event_hdr_t *
event_alloc(const mpool_elem_t *pool_elem, size_t size, em_event_type_t type)
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
		ev_hdr = event_alloc_pkt(pool_elem, size, type);
	else if (pool_elem->event_type == EM_EVENT_TYPE_SW)
		ev_hdr = event_alloc_buf(pool_elem, size, type);

	/* event now allocated (if !NULL): ev_hdr->event */

	/*
	 * ESV state update for the event still needs to be done by the caller,
	 * not done here since there are different callers of this function.
	 *      if (esv_enabled())
	 *             event = evstate_alloc/clone/...(event, ev_hdr);
	 */

	return ev_hdr; /* can be NULL */
}

/**
 * Start-up helper for pool preallocation
 */
static inline em_event_t
event_prealloc(const mpool_elem_t *pool_elem, size_t size, em_event_type_t type)
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
		ev_hdr = event_alloc_pkt(pool_elem, size, type);
	else if (pool_elem->event_type == EM_EVENT_TYPE_SW)
		ev_hdr = event_alloc_buf(pool_elem, size, type);

	if (unlikely(ev_hdr == NULL))
		return EM_EVENT_UNDEF;

	/* event now allocated */
	em_event_t event = ev_hdr->event;

	if (esv_enabled())
		event = evstate_prealloc(event, ev_hdr);

	return event;
}

static inline event_hdr_t *
start_node_to_event_hdr(list_node_t *const list_node)
{
	event_hdr_t *const ev_hdr = (event_hdr_t *)(uintptr_t)
		((uint8_t *)list_node - offsetof(event_hdr_t, start_node));

	return likely(list_node != NULL) ? ev_hdr : NULL;
}

static inline em_status_t
send_event(em_event_t event, const queue_elem_t *q_elem)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 &&
		     (odp_event == ODP_EVENT_INVALID ||
		      odp_queue == ODP_QUEUE_INVALID)))
		return EM_ERR_NOT_FOUND;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY)) {
		return EM_ERR_BAD_STATE;
	}

	ret = odp_queue_enq(odp_queue, odp_event);
	if (unlikely(EM_CHECK_LEVEL > 0 && ret != 0))
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

static inline int
send_event_multi(const em_event_t events[], const int num,
		 const queue_elem_t *q_elem)
{
	odp_event_t odp_events[num];
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 && odp_queue == ODP_QUEUE_INVALID))
		return 0;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY)) {
		return 0;
	}

	events_em2odp(events, odp_events/*out*/, num);

	ret = odp_queue_enq_multi(odp_queue, odp_events, num);
	if (unlikely(ret < 0))
		return 0;

	return ret;
}

static inline em_status_t
send_local(em_event_t event, event_hdr_t *const ev_hdr,
	   queue_elem_t *const q_elem)
{
	em_locm_t *const locm = &em_locm;
	const em_queue_prio_t prio = q_elem->priority;
	odp_event_t odp_event = event_em2odp(event);
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY))
		return EM_ERR_BAD_STATE;

	ev_hdr->q_elem = q_elem;

	ret = odp_queue_enq(locm->local_queues.prio[prio].queue, odp_event);
	if (likely(ret == 0)) {
		locm->local_queues.empty = 0;
		locm->local_queues.prio[prio].empty_prio = 0;
		return EM_OK;
	}

	return EM_ERR_LIB_FAILED;
}

static inline int
send_local_multi(const em_event_t events[], event_hdr_t *const ev_hdrs[],
		 const int num, queue_elem_t *const q_elem)
{
	em_locm_t *const locm = &em_locm;
	const em_queue_prio_t prio = q_elem->priority;
	odp_event_t odp_events[num];
	int enq;
	int i;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY))
		return 0;

	for (i = 0; i < num; i++)
		ev_hdrs[i]->q_elem = q_elem;

	events_em2odp(events, odp_events, num);

	enq = odp_queue_enq_multi(locm->local_queues.prio[prio].queue,
				  odp_events, num);
	if (likely(enq > 0)) {
		locm->local_queues.empty = 0;
		locm->local_queues.prio[prio].empty_prio = 0;
		return enq;
	}

	return 0;
}

/**
 * Send one event to a queue of type EM_QUEUE_TYPE_OUTPUT
 */
static inline em_status_t
send_output(em_event_t event, event_hdr_t *const ev_hdr,
	    queue_elem_t *const output_q_elem)
{
	const em_sched_context_type_t sched_ctx_type =
		em_locm.current.sched_context_type;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     output_q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return EM_ERR_BAD_STATE;

	/*
	 * An event sent to an output queue from an ordered context needs to
	 * be 're-ordered' before calling the user provided output-function.
	 * Order is maintained by enqueuing and dequeuing into an odp-queue
	 * that takes care of order.
	 */
	if (sched_ctx_type == EM_SCHED_CONTEXT_TYPE_ORDERED) {
		const odp_queue_t odp_queue = output_q_elem->odp_queue;
		odp_event_t odp_event = event_em2odp(event);
		int ret;

		if (unlikely(EM_CHECK_LEVEL > 1 &&
			     (odp_event == ODP_EVENT_INVALID ||
			      odp_queue == ODP_QUEUE_INVALID)))
			return EM_ERR_NOT_FOUND;

		if (!EM_OUTPUT_QUEUE_IMMEDIATE)
			output_queue_track(output_q_elem);

		/* enqueue to enforce odp to handle ordering */
		ret = odp_queue_enq(odp_queue, odp_event);
		if (unlikely(ret != 0))
			return EM_ERR_LIB_FAILED;

		/* return value must be EM_OK after this since event enqueued */

		if (EM_OUTPUT_QUEUE_IMMEDIATE) {
			env_spinlock_t *const lock =
				&output_q_elem->output.lock;

			if (!env_spinlock_trylock(lock))
				return EM_OK;
			output_queue_drain(output_q_elem);
			env_spinlock_unlock(lock);
		}

		return EM_OK;
	}

	/*
	 * No ordered context - call output_fn() directly
	 */
	const em_queue_t output_queue = output_q_elem->queue;
	const em_output_func_t output_fn =
		output_q_elem->output.output_conf.output_fn;
	void *const output_fn_args =
		output_q_elem->output.output_conf.output_fn_args;
	int sent;

	/* decrement pool statistics before passing event out-of-EM */
	if (em_shm->opt.pool.statistics_enable)
		poolstat_dec_evhdr_output(ev_hdr);

	if (!esv_enabled()) {
		sent = output_fn(&event, 1, output_queue, output_fn_args);
		if (unlikely(sent != 1))
			return EM_ERR_OPERATION_FAILED;
		return EM_OK;
	}

	/*
	 * ESV enabled:
	 */
	event = evstate_em2usr(event, ev_hdr, EVSTATE__OUTPUT);
	sent = output_fn(&event, 1, output_queue, output_fn_args);
	if (likely(sent == 1))
		return EM_OK; /* output success! */

	/* revert event-state on output-error */
	event = evstate_em2usr_revert(event, ev_hdr, EVSTATE__OUTPUT__FAIL);

	return EM_ERR_OPERATION_FAILED;
}

/**
 * Send events to a queue of type EM_QUEUE_TYPE_OUTPUT
 */
static inline int
send_output_multi(const em_event_t events[], event_hdr_t *const ev_hdrs[],
		  const unsigned int num, queue_elem_t *const output_q_elem)
{
	const em_sched_context_type_t sched_ctx_type =
		em_locm.current.sched_context_type;
	int sent;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     output_q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return 0;

	/*
	 * Event sent to an output queue from an ordered context needs to
	 * be 're-ordered' before calling the user provided output-function.
	 * Order is maintained by enqueuing and dequeuing into an odp-queue
	 * that takes care of order.
	 */
	if (sched_ctx_type == EM_SCHED_CONTEXT_TYPE_ORDERED) {
		const odp_queue_t odp_queue = output_q_elem->odp_queue;
		odp_event_t odp_events[num];

		if (unlikely(EM_CHECK_LEVEL > 1 &&
			     odp_queue == ODP_QUEUE_INVALID))
			return 0;

		if (!EM_OUTPUT_QUEUE_IMMEDIATE)
			output_queue_track(output_q_elem);

		events_em2odp(events, odp_events/*out*/, num);

		/* enqueue to enforce odp to handle ordering */
		sent = odp_queue_enq_multi(odp_queue, odp_events, num);
		if (unlikely(sent <= 0))
			return 0;

		/* the return value must be the number of enqueued events */

		if (EM_OUTPUT_QUEUE_IMMEDIATE) {
			env_spinlock_t *const lock =
				&output_q_elem->output.lock;

			if (!env_spinlock_trylock(lock))
				return sent;
			output_queue_drain(output_q_elem);
			env_spinlock_unlock(lock);
		}

		return sent;
	}

	/*
	 * No ordered context - call output_fn() directly
	 */
	const em_queue_t output_queue = output_q_elem->queue;
	const em_output_func_t output_fn = output_q_elem->output.output_conf.output_fn;
	void *const output_fn_args = output_q_elem->output.output_conf.output_fn_args;

	/* decrement pool statistics before passing event out-of-EM */
	if (em_shm->opt.pool.statistics_enable)
		poolstat_dec_evhdr_multi_output(ev_hdrs, num);

	if (!esv_enabled())
		return output_fn(events, num, output_queue, output_fn_args);

	/*
	 * ESV enabled:
	 */
	em_event_t tmp_events[num];

	/* need copy, don't change "const events[]" */
	for (unsigned int i = 0; i < num; i++)
		tmp_events[i] = events[i];
	evstate_em2usr_multi(tmp_events/*in/out*/, ev_hdrs, num,
			     EVSTATE__OUTPUT_MULTI);
	sent = output_fn(tmp_events, num, output_queue, output_fn_args);

	if (unlikely(sent < (int)num && sent >= 0))
		evstate_em2usr_revert_multi(&tmp_events[sent]/*in/out*/,
					    &ev_hdrs[sent], num - sent,
					    EVSTATE__OUTPUT_MULTI__FAIL);
	return sent;
}

/**
 * Return a pointer to the EM event user payload.
 * Helper to e.g. EM API em_event_pointer()
 */
static inline void *
event_pointer(em_event_t event)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t odp_etype = odp_event_type(odp_event);
	void *ev_ptr = NULL; /* return value */

	if (odp_etype == ODP_EVENT_PACKET) {
		odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

		ev_ptr = odp_packet_data(odp_pkt);
	} else if (odp_etype == ODP_EVENT_BUFFER) {
		odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);
		const event_hdr_t *ev_hdr = odp_buffer_addr(odp_buf);

		ev_ptr = (void *)((uintptr_t)ev_hdr + sizeof(event_hdr_t)
				  - ev_hdr->align_offset);
	}

	return ev_ptr; /* NULL for unrecognized odp_etype */
}

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_H_ */
