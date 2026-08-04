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
 *
 * EM internal event group functions
 */

#ifndef EM_EVENT_GROUP_H_
#define EM_EVENT_GROUP_H_

#include <stdbool.h>
#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_error.h"
#include "em_event_group_types.h"
#include "em_internal_event.h"
#include "em_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EM_EVENT_GROUP_SAFE_MODE
/** Error reporting macro for event group counters in safe mode (.pre_cnt & .post_cnt) */
#define EGRP_CNT_ERROR(err, escope, egrp_cnt, fmt, ...) \
	INTERNAL_ERROR((err), (escope), \
		       fmt "\n  pre_cnt:%u post_cnt:%u gen:%u applied:%s\n", \
		       ##__VA_ARGS__, \
		       (uint32_t)(egrp_cnt).pre_cnt, (uint32_t)(egrp_cnt).post_cnt, \
		       (uint32_t)(egrp_cnt).gen, (egrp_cnt).applied ? "true" : "false")
#else
/** Error reporting macro for event group counters in non-safe mode (only .post_cnt) */
#define EGRP_CNT_ERROR(err, escope, egrp_cnt, fmt, ...) \
	INTERNAL_ERROR((err), (escope), \
		       fmt "\n  post_cnt:%u gen:%u applied:%s\n", \
		       ##__VA_ARGS__, \
		       (uint32_t)(egrp_cnt).post_cnt, (uint32_t)(egrp_cnt).gen, \
		       (egrp_cnt).applied ? "true" : "false")
#endif

#define invalid_egrp(event_group) \
	((unsigned int)egrp_hdl2idx((event_group)) >= EM_MAX_EVENT_GROUPS)

em_status_t event_group_init(event_group_tbl_t *const event_group_tbl,
			     odp_stash_t *const event_group_pool);
em_status_t event_group_term(void);

em_event_group_t event_group_alloc(void);
em_status_t event_group_free(em_event_group_t event_group);

static inline int
event_group_allocated(const event_group_elem_t *egrp_elem)
{
	return !egrp_elem->in_stash;
}

static inline int
egrp_hdl2idx(const em_event_group_t event_group)
{
	return (int)((uintptr_t)event_group - 1);
}

static inline em_event_group_t
egrp_idx2hdl(const int event_group_idx)
{
	return (em_event_group_t)(uintptr_t)(event_group_idx + 1);
}

static inline event_group_elem_t *
event_group_elem_get(const em_event_group_t event_group)
{
	const int egrp_idx = egrp_hdl2idx(event_group);
	event_group_elem_t *egrp_elem;

	if (unlikely((uint32_t)egrp_idx > EM_MAX_EVENT_GROUPS - 1))
		return NULL;

	egrp_elem = &em_shm->event_group_tbl.egrp_elem[egrp_idx];

	return egrp_elem;
}

static inline bool valid_pre_cnt(const event_group_elem_t *egrp_elem,
				 const egrp_counter_t egrp_cnt, const unsigned int decr,
				 const uint32_t event_egrp_gen)
{
	if (unlikely(egrp_cnt.pre_cnt < decr || !egrp_cnt.applied ||
		     egrp_cnt.gen != event_egrp_gen)) {
		EGRP_CNT_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_UPDATE_PRECNT, egrp_cnt,
			       "Expired event(s) or inv. decr in event group:%" PRI_EGRP ":\n"
			       "  event-egrp-gen:%u, decr:%u",
			       egrp_elem->event_group, event_egrp_gen, decr);
		return false;
	}

	return true;
}

/**
 * Decrement event group pre-counter atomically.
 *
 * Returns true on success, false on failure (invalid pre-count or generation).
 * Only used when EM_EVENT_GROUP_SAFE_MODE is enabled.
 */
static inline bool
pre_count_decrement(event_group_elem_t *const egrp_elem, unsigned int decr,
		    uint32_t event_egrp_gen)
{
	egrp_counter_t egrp_cnt;
	egrp_counter_t new_cnt;

	do {
		/* Fully relaxed: just a value check & arithmetic */
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_RELAXED);

		if (unlikely(!valid_pre_cnt(egrp_elem, egrp_cnt, decr, event_egrp_gen)))
			return false;

		new_cnt.all_u64 = egrp_cnt.all_u64;
		new_cnt.pre_cnt -= decr;
	} while (!__atomic_compare_exchange_n(&egrp_elem->counts.atomic_u64,
					      &egrp_cnt.all_u64, new_cnt.all_u64, false,
					      __ATOMIC_RELAXED /*success*/,
					      __ATOMIC_RELAXED /*failure*/));
	return true; /* success */
}

/**
 * Set core local event group.
 *
 * Verifies event group state and updates pre-count before setting core local
 * event group. Sets evgroup to undefined for excess and expired evgroup events.
 *
 * Only called by the EM-dispatcher before receive function.
 */
static inline void
event_group_set_local(em_event_group_t egrp, uint32_t event_egrp_gen, unsigned int decr)
{
	if (egrp == EM_EVENT_GROUP_UNDEF)
		return;

	/* Group is validated before setting */
	em_locm_t *const locm = &em_locm;
	event_group_elem_t *const egrp_elem = event_group_elem_get(egrp);

	if (unlikely(!egrp_elem))
		return;

	if (EM_EVENT_GROUP_SAFE_MODE) {
		bool valid_precnt = pre_count_decrement(egrp_elem, decr, event_egrp_gen);

		if (unlikely(!valid_precnt)) {
			locm->current.egrp_elem = NULL;
			return;
		}
	}

	locm->current.egrp_elem = egrp_elem;
	locm->current.egrp_gen = event_egrp_gen;
}

