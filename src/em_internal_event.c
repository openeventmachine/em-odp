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

static void
i_event__internal_done(const internal_event_t *i_ev);

/**
 * Sends an internal control event to each core set in 'mask'.
 *
 * When all cores set in 'mask' has processed the event an additional
 * internal 'done' msg is sent to synchronize - this done-event can then
 * trigger any notifications for the user that the operation was completed.
 * Processing of the 'done' event will also call the 'f_done_callback'
 * function if given.
 *
 * @return 0 on success, otherwise return the number of ctrl events that could
 *         not be sent due to error
 */
int
send_core_ctrl_events(const em_core_mask_t *const mask, em_event_t ctrl_event,
		      void (*f_done_callback)(void *arg_ptr),
		      void *f_done_arg_ptr,
		      int num_notif, const em_notif_t notif_tbl[])
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

	event_group = em_event_group_create();
	if (unlikely(event_group == EM_EVENT_GROUP_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_INTERNAL_NOTIF,
			       "Event group alloc failed");
		return unsent_count;
	}

	/*
	 * Set up internal notification when all cores are done.
	 */
	err = internal_done_w_notif_req(event_group, mask_count,
					f_done_callback, f_done_arg_ptr,
					num_notif, notif_tbl);
	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_INTERNAL_NOTIF,
			       "Internal 'done' notif setup failed");
		goto err_delete_event_group;
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
			goto err_free_events;
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
	first_qidx = queue_id2idx(FIRST_INTERNAL_QUEUE);

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
				goto err_free_events;
			}
			sent_count++;
			unsent_count--;
		}
	}

	return 0; /* Success, all ctrl events sent */

	/* Error handling, free resources */
err_free_events:
	for (i = sent_count; i < alloc_count; i++)
		em_free(events[i]);
err_delete_event_group:
	(void)em_event_group_abort(event_group);
	(void)em_event_group_delete(event_group);
	return unsent_count;
}

/**
 * Receive function for handling internal ctrl events, called by the daemon EO
 */
void
internal_event_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		       em_queue_t queue, void *q_ctx)
{
	const internal_event_t *i_event;

	/* currently unused args */
	(void)eo_ctx;
	(void)type;
	(void)q_ctx;

	i_event = em_event_pointer(event);

	switch (i_event->id) {
	/*
	 * Internal Done event
	 */
	case EM_INTERNAL_DONE:
		i_event__internal_done(i_event);
		break;
	/*
	 * Internal event related to Queue Group modification: remove a core
	 */
	case QUEUE_GROUP_REM_REQ:
	case QUEUE_GROUP_REM_SYNC_REQ:
		/*
		 * Nothing needs to be done, enough to ensure the core is not
		 * processing an event from a queue in the modified queue group
		 */
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

	em_free(event);
}

/**
 * Handle the internal 'done' event
 */
static void
i_event__internal_done(const internal_event_t *i_ev)
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
 * Helper func: Allocate & set up the internal 'done' event with
 * function callbacks and notification events.
 */
em_status_t
internal_done_w_notif_req(em_event_group_t event_group,
			  int event_group_count,
			  void (*f_done_callback)(void *arg_ptr),
			  void  *f_done_arg_ptr,
			  int num_notif, const em_notif_t notif_tbl[])
{
	em_event_t event;
	internal_event_t *i_event;
	em_notif_t i_notif;
	em_status_t err;
	int i;

	event = em_alloc(sizeof(internal_event_t), EM_EVENT_TYPE_SW,
			 EM_POOL_DEFAULT);
	RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED,
			EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
			"Internal event 'DONE' alloc failed!");

	i_event = em_event_pointer(event);
	i_event->id = EM_INTERNAL_DONE;
	i_event->done.event_group = event_group;
	i_event->done.f_done_callback = f_done_callback;
	i_event->done.f_done_arg_ptr = f_done_arg_ptr;
	i_event->done.num_notif = num_notif;

	for (i = 0; i < num_notif; i++) {
		i_event->done.notif_tbl[i].event = notif_tbl[i].event;
		i_event->done.notif_tbl[i].queue = notif_tbl[i].queue;
		i_event->done.notif_tbl[i].egroup = notif_tbl[i].egroup;
	}

	i_notif.event = event;
	i_notif.queue = queue_id2hdl(SHARED_INTERNAL_QUEUE);
	i_notif.egroup = EM_EVENT_GROUP_UNDEF;

	/*
	 * Request sending of EM_INTERNAL_DONE when 'event_group_count' events
	 * in 'event_group' have been seen. The 'Done' event will trigger the
	 * notifications to be sent.
	 */
	err = em_event_group_apply(event_group, event_group_count,
				   1, &i_notif);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
			"Event group apply failed");

	return EM_OK;
}

/**
 * Helper func to send notifications events
 */
em_status_t
send_notifs(const int num_notif, const em_notif_t notif_tbl[])
{
	int i;
	em_status_t err;

	for (i = 0; i < num_notif; i++) {
		const em_event_t event = notif_tbl[i].event;
		const em_queue_t queue = notif_tbl[i].queue;
		const em_event_group_t egrp = notif_tbl[i].egroup;

		/* 'egroup' may be uninit in old appl code, check */
		if (invalid_egrp(egrp))
			err = em_send(event, queue);
		else
			err = em_send_group(event, queue, egrp);

		if (unlikely(err != EM_OK))
			return err;
	}

	return EM_OK;
}

em_status_t
check_notif(const em_notif_t *const notif)
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

em_status_t
check_notif_tbl(const int num_notif, const em_notif_t notif_tbl[])
{
	em_status_t err;
	int i;

	if (unlikely((unsigned int)num_notif > EM_EVENT_GROUP_MAX_NOTIF))
		return EM_ERR_TOO_LARGE;

	if (unlikely(num_notif > 0 && notif_tbl == NULL))
		return EM_ERR_BAD_POINTER;

	for (i = 0; i < num_notif; i++) {
		err = check_notif(&notif_tbl[i]);
		if (unlikely(err != EM_OK))
			return err;
	}

	return EM_OK;
}
