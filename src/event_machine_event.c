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

em_event_t em_alloc(uint32_t size, em_event_type_t type, em_pool_t pool)
{
	const mpool_elem_t *const pool_elem = pool_elem_get(pool);
	em_event_type_t major_type = em_event_type_major(type);

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(size == 0 || !pool_elem ||
		     em_event_type_major(type) == EM_EVENT_TYPE_TIMER)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ALLOC,
			       "Invalid args: size:%u type:%u pool:%" PRI_POOL "",
			       size, type, pool);
		return EM_EVENT_UNDEF;
	}
	if (EM_CHECK_LEVEL >= 2 && unlikely(!pool_allocated(pool_elem)))
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_ALLOC,
			       "Invalid pool:%" PRI_POOL ", pool not created", pool);

	/*
	 * EM event pools created with type=SW can not support pkt events.
	 */
	if (EM_CHECK_LEVEL >= 1 &&
	    unlikely(pool_elem->event_type == EM_EVENT_TYPE_SW &&
		     major_type == EM_EVENT_TYPE_PACKET)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC,
			       "EM-pool:%s(%" PRI_POOL "):\n"
			       "Invalid event type:0x%x for buf",
			       pool_elem->name, pool_elem->em_pool, type);
		return EM_EVENT_UNDEF;
	}
	if (EM_CHECK_LEVEL >= 1 &&
	    unlikely(pool_elem->event_type == EM_EVENT_TYPE_VECTOR &&
		     major_type != EM_EVENT_TYPE_VECTOR)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC,
			       "EM-pool:%s(%" PRI_POOL "):\n"
			       "Invalid event type:0x%x for vector",
			       pool_elem->name, pool_elem->em_pool, type);
		return EM_EVENT_UNDEF;
	}

	const em_event_t event = event_alloc(pool_elem, size, type, EVSTATE__ALLOC);

	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		em_status_t err =
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_ALLOC,
			       "EM-pool:'%s': sz:%u type:0x%x pool:%" PRI_POOL "",
			       pool_elem->name, size, type, pool);
		if (EM_DEBUG_PRINT && err != EM_OK &&
		    (pool_elem->stats_opt.bit.available ||
		     pool_elem->stats_opt.bit.cache_available)) {
			em_pool_info_print(pool);
		}
		return EM_EVENT_UNDEF;
	}

	if (EM_API_HOOKS_ENABLE && event != EM_EVENT_UNDEF)
		call_api_hooks_alloc(&event, 1, 1, size, type, pool);

	return event;
}

int em_alloc_multi(em_event_t events[/*out*/], int num,
		   uint32_t size, em_event_type_t type, em_pool_t pool)
{
	if (unlikely(num == 0))
		return 0;

	const mpool_elem_t *const pool_elem = pool_elem_get(pool);
	int ret = 0;

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(!events || num < 0 || size == 0 || !pool_elem ||
		     em_event_type_major(type) == EM_EVENT_TYPE_TIMER)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ALLOC_MULTI,
			       "Invalid args: events:%p num:%d size:%u type:%u pool:%" PRI_POOL "",
			       events, num, size, type, pool);
		return 0;
	}
	if (EM_CHECK_LEVEL >= 2 && unlikely(!pool_allocated(pool_elem)))
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_ALLOC_MULTI,
			       "Invalid pool:%" PRI_POOL ", pool not created", pool);

	if (pool_elem->event_type == EM_EVENT_TYPE_PACKET) {
		/*
		 * EM event pools created with type=PKT can support SW events
		 * as well as pkt events.
		 */
		ret = event_alloc_pkt_multi(events, num, pool_elem, size, type);
	} else if (pool_elem->event_type == EM_EVENT_TYPE_SW) {
		/*
		 * EM event pools created with type=SW can not support
		 * pkt events.
		 */
		if (EM_CHECK_LEVEL >= 1 &&
		    unlikely(em_event_type_major(type) == EM_EVENT_TYPE_PACKET)) {
			INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC_MULTI,
				       "EM-pool:%s(%" PRI_POOL "): Invalid event type:0x%x for buf",
				       pool_elem->name, pool, type);
			return 0;
		}
		ret = event_alloc_buf_multi(events, num, pool_elem, size, type);
	} else if (pool_elem->event_type == EM_EVENT_TYPE_VECTOR) {
		if (EM_CHECK_LEVEL >= 1 &&
		    unlikely(em_event_type_major(type) != EM_EVENT_TYPE_VECTOR)) {
			INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC,
				       "EM-pool:%s(%" PRI_POOL "): Inv. event type:0x%x for vector",
				       pool_elem->name, pool, type);
			return 0;
		}
		ret = event_alloc_vector_multi(events, num, pool_elem, size, type);
	}

	if (unlikely(EM_CHECK_LEVEL > 0 && ret != num)) {
		em_status_t err =
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_ALLOC_MULTI,
			       "Requested num:%d events, allocated:%d\n"
			       "EM-pool:'%s': sz:%u type:0x%x pool:%" PRI_POOL "",
			       num, ret,
			       pool_elem->name, size, type, pool);
		if (EM_DEBUG_PRINT && err != EM_OK &&
		    (pool_elem->stats_opt.bit.available ||
		     pool_elem->stats_opt.bit.cache_available)) {
			em_pool_info_print(pool);
		}
	}

	if (EM_API_HOOKS_ENABLE && ret > 0)
		call_api_hooks_alloc(events, ret, num, size, type, pool);

	return ret;
}

