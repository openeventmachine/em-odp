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
#ifndef EVENT_MACHINE_ADD_ON_ERROR_H_
#define EVENT_MACHINE_ADD_ON_ERROR_H_

#pragma GCC visibility push(default)

#include <event_machine/api/event_machine_types.h>

/**
 * @file
 *
 * Event Machine add-on types and definitions for exception management
 */

#define EM_ESCOPE_TIMER_CREATE              (EM_ESCOPE_ADD_ON_API_BASE | 0x000)
#define EM_ESCOPE_TIMER_DELETE              (EM_ESCOPE_ADD_ON_API_BASE | 0x001)
#define EM_ESCOPE_TIMER_CUR_TICK            (EM_ESCOPE_ADD_ON_API_BASE | 0x002)
#define EM_ESCOPE_TIMER_GET_ALL             (EM_ESCOPE_ADD_ON_API_BASE | 0x003)
#define EM_ESCOPE_TIMER_GET_ATTR            (EM_ESCOPE_ADD_ON_API_BASE | 0x004)
#define EM_ESCOPE_TIMER_GET_FREQ            (EM_ESCOPE_ADD_ON_API_BASE | 0x005)
#define EM_ESCOPE_TMO_CREATE                (EM_ESCOPE_ADD_ON_API_BASE | 0x006)
#define EM_ESCOPE_TMO_DELETE                (EM_ESCOPE_ADD_ON_API_BASE | 0x007)
#define EM_ESCOPE_TMO_SET_ABS               (EM_ESCOPE_ADD_ON_API_BASE | 0x008)
#define EM_ESCOPE_TMO_SET_REL               (EM_ESCOPE_ADD_ON_API_BASE | 0x009)
#define EM_ESCOPE_TMO_CANCEL                (EM_ESCOPE_ADD_ON_API_BASE | 0x00A)
#define EM_ESCOPE_TMO_ACK                   (EM_ESCOPE_ADD_ON_API_BASE | 0x00B)
#define EM_ESCOPE_TMO_GET_STATE             (EM_ESCOPE_ADD_ON_API_BASE | 0x00C)

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_ADD_ON_ERROR_H_ */
