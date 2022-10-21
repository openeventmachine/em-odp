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

em_event_t
em_alloc(size_t size, em_event_type_t type, em_pool_t pool)
{
	mpool_elem_t *pool_elem;

	pool_elem = pool_elem_get(pool);
	if (unlikely(size == 0 ||
		     pool_elem == NULL || !pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_ALLOC,
			       "Invalid args: size:%zu type:%u pool:%" PRI_POOL "",
			       size, type, pool);
		return EM_EVENT_UNDEF;
	}

	/*
	 * EM event pools created with type=SW can not support pkt events.
	 */
	if (unlikely(pool_elem->event_type == EM_EVENT_TYPE_SW &&
		     em_get_type_major(type) == EM_EVENT_TYPE_PACKET)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC,
			       "EM-pool:%s(%" PRI_POOL "):\n"
			       "Invalid event type:0x%" PRIx32 " for buf",
			       pool_elem->name, pool_elem->em_pool, type);
		return EM_EVENT_UNDEF;
	}

	event_hdr_t *ev_hdr = event_alloc(pool_elem, size, type);

	if (unlikely(!ev_hdr)) {
		em_status_t err =
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_ALLOC,
			       "EM-pool:'%s': sz:%zu type:0x%x pool:%" PRI_POOL "",
			       pool_elem->name, size, type, pool);
		if (EM_CHECK_LEVEL > 1 && err != EM_OK &&
		    em_shm->opt.pool.statistics_enable) {
			em_pool_info_print(pool);
		}
		return EM_EVENT_UNDEF;
	}

	em_event_t event = ev_hdr->event;

	/* Update event ESV state for alloc */
	if (esv_enabled())
		event = evstate_alloc(event, ev_hdr);

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_alloc(&event, 1, 1, size, type, pool);

	return event;
}

int
em_alloc_multi(em_event_t events[/*out*/], int num,
	       size_t size, em_event_type_t type, em_pool_t pool)
{
	if (unlikely(num <= 0)) {
		if (num < 0)
			INTERNAL_ERROR(EM_ERR_TOO_SMALL, EM_ESCOPE_ALLOC_MULTI,
				       "Invalid arg: num:%d", num);
		return 0;
	}

	mpool_elem_t *const pool_elem = pool_elem_get(pool);
	int ret;

	if (unlikely(size == 0 ||
		     pool_elem == NULL || !pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_ALLOC_MULTI,
			       "Invalid args: size:%zu type:%u pool:%" PRI_POOL "",
			       size, type, pool);
		return 0;
	}

	if (pool_elem->event_type == EM_EVENT_TYPE_PACKET) {
		/*
		 * EM event pools created with type=PKT can support SW events
		 * as well as pkt events.
		 */
		ret = event_alloc_pkt_multi(events, num, pool_elem, size, type);
	} else { /* pool_elem->event_type == EM_EVENT_TYPE_SW */
		/*
		 * EM event pools created with type=SW can not support
		 * pkt events.
		 */
		if (unlikely(em_get_type_major(type) == EM_EVENT_TYPE_PACKET)) {
			INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC_MULTI,
				       "EM-pool:%s(%" PRI_POOL "): Invalid event type:%u for buf",
				       pool_elem->name, pool, type);
			return 0;
		}

		ret = event_alloc_buf_multi(events, num, pool_elem, size, type);
	}

	if (unlikely(ret != num)) {
		em_status_t err =
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_ALLOC_MULTI,
			       "Requested num:%d events, allocated:%d\n"
			       "EM-pool:'%s': sz:%zu type:0x%x pool:%" PRI_POOL "",
			       num, ret,
			       pool_elem->name, size, type, pool);
		if (EM_CHECK_LEVEL > 1 && err != EM_OK &&
		    em_shm->opt.pool.statistics_enable) {
			em_pool_info_print(pool);
		}
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_alloc(events, ret, num, size, type, pool);

	return ret;
}

void
em_free(em_event_t event)
{
	odp_event_t odp_event;

	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_FREE,
			       "event undefined!");
		return;
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(&event, 1);

	if (esv_enabled()) {
		event_hdr_t *const ev_hdr = event_to_hdr(event);

		evstate_free(event, ev_hdr, EVSTATE__FREE);
	}

	odp_event = event_em2odp(event);
	odp_event_free(odp_event);
}