/**
 * @brief Helper to check if the event is a vector
 *
 * @param vector_event  Event handle
 * @return true   the event is a vector
 * @return false  the event is NOT a vector
 */
static inline bool is_vector_type(em_event_t vector_event)
{
	odp_event_t odp_event = event_em2odp(vector_event);
	odp_event_type_t odp_etype = odp_event_type(odp_event);

	if (odp_etype == ODP_EVENT_PACKET_VECTOR)
		return true;

	return false;
}

/**
 * @brief Helper to check if the event is a vector, if not report an error
 *
 * @param vector_event  Event handle
 * @param escope        Error scope to use if reporting an error
 * @return true   the event is a vector
 * @return false  the event is NOT a vector, reports an error
 */
static inline bool is_vector_type_or_error(em_event_t vector_event,
					   em_escope_t escope)
{
	bool is_vec = is_vector_type(vector_event);

	if (likely(is_vec))
		return true;

	INTERNAL_ERROR(EM_ERR_BAD_TYPE, escope, "Event not a vector");
	return false;
}

/**
 * @brief Handle ESV state for 'em_free' for the event-table of a vector event
 *
 * @param event  Vector event handle
 */
static void event_vector_prepare_free_full(em_event_t event, const uint16_t api_op)
{
	/* em_free() frees the vector as well as all the events it contains */
	em_event_t *ev_tbl;
	uint32_t sz = event_vector_tbl(event, &ev_tbl);

	if (sz) {
		event_hdr_t *ev_hdrs[sz];

		event_to_hdr_multi(ev_tbl, ev_hdrs, sz);
		evstate_free_multi(ev_tbl, ev_hdrs, sz, api_op);

		/* drop ESV generation from event handles */
		(void)events_em2pkt_inplace(ev_tbl, sz);
	}
}

/**
 * @brief Handle ESV state for 'em_event_unmark_free/_multi' for the event-table
 *        of a vector event.
 *
 * @param event  Vector event handle
 */
static void event_vector_prepare_free_full__revert(em_event_t event, const uint16_t api_op)
{
	/* em_free() frees the vector as well as all the events it contains */
	em_event_t *ev_tbl;
	uint32_t sz = event_vector_tbl(event, &ev_tbl);

	if (sz) {
		event_hdr_t *ev_hdrs[sz];

		event_to_hdr_multi(ev_tbl, ev_hdrs, sz);
		evstate_unmark_free_multi(ev_tbl, ev_hdrs, sz, api_op);

		/* restore dropped ESV generation to event handles, unmodified in header */
		for (unsigned int i = 0; i < sz; i++)
			ev_tbl[i] = ev_hdrs[i]->event;
	}
}

void em_free(em_event_t event)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_FREE,
			       "event undefined!");
		return;
	}

	event_hdr_t *ev_hdr = NULL;

	if (esv_enabled()) {
		ev_hdr = event_to_hdr(event);
		evstate_free(event, ev_hdr, EVSTATE__FREE);

		if (is_vector_type(event))
			event_vector_prepare_free_full(event, EVSTATE__FREE);
	}

	odp_event_t odp_event = event_em2odp(event);

	if (EM_CHECK_LEVEL > 0 && unlikely(odp_event_type(odp_event) == ODP_EVENT_TIMEOUT)) {
		if (!ev_hdr)
			ev_hdr = event_to_hdr(event);
		if (unlikely(ev_hdr->flags.tmo_type != EM_TMO_TYPE_NONE)) {
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_FREE,
				       "Can't free active TIMER event");
			return;
		}
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(&event, 1);

	odp_event_free(odp_event);
}

/* helper to remove active TIMER events from free list */
static inline int remove_active_timers(int num, odp_event_t odp_evtbl[/*in/out*/],
				       event_hdr_t *ev_hdr_tbl[/*in/out*/],
				       em_event_t ev_tbl[/*in/out*/])
{
	for (int i = 0; i < num; i++) {
		if (unlikely(odp_event_type(odp_evtbl[i]) == ODP_EVENT_TIMEOUT &&
			     ev_hdr_tbl[i]->flags.tmo_type != EM_TMO_TYPE_NONE)) {
			for (int j = i; j + 1 < num; j++) {
				odp_evtbl[j] = odp_evtbl[j + 1];
				ev_hdr_tbl[j] = ev_hdr_tbl[j + 1];
				ev_tbl[j] = ev_tbl[j + 1];
			}
			num--;
		}
	}

	return num;
}

