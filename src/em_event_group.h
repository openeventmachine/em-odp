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

/**
 * @file
 *
 * EM internal event group functions
 *
 */

#ifndef EM_EVENT_GROUP_H_
#define EM_EVENT_GROUP_H_

#ifdef __cplusplus
extern "C" {
#endif

#define invalid_egrp(event_group) \
	((unsigned int)egrp_hdl2idx((event_group)) >= EM_MAX_EVENT_GROUPS)

em_status_t
event_group_init(event_group_tbl_t *const event_group_tbl,
		 event_group_pool_t *const event_group_pool);

em_event_group_t
event_group_alloc(void);

em_status_t
event_group_free(em_event_group_t event_group);

static inline int
event_group_allocated(event_group_elem_t *const egrp_elem)
{
	return !objpool_in_pool(&egrp_elem->event_group_pool_elem);
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

	if (unlikely((unsigned int)egrp_idx > EM_MAX_EVENT_GROUPS - 1))
		return NULL;

	egrp_elem = &em_shm->event_group_tbl.egrp_elem[egrp_idx];

	return egrp_elem;
}

static inline uint64_t
event_group_gen_get(event_group_elem_t *const egrp_elem)
{
	if (unlikely(egrp_elem != NULL)) {
		egrp_counter_t egrp_count;

		egrp_count.all = EM_ATOMIC_GET(&egrp_elem->post.atomic);
		return egrp_count.gen;
	} else {
		return 0;
	}
}

/**
 * Verifies event group state and updates pre count before setting core local
 * event group. Sets group to undefined for excess and expired group events.
 */
static inline void
set_local_safe(const event_hdr_t *const ev_hdr)
{
	uint64_t current_count;
	egrp_counter_t new_count;
	event_group_elem_t *const egrp_elem =
		event_group_elem_get(ev_hdr->egrp);

	do {
		current_count = EM_ATOMIC_GET(&egrp_elem->pre.atomic);
		new_count.all = current_count;
		new_count.count--;
		/* Check for excess and expired group events */
		if (unlikely(new_count.count < 0 ||
			     new_count.gen != ev_hdr->egrp_gen)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID,
				       EM_ESCOPE_EVENT_GROUP_UPDATE,
				       "Expired event group event received!");
			em_locm.current.egrp = EM_EVENT_GROUP_UNDEF;
			return;
		}
	} while (!EM_ATOMIC_CMPSET(&egrp_elem->pre.atomic,
				   current_count, new_count.all));

	em_locm.current.egrp_gen = ev_hdr->egrp_gen;
	em_locm.current.egrp = ev_hdr->egrp;
	em_locm.current.egrp_elem = egrp_elem;
}

/**
 * Set core local event group.
 *
 * Validates event group if EM_EVENT_GROUP_SAFE_MODE is enabled.
 *
 * Only called by the EM-dispatcher before receive function.
 */
static inline void
event_group_set_local(const event_hdr_t *const ev_hdr)
{
	if (ev_hdr->egrp == EM_EVENT_GROUP_UNDEF)
		return;

	/* event group is set: */
	if (EM_EVENT_GROUP_SAFE_MODE) {
		/* Group is validated before setting */
		set_local_safe(ev_hdr);
	} else {
		em_locm.current.egrp_elem = event_group_elem_get(ev_hdr->egrp);
		em_locm.current.egrp = ev_hdr->egrp;
	}
}

/**
 * Updates event group counter safely. Generation and count must be valid.
 */
static inline int64_t
count_decrement_safe(event_group_elem_t *const egrp_elem,
		     const unsigned int decr)
{
	uint64_t current_count;
	egrp_counter_t new_count;

	do {
		current_count = EM_ATOMIC_GET(&egrp_elem->post.atomic);
		new_count.all = current_count;
		new_count.count -= decr;
		/* Validate group state and generation before changing count */
		if (unlikely(new_count.count < 0 ||
			     new_count.gen != em_locm.current.egrp_gen)) {
			/* Suppress error if group is aborted */
			if (!egrp_elem->ready)
				INTERNAL_ERROR(EM_ERR_BAD_ID,
					       EM_ESCOPE_EVENT_GROUP_UPDATE,
					       "Expired grp event in post cnt!"
					      );
			return -1;
		}
	} while (!EM_ATOMIC_CMPSET(&egrp_elem->post.atomic, current_count,
				   new_count.all));
	return new_count.count;
}

/**
 * Decrements the event group count and sends notif events when group is done
 *
 * Only called by the EM-dispatcher after receive function.
 */
static inline void
event_group_count_decrement(const unsigned int decr)
{
	int64_t count;
	event_group_elem_t *const egrp_elem = em_locm.current.egrp_elem;

	if (EM_EVENT_GROUP_SAFE_MODE) {
		/* Validates group before updating counters */
		count = count_decrement_safe(egrp_elem, decr);
	} else {
		count = EM_ATOMIC_SUB_RETURN(&egrp_elem->post.atomic, decr);

		if (unlikely(count < 0)) {
			if (egrp_elem->ready) {
				/* Counter should stay zero if aborted */
				egrp_elem->post.all = 0;
				return;
			}

			INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_ID),
				       EM_ESCOPE_EVENT_GROUP_UPDATE,
				       "Group count already 0!");
		}
	}

	if (count == 0) { /* Last event in the group */
		/* Setting pre_count here does nothing as both counters should
		 * be zero. Only due to incorrect usage pre_count is other than
		 * zero when notif events are about to be sent.
		 */
		if (EM_EVENT_GROUP_SAFE_MODE)
			egrp_elem->pre.count = 0;

		const int num_notif = egrp_elem->num_notif;
		em_status_t ret;

		/* Copy notifications to local memory */
		em_notif_t notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];

		for (int i = 0; i < num_notif; i++) {
			notif_tbl[i].event = egrp_elem->notif_tbl[i].event;
			notif_tbl[i].queue = egrp_elem->notif_tbl[i].queue;
			notif_tbl[i].egroup = egrp_elem->notif_tbl[i].egroup;
		}

		egrp_elem->ready = 1;
		ret = send_notifs(num_notif, notif_tbl);
		if (unlikely(ret != EM_OK))
			INTERNAL_ERROR(ret, EM_ESCOPE_EVENT_GROUP_UPDATE,
				       "send notifs failed");
	}
}

static inline void
save_current_evgrp(em_event_group_t *save_egrp /*out*/,
		   event_group_elem_t **save_egrp_elem /*out*/,
		   int32_t *save_egrp_gen /*out*/)
{
	*save_egrp_elem = em_locm.current.egrp_elem;
	*save_egrp = em_locm.current.egrp;
	*save_egrp_gen = em_locm.current.egrp_gen;
}

static inline void
restore_current_evgrp(const em_event_group_t saved_egrp,
		      event_group_elem_t *const saved_egrp_elem,
		      const int32_t saved_egrp_gen)
{
	em_locm.current.egrp_elem = saved_egrp_elem;
	em_locm.current.egrp = saved_egrp;
	em_locm.current.egrp_gen = saved_egrp_gen;
}

unsigned int
event_group_count(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_GROUP_H_ */
