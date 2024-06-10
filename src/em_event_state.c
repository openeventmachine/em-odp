/*
 *   Copyright (c) 2020-2022, Nokia Solutions and Networks
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

static int read_config_file(void);

/**
 * Initial counter values set during an alloc-operation: ref=1, send=0
 * (em_alloc/_multi(), em_event_clone())
 */
static const evstate_cnt_t init_cnt_alloc = {.evgen = EVGEN_INIT,
					     .rsvd = 0,
					     .ref_cnt = REF_CNT_INIT - 1,
					     .send_cnt = 0 + SEND_CNT_INIT};
/**
 * Initial counter values for external events entering into EM
 * (event not allocated by EM): ref=1, send=1
 */
static const evstate_cnt_t init_cnt_extev = {.evgen = EVGEN_INIT,
					     .rsvd = 0,
					     .ref_cnt = REF_CNT_INIT - 1,
					     .send_cnt = 1 + SEND_CNT_INIT};

/**
 * Information about an event-state update location
 */
typedef struct {
	const char *str;
	em_escope_t escope;
} evstate_info_t;

/**
 * Constant table containing event-state update location information.
 * Only accessed when an erroneous event state has been detected and is being
 * reported to the error handler.
 */
static const evstate_info_t evstate_info_tbl[] = {
	[EVSTATE__UNDEF] = {.str = "undefined",
			    .escope = (EM_ESCOPE_INTERNAL_MASK | 0)},
	[EVSTATE__PREALLOC] = {.str = "pool-create(prealloc-events)",
			       .escope = EM_ESCOPE_POOL_CREATE},
	[EVSTATE__ALLOC] = {.str = "em_alloc()",
			    .escope = EM_ESCOPE_ALLOC},
	[EVSTATE__ALLOC_MULTI] = {.str = "em_alloc_multi()",
				  .escope = EM_ESCOPE_ALLOC_MULTI},
	[EVSTATE__EVENT_CLONE] = {.str = "em_event_clone()",
				  .escope = EM_ESCOPE_EVENT_CLONE},
	[EVSTATE__EVENT_REF] = {.str = "em_event_ref()",
				.escope = EM_ESCOPE_EVENT_REF},
	[EVSTATE__FREE] = {.str = "em_free()",
			   .escope = EM_ESCOPE_FREE},
	[EVSTATE__FREE_MULTI] = {.str = "em_free_multi()",
				 .escope = EM_ESCOPE_FREE_MULTI},
	[EVSTATE__EVENT_VECTOR_FREE] = {.str = "em_event_vector_free()",
					.escope = EM_ESCOPE_EVENT_VECTOR_FREE},
	[EVSTATE__INIT] = {.str = "init-event",
			   .escope = EM_ESCOPE_ODP_EXT},
	[EVSTATE__INIT_MULTI] = {.str = "init-events",
				 .escope = EM_ESCOPE_ODP_EXT},
	[EVSTATE__INIT_EXTEV] = {.str = "dispatch(init-ext-event)",
				 .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__INIT_EXTEV_MULTI] = {.str = "dispatch(init-ext-events)",
				       .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__UPDATE_EXTEV] = {.str = "dispatch(update-ext-event)",
				   .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__SEND] = {.str = "em_send()",
			   .escope = EM_ESCOPE_SEND},
	[EVSTATE__SEND__FAIL] = {.str = "em_send(fail)",
				 .escope = EM_ESCOPE_SEND},
	[EVSTATE__SEND_EGRP] = {.str = "em_send_group()",
				.escope = EM_ESCOPE_SEND_GROUP},
	[EVSTATE__SEND_EGRP__FAIL] = {.str = "em_send_group(fail)",
				      .escope = EM_ESCOPE_SEND_GROUP},
	[EVSTATE__SEND_MULTI] = {.str = "em_send_multi()",
				 .escope = EM_ESCOPE_SEND_MULTI},
	[EVSTATE__SEND_MULTI__FAIL] = {.str = "em_send_multi(fail)",
				       .escope = EM_ESCOPE_SEND_MULTI},
	[EVSTATE__SEND_EGRP_MULTI] = {.str = "em_send_group_multi()",
				      .escope = EM_ESCOPE_SEND_GROUP_MULTI},
	[EVSTATE__SEND_EGRP_MULTI__FAIL] = {.str = "em_send_group_multi(fail)",
					    .escope = EM_ESCOPE_SEND_GROUP_MULTI},
	[EVSTATE__EO_START_SEND_BUFFERED] = {.str = "eo-start:send-buffered-events()",
					     .escope = EM_ESCOPE_SEND_MULTI},
	[EVSTATE__MARK_SEND] = {.str = "em_event_mark_send()",
				.escope = EM_ESCOPE_EVENT_MARK_SEND},
	[EVSTATE__UNMARK_SEND] = {.str = "em_event_unmark_send()",
				  .escope = EM_ESCOPE_EVENT_UNMARK_SEND},
	[EVSTATE__MARK_FREE] = {.str = "em_event_mark_free()",
				.escope = EM_ESCOPE_EVENT_MARK_FREE},
	[EVSTATE__UNMARK_FREE] = {.str = "em_event_unmark_free()",
				  .escope = EM_ESCOPE_EVENT_UNMARK_FREE},
	[EVSTATE__MARK_FREE_MULTI] = {.str = "em_event_mark_free_multi()",
				      .escope = EM_ESCOPE_EVENT_MARK_FREE_MULTI},
	[EVSTATE__UNMARK_FREE_MULTI] = {.str = "em_event_unmark_free_multi()",
					.escope = EM_ESCOPE_EVENT_UNMARK_FREE_MULTI},
	[EVSTATE__DISPATCH] = {.str = "em_dispatch(single-event)",
			       .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__DISPATCH_MULTI] = {.str = "em_dispatch(multiple-events)",
				     .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__DISPATCH_SCHED__FAIL] = {.str = "em_dispatch(drop sched-events)",
					   .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__DISPATCH_LOCAL__FAIL] = {.str = "em_dispatch(drop local-events)",
					   .escope = EM_ESCOPE_DISPATCH},
	[EVSTATE__DEQUEUE] = {.str = "em_queue_dequeue()",
			      .escope = EM_ESCOPE_QUEUE_DEQUEUE},
	[EVSTATE__DEQUEUE_MULTI] = {.str = "em_queue_dequeue_multi()",
				    .escope = EM_ESCOPE_QUEUE_DEQUEUE_MULTI},
	[EVSTATE__TMO_SET_ABS] = {.str = "em_tmo_set_abs()",
				  .escope = EM_ESCOPE_TMO_SET_ABS},
	[EVSTATE__TMO_SET_ABS__FAIL] = {.str = "em_tmo_set_abs(fail)",
					.escope = EM_ESCOPE_TMO_SET_ABS},
	[EVSTATE__TMO_SET_REL] = {.str = "em_tmo_set_rel()",
				  .escope = EM_ESCOPE_TMO_SET_REL},
	[EVSTATE__TMO_SET_REL__FAIL] = {.str = "em_tmo_set_rel(fail)",
					.escope = EM_ESCOPE_TMO_SET_REL},
	[EVSTATE__TMO_SET_PERIODIC] = {.str = "em_tmo_set_periodic()",
				       .escope = EM_ESCOPE_TMO_SET_PERIODIC},
	[EVSTATE__TMO_SET_PERIODIC__FAIL] = {.str = "em_tmo_set_periodic(fail)",
					     .escope = EM_ESCOPE_TMO_SET_PERIODIC},
	[EVSTATE__TMO_CANCEL] = {.str = "em_tmo_cancel()",
				 .escope = EM_ESCOPE_TMO_CANCEL},
	[EVSTATE__TMO_ACK] = {.str = "em_tmo_ack()",
			      .escope = EM_ESCOPE_TMO_ACK},
	[EVSTATE__TMO_ACK__NOSKIP] = {.str = "em_tmo_ack(noskip)",
				      .escope = EM_ESCOPE_TMO_ACK},
	[EVSTATE__TMO_ACK__FAIL] = {.str = "em_tmo_ack(fail)",
				    .escope = EM_ESCOPE_TMO_ACK},
	[EVSTATE__TMO_CREATE] = {.str = "em_tmo_create()",
				 .escope = EM_ESCOPE_TMO_CREATE},
	[EVSTATE__TMO_DELETE] = {.str = "em_tmo_delete()",
				 .escope = EM_ESCOPE_TMO_DELETE},
	[EVSTATE__AG_DELETE] = {.str = "em_atomic_group_delete(flush)",
				 .escope = EM_ESCOPE_ATOMIC_GROUP_DELETE},
	[EVSTATE__TERM_CORE__QUEUE_LOCAL] = {.str = "em_term_core(local-queue)",
					     .escope = EM_ESCOPE_TERM_CORE},
	[EVSTATE__TERM] = {.str = "em_term()",
			   .escope = EM_ESCOPE_TERM},
	/* Last: */
	[EVSTATE__LAST] = {.str = "last",
			   .escope = (EM_ESCOPE_INTERNAL_MASK | 0)}
};

