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

#include <stdatomic.h>
#include <odp_api.h>
#include <event_machine/add-ons/event_machine_timer.h>
#include "em_timer_conf.h"

/* per timer (ODP timer pool) */
typedef struct event_timer_t {
	odp_timer_pool_t odp_tmr_pool;
	odp_pool_t tmo_pool;
	em_timer_flag_t	flags;
	int idx;
	int plain_q_ok;
	bool is_ring;
	uint32_t num_ring_reserve;
	uint32_t num_tmo_reserve;
} event_timer_t;

/* Timer */
typedef struct {
	odp_ticketlock_t timer_lock; /* locks handling of these values */
	odp_pool_t shared_tmo_pool;  /* shared tmo pool if used */
	odp_pool_t ring_tmo_pool;    /* odp timeout pool for ring timers */
	uint32_t num_rings;	     /* counter for shared pool mgmt */
	uint32_t num_timers;	     /* all timers count for cleanup */
	uint32_t ring_reserved;      /* how many events reserved by ring timers so far */
	uint32_t reserved;           /* how many tmos reserved by timers so far */
	event_timer_t timer[EM_ODP_MAX_TIMERS];
	uint32_t init_check;
} timer_storage_t;

/* EM timeout handle points to this. Holds the timer state.
 * Some values are copies from e.g. timer data for faster access.
 */
typedef struct em_timer_timeout_t {
	odp_atomic_u32_t state;		/* timeout state */
	uint64_t period;		/* for periodic */
	uint64_t last_tick;		/* for periodic */
	em_tmo_flag_t flags;		/* oneshot/periodic etc */
	bool is_ring;			/* ring or normal timer */
	em_queue_t queue;		/* destination queue */
	odp_timer_t odp_timer;		/* odp timer / em tmo */
	odp_timer_pool_t odp_timer_pool;/* odp timer_pool <- em timer */
	odp_buffer_t odp_buffer;	/* this data is in odp buffer */
	/* 2nd cache line: */
	em_timer_t timer;		/* related timer (can't lookup from odp timer pool) */
	odp_pool_t ring_tmo_pool;	/* if ring: this is the pool for odp timeout */
	odp_event_t odp_timeout;	/* if ring: this is pre-allocated odp timeout */
	em_tmo_stats_t stats;		/* per tmo statistics */
} em_timer_timeout_t;

#endif /* EM_TIMER_TYPES_H_ */
