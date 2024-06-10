/*
 *   Copyright (c) 2020, Nokia Solutions and Networks
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
  * EM event state verification support
  */

#ifndef EM_EVENT_CHECKS_H_
#define EM_EVENT_CHECKS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESV API operation IDs
 */
typedef enum {
	EVSTATE__UNDEF = 0, /* Must be first! */
	EVSTATE__PREALLOC,
	EVSTATE__ALLOC,
	EVSTATE__ALLOC_MULTI,
	EVSTATE__EVENT_CLONE,
	EVSTATE__EVENT_REF,
	EVSTATE__FREE,
	EVSTATE__FREE_MULTI,
	EVSTATE__EVENT_VECTOR_FREE,
	EVSTATE__INIT,
	EVSTATE__INIT_MULTI,
	EVSTATE__INIT_EXTEV,
	EVSTATE__INIT_EXTEV_MULTI,
	EVSTATE__UPDATE_EXTEV,
	EVSTATE__SEND,
	EVSTATE__SEND__FAIL,
	EVSTATE__SEND_EGRP,
	EVSTATE__SEND_EGRP__FAIL,
	EVSTATE__SEND_MULTI,
	EVSTATE__SEND_MULTI__FAIL,
	EVSTATE__SEND_EGRP_MULTI,
	EVSTATE__SEND_EGRP_MULTI__FAIL,
	EVSTATE__EO_START_SEND_BUFFERED,
	EVSTATE__MARK_SEND,
	EVSTATE__UNMARK_SEND,
	EVSTATE__MARK_FREE,
	EVSTATE__UNMARK_FREE,
	EVSTATE__MARK_FREE_MULTI,
	EVSTATE__UNMARK_FREE_MULTI,
	EVSTATE__DISPATCH,
	EVSTATE__DISPATCH_MULTI,
	EVSTATE__DISPATCH_SCHED__FAIL,
	EVSTATE__DISPATCH_LOCAL__FAIL,
	EVSTATE__DEQUEUE,
	EVSTATE__DEQUEUE_MULTI,
	EVSTATE__TMO_SET_ABS,
	EVSTATE__TMO_SET_ABS__FAIL,
	EVSTATE__TMO_SET_REL,
	EVSTATE__TMO_SET_REL__FAIL,
	EVSTATE__TMO_SET_PERIODIC,
	EVSTATE__TMO_SET_PERIODIC__FAIL,
	EVSTATE__TMO_CANCEL,
	EVSTATE__TMO_ACK,
	EVSTATE__TMO_ACK__NOSKIP,
	EVSTATE__TMO_ACK__FAIL,
	EVSTATE__TMO_CREATE,
	EVSTATE__TMO_DELETE,
	EVSTATE__AG_DELETE,
	EVSTATE__TERM_CORE__QUEUE_LOCAL,
	EVSTATE__TERM,
	EVSTATE__LAST /* Must be last! */
} esv_apiop_t;

/* esv_apiop_t::EVSTATE__LAST must fit into ev_hdr_state_t::uint8_t api_op */
COMPILE_TIME_ASSERT(EVSTATE__LAST <= UINT8_MAX, EVSTATE__LAST__TOO_BIG);

/**
 * Init values for the event-state counters.
 *
 * The counters are 16-bit but are updated as one combined 64-bit atomic var,
 * thus the init values are in the middle of the u16-range to avoid wraparounds
 * when decrementing below '0'.
 */
/** Initial event generation value */
#define EVGEN_INIT    ((uint16_t)0x1000)
/** Max evgen value before resetting to 'EVGEN_INIT' to avoid wrap */
#define EVGEN_MAX  ((uint16_t)UINT16_MAX - 0x1000)
/** Initial send count value */
#define SEND_CNT_INIT ((uint16_t)0x8000) /* =  0 + 'offset' */
/** Initial reference count value */
#define REF_CNT_INIT    ((uint16_t)0x8000) /* =  0 + 'offset' */
/** Max reference count before resetting to 'REF_CNT_INIT' to avoid wrap */
#define REF_CNT_MAX  ((uint16_t)UINT16_MAX - 0x1000)

/**
 * Return 'true' if ESV is enabled
 *
 * - EM_ESV_ENABLE is set via the 'configure' script: --enable/disable-esv
 * - esv.enable' is set via the EM config file (default: conf/em-odp.conf)
 */
static inline bool esv_enabled(void)
{
	return EM_ESV_ENABLE && em_shm->opt.esv.enable;
}

/**
 * Init ESV (if enabled at compile time), read config options
 */
em_status_t esv_init(void);
/**
 * In the case that ESV has been disabled during compile time, verify that the compile-time
 * option and runtime config file options do not clash - warn the user otherwise!
 *
 * The user might not notice that the run time config file option has no effect if ESV has
 * been disabled during compile time. The warning printed at startup is meant to notify the
 * user that ESV will be disabled no matter the content in the run time config file.
 */
void esv_disabled_warn_config(void);
/**
 * Set the initial event state during em_pool_create() when preallocating events
 */