void em_free_multi(const em_event_t events[], int num)
{
	if (unlikely(!events || num < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_FREE_MULTI,
			       "Inv.args: events[]:%p num:%d", events, num);
		return;
	}
	if (unlikely(num == 0))
		return;

	if (EM_CHECK_LEVEL > 1) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_FREE_MULTI,
				       "events[%d] undefined!", i);
			return;
		}
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(events, num);

	odp_event_t odp_events[num];

	if (esv_enabled()) {
		event_hdr_t *ev_hdrs[num];

		event_to_hdr_multi(events, ev_hdrs, num);
		evstate_free_multi(events, ev_hdrs, num, EVSTATE__FREE_MULTI);
	}

	events_em2odp(events, odp_events/*out*/, num);
	odp_event_free_multi(odp_events, num);
}

em_status_t
em_send(em_event_t event, em_queue_t queue)
{
	const bool is_external = queue_external(queue);
	queue_elem_t *q_elem = NULL;
	event_hdr_t *ev_hdr;
	int num_sent;
	em_status_t stat;

	/*
	 * Check all args.
	 */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF,
			EM_ERR_BAD_ID, EM_ESCOPE_SEND, "Invalid event");

	ev_hdr = event_to_hdr(event);
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	if (!is_external) {
		/* queue belongs to this EM instance */
		q_elem = queue_elem_get(queue);
		RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !q_elem,
				EM_ERR_BAD_ID, EM_ESCOPE_SEND,
				"Invalid queue:%" PRI_QUEUE "", queue);
		RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !queue_allocated(q_elem),
				EM_ERR_BAD_STATE, EM_ESCOPE_SEND,
				"Invalid queue:%" PRI_QUEUE "", queue);
	}

	/* Buffer events from EO-start sent to scheduled queues */
	if (unlikely(!is_external &&
		     q_elem->scheduled && em_locm.start_eo_elem)) {
		/*
		 * em_send() called from within an EO-start function:
		 * all events sent to scheduled queues will be buffered
		 * and sent when the EO-start operation completes.
		 */
		num_sent = eo_start_buffer_events(&event, 1, queue,
						  EM_EVENT_GROUP_UNDEF);
		stat = num_sent == 1 ? EM_OK : EM_ERR_OPERATION_FAILED;
		if (EM_CHECK_LEVEL == 0)
			return stat;
		RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_SEND,
				"send from EO-start failed");
		return EM_OK;
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, EM_EVENT_GROUP_UNDEF);

	if (esv_enabled())
		evstate_usr2em(event, ev_hdr, EVSTATE__SEND);

	if (is_external) {
		/*
		 * Send out of EM to another device via event-chaining and a
		 * user-provided function 'event_send_device()'
		 */
		stat = send_chaining(event, ev_hdr, queue);
		if (EM_CHECK_LEVEL == 0)
			return stat;
		if (unlikely(stat != EM_OK)) {
			stat = INTERNAL_ERROR(stat, EM_ESCOPE_SEND,
					      "send_chaining failed: Q:%" PRI_QUEUE "",
					      queue);
			goto send_err;
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
	case EM_QUEUE_TYPE_OUTPUT:
		stat = send_output(event, ev_hdr, q_elem);
		break;
	default:
		stat = EM_ERR_NOT_FOUND;
		break;
	}

	if (EM_CHECK_LEVEL == 0)
		return stat;

	if (unlikely(stat != EM_OK)) {
		stat =
		INTERNAL_ERROR(stat, EM_ESCOPE_SEND,
			       "send failed: Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
			       queue, q_elem->type);
		goto send_err;
	}

	return EM_OK;

send_err:
	if (esv_enabled())
		evstate_usr2em_revert(event, ev_hdr, EVSTATE__SEND__FAIL);
	return stat;
}

/*
 * em_send_group_multi() helper: check function arguments
 */
