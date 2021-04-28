/*
 *   Copyright (c) 2015, Nokia Solutions and Networks
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

#ifndef EM_EO_H_
#define EM_EO_H_

/**
 * @file
 * EM internal EO functions
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#define invalid_eo(eo) ((unsigned int)eo_hdl2idx((eo)) >= EM_MAX_EOS)

em_status_t
eo_init(eo_tbl_t eo_tbl[], eo_pool_t *eo_pool);

em_eo_t
eo_alloc(void);

em_status_t
eo_free(em_eo_t eo);

em_status_t
eo_add_queue(eo_elem_t *const eo_elem, queue_elem_t *const q_elem);

em_status_t
eo_rem_queue(eo_elem_t *const eo_elem, queue_elem_t *const q_elem);

em_status_t
eo_rem_queue_all(eo_elem_t *const eo_elem);

em_status_t
eo_delete_queue_all(eo_elem_t *const eo_elem);

em_status_t
eo_start_local_req(eo_elem_t *const eo_elem,
		   int num_notif, const em_notif_t notif_tbl[]);
em_status_t
eo_start_sync_local_req(eo_elem_t *const eo_elem);

int
eo_start_buffer_events(const em_event_t events[], int num, em_queue_t queue,
		       em_event_group_t event_group);
void
eo_start_send_buffered_events(eo_elem_t *const eo_elem);

em_status_t
eo_stop_local_req(eo_elem_t *const eo_elem,
		  int num_notif, const em_notif_t notif_tbl[]);
em_status_t
eo_stop_sync_local_req(eo_elem_t *const eo_elem);

em_status_t
eo_remove_queue_local_req(eo_elem_t *const eo_elem, queue_elem_t *const q_elem,
			  int num_notif, const em_notif_t notif_tbl[]);
em_status_t
eo_remove_queue_sync_local_req(eo_elem_t *const eo_elem,
			       queue_elem_t *const q_elem);
em_status_t
eo_remove_queue_all_local_req(eo_elem_t *const eo_elem, int delete_queues,
			      int num_notif, const em_notif_t notif_tbl[]);
em_status_t
eo_remove_queue_all_sync_local_req(eo_elem_t *const eo_elem, int delete_queues);

unsigned int
eo_count(void);

size_t eo_get_name(const eo_elem_t *const eo_elem,
		   char name[/*out*/], const size_t maxlen);

static inline int
eo_allocated(const eo_elem_t *const eo_elem)
{
	return !objpool_in_pool(&eo_elem->eo_pool_elem);
}

/** Convert eo handle to eo index */
static inline int
eo_hdl2idx(em_eo_t eo)
{
	return (int)(uintptr_t)eo - 1;
}

/** Convert eo index to eo handle */
static inline em_eo_t
eo_idx2hdl(int eo_idx)
{
	return (em_eo_t)(uintptr_t)(eo_idx + 1);
}

/** Returns EO element associated with EO handle */
static inline eo_elem_t *
eo_elem_get(em_eo_t eo)
{
	const int eo_idx = eo_hdl2idx(eo);
	eo_elem_t *eo_elem;

	if (unlikely((unsigned int)eo_idx > EM_MAX_EOS - 1))
		return NULL;

	eo_elem = &em_shm->eo_tbl.eo_elem[eo_idx];

	return eo_elem;
}

/** Returns the EO element of the currently active EO (if any)*/
static inline eo_elem_t *
eo_elem_current(void)
{
	const queue_elem_t *const q_elem = em_locm.current.q_elem;

	if (unlikely(q_elem == NULL))
		return NULL;

	return q_elem->eo_elem;
}

static inline em_eo_t
eo_current(void)
{
	const queue_elem_t *const q_elem = em_locm.current.q_elem;

	if (unlikely(q_elem == NULL))
		return EM_EO_UNDEF;

	return q_elem->eo;
}

/**
 * EM internal event handler (see em_internal_event.c&h)
 * Handle the internal event requesting a local function call.
 */
void
i_event__eo_local_func_call_req(const internal_event_t *i_ev);

#ifdef __cplusplus
}
#endif

#endif /* EM_EO_H_ */