void em_free_multi(em_event_t events[], int num)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(!events || num < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_FREE_MULTI,
			       "Inv.args: events[]:%p num:%d", events, num);
		return;
	}
	if (unlikely(num == 0))
		return;

	if (EM_CHECK_LEVEL >= 3) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_FREE_MULTI,
				       "events[%d] undefined!", i);
			return;
		}
	}

	int num_free = num;
	const bool esv_ena = esv_enabled();
	odp_event_t odp_events[num];

	events_em2odp(events, odp_events/*out*/, num);

	if (EM_CHECK_LEVEL > 1 || esv_ena) {
		event_hdr_t *ev_hdrs[num];

		event_to_hdr_multi(events, ev_hdrs, num);

		if (EM_CHECK_LEVEL > 1) {
			num_free = remove_active_timers(num, odp_events, ev_hdrs, events);
			if (unlikely(num_free != num))
				INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_FREE_MULTI,
					       "Can't free active TIMER events: %d of %d ignored",
					       num_free, num);
		}

		if (esv_ena) {
			evstate_free_multi(events, ev_hdrs, num_free, EVSTATE__FREE_MULTI);

			for (int i = 0; i < num_free; i++) {
				if (is_vector_type(events[i]))
					event_vector_prepare_free_full(events[i],
								       EVSTATE__FREE_MULTI);
			}
		}
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(events, num_free);

	odp_event_free_multi(odp_events, num_free);
}

/**
 * Helper to em_send().
 * Send out of EM via event-chaining and a user-provided function
 * 'event_send_device()' to another device
 */
static inline em_status_t
send_external(em_event_t event, em_queue_t queue)
{
	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, EM_EVENT_GROUP_UNDEF);

	em_status_t stat = send_chaining(event, queue);

	if (EM_CHECK_LEVEL == 0)
		return stat;

	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_SEND,
			"send out-of-EM via event-chaining failed: Q:%" PRI_QUEUE "", queue);
	return EM_OK;
}

/**
 * Helper to em_send_multi().
 * Send out of EM via event-chaining and a user-provided function
 * 'event_send_device()' to another device
 */
static inline int
send_external_multi(const em_event_t events[], int num, em_queue_t queue)
{
	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, EM_EVENT_GROUP_UNDEF);

	int num_sent = send_chaining_multi(events, num, queue);

	if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_SEND_MULTI,
			       "send_chaining_multi: req:%d, sent:%d",
			       num, num_sent);
	}

	return num_sent;
}

/**
 * Helper to em_send().
 * Send to an EM internal queue.
 */
static inline em_status_t
send_internal(em_event_t event, event_hdr_t *ev_hdr, em_queue_t queue)
{
	queue_elem_t *q_elem = queue_elem_get(queue);
	em_status_t stat;

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !q_elem,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND,
			"Invalid queue:%" PRI_QUEUE "", queue);
	RETURN_ERROR_IF(EM_CHECK_LEVEL >= 2 && !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_SEND,
			"Invalid queue:%" PRI_QUEUE "", queue);

	/* Buffer events sent from EO-start to scheduled queues */
	if (unlikely(em_locm.start_eo_elem && q_elem->flags.scheduled)) {
		/*
		 * em_send() called from within an EO-start function:
		 * all events sent to scheduled queues will be buffered
		 * and sent when the EO-start operation completes.
		 */
		if (esv_enabled())
			evstate_usr2em(event, ev_hdr, EVSTATE__SEND);

		int num_sent = eo_start_buffer_events(&event, 1, queue);

		if (unlikely(num_sent != 1)) {
			stat = EM_ERR_OPERATION_FAILED;
			goto error_return;
		}

		return EM_OK; /* Success */
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, EM_EVENT_GROUP_UNDEF);

	if (q_elem->type == EM_QUEUE_TYPE_OUTPUT) {
		/*
		 * Send out of EM via an EM output-queue and a user provided
		 * function of type em_output_func_t
		 */
		stat = send_output(event, q_elem);

		if (unlikely(stat != EM_OK))
			goto error_return_noesv;

		return EM_OK; /* Success */
	}

	/*
	 * Normal send to a queue on this device
	 */
	if (esv_enabled())
		evstate_usr2em(event, ev_hdr, EVSTATE__SEND);

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
		evstate_usr2em_revert(event, ev_hdr, EVSTATE__SEND__FAIL);
error_return_noesv:
	if (EM_CHECK_LEVEL == 0)
		return stat;
	stat = INTERNAL_ERROR(stat, EM_ESCOPE_SEND,
			      "send failed: Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
			      queue, q_elem->type);
	return stat;
}

/**
 * Helper to em_send_multi().
 * Send to an EM internal queue.
 */
