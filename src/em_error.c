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

/*
 * EM Error Handling
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libgen.h> /* basename() */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <odp_api.h>
#include <odp/helper/odph_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_eo.h"
#include "em_eo_types.h"
#include "em_error.h"
#include "em_error_types.h"
#include "em_event_types.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_queue_types.h"

static int error_handler_initialized; /* static always initialized to 0 */

static early_log_t early_log = {default_log, vdefault_log};

static em_status_t early_error_handler(em_eo_t eo, em_status_t error,
				       em_escope_t escope, va_list args);

/* String names for em_status_e (index = enum value). */
static const char *const err_status_name_tbl[] = {
#define ERR_STR(name)\
	[name] = #name

	/* Keep the order same as in em_status_e */
	ERR_STR(EM_ERR_BAD_ARG),
	ERR_STR(EM_ERR_BAD_STATE),
	ERR_STR(EM_ERR_BAD_ID),
	ERR_STR(EM_ERR_BAD_TYPE),
	ERR_STR(EM_ERR_BAD_CONTEXT),
	ERR_STR(EM_ERR_BAD_POINTER),
	ERR_STR(EM_ERR_NOT_CREATED),
	ERR_STR(EM_ERR_NOT_FREE),
	ERR_STR(EM_ERR_NOT_FOUND),
	ERR_STR(EM_ERR_NOT_INITIALIZED),
	ERR_STR(EM_ERR_NOT_IMPLEMENTED),
	ERR_STR(EM_ERR_NOT_SUPPORTED),
	ERR_STR(EM_ERR_ALLOC_FAILED),
	ERR_STR(EM_ERR_OPERATION_FAILED),
	ERR_STR(EM_ERR_LIB_FAILED),
	ERR_STR(EM_ERR_TOO_LARGE),
	ERR_STR(EM_ERR_TOO_SMALL),
	ERR_STR(EM_ERR_EVENT_STATE),
	ERR_STR(EM_ERR_TIMEOUT),
	ERR_STR(EM_ERR_TOONEAR),
	ERR_STR(EM_ERR_TOOFAR),
	ERR_STR(EM_ERR_CANCELED),
	ERR_STR(EM_ERR_BUSY),
	ERR_STR(EM_ERR)
#undef ERR_STR
};

COMPILE_TIME_ASSERT(ODPH_ARRAY_SIZE(err_status_name_tbl) == EM_ERR + 1, ERR_TBL_SIZE);

/* Make sure that EM internally used error scopes are "seen" by EM_ESCOPE() */
COMPILE_TIME_ASSERT(EM_ESCOPE(EM_ESCOPE_API_MASK),
		    EM_ESCOPE_API_IS_NOT_PART_EM_ESCOPE__ERROR);
COMPILE_TIME_ASSERT(EM_ESCOPE(EM_ESCOPE_INTERNAL_MASK),
		    EM_ESCOPE_INTERNAL_IS_NOT_PART_OF_EM_ESCOPE__ERROR);

/*
 * String names for em_escope_t - EM API escopes:
 */
#define ESCOPE_API_IDX(escope) ((escope) & ~EM_ESCOPE_API_MASK)