static inline em_status_t
send_multi_check_args(const em_event_t events[], int num, em_queue_t queue,
		      bool *is_external__out /*out if EM_OK*/,
		      queue_elem_t **q_elem__out /*out if EM_OK*/)
{
	const bool is_external = queue_external(queue);
	queue_elem_t *q_elem = NULL;
	int i;

	if (EM_CHECK_LEVEL > 0 && unlikely(!events || num <= 0))
		return EM_ERR_BAD_ARG;

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
em_send_multi(const em_event_t events[], int num, em_queue_t queue)
{
	bool is_external = false; /* set by check_args */
	queue_elem_t *q_elem = NULL; /* set by check_args */
	int num_sent;
	int i;

	/*
	 * Check all args.
	 */
	em_status_t err =
	send_multi_check_args(events, num, queue,
			      /*out if EM_OK:*/ &is_external, &q_elem);
	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_SEND_MULTI,
			       "Invalid args: events:%p num:%d Q:%" PRI_QUEUE "",
			       events, num, queue);
		return 0;
	}

	/* Buffer events from EO-start sent to scheduled queues */
	if (unlikely(!is_external &&
		     q_elem->scheduled && em_locm.start_eo_elem)) {
		/*
		 * em_send_multi() called from within an EO-start function:
		 * all events sent to scheduled queues will be buffered
		 * and sent when the EO-start operation completes.
		 */
		num_sent = eo_start_buffer_events(events, num, queue,
						  EM_EVENT_GROUP_UNDEF);
		if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num))
			INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_MULTI,
				       "send-multi EO-start: req:%d, sent:%d",
				       num, num_sent);
		return num_sent;
	}

	event_hdr_t *ev_hdrs[num];

	event_to_hdr_multi(events, ev_hdrs, num);
	for (i = 0; i < num; i++)
		ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, EM_EVENT_GROUP_UNDEF);

	if (esv_enabled())
		evstate_usr2em_multi(events, ev_hdrs, num, EVSTATE__SEND_MULTI);

	if (is_external) {
		/*
		 * Send out of EM to another device via event-chaining and a
		 * user-provided function 'event_send_device_multi()'
		 */
		num_sent = send_chaining_multi(events, ev_hdrs, num, queue);
		if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
			INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
				       EM_ESCOPE_SEND_MULTI,
				       "send_chaining_multi: req:%d, sent:%d",
				       num, num_sent);
			goto send_multi_err;
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
	case EM_QUEUE_TYPE_UNSCHEDULED:
		num_sent = queue_unsched_enqueue_multi(events, num, q_elem);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		num_sent = send_local_multi(events, ev_hdrs, num, q_elem);
		break;
	case EM_QUEUE_TYPE_OUTPUT:
		num_sent = send_output_multi(events, ev_hdrs, num, q_elem);
		break;
	default:
		num_sent = 0;
		break;
	}

	if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_MULTI,
			       "send-multi failed: req:%d, sent:%d",
			       num, num_sent);
		goto send_multi_err;
	}

	return num_sent;

send_multi_err:
	if (esv_enabled()) {
		evstate_usr2em_revert_multi(&events[num_sent], &ev_hdrs[num_sent],
					    num - num_sent,
					    EVSTATE__SEND_MULTI__FAIL);
	}
	return num_sent;
}

void *
em_event_pointer(em_event_t event)
{
	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EVENT_POINTER,
			       "event undefined!");
		return NULL;
	}

	void *ev_ptr = event_pointer(event);

	if (unlikely(!ev_ptr))
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EVENT_POINTER,
			       "Event pointer NULL (unrecognized event type)");

	return ev_ptr;
}

size_t
em_event_get_size(em_event_t event)
{
	odp_event_t odp_event;
	odp_event_type_t odp_etype;

	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GET_SIZE,
			       "event undefined!");
		return 0;
	}

	odp_event = event_em2odp(event);
	odp_etype = odp_event_type(odp_event);

	if (odp_etype == ODP_EVENT_PACKET) {
		odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

		return odp_packet_seg_len(odp_pkt);
	} else if (odp_etype == ODP_EVENT_BUFFER) {
		const event_hdr_t *ev_hdr = event_to_hdr(event);

		return ev_hdr->event_size;
	}

	INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_EVENT_GET_SIZE,
		       "Unexpected odp event type:%u", odp_etype);
	return 0;
}

em_pool_t em_event_get_pool(em_event_t event)
{
	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GET_POOL,
			       "event undefined!");
		return EM_POOL_UNDEF;
	}

	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t type = odp_event_type(odp_event);
	odp_pool_t odp_pool = ODP_POOL_INVALID;

	if (type == ODP_EVENT_PACKET) {
		odp_packet_t pkt = odp_packet_from_event(odp_event);

		odp_pool = odp_packet_pool(pkt);
	} else if (type == ODP_EVENT_BUFFER) {
		odp_buffer_t buf = odp_buffer_from_event(odp_event);

		odp_pool = odp_buffer_pool(buf);
	}

	if (unlikely(odp_pool == ODP_POOL_INVALID))
		return EM_POOL_UNDEF;

	em_pool_t pool = pool_odp2em(odp_pool);

	/*
	 * Don't report an error if 'pool == EM_POOL_UNDEF' since that might
	 * happen if the event is input from pktio that is using external
	 * (to EM) odp pools.
	 */
	return pool;
}

