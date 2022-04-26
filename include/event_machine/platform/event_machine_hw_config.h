/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2015-2022, Nokia Solutions and Networks
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
 * Event Machine HW dependent constants and definitions.
 *
 * @note Always use the defined names from this file instead of direct
 *       numerical values. The values are platform/implementation specific.
 */

#ifndef EVENT_MACHINE_HW_CONFIG_H
#define EVENT_MACHINE_HW_CONFIG_H

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HW specific constants
 ***************************************
 */

/* Max number of EM cores supported */
#define EM_MAX_CORES       64

/**
 * @def EM_UNDEF_U64
 * EM undefined u64
 */
#define EM_UNDEF_U64       0

/**
 * @def EM_UNDEF_U32
 * EM undefined u32
 */
#define EM_UNDEF_U32       0

/**
 * @def EM_UNDEF_U16
 * EM undefined u16
 */
#define EM_UNDEF_U16       0

/**
 * @def EM_UNDEF_U8
 * EM undefined u8
 */
#define EM_UNDEF_U8        0

/**
 * @def EM_UNDEF_UINTPTR
 * EM undefined uintptr
 */
#define EM_UNDEF_UINTPTR  EM_STATIC_CAST(uintptr_t, NULL)

/**
 * @def EM_UNDEF_PTR
 * EM undefined pointer
 */
#ifndef __cplusplus
#define EM_UNDEF_PTR  NULL
#else
#define EM_UNDEF_PTR  nullptr
#endif

/**
 * @def EM_QUEUE_PRIO_NUM
 * Number of queue scheduling priorities, normal default is 8.
 * @see em_queue_prio_e
 */
#define EM_QUEUE_PRIO_NUM  8

/**
 * @def EM_QUEUE_RANGE_OFFSET
 * Determines the EM queue handles range to use.
 * Note: value must be >=1 and <='UINT16_MAX-EM_MAX_QUEUES+1'
 *   idx     EM Queue handle
 *    0   ->   0 + offset
 *    1   ->   1 + offset
 *   ...
 *  max-1 ->   max-1 + offset
 */
#define EM_QUEUE_RANGE_OFFSET  1

/*
 * Static EM queue IDs
 */
#define _EM_QUEUE_STATIC_MIN  (0 + EM_QUEUE_RANGE_OFFSET)
#define _EM_QUEUE_STATIC_MAX  (0xFE + EM_QUEUE_RANGE_OFFSET)
/**
 * @def EM_QUEUE_STATIC_MIN
 * Minimum static queue ID
 */
#define EM_QUEUE_STATIC_MIN  EM_STATIC_CAST(uint16_t, _EM_QUEUE_STATIC_MIN)
/**
 * @def EM_QUEUE_STATIC_MAX
 * Maximum static queue ID
 */
#define EM_QUEUE_STATIC_MAX  EM_STATIC_CAST(uint16_t, _EM_QUEUE_STATIC_MAX)
/**
 * @def EM_QUEUE_STATIC_NUM
 * Number of static queues
 */
#define EM_QUEUE_STATIC_NUM  (_EM_QUEUE_STATIC_MAX - _EM_QUEUE_STATIC_MIN + 1)

/**
 * @def EM_MAX_QUEUE_GROUPS
 * Maximum number of EM queue groups
 */
#define EM_MAX_QUEUE_GROUPS  (EM_MAX_CORES + 64)
/**
 * @def EM_QUEUE_GROUP_DEFAULT
 * Default queue group for EM
 */
#define EM_QUEUE_GROUP_DEFAULT  EM_REINTERPRET_CAST(em_queue_group_t, EM_MAX_QUEUE_GROUPS)
/**
 * @def EM_QUEUE_GROUP_NAME_LEN
 * Max queue group name length
 */
#define EM_QUEUE_GROUP_NAME_LEN  32

/**
 * @def EM_QUEUE_GROUP_DEFAULT_NAME
 * The name of the EM default queue group
 */
#define EM_QUEUE_GROUP_DEFAULT_NAME "default"

/**
 * @def EM_QUEUE_GROUP_CORE_BASE_NAME
 * Base-name of EM core-specific queue groups (one per EM-core),
 * if created by EM (note: see the EM runtime config file for option).
 * The full queue group name for a single-core group is: "core" + "%d",
 * which gives "core0", "core1", ... "core99", ...
 * EM earlier relied on these queue groups for internal core specific
 * messaging and also allowed applications to use them. Currently EM
 * does not internally need these groups but will create them based on
 * an EM config file option for applications relying on their existence.
 *
 * Example: Find the queue group that includes only this core.
 *          (EM single-core queue group creation enabled in config file)
 * @code
 *	char qgrp_name[EM_QUEUE_GROUP_NAME_LEN];
 *	int core = em_core_id();
 *	em_queue_group_t qgrp_core;
 *
 *	snprintf(qgrp_name, sizeof(qgrp_name), "%s%d",
 *		 EM_QUEUE_GROUP_CORE_BASE_NAME, core);
 *	...
 *	qgrp_core = em_queue_group_find(qgrp_name);
 *	...
 * @endcode
 */
#define EM_QUEUE_GROUP_CORE_BASE_NAME "core"

/**
 * @def EM_POOL_DEFAULT
 * Define the EM default event pool
 */
#define EM_POOL_DEFAULT  EM_REINTERPRET_CAST(em_pool_t, 1)
/**
 * @def EM_POOL_NAME_LEN
 * Max event pool name length
 */
#define EM_POOL_NAME_LEN  32

/**
 * @def EM_POOL_DEFAULT_NAME
 * The name of the EM default event pool
 */
#define EM_POOL_DEFAULT_NAME "default"

/**
 * @def EM_EVENT_USER_AREA_MAX_SIZE
 * The maximum size in bytes that can be configured for the event user area.
 * The user area is located outside of the payload in the event metadata (hdr)
 * and can be used to store event related state without affecting the payload.
 */
#define EM_EVENT_USER_AREA_MAX_SIZE 256

/**
 * @def EM_SCHED_MULTI_MAX_BURST
 * The maximum number of events to request from the scheduler and then
 * dispatch in one burst.
 *
 * @note the odp sched burst size is determined by the odp-config-file values:
 *       sched_basic: burst_size_default[...] and burst_size_max[...]
 */
#define EM_SCHED_MULTI_MAX_BURST  32

/**
 * @def EM_SCHED_AG_MULTI_MAX_BURST
 * The maximum number of events from an atomic group to dispatch in one burst.
 */
#define EM_SCHED_AG_MULTI_MAX_BURST  32

/**
 * @def EM_EO_MULTIRCV_MAX_EVENTS
 * The default maximum number of events passed to the EO's multi-event
 * receive function (when the EO has been created with em_eo_create_multircv()).
 * This value is used by EM as a default if the user does not specify
 * a value (i.e. gives '0') for 'em_eo_multircv_param_t::max_events' when
 * calling em_eo_create_multircv()
 */
#define EM_EO_MULTIRCV_MAX_EVENTS  32

/**
 * @def EM_OUTPUT_QUEUE_IMMEDIATE
 *   '0': allow EM to buffer events sent to output queues before calling the
 *        user provided output callback to improve throughput
 *   '1': each em_send/_multi() will immediately call the user provided output
 *        queue callback with no EM internal buffering
 * This define mostly affects behaviour and performance when sending events from
 * an ordered scheduling context where EM needs to ensure event ordering before
 * calling the user provided output callback function.
 */
#define EM_OUTPUT_QUEUE_IMMEDIATE 0

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_HW_CONFIG_H */
