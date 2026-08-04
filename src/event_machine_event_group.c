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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_chaining.h"
#include "em_eo.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_group.h"
#include "em_event_group_types.h"
#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_hooks.h"
#include "em_internal_event.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"

/* per core (thread) state for em_event_group_next() */
static ENV_LOCAL unsigned int _egrp_tbl_iter_idx;

em_event_group_t em_event_group_create(void)
{
	em_event_group_t egrp;
	event_group_elem_t *egrp_elem;
	egrp_counter_t egrp_cnt;

	egrp = event_group_alloc();
	if (unlikely(egrp == EM_EVENT_GROUP_UNDEF)) {
		unsigned int evgrp_cnt = 0;

		(void)em_event_group_first(&evgrp_cnt);
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EVENT_GROUP_CREATE,
			       "Event group alloc failed! evgrps created: %u, max evgrps: %u",
			       evgrp_cnt, EM_MAX_EVENT_GROUPS);
		return EM_EVENT_GROUP_UNDEF;
	}

	egrp_elem = event_group_elem_get(egrp);

	egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
					   __ATOMIC_RELAXED);
	egrp_cnt.pre_cnt = 0;
	egrp_cnt.post_cnt = 0;
	egrp_cnt.applied = 0;
	/* egrp_cnt.gen = don't change */

	/* Set group ready for 'apply' */
	__atomic_store_n(&egrp_elem->counts.atomic_u64, egrp_cnt.all_u64,
			 __ATOMIC_RELAXED);
	/* Alloc succeeded, return event group handle */
	return egrp;
}

em_status_t em_event_group_delete(em_event_group_t event_group)
{
	em_status_t status;
	event_group_elem_t *const egrp_elem = event_group_elem_get(event_group);
	egrp_counter_t egrp_cnt;

	RETURN_ERROR_IF(egrp_elem == NULL || !event_group_allocated(egrp_elem),
			EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_DELETE,
			"Invalid event group: %" PRI_EGRP "", event_group);

	egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
					   __ATOMIC_RELAXED);
	if (unlikely(egrp_cnt.applied))
		return EGRP_CNT_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_DELETE, egrp_cnt,
				      "Event group:%" PRI_EGRP " not in a state for delete!",
				      event_group);

	egrp_elem->num_notif = 0;

	status = event_group_free(event_group);
	RETURN_ERROR_IF(status != EM_OK, status, EM_ESCOPE_EVENT_GROUP_DELETE,
			"Event Group delete failed!");

	return EM_OK;
}

em_status_t em_event_group_apply(em_event_group_t event_group, int count,
				 int num_notif, const em_notif_t notif_tbl[])
{
	event_group_elem_t *const egrp_elem = event_group_elem_get(event_group);

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(egrp_elem == NULL || count <= 0 || (uint64_t)count > EGRP_CNT_MAX,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_APPLY,
				"Invalid args: event group:%" PRI_EGRP ", count:%d [1, %llu]",
				event_group, count, EGRP_CNT_MAX);
	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_APPLY,
				"Event group:%" PRI_EGRP " not created!", event_group);
	if (EM_CHECK_LEVEL >= 3) {
		em_status_t ret = check_notif_tbl(num_notif, notif_tbl);

		RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EVENT_GROUP_APPLY,
				"Invalid notif cfg given!");
	}

	egrp_counter_t egrp_cnt;
	egrp_counter_t new_cnt;

	do {
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		if (unlikely(egrp_cnt.applied))
			return EGRP_CNT_ERROR(EM_ERR_BAD_STATE,
					      EM_ESCOPE_EVENT_GROUP_APPLY, egrp_cnt,
					      "Event group %" PRI_EGRP " in use, cannot apply!",
					      event_group);

		egrp_elem->num_notif = num_notif;

		for (int i = 0; i < num_notif; i++) {
			egrp_elem->notif_tbl[i].event = notif_tbl[i].event;
			egrp_elem->notif_tbl[i].queue = notif_tbl[i].queue;
			egrp_elem->notif_tbl[i].egroup = notif_tbl[i].egroup;
		}

		/* Set the new count */
		new_cnt.all_u64 = egrp_cnt.all_u64;
		if (EM_EVENT_GROUP_SAFE_MODE)
			new_cnt.pre_cnt = count;
		new_cnt.post_cnt = count;
		new_cnt.applied = 1;
		/* Event group generation increments when _apply() is called */
		new_cnt.gen++;
	} while (!__atomic_compare_exchange_n(&egrp_elem->counts.atomic_u64,
					      &egrp_cnt.all_u64, new_cnt.all_u64, false,
					      /*success, publish notif data + counts*/
					      __ATOMIC_RELEASE,
					      __ATOMIC_RELAXED /*failure*/));

	return EM_OK;
}

