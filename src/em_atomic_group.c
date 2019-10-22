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
	int i, ret;
	atomic_group_elem_t *atomic_group_elem;
	const int cores = em_core_count();

	memset(atomic_group_tbl, 0, sizeof(atomic_group_tbl_t));
	memset(atomic_group_pool, 0, sizeof(atomic_group_pool_t));
	env_atomic32_init(&em_shm->atomic_group_count);

	for (i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
		em_atomic_group_t agrp = agrp_idx2hdl(i);
		atomic_group_elem_t *const agrp_elem =
				atomic_group_elem_get(agrp);

		agrp_elem->atomic_group = agrp; /* store handle */

		/* Init list and lock */
		env_spinlock_init(&agrp_elem->lock);
		list_init(&agrp_elem->qlist_head);
		env_atomic32_init(&agrp_elem->num_queues);
	}

	ret = objpool_init(&atomic_group_pool->objpool, cores);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
		atomic_group_elem = &atomic_group_tbl->ag_elem[i];
		objpool_add(&atomic_group_pool->objpool, i % cores,
			    &atomic_group_elem->atomic_group_pool_elem);
	}

	return EM_OK;
}

static inline atomic_group_elem_t *
ag_pool_elem2ag_elem(objpool_elem_t *const atomic_group_pool_elem)
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
	atomic_group_elem_t *ag_elem;
	objpool_elem_t *ag_p_elem;

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
	/*
	 * Check if atomic group processing has ended for this core, meaning
	 * the application called em_atomic_processing_end()
	 */
	if (em_locm.atomic_group_released) {
		em_locm.atomic_group_released = 0;
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

static inline void
ag_internal_enq(atomic_group_elem_t *const ag_elem, em_event_t ev_tbl[],
		const int num_events)
{
	odp_event_t *const odp_ev_tbl = events_em2odp(ev_tbl);

	/* Enqueue events to internal queue */
	const int ret = odp_queue_enq_multi(ag_elem->internal_queue,
					    odp_ev_tbl, num_events);

	if (ret != num_events)
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_ERROR,
			       "Atomic group internal queue enqueue failed!");
}

/*
 * Atomic group event dispatch loop starts when an event is scheduled from
 * the atomic queue that belongs to the group. The atomic context of that queue
 * is locked until the core calls odp_schedule() again. The atomic group
 * dispatch loop can run a long time, or possibly indefinitely. To ensure
 * this queue is scheduled normally by other cores, the atomic context
 * must be freed from this core.
 */
static inline void
atomic_group_release_sched_queue(atomic_group_elem_t *const ag_elem,
				 queue_elem_t *const q_elem)
{
	odp_event_t odp_events[EVENT_CACHE_FLUSH];
	event_hdr_t *ev_hdrs[EVENT_CACHE_FLUSH];
	em_event_t *events;
	int i, ev_cnt, enq_cnt;

	/* Pause global scheduling for this core */
	odp_schedule_pause();

	/*
	 * All events received from scheduler are now prescheduled
	 * events from core local cache.
	 */
	while ((ev_cnt = odp_schedule_multi_no_wait(NULL, odp_events,
						    EVENT_CACHE_FLUSH)) > 0) {
		events = events_odp2em(odp_events);
		events_to_event_hdrs(events, ev_hdrs, ev_cnt);
		/* Insert the original q_elem pointer into the event header */
		for (i = 0; i < ev_cnt; i++)
			ev_hdrs[i]->q_elem = q_elem;

		enq_cnt = odp_queue_enq_multi(ag_elem->internal_queue,
					      odp_events, ev_cnt);

		if (unlikely(ev_cnt != enq_cnt)) {
			if (enq_cnt < 0)
				enq_cnt = 0;
			event_free_multi(&events[enq_cnt], ev_cnt - enq_cnt);
			INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_ERROR,
				       "Atomic group internal enqueue failed");
		}
	}

	/* Resume global scheduling for this core */
	odp_schedule_resume();
	/*
	 * Now that core local cache is empty, the following call will release
	 * the atomic queue context in the scheduler. If there are prescheduled
	 * events for this core, this call would do nothing.
	 */
	odp_schedule_release_atomic();
}

void
atomic_group_dispatch(em_event_t ev_tbl[], event_hdr_t *const ev_hdr_tbl[],
		      const int num_events, queue_elem_t *const q_elem)
{
	atomic_group_elem_t *const ag_elem =
		atomic_group_elem_get(q_elem->atomic_group);

	/* Insert the original q_elem pointer into the event header */
	for (int i = 0; i < num_events; i++)
		ev_hdr_tbl[i]->q_elem = q_elem;

	/* Put scheduled event to atomic group internal queue */
	ag_internal_enq(ag_elem, ev_tbl, num_events);

	/* Try to acquire atomic group */
	if (env_spinlock_trylock(&ag_elem->lock)) {
		/*
		 * Release the scheduled atomic odp queue context before
		 * dedicating this core for atomic group event dispatching.
		 */
		atomic_group_release_sched_queue(ag_elem, q_elem);

		odp_event_t ag_ev_tbl[EM_SCHED_AG_MULTI_MAX_BURST];
		event_hdr_t *deq_hdr_tbl[EM_SCHED_AG_MULTI_MAX_BURST];
		em_event_t *deq_ev_tbl;
		int ag_ev_cnt, i;

		em_locm.atomic_group_released = 0;

		/*
		 * Loop until no more events or until atomic processing end.
		 * Events in the ag_elem->internal_queue have been scheduled
		 * already once and should be dispatched asap.
		 */
		do {
			ag_ev_cnt =
			odp_queue_deq_multi(ag_elem->internal_queue,
					    ag_ev_tbl,
					    EM_SCHED_AG_MULTI_MAX_BURST);
			if (unlikely(ag_ev_cnt <= 0)) {
				env_spinlock_unlock(&ag_elem->lock);
				return;
			}

			deq_ev_tbl = events_odp2em(ag_ev_tbl);
			events_to_event_hdrs(deq_ev_tbl, deq_hdr_tbl,
					     ag_ev_cnt);

			em_locm.event_burst_cnt = ag_ev_cnt;
			for (i = 0; i < ag_ev_cnt; i++) {
				/*
				 * Need to dispatch 1-by-1 because every event
				 * might originate from a different queue in
				 * the atomic group
				 */
				dispatch_events(&deq_hdr_tbl[i], 1,
						deq_hdr_tbl[i]->q_elem);
			}
		} while (!ag_local_processing_ended(ag_elem));
	}
}
