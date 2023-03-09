/*
 *   Copyright (c) 2015-2021, Nokia Solutions and Networks
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
  * Event Machine ODP API extensions
  *
  */

#include <em_include.h>
#include <event_machine/platform/event_machine_odp_ext.h>

odp_queue_t em_odp_queue_odp(em_queue_t queue)
{
	const queue_elem_t *queue_elem = queue_elem_get(queue);

	if (unlikely(queue_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_ODP_EXT,
			       "queue_elem ptr NULL!");
		return ODP_QUEUE_INVALID;
	}

	return queue_elem->odp_queue;
}

em_queue_t em_odp_queue_em(odp_queue_t queue)
{
	const queue_elem_t *queue_elem = odp_queue_context(queue);

	/* verify that the odp context is an EM queue elem */
	if (unlikely(!queue_elem ||
		     queue_elem->valid_check != QUEUE_ELEM_VALID))
		return EM_QUEUE_UNDEF;

	return queue_elem->queue;
}

/**
 * @brief Helper to em_odp_pktin_event_queues2em()
 *
 * @param odp_queue  ODP pktin-queue to convert to an EM-queue.
 *                   The given ODP queue handle must have been returned by
 *                   odp_pktin_event_queue().
 * @return em_queue_t: New EM queue mapped to use the ODP pktin event queue
 */
static em_queue_t pktin_event_queue2em(odp_queue_t odp_queue)
{
	em_queue_t queue = EM_QUEUE_UNDEF; /* return value */
	const char *err_str = "";
	odp_queue_info_t odp_qinfo;
	int ret = 0;

	if (unlikely(odp_queue == ODP_QUEUE_INVALID)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ODP_EXT,
			       "Bad arg: ODP queue invalid!");
		return EM_QUEUE_UNDEF;
	}

	queue = em_odp_queue_em(odp_queue);
	if (unlikely(queue != EM_QUEUE_UNDEF)) {
		/* The given ODP queue is already associated with an EM queue */
		return queue;
	}

	ret = odp_queue_info(odp_queue, &odp_qinfo);
	if (unlikely(ret || odp_qinfo.param.type != ODP_QUEUE_TYPE_SCHED)) {
		err_str = "odp_queue_info(): unsuitable odp queue";
		goto err_return;
	}

	/*
	 * Determine EM queue priority:
	 */
	odp_schedule_prio_t odp_prio = odp_schedule_default_prio();
	em_queue_prio_t prio = EM_QUEUE_PRIO_UNDEF;
	int num_prio = em_queue_get_num_prio(NULL);

	for (int i = 0; i < num_prio; i++) {
		prio_em2odp(i, &odp_prio/*out*/);
		if (odp_prio == odp_qinfo.param.sched.prio) {
			prio = i;
			break;
		}
	}
	if (unlikely(prio == EM_QUEUE_PRIO_UNDEF)) {
		err_str = "Can't convert ODP qprio to EM qprio";
		goto err_return;
	}

	/*
	 * Determine scheduled EM queue type
	 */
	em_queue_type_t queue_type = EM_QUEUE_TYPE_UNDEF;

	ret = scheduled_queue_type_odp2em(odp_qinfo.param.sched.sync,
					  &queue_type /*out*/);
	if (unlikely(ret)) {
		err_str = "Can't convert ODP qtype to EM qtype";
		goto err_return;
	}

	/*
	 * Determine EM queue group
	 */
	em_queue_group_t queue_group;
	const queue_group_elem_t *qgrp_elem;

	queue_group = em_queue_group_get_first(NULL);
	while (queue_group != EM_QUEUE_GROUP_UNDEF) {
		qgrp_elem = queue_group_elem_get(queue_group);
		if (qgrp_elem &&
		    qgrp_elem->odp_sched_group == odp_qinfo.param.sched.group)
			break; /* found match! */
		queue_group = em_queue_group_get_next();
	}
	if (unlikely(queue_group == EM_QUEUE_GROUP_UNDEF)) {
		err_str = "No matching EM Queue Group found";
		goto err_return;
	}

	/*
	 * Set EM queue name based on the ODP queue name
	 */
	char q_name[ODP_QUEUE_NAME_LEN];

	snprintf(q_name, sizeof(q_name), "EM:%s", odp_qinfo.name);
	q_name[ODP_QUEUE_NAME_LEN - 1] = '\0';

	/*
	 * Set up the EM queue based on gathered info
	 */
	queue_setup_t setup = {.name = q_name,
			       .type = queue_type,
			       .prio = prio,
			       .atomic_group = EM_ATOMIC_GROUP_UNDEF,
			       .queue_group = queue_group,
			       .conf = NULL};

	queue = queue_alloc(EM_QUEUE_UNDEF, &err_str);
	if (unlikely(queue == EM_QUEUE_UNDEF))
		goto err_return; /* err_str set by queue_alloc() */

	queue_elem_t *q_elem = queue_elem_get(queue);

	if (unlikely(!q_elem)) {
		err_str = "Queue elem NULL!";
		goto err_return;
	}

	/* Set common queue-elem fields based on 'setup' */
	queue_setup_common(q_elem, &setup);
	/* Set queue-elem fields for a pktin event queue */
	q_elem->odp_queue = odp_queue;
	q_elem->is_pktin = true;
	q_elem->scheduled = EM_TRUE;
	q_elem->state = EM_QUEUE_STATE_INIT;

	/*
	 * Note: The ODP queue context points to the EM queue elem.
	 * The EM queue context set by the user using the API function
	 * em_queue_set_context() is accessed through the queue_elem_t::context
	 * and retrieved with em_queue_get_context() or passed by EM to the
	 * EO-receive function for scheduled queues.
	 *
	 * Set the odp context data length (in bytes) for potential prefetching.
	 * The ODP implementation may use this value as a hint for the number
	 * of context data bytes to prefetch.
	 */
	ret = odp_queue_context_set(odp_queue, q_elem, sizeof(*q_elem));
	if (unlikely(ret)) {
		err_str = "odp_queue_context_set() failed";
		goto err_return;
	}

	return queue; /* success */

