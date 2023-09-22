/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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

#ifndef __clang__
COMPILE_TIME_ASSERT((uintptr_t)EM_EVENT_UNDEF == (uintptr_t)ODP_EVENT_INVALID,
		    EM_EVENT_NOT_EQUAL_TO_ODP_EVENT);
COMPILE_TIME_ASSERT(EM_TMO_TYPE_NONE == 0,
		    "EM_TMO_TYPE_NONE must be 0");
#endif

em_status_t event_init(void);
void print_event_info(void);
em_event_t pkt_clone_odp(odp_packet_t pkt, odp_pool_t pkt_pool,
			 uint32_t offset, uint32_t size, bool is_clone_part);
void output_queue_track(queue_elem_t *const output_q_elem);
void output_queue_drain(const queue_elem_t *output_q_elem);
void output_queue_buffering_drain(void);

uint32_t event_vector_tbl(em_event_t vector_event, em_event_t **event_tbl/*out*/);
em_status_t event_vector_max_size(em_event_t vector_event, uint32_t *max_size /*out*/,
				  em_escope_t escope);

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
	ev_hdr->flags.all = 0;

	return event;
}

/**
 * Initialize the event headers of packets allocated outside of EM.
 */
static inline void
evhdr_init_pkt_multi(event_hdr_t *const ev_hdrs[],
		     em_event_t events[/*in,out*/],
		     const odp_packet_t odp_pkts[/*in*/],
		     const int num, bool is_extev)
{
	const bool esv_ena = esv_enabled();
	int user_flag_set;

	int needs_init_idx[num];
	int needs_init_num = 0;
	int idx;

	for (int i = 0; i < num; i++) {
		user_flag_set = odp_packet_user_flag(odp_pkts[i]);
		if (user_flag_set) {
			/* Event already initialized by EM */
			if (esv_ena) {
				events[i] = ev_hdrs[i]->event;
				if (is_extev)
					events[i] = evstate_em2usr(events[i], ev_hdrs[i],
								   EVSTATE__DISPATCH_MULTI);
			}
			/* else events[i] = events[i] */
		} else {
			odp_packet_user_flag_set(odp_pkts[i], USER_FLAG_SET);
			needs_init_idx[needs_init_num] = i;
			needs_init_num++;
		}
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
		ev_hdrs[idx]->flags.all = 0;
		ev_hdrs[idx]->event_type = EM_EVENT_TYPE_PACKET;
		ev_hdrs[idx]->egrp = EM_EVENT_GROUP_UNDEF;
		ev_hdrs[idx]->user_area.all = 0; /* uarea fields init when used */
	}
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
	 * ODP pkt from outside of EM - not allocated by EM & needs init
	 */
	odp_packet_vector_user_flag_set(odp_pktvec, USER_FLAG_SET);
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
			if (is_extev)
				return event;
		}
	}

	return event;
}

/**
 * Initialize the event headers of packet vectors allocated outside of EM.
 */
