/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EVENT_GROUP_CREATE,
			       "Event group alloc failed!");
		return EM_EVENT_GROUP_UNDEF;
	}

	egrp_elem = event_group_elem_get(egrp);

	/* Alloc succeeded, return event group handle */
	egrp_elem->ready = true; /* Set group ready for 'apply' */
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
			EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_DELETE,
			"Invalid event group: %" PRI_EGRP "", event_group);

	egrp_count.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);

	if (EM_EVENT_GROUP_SAFE_MODE)
		count = egrp_count.count;
	else
		count = egrp_count.all;

	RETURN_ERROR_IF(count != 0, EM_ERR_BAD_STATE,
			EM_ESCOPE_EVENT_GROUP_DELETE,
			"Event group:%" PRI_EGRP " count not zero!",
			event_group);

	/* set num_notif = 0, ready = 0/false */
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
	uint64_t egrp_count;
	em_status_t ret;

	event_group_elem_t *const egrp_elem =
		event_group_elem_get(event_group);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(egrp_elem == NULL || count <= 0,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_APPLY,
				"Invalid args: event group:%" PRI_EGRP ", count:%d",
				event_group, count);
	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_APPLY,
				"Event group:%" PRI_EGRP " not created!", event_group);

	ret = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EVENT_GROUP_APPLY,
			"Invalid notif cfg given!");

	if (EM_EVENT_GROUP_SAFE_MODE)
		egrp_count = egrp_elem->post.count;
	else
		egrp_count = egrp_elem->post.all;

	RETURN_ERROR_IF(egrp_count != 0 || !egrp_elem->ready,
			EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_APPLY,
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

	egrp_elem->ready = false;
	egrp_elem->num_notif = num_notif;

	for (int i = 0; i < num_notif; i++) {
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

	RETURN_ERROR_IF(egrp_elem == NULL || egrp_elem->ready,
			EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_INCREMENT,
			"No current event group (%" PRI_EGRP ") or not applied",
			egrp);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
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

int em_event_group_is_ready(em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem =
		event_group_elem_get(event_group);

	if (unlikely(EM_CHECK_LEVEL > 0 && egrp_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_IS_READY,
			       "Invalid event group: %" PRI_EGRP "",
			       event_group);
		return EM_FALSE;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !event_group_allocated(egrp_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_IS_READY,
			       "Event group: %" PRI_EGRP " not created",
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

/**
 * Helper to em_send_group().
 * Send out of EM via event-chaining and a user-provided function
 * 'event_send_device()' to another device
 */
static inline em_status_t
send_external_egrp(em_event_t event, event_hdr_t *const ev_hdr,
		   em_queue_t queue, em_event_group_t event_group,
		   const event_group_elem_t *egrp_elem)
{
	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, event_group);

	em_status_t stat = send_chaining_egrp(event, ev_hdr, queue, egrp_elem);

	if (EM_CHECK_LEVEL == 0)
		return stat;

	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_SEND_GROUP,
			"send_chaining_egrp: Q:%" PRI_QUEUE "", queue);
	return EM_OK;
}

/**
 * Helper to em_send_multi().
 * Send out of EM via event-chaining and a user-provided function
 * 'event_send_device()' to another device
 */
static inline int
send_external_egrp_multi(const em_event_t events[], event_hdr_t *ev_hdrs[], int num,
			 em_queue_t queue, em_event_group_t event_group,
			 const event_group_elem_t *egrp_elem)
{
	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, event_group);

	int num_sent = send_chaining_egrp_multi(events, ev_hdrs, num,
						queue, egrp_elem);
	if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
			       "send_chaining_egrp_multi: req:%d, sent:%d",
			       num, num_sent);
	}

	return num_sent;
}

/**
 * Helper to em_send_group().
 * Send to an EM internal queue.
 */
static inline em_status_t
send_internal_egrp(em_event_t event, event_hdr_t *ev_hdr, em_queue_t queue,
		   em_event_group_t event_group)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);
	em_status_t stat;

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !q_elem,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP,
			"Invalid queue:%" PRI_QUEUE "", queue);
	RETURN_ERROR_IF(EM_CHECK_LEVEL >= 2 && !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_SEND_GROUP,
			"Invalid queue:%" PRI_QUEUE "", queue);

	/* Buffer events sent from EO-start to scheduled queues */
	if (unlikely(em_locm.start_eo_elem && q_elem->flags.scheduled)) {
		/*
		 * em_send_group() called from within an EO-start function:
		 * all events sent to scheduled queues will be buffered
		 * and sent when the EO-start operation completes.
		 */
		if (esv_enabled())
			evstate_usr2em(event, ev_hdr, EVSTATE__SEND_EGRP);

		int num_sent = eo_start_buffer_events(&event, 1, queue);

		if (unlikely(num_sent != 1)) {
			stat = EM_ERR_OPERATION_FAILED;
			goto error_return;
		}

		return EM_OK; /* Success */
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, event_group);

	/*
	 * Normal send to a queue on this device
	 */
	if (esv_enabled())
		evstate_usr2em(event, ev_hdr, EVSTATE__SEND_EGRP);

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
		stat = send_local(event, q_elem);
		break;
	default:
		stat = EM_ERR_NOT_FOUND;
		break;
	}

	if (likely(stat == EM_OK))
		return EM_OK; /* Success */

