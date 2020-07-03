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
 */

#ifndef EM_DISPATCHER_H_
#define EM_DISPATCHER_H_

#ifdef __cplusplus
extern "C" {
#endif

static inline void
_dispatch_multi_receive(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
			const int num_events, queue_elem_t *const q_elem,
			const bool check_local_qs);
static inline void
_dispatch_single_receive(event_hdr_t *ev_hdr_tbl[], const int num_events,
			 queue_elem_t *const q_elem,
			 const bool check_local_qs);

/**
 * Helper: Remove undef-event entries from ev_tbl[]
 */
static inline int _pack_ev_tbl(em_event_t ev_tbl[], const int num)
{
	if (num == 1) {
		if (ev_tbl[0] != EM_EVENT_UNDEF)
			return 1;
		else
			return 0;
	}

	int pack = 0;

	for (int i = 0; i < num; i++) {
		if (ev_tbl[i] != EM_EVENT_UNDEF) {
			if (pack < i)
				ev_tbl[pack] = ev_tbl[i];
			pack++;
		}
	}

	return pack;
}

/**
 * Run all dispatch enter-callback functions.
 *
 * @note Neither EO-receive nor any further enter-callbacks will be called if
 *       all events have been dropped by the callbacks already run, i.e.
 *       no callback or EO-receive will be called with 'num=0'.
 *
 * @param eo            EO handle
 * @param eo_ctx        EO context data
 * @param event_tbl     Event table
 * @param num_events    Number of events in the event table
 * @param queue         Queue from which this event came from
 * @param q_ctx         Queue context data
 *
 * @return The number of events in ev_tbl[] after all dispatch enter callbacks
 */
static inline int
dispatch_enter_cb(em_eo_t eo, void **eo_ctx,
		  em_event_t ev_tbl[], const int num_events,
		  em_queue_t *queue, void **q_ctx)
{
	hook_tbl_t *const cb_tbl = em_shm->dispatch_enter_cb_tbl;
	em_dispatch_enter_func_t dispatch_enter_fn;
	int num = num_events;

	for (int i = 0; i < EM_CALLBACKS_MAX && num > 0; i++) {
		dispatch_enter_fn = cb_tbl->tbl[i].disp_enter;
		if (dispatch_enter_fn == NULL)
			break;
		dispatch_enter_fn(eo, eo_ctx, ev_tbl, num, queue, q_ctx);
		num = _pack_ev_tbl(ev_tbl, num);
	}

	return num;
}

/**
 * Run all dispatch exit-callback functions.
 *
 * @param eo    EO handle
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
	int num = 1;

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(env_atomic32_get(&ev_hdr->allocated) != 1)) {
		const char *eo_name = q_elem->eo_elem == NULL ?
				      "" : q_elem->eo_elem->name;
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_DISPATCH,
			       "EO:%" PRI_EO ":%s Q:%" PRI_QUEUE "\n"
			       "Event:%" PRI_EVENT " already freed",
			       eo, eo_name, queue, event);
	}

	em_locm.current.q_elem = q_elem;
	em_locm.current.rcv_multi_cnt = 1;
	/* Check and set core local event group (before dispatch callback(s)) */
	event_group_set_local(ev_hdr->egrp, ev_hdr->egrp_gen, 1);

	if (EM_DISPATCH_CALLBACKS_ENABLE)
		num = dispatch_enter_cb(eo, &eo_ctx, &event, 1,
					&queue, &queue_ctx);

	if (likely(num == 1)) {
		em_event_type_t event_type = ev_hdr->event_type;
		/*
		 * Call the EO receive function
		 * (only if the dispatch callback(s) did not free the event)
		 */
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

/**
 * @note All events belong to the same event group
 * @note Event type dropped from multi-event receive - use em_event_get_type()
 */
static inline void
call_eo_receive_multi_fn(const em_eo_t eo,
			 const em_receive_multi_func_t eo_receive_multi_func,
			 em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
			 const int num_events, queue_elem_t *const q_elem)
{
	em_queue_t queue = q_elem->queue;
	void *queue_ctx = q_elem->context;
	void *eo_ctx = q_elem->eo_ctx;
	int num = num_events;
	int i;

	if (EM_CHECK_LEVEL > 2) {
		for (i = 0; i < num_events &&
		     env_atomic32_get(&ev_hdr_tbl[i]->allocated) == 1; i++)
			;
		if (unlikely(i != num_events)) {
			INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE),
				       EM_ESCOPE_DISPATCH,
				       "EO:%" PRI_EO ": rcvd event(s) freed!");
		}
	}

	em_locm.current.q_elem = q_elem;
	em_locm.current.rcv_multi_cnt = num_events;
	/* Check and set core local event group (before dispatch callback(s)) */
	event_group_set_local(ev_hdr_tbl[0]->egrp, ev_hdr_tbl[0]->egrp_gen,
			      num_events);

	if (EM_DISPATCH_CALLBACKS_ENABLE)
		num = dispatch_enter_cb(eo, &eo_ctx,
					ev_tbl /* in/out */, num_events,
					&queue, &queue_ctx);
	if (likely(num > 0)) {
		/*
		 * Call the EO multi-event receive function
		 * (only if the dispatch callback(s) did not free all events)
		 */
		eo_receive_multi_func(eo_ctx, ev_tbl, num, queue, queue_ctx);
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
		event_group_count_decrement(num_events);
	}
	em_locm.current.egrp = EM_EVENT_GROUP_UNDEF;
}