static inline void
evhdr_init_pktvec_multi(event_hdr_t *ev_hdrs[/*out*/],
			em_event_t events[/*in,out*/],
			const odp_packet_vector_t odp_pktvecs[/*in*/],
			const int num, bool is_extev)
{
	const bool esv_ena = esv_enabled();

	int needs_init_idx[num];
	int needs_init_num = 0;
	int idx;

	for (int i = 0; i < num; i++) {
		int user_flag = odp_packet_vector_user_flag(odp_pktvecs[i]);

		if (user_flag == USER_FLAG_SET) {
			/* Event already initialized by EM */
			if (esv_ena) {
				events[i] = ev_hdrs[i]->event;
				if (is_extev)
					events[i] = evstate_em2usr(events[i], ev_hdrs[i],
								   EVSTATE__DISPATCH_MULTI);
			}
			/* else events[i] = events[i] */
		} else {
			needs_init_idx[needs_init_num] = i;
			needs_init_num++;
		}
	}

	if (needs_init_num == 0)
		return;

	/*
	 * ODP pkt vector from outside of EM - not allocated by EM & needs init
	 */

	if (!esv_ena) {
		for (int i = 0; i < needs_init_num; i++) {
			idx = needs_init_idx[i];
			odp_packet_vector_user_flag_set(odp_pktvecs[idx], USER_FLAG_SET);
			ev_hdrs[idx]->user_area.all = 0; /* uarea fields init when used */
			ev_hdrs[idx]->event_type = EM_EVENT_TYPE_VECTOR;
			ev_hdrs[idx]->egrp = EM_EVENT_GROUP_UNDEF;
			ev_hdrs[idx]->event = events[idx];
		}

		return;
	}

	/*
	 * ESV enabled:
	 */
	for (int i = 0; i < needs_init_num; i++) {
		idx = needs_init_idx[i];
		odp_packet_vector_user_flag_set(odp_pktvecs[idx], USER_FLAG_SET);
		ev_hdrs[idx]->user_area.all = 0; /* uarea fields init when used */
		ev_hdrs[idx]->event_type = EM_EVENT_TYPE_VECTOR;
		ev_hdrs[idx]->egrp = EM_EVENT_GROUP_UNDEF;
	}

	if (!em_shm->opt.esv.prealloc_pools) {
		for (int i = 0; i < needs_init_num; i++) {
			idx = needs_init_idx[i];
			events[idx] = evstate_init(events[idx], ev_hdrs[idx], is_extev);
		}

		return;
	}

	/*
	 * em_shm->opt.esv.prealloc_pools == true
	 */
	for (int i = 0; i < needs_init_num; i++) {
		idx = needs_init_idx[i];

		odp_pool_t odp_pool = odp_packet_vector_pool(odp_pktvecs[idx]);
		em_pool_t pool = pool_odp2em(odp_pool);

		if (pool == EM_POOL_UNDEF) {
			/* External odp pkt originates from an ODP-pool */
			events[idx] = evstate_init(events[idx], ev_hdrs[idx], is_extev);
		} else {
			/* External odp pkt originates from an EM-pool */
			events[idx] = evstate_update(events[idx], ev_hdrs[idx], is_extev);
		}
	}
}

/**
 * Initialize an external ODP event that have been input into EM.
 *
 * Initialize the event header if needed, i.e. if event originated from outside
 * of EM from pktio or other input and was not allocated by EM via em_alloc().
 * The odp pkt-user-ptr is used to determine whether the header has been
 * initialized or not.
 */
static inline em_event_t
event_init_odp(odp_event_t odp_event, bool is_extev, event_hdr_t **ev_hdr__out)
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

		if (esv_ena) { /* update event handle (ESV) */
			event = ev_hdr->event;
			if (is_extev)
				event = evstate_em2usr(event, ev_hdr, EVSTATE__DISPATCH);
		}
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
		INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_IMPLEMENTED),
			       EM_ESCOPE_EVENT_INIT_ODP,
			       "Unexpected odp event type:%u", odp_type);
		__builtin_unreachable();
		/* never reached */
		return EM_EVENT_UNDEF;
	}
}