static const char *const help_str_em2usr =
"OK: 'send < ref, both >=0'. Err otherwise";
static const char *const help_str_usr2em =
"OK: 'send <= ref, both >=0' AND 'hdl evgen == evgen'. Err otherwise";
static const char *const help_str_usr2em_ref =
"OK: 'send <= ref, both >=0'. Err otherwise";

static inline void
esv_update_state(ev_hdr_state_t *const evstate, const uint16_t api_op,
		 const void *const ev_ptr)
{
	const em_locm_t *const locm = &em_locm;
	const uint32_t *const pl_u32 = ev_ptr;
	const queue_elem_t *const q_elem = locm->current.q_elem;

	if (ev_ptr)
		evstate->payload_first = *pl_u32;

	if (!q_elem) {
		evstate->eo_idx = (int16_t)eo_hdl2idx(EM_EO_UNDEF); /* -1 is fine */
		evstate->queue_idx = (int16_t)queue_hdl2idx(EM_QUEUE_UNDEF); /* -1 is fine */
	} else {
		evstate->eo_idx = (int16_t)eo_hdl2idx((em_eo_t)(uintptr_t)q_elem->eo);
		evstate->queue_idx = (int16_t)queue_hdl2idx((em_queue_t)(uintptr_t)q_elem->queue);
	}
	evstate->api_op = (uint8_t)api_op; /* no trucation */
	evstate->core = locm->core_id;
}

static inline void
evhdr_update_state(event_hdr_t *const ev_hdr, const uint16_t api_op)
{
	if (!em_shm->opt.esv.store_state)
		return; /* don't store updated state */

	const void *ev_ptr = NULL;

	if (em_shm->opt.esv.store_first_u32)
		ev_ptr = event_pointer(ev_hdr->event);

	esv_update_state(&ev_hdr->state, api_op, ev_ptr);
}

/* "Normal" ESV Error format */
#define EVSTATE_ERROR_FMT \
"ESV: Event:%" PRI_EVENT " state error -- counts:\t"                      \
"send:%" PRIi16 " ref:%" PRIi16 " evgen:%" PRIu16 "(%" PRIu16 ")\n"       \
"     Help: %s\n"                                                         \
"  prev-state:%s core:%02u:\t"                                            \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"            \
"=> err-state:%s core:%02u:\t"                                            \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"            \
"   event:0x%016" PRIx64 ": ptr:0x%" PRIx64 ""

/* ESV Error format for references */
#define EVSTATE_REF_ERROR_FMT \
"ESV: RefEvent:%" PRI_EVENT " state error -- counts:\t"                   \
"send:%" PRIi16 " ref:%" PRIi16 " (evgen:%" PRIu16 " ignored for refs)\n" \
"     Help: %s\n"                                                         \
"  prev-state:n/a (not valid for event references)\n"                     \
"=> err-state:%s core:%02u:\t"                                            \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"            \
"   event:0x%016" PRIx64 ": ptr:0x%" PRIx64 ""

/* ESV Error format for em_event_unmark_send/free/_multi() */
#define EVSTATE_UNMARK_ERROR_FMT \
"ESV: Event:%" PRI_EVENT " state error - Invalid 'unmark'-API use\n"\
"  prev-state:%s core:%02u:\t"                                      \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"      \
"=> err-state:%s core:%02u:\t"                                      \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"

