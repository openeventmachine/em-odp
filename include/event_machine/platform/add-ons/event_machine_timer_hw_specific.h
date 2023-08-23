/*
 *   Copyright (c) 2016, Nokia Solutions and Networks
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
 * EM ODP specific timer definition template
 */
#ifndef EVENT_MACHINE_TIMER_HW_SPECIFIC_H
#define EVENT_MACHINE_TIMER_HW_SPECIFIC_H

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * em_timer_t (timer handle) needs to be defined by the EM implementation.
 */
EM_HANDLE_T(em_timer_t);
/** em_timer_t printf format */
#define PRI_TMR  PRI_HDL

/*
 * em_timer_flag_t is used to request timer specific features by
 * setting individual flag bits, i.e. values should be powers of two or
 * otherwise possible to combine by bitwise OR.
 * Values below 0x100 are reserved for future API.
 */
typedef enum em_timer_flag_t {
	/** default */
	EM_TIMER_FLAG_NONE = 0,
	/** single thread use. Not multithread safe, but potentially faster */
	EM_TIMER_FLAG_PRIVATE = 1,
	/** is periodic ring. This does not need to be manually set, init does */
	EM_TIMER_FLAG_RING = 2
} em_timer_flag_t;
#define EM_TIMER_FLAG_DEFAULT EM_TIMER_FLAG_NONE

/**
 * em_tmo_t (timeout handle) needs to be defined by the EM implementation
 */
typedef struct em_timer_timeout_t *em_tmo_t;
/** em_tmo_t printf format */
#define PRI_TMO  "p"

/*
 * em_tmo_flag_t is used to request timeout specific features by
 * setting individual flag bits, i.e. values should be powers of two or
 * otherwise possible to combine by OR.
 * Values below 0x100 are reserved for future API.
 */
typedef enum em_tmo_flag_t {
	EM_TMO_FLAG_ONESHOT  = 1, /**< to select one-shot */
	EM_TMO_FLAG_PERIODIC = 2, /**< to select periodic */
	EM_TMO_FLAG_NOSKIP   = 4, /**< see periodic ack */
} em_tmo_flag_t;
/** default timeout is oneshot */
#define EM_TMO_FLAG_DEFAULT EM_TMO_FLAG_ONESHOT

/*
 * em_timer_clksrc_t is used to select the timer clock source in case multiple
 * are supported.
 *
 * EM_TIMER_CLKSRC_DEFAULT is always available portable definition. More
 * can be defined (implementation specific).
 */
typedef enum em_timer_clksrc_t {
	EM_TIMER_CLKSRC_0,
	EM_TIMER_CLKSRC_1,
	EM_TIMER_CLKSRC_2,
	EM_TIMER_CLKSRC_3,
	EM_TIMER_CLKSRC_4,
	EM_TIMER_CLKSRC_5,
	EM_TIMER_NUM_CLKSRC
} em_timer_clksrc_t;

/** portable default clock */
#define EM_TIMER_CLKSRC_DEFAULT EM_TIMER_CLKSRC_0

/** Backwards compatible macro.
 * @deprecated Temporary backwards compatibility, will be removed later
 */
#define EM_TIMER_CLKSRC_CPU	EM_TIMER_CLKSRC_0
/** Backwards compatible macro.
 * @deprecated Temporary backwards compatibility, will be removed later
 */
#define EM_TIMER_CLKSRC_EXT	EM_TIMER_CLKSRC_2

/**
 * EM_TIMER_UNDEF value must be defined here and should normally be 0
 */
#define EM_TIMER_UNDEF  EM_STATIC_CAST(em_timer_t, EM_HDL_UNDEF)

/**
 * EM_TMO_UNDEF value must be defined here and should normally be 0
 */
#define EM_TMO_UNDEF  EM_STATIC_CAST(em_tmo_t, EM_UNDEF_PTR)

/*
 * EM_TIMER_NAME_LEN value should be defined here.
 */
#define EM_TIMER_NAME_LEN 16

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif
