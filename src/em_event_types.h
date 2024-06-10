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
 * @def USER_FLAG_SET
 *
 * Used to detect whether the event's event-header has been initialized by EM.
 *
 * Set the odp-pkt/vector user-flag to be able to recognize events that EM has
 * created vs. events from pkt-input that needs their ev-hdrs to be initialized
 * before further EM processing.
 */
#define USER_FLAG_SET 1

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
 * Stash entry for EM Atomic Groups internal stashes and Local Queue storage
 * Stash a combo of dst-queue and event as one 64-bit value into the stash.
 */
typedef union {
	uint64_t u64;
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		uint64_t evptr : 48;
		uint64_t qidx  : 16;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		uint64_t qidx : 16;
		uint64_t evptr : 48;
#endif
	};
} stash_entry_t;

COMPILE_TIME_ASSERT(sizeof(stash_entry_t) == sizeof(uint64_t),
		    STASH_ENTRY_T_SIZE_ERROR);

/**
 * Event-state counters: 'evgen', 'ref_cnt' and 'send_cnt'.
 *
 * Atomically updated as one single var via 'evstate_cnt_t::u64'.
 */
typedef union ODP_ALIGNED(sizeof(uint64_t)) {
	uint64_t u64; /* updated atomically in the event-hdr */
	struct {
		uint16_t evgen;
		uint16_t rsvd;
		uint16_t ref_cnt;
		uint16_t send_cnt;
	};
} evstate_cnt_t;

/* Verify size of struct, i.e. accept no padding */
COMPILE_TIME_ASSERT(sizeof(evstate_cnt_t) == sizeof(uint64_t),
		    EVSTATE_CNT_T_SIZE_ERROR);

/**
 * Event state information, updated on valid state transitions.
 * "Best effort" update, i.e. atomic update of state is not
 * guaranteed in invalid simultaneous state updates.
 *
 * Contains the previously known good state and will be
 * printed when detecting an invalid state transition.
 */
typedef struct ODP_PACKED {
	/**
	 * First 'word' of the event payload as seen
	 * at the time of the previous state update.
	 */
	uint32_t payload_first;

	/**
	 * EO-index
	 *
	 * Obtained from the EO with eo_hdl2idx(eo) to save hdr space.
	 */
	int16_t eo_idx;

	/**
	 * Queue-index
	 *
	 * Obtained from the queue with queue_hdl2idx(queue) to save hdr space
	 */
	int16_t queue_idx;

	/**
	 * EM API operation ID.
	 * Identifies the previously called API func that altered state
	 */
	uint8_t api_op;
	/** EM core that called API('api_op') */
	uint8_t core;
} ev_hdr_state_t;

/**
 * Event User Area metadata in the event header
 */
typedef union {
	uint32_t all;
	struct {
		/** is the user area id set? */
		uint32_t isset_id : 1;
		/** is the uarea initialized? */
		uint32_t isinit   : 1;
		/** requested size (bytes), <= EM_EVENT_USER_AREA_MAX_SIZE */
		uint32_t size     : 14;
		/** user area id */
		uint32_t id       : 16;
	};
} ev_hdr_user_area_t;

COMPILE_TIME_ASSERT(sizeof(ev_hdr_user_area_t) == sizeof(uint32_t),
		    EV_HDR_USER_AREA_T_SIZE_ERROR);

/**
 * Event header
 *
 * SW & I/O originated events.
 */
