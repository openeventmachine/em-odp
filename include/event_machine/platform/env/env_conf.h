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
 * Configuration affecting other env files - don't include this file directly,
 * instead #include <environment.h>
 */

#ifndef _ENV_CONF_H_
#define _ENV_CONF_H_

#pragma GCC visibility push(default)

#include <event_machine/api/event_machine_deprecated.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !(defined(ENV_64_BIT) || defined(ENV_32_BIT))
  #error Missing architecture definition. Define ENV_64_BIT or ENV_32_BIT!
#endif

/*
 * Deprecated Environment API Definitions
 *
 * ENV_DEPRECATED_API is derived from EM_DEPRECATED_API (set via the
 * configure option --enable-deprecated). Defined here so that env
 * sub-headers can use it without depending on the full EM API and
 * without relying on environment.h include order.
 *
 * Values:
 *   0 - Deprecated environment API definitions are disabled (default)
 *   1 - Deprecated environment API definitions are enabled
 */
#if defined(EM_DEPRECATED_API) && (EM_DEPRECATED_API == 1)
#define ENV_DEPRECATED_API 1
#else
#define ENV_DEPRECATED_API 0
#endif

#ifdef __cplusplus
}
#endif
#pragma GCC visibility pop
#endif