err_return:
	INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_ODP_EXT,
		       "%s (ret=%d)", err_str, ret);
	if (EM_DEBUG_PRINT && odp_queue != ODP_QUEUE_INVALID)
		odp_queue_print(odp_queue);
	if (queue != EM_QUEUE_UNDEF)
		queue_free(queue);
	return EM_QUEUE_UNDEF;
}

int em_odp_pktin_event_queues2em(const odp_queue_t odp_pktin_event_queues[/*num*/],
				 em_queue_t queues[/*out:num*/], int num)
{
	int i;

	for (i = 0; i < num; i++) {
		queues[i] = pktin_event_queue2em(odp_pktin_event_queues[i]);
		if (unlikely(queues[i] == EM_QUEUE_UNDEF)) {
			INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_ODP_EXT,
				       "Cannot create EM-Q using pktin-queue:%d (hdl:%" PRIu64 ")",
				       i, odp_queue_to_u64(odp_pktin_event_queues[i]));
			break;
		}
	}

	return i;
}

uint32_t em_odp_event_hdr_size(void)
{
	return sizeof(event_hdr_t);
}

odp_event_t em_odp_event2odp(em_event_t event)
{
	return event_em2odp(event);
}

void em_odp_events2odp(const em_event_t events[/*num*/],
		       odp_event_t odp_events[/*out:num*/], int num)
{
	if (unlikely(num <= 0))
		return;

	events_em2odp(events, odp_events/*out*/, num);
}

em_event_t em_odp_event2em(odp_event_t odp_event)
{
	em_event_t event = event_init_odp(odp_event, false/*!is_extev*/, NULL);

	return event;
}

void em_odp_events2em(const odp_event_t odp_events[/*num*/],
		      em_event_t events[/*out:num*/], int num)
{
	if (unlikely(num <= 0))
		return;

	event_hdr_t *ev_hdrs[num];

	event_init_odp_multi(odp_events, events/*out*/, ev_hdrs/*out*/, num,
			     false/*!is_extev*/);
}

