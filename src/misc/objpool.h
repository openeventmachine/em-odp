/*
 *   Copyright (c) 2015-2026, Nokia Solutions and Networks
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

#ifndef OBJPOOL_H_
#define OBJPOOL_H_

/**
 * @file
 * Simple object pool for managing collections of objects.
 *
 * Objects stored in the pool must embed an objpool_elem_t member that is used
 * internally to link the object into the pool. The pool is divided into
 * independently locked subpools to reduce contention in multithreaded use.
 */

#include <stdint.h>

#include <event_machine/platform/env/environment.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum number of subpools in an object pool.
 *
 * This compile-time limit is set to 8 as a balance between reducing lock
 * contention (more subpools) and keeping the objpool_t structure small enough
 * to preserve cache locality. Increasing this value is possible if a
 * particular workload benefits from more independently locked subpools, but
 * doing so will grow the size of objpool_t proportionally.
 */
#define OBJSUBPOOLS_MAX 8

/**
 * Object pool element, embedded in each object stored in the pool.
 *
 * Must be included as a member of any struct that is to be managed by
 * an objpool_t.  The containing struct can be recovered with offsetof().
 */
typedef struct {
	/** node for linking into a subpool list */
	list_node_t list_node;
	/** subpool index: 0 .. objpool_t::nbr_subpools - 1 */
	uint32_t subpool_idx;
	/** in pool: 1 = yes, 0 = no */
	uint32_t in_pool;
} objpool_elem_t;

/** A single independently locked subpool within an object pool */
typedef union {
	/** Padding to half a cache line */
	uint8_t u8[ENV_CACHE_LINE_SIZE / 2];
	struct {
		/** Lock protecting this subpool */
		odp_ticketlock_t lock;
		/** Head of the free-element list */
		list_node_t list_head;
	};
} objsubpool_t;

COMPILE_TIME_ASSERT(2 * sizeof(objsubpool_t) == ENV_CACHE_LINE_SIZE,
		    OBJSUBPOOL_T_SIZE_ERROR);

/** Object pool consisting of independently locked subpools */
typedef struct {
	/** Array of subpools */
	objsubpool_t subpool[OBJSUBPOOLS_MAX];
	/** Number of subpools in use: 1 .. OBJSUBPOOLS_MAX */
	uint32_t nbr_subpools;
} objpool_t;

/**
 * Initialize an object pool.
 *
 * @param objpool      Object pool to initialize.
 * @param nbr_subpools Number of subpools, clamped to OBJSUBPOOLS_MAX.
 *
 * @return 0 on success.
 * @retval <0 on failure (invalid parameters).
 */
int objpool_init(objpool_t *const objpool, uint32_t nbr_subpools);

/**
 * Return the number of subpools in use by the object pool.
 *
 * @param objpool  Object pool.
 *
 * @return Number of subpools.
 */
uint32_t objpool_subpools(const objpool_t *const objpool);

/**
 * Add an object to the object pool.
 *
 * @param objpool     Object pool.
 * @param subpool_idx Index of the subpool to add the element to
 *                    (modulo nbr_subpools).
 * @param elem        Object pool element embedded in the object to add.
 */
void objpool_add(objpool_t *const objpool, uint32_t subpool_idx,
		 objpool_elem_t *const elem);

/**
 * Remove and return an object from the object pool.
 *
 * Tries the given subpool first, then searches the remaining subpools
 * round-robin until a free object is found.
 *
 * @param objpool     Object pool.
 * @param subpool_idx Preferred subpool index to remove from first.
 *
 * @return Pointer to the removed element, or NULL if the pool is empty.
 */
objpool_elem_t *
objpool_rem(objpool_t *const objpool, uint32_t subpool_idx);

/**
 * Remove a specific object from the object pool.
 *
 * @param objpool  Object pool.
 * @param elem     Object pool element to remove.
 *
 * @return 0 on success, -1 if the element was not in the pool.
 */
int objpool_rem_elem(objpool_t *const objpool, objpool_elem_t *const elem);

/**
 * Check whether an object is currently stored in an object pool.
 *
 * @param elem  Object pool element to check.
 *
 * @return 1 if in pool, 0 if not.
 */
static inline int
objpool_in_pool(const objpool_elem_t *elem)
{
	return elem->in_pool;
}

#ifdef __cplusplus
}
#endif

#endif /* OBJPOOL_H_ */