static inline void
check_local_queues(void)
{
	em_event_t ev_tbl[EM_SCHED_MULTI_MAX_BURST];
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	int num, i;
	int idx;

	if (em_locm.local_queues.empty)
		return;

	/*
	 * Check if the previous EO receive function sent events to a
	 * local queue ('EM_QUEUE_TYPE_LOCAL') - and if so, dispatch
	 * those events immediately.
	 */
	for (;;) {
		num = next_local_queue_events(ev_tbl, EM_SCHED_MULTI_MAX_BURST);
		if (num <= 0)
			return;

		event_to_hdr_multi(ev_tbl, ev_hdr_tbl, num);

		idx = 0; /* index into ev_tbl[] & ev_hdr_tbl[] */
		do {
			queue_elem_t *const q_elem = ev_hdr_tbl[idx]->q_elem;

			/* count events sent to the same local queue */
			i = idx + 1;
			for (; i < num && q_elem == ev_hdr_tbl[i]->q_elem; i++)
				;

			const int ev_cnt = i - idx; /* 1 to num */

			if (unlikely(q_elem == NULL ||
				     q_elem->state != EM_QUEUE_STATE_READY)) {
				em_free_multi(&ev_tbl[idx], ev_cnt);
				/* Consider removing the logging */
				EM_LOG(EM_LOG_PRINT,
				       "EM info: %s(): localQ:%" PRI_QUEUE ":\n"
				       "Not ready - state:%d drop:%d events\n",
				       __func__, q_elem->queue,
				       q_elem->state, ev_cnt);
				idx += ev_cnt;
				continue;
			}

			if (q_elem->use_multi_rcv)
				_dispatch_multi_receive(&ev_tbl[idx],
							&ev_hdr_tbl[idx],
							ev_cnt, q_elem,
							false);
			else
				_dispatch_single_receive(&ev_hdr_tbl[idx],
							 ev_cnt, q_elem, false);
			idx += ev_cnt;
		} while (idx < num);
	}
}