em_status_t
em_event_set_type(em_event_t event, em_event_type_t newtype)
{
	event_hdr_t *ev_hdr;

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_BAD_ID,
				EM_ESCOPE_EVENT_SET_TYPE, "event undefined!")

	ev_hdr = event_to_hdr(event);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(ev_hdr == NULL, EM_ERR_BAD_POINTER,
				EM_ESCOPE_EVENT_SET_TYPE, "ev_hdr == NULL");

	ev_hdr->event_type = newtype;

	return EM_OK;
}

em_event_type_t
em_event_get_type(em_event_t event)
{
	const event_hdr_t *ev_hdr;

	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GET_TYPE,
			       "event undefined!");
		return EM_EVENT_TYPE_UNDEF;
	}

	ev_hdr = event_to_hdr(event);

	if (EM_CHECK_LEVEL > 0 && unlikely(ev_hdr == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EVENT_GET_TYPE,
			       "ev_hdr == NULL");
		return EM_EVENT_TYPE_UNDEF;
	}

	return ev_hdr->event_type;
}

int em_event_get_type_multi(const em_event_t events[], int num,
			    em_event_type_t types[/*out:num*/])
{
	int i;

	/* Check all args */
	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(!events || num < 0 || !types)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG,
				       EM_ESCOPE_EVENT_GET_TYPE_MULTI,
				       "Inv.args: events:%p num:%d types:%p",
				       events, num, types);
			return 0;
		}
		if (unlikely(!num))
			return 0;
	}

	if (EM_CHECK_LEVEL > 1) {
		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_POINTER,
				       EM_ESCOPE_EVENT_GET_TYPE_MULTI,
				       "events[%d] undefined!", i);
			return 0;
		}
	}

	event_hdr_t *ev_hdrs[num];

	event_to_hdr_multi(events, ev_hdrs, num);

	for (i = 0; i < num; i++)
		types[i] = ev_hdrs[i]->event_type;

	return num;
}

int em_event_same_type_multi(const em_event_t events[], int num,
			     em_event_type_t *same_type /*out*/)
{
	/* Check all args */
	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(!events || num < 0 || !same_type)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG,
				       EM_ESCOPE_EVENT_SAME_TYPE_MULTI,
				       "Inv.args: events:%p num:%d same_type:%p",
				       events, num, same_type);
			return 0;
		}
		if (unlikely(!num))
			return 0;
	}

	if (EM_CHECK_LEVEL > 1) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_POINTER,
				       EM_ESCOPE_EVENT_SAME_TYPE_MULTI,
				       "events[%d] undefined!", i);
			return 0;
		}
	}

	const em_event_type_t type = event_to_hdr(events[0])->event_type;
	int same = 1;

	for (; same < num && type == event_to_hdr(events[same])->event_type;
	     same++)
		;

	*same_type = type;
	return same;
}

em_status_t em_event_mark_send(em_event_t event, em_queue_t queue)
{
	if (!esv_enabled())
		return EM_OK;

	const queue_elem_t *const q_elem = queue_elem_get(queue);

	/* Check all args */
	if (EM_CHECK_LEVEL >= 1)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF || q_elem == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_SEND,
				"Inv.args: event:%" PRI_EVENT " Q:%" PRI_QUEUE "",
				event, queue);
	if (EM_CHECK_LEVEL >= 1)
		RETURN_ERROR_IF(!queue_allocated(q_elem) || !q_elem->scheduled,
				EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_MARK_SEND,
				"Inv.queue:%" PRI_QUEUE " type:%" PRI_QTYPE "",
				queue, q_elem->type);

	event_hdr_t *ev_hdr = event_to_hdr(event);

	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	evstate_usr2em(event, ev_hdr, EVSTATE__MARK_SEND);

	/*
	 * Data memory barrier, we are bypassing em_send(), odp_queue_enq()
	 * and need to guarantee memory sync before the event ends up into an
	 * EM queue again.
	 */
	odp_mb_full();

	return EM_OK;
}

