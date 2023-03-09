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

#define EVSTATE__UNDEF                         0
#define EVSTATE__PREALLOC                      1
#define EVSTATE__ALLOC                         2
#define EVSTATE__ALLOC_MULTI                   3
#define EVSTATE__EVENT_CLONE                   4
#define EVSTATE__EVENT_REF                     5
#define EVSTATE__FREE                          6
#define EVSTATE__FREE_MULTI                    7
#define EVSTATE__EVENT_VECTOR_FREE             8
#define EVSTATE__INIT                          9
#define EVSTATE__INIT_MULTI                   10
#define EVSTATE__INIT_EXTEV                   11
#define EVSTATE__INIT_EXTEV_MULTI             12
#define EVSTATE__UPDATE_EXTEV                 13
#define EVSTATE__SEND                         14
#define EVSTATE__SEND__FAIL                   15
#define EVSTATE__SEND_EGRP                    16
#define EVSTATE__SEND_EGRP__FAIL              17
#define EVSTATE__SEND_MULTI                   18
#define EVSTATE__SEND_MULTI__FAIL             19
#define EVSTATE__SEND_EGRP_MULTI              20
#define EVSTATE__SEND_EGRP_MULTI__FAIL        21
#define EVSTATE__EO_START_SEND_BUFFERED       22
#define EVSTATE__MARK_SEND                    23
#define EVSTATE__UNMARK_SEND                  24
#define EVSTATE__MARK_FREE                    25
#define EVSTATE__UNMARK_FREE                  26
#define EVSTATE__MARK_FREE_MULTI              27
#define EVSTATE__UNMARK_FREE_MULTI            28
#define EVSTATE__DISPATCH                     29
#define EVSTATE__DISPATCH_MULTI               30
#define EVSTATE__DISPATCH_SCHED__FAIL         31
#define EVSTATE__DISPATCH_LOCAL__FAIL         32
#define EVSTATE__DEQUEUE                      33
#define EVSTATE__DEQUEUE_MULTI                34
#define EVSTATE__TMO_SET_ABS                  35
#define EVSTATE__TMO_SET_ABS__FAIL            36
#define EVSTATE__TMO_SET_REL                  37
#define EVSTATE__TMO_SET_REL__FAIL            38
#define EVSTATE__TMO_SET_PERIODIC             39
#define EVSTATE__TMO_SET_PERIODIC__FAIL       40
#define EVSTATE__TMO_CANCEL                   41
#define EVSTATE__TMO_ACK                      42
#define EVSTATE__TMO_ACK__NOSKIP              43
#define EVSTATE__TMO_ACK__FAIL                44
#define EVSTATE__TMO_DELETE                   45
#define EVSTATE__AG_DELETE                    46
#define EVSTATE__TERM_CORE__QUEUE_LOCAL       47
#define EVSTATE__TERM                         48
#define EVSTATE__LAST                         49 /* Must be largest number! */

/**
 * Init values for the event-state counters.
 *
 * The counters are 16-bit but are updated as one combined 64-bit atomic var,
 * thus the init values are in the middle of the u16-range to avoid wraparounds
 * when decrementing below '0'.
 */
/** Initial event generation value */
#define EVGEN_INIT    ((uint16_t)1)
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
 * Set the initial event state during em_pool_create() when preallocating events
 */
em_event_t evstate_prealloc(const em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Set the initial event state during em_alloc()
 */
em_event_t evstate_alloc(const em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Set the initial state of multiple events during em_alloc_multi()
 */
void evstate_alloc_multi(em_event_t ev_tbl[/*in/out*/],
			 event_hdr_t *const ev_hdr_tbl[], const int num);
/**
 * Check & update event state during em_event_clone()
 */
em_event_t evstate_clone(const em_event_t event, event_hdr_t *const ev_hdr);

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
void evstate_unmark_free(const em_event_t event, event_hdr_t *const ev_hdr);

/**
 * Check & update event state for multiple events during
 * em_event_unmark_free_multi()
 *
 * Wrapper function for
 * evstate_free_revert_multi(..., EVSTATE__UNMARK_FREE_MULTI)
 * with extra error checks.
 */
void evstate_unmark_free_multi(const em_event_t ev_tbl[],
			       event_hdr_t *const ev_hdr_tbl[], const int num);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_CHECKS_H_ */
