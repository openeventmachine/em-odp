/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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

#ifndef EVENT_MACHINE_TYPES_H_
#define EVENT_MACHINE_TYPES_H_

#pragma GCC visibility push(default)

/**
 * @file
 *
 * Event Machine basic types
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

/** EM boolean values. */
#define EM_TRUE   1 /**< True */
#define EM_FALSE  0 /**< False */

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
 * @typedef em_event_type_t
 * Event type
 *
 * The event type is given to the EO-receive function for each received event
 * and is also needed for event allocation. This type is an integer that is
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
 * @see em_get_type_major(), em_get_type_minor(), em_receive_func_t()
 */
typedef uint32_t em_event_type_t;

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
 * EM cores running this execution binary instance.
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
 * This is used for fork-join event handling.
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
 * This is used to combine multiple atomic queues into one
 * atomically scheduled group.
 *
 * @see em_atomic_group_create()
 */
EM_HANDLE_T(em_atomic_group_t);
/** Undefined atomic group */
#define EM_ATOMIC_GROUP_UNDEF  EM_STATIC_CAST(em_atomic_group_t, EM_HDL_UNDEF)
/** em_atomic_group_t printf format */
#define PRI_AGRP  PRI_HDL

/**
 * @typedef em_queue_type_t
 * Queue type.
 *
 * Affects the scheduling principle
 *
 * @see em_queue_create(), event_machine_hw_config.h
 */
typedef uint32_t em_queue_type_t;
#define PRI_QTYPE  PRIu32

/**
 * @typedef em_queue_prio_t
 * Queue priority
 *
 * Queue priority defines implementation specific QoS class for event
 * scheduling. Priority is an integer in range 0 (lowest) to num priorities - 1.
 * Note, that the exact scheduling rules are not defined by EM and all available
 * priorities may not be relative to the adjacent one (e.g. using dynamic
 * priority, rate limiting or other more complex scheduling discipline).
 * There are 5 generic predefined values (em_queue_prio_e) mapped to available
 * runtime priorities for portability.
 *
 * @see em_queue_create(), em_queue_get_num_prio(), event_machine_hw_config.h,
 *      em_queue_prio_e
 */
typedef uint32_t em_queue_prio_t;
#define PRI_QPRIO  PRIu32

/**
 * Type for queue flags.
 *
 * This is an unsigned integer with defined flags, that can be combined by
 * bitwise 'OR' only. EM_QUEUE_FLAG_DEFAULT can be used in most cases.
 * Unused bits must be set to zero. The actual values are system specific, but
 * the implementation need to define at least: EM_QUEUE_FLAG_DEFAULT,
 * EM_QUEUE_FLAG_BLOCKING, EM_QUEUE_FLAG_NONBLOCKING_LF and
 * EM_QUEUE_FLAG_NONBLOCKING_WF even if those would not be supported.
 **/
typedef uint32_t em_queue_flag_t;
/**
 * @def EM_QUEUE_FLAG_MASK
 * The low 16 bits are reserved for EM, the upper bits are free
 * for system-specific use.
 */
#define EM_QUEUE_FLAG_MASK 0x0000FFFF

/**
 * Queue configuration data for queue-create APIs. The use of this conf is
 * optional, but provides a standard way to pass extra parameters or specify
 * extra requirements.
 **/
typedef struct {
	/**
	 * Extra flags. See em_queue_flag_t for choices.
	 * EM_QUEUE_FLAG_DEFAULT is defined by all systems and indicates a
	 * default multithread-safe queue without any special guarantees.
	 **/
	em_queue_flag_t flags;
	/**
	 * Request for a minimum amount of events the queue can hold or use
	 * 0 for EM default value. Queue creation will fail, if the system
	 * cannot support the requested amount.
	 **/
	unsigned int min_events;
	/**
	 * Size of the data passed via 'conf'. 'conf' is ignored,
	 * if 'conf_len' is 0.
	 **/
	size_t conf_len;
	/**
	 * Extra queue configuration data. This can also work
	 * as a placeholder for directly attached extra data.
	 **/
	void *conf;
} em_queue_conf_t;

