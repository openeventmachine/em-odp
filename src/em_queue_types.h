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
  * EM internal queue types & definitions
  *
  */

#ifndef EM_QUEUE_TYPES_H_
#define EM_QUEUE_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EM internal queue ids - local part of the queue only, i.e missing the
 * device-id.
 * Note that the EM queue handle range is determined by 'EM_QUEUE_RANGE_OFFSET'
 */
#define MAX_INTERNAL_QUEUES   ROUND_UP(EM_MAX_CORES + 1, 32)

#define _FIRST_INTERNAL_QUEUE (_EM_QUEUE_STATIC_MAX + 1)
#define FIRST_INTERNAL_QUEUE  ((uint16_t)_FIRST_INTERNAL_QUEUE)

#define _LAST_INTERNAL_QUEUE  (_FIRST_INTERNAL_QUEUE + MAX_INTERNAL_QUEUES - 1)
#define LAST_INTERNAL_QUEUE   ((uint16_t)_LAST_INTERNAL_QUEUE)

#define FIRST_INTERNAL_UNSCHED_QUEUE   (FIRST_INTERNAL_QUEUE)
#define SHARED_INTERNAL_UNSCHED_QUEUE  (LAST_INTERNAL_QUEUE)

/* Priority for the EM-internal queues */
#define INTERNAL_QUEUE_PRIORITY (EM_QUEUE_PRIO_HIGHEST)

COMPILE_TIME_ASSERT(MAX_INTERNAL_QUEUES - 1 >= EM_MAX_CORES,
		    TOO_FEW_INTERNAL_QUEUES_ERROR);

/* Dynamic queue ids */
#define _FIRST_DYN_QUEUE (_LAST_INTERNAL_QUEUE + 1)
#define FIRST_DYN_QUEUE  ((uint16_t)_FIRST_DYN_QUEUE)

#define MAX_DYN_QUEUES  (EM_MAX_QUEUES - \
			 (_FIRST_DYN_QUEUE - EM_QUEUE_RANGE_OFFSET))

#define _LAST_DYN_QUEUE  (_FIRST_DYN_QUEUE + MAX_DYN_QUEUES - 1)
#define LAST_DYN_QUEUE   ((uint16_t)_LAST_DYN_QUEUE)

COMPILE_TIME_ASSERT(_FIRST_DYN_QUEUE > _LAST_INTERNAL_QUEUE,
		    FIRST_DYN_QUEUE_ERROR);

#define QUEUE_ELEM_VALID ((uint16_t)0xCAFE)

/* Verify that the byte order is defined for 'internal_queue_t' */
#if \
(__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__) && \
(__BYTE_ORDER__ != __ORDER_BIG_ENDIAN__)
#error __BYTE_ORDER__ not defined!
#endif

/**
 * Queue create-params passed to queue_setup...()
 */
typedef struct {
	const char *name;
	em_queue_type_t type;
	em_queue_prio_t prio;
	em_atomic_group_t atomic_group;
	em_queue_group_t queue_group;
	const em_queue_conf_t *conf;
} queue_setup_t;

/**
 * Internal represenation of the EM queue handle
 * The EM queue handle contains a 16-bit queue-id and a 16-bit device-id.
 */
typedef union {
	em_queue_t queue;
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		uint16_t queue_id;
		uint16_t device_id;
#ifdef EM_64_BIT
		uint32_t unused;
#endif
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#ifdef EM_64_BIT
		uint32_t unused;
#endif
		uint16_t device_id;
		uint16_t queue_id;
#endif
	};
} internal_queue_t;

/* Assert that all EM queues can fit into the .queue_id field */
COMPILE_TIME_ASSERT(UINT16_MAX >= EM_MAX_QUEUES,
		    INTERNAL_QUEUE_ID_MAX_ERROR);
/* Verify size of struct, i.e. accept no padding */
COMPILE_TIME_ASSERT(sizeof(internal_queue_t) == sizeof(em_queue_t),
		    INTERNAL_QUEUE_T_SIZE_ERROR);

/**
 * Queue state
 */
typedef enum queue_state {
	/** Invalid queue state, queue not created/allocated */
	EM_QUEUE_STATE_INVALID = 0,

	/*
	 * Scheduled queue (ATOMIC, PARALLEL, ORDERED) states:
	 * (keep state values consecutive: ...n-1,n,n+1...)
	 */
	/** Queue initialization, allocated and being set up */
	EM_QUEUE_STATE_INIT = 1,
	/** Queue added/bound to an EO, but EO-start not yet complete */
	EM_QUEUE_STATE_BIND = 2,
	/** Queue ready, related EO started  */
	EM_QUEUE_STATE_READY = 3,

	/*
	 * Non-scheduled queue (UNSCHED, OUTPUT) state use the UNSCHEDULED-state.
	 */
	/* Use separete value for unscheduled queues to catch illegal usage */
	EM_QUEUE_STATE_UNSCHEDULED = 255
} queue_state_e;
/**
 * Queue state, packed into uint8_t.
 *
 * The 'enum queue_state' or queue_state_e will always fit into an uint8_t.
 * Save space in the queue_elem_t by using this instead.
 */