static const char *const escope_api_name_tbl[EM_ESCOPE_API_LAST -
					     EM_ESCOPE_API_FIRST + 1] = {
#define ESCOPE_API_STR(escope) \
	[ESCOPE_API_IDX(escope)] = #escope

	/* EM API escopes: Atomic Group */
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_FIND),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_NAME),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_QUEUE_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_GROUP_QUEUE_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CREATE_AG),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CREATE_STATIC_AG),
	/* EM API escopes: Core */
	ESCOPE_API_STR(EM_ESCOPE_CORE_COUNT),
	ESCOPE_API_STR(EM_ESCOPE_CORE_ID),
	/* EM API escopes: Dispatcher */
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_DURATION),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_EVENTS),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_NS),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_OPT_INIT),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_REGISTER_ENTER_CB),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_REGISTER_EXIT_CB),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_ROUNDS),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_UNREGISTER_ENTER_CB),
	ESCOPE_API_STR(EM_ESCOPE_DISPATCH_UNREGISTER_EXIT_CB),
	/* EM API escopes: EO */
	ESCOPE_API_STR(EM_ESCOPE_EO_ADD_QUEUE),
	ESCOPE_API_STR(EM_ESCOPE_EO_ADD_QUEUE_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_EO_CONTEXT),
	ESCOPE_API_STR(EM_ESCOPE_EO_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_EO_CREATE_MULTIRCV),
	ESCOPE_API_STR(EM_ESCOPE_EO_CREATE_PARAM),
	ESCOPE_API_STR(EM_ESCOPE_EO_CURRENT),
	ESCOPE_API_STR(EM_ESCOPE_EO_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_EO_FIND),
	ESCOPE_API_STR(EM_ESCOPE_EO_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_EO_MULTIRCV_PARAM_INIT),
	ESCOPE_API_STR(EM_ESCOPE_EO_NAME),
	ESCOPE_API_STR(EM_ESCOPE_EO_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_EO_PARAM_INIT),
	ESCOPE_API_STR(EM_ESCOPE_EO_QUEUE_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_EO_QUEUE_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_EO_REGISTER_ERROR_HANDLER),
	ESCOPE_API_STR(EM_ESCOPE_EO_REMOVE_QUEUE),
	ESCOPE_API_STR(EM_ESCOPE_EO_REMOVE_QUEUE_ALL),
	ESCOPE_API_STR(EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_EO_REMOVE_QUEUE_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_EO_START),
	ESCOPE_API_STR(EM_ESCOPE_EO_START_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_EO_STATE),
	ESCOPE_API_STR(EM_ESCOPE_EO_STOP),
	ESCOPE_API_STR(EM_ESCOPE_EO_STOP_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER),
	/* EM API escopes: Error */
	ESCOPE_API_STR(EM_ESCOPE_ERROR),
	ESCOPE_API_STR(EM_ESCOPE_REGISTER_ERROR_HANDLER),
	ESCOPE_API_STR(EM_ESCOPE_UNREGISTER_ERROR_HANDLER),
	/* EM API escopes: Event Group */
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_ABORT),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_APPLY),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_ASSIGN),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_CURRENT),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_INCREMENT),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_IS_READY),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_NOTIF),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_GROUP_PROCESSING_END),
	ESCOPE_API_STR(EM_ESCOPE_SEND_GROUP),
	ESCOPE_API_STR(EM_ESCOPE_SEND_GROUP_MULTI),
	/* EM API escopes: Event */
	ESCOPE_API_STR(EM_ESCOPE_ALLOC),
	ESCOPE_API_STR(EM_ESCOPE_ALLOC_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_CLONE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_CLONE_PART),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_HAS_REF),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_MARK_FREE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_MARK_FREE_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_MARK_SEND),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_POINTER),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_POINTER_AND_SIZE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_POOL),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_POOL_SUBPOOL),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_REF),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_SAME_TYPE_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_SET_TYPE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_SIZE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_TYPE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_TYPE_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UAREA),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UAREA_ID),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UAREA_ID_SET),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UAREA_INFO),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UNMARK_FREE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UNMARK_FREE_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_UNMARK_SEND),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_FREE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_INFO),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_MAX_SIZE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_SIZE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_SIZE_SET),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_TBL),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_TYPE),
	ESCOPE_API_STR(EM_ESCOPE_EVENT_VECTOR_TYPE_SET),
	ESCOPE_API_STR(EM_ESCOPE_FREE),
	ESCOPE_API_STR(EM_ESCOPE_FREE_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_SEND),
	ESCOPE_API_STR(EM_ESCOPE_SEND_MULTI),
	/* EM API escopes: Event Packet */
	ESCOPE_API_STR(EM_ESCOPE_PACKET_HEADROOM),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_POINTER),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_POINTER_AND_SIZE),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_PULL_HEAD),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_PULL_TAIL),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_PUSH_HEAD),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_PUSH_TAIL),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_RESET),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_RESIZE),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_SIZE),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_TAILROOM),
	ESCOPE_API_STR(EM_ESCOPE_PACKET_CONCAT),
	/* EM API escopes: Hooks */
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_REGISTER_ALLOC),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_REGISTER_FREE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_REGISTER_SEND),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_REGISTER_TO_ACTIVE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_REGISTER_TO_IDLE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_REGISTER_WHILE_IDLE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_UNREGISTER_ALLOC),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_UNREGISTER_FREE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_UNREGISTER_SEND),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_UNREGISTER_TO_ACTIVE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_UNREGISTER_TO_IDLE),
	ESCOPE_API_STR(EM_ESCOPE_HOOKS_UNREGISTER_WHILE_IDLE),
	/* EM API escopes: Init / Term */
	ESCOPE_API_STR(EM_ESCOPE_CONF_INIT),
	ESCOPE_API_STR(EM_ESCOPE_CONF_LOCAL_INIT),
	ESCOPE_API_STR(EM_ESCOPE_CONF_OPTS),
	ESCOPE_API_STR(EM_ESCOPE_CONF_LOCAL_OPTS),
	ESCOPE_API_STR(EM_ESCOPE_INIT),
	ESCOPE_API_STR(EM_ESCOPE_INIT_CORE),
	ESCOPE_API_STR(EM_ESCOPE_INIT_LOCAL),
	ESCOPE_API_STR(EM_ESCOPE_TERM),
	ESCOPE_API_STR(EM_ESCOPE_TERM_CORE),
	ESCOPE_API_STR(EM_ESCOPE_TERM_LOCAL),
	ESCOPE_API_STR(EM_ESCOPE_TERM_LOCAL_INIT),
	ESCOPE_API_STR(EM_ESCOPE_CFGFILE_OPTS),
	/* EM API escopes: Pool */
	ESCOPE_API_STR(EM_ESCOPE_POOL_CFG_INIT),
	ESCOPE_API_STR(EM_ESCOPE_POOL_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_POOL_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_POOL_FIND),
	ESCOPE_API_STR(EM_ESCOPE_POOL_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_POOL_INFO),
	ESCOPE_API_STR(EM_ESCOPE_POOL_NAME),
	ESCOPE_API_STR(EM_ESCOPE_POOL_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_POOL_NUM_SUBPOOLS),
	ESCOPE_API_STR(EM_ESCOPE_POOL_STATS),
	ESCOPE_API_STR(EM_ESCOPE_POOL_STATS_RESET),
	ESCOPE_API_STR(EM_ESCOPE_POOL_STATS_SELECTED),
	ESCOPE_API_STR(EM_ESCOPE_POOL_SUBPOOL_STATS),
	ESCOPE_API_STR(EM_ESCOPE_POOL_SUBPOOL_STATS_RESET),
	ESCOPE_API_STR(EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED),
	ESCOPE_API_STR(EM_ESCOPE_POOL_STATS_OPT),
	/* EM API escopes: Queue Group */
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_FIND),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_MASK),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_MODIFY),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_NAME),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_QUEUE_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_GROUP_QUEUE_NEXT),
	/* EM API escopes: Queue */
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_AGGR_CONF_INIT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_AGGR),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_AGGR_INFO),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_AGGR_LIST),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_AGGR_NUM),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_AGROUP),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CONTEXT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CREATE_STATIC),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CREATE_PARAM),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_CURRENT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_DEQUEUE),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_DEQUEUE_MULTI),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_FIND),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_FIRST),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_INDEX),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_NAME),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_NEXT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_NUM_PRIO),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_PARAM_INIT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_PRIORITY),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_QGROUP),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_SET_CONTEXT),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_TYPE),
	ESCOPE_API_STR(EM_ESCOPE_QUEUE_STATIC_HANDLE),
	/* EM API escopes: Scheduler */
	ESCOPE_API_STR(EM_ESCOPE_ATOMIC_PROCESSING_END),
	ESCOPE_API_STR(EM_ESCOPE_ORDERED_PROCESSING_END),
	ESCOPE_API_STR(EM_ESCOPE_PRESCHEDULE),
	ESCOPE_API_STR(EM_ESCOPE_SCHED_CONTEXT_TYPE_CURRENT),
	/* EM API escopes: Timer */
	ESCOPE_API_STR(EM_ESCOPE_TIMER_ALL),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_ATTR),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_ATTR_INIT),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_CAPABILITY),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_CUR_TICK),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_FREQ),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_NS_TO_TICK),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_RES_CAPABILITY),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_RING_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_RING_FREQ_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_TIMER_TICK_TO_NS),
	ESCOPE_API_STR(EM_ESCOPE_TMO_ACK),
	ESCOPE_API_STR(EM_ESCOPE_TMO_CANCEL),
	ESCOPE_API_STR(EM_ESCOPE_TMO_CREATE),
	ESCOPE_API_STR(EM_ESCOPE_TMO_CREATE_ARG),
	ESCOPE_API_STR(EM_ESCOPE_TMO_DELETE),
	ESCOPE_API_STR(EM_ESCOPE_TMO_SET_ABS),
	ESCOPE_API_STR(EM_ESCOPE_TMO_SET_PERIODIC),
	ESCOPE_API_STR(EM_ESCOPE_TMO_SET_PERIODIC_RING),
	ESCOPE_API_STR(EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ),
	ESCOPE_API_STR(EM_ESCOPE_TMO_SET_REL),
	ESCOPE_API_STR(EM_ESCOPE_TMO_STATE),
	ESCOPE_API_STR(EM_ESCOPE_TMO_STATS),
	ESCOPE_API_STR(EM_ESCOPE_TMO_TIMER),
	ESCOPE_API_STR(EM_ESCOPE_TMO_TYPE),
	ESCOPE_API_STR(EM_ESCOPE_TMO_USERPTR),
	ESCOPE_API_STR(EM_ESCOPE_TMO_RING_ACK)

