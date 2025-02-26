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
	EM_PRINT("\n"
		 "EM Events\n"
		 "---------\n"
		 "event-hdr size: %zu B\n",
		 sizeof(event_hdr_t));

	EM_DBG("\t\toffset\tsize\n"
	       "\t\t------\t----\n"
	       "esv.state_cnt:\t%3zu B\t%2zu B\n"
	       "esv.state:\t%3zu B\t%2zu B\n"
	       "flags:\t\t%3zu B\t%2zu B\n"
	       "align_offset:\t%3zu B\t%2zu B\n"
	       "event_type:\t%3zu B\t%2zu B\n"
	       "event:\t\t%3zu B\t%2zu B\n"
	       "event_size:\t%3zu B\t%2zu B\n"
	       "egrp_gen:\t%3zu B\t%2zu B\n"
	       "egrp:\t\t%3zu B\t%2zu B\n"
	       "tmo:\t\t%3zu B\t%2zu B\n"
	       "user_area info:\t%3zu B\t%2zu B\n"
	       "end_hdr_data:\t%3zu B\t%2zu B\n"
	       "  <pad>\t\t%3zu B\n"
	       "end:\t\t%3zu B\t%2zu B\n",
	       offsetof(event_hdr_t, state_cnt), sizeof_field(event_hdr_t, state_cnt),
	       offsetof(event_hdr_t, state), sizeof_field(event_hdr_t, state),
	       offsetof(event_hdr_t, flags), sizeof_field(event_hdr_t, flags),
	       offsetof(event_hdr_t, align_offset), sizeof_field(event_hdr_t, align_offset),
	       offsetof(event_hdr_t, event_type), sizeof_field(event_hdr_t, event_type),
	       offsetof(event_hdr_t, event), sizeof_field(event_hdr_t, event),
	       offsetof(event_hdr_t, event_size), sizeof_field(event_hdr_t, event_size),
	       offsetof(event_hdr_t, egrp_gen), sizeof_field(event_hdr_t, egrp_gen),
	       offsetof(event_hdr_t, egrp), sizeof_field(event_hdr_t, egrp),
	       offsetof(event_hdr_t, tmo), sizeof_field(event_hdr_t, tmo),
	       offsetof(event_hdr_t, user_area), sizeof_field(event_hdr_t, user_area),
	       offsetof(event_hdr_t, end_hdr_data), sizeof_field(event_hdr_t, end_hdr_data),
	       offsetof(event_hdr_t, end) - offsetof(event_hdr_t, end_hdr_data),
	       offsetof(event_hdr_t, end), sizeof_field(event_hdr_t, end));

	       EM_PRINT("\n");
}

/**
 * Helper for em_event_clone().
 *
 * Clone an event originating from an external odp pkt-pool.
 * Initialize the new cloned event as an EM event and return it.
 *
 * Alloc and copy content via ODP.
 * Also the ev_hdr in the odp-pkt user_area is copied.
 */
em_event_t pkt_clone_odp(odp_packet_t pkt, odp_pool_t pkt_pool,
			 uint32_t offset, uint32_t size,
			 bool clone_uarea, bool is_clone_part)
{
	odp_packet_t clone_pkt;

	if (is_clone_part) {
		/* only data is copied, ODP-uarea isn't */
		clone_pkt = odp_packet_copy_part(pkt, offset, size, pkt_pool);
		if (unlikely(clone_pkt == ODP_PACKET_INVALID))
			return EM_EVENT_UNDEF;

		const void *src_odp_uarea = odp_packet_user_area(pkt);
		void *dst_odp_uarea = odp_packet_user_area(clone_pkt);
		size_t cpy_size = sizeof(event_hdr_t);

		if (clone_uarea) {
			/* copy ODP-uarea (EM-hdr + EM-uarea) */
			uint32_t src_uarea_size = odp_packet_user_area_size(pkt);
			uint32_t dst_uarea_size = odp_packet_user_area_size(clone_pkt);

			if (unlikely(dst_uarea_size < src_uarea_size)) {
				odp_packet_free(clone_pkt);
				return EM_EVENT_UNDEF;
			}
			/* update 'cpy_size' to include the whole ODP-uarea (EM-hdr + EM-uarea) */
			cpy_size = src_uarea_size;
		}
		/* copy the EM-hdr and possibly also the EM-uarea if requested */
		memcpy(dst_odp_uarea, src_odp_uarea, cpy_size);
	} else {
		/* identical clone, also ODP-uarea (EM-hdr + EM-uarea) is copied */
		clone_pkt = odp_packet_copy(pkt, pkt_pool);
		if (unlikely(clone_pkt == ODP_PACKET_INVALID))
			return EM_EVENT_UNDEF;
	}

	odp_packet_user_flag_set(clone_pkt, USER_FLAG_SET);

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

	clone_hdr->flags.all = 0;
	clone_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	/* other fields: use parent's values as is */

	return clone_event;
}

