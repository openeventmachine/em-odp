/*
 *   Copyright (c) 2017, Nokia Solutions and Networks
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
#ifndef EM_TIMER_TYPES_H_
#define EM_TIMER_TYPES_H_

#include <odp_api.h>
#include <event_machine_timer.h>
#include "em_timer_conf.h"

/* per timer (ODP timer pool) */
typedef struct event_timer_t {
	odp_timer_pool_t odp_tmr_pool;
	odp_pool_t tmo_pool;
	em_timer_flag_t	flags;
	int idx;
} event_timer_t;

/* global shared memory data */
typedef struct timer_shm_t {
	odp_shm_t odp_shm ODP_ALIGNED_CACHE;
	odp_ticketlock_t tlock;
	event_timer_t timer[EM_ODP_MAX_TIMERS];
} timer_shm_t;

/* EM timer handle points to this. Holds the timer state. */
typedef struct em_timer_timeout_t {
	odp_timer_t odp_timer;
	odp_timer_pool_t odp_timer_pool;
	odp_buffer_t odp_buffer;
	uint64_t period;
	uint64_t last_tick;
	odp_atomic_u32_t state;
	em_tmo_flag_t flags;
	em_queue_t queue;
} em_timer_timeout_t;

#endif /* EM_TIMER_TYPES_H_ */
