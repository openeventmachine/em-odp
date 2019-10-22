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
 * EM internal dispatcher functions
 *
 */

#ifndef EM_DISPATCHER_H_
#define EM_DISPATCHER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Helper function for running all dispatcher enter callback functions.
 *
 * Note: All callbacks are run even if the event is freed (=EM_EVENT_UNDEF).
 *
 * @param eo            EO handle
 * @param eo_ctx        EO context data
 * @param event         Event handle
 * @param type          Event type
 * @param queue         Queue from which this event came from
 * @param q_ctx         Queue context data
 */
static inline void
dispatch_enter_cb(em_eo_t eo, void **eo_ctx, em_event_t *event,
		  em_event_type_t *type, em_queue_t *queue, void **q_ctx)
{
	hook_tbl_t *const dispatch_enter_cb_tbl = em_shm->dispatch_enter_cb_tbl;
	em_dispatch_enter_func_t dispatch_enter_fn;
	int i;

	for (i = 0; i < EM_CALLBACKS_MAX; i++) {
		dispatch_enter_fn = dispatch_enter_cb_tbl->tbl[i].disp_enter;
		if (dispatch_enter_fn == NULL)
			return;
		dispatch_enter_fn(eo, eo_ctx, event, type, queue, q_ctx);
	}
}

/**
 * Helper function for running all dispatcher exit callback functions.
 *
 * @param eo            EO handle
 */
static inline void
dispatch_exit_cb(em_eo_t eo)
{
	hook_tbl_t *const dispatch_exit_cb_tbl = em_shm->dispatch_exit_cb_tbl;
	em_dispatch_exit_func_t dispatch_exit_fn;
	int i;

	for (i = 0; i < EM_CALLBACKS_MAX; i++) {
		dispatch_exit_fn = dispatch_exit_cb_tbl->tbl[i].disp_exit;
		if (dispatch_exit_fn == NULL)
			return;
		dispatch_exit_fn(eo);
	}
}

static inline void
call_eo_receive_fn(const em_eo_t eo, const em_receive_func_t eo_receive_func,
		   event_hdr_t *const ev_hdr, queue_elem_t *const q_elem)
{
	em_queue_t queue = q_elem->queue;
	void *queue_ctx = q_elem->context;
	void *eo_ctx = q_elem->eo_ctx;

	em_event_t event = event_hdr_to_event(ev_hdr);
	em_event_type_t event_type = ev_hdr->event_type;

	em_locm.current.q_elem = q_elem;

	if (EM_DISPATCH_CALLBACKS_ENABLE)
		dispatch_enter_cb(eo, &eo_ctx, &event, &event_type,
				  &queue, &queue_ctx);

	if (likely(event != EM_EVENT_UNDEF)) {
		/* Check and set core local event group */
		event_group_set_local(ev_hdr);
		/* Call the EO receive function */
		eo_receive_func(eo_ctx, event, event_type,
				queue, queue_ctx);
	}

	if (EM_DISPATCH_CALLBACKS_ENABLE)
		dispatch_exit_cb(eo);

	/*
	 * Event belongs to an event_group, update the count and
	 * if requested send notifications
	 */
	if (em_locm.current.egrp != EM_EVENT_GROUP_UNDEF) {
		/*
		 * Atomically decrease the event group count.
		 * If the new count is zero, send notification events.
		 */
		event_group_count_decrement(1);
	}
	em_locm.current.egrp = EM_EVENT_GROUP_UNDEF;
}

static inline void
check_local_queues(void)
{
	event_hdr_t *ev_hdr;

	/*
	 * Check if the previous EO receive function sent events to a
	 * local queue ('EM_QUEUE_TYPE_LOCAL') - and if so, dispatch
	 * those events immediately.
	 */
	while ((ev_hdr = next_local_queue_event()) != NULL) {
		/* Dispatch event from a local queue */
		queue_elem_t *const local_q_elem = ev_hdr->q_elem;
		const em_eo_t eo = local_q_elem->eo;
		const em_receive_func_t eo_receive_func =
			local_q_elem->receive_func;

		/*
		 * Call the Execution Object (EO) receive function.
		 * Scheduling context may be released during this
		 */
		call_eo_receive_fn(eo, eo_receive_func, ev_hdr, local_q_elem);
	}
}

