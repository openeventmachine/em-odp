/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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
 * Event Machine configuration options
 *
 */

#ifndef EVENT_MACHINE_CONFIG_H
#define EVENT_MACHINE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef EM_64_BIT
/**
 * @page page_version 64-bit version
 * This documentation represent the 64-bit version of Event Machine API.
 * Define EM_64_BIT or EM_32_BIT to select between 64- and 32-bit versions.
 */
#elif defined(EM_32_BIT)
/**
 * @page page_version 32-bit version
 * This documentation represent the 32-bit version of Event Machine API.
 * Define EM_64_BIT or EM_32_BIT to select between 64- and 32-bit versions.
 */
#else
#error Missing architecture definition. Define EM_64_BIT or EM_32_BIT!
/**
 * @page page_version 64/32-bit version not selected
 * This documentation has not selected between 64/32-bit version of
 * the Event Machine API. Some types might be missing.
 * Define EM_64_BIT or EM_32_BIT to select between 64- and 32-bit
 * versions.
 */
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
#define EM_HDL_UNDEF EM_UNDEF_UINTPTR

/**
 * @def PRI_HDL
 * EM-handle printf format
 */
#define PRI_HDL "p"

/**
 * @def EM_CONFIG_POOLS
 * Maximum number of EM pools
 */
#define EM_CONFIG_POOLS  16

/**
 * @def EM_MAX_QUEUES
 * Maximum total number of queues
 */
#define EM_MAX_QUEUES  960  /* Should be <= odp-max-queues */

/**
 * @def EM_QUEUE_NAME_LEN
 * Maximum queue name string length
 */
#define EM_QUEUE_NAME_LEN  32

/**
 * @def EM_MAX_ATOMIC_GROUPS
 * Maximum number of EM atomic groups
 */
#define EM_MAX_ATOMIC_GROUPS  128

/**
 * @def EM_ATOMIC_GROUP_NAME_LEN
 * Max atomic group name length
 */
#define EM_ATOMIC_GROUP_NAME_LEN  32

/**
 * @def EM_MAX_EOS
 * Maximum total number of EOs
 */
#define EM_MAX_EOS  512

/**
 * @def EM_EO_NAME_LEN
 * Maximum EO name string length
 */
#define EM_EO_NAME_LEN  32

/**
 * @def EM_MAX_EVENT_GROUPS
 * Maximum number of event groups
 */
#define EM_MAX_EVENT_GROUPS  1024

/**
 * @def EM_EVENT_GROUP_MAX_NOTIF
 * Maximum number of notifications
 */
#define EM_EVENT_GROUP_MAX_NOTIF  6

/*
 * @def EM_DISPATCH_CALLBACKS_ENABLE
 * Enable dispatcher callback functions
 */
#define EM_DISPATCH_CALLBACKS_ENABLE 1

/**
 * @def EM_API_HOOKS_ENABLE
 * Enable the usage of EM API hooks
 *
 * User provided API hook functions can be provided via em_init(). EM will
 * call the given hooks each time the corresponding API function is called.
 */
#define EM_API_HOOKS_ENABLE  1

/**
 * @def EM_CALLBACKS_MAX
 * Maximum number of EM callbacks/hooks that can be registered.
 *
 * The user may register up to the number 'EM_CALLBACKS_MAX' of each
 * callback/hook. API-hooks, such as the alloc-, free- and send-hook, or
 * dispatcher callbacks, such as the enter- and exit-callbacks, can be
 * registered each up to this limit.
 */
#define EM_CALLBACKS_MAX  8

/**
 * @def EM_CHECK_LEVEL
 * Error check level
 *
 * Conditionally compiled error checking level, range 0...3
 * Level 0 does not do any runtime argument checking (be careful!)
 * Level 1 adds minimum checks
 * Level 2 adds most checks except the slowest ones
 * Level 3 adds all checks and gives lowest performance
 *
 * @note em-odp: the 'EM_CHECK_LEVEL' value can be overridden by a command-line
 *               option to the 'configure' script, e.g.:
 *               $build> ../configure ... --enable-check-level=3
 *               The overridden value will be made available to the application
 *               via a pkgconfig set define.
 */
#ifndef EM_CHECK_LEVEL
#define EM_CHECK_LEVEL  1
#endif

/**
 * @def EM_EVENT_GROUP_SAFE_MODE
 * Guards event groups in undefined and error situations
 *
 * Excess and aborted group events don't belong to a valid group when received.
 * Most event group APIs check if the core local event group has expired during
 * receive function. Impacts performance when event groups are used.
 */
#define EM_EVENT_GROUP_SAFE_MODE  1

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_CONFIG_H */