void
output_queue_track(queue_elem_t *const output_q_elem)
{
	output_queue_track_t *const track =
		&em_locm.output_queue_track;
	const uint32_t qidx = output_q_elem->output.idx;

	if (track->used_queues[qidx] == NULL) {
		track->used_queues[qidx] = output_q_elem;
		track->idx[track->idx_cnt++] = qidx;
	}
}

void
output_queue_drain(const queue_elem_t *output_q_elem)
{
	const em_queue_t output_queue = (em_queue_t)(uintptr_t)output_q_elem->queue;
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
		/* odp_deq_events[] == output_ev_tbl[] */
		if (esv_ena) {
			event_hdr_t *ev_hdrs[output_num];

			/* Restore hdls from ev_hdrs, odp-ev conv lost evgen */
			event_to_hdr_multi(output_ev_tbl, ev_hdrs, output_num);
			for (unsigned int i = 0; i < output_num; i++)
				output_ev_tbl[i] = ev_hdrs[i]->event;
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

uint32_t event_vector_tbl(em_event_t vector_event,
			  em_event_t **event_tbl /*out*/)
{
	odp_event_t odp_event = event_em2odp(vector_event);
	odp_packet_vector_t pkt_vec = odp_packet_vector_from_event(odp_event);
	odp_packet_t *pkt_tbl = NULL;
	const int pkts = odp_packet_vector_tbl(pkt_vec, &pkt_tbl/*out*/);

	*event_tbl = (em_event_t *)pkt_tbl; /* Careful! Points to same table */

	if (!pkts)
		return 0;

	event_hdr_t *ev_hdr_tbl[pkts];

	/*
	 * Init the event-table as needed, might contain EM events or
	 * ODP packets depending on source.
	 */
	if (esv_enabled()) {
		odp_packet_t odp_pkttbl[pkts];

		/*
		 * Drop ESV generation from event handles by converting to
		 * odp-packets, then init as needed as EM events.
		 */
		events_em2pkt(*event_tbl/*in*/, odp_pkttbl/*out*/, pkts);

		event_init_pkt_multi(odp_pkttbl /*in*/, *event_tbl /*in,out*/,
				     ev_hdr_tbl /*out*/, pkts, false);
	} else {
		event_init_pkt_multi(pkt_tbl /*in*/, *event_tbl /*in,out*/,
				     ev_hdr_tbl /*out*/, pkts, false);
	}

	return pkts;
}

em_status_t event_vector_max_size(em_event_t vector_event, uint32_t *max_size /*out*/,
				  em_escope_t escope)
{
	odp_event_t odp_event = event_em2odp(vector_event);
	odp_packet_vector_t pktvec = odp_packet_vector_from_event(odp_event);
	odp_pool_t odp_pool = odp_packet_vector_pool(pktvec);
	pool_subpool_t pool_subpool = pool_subpool_odp2em(odp_pool);
	em_pool_t pool = (em_pool_t)(uintptr_t)pool_subpool.pool;
	int subpool = pool_subpool.subpool;

	if (unlikely(pool == EM_POOL_UNDEF)) {
		/*
		 * Don't report an error if 'pool == EM_POOL_UNDEF' since that
		 * might happen if the vector is input from pktio that is using
		 * external (to EM) odp vector pools.
		 */
		*max_size = 0;
		return EM_OK; /* EM does not have the max_size info */
	}

	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	if (unlikely(!pool_elem ||
		     (EM_CHECK_LEVEL > 2 && !pool_allocated(pool_elem)))) {
		*max_size = 0;
		return INTERNAL_ERROR(EM_ERR_BAD_STATE, escope,
				      "Invalid pool:%" PRI_POOL "", pool);
	}

	if (unlikely(subpool >= pool_elem->num_subpools)) {
		/* not found */
		*max_size = 0;
		return INTERNAL_ERROR(EM_ERR_NOT_FOUND, escope,
				      "Subpool not found, pool:%" PRI_POOL "", pool);
	}

	/* subpool index found, store corresponding size */
	*max_size = pool_elem->size[subpool];

	return EM_OK;
}