/* ESV Error format when esv.store_state = false */
#define EVSTATE__NO_PREV_STATE__ERROR_FMT \
"ESV: Event:%" PRI_EVENT " state error -- counts:\t"                      \
"send:%" PRIi16 " ref:%" PRIi16 " evgen:%" PRIu16 "(%" PRIu16 ")\n"       \
"     Help: %s\n"                                                         \
"  prev-state:n/a (disabled in conf)\n"                                   \
"=> err-state:%s core:%02u:\t"                                            \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"            \
"   event:0x%016" PRIx64 ": ptr:0x%" PRIx64 ""

/* ESV Error format for em_event_unmark_send/free/_multi() when esv.store_state = false */
#define EVSTATE__NO_PREV_STATE__UNMARK_ERROR_FMT \
"ESV: Event:%" PRI_EVENT " state error - Invalid 'unmark'-API use\n"\
"  prev-state:n/a (disabled in conf)\n"                             \
"=> err-state:%s core:%02u:\t"                                      \
"   EO:%" PRI_EO "-\"%s\" Q:%" PRI_QUEUE "-\"%s\" u32[0]:%s\n"

/**
 * ESV Error reporting
 */
static inline void
esv_error(const evstate_cnt_t cnt,
	  evhdl_t evhdl, const event_hdr_t *const ev_hdr,
	  const uint16_t api_op, bool is_unmark_error,
	  const char *const help_str)
{
	uint16_t prev_op = ev_hdr->state.api_op;
	ev_hdr_state_t prev_state = ev_hdr->state; /* store prev good state */
	ev_hdr_state_t err_state = {0}; /* store current invalid/error state */
	const em_event_t event = event_hdr_to_event(ev_hdr);
	const void *ev_ptr = NULL;

	if (unlikely(prev_op > EVSTATE__LAST))
		prev_op = EVSTATE__UNDEF;

	const evstate_info_t *err_info = &evstate_info_tbl[api_op];
	const evstate_info_t *prev_info = &evstate_info_tbl[prev_op];

	char curr_eoname[EM_EO_NAME_LEN] = "(noname)";
	char prev_eoname[EM_EO_NAME_LEN] = "(noname)";
	char curr_qname[EM_QUEUE_NAME_LEN] = "(noname)";
	char prev_qname[EM_QUEUE_NAME_LEN] = "(noname)";
	char curr_payload[sizeof("0x12345678 ")] = "(n/a)";
	char prev_payload[sizeof("0x12345678 ")] = "(n/a)";

	const eo_elem_t *eo_elem;
	const queue_elem_t *q_elem;

	/* Check event!=undef to avoid error in event_pointer() */
	if (likely(event != EM_EVENT_UNDEF))
		ev_ptr = event_pointer(event);
	/* Store the new _invalid_ event-state info into a separate struct */
	esv_update_state(&err_state, api_op, ev_ptr);

	/*
	 * Print the first 32bits of the event payload on failure,
	 * the option 'esv.store_payload_first_u32' affects storing during valid
	 * state transitions.
	 */
	if (ev_ptr) {
		snprintf(curr_payload, sizeof(curr_payload),
			 "0x%08" PRIx32 "", err_state.payload_first);
		curr_payload[sizeof(curr_payload) - 1] = '\0';
	}

	em_eo_t curr_eo = eo_idx2hdl(err_state.eo_idx);
	em_queue_t curr_queue = queue_idx2hdl(err_state.queue_idx);

	/* current EO-name: */
	eo_elem = eo_elem_get(curr_eo);
	if (eo_elem != NULL)
		eo_get_name(eo_elem, curr_eoname, sizeof(curr_eoname));
	/* current queue-name: */
	q_elem = queue_elem_get(curr_queue);
	if (q_elem != NULL)
		queue_get_name(q_elem, curr_qname, sizeof(curr_qname));

	const int16_t send_cnt = cnt.send_cnt - SEND_CNT_INIT;
	uint16_t evgen_cnt = cnt.evgen - EVGEN_INIT;
	const uint16_t evgen_hdl = evhdl.evgen - EVGEN_INIT;
	const int16_t ref_cnt = REF_CNT_INIT - cnt.ref_cnt;

	/* Read the previous event state only if it has been stored */
	if (em_shm->opt.esv.store_state) {
		/*
		 * Print the first 32 bits of the event payload for the previous
		 * valid state transition, if enabled in the EM config file:
		 * 'esv.store_payload_first_u32 = true', otherwise not stored.
		 */
		if (em_shm->opt.esv.store_first_u32) {
			snprintf(prev_payload, sizeof(prev_payload),
				 "0x%08" PRIx32 "", prev_state.payload_first);
			prev_payload[sizeof(prev_payload) - 1] = '\0';
		}

		em_eo_t prev_eo = eo_idx2hdl(prev_state.eo_idx);
		em_queue_t prev_queue = queue_idx2hdl(prev_state.queue_idx);

		/* previous EO-name: */
		eo_elem = eo_elem_get(prev_eo);
		if (eo_elem != NULL)
			eo_get_name(eo_elem, prev_eoname, sizeof(prev_eoname));
		/* previous queue-name: */
		q_elem = queue_elem_get(prev_queue);
		if (q_elem != NULL)
			queue_get_name(q_elem, prev_qname, sizeof(prev_qname));

		if (ev_hdr->flags.refs_used) {
			/* Reference ESV Error, prev state available */
			INTERNAL_ERROR(EM_FATAL(EM_ERR_EVENT_STATE),
				       err_info->escope, EVSTATE_REF_ERROR_FMT,
				       event, send_cnt, ref_cnt, evgen_cnt, help_str,
				       err_info->str, err_state.core,
				       curr_eo, curr_eoname, curr_queue, curr_qname,
				       curr_payload, evhdl.event, evhdl.evptr);
		} else if (!is_unmark_error) {
			/* "Normal" ESV Error, prev state available */
			INTERNAL_ERROR(EM_FATAL(EM_ERR_EVENT_STATE),
				       err_info->escope, EVSTATE_ERROR_FMT,
				       event, send_cnt, ref_cnt, evgen_hdl, evgen_cnt, help_str,
				       prev_info->str, prev_state.core, prev_eo, prev_eoname,
				       prev_queue, prev_qname, prev_payload,
				       err_info->str, err_state.core, curr_eo, curr_eoname,
				       curr_queue, curr_qname, curr_payload,
				       evhdl.event, evhdl.evptr);
		} else {
			/*
			 * ESV Error from em_event_unmark_send/free/_multi(),
			 * prev state available.
			 */
			INTERNAL_ERROR(EM_FATAL(EM_ERR_EVENT_STATE),
				       err_info->escope, EVSTATE_UNMARK_ERROR_FMT,
				       event,
				       prev_info->str, prev_state.core,
				       prev_eo, prev_eoname,
				       prev_queue, prev_qname, prev_payload,
				       err_info->str, err_state.core,
				       curr_eo, curr_eoname,
				       curr_queue, curr_qname, curr_payload);
		}
	} else { /* em_shm->opt.esv.store_state == false */
		/* No previous state stored by EM at runtime */
		if (!is_unmark_error) {
			/* "Normal" ESV Error, prev state not stored */
			INTERNAL_ERROR(EM_FATAL(EM_ERR_EVENT_STATE),
				       err_info->escope, EVSTATE__NO_PREV_STATE__ERROR_FMT,
				       event, send_cnt, ref_cnt, evgen_hdl, evgen_cnt, help_str,
				       err_info->str, err_state.core, curr_eo, curr_eoname,
				       curr_queue, curr_qname, curr_payload,
				       evhdl.event, evhdl.evptr);
		} else {
			/*
			 * ESV Error from em_event_unmark_send/free/_multi(),
			 * prev state not stored.
			 */
			INTERNAL_ERROR(EM_FATAL(EM_ERR_EVENT_STATE),
				       err_info->escope, EVSTATE__NO_PREV_STATE__UNMARK_ERROR_FMT,
				       event,
				       err_info->str, err_state.core, curr_eo, curr_eoname,
				       curr_queue, curr_qname, curr_payload);
		}
	}
}

