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

#ifndef EM_EVENT_GROUP_TYPES_H_
#define EM_EVENT_GROUP_TYPES_H_

/**
 * @file
 * EM internal event group types & definitions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Event Group counter
 */
typedef union {
	struct {
		/** Counter value */
		int32_t count;
		/** Current event group generation */
		int32_t gen;
	};
	uint64_t all;
	env_atomic64_t atomic;
} egrp_counter_t;

COMPILE_TIME_ASSERT(sizeof(egrp_counter_t) == sizeof(uint64_t),
		    EGRP_COUNTER_SIZE_ERROR);

/**
 * EM Event Group element
 */
typedef struct {
	/**
	 * Contains the notification events to send when
	 * the event group count reaches zero.
	 */
	em_notif_t notif_tbl[EM_EVENT_GROUP_MAX_NOTIF] ENV_CACHE_LINE_ALIGNED;

	/**
	 * These event group counts determine the number of events to process
	 * until completion. Events are counted before and after the receive
	 * function in the dispatcher.
	 */
	egrp_counter_t pre ENV_CACHE_LINE_ALIGNED;

	egrp_counter_t post ENV_CACHE_LINE_ALIGNED;

	union {
		struct {
			/** The number of notif events stored in notif_tbl[] */
			int num_notif;
			/** true/false, event group is ready for apply */
			uint8_t ready;
		};
		/** clear all options at once */
		uint64_t all;
	};

	/** The event group handle associated with this element */
	em_event_group_t event_group;
	/** Associated pool element for this event group */
	objpool_elem_t event_group_pool_elem;
} event_group_elem_t ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(offsetof(event_group_elem_t, all) + sizeof(uint64_t)
		    >= offsetof(event_group_elem_t, ready) + sizeof(uint8_t),
		    EVENT_GROUP_ELEM_T__SIZE_ERROR);
/**
 * Event group table
 */
typedef struct {
	/** Event group element table */
	event_group_elem_t egrp_elem[EM_MAX_EVENT_GROUPS];
} event_group_tbl_t;

/**
 * Pool of free event groups
 */
typedef struct {
	objpool_t objpool;
} event_group_pool_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_EVENT_GROUP_TYPES_H_ */
