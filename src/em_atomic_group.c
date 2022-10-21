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

/**
 * Atomic group inits done at global init (once at startup on one core)
 */
em_status_t
atomic_group_init(atomic_group_tbl_t *const atomic_group_tbl,
		  atomic_group_pool_t *const atomic_group_pool)
{
	atomic_group_elem_t *atomic_group_elem;
	const int cores = em_core_count();
	int ret;

	memset(atomic_group_tbl, 0, sizeof(atomic_group_tbl_t));
	memset(atomic_group_pool, 0, sizeof(atomic_group_pool_t));
	env_atomic32_init(&em_shm->atomic_group_count);

	for (int i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
		em_atomic_group_t agrp = agrp_idx2hdl(i);
		atomic_group_elem_t *const agrp_elem =
				atomic_group_elem_get(agrp);

		if (unlikely(!agrp_elem))
			return EM_ERR_BAD_POINTER;

		agrp_elem->atomic_group = agrp; /* store handle */

		/* Init list and lock */
		env_spinlock_init(&agrp_elem->lock);
		list_init(&agrp_elem->qlist_head);
		env_atomic32_init(&agrp_elem->num_queues);
	}

	ret = objpool_init(&atomic_group_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (int i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
		atomic_group_elem = &atomic_group_tbl->ag_elem[i];
		objpool_add(&atomic_group_pool->objpool, i % cores,
			    &atomic_group_elem->atomic_group_pool_elem);
	}

	return EM_OK;
}

static inline atomic_group_elem_t *
ag_pool_elem2ag_elem(const objpool_elem_t *const atomic_group_pool_elem)
{
	return (atomic_group_elem_t *)((uintptr_t)atomic_group_pool_elem -
			offsetof(atomic_group_elem_t, atomic_group_pool_elem));
}

/**
 * Dynamic atomic group allocation
 */
em_atomic_group_t
atomic_group_alloc(void)
{
	const atomic_group_elem_t *ag_elem;
	const objpool_elem_t *ag_p_elem;

	ag_p_elem = objpool_rem(&em_shm->atomic_group_pool.objpool,
				em_core_id());

	if (unlikely(ag_p_elem == NULL))
		return EM_ATOMIC_GROUP_UNDEF;

	ag_elem = ag_pool_elem2ag_elem(ag_p_elem);

	env_atomic32_inc(&em_shm->atomic_group_count);
	return ag_elem->atomic_group;
}

em_status_t
atomic_group_free(em_atomic_group_t atomic_group)
{
	atomic_group_elem_t *agrp_elem = atomic_group_elem_get(atomic_group);

	if (unlikely(agrp_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(&em_shm->atomic_group_pool.objpool,
		    agrp_elem->atomic_group_pool_elem.subpool_idx,
		    &agrp_elem->atomic_group_pool_elem);

	env_atomic32_dec(&em_shm->atomic_group_count);
	return EM_OK;
}

/**
 * Called by em_queue_delete() to remove the queue from the atomic group list
 */
void
atomic_group_remove_queue(queue_elem_t *const q_elem)
{
	if (!invalid_atomic_group(q_elem->atomic_group)) {
		atomic_group_elem_t *const ag_elem =
			atomic_group_elem_get(q_elem->atomic_group);

		atomic_group_rem_queue_list(ag_elem, q_elem);
		q_elem->atomic_group = EM_ATOMIC_GROUP_UNDEF;
	}
}

unsigned int
atomic_group_count(void)
{
	return env_atomic32_get(&em_shm->atomic_group_count);
}

static inline int
ag_local_processing_ended(atomic_group_elem_t *const ag_elem)
{
	em_locm_t *const locm = &em_locm;

	/*
	 * Check if atomic group processing has ended for this core, meaning
	 * the application called em_atomic_processing_end()
	 */
	if (locm->atomic_group_released) {
		locm->atomic_group_released = 0;
		/*
		 * Try to acquire the atomic group lock and continue processing.
		 * It is possible that another core has acquired the lock
		 */
		if (env_spinlock_trylock(&ag_elem->lock))
			return 0;
		else
			return 1;
	}

	return 0;
}

static inline int
ag_internal_enq(const atomic_group_elem_t *ag_elem, const em_event_t ev_tbl[],
		const int num_events, const em_queue_prio_t priority)
{
	odp_event_t odp_ev_tbl[num_events];
	odp_queue_t plain_q;
	int ret;

	events_em2odp(ev_tbl, odp_ev_tbl, num_events);

	if (priority == EM_QUEUE_PRIO_HIGHEST)
		plain_q = ag_elem->internal_queue.hi_prio;
	else
		plain_q = ag_elem->internal_queue.lo_prio;

	/* Enqueue events to internal queue */
	ret = odp_queue_enq_multi(plain_q, odp_ev_tbl, num_events);
	if (unlikely(ret != num_events))
		return ret > 0 ? ret : 0;

	return num_events;
}

static inline int
ag_internal_deq(const atomic_group_elem_t *ag_elem, em_event_t ev_tbl[/*out*/],
		const int num_events)
{
	/*
	 * Dequeue odp events directly into ev_tbl[].
	 * The function call_eo_receive_fn/multi() will convert to
	 * EM events with event-generation counts, if ESV is enabled,
	 * before passing the events to the user EO.
	 */
	odp_event_t *const ag_ev_tbl = (odp_event_t *const)ev_tbl;
	int hi_cnt;
	int lo_cnt;

	/* hi-prio events */
	hi_cnt = odp_queue_deq_multi(ag_elem->internal_queue.hi_prio,
				     ag_ev_tbl/*out*/, num_events);
	if (hi_cnt == num_events || hi_cnt < 0)
		return hi_cnt;

	/* ...then lo-prio events */
	lo_cnt = odp_queue_deq_multi(ag_elem->internal_queue.lo_prio,
				     &ag_ev_tbl[hi_cnt]/*out*/,
				     num_events - hi_cnt);
	if (unlikely(lo_cnt < 0))
		return hi_cnt;

	return hi_cnt + lo_cnt;
}

void
atomic_group_dispatch(em_event_t ev_tbl[], event_hdr_t *const ev_hdr_tbl[],
		      const int num_events, queue_elem_t *const q_elem)
{
	atomic_group_elem_t *const ag_elem =
		atomic_group_elem_get(q_elem->atomic_group);
	const em_queue_prio_t priority = q_elem->priority;
	int enq_cnt;

	/* Insert the original q_elem pointer into the event header */
	for (int i = 0; i < num_events; i++)
		ev_hdr_tbl[i]->q_elem = q_elem;

	/* Enqueue the scheduled events into the atomic group internal queue */
	enq_cnt = ag_internal_enq(ag_elem, ev_tbl, num_events, priority);

	if (unlikely(enq_cnt < num_events)) {
		em_free_multi(&ev_tbl[enq_cnt], num_events - enq_cnt);
		/*
		 * Use dispatch escope since this func is called only from
		 * dispatch_round() => atomic_group_dispatch()
		 */
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_DISPATCH,
			       "Atomic group:%" PRI_AGRP " internal enqueue:\n"
			       "  num_events:%d enq_cnt:%d",
			       ag_elem->atomic_group, num_events, enq_cnt);
	}

	/*
	 * Try to acquire the atomic group lock - if not available then some
	 * other core is already handling the same atomic group.
	 */
	if (!env_spinlock_trylock(&ag_elem->lock))
		return;

	em_locm_t *const locm = &em_locm;

	/* hint */
	odp_schedule_release_atomic();

	locm->atomic_group_released = 0;
	/*
	 * Loop until no more events or until atomic processing end.
	 * Events in the ag_elem->internal_queue:s have been scheduled
	 * already once and should be dispatched asap.
	 */
	em_event_t deq_ev_tbl[EM_SCHED_AG_MULTI_MAX_BURST];
	event_hdr_t *deq_hdr_tbl[EM_SCHED_AG_MULTI_MAX_BURST];

	do {
		int deq_cnt = ag_internal_deq(ag_elem, deq_ev_tbl /*out*/,
					      EM_SCHED_AG_MULTI_MAX_BURST);

		if (unlikely(deq_cnt <= 0)) {
			env_spinlock_unlock(&ag_elem->lock);
			/* return if no more events available */
			return;
		}

		locm->event_burst_cnt = deq_cnt;
		event_to_hdr_multi(deq_ev_tbl, deq_hdr_tbl /*out*/, deq_cnt);
		int tbl_idx = 0; /* index into 'deq_hdr_tbl[]' */

		/*
		 * Dispatch in batches of 'batch_cnt' events.
		 * Each batch contains events from the same atomic queue.
		 */
		do {
			queue_elem_t *const batch_qelem =
				deq_hdr_tbl[tbl_idx]->q_elem;
			int batch_cnt = 1;

			for (int i = tbl_idx + 1; i < deq_cnt &&
			     deq_hdr_tbl[i]->q_elem == batch_qelem; i++) {
				batch_cnt++;
			}

			dispatch_events(&deq_ev_tbl[tbl_idx],
					&deq_hdr_tbl[tbl_idx],
					batch_cnt, batch_qelem);
			tbl_idx += batch_cnt;
		} while (tbl_idx < deq_cnt);

	} while (!ag_local_processing_ended(ag_elem));
}

#define AG_INFO_HDR_STR \
"Number of atomic groups: %d\n\n" \
"ID        Name                            Qgrp      Q-num\n" \
"---------------------------------------------------------\n%s\n"

#define AG_INFO_LEN 58
#define AG_INFO_FMT "%-10" PRI_AGRP "%-32s%-10" PRI_QGRP "%-5d\n"/*58 characters*/

void print_atomic_group_info(void)
{
	unsigned int ag_num; /*atomic group number*/
	const atomic_group_elem_t *ag_elem;
	em_atomic_group_t ag_check;
	char ag_name[EM_ATOMIC_GROUP_NAME_LEN];
	int len = 0;
	int n_print = 0;

	em_atomic_group_t ag = em_atomic_group_get_first(&ag_num);

	/*
	 * ag_num might not match the actual number of atomic groups returned
	 * by iterating with func em_atomic_group_get_next() if atomic groups
	 * are added or removed in parallel by another core. Thus space for 10
	 * extra atomic groups is reserved. If more than 10 atomic groups are
	 * added in parallel by other cores, we print only information of the
	 * (ag_num + 10) atomic groups.
	 *
	 * The extra 1 byte is reserved for the terminating null byte.
	 */
	const int ag_info_str_len = (ag_num + 10) * AG_INFO_LEN + 1;
	char ag_info_str[ag_info_str_len];

	while (ag != EM_ATOMIC_GROUP_UNDEF) {
		ag_elem = atomic_group_elem_get(ag);

		em_atomic_group_get_name(ag, ag_name, sizeof(ag_name));

		ag_check = em_atomic_group_find(ag_name);
		if (unlikely(ag_elem == NULL || ag_check != ag ||
			     !atomic_group_allocated(ag_elem))) {
			ag = em_atomic_group_get_next();
			continue;
		}

		n_print = snprintf(ag_info_str + len, ag_info_str_len - len,
				   AG_INFO_FMT, ag, ag_name, ag_elem->queue_group,
				   env_atomic32_get(&ag_elem->num_queues));

		/* Not enough space to hold more atomic group info */
		if (n_print >= ag_info_str_len - len)
			break;

		len += n_print;
		ag = em_atomic_group_get_next();
	}

	/* No atomic group */
	if (len == 0) {
		EM_PRINT("No atomic group has been created\n");
		return;
	}

	/*
	 * To prevent printing incomplete information of the last atomic group
	 * when there is not enough space to hold all atomic group info.
	 */
	ag_info_str[len] = '\0';
	EM_PRINT(AG_INFO_HDR_STR, ag_num, ag_info_str);
}

#define AG_QUEUE_INFO_HDR_STR \
"Atomic group %" PRI_AGRP "(%s) has %d queue(s):\n\n" \
"ID        Name                           Priority  Type      State    Qgrp      Ctx\n" \
"-----------------------------------------------------------------------------------\n" \
"%s\n"

#define AG_Q_INFO_LEN 85
#define AG_Q_INFO_FMT "%-10" PRI_QUEUE "%-32s%-10d%-10s%-9s%-10" PRI_QGRP "%-3c\n"

void print_atomic_group_queues(em_atomic_group_t ag)
{
	unsigned int q_num;
	em_queue_t ag_queue;
	const queue_elem_t *q_elem;
	char q_name[EM_QUEUE_NAME_LEN];
	int len = 0;
	int n_print = 0;

	atomic_group_elem_t *ag_elem = atomic_group_elem_get(ag);

	if (unlikely(ag_elem == NULL || !atomic_group_allocated(ag_elem))) {
		EM_PRINT("Atomic group %" PRI_AGRP "is not created!\n", ag);
		return;
	}

	ag_queue = em_atomic_group_queue_get_first(&q_num, ag);

	/*
	 * q_num may not match the number of queues actually returned by iterating
	 * with em_atomic_group_queue_get_next() if queues are added or removed
	 * in parallel by another core. Thus space for 10 extra queues is reserved.
	 * If more than 10 queues are added to this atomic group by other cores
	 * in parallel, we print only information of the (q_num + 10) queues.
	 *
	 * The extra 1 byte is reserved for the terminating null byte.
	 */
	int q_info_str_len = (q_num + 10) * AG_Q_INFO_LEN + 1;
	char q_info_str[q_info_str_len];

	while (ag_queue != EM_QUEUE_UNDEF) {
		q_elem = queue_elem_get(ag_queue);

		if (unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
			ag_queue = em_atomic_group_queue_get_next();
			continue;
		}

		queue_get_name(q_elem, q_name, EM_QUEUE_NAME_LEN - 1);

		n_print = snprintf(q_info_str + len, q_info_str_len - len,
				   AG_Q_INFO_FMT, ag_queue, q_name,
				   q_elem->priority,
				   queue_get_type_str(q_elem->type),
				   queue_get_state_str(q_elem->state),
				   q_elem->queue_group,
				   q_elem->context ? 'Y' : 'N');

		/* Not enough space to hold more queue info */
		if (n_print >= q_info_str_len - len)
			break;

		len += n_print;
		ag_queue = em_atomic_group_queue_get_next();
	}

	/* Atomic group has no queue */
	if (!len) {
		EM_PRINT("Atomic group %" PRI_AGRP "(%s) has no queue!\n",
			 ag, ag_elem->name);
		return;
	}

	/*
	 * To prevent printing incomplete information of the last queue when
	 * there is not enough space to hold all queue info.
	 */
	q_info_str[len] = '\0';
	EM_PRINT(AG_QUEUE_INFO_HDR_STR, ag, ag_elem->name, q_num, q_info_str);
}