static void
evstate_error(const evstate_cnt_t cnt, evhdl_t evhdl,
	      const event_hdr_t *const ev_hdr, const uint16_t api_op,
	      const char *const help_str)
{
	/* "Normal" ESV Error */
	esv_error(cnt, evhdl, ev_hdr, api_op, false, help_str);
}

/**
 * ESV Error reporting for invalid em_event_unmark...() API use
 */
static void
evstate_unmark_error(const event_hdr_t *const ev_hdr, const uint16_t api_op)
{
	evstate_cnt_t dont_care = {.u64 = 0};
	evhdl_t dont_care_hdl = {.event = EM_EVENT_UNDEF};

	/* ESV Error from em_event_unmark_send/free/_multi() */
	esv_error(dont_care, dont_care_hdl, ev_hdr, api_op, true, "n/a");
}

static inline em_event_t
esv_evinit(const em_event_t event, event_hdr_t *const ev_hdr,
	   const evstate_cnt_t init_cnt, const uint16_t api_op)
{
	evhdl_t evhdl = {.event = event};

	evhdl.evgen = EVGEN_INIT;
	ev_hdr->event = evhdl.event;

	/* Set initial counters (atomic) */
	__atomic_store_n(&ev_hdr->state_cnt.u64, init_cnt.u64,
			 __ATOMIC_RELAXED);
	/* Set initial state information (non-atomic) */
	evhdr_update_state(ev_hdr, api_op);

	return evhdl.event;
}

static inline void
esv_evinit_multi(em_event_t ev_tbl[/*in/out*/],
		 event_hdr_t *const ev_hdr_tbl[], const int num,
		 const evstate_cnt_t init_cnt, const uint16_t api_op)
{
	evhdl_t *const evhdl_tbl = (evhdl_t *)ev_tbl;

	for (int i = 0; i < num; i++) {
		evhdl_tbl[i].evgen = EVGEN_INIT;
		ev_hdr_tbl[i]->event = evhdl_tbl[i].event;

		/* Set initial counters for ext-events (atomic) */
		__atomic_store_n(&ev_hdr_tbl[i]->state_cnt.u64,
				 init_cnt.u64, __ATOMIC_RELAXED);
		/* Set initial state information (non-atomic) */
		evhdr_update_state(ev_hdr_tbl[i], api_op);
	}
}

static inline em_event_t
esv_evinit_ext(const em_event_t event, event_hdr_t *const ev_hdr,
	       const uint16_t api_op)
{
	/*
	 * Combination of:
	 * event = esv_evinit(..., init_cnt_extev, ...)
	 * return evstate_em2usr(event, ...);
	 */
	evhdl_t evhdl = {.event = event};
	const evstate_cnt_t init = init_cnt_extev;
	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};
	const evstate_cnt_t cnt = {.u64 = init.u64 - sub.u64};

	evhdl.evgen = cnt.evgen;
	ev_hdr->event = evhdl.event;

	/* Set initial counters (atomic) */
	__atomic_store_n(&ev_hdr->state_cnt.u64, cnt.u64,
			 __ATOMIC_RELAXED);

	/* Set initial state information (non-atomic) */
	evhdr_update_state(ev_hdr, api_op);

	return evhdl.event;
}

static inline em_event_t
esv_em2usr(const em_event_t event, event_hdr_t *const ev_hdr,
	   const evstate_cnt_t cnt, const uint16_t api_op, const bool is_revert)
{
	const bool refs_used = ev_hdr->flags.refs_used;
	evhdl_t evhdl = {.event = event};
	evstate_cnt_t new_cnt;

	/* Update state-count and return value of all counters (atomic) */
	if (unlikely(is_revert)) {
		/* Revert previous em2usr counter update on failed operation */
		new_cnt.u64 = __atomic_add_fetch(&ev_hdr->state_cnt.u64,
						 cnt.u64, __ATOMIC_RELAXED);
	} else {
		/* Normal em2usr counter update */
		new_cnt.u64 = __atomic_sub_fetch(&ev_hdr->state_cnt.u64,
						 cnt.u64, __ATOMIC_RELAXED);
	}

	if (!refs_used) {
		evhdl.evgen = new_cnt.evgen;
		ev_hdr->event = evhdl.event;
	}

	const int16_t ref_cnt = REF_CNT_INIT - new_cnt.ref_cnt;
	const int16_t send_cnt = new_cnt.send_cnt - SEND_CNT_INIT;

	/*
	 * Check state count:
	 * OK: send_cnt < ref_cnt and both >=0. Error otherwise.
	 */
	if (unlikely(send_cnt >= ref_cnt || send_cnt < 0)) {
		/* report fatal event-state error, never return */
		evstate_error(new_cnt, evhdl, ev_hdr, api_op, help_str_em2usr);
		/* never reached */
	}

	/*
	 * Valid state transition, update state (non-atomic)
	 */
	if (!refs_used)
		evhdr_update_state(ev_hdr, api_op);

	return evhdl.event;
}