static inline int
send_internal_multi(const em_event_t events[], event_hdr_t *ev_hdrs[],
		    int num, em_queue_t queue)
{
	queue_elem_t *q_elem = queue_elem_get(queue);
	int num_sent;

	if (EM_CHECK_LEVEL > 0 && unlikely(!q_elem)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_SEND_MULTI,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return 0;
	}
	if (EM_CHECK_LEVEL >= 2 && unlikely(!queue_allocated(q_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_SEND_MULTI,
			       "Invalid queue:%" PRI_QUEUE "", queue);
		return 0;
	}

	/* Buffer events sent from EO-start to scheduled queues */
	if (unlikely(em_locm.start_eo_elem && q_elem->flags.scheduled)) {
		/*
		 * em_send_multi() called from within an EO-start function:
		 * all events sent to scheduled queues will be buffered
		 * and sent when the EO-start operation completes.
		 */
		if (esv_enabled())
			evstate_usr2em_multi(events, ev_hdrs, num,
					     EVSTATE__SEND_MULTI);
		num_sent = eo_start_buffer_events(events, num, queue);

		if (unlikely(num_sent != num))
			goto error_return;

		return num_sent; /* Success */
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, EM_EVENT_GROUP_UNDEF);

	if (q_elem->type == EM_QUEUE_TYPE_OUTPUT) {
		/*
		 * Send out of EM via an EM output-queue and a user provided
		 * function of type em_output_func_t
		 */
		num_sent = send_output_multi(events, num, q_elem);

		if (unlikely(num_sent != num))
			goto error_return_noesv;

		return num_sent; /* Success */
	}

	/*
	 * Normal send to a queue on this device
	 */
	if (esv_enabled())
		evstate_usr2em_multi(events, ev_hdrs, num, EVSTATE__SEND_MULTI);

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
					    EVSTATE__SEND_MULTI__FAIL);
error_return_noesv:
	if (EM_CHECK_LEVEL > 0)
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_MULTI,
			       "send-multi failed: req:%d, sent:%d",
			       num, num_sent);
	return num_sent;
}

em_status_t em_send(em_event_t event, em_queue_t queue)
{
	const bool is_external = queue_external(queue);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND, "Invalid event");

	event_hdr_t *ev_hdr = event_to_hdr(event);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_TIMER,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND, "Timer event can't be sent");

	/* avoid unnecessary writing 'undef' in case event is a ref */
	if (ev_hdr->egrp != EM_EVENT_GROUP_UNDEF)
		ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	/*
	 * External queue belongs to another EM instance, send out via EMC/BIP
	 */
	if (is_external)
		return send_external(event, queue);

	/*
	 * Queue belongs to this EM instance
	 */
	return send_internal(event, ev_hdr, queue);
}

/*
 * em_send_group_multi() helper: check events
 */
static inline em_status_t
send_multi_check_events(const em_event_t events[], int num)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(!events || num <= 0))
		return EM_ERR_BAD_ARG;

	if (EM_CHECK_LEVEL >= 3) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num))
			return EM_ERR_BAD_POINTER;
	}

	return EM_OK;
}

int em_send_multi(const em_event_t events[], int num, em_queue_t queue)
{
	const bool is_external = queue_external(queue);
	event_hdr_t *ev_hdrs[num];

	/* Check events */
	em_status_t err = send_multi_check_events(events, num);

	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_SEND_MULTI,
			       "Invalid events:%p num:%d", events, num);
		return 0;
	}

	event_to_hdr_multi(events, ev_hdrs, num);

	for (int i = 0; i < num; i++) {
		if (EM_CHECK_LEVEL > 0 && unlikely(ev_hdrs[i]->event_type == EM_EVENT_TYPE_TIMER)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_SEND_MULTI,
				       "Timer event[%d] can't be sent", i);
			return 0;
		}
		/* avoid unnecessary writing 'undef' in case event is a ref */
		if (ev_hdrs[i]->egrp != EM_EVENT_GROUP_UNDEF)
			ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;
	}

	/*
	 * External queue belongs to another EM instance, send out via EMC/BIP
	 */
	if (is_external)
		return send_external_multi(events, num, queue);

	/*
	 * Queue belongs to this EM instance
	 */
	return send_internal_multi(events, ev_hdrs, num, queue);
}

void *em_event_pointer(em_event_t event)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_POINTER,
			       "event undefined!");
		return NULL;
	}

	void *ev_ptr = event_pointer(event);

	if (EM_CHECK_LEVEL > 0 && unlikely(!ev_ptr))
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EVENT_POINTER,
			       "Event pointer NULL (unsupported event type)");

	return ev_ptr;
}

uint32_t em_event_get_size(em_event_t event)
{
	if (unlikely(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GET_SIZE,
			       "event undefined!");
		return 0;
	}

	const odp_event_t odp_event = event_em2odp(event);
	const odp_event_type_t odp_etype = odp_event_type(odp_event);

	if (odp_etype == ODP_EVENT_PACKET) {
		odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

		return odp_packet_seg_len(odp_pkt);
	} else if (odp_etype == ODP_EVENT_BUFFER) {
		odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);
		const event_hdr_t *ev_hdr = odp_buffer_user_area(odp_buf);

		return ev_hdr->event_size;
	} else if (odp_etype == ODP_EVENT_TIMEOUT) {
		return 0;
	}

	if (EM_CHECK_LEVEL > 0)
		INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_EVENT_GET_SIZE,
			       "Unexpected odp event type:%u", odp_etype);
	return 0;
}

