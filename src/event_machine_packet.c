/*
 *   Copyright (c) 2024-2026, Nokia Solutions and Networks
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

#include "em_error.h"
#include "em_event.h"
#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_mem.h"
#include "em_pool.h"
#include "em_pool_inline.h"
#include "em_pool_types.h"

static inline bool is_odp_packet(odp_event_t odp_event)
{
	if (unlikely(odp_event == ODP_EVENT_INVALID ||
		     odp_event_type(odp_event) != ODP_EVENT_PACKET))
		return false;

	return true;
}

void *em_packet_pointer(em_event_t pktev)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_POINTER,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

	return odp_packet_data(odp_pkt);
}

uint32_t em_packet_size(em_event_t pktev)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_SIZE,
			       "Invalid packet event");
		return 0;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	uint32_t seg_len = odp_packet_seg_len(odp_pkt);

	return seg_len;
}

void *em_packet_pointer_and_size(em_event_t pktev, uint32_t *size /*out*/)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_POINTER_AND_SIZE,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

	if (!size) {
		/* User not interested in 'size',
		 * fall back to em_packet_pointer() functionality
		 */
		return odp_packet_data(odp_pkt);
	}

	return odp_packet_data_seg_len(odp_pkt, size /*out*/);
}

void *em_packet_resize(em_event_t pktev, uint32_t size)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_RESIZE,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	uint32_t seg_len = odp_packet_seg_len(odp_pkt);
	const void *tail;

	if (EM_CHECK_LEVEL >= 3) {
		uint32_t tailroom = odp_packet_tailroom(odp_pkt);

		if (unlikely(size == 0 || size > seg_len + tailroom)) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_RESIZE,
				       "Invalid packet resize:%u > max:%u",
				       size, seg_len + tailroom);
			return NULL;
		}
	}

	if (size > seg_len)
		tail = odp_packet_push_tail(odp_pkt, size - seg_len);
	else /* size <= seg_len */
		tail = odp_packet_pull_tail(odp_pkt, seg_len - size);

	if (unlikely(!tail))
		return NULL;

	return odp_packet_data(odp_pkt);
}

uint32_t em_packet_headroom(em_event_t pktev)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_HEADROOM,
			       "Invalid packet event");
		return 0;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

	return odp_packet_headroom(odp_pkt);
}

uint32_t em_packet_tailroom(em_event_t pktev)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_TAILROOM,
			       "Invalid packet event");
		return 0;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);

	return odp_packet_tailroom(odp_pkt);
}

void *em_packet_push_head(em_event_t pktev, uint32_t len)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PUSH_HEAD,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	void *ptr = odp_packet_push_head(odp_pkt, len);

	if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PUSH_HEAD,
			       "packet push head failed, check len=%u", len);
		return NULL;
	}

	return ptr;
}

void *em_packet_pull_head(em_event_t pktev, uint32_t len)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PULL_HEAD,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	void *ptr = odp_packet_pull_head(odp_pkt, len);

	if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PULL_HEAD,
			       "packet pull head failed, check len=%u", len);
		return NULL;
	}

	return ptr;
}

void *em_packet_push_tail(em_event_t pktev, uint32_t len)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PUSH_TAIL,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	void *ptr = odp_packet_push_tail(odp_pkt, len);

	if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PUSH_TAIL,
			       "packet push tail failed, check len=%u", len);
		return NULL;
	}

	return ptr;
}

void *em_packet_pull_tail(em_event_t pktev, uint32_t len)
{
	odp_event_t odp_event = event_em2odp(pktev);

	if (EM_CHECK_LEVEL >= 3 &&
	    unlikely(!is_odp_packet(odp_event))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PULL_TAIL,
			       "Invalid packet event");
		return NULL;
	}

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	void *ptr = odp_packet_pull_tail(odp_pkt, len);

	if (EM_CHECK_LEVEL >= 3 && unlikely(!ptr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_PULL_TAIL,
			       "packet pull tail failed, check len=%u", len);
		return NULL;
	}

	return ptr;
}