static inline void
esv_em2usr_multi(em_event_t ev_tbl[/*in/out*/],
		 event_hdr_t *const ev_hdr_tbl[], const int num,
		 const evstate_cnt_t cnt, const uint16_t api_op,
		 const bool is_revert)
{
	evhdl_t *const evhdl_tbl = (evhdl_t *)ev_tbl;
	evstate_cnt_t new_cnt;

	for (int i = 0; i < num; i++) {
		const bool refs_used = ev_hdr_tbl[i]->flags.refs_used;

		/* Update state-count and return value of all counters (atomic) */
		if (unlikely(is_revert)) {
			/* Revert em2usr counter update on failed operation */
			new_cnt.u64 =
			__atomic_add_fetch(&ev_hdr_tbl[i]->state_cnt.u64,
					   cnt.u64, __ATOMIC_RELAXED);
		} else {
			/* Normal em2usr counter update */
			new_cnt.u64 =
			__atomic_sub_fetch(&ev_hdr_tbl[i]->state_cnt.u64,
					   cnt.u64, __ATOMIC_RELAXED);
		}

		if (!refs_used) {
			evhdl_tbl[i].evgen = new_cnt.evgen;
			ev_hdr_tbl[i]->event = evhdl_tbl[i].event;
		}

		const int16_t ref_cnt = REF_CNT_INIT - new_cnt.ref_cnt;
		const int16_t send_cnt = new_cnt.send_cnt - SEND_CNT_INIT;

		/*
		 * Check state count:
		 * OK: send_cnt < ref_cnt and both >=0. Error otherwise.
		 */
		if (unlikely(send_cnt >= ref_cnt || send_cnt < 0)) {
			/* report fatal event-state error, never return */
			evstate_error(new_cnt, evhdl_tbl[i], ev_hdr_tbl[i],
				      api_op, help_str_em2usr);
			/* never reached */
		}

		/*
		 * Valid state transition, update state (non-atomic)
		 */
		if (!refs_used)
			evhdr_update_state(ev_hdr_tbl[i], api_op);
	}
}

static inline void
esv_usr2em(const em_event_t event, event_hdr_t *const ev_hdr,
	   const evstate_cnt_t cnt, const uint16_t api_op, const bool is_revert)
{
	const bool refs_used = ev_hdr->flags.refs_used;
	evhdl_t evhdl = {.event = event};
	evstate_cnt_t new_cnt;

	/* Update state-count and return value of all counters (atomic) */
	if (unlikely(is_revert)) {
		/* Revert previous usr2em counter update on failed operation */
		new_cnt.u64 = __atomic_sub_fetch(&ev_hdr->state_cnt.u64,
						 cnt.u64, __ATOMIC_RELAXED);

		if (unlikely(new_cnt.evgen == EVGEN_INIT - 1)) {
			/* Avoid .evgen counter wrap */
			const evstate_cnt_t add = {.evgen = EVGEN_MAX - EVGEN_INIT,
						   .rsvd = 0, .ref_cnt = 0, .send_cnt = 0};
			new_cnt.u64 = __atomic_add_fetch(&ev_hdr->state_cnt.u64,
							 add.u64, __ATOMIC_RELAXED);
		}
	} else {
		/* Normal usr2em counter update */
		new_cnt.u64 = __atomic_add_fetch(&ev_hdr->state_cnt.u64,
						 cnt.u64, __ATOMIC_RELAXED);

		if (unlikely(new_cnt.evgen == EVGEN_MAX)) {
			/* Avoid .evgen counter wrap */
			const evstate_cnt_t sub = {.evgen = EVGEN_MAX - EVGEN_INIT,
						   .rsvd = 0, .ref_cnt = 0, .send_cnt = 0};
			__atomic_fetch_sub(&ev_hdr->state_cnt.u64, sub.u64,
					   __ATOMIC_RELAXED);
		}
		/* cmp new_cnt.evgen vs evhdl.evgen of previous gen, thus -1 */
		new_cnt.evgen -= 1;
	}

	const int16_t ref_cnt = REF_CNT_INIT - new_cnt.ref_cnt;
	const int16_t send_cnt = new_cnt.send_cnt - SEND_CNT_INIT;

	/*
	 * Check state count:
	 * OK: send_cnt <= ref_cnt and both >=0.
	 *   AND
	 * OK: event handle evgen == evgen count (not checked for references)
	 * Error otherwise.
	 *
	 * Check evgen only for events that never had references.
	 * Reference usage mixes up the evgen since the same event can be
	 * sent and freed multiple times.
	 */
	if (unlikely((send_cnt > ref_cnt || send_cnt < 0) ||
		     (!refs_used && evhdl.evgen != new_cnt.evgen))) {
		const char *const help_str = refs_used ? help_str_usr2em_ref : help_str_usr2em;

		/* report fatal event-state error, never return */
		evstate_error(new_cnt, evhdl, ev_hdr, api_op, help_str);
		/* never reached */
	}

	/*
	 * Valid state transition, update state (non-atomic)
	 */
	if (!refs_used)
		evhdr_update_state(ev_hdr, api_op);
}