em_status_t em_event_group_increment(int count)
{
	em_locm_t *const locm = &em_locm;
	em_event_group_t const egrp = em_event_group_current();
	event_group_elem_t *egrp_elem = NULL;
	const uint32_t incr = (uint32_t)count;

	if (likely(egrp != EM_EVENT_GROUP_UNDEF))
		egrp_elem = locm->current.egrp_elem;

	RETURN_ERROR_IF(!egrp_elem, EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_INCREMENT,
			"No current event group (%" PRI_EGRP ")", egrp);
	RETURN_ERROR_IF(count <= 0 || (uint64_t)count > EGRP_CNT_MAX - 1, EM_ERR_BAD_ARG,
			EM_ESCOPE_EVENT_GROUP_INCREMENT,
			"Invalid arg: count:%d", count);

	if (EM_CHECK_LEVEL >= 2) {
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_BAD_STATE, EM_ESCOPE_EVENT_GROUP_INCREMENT,
				"Current event group in a bad state (%" PRI_EGRP ")", egrp);
	}

	egrp_counter_t egrp_cnt;
	egrp_counter_t new_cnt;

	/* Add to post counter before count is zero or generation mismatch */
	do {
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		if (unlikely(egrp_cnt.gen != locm->current.egrp_gen || !egrp_cnt.applied))
			return EGRP_CNT_ERROR(EM_ERR_BAD_STATE,
					      EM_ESCOPE_EVENT_GROUP_INCREMENT, egrp_cnt,
					      "Expired event group (%" PRI_EGRP ") or not applied:\n"
					      "  curr gen:%u", egrp);
		/* Check for overflow */
		if (unlikely((EM_EVENT_GROUP_SAFE_MODE && /* safe mode for .pre_cnt */
			      (egrp_cnt.pre_cnt + incr > EGRP_CNT_MAX)) ||
			     (egrp_cnt.post_cnt + incr > EGRP_CNT_MAX))) {
			return EGRP_CNT_ERROR(EM_ERR_BAD_ARG,
					      EM_ESCOPE_EVENT_GROUP_INCREMENT, egrp_cnt,
					      "Event group %" PRI_EGRP " counter would overflow:\n"
					      "  count + increment(%u) > max(%u)",
					      egrp, incr, EGRP_CNT_MAX);
		}

		new_cnt.all_u64 = egrp_cnt.all_u64;
		if (EM_EVENT_GROUP_SAFE_MODE)
			new_cnt.pre_cnt += incr;
		new_cnt.post_cnt += incr;
	} while (!__atomic_compare_exchange_n(&egrp_elem->counts.atomic_u64,
					      &egrp_cnt.all_u64, new_cnt.all_u64, false,
					      __ATOMIC_RELAXED /*success*/,
					      __ATOMIC_RELAXED /*failure*/));

	return EM_OK;
}

int em_event_group_is_ready(em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);

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

	egrp_counter_t egrp_cnt;

	egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
					   __ATOMIC_RELAXED);
	if (egrp_cnt.applied)
		return EM_FALSE; /* not ready for a new em_event_group_apply() */
	else
		return EM_TRUE; /* ready for a new em_event_group_apply() */
}

em_event_group_t em_event_group_current(void)
{
	const event_group_elem_t *egrp_elem = em_locm.current.egrp_elem;

	return egrp_elem ? egrp_elem->event_group : EM_EVENT_GROUP_UNDEF;
}