em_pool_t em_event_get_pool(em_event_t event)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
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
	} else if (type == ODP_EVENT_PACKET_VECTOR) {
		odp_packet_vector_t pktvec = odp_packet_vector_from_event(odp_event);

		odp_pool = odp_packet_vector_pool(pktvec);
	} else if (type == ODP_EVENT_TIMEOUT) {
		return EM_POOL_UNDEF;
	}

	if (unlikely(odp_pool == ODP_POOL_INVALID))
		return EM_POOL_UNDEF;

	em_pool_t pool = pool_odp2em(odp_pool);

	/*
	 * Don't report an error if 'pool == EM_POOL_UNDEF' since that might
	 * happen if the event is e.g. input from pktio that is using external
	 * (to EM) odp pools.
	 */
	return pool;
}

em_status_t em_event_set_type(em_event_t event, em_event_type_t newtype)
{
	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_BAD_ARG,
				EM_ESCOPE_EVENT_SET_TYPE, "event undefined!");

	/* similar to 'ev_hdr = event_to_hdr(event)', slightly extended: */
	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t evtype = odp_event_type(odp_event);
	event_hdr_t *ev_hdr;

	switch (evtype) {
	case ODP_EVENT_PACKET: {
		odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

		ev_hdr = odp_packet_user_area(odp_pkt);
		break;
	}
	case ODP_EVENT_BUFFER: {
		odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);

		ev_hdr = odp_buffer_user_area(odp_buf);
		break;
	}
	case ODP_EVENT_PACKET_VECTOR: {
		odp_packet_vector_t odp_pktvec = odp_packet_vector_from_event(odp_event);
		em_event_type_t new_major = em_event_type_major(newtype);

		if (EM_CHECK_LEVEL >= 1)
			RETURN_ERROR_IF(new_major != EM_EVENT_TYPE_VECTOR,
					EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_SET_TYPE,
					"Event type:0x%x not suitable for a vector", newtype);
		ev_hdr = odp_packet_vector_user_area(odp_pktvec);
		break;
	}
	default:
		return INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_EVENT_SET_TYPE,
				      "Unsupported odp event type:%u", evtype);
	}

	ev_hdr->event_type = newtype;

	return EM_OK;
}

em_event_type_t em_event_get_type(em_event_t event)
{
	const event_hdr_t *ev_hdr;

	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GET_TYPE,
			       "event undefined!");
		return EM_EVENT_TYPE_UNDEF;
	}

	ev_hdr = event_to_hdr(event);

	if (EM_CHECK_LEVEL >= 3 && unlikely(ev_hdr == NULL)) {
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

	if (EM_CHECK_LEVEL >= 3) {
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

	if (EM_CHECK_LEVEL >= 3) {
		int i;

		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG,
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

	/* Check all args */
	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_SEND,
				"Inv.args: event:%" PRI_EVENT "", event);
	if (EM_CHECK_LEVEL >= 3) {
		const queue_elem_t *const q_elem = queue_elem_get(queue);

		RETURN_ERROR_IF(!q_elem, EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_SEND,
				"Inv.args: Q:%" PRI_QUEUE "", queue);
		RETURN_ERROR_IF(!queue_allocated(q_elem) || !q_elem->flags.scheduled,
				EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_MARK_SEND,
				"Inv.queue:%" PRI_QUEUE " type:%" PRI_QTYPE "",
				queue, q_elem->type);
	}

	event_hdr_t *ev_hdr = event_to_hdr(event);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_TIMER,
			EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_SEND, "TIMER event not allowed");

	/* avoid unnecessary writing 'undef' in case event is a ref */
	if (ev_hdr->egrp != EM_EVENT_GROUP_UNDEF)
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
	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_SEND,
				"Inv.args: event:%" PRI_EVENT "", event);

	event_hdr_t *ev_hdr = event_to_hdr(event);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_TIMER,
			EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_SEND, "TIMER event not allowed");

	evstate_unmark_send(event, ev_hdr);

	return EM_OK;
}

void em_event_mark_free(em_event_t event)
{
	if (!esv_enabled())
		return;

	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_FREE,
			       "Event undefined!");
		return;
	}

	event_hdr_t *const ev_hdr = event_to_hdr(event);

	if (EM_CHECK_LEVEL > 0 && unlikely(ev_hdr->event_type == EM_EVENT_TYPE_TIMER)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_FREE,
			       "TIMER event not allowed");
		return;
	}

	evstate_free(event, ev_hdr, EVSTATE__MARK_FREE);

	if (is_vector_type(event))
		event_vector_prepare_free_full(event, EVSTATE__MARK_FREE);
}

void em_event_unmark_free(em_event_t event)
{
	if (!esv_enabled())
		return;

	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_FREE,
			       "Event undefined!");
		return;
	}

	event_hdr_t *const ev_hdr = event_to_hdr(event);

	if (EM_CHECK_LEVEL > 0 && unlikely(ev_hdr->event_type == EM_EVENT_TYPE_TIMER)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_FREE,
			       "TIMER event not allowed");
		return;
	}

	evstate_unmark_free(event, ev_hdr, EVSTATE__UNMARK_FREE);
	if (is_vector_type(event))
		event_vector_prepare_free_full__revert(event, EVSTATE__UNMARK_FREE);
}

