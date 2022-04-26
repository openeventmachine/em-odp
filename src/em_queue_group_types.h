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
 *
 * EM internal queue group types & definitions
 *
 */

#ifndef EM_QUEUE_GROUP_TYPES_H_
#define EM_QUEUE_GROUP_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

COMPILE_TIME_ASSERT(EM_QUEUE_GROUP_NAME_LEN <= ODP_SCHED_GROUP_NAME_LEN,
		    EM_QUEUE_GROUP_NAME_LEN_ERROR);

COMPILE_TIME_ASSERT(sizeof(EM_QUEUE_GROUP_DEFAULT_NAME) <=
		    EM_QUEUE_GROUP_NAME_LEN,
		    EM_QUEUE_GROUP_DEFAULT_NAME_SIZE_ERROR);

COMPILE_TIME_ASSERT(sizeof(EM_QUEUE_GROUP_CORE_BASE_NAME) <=
		    EM_QUEUE_GROUP_NAME_LEN,
		    EM_QUEUE_GROUP_CORE_BASE_NAME_SIZE_ERROR);

/**
 * EM queue group element
 */
typedef struct queue_group_elem_t {
	/** EM queue group handle/id */
	em_queue_group_t queue_group;
	/** Associated ODP schedule group */
	odp_schedule_group_t odp_sched_group;
	/** EM core mask */
	em_core_mask_t core_mask;
	/** Queue list, all queues that belong to this queue group */
	list_node_t queue_list;
	/** Queue pool elem for linking free queues for queue_alloc()*/
	objpool_elem_t queue_group_pool_elem;
	/** Number of queues that belong to this queue group */
	env_atomic32_t num_queues;
	/** is queue group deletion ongoing? true/false */
	bool ongoing_delete;
	/** Queue group elem lock */
	env_spinlock_t lock;
	/** Pad to multiple of cache line size  */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} queue_group_elem_t ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(queue_group_elem_t) % ENV_CACHE_LINE_SIZE == 0,
		    QUEUE_GROUP_ELEM_T__SIZE_ERROR);

/**
 * EM queue element table
 */
typedef struct queue_group_tbl_t {
	/** Queue group element table */
	queue_group_elem_t queue_group_elem[EM_MAX_QUEUE_GROUPS];
} queue_group_tbl_t;

/**
 * Pool of free queue groups
 */
typedef struct queue_group_pool_t {
	objpool_t objpool;
} queue_group_pool_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_GROUP_TYPES_H_ */
