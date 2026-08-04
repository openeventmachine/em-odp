/*
 *   Copyright (c) 2026, Nokia Solutions and Networks
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
 * Weak default implementations of the event-chaining output hooks
 * event_send_device() / event_send_device_multi().
 *
 * These live in their own translation unit, deliberately built without LTO
 * (see src/Makefile.am), because they are weak symbols that applications
 * override at link time. GCC <= 13 mis-binds such an override to the weak
 * default when the library is built with LTO and linked statically: the LTO
 * plugin lets the in-IR weak definition prevail over the application's strong,
 * non-IR override (fixed in GCC 14). Keeping the weak defaults out of the LTO
 * IR makes the override resolve reliably regardless of toolchain and link mode.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_chaining.h"
#include "em_error.h"
#include "em_queue_types.h"

/**
 * Default implementation is declared as a weak symbol, meaning that the
 * user can override the function during linking with another implementation.
 */
__attribute__((weak))
em_status_t event_send_device(em_event_t event, em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	(void)event;

	return INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED,
			      EM_ESCOPE_EVENT_SEND_DEVICE,
			      "No %s() function given!\t"
			      "device:0x%" PRIx16 " Q-id:0x%" PRIx16 "\n",
			      __func__, iq.device_id, iq.queue_id);
}

/**
 * Default implementation is declared as a weak symbol, meaning that the
 * user can override the function during linking with another implementation.
 */
__attribute__((weak))
int event_send_device_multi(const em_event_t events[], int num, em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	(void)events;
	(void)num;

	INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED,
		       EM_ESCOPE_EVENT_SEND_DEVICE_MULTI,
		       "No %s() function given!\t"
		       "device:0x%" PRIx16 " Q-id:0x%" PRIx16 "\n",
		       __func__, iq.device_id, iq.queue_id);
	return 0;
}