/**
 * Dispatch events - call the EO-receive functions and pass the
 * events for processing
 */
static inline void
dispatch_events(event_hdr_t *const ev_hdr_tbl[], const int num_events,
		queue_elem_t *const q_elem)
{
	const em_queue_type_t q_type = q_elem->type;
	const em_eo_t eo = q_elem->eo;
	const em_receive_func_t eo_receive_func = q_elem->receive_func;
	em_sched_context_type_t sched_ctx_type = EM_SCHED_CONTEXT_TYPE_NONE;

	if (q_type == EM_QUEUE_TYPE_ATOMIC)
		sched_ctx_type = EM_SCHED_CONTEXT_TYPE_ATOMIC;
	else if (q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
		sched_ctx_type = EM_SCHED_CONTEXT_TYPE_ORDERED;

	em_locm.current.sched_context_type = sched_ctx_type;
	em_locm.current.sched_q_elem = q_elem;
	/* here: em_locm.current.egrp == EM_EVENT_GROUP_UNDEF */

	/*
	 * Call the Execution Object (EO) receive function for each event.
	 * Scheduling context may be released during this.
	 */
	for (int i = 0; i < num_events; i++) {
		event_hdr_t *const ev_hdr = ev_hdr_tbl[i];

		em_locm.event_burst_cnt--;
		call_eo_receive_fn(eo, eo_receive_func, ev_hdr, q_elem);
		check_local_queues();
	}

	/*
	 * Check for buffered events sent to output queues during the previous
	 * dispatch rounds
	 */
	if (!EM_OUTPUT_QUEUE_IMMEDIATE &&
	    em_locm.output_queue_track.idx_cnt > 0)
		output_queue_buffering_drain();

	em_locm.current.q_elem = NULL;
	em_locm.current.sched_q_elem = NULL;
	em_locm.current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE;
}

static inline void
dispatch_local_queues(void)
{
	event_hdr_t *const ev_hdr = next_local_queue_event();
	queue_elem_t *local_q_elem;

	if (ev_hdr != NULL) {
		local_q_elem = ev_hdr->q_elem;
		dispatch_events(&ev_hdr, 1, local_q_elem);
	}
}

/*
 * Run a dispatch round - query the scheduler for events and dispatch
 */
static inline int
dispatch_round(void)
{
	odp_queue_t odp_queue;
	odp_event_t odp_ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	em_event_t *ev_tbl;
	queue_elem_t *queue_elem;

	/*
	 * Schedule events to the core from queues
	 */
	const int num_events =
		odp_schedule_multi_no_wait(&odp_queue, odp_ev_tbl,
					   EM_SCHED_MULTI_MAX_BURST);
	if (unlikely(num_events <= 0)) {
		/*
		 * No scheduled events available, check if the local queues
		 * contain anything on this core - e.g. pktio or something
		 * outside the dispatch-context might have sent to a local queue
		 */
		dispatch_local_queues();
		return 0;
	}

	ev_tbl = events_odp2em(odp_ev_tbl);
	events_to_event_hdrs(ev_tbl, ev_hdr_tbl, num_events);

	queue_elem = odp_queue_context(odp_queue);
	if (unlikely(queue_elem == NULL ||
		     queue_elem->state != EM_QUEUE_STATE_READY)) {
		/* Drop all events dequeued from this queue */
		event_free_multi(ev_tbl, num_events);

		if (queue_elem == NULL)
			INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_DISPATCH,
				       "Event(s) from non-EM Q, drop %d events",
				       num_events);
		else
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_DISPATCH,
				       "Q:%" PRI_QUEUE " not ready, state=%d",
				       queue_elem->queue, queue_elem->state);
		return 0;
	}

	if (queue_elem->atomic_group == EM_ATOMIC_GROUP_UNDEF) {
		em_locm.event_burst_cnt = num_events;
		dispatch_events(ev_hdr_tbl, num_events, queue_elem);
	} else {
		atomic_group_dispatch(ev_tbl, ev_hdr_tbl,
				      num_events, queue_elem);
	}

	return num_events;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_DISPATCHER_H_ */