/* Helper to event_init_odp_multi() */
static inline void
event_init_pkt_multi(const odp_packet_t odp_pkts[/*in*/],
		     em_event_t events[/*in,out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_packet_user_area(odp_pkts[i]);

	evhdr_init_pkt_multi(ev_hdrs, events, odp_pkts, num, is_extev);
}

/* Helper to event_init_odp_multi() */
static inline void
event_init_buf_multi(const odp_buffer_t odp_bufs[/*in*/],
		     em_event_t events[/*in,out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num, bool is_extev)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_buffer_user_area(odp_bufs[i]);

	if (esv_enabled()) {
		/* update event handle (ESV) */
		for (int i = 0; i < num; i++)
			events[i] = ev_hdrs[i]->event;

		if (is_extev)
			evstate_em2usr_multi(events, ev_hdrs, num,
					     EVSTATE__DISPATCH_MULTI);
	}
}

/* Helper to event_init_odp_multi() */
static inline void
event_init_tmo_multi(const odp_timeout_t odp_tmos[/*in*/],
		     em_event_t events[/*in,out*/], event_hdr_t *ev_hdrs[/*out*/],
		     const int num)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_timeout_user_area(odp_tmos[i]);

	/* ignore ESV */
	(void)events;
}

/* Helper to event_init_odp_multi() */
static inline void
event_init_pktvec_multi(const odp_packet_vector_t odp_pktvecs[/*in*/],
			em_event_t events[/*in,out*/], event_hdr_t *ev_hdrs[/*out*/],
			const int num, bool is_extev)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i] = odp_packet_vector_user_area(odp_pktvecs[i]);

	evhdr_init_pktvec_multi(ev_hdrs, events, odp_pktvecs, num, is_extev);
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
	for (int i = 0; i < num; i++)
		events[i] = event_init_odp(odp_events[i], is_extev, &ev_hdrs[i]);
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
	 * at the beginning of the buffer.
	 */
	event_hdr_t *const ev_hdr = odp_buffer_user_area(odp_buf);
	odp_event_t odp_event = odp_buffer_to_event(odp_buf);
	em_event_t event = event_odp2em(odp_event);

	ev_hdr->event = event;  /* store this event handle */
	ev_hdr->align_offset = pool_elem->align_offset;

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

			ev_hdrs[i]->align_offset = pool_elem->align_offset;
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
	odp_packet_free_multi(odp_pkts, num_pkts);
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
			ev_hdrs[i]->event_size = size;
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
event_alloc_vector(const mpool_elem_t *pool_elem, uint32_t size)
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

	/* init common ev_hdr fields in the caller */

	return ev_hdr;

err_vecalloc:
	odp_packet_vector_free(odp_pktvec);
	return NULL;
}

/*
 * Helper for event_alloc_vec_multi()
 */
static inline int
vecalloc_multi(odp_packet_vector_t odp_pktvecs[/*out*/], int num,
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
	 * Set the pkt vector user ptr to be able to recognize vector-events
	 * that EM has created vs vectors from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	for (i = 0; i < num_vecs; i++)
		odp_packet_vector_user_flag_set(odp_pktvecs[i], USER_FLAG_SET);

	return num_vecs;
}

/**
 * Allocate & initialize multiple events based on odp-pkt-vectors.
 */
static inline int
event_alloc_vector_multi(em_event_t events[/*out*/], const int num,
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

		int ret = vecalloc_multi(&odp_pktvecs[num_vecs], num_req,
					 odp_pool);
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
			ev_hdrs[i]->event_type = type;
			if (!esv_ena)
				ev_hdrs[i]->event = events[i];
			ev_hdrs[i]->event_size = size;
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

			ev_hdrs[i]->user_area.all = 0;
			ev_hdrs[i]->user_area.size = pool_elem->user_area.size;
			ev_hdrs[i]->user_area.isinit = 1;
			/*ev_hdrs[i]->align_offset = needed by odp bufs only*/
		}

		num_vecs += ret;
		if (likely(num_vecs == num))
			break; /* all allocated */
		num_req -= ret;
	}

	return num_vecs; /* number of allocated pkts */
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
 * Start-up helper for pool preallocation
 */
static inline event_prealloc_hdr_t *
event_prealloc(const mpool_elem_t *pool_elem, uint32_t size)
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

	if (unlikely(ev_hdr == NULL))
		return NULL;

	/* event now allocated */

	if (esv_enabled()) {
		em_event_t event = ev_hdr->event;

		(void)evstate_prealloc(event, ev_hdr);
	}

	event_prealloc_hdr_t *prealloc_hdr = (event_prealloc_hdr_t *)ev_hdr;

	return prealloc_hdr;
}

