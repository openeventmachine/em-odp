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

#include "em_include.h"

/* per core (thread) state for em_event_group_get_next() */
static ENV_LOCAL unsigned int _egrp_tbl_iter_idx;

em_event_group_t
em_event_group_create(void)
{
	em_event_group_t egrp;
	event_group_elem_t *egrp_elem;

	egrp = event_group_alloc();
	if (unlikely(egrp == EM_EVENT_GROUP_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_CREATE,
			       "Event group alloc failed!");
		return EM_EVENT_GROUP_UNDEF;
	}

	egrp_elem = event_group_elem_get(egrp);

	/* Alloc succeeded, return event group handle */
	egrp_elem->ready = 1; /* Set group ready to be applied */
	return egrp;
}

em_status_t
em_event_group_delete(em_event_group_t event_group)
{
	em_status_t status;
	event_group_elem_t *const egrp_elem =
		event_group_elem_get(event_group);
	egrp_counter_t egrp_count;
	uint64_t count;

	RETURN_ERROR_IF(egrp_elem == NULL || !event_group_allocated(egrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_DELETE,
			"Invalid event group: %" PRI_EGRP "", event_group);

	egrp_count.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);

	if (EM_EVENT_GROUP_SAFE_MODE)
		count = egrp_count.count;
	else
		count = egrp_count.all;

	RETURN_ERROR_IF(count != 0, EM_ERR_NOT_FREE,
			EM_ESCOPE_EVENT_GROUP_DELETE,
			"Event group:%" PRI_EGRP " count not zero!",
			event_group);

	/* set num_notif = 0, ready = 0 */
	egrp_elem->all = 0;

	status = event_group_free(event_group);
	RETURN_ERROR_IF(status != EM_OK,
			status, EM_ESCOPE_EVENT_GROUP_DELETE,
			"Event Group delete failed!");

	return EM_OK;
}

em_status_t
em_event_group_apply(em_event_group_t event_group, int count,
		     int num_notif, const em_notif_t notif_tbl[])
{
	int i;
	uint64_t egrp_count;
	em_status_t ret;

	event_group_elem_t *const egrp_elem =
		event_group_elem_get(event_group);

	RETURN_ERROR_IF(egrp_elem == NULL || !event_group_allocated(egrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_APPLY,
			"Invalid event group: %" PRI_EGRP "", event_group);

	RETURN_ERROR_IF(count <= 0,
			EM_ERR_TOO_LARGE, EM_ESCOPE_EVENT_GROUP_APPLY,
			"Invalid argument: count %i", count);

	ret = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EVENT_GROUP_APPLY,
			"Invalid notif cfg given!");

	if (EM_EVENT_GROUP_SAFE_MODE)
		egrp_count = egrp_elem->post.count;
	else
		egrp_count = egrp_elem->post.all;

	RETURN_ERROR_IF(egrp_count != 0 || egrp_elem->ready == 0,
			EM_ERR_NOT_FREE, EM_ESCOPE_EVENT_GROUP_APPLY,
			"Event group %" PRI_EGRP " currently in use! count: %i",
			event_group, egrp_count);

	if (EM_EVENT_GROUP_SAFE_MODE) {
		egrp_elem->post.count = count;
		/* Event group generation increments when _apply() is called */
		egrp_elem->post.gen++;
		egrp_elem->pre.all = egrp_elem->post.all;
	} else {
		egrp_elem->post.all = count;
	}

	egrp_elem->ready = 0;
	egrp_elem->num_notif = num_notif;

	for (i = 0; i < num_notif; i++) {
		egrp_elem->notif_tbl[i].event = notif_tbl[i].event;
		egrp_elem->notif_tbl[i].queue = notif_tbl[i].queue;
		egrp_elem->notif_tbl[i].egroup = notif_tbl[i].egroup;
	}

	/* Sync mem */
	env_sync_mem();

	return EM_OK;
}

