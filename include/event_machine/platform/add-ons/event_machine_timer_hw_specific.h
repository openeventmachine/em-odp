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
 * otherwise possible to combine by OR.
 * Values below 0x100 are reserved for future API.
 */
typedef enum em_timer_flag_t {
	/** alias for no flag/default */
	EM_TIMER_FLAG_DEFAULT = 0,
	/** single thread use, not multithread safe, but potentially faster */
	EM_TIMER_FLAG_PRIVATE = 1
} em_timer_flag_t;

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
	EM_TMO_FLAG_DEFAULT = EM_TMO_FLAG_ONESHOT /**< for default one-shot */
} em_tmo_flag_t;

/*
 * em_timer_clksrc_t is used to select the timer clock source.
 *
 * EM_TIMER_CLKSRC_DEFAULT is a standard definition, but more can be declared.
 * The default clock source could e.g. use the CPU internal timer and another
 * value for a separate timer running out of an external clock/trigger with
 * different resolution.
 */
typedef enum em_timer_clksrc_t {
	/** Portable default clock source */
	EM_TIMER_CLKSRC_DEFAULT = 0,
	/** CPU clock as clock source */
	EM_TIMER_CLKSRC_CPU  = 1,
	/** External clock source */
	EM_TIMER_CLKSRC_EXT = 2
} em_timer_clksrc_t;

/**
 * EM_TIMER_UNDEF value must be defined here and should normally be 0
 */
#define EM_TIMER_UNDEF ((em_timer_t)EM_HDL_UNDEF)

/**
 * EM_TMO_UNDEF value must be defined here and should normally be 0
 */
#define EM_TMO_UNDEF ((em_tmo_t)EM_UNDEF_UINTPTR)

/*
 * EM_TIMER_NAME_LEN value should be defined here.
 */
#define EM_TIMER_NAME_LEN 16

#ifdef __cplusplus
}
#endif

#endif