static inline void
_dispatch_multi_receive(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
			const int num_events, queue_elem_t *const q_elem,
			const bool check_local_qs)
{
	const em_eo_t eo = q_elem->eo;
	const em_receive_multi_func_t eo_rcv_multi_fn =
		q_elem->receive_multi_func;
	int idx = 0; /* index into ev_hdr_tbl[] */

	do {
		const em_event_group_t egrp = ev_hdr_tbl[idx]->egrp;
		int i, j;

		/* count events with the same event group */
		i = idx + 1;
		if (EM_EVENT_GROUP_SAFE_MODE) {
			const int32_t egrp_gen =
				ev_hdr_tbl[idx]->egrp_gen;
			for (; i < num_events &&
			     egrp == ev_hdr_tbl[i]->egrp &&
			     egrp_gen == ev_hdr_tbl[i]->egrp_gen; i++)
				;
		} else {
			for (; i < num_events &&
			     egrp == ev_hdr_tbl[i]->egrp; i++)
				;
		}

		const int egrp_cnt = i - idx; /* 1 to num_events */
		const int max = q_elem->max_events;
		const int num = MIN(egrp_cnt, max);
		const int rounds = egrp_cnt / num;
		const int left_over = egrp_cnt % num;

		if (check_local_qs) {
			for (i = 0, j = idx; i < rounds; i++, j += num) {
				em_locm.event_burst_cnt -= num;
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 num, q_elem);
				check_local_queues();
			}
			if (left_over) {
				em_locm.event_burst_cnt = 0;
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 left_over, q_elem);
				check_local_queues();
			}
		} else {
			for (i = 0, j = idx; i < rounds; i++, j += num) {
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 num, q_elem);
			}
			if (left_over) {
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 left_over, q_elem);
			}
		}

		idx += egrp_cnt;
	} while (idx < num_events);
}

static inline void
_dispatch_single_receive(event_hdr_t *ev_hdr_tbl[], const int num_events,
			 queue_elem_t *const q_elem,
			 const bool check_local_qs)
{
	const em_eo_t eo = q_elem->eo;
	const em_receive_func_t eo_rcv_fn = q_elem->receive_func;
	int i;

	if (check_local_qs) {
		for (i = 0; i < num_events; i++) {
			em_locm.event_burst_cnt--;
			call_eo_receive_fn(eo, eo_rcv_fn,
					   ev_hdr_tbl[i], q_elem);
			check_local_queues();
		}
	} else {
		for (i = 0; i < num_events; i++)
			call_eo_receive_fn(eo, eo_rcv_fn,
					   ev_hdr_tbl[i], q_elem);
	}
}

/**
 * Dispatch events - call the EO-receive functions and pass the
 * events for processing
 */
static inline void
dispatch_events(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
		const int num_events, queue_elem_t *const q_elem)
{
	const em_queue_type_t q_type = q_elem->type;
	em_sched_context_type_t sched_ctx_type = EM_SCHED_CONTEXT_TYPE_NONE;

	if (q_type == EM_QUEUE_TYPE_ATOMIC)
		sched_ctx_type = EM_SCHED_CONTEXT_TYPE_ATOMIC;
	else if (q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
		sched_ctx_type = EM_SCHED_CONTEXT_TYPE_ORDERED;

	em_locm.current.sched_context_type = sched_ctx_type;
	em_locm.current.sched_q_elem = q_elem;
	/* here: em_locm.current.egrp == EM_EVENT_GROUP_UNDEF */

	/*
	 * Call the Execution Object (EO) receive function.
	 * Scheduling context may be released during this.
	 */
	if (q_elem->use_multi_rcv)
		_dispatch_multi_receive(ev_tbl, ev_hdr_tbl, num_events, q_elem,
					true);
	else
		_dispatch_single_receive(ev_hdr_tbl, num_events, q_elem,
					 true);

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
		check_local_queues();
		return 0;
	}

	ev_tbl = events_odp2em(odp_ev_tbl);
	/* Events might originate from outside of EM and need hdr-init */
	event_to_hdr_init_multi(ev_tbl, ev_hdr_tbl, num_events);

	queue_elem = odp_queue_context(odp_queue);
	if (unlikely(queue_elem == NULL ||
		     queue_elem->state != EM_QUEUE_STATE_READY)) {
		/* Drop all events dequeued from this queue */
		em_free_multi(ev_tbl, num_events);

		if (queue_elem == NULL)
			INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_DISPATCH,
				       "Event(s) from non-EM Q, drop %d events",
				       num_events);
		else
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_DISPATCH,
				       "Q:%" PRI_QUEUE " not ready, state=%d\n"
				       "    drop:%d event(s)\n",
				       queue_elem->queue, queue_elem->state,
				       num_events);
		return 0;
	}

	if (queue_elem->atomic_group == EM_ATOMIC_GROUP_UNDEF) {
		em_locm.event_burst_cnt = num_events;
		dispatch_events(ev_tbl, ev_hdr_tbl, num_events, queue_elem);
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