void em_event_group_processing_end(void)
{
	em_locm_t *const locm = &em_locm;
	event_group_elem_t *const current_egrp_elem = locm->current.egrp_elem;

	if (unlikely(!current_egrp_elem))
		return;

	/*
	 * Atomically decrement the event group count.
	 * If new count is zero, send notification events.
	 */
	event_group_count_decrement(locm->current.rcv_multi_cnt, current_egrp_elem);

	locm->current.egrp_elem = NULL;
}

em_status_t em_event_group_assign(em_event_group_t event_group)
{
	em_locm_t *const locm = &em_locm;
	event_group_elem_t *const egrp_elem = event_group_elem_get(event_group);
	const int num_events = locm->current.rcv_multi_cnt; /* only valid in EO-receive */

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(egrp_elem == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_ASSIGN,
				"Invalid event group: %" PRI_EGRP "", event_group);
	if (EM_CHECK_LEVEL >= 2) {
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_ASSIGN,
				"Invalid event group: %" PRI_EGRP "", event_group);
		RETURN_ERROR_IF(!locm->current.q_elem || num_events <= 0,
				EM_ERR_TOO_SMALL, EM_ESCOPE_EVENT_GROUP_ASSIGN,
				"Cannot assign event group %" PRI_EGRP ",\n"
				"not called from EO-receive or no events(%d)!",
				event_group, num_events);
	}
	RETURN_ERROR_IF(locm->current.egrp_elem != NULL,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_EVENT_GROUP_ASSIGN,
			"Cannot assign event group %" PRI_EGRP ",\n"
			"Event group %" PRI_EGRP " already assigned!",
			event_group, locm->current.egrp_elem->event_group);

	egrp_counter_t egrp_cnt;
	egrp_counter_t new_cnt;

	if (!EM_EVENT_GROUP_SAFE_MODE) {
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		if (unlikely(!egrp_cnt.applied))
			return EGRP_CNT_ERROR(EM_ERR_BAD_STATE,
					      EM_ESCOPE_EVENT_GROUP_ASSIGN, egrp_cnt,
					      "Cannot assign event group %" PRI_EGRP ",\n"
					      "has not been applied!", event_group);

		locm->current.egrp_elem = egrp_elem;
		locm->current.egrp_gen = egrp_cnt.gen;

		return EM_OK;
	}

	/*
	 * EM_EVENT_GROUP_SAFE_MODE:
	 */
	do {
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		if (unlikely(egrp_cnt.pre_cnt < num_events || !egrp_cnt.applied))
			return EGRP_CNT_ERROR(EM_ERR_BAD_STATE,
					      EM_ESCOPE_EVENT_GROUP_ASSIGN, egrp_cnt,
					      "Cannot assign event group %" PRI_EGRP ",\n"
					      "has not been applied or too many events:%d!",
					      event_group, num_events);

		new_cnt.all_u64 = egrp_cnt.all_u64;
		new_cnt.pre_cnt -= num_events;
	} while (!__atomic_compare_exchange_n(&egrp_elem->counts.atomic_u64,
					      &egrp_cnt.all_u64, new_cnt.all_u64, false,
					      __ATOMIC_RELAXED /*success*/,
					      __ATOMIC_RELAXED /*failure*/));
	locm->current.egrp_elem = egrp_elem;
	locm->current.egrp_gen = new_cnt.gen;

	return EM_OK;
}