#undef ESCOPE_API_STR
};

COMPILE_TIME_ASSERT(ODPH_ARRAY_SIZE(escope_api_name_tbl) <=
		    ESCOPE_API_IDX(EM_ESCOPE_API_LAST) + 1, ERR_ESCOPE_API_TBL_SIZE);

/*
 * String names for em_escope_t - EM internal escopes:
 */
#define ESCOPE_INTERNAL_IDX(escope) ((escope) & ~EM_ESCOPE_INTERNAL_MASK)

static const char *const escope_internal_name_tbl[EM_ESCOPE_INTERNAL_LAST -
						  EM_ESCOPE_INTERNAL_FIRST + 1] = {
#define ESCOPE_INTERNAL_STR(escope) \
	[ESCOPE_INTERNAL_IDX(escope)] = #escope

	/* EM internal escopes: Event Chaining */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_SEND_DEVICE),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_SEND_DEVICE_MULTI),
	/* EM internal escopes: Event Group */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_GROUP_UPDATE),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_GROUP_UPDATE_PRECNT),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_GROUP_UPDATE_POSTCNT),
	/* EM internal escopes: EO */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_LOCAL_FUNC_CALL_REQ),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_REMOVE_QUEUE_ALL_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_REMOVE_QUEUE_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_REMOVE_QUEUE_SYNC_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_START_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_START_SYNC_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_STOP_DONE_CB),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EO_STOP_SYNC_DONE_CB),
	/* EM internal escopes: Queue */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_DISABLE),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_DISABLE_ALL),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_ENABLE),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_ENABLE_ALL),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_STATE_CHANGE),
	/* EM internal escopes: Queue Groups */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_GROUP_ADD_CORE),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_QUEUE_GROUP_REM_CORE),
	/* EM internal escopes: Other */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_DELETE_CTRL_QUEUES),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_INTERNAL_DONE),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_EVENT_INTERNAL_LFUNC_CALL),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_INTERNAL_EVENT_RECV_FUNC),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_INTERNAL_NOTIF),
	ESCOPE_INTERNAL_STR(EM_ESCOPE_SEND_CTRL_QUEUE),
	/* EM ODP extensions error scope */
	ESCOPE_INTERNAL_STR(EM_ESCOPE_ODP_EXT)

