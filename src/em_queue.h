/*
 *   Copyright (c) 2015-2026, Nokia Solutions and Networks
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
 * EM internal queue functions
 */

#ifndef EM_QUEUE_H_
#define EM_QUEUE_H_

#include <stddef.h>
#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_eo_types.h"
#include "em_event_inline.h"
#include "em_mem.h"
#include "em_queue_types.h"

#ifdef __cplusplus
extern "C" {
#endif

em_status_t queue_init(queue_tbl_t *const queue_tbl,
		       queue_pool_t *const queue_pool,
		       queue_pool_t *const queue_pool_static,
		       queue_pool_t *const queue_pool_aggr);

em_status_t queue_init_local(void);
em_status_t queue_term_local(void);

em_queue_t queue_alloc(em_queue_t queue, const char **err_str /*out*/);
em_status_t queue_free(em_queue_t queue);

void queue_setup_common(const char *name, const em_queue_param_t *param,
			queue_elem_t *q_elem /*in/out*/);

em_queue_t queue_create_param(const char *name, const em_queue_param_t *param,
			      const char **err_str /*out*/);

/**
 * Apply em_queue_conf_t fields onto an em_queue_param_t.
 *
 * Maps 'flags', 'min_events' and (for output queues) 'output_conf' from
 * 'conf' onto 'param'. If 'conf' is NULL, 'param' is left untouched so the
 * defaults set by em_queue_param_init() remain in effect.
 *
 * 'param->type' must be set by the caller before calling this helper, as it
 * controls whether 'output_conf' is copied.
 */
void queue_param_apply_conf(em_queue_param_t *param /*in,out*/,
			    const em_queue_conf_t *conf);

em_status_t queue_delete(queue_elem_t *const queue_elem, const char **err_str /*out*/);

em_status_t queue_enable(queue_elem_t *const q_elem);

em_status_t queue_enable_all(eo_elem_t *const eo_elem);

em_status_t queue_disable(queue_elem_t *const q_elem);

em_status_t queue_disable_all(eo_elem_t *const eo_elem);

em_status_t queue_state_change__check(queue_state_t old_state, queue_state_t new_state,
				      int is_setup /* vs. is_teardown */);
em_status_t queue_state_change(queue_elem_t *const queue_elem, queue_state_t new_state);

em_status_t queue_state_change_all(eo_elem_t *const eo_elem, queue_state_t new_state);

unsigned int queue_count(void);

size_t queue_name(const queue_elem_t *const q_elem,
		  char name[/*out*/], const size_t maxlen);

/** Print information about all EM queues */
void print_queue_info(void);
/** Print queue capabilities */
void print_queue_capa(void);
void print_queue_prio_info(void);
void print_queue_elem_info(void);

/** Get the string of a queue state */
const char *queue_state_str(queue_state_t state);
/** Get the string of a queue type */
const char *queue_type_str(em_queue_type_t type);

/**
 * Enqueue multiple events into an unscheduled queue.
 * Internal func, application should use em_send_multi() instead.
 */
static inline unsigned int
queue_unsched_enqueue_multi(const em_event_t events[], int num,
			    const queue_elem_t *const q_elem)
{
	odp_event_t odp_events[num];
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 && odp_queue == ODP_QUEUE_INVALID))
		return 0;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return 0;

	events_em2odp(events, odp_events/*out*/, num);

	/*
	 * Convert a vector's inner-event table to odp-form (drop evgen) before
	 * enqueue, but only for an aggregator-parent destination
	 * (flags.has_aggr): only its dequeue reads the inner table as raw odp
	 * events (esv_aggr_inner_em2usr()). A plain unscheduled queue's dequeue
	 * is scalar and the user read / em_free() paths are evgen-tolerant, so
	 * the conversion would be dead work there. Mirrors the has_aggr-gated
	 * dequeue in queue_dequeue_multi().
	 *
	 * Note: send_sched_multi() must convert unconditionally because
	 * flush_scheduler_events() force-scans leftover scheduled vectors
	 * regardless of has_aggr; unscheduled queues have no such flush path.
	 */
	if (esv_enabled() && q_elem->flags.has_aggr) {
		odp_event_type_t odp_etypes[num];

		for (int i = 0; i < num; i++)
			odp_etypes[i] = odp_event_type(odp_events[i]);

		for (int i = 0; i < num; i++) {
			if (odp_etypes[i] == ODP_EVENT_PACKET_VECTOR)
				pktvec_tbl2odp(odp_events[i]);
			else if (odp_etypes[i] == ODP_EVENT_VECTOR)
				evvec_tbl2odp(odp_events[i]);
		}

		ret = odp_queue_enq_multi(odp_queue, odp_events, num);

		if (likely(ret == num))
			return num; /* Success! */

		/* Restore the inner-table of the events that failed to enqueue */
		int enq = ret < 0 ? 0 : ret;

		for (int i = enq; i < num; i++) {
			if (odp_etypes[i] == ODP_EVENT_PACKET_VECTOR)
				pktvec_tbl2odp_revert(odp_events[i]);
			else if (odp_etypes[i] == ODP_EVENT_VECTOR)
				evvec_tbl2odp_revert(odp_events[i]);
		}

		return enq; /* enq < num */
	}

	/* No inner-table conversion needed (ESV off or non-aggregator queue) */
	ret = odp_queue_enq_multi(odp_queue, odp_events, num);
	if (unlikely(ret < 0))
		return 0;

	return ret;
}