static inline void
esv_usr2em_multi(const em_event_t ev_tbl[],
		 event_hdr_t *const ev_hdr_tbl[], const int num,
		 const evstate_cnt_t cnt, const uint16_t api_op,
		 const bool is_revert)
{
	const evhdl_t *const evhdl_tbl = (const evhdl_t *)ev_tbl;
	evstate_cnt_t new_cnt;

	for (int i = 0; i < num; i++) {
		const bool refs_used = ev_hdr_tbl[i]->flags.refs_used;

		/* Update state-count and return value of all counters (atomic) */
		if (unlikely(is_revert)) {
			/* Revert usr2em counter update on failed operation */
			new_cnt.u64 =
			__atomic_sub_fetch(&ev_hdr_tbl[i]->state_cnt.u64,
					   cnt.u64, __ATOMIC_RELAXED);

			if (unlikely(new_cnt.evgen == EVGEN_INIT - 1)) {
				/* Avoid .evgen counter wrap */
				const evstate_cnt_t add = {.evgen = EVGEN_MAX - EVGEN_INIT,
							   .rsvd = 0, .ref_cnt = 0, .send_cnt = 0};
				new_cnt.u64 =
				__atomic_add_fetch(&ev_hdr_tbl[i]->state_cnt.u64,
						   add.u64, __ATOMIC_RELAXED);
			}
		} else {
			/* Normal usr2em counter update */
			new_cnt.u64 =
			__atomic_add_fetch(&ev_hdr_tbl[i]->state_cnt.u64,
					   cnt.u64, __ATOMIC_RELAXED);

			if (unlikely(new_cnt.evgen == EVGEN_MAX)) {
				/* Avoid .evgen counter wrap */
				const evstate_cnt_t sub = {.evgen = EVGEN_MAX - EVGEN_INIT,
							   .rsvd = 0, .ref_cnt = 0, .send_cnt = 0};
				__atomic_fetch_sub(&ev_hdr_tbl[i]->state_cnt.u64, sub.u64,
						   __ATOMIC_RELAXED);
			}

			new_cnt.evgen -= 1;
		}

		const int16_t ref_cnt = REF_CNT_INIT - new_cnt.ref_cnt;
		const int16_t send_cnt = new_cnt.send_cnt - SEND_CNT_INIT;

		/*
		 * Check state count:
		 * OK: send_cnt <= ref_cnt and both >=0.
		 *   AND
		 * OK: event handle evgen == evgen count (not checked for references)
		 * Error otherwise.
		 *
		 * Check evgen only for events that never had references.
		 * Reference usage mixes up the evgen since the same event can be
		 * sent and freed multiple times.
		 */
		if (unlikely((send_cnt > ref_cnt || send_cnt < 0) ||
			     (!refs_used && evhdl_tbl[i].evgen != new_cnt.evgen))) {
			/* report fatal event-state error, never return */
			evstate_error(new_cnt, evhdl_tbl[i], ev_hdr_tbl[i],
				      api_op, help_str_usr2em);
			/* never reached */
		}

		/*
		 * Valid state transition, update state (non-atomic)
		 */
		if (!refs_used)
			evhdr_update_state(ev_hdr_tbl[i], api_op);
	}
}

em_event_t evstate_prealloc(const em_event_t event, event_hdr_t *const ev_hdr)
{
	return esv_evinit(event, ev_hdr, init_cnt_alloc, EVSTATE__PREALLOC);
}

em_event_t evstate_alloc(const em_event_t event, event_hdr_t *const ev_hdr,
			 const uint16_t api_op)
{
	if (!em_shm->opt.esv.prealloc_pools || ev_hdr->flags.refs_used)
		return esv_evinit(event, ev_hdr, init_cnt_alloc, api_op);

	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	return esv_em2usr(event, ev_hdr, sub, api_op, false);
}

em_event_t evstate_alloc_tmo(const em_event_t event, event_hdr_t *const ev_hdr)
{
	return esv_evinit(event, ev_hdr, init_cnt_alloc, EVSTATE__TMO_CREATE);
}

void evstate_alloc_multi(em_event_t ev_tbl[/*in/out*/],
			 event_hdr_t *const ev_hdr_tbl[], const int num)
{
	if (!em_shm->opt.esv.prealloc_pools) {
		esv_evinit_multi(ev_tbl/*in/out*/, ev_hdr_tbl, num,
				 init_cnt_alloc, EVSTATE__ALLOC_MULTI);
		return;
	}

	/* em_shm->opt.esv.prealloc_pools: */
	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	for (int i = 0; i < num; i++) {
		if (ev_hdr_tbl[i]->flags.refs_used) {
			ev_tbl[i] = esv_evinit(ev_tbl[i], ev_hdr_tbl[i],
					       init_cnt_alloc,
					       EVSTATE__ALLOC_MULTI);
		} else {
			ev_tbl[i] = esv_em2usr(ev_tbl[i], ev_hdr_tbl[i], sub,
					       EVSTATE__ALLOC_MULTI, false);
		}
	}
}

em_event_t evstate_ref(const em_event_t event, event_hdr_t *const ev_hdr)
{
	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	return esv_em2usr(event, ev_hdr, sub, EVSTATE__EVENT_REF, false);
}

em_event_t evstate_init(const em_event_t event, event_hdr_t *const ev_hdr,
			bool is_extev)
{
	if (is_extev)
		return esv_evinit_ext(event, ev_hdr, EVSTATE__INIT_EXTEV);
	else
		return esv_evinit(event, ev_hdr, init_cnt_alloc, EVSTATE__INIT);
}

void evstate_init_multi(em_event_t ev_tbl[/*in/out*/],
			event_hdr_t *const ev_hdr_tbl[], const int num,
			bool is_extev)
{
	uint16_t api_op;
	evstate_cnt_t init_cnt;

	if (is_extev) {
		api_op = EVSTATE__INIT_EXTEV_MULTI;
		init_cnt = init_cnt_extev;
	} else {
		api_op = EVSTATE__INIT_MULTI;
		init_cnt = init_cnt_alloc;
	}

	esv_evinit_multi(ev_tbl/*in/out*/, ev_hdr_tbl, num,
			 init_cnt, api_op);
}

