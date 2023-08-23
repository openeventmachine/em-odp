/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2014-2016, Nokia Solutions and Networks
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
 *
 * EM Internal Control
 */

#include "em_include.h"

/* doc in header file */
em_status_t create_ctrl_queues(void)
{
	const int num_cores = em_core_count();
	char q_name[EM_QUEUE_NAME_LEN];
	em_queue_t shared_unsched_queue;
	em_queue_t queue;
	em_queue_conf_t unsch_conf;

	const char *err_str = "";

	EM_DBG("%s()\n", __func__);

	/*
	 * Create shared internal unsched queue used for internal EM messaging.
	 * Cannot use em_queue_create_static() here since the requested handle
	 * 'SHARED_INTERNAL_UNSCHED_QUEUE' lies outside of the normal static
	 * range.
	 */
	shared_unsched_queue = queue_id2hdl(SHARED_INTERNAL_UNSCHED_QUEUE);
	queue = queue_create("EMctrl-unschedQ-shared", EM_QUEUE_TYPE_UNSCHEDULED,
			     EM_QUEUE_PRIO_UNDEF, EM_QUEUE_GROUP_UNDEF,
			     shared_unsched_queue, EM_ATOMIC_GROUP_UNDEF,
			     NULL /* use default queue config */, &err_str);
	if (queue == EM_QUEUE_UNDEF || queue != shared_unsched_queue)
		return EM_FATAL(EM_ERR_NOT_FREE);

	/*
	 * Create static internal per-core UNSCHEDULED queues used for
	 * internal EM messaging. Cannot use em_queue_create_static()
	 * here since the requested handles lies outside of the normal
	 * static range.
	 */
	memset(&unsch_conf, 0, sizeof(unsch_conf));
	unsch_conf.flags |= EM_QUEUE_FLAG_DEQ_NOT_MTSAFE;

	for (int i = 0; i < num_cores; i++) {
		em_queue_t queue_req;

		queue_req = queue_id2hdl(FIRST_INTERNAL_UNSCHED_QUEUE + i);
		snprintf(q_name, sizeof(q_name), "EMctrl-unschedQ-core%d", i);
		q_name[EM_QUEUE_NAME_LEN - 1] = '\0';

		queue = queue_create(q_name, EM_QUEUE_TYPE_UNSCHEDULED,
				     EM_QUEUE_PRIO_UNDEF, EM_QUEUE_GROUP_UNDEF,
				     queue_req, EM_ATOMIC_GROUP_UNDEF,
				     &unsch_conf, /* request deq-not-mtsafe */
				     &err_str);
		if (unlikely(queue == EM_QUEUE_UNDEF || queue != queue_req))
			return EM_FATAL(EM_ERR_NOT_FREE);
	}

	return EM_OK;
}

/* doc in header file */
em_status_t delete_ctrl_queues(void)
{
	const int num_cores = em_core_count();
	em_queue_t unsched_queue;
	em_event_t unsched_event;
	em_status_t stat;

	unsched_queue = queue_id2hdl(SHARED_INTERNAL_UNSCHED_QUEUE);
	for (;/* flush unsched queue */;) {
		unsched_event = em_queue_dequeue(unsched_queue);
		if (unsched_event == EM_EVENT_UNDEF)
			break;
		em_free(unsched_event);
	}
	stat = em_queue_delete(unsched_queue);
	if (unlikely(stat != EM_OK))
		return INTERNAL_ERROR(stat, EM_ESCOPE_DELETE_CTRL_QUEUES,
				      "shared unschedQ delete");

	for (int i = 0; i < num_cores; i++) {
		unsched_queue = queue_id2hdl(FIRST_INTERNAL_UNSCHED_QUEUE + i);

		for (;/* flush unsched queue */;) {
			unsched_event = em_queue_dequeue(unsched_queue);
			if (unsched_event == EM_EVENT_UNDEF)
				break;
			em_free(unsched_event);
		}

		stat = em_queue_delete(unsched_queue);
		if (unlikely(stat != EM_OK))
			return INTERNAL_ERROR(stat, EM_ESCOPE_DELETE_CTRL_QUEUES,
					      "core unschedQ:%d delete", i);
	}

	return stat;
}

