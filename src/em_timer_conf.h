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

/**
 * @file
 * Compile time configuration for the event timer
 * Can be overridden via makefile
 *
 */
#ifndef EM_TIMER_CONF_H_
#define EM_TIMER_CONF_H_

/*
 * Global configuration for the EM timer
 */

#ifndef EM_ODP_TIMER_RESOL_DEF_NS
/* Default timer resolution in ns (if none given by the user) */
#define EM_ODP_TIMER_RESOL_DEF_NS  (1000ULL * 1000ULL * 1ULL) /* 1ms */
#endif

#ifndef EM_ODP_MAX_TIMERS
/* Max number of supported EM timers (ODP timer pools) */
#define EM_ODP_MAX_TIMERS	16
#endif

#ifndef EM_ODP_DEFAULT_TMOS
/* Default number of simultaneous timeouts per timer (handle pool size) */
#define EM_ODP_DEFAULT_TMOS	1000
#endif

#ifndef EM_ODP_DEFAULT_RING_TMOS
/* Default per ring timer number of timeouts. Total comes from runtime config */
#define EM_ODP_DEFAULT_RING_TMOS 100
#endif

#ifndef EM_TIMER_TMO_STATS
/* use 0 to exclude timeout statistics support */
#define EM_TIMER_TMO_STATS	1
#endif

#ifndef EM_TIMER_ACK_TRIES
/* periodic ack() watchdog - how many times to try setting new timeout on late situation.
 * Setting this to 1 effectively disables catch up, i.e. ack will fail if late. Setting this to 0
 * will enable ~forever retry. 3 or bigger is recommended
 */
#define EM_TIMER_ACK_TRIES	5
#endif

#endif /* EM_TIMER_CONF_H_ */
