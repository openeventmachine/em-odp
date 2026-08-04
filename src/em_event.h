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

#ifndef EM_EVENT_H_
#define EM_EVENT_H_

#include <stdbool.h>
#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_mem.h"
#include "em_pool_types.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"
#include "misc/list.h"

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
			 uint32_t offset, uint32_t size,
			 bool clone_uarea, bool is_clone_part);
void output_queue_track(queue_elem_t *const output_q_elem);
void output_queue_drain(const queue_elem_t *output_q_elem);
void output_queue_buffering_drain(void);

uint32_t event_vector_tbl(em_event_t vector_event, em_event_t **event_tbl/*out*/);
uint32_t event_vector_tbl__odp_pktvec(odp_packet_vector_t pkt_vec,
				      em_event_t **event_tbl /*out*/);
uint32_t event_vector_tbl__odp_evvec(odp_event_vector_t evvec,
				     em_event_t **event_tbl /*out*/);

em_status_t event_vector_max_size(em_event_t vector_event, uint32_t *max_size /*out*/,
				  em_escope_t escope);

/** Start-up helper for pool preallocation */
event_prealloc_hdr_t *event_prealloc(const mpool_elem_t *pool_elem, uint32_t size);
event_prealloc_hdr_t *list_node_to_prealloc_hdr(list_node_t *const list_node);

/*
 * Send an event to a scheduled queue.
 *
 * @param event   The event to send.
 * @param q_elem  The queue element of the destination queue.
 * @return EM_OK on success, EM_ERR_LIB_FAILED on failure.
 */
static inline em_status_t
send_sched(em_event_t event, const queue_elem_t *q_elem)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_queue_t odp_queue = q_elem->odp_queue;

	if (unlikely(EM_CHECK_LEVEL > 1 &&
		     (odp_event == ODP_EVENT_INVALID ||
		      odp_queue == ODP_QUEUE_INVALID)))
		return EM_ERR_NOT_FOUND;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY)) {
		return EM_ERR_BAD_STATE;
	}

	/* ESV enabled */
	if (esv_enabled()) {
		odp_event_type_t odp_etype = odp_event_type(odp_event);

		/*
		 * Check if the event is a vector event.
		 * is vector: convert the EM event vector table to a table of odp packets/events
		 * (in-place) before passing the vector and contents to the scheduler.
		 */
		if (odp_etype == ODP_EVENT_PACKET_VECTOR)
			pktvec_tbl2odp(odp_event);
		else if (odp_etype == ODP_EVENT_VECTOR)
			evvec_tbl2odp(odp_event);

		/* Enqueue event for scheduling */
		int ret = odp_queue_enq(odp_queue, odp_event);

		if (unlikely(EM_CHECK_LEVEL > 0 && ret != 0)) {
			/* Restore EM vector event-table before returning vector to user */
			if (odp_etype == ODP_EVENT_PACKET_VECTOR)
				pktvec_tbl2odp_revert(odp_event);
			else if (odp_etype == ODP_EVENT_VECTOR)
				evvec_tbl2odp_revert(odp_event);

			return EM_ERR_LIB_FAILED;
		}

		return EM_OK;
	}

	/* ESV disabled: */

	/* Enqueue event for scheduling */
	int ret = odp_queue_enq(odp_queue, odp_event);

	if (unlikely(EM_CHECK_LEVEL > 0 && ret != 0))
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

