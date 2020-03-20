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

#ifndef EM_INCLUDE_H_
#define EM_INCLUDE_H_

/**
 * @file
 * EM internal include file
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for strnlen() */
#endif
#include <stdio.h>
#include <string.h>
#include <libconfig.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

/**
 * Branch Prediction macros
 */
#undef likely
#undef unlikely
#define likely(x)    odp_likely(!!(x))
#define unlikely(x)  odp_unlikely(!!(x))

#include "misc/list.h"
#include "misc/objpool.h"

#include "em_init.h"

#include "em_atomic.h"
#include "em_chaining_types.h"
#include "em_core_types.h"
#include "em_error_types.h"
#include "em_pool_types.h"
#include "em_eo_types.h"
#include "em_event_group_types.h"
#include "em_queue_types.h"
#include "em_event_types.h"
#include "em_queue_group_types.h"
#include "em_atomic_group_types.h"
#include "em_internal_event_types.h"
#include "em_dispatcher_types.h"
#include "em_sync_api_types.h"
#include "em_hook_types.h"
#include "em_libconfig_types.h"

#include "em_mem.h"

#include "em_core.h"
#include "em_error.h"
#include "em_eo.h"
#include "em_internal_event.h"
#include "em_info.h"
#include "em_pool.h"
#include "em_event.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_event_group.h"
#include "em_daemon_eo.h"
#include "em_atomic_group.h"
#include "em_dispatcher.h"
#include "em_libconfig.h"
#include "em_hooks.h"
#include "em_chaining.h"

#ifdef __cplusplus
}
#endif

#endif /* EM_INCLUDE_H_ */
