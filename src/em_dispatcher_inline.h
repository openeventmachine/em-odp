/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2023, Nokia Solutions and Networks
 */

/**
 * @file
 * EM internal dispatcher functions
 */

#ifndef EM_DISPATCHER_INLINE_H_
#define EM_DISPATCHER_INLINE_H_

#include <event_machine/helper/event_machine_debug.h>

#ifdef __cplusplus
extern "C" {
#endif

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
	em_locm_t *const locm = &em_locm;
	odp_event_t odp_event;
	em_event_t event;
	event_hdr_t *ev_hdr;

	/* dst local queue */
	const int qidx = entry.qidx;
	const em_queue_t queue = queue_idx2hdl(qidx);
	queue_elem_t *const q_elem = queue_elem_get(queue);

	locm->current.q_elem = q_elem; /* before event_init_... for ESV error prints */

	odp_event = (odp_event_t)(uintptr_t)entry.evptr;
	/* Event might originate from outside (via polled pktio) of EM and need init */
	event = event_init_odp(odp_event, true/*is_extev*/, &ev_hdr/*out*/);

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
	em_locm_t *const locm = &em_locm;
	int idx = 0; /* index into ev_tbl[] & ev_hdr_tbl[] */
	int ev_cnt;  /* number of events to the same local-queue */

	odp_event_t odp_evtbl[num];
	em_event_t ev_tbl[num];
	event_hdr_t *evhdr_tbl[num];

	for (int i = 0; i < num; i++)
		odp_evtbl[i] = (odp_event_t)(uintptr_t)entry_tbl[i].evptr;

	/* Loop through 'num' events and dispatch in batches to local queues */
	do {
		/* dst local queue */
		const int qidx = entry_tbl[idx].qidx;
		const em_queue_t queue = queue_idx2hdl(qidx);
		queue_elem_t *const q_elem = queue_elem_get(queue);
		int i;

		locm->current.q_elem = q_elem; /* before event_init_... for ESV error prints */

		/*
		 * Count events sent to the same local queue,
		 * i < num <= EM_QUEUE_LOCAL_MULTI_MAX_BURST
		 */
		for (i = idx + 1; i < num && entry_tbl[i].qidx == qidx; i++)
			;

		ev_cnt = i - idx; /* '1 to num' events */

		/* Events might originate from outside (via polled pktio) of EM and need init */
		event_init_odp_multi(&odp_evtbl[idx], ev_tbl/*out*/, evhdr_tbl/*out*/,
				     ev_cnt, true/*is_extev*/);

		if (unlikely(q_elem == NULL || q_elem->state != EM_QUEUE_STATE_READY)) {
			em_free_multi(ev_tbl, ev_cnt);
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
			dispatch_multi_receive(ev_tbl, evhdr_tbl, ev_cnt, q_elem, false);
		else
			dispatch_single_receive(ev_tbl, evhdr_tbl, ev_cnt, q_elem, false);

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
	em_locm_t *const locm = &em_locm;

	if (locm->local_queues.empty)
		return;

	/*
	 * Check if the previous EO receive function sent events to a
	 * local queue ('EM_QUEUE_TYPE_LOCAL') - and if so, dispatch
	 * those events immediately.
	 */
	stash_entry_t entry_tbl[EM_QUEUE_LOCAL_MULTI_MAX_BURST];

	for (;;) {
		if (EM_DEBUG_TIMESTAMP_ENABLE)
			locm->debug_ts[EM_DEBUG_TSP_SCHED_ENTRY] = debug_timestamp();

		int num = next_local_queue_events(entry_tbl /*[out]*/,
						  EM_QUEUE_LOCAL_MULTI_MAX_BURST);
		if (EM_DEBUG_TIMESTAMP_ENABLE)
			locm->debug_ts[EM_DEBUG_TSP_SCHED_RETURN] = debug_timestamp();

		if (num <= 0)
			break;

		dispatch_local_queues(entry_tbl, num);
	}

	/* Restore */
	locm->current.q_elem = locm->current.sched_q_elem;
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
	em_locm_t *const locm = &em_locm;
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
	em_locm_t *const locm = &em_locm;
	const em_eo_t eo = (em_eo_t)(uintptr_t)q_elem->eo;
	const em_receive_func_t eo_rcv_fn = q_elem->receive_func;
	int i;

	if (check_local_qs) {
		for (i = 0; i < num_events; i++) {
			locm->event_burst_cnt--;
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
	locm->current.q_elem = q_elem; /* before event_init_... for ESV error prints */
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
to_idle(const em_dispatch_opt_t *opt)
{
	if (EM_IDLE_HOOKS_ENABLE) {
		em_locm_t *const locm = &em_locm;

		if (locm->idle_state == IDLE_STATE_ACTIVE) {
			uint64_t to_idle_delay_ns = 0;

			if (EM_DEBUG_TIMESTAMP_ENABLE) {
				to_idle_delay_ns = debug_timestamp() -
						   locm->debug_ts[EM_DEBUG_TSP_SCHED_ENTRY];
			} else if (EM_SCHED_WAIT_ENABLE) {
				to_idle_delay_ns = opt ? opt->wait_ns :
						   em_shm->opt.dispatch.sched_wait_ns;
			} else if (opt) {
				to_idle_delay_ns = opt->wait_ns;
			}

			call_idle_hooks_to_idle(to_idle_delay_ns);
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

/**
 * @brief Dispatcher calls the scheduler and request events for preocessing
 */
static inline int
dispatch_schedule(odp_queue_t *odp_queue /*out*/, uint64_t sched_wait,
		  odp_event_t odp_evtbl[/*out*/], int num)
{
	int ret;

	if (EM_DEBUG_TIMESTAMP_ENABLE)
		em_locm.debug_ts[EM_DEBUG_TSP_SCHED_ENTRY] = debug_timestamp();

	ret = odp_schedule_multi(odp_queue, sched_wait, odp_evtbl, num);

	if (EM_DEBUG_TIMESTAMP_ENABLE)
		em_locm.debug_ts[EM_DEBUG_TSP_SCHED_RETURN] = debug_timestamp();

	return ret;
}

static inline bool
is_invalid_queue(const queue_elem_t *const q_elem)
{
	const bool not_emq = !q_elem || (EM_CHECK_LEVEL > 2 &&
			     q_elem->valid_check != QUEUE_ELEM_VALID);

	if (unlikely(not_emq || q_elem->state != EM_QUEUE_STATE_READY)) {
		if (not_emq)
			INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_DISPATCH,
				       "Drop event(s) from non-EM Q");
		else
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_DISPATCH,
				       "Drop event(s) from Q:%" PRI_QUEUE ": not ready, state=%d",
				       q_elem->queue, q_elem->state);
		return true; /* invalid queue */
	}

	return false; /* not invalid, i.e. a valid queue */
}

static inline void
free_invalid_events(odp_event_t odp_evtbl[], int num)
{
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	em_event_t ev_tbl[EM_SCHED_MULTI_MAX_BURST];

	event_init_odp_multi(odp_evtbl, ev_tbl/*out*/, ev_hdr_tbl/*out*/,
			     num, true/*is_extev*/);
	em_free_multi(ev_tbl, num);
}

/*
 * Run a dispatch round - query the scheduler for events and dispatch
 */
static inline int
dispatch_round(uint64_t sched_wait, uint16_t burst_size,
	       const em_dispatch_opt_t *opt /*optional, can be NULL*/)
{
	odp_queue_t odp_queue;
	odp_event_t odp_evtbl[burst_size];
	int num;

	dispatch_poll_ctrl_queue();

	num = dispatch_schedule(&odp_queue/*out*/, sched_wait,
				odp_evtbl/*out[]*/, burst_size);
	if (unlikely(num <= 0)) {
		/*
		 * No scheduled events available, check if the local queues
		 * contain anything on this core - e.g. pktio or something
		 * outside the dispatch-context might have sent to a local queue
		 * Update the EM_IDLE_STATE and call idle hooks if they are
		 * enabled
		 */
		if (em_locm.local_queues.empty) {
			to_idle(opt);
		} else {
			to_active();
			check_local_queues();
		}
		return 0;
	}

	queue_elem_t *const q_elem = odp_queue_context(odp_queue);

	if (unlikely(is_invalid_queue(q_elem))) {
		/* Free all events from an invalid queue */
		free_invalid_events(odp_evtbl, num);
		return 0;
	}

	/*
	 * If scheduled events are available, update the EM_IDLE_STATE and
	 * call idle hooks if they are enabled.
	 */
	to_active();

	if (q_elem->flags.in_atomic_group) {
		atomic_group_dispatch(odp_evtbl, num, q_elem);
	} else {
		em_locm.event_burst_cnt = num;
		dispatch_events(odp_evtbl, num, q_elem);
	}

	return num;
}

/*
 * em_dispatch() helper: check if the user provided callback functions
 *			 'input_poll' and 'output_drain' should be called in
 *			 this dispatch round
 */
static inline bool
check_poll_drain_round(unsigned int interval, odp_time_t poll_drain_period)
{
	if (interval > 1) {
		em_locm_t *const locm = &em_locm;

		locm->poll_drain_dispatch_cnt--;
		if (locm->poll_drain_dispatch_cnt == 0) {
			odp_time_t now = odp_time_global();
			odp_time_t period;

			period = odp_time_diff(now, locm->poll_drain_dispatch_last_run);
			locm->poll_drain_dispatch_cnt = interval;

			if (odp_time_cmp(poll_drain_period, period) < 0) {
				locm->poll_drain_dispatch_last_run = now;
				return true;
			}
		}
	} else {
		return true;
	}
	return false;
}

/*
 * em_dispatch() helper: dispatch and call the user provided callback functions
 *                       'input_poll' and 'output_drain'
 */
static inline uint64_t
dispatch_with_userfn(uint64_t rounds, bool do_input_poll, bool do_output_drain)
{
	const bool do_forever = rounds == 0 ? true : false;
	const em_input_poll_func_t input_poll = em_shm->conf.input.input_poll_fn;
	const em_output_drain_func_t output_drain = em_shm->conf.output.output_drain_fn;
	const unsigned int poll_interval = em_shm->opt.dispatch.poll_drain_interval;
	const odp_time_t poll_period = em_shm->opt.dispatch.poll_drain_interval_time;
	const uint64_t sched_wait = EM_SCHED_WAIT_ENABLE ?
				    em_shm->opt.dispatch.sched_wait : ODP_SCHED_NO_WAIT;
	int rx_events = 0;
	uint64_t events = 0;
	int dispatched_events;
	int round_events;
	bool do_poll_drain_round;

	for (uint64_t i = 0; do_forever || i < rounds;) {
		dispatched_events = 0;

		do_poll_drain_round = check_poll_drain_round(poll_interval, poll_period);

		if (do_input_poll && do_poll_drain_round)
			rx_events = input_poll();

		do {
			round_events = dispatch_round(sched_wait, EM_SCHED_MULTI_MAX_BURST, NULL);
			dispatched_events += round_events;
			i++; /* inc rounds */
		} while (dispatched_events < rx_events &&
			 round_events > 0 && (do_forever || i < rounds));

		events += dispatched_events; /* inc ret value*/
		if (do_output_drain && do_poll_drain_round)
			(void)output_drain();
	}

	return events;
}

/*
 * em_dispatch() helper: dispatch without calling any user provided callbacks
 */
static inline uint64_t
dispatch_no_userfn(uint64_t rounds)
{
	const bool do_forever = rounds == 0 ? true : false;
	const uint64_t sched_wait = EM_SCHED_WAIT_ENABLE ?
				    em_shm->opt.dispatch.sched_wait : ODP_SCHED_NO_WAIT;
	uint64_t events = 0;

	if (do_forever) {
		for (;/*ever*/;)
			dispatch_round(sched_wait, EM_SCHED_MULTI_MAX_BURST, NULL);
	} else {
		for (uint64_t i = 0; i < rounds; i++)
			events += dispatch_round(sched_wait, EM_SCHED_MULTI_MAX_BURST, NULL);
	}

	return events;
}

/*
 * em_dispatch() helper: dispatch and call the user provided callback functions
 *                       'input_poll' and 'output_drain'
 */
static inline uint64_t
dispatch_duration_with_userfn(const em_dispatch_duration_t *duration,
			      const em_dispatch_opt_t *opt,
			      em_dispatch_results_t *results /*out*/,
			      const bool do_input_poll, const bool do_output_drain)
{
	const em_input_poll_func_t input_poll = em_shm->conf.input.input_poll_fn;
	const em_output_drain_func_t output_drain = em_shm->conf.output.output_drain_fn;
	const unsigned int poll_interval = em_shm->opt.dispatch.poll_drain_interval;
	const odp_time_t poll_period = em_shm->opt.dispatch.poll_drain_interval_time;
	const uint64_t sched_wait = odp_schedule_wait_time(opt->wait_ns);
	const uint16_t burst_size = opt->burst_size;
	bool do_poll_drain_round = false;

	const bool duration_forever =
		duration->select == EM_DISPATCH_DURATION_FOREVER ? true : false;
	const bool duration_rounds =
		duration->select & EM_DISPATCH_DURATION_ROUNDS ? true : false;
	const bool duration_ns =
		duration->select & EM_DISPATCH_DURATION_NS ? true : false;
	const bool duration_events =
		duration->select & EM_DISPATCH_DURATION_EVENTS ? true : false;
	const bool duration_noev_rounds =
		duration->select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS ? true : false;
	const bool duration_noev_ns =
		duration->select & EM_DISPATCH_DURATION_NO_EVENTS_NS ? true : false;

	if (unlikely(duration_forever)) {
		for (;/*ever*/;) {
			/* check if callback functions should be called */
			do_poll_drain_round = check_poll_drain_round(poll_interval, poll_period);

			if (do_input_poll && do_poll_drain_round)
				(void)input_poll();

			/* dispatch one round */
			(void)dispatch_round(sched_wait, burst_size, opt);

			if (do_output_drain && do_poll_drain_round)
				(void)output_drain();
		}
		/* never return */
	}

	uint64_t events = 0;
	uint64_t rounds = 0;
	uint64_t noev_rounds = 0;

	uint64_t start_ns = 0;
	uint64_t stop_ns = 0;
	uint64_t noev_stop_ns = 0;
	uint64_t time_ns = 0;

	if (duration_ns || duration_noev_ns) {
		start_ns = odp_time_local_ns();
		stop_ns = start_ns + duration->ns;
		noev_stop_ns = start_ns + duration->no_events.ns;
		time_ns = start_ns;
	}

	while ((!duration_rounds || rounds < duration->rounds) &&
	       (!duration_ns || time_ns < stop_ns) &&
	       (!duration_events || events < duration->events) &&
	       (!duration_noev_rounds || noev_rounds < duration->no_events.rounds) &&
	       (!duration_noev_ns || time_ns < noev_stop_ns)) {
		/* check if callback functions should be called */
		do_poll_drain_round = check_poll_drain_round(poll_interval, poll_period);

		if (do_input_poll && do_poll_drain_round)
			(void)input_poll();

		/* dispatch one round */
		int round_events = dispatch_round(sched_wait, burst_size, opt);

		events += round_events;
		rounds++;

		if (do_output_drain && do_poll_drain_round)
			(void)output_drain();

		if (duration_noev_rounds) {
			if (round_events == 0)
				noev_rounds++;
			else
				noev_rounds = 0;
		}

		if (duration_ns || duration_noev_ns) {
			time_ns = odp_time_local_ns();

			if (duration_noev_ns && round_events > 0)
				noev_stop_ns = time_ns + duration->no_events.ns;
		}
	}

	if (results) {
		results->rounds = rounds;
		if (duration_ns || duration_noev_ns)
			results->ns = time_ns - start_ns;
		else
			results->ns = 0;
		results->events = events;
	}

	return events;
}

static inline uint64_t
dispatch_duration_no_userfn(const em_dispatch_duration_t *duration,
			    const em_dispatch_opt_t *opt,
			    em_dispatch_results_t *results/*out*/)
{
	const uint64_t sched_wait = odp_schedule_wait_time(opt->wait_ns);
	const uint16_t burst_size = opt->burst_size;

	const bool duration_forever =
		duration->select == EM_DISPATCH_DURATION_FOREVER ? true : false;
	const bool duration_rounds =
		duration->select & EM_DISPATCH_DURATION_ROUNDS ? true : false;
	const bool duration_ns =
		duration->select & EM_DISPATCH_DURATION_NS ? true : false;
	const bool duration_events =
		duration->select & EM_DISPATCH_DURATION_EVENTS ? true : false;
	const bool duration_noev_rounds =
		duration->select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS ? true : false;
	const bool duration_noev_ns =
		duration->select & EM_DISPATCH_DURATION_NO_EVENTS_NS ? true : false;

	if (unlikely(duration_forever)) {
		for (;/*ever*/;)
			(void)dispatch_round(sched_wait, burst_size, opt);
		/* never return */
	}

	uint64_t events = 0;
	uint64_t rounds = 0;
	uint64_t noev_rounds = 0;

	uint64_t start_ns = 0;
	uint64_t stop_ns = 0;
	uint64_t noev_stop_ns = 0;
	uint64_t time_ns = 0;

	if (duration_ns || duration_noev_ns) {
		start_ns = odp_time_local_ns();
		stop_ns = start_ns + duration->ns;
		noev_stop_ns = start_ns + duration->no_events.ns;
		time_ns = start_ns;
	}

	while ((!duration_rounds || rounds < duration->rounds) &&
	       (!duration_ns || time_ns < stop_ns) &&
	       (!duration_events || events < duration->events) &&
	       (!duration_noev_rounds || noev_rounds < duration->no_events.rounds) &&
	       (!duration_noev_ns || time_ns < noev_stop_ns)) {
		/* dispatch one round */
		int round_events = dispatch_round(sched_wait, burst_size, opt);

		events += round_events;
		rounds++;

		if (duration_noev_rounds) {
			if (round_events == 0)
				noev_rounds++;
			else
				noev_rounds = 0;
		}

		if (duration_ns || duration_noev_ns) {
			time_ns = odp_time_local_ns();
			if (duration_noev_ns && round_events > 0)
				noev_stop_ns = time_ns + duration->no_events.ns;
		}
	}

	if (results) {
		results->rounds = rounds;
		if (duration_ns || duration_noev_ns)
			results->ns = time_ns - start_ns;
		else
			results->ns = 0;
		results->events = events;
	}

	return events;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_DISPATCHER_INLINE_H_ */