/**
 * Enqueue en event into an unscheduled queue.
 * Internal func, application should use em_send() instead.
 */
static inline em_status_t
queue_unsched_enqueue(em_event_t event, const queue_elem_t *const q_elem)
{
	odp_event_t odp_event = event_em2odp(event);
	odp_queue_t odp_queue = q_elem->odp_queue;
	int ret;

	if (unlikely(EM_CHECK_LEVEL > 1 &&
		     (odp_event == ODP_EVENT_INVALID ||
		      odp_queue == ODP_QUEUE_INVALID)))
		return EM_ERR_NOT_FOUND;

	if (unlikely(EM_CHECK_LEVEL > 0 &&
		     q_elem->state != EM_QUEUE_STATE_UNSCHEDULED))
		return EM_ERR_BAD_STATE;

	/*
	 * Convert the vector inner-event table to odp-form before enqueue only
	 * for an aggregator-parent destination (flags.has_aggr); a plain
	 * unscheduled queue never reads it as raw odp. See
	 * queue_unsched_enqueue_multi() for the full rationale.
	 */
	if (esv_enabled() && q_elem->flags.has_aggr) {
		odp_event_type_t odp_etype = odp_event_type(odp_event);

		if (odp_etype == ODP_EVENT_PACKET_VECTOR)
			pktvec_tbl2odp(odp_event);
		else if (odp_etype == ODP_EVENT_VECTOR)
			evvec_tbl2odp(odp_event);

		ret = odp_queue_enq(odp_queue, odp_event);
		/*
		 * Always check the ODP enqueue result: this is a real runtime
		 * operation failure from the underlying library, not an optional
		 * EM_CHECK_LEVEL-controlled parameter/state validation.
		 */
		if (unlikely(ret != 0)) {
			/* Restore EM vector inner-table before returning to user */
			if (odp_etype == ODP_EVENT_PACKET_VECTOR)
				pktvec_tbl2odp_revert(odp_event);
			else if (odp_etype == ODP_EVENT_VECTOR)
				evvec_tbl2odp_revert(odp_event);

			return EM_ERR_LIB_FAILED;
		}

		return EM_OK;
	}

	/* No inner-table conversion needed (ESV off or non-aggregator queue) */
	ret = odp_queue_enq(odp_queue, odp_event);
	if (unlikely(ret != 0))
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

static inline int
next_local_queue_events(stash_entry_t entry_tbl[/*out*/], int num_events)
{
	em_locm_t *const locm = &em_locm;

	if (locm->local_queues.empty)
		return 0;

	em_queue_prio_t prio = EM_QUEUE_PRIO_NUM - 1;

	for (int i = 0; i < EM_QUEUE_PRIO_NUM; i++) {
		/* from hi to lo prio: next prio if local queue is empty */
		if (locm->local_queues.prio[prio].empty_prio) {
			prio--;
			continue;
		}

		odp_stash_t stash = locm->local_queues.prio[prio].stash;
		int num = odp_stash_get_u64(stash, &entry_tbl[0].u64 /*[out]*/,
					    num_events);
		if (num > 0)
			return num;

		locm->local_queues.prio[prio].empty_prio = 1;
		prio--;
	}

	locm->local_queues.empty = 1;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_H_ */
