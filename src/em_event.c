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

em_status_t event_init(void)
{
	return EM_OK;
}

void print_event_info(void)
{
	/* for sizeof() only: */
	event_hdr_t evhdr = {0};
	queue_elem_t *q_elem = NULL;

	EM_PRINT("\n"
		 "EM Events\n"
		 "---------\n"
		 "event-hdr size: %zu B\n",
		 sizeof(event_hdr_t));

	EM_DBG("\t\toffset\tsize\n"
	       "\t\t------\t----\n"
	       "esv.state_cnt:\t%3zu B\t%2zu B\n"
	       "esv.state:\t%3zu B\t%2zu B\n"
	       "start_node:\t%3zu B\t%2zu B\n"
	       "q_elem:\t\t%3zu B\t%2zu B\n"
	       "  <empty>\t ---\t%2zu B\n"
	       "event:\t\t%3zu B\t%2zu B\n"
	       "queue:\t\t%3zu B\t%2zu B\n"
	       "egrp:\t\t%3zu B\t%2zu B\n"
	       "egrp_gen:\t%3zu B\t%2zu B\n"
	       "event_size:\t%3zu B\t%2zu B\n"
	       "pool:\t\t%3zu B\t%2zu B\n"
	       "subpool:\t%3zu B\t%2zu B\n"
	       "align_offset:\t%3zu B\t%2zu B\n"
	       "event_type:\t%3zu B\t%2zu B\n"
	       "end_hdr_data:\t%3zu B\t%2zu B\n"
	       "  <align adj.>\t---\t%2zu B\n"
	       "end:\t\t%3zu B\t%2zu B\n",
	       offsetof(event_hdr_t, state_cnt), sizeof(evhdr.state_cnt),
	       offsetof(event_hdr_t, state), sizeof(evhdr.state),
	       offsetof(event_hdr_t, start_node), sizeof(evhdr.start_node),
	       offsetof(event_hdr_t, q_elem), sizeof(evhdr.q_elem),
	       offsetof(event_hdr_t, event) - offsetof(event_hdr_t, q_elem) - sizeof(q_elem),
	       offsetof(event_hdr_t, event), sizeof(evhdr.event),
	       offsetof(event_hdr_t, queue), sizeof(evhdr.queue),
	       offsetof(event_hdr_t, egrp), sizeof(evhdr.egrp),
	       offsetof(event_hdr_t, egrp_gen), sizeof(evhdr.egrp_gen),
	       offsetof(event_hdr_t, event_size), sizeof(evhdr.event_size),
	       offsetof(event_hdr_t, pool), sizeof(evhdr.pool),
	       offsetof(event_hdr_t, subpool), sizeof(evhdr.subpool),
	       offsetof(event_hdr_t, align_offset), sizeof(evhdr.align_offset),
	       offsetof(event_hdr_t, event_type), sizeof(evhdr.event_type),
	       offsetof(event_hdr_t, end_hdr_data), sizeof(evhdr.end_hdr_data),
	       offsetof(event_hdr_t, end) - offsetof(event_hdr_t, end_hdr_data),
	       offsetof(event_hdr_t, end), sizeof(evhdr.end));

	       EM_PRINT("\n");
}

/**
 * Helper for em_event_clone().
 *
 * Clone an event originating from an external odp pkt-pool.
 * Initialize the new cloned event as an EM event and return it.
 */
em_event_t pkt_clone_odp(odp_packet_t pkt, odp_pool_t pkt_pool)
{
	/*
	 * Alloc and copy content via ODP.
	 * Also the ev_hdr in the odp-pkt user_area is copied.
	 */
	odp_packet_t clone_pkt = odp_packet_copy(pkt, pkt_pool);

	if (unlikely(clone_pkt == ODP_PACKET_INVALID))
		return EM_EVENT_UNDEF;

	odp_packet_user_ptr_set(clone_pkt, PKT_USERPTR_MAGIC_NBR);

	odp_event_t odp_clone_event = odp_packet_to_event(clone_pkt);
	event_hdr_t *clone_hdr = odp_packet_user_area(clone_pkt);
	em_event_t clone_event = event_odp2em(odp_clone_event);

	/*
	 * Init hdr of event, also ESV init if needed.
	 * The clone_hdr is a copy of parent's, update only relevant fields.
	 */
	if (esv_enabled())
		clone_event = evstate_init(clone_event, clone_hdr, false);
	else
		clone_hdr->event = clone_event;

	/* clone_hdr->event_type = use parent's type as is */
	clone_hdr->egrp = EM_EVENT_GROUP_UNDEF;

	if (em_shm->opt.pool.statistics_enable) {
		/* clone_hdr->subpool = 0; */
		clone_hdr->pool = EM_POOL_UNDEF;
	}

	return clone_event;
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
output_queue_drain(const queue_elem_t *output_q_elem)
{
	const em_queue_t output_queue = output_q_elem->queue;
	const em_output_func_t output_fn =
		output_q_elem->output.output_conf.output_fn;
	void *const output_fn_args =
		output_q_elem->output.output_conf.output_fn_args;

	const int deq_max = 32;

	em_event_t output_ev_tbl[deq_max];
	/* use same event-tbl, dequeue odp events into the EM event-tbl */
	odp_event_t *const odp_deq_events = (odp_event_t *)output_ev_tbl;

	const odp_queue_t odp_queue = output_q_elem->odp_queue;
	unsigned int output_num;
	int deq;
	int ret;

	const bool esv_ena = esv_enabled();

	do {
		deq = odp_queue_deq_multi(odp_queue,
					  odp_deq_events/*out=output_ev_tbl[]*/,
					  deq_max);
		if (unlikely(deq <= 0))
			return;

		output_num = (unsigned int)deq;
		/* odp_deq_events[] == output_ev_tbl[], .evgen still missing */

		/* decrement pool statistics before passing events out-of-EM */
		if (em_shm->opt.pool.statistics_enable || esv_ena) {
			event_hdr_t *ev_hdrs[output_num];

			event_to_hdr_multi(output_ev_tbl, ev_hdrs, output_num);
			if (em_shm->opt.pool.statistics_enable) {
				poolstat_dec_evhdr_multi_output(ev_hdrs,
								output_num);
			}
			if (esv_ena)
				evstate_em2usr_multi(output_ev_tbl/*in/out*/,
						     ev_hdrs, output_num,
						     EVSTATE__OUTPUT_MULTI);
		}

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