/**
 * This is a combined calculation of the following three separate
 * calculations:
 *
 * mark allocated:
 * const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
 *                            .ref_cnt = 1, .send_cnt = 0};
 * event = esv_em2usr(event, ev_hdr, sub, api_op, false);
 *
 * mark sent:
 * const evstate_cnt_t add = {.evgen = 1, .rsvd = 0,
 *                            .ref_cnt = 0, .send_cnt = 1};
 * esv_usr2em(event, ev_hdr, add, api_op, false);
 *
 * mark em2usr for dispatch to user EO:
 * const evstate_cnt_t sub2 = {.evgen = 0, .rsvd = 0,
 *                             .ref_cnt = 0, .send_cnt = 1};
 * event = esv_em2usr(event, ev_hdr, sub2, api_op, false);
 *
 * combined = add - sub - sub2
 * add:    {.evgen = 1, .rsvd = 0, .ref_cnt = 0, .send_cnt = 1}
 * sub:  - {.evgen = 0, .rsvd = 0, .ref_cnt = 1, .send_cnt = 0}
 * sub2: - {.evgen = 0, .rsvd = 0, .ref_cnt = 0, .send_cnt = 1}
 *       -------------------------------------------------------
 * cmb =   {.evgen = 1, .rsvd = 0, .ref_cnt =-1, .send_cnt = 0}
 */
static inline em_event_t
esv_update_ext(const em_event_t event, event_hdr_t *const ev_hdr,
	       const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};
	const evstate_cnt_t add = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 0};
	const evstate_cnt_t cmb = {.u64 = add.u64 - sub.u64}; /* combined, wraps */

	const bool refs_used = ev_hdr->flags.refs_used;
	evhdl_t evhdl = {.event = event};
	evstate_cnt_t new_cnt;

	/* Update state-count and return value of all counters (atomic) */
	new_cnt.u64 = __atomic_add_fetch(&ev_hdr->state_cnt.u64,
					 cmb.u64, __ATOMIC_RELAXED);

	if (unlikely(new_cnt.evgen == EVGEN_MAX)) {
		/* Avoid .evgen counter wrap */
		const evstate_cnt_t wrap = {.evgen = EVGEN_MAX - EVGEN_INIT,
					    .rsvd = 0, .ref_cnt = 0, .send_cnt = 0};
		new_cnt.u64 = __atomic_sub_fetch(&ev_hdr->state_cnt.u64, wrap.u64,
						 __ATOMIC_RELAXED);
	}

	if (!refs_used) {
		evhdl.evgen = new_cnt.evgen;
		ev_hdr->event = evhdl.event;
	}

	const int16_t ref_cnt = REF_CNT_INIT - new_cnt.ref_cnt;
	const int16_t send_cnt = new_cnt.send_cnt - SEND_CNT_INIT;

	/*
	 * Check state count:
	 * OK: send_cnt < ref_cnt and both >=0. Error otherwise.
	 */
	if (unlikely(send_cnt >= ref_cnt || send_cnt < 0)) {
		/* report fatal event-state error, never return */
		evstate_error(new_cnt, evhdl, ev_hdr, api_op, help_str_em2usr);
		/* never reached */
	}

	/*
	 * Valid state transition, update state (non-atomic)
	 */
	if (!refs_used)
		evhdr_update_state(ev_hdr, api_op);

	return evhdl.event;
}

em_event_t evstate_update(const em_event_t event, event_hdr_t *const ev_hdr,
			  bool is_extev)
{
	em_event_t ret_event;

	if (is_extev) {
		/* combined mark allocated & mark sent */
		ret_event = esv_update_ext(event, ev_hdr, EVSTATE__UPDATE_EXTEV);
	} else {
		/* mark allocated */
		const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
					   .ref_cnt = 1, .send_cnt = 0};

		ret_event = esv_em2usr(event, ev_hdr, sub, EVSTATE__UPDATE_EXTEV, false);
	}

	return ret_event;
}

void evstate_free(em_event_t event, event_hdr_t *const ev_hdr,
		  const uint16_t api_op)
{
	const evstate_cnt_t add = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	esv_usr2em(event, ev_hdr, add, api_op, false);
}

void evstate_free_revert(em_event_t event, event_hdr_t *const ev_hdr,
			 const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	esv_usr2em(event, ev_hdr, sub, api_op, true /*revert*/);
}

void evstate_free_multi(const em_event_t ev_tbl[],
			event_hdr_t *const ev_hdr_tbl[], const int num,
			const uint16_t api_op)
{
	const evstate_cnt_t add = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	esv_usr2em_multi(ev_tbl, ev_hdr_tbl, num, add, api_op, false);
}

void evstate_free_revert_multi(const em_event_t ev_tbl[],
			       event_hdr_t *const ev_hdr_tbl[], const int num,
			       const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 1, .send_cnt = 0};

	esv_usr2em_multi(ev_tbl, ev_hdr_tbl, num, sub, api_op, true /*revert*/);
}

em_event_t evstate_em2usr(const em_event_t event, event_hdr_t *const ev_hdr,
			  const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	return esv_em2usr(event, ev_hdr, sub, api_op, false);
}

em_event_t evstate_em2usr_revert(const em_event_t event, event_hdr_t *const ev_hdr,
				 const uint16_t api_op)
{
	const evstate_cnt_t add = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	return esv_em2usr(event, ev_hdr, add, api_op, true /*revert*/);
}

void evstate_em2usr_multi(em_event_t ev_tbl[/*in/out*/],
			  event_hdr_t *const ev_hdr_tbl[], const int num,
			  const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	esv_em2usr_multi(ev_tbl/*in/out*/, ev_hdr_tbl, num, sub, api_op, false);
}