em_status_t
em_event_group_increment(int count)
{
	const em_locm_t *const locm = &em_locm;
	em_event_group_t const egrp = em_event_group_current();
	event_group_elem_t *egrp_elem = NULL;

	if (egrp != EM_EVENT_GROUP_UNDEF)
		egrp_elem = locm->current.egrp_elem;

	RETURN_ERROR_IF(egrp_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_INCREMENT,
			"No current event group (%" PRI_EGRP ")", egrp);

	RETURN_ERROR_IF(!event_group_allocated(egrp_elem) || egrp_elem->ready,
			EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_INCREMENT,
			"Current event group in a bad state (%" PRI_EGRP ")",
			egrp);

	if (!EM_EVENT_GROUP_SAFE_MODE) {
		EM_ATOMIC_ADD(&egrp_elem->post.atomic, count);
		return EM_OK;
	}

	egrp_counter_t current_count;
	egrp_counter_t new_count;
	/* Add to post counter before count is zero or generation mismatch */
	do {
		current_count.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);

		RETURN_ERROR_IF(current_count.count <= 0 ||
				current_count.gen != locm->current.egrp_gen,
				EM_ERR_BAD_STATE,
				EM_ESCOPE_EVENT_GROUP_INCREMENT,
				"Expired event group (%" PRI_EGRP ")",
				egrp);

		new_count = current_count;
		new_count.count += count;
	} while (!EM_ATOMIC_CMPSET(&egrp_elem->post.atomic,
				   current_count.all, new_count.all));

	/* Add to pre counter if generation matches */
	do {
		current_count.all = EM_ATOMIC_GET(&egrp_elem->pre.atomic);

		RETURN_ERROR_IF(current_count.gen != locm->current.egrp_gen,
				EM_ERR_BAD_STATE,
				EM_ESCOPE_EVENT_GROUP_INCREMENT,
				"Expired event group (%" PRI_EGRP ")",
				egrp);

		new_count = current_count;
		new_count.count += count;
	} while (!EM_ATOMIC_CMPSET(&egrp_elem->pre.atomic,
				   current_count.all, new_count.all));

	return EM_OK;
}

int
em_event_group_is_ready(em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem =
		event_group_elem_get(event_group);

	if (unlikely(egrp_elem == NULL || !event_group_allocated(egrp_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_IS_READY,
			       "Invalid event group: %" PRI_EGRP "",
			       event_group);
		return EM_FALSE;
	}

	uint64_t count;

	if (EM_EVENT_GROUP_SAFE_MODE)
		count = egrp_elem->post.count;
	else
		count = egrp_elem->post.all;

	if (count == 0 && egrp_elem->ready)
		return EM_TRUE;
	else
		return EM_FALSE;
}

em_event_group_t
em_event_group_current(void)
{
	em_locm_t *const locm = &em_locm;

	if (!EM_EVENT_GROUP_SAFE_MODE)
		return locm->current.egrp;

	if (locm->current.egrp == EM_EVENT_GROUP_UNDEF)
		return EM_EVENT_GROUP_UNDEF;

	const event_group_elem_t *egrp_elem = locm->current.egrp_elem;
	egrp_counter_t current;

	if (egrp_elem == NULL)
		return EM_EVENT_GROUP_UNDEF;

	current.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);

	if (locm->current.egrp_gen != current.gen || current.count <= 0)
		locm->current.egrp = EM_EVENT_GROUP_UNDEF;

	return locm->current.egrp;
}

