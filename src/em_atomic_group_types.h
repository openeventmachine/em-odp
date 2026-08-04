/*
 *   Copyright (c) 2014-2026, Nokia Solutions and Networks
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
 * EM internal atomic group types & definitions
 *
 */

#ifndef EM_ATOMIC_GROUP_TYPES_H_
#define EM_ATOMIC_GROUP_TYPES_H_

#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "misc/list.h"
#include "misc/objpool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WRAP_PROTECT 0x0FFF

typedef union {
	/** Combined lock and event count for 64-bit atomic updates */
	uint64_t lock_and_evcnt; /* accessed atomically with __atomic_...() */
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		uint32_t evcnt;
		uint16_t wrap;
		uint16_t lock;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		uint16_t lock;
		uint16_t wrap;
		uint32_t evcnt;
#endif
	} parts;
} atomic_group_dispatch_lock_t;

typedef struct {
	/** The atomic group ID (handle) */
	em_atomic_group_t atomic_group;
	/** Queue group that the atomic group belongs to */
	em_queue_group_t queue_group;
	/** AG pool elem for linking free AG:s for AG-alloc */
	objpool_elem_t atomic_group_pool_elem;
	/** Internal stashes for events belonging to this group */
	struct {
		/** for high priority events */
		odp_stash_t hi_prio;
		/** for events of all other priority levels */
		odp_stash_t lo_prio;
	} stashes;

	/** Number of EM_QUEUE_PRIO_HIGHEST queues in this atomic group */
	odp_atomic_u32_t num_hi_prio_queues;

	/** Atomic group element lock */
	odp_ticketlock_t lock ENV_CACHE_LINE_ALIGNED;

	/** Atomic group dispatch lock and event count */
	atomic_group_dispatch_lock_t dispatch_lock ENV_CACHE_LINE_ALIGNED;

	/** Number of queues that belong to this atomic group */
	odp_atomic_u32_t num_queues;

	/** List of queues (q_elems) that belong to this atomic group */
	list_node_t qlist_head;
	/** Atomic group name */
	char name[EM_ATOMIC_GROUP_NAME_LEN];

	char end[0] ENV_CACHE_LINE_ALIGNED;
} atomic_group_elem_t ENV_CACHE_LINE_ALIGNED;

/**
 * Atomic group table
 */
typedef struct {
	/** Atomic group element table */
	atomic_group_elem_t ag_elem[EM_MAX_ATOMIC_GROUPS];
} atomic_group_tbl_t;

/**
 * Pool of free atomic groups
 */
typedef struct {
	objpool_t objpool;
} atomic_group_pool_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_ATOMIC_GROUP_TYPES_H_ */
