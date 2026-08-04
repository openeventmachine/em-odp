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
#include <event_machine/platform/env/environment.h>

#include "em_atomic_group.h"
#include "em_atomic_group_types.h"
#include "em_dispatcher_inline.h"
#include "em_error.h"
#include "em_eo.h"
#include "em_eo_types.h"
#include "em_event.h"
#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"
#include "misc/list.h"
#include "misc/objpool.h"

/**
 * Atomic group inits done at global init (once at startup on one core)
 */
em_status_t atomic_group_init(atomic_group_tbl_t *const atomic_group_tbl,
			      atomic_group_pool_t *const atomic_group_pool)
{
	atomic_group_elem_t *atomic_group_elem;
	const uint32_t objpool_subpools = MIN(4, OBJSUBPOOLS_MAX);
	int ret;

	memset(atomic_group_tbl, 0, sizeof(atomic_group_tbl_t));
	memset(atomic_group_pool, 0, sizeof(atomic_group_pool_t));
	odp_atomic_init_u32(&em_shm->atomic_group_count, 0);

	for (int i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
		em_atomic_group_t agrp = agrp_idx2hdl(i);
		atomic_group_elem_t *const agrp_elem =
				atomic_group_elem_get(agrp);

		if (unlikely(!agrp_elem))
			return EM_ERR_BAD_POINTER;

		agrp_elem->atomic_group = agrp; /* store handle */

		/* Init list and lock */
		odp_ticketlock_init(&agrp_elem->lock);
		list_init(&agrp_elem->qlist_head);
		odp_atomic_init_u32(&agrp_elem->num_queues, 0);
		odp_atomic_init_u32(&agrp_elem->num_hi_prio_queues, 0);
	}

	ret = objpool_init(&atomic_group_pool->objpool, objpool_subpools);
	if (ret != 0)
		return EM_ERR_LIB_FAILED;

	for (uint32_t i = 0; i < EM_MAX_ATOMIC_GROUPS; i++) {
		atomic_group_elem = &atomic_group_tbl->ag_elem[i];
		objpool_add(&atomic_group_pool->objpool, i % objpool_subpools,
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
em_atomic_group_t atomic_group_alloc(void)
{
	const atomic_group_elem_t *ag_elem;
	const objpool_elem_t *ag_p_elem;

	ag_p_elem = objpool_rem(&em_shm->atomic_group_pool.objpool,
				em_core_id());

	if (unlikely(ag_p_elem == NULL))
		return EM_ATOMIC_GROUP_UNDEF;

	ag_elem = ag_pool_elem2ag_elem(ag_p_elem);

	odp_atomic_inc_u32(&em_shm->atomic_group_count);
	return ag_elem->atomic_group;
}

em_status_t atomic_group_free(em_atomic_group_t atomic_group)
{
	atomic_group_elem_t *agrp_elem = atomic_group_elem_get(atomic_group);

	if (unlikely(agrp_elem == NULL))
		return EM_ERR_BAD_ID;

	objpool_add(&em_shm->atomic_group_pool.objpool,
		    agrp_elem->atomic_group_pool_elem.subpool_idx,
		    &agrp_elem->atomic_group_pool_elem);

	odp_atomic_dec_u32(&em_shm->atomic_group_count);
	return EM_OK;
}

/**
 * Called by em_queue_delete() to remove the queue from the atomic group list
 */
void atomic_group_remove_queue(queue_elem_t *const q_elem)
{
	if (!q_elem->flags.in_atomic_group)
		return;

	em_atomic_group_t atomic_group = q_elem->agrp.atomic_group;

	if (!invalid_atomic_group(atomic_group)) {
		atomic_group_elem_t *const ag_elem =
			atomic_group_elem_get(atomic_group);

		atomic_group_rem_queue_list(ag_elem, q_elem);
		q_elem->flags.in_atomic_group = false;
		q_elem->agrp.atomic_group = EM_ATOMIC_GROUP_UNDEF;
	}
}

unsigned int atomic_group_count(void)
{
	return odp_atomic_load_u32(&em_shm->atomic_group_count);
}

/**
 * Handle enqueue error for the specified atomic group.
 *
 * Called when an error occurs during the enqueue operation for the
 * given atomic group.
 *
 * @param atomic_group Atomic group handle for which the enqueue error occurred.
 */
static void ag_internal_enq_error(em_atomic_group_t atomic_group,
				  odp_event_t odp_evtbl[], const int num_free)
{
	/* num_free <= EM_SCHED_MULTI_MAX_BURST, use fixed-size (non-VLA) tables */
	event_hdr_t *ev_hdr_tbl[EM_SCHED_MULTI_MAX_BURST];
	em_event_t ev_tbl[EM_SCHED_MULTI_MAX_BURST];

	event_init_odp_multi(odp_evtbl, ev_tbl/*out*/, ev_hdr_tbl/*out*/,
			     num_free, true/*is_extev*/, NULL/*not a dispatch: always scan*/);
	/* Drop events that could not be enqueued */
	em_free_multi(ev_tbl, num_free);
	/*
	 * Use dispatch escope since this func is called only from
	 * dispatch_round() => atomic_group_dispatch()
	 */
	INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_DISPATCH,
		       "Atomic group:%" PRI_AGRP " internal enqueue fails: %d events dropped",
		       atomic_group, num_free);
}

/**
 * Enqueue atomic group events into the AG-specific internal queue (=stash).
 *
 * This function is run concurrently by all cores dispatching events from
 * queues that belong to this atomic group - enqueuing is done potentially in
 * parallel.
 *
 * @param ag_elem     Atomic group element
 * @param q_elem      Queue element
 * @param odp_evtbl   Array of ODP events to enqueue
 * @param num_events  Number of events in the array
 * @param priority    Priority of the queue
 */
static void ag_internal_enq(atomic_group_elem_t *ag_elem, const queue_elem_t *q_elem,
			    odp_event_t odp_evtbl[], const int num_events,
			    const em_queue_prio_t priority)
{
	/* num_events <= EM_SCHED_MULTI_MAX_BURST, use fixed-size (non-VLA) table */
	stash_entry_t entry_tbl[EM_SCHED_MULTI_MAX_BURST];
	odp_stash_t stash;

	const em_queue_t queue = (em_queue_t)(uintptr_t)q_elem->queue;
	const uint16_t qidx = (uint16_t)queue_hdl2idx(queue);

	for (int i = 0; i < num_events; i++)
		entry_tbl[i] = (stash_entry_t){.qidx = qidx,
					       .evptr = (uintptr_t)odp_evtbl[i]};

	if (priority == EM_QUEUE_PRIO_HIGHEST)
		stash = ag_elem->stashes.hi_prio;
	else
		stash = ag_elem->stashes.lo_prio;

	/*
	 * Increment the event count before enqueueing to ensure the consumer
	 * stays in the loop rather a bit too long than leaves before all events
	 * are processed.
	 */
	(void)__atomic_fetch_add(&ag_elem->dispatch_lock.lock_and_evcnt,
				 (uint64_t)num_events, __ATOMIC_RELEASE);

	/* Enqueue events to internal queue (=stash) */
	int ret = odp_stash_put_u64(stash, &entry_tbl[0].u64, num_events);

	if (unlikely(ret != num_events)) {
		int enq_cnt = ret > 0 ? ret : 0;

		(void)__atomic_fetch_sub(&ag_elem->dispatch_lock.lock_and_evcnt,
					 (uint64_t)(num_events - enq_cnt),
					 __ATOMIC_RELEASE);
		ag_internal_enq_error(ag_elem->atomic_group,
				      &odp_evtbl[enq_cnt], num_events - enq_cnt);
	}
}

/**
 * Dequeue from the AG-internal queue (stash).
 *
 * This function attempts to dequeue up to 'num_events' entries from the atomic
 * group internal queue (stash). Returns the number of entries actually dequeued.
 *
 * @param ag_elem     Atomic group element.
 * @param entry_tbl   Output array for dequeued entries.
 * @param num_events  Maximum number of events to dequeue.
 *
 * @return Number of entries dequeued.
 */
static uint32_t ag_internal_deq(atomic_group_elem_t *ag_elem,
				stash_entry_t entry_tbl[/*out*/], const int num_events)
{
	/*
	 * The function call_eo_receive_fn/multi() will convert to
	 * EM events with event-generation counts, if ESV is enabled,
	 * before passing the events to the user EO.
	 */
	int32_t hi_cnt = 0;
	int32_t lo_cnt = 0;
	uint32_t deq_cnt = 0;
	uint32_t max_deq = num_events;

	/*
	 * hi-prio events: only touch the hi-prio stash when the atomic group
	 * actually has an EM_QUEUE_PRIO_HIGHEST queue.
	 */
	if (odp_atomic_load_u32(&ag_elem->num_hi_prio_queues) > 0) {
		hi_cnt = odp_stash_get_u64(ag_elem->stashes.hi_prio,
					   &entry_tbl[0].u64 /*[out]*/, max_deq);
		if (hi_cnt > 0)
			deq_cnt = hi_cnt;
	}

	/* ...then lo-prio events */
	if (deq_cnt < max_deq) {
		lo_cnt = odp_stash_get_u64(ag_elem->stashes.lo_prio,
					   &entry_tbl[deq_cnt].u64 /*[out]*/,
					   max_deq - deq_cnt);
		if (lo_cnt > 0)
			deq_cnt += lo_cnt;
	}

	if (unlikely(EM_CHECK_LEVEL > 1 && (hi_cnt < 0 || lo_cnt < 0))) {
		/* Report error if dequeue failed */
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED, EM_ESCOPE_DISPATCH,
			       "Atomic group:%" PRI_AGRP " internal dequeue fails:\n"
			       "  max_deq:%d deq_cnt:%d (hi_cnt:%d lo_cnt:%d)",
			       ag_elem->atomic_group, max_deq, deq_cnt, hi_cnt, lo_cnt);
	}

	/* Decrement the event count by the number dequeued */
	if (deq_cnt > 0)
		(void)__atomic_fetch_sub(&ag_elem->dispatch_lock.lock_and_evcnt,
					 deq_cnt, __ATOMIC_ACQUIRE);

	return deq_cnt; /* number of events dequeued */
}

/**
 * Dispatch the dequeued atomic group events in batches
 *
 * Dispatch the dequeued events in batches, where each batch contains events
 * from the same atomic queue.
 *
 * @param deq_entry_tbl  Array of dequeued entries to be dispatched.
 * @param deq_cnt        Number of dequeued entries.
 */
static inline void
ag_dispatch_batches(const stash_entry_t deq_entry_tbl[], const uint32_t deq_cnt)
{
	em_locm_t *const locm = &em_locm;
	/* deq_cnt <= EM_SCHED_AG_MULTI_MAX_BURST, use fixed-size (non-VLA) table */
	odp_event_t deq_evtbl[EM_SCHED_AG_MULTI_MAX_BURST];

	for (uint32_t i = 0; i < deq_cnt; i++)
		deq_evtbl[i] = (odp_event_t)(uintptr_t)deq_entry_tbl[i].evptr;

	locm->event_burst_cnt = deq_cnt;
	uint32_t tbl_idx = 0; /* index into ..._tbl[] */

	/*
	 * Dispatch in batches of 'batch_cnt' events.
	 * Each batch contains events from the same atomic queue.
	 */
	do {
		const uint32_t qidx = deq_entry_tbl[tbl_idx].qidx;
		const em_queue_t queue = queue_idx2hdl(qidx);
		queue_elem_t *const batch_qelem = queue_elem_get(queue);
		uint32_t batch_cnt = 1;

		/* i < deq_cnt <= EM_SCHED_AG_MULTI_MAX_BURST */
		for (uint32_t i = tbl_idx + 1; i < deq_cnt &&
		     deq_entry_tbl[i].qidx == qidx; i++) {
			batch_cnt++;
		}

		dispatch_events(&deq_evtbl[tbl_idx],
				batch_cnt, batch_qelem);
		tbl_idx += batch_cnt;
	} while (tbl_idx < deq_cnt);
}

/**
 * Fast-path acquire of the atomic group dispatch lock.
 *
 * Single compare-and-swap that succeeds only when the lock is free AND the
 * event count is zero, i.e. the internal stash is empty. On success the caller
 * holds the lock with an empty stash and can dispatch its just-scheduled events
 * directly, bypassing the stash entirely.
 *
 * Unlike ag_trylock() this never spins: any contention (lock held or events
 * already stashed) returns false and the caller falls back to the slow path.
 *
 * @param ag_elem Pointer to the atomic group element.
 *
 * @return true if the lock was acquired with an empty stash, false otherwise.
 */
static bool ag_acquire_if_idle(atomic_group_elem_t *const ag_elem)
{
	atomic_group_dispatch_lock_t expected = {.parts = {.lock = 0,
							   .wrap = WRAP_PROTECT,
							   .evcnt = 0}};
	atomic_group_dispatch_lock_t desired = {.parts = {.lock = 1,
							  .wrap = WRAP_PROTECT,
							  .evcnt = 0}};

	return __atomic_compare_exchange_n(&ag_elem->dispatch_lock.lock_and_evcnt,
					   &expected.lock_and_evcnt,
					   desired.lock_and_evcnt,
					   false, __ATOMIC_ACQUIRE /*success*/,
					   __ATOMIC_RELAXED /*failure*/);
}

/**
 * Try to acquire the atomic group dispatch lock.
 *
 * Attempts to atomically set the lock bit in the atomic group's dispatch lock.
 * If the lock was not held, it will be acquired and the function returns true.
 * If the lock was already held, the function returns false.
 * Sets both the lock and the event count with a single atomic operation.
 *
 * Must loop to ensure that this function together with the ag_unlock() manages
 * to handle all events and no events are left in the internal queue when
 * returning.
 *
 * This function pairs with ag_unlock() to release the lock.
 *
 * @param ag_elem Pointer to the atomic group element.
 *
 * @return true if the lock was successfully acquired, false otherwise.
 */
static bool ag_trylock(atomic_group_elem_t *const ag_elem)
{
	bool success = false;
	atomic_group_dispatch_lock_t lock_expected;
	atomic_group_dispatch_lock_t lock_desired;

	do {
		lock_expected.lock_and_evcnt =
			__atomic_load_n(&ag_elem->dispatch_lock.lock_and_evcnt,
					__ATOMIC_RELAXED);
		lock_desired.parts.lock = 1;
		lock_desired.parts.wrap = lock_expected.parts.wrap;
		lock_desired.parts.evcnt = lock_expected.parts.evcnt;

		success = __atomic_compare_exchange_n(&ag_elem->dispatch_lock.lock_and_evcnt,
						      &lock_expected.lock_and_evcnt,
						      lock_desired.lock_and_evcnt,
						      false, __ATOMIC_ACQUIRE /*success*/,
						      __ATOMIC_RELAXED /*failure*/);
	} while (!success);

	bool lock_taken = lock_expected.parts.lock == 0 && lock_desired.parts.lock == 1;

	return lock_taken;
}

/**
 * @brief Release the atomic group dispatch lock.
 *
 * Normal scenario (em_atomic_processing_end() NOT called):
 * Atomically releases the lock for the atomic group, setting the lock bit to 0.
 * Only succeeds if the lock is currently held and the event count is zero.
 * Clears both the lock and the event count with a single atomic operation.
 * OR:
 * This core called em_atomic_processing_end() during dispatch:
 * The atomic context was dropped for the atomic group, i.e. the atomic
 * group lock was released - another EM-core might now be processing
 * events from the same atomic group.
 *
 * This function pairs with ag_trylock() to acquire the lock.
 *
 * @param ag_elem Pointer to the atomic group element.
 *
 * @return true if the lock is unlocked, false if locked.
 */
static bool ag_unlock(atomic_group_elem_t *const ag_elem)
{
	em_locm_t *const locm = &em_locm;

	if (!locm->atomic_group_released) {
		/*
		 * Normal scenario - em_atomic_processing_end() NOT called:
		 */
		atomic_group_dispatch_lock_t unlock_expected = {.parts = {.lock = 1,
									  .wrap = WRAP_PROTECT,
									  .evcnt = 0}};
		atomic_group_dispatch_lock_t unlock_desired = {.parts = {.lock = 0,
									 .wrap = WRAP_PROTECT,
									 .evcnt = 0}};
		bool unlocked = __atomic_compare_exchange_n(&ag_elem->dispatch_lock.lock_and_evcnt,
							    &unlock_expected.lock_and_evcnt,
							    unlock_desired.lock_and_evcnt,
							    false, __ATOMIC_RELEASE /*success*/,
							    __ATOMIC_RELAXED /*failure*/);
		return unlocked;
	}

	/*
	 * This core called em_atomic_processing_end() during dispatch.
	 *
	 * The atomic context was dropped for the atomic group, i.e. the atomic
	 * group lock was released - another EM-core might now be processing
	 * events from the same atomic group.
	 *
	 * atomic_group_release() set ag_elem->dispatch_lock.part.lock = 0.
	 */
	locm->atomic_group_released = false; /* reset for next round */

	bool success = false;
	atomic_group_dispatch_lock_t lock_expected;
	atomic_group_dispatch_lock_t lock_desired;

	do {
		lock_expected.lock_and_evcnt =
			__atomic_load_n(&ag_elem->dispatch_lock.lock_and_evcnt,
					__ATOMIC_RELAXED);

		if (lock_expected.parts.lock == 1) {
			/* Agrp lock taken by another core, let it handle the remaining events */
			return true; /* unlocked = true (this core doesn’t hold the lock) */
		}

		/*
		 * lock_expected.parts.lock == 0
		 * Agrp lock not yet taken by another core:
		 */

		if (lock_expected.parts.evcnt == 0 &&
		    lock_expected.parts.wrap == WRAP_PROTECT) {
			/* No events */
			return true; /* unlocked = true */
		}

		/* Still events, try to re-acquire lock */
		lock_desired.parts.lock = 1;
		lock_desired.parts.wrap = lock_expected.parts.wrap;
		lock_desired.parts.evcnt = lock_expected.parts.evcnt;

		success = __atomic_compare_exchange_n(&ag_elem->dispatch_lock.lock_and_evcnt,
						      &lock_expected.lock_and_evcnt,
						      lock_desired.lock_and_evcnt,
						      false, __ATOMIC_ACQUIRE /*success*/,
						      __ATOMIC_RELAXED /*failure*/);
		/* 'unlocked = false' here */
	} while (!success);

	/* unlocked = false - this core has the AG lock */
	return false;
}

/**
 * Release the atomic group dispatch lock.
 *
 * Called by em_atomic_processing_end() if 'locm->event_burst_cnt == 0' since
 * the atomic context can only be dropped for the last event (or last set of
 * events in the case of EO multircv).
 */
void atomic_group_release(void)
{
	em_locm_t *const locm = &em_locm;

	if (unlikely(locm->atomic_group_released)) {
		/*
		 * The atomic group lock has already been released by
		 * em_atomic_processing_end() - no need to release it again.
		 */
		return;
	}

	/*
	 * The caller, i.e. em_atomic_processing_end() has verified that
	 * locm->event_burst_cnt == 0.
	 */
	const queue_elem_t *q_elem = locm->current.sched_q_elem;
	em_atomic_group_t atomic_group = q_elem->agrp.atomic_group;
	atomic_group_elem_t *const ag_elem = atomic_group_elem_get(atomic_group);

	bool success = false;
	atomic_group_dispatch_lock_t lock_expected;
	atomic_group_dispatch_lock_t lock_desired;

	do {
		lock_expected.lock_and_evcnt =
			__atomic_load_n(&ag_elem->dispatch_lock.lock_and_evcnt,
					__ATOMIC_RELAXED);
		lock_desired.parts.lock = 0;
		lock_desired.parts.wrap = lock_expected.parts.wrap;
		lock_desired.parts.evcnt = lock_expected.parts.evcnt;

		success = __atomic_compare_exchange_n(&ag_elem->dispatch_lock.lock_and_evcnt,
						      &lock_expected.lock_and_evcnt,
						      lock_desired.lock_and_evcnt,
						      false, __ATOMIC_ACQUIRE /*success*/,
						      __ATOMIC_RELAXED /*failure*/);
	} while (!success);

	locm->atomic_group_released = true;
}

/**
 * Drain and dispatch the atomic group's stashed events until the stash empties.
 *
 * The caller must hold the atomic group dispatch lock. Loops dequeuing and
 * dispatching stashed events (added by this and other cores) in batches until
 * ag_unlock() reports the lock released with the stash drained - or until
 * em_atomic_processing_end() handed dispatching over to another core.
 *
 * @param ag_elem Atomic group element (dispatch lock held by the caller).
 */
static void ag_dispatch_loop(atomic_group_elem_t *const ag_elem)
{
	/* deq_cnt <= EM_SCHED_AG_MULTI_MAX_BURST, use fixed-size (non-VLA) table */
	stash_entry_t entry_tbl[EM_SCHED_AG_MULTI_MAX_BURST];

	do {
		uint32_t deq_cnt = ag_internal_deq(ag_elem, entry_tbl /*[out]*/,
						   EM_SCHED_AG_MULTI_MAX_BURST);
		/*
		 * Dispatch the dequeued events/entries in batches.
		 * Each batch contains events from the same atomic queue.
		 */
		if (deq_cnt > 0)
			ag_dispatch_batches(entry_tbl, deq_cnt);

		/* Check if the lock can be unlocked */
		bool unlocked = ag_unlock(ag_elem);

		if (unlocked) {
			/*
			 * The atomic group lock is released and the event count
			 * is zero or em_atomic_processing_end() was called
			 * during dispatch and another core took over dispatching.
			 * The atomic group processing (on this core) has ended.
			 */
			return;
		}
	} while (true);
}

/**
 * Dispatch function for events from queues belonging to an atomic group.
 *
 * Atomicity across the queues of an atomic group is guaranteed by letting only
 * one core dispatch the group's events at a time, serialized by the per-group
 * dispatch lock.
 *
 * Fast path: when the lock is free and the internal stash is empty, take the
 * lock and dispatch the just-scheduled events directly. They have already been
 * scheduled and, with an empty stash, are the head of the atomic group FIFO -
 * pushing them through the stash only to immediately pull them back out is pure
 * overhead. Events that other cores stash while this core dispatches are
 * drained afterwards by ag_dispatch_loop().
 *
 * Slow path (lock held elsewhere or stash non-empty): enqueue the events into
 * the group's internal stash - this can run in parallel on several cores - then
 * try to become the single dispatching core.
 *
 * @param odp_evtbl   Events received from the scheduler from an AG queue
 * @param num_events  Number of events received
 * @param q_elem      Queue element, queue is part of the atomic group
 */
void atomic_group_dispatch(odp_event_t odp_evtbl[], const int num_events,
			   queue_elem_t *const q_elem)
{
	atomic_group_elem_t *ag_elem = atomic_group_elem_get(q_elem->agrp.atomic_group);

	if (ag_acquire_if_idle(ag_elem)) {
		/*
		 * Fast path: lock taken with an empty stash. Release the source
		 * queue's ODP atomic context (the AG dispatch lock provides
		 * atomicity) and dispatch the scheduled events directly, without
		 * a stash round-trip.
		 */
		odp_schedule_release_atomic();
		em_locm.event_burst_cnt = num_events;
		dispatch_events(odp_evtbl, num_events, q_elem);
		/*
		 * dispatch_events() may have released the AG lock via
		 * em_atomic_processing_end(). ag_unlock() resolves the lock
		 * state: when it returns false this core still holds the lock
		 * and drains any events other cores stashed while it dispatched
		 * (the stash get inside ag_dispatch_loop() is only safe while
		 * holding the lock).
		 */
		if (!ag_unlock(ag_elem))
			ag_dispatch_loop(ag_elem);
		return;
	}

	/*
	 * Slow path: another core holds the lock or the stash already holds
	 * events. Enqueue into the AG internal stash to preserve ordering, then
	 * try to acquire the lock - if unavailable another core is already
	 * dispatching this atomic group.
	 */
	ag_internal_enq(ag_elem, q_elem, odp_evtbl, num_events, q_elem->priority);

	if (!ag_trylock(ag_elem))
		return; /* Lock is already held by another thread */

	/* hint */
	odp_schedule_release_atomic();
	ag_dispatch_loop(ag_elem);
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
	atomic_group_elem_t *ag_elem;
	em_atomic_group_t ag_check;
	char ag_name[EM_ATOMIC_GROUP_NAME_LEN];
	int len = 0;
	int n_print = 0;

	em_atomic_group_t ag = em_atomic_group_first(&ag_num);

	/*
	 * ag_num might not match the actual number of atomic groups returned
	 * by iterating with func em_atomic_group_next() if atomic groups
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

		em_atomic_group_name(ag, ag_name, sizeof(ag_name));

		ag_check = em_atomic_group_find(ag_name);
		if (unlikely(ag_elem == NULL || ag_check != ag ||
			     !atomic_group_allocated(ag_elem))) {
			ag = em_atomic_group_next();
			continue;
		}

		n_print = snprintf(ag_info_str + len, ag_info_str_len - len,
				   AG_INFO_FMT, ag, ag_name, ag_elem->queue_group,
				   odp_atomic_load_u32(&ag_elem->num_queues));

		/* Not enough space to hold more atomic group info */
		if (n_print >= ag_info_str_len - len)
			break;

		len += n_print;
		ag = em_atomic_group_next();
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

	ag_queue = em_atomic_group_queue_first(&q_num, ag);

	/*
	 * q_num may not match the number of queues actually returned by iterating
	 * with em_atomic_group_queue_next() if queues are added or removed
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
			ag_queue = em_atomic_group_queue_next();
			continue;
		}

		queue_name(q_elem, q_name, EM_QUEUE_NAME_LEN - 1);

		n_print = snprintf(q_info_str + len, q_info_str_len - len,
				   AG_Q_INFO_FMT, ag_queue, q_name,
				   q_elem->priority,
				   queue_type_str(q_elem->type),
				   queue_state_str(q_elem->state),
				   q_elem->queue_group,
				   q_elem->context ? 'Y' : 'N');

		/* Not enough space to hold more queue info */
		if (n_print >= q_info_str_len - len)
			break;

		len += n_print;
		ag_queue = em_atomic_group_queue_next();
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

void print_ag_elem_info(void)
{
	EM_PRINT("\n"
		 "EM Atomic Groups\n"
		 "----------------\n"
		 "ag-elem size: %zu B\n",
		 sizeof(atomic_group_elem_t));

	DBG_PRINT("\t\toffset\tsize\n"
		  "\t\t------\t-----\n"
		  "atomic_group:\t%3zu B\t%3zu B\n"
		  "queue_group:\t%3zu B\t%3zu B\n"
		  "ag pool_elem:\t%3zu B\t%3zu B\n"
		  "stashes:\t%3zu B\t%3zu B\n"
		  "lock:\t\t%3zu B\t%3zu B\n"
		  "num_queues:\t%3zu B\t%3zu B\n"
		  "qlist_head[]:\t%3zu B\t%3zu B\n"
		  "name:\t\t%3zu B\t%3zu B\n",
		  offsetof(atomic_group_elem_t, atomic_group),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  sizeof_field(atomic_group_elem_t, atomic_group),
		  offsetof(atomic_group_elem_t, queue_group),
		  /* NOLINTNEXTLINE(bugprone-sizeof-expression) */
		  sizeof_field(atomic_group_elem_t, queue_group),
		  offsetof(atomic_group_elem_t, atomic_group_pool_elem),
		  sizeof_field(atomic_group_elem_t, atomic_group_pool_elem),
		  offsetof(atomic_group_elem_t, stashes),
		  sizeof_field(atomic_group_elem_t, stashes),
		  offsetof(atomic_group_elem_t, lock),
		  sizeof_field(atomic_group_elem_t, lock),
		  offsetof(atomic_group_elem_t, num_queues),
		  sizeof_field(atomic_group_elem_t, num_queues),
		  offsetof(atomic_group_elem_t, qlist_head),
		  sizeof_field(atomic_group_elem_t, qlist_head),
		  offsetof(atomic_group_elem_t, name),
		  sizeof_field(atomic_group_elem_t, name));

	EM_PRINT("\n");
}