static inline int
send_sched_multi(const em_event_t events[], const int num,
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
		odp_event_type_t odp_etypes[num];

		for (int i = 0; i < num; i++)
			odp_etypes[i] = odp_event_type(odp_events[i]);

		for (int i = 0; i < num; i++) {
			if (odp_etypes[i] == ODP_EVENT_PACKET_VECTOR)
				pktvec_tbl2odp(odp_events[i]);
			else if (odp_etypes[i] == ODP_EVENT_VECTOR)
				evvec_tbl2odp(odp_events[i]);
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
		for (int i = enq; i < num; i++) {
			if (odp_etypes[i] == ODP_EVENT_PACKET_VECTOR)
				pktvec_tbl2odp_revert(odp_events[i]);
			else if (odp_etypes[i] == ODP_EVENT_VECTOR)
				evvec_tbl2odp_revert(odp_events[i]);
		}
		return enq; /* enq < num */
	}

	/* ESV disabled: */

	/* Enqueue events for scheduling */
	int ret = odp_queue_enq_multi(odp_queue, odp_events, num);

	if (likely(ret == num))
		return num; /* Success! */

	/*
	 * Fail: could not enqueue all events (ret != num)
	 */
	int enq = ret < 0 ? 0 : ret;

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

	for (int i = 0; i < num; i++)
		entry_tbl[i] = (stash_entry_t){.qidx = qidx,
					       .evptr = evhdl_tbl[i].evptr};

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
			odp_ticketlock_t *const lock =
				&output_q_elem->output.lock;

			if (!odp_ticketlock_trylock(lock))
				return EM_OK;
			output_queue_drain(output_q_elem);
			odp_ticketlock_unlock(lock);
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
			odp_ticketlock_t *const lock =
				&output_q_elem->output.lock;

			if (!odp_ticketlock_trylock(lock))
				return sent;
			output_queue_drain(output_q_elem);
			odp_ticketlock_unlock(lock);
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

/*
 * Send an event to an aggregator queue.
 *
 * @param event   The event to send.
 * @param ev_hdr  The event header (ESV: aggr_sent flag managed here).
 * @param q_elem  The queue element of the destination queue.
 * @return EM_OK on success, EM_ERR_LIB_FAILED on failure.
 */
static inline em_status_t
send_aggr(em_event_t event, event_hdr_t *ev_hdr, const queue_elem_t *q_elem)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_queue_t odp_queue = q_elem->odp_queue;

	if (unlikely(EM_CHECK_LEVEL > 1 &&
		     (odp_event == ODP_EVENT_INVALID ||
		      odp_queue == ODP_QUEUE_INVALID)))
		return EM_ERR_NOT_FOUND;

	/*
	 * Aggregator children inherit the parent's operational state: READY for
	 * scheduled parents, UNSCHEDULED for unscheduled parents. Accept both.
	 */
	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY &&
		     q_elem->state != EM_QUEUE_STATE_UNSCHEDULED)) {
		return EM_ERR_BAD_STATE;
	}

	if (EM_CHECK_LEVEL >= 3) {
		odp_event_type_t odp_etype = odp_event_type(odp_event);

		if (unlikely(odp_etype == ODP_EVENT_VECTOR ||
			     odp_etype == ODP_EVENT_PACKET_VECTOR)) {
			return EM_ERR_BAD_TYPE;
		}
	}

	/*
	 * Set flag before enqueue: another core may dispatch the event from
	 * the aggregation vector before odp_queue_enq() returns.
	 */
	if (esv_enabled() && !ev_hdr->flags.refs_used)
		ev_hdr->flags.aggr_sent = 1;

	/* Enqueue event */
	int ret = odp_queue_enq(odp_queue, odp_event);

	if (unlikely(ret != 0)) {
		if (esv_enabled() && !ev_hdr->flags.refs_used)
			ev_hdr->flags.aggr_sent = 0;
		return EM_ERR_LIB_FAILED;
	}

	return EM_OK;
}

static inline int
send_aggr_multi(const em_event_t events[], event_hdr_t *const ev_hdrs[],
		const int num, const queue_elem_t *q_elem)
{
	odp_event_t odp_events[num];
	odp_queue_t odp_queue = q_elem->odp_queue;

	if (unlikely(EM_CHECK_LEVEL > 1 && odp_queue == ODP_QUEUE_INVALID))
		return 0;

	/* Accept both scheduled (READY) and unscheduled aggregator parents */
	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_READY &&
		     q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return 0;

	events_em2odp(events, odp_events/*out*/, num);

	if (EM_CHECK_LEVEL >= 3) {
		odp_event_type_t odp_etypes[num];

		for (int i = 0; i < num; i++)
			odp_etypes[i] = odp_event_type(odp_events[i]);

		for (int i = 0; i < num; i++) {
			if (unlikely(odp_etypes[i] == ODP_EVENT_VECTOR ||
				     odp_etypes[i] == ODP_EVENT_PACKET_VECTOR))
				return 0;
		}
	}

	/*
	 * Set flags before enqueue: another core may dispatch events from
	 * the aggregation vector before odp_queue_enq_multi() returns.
	 */
	if (esv_enabled()) {
		for (int i = 0; i < num; i++) {
			if (!ev_hdrs[i]->flags.refs_used)
				ev_hdrs[i]->flags.aggr_sent = 1;
		}
	}

	/* Enqueue events */
	int ret = odp_queue_enq_multi(odp_queue, odp_events, num);

	if (likely(ret == num))
		return num; /* Success! */

	/*
	 * Fail: could not enqueue all events (ret != num)
	 */
	int enq = ret < 0 ? 0 : ret;

	/* Clear flag for events that failed to enqueue */
	if (esv_enabled()) {
		for (int i = enq; i < num; i++) {
			if (!ev_hdrs[i]->flags.refs_used)
				ev_hdrs[i]->flags.aggr_sent = 0;
		}
	}

	return enq; /* enq < num */
}

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_H_ */