static inline bool valid_post_cnt(const event_group_elem_t *egrp_elem,
				  const egrp_counter_t egrp_cnt,
				  const unsigned int decr, const uint32_t egrp_gen)
{
	if (unlikely(egrp_cnt.post_cnt < decr || egrp_cnt.gen != egrp_gen)) {
		if (egrp_cnt.applied) {
			EGRP_CNT_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_UPDATE_POSTCNT,
				       egrp_cnt,
				       "Expired event(s) or inv. decr in event group:%" PRI_EGRP ":\n"
				       "  current-egrp-gen:%u, decr:%u",
				       egrp_elem->event_group, egrp_gen, decr);
		}
		return false;
	}

	return true;
}

/**
 * Updates event group post-counter. Generation and count must be valid.
 */
static inline int /* num_notifs when evgrp is done */
post_count_decrement(event_group_elem_t *const egrp_elem,
		     const unsigned int decr,
		     em_notif_t notif_tbl[static EM_EVENT_GROUP_MAX_NOTIF /*out*/])
{
	const em_locm_t *locm = &em_locm;
	const uint32_t egrp_gen = locm->current.egrp_gen;

	egrp_counter_t egrp_cnt;
	egrp_counter_t new_cnt;
	int succ_mo;
	int num_notif; /* return value: the num notifs to send when evgrp done */

	do {
		num_notif = 0;
		succ_mo = __ATOMIC_RELAXED;
		/*
		 * Need acquire here so that if this turns out to be the last event
		 * (reads notif_tbl[]), we see prior initialization (published with a
		 * release when applied).
		 */
		egrp_cnt.all_u64 = __atomic_load_n(&egrp_elem->counts.atomic_u64,
						   __ATOMIC_ACQUIRE);

		if (unlikely(!valid_post_cnt(egrp_elem, egrp_cnt, decr, egrp_gen)))
			return 0;

		new_cnt.all_u64 = egrp_cnt.all_u64;
		new_cnt.post_cnt -= decr;

		if (new_cnt.post_cnt == 0) {
			/*
			 * All events in event group handled!
			 */
			num_notif = MIN(egrp_elem->num_notif, EM_EVENT_GROUP_MAX_NOTIF);
			/* Copy notifs before clearing '.applied' */
			for (int i = 0; i < num_notif; i++) {
				notif_tbl[i].event = egrp_elem->notif_tbl[i].event;
				notif_tbl[i].queue = egrp_elem->notif_tbl[i].queue;
				notif_tbl[i].egroup = egrp_elem->notif_tbl[i].egroup;
			}
			/* Clear '.applied' before sending notifications */
			new_cnt.applied = 0;
			/*
			 * Use 'release' when we clear applied (last event) so a re-user
			 * 'acquiring' the flag sees all prior writes. Otherwise 'relaxed'.
			 */
			succ_mo = __ATOMIC_RELEASE;
		}
	} while (!__atomic_compare_exchange_n(&egrp_elem->counts.atomic_u64,
					      &egrp_cnt.all_u64, new_cnt.all_u64, false,
					      succ_mo /*success*/,
					      __ATOMIC_RELAXED /*failure*/));

	return num_notif;
}

/**
 * Decrements the event group (post-)count and sends notif events when group is done
 *
 * Called by the EM-dispatcher after the EO-receive function.
 *
 * Only called when the core-local current event group is set, e.g.,
 *        if (em_locm.current.egrp_elem != NULL)
 *                event_group_count_decrement(1, em_locm.current.egrp_elem);
 */
static inline void
event_group_count_decrement(const unsigned int decr, event_group_elem_t *const egrp_elem)
{
	em_notif_t notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];

	/* Validates group before updating counters */
	const int num_notif = post_count_decrement(egrp_elem, decr, notif_tbl /*out*/);

	/* Last event in the group, send notifications */
	if (num_notif > 0) {
		em_status_t ret = send_notifs(num_notif, notif_tbl);

		if (unlikely(ret != EM_OK))
			INTERNAL_ERROR(ret, EM_ESCOPE_EVENT_GROUP_UPDATE,
				       "send notifs failed");
	}
}

static inline void
save_current_evgrp(event_group_elem_t **save_egrp_elem /*out*/,
		   uint32_t *save_egrp_gen /*out*/)
{
	const em_locm_t *locm = &em_locm;

	*save_egrp_elem = locm->current.egrp_elem;
	*save_egrp_gen = locm->current.egrp_gen;
}

static inline void
restore_current_evgrp(event_group_elem_t *const saved_egrp_elem,
		      const uint32_t saved_egrp_gen)
{
	em_locm_t *const locm = &em_locm;

	locm->current.egrp_elem = saved_egrp_elem;
	locm->current.egrp_gen = saved_egrp_gen;
}

unsigned int event_group_count(void);

/**
 * Send an internal control event to an EM unscheduled control queue, tagged to
 * an event group.
 *
 * Internal counterpart to em_send_group() for EM's own control-event plumbing:
 * the per-core/shared unscheduled ctrl queues are dispatched by EM, so the
 * event group count is decremented normally. Unlike em_send_group() this skips
 * the user-facing argument checks, the unscheduled-queue restriction and the
 * user API send-hooks. The caller must pass valid internal arguments.
 *
 * @return EM_OK if successful.
 */
em_status_t send_ctrl_queue(em_event_t event, em_queue_t queue,
			    em_event_group_t event_group);

/** Print information about all event groups */
void event_group_info_print(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_GROUP_H_ */