/**
 * EO configuration data via em_eo_start. The use of this is
 * optional, but provides a standard way to pass data to EO start.
 * EM does not dereference any of the fields here.
 **/
typedef struct {
	/** Size of the data passed via conf pointer */
	size_t conf_len;
	/** Application specific configuration data */
	void *conf;
} em_eo_conf_t;

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

/**
 * Scheduling context types
 */
typedef enum {
	/**
	 * Parallel or released context
	 */
	EM_SCHED_CONTEXT_TYPE_NONE = 0,
	/**
	 * Atomic context
	 */
	EM_SCHED_CONTEXT_TYPE_ATOMIC = 1,
	/**
	 * Ordered context
	 */
	EM_SCHED_CONTEXT_TYPE_ORDERED = 2
} em_sched_context_type_t;

/**
 * EO running state. Event dispatching is only enabled in running state.
 **/
typedef enum {
	/** Undefined */
	EM_EO_STATE_UNDEF = 0,
	/** Initial state after creation */
	EM_EO_STATE_CREATED = 1,
	/** start called, not completed */
	EM_EO_STATE_STARTING = 2,
	/** running, event dispatching enabled */
	EM_EO_STATE_RUNNING = 3,
	/** stop called, not completed. Next state EM_EO_STATE_CREATED */
	EM_EO_STATE_STOPPING = 4,
	/** exceptional state, only delete allowed */
	EM_EO_STATE_ERROR = 5
} em_eo_state_t;

/**
 * @typedef em_status_t
 * Error/Status code.
 *
 * EM_OK (0) is the general code for success, other values
 * describe failed operation.
 * There is a generic error code EM_ERROR, but application should
 * normally test for not equal to EM_OK.
 *
 * @see event_machine_hw_config.h, em_error_handler_t(), em_error()
 */
typedef uint32_t em_status_t;
#define PRI_STAT  PRIu32
#define PRIxSTAT  PRIx32

/**
 * @def EM_OK
 * Operation successful
 */
#define EM_OK    0

/**
 * @def EM_ERROR
 * Operation not successful.
 *
 * Generic error code, other error codes are system specific.
 */
#define EM_ERROR 0xffffffff

/**
 * @typedef em_escope_t
 * Error scope.
 *
 * Identifies the error scope for interpreting error codes and variable
 * arguments.
 *
 * @see em_error_handler_t(), em_error()
 */
typedef uint32_t em_escope_t;
#define PRI_ESCOPE  PRIu32

/**
 * @def EM_ESCOPE_BIT
 * All EM internal error scopes should have bit 31 set
 *
 * NOTE: High bit is RESERVED for EM internal escopes and should not be
 * used by the application.
 */
#define EM_ESCOPE_BIT         (0x80000000u)

/**
 * @def EM_ESCOPE
 * Test if the error scope identifies an EM function (API or other internal)
 */
#define EM_ESCOPE(escope)     (EM_ESCOPE_BIT & (escope))

/**
 * @def EM_ESCOPE_MASK
 * Mask selects the high byte of the 32-bit escope
 */
#define EM_ESCOPE_MASK        (0xFF000000)

/**
 * @def EM_ESCOPE_API_TYPE
 * EM API functions error scope
 */
#define EM_ESCOPE_API_TYPE    (0xFFu)

/**
 * @def EM_ESCOPE_API_MASK
 * EM API functions error mask
 */
#define EM_ESCOPE_API_MASK    (EM_ESCOPE_BIT | (EM_ESCOPE_API_TYPE << 24))

/**
 * @def EM_ESCOPE_API
 * Test if the error scope identifies an EM API function
 */
#define EM_ESCOPE_API(escope) (((escope) & EM_ESCOPE_MASK) == \
				EM_ESCOPE_API_MASK)

