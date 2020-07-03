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
	em_event_t event;

	pool_elem = pool_elem_get(pool);
	if (unlikely(size == 0 ||
		     pool_elem == NULL || !pool_allocated(pool_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_ALLOC,
			       "Invalid args: size:%zu type:%u pool:%" PRI_POOL "",
			       size, type, pool);
		return EM_EVENT_UNDEF;
	}

	if (pool_elem->event_type == EM_EVENT_TYPE_PACKET) {
		/*
		 * EM event pools created with type=PKT can support SW events
		 * as well as pkt events.
		 */
		event = event_alloc_pkt(pool_elem, size, type);
	} else { /* pool_elem->event_type == EM_EVENT_TYPE_SW */
		/*
		 * EM event pools created with type=SW can not support
		 * pkt events.
		 */
		if (unlikely(em_get_type_major(type) ==
			     EM_EVENT_TYPE_PACKET)) {
			INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_ALLOC,
				       "EM-pool:%s(%" PRI_POOL "):\n"
				       "Invalid event type:%u for buf",
				       pool_elem->name, pool, type);
			return EM_EVENT_UNDEF;
		}

		event = event_alloc_buf(pool_elem, size, type);
	}

	if (unlikely(event == EM_EVENT_UNDEF)) {
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
		if (unlikely(em_get_type_major(type) ==
			     EM_EVENT_TYPE_PACKET)) {
			INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED,
				       EM_ESCOPE_ALLOC_MULTI,
				       "EM-pool:%s(%" PRI_POOL "):\n"
				       "Invalid event type:%u for buf",
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
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_FREE,
			       "event undefined!");
		return;
	}

	if (EM_CHECK_LEVEL > 1 || em_shm->opt.pool.statistics_enable) {
		event_hdr_t *const ev_hdr = event_to_hdr(event);

		if (EM_CHECK_LEVEL > 1) {
			/* Simple double-free detection */
			const uint32_t allocated =
			env_atomic32_sub_return(&ev_hdr->allocated, 1);

			if (unlikely(allocated != 0)) {
				const char *const fmt =
					"Double free:event:%" PRI_EVENT "";
				INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_POINTER),
					       EM_ESCOPE_FREE, fmt, event);
			}
		}
		if (em_shm->opt.pool.statistics_enable) {
			/* Update pool statistcs */
			em_pool_t pool = ev_hdr->pool;
			int subpool;

			if (pool != EM_POOL_UNDEF) {
				subpool = ev_hdr->subpool;
				if (valid_pool(pool))
					pool_stat_decrement(pool, subpool, 1);
			}
		}
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(&event, 1);

	odp_event = event_em2odp(event);
	odp_event_free(odp_event);
}

void em_free_multi(const em_event_t events[], int num)
{
	odp_event_t *odp_events;
	int i;

	if (unlikely(num <= 0)) {
		if (num < 0)
			INTERNAL_ERROR(EM_ERR_TOO_SMALL, EM_ESCOPE_FREE_MULTI,
				       "Invalid arg: num:%d", num);
		return;
	}

	if (EM_CHECK_LEVEL > 1) {
		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_FREE_MULTI,
				       "events[%d] undefined!", i);
			return;
		}
	}

	if (EM_CHECK_LEVEL > 1 || em_shm->opt.pool.statistics_enable) {
		event_hdr_t *ev_hdrs[num];

		event_to_hdr_multi(events, ev_hdrs, num);

		if (EM_CHECK_LEVEL > 1) {
			uint32_t allocated;
			em_status_t err;
			em_escope_t escope;

			/* Simple double-free detection */
			for (i = 0; i < num; i++) {
				allocated =
				env_atomic32_sub_return(&ev_hdrs[i]->allocated,
							1);
				if (unlikely(allocated != 0)) {
					const char *const fmt =
					"Double free:events[%d]:%" PRI_EVENT "";
					err = EM_FATAL(EM_ERR_BAD_POINTER);
					escope = EM_ESCOPE_FREE_MULTI;
					INTERNAL_ERROR(err, escope, fmt,
						       i, events[i]);
				}
			}
		}

		if (em_shm->opt.pool.statistics_enable) {
			/* Update pool statistcs */
			int idx = 0; /* index into ev_hdrs[] */
			int dec = 0;

			do {
				for (; idx < num &&
				     ev_hdrs[idx]->pool == EM_POOL_UNDEF; idx++)
					; /* skip events from external pools */
				if (idx >= num)
					break;

				em_pool_t pool = ev_hdrs[idx]->pool;
				int subpool = ev_hdrs[idx]->subpool;

				for (i = idx + 1; i < num &&
				     ev_hdrs[i]->pool == pool &&
				     ev_hdrs[i]->subpool == subpool; i++)
					; /* count events from same pool */
				dec = i - idx;
				idx = i;
				if (likely(valid_pool(pool)))
					pool_stat_decrement(pool, subpool, dec);
			} while (idx < num);
		}
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_free(events, num);

	odp_events = events_em2odp(events);

	odp_event_free_multi(odp_events, num);
}