#undef ESCOPE_INTERNAL_STR
};

COMPILE_TIME_ASSERT(ODPH_ARRAY_SIZE(escope_internal_name_tbl) <=
		    ESCOPE_INTERNAL_IDX(EM_ESCOPE_INTERNAL_LAST) + 1, ERR_ESCOPE_INTERNAL_TBL_SIZE);

const char *err_status_str(em_status_t status)
{
	if (status < ODPH_ARRAY_SIZE(err_status_name_tbl) &&
	    err_status_name_tbl[status] /*avoid gaps*/)
		return err_status_name_tbl[status];

	return "Unknown ERROR";
}

const char *escope_api_str(em_escope_t escope)
{
	uint32_t escope_idx = ESCOPE_API_IDX(escope);

	if (EM_ESCOPE_API(escope) &&
	    escope_idx < ODPH_ARRAY_SIZE(escope_api_name_tbl) &&
	    escope_api_name_tbl[escope_idx] /*avoid gaps*/)
		return escope_api_name_tbl[escope_idx];

	return "Unknown ESCOPE";
}

const char *escope_internal_str(em_escope_t escope)
{
	uint32_t escope_idx = ESCOPE_INTERNAL_IDX(escope);

	if (EM_ESCOPE_INTERNAL(escope) &&
	    escope_idx < ODPH_ARRAY_SIZE(escope_internal_name_tbl) &&
	    escope_internal_name_tbl[escope_idx] /*avoid gaps*/)
		return escope_internal_name_tbl[escope_idx];

	return "Unknown ESCOPE";
}

