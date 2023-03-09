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

#ifndef EM_QUEUE_INLINE_H_
#define EM_QUEUE_INLINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#define DIFF_ABS(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define SMALLEST_NBR(a, b) ((a) > (b) ? (b) : (a))

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

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_INLINE_H_ */
