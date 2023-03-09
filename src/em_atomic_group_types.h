/*
 *   Copyright (c) 2014, Nokia Solutions and Networks
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

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_CACHE_FLUSH 32

typedef struct {
	/** Atomic group name */
	char name[EM_ATOMIC_GROUP_NAME_LEN];
	/** The atomic group ID (handle) */
	em_atomic_group_t atomic_group;
	/** Queue group that the atomic group belongs to */
	em_queue_group_t queue_group;
	/** AG pool elem for linking free AGs for AG-alloc */
	objpool_elem_t atomic_group_pool_elem;
	/** Internal stashes for events belonging to this group */
	struct {
		/** for high priority events */
		odp_stash_t hi_prio;
		/** for events of all other priority levels */
		odp_stash_t lo_prio;
	} stashes;

	/** Atomic group element lock */
	env_spinlock_t lock ENV_CACHE_LINE_ALIGNED;
	/** List of queues (q_elems) that belong to this atomic group */
	list_node_t qlist_head;
	/** Number of queues that belong to this atomic group */
	env_atomic32_t num_queues;
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