void em_event_mark_free_multi(const em_event_t events[], int num)
{
	if (!esv_enabled())
		return;

	if (EM_CHECK_LEVEL > 0 && unlikely(!events || num < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_FREE_MULTI,
			       "Inv.args: events[]:%p num:%d", events, num);
		return;
	}
	if (unlikely(num == 0))
		return;

	if (EM_CHECK_LEVEL >= 3) {
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

	for (int i = 0; i < num; i++) {
		if (EM_CHECK_LEVEL > 0 &&
		    unlikely(ev_hdrs[i]->event_type == EM_EVENT_TYPE_TIMER)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_MARK_FREE_MULTI,
				       "TIMER event[%d] not allowed", i);
			continue;
		}

		evstate_free(events[i], ev_hdrs[i], EVSTATE__MARK_FREE_MULTI);
		if (is_vector_type(events[i]))
			event_vector_prepare_free_full(events[i], EVSTATE__MARK_FREE_MULTI);
	}
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

	if (EM_CHECK_LEVEL >= 3) {
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

	for (int i = 0; i < num; i++) {
		if (EM_CHECK_LEVEL > 0 &&
		    unlikely(ev_hdrs[i]->event_type == EM_EVENT_TYPE_TIMER)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UNMARK_FREE_MULTI,
				       "TIMER event[%d] not allowed", i);
			continue;
		}

		evstate_unmark_free(events[i], ev_hdrs[i], EVSTATE__UNMARK_FREE_MULTI);
		if (is_vector_type(events[i]))
			event_vector_prepare_free_full__revert(events[i],
							       EVSTATE__UNMARK_FREE_MULTI);
	}
}

em_event_t em_event_clone(em_event_t event, em_pool_t pool/*or EM_POOL_UNDEF*/)
{
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	/* Check all args */
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(event == EM_EVENT_UNDEF ||
		     (pool != EM_POOL_UNDEF && !pool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_CLONE,
			       "Inv.args: event:%" PRI_EVENT " pool:%" PRI_POOL "",
			       event, pool);
		return EM_EVENT_UNDEF;
	}

	if (EM_CHECK_LEVEL >= 2 &&
	    unlikely(pool_elem && !pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_CLONE,
			       "Inv.args: pool:%" PRI_POOL " not created", pool);
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
	uint32_t size;
	em_event_type_t type;
	em_pool_t em_pool = pool;
	event_hdr_t *clone_hdr;
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
		ev_hdr = odp_buffer_user_area(buf);
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
	if (em_pool != pool)
		pool_elem = pool_elem_get(em_pool);
	type = ev_hdr->event_type;

	/* EM event pools created with type=SW can not support pkt events */
	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     pool_elem->event_type == EM_EVENT_TYPE_SW &&
		     em_event_type_major(type) == EM_EVENT_TYPE_PACKET)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_EVENT_CLONE,
			       "EM-pool:%s(%" PRI_POOL "):\n"
			       "Invalid event type:0x%x for buf",
			       pool_elem->name, em_pool, type);
		return EM_EVENT_UNDEF;
	}

	if (pool_elem->event_type == EM_EVENT_TYPE_PACKET)
		clone_hdr = event_alloc_pkt(pool_elem, size);
	else /* EM_EVENT_TYPE_SW */
		clone_hdr = event_alloc_buf(pool_elem, size);

	if (EM_CHECK_LEVEL > 0 && unlikely(!clone_hdr)) {
		em_status_t err = INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EVENT_CLONE,
						 "EM-pool:'%s': sz:%u type:0x%x pool:%" PRI_POOL "",
						 pool_elem->name, size, type, em_pool);
		if (EM_DEBUG_PRINT && err != EM_OK &&
		    (pool_elem->stats_opt.bit.available ||
		     pool_elem->stats_opt.bit.cache_available))
			em_pool_info_print(em_pool);
		return EM_EVENT_UNDEF;
	}

	/* Update event ESV state for alloc/clone */
	if (esv_enabled())
		(void)evstate_alloc(clone_hdr->event, clone_hdr, EVSTATE__EVENT_CLONE);

	clone_hdr->flags.all = 0; /* clear only after evstate_alloc() */
	clone_hdr->event_type = type; /* store the event type */
	clone_hdr->event_size = size; /* store requested size */
	clone_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	/* Copy the uarea meta-data */
	clone_hdr->user_area.all = ev_hdr->user_area.all;
	/* Copy the event uarea content if used */
	if (ev_hdr->user_area.isinit && ev_hdr->user_area.size > 0) {
		const void *uarea_ptr = (void *)((uintptr_t)ev_hdr + sizeof(event_hdr_t));
		void *clone_uarea_ptr = (void *)((uintptr_t)clone_hdr + sizeof(event_hdr_t));

		memcpy(clone_uarea_ptr, uarea_ptr, ev_hdr->user_area.size);
	}

	clone_event = clone_hdr->event;

	/* Copy event payload from the parent event into the clone event */
	const void *src = event_pointer(event);
	void *dst = event_pointer(clone_event);

	memcpy(dst, src, size);

	/* Call the 'alloc' API hook function also for event-clone */
	if (EM_API_HOOKS_ENABLE && clone_event != EM_EVENT_UNDEF)
		call_api_hooks_alloc(&clone_event, 1, 1, size, type, pool);

	return clone_event;
}