em_status_t em_event_unmark_send(em_event_t event)
{
	if (!esv_enabled())
		return EM_OK;

	/* Check all args */
	if (EM_CHECK_LEVEL >= 1)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_SEND,
				"Inv.args: event:%" PRI_EVENT "", event);

	event_hdr_t *ev_hdr = event_to_hdr(event);

	evstate_unmark_send(event, ev_hdr);

	return EM_OK;
}

void em_event_mark_free(em_event_t event)
{
	if (!esv_enabled())
		return;

	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_FREE,
			       "Event undefined!");
		return;
	}

	event_hdr_t *const ev_hdr = event_to_hdr(event);

	evstate_free(event, ev_hdr, EVSTATE__MARK_FREE);
}

void em_event_unmark_free(em_event_t event)
{
	if (!esv_enabled())
		return;

	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_FREE,
			       "Event undefined!");
		return;
	}

	event_hdr_t *const ev_hdr = event_to_hdr(event);

	evstate_unmark_free(event, ev_hdr);
}

void em_event_mark_free_multi(const em_event_t events[], int num)
{
	if (!esv_enabled())
		return;

	if (unlikely(!events || num < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_FREE_MULTI,
			       "Inv.args: events[]:%p num:%d", events, num);
		return;
	}
	if (unlikely(num == 0))
		return;

	if (EM_CHECK_LEVEL > 1) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG,
				       EM_ESCOPE_EVENT_MARK_FREE_MULTI,
				       "events[%d] undefined!", i);
			return;
		}
	}

	event_hdr_t *ev_hdrs[num];

	event_to_hdr_multi(events, ev_hdrs, num);
	evstate_free_multi(events, ev_hdrs, num, EVSTATE__MARK_FREE_MULTI);
}

void em_event_unmark_free_multi(const em_event_t events[], int num)
{
	if (!esv_enabled())
		return;

	if (unlikely(!events || num < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_FREE_MULTI,
			       "Inv.args: events[]:%p num:%d", events, num);
		return;
	}
	if (unlikely(num == 0))
		return;

	if (EM_CHECK_LEVEL > 1) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG,
				       EM_ESCOPE_EVENT_UNMARK_FREE_MULTI,
				       "events[%d] undefined!", i);
			return;
		}
	}

	event_hdr_t *ev_hdrs[num];

	event_to_hdr_multi(events, ev_hdrs, num);
	evstate_unmark_free_multi(events, ev_hdrs, num);
}

