/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_core.h"
#include "em_core_types.h"
#include "em_error.h"
#include "em_mem.h"

/* Per core (thread) state of em_core_id_first/next() */
static ENV_LOCAL struct {
	int idx;
	em_core_mask_t *src_core_mask;
} _core_id_iter = {.idx = -1, .src_core_mask = NULL};

int em_core_id(void)
{
	return em_locm.core_id;
}

uint32_t em_core_count(void)
{
	return odp_atomic_load_u32(&em_shm->core_map.current_core_count);
}

uint32_t em_core_count_max(void)
{
	return em_shm->conf.core_count;
}

uint32_t em_core_type_count(em_core_type_t core_type)
{
	core_map_t *const core_map = &em_shm->core_map;
	int count;

	switch (core_type) {
	case EM_CORE_TYPE_WORKER:
		count = odp_atomic_load_u32(&core_map->worker_core_count);
		break;
	case EM_CORE_TYPE_CONTROL:
		count = odp_atomic_load_u32(&core_map->control_core_count);
		break;
	case EM_CORE_TYPE_EXTERNAL:
		count = odp_atomic_load_u32(&core_map->external_thread_count);
		break;
	case EM_CORE_TYPE_UNDEF:
		count = odp_atomic_load_u32(&core_map->current_core_count);
		break;
	default:
		count = 0;
		break;
	}

	return count;
}

int em_core_id_first(em_core_type_t core_type, uint32_t *num /*out*/)
{
	core_map_t *const core_map = &em_shm->core_map;
	uint32_t current_core_count;
	odp_atomic_u32_t *src_core_count;
	em_core_mask_t *src_core_mask;
	em_core_mask_t dst_core_mask;

	if (unlikely(!(core_type == EM_CORE_TYPE_UNDEF ||
		       core_type == EM_CORE_TYPE_CONTROL ||
		       core_type == EM_CORE_TYPE_WORKER))) {
		_core_id_iter.idx =  -1;
		if (num)
			*num = 0;
		return -1;
	}

	if (core_type == EM_CORE_TYPE_WORKER) {
		src_core_count = &core_map->worker_core_count;
		src_core_mask = &core_map->rwlocked.worker_mask;
	} else if (core_type == EM_CORE_TYPE_CONTROL) {
		src_core_count = &core_map->control_core_count;
		src_core_mask = &core_map->rwlocked.control_mask;
	} else {
		src_core_count = &core_map->current_core_count;
		src_core_mask = &core_map->rwlocked.logic_mask;
	}

	odp_rwlock_read_lock(&core_map->rwlock);
	current_core_count = odp_atomic_load_u32(src_core_count);
	em_core_mask_copy(&dst_core_mask, src_core_mask);
	odp_rwlock_read_unlock(&core_map->rwlock);

	if (num)
		*num = current_core_count;

	if (current_core_count == 0) {
		_core_id_iter.idx = -1;
		return -1;
	}

	_core_id_iter.idx = em_core_mask_first(&dst_core_mask);
	_core_id_iter.src_core_mask = src_core_mask;

	return _core_id_iter.idx;
}

int em_core_id_next(void)
{
	core_map_t *const core_map = &em_shm->core_map;
	const em_core_mask_t *src_core_mask = _core_id_iter.src_core_mask;
	em_core_mask_t dst_core_mask;

	if (unlikely(_core_id_iter.idx < 0))
		return -1;

	odp_rwlock_read_lock(&core_map->rwlock);
	em_core_mask_copy(&dst_core_mask, src_core_mask);
	odp_rwlock_read_unlock(&core_map->rwlock);

	_core_id_iter.idx = em_core_mask_next(&dst_core_mask, _core_id_iter.idx);
	if (unlikely(_core_id_iter.idx < 0)) {
		_core_id_iter.idx = -1;
		return -1;
	}

	return _core_id_iter.idx;
}

uint32_t em_core_ids_available(em_core_type_t core_type,
			       em_core_mask_t *core_mask /*out*/)
{
	core_map_t *const core_map = &em_shm->core_map;
	uint32_t current_core_count;
	odp_atomic_u32_t *src_core_count;
	const em_core_mask_t *src_core_mask;

	if (unlikely(!core_mask ||
		     !(core_type == EM_CORE_TYPE_UNDEF ||
		       core_type == EM_CORE_TYPE_CONTROL ||
		       core_type == EM_CORE_TYPE_WORKER))) {
		if (core_mask)
			em_core_mask_zero(core_mask);
		return 0;
	}

	if (core_type == EM_CORE_TYPE_WORKER) {
		src_core_count = &core_map->worker_core_count;
		src_core_mask = &core_map->rwlocked.worker_mask;
	} else if (core_type == EM_CORE_TYPE_CONTROL) {
		src_core_count = &core_map->control_core_count;
		src_core_mask = &core_map->rwlocked.control_mask;
	} else {
		src_core_count = &core_map->current_core_count;
		src_core_mask = &core_map->rwlocked.logic_mask;
	}

	odp_rwlock_read_lock(&core_map->rwlock);
	current_core_count = odp_atomic_load_u32(src_core_count);
	em_core_mask_copy(core_mask, src_core_mask);
	odp_rwlock_read_unlock(&core_map->rwlock);

	return current_core_count;
}

em_core_type_t em_core_type(void)
{
	return em_locm.conf_local.core_type;
}
