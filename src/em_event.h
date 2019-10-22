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

em_status_t
event_init(void);

/**
 * Free multiple events.
 *
 * It is assumed that the implementation can detect the memory area/pool that
 * the event was originally allocated from.
 *
 * The event_free_multi() function transfers ownership of the events back to the
 * system and the events must not be touched after calling it.
 *
 * @param events         Array of events to be freed
 * @param num            The number of events in the array 'events[]'
 */
void event_free_multi(em_event_t *const events, const int num);

/**
 * This function is declared as a weak symbol, meaning that the user can
 * override it during linking with another implementation.
 */
em_status_t __attribute__((weak))
event_send_device(em_event_t event, em_queue_t queue);
/**
 * This function is declared as a weak symbol, meaning that the user can
 * override it during linking with another implementation.
 */
int __attribute__((weak))
event_send_device_multi(em_event_t *const events, int num, em_queue_t queue);

COMPILE_TIME_ASSERT((uintptr_t)EM_EVENT_UNDEF == (uintptr_t)ODP_EVENT_INVALID,
		    EM_EVENT_NOT_EQUAL_TO_ODP_EVENT);

void output_queue_track(queue_elem_t *const output_q_elem);
void output_queue_drain(queue_elem_t *const output_q_elem);
void output_queue_buffering_drain(void);

static inline odp_event_t
event_em2odp(em_event_t em_event)
{
	return (odp_event_t)em_event;
}

static inline em_event_t
event_odp2em(odp_event_t odp_event)
{
	return (em_event_t)odp_event;
}

static inline em_event_t *
events_odp2em(odp_event_t odp_events[])
{
	return (em_event_t *)odp_events;
}

static inline odp_event_t *
events_em2odp(em_event_t events[])
{
	return (odp_event_t *)events;
}

static inline void
pkt_event_hdr_init(event_hdr_t *ev_hdr, const em_event_t event)
{
	ev_hdr->event = event;
	ev_hdr->event_type = EM_EVENT_TYPE_PACKET;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	if (EM_CHECK_LEVEL > 1) {
		/* Simple double-free detection */
		env_atomic32_set(&ev_hdr->allocated, 1);
	}
	if (EM_POOL_STATISTICS_ENABLE) {
		/* ev_hdr->subpool = 0; */
		ev_hdr->pool = EM_POOL_UNDEF;
	}
}

static inline void
pkt_event_hdrs_init(event_hdr_t *ev_hdrs[], const em_event_t events[],
		    const int num)
{
	for (int i = 0; i < num; i++) {
		ev_hdrs[i]->event = events[i];
		ev_hdrs[i]->event_type = EM_EVENT_TYPE_PACKET;
		ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;
		if (EM_CHECK_LEVEL > 1) {
			/* Simple double-free detection */
			env_atomic32_set(&ev_hdrs[i]->allocated, 1);
		}
		if (EM_POOL_STATISTICS_ENABLE) {
			/* ev_hdrs[i]->subpool = 0; */
			ev_hdrs[i]->pool = EM_POOL_UNDEF;
		}
	}
}

/* packet-input: init ev-hdrs in the received odp-pkts */
static inline void
pkts_to_event_hdrs(const odp_packet_t odp_pkts[], const em_event_t events[],
		   event_hdr_t *ev_hdrs[], const int num)
{
	for (int i = 0; i < num; i++) {
		ev_hdrs[i] = odp_packet_user_area(odp_pkts[i]);
		odp_prefetch_store(ev_hdrs[i]);
		odp_packet_user_ptr_set(odp_pkts[i], PKT_USERPTR_MAGIC_NBR);
	}

	pkt_event_hdrs_init(ev_hdrs, events, num);
}

/** Convert from EM event to event header */
static inline event_hdr_t *
event_to_event_hdr(em_event_t event)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_packet_t odp_pkt;
	odp_buffer_t odp_buf;
	event_hdr_t *ev_hdr;

	if (odp_event_type(odp_event) == ODP_EVENT_PACKET) {
		odp_pkt = odp_packet_from_event(odp_event);
		ev_hdr = odp_packet_user_area(odp_pkt);

		if (odp_packet_user_ptr(odp_pkt) != PKT_USERPTR_MAGIC_NBR) {
			/* Pkt from outside of EM, need to init ev_hdr */
			odp_packet_user_ptr_set(odp_pkt, PKT_USERPTR_MAGIC_NBR);
			pkt_event_hdr_init(ev_hdr, event);
		}
	} else {
		odp_buf = odp_buffer_from_event(odp_event);
		ev_hdr = odp_buffer_addr(odp_buf);
	}

	return ev_hdr;
}

