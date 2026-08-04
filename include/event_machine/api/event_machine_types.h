/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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

#ifndef EVENT_MACHINE_TYPES_H_
#define EVENT_MACHINE_TYPES_H_

#pragma GCC visibility push(default)

/**
 * @file
 *
 * Event Machine basic types and defines
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

#include <odp/api/cpumask.h>

#include <event_machine/api/event_machine_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EM boolean value: True */
#define EM_TRUE   1
/** EM boolean value: False */
#define EM_FALSE  0

/** EM undefined u64 */
#define EM_UNDEF_U64  0
/** EM undefined u32 */
#define EM_UNDEF_U32  0
/** EM undefined u16 */
#define EM_UNDEF_U16  0
/** EM undefined u8 */
#define EM_UNDEF_U8   0
/** EM undefined uintptr */
#define EM_UNDEF_UINTPTR  EM_STATIC_CAST(uintptr_t, NULL)
/** EM undefined pointer */
#ifndef __cplusplus
#define EM_UNDEF_PTR  NULL
#else
#define EM_UNDEF_PTR  nullptr
#endif

/**
 * @def EM_HANDLE_T
 * Define 'type_t' as a struct ptr to improve type safety
 */
#define EM_HANDLE_T(type_t) \
	typedef struct _##type_t { \
		void *unused; \
	} *(type_t)

/**
 * @def EM_HDL_UNDEF
 * Undefined EM-handle
 */
#ifndef __cplusplus
#define EM_HDL_UNDEF NULL
#else
#define EM_HDL_UNDEF nullptr
#endif

/**
 * @def PRI_HDL
 * EM-handle printf format
 */
#define PRI_HDL "p"

/**
 * @typedef em_event_t
 * Event handle
 */
EM_HANDLE_T(em_event_t);

/** Undefined event */
#define EM_EVENT_UNDEF  EM_STATIC_CAST(em_event_t, EM_HDL_UNDEF)
/** em_event_t printf format */
#define PRI_EVENT  PRI_HDL

/**
 * @typedef em_pool_t
 * Memory/Event Pool handle.
 *
 * Defines the memory pool e.g. used in em_alloc() and
 * created by em_pool_create().
 * The default pool is defined by EM_POOL_DEFAULT.
 */
EM_HANDLE_T(em_pool_t);
/** Undefined EM pool */
#define EM_POOL_UNDEF  EM_STATIC_CAST(em_pool_t, EM_HDL_UNDEF)
/** em_pool_t printf format */
#define PRI_POOL  PRI_HDL

/**
 * @typedef em_eo_t
 * Execution Object handle
 *
 * @see em_eo_create()
 */
EM_HANDLE_T(em_eo_t);
/** Undefined EO */
#define EM_EO_UNDEF  EM_STATIC_CAST(em_eo_t, EM_HDL_UNDEF)
/** em_eo_t printf format */
#define PRI_EO  PRI_HDL

/**
 * @typedef em_queue_t
 * Queue handle
 *
 * @see em_queue_create(), em_receive_func_t(), em_send()
 */
EM_HANDLE_T(em_queue_t);
/** Undefined queue */
#define EM_QUEUE_UNDEF  EM_STATIC_CAST(em_queue_t, EM_HDL_UNDEF)
/** em_queue_t printf format */
#define PRI_QUEUE  PRI_HDL

/**
 * @typedef em_queue_group_t
 * Queue Group handle
 *
 * Each queue belongs to one queue group that defines a core mask for
 * scheduling events, i.e. defines which cores participate in load balancing.
 * A queue group can also allow only a single core for no load balancing.
 *
 * Queue groups need to be created as needed. One default queue group, i.e.
 * EM_QUEUE_GROUP_DEFAULT, always exists, and that allows scheduling to all the
 * EM cores running on the EM instance.
 *
 * @see em_queue_group_create()
 */
EM_HANDLE_T(em_queue_group_t);
/** Undefined queue group */
#define EM_QUEUE_GROUP_UNDEF  EM_STATIC_CAST(em_queue_group_t, EM_HDL_UNDEF)
/** em_queue_group_t printf format */
#define PRI_QGRP  PRI_HDL

/**
 * @typedef em_event_group_t
 * Event Group handle
 *
 * Event groups are used for fork-join event handling.
 *
 * @see em_event_group_create()
 */
EM_HANDLE_T(em_event_group_t);
/** Undefined event group */
#define EM_EVENT_GROUP_UNDEF  EM_STATIC_CAST(em_event_group_t, EM_HDL_UNDEF)
/** em_event_group_t printf format */
#define PRI_EGRP  PRI_HDL

/**
 * @typedef em_atomic_group_t
 * Atomic Group handle
 *
 * Atomic groups combine multiple atomic queues into one atomically
 * scheduled group.
 *
 * @see em_atomic_group_create()
 */
EM_HANDLE_T(em_atomic_group_t);
/** Undefined atomic group */
#define EM_ATOMIC_GROUP_UNDEF  EM_STATIC_CAST(em_atomic_group_t, EM_HDL_UNDEF)
/** em_atomic_group_t printf format */
#define PRI_AGRP  PRI_HDL

/**
 * @typedef em_event_type_t
 * Event type
 *
 * The event type is given to the EO-receive function for each received event
 * and is also needed for event allocation. This type is an unsigned integer
 * split into major and minor parts:
 *   1) the major-field categorizes the event and
 *   2) the minor is a more detailed system specific description.
 * The major-part will not change by HW, but the minor-part can be
 * HW/SW platform specific and thus could be split into more sub-fields as
 * needed. The application should use the access functions for reading major
 * and minor parts.
 *
 * The only event type with defined content is EM_EVENT_TYPE_SW with
 * minor type 0, which needs to be portable (direct pointer to data).
 *
 * @see em_event_type_major_e, em_event_type_sw_minor_e for named type constants
 * @see em_event_type_major(), em_event_type_minor(), em_receive_func_t()
 */
typedef uint32_t em_event_type_t;

/**
 * Notification
 *
 * A notification structure allows the user to define a notification event and
 * a destination queue with an optional event group. EM will notify the user by
 * sending the event into the given queue.
 *
 * The egroup-field defines an optional event group for this notification.
 * The used event group has to exist and be initialized. Use value
 * EM_EVENT_GROUP_UNDEF for normal operation (API 1.0 functionality), i.e.
 * notification is not sent to a group.
 * egroup should not be the originating group, i.e. should not be sent
 * back to the group.
 *
 * @attention API 1.0 code using notifications may need to be modified
 * as the new field need to be initialized. Value EM_EVENT_GROUP_UNDEF
 * is the correct value to use for non-group notification but value 0
 * is an alias, e.g. it is safe to initialize the structure with memset(0,..).
 */
typedef struct {
	em_event_t event;  /**< User defined notification event */
	em_queue_t queue;  /**< Destination queue */
	em_event_group_t egroup; /**< Event group for this event */
} em_notif_t;

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_TYPES_H_ */