em_status_t em_packet_reset(em_event_t pktev, uint32_t size)
{
	odp_event_t odp_event = event_em2odp(pktev);

	RETURN_ERROR_IF(EM_CHECK_LEVEL >= 3 && !is_odp_packet(odp_event),
			EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_RESET,
			"Invalid packet event");

	odp_packet_t odp_pkt = odp_packet_from_event(odp_event);
	odp_pool_t odp_pool = odp_packet_pool(odp_pkt);
	em_pool_t pool = pool_odp2em(odp_pool);
	const mpool_elem_t *pool_elem = pool_elem_get(pool);

	RETURN_ERROR_IF(!pool_elem || (EM_CHECK_LEVEL >= 3 && !pool_allocated(pool_elem)),
			EM_ERR_BAD_ID, EM_ESCOPE_PACKET_RESET,
			"Invalid EM pool:%" PRI_POOL "", pool);

	const uint32_t push_head_len = pool_elem->align_offset;
	uint32_t pull_tail_len;
	uint32_t reset_len;

	if (size > push_head_len) {
		reset_len = size - push_head_len;
		pull_tail_len = 0;
	} else {
		reset_len = 1; /* min allowed */
		pull_tail_len = push_head_len + 1 - size;
	}

	int ret = odp_packet_reset(odp_pkt, reset_len);

	RETURN_ERROR_IF(ret, EM_ERR_LIB_FAILED, EM_ESCOPE_PACKET_RESET,
			"odp-packet reset failed:%d", ret);

	/* Adjust event payload start-address based on alignment config */
	if (push_head_len) {
		const void *ptr = odp_packet_push_head(odp_pkt, push_head_len);

		RETURN_ERROR_IF(EM_CHECK_LEVEL >= 3 && !ptr,
				EM_ERR_LIB_FAILED, EM_ESCOPE_PACKET_RESET,
				"odp_packet_push_head() failed");
	}
	if (pull_tail_len) {
		const void *ptr = odp_packet_pull_tail(odp_pkt, pull_tail_len);

		RETURN_ERROR_IF(EM_CHECK_LEVEL >= 3 && !ptr,
				EM_ERR_LIB_FAILED, EM_ESCOPE_PACKET_RESET,
				"odp_packet_pull_tail() failed");
	}

	/*
	 * Set the pkt user ptr to be able to recognize pkt-events that
	 * EM has created vs pkts from pkt-input that needs their
	 * ev-hdrs to be initialized.
	 */
	odp_packet_user_flag_set(odp_pkt, USER_FLAG_SET);

	event_hdr_t *const ev_hdr = odp_packet_user_area(odp_pkt);
	ev_hdr_user_area_t uarea_save = ev_hdr->user_area;

	ev_hdr->event_size = size; /* store new 'original' size */
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	ev_hdr->user_area.all = 0;
	ev_hdr->user_area.size = uarea_save.size;
	ev_hdr->user_area.isinit = 1;

	return EM_OK;
}

/**
 * Helper function for em_packet_concat() checks whether the event pointer has
 * changed, updates the new handle with the correct evgen, and modifies the
 * event header accordingly.
 */
static inline em_event_t
check_update_event(evhdl_t original_hdl, evhdl_t updated_hdl, event_hdr_t *updated_hdr)
{
	if (original_hdl.evptr != updated_hdl.evptr) {
		/* update event handle and ev_hdr, ev_hdr is copied from original first_pkt */
		updated_hdl.evgen = original_hdl.evgen;
		updated_hdr->event = updated_hdl.event;
		return	updated_hdl.event;
	}
	return original_hdl.event;
}

/**
 * Helper function for em_packet_concat(), does a packet copy if the second_pkt
 * has references.
 *
 * Only needed with ODP API < 1.50, where odp_packet_concat() does not support a
 * 'src' packet that has references. ODP API >= 1.50 handles this natively.
 */