em_status_t em_event_group_abort(em_event_group_t event_group)
{
	em_locm_t *const locm = &em_locm;
	event_group_elem_t *const egrp_elem = event_group_elem_get(event_group);
	egrp_counter_t egrp_cnt;
	egrp_counter_t new_cnt;

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(egrp_elem == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Invalid event group: %" PRI_EGRP "", event_group);

	if (EM_CHECK_LEVEL >= 2)
		RETURN_ERROR_IF(!event_group_allocated(egrp_elem),
				EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_ABORT,
				"Event group: %" PRI_EGRP " not created", event_group);

	/* Attempt to set count to zero before count reaches zero */
	do {
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		if (unlikely(!egrp_cnt.applied)) {
			/* Don't report as error since aborting during teardown might be normal */
			DBG_PRINT("%s():Event group:%" PRI_EGRP " abort late, notifs already sent\n"
				  "  pre_cnt:%u post_cnt:%u, gen:%u, applied:%s\n",
				  __func__, event_group, (uint32_t)egrp_cnt.pre_cnt,
				  (uint32_t)egrp_cnt.post_cnt, (uint32_t)egrp_cnt.gen,
				  egrp_cnt.applied ? "true" : "false");
			return EM_ERR_BAD_STATE;
		}

		new_cnt.all_u64 = egrp_cnt.all_u64;
		new_cnt.pre_cnt = 0;
		new_cnt.post_cnt = 0;
		new_cnt.applied = 0;
	} while (!__atomic_compare_exchange_n(&egrp_elem->counts.atomic_u64,
					      &egrp_cnt.all_u64, new_cnt.all_u64, false,
					      __ATOMIC_RELAXED /*success*/,
					      __ATOMIC_RELAXED /*failure*/));

	/* can only modify local event group, other cores might have this set */
	if (locm->current.egrp_elem == egrp_elem)
		locm->current.egrp_elem = NULL;

	return EM_OK;
}

int em_event_group_notif(em_event_group_t event_group,
			 int max_notif, em_notif_t notif_tbl[])
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	int num_notif = 0; /* return value */

	if (EM_CHECK_LEVEL > 0 &&
	    unlikely(egrp_elem == NULL || max_notif < 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_EVENT_GROUP_NOTIF,
			       "Invalid args: evgrp:%" PRI_EGRP ", notifs:%d",
			       event_group, max_notif);
		return 0;
	}

	if (unlikely(EM_CHECK_LEVEL >= 2 && !event_group_allocated(egrp_elem))) {
		INTERNAL_ERROR(EM_ERR_NOT_CREATED, EM_ESCOPE_EVENT_GROUP_NOTIF,
			       "Event group:%" PRI_EGRP " not created", event_group);
		return 0;
	}

	if (unlikely(max_notif == 0))
		return 0;

	if (unlikely(notif_tbl == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EVENT_GROUP_NOTIF,
			       "Invalid notif_tbl[] given");
		return 0;
	}

	egrp_counter_t egrp_cnt;

	egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64, __ATOMIC_ACQUIRE);

	if (egrp_cnt.applied) {
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
 * Tag a single event's header with the given event group.
 * Shared by em_send_group() and send_ctrl_queue().
 */
static inline void
egrp_tag_event(event_hdr_t *ev_hdr, const event_group_elem_t *egrp_elem)
{
	if (egrp_elem) {
		egrp_counter_t egrp_cnt;

		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		ev_hdr->egrp = egrp_elem->event_group;
		ev_hdr->egrp_gen = egrp_cnt.gen;
	} else {
		ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	}
}

/**
 * Tag multiple events' headers with the given event group.
 * Used by em_send_group_multi().
 */
static inline void
egrp_tag_events_multi(event_hdr_t *const ev_hdrs[], int num,
		      em_event_group_t event_group,
		      const event_group_elem_t *egrp_elem)
{
	for (int i = 0; i < num; i++)
		ev_hdrs[i]->egrp = event_group; /* can be EM_EVENT_GROUP_UNDEF */

	if (egrp_elem) { /* NULL with EM_EVENT_GROUP_UNDEF */
		egrp_counter_t egrp_cnt;

		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);
		uint32_t egrp_gen = egrp_cnt.gen;

		for (int i = 0; i < num; i++)
			ev_hdrs[i]->egrp_gen = egrp_gen;
	}
}

/**
 * Shared backend for em_send_group() and send_ctrl_queue().
 *
 * Sends a single event to a queue on this device and returns the status. The
 * caller has fetched and validated 'q_elem', tagged the event with the event
 * group, and is responsible for any send-hooks and for reporting errors (with
 * its own error scope).
 */