static inline event_prealloc_hdr_t *
list_node_to_prealloc_hdr(list_node_t *const list_node)
{
	event_prealloc_hdr_t *const ev_hdr = (event_prealloc_hdr_t *)(uintptr_t)
		((uint8_t *)list_node - offsetof(event_prealloc_hdr_t, list_node));

	return likely(list_node != NULL) ? ev_hdr : NULL;
}

/**
 * @brief Convert event vector table content to odp packets in-place.
 *
 * Convert an EM event vector table, containing em_event_t:s with
 * esv-info (evgen), to a table of odp packets (remove handles' evgen in-place).
 */
static inline void
vector_tbl2odp(odp_event_t odp_event_pktvec)
{
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event_pktvec);
	odp_packet_t *pkt_tbl = NULL;
	const int pkts = odp_packet_vector_tbl(pkt_vec, &pkt_tbl/*out*/);

	if (likely(pkts > 0)) {
		/* Careful! Points to same table */
		em_event_t *event_tbl = (em_event_t *)pkt_tbl;

		/* Drop ESV event generation (evgen) from event handle */
		(void)events_em2pkt_inplace(event_tbl, pkts);
	}
}

/**
 * @brief Convert ODP packet vector table content to EM events.
 *
 * Convert an ODP packet vector table to a table of EM events.
 * The content must be known to be raw odp packets.
 *
 * For recovery purposes only.
 */
static inline void
vector_tbl2em(odp_event_t odp_event_pktvec)
{
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event_pktvec);
	odp_packet_t *pkt_tbl = NULL;
	const int pkts = odp_packet_vector_tbl(pkt_vec, &pkt_tbl/*out*/);

	if (likely(pkts > 0)) {
		em_event_t *const ev_tbl = (em_event_t *const)pkt_tbl;
		odp_packet_t odp_pkttbl[pkts];
		event_hdr_t *ev_hdr_tbl[pkts];

		/*
		 * Copy pkts from vector's pkt-table using events_em2pkt() that
		 * also drops any evgen-info from the handles if present.
		 */
		events_em2pkt(ev_tbl/*in*/, odp_pkttbl/*out*/, pkts);

		event_init_pkt_multi(odp_pkttbl /*in*/, ev_tbl /*in,out*/,
				     ev_hdr_tbl /*out*/, pkts, false);
	}
}

static inline em_status_t
send_event(em_event_t event, const queue_elem_t *q_elem)
{
	const bool esv_ena = esv_enabled();
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

	/*
	 * Vector: convert the event vector table to a table of odp packets
	 * (in-place) before passing the vector and contents to the scheduler.
	 */
	if (esv_ena && odp_event_type(odp_event) == ODP_EVENT_PACKET_VECTOR)
		vector_tbl2odp(odp_event);

	/* Enqueue event for scheduling */
	ret = odp_queue_enq(odp_queue, odp_event);

	if (unlikely(EM_CHECK_LEVEL > 0 && ret != 0)) {
		/* Restore EM vector event-table before returning vector to user */
		if (esv_ena && odp_event_type(odp_event) == ODP_EVENT_PACKET_VECTOR)
			vector_tbl2em(odp_event);

		return EM_ERR_LIB_FAILED;
	}

	return EM_OK;
}