em_status_t
em_send(em_event_t event, em_queue_t queue)
{
	const int is_external = queue_external(queue);
	queue_elem_t *q_elem = NULL;
	event_hdr_t *ev_hdr;
	int num_sent;
	em_status_t stat;

	/*
	 * Check all args.
	 */
	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(event == EM_EVENT_UNDEF,
				EM_ERR_BAD_ID, EM_ESCOPE_SEND,
				"Invalid event");

	ev_hdr = event_to_hdr(event);
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	if (EM_CHECK_LEVEL > 2)
		RETURN_ERROR_IF(env_atomic32_get(&ev_hdr->allocated) != 1,
				EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_SEND,
				"Event:%" PRI_EVENT " already freed!", event);

	if (!is_external) {
		/* queue belongs to this EM instance */
		q_elem = queue_elem_get(queue);
		if (EM_CHECK_LEVEL > 0)
			RETURN_ERROR_IF(q_elem == NULL,
					EM_ERR_BAD_ID, EM_ESCOPE_SEND,
					"Invalid queue:%" PRI_QUEUE "", queue);
		if (EM_CHECK_LEVEL > 1)
			RETURN_ERROR_IF(!queue_allocated(q_elem),
					EM_ERR_BAD_STATE, EM_ESCOPE_SEND,
					"Invalid queue:%" PRI_QUEUE "", queue);
	}

	/* Buffer events from EO-start sent to scheduled queues */
	if (unlikely(em_locm.start_eo_elem != NULL &&
		     !is_external && q_elem->scheduled)) {
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

	if (is_external) {
		/*
		 * Send out of EM to another device via event-chaining and a
		 * user-provided function 'event_send_device()'
		 */
		stat = send_chaining(event, ev_hdr, queue);
		if (EM_CHECK_LEVEL == 0)
			return stat;
		RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_SEND,
				"send_chaining failed: Q:%" PRI_QUEUE "",
				queue);
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
		stat = send_output(event, q_elem);
		break;
	default:
		stat = EM_ERR_NOT_FOUND;
		break;
	}

	if (EM_CHECK_LEVEL == 0)
		return stat;
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_SEND,
			"send failed: Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
			queue, q_elem->type);

	return EM_OK;
}