static inline int
event_uarea_init(em_event_t event, event_hdr_t **ev_hdr/*out*/)
{
	const odp_event_t odp_event = event_em2odp(event);
	const odp_event_type_t odp_evtype = odp_event_type(odp_event);
	odp_pool_t odp_pool = ODP_POOL_INVALID;
	odp_packet_t odp_pkt;
	odp_buffer_t odp_buf;
	odp_packet_vector_t odp_pktvec;
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
		hdr = odp_buffer_user_area(odp_buf);
		is_init = hdr->user_area.isinit;
		if (!is_init)
			odp_pool = odp_buffer_pool(odp_buf);
		break;
	case ODP_EVENT_PACKET_VECTOR:
		odp_pktvec = odp_packet_vector_from_event(odp_event);
		hdr = odp_packet_vector_user_area(odp_pktvec);
		is_init = hdr->user_area.isinit;
		if (!is_init)
			odp_pool = odp_packet_vector_pool(odp_pktvec);
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

		hdr->user_area.size = pool_elem->user_area.size;
	}

	return 0;
}

void *em_event_uarea_get(em_event_t event, size_t *size /*out, if given*/)
{
	/* Check args */
	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_GET,
			       "Inv.arg: event undef");
		goto no_uarea;
	}

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (EM_CHECK_LEVEL > 0 && unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_EVENT_UAREA_GET,
			       "Cannot init event user area: %d", err);
		goto no_uarea;
	}

	if (ev_hdr->user_area.size == 0)
		goto no_uarea;

	/*
	 * Event has user area configured, return pointer and size
	 */
	void *uarea_ptr = (void *)((uintptr_t)ev_hdr + sizeof(event_hdr_t));

	if (size)
		*size = ev_hdr->user_area.size;

	return uarea_ptr;

no_uarea:
	if (size)
		*size = 0;
	return NULL;
}

em_status_t em_event_uarea_id_set(em_event_t event, uint16_t id)
{
	/* Check args */
	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_ID_SET,
				"Inv.arg: event undef");

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (EM_CHECK_LEVEL > 0)
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
	if (EM_CHECK_LEVEL > 0 &&
	    (event == EM_EVENT_UNDEF || !(id || isset))) {
		status = INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_ID_GET,
					"Inv.args: event:%" PRI_EVENT " isset:%p id:%p",
					event, isset, id);
		goto id_isset;
	}

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (EM_CHECK_LEVEL > 0 && unlikely(err)) {
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
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(event == EM_EVENT_UNDEF || !uarea_info)) {
		status = INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_UAREA_INFO,
					"Inv.args: event:%" PRI_EVENT " uarea_info:%p",
					event, uarea_info);
		goto err_uarea;
	}

	event_hdr_t *ev_hdr = NULL;
	int err = event_uarea_init(event, &ev_hdr/*out*/);

	if (EM_CHECK_LEVEL > 0 && unlikely(err)) {
		status = INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
					EM_ESCOPE_EVENT_UAREA_INFO,
					"Cannot init event user area: %d", err);
		goto err_uarea;
	}

	if (ev_hdr->user_area.size == 0) {
		uarea_info->uarea = NULL;
		uarea_info->size = 0;
	} else {
		uarea_info->uarea = (void *)((uintptr_t)ev_hdr +
					     sizeof(event_hdr_t));
		uarea_info->size = ev_hdr->user_area.size;
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

em_event_t em_event_ref(em_event_t event)
{
	/* Check args */
	if (unlikely(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_REF,
			       "Invalid arg: event:%" PRI_EVENT "", event);
		return EM_EVENT_UNDEF;
	}

	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t odp_etype = odp_event_type(odp_event);

	if (EM_CHECK_LEVEL > 0 && unlikely(odp_etype != ODP_EVENT_PACKET)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_EVENT_REF,
			       "Event not a packet! Refs not supported for odp-events of type:%d",
			       odp_etype);
		return EM_EVENT_UNDEF;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	odp_packet_t pkt_ref = odp_packet_ref_static(odp_pkt);
	event_hdr_t *ev_hdr = odp_packet_user_area(odp_pkt);

	if (EM_CHECK_LEVEL > 0 && unlikely(pkt_ref == ODP_PACKET_INVALID)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_EVENT_REF,
			       "ODP failure in odp_packet_ref_static()");
		return EM_EVENT_UNDEF;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && odp_pkt != pkt_ref)) {
		INTERNAL_ERROR(EM_FATAL(EM_ERR_NOT_IMPLEMENTED), EM_ESCOPE_EVENT_REF,
			       "EM assumes all refs use the same handle");
		odp_packet_free(odp_pkt);
		return EM_EVENT_UNDEF;
	}

	/*
	 * Indicate that this event has references and some of the ESV checks
	 * must be omitted (evgen) - 'refs_used' will be set for the whole
	 * lifetime of this event, i.e. until the event is freed back into the
	 * pool. Important only for the first call of em_event_ref(), subsequent
	 * calls write same value.
	 */
	ev_hdr->flags.refs_used = 1;

	em_event_t ref = event;

	if (esv_enabled())
		ref = evstate_ref(event, ev_hdr);

	return ref;
}

