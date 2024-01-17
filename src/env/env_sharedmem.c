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

#include <odp_api.h>
#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

/**
 * Shared memory header
 */
typedef struct {
	odp_shm_t odp_shm;
} env_shm_hdr_t;

/**
 * Shared memory buffer incl. header
 */
typedef struct {
	env_shm_hdr_t hdr;
	/**
	 * Actual data buffer returned to user, needs to be aligned to cache
	 * line, even if wasting a bit of memory in the hdr, to ensure that
	 * the user data is properly aligned.
	 */
	uint8_t data[] ENV_CACHE_LINE_ALIGNED;
} env_shm_buf_t;

ODP_STATIC_ASSERT(offsetof(env_shm_buf_t, data) == ENV_CACHE_LINE_SIZE,
		  "ENV_SHM_ALIGNMENT_ERROR");

static void *
shared_reserve(const char *name, size_t size)
{
	env_shm_buf_t *env_shm_buf;
	odp_shm_t shm;
	uint32_t flags = 0;

	odp_shm_capability_t shm_capa;
	int ret = odp_shm_capability(&shm_capa);

	if (unlikely(ret))
		return NULL;

	if (shm_capa.flags & ODP_SHM_SINGLE_VA)
		flags |= ODP_SHM_SINGLE_VA;

	shm = odp_shm_reserve(name, offsetof(env_shm_buf_t, data) + size,
			      ODP_CACHE_LINE_SIZE, flags);

	if (unlikely(shm == ODP_SHM_INVALID))
		return NULL;

	env_shm_buf = odp_shm_addr(shm);
	env_shm_buf->hdr.odp_shm = shm;

	return (void *)env_shm_buf->data;
}

void *
env_shared_reserve(const char *name, size_t size)
{
	if (unlikely(name == NULL || size == 0))
		return NULL;

	return shared_reserve(name, size);
}

void *
env_shared_lookup(const char *name)
{
	env_shm_buf_t *env_shm_buf;
	odp_shm_t shm;

	if (unlikely(name == NULL))
		return NULL;

	shm = odp_shm_lookup(name);
	if (unlikely(shm == ODP_SHM_INVALID))
		return NULL;

	env_shm_buf = odp_shm_addr(shm);

	return (void *)env_shm_buf->data;
}

void *
env_shared_malloc(size_t size)
{
	if (unlikely(size == 0))
		return NULL;

	return shared_reserve(NULL, size);
}

void
env_shared_free(void *buf)
{
	const env_shm_buf_t *env_shm_buf;
	odp_shm_t shm;
	int ret;

	if (buf == NULL)
		return;

	env_shm_buf = (env_shm_buf_t *)((uintptr_t)buf -
					offsetof(env_shm_buf_t, data));

	shm = env_shm_buf->hdr.odp_shm;
	ret = odp_shm_free(shm);
	if (unlikely(ret != 0))
		env_panic("");
}