static inline em_status_t
send_egrp_common(em_event_t event, event_hdr_t *ev_hdr,
		 const queue_elem_t *q_elem)
{
	em_status_t stat;

	if (esv_enabled())
		evstate_usr2em(event, ev_hdr, EVSTATE__SEND_EGRP);

	switch (q_elem->type) {
	case EM_QUEUE_TYPE_ATOMIC:
	case EM_QUEUE_TYPE_PARALLEL:
	case EM_QUEUE_TYPE_ORDERED:
		stat = send_sched(event, q_elem);
		break;
	case EM_QUEUE_TYPE_UNSCHEDULED:
		/*
		 * The user API rejects a real event group here (see
		 * em_send_group()); reached with a real group only via
		 * send_ctrl_queue() for the internal ctrl queues, which EM
		 * dispatches and whose group count it decrements.
		 */
		stat = queue_unsched_enqueue(event, q_elem);
		break;
	case EM_QUEUE_TYPE_AGGR:
		stat = send_aggr(event, ev_hdr, q_elem);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		stat = send_local(event, q_elem);
		break;
	default:
		stat = EM_ERR_NOT_FOUND;
		break;
	}

	/* Revert the ESV state on failure; the caller reports the error */
	if (unlikely(stat != EM_OK && esv_enabled()))
		evstate_usr2em_revert(event, ev_hdr, EVSTATE__SEND_EGRP__FAIL);

	return stat;
}

/**
 * Backend for em_send_group_multi().
 *
 * Sends multiple events to a queue on this device and returns the number sent.
 * The caller has fetched and validated 'q_elem', tagged the events with the
 * event group, and is responsible for any send-hooks and for reporting errors
 * (with its own error scope).
 */
static inline int
send_egrp_common_multi(const em_event_t events[], event_hdr_t *ev_hdrs[],
		       int num, const queue_elem_t *q_elem)
{
	int num_sent;

	if (esv_enabled())
		evstate_usr2em_multi(events, ev_hdrs, num, EVSTATE__SEND_EGRP_MULTI);

	switch (q_elem->type) {
	case EM_QUEUE_TYPE_ATOMIC:
	case EM_QUEUE_TYPE_PARALLEL:
	case EM_QUEUE_TYPE_ORDERED:
		num_sent = send_sched_multi(events, num, q_elem);
		break;
	case EM_QUEUE_TYPE_UNSCHEDULED:
		/*
		 * Reached only with EM_EVENT_GROUP_UNDEF: em_send_group_multi()
		 * rejects a real event group on unscheduled queues.
		 */
		num_sent = queue_unsched_enqueue_multi(events, num, q_elem);
		break;
	case EM_QUEUE_TYPE_AGGR:
		num_sent = send_aggr_multi(events, ev_hdrs, num, q_elem);
		break;
	case EM_QUEUE_TYPE_LOCAL:
		num_sent = send_local_multi(events, num, q_elem);
		break;
	default:
		num_sent = 0;
		break;
	}

	/* Revert the ESV state for the unsent events on failure; caller reports */
	if (unlikely(num_sent != num && esv_enabled()))
		evstate_usr2em_revert_multi(&events[num_sent],
					    &ev_hdrs[num_sent],
					    num - num_sent,
					    EVSTATE__SEND_EGRP_MULTI__FAIL);

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

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(ev_hdr->event_type == EM_EVENT_TYPE_ODP ||
			 ev_hdr->event_type == EM_EVENT_TYPE_TIMER_IND),
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP,
			"Event of type %u can't be sent with %s()",
			ev_hdr->event_type, __func__);

	/* Store the event group information in the event header */
	egrp_tag_event(ev_hdr, egrp_elem);

	/*
	 * External queue belongs to another EM instance, send out via EMC/BIP
	 */
	if (is_external)
		return send_external_egrp(event, ev_hdr, queue,
					  event_group, egrp_elem);

	const queue_elem_t *q_elem = queue_elem_get(queue);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !q_elem,
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP,
			"Invalid queue:%" PRI_QUEUE "", queue);
	RETURN_ERROR_IF(EM_CHECK_LEVEL >= 2 && !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_SEND_GROUP,
			"Invalid queue:%" PRI_QUEUE "", queue);
	/*
	 * Event groups are not supported with unscheduled queues: the group
	 * count is decremented only when EO-receive() returns; events on an
	 * unscheduled queue are dequeued directly by the application and are
	 * not dispatched to an EO, so the count would never reach zero.
	 * EM_EVENT_GROUP_UNDEF carries no group and is allowed, behaving like
	 * em_send(). Internal ctrl-queue sends use send_ctrl_queue() instead.
	 */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 &&
			event_group != EM_EVENT_GROUP_UNDEF &&
			q_elem->type == EM_QUEUE_TYPE_UNSCHEDULED,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_SEND_GROUP,
			"Event group not supported with unscheduled queue:%" PRI_QUEUE "",
			queue);

	/*
	 * em_send_group() called from within an EO-start function: events sent
	 * to scheduled queues are buffered and sent when EO-start completes.
	 */
	if (unlikely(em_locm.start_eo_elem && q_elem->flags.scheduled)) {
		if (esv_enabled())
			evstate_usr2em(event, ev_hdr, EVSTATE__SEND_EGRP);

		if (likely(eo_start_buffer_events(&event, 1, queue) == 1))
			return EM_OK; /* Success */

		/* Buffering failed: revert ESV state and report */
		if (esv_enabled())
			evstate_usr2em_revert(event, ev_hdr, EVSTATE__SEND_EGRP__FAIL);
		if (EM_CHECK_LEVEL == 0)
			return EM_ERR_OPERATION_FAILED;
		return INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_SEND_GROUP,
				      "EO-start event buffering failed, Q:%" PRI_QUEUE "",
				      queue);
	}

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(&event, 1, queue, event_group);

	/*
	 * Queue belongs to this EM instance
	 */
	em_status_t stat = send_egrp_common(event, ev_hdr, q_elem);

	if (likely(stat == EM_OK))
		return EM_OK; /* Success */
	if (EM_CHECK_LEVEL == 0)
		return stat;
	return INTERNAL_ERROR(stat, EM_ESCOPE_SEND_GROUP,
			      "send egrp: Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
			      queue, q_elem->type);
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