em_status_t
em_send_group(em_event_t event, em_queue_t queue,
	      em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	const bool is_external = queue_external(queue);
	queue_elem_t *q_elem = NULL;
	event_hdr_t *ev_hdr;
	em_status_t stat;

	/*
	 * Check all args
	 */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF,
			EM_ERR_BAD_ID, EM_ESCOPE_SEND_GROUP, "Invalid event");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			event_group != EM_EVENT_GROUP_UNDEF && !egrp_elem,
			EM_ERR_NOT_FOUND, EM_ESCOPE_SEND_GROUP,
			"Invalid event group:%" PRI_EGRP "", event_group);

	ev_hdr = event_to_hdr(event);

	if (!is_external) {
		/* queue belongs to this EM instance */
		q_elem = queue_elem_get(queue);
		RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !q_elem,
				EM_ERR_BAD_ID, EM_ESCOPE_SEND_GROUP,
				"Invalid queue:%" PRI_QUEUE "", queue);
		RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !queue_allocated(q_elem),
				EM_ERR_BAD_STATE, EM_ESCOPE_SEND_GROUP,
				"Invalid queue:%" PRI_QUEUE "", queue);
	}

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && event_group != EM_EVENT_GROUP_UNDEF &&
			!event_group_allocated(egrp_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_SEND_GROUP,
			"Invalid event group:%" PRI_EGRP "", event_group);

	/* Buffer events sent from EO-start to scheduled queues */
	if (unlikely(!is_external &&
		     q_elem->scheduled && em_locm.start_eo_elem)) {
		/*
		 * em_send_group() called from within an EO-start function:
		 * all events sent to scheduled queues will be buffered
		 * and sent when the EO-start operation completes.
		 */
		int num = eo_start_buffer_events(&event, 1, queue, event_group);

		stat = num == 1 ? EM_OK : EM_ERR_OPERATION_FAILED;
		if (EM_CHECK_LEVEL == 0)
			return stat;
		RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_SEND_GROUP,
				"send-group from EO-start failed");
		return EM_OK;
	}

	/* Store the event group information in the event header */
	if (egrp_elem) {
		ev_hdr->egrp = egrp_elem->event_group;
		if (EM_EVENT_GROUP_SAFE_MODE)
			ev_hdr->egrp_gen = event_group_gen_get(egrp_elem);
	} else {
		ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, event_group);

	if (esv_enabled())
		evstate_usr2em(event, ev_hdr, EVSTATE__SEND_EGRP);

	if (is_external) {
		/*
		 * Send out of EM to another device via event-chaining and a
		 * user-provided function 'event_send_device()'
		 */
		stat = send_chaining_egrp(event, ev_hdr, queue, egrp_elem);
		if (EM_CHECK_LEVEL == 0)
			return stat;
		if (unlikely(stat != EM_OK)) {
			stat = INTERNAL_ERROR(stat, EM_ESCOPE_SEND_GROUP,
					      "send_chaining_egrp: Q:%" PRI_QUEUE "",
					      queue);
			goto send_group_err;
		}

		return EM_OK;
	}

	/*
	 * Normal send to a queue on this device
	 */
	switch (q_elem->type) {
	case EM_QUEUE_TYPE_ATOMIC:
	case EM_QUEUE_TYPE_PARALLEL:
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
		stat = send_event(event, q_elem);
		break;
	case EM_QUEUE_TYPE_UNSCHEDULED:
		stat = queue_unsched_enqueue(event, q_elem);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		stat = send_local(event, ev_hdr, q_elem);
		break;
	default:
		stat = EM_ERR_NOT_FOUND;
		break;
	}

	if (EM_CHECK_LEVEL == 0)
		return stat;

	if (unlikely(stat != EM_OK)) {
		stat = INTERNAL_ERROR(stat, EM_ESCOPE_SEND_GROUP,
				      "Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
				      queue, q_elem->type);
		goto send_group_err;
	}

	return EM_OK;

send_group_err:
	if (esv_enabled())
		evstate_usr2em_revert(event, ev_hdr, EVSTATE__SEND_EGRP__FAIL);
	return stat;
}

/*
 * em_send_group_multi() helper: check function arguments
 */
static inline em_status_t
send_grpmulti_check_args(const em_event_t events[], int num, em_queue_t queue,
			 em_event_group_t event_group,
			 const event_group_elem_t *egrp_elem,
			 bool *is_external__out /*out if EM_OK*/,
			 queue_elem_t **q_elem__out /*out if EM_OK*/)
{
	const bool is_external = queue_external(queue);
	queue_elem_t *q_elem = NULL;
	int i;

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!events || num <= 0 ||
		     (event_group != EM_EVENT_GROUP_UNDEF && !egrp_elem)))
		return EM_ERR_BAD_ARG;

	if (EM_CHECK_LEVEL > 1 &&
	    unlikely(event_group != EM_EVENT_GROUP_UNDEF &&
		     !event_group_allocated(egrp_elem)))
		return EM_ERR_BAD_STATE;

	if (EM_CHECK_LEVEL > 2) {
		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num))
			return EM_ERR_BAD_POINTER;
	}

	if (!is_external) {
		/* queue belongs to this EM instance */
		q_elem = queue_elem_get(queue);

		if (EM_CHECK_LEVEL > 0 && unlikely(!q_elem))
			return EM_ERR_BAD_ARG;
		if (EM_CHECK_LEVEL > 1 && unlikely(!queue_allocated(q_elem)))
			return EM_ERR_BAD_STATE;
	}

	*is_external__out = is_external;
	*q_elem__out = q_elem; /* NULL if is_external */
	return EM_OK;
}

int
em_send_group_multi(const em_event_t events[], int num, em_queue_t queue,
		    em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	bool is_external = false; /* set by check_args */
	queue_elem_t *q_elem = NULL; /* set by check_args */
	int num_sent;
	int i;

	/*
	 * Check all args.
	 */
	em_status_t err =
	send_grpmulti_check_args(events, num, queue, event_group, egrp_elem,
				 /*out if EM_OK:*/ &is_external, &q_elem);
	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_SEND_GROUP_MULTI,
			       "Invalid args: events:%p num:%d\n"
			       "Q:%" PRI_QUEUE " event_group:%" PRI_EGRP "",
			       events, num, queue, event_group);
		return 0;
	}

	/* Buffer events sent from EO-start to scheduled queues */
	if (unlikely(!is_external &&
		     q_elem->scheduled && em_locm.start_eo_elem)) {
		/*
		 * em_send_group_multi() called from within an EO-start
		 * function: all events sent to scheduled queues will be
		 * buffered and sent when the EO-start operation completes.
		 */
		num_sent = eo_start_buffer_events(events, num, queue, event_group);
		if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
			INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
				       "send-egrp-multi EO-start:req:%d sent:%d",
				       num, num_sent);
		}
		return num_sent;
	}

	event_hdr_t *ev_hdrs[num];

	event_to_hdr_multi(events, ev_hdrs, num);

	/* Store the event group information in the event header */
	for (i = 0; i < num; i++)
		ev_hdrs[i]->egrp = event_group; /* can be EM_EVENT_GROUP_UNDEF*/

	if (EM_EVENT_GROUP_SAFE_MODE && egrp_elem) {
		uint64_t egrp_gen = event_group_gen_get(egrp_elem);

		for (i = 0; i < num; i++)
			ev_hdrs[i]->egrp_gen = egrp_gen;
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, event_group);

	if (esv_enabled())
		evstate_usr2em_multi(events, ev_hdrs, num,
				     EVSTATE__SEND_EGRP_MULTI);

	if (is_external) {
		/*
		 * Send out of EM to another device via event-chaining and a
		 * user-provided function 'event_send_device_multi()'
		 */
		num_sent = send_chaining_egrp_multi(events, ev_hdrs, num,
						    queue, egrp_elem);
		if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
			INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
				       "send_chaining_egrp_multi: req:%d, sent:%d",
				       num, num_sent);
			goto send_group_multi_err;
		}

		return num_sent;
	}

	/*
	 * Normal send to a queue on this device
	 */
	switch (q_elem->type) {
	case EM_QUEUE_TYPE_ATOMIC:
	case EM_QUEUE_TYPE_PARALLEL:
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
		num_sent = send_event_multi(events, num, q_elem);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		num_sent = send_local_multi(events, ev_hdrs, num, q_elem);
		break;
	default:
		num_sent = 0;
		break;
	}

	if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
			       "send-egrp-multi failed: req:%d, sent:%d",
			       num, num_sent);
		goto send_group_multi_err;
	}

	return num_sent;

send_group_multi_err:
	if (esv_enabled()) {
		evstate_usr2em_revert_multi(&events[num_sent], &ev_hdrs[num_sent],
					    num - num_sent,
					    EVSTATE__SEND_EGRP_MULTI__FAIL);
	}
	return num_sent;
}

void
em_event_group_processing_end(void)
{
	em_locm_t *const locm = &em_locm;
	const em_event_group_t event_group = em_event_group_current();

	if (unlikely(invalid_egrp(event_group)))
		return;

	/*
	 * Atomically decrement the event group count.
	 * If new count is zero, send notification events.
	 */
	event_group_count_decrement(locm->current.rcv_multi_cnt);

	locm->current.egrp = EM_EVENT_GROUP_UNDEF;
	locm->current.egrp_elem = NULL;
}

