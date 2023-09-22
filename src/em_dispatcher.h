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

#include <event_machine/helper/event_machine_debug.h>

#ifdef __cplusplus
extern "C" {
#endif

em_status_t dispatch_init(void);
em_status_t dispatch_init_local(void);

static inline void
dispatch_multi_receive(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
		       const int num_events, queue_elem_t *const q_elem,
		       const bool check_local_qs);
static inline void
dispatch_single_receive(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
			const int num_events, queue_elem_t *const q_elem,
			const bool check_local_qs);

/**
 * Helper: Remove undef-event entries from ev_tbl[]
 */
static inline int pack_ev_tbl(em_event_t ev_tbl[/*in,out*/], const int num)
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

static inline uint64_t debug_timestamp(void)
{
	/* compile time selection */
	return EM_DEBUG_TIMESTAMP_ENABLE == 1 ? odp_time_global_ns() : odp_time_global_strict_ns();
}

/**
 * Run all dispatch enter-callback functions.
 *
 * @note Neither EO-receive nor any further enter-callbacks will be called if
 *       all events have been dropped by the callbacks already run, i.e.
 *       no callback or EO-receive will be called with 'num=0'.
 *
 * @param eo              EO handle
 * @param eo_ctx          EO context data
 * @param[in,out] ev_tbl  Event table
 * @param num_events      Number of events in the event table
 * @param queue           Queue from which this event came from
 * @param q_ctx           Queue context data
 *
 * @return The number of events in ev_tbl[] after all dispatch enter callbacks
 */
static inline int
dispatch_enter_cb(em_eo_t eo, void **eo_ctx,
		  em_event_t ev_tbl[/*in,out*/], const int num_events,
		  em_queue_t *queue, void **q_ctx)
{
	const hook_tbl_t *cb_tbl = em_shm->dispatch_enter_cb_tbl;
	em_dispatch_enter_func_t dispatch_enter_fn;
	int num = num_events;

	for (int i = 0; i < EM_CALLBACKS_MAX && num > 0; i++) {
		dispatch_enter_fn = cb_tbl->tbl[i].disp_enter;
		if (dispatch_enter_fn == NULL)
			break;
		dispatch_enter_fn(eo, eo_ctx, ev_tbl, num, queue, q_ctx);
		num = pack_ev_tbl(ev_tbl, num);
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
	const hook_tbl_t *dispatch_exit_cb_tbl = em_shm->dispatch_exit_cb_tbl;
	em_dispatch_exit_func_t dispatch_exit_fn;

	for (int i = 0; i < EM_CALLBACKS_MAX; i++) {
		dispatch_exit_fn = dispatch_exit_cb_tbl->tbl[i].disp_exit;
		if (dispatch_exit_fn == NULL)
			return;
		dispatch_exit_fn(eo);
	}
}

static inline void
call_eo_receive_fn(const em_eo_t eo, const em_receive_func_t eo_receive_func,
		   em_event_t event, event_hdr_t *ev_hdr, queue_elem_t *const q_elem)
{
	em_locm_t *const locm = &em_locm;
	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	void *queue_ctx = q_elem->context;
	void *eo_ctx = q_elem->eo_ctx;
	int num = 1;

	locm->current.q_elem = q_elem;
	locm->current.rcv_multi_cnt = 1;
	/* Check and set core local event group (before dispatch callback(s)) */
	event_group_set_local(ev_hdr->egrp, ev_hdr->egrp_gen, 1);

	if (EM_DISPATCH_CALLBACKS_ENABLE) {
		em_event_t ev_tbl[1] = {event};

		num = dispatch_enter_cb(eo, &eo_ctx, ev_tbl/*in,out*/, 1,
					&queue, &queue_ctx);
		if (num && ev_tbl[0] != event) {
			/* user-callback changed event: update event & hdr */
			event = ev_tbl[0];
			ev_hdr = event_to_hdr(event);
		}
	}

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
	if (locm->current.egrp != EM_EVENT_GROUP_UNDEF) {
		/*
		 * Atomically decrease the event group count.
		 * If the new count is zero, send notification events.
		 */
		event_group_count_decrement(1);
	}
	locm->current.egrp = EM_EVENT_GROUP_UNDEF;
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
	em_locm_t *const locm = &em_locm;
	em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	void *queue_ctx = q_elem->context;
	void *eo_ctx = q_elem->eo_ctx;
	int num = num_events;

	locm->current.q_elem = q_elem;
	locm->current.rcv_multi_cnt = num_events;
	/* Check and set core local event group (before dispatch callback(s)) */
	event_group_set_local(ev_hdr_tbl[0]->egrp, ev_hdr_tbl[0]->egrp_gen,
			      num_events);

	if (EM_DISPATCH_CALLBACKS_ENABLE)
		num = dispatch_enter_cb(eo, &eo_ctx,
					ev_tbl/*in,out*/, num_events,
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
	if (locm->current.egrp != EM_EVENT_GROUP_UNDEF) {
		/*
		 * Atomically decrease the event group count.
		 * If the new count is zero, send notification events.
		 */
		event_group_count_decrement(num_events);
	}
	locm->current.egrp = EM_EVENT_GROUP_UNDEF;
}

/**
 * @brief Helper to dispatch_local_queues() for a single event
 */
static inline void
_dispatch_local(stash_entry_t entry)
{
	odp_event_t odp_event;
	em_event_t event;
	event_hdr_t *ev_hdr;

	odp_event = (odp_event_t)(uintptr_t)entry.evptr;

	/* Event might originate from outside (via polled pktio) of EM and need init */
	event = event_init_odp(odp_event, true/*is_extev*/, &ev_hdr/*out*/);

	/* dst local queue */
	const int qidx = entry.qidx;
	const em_queue_t queue = queue_idx2hdl(qidx);
	queue_elem_t *const q_elem = queue_elem_get(queue);

	if (unlikely(q_elem == NULL || q_elem->state != EM_QUEUE_STATE_READY)) {
		em_free(event);
		/* Consider removing the logging */
		EM_LOG(EM_LOG_PRINT,
		       "EM info: %s(): localQ:%" PRIx32 ":\n"
		       "Not ready - state:%d drop:1 event\n",
		       __func__, q_elem->queue, q_elem->state);
		return;
	}

	if (q_elem->flags.use_multi_rcv)
		dispatch_multi_receive(&event, &ev_hdr, 1, q_elem, false);
	else
		dispatch_single_receive(&event, &ev_hdr, 1, q_elem, false);
}

/**
 * @brief Helper to dispatch_local_queues() for multiple events
 */
static inline void
_dispatch_local_multi(const stash_entry_t entry_tbl[], const int num)
{
	int idx = 0; /* index into ev_tbl[] & ev_hdr_tbl[] */
	int ev_cnt;  /* number of events to the same local-queue */

	odp_event_t odp_evtbl[num];
	em_event_t ev_tbl[num];
	event_hdr_t *evhdr_tbl[num];

	for (int i = 0; i < num; i++)
		odp_evtbl[i] = (odp_event_t)(uintptr_t)entry_tbl[i].evptr;

	/* Events might originate from outside (via polled pktio) of EM and need init */
	event_init_odp_multi(odp_evtbl, ev_tbl/*out*/, evhdr_tbl/*out*/,
			     num, true/*is_extev*/);

	/* Loop through 'num' events and dispatch in batches to local queues */
	do {
		/* dst local queue */
		const int qidx = entry_tbl[idx].qidx;
		const em_queue_t queue = queue_idx2hdl(qidx);
		queue_elem_t *const q_elem = queue_elem_get(queue);
		int i;

		/*
		 * Count events sent to the same local queue,
		 * i < num <= EM_QUEUE_LOCAL_MULTI_MAX_BURST
		 */
		for (i = idx + 1; i < num && entry_tbl[i].qidx == qidx; i++)
			;

		ev_cnt = i - idx; /* '1 to num' events */

		if (unlikely(q_elem == NULL || q_elem->state != EM_QUEUE_STATE_READY)) {
			em_free_multi(&ev_tbl[idx], ev_cnt);
			/* Consider removing the logging */
			EM_LOG(EM_LOG_PRINT,
			       "EM info: %s(): localQ:%" PRIx32 ":\n"
			       "Not ready - state:%d drop:%d events\n",
			       __func__, q_elem->queue,
			       q_elem->state, ev_cnt);
			idx += ev_cnt;
			continue;
		}

		if (q_elem->flags.use_multi_rcv)
			dispatch_multi_receive(&ev_tbl[idx],
					       &evhdr_tbl[idx],
					       ev_cnt, q_elem, false);
		else
			dispatch_single_receive(&ev_tbl[idx],
						&evhdr_tbl[idx],
						ev_cnt, q_elem, false);
		idx += ev_cnt;
	} while (idx < num);
}

static inline void
dispatch_local_queues(const stash_entry_t entry_tbl[], const int num)
{
	if (num == 1)
		_dispatch_local(entry_tbl[0]);
	else
		_dispatch_local_multi(entry_tbl, num);
}

static inline void
check_local_queues(void)
{
	if (em_locm.local_queues.empty)
		return;

	/*
	 * Check if the previous EO receive function sent events to a
	 * local queue ('EM_QUEUE_TYPE_LOCAL') - and if so, dispatch
	 * those events immediately.
	 */
	stash_entry_t entry_tbl[EM_QUEUE_LOCAL_MULTI_MAX_BURST];

	for (;;) {
		int num = next_local_queue_events(entry_tbl /*[out]*/,
						  EM_QUEUE_LOCAL_MULTI_MAX_BURST);
		if (num <= 0)
			return;

		dispatch_local_queues(entry_tbl, num);
	}
}

/**
 * Count events (hdrs) sent/tagged with the same event group
 */
static inline int
count_same_evgroup(event_hdr_t *ev_hdr_tbl[], const unsigned int num)
{
	if (unlikely(num < 2))
		return num;

	const em_event_group_t egrp = ev_hdr_tbl[0]->egrp;
	unsigned int i = 1; /* 2nd hdr */

	if (EM_EVENT_GROUP_SAFE_MODE && egrp != EM_EVENT_GROUP_UNDEF) {
		const int32_t egrp_gen = ev_hdr_tbl[0]->egrp_gen;

		for (; i < num &&
		     egrp == ev_hdr_tbl[i]->egrp &&
		     egrp_gen == ev_hdr_tbl[i]->egrp_gen; i++)
			;
	} else {
		for (; i < num &&
		     egrp == ev_hdr_tbl[i]->egrp; i++)
			;
	}

	return i;
}

static inline void
dispatch_multi_receive(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
		       const int num_events, queue_elem_t *const q_elem,
		       const bool check_local_qs)
{
	const em_eo_t eo = (em_eo_t)(uintptr_t)q_elem->eo;
	const em_receive_multi_func_t eo_rcv_multi_fn =
		q_elem->receive_multi_func;
	int idx = 0; /* index into ev_hdr_tbl[] */
	int i;
	int j;

	do {
		/* count same event groups: 1 to num_events */
		const int egrp_cnt = count_same_evgroup(&ev_hdr_tbl[idx],
							num_events - idx);
		const int max = q_elem->max_events;
		const int num = MIN(egrp_cnt, max);
		const int rounds = egrp_cnt / num;
		const int left_over = egrp_cnt % num;

		if (check_local_qs) {
			em_locm_t *const locm = &em_locm;

			j = idx;
			for (i = 0; i < rounds; i++) {
				locm->event_burst_cnt -= num;
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 num, q_elem);
				check_local_queues();
				j += num;
			}
			if (left_over) {
				locm->event_burst_cnt = 0;
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 left_over, q_elem);
				check_local_queues();
			}
		} else {
			j = idx;
			for (i = 0; i < rounds; i++) {
				call_eo_receive_multi_fn(eo, eo_rcv_multi_fn,
							 &ev_tbl[j],
							 &ev_hdr_tbl[j],
							 num, q_elem);
				j += num;
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
dispatch_single_receive(em_event_t ev_tbl[], event_hdr_t *ev_hdr_tbl[],
			const int num_events, queue_elem_t *const q_elem,
			const bool check_local_qs)
{
	const em_eo_t eo = (em_eo_t)(uintptr_t)q_elem->eo;
	const em_receive_func_t eo_rcv_fn = q_elem->receive_func;
	int i;

	if (check_local_qs) {
		for (i = 0; i < num_events; i++) {
			em_locm.event_burst_cnt--;
			call_eo_receive_fn(eo, eo_rcv_fn,
					   ev_tbl[i], ev_hdr_tbl[i],
					   q_elem);
			check_local_queues();
		}
	} else {
		for (i = 0; i < num_events; i++)
			call_eo_receive_fn(eo, eo_rcv_fn,
					   ev_tbl[i], ev_hdr_tbl[i],
					   q_elem);
	}
}

/**
 * Dispatch events - call the EO-receive functions and pass the
 * events for processing
 */
static inline void
dispatch_events(odp_event_t odp_evtbl[], const int num_events,
		queue_elem_t *const q_elem)
{
	em_locm_t *const locm = &em_locm;
	const em_queue_type_t q_type = q_elem->type;
	em_sched_context_type_t sched_ctx_type = EM_SCHED_CONTEXT_TYPE_NONE;

	if (q_type == EM_QUEUE_TYPE_ATOMIC)
		sched_ctx_type = EM_SCHED_CONTEXT_TYPE_ATOMIC;
	else if (q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
		sched_ctx_type = EM_SCHED_CONTEXT_TYPE_ORDERED;

	locm->current.sched_context_type = sched_ctx_type;
	locm->current.sched_q_elem = q_elem;
	/* here: locm->current.egrp == EM_EVENT_GROUP_UNDEF */

	event_hdr_t *evhdr_tbl[num_events];
	em_event_t ev_tbl[num_events];

	/* Events might originate from outside of EM and need init */
	event_init_odp_multi(odp_evtbl, ev_tbl/*out*/, evhdr_tbl/*out*/,
			     num_events, true/*is_extev*/);

	/*
	 * Call the Execution Object (EO) receive function.
	 * Scheduling context may be released during this.
	 */
	if (q_elem->flags.use_multi_rcv)
		dispatch_multi_receive(ev_tbl, evhdr_tbl, num_events,
				       q_elem, true);
	else
		dispatch_single_receive(ev_tbl, evhdr_tbl, num_events,
					q_elem, true);

	/*
	 * Check for buffered events sent to output queues during the previous
	 * dispatch rounds. Currently buffered only for ordered sched context,
	 * use local var 'sched_ctx_type' since the type might have been changed
	 * from _ORDERED by 'em_ordered_processing_end()'.
	 */
	if (!EM_OUTPUT_QUEUE_IMMEDIATE &&
	    sched_ctx_type == EM_SCHED_CONTEXT_TYPE_ORDERED &&
	    locm->output_queue_track.idx_cnt > 0)
		output_queue_buffering_drain();

	locm->current.q_elem = NULL;
	locm->current.sched_q_elem = NULL;
	locm->current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE;
}

static inline void
dispatch_poll_ctrl_queue(void)
{
	const unsigned int poll_interval = em_shm->opt.dispatch.poll_ctrl_interval;

	/*
	 * Rate limit how often this core checks the unsched ctrl queue.
	 */

	if (poll_interval > 1) {
		em_locm_t *const locm = &em_locm;

		locm->dispatch_cnt--;
		if (locm->dispatch_cnt > 0)
			return;
		locm->dispatch_cnt = poll_interval;

		odp_time_t now = odp_time_global();
		odp_time_t period = odp_time_diff(now, locm->dispatch_last_run);
		odp_time_t poll_period = em_shm->opt.dispatch.poll_ctrl_interval_time;

		if (odp_time_cmp(period, poll_period) < 0)
			return;
		locm->dispatch_last_run = now;
	}

	/* Poll internal unscheduled ctrl queues */
	poll_unsched_ctrl_queue();
}

/*
 * Change the core state to idle and call idle hooks. If the core state changes,
 * call to_idle hooks. If the core state is already idle, call while_idle hooks.
 */
static inline void
to_idle(void)
{
	if (EM_IDLE_HOOKS_ENABLE) {
		em_locm_t *const locm = &em_locm;

		if (locm->idle_state == IDLE_STATE_ACTIVE) {
			call_idle_hooks_to_idle(0);
			locm->idle_state = IDLE_STATE_IDLE;
		} else if (locm->idle_state == IDLE_STATE_IDLE) {
			call_idle_hooks_while_idle();
		}
	}
}

/*
 * Change the core state to active and call idle hooks. If the core state
 * changes call to_active hooks. If the core state is already active no idle
 * hooks will be called.
 */
static inline void
to_active(void)
{
	if (EM_IDLE_HOOKS_ENABLE) {
		em_locm_t *const locm = &em_locm;

		if (locm->idle_state == IDLE_STATE_IDLE) {
			call_idle_hooks_to_active();
			locm->idle_state = IDLE_STATE_ACTIVE;
		}
	}
}

/*
 * Run a dispatch round - query the scheduler for events and dispatch
 */
static inline int
dispatch_round(void)
{
	odp_queue_t odp_queue;
	odp_event_t odp_evtbl[EM_SCHED_MULTI_MAX_BURST];
	int num;

	dispatch_poll_ctrl_queue();

	/*
	 * Schedule events to the core from queues
	 */
	if (EM_DEBUG_TIMESTAMP_ENABLE)
		em_locm.debug_ts[EM_DEBUG_TSP_SCHED_ENTRY] = debug_timestamp();

	if (EM_SCHED_WAIT_ENABLE) {
		num = odp_schedule_multi(&odp_queue, em_shm->opt.dispatch.sched_wait,
					 odp_evtbl, EM_SCHED_MULTI_MAX_BURST);
	} else {
		num = odp_schedule_multi_no_wait(&odp_queue, odp_evtbl,
						 EM_SCHED_MULTI_MAX_BURST);
	}

	if (EM_DEBUG_TIMESTAMP_ENABLE)
		em_locm.debug_ts[EM_DEBUG_TSP_SCHED_RETURN] = debug_timestamp();

	if (unlikely(num <= 0)) {
		/*
		 * No scheduled events available, check if the local queues
		 * contain anything on this core - e.g. pktio or something
		 * outside the dispatch-context might have sent to a local queue
		 * Update the EM_IDLE_STATE and call idle hooks if they are
		 * enabled
		 */
		if (em_locm.local_queues.empty) {
			to_idle();
		} else {
			to_active();
			check_local_queues();
		}
		return 0;
	}

	/* If scheduled events are available, update the EM_IDLE_STATE and call
	 * idle hooks if they are enabled
	 */
	to_active();

	queue_elem_t *const q_elem = odp_queue_context(odp_queue);
	const bool not_emq = !q_elem || (EM_CHECK_LEVEL > 2 &&
			     q_elem->valid_check != QUEUE_ELEM_VALID);

	if (unlikely(not_emq || q_elem->state != EM_QUEUE_STATE_READY)) {
		event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
		em_event_t ev_tbl[EM_SCHED_MULTI_MAX_BURST];

		event_init_odp_multi(odp_evtbl, ev_tbl/*out*/, ev_hdr_tbl/*out*/,
				     num, true/*is_extev*/);
		/* Drop all events dequeued from this queue */
		em_free_multi(ev_tbl, num);

		if (not_emq)
			INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_DISPATCH,
				       "Event(s) from non-EM Q, drop %d events", num);
		else
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_DISPATCH,
				       "Q:%" PRI_QUEUE " not ready, state=%d\n"
				       "    drop:%d event(s)\n",
				       q_elem->queue, q_elem->state, num);
		return 0;
	}

	if (q_elem->flags.in_atomic_group) {
		atomic_group_dispatch(odp_evtbl, num, q_elem);
	} else {
		em_locm.event_burst_cnt = num;
		dispatch_events(odp_evtbl, num, q_elem);
	}

	return num;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_DISPATCHER_H_ */