/* doc in header file */
int send_core_ctrl_events(const em_core_mask_t *const mask, em_event_t ctrl_event,
			  void (*f_done_callback)(void *arg_ptr),
			  void *f_done_arg_ptr,
			  int num_notif, const em_notif_t notif_tbl[],
			  bool sync_operation)
{
	em_status_t err;
	em_event_group_t event_group = EM_EVENT_GROUP_UNDEF;
	const internal_event_t *i_event  = em_event_pointer(ctrl_event);
	const int core_count = em_core_count(); /* All running EM cores */
	const int mask_count = em_core_mask_count(mask); /* Subset of cores*/
	int alloc_count = 0;
	int sent_count = 0;
	int unsent_count = mask_count;
	int first_qidx;
	int i;
	em_event_t events[mask_count];

	if (unlikely(num_notif > EM_EVENT_GROUP_MAX_NOTIF)) {
		INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_INTERNAL_NOTIF,
			       "Too large notif table (%i)", num_notif);
		return unsent_count;
	}

	/*
	 * Set up internal notification when all cores are done.
	 */
	event_group = internal_done_w_notif_req(mask_count /*=evgrp count*/,
						f_done_callback, f_done_arg_ptr,
						num_notif, notif_tbl,
						sync_operation);
	if (unlikely(event_group == EM_EVENT_GROUP_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_NOT_FREE, EM_ESCOPE_INTERNAL_NOTIF,
			       "Internal 'done' notif setup failed");
		return unsent_count;
	}

	/*
	 * Allocate ctrl events to be sent to the concerned cores.
	 * Reuse the input ctrl_event later so alloc one less.
	 * Copy content from input ctrl_event into all allocated events.
	 */
	for (i = 0; i < mask_count - 1; i++) {
		events[i] = em_alloc(sizeof(internal_event_t),
				     EM_EVENT_TYPE_SW,
				     EM_POOL_DEFAULT);
		if (unlikely(events[i] == EM_EVENT_UNDEF)) {
			INTERNAL_ERROR(EM_ERR_ALLOC_FAILED,
				       EM_ESCOPE_INTERNAL_NOTIF,
				       "Internal event alloc failed");
			goto err_free_resources;
		}
		alloc_count++;

		internal_event_t *i_event_tmp = em_event_pointer(events[i]);
		/* Copy input event content */
		*i_event_tmp = *i_event;
	}
	/* Reuse the input event */
	events[i] = ctrl_event;
	/* don't increment alloc_count++, caller frees input event on error */

	/*
	 * Send ctrl events to the concerned cores
	 */
	first_qidx = queue_id2idx(FIRST_INTERNAL_UNSCHED_QUEUE);

	for (i = 0; i < core_count; i++) {
		if (em_core_mask_isset(i, mask)) {
			/*
			 * Send copy to each core-specific queue,
			 * track completion using an event group.
			 */
			err = em_send_group(events[sent_count],
					    queue_idx2hdl(first_qidx + i),
					    event_group);
			if (unlikely(err != EM_OK)) {
				INTERNAL_ERROR(err, EM_ESCOPE_INTERNAL_NOTIF,
					       "Event group send failed");
				goto err_free_resources;
			}
			sent_count++;
			unsent_count--;
		}
	}

	return 0; /* Success, all ctrl events sent */

	/* Error handling, free resources */
err_free_resources:
	for (i = sent_count; i < alloc_count; i++)
		em_free(events[i]);
	evgrp_abort_delete(event_group);
	return unsent_count;
}

/**
 * Handle the internal 'done' event
 */
static void i_event__internal_done(const internal_event_t *i_ev)
{
	int num_notif;
	em_status_t ret;

	/* Release the event group, we are done with it */
	ret = em_event_group_delete(i_ev->done.event_group);

	if (unlikely(ret != EM_OK))
		INTERNAL_ERROR(ret, EM_ESCOPE_EVENT_INTERNAL_DONE,
			       "Event group %" PRI_EGRP " delete failed (ret=%u)",
			       i_ev->done.event_group, ret);

	/* Call the callback function, performs custom actions at 'done' */
	if (i_ev->done.f_done_callback != NULL)
		i_ev->done.f_done_callback(i_ev->done.f_done_arg_ptr);

	/*
	 * Send notification events if requested by the caller.
	 */
	num_notif = i_ev->done.num_notif;

	if (num_notif > 0) {
		ret = send_notifs(num_notif, i_ev->done.notif_tbl);
		if (unlikely(ret != EM_OK))
			INTERNAL_ERROR(ret, EM_ESCOPE_EVENT_INTERNAL_DONE,
				       "em_send() of notifs(%d) failed",
				       num_notif);
	}
}

/**
 * Handle internal ctrl events
 */