bool em_event_has_ref(em_event_t event)
{
	/* Check args */
	if (unlikely(EM_CHECK_LEVEL > 0 && event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_HAS_REF,
			       "Invalid arg: event:%" PRI_EVENT "", event);
		return false;
	}

	return event_has_ref(event);
}

void em_event_vector_free(em_event_t vector_event)
{
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(vector_event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_VECTOR_FREE,
			       "Invalid args: vector_event:%" PRI_EVENT "",
			       vector_event);
		return;
	}

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(!is_vector_type_or_error(vector_event, EM_ESCOPE_EVENT_VECTOR_FREE))) {
		return;
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(&vector_event, 1);

	if (esv_enabled()) {
		event_hdr_t *const ev_hdr = eventvec_to_hdr(vector_event);

		evstate_free(vector_event, ev_hdr, EVSTATE__EVENT_VECTOR_FREE);
	}

	odp_event_t odp_event = event_em2odp(vector_event);
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event);

	odp_packet_vector_free(pkt_vec);
}

uint32_t em_event_vector_tbl(em_event_t vector_event,
			     em_event_t **event_tbl/*out*/)
{
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(vector_event == EM_EVENT_UNDEF || !event_tbl)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_VECTOR_TBL,
			       "Invalid args: vector_event:%" PRI_EVENT " event_tbl:%p",
			       vector_event, event_tbl);
		return 0;
	}

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(!is_vector_type_or_error(vector_event, EM_ESCOPE_EVENT_VECTOR_TBL))) {
		*event_tbl = NULL;
		return 0;
	}

	return event_vector_tbl(vector_event, event_tbl /*out*/);
}

uint32_t em_event_vector_size(em_event_t vector_event)
{
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(vector_event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_VECTOR_SIZE,
			       "Invalid arg, vector_event undefined!", vector_event);
		return 0;
	}

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(!is_vector_type_or_error(vector_event, EM_ESCOPE_EVENT_VECTOR_SIZE)))
		return 0;

	odp_event_t odp_event = event_em2odp(vector_event);
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event);

	return odp_packet_vector_size(pkt_vec);
}

void em_event_vector_size_set(em_event_t vector_event, uint32_t size)
{
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(vector_event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_VECTOR_SIZE_SET,
			       "Invalid arg, vector_event undefined!", vector_event);
		return;
	}

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(!is_vector_type_or_error(vector_event, EM_ESCOPE_EVENT_VECTOR_SIZE_SET)))
		return;

	odp_event_t odp_event = event_em2odp(vector_event);
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event);

	odp_packet_vector_size_set(pkt_vec, size);
}

uint32_t em_event_vector_max_size(em_event_t vector_event)
{
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(vector_event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_VECTOR_MAX_SIZE,
			       "Invalid arg, vector_event undefined!", vector_event);
		return 0;
	}

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(!is_vector_type_or_error(vector_event, EM_ESCOPE_EVENT_VECTOR_MAX_SIZE)))
		return 0;

	uint32_t max_size = 0;
	em_status_t err = event_vector_max_size(vector_event, &max_size,
						EM_ESCOPE_EVENT_VECTOR_MAX_SIZE);
	if (unlikely(err != EM_OK))
		return 0;

	return max_size;
}

em_status_t em_event_vector_info(em_event_t vector_event,
				 em_event_vector_info_t *vector_info /*out*/)
{
	em_status_t status = EM_ERROR;

	/* Check args */
	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(vector_event == EM_EVENT_UNDEF || !vector_info)) {
		status = INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_VECTOR_INFO,
					"Invalid args: vector_event:%" PRI_EVENT " vector_info:%p",
					vector_event, vector_info);
		goto err_vecinfo;
	}

	if (EM_CHECK_LEVEL > 2 &&
	    unlikely(!is_vector_type_or_error(vector_event, EM_ESCOPE_EVENT_VECTOR_INFO))) {
		status = EM_ERR_BAD_TYPE;
		goto err_vecinfo;
	}

	/* Get the max size */
	status = event_vector_max_size(vector_event, &vector_info->max_size,
				       EM_ESCOPE_EVENT_VECTOR_INFO);
	if (unlikely(status != EM_OK))
		goto err_vecinfo;

	/* Get vector size and the event-table */
	vector_info->size = event_vector_tbl(vector_event, &vector_info->event_tbl/*out*/);

	return EM_OK;

err_vecinfo:
	if (vector_info) {
		vector_info->event_tbl = NULL;
		vector_info->size = 0;
		vector_info->max_size = 0;
	}
	return status;
}
