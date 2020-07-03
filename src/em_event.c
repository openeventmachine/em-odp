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

/*
 * Sanity check that no extra padding is added to the event_hdr_t by
 * alignment directives etc.
 */
typedef event_hdr_t _ev_hdr__size_check__arr_t[3];
COMPILE_TIME_ASSERT(sizeof(_ev_hdr__size_check__arr_t) ==
		    3 * sizeof(event_hdr_t), EVENT_HDR_SIZE_ERROR2);

/*
 * Verify the value set for EM_CHECK_LEVEL - this define is set either from
 * the include/event_machine/platform/event_machine_config.h file or by the
 * configure.ac option --enable-check-level=N.
 */
COMPILE_TIME_ASSERT(EM_CHECK_LEVEL >= 0 && EM_CHECK_LEVEL <= 3,
		    EM_CHECK_LEVEL__BAD_VALUE);

em_status_t
event_init(void)
{
	return EM_OK;
}

void
output_queue_track(queue_elem_t *const output_q_elem)
{
	output_queue_track_t *const track =
		&em_locm.output_queue_track;
	const int qidx = queue_hdl2idx(output_q_elem->queue);

	if (track->used_queues[qidx] == NULL) {
		track->used_queues[qidx] = output_q_elem;
		track->idx[track->idx_cnt++] = qidx;
	}
}

void
output_queue_drain(queue_elem_t *const output_q_elem)
{
	const em_queue_t output_queue = output_q_elem->queue;
	const em_output_func_t output_fn =
		output_q_elem->output.output_conf.output_fn;
	void *const output_fn_args =
		output_q_elem->output.output_conf.output_fn_args;

	const int deq_max = 32;
	const odp_queue_t odp_queue = output_q_elem->odp_queue;
	odp_event_t odp_deq_events[deq_max];
	em_event_t *output_ev_tbl;
	unsigned int output_num;
	int deq, ret;

	do {
		deq = odp_queue_deq_multi(odp_queue, odp_deq_events, deq_max);
		if (unlikely(deq <= 0))
			return;

		output_num = (unsigned int)deq;
		output_ev_tbl = events_odp2em(odp_deq_events);

		ret = output_fn(output_ev_tbl, output_num,
				output_queue, output_fn_args);
		if (unlikely((unsigned int)ret != output_num))
			em_free_multi(&output_ev_tbl[ret], output_num - ret);
	} while (deq > 0);
}

void
output_queue_buffering_drain(void)
{
	output_queue_track_t *const track = &em_locm.output_queue_track;

	for (unsigned int i = 0; i < track->idx_cnt; i++) {
		int qidx = track->idx[i];
		queue_elem_t *output_q_elem = track->used_queues[qidx];
		env_spinlock_t *lock = &output_q_elem->output.lock;

		/*
		 * drain if lock available, otherwise another core is already
		 * draining so no need to do anything.
		 */
		if (env_spinlock_trylock(lock)) {
			output_queue_drain(output_q_elem);
			env_spinlock_unlock(lock);
		}

		track->idx[i] = 0;
		track->used_queues[qidx] = NULL;
	}
	track->idx_cnt = 0;
}