typedef struct event_hdr {
	/**
	 * Event State Verification (ESV) counters.
	 *
	 * Together the evstate_cnt_t counters (evgen, ref_cnt
	 * and send_cnt) can be used to detect invalid states
	 * and operations on the event, e.g.:
	 * double-free, double-send, send-after-free,
	 * free-after-send, usage-after-output,
	 * usage-after-timer-tmo-set/ack/cancel/delete etc.
	 */
	evstate_cnt_t state_cnt;

	/**
	 * Event State Verification (ESV) state.
	 *
	 * Event state, updated on valid state transitions.
	 * "Best effort" update, i.e. atomic update not
	 * guaranteed in invalid simultaneous state-updates.
	 *
	 * Contains the previously known good state and will be
	 * printed when detecting an invalid state transition.
	 */
	ev_hdr_state_t state;

	/**
	 * Event flags
	 */
	union {
		uint8_t all;
		struct {
			/**
			 * Indicate that this event has (or had) references and
			 * some of the ESV checks must be omitted (evgen).
			 * Will be set for the whole lifetime of the event.
			 */
			uint8_t refs_used : 1;
			/**
			 * Indicate that this event is used as tmo indication.
			 * See em_tmo_type_t. Initially 0 = EM_TMO_TYPE_NONE
			 */
			uint8_t tmo_type : 2;

			/** currently unused bits */
			uint8_t unused : 5;
		};
	} flags;

	/**
	 * Payload alloc alignment offset.
	 * Value is copied from pool_elem->align_offset for easy access.
	 */
	uint8_t align_offset;

	/**
	 * Event type, contains major and minor parts
	 */
	em_event_type_t event_type;

	/**
	 * Event handle (this event)
	 */
	em_event_t event;

	/**
	 * Event size
	 *
	 * buf: current size
	 * pkt & vec: original alloc size (otherwise not used, odp size used)
	 * periodic ring timer tmo (EM_EVENT_TYPE_TIMER_IND): 0
	 */
	uint32_t event_size;

	/**
	 * Event group generation
	 */
	int32_t egrp_gen;

	/**
	 * Event Group handle (cannot be used by event references)
	 */
	em_event_group_t egrp;

	/**
	 * Holds the tmo handle in case event is used as timeout indication.
	 * Only valid if flags.tmo_type is not EM_TMO_TYPE_NONE (0).
	 * Initialized only when used as timeout indication by timer code.
	 */
	em_tmo_t tmo;

	/**
	 * Event User Area metadata
	 */
	ev_hdr_user_area_t user_area;

	/**
	 * End of event header data,
	 * for offsetof(event_hdr_t, end_hdr_data)
	 */
	uint8_t end_hdr_data[0];

	/*
	 * ! EMPTY SPACE !
	 */

	void *end[0] ODP_ALIGNED(8); /* pad to next 8B boundary */
} event_hdr_t;

COMPILE_TIME_ASSERT(sizeof(event_hdr_t) <= 64, EVENT_HDR_SIZE_ERROR);
COMPILE_TIME_ASSERT(sizeof(event_hdr_t) % sizeof(uint64_t) == 0, EVENT_HDR_SIZE_ERROR2);

/**
 * Event header used only when pre-allocating the pool during pool creation to
 * be able to link all the event headers together into a linked list.
 * Make sure not to overwrite the event state information in the header with the
 * linked list information.
 */
typedef union event_prealloc_hdr {
	event_hdr_t ev_hdr;

	struct {
		uint8_t u8[sizeof(event_hdr_t) - sizeof(list_node_t)];
		/**
		 * Pool pre-allocation: allocate and link each event in the pool into a
		 * linked list to be able to initialize the event state into a known
		 * state for ESV.
		 */
		list_node_t list_node;
	};
} event_prealloc_hdr_t;

COMPILE_TIME_ASSERT(sizeof(event_prealloc_hdr_t) == sizeof(event_hdr_t),
		    EVENT_PREALLOC_HDR_SIZE_ERROR);
COMPILE_TIME_ASSERT(offsetof(event_prealloc_hdr_t, list_node) >
		    offsetof(event_hdr_t, state) + sizeof(ev_hdr_state_t),
		    EVENT_PREALLOC_HDR_SIZE_ERROR2);
COMPILE_TIME_ASSERT(offsetof(event_prealloc_hdr_t, list_node) >
		    offsetof(event_hdr_t, event) + sizeof(em_event_t),
		    EVENT_PREALLOC_HDR_SIZE_ERROR3);

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_TYPES_H_ */
