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
 * EM internal queue functions
 */

#ifndef EM_QUEUE_H_
#define EM_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#define DIFF_ABS(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define SMALLEST_NBR(a, b) ((a) > (b) ? (b) : (a))

em_status_t
queue_init(queue_tbl_t *const queue_tbl,
	   queue_pool_t *const queue_pool,
	   queue_pool_t *const queue_pool_static);

em_status_t
queue_init_local(void);
em_status_t
queue_term_local(void);

em_queue_t
queue_create(const char *name, em_queue_type_t type, em_queue_prio_t prio,
	     em_queue_group_t queue_group, em_queue_t queue_req,
	     em_atomic_group_t atomic_group, const em_queue_conf_t *conf,
	     const char **err_str);

em_status_t
queue_delete(queue_elem_t *const queue_elem);

em_status_t
queue_enable(queue_elem_t *const q_elem);

em_status_t
queue_enable_all(eo_elem_t *const eo_elem);

em_status_t
queue_disable(queue_elem_t *const q_elem);

em_status_t
queue_disable_all(eo_elem_t *const eo_elem);

em_status_t
queue_state_change__check(queue_state_t old_state, queue_state_t new_state,
			  int is_setup /* vs. is_teardown */);
em_status_t
queue_state_change(queue_elem_t *const queue_elem, queue_state_t new_state);

em_status_t
queue_state_change_all(eo_elem_t *const eo_elem, queue_state_t new_state);

unsigned int queue_count(void);

size_t queue_get_name(const queue_elem_t *const q_elem,
		      char name[/*out*/], const size_t maxlen);

void print_queue_info(void);

/**
 * Enqueue multiple events into an unscheduled queue.
 * Internal func, application should use em_send_multi() instead.
 */
static inline unsigned int
queue_unsched_enqueue_multi(const em_event_t events[], int num,
			    const queue_elem_t *const q_elem)
{
	odp_event_t odp_events[num];
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 && odp_queue == ODP_QUEUE_INVALID))
		return 0;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return 0;

	events_em2odp(events, odp_events, num);

	ret = odp_queue_enq_multi(odp_queue, odp_events, num);
	if (unlikely(ret < 0))
		return 0;

	return ret;
}

/**
 * Enqueue en event into an unscheduled queue.
 * Internal func, application should use em_send() instead.
 */
static inline em_status_t
queue_unsched_enqueue(em_event_t event, const queue_elem_t *const q_elem)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 &&
		     (odp_event == ODP_EVENT_INVALID ||
		      odp_queue == ODP_QUEUE_INVALID)))
		return EM_ERR_NOT_FOUND;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return EM_ERR_BAD_STATE;

	ret = odp_queue_enq(odp_queue, odp_event);
	if (unlikely(EM_CHECK_LEVEL > 0 && ret != 0))
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

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
		     (unsigned int)queue_idx > EM_MAX_QUEUES - 1))
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

	return q_elem->queue;
}

static inline queue_elem_t *
list_node_to_queue_elem(const list_node_t *const list_node)
{
	queue_elem_t *const q_elem = (queue_elem_t *)((uintptr_t)list_node
				     - offsetof(queue_elem_t, queue_node));

	return likely(list_node != NULL) ? q_elem : NULL;
}

static inline int
prio_em2odp(em_queue_prio_t em_prio, odp_schedule_prio_t *odp_prio /*out*/)
{
	switch (em_prio) {
	case EM_QUEUE_PRIO_LOWEST:
		/* fallthrough */
	case EM_QUEUE_PRIO_LOW:
		*odp_prio = odp_schedule_min_prio();
		return 0;
	case EM_QUEUE_PRIO_NORMAL:
		*odp_prio = odp_schedule_default_prio();
		return 0;
	case EM_QUEUE_PRIO_HIGH:
		/* fallthrough */
	case EM_QUEUE_PRIO_HIGHEST:
		*odp_prio = odp_schedule_max_prio();
		return 0;
	default:
		return -1;
	}
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
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
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
		*em_queue_type = EM_QUEUE_TYPE_PARALLEL_ORDERED;
		return 0;
	default:
		*em_queue_type = EM_QUEUE_TYPE_UNDEF;
		return 0;
	}
}

static inline event_hdr_t *
local_queue_dequeue(void)
{
	em_locm_t *const locm = &em_locm;
	odp_queue_t local_queue;
	odp_event_t odp_event;
	em_event_t event;
	event_hdr_t *ev_hdr;
	em_queue_prio_t prio;
	int i;

	if (locm->local_queues.empty)
		return NULL;

	prio = EM_QUEUE_PRIO_HIGHEST;
	for (i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
		/* from hi to lo prio: next prio if local queue is empty */
		if (locm->local_queues.prio[prio].empty_prio) {
			prio--;
			continue;
		}

		local_queue = locm->local_queues.prio[prio].queue;
		odp_event = odp_queue_deq(local_queue);

		if (odp_event != ODP_EVENT_INVALID) {
			event = event_odp2em(odp_event); /* .evgen not set */
			ev_hdr = event_to_hdr(event);
			return ev_hdr;
		}

		locm->local_queues.prio[prio].empty_prio = 1;
		prio--;
	}

	locm->local_queues.empty = 1;
	return NULL;
}

static inline int
next_local_queue_events(em_event_t ev_tbl[/*out*/], int num_events)
{
	em_locm_t *const locm = &em_locm;

	if (locm->local_queues.empty)
		return 0;

	/* use same output-array: odp_evtbl[] = ev_tbl[] */
	odp_event_t *const odp_evtbl = (odp_event_t *)ev_tbl;

	em_queue_prio_t prio;
	odp_queue_t local_queue;
	int num;

	prio = EM_QUEUE_PRIO_HIGHEST;
	for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
		/* from hi to lo prio: next prio if local queue is empty */
		if (locm->local_queues.prio[prio].empty_prio) {
			prio--;
			continue;
		}

		local_queue = locm->local_queues.prio[prio].queue;
		num = odp_queue_deq_multi(local_queue, odp_evtbl/*out=ev_tbl*/,
					  num_events);
		if (num > 0)
			return num; /* odp_evtbl[] = ev_tbl[], .evgen not set */

		locm->local_queues.prio[prio].empty_prio = 1;
		prio--;
	}

	locm->local_queues.empty = 1;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_H_ */
