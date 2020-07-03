/*
 *   Copyright (c) 2013-2019, Nokia Solutions and Networks
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
 * env helper include file - don't include this file directly,
 * instead #include <environment.h>
 */

#ifndef _ENV_SHAREDMEM_H_
#define _ENV_SHAREDMEM_H_

#pragma GCC visibility push(default)

/**
 * Shared memory allocation routines
 */

#define ENV_SHARED_NAMESIZE 32

/**
 * Reserve memory that can be shared by multiple processes.
 *
 * @param name  Name of the shared memory block
 * @param size  Size requested by the user, actual size might be larger
 *
 * @note Not for use in the fast path, func call overhead might be too much.
 *       Buffers allocated at setup can be used in fast path processing though.
 */
void *env_shared_reserve(const char *name, size_t size);

/**
 * Lookup shared memory previously reserved by env_shared_reserve().
 *
 * @param name  Name of the shared memory block to look for
 *
 * @note Not for use in the fast path, func call overhead might be too much.
 *       Buffers allocated at setup can be used in fast path processing though.
 *
 * @see env_shared_reserve()
 */
void *env_shared_lookup(const char *name);

/**
 * Allocate shared memory.
 *
 * @note Not for use in the fast path, func call overhead might be too much.
 *       Buffers allocated at setup can be used in fast path processing though.
 */
void *env_shared_malloc(size_t size);

/**
 * Frees memory previously allocated by env_shared_reserve() or
 * env_shared_malloc()
 *
 * @note Not for use in the fast path, func call overhead might be too much.
 */
void env_shared_free(void *buf);

#pragma GCC visibility pop
#endif /* _ENV_SHAREDMEM_H_ */