static inline em_status_t
concat_packet_with_ref(em_event_t *first_pkt, em_event_t second_pkt)
{
	odp_event_t odp_event_first = event_em2odp(*first_pkt);
	odp_event_t odp_event_second = event_em2odp(second_pkt);

	if (unlikely(EM_CHECK_LEVEL >= 3 &&
		     (!is_odp_packet(odp_event_first) ||
		      !is_odp_packet(odp_event_second)))) {
		(void)INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_CONCAT,
				     "first_pkt and/or second_pkt not a packet");
		return -1;
	}

	const bool esv_ena = esv_enabled();
	event_hdr_t *ev_hdr_first;

	/* Save event hdl for checking if it has changed */
	evhdl_t first_ev = {.event = *first_pkt};

	odp_packet_t odp_pkt_first = odp_packet_from_event(odp_event_first);
	odp_packet_t odp_pkt_second = odp_packet_from_event(odp_event_second);
	uint32_t first_len = odp_packet_len(odp_pkt_first);
	uint32_t second_len = odp_packet_len(odp_pkt_second);

	/* Mark first_pkt as free, since ODP might free it */
	if (esv_ena) {
		ev_hdr_first = odp_packet_user_area(odp_pkt_first);
		evstate_free(*first_pkt, ev_hdr_first, EVSTATE__MARK_FREE);
	}
	/* Increase packet data length to fit the second_pkt data */
	int ret = odp_packet_extend_tail(&odp_pkt_first, second_len, NULL, NULL);

	if (unlikely(ret < 0)) {
		if (esv_ena)
			evstate_unmark_free(*first_pkt, ev_hdr_first, EVSTATE__UNMARK_FREE);

		return -1;
	}
	/* Copy second_pkt data to the first_pkt */
	(void)odp_packet_copy_from_pkt(odp_pkt_first, first_len,
				       odp_pkt_second, 0, second_len);
	evhdl_t ev_hdl_updated = {.event = event_odp2em(odp_packet_to_event(odp_pkt_first))};

	ev_hdr_first = odp_packet_user_area(odp_pkt_first);

	/* Check if event handle has changed, and update it */
	*first_pkt = check_update_event(first_ev, ev_hdl_updated, ev_hdr_first);

	/* Unmark free resulted first_pkt */
	if (esv_ena)
		evstate_unmark_free(*first_pkt, ev_hdr_first, EVSTATE__UNMARK_FREE);

	em_free(second_pkt);

	/* Packet data was copied */
	return 1;
}

int em_packet_concat(em_event_t *first_pkt /*in,out*/, em_event_t second_pkt)
{
	if (unlikely(EM_CHECK_LEVEL >= 3 && event_has_ref(*first_pkt))) {
		(void)INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_CONCAT,
				     "first_pkt has a reference");
		return -1;
	}

	if (ODP_VERSION_API_NUM(1, 50, 0) > ODP_VERSION_API &&
	    unlikely(event_has_ref(second_pkt))) {
		/*
		 * ODP API < 1.50 does not support packet concatenating if second_pkt has
		 * references, so fall back to a manual data copy. ODP API >= 1.50 handles
		 * a referenced 'src' packet natively in odp_packet_concat() below.
		 */
		return concat_packet_with_ref(first_pkt, second_pkt);
	}

	odp_event_t odp_event_first = event_em2odp(*first_pkt);
	odp_event_t odp_event_second = event_em2odp(second_pkt);

	if (unlikely(EM_CHECK_LEVEL >= 3 &&
		     (!is_odp_packet(odp_event_first) ||
		      !is_odp_packet(odp_event_second)))) {
		(void)INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_PACKET_CONCAT,
				     "first_pkt and/or second_pkt not a packet");
		return -1;
	}

	const bool esv_ena = esv_enabled();
	event_hdr_t *ev_hdr_first;
	event_hdr_t *ev_hdr_second;

	/* Save event hdl for later checking if it has changed */
	evhdl_t ev_hdl_original = {.event = *first_pkt};

	odp_packet_t odp_pkt_first = odp_packet_from_event(odp_event_first);
	odp_packet_t odp_pkt_second = odp_packet_from_event(odp_event_second);

	/* Mark pkts as free, since ODP might free it */
	if (esv_ena) {
		ev_hdr_first = odp_packet_user_area(odp_pkt_first);
		ev_hdr_second = odp_packet_user_area(odp_pkt_second);

		evstate_free(*first_pkt, ev_hdr_first, EVSTATE__MARK_FREE);
		evstate_free(second_pkt, ev_hdr_second, EVSTATE__MARK_FREE);
	}

	int ret = odp_packet_concat(&odp_pkt_first, odp_pkt_second);

	if (unlikely(ret < 0)) {
		/* If odp_packet_concat() fails, pkt handles will not change */
		if (esv_ena) {
			evstate_unmark_free(*first_pkt, ev_hdr_first, EVSTATE__UNMARK_FREE);
			evstate_unmark_free(second_pkt, ev_hdr_second, EVSTATE__UNMARK_FREE);
		}
		return -1;
	}
	ev_hdr_first = odp_packet_user_area(odp_pkt_first);
	/* Check if the event handle has changed, and update it and event hdr */
	evhdl_t ev_hdl_updated = {.event = event_odp2em(odp_packet_to_event(odp_pkt_first))};

	*first_pkt = check_update_event(ev_hdl_original, ev_hdl_updated, ev_hdr_first);
	/* Unmark free resulted first_pkt */
	if (esv_ena)
		evstate_unmark_free(*first_pkt, ev_hdr_first, EVSTATE__UNMARK_FREE);

	if (ret == 0)
		return 0;

	return 1;
}