const char *escope_addon_api_str(em_escope_t escope)
{
	if (EM_ESCOPE_ADDON_API(escope))
		return "EM Add-on API ESCOPE";

	return "Unknown ESCOPE";
}

/*
 * Build an internal EM error message (EM_ESCOPE(escope)==true).
 * Shared between early_error_handler() and default_error_handler() and the
 * EM helper API em_error_format_string().
 *
 * Always '\0'-terminates the 'err_msg' string (if given and valid) but silently
 * truncates messages longer than the size limit 'errmsg_sz - 1'.
 *
 * @return Number of chars written to 'err_msg' (excluding final '\0')
 */
size_t format_errmsg(em_status_t error, em_escope_t escope,
		     const char *eo_str, va_list args, bool early_mode,
		     char err_msg[/*out*/], size_t errmsg_sz)
{
	if (!err_msg || errmsg_sz == 0)
		return 0;

	err_msg[0] = '\0';

	/*
	 * va_list contains: __FILE__, __func__, __LINE__, (user_fmt),
	 * ## __VA_ARGS__ as reported by the INTERNAL_ERROR macro
	 */
	char *file = va_arg(args, char *);
	const char *func = va_arg(args, const char *);
	const int line = va_arg(args, const int);
	const char *user_fmt = va_arg(args, const char *);
	const char *base_file = basename(file);
	const char *error_str = err_status_str(error);
	const char *escope_str = "Unknown ESCOPE";
	int n;

	if (EM_ESCOPE_API(escope))
		escope_str = escope_api_str(escope);
	else if (EM_ESCOPE_INTERNAL(escope))
		escope_str = escope_internal_str(escope);
	else if (EM_ESCOPE_ADDON_API(escope))
		escope_str = escope_addon_api_str(escope);

	if (early_mode) {
		int cpu_id = odp_cpu_id(); /* em_core_id() does not work here */

		n = snprintf(err_msg, errmsg_sz,
			     "\nEM ERROR:0x%08X(%s)  ESCOPE:0x%08X(%s)  %s (Early Error)\n"
			     "cpu:%02d %s:%d %s()\n",
			     error, error_str, escope, escope_str, eo_str,
			     cpu_id, base_file, line, func);
	} else {
		const em_locm_t *locm = &em_locm;
		const uint64_t global_err_cnt = load_global_err_cnt();
		const uint64_t local_err_cnt = locm->error_count;
		const int core_id = locm->core_id;

		n = snprintf(err_msg, errmsg_sz,
			     "\nEM ERROR:0x%08X(%s)  ESCOPE:0x%08X(%s)  %s\n"
			     "core:%02d ecount:%" PRIu64 "(%" PRIu64 ") %s:%d %s()\n",
			     error, error_str, escope, escope_str, eo_str,
			     core_id, global_err_cnt, local_err_cnt, base_file, line, func);
	}

	if (unlikely(n < 0))
		return 0;
	else if (unlikely((size_t)n >= errmsg_sz - 1))
		return errmsg_sz - 1; /* truncated or all used */

	if (!user_fmt)
		return (size_t)n;

	/*
	 * Append error-specific message since space left
	 */
	size_t rem_sz = errmsg_sz - n;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	int m = vsnprintf(&err_msg[n], rem_sz, user_fmt, args);
#pragma GCC diagnostic pop

	if (unlikely(m < 0))
		return (size_t)n; /* No user message appended */

	if ((size_t)m >= rem_sz - 1)
		return errmsg_sz - 1; /* truncated or all used */

	int end = n + m; /* index of '\0' */

	/* Add newline if space available */
	if ((size_t)end + 1 < errmsg_sz) {
		err_msg[end++] = '\n';
		err_msg[end] = '\0';
	}

	return (size_t)end;
}

static void increment_global_err_cnt(void)
{
	odp_atomic_inc_u64(&em_shm->error_handler.global_error_count);
}

uint64_t load_global_err_cnt(void)
{
	return odp_atomic_load_u64(&em_shm->error_handler.global_error_count);
}

