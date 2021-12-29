/*
 *   Copyright (c) 2015-2021, Nokia Solutions and Networks
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
 * by EM in events based on odp-pkt-buffers.
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
 * Internal representation of the event handle (em_event_t) when using
 * Event State Verification (ESV)
 *
 * An event-generation-count is encoded into the high bits of the event handle
 * to catch illegal usage after the event ownership has been transferred.
 * Each user-to-EM event state transition increments the .evgen and thus
 * obsoletes any further use of the handle by that user.
 */
typedef union {
	em_event_t event;
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		uint64_t evptr : 48;
		uint64_t evgen : 16;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		uint64_t evgen : 16;
		uint64_t evptr : 48;
#endif
	};
} evhdl_t;

COMPILE_TIME_ASSERT(sizeof(evhdl_t) == sizeof(em_event_t), EVHDL_T_SIZE_ERROR);

/**
 * Event-state counters: 'evgen', 'free_cnt' and 'send_cnt'.
 *
 * Updated as one single atomic var via 'evstate_cnt_t::u64'.
 */
typedef union ODP_ALIGNED(sizeof(uint64_t)) {
	uint64_t u64; /* updated atomically in the event-hdr */
	struct {
		uint16_t evgen;
		uint16_t rsvd;
		union {
			struct {
				uint16_t free_cnt;
				uint16_t send_cnt;
			};
			uint32_t free_send_cnt;
		};
	};
} evstate_cnt_t;

/* Verify size of struct, i.e. accept no padding */
COMPILE_TIME_ASSERT(sizeof(evstate_cnt_t) == sizeof(uint64_t),
		    EVSTATE_CNT_T_SIZE_ERROR);

/**
 * Event-state information.
 * Not atomically updated (but evstate_cnt_t is updated atomically)
 */
typedef struct {
	/**
	 * Event state, updated on valid state trasitions.
	 * "Best effort" update, i.e. atomic update of state not
	 * guaranteed in invalid simultaneous state updates.
	 *
	 * Contains the previously known good state and will be
	 * printed when detecting an invalid state transition.
	 */
	em_eo_t eo;
	em_queue_t queue;
	/**
	 * EM API operation ID.
	 * Identifies the previously called API func that altered state
	 */
	uint16_t api_op;
	/** EM core that called API('api_op') */
	uint16_t core;
	/**
	 * First 'word' of the event payload as seen
	 * at the time of the previous state update.
	 */
	uint32_t payload_first;
} ev_hdr_state_t;

/**
 * Event header
 *
 * SW & I/O originated events.
 */
typedef struct {
	/**
	 * Event State Verification (ESV): event state data
	 */
	union {
		uint8_t u8[32];
		struct {
			/**
			 * Together the evstate_cnt_t counters (evgen, free_cnt
			 * and send_cnt) can be used to  detect invalid states
			 * and operations on the event, e.g.:
			 * double-free, double-send, send-after-free,
			 * free-after-send, usage-after-output,
			 * usage-after-timer-tmo-set/ack/cancel/delete etc.
			 */
			evstate_cnt_t state_cnt;

			/**
			 * Event state, updated on valid state trasitions.
			 * "Best effort" update, i.e. atomic update not
			 * guaranteed in invalid simultaneous state-updates.
			 *
			 * Contains the previously known good state and will be
			 * printed when detecting an invalid state transition.
			 */
			ev_hdr_state_t state;
		};
	};
	/**
	 * EO-start send event buffering, event linked-list node
	 */
	list_node_t start_node;
	/**
	 * Queue element for the associated queue
	 * @note only used for atomic-group- or local-queues
	 */
	queue_elem_t *q_elem;

	union {
		uint64_t all;
		struct {
			/** requested size (bytes) */
			uint64_t req_size : 16;
			/** + padding, incl. space for align_offset (bytes) */
			uint64_t pad_size : 16;
			/** user area id */
			uint64_t id       : 16;
			/** is the user area id set? */
			uint64_t isset_id : 1;
			/** is the uarea initialized? */
			uint64_t isinit   : 1;
			/** reserved bits */
			uint64_t rsvd     : 14;
		};
	} user_area;

	/* --- CACHE LINE on systems with a 64B cache line size --- */

	/**
	 * Event handle (this event)
	 */
	em_event_t event ODP_ALIGNED(64);
	/**
	 * Queue handle
	 * @note only used by EM chaining & EO-start send event buffering
	 */
	em_queue_t queue;
	/**
	 * Event Group handle
	 */
	em_event_group_t egrp;
	/**
	 * Event group generation
	 */
	int32_t egrp_gen;
	/**
	 * Event size
	 */
	uint32_t event_size;
	/**
	 * Handle of the EM pool the event was allocated from.
	 * @note only used if EM config file: pool.statistics_enable=true
	 */
	em_pool_t pool;
	/**
	 * Subpool index of the EM pool the event was allocated from.
	 * @note only used if EM config file: pool.statistics_enable=true
	 */
	int16_t subpool;
	/**
	 * Payload alloc alignment offset/push into free area of ev_hdr.
	 * Only used by events based on ODP buffers that have the ev_hdr in the
	 * beginning of the buf payload (pkts use 'user-area' for ev_hdr).
	 * Value is copied from pool_elem->align_offset for easy access.
	 */
	uint16_t align_offset;
	/**
	 * Event type, contains major and major parts
	 */
	em_event_type_t event_type;

	/**
	 * End of event header data,
	 * for offsetof(event_hdr_t, end_hdr_data)
	 */
	uint8_t end_hdr_data[0];

	/*
	 * ! EMPTY SPACE !
	 * Events based on odp_buffer_t only:
	 *   - space for alignment adjustments as set by
	 *      a) config file option - 'pool.align_offset' or
	 *      b) pool config param  - 'em_pool_cfg_t:align_offset{}'
	 *   - space available:
	 *         sizeof(event_hdr_t) - offsetof(event_hdr_t, end_hdr_data)
	 *   - events based on odp_packet_t have their event header in the
	 *     odp pkt user area and alignment is adjusted in the pkt headroom.
	 *
	 * Note: If the event user area is enabled then (for bufs) it will start
	 *       after the event header and the align offset is not included
	 *       in the event header but instead starts after the user area.
	 */

	void *end[0] ODP_ALIGNED(64); /* pad to next 64B boundary */
} event_hdr_t;

COMPILE_TIME_ASSERT(sizeof(event_hdr_t) <= 128, EVENT_HDR_SIZE_ERROR);
COMPILE_TIME_ASSERT(sizeof(event_hdr_t) % 32 == 0, EVENT_HDR_SIZE_ERROR2);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_TYPES_H_ */