int em_odp_pool2odp(em_pool_t pool, odp_pool_t odp_pools[/*out*/], int num)
{
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (unlikely(!pool_elem || !pool_allocated(pool_elem) ||
		     !odp_pools || num <= 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ODP_EXT,
			       "Inv.args: pool:%" PRI_POOL " odp_pools:%p num:%d",
			       pool, odp_pools, num);
		return 0;
	}

	int num_subpools = MIN(num, pool_elem->num_subpools);

	for (int i = 0; i < num_subpools; i++)
		odp_pools[i] = pool_elem->odp_pool[i];

	/* return the number of odp-pools filled into 'odp_pools[]' */
	return num_subpools;
}

em_pool_t em_odp_pool2em(odp_pool_t odp_pool)
{
	if (unlikely(odp_pool == ODP_POOL_INVALID)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_ODP_EXT,
			       "Inv.arg: odp_pool invalid");
		return EM_POOL_UNDEF;
	}

	return pool_odp2em(odp_pool);
}

int pkt_enqueue(const odp_packet_t pkt_tbl[/*num*/], int num, em_queue_t queue)
{
	if (unlikely(!pkt_tbl || num <= 0))
		return 0;

	queue_elem_t *const q_elem = queue_elem_get(queue);
	odp_event_t odp_event_tbl[num];
	int sent = 0;

	odp_packet_to_event_multi(pkt_tbl, odp_event_tbl/*out*/, num);

	if (q_elem != NULL && q_elem->scheduled) {
		/*
		 * Enqueue the events into a scheduled em-odp queue.
		 * No need to init the ev-hdrs - init is done in dispatch.
		 */
		sent = odp_queue_enq_multi(q_elem->odp_queue,
					   odp_event_tbl, num);
	} else {
		em_event_t event_tbl[num];
		event_hdr_t *evhdr_tbl[num];

		events_odp2em(odp_event_tbl, event_tbl/*out*/, num);

		/* Init the event-hdrs for incoming non-scheduled pkts */
		event_init_pkt_multi(pkt_tbl, event_tbl/*in/out*/,
				     evhdr_tbl/*out*/, num, true /*is_extev*/);

		if (q_elem == NULL) {
			/* Send directly out via event chaining */
			if (likely(queue_external(queue)))
				sent = send_chaining_multi(event_tbl, num, queue);
		} else if (q_elem->type == EM_QUEUE_TYPE_UNSCHEDULED) {
			/* Enqueue into an unscheduled em-odp queue */
			sent = odp_queue_enq_multi(q_elem->odp_queue,
						   odp_event_tbl, num);
		} else if (q_elem->type ==  EM_QUEUE_TYPE_LOCAL) {
			/* Send into an local em-odp queue */
			sent = send_local_multi(event_tbl, num, q_elem);
		} else if (q_elem->type ==  EM_QUEUE_TYPE_OUTPUT) {
			/* Send directly out via an output em-odp queue */
			sent = send_output_multi(event_tbl, num, q_elem);
		}
	}

	if (unlikely(sent < num)) {
		sent = unlikely(sent < 0) ? 0 : sent;
		odp_packet_free_multi(&pkt_tbl[sent], num - sent);
		/*
		 * Event state checking: No need to adjust the event state
		 * since the events were never enqueued into EM.
		 */
	}

	return sent;
}

odp_schedule_group_t em_odp_qgrp2odp(em_queue_group_t queue_group)
{
	const queue_group_elem_t *qgrp_elem =
		queue_group_elem_get(queue_group);

	RETURN_ERROR_IF(!qgrp_elem || !queue_group_allocated(qgrp_elem),
			EM_ERR_BAD_ARG, EM_ESCOPE_ODP_EXT,
			"Invalid queue group:%" PRI_QGRP "", queue_group);

	return qgrp_elem->odp_sched_group;
}