/*
 * em_send_group_multi() helper: buffer events when called from within an
 * EO-start function - scheduled-queue events are sent once EO-start completes.
 */
static inline int
send_egrp_multi_eo_start(const em_event_t events[], event_hdr_t *ev_hdrs[],
			 int num, em_queue_t queue)
{
	if (esv_enabled())
		evstate_usr2em_multi(events, ev_hdrs, num,
				     EVSTATE__SEND_EGRP_MULTI);

	int num_sent = eo_start_buffer_events(events, num, queue);

	if (likely(num_sent == num))
		return num_sent; /* Success */

	/* Buffering failed for the tail events: revert ESV and report */
	if (esv_enabled())
		evstate_usr2em_revert_multi(&events[num_sent],
					    &ev_hdrs[num_sent],
					    num - num_sent,
					    EVSTATE__SEND_EGRP_MULTI__FAIL);
	if (EM_CHECK_LEVEL > 0)
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
			       "EO-start event buffering failed: req:%d, sent:%d",
			       num, num_sent);
	return num_sent;
}

int em_send_group_multi(const em_event_t events[], int num, em_queue_t queue,
			em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	const bool is_external = queue_external(queue);
	event_hdr_t *ev_hdrs[num];

	em_status_t err = send_grpmulti_check(events, num, event_group, egrp_elem);

	if (unlikely(err != EM_OK)) {
		INTERNAL_ERROR(err, EM_ESCOPE_SEND_GROUP_MULTI,
			       "Invalid args: events:%p num:%d event_group:%" PRI_EGRP "",
			       events, num, event_group);
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

	/* check for invalid TIMER events and EM_EVENT_TYPE_ODP */
	if (EM_CHECK_LEVEL > 0) {
		for (int i = 0; i < num; i++) {
			if (unlikely(ev_hdrs[i]->event_type == EM_EVENT_TYPE_ODP ||
				     ev_hdrs[i]->event_type == EM_EVENT_TYPE_TIMER_IND)) {
				INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_SEND_GROUP_MULTI,
					       "Event[%d] of type %u can't be sent with %s()",
					       i, ev_hdrs[i]->event_type, __func__);
				return 0;
			}
		}
	}

	/* Store the event group information in the event header */
	egrp_tag_events_multi(ev_hdrs, num, event_group, egrp_elem);

	/*
	 * External queue belongs to another EM instance, send out via EMC/BIP
	 */
	if (is_external)
		return send_external_egrp_multi(events, ev_hdrs, num, queue,
						event_group, egrp_elem);

	const queue_elem_t *q_elem = queue_elem_get(queue);

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
	/*
	 * Event groups are not supported with unscheduled queues: the group
	 * count is decremented only when EO-receive() returns; events on an
	 * unscheduled queue are dequeued directly by the application and are
	 * not dispatched to an EO, so the count would never reach zero.
	 * EM_EVENT_GROUP_UNDEF carries no group and is allowed, behaving like
	 * em_send_multi().
	 */
	if (EM_CHECK_LEVEL > 1 && unlikely(event_group != EM_EVENT_GROUP_UNDEF &&
					   q_elem->type == EM_QUEUE_TYPE_UNSCHEDULED)) {
		INTERNAL_ERROR(EM_ERR_BAD_CONTEXT, EM_ESCOPE_SEND_GROUP_MULTI,
			       "Event group not supported with unscheduled queue:%" PRI_QUEUE "",
			       queue);
		return 0;
	}

	/*
	 * em_send_group_multi() called from within an EO-start function: events
	 * sent to scheduled queues are buffered and sent when EO-start completes.
	 */
	if (unlikely(em_locm.start_eo_elem && q_elem->flags.scheduled))
		return send_egrp_multi_eo_start(events, ev_hdrs, num, queue);

	if (EM_API_HOOKS_ENABLE)
		call_api_hooks_send(events, num, queue, event_group);

	/*
	 * Queue belongs to this EM instance
	 */
	int num_sent = send_egrp_common_multi(events, ev_hdrs, num, q_elem);

	if (unlikely(num_sent != num && EM_CHECK_LEVEL > 0))
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_GROUP_MULTI,
			       "send-egrp-multi failed: req:%d, sent:%d",
			       num, num_sent);
	return num_sent;
}