em_event_t evstate_prealloc(const em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Set the initial event state during timeout allocation.
 */
em_event_t evstate_alloc_tmo(const em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Set the initial event state during em_alloc() / em_event_clone()
 */
em_event_t evstate_alloc(const em_event_t event, event_hdr_t *const ev_hdr,
			 const uint16_t api_op);
/**
 * Set the initial state of multiple events during em_alloc_multi()
 */
void evstate_alloc_multi(em_event_t ev_tbl[/*in/out*/],
			 event_hdr_t *const ev_hdr_tbl[], const int num);
/**
 * Update event state during em_event_ref()
 */
em_event_t evstate_ref(const em_event_t event, event_hdr_t *const ev_hdr);

/**
 * Set the initial state for an event
 * (e.g. an new odp-event converted into an EM-event)
 */
em_event_t evstate_init(const em_event_t event, event_hdr_t *const ev_hdr,
			bool is_extev);
/**
 * Set the initial state for events
 * (e.g. new odp-events converted into EM-events)
 */
void evstate_init_multi(em_event_t ev_tbl[/*in/out*/],
			event_hdr_t *const ev_hdr_tbl[], const int num,
			bool is_extev);

/**
 * Update the state for external events input into EM.
 * Used when esv.prealloc_pools = true and the input event was allocated
 * externally to EM (e.g. by ODP) but from an EM event-pool.
 */
em_event_t evstate_update(const em_event_t event,
			  event_hdr_t *const ev_hdr, bool is_extev);

/**
 * Check & update event state during em_free() or em_event_mark_free()
 */
void evstate_free(em_event_t event, event_hdr_t *const ev_hdr,
		  const uint16_t api_op);
/**
 * Check & update event state during em_event_unmark_free()
 */
void evstate_free_revert(em_event_t event, event_hdr_t *const ev_hdr,
			 const uint16_t api_op);

/**
 * Check & update the state of multiple events during em_free_multi() or
 * em_event_mark_free_multi()
 */
void evstate_free_multi(const em_event_t ev_tbl[],
			event_hdr_t *const ev_hdr_tbl[], const int num,
			const uint16_t api_op);
/**
 * Check & update event state during em_event_unmark_free_multi()
 */
void evstate_free_revert_multi(const em_event_t ev_tbl[],
			       event_hdr_t *const ev_hdr_tbl[], const int num,
			       const uint16_t api_op);
/**
 * Check & update event state - event passed from EM to user.
 *
 * em_dispatch(), em_queue_dequeue(), em_tmo_cancel(), em_tmo_delete()
 */
em_event_t evstate_em2usr(em_event_t event, event_hdr_t *const ev_hdr,
			  const uint16_t api_op);
/**
 * Revert EM-to-user event-state update on failed operation.
 */
em_event_t evstate_em2usr_revert(em_event_t event, event_hdr_t *const ev_hdr,
				 const uint16_t api_op);
/**
 * Check & update the state of multiple events - events passed from EM to user
 *
 * em_dispatch(), em_queue_dequeue_multi(), em_term()
 */
void evstate_em2usr_multi(em_event_t ev_tbl[/*in/out*/],
			  event_hdr_t *const ev_hdr_tbl[], const int num,
			  const uint16_t api_op);
/**
 * Revert EM-to-user event-state updates on failed operation.
 */
void evstate_em2usr_revert_multi(em_event_t ev_tbl[/*in/out*/],
				 event_hdr_t *const ev_hdr_tbl[], const int num,
				 const uint16_t api_op);
/**
 * Check & update event state - event passed from the user to EM.
 *
 * em_send(), em_send_group(), em_tmo_set_abs/rel/periodic(), em_tmo_ack()
 */
void evstate_usr2em(em_event_t event, event_hdr_t *const ev_hdr,
		    const uint16_t api_op);
/**
 * Revert user-to-EM event-state update on failed operation.
 */
void evstate_usr2em_revert(em_event_t event, event_hdr_t *const ev_hdr,
			   const uint16_t api_op);
/**
 * Check & update the state of multiple events - events passed from user to EM
 *
 * em_send_multi(), em_send_group_multi()
 */
void evstate_usr2em_multi(const em_event_t ev_tbl[],
			  event_hdr_t *const ev_hdr_tbl[], const int num,
			  const uint16_t api_op);
/**
 * Revert user-to-EM event-state updates on failed operation.
 */
void evstate_usr2em_revert_multi(const em_event_t ev_tbl[],
				 event_hdr_t *const ev_hdr_tbl[], const int num,
				 const uint16_t api_op);
/**
 * Check & update event state during em_event_unmark_send()
 *
 * Wrapper function for evstate_usr2em_revert(..., EVSTATE__UNMARK_SEND) with
 * extra error checks.
 */
void evstate_unmark_send(const em_event_t event, event_hdr_t *const ev_hdr);

/**
 * Check & update event state during em_event_unmark_free()
 *
 * Wrapper function for evstate_free_revert(..., EVSTATE__UNMARK_FREE) with
 * extra error checks.
 */
void evstate_unmark_free(const em_event_t event, event_hdr_t *const ev_hdr,
			 const uint16_t api_op);

/**
 * Check & update event state for multiple events during
 * em_event_unmark_free_multi()
 *
 * Wrapper function for
 * evstate_free_revert_multi(..., EVSTATE__UNMARK_FREE_MULTI)
 * with extra error checks.
 */
void evstate_unmark_free_multi(const em_event_t ev_tbl[],
			       event_hdr_t *const ev_hdr_tbl[], const int num,
			       const uint16_t api_op);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_CHECKS_H_ */