/*
 * EM API functions error scopes:
 */

/* EM API escopes: Atomic Group */
#define EM_ESCOPE_ATOMIC_GROUP_CREATE             (EM_ESCOPE_API_MASK | 0x0001)
#define EM_ESCOPE_ATOMIC_GROUP_DELETE             (EM_ESCOPE_API_MASK | 0x0002)
#define EM_ESCOPE_QUEUE_CREATE_AG                 (EM_ESCOPE_API_MASK | 0x0003)
#define EM_ESCOPE_QUEUE_CREATE_STATIC_AG          (EM_ESCOPE_API_MASK | 0x0004)
#define EM_ESCOPE_ATOMIC_GROUP_GET                (EM_ESCOPE_API_MASK | 0x0005)
#define EM_ESCOPE_ATOMIC_GROUP_GET_NAME           (EM_ESCOPE_API_MASK | 0x0006)
#define EM_ESCOPE_ATOMIC_GROUP_FIND               (EM_ESCOPE_API_MASK | 0x0007)
#define EM_ESCOPE_ATOMIC_GROUP_GET_FIRST          (EM_ESCOPE_API_MASK | 0x0008)
#define EM_ESCOPE_ATOMIC_GROUP_GET_NEXT           (EM_ESCOPE_API_MASK | 0x0009)
#define EM_ESCOPE_ATOMIC_GROUP_QUEUE_GET_FIRST    (EM_ESCOPE_API_MASK | 0x000A)
#define EM_ESCOPE_ATOMIC_GROUP_QUEUE_GET_NEXT     (EM_ESCOPE_API_MASK | 0x000B)

/* EM API escopes: Core */
#define EM_ESCOPE_CORE_ID                         (EM_ESCOPE_API_MASK | 0x0101)
#define EM_ESCOPE_CORE_COUNT                      (EM_ESCOPE_API_MASK | 0x0102)

/* EM API escopes: Dispatcher */
#define EM_ESCOPE_DISPATCH                        (EM_ESCOPE_API_MASK | 0x0201)
#define EM_ESCOPE_DISPATCH_REGISTER_ENTER_CB      (EM_ESCOPE_API_MASK | 0x0202)
#define EM_ESCOPE_DISPATCH_UNREGISTER_ENTER_CB    (EM_ESCOPE_API_MASK | 0x0203)
#define EM_ESCOPE_DISPATCH_REGISTER_EXIT_CB       (EM_ESCOPE_API_MASK | 0x0204)
#define EM_ESCOPE_DISPATCH_UNREGISTER_EXIT_CB     (EM_ESCOPE_API_MASK | 0x0205)