static inline void
events_to_event_hdrs(em_event_t events[], event_hdr_t *ev_hdrs[], const int num)
{
	odp_event_t *const odp_events = events_em2odp(events);
	odp_packet_t odp_pkts[num];
	odp_buffer_t odp_buf;
	odp_event_type_t type;
	int num_type;
	int init_num;
	int ev = 0; /* event & ev_hdr tbl index*/
	int i;

	do {
		num_type =
		odp_event_type_multi(&odp_events[ev], num - ev, &type);

		if (type == ODP_EVENT_PACKET) {
			odp_packet_from_event_multi(odp_pkts, &odp_events[ev],
						    num_type);
			for (i = 0; i < num_type; i++) {
				ev_hdrs[ev + i] =
					odp_packet_user_area(odp_pkts[i]);
				odp_prefetch(ev_hdrs[ev + i]);
			}

			event_hdr_t *init_hdrs[num_type];
			em_event_t init_events[num_type];

			init_num = 0;
			for (i = 0; i < num_type; i++) {
				if (odp_packet_user_ptr(odp_pkts[i]) ==
				    PKT_USERPTR_MAGIC_NBR)
					continue;
				odp_packet_user_ptr_set(odp_pkts[i],
							PKT_USERPTR_MAGIC_NBR);
				init_hdrs[init_num] = ev_hdrs[ev + i];
				init_events[init_num++] = events[ev + i];
			}

			/* If pkt from outside of EM: need to init ev_hdrs */
			if (init_num)
				pkt_event_hdrs_init(init_hdrs, init_events,
						    init_num);
		} else {
			for (i = 0; i < num_type; i++) {
				odp_buf =
				odp_buffer_from_event(odp_events[ev + i]);
				ev_hdrs[ev + i] = odp_buffer_addr(odp_buf);
				odp_prefetch(ev_hdrs[ev + i]);
			}
		}
		ev += num_type;
	} while (ev < num);
}

/** Convert from event header to EM event */
static inline em_event_t
event_hdr_to_event(event_hdr_t *const event_hdr)
{
	return event_hdr->event;
}

static inline em_event_t
event_alloc_buf(mpool_elem_t *const pool_elem, size_t size,
		em_event_type_t type)
{
	int subpool;
	odp_buffer_t odp_buf;
	odp_event_t odp_event;
	em_event_t em_event;
	event_hdr_t *ev_hdr;

	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return EM_EVENT_UNDEF;

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has buffers available of a bigger size.
	 */
	do {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL > 1 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return EM_EVENT_UNDEF;

		odp_buf = odp_buffer_alloc(odp_pool);
		if (likely(odp_buf != ODP_BUFFER_INVALID))
			break;
	} while (EM_MAX_SUBPOOLS > 1 /* Compile time option */ &&
		 ++subpool < pool_elem->num_subpools);

	if (unlikely(odp_buf == ODP_BUFFER_INVALID))
		return EM_EVENT_UNDEF;

	ev_hdr = odp_buffer_addr(odp_buf);
	odp_event = odp_buffer_to_event(odp_buf);
	em_event = event_odp2em(odp_event);

	/* For optimization, no initialization for feature variables */
	ev_hdr->event = em_event;  /* store this event handle */
	ev_hdr->event_size = size; /* store requested size */
	ev_hdr->event_type = type; /* store the event type */
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	/* Simple double-free detection */
	if (EM_CHECK_LEVEL > 1)
		env_atomic32_set(&ev_hdr->allocated, 1);

	/* Pool usage statistics */
	if (EM_POOL_STATISTICS_ENABLE) {
		em_pool_t pool = pool_elem->em_pool;

		ev_hdr->subpool = subpool;
		ev_hdr->pool = pool;

		pool_stat_increment(pool, subpool);
	}

	return em_event;
}

static inline em_event_t
event_alloc_pkt(mpool_elem_t *const pool_elem, size_t size,
		em_event_type_t type)
{
	int subpool;
	odp_packet_t odp_pkt;
	odp_event_t odp_event;
	em_event_t em_event;
	event_hdr_t *ev_hdr;

	subpool = pool_find_subpool(pool_elem, size);
	if (unlikely(subpool < 0))
		return EM_EVENT_UNDEF;

	/*
	 * Allocate from the 'best fit' subpool, or if that is full, from the
	 * next subpool that has pkts available of a bigger size.
	 */
	do {
		odp_pool_t odp_pool = pool_elem->odp_pool[subpool];

		if (EM_CHECK_LEVEL > 1 &&
		    unlikely(odp_pool == ODP_POOL_INVALID))
			return EM_EVENT_UNDEF;

		odp_pkt = odp_packet_alloc(odp_pool, size);
		if (likely(odp_pkt != ODP_PACKET_INVALID))
			break;
	} while (EM_MAX_SUBPOOLS > 1 /* Compile time option */ &&
		 ++subpool < pool_elem->num_subpools);

	if (unlikely(odp_pkt == ODP_PACKET_INVALID))
		return EM_EVENT_UNDEF;

	ev_hdr = odp_packet_user_area(odp_pkt);
	if (unlikely(ev_hdr == NULL)) {
		odp_packet_free(odp_pkt);
		return EM_EVENT_UNDEF;
	}

	/*
	 * Set the pkt user ptr to be able to recognize pkt-events that
	 * EM has created vs pkts from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	odp_packet_user_ptr_set(odp_pkt, PKT_USERPTR_MAGIC_NBR);

	odp_event = odp_packet_to_event(odp_pkt);
	em_event = event_odp2em(odp_event);

	/* For optimization, no initialization for feature variables */
	ev_hdr->event = em_event;  /* store this event handle */
	ev_hdr->event_size = size; /* store requested size */
	ev_hdr->event_type = type; /* store the event type */
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	/* Simple double-free detection */
	if (EM_CHECK_LEVEL > 1)
		env_atomic32_set(&ev_hdr->allocated, 1);

	/* Pool usage statistics */
	if (EM_POOL_STATISTICS_ENABLE) {
		em_pool_t pool = pool_elem->em_pool;

		ev_hdr->subpool = subpool;
		ev_hdr->pool = pool;

		pool_stat_increment(pool, subpool);
	}

	return em_event;
}