int
em_send_multi(em_event_t *const events, int num, em_queue_t queue)
{
	event_hdr_t *ev_hdrs[num];
	const int is_external = queue_external(queue);
	queue_elem_t *q_elem = NULL;
	int num_sent;
	int i;

	/*
	 * Check all args.
	 */
	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(events == NULL || num <= 0)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_SEND_MULTI,
				       "Invalid events");
			return 0;
		}
	}
	if (EM_CHECK_LEVEL > 2) {
		for (i = 0; i < num && events[i] != EM_EVENT_UNDEF; i++)
			;
		if (unlikely(i != num)) {
			INTERNAL_ERROR(EM_ERR_BAD_POINTER,
				       EM_ESCOPE_SEND_MULTI,
				       "Invalid events[%d]=%" PRI_EVENT "",
				       i, events[i]);
			return 0;
		}
	}

	event_to_hdr_multi(events, ev_hdrs, num);
	for (i = 0; i < num; i++)
		ev_hdrs[i]->egrp = EM_EVENT_GROUP_UNDEF;

	if (EM_CHECK_LEVEL > 2) {
		for (i = 0; i < num &&
		     env_atomic32_get(&ev_hdrs[i]->allocated) == 1; i++)
			;
		if (unlikely(i != num)) {
			const char *const fmt =
				"events[%d]:%" PRI_EVENT " already freed!";
			INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE),
				       EM_ESCOPE_SEND_MULTI, fmt, i, events[i]);
			return 0;
		}
	}

	if (!is_external) {
		/* queue belongs to this EM instance */
		q_elem = queue_elem_get(queue);
		if (EM_CHECK_LEVEL > 0) {
			if (unlikely(q_elem == NULL)) {
				INTERNAL_ERROR(EM_ERR_BAD_ID,
					       EM_ESCOPE_SEND_MULTI,
					       "Invalid queue:%" PRI_QUEUE "",
					       queue);
				return 0;
			}
		}
		if (EM_CHECK_LEVEL > 1) {
			if (unlikely(!queue_allocated(q_elem))) {
				INTERNAL_ERROR(EM_ERR_BAD_STATE,
					       EM_ESCOPE_SEND_MULTI,
					       "Invalid queue:%" PRI_QUEUE "",
					       queue);
				return 0;
			}
		}
	}

	/* Buffer events from EO-start sent to scheduled queues */
	if (unlikely(em_locm.start_eo_elem != NULL &&
		     !is_external && q_elem->scheduled)) {
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

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, EM_EVENT_GROUP_UNDEF);

	if (is_external) {
		/*
		 * Send out of EM to another device via event-chaining and a
		 * user-provided function 'event_send_device_multi()'
		 */
		num_sent = send_chaining_multi(events, ev_hdrs, num, queue);
		if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
			INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
				       EM_ESCOPE_SEND_MULTI,
				       "send-egrp-multi fails: req:%d, sent:%d",
				       num, num_sent);
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
		num_sent = send_output_multi(events, num, q_elem);
		break;
	default:
		num_sent = 0;
		break;
	}

	if (EM_CHECK_LEVEL > 0 && unlikely(num_sent != num)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_MULTI,
			       "send-multi failed: req:%d, sent:%d",
			       num, num_sent);
	}

	return num_sent;
}

void *
em_event_pointer(em_event_t event)
{
	odp_event_t odp_event;
	odp_event_type_t odp_etype;

	if (unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EVENT_POINTER,
			       "event undefined!");
		return NULL;
	}

	odp_event = event_em2odp(event);
	odp_etype = odp_event_type(odp_event);

	switch (odp_etype) {
	case ODP_EVENT_PACKET: {
		odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

		return odp_packet_data(odp_pkt);
	}
	case ODP_EVENT_BUFFER: {
		odp_buffer_t odp_buf = odp_buffer_from_event(odp_event);
		event_hdr_t *const ev_hdr = odp_buffer_addr(odp_buf);

		return (void *)((uintptr_t)ev_hdr + sizeof(event_hdr_t)
				- ev_hdr->align_offset);
	}
	case ODP_EVENT_TIMEOUT: /* @TBD */
	case ODP_EVENT_CRYPTO_COMPL: /* @TBD */
	default:
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_EVENT_POINTER,
			       "Unexpected odp event type:%u", odp_etype);
		return NULL;
	}
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
		event_hdr_t *const ev_hdr = event_to_hdr(event);

		return ev_hdr->event_size;
	}

	INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_EVENT_GET_SIZE,
		       "Unexpected odp event type:%u", odp_etype);
	return 0;
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
	event_hdr_t *ev_hdr;

	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(event == EM_EVENT_UNDEF)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GET_TYPE,
				       "event undefined!");
			return EM_EVENT_TYPE_UNDEF;
		}
	}

	ev_hdr = event_to_hdr(event);

	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(ev_hdr == NULL)) {
			INTERNAL_ERROR(EM_ERR_BAD_POINTER,
				       EM_ESCOPE_EVENT_GET_TYPE,
				       "ev_hdr == NULL");
			return EM_EVENT_TYPE_UNDEF;
		}
	}

	return ev_hdr->event_type;
}

int em_event_get_type_multi(em_event_t events[], int num,
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

int em_event_same_type_multi(em_event_t events[], int num,
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