ODP_PRINTF_FORMAT(2, 0)
int vdefault_log(em_log_level_t level, const char *fmt, va_list args)
{
	int r;
	FILE *logfd;

	switch (level) {
	case EM_LOG_DBG:
	case EM_LOG_PRINT:
		logfd = stdout;
		break;
	default:
		/* also level=EM_LOG_ERR */
		logfd = stderr;
		break;
	}

	r = vfprintf(logfd, fmt, args);
	return r;
}

ODP_PRINTF_FORMAT(2, 3)
int default_log(em_log_level_t level, const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vdefault_log(level, fmt, args);
	va_end(args);

	return r;
}

/**
 * Early EM Error Handler
 *
 * The early error handler is used when reporting errors at startup before
 * the default, application and EO specific error handlers have been set up.
 *
 * @param eo      unused
 * @param error   The error code (reason), see em_status_e
 * @param escope  The error scope from within the error was reported, also
 *                tells whether the error was EM internal or application
 *                specific
 * @param args    va_list of args
 *
 * @return The function may not return depending on implementation / error
 *         code / error scope. If it returns, the return value is the original
 *         (or modified) error code from the caller.
 */
static em_status_t early_error_handler(em_eo_t eo, em_status_t error,
				       em_escope_t escope, va_list args)
{
	em_log_func_t log_fn = early_log.log_fn;

	(void)eo;

	if (EM_ESCOPE(escope)) {
		char err_msg[1024];

		format_errmsg(error, escope, "EO:n/a", args, true /*early_mode*/,
			      err_msg /*out*/, sizeof(err_msg));
		log_fn(EM_LOG_ERR, "%s", err_msg);
	} else {
		/* va_list from application - don't touch. */
		int cpu_id = odp_cpu_id(); /* em_core_id() does not work here */

		log_fn(EM_LOG_ERR, "\n"
		       "APPL ERROR:0x%08X  ESCOPE:0x%08X  (Early Error)\n"
		       "cpu:%02d\n", error, escope, cpu_id);
	}

	if (unlikely(EM_ERROR_IS_FATAL(error))) {
		log_fn(EM_LOG_ERR,
		       "FATAL EM ERROR:0x%08X (Early Error) - ABORT!\n",
		       error);
		abort();
	}

	return error;
}

/**
 * Default EM Error Handler
 *
 * The default error handler is called upon error if the application(s)
 * have not registered their own global and/or EO-specific error handlers
 *
 * @param eo      EO reporting the error (if applicable)
 * @param error   The error code (reason), see em_status_e
 * @param escope  The error scope from within the error was reported, also
 *                tells whether the error was EM internal or application
 *                specific
 * @param args    va_list of args
 *
 * @return The function may not return depending on implementation / error
 *         code / error scope. If it returns, the return value is the original
 *         (or modified) error code from the caller.
 */
em_status_t default_error_handler(em_eo_t eo, em_status_t error,
				  em_escope_t escope, va_list args)
{
	char eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];

	if (eo == EM_EO_UNDEF) {
		eo_str[0] = '\0';
	} else {
		char eo_name[EM_EO_NAME_LEN];
		size_t nlen;

		nlen = em_eo_name(eo, eo_name, sizeof(eo_name));
		eo_name[nlen] = '\0';

		snprintf(eo_str, sizeof(eo_str),
			 "EO:%" PRI_EO "-\"%s\"  ", eo, eo_name);
		eo_str[sizeof(eo_str) - 1] = '\0';
	}

	if (EM_ESCOPE(escope)) {
		char err_msg[1024];

		format_errmsg(error, escope, eo_str, args,
			      false /*not early_mode*/,
			      err_msg /*out*/, sizeof(err_msg));

		EM_LOG(EM_LOG_ERR, "%s", err_msg);
	} else {
		/* va_list from application - don't touch. */
		const uint64_t local_err_cnt = em_locm.error_count;
		const uint64_t global_err_cnt = load_global_err_cnt();
		const int core_id = em_core_id();

		EM_LOG(EM_LOG_ERR, "\n"
		       "APPL ERROR:0x%08X  ESCOPE:0x%08X  %s\n"
		       "core:%02d ecount:%" PRIu64 "(%" PRIu64 ")\n",
		       error, escope, eo_str, core_id,
		       global_err_cnt, local_err_cnt);
	}

	if (unlikely(EM_ERROR_IS_FATAL(error))) {
		EM_LOG(EM_LOG_ERR, "FATAL EM ERROR:0x%08X - ABORT!\n", error);
		abort();
	}

	return error;
}