error_return:
	if (esv_enabled())
		evstate_usr2em_revert(event, ev_hdr, EVSTATE__SEND_EGRP__FAIL);

	if (EM_CHECK_LEVEL == 0)
		return stat;
	stat = INTERNAL_ERROR(stat, EM_ESCOPE_SEND_GROUP,
			      "send egrp: Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
			      queue, q_elem->type);
	return stat;
}

/**
 * Helper to em_send_multi().
 * Send to an EM internal queue.
 */
static inline int
send_internal_egrp_multi(const em_event_t events[], event_hdr_t *ev_hdrs[],
			 int num, em_queue_t queue, em_event_group_t event_group)
{
	const queue_elem_t *q_elem = queue_elem_get(queue);
	int num_sent;

	if (EM_CHECK_LEVEL > 0 && unlikely(!q_elem)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP_MULTI,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return 0;
	}
	if (EM_CHECK_LEVEL >= 2 && unlikely(!queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_SEND_GROUP_MULTI,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return 0;
	}

	/* Buffer events sent from EO-start to scheduled queues */
	if (unlikely(em_locm.start_eo_elem && q_elem->flags.scheduled)) {
		/*
		 * em_send_group_multi() called from within an EO-start
		 * function: all events sent to scheduled queues will be
		 * buffered and sent when the EO-start operation completes.
		 */
		if (esv_enabled())
			evstate_usr2em_multi(events, ev_hdrs, num,
					     EVSTATE__SEND_EGRP_MULTI);
		num_sent = eo_start_buffer_events(events, num, queue);

		if (unlikely(num_sent != num))
			goto error_return;

		return num_sent; /* Success */
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, event_group);

	/*
	 * Normal send to a queue on this device
	 */
	if (esv_enabled())
		evstate_usr2em_multi(events, ev_hdrs, num, EVSTATE__SEND_EGRP_MULTI);

	switch (q_elem->type) {
	case EM_QUEUE_TYPE_ATOMIC:
	case EM_QUEUE_TYPE_PARALLEL:
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
		num_sent = send_event_multi(events, num, q_elem);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		num_sent = send_local_multi(events, num, q_elem);
		break;
	default:
		num_sent = 0;
		break;
	}

	if (likely(num_sent == num))
		return num_sent; /* Success */

error_return:
	if (esv_enabled())
		evstate_usr2em_revert_multi(&events[num_sent],
					    &ev_hdrs[num_sent],
					    num - num_sent,
						    EVSTATE__SEND_EGRP_MULTI__FAIL);
	if (EM_CHECK_LEVEL > 0)
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
			       "send-egrp-multi failed: req:%d, sent:%d",
			       num, num_sent);
	return num_sent;
}

em_status_t em_send_group(em_event_t event, em_queue_t queue,
			  em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	const bool is_external = queue_external(queue);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP, "Invalid event");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			event_group != EM_EVENT_GROUP_UNDEF && !egrp_elem,
			EM_ERR_NOT_FOUND, EM_ESCOPE_SEND_GROUP,
			"Invalid event group:%" PRI_EGRP "", event_group);
	RETURN_ERROR_IF(EM_CHECK_LEVEL >= 2 && event_group != EM_EVENT_GROUP_UNDEF &&
			!event_group_allocated(egrp_elem),
			EM_ERR_NOT_CREATED, EM_ESCOPE_SEND_GROUP,
			"Event group:%" PRI_EGRP " not created", event_group);
	/*
	 * Verify that event references are not used with event groups.
	 * Cannot save the event group into an event header shared between
	 * all the references.
	 */
	RETURN_ERROR_IF(EM_CHECK_LEVEL >= 3 &&
			event_group != EM_EVENT_GROUP_UNDEF && event_has_ref(event),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_SEND_GROUP,
			"Event has references: can't use references with event groups");

	event_hdr_t *ev_hdr = event_to_hdr(event);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_TIMER_IND,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP, "Timer-ring event can't be sent");

	/* Store the event group information in the event header */
	if (egrp_elem) {
		ev_hdr->egrp = egrp_elem->event_group;
		if (EM_EVENT_GROUP_SAFE_MODE)
			ev_hdr->egrp_gen = event_group_gen_get(egrp_elem);
	} else {
		ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	}

	/*
	 * External queue belongs to another EM instance, send out via EMC/BIP
	 */
	if (is_external)
		return send_external_egrp(event, ev_hdr, queue,
					  event_group, egrp_elem);
	/*
	 * Queue belongs to this EM instance
	 */
	return send_internal_egrp(event, ev_hdr, queue, event_group);
}

/*
 * em_send_group_multi() helper: check function arguments
 */
static inline em_status_t
send_grpmulti_check(const em_event_t events[], int num,
		    em_event_group_t event_group,
		    const event_group_elem_t *egrp_elem)
{
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!events || num <= 0 ||
		     (event_group != EM_EVENT_GROUP_UNDEF && !egrp_elem)))
		return EM_ERR_BAD_ARG;

	if (EM_CHECK_LEVEL >= 2 &&
	    unlikely(event_group != EM_EVENT_GROUP_UNDEF &&
		     !event_group_allocated(egrp_elem)))
		return EM_ERR_NOT_CREATED;

	if (EM_CHECK_LEVEL >= 3) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num))
			return EM_ERR_BAD_POINTER;
	}

	return EM_OK;
}

int em_send_group_multi(const em_event_t events[], int num, em_queue_t queue,
			em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	const bool is_external = queue_external(queue);
	event_hdr_t *ev_hdrs[num];

	em_status_t err = send_grpmulti_check(events, num,
					      event_group, egrp_elem);
	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_SEND_GROUP_MULTI,
			       "Invalid args: events:%p num:%d event_group:%" PRI_EGRP "",
			       events, num, queue, event_group);
		return 0;
	}

	/*
	 * Verify that event references are not used with event groups.
	 * Cannot save the event group into an event header shared between
	 * all the references
	 */
	if (unlikely(EM_CHECK_LEVEL >= 3 && event_group != EM_EVENT_GROUP_UNDEF)) {
		for (int i = 0; i < num; i++) {
			if (likely(!event_has_ref(events[i])))
				continue;

			INTERNAL_ERROR(EM_ERR_BAD_CONTEXT, EM_ESCOPE_SEND_GROUP_MULTI,
				       "event[%d] has references: can't use with event groups", i);
			return 0;
		}
	}

	event_to_hdr_multi(events, ev_hdrs, num);

	/* check for invalid TIMER events */
	if (EM_CHECK_LEVEL > 0) {
		for (int i = 0; i < num; i++) {
			if (unlikely(ev_hdrs[i]->event_type == EM_EVENT_TYPE_TIMER_IND)) {
				INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP_MULTI,
					       "Timer-ring event[%d] can't be sent", i);
				return 0;
			}
		}
	}

	/* Store the event group information in the event header */
	for (int i = 0; i < num; i++)
		ev_hdrs[i]->egrp = event_group; /* can be EM_EVENT_GROUP_UNDEF*/

	if (EM_EVENT_GROUP_SAFE_MODE && egrp_elem) {
		uint64_t egrp_gen = event_group_gen_get(egrp_elem);

		for (int i = 0; i < num; i++)
			ev_hdrs[i]->egrp_gen = egrp_gen;
	}

	/*
	 * External queue belongs to another EM instance, send out via EMC/BIP
	 */
	if (is_external)
		return send_external_egrp_multi(events, ev_hdrs, num, queue,
						event_group, egrp_elem);
	/*
	 * Queue belongs to this EM instance
	 */
	return send_internal_egrp_multi(events, ev_hdrs, num, queue, event_group);
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

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(egrp_elem == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_ASSIGN,
				"Invalid event group: %" PRI_EGRP "", event_group);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_ASSIGN,
				"Invalid event group: %" PRI_EGRP "", event_group);

	RETURN_ERROR_IF(locm->current.egrp != EM_EVENT_GROUP_UNDEF,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_EVENT_GROUP_ASSIGN,
			"Cannot assign event group %" PRI_EGRP ",\n"
			"event already belongs to event group %" PRI_EGRP "",
			event_group, locm->current.egrp);

	RETURN_ERROR_IF(egrp_elem->ready,
			EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_ASSIGN,
			"Cannot assign event group %" PRI_EGRP ".\n"
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

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(egrp_elem == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Invalid event group: %" PRI_EGRP "", event_group);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Event group: %" PRI_EGRP " not created", event_group);

	if (!EM_EVENT_GROUP_SAFE_MODE) {
		RETURN_ERROR_IF(egrp_elem->post.all <= 0,
				EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Event group abort too late, notifs already sent");
		egrp_elem->post.all = 0;
		/* mark group ready for new apply and stop notifs */
		egrp_elem->ready = true;
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
	egrp_elem->ready = true;

	return EM_OK;
}

int em_event_group_get_notif(em_event_group_t event_group,
			     int max_notif, em_notif_t notif_tbl[])
{
	const event_group_elem_t *egrp_elem =
		event_group_elem_get(event_group);
	int num_notif = 0; /* return value */

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(egrp_elem == NULL || max_notif < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_GET_NOTIF,
			       "Invalid args: evgrp:%" PRI_EGRP ", notifs:%d",
			       event_group, max_notif);
		return 0;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !event_group_allocated(egrp_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_GET_NOTIF,
			       "Event group:%" PRI_EGRP " not created",
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
		num_notif = max_notif < egrp_elem->num_notif ?
			    max_notif : egrp_elem->num_notif;

		for (int i = 0; i < num_notif; i++) {
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

uint64_t em_event_group_to_u64(em_event_group_t event_group)
{
	return (uint64_t)event_group;
}