/* doc in em_event_group.h */
em_status_t send_ctrl_queue(em_event_t event, em_queue_t queue,
			    em_event_group_t event_group)
{
	const event_group_elem_t *egrp_elem = event_group_elem_get(event_group);
	event_hdr_t *ev_hdr = event_to_hdr(event);
	const queue_elem_t *q_elem = queue_elem_get(queue);

	/* validate the ctrl queue, egrp checked by caller */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 &&
			(!q_elem || !queue_allocated(q_elem)),
			EM_ERR_BAD_ARG, EM_ESCOPE_SEND_CTRL_QUEUE,
			"Invalid ctrl queue:%" PRI_QUEUE "", queue);

	/* Tag with the event group and send via the shared backend (no guard) */
	egrp_tag_event(ev_hdr, egrp_elem);

	em_status_t stat = send_egrp_common(event, ev_hdr, q_elem);

	if (likely(stat == EM_OK))
		return EM_OK; /* Success */
	if (EM_CHECK_LEVEL == 0)
		return stat;
	return INTERNAL_ERROR(stat, EM_ESCOPE_SEND_CTRL_QUEUE,
			      "ctrl-queue send: Q:%" PRI_QUEUE " type:%" PRI_QTYPE "",
			      queue, q_elem->type);
}

em_event_group_t em_event_group_first(unsigned int *num)
{
	const event_group_elem_t *const egrp_elem_tbl =
		em_shm->event_group_tbl.egrp_elem;
	const event_group_elem_t *egrp_elem = &egrp_elem_tbl[0];
	const unsigned int egrp_count = event_group_count();

	_egrp_tbl_iter_idx = 0; /* reset iteration */

	if (num)
		*num = egrp_count;

	if (egrp_count == 0) {
		_egrp_tbl_iter_idx = EM_MAX_EVENT_GROUPS; /* UNDEF=_next()*/
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

em_event_group_t em_event_group_next(void)
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