em_event_t em_event_clone(em_event_t event, em_pool_t pool/*or EM_POOL_UNDEF*/)
{
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	/* Check all args */
	if (EM_CHECK_LEVEL >= 1 &&
	    unlikely(event == EM_EVENT_UNDEF ||
		     (pool != EM_POOL_UNDEF &&
		      (pool_elem == NULL || !pool_allocated(pool_elem))))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_CLONE,
			       "Inv.args: event:%" PRI_EVENT " pool:%" PRI_POOL "",
			       event, pool);
		return EM_EVENT_UNDEF;
	}

	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t odp_evtype = odp_event_type(odp_event);
	odp_pool_t odp_pool = ODP_POOL_INVALID;
	odp_packet_t pkt = ODP_PACKET_INVALID;
	odp_buffer_t buf = ODP_BUFFER_INVALID;

	if (unlikely(odp_evtype != ODP_EVENT_PACKET &&
		     odp_evtype != ODP_EVENT_BUFFER)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_CLONE,
			       "Inv. odp-event-type:%d", odp_evtype);
		return EM_EVENT_UNDEF;
	}

	/* Obtain the event-hdr, event-size and the pool to use */
	const event_hdr_t *ev_hdr;
	size_t size;
	em_event_type_t type;
	em_pool_t em_pool = pool;
	em_event_t clone_event; /* return value */

	if (odp_evtype == ODP_EVENT_PACKET) {
		pkt = odp_packet_from_event(odp_event);
		ev_hdr = odp_packet_user_area(pkt);
		size = odp_packet_seg_len(pkt);
		if (pool == EM_POOL_UNDEF) {
			odp_pool = odp_packet_pool(pkt);
			em_pool = pool_odp2em(odp_pool);
		}
	} else /* ODP_EVENT_BUFFER */ {
		buf = odp_buffer_from_event(odp_event);
		ev_hdr = odp_buffer_addr(buf);
		size = ev_hdr->event_size;
		if (pool == EM_POOL_UNDEF) {
			odp_pool = odp_buffer_pool(buf);
			em_pool = pool_odp2em(odp_pool);
		}
	}

	/* No EM-pool found */
	if (em_pool == EM_POOL_UNDEF) {
		if (unlikely(odp_evtype == ODP_EVENT_BUFFER)) {
			INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_EVENT_CLONE,
				       "No suitable event-pool found");
			return EM_EVENT_UNDEF;
		}
		/* odp_evtype == ODP_EVENT_PACKET:
		 * Not an EM-pool, e.g. event from external pktio odp-pool.
		 * Allocate and clone pkt via ODP directly.
		 */
		clone_event = pkt_clone_odp(pkt, odp_pool);
		if (unlikely(clone_event == EM_EVENT_UNDEF)) {
			INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_EVENT_CLONE,
				       "Cloning from ext odp-pool:%" PRIu64 " failed",
				       odp_pool_to_u64(odp_pool));
		}
		return clone_event;
	}

	/*
	 * Clone the event from an EM-pool:
	 */
	pool_elem = pool_elem_get(em_pool);
	type = ev_hdr->event_type;

	/* EM event pools created with type=SW can not support pkt events */
	if (unlikely(pool_elem->event_type == EM_EVENT_TYPE_SW &&
		     em_get_type_major(type) == EM_EVENT_TYPE_PACKET)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_EVENT_CLONE,
			       "EM-pool:%s(%" PRI_POOL "):\n"
			       "Invalid event type:0x%" PRIx32 " for buf",
			       pool_elem->name, em_pool, type);
		return EM_EVENT_UNDEF;
	}

	event_hdr_t *clone_hdr = event_alloc(pool_elem, size, type);

	if (unlikely(!clone_hdr)) {
		em_status_t err =
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EVENT_CLONE,
			       "EM-pool:'%s': sz:%zu type:0x%x pool:%" PRI_POOL "",
			       pool_elem->name, size, type, em_pool);
		if (EM_CHECK_LEVEL > 1 && err != EM_OK &&
		    em_shm->opt.pool.statistics_enable)
			em_pool_info_print(em_pool);
		return EM_EVENT_UNDEF;
	}

	clone_event = clone_hdr->event;
	/* Update clone_event ESV state for the clone-alloc */
	if (esv_enabled())
		clone_event = evstate_clone(clone_event, clone_hdr);

	/* Call the 'alloc' API hook function also for event-clone */
	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_alloc(&clone_event, 1, 1, size, type, pool);

	/* Copy event payload from the parent event into the clone event */
	const void *src = event_pointer(event);
	void *dst = event_pointer(clone_event);

	memcpy(dst, src, size);

	return clone_event;
}

static int event_uarea_init(em_event_t event, event_hdr_t **ev_hdr/*out*/)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t odp_evtype = odp_event_type(odp_event);
	odp_pool_t odp_pool = ODP_POOL_INVALID;
	odp_packet_t odp_pkt;
	odp_buffer_t odp_buf;
	event_hdr_t *hdr;
	bool is_init;

	switch (odp_evtype) {
	case ODP_EVENT_PACKET:
		odp_pkt = odp_packet_from_event(odp_event);
		hdr = odp_packet_user_area(odp_pkt);
		is_init = hdr->user_area.isinit;
		if (!is_init)
			odp_pool = odp_packet_pool(odp_pkt);
		break;
	case ODP_EVENT_BUFFER:
		odp_buf = odp_buffer_from_event(odp_event);
		hdr = odp_buffer_addr(odp_buf);
		is_init = hdr->user_area.isinit;
		if (!is_init)
			odp_pool = odp_buffer_pool(odp_buf);
		break;
	default:
		return -1;
	}

	*ev_hdr = hdr;

	if (!is_init) {
		/*
		 * Event user area metadata is not initialized in
		 * the event header - initialize it:
		 */
		hdr->user_area.all = 0; /* user_area.{} = all zero (.sizes=0) */
		hdr->user_area.isinit = 1;

		em_pool_t pool = pool_odp2em(odp_pool);

		if (pool == EM_POOL_UNDEF)
			return 0; /* ext ODP pool: OK, no user area, sz=0 */

		/* Event from an EM event pool, can init event user area */
		const mpool_elem_t *pool_elem = pool_elem_get(pool);

		if (unlikely(!pool_elem))
			return -2; /* invalid pool_elem */

		hdr->user_area.req_size = pool_elem->user_area.req_size;
		hdr->user_area.pad_size = pool_elem->user_area.pad_size;
	}

	return 0;
}

