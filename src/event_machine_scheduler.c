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

void
em_atomic_processing_end(void)
{
	em_locm_t *const locm = &em_locm;

	if (locm->current.sched_context_type != EM_SCHED_CONTEXT_TYPE_ATOMIC)
		return;

	if (locm->event_burst_cnt != 0)
		return;

	const queue_elem_t *q_elem = locm->current.sched_q_elem;

	/*
	 * Do nothing for non-atomic queues or if the func has already
	 * been called.
	 */
	if (unlikely(q_elem == NULL ||
		     q_elem->type != EM_QUEUE_TYPE_ATOMIC))
		return;

	if (q_elem->flags.in_atomic_group)
		atomic_group_release();
	else
		odp_schedule_release_atomic();

	/*
	 * Mark that em_atomic_processing_end() has been called
	 * for the current queue.
	 */
	locm->current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE;
}

void
em_ordered_processing_end(void)
{
	em_locm_t *const locm = &em_locm;

	if (locm->event_burst_cnt != 0)
		return;

	const queue_elem_t *q_elem = locm->current.q_elem;
	em_queue_t queue;
	em_queue_type_t qtype;

	if (unlikely(q_elem == NULL))
		return;

	queue = (em_queue_t)(uintptr_t)q_elem->queue;
	qtype = em_queue_get_type(queue);
	if (unlikely(qtype != EM_QUEUE_TYPE_PARALLEL_ORDERED))
		return;

	odp_schedule_release_ordered();
	/*
	 * ODP might not actually release the ordered context here. From an EM
	 * point of view the context needs to be ended since the ODP result is
	 * unknown.
	 */
	locm->current.sched_context_type = EM_SCHED_CONTEXT_TYPE_NONE;
}

void
em_preschedule(void)
{
	odp_schedule_prefetch(1);
}

em_sched_context_type_t
em_sched_context_type_current(em_queue_t *queue)
{
	const em_locm_t *const locm = &em_locm;

	if (locm->current.sched_q_elem == NULL) {
		if (queue != NULL)
			*queue = EM_QUEUE_UNDEF;
		return EM_SCHED_CONTEXT_TYPE_NONE;
	}

	if (queue != NULL)
		*queue = (em_queue_t)(uintptr_t)locm->current.sched_q_elem->queue;

	return locm->current.sched_context_type;
}