static inline event_hdr_t *
start_node_to_event_hdr(list_node_t *const list_node)
{
	event_hdr_t *const ev_hdr = (event_hdr_t *)((uint8_t *)list_node
				     - offsetof(event_hdr_t, start_node));

	return likely(list_node != NULL) ? ev_hdr : NULL;
}

static inline em_status_t
send_event(em_event_t event, queue_elem_t *const q_elem)
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
send_event_multi(em_event_t events[], const int num, queue_elem_t *const q_elem)
{
	odp_event_t *const odp_events = events_em2odp(events);
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 && odp_queue == ODP_QUEUE_INVALID))
		return 0;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY)) {
		return 0;
	}

	ret = odp_queue_enq_multi(odp_queue, odp_events, num);
	if (unlikely(ret < 0))
		return 0;

	return ret;
}

static inline em_status_t
send_local(em_event_t event, event_hdr_t *const ev_hdr,
	   queue_elem_t *const q_elem)
{
	const em_queue_prio_t prio = q_elem->priority;
	odp_event_t odp_event = event_em2odp(event);
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY))
		return EM_ERR_BAD_STATE;

	ev_hdr->q_elem = q_elem;

	ret = odp_queue_enq(em_locm.local_queues.prio[prio].queue, odp_event);
	if (likely(ret == 0)) {
		em_locm.local_queues.empty = 0;
		em_locm.local_queues.prio[prio].empty_prio = 0;
		return EM_OK;
	}

	return EM_ERR_LIB_FAILED;
}

static inline int
send_local_multi(em_event_t events[], event_hdr_t *const ev_hdrs[],
		 const int num, queue_elem_t *const q_elem)
{
	odp_event_t *const odp_events = events_em2odp(events);
	const em_queue_prio_t prio = q_elem->priority;
	int enq;
	int i;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY))
		return 0;

	for (i = 0; i < num; i++)
		ev_hdrs[i]->q_elem = q_elem;

	enq = odp_queue_enq_multi(em_locm.local_queues.prio[prio].queue,
				  odp_events, num);
	if (likely(enq > 0)) {
		em_locm.local_queues.empty = 0;
		em_locm.local_queues.prio[prio].empty_prio = 0;
		return enq;
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
	} else {
		/*
		 * no ordered context - call output_fn() directly
		 */
		const em_queue_t output_queue = output_q_elem->queue;
		const em_output_func_t output_fn =
			output_q_elem->output.output_conf.output_fn;
		void *const output_fn_args =
			output_q_elem->output.output_conf.output_fn_args;
		int sent;

		sent = output_fn(&event, 1, output_queue, output_fn_args);
		if (unlikely(sent != 1))
			return EM_ERR_OPERATION_FAILED;
	}

	return EM_OK;
}

/**
 * Send events to a queue of type EM_QUEUE_TYPE_OUTPUT
 */
static inline int
send_output_multi(em_event_t events[], const unsigned int num,
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
		odp_event_t *const odp_events = events_em2odp(events);

		if (unlikely(EM_CHECK_LEVEL > 1 &&
			     odp_queue == ODP_QUEUE_INVALID))
			return 0;

		if (!EM_OUTPUT_QUEUE_IMMEDIATE)
			output_queue_track(output_q_elem);

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
	} else {
		/*
		 * no ordered context - call output_fn() directly
		 */
		const em_queue_t output_queue = output_q_elem->queue;
		const em_output_func_t output_fn =
			output_q_elem->output.output_conf.output_fn;
		void *const output_fn_args =
			output_q_elem->output.output_conf.output_fn_args;

		sent = output_fn(events, num, output_queue, output_fn_args);
	}

	return sent;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_H_ */