/* EM API escopes: EO */
#define EM_ESCOPE_EO_CREATE                       (EM_ESCOPE_API_MASK | 0x0301)
#define EM_ESCOPE_EO_CREATE_MULTIRCV              (EM_ESCOPE_API_MASK | 0x0302)
#define EM_ESCOPE_EO_MULTIRCV_PARAM_INIT          (EM_ESCOPE_API_MASK | 0x0303)
#define EM_ESCOPE_EO_DELETE                       (EM_ESCOPE_API_MASK | 0x0304)
#define EM_ESCOPE_EO_GET_NAME                     (EM_ESCOPE_API_MASK | 0x0305)
#define EM_ESCOPE_EO_FIND                         (EM_ESCOPE_API_MASK | 0x0306)
#define EM_ESCOPE_EO_ADD_QUEUE                    (EM_ESCOPE_API_MASK | 0x0307)
#define EM_ESCOPE_EO_ADD_QUEUE_SYNC               (EM_ESCOPE_API_MASK | 0x0308)
#define EM_ESCOPE_EO_REMOVE_QUEUE                 (EM_ESCOPE_API_MASK | 0x0309)
#define EM_ESCOPE_EO_REMOVE_QUEUE_SYNC            (EM_ESCOPE_API_MASK | 0x030A)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL             (EM_ESCOPE_API_MASK | 0x030B)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC        (EM_ESCOPE_API_MASK | 0x030C)
#define EM_ESCOPE_EO_REGISTER_ERROR_HANDLER       (EM_ESCOPE_API_MASK | 0x030D)
#define EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER     (EM_ESCOPE_API_MASK | 0x030E)
#define EM_ESCOPE_EO_START                        (EM_ESCOPE_API_MASK | 0x030F)
#define EM_ESCOPE_EO_START_SYNC                   (EM_ESCOPE_API_MASK | 0x0310)
#define EM_ESCOPE_EO_STOP                         (EM_ESCOPE_API_MASK | 0x0311)
#define EM_ESCOPE_EO_STOP_SYNC                    (EM_ESCOPE_API_MASK | 0x0312)
#define EM_ESCOPE_EO_CURRENT                      (EM_ESCOPE_API_MASK | 0x0313)
#define EM_ESCOPE_EO_GET_CONTEXT                  (EM_ESCOPE_API_MASK | 0x0314)
#define EM_ESCOPE_EO_GET_FIRST                    (EM_ESCOPE_API_MASK | 0x0315)
#define EM_ESCOPE_EO_GET_NEXT                     (EM_ESCOPE_API_MASK | 0x0316)
#define EM_ESCOPE_EO_GET_STATE                    (EM_ESCOPE_API_MASK | 0x0317)
#define EM_ESCOPE_EO_QUEUE_GET_FIRST              (EM_ESCOPE_API_MASK | 0x0318)
#define EM_ESCOPE_EO_QUEUE_GET_NEXT               (EM_ESCOPE_API_MASK | 0x0319)

/* EM API escopes: Error */
#define EM_ESCOPE_REGISTER_ERROR_HANDLER          (EM_ESCOPE_API_MASK | 0x0401)
#define EM_ESCOPE_UNREGISTER_ERROR_HANDLER        (EM_ESCOPE_API_MASK | 0x0402)
#define EM_ESCOPE_ERROR                           (EM_ESCOPE_API_MASK | 0x0403)

/* EM API escopes: Event Group */
#define EM_ESCOPE_EVENT_GROUP_CREATE              (EM_ESCOPE_API_MASK | 0x0501)
#define EM_ESCOPE_EVENT_GROUP_DELETE              (EM_ESCOPE_API_MASK | 0x0502)
#define EM_ESCOPE_EVENT_GROUP_APPLY               (EM_ESCOPE_API_MASK | 0x0503)
#define EM_ESCOPE_EVENT_GROUP_INCREMENT           (EM_ESCOPE_API_MASK | 0x0504)
#define EM_ESCOPE_EVENT_GROUP_CURRENT             (EM_ESCOPE_API_MASK | 0x0505)
#define EM_ESCOPE_EVENT_GROUP_IS_READY            (EM_ESCOPE_API_MASK | 0x0506)
#define EM_ESCOPE_SEND_GROUP                      (EM_ESCOPE_API_MASK | 0x0507)
#define EM_ESCOPE_SEND_GROUP_MULTI                (EM_ESCOPE_API_MASK | 0x0508)
#define EM_ESCOPE_EVENT_GROUP_PROCESSING_END      (EM_ESCOPE_API_MASK | 0x0509)
#define EM_ESCOPE_EVENT_GROUP_ASSIGN              (EM_ESCOPE_API_MASK | 0x050A)
#define EM_ESCOPE_EVENT_GROUP_ABORT               (EM_ESCOPE_API_MASK | 0x050B)
#define EM_ESCOPE_EVENT_GROUP_GET_NOTIF           (EM_ESCOPE_API_MASK | 0x050C)
#define EM_ESCOPE_EVENT_GROUP_GET_FIRST           (EM_ESCOPE_API_MASK | 0x050D)
#define EM_ESCOPE_EVENT_GROUP_GET_NEXT            (EM_ESCOPE_API_MASK | 0x050E)