em_status_t
em_event_group_assign(em_event_group_t event_group)
{
	em_locm_t *const locm = &em_locm;
	event_group_elem_t *const egrp_elem =
		event_group_elem_get(event_group);

	RETURN_ERROR_IF(egrp_elem == NULL || !event_group_allocated(egrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_ASSIGN,
			"Invalid event group: %" PRI_EGRP "", event_group);

	RETURN_ERROR_IF(locm->current.egrp != EM_EVENT_GROUP_UNDEF,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_EVENT_GROUP_ASSIGN,
			"Cannot assign event group %" PRI_EGRP ",\n"
			"event already belongs to event group %" PRI_EGRP "",
			event_group, locm->current.egrp);

	RETURN_ERROR_IF(egrp_elem->ready,
			EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_ASSIGN,
			"Cannot assign event group %" PRI_EGRP ",\n"
			"Event group has not been applied", event_group);

	locm->current.egrp = event_group;
	locm->current.egrp_elem = egrp_elem;

	if (EM_EVENT_GROUP_SAFE_MODE)
		locm->current.egrp_gen = egrp_elem->post.gen;

	return EM_OK;
}

/*
 * Abort is successful if generation can be incremented before post_count
 * reaches zero.
 */
em_status_t
em_event_group_abort(em_event_group_t event_group)
{
	event_group_elem_t *const egrp_elem =
		event_group_elem_get(event_group);

	RETURN_ERROR_IF(egrp_elem == NULL || !event_group_allocated(egrp_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_ABORT,
			"Invalid event group: %" PRI_EGRP "", event_group);

	if (!EM_EVENT_GROUP_SAFE_MODE) {
		RETURN_ERROR_IF(egrp_elem->post.all <= 0,
				EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Event group abort too late, notifs already sent");
		egrp_elem->post.all = 0;
		/* mark group ready for new apply and stop notifs */
		egrp_elem->ready = 1;
		return EM_OK;
	}

	egrp_counter_t current_count;
	egrp_counter_t new_count;

	/* Attemp to set count to zero before count reaches zero */
	do {
		current_count.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);

		RETURN_ERROR_IF(current_count.count <= 0,
				EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Event group abort late, notifs already sent");
		new_count = current_count;
		new_count.count = 0;
	} while (!EM_ATOMIC_CMPSET(&egrp_elem->post.atomic,
				   current_count.all, new_count.all));
	/*
	 * Change pre_count also to prevent expired event group events
	 * from reaching receive function.
	 */
	EM_ATOMIC_SET(&egrp_elem->pre.atomic, new_count.all);
	/* Ready for new apply */
	egrp_elem->ready = 1;

	return EM_OK;
}

int
em_event_group_get_notif(em_event_group_t event_group,
			 int max_notif, em_notif_t notif_tbl[])
{
	const event_group_elem_t *egrp_elem =
		event_group_elem_get(event_group);
	int num_notif = 0; /* return value */

	if (unlikely(egrp_elem == NULL || !event_group_allocated(egrp_elem) ||
		     max_notif < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_GET_NOTIF,
			       "Invalid args: evgrp:%" PRI_EGRP ", notifs:%d",
			       event_group, max_notif);
		return 0;
	}

	if (unlikely(max_notif == 0))
		return 0;

	if (unlikely(notif_tbl == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER,
			       EM_ESCOPE_EVENT_GROUP_GET_NOTIF,
			       "Invalid notif_tbl[] given");
		return 0;
	}

	if (!egrp_elem->ready) {
		int i;

		num_notif = max_notif < egrp_elem->num_notif ?
			    max_notif : egrp_elem->num_notif;

		for (i = 0; i < num_notif; i++) {
			notif_tbl[i].event = egrp_elem->notif_tbl[i].event;
			notif_tbl[i].queue = egrp_elem->notif_tbl[i].queue;
			notif_tbl[i].egroup = egrp_elem->notif_tbl[i].egroup;
		}
	}

	return num_notif;
}

em_event_group_t
em_event_group_get_first(unsigned int *num)
{
	const event_group_elem_t *const egrp_elem_tbl =
		em_shm->event_group_tbl.egrp_elem;
	const event_group_elem_t *egrp_elem = &egrp_elem_tbl[0];
	const unsigned int egrp_count = event_group_count();

	_egrp_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = egrp_count;

	if (egrp_count == 0) {
		_egrp_tbl_iter_idx = EM_MAX_EVENT_GROUPS; /* UNDEF=_get_next()*/
		return EM_EVENT_GROUP_UNDEF;
	}

	/* find first */
	while (!event_group_allocated(egrp_elem)) {
		_egrp_tbl_iter_idx++;
		if (_egrp_tbl_iter_idx >= EM_MAX_EVENT_GROUPS)
			return EM_EVENT_GROUP_UNDEF;
		egrp_elem = &egrp_elem_tbl[_egrp_tbl_iter_idx];
	}

	return egrp_idx2hdl(_egrp_tbl_iter_idx);
}

em_event_group_t
em_event_group_get_next(void)
{
	if (_egrp_tbl_iter_idx >= EM_MAX_EVENT_GROUPS - 1)
		return EM_EVENT_GROUP_UNDEF;

	_egrp_tbl_iter_idx++;

	const event_group_elem_t *const egrp_elem_tbl =
		em_shm->event_group_tbl.egrp_elem;
	const event_group_elem_t *egrp_elem =
		&egrp_elem_tbl[_egrp_tbl_iter_idx];

	/* find next */
	while (!event_group_allocated(egrp_elem)) {
		_egrp_tbl_iter_idx++;
		if (_egrp_tbl_iter_idx >= EM_MAX_EVENT_GROUPS)
			return EM_EVENT_GROUP_UNDEF;
		egrp_elem = &egrp_elem_tbl[_egrp_tbl_iter_idx];
	}

	return egrp_idx2hdl(_egrp_tbl_iter_idx);
}