static inline int
send_event_multi(const em_event_t events[], const int num,
		 const queue_elem_t *q_elem)
{
	const bool esv_ena = esv_enabled();
	odp_event_t odp_events[num];
	odp_queue_t odp_queue = q_elem->odp_queue;

	if (unlikely(EM_CHECK_LEVEL > 1 && odp_queue == ODP_QUEUE_INVALID))
		return 0;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY)) {
		return 0;
	}

	events_em2odp(events, odp_events/*out*/, num);

	/*
	 * Vector: convert the event vector table to a table of odp packets
	 * (in-place) before passing the vector and contents to the scheduler.
	 */
	if (esv_ena) {
		for (int i = 0; i < num; i++) {
			if (odp_event_type(odp_events[i]) == ODP_EVENT_PACKET_VECTOR)
				vector_tbl2odp(odp_events[i]);
		}
	}

	/* Enqueue events for scheduling */
	int ret = odp_queue_enq_multi(odp_queue, odp_events, num);

	if (likely(ret == num))
		return num; /* Success! */

	/*
	 * Fail: could not enqueue all events (ret != num)
	 */
	int enq = ret < 0 ? 0 : ret;

	/* Restore EM vector event-table before returning vector to user */
	if (esv_ena) {
		for (int i = enq; i < num; i++) {
			if (odp_event_type(odp_events[i]) == ODP_EVENT_PACKET_VECTOR)
				vector_tbl2em(odp_events[i]);
		}
	}

	return enq; /* enq < num */
}

static inline em_status_t
send_local(em_event_t event, const queue_elem_t *q_elem)
{
	em_locm_t *const locm = &em_locm;
	const em_queue_prio_t prio = q_elem->priority;
	evhdl_t evhdl = {.event = event};
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY))
		return EM_ERR_BAD_STATE;

	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	stash_entry_t entry = {.qidx = queue_hdl2idx(queue),
			       .evptr = evhdl.evptr};

	ret = odp_stash_put_u64(locm->local_queues.prio[prio].stash,
				&entry.u64, 1);
	if (likely(ret == 1)) {
		locm->local_queues.empty = 0;
		locm->local_queues.prio[prio].empty_prio = 0;
		return EM_OK;
	}

	return EM_ERR_LIB_FAILED;
}

static inline int
send_local_multi(const em_event_t events[], const int num,
		 const queue_elem_t *q_elem)
{
	em_locm_t *const locm = &em_locm;
	const em_queue_prio_t prio = q_elem->priority;
	const evhdl_t *const evhdl_tbl = (const evhdl_t *const)events;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY))
		return 0;

	stash_entry_t entry_tbl[num];
	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	const uint16_t qidx = (uint16_t)queue_hdl2idx(queue);

	for (int i = 0; i < num; i++) {
		entry_tbl[i].qidx = qidx;
		entry_tbl[i].evptr = evhdl_tbl[i].evptr;
	}

	int ret = odp_stash_put_u64(locm->local_queues.prio[prio].stash,
				    &entry_tbl[0].u64, num);
	if (likely(ret > 0)) {
		locm->local_queues.empty = 0;
		locm->local_queues.prio[prio].empty_prio = 0;
		return ret;
	}

	return 0;
}

/**
 * Send one event to a queue of type EM_QUEUE_TYPE_OUTPUT
 */
static inline em_status_t
send_output(em_event_t event, queue_elem_t *const output_q_elem)
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
	const em_queue_t output_queue = (em_queue_t)(uintptr_t)output_q_elem->queue;
	const em_output_func_t output_fn =
		output_q_elem->output.output_conf.output_fn;
	void *const output_fn_args =
		output_q_elem->output.output_conf.output_fn_args;
	int sent;

	sent = output_fn(&event, 1, output_queue, output_fn_args);
	if (unlikely(sent != 1))
		return EM_ERR_OPERATION_FAILED;

	return EM_OK;
}

/**
 * Send events to a queue of type EM_QUEUE_TYPE_OUTPUT
 */
static inline int
send_output_multi(const em_event_t events[], const unsigned int num,
		  queue_elem_t *const output_q_elem)
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
	const em_queue_t output_queue = (em_queue_t)(uintptr_t)output_q_elem->queue;
	const em_output_func_t output_fn = output_q_elem->output.output_conf.output_fn;
	void *const output_fn_args = output_q_elem->output.output_conf.output_fn_args;

	sent = output_fn(events, num, output_queue, output_fn_args);

	return sent;
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

#endif /* EM_EVENT_H_ */
