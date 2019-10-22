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

 /**
  * @file
  * EM internal event types & definitions
  *
  */

#ifndef EM_EVENT_TYPES_H_
#define EM_EVENT_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

COMPILE_TIME_ASSERT(sizeof(em_event_t) == sizeof(odp_event_t),
		    EM_EVENT_SIZE_MISMATCH);

/**
 * @def PKT_USERPTR_MAGIC_NBR
 *
 * Magic number used to detect whether the EM event-header has been initialized
 * in EM events based on odp-pkt-buffers.
 *
 * Set the odp-pkt user-ptr to this magic number to be able to recognize
 * pkt-events that EM has created vs. pkts from pkt-input that needs their
 * ev-hdrs to be initialized before further EM processing.
 *
 *	if (odp_packet_user_ptr(odp_pkt) != PKT_USERPTR_MAGIC_NBR) {
 *		// Pkt from outside of EM, need to init ev_hdr
 *		odp_packet_user_ptr_set(odp_pkt, PKT_USERPTR_MAGIC_NBR);
 *		init_ev_hdr('ev_hdr in the user-area of the odp-pkt');
 *		...
 *	}
 */
#define PKT_USERPTR_MAGIC_NBR ((void *)(intptr_t)0xA5A5)

/**
 * Event header
 *
 * SW & I/O originated events.
 */
typedef struct {
	/** This event */
	em_event_t event;
	/** Event size */
	size_t event_size;
	/** Event type */
	em_event_type_t event_type;
	/**
	 * Subpool index of the EM pool the event was allocated from.
	 * @note only used if EM_POOL_STATISTICS_ENABLE is set ('1')
	 */
	int32_t subpool;
	/**
	 * Handle of the EM pool the event was allocated from.
	 * @note only used if EM_POOL_STATISTICS_ENABLE is set ('1')
	 */
	em_pool_t pool;

	/** Event Group handle */
	em_event_group_t egrp;
	/** Event group element */
	event_group_elem_t *egrp_elem;
	/** Event group generation */
	int32_t egrp_gen;

	/** Catch double free errors */
	env_atomic32_t allocated;

	/** Queue element for associated queue (for AG or local queue) */
	queue_elem_t *q_elem;

	/* EO start event buffering */
	em_queue_t start_queue; /* EO start event buffering */
	list_node_t start_node;
} event_hdr_t;

COMPILE_TIME_ASSERT(sizeof(event_hdr_t) % sizeof(uint64_t) == 0,
		    EVENT_HDR_SIZE_ERROR);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_TYPES_H_ */