static inline void
internal_event_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		       em_queue_t queue, void *q_ctx)
{
	/* currently unused args */
	(void)eo_ctx;
	(void)type;
	(void)q_ctx;

	internal_event_t *i_event = em_event_pointer(event);

	if (unlikely(!i_event)) {
		if (event != EM_EVENT_UNDEF)
			em_free(event); /* unrecognized odp event type? */
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_INTERNAL_EVENT_RECV_FUNC,
			       "Q:%" PRI_QUEUE ": Invalid event, evptr NULL", queue);
		return;
	}

	switch (i_event->id) {
	/*
	 * Internal Done event
	 */
	case EM_INTERNAL_DONE:
		i_event__internal_done(i_event);
		break;

	/*
	 * Internal event related to Queue Group modification: add a core
	 */
	case QUEUE_GROUP_ADD_REQ:
		i_event__qgrp_add_core_req(i_event);
		break;

	/*
	 * Internal event related to Queue Group modification: remove a core
	 */
	case QUEUE_GROUP_REM_REQ:
		i_event__qgrp_rem_core_req(i_event);
		break;
	/*
	 * Internal events related to EO local start&stop functionality
	 */
	case EO_START_LOCAL_REQ:
	case EO_START_SYNC_LOCAL_REQ:
	case EO_STOP_LOCAL_REQ:
	case EO_STOP_SYNC_LOCAL_REQ:
	case EO_REM_QUEUE_LOCAL_REQ:
	case EO_REM_QUEUE_SYNC_LOCAL_REQ:
	case EO_REM_QUEUE_ALL_LOCAL_REQ:
	case EO_REM_QUEUE_ALL_SYNC_LOCAL_REQ:
		i_event__eo_local_func_call_req(i_event);
		break;

	default:
		INTERNAL_ERROR(EM_ERR_BAD_ID,
			       EM_ESCOPE_INTERNAL_EVENT_RECV_FUNC,
			       "Internal ev-id:0x%" PRIx64 " Q:%" PRI_QUEUE "",
			       i_event->id, queue);
		break;
	}

	i_event->id = 0;
	em_free(event);
}

/* doc in header file */
em_event_group_t internal_done_w_notif_req(int event_group_count,
					   void (*f_done_callback)(void *arg_ptr),
					   void  *f_done_arg_ptr,
					   int num_notif, const em_notif_t notif_tbl[],
					   bool sync_operation)
{
	em_event_group_t event_group;
	em_event_t event;
	internal_event_t *i_event;
	em_notif_t i_notif;
	em_status_t err;

	event = em_alloc(sizeof(internal_event_t), EM_EVENT_TYPE_SW,
			 EM_POOL_DEFAULT);
	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED,
			       EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
			       "Internal event 'DONE' alloc failed!");
		return EM_EVENT_GROUP_UNDEF;
	}

	event_group = em_event_group_create();
	if (unlikely(event_group == EM_EVENT_GROUP_UNDEF)) {
		em_free(event);
		INTERNAL_ERROR(EM_ERR_NOT_FREE,
			       EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
			       "Event group create failed!");
		return EM_EVENT_GROUP_UNDEF;
	}

	i_event = em_event_pointer(event);
	i_event->id = EM_INTERNAL_DONE;
	i_event->done.event_group = event_group;
	i_event->done.f_done_callback = f_done_callback;
	i_event->done.f_done_arg_ptr = f_done_arg_ptr;
	i_event->done.num_notif = num_notif;

	for (int i = 0; i < num_notif; i++) {
		i_event->done.notif_tbl[i].event = notif_tbl[i].event;
		i_event->done.notif_tbl[i].queue = notif_tbl[i].queue;
		i_event->done.notif_tbl[i].egroup = notif_tbl[i].egroup;
	}

	i_notif.event = event;
	if (sync_operation) {
		i_notif.queue = queue_id2hdl(FIRST_INTERNAL_UNSCHED_QUEUE +
					     em_core_id());
	} else {
		i_notif.queue = queue_id2hdl(SHARED_INTERNAL_UNSCHED_QUEUE);
	}
	i_notif.egroup = EM_EVENT_GROUP_UNDEF;

	/*
	 * Request sending of EM_INTERNAL_DONE when 'event_group_count' events
	 * in 'event_group' have been seen. The 'Done' event will trigger the
	 * notifications to be sent.
	 */
	err = em_event_group_apply(event_group, event_group_count,
				   1, &i_notif);
	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
			       "Event group apply failed");
		em_free(event);
		(void)em_event_group_delete(event_group);
		return EM_EVENT_GROUP_UNDEF;
	}

	return event_group;
}

/* doc in header file */
void evgrp_abort_delete(em_event_group_t event_group)
{
	em_notif_t free_notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];

	int num = em_event_group_get_notif(event_group,
					   EM_EVENT_GROUP_MAX_NOTIF,
					   free_notif_tbl);
	em_status_t err = em_event_group_abort(event_group);

	if (err == EM_OK && num > 0) {
		for (int i = 0; i < num; i++)
			em_free(free_notif_tbl[i].event);
	}
	(void)em_event_group_delete(event_group);
}

/* doc in header file */
em_status_t send_notifs(const int num_notif, const em_notif_t notif_tbl[])
{
	em_status_t err;
	em_status_t ret = EM_OK;

	for (int i = 0; i < num_notif; i++) {
		const em_event_t event = notif_tbl[i].event;
		const em_queue_t queue = notif_tbl[i].queue;
		const em_event_group_t egrp = notif_tbl[i].egroup;

		/* 'egroup' may be uninit in old appl code, check */
		if (invalid_egrp(egrp))
			err = em_send(event, queue);
		else
			err = em_send_group(event, queue, egrp);

		if (unlikely(err != EM_OK)) {
			em_free(event);
			if (ret == EM_OK)
				ret = err; /* return the first error */
		}
	}

	return ret;
}