void evstate_em2usr_revert_multi(em_event_t ev_tbl[/*in/out*/],
				 event_hdr_t *const ev_hdr_tbl[], const int num,
				 const uint16_t api_op)
{
	const evstate_cnt_t add = {.evgen = 0, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	esv_em2usr_multi(ev_tbl/*in/out*/, ev_hdr_tbl, num, add, api_op, true /*revert*/);
}

void evstate_usr2em(const em_event_t event, event_hdr_t *const ev_hdr,
		    const uint16_t api_op)
{
	const evstate_cnt_t add = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	esv_usr2em(event, ev_hdr, add, api_op, false);
}

void evstate_usr2em_revert(const em_event_t event, event_hdr_t *const ev_hdr,
			   const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	esv_usr2em(event, ev_hdr, sub, api_op, true /*revert*/);
}

void evstate_usr2em_multi(const em_event_t ev_tbl[],
			  event_hdr_t *const ev_hdr_tbl[], const int num,
			  const uint16_t api_op)
{
	const evstate_cnt_t add = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	esv_usr2em_multi(ev_tbl, ev_hdr_tbl, num, add, api_op, false);
}

void evstate_usr2em_revert_multi(const em_event_t ev_tbl[],
				 event_hdr_t *const ev_hdr_tbl[], const int num,
				 const uint16_t api_op)
{
	const evstate_cnt_t sub = {.evgen = 1, .rsvd = 0,
				   .ref_cnt = 0, .send_cnt = 1};

	esv_usr2em_multi(ev_tbl, ev_hdr_tbl, num, sub, api_op, true /*revert*/);
}

/*
 * Ensure that em_event_unmark_...() is only called after
 * em_event_mark_...() (not after normal em_send/free() etc).
 */
static inline void
check_valid_unmark(const event_hdr_t *ev_hdr, uint16_t api_op,
		   const uint16_t expected_ops[], const int num_ops)
{
	/* event refs: can't rely on prev api_op */
	if (ev_hdr->flags.refs_used)
		return;

	uint16_t prev_op = ev_hdr->state.api_op;

	for (int i = 0; i < num_ops; i++) {
		if (prev_op == expected_ops[i])
			return; /* success */
	}

	/* previous API was NOT em_event_mark_..., report FATAL error! */
	evstate_unmark_error(ev_hdr, api_op);
}

static inline void
check_valid_unmark_multi(event_hdr_t *const ev_hdr_tbl[], const int num_evs,
			 uint16_t api_op, const uint16_t expected_ops[], const int num_ops)
{
	uint16_t prev_op;
	bool is_valid;

	for (int i = 0; i < num_evs; i++) {
		/* event refs: can't rely on prev api_op */
		if (ev_hdr_tbl[i]->flags.refs_used)
			continue;

		prev_op = ev_hdr_tbl[i]->state.api_op;
		is_valid = false;

		for (int j = 0; j < num_ops; j++) {
			if (prev_op == expected_ops[j]) {
				is_valid = true;
				break; /* success */
			}
		}

		/* previous API was NOT em_event_mark_..., report FATAL error!*/
		if (unlikely(!is_valid))
			evstate_unmark_error(ev_hdr_tbl[i], api_op);
	}
}

void evstate_unmark_send(const em_event_t event, event_hdr_t *const ev_hdr)
{
	if (em_shm->opt.esv.store_state) {
		uint16_t expected_prev_ops[1] = {EVSTATE__MARK_SEND};
		/*
		 * Ensure that em_event_unmark_send() is only called after
		 * em_event_mark_send/_multi() (not after em_send() etc).
		 */
		check_valid_unmark(ev_hdr, EVSTATE__UNMARK_SEND,
				   expected_prev_ops, 1);
	}

	evstate_usr2em_revert(event, ev_hdr, EVSTATE__UNMARK_SEND);
}

void evstate_unmark_free(const em_event_t event, event_hdr_t *const ev_hdr,
			 const uint16_t api_op)
{
	if (em_shm->opt.esv.store_state) {
		uint16_t expected_prev_ops[2] = {EVSTATE__MARK_FREE,
						 EVSTATE__MARK_FREE_MULTI};
		/*
		 * Ensure that em_event_unmark_free() is only called
		 * after em_event_mark_free() (not after em_free() etc).
		 */
		check_valid_unmark(ev_hdr, api_op, expected_prev_ops, 2);
	}

	evstate_free_revert(event, ev_hdr, api_op);
}

void evstate_unmark_free_multi(const em_event_t ev_tbl[],
			       event_hdr_t *const ev_hdr_tbl[], const int num,
			       const uint16_t api_op)
{
	if (em_shm->opt.esv.store_state) {
		uint16_t expected_prev_ops[2] = {EVSTATE__MARK_FREE_MULTI,
						 EVSTATE__MARK_FREE};
		/*
		 * Ensure that em_event_unmark_free_multi() is only
		 * called after em_event_mark_free_multi()
		 * (not after em_free/_multi() etc).
		 */
		check_valid_unmark_multi(ev_hdr_tbl, num, api_op,
					 expected_prev_ops, 2);
	}

	evstate_free_revert_multi(ev_tbl, ev_hdr_tbl, num, api_op);
}

static int read_config_file(void)
{
	const char *conf_str;
	bool val_bool = false;
	int ret;

	EM_PRINT("EM ESV config: (EM_ESV_ENABLE=%d)\n", EM_ESV_ENABLE);

	/*
	 * Option: esv.enable - runtime enable/disable
	 */
	conf_str = "esv.enable";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.esv.enable = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	if (!em_shm->opt.esv.enable) {
		/* Read no more options if ESV is disabled */
		memset(&em_shm->opt.esv, 0, sizeof(em_shm->opt.esv));
		return 0;
	}

	/*
	 * Option: esv.store_state
	 */
	conf_str = "esv.store_state";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.esv.store_state = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	/*
	 * Option: esv.store_payload_first_u32
	 */
	conf_str = "esv.store_payload_first_u32";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.esv.store_first_u32 = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	/*
	 * Option: esv.prealloc_pools
	 */
	conf_str = "esv.prealloc_pools";
	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", conf_str);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.esv.prealloc_pools = (int)val_bool;
	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false",
		 val_bool);

	return 0;
}

em_status_t esv_init(void)
{
	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

void esv_disabled_warn_config(void)
{
	const char *conf_str = "esv.enable";
	bool val_bool = false;
	int ret;

	EM_PRINT("EM ESV config: (EM_ESV_ENABLE=%d)\n", EM_ESV_ENABLE);
	EM_PRINT("  ESV disabled\n");

	ret = em_libconfig_lookup_bool(&em_shm->libconfig, conf_str, &val_bool);
	if (unlikely(!ret))
		return; /* ESV state option not found in runtime, no warning */

	EM_PRINT("  %s: %s(%d)\n", conf_str, val_bool ? "true" : "false", val_bool);

	if (unlikely(val_bool))
		EM_PRINT("  WARNING: ESV disabled (build-time) - config file option IGNORED!\n");
}
