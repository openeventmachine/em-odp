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
  * EM event chaining support
  */

#ifndef EM_EVENT_CHECKS_H_
#define EM_EVENT_CHECKS_H_

#ifdef __cplusplus
extern "C" {
#endif

#pragma GCC visibility push(default)

#define EVSTATE__UNDEF                         0
#define EVSTATE__PREALLOC                      1
#define EVSTATE__ALLOC                         2
#define EVSTATE__ALLOC_MULTI                   3
#define EVSTATE__FREE                          4
#define EVSTATE__FREE_MULTI                    5
#define EVSTATE__INIT                          6
#define EVSTATE__INIT_MULTI                    7
#define EVSTATE__INIT_EXTEV                    8
#define EVSTATE__INIT_EXTEV_MULTI              9
#define EVSTATE__SEND                         10
#define EVSTATE__SEND__FAIL                   11
#define EVSTATE__SEND_EGRP                    12
#define EVSTATE__SEND_EGRP__FAIL              13
#define EVSTATE__SEND_MULTI                   14
#define EVSTATE__SEND_MULTI__FAIL             15
#define EVSTATE__SEND_EGRP_MULTI              16
#define EVSTATE__SEND_EGRP_MULTI__FAIL        17
#define EVSTATE__MARK_SEND                    18
#define EVSTATE__UNMARK_SEND                  19
#define EVSTATE__DISPATCH                     20
#define EVSTATE__DISPATCH_MULTI               21
#define EVSTATE__DISPATCH_SCHED__FAIL         22
#define EVSTATE__DISPATCH_LOCAL__FAIL         23
#define EVSTATE__DEQUEUE                      24
#define EVSTATE__DEQUEUE_MULTI                25
#define EVSTATE__OUTPUT                       26 /* before output-queue callback-fn */
#define EVSTATE__OUTPUT__FAIL                 27
#define EVSTATE__OUTPUT_MULTI                 28 /* before output-queue callback-fn */
#define EVSTATE__OUTPUT_MULTI__FAIL           29
#define EVSTATE__OUTPUT_CHAINING              30 /* before event_send_device() */
#define EVSTATE__OUTPUT_CHAINING__FAIL        31
#define EVSTATE__OUTPUT_CHAINING_MULTI        32 /* before event_send_device_multi()*/
#define EVSTATE__OUTPUT_CHAINING_MULTI__FAIL  33 /* before event_send_device_multi()*/
#define EVSTATE__TMO_SET_ABS                  34
#define EVSTATE__TMO_SET_ABS__FAIL            35
#define EVSTATE__TMO_SET_REL                  36
#define EVSTATE__TMO_SET_REL__FAIL            37
#define EVSTATE__TMO_SET_PERIODIC             38
#define EVSTATE__TMO_SET_PERIODIC__FAIL       39
#define EVSTATE__TMO_CANCEL                   40
#define EVSTATE__TMO_ACK                      41
#define EVSTATE__TMO_ACK__NOSKIP              42
#define EVSTATE__TMO_ACK__FAIL                43
#define EVSTATE__TMO_DELETE                   44
#define EVSTATE__AG_DELETE                    45
#define EVSTATE__TERM_CORE__QUEUE_LOCAL       46
#define EVSTATE__TERM                         47
#define EVSTATE__LAST                         48 /* Must be largest number! */

/**
 * Init values for the event-state counters 'free_cnt' and 'send_cnt'.
 *
 * The counters are 32-bit but are updated as one combined 64-bit atomic var,
 * thus the init values are in the middle of the u32-range to avoid wraparounds
 * when decrementing below '0'.
 */
#define FREE_CNT_INIT ((uint16_t)0x0100) /* =  0 + 'offset' */
#define SEND_CNT_INIT ((uint16_t)0x0100) /* =  0 + 'offset' */
/** Initial event generation value */
#define EVGEN_INIT    ((uint16_t)1)
/** Max evgen value before resetting to 'EVGEN_INIT' to avoid wrap */
#define EVGEN_MAX  ((uint16_t)UINT16_MAX - 0x1000)

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
 * Set the initial state for an event
 * (e.g. an new odp-event converted into an EM-event)
 */
em_event_t evstate_init(const em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Set the initial state for events
 * (e.g. new odp-events converted into EM-events)
 */
void evstate_init_multi(em_event_t ev_tbl[/*in/out*/],
			event_hdr_t *const ev_hdr_tbl[], const int num);
/**
 * Set the initial state for an external event input into EM
 * (e.g. an external odp pktio event input into EM and seen in the dispatcher)
 */
em_event_t evstate_init_extev(em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Set the initial state for external events input into EM
 * (e.g. external odp pktio events input into EM and seen in the dispatcher)
 */
void evstate_init_extev_multi(em_event_t ev_tbl[/*in/out*/],
			      event_hdr_t *const ev_hdr_tbl[], const int num);

/**
 * Check & update event state during em_free()
 */
void evstate_free(em_event_t event, event_hdr_t *const ev_hdr);
/**
 * Check & update the state of multiple events during em_free_multi()
 */
void evstate_free_multi(const em_event_t ev_tbl[],
			event_hdr_t *const ev_hdr_tbl[], const int num);

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
#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_CHECKS_H_ */