void *em_event_uarea_get(em_event_t event, size_t *size /*out, if given*/)
{
	/* Check args */
	if (EM_CHECK_LEVEL >= 1 &&
	    unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_GET,
			       "Inv.arg: event undef");
		goto no_uarea;
	}

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_EVENT_UAREA_GET,
			       "Cannot init event user area: %d", err);
		goto no_uarea;
	}

	if (ev_hdr->user_area.req_size == 0)
		goto no_uarea;

	/*
	 * Event has user area configured, return pointer and size
	 */
	void *uarea_ptr = (void *)((uintptr_t)ev_hdr + sizeof(event_hdr_t));

	if (size)
		*size = ev_hdr->user_area.req_size;

	return uarea_ptr;

no_uarea:
	if (size)
		*size = 0;
	return NULL;
}

em_status_t em_event_uarea_id_set(em_event_t event, uint16_t id)
{
	/* Check args */
	if (EM_CHECK_LEVEL >= 1)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_ID_SET,
				"Inv.arg: event undef");

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	RETURN_ERROR_IF(err, EM_ERR_OPERATION_FAILED,
			EM_ESCOPE_EVENT_UAREA_ID_SET,
			"Cannot init event user area: %d", err);

	ev_hdr->user_area.id = id;
	ev_hdr->user_area.isset_id = 1;

	return EM_OK;
}

em_status_t em_event_uarea_id_get(em_event_t event, bool *isset /*out*/,
				  uint16_t *id /*out*/)
{
	bool id_set = false;
	em_status_t status = EM_OK;

	/* Check args, either 'isset' or 'id' ptrs must be provided (or both) */
	if (EM_CHECK_LEVEL >= 1 &&
	    (event == EM_EVENT_UNDEF || !(id || isset))) {
		status = INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_ID_GET,
					"Inv.args: event:%" PRI_EVENT " isset:%p id:%p",
					event, isset, id);
		goto id_isset;
	}

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (unlikely(err)) {
		status = INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
					EM_ESCOPE_EVENT_UAREA_ID_GET,
					"Cannot init event user area: %d", err);
		goto id_isset;
	}

	if (ev_hdr->user_area.isset_id) {
		/* user-area-id has been set */
		id_set = true;
		if (id)
			*id = ev_hdr->user_area.id; /*out*/
	}

id_isset:
	if (isset)
		*isset = id_set; /*out*/
	return status;
}

em_status_t em_event_uarea_info(em_event_t event,
				em_event_uarea_info_t *uarea_info /*out*/)
{
	em_status_t status = EM_ERROR;

	/* Check args */
	if (EM_CHECK_LEVEL >= 1 &&
	    unlikely(event == EM_EVENT_UNDEF || !uarea_info)) {
		status = INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_INFO,
					"Inv.args: event:%" PRI_EVENT " uarea_info:%p",
					event, uarea_info);
		goto err_uarea;
	}

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (unlikely(err)) {
		status = INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
					EM_ESCOPE_EVENT_UAREA_INFO,
					"Cannot init event user area: %d", err);
		goto err_uarea;
	}

	if (ev_hdr->user_area.req_size == 0) {
		uarea_info->uarea = NULL;
		uarea_info->size = 0;
	} else {
		uarea_info->uarea = (void *)((uintptr_t)ev_hdr +
					     sizeof(event_hdr_t));
		uarea_info->size = ev_hdr->user_area.req_size;
	}

	if (ev_hdr->user_area.isset_id) {
		uarea_info->id.isset = true;
		uarea_info->id.value = ev_hdr->user_area.id;
	} else {
		uarea_info->id.isset = false;
		uarea_info->id.value = 0;
	}

	return EM_OK;

err_uarea:
	if (uarea_info) {
		uarea_info->uarea = NULL;
		uarea_info->size = 0;
		uarea_info->id.isset = false;
		uarea_info->id.value = 0;
	}
	return status;
}