/* doc in header file */
em_status_t check_notif(const em_notif_t *const notif)
{
	if (unlikely(notif == NULL || notif->event == EM_EVENT_UNDEF))
		return EM_ERR_BAD_POINTER;

	const bool is_external = queue_external(notif->queue);

	if (!is_external) {
		const queue_elem_t *q_elem = queue_elem_get(notif->queue);

		if (unlikely(q_elem == NULL || !queue_allocated(q_elem)))
			return EM_ERR_NOT_FOUND;
	}

	if (notif->egroup != EM_EVENT_GROUP_UNDEF) {
		const event_group_elem_t *egrp_elem =
			event_group_elem_get(notif->egroup);

		if (unlikely(egrp_elem == NULL ||
			     !event_group_allocated(egrp_elem)))
			return EM_ERR_BAD_ID;
	}

	return EM_OK;
}

/* doc in header file */
em_status_t check_notif_tbl(const int num_notif, const em_notif_t notif_tbl[])
{
	em_status_t err;

	if (unlikely((unsigned int)num_notif > EM_EVENT_GROUP_MAX_NOTIF))
		return EM_ERR_TOO_LARGE;

	if (unlikely(num_notif > 0 && notif_tbl == NULL))
		return EM_ERR_BAD_POINTER;

	for (int i = 0; i < num_notif; i++) {
		err = check_notif(&notif_tbl[i]);
		if (unlikely(err != EM_OK))
			return err;
	}

	return EM_OK;
}

/**
 * @brief Helper for poll_unsched_ctrl_queue()
 */
static inline void
handle_ctrl_events(em_queue_t unsched_queue,
		   const em_event_t ev_tbl[], const int num)
{
	em_locm_t *const locm = &em_locm;
	event_hdr_t *evhdr_tbl[num];

	event_to_hdr_multi(ev_tbl, evhdr_tbl/*out*/, num);

	for (int i = 0; i < num; i++) {
		/*
		 * Simulate a dispatch-round for the core-local ctrl event.
		 * Dispatch an unscheduled event as scheduled, be careful!
		 * Don't call dispatch enter/exit callbacks here.
		 */
		em_event_t event = ev_tbl[i];
		const event_hdr_t *ev_hdr = evhdr_tbl[i];
		em_event_type_t event_type = ev_hdr->event_type;

		/* Check and set core local event group */
		event_group_set_local(ev_hdr->egrp, ev_hdr->egrp_gen, 1);

		internal_event_receive(NULL, event, event_type,
				       unsched_queue, NULL);

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
}

/* doc in header file */
void poll_unsched_ctrl_queue(void)
{
	em_locm_t *const locm = &em_locm;

	queue_elem_t *core_unsch_qelem = locm->sync_api.ctrl_poll.core_unsched_qelem;
	em_queue_t core_unsched_queue = locm->sync_api.ctrl_poll.core_unsched_queue;

	queue_elem_t *shared_unsch_qelem = locm->sync_api.ctrl_poll.shared_unsched_qelem;
	em_queue_t shared_unsched_queue = locm->sync_api.ctrl_poll.shared_unsched_queue;

	em_locm_current_t current;

	const int deq_max = 16;
	em_event_t core_ev_tbl[deq_max];
	em_event_t shared_ev_tbl[deq_max];
	int core_num;
	int shared_num;
	int round = 0;

	do {
		core_num = queue_dequeue_multi(core_unsch_qelem,
					       core_ev_tbl/*out*/, deq_max);
		shared_num = queue_dequeue_multi(shared_unsch_qelem,
						 shared_ev_tbl/*out*/, deq_max);
		if (core_num <= 0 && shared_num <= 0)
			break; /* no ctrl events, exit loop */

		/* Save local current state the first time only */
		if (round == 0) {
			current = locm->current; /* save */
			locm->current.rcv_multi_cnt = 1;
			locm->current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE;
		}

		if (core_num > 0) {
			locm->current.q_elem = core_unsch_qelem;
			locm->current.sched_q_elem = core_unsch_qelem;
			handle_ctrl_events(core_unsched_queue, core_ev_tbl, core_num);
		}
		if (shared_num > 0) {
			locm->current.q_elem = shared_unsch_qelem;
			locm->current.sched_q_elem = shared_unsch_qelem;
			handle_ctrl_events(shared_unsched_queue, shared_ev_tbl, shared_num);
		}

		round++;
	} while (true);

	if (round > 0)
		locm->current = current; /* restore */
}