/* EM API escopes: Event */
#define EM_ESCOPE_ALLOC                           (EM_ESCOPE_API_MASK | 0x0601)
#define EM_ESCOPE_ALLOC_MULTI                     (EM_ESCOPE_API_MASK | 0x0602)
#define EM_ESCOPE_FREE                            (EM_ESCOPE_API_MASK | 0x0603)
#define EM_ESCOPE_FREE_MULTI                      (EM_ESCOPE_API_MASK | 0x0604)
#define EM_ESCOPE_SEND                            (EM_ESCOPE_API_MASK | 0x0605)
#define EM_ESCOPE_SEND_MULTI                      (EM_ESCOPE_API_MASK | 0x0606)
#define EM_ESCOPE_EVENT_POINTER                   (EM_ESCOPE_API_MASK | 0x0607)
#define EM_ESCOPE_EVENT_GET_SIZE                  (EM_ESCOPE_API_MASK | 0x0608)
#define EM_ESCOPE_EVENT_GET_POOL                  (EM_ESCOPE_API_MASK | 0x0609)
#define EM_ESCOPE_EVENT_SET_TYPE                  (EM_ESCOPE_API_MASK | 0x060A)
#define EM_ESCOPE_EVENT_GET_TYPE                  (EM_ESCOPE_API_MASK | 0x060B)
#define EM_ESCOPE_EVENT_GET_TYPE_MULTI            (EM_ESCOPE_API_MASK | 0x060C)
#define EM_ESCOPE_EVENT_SAME_TYPE_MULTI           (EM_ESCOPE_API_MASK | 0x060D)
#define EM_ESCOPE_EVENT_MARK_SEND                 (EM_ESCOPE_API_MASK | 0x060E)
#define EM_ESCOPE_EVENT_UNMARK_SEND               (EM_ESCOPE_API_MASK | 0x060F)
#define EM_ESCOPE_EVENT_MARK_FREE                 (EM_ESCOPE_API_MASK | 0x0610)
#define EM_ESCOPE_EVENT_UNMARK_FREE               (EM_ESCOPE_API_MASK | 0x0611)
#define EM_ESCOPE_EVENT_MARK_FREE_MULTI           (EM_ESCOPE_API_MASK | 0x0612)
#define EM_ESCOPE_EVENT_UNMARK_FREE_MULTI         (EM_ESCOPE_API_MASK | 0x0613)
#define EM_ESCOPE_EVENT_CLONE                     (EM_ESCOPE_API_MASK | 0x0614)
#define EM_ESCOPE_EVENT_UAREA_GET                 (EM_ESCOPE_API_MASK | 0x0615)
#define EM_ESCOPE_EVENT_UAREA_ID_GET              (EM_ESCOPE_API_MASK | 0x0616)
#define EM_ESCOPE_EVENT_UAREA_ID_SET              (EM_ESCOPE_API_MASK | 0x0617)
#define EM_ESCOPE_EVENT_UAREA_INFO                (EM_ESCOPE_API_MASK | 0x0618)

