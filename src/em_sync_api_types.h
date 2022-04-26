/*
 *   Copyright (c) 2018, Nokia Solutions and Networks
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

#ifndef EM_SYNC_API_TYPES_H_
#define EM_SYNC_API_TYPES_H_

/**
 * @file
 * EM internal types & definitions for synchronous API functions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef sync_api_t
 * EM-core local sync-API info/state.
 */
typedef struct {
	struct {
		/** Core-local ctrl queue */
		em_queue_t core_unsched_queue;
		/** Queue element for the core-local unsched queue above */
		queue_elem_t *core_unsched_qelem;
		/** Corresponding ODP plain queue */
		odp_queue_t core_odp_plain_queue;

		/** Shared ctrl queue */
		em_queue_t shared_unsched_queue;
		/** Queue element for the shared unsched queue above */
		queue_elem_t *shared_unsched_qelem;
		/** Corresponding ODP plain queue */
		odp_queue_t shared_odp_plain_queue;
	} ctrl_poll;
	/** Indication whether a sync-API is in progress on this core */
	bool in_progress;
} sync_api_t;

#ifdef __cplusplus
}
#endif

#endif /* EM_SYNC_API_TYPES_H_ */