/**
 * Select and call an error handler.
 *
 * @param error         Error code
 * @param escope        Error scope. Identifies the scope for interpreting
 *                      the error code and variable arguments.
 * @param args_list     Variable number and type of arguments
 *
 * @return Returns the 'error' argument given as input if the called error
 *         handler has not changed this value.
 */
em_status_t select_error_handler(em_status_t error, em_escope_t escope,
				 va_list args_list)
{
	if (unlikely(!error_handler_initialized || !em_shm)) {
		/*
		 * Early errors reported at startup before error handling
		 * is properly initialized.
		 */
		error = early_error_handler(EM_EO_UNDEF, error, escope, args_list);
		return error;
	}

	em_error_handler_t error_handler;
	em_error_handler_t eo_handler;
	em_log_func_t log_fn = em_shm->log_fn;
	const eo_elem_t *const eo_elem = eo_elem_current();
	em_eo_t eo = EM_EO_UNDEF;

	/* Global error handler, default or user registered */
	error_handler = em_shm->error_handler.em_error_handler;

	if (eo_elem && eo_allocated(eo_elem)) {
		eo_handler = eo_elem->error_handler_func;
		eo = eo_elem->eo;
		if (eo_handler)
			error_handler = eo_handler;
	}

	/* fallback, should never happen */
	if (unlikely(error_handler == NULL)) {
		error_handler = early_error_handler;
		log_fn = early_log.log_fn; /* for the "FATAL EM ERROR:" below */
	}

	/*
	 * Call the selected error handler and possibly change the error code.
	 */
	error = error_handler(eo, error, escope, args_list);

	if (error != EM_OK) {
		/* Increase the error count, used in logs/printouts */
		increment_global_err_cnt();
		em_locm.error_count += 1;
	}

	/*
	 * An error handler cannot return a fatal EM-error and continue running.
	 */
	if (unlikely(EM_ESCOPE(escope) && EM_ERROR_IS_FATAL(error))) {
		log_fn(EM_LOG_ERR,
		       "FATAL EM ERROR:0x%08X  ESCOPE:0x%08X - ABORT!\n"
		       "EM: an error-handler must not return a fatal EM-error!",
		       error, escope);
		abort();
	}

	/* Return input error or value changed by error_handler */
	return error;
}

/**
 * Called ONLY from INTERNAL_ERROR macro - do not use for anything else!
 * internal_error((error), (escope), __FILE__, __func__, __LINE__,
 *                 (user_fmt), ## __VA_ARGS__)
 */
em_status_t internal_error(em_status_t error, em_escope_t escope, ...)
{
	/*
	 * va_list contains:
	 * __FILE__, __func__, __LINE__, (user_fmt), ## __VA_ARGS__
	 */
	va_list args;

	va_start(args, escope);

	/* Select and call an error handler. Possibly modifies the error code*/
	error = select_error_handler(error, escope, args);

	va_end(args);

	/* Return input error or value changed by error_handler */
	return error;
}

/**
 * Initialize the EM Error Handling
 */
void error_init(void)
{
	odp_ticketlock_init(&em_shm->error_handler.lock);

	em_shm->error_handler.em_error_handler = default_error_handler;

	odp_atomic_init_u64(&em_shm->error_handler.global_error_count, 0);

	em_shm->error_handler.initialized = 1; /* For API functions */
	error_handler_initialized = 1; /* For early error handler select */
}

em_status_t early_log_init(em_log_func_t user_log_fn, em_vlog_func_t user_vlog_fn)
{
	/* Check that both log fns are either set or both NULL */
	int is_log_null = user_log_fn == NULL;
	int is_vlog_null = user_vlog_fn == NULL;

	if (is_log_null != is_vlog_null)
		return EM_ERR_BAD_POINTER;

	/* store user provided log functions */
	if (user_log_fn != NULL) {
		early_log.log_fn = user_log_fn;
		early_log.vlog_fn = user_vlog_fn;
	}

	return EM_OK;
}

void log_init(void)
{
	em_shm->log_fn = early_log.log_fn;
	em_shm->vlog_fn = early_log.vlog_fn;
}