/* EM API escopes: Queue Group */
#define EM_ESCOPE_QUEUE_GROUP_CREATE              (EM_ESCOPE_API_MASK | 0x0701)
#define EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC         (EM_ESCOPE_API_MASK | 0x0702)
#define EM_ESCOPE_QUEUE_GROUP_DELETE              (EM_ESCOPE_API_MASK | 0x0703)
#define EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC         (EM_ESCOPE_API_MASK | 0x0704)
#define EM_ESCOPE_QUEUE_GROUP_MODIFY              (EM_ESCOPE_API_MASK | 0x0705)
#define EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC         (EM_ESCOPE_API_MASK | 0x0706)
#define EM_ESCOPE_QUEUE_GROUP_FIND                (EM_ESCOPE_API_MASK | 0x0707)
#define EM_ESCOPE_QUEUE_GROUP_MASK                (EM_ESCOPE_API_MASK | 0x0708)
#define EM_ESCOPE_QUEUE_GROUP_GET_NAME            (EM_ESCOPE_API_MASK | 0x0709)
#define EM_ESCOPE_QUEUE_GROUP_GET_FIRST           (EM_ESCOPE_API_MASK | 0x070A)
#define EM_ESCOPE_QUEUE_GROUP_GET_NEXT            (EM_ESCOPE_API_MASK | 0x070B)
#define EM_ESCOPE_QUEUE_GROUP_QUEUE_GET_FIRST     (EM_ESCOPE_API_MASK | 0x070C)
#define EM_ESCOPE_QUEUE_GROUP_QUEUE_GET_NEXT      (EM_ESCOPE_API_MASK | 0x070D)

/* EM API escopes: Queue */
#define EM_ESCOPE_QUEUE_CREATE                    (EM_ESCOPE_API_MASK | 0x0801)
#define EM_ESCOPE_QUEUE_CREATE_STATIC             (EM_ESCOPE_API_MASK | 0x0802)
#define EM_ESCOPE_QUEUE_DELETE                    (EM_ESCOPE_API_MASK | 0x0803)
#define EM_ESCOPE_QUEUE_SET_CONTEXT               (EM_ESCOPE_API_MASK | 0x0804)
#define EM_ESCOPE_QUEUE_GET_CONTEXT               (EM_ESCOPE_API_MASK | 0x0805)
#define EM_ESCOPE_QUEUE_GET_NAME                  (EM_ESCOPE_API_MASK | 0x0806)
#define EM_ESCOPE_QUEUE_GET_PRIORITY              (EM_ESCOPE_API_MASK | 0x0807)
#define EM_ESCOPE_QUEUE_GET_TYPE                  (EM_ESCOPE_API_MASK | 0x0808)
#define EM_ESCOPE_QUEUE_GET_GROUP                 (EM_ESCOPE_API_MASK | 0x0809)
#define EM_ESCOPE_QUEUE_FIND                      (EM_ESCOPE_API_MASK | 0x080A)
#define EM_ESCOPE_QUEUE_DEQUEUE                   (EM_ESCOPE_API_MASK | 0x080B)
#define EM_ESCOPE_QUEUE_DEQUEUE_MULTI             (EM_ESCOPE_API_MASK | 0x080C)
#define EM_ESCOPE_QUEUE_CURRENT                   (EM_ESCOPE_API_MASK | 0x080D)
#define EM_ESCOPE_QUEUE_GET_FIRST                 (EM_ESCOPE_API_MASK | 0x080E)
#define EM_ESCOPE_QUEUE_GET_NEXT                  (EM_ESCOPE_API_MASK | 0x080F)
#define EM_ESCOPE_QUEUE_GET_INDEX                 (EM_ESCOPE_API_MASK | 0x0810)
#define EM_ESCOPE_QUEUE_GET_NUM_PRIO		  (EM_ESCOPE_API_MASK | 0x0811)

/* EM API escopes: Scheduler */
#define EM_ESCOPE_ATOMIC_PROCESSING_END           (EM_ESCOPE_API_MASK | 0x0901)
#define EM_ESCOPE_ORDERED_PROCESSING_END          (EM_ESCOPE_API_MASK | 0x0902)
#define EM_ESCOPE_PRESCHEDULE                     (EM_ESCOPE_API_MASK | 0x0903)
#define EM_ESCOPE_SCHED_CONTEXT_TYPE_CURRENT      (EM_ESCOPE_API_MASK | 0x0904)

/* add-on APIs have a separate escope file but define a base here */
#define EM_ESCOPE_ADD_ON_API_BASE                 (EM_ESCOPE_API_MASK | 0x1000)

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_TYPES_H_ */