typedef uint8_t queue_state_t;

/**
 * Atomic-group queue specific part of the queue element
 */
typedef struct q_elem_atomic_group_ {
	/** The atomic group handle (if any) of this queue */
	em_atomic_group_t atomic_group;
	/** List node for linking queue elems belonging to an atomic group */
	list_node_t agrp_node;
} q_elem_atomic_group_t;

/**
 * Output queue specific part of the queue element
 */
typedef struct q_elem_output_ {
	/** Output Queue config, incl. output_fn(..., output_fn_args) */
	em_output_queue_conf_t output_conf;
	/** Copied output_fn_args content of length 'args_len' stored in event */
	em_event_t output_fn_args_event;
	/** Lock for output queues during an ordered-context */
	env_spinlock_t lock;
} q_elem_output_t;

/**
 * EM queue element
 */
typedef struct queue_elem_t {
	/**
	 * Check that contents is an EM queue elem.
	 *
	 * EM will verify that the ODP queue context actually points to an
	 * EM queue elem and not to something else:
	 *     queue_elem_t *q_elem = odp_queue_context(odp_queue);
	 *     if (!q_elem || q_elem->valid_check != QUEUE_ELEM_VALID)
	 *             EM_ERROR(...);
	 * Keep first.
	 */
	uint16_t valid_check;

	union {
		uint8_t all;
		struct {
			/* true:receive_multi_func(), false:receive_func() */
			uint8_t use_multi_rcv   : 1;
			/** set if queue is scheduled, i.e. atomic, parallel or ordered */
			uint8_t scheduled       : 1;
			/** Does this queue belong to an EM Atomic Group (true/false)? */
			uint8_t in_atomic_group : 1;
			/** Is this an ODP pktin event queue (true/false)? */
			uint8_t is_pktin        : 1;
			/** reserved bits */
			uint8_t rsvd            : 4;
		};
	} flags;

	/** Queue state */
	queue_state_t state; /* queue_state_e */

	/** Queue priority */
	uint8_t priority; /* em_queue_prio_t */

	/** Atomic, parallel, ordered, unscheduled, local, output */
	uint8_t type; /* em_queue_type_t */

	/** Max number of events passed to the EO's multi-event receive function */
	uint16_t max_events; /* only used if flags.use_multi_rcv == true */

	/** EM EO that this queue belongs to */
	uint16_t eo; /* em_eo_t */

	/** Queue handle */
	uint32_t queue; /* em_queue_t */

	/** Associated ODP queue handle */
	odp_queue_t odp_queue;

	/** User defined queue context (can be NULL) */
	void *context;

	union {
		/** Copy of the event receive function for better performance */
		em_receive_func_t receive_func;
		/** Copy of the multi-event receive function for better performance */
		em_receive_multi_func_t receive_multi_func;
	};

	/** Copy of the user defined eo context (or NULL) for performance */
	void *eo_ctx;

	union {
		q_elem_atomic_group_t agrp;
		q_elem_output_t output;
	};

	/** Associated eo element */
	eo_elem_t *eo_elem;

	/** Queue group handle of this queue */
	em_queue_group_t queue_group;

	/** List node for linking queue elems belonging to an EO */
	list_node_t queue_node;
	/** List node for linking queue elems belonging to a queue group */
	list_node_t qgrp_node;
	/** Queue pool elem for linking free queues for queue_alloc() */
	objpool_elem_t queue_pool_elem;

	/** Guarantee that size is a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} queue_elem_t ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(queue_elem_t) % ENV_CACHE_LINE_SIZE == 0,
		    QUEUE_ELEM_T__SIZE_ERROR);

/**
 * EM queue element table
 */
typedef struct queue_tbl_t {
	/** Queue element table */
	queue_elem_t queue_elem[EM_MAX_QUEUES] ENV_CACHE_LINE_ALIGNED;
	/** ODP queue capabilities common for all queues */
	odp_queue_capability_t odp_queue_capability;
	/** ODP schedule capabilities related to queues */
	odp_schedule_capability_t odp_schedule_capability;
	/** Queue name table */
	char name[EM_MAX_QUEUES][EM_QUEUE_NAME_LEN] ENV_CACHE_LINE_ALIGNED;
} queue_tbl_t;

/**
 * Pool of free queues
 */
typedef struct queue_pool_t {
	objpool_t objpool;
} queue_pool_t;

/**
 * Local queues, i.e. core-local storage for events to local queues
 */
typedef struct local_queues_t {
	int empty;
	struct {
		int empty_prio;
		odp_stash_t stash;
	} prio[EM_QUEUE_PRIO_NUM];
} local_queues_t;

/**
 * Track output-queues used during a dispatch round (burst)
 */
typedef struct output_queue_track_t {
	unsigned int idx_cnt;
	uint16_t idx[EM_MAX_QUEUES];
	queue_elem_t *used_queues[EM_MAX_QUEUES];
} output_queue_track_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_QUEUE_TYPES_H_ */
