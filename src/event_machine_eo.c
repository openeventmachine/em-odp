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

#include "em_include.h"

/* Per core (thread) state of em_eo_get_next() */
static ENV_LOCAL unsigned int _eo_tbl_iter_idx;
/* Per core (thread) state of em_eo_queue_get_next() */
static ENV_LOCAL unsigned int _eo_q_iter_idx;
static ENV_LOCAL em_eo_t _eo_q_iter_eo;

em_eo_t
em_eo_create(const char *name,
	     em_start_func_t start,
	     em_start_local_func_t local_start,
	     em_stop_func_t stop,
	     em_stop_local_func_t local_stop,
	     em_receive_func_t receive,
	     const void *eo_ctx)
{
	em_eo_t eo;
	eo_elem_t *eo_elem;

	if (unlikely(start == NULL || stop == NULL || receive == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EO_CREATE,
			       "Mandatory function pointer(s) NULL!");
		return EM_EO_UNDEF;
	}

	eo = eo_alloc();
	if (unlikely(eo == EM_EO_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EO_CREATE,
			       "EO alloc failed!");
		return EM_EO_UNDEF;
	}

	eo_elem = eo_elem_get(eo);
	if (unlikely(eo_elem == NULL)) {
		/* Fatal since eo_alloc() returned 'ok', should never happen */
		INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_ID), EM_ESCOPE_EO_CREATE,
			       "Invalid EO id %" PRI_EO "", eo);
		return EM_EO_UNDEF;
	}

	env_spinlock_lock(&eo_elem->lock);

	/* Store the name */
	if (name != NULL) {
		strncpy(eo_elem->name, name, sizeof(eo_elem->name));
		eo_elem->name[sizeof(eo_elem->name) - 1] = '\0';
	} else {
		eo_elem->name[0] = '\0';
	}

	/* EO's queue list init */
	list_init(&eo_elem->queue_list);
	/* EO start: event buffering list init */
	list_init(&eo_elem->startfn_evlist);

	eo_elem->state = EM_EO_STATE_CREATED;
	eo_elem->start_func = start;
	eo_elem->start_local_func = local_start;
	eo_elem->stop_func = stop;
	eo_elem->stop_local_func = local_stop;
	eo_elem->receive_func = receive;
	eo_elem->error_handler_func = NULL;
	eo_elem->eo_ctx = (void *)(uintptr_t)eo_ctx;
	eo_elem->eo = eo;
	env_atomic32_init(&eo_elem->num_queues);

	env_spinlock_unlock(&eo_elem->lock);

	return eo;
}

em_status_t
em_eo_delete(em_eo_t eo)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	em_status_t status;

	RETURN_ERROR_IF(eo_elem == NULL, EM_ERR_BAD_ID, EM_ESCOPE_EO_DELETE,
			"Invalid EO:%" PRI_EO "!", eo);

	RETURN_ERROR_IF(!eo_allocated(eo_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_DELETE,
			"EO not allocated:%" PRI_EO "", eo);

	RETURN_ERROR_IF(eo_elem->state != EM_EO_STATE_CREATED &&
			eo_elem->state != EM_EO_STATE_ERROR,
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_DELETE,
			"EO invalid state, cannot delete:%d", eo_elem->state);

	status = eo_delete_queue_all(eo_elem);

	RETURN_ERROR_IF(status != EM_OK, status, EM_ESCOPE_EO_DELETE,
			"EO delete: delete queues failed!");

	/* Free EO back into the eo-pool and mark state=EO_STATE_UNDEF */
	status = eo_free(eo);
	RETURN_ERROR_IF(status != EM_OK, status, EM_ESCOPE_EO_DELETE,
			"EO delete failed!");

	return status;
}

size_t
em_eo_get_name(em_eo_t eo, char *name, size_t maxlen)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	size_t len;

	if (name == NULL || maxlen == 0) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_EO_GET_NAME,
			       "Invalid ptr or maxlen (name=0x%" PRIx64 ", maxlen=%zu)",
			       name, maxlen);
		return 0;
	}

	name[0] = '\0';

	if (unlikely(eo_elem == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EO_GET_NAME,
			       "Invalid EO id %" PRI_EO "", eo);
		return 0;
	}

	if (unlikely(!eo_allocated(eo_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_EO_GET_NAME,
			       "EO not allocated:%" PRI_EO "", eo);
		return 0;
	}

	len = strnlen(eo_elem->name, sizeof(eo_elem->name) - 1);
	if (maxlen - 1 < len)
		len = maxlen - 1;

	memcpy(name, eo_elem->name, len);
	name[len] = '\0';

	return len;
}

em_eo_t
em_eo_find(const char *name)
{
	if (name && *name) {
		for (int i = 0; i < EM_MAX_EOS; i++) {
			eo_elem_t *eo_elem = &em_shm->eo_tbl.eo_elem[i];

			if (eo_elem->state != EM_EO_STATE_UNDEF &&
			    !strncmp(name, eo_elem->name, EM_EO_NAME_LEN - 1))
				return eo_elem->eo;
		}
	}
	return EM_EO_UNDEF;
}

em_status_t
em_eo_add_queue(em_eo_t eo, em_queue_t queue,
		int num_notif, const em_notif_t *notif_tbl)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const q_elem = queue_elem_get(queue);
	em_queue_type_t q_type;
	em_status_t err;
	int valid;

	RETURN_ERROR_IF(eo_elem == NULL || q_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_EO_ADD_QUEUE,
			"Invalid args: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);
	RETURN_ERROR_IF(!eo_allocated(eo_elem) || !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_ADD_QUEUE,
			"Not allocated: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);

	q_type = em_queue_get_type(queue);
	valid = q_type == EM_QUEUE_TYPE_ATOMIC ||
		q_type == EM_QUEUE_TYPE_PARALLEL ||
		q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED ||
		q_type == EM_QUEUE_TYPE_LOCAL;
	RETURN_ERROR_IF(!valid, EM_ERR_BAD_CONTEXT, EM_ESCOPE_EO_ADD_QUEUE,
			"Invalid queue type: %" PRI_QTYPE "", q_type);

	err = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_EO_ADD_QUEUE,
			"Invalid notif cfg given!");

	err = eo_add_queue(eo_elem, q_elem);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_EO_ADD_QUEUE,
			"eo_add_queue(Q:%" PRI_QUEUE ") failed", queue);

	if (eo_elem->state == EM_EO_STATE_RUNNING)
		queue_enable(q_elem); /* enabled later in eo-start otherwise */

	if (num_notif > 0) {
		/* Send notifications if requested */
		err = send_notifs(num_notif, notif_tbl);

		RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_EO_ADD_QUEUE,
				"EO:%" PRI_EO " send notif fails", eo);
	}

	return EM_OK;
}

em_status_t
em_eo_add_queue_sync(em_eo_t eo, em_queue_t queue)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const q_elem = queue_elem_get(queue);
	em_queue_type_t q_type;
	em_status_t err;
	int valid, lock_taken;

	RETURN_ERROR_IF(eo_elem == NULL || q_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_EO_ADD_QUEUE_SYNC,
			"Invalid args: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);
	RETURN_ERROR_IF(!eo_allocated(eo_elem) || !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_ADD_QUEUE_SYNC,
			"Not allocated: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);

	q_type = em_queue_get_type(queue);
	valid = q_type == EM_QUEUE_TYPE_ATOMIC ||
		q_type == EM_QUEUE_TYPE_PARALLEL ||
		q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED ||
		q_type == EM_QUEUE_TYPE_LOCAL;
	RETURN_ERROR_IF(!valid, EM_ERR_BAD_CONTEXT, EM_ESCOPE_EO_ADD_QUEUE_SYNC,
			"Invalid queue type: %" PRI_QTYPE "", q_type);

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(!lock_taken, EM_ERR_NOT_FREE,
			EM_ESCOPE_EO_ADD_QUEUE_SYNC,
			"Another sync API function in progress");
	/* locked: */
	err = eo_add_queue(eo_elem, q_elem);
	if (err != EM_OK)
		goto eo_add_queue_sync_err;

	if (eo_elem->state == EM_EO_STATE_RUNNING) {
		err = queue_enable(q_elem); /* otherwise enabled in eo-start */
		if (err != EM_OK)
			goto eo_add_queue_sync_err;
	}

eo_add_queue_sync_err:
	env_spinlock_unlock(&em_shm->sync_api.lock_global);

	RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_EO_ADD_QUEUE_SYNC,
			"Failure: EO:%" PRI_EO " Q:%" PRI_QUEUE "", eo, queue);
	return EM_OK;
}

em_status_t
em_eo_remove_queue(em_eo_t eo, em_queue_t queue,
		   int num_notif, const em_notif_t *notif_tbl)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const q_elem = queue_elem_get(queue);
	em_queue_type_t q_type;
	em_status_t ret;
	int valid;

	RETURN_ERROR_IF(eo_elem == NULL || q_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_EO_REMOVE_QUEUE,
			"Invalid args: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);
	RETURN_ERROR_IF(!eo_allocated(eo_elem) || !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_REMOVE_QUEUE,
			"Not allocated: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);

	q_type = em_queue_get_type(queue);
	valid = q_type == EM_QUEUE_TYPE_ATOMIC ||
		q_type == EM_QUEUE_TYPE_PARALLEL ||
		q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED ||
		q_type == EM_QUEUE_TYPE_LOCAL;
	RETURN_ERROR_IF(!valid, EM_ERR_BAD_CONTEXT, EM_ESCOPE_EO_REMOVE_QUEUE,
			"Invalid queue type: %" PRI_QTYPE "", q_type);

	ret = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE,
			"Invalid notif cfg given!");
	RETURN_ERROR_IF(eo_elem != q_elem->eo_elem,
			EM_ERR_BAD_POINTER, EM_ESCOPE_EO_REMOVE_QUEUE,
			"Can't remove Q:%" PRI_QUEUE ", not added to this EO",
			queue);

	/*
	 * Disable the queue if not already done, dispatcher will drop any
	 * further events. Need to handle events from the queue being processed
	 * in an EO receive function properly still.
	 */
	if (q_elem->state == EM_QUEUE_STATE_READY) {
		ret = queue_disable(q_elem);

		RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE,
				"queue_disable(queue=%" PRI_QUEUE ") fails",
				queue);
	}

	/*
	 * Request each core to run locally the eo_remove_queue_local() function
	 * and when all are done call eo_remove_queue_done_callback().
	 * The callback will finally remove the queue from the EO when it's
	 * known that no core is anymore processing events from that EO/queue.
	 */
	return eo_remove_queue_local_req(eo_elem, q_elem, num_notif, notif_tbl);
}

em_status_t
em_eo_remove_queue_sync(em_eo_t eo, em_queue_t queue)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const q_elem = queue_elem_get(queue);
	em_queue_type_t q_type;
	em_status_t ret;
	int valid, lock_taken;

	RETURN_ERROR_IF(eo_elem == NULL || q_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Invalid args: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);
	RETURN_ERROR_IF(!eo_allocated(eo_elem) || !queue_allocated(q_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Not allocated: EO:%" PRI_EO " Q:%" PRI_QUEUE "",
			eo, queue);

	q_type = em_queue_get_type(queue);
	valid = q_type == EM_QUEUE_TYPE_ATOMIC ||
		q_type == EM_QUEUE_TYPE_PARALLEL ||
		q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED ||
		q_type == EM_QUEUE_TYPE_LOCAL;
	RETURN_ERROR_IF(!valid, EM_ERR_BAD_CONTEXT,
			EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Invalid queue type: %" PRI_QTYPE "", q_type);

	RETURN_ERROR_IF(eo_elem != q_elem->eo_elem,
			EM_ERR_BAD_POINTER, EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Can't remove Q:%" PRI_QUEUE ", not added to this EO",
			queue);

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(!lock_taken, EM_ERR_NOT_FREE,
			EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Another sync API function in progress");
	/* locked: */

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_caller);
	if (unlikely(!lock_taken)) {
		ret = EM_ERR_LIB_FAILED;
		env_spinlock_unlock(&em_shm->sync_api.lock_global);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED,
				      EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
				      "Sync API-caller lock taken");
	}

	/*
	 * Disable the queue if not already done, dispatcher will drop any
	 * further events. Need to handle events from the queue being processed
	 * in an EO receive function properly still.
	 */
	if (q_elem->state == EM_QUEUE_STATE_READY) {
		ret = queue_disable(q_elem);

		if (unlikely(ret != EM_OK))
			goto eo_remove_queue_sync_error;
	}

	/*
	 * Request each core to run locally the eo_remove_queue_local() function
	 * and when all are done call eo_remove_queue_sync_done_callback.
	 * The callback will finally remove the queue from the EO when it's
	 * known that no core is anymore processing events from that EO/queue.
	 */
	ret = eo_remove_queue_sync_local_req(eo_elem, q_elem);
	if (unlikely(ret != EM_OK))
		goto eo_remove_queue_sync_error;

	/*
	 * Spin on the lock until eo_remove_queue_sync_done_callback()
	 * unlocks when the operation has completed.
	 */
	env_spinlock_lock(&em_shm->sync_api.lock_caller);

eo_remove_queue_sync_error:
	env_spinlock_unlock(&em_shm->sync_api.lock_caller);
	env_spinlock_unlock(&em_shm->sync_api.lock_global);

	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Failure: EO:%" PRI_EO " Q:%" PRI_QUEUE "", eo, queue);
	return EM_OK;
}

em_status_t
em_eo_remove_queue_all(em_eo_t eo, int delete_queues,
		       int num_notif, const em_notif_t *notif_tbl)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	em_status_t ret;

	RETURN_ERROR_IF(eo_elem == NULL, EM_ERR_BAD_ID,
			EM_ESCOPE_EO_REMOVE_QUEUE_ALL,
			"Invalid EO:%" PRI_EO "", eo);
	RETURN_ERROR_IF(!eo_allocated(eo_elem), EM_ERR_BAD_STATE,
			EM_ESCOPE_EO_REMOVE_QUEUE_ALL,
			"Not allocated: EO:%" PRI_EO "", eo);
	ret = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE_ALL,
			"Invalid notif cfg given!");

	ret = queue_disable_all(eo_elem);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE_ALL,
			"queue_disable_all() failed!");

	/*
	 * Request each core to run locally the eo_remove_queue_local() function
	 * and when all are done call eo_remove_queue_all_done_callback().
	 * The callback will finally remove the queue from the EO when it's
	 * known that no core is anymore processing events from that EO/queue.
	 */
	return eo_remove_queue_all_local_req(eo_elem, delete_queues,
					     num_notif, notif_tbl);
}

em_status_t
em_eo_remove_queue_all_sync(em_eo_t eo, int delete_queues)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	int lock_taken;
	em_status_t ret;

	RETURN_ERROR_IF(eo_elem == NULL, EM_ERR_BAD_ID,
			EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC,
			"Invalid EO:%" PRI_EO "", eo);
	RETURN_ERROR_IF(!eo_allocated(eo_elem), EM_ERR_BAD_STATE,
			EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC,
			"Not allocated: EO:%" PRI_EO "", eo);

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(!lock_taken, EM_ERR_NOT_FREE,
			EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC,
			"Another sync API function in progress");
	/* locked: */

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_caller);
	if (unlikely(!lock_taken)) {
		env_spinlock_unlock(&em_shm->sync_api.lock_global);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED,
				      EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC,
				      "Sync API-caller lock taken");
	}

	ret = queue_disable_all(eo_elem);
	if (unlikely(ret != EM_OK))
		goto eo_remove_queue_all_sync_error;

	/*
	 * Request each core to run locally the eo_remove_queue_local() function
	 * and when all are done call eo_remove_queue_all_sync_done_callback().
	 * The callback will finally remove the queue from the EO when it's
	 * known that no core is anymore processing events from that EO/queue.
	 */
	ret = eo_remove_queue_all_sync_local_req(eo_elem, delete_queues);
	if (unlikely(ret != EM_OK))
		goto eo_remove_queue_all_sync_error;

	/*
	 * Spin on the lock until eo_remove_queue_all_sync_done_callback
	 * unlocks when the operation has completed.
	 */
	env_spinlock_lock(&em_shm->sync_api.lock_caller);

eo_remove_queue_all_sync_error:
	env_spinlock_unlock(&em_shm->sync_api.lock_caller);
	env_spinlock_unlock(&em_shm->sync_api.lock_global);

	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE_SYNC,
			"Failure: EO:%" PRI_EO "", eo);
	return EM_OK;
}

em_status_t
em_eo_register_error_handler(em_eo_t eo, em_error_handler_t handler)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);

	RETURN_ERROR_IF(eo_elem == NULL,
			EM_ERR_BAD_ID, EM_ESCOPE_EO_REGISTER_ERROR_HANDLER,
			"Invalid EO id %" PRI_EO "", eo);
	RETURN_ERROR_IF(!eo_allocated(eo_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_REGISTER_ERROR_HANDLER,
			"EO not allocated:%" PRI_EO "", eo);

	eo_elem->error_handler_func = handler;

	return EM_OK;
}

em_status_t
em_eo_unregister_error_handler(em_eo_t eo)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);

	RETURN_ERROR_IF(eo_elem == NULL, EM_ERR_BAD_ID,
			EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER,
			"Invalid EO id %" PRI_EO "", eo);
	RETURN_ERROR_IF(!eo_allocated(eo_elem), EM_ERR_BAD_STATE,
			EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER,
			"EO not allocated:%" PRI_EO "", eo);

	eo_elem->error_handler_func = NULL;

	return EM_OK;
}

em_status_t
em_eo_start(em_eo_t eo, em_status_t *result, const em_eo_conf_t *conf,
	    int num_notif, const em_notif_t *notif_tbl)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const save_q_elem = em_locm.current.q_elem;
	queue_elem_t tmp_q_elem;
	em_status_t ret;

	RETURN_ERROR_IF(eo_elem == NULL, EM_ERR_BAD_ID, EM_ESCOPE_EO_START,
			"Invalid EO id %" PRI_EO "", eo);
	RETURN_ERROR_IF(!eo_allocated(eo_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_START,
			"EO not allocated:%" PRI_EO "", eo);
	RETURN_ERROR_IF(eo_elem->state != EM_EO_STATE_CREATED,
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_START,
			"EO invalid state, cannot start:%d", eo_elem->state);
	ret = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_START,
			"Invalid notif cfg given!");

	eo_elem->state = EM_EO_STATE_STARTING;
	/* This core is in the EO start function: buffer all sent events */
	em_locm.start_eo_elem = eo_elem;
	/*
	 * Use a tmp q_elem as the 'current q_elem' to enable calling
	 * em_eo_current() from the EO start functions.
	 * Before returning, restore the original 'current q_elem' from
	 * 'save_q_elem'.
	 */
	memset(&tmp_q_elem, 0, sizeof(tmp_q_elem));
	tmp_q_elem.eo = eo;

	em_locm.current.q_elem = &tmp_q_elem;
	/* Call the global EO start function */
	ret = eo_elem->start_func(eo_elem->eo_ctx, eo, conf);
	/* Restore the original 'current q_elem' */
	em_locm.current.q_elem = save_q_elem;
	em_locm.start_eo_elem = NULL;

	/* Store the return value of the actual EO global start function */
	if (result != NULL)
		*result = ret;

	if (unlikely(ret != EM_OK)) {
		ret = INTERNAL_ERROR(EM_ERR, EM_ESCOPE_EO_START,
				     "EO:%" PRI_EO " start func fails:0x%08x",
				     eo, ret);
		/* user error handler might change error from own eo-start */
		if (ret != EM_OK)
			goto eo_start_error;
	}

	if (eo_elem->start_local_func != NULL) {
		/*
		 * Notifications sent when the local start functions
		 * have completed.
		 */
		ret = eo_start_local_req(eo_elem, num_notif, notif_tbl);

		if (unlikely(ret != EM_OK)) {
			INTERNAL_ERROR(ret, EM_ESCOPE_EO_START,
				       "EO:%" PRI_EO " local start func fails",
				       eo);
			/* Can't allow user err handler to change error here */
			goto eo_start_error;
		}
		/*
		 * Note: Return here, queues will be enabled after the local
		 * start funcs complete.
		 * EO state changed to 'EO_STATE_STARTED' after successful
		 * completion of EO local starts on all cores.
		 */
		return EM_OK;
	}

	/*
	 * Enable all the EO's queues.
	 * Note: if local start functions are given then enable can be done only
	 *       after they have been run on each core.
	 */
	ret = queue_enable_all(eo_elem);
	if (unlikely(ret != EM_OK))
		goto eo_start_error;

	eo_elem->state = EM_EO_STATE_RUNNING;

	/* Send events buffered during the EO-start/local-start functions */
	eo_start_send_buffered_events(eo_elem);

	if (num_notif > 0) {
		/* Send notifications if requested */
		ret = send_notifs(num_notif, notif_tbl);

		if (unlikely(ret != EM_OK)) {
			ret = INTERNAL_ERROR(ret, EM_ESCOPE_EO_START,
					     "EO:%" PRI_EO " send notif fails",
					     eo);
			/* user error handler might change error */
			if (ret != EM_OK)
				goto eo_start_error;
		}
	}

	return EM_OK;

eo_start_error:
	/* roll back state to allow EO delete */
	eo_elem->state = EM_EO_STATE_ERROR;
	return ret;
}

em_status_t
em_eo_start_sync(em_eo_t eo, em_status_t *result, const em_eo_conf_t *conf)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const save_q_elem = em_locm.current.q_elem;
	queue_elem_t tmp_q_elem;
	em_status_t ret;
	int lock_taken;

	RETURN_ERROR_IF(eo_elem == NULL, EM_ERR_BAD_ID, EM_ESCOPE_EO_START_SYNC,
			"Invalid EO id %" PRI_EO "", eo);
	RETURN_ERROR_IF(!eo_allocated(eo_elem),
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_START_SYNC,
			"EO not allocated:%" PRI_EO "", eo);
	RETURN_ERROR_IF(eo_elem->state != EM_EO_STATE_CREATED,
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_START_SYNC,
			"EO invalid state, cannot start:%d", eo_elem->state);

	eo_elem->state = EM_EO_STATE_STARTING;
	/* This core is in the EO start function: buffer all sent events */
	em_locm.start_eo_elem = eo_elem;
	/*
	 * Use a tmp q_elem as the 'current q_elem' to enable calling
	 * em_eo_current() from the EO start functions.
	 * Before returning, restore the original 'current q_elem' from
	 * 'save_q_elem'.
	 */
	memset(&tmp_q_elem, 0, sizeof(tmp_q_elem));
	tmp_q_elem.eo = eo;
	em_locm.current.q_elem = &tmp_q_elem;
	/* Call the global EO start function */
	ret = eo_elem->start_func(eo_elem->eo_ctx, eo, conf);
	/* Restore the original 'current q_elem' */
	em_locm.current.q_elem = save_q_elem;
	em_locm.start_eo_elem = NULL;

	/* Store the return value of the actual EO global start function */
	if (result != NULL)
		*result = ret;

	if (unlikely(ret != EM_OK)) {
		ret = INTERNAL_ERROR(EM_ERR, EM_ESCOPE_EO_START_SYNC,
				     "EO:%" PRI_EO " start func fails:0x%08x",
				     eo, ret);
		/* user error handler might change error from own eo-start */
		if (ret != EM_OK) {
			/* roll back state to allow EO delete */
			eo_elem->state = EM_EO_STATE_ERROR;
			return ret;
		}
	}

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(!lock_taken, EM_ERR_NOT_FREE, EM_ESCOPE_EO_START_SYNC,
			"Another sync API function in progress");
	/* locked: */

	if (eo_elem->start_local_func != NULL) {
		lock_taken =
		env_spinlock_trylock(&em_shm->sync_api.lock_caller);

		if (unlikely(!lock_taken)) {
			ret = EM_ERR_LIB_FAILED;
			INTERNAL_ERROR(ret, EM_ESCOPE_EO_START_SYNC,
				       "EO:%" PRI_EO " caller sync lock fails",
				       eo);
			/* Can't allow user err handler to change error here */
			goto eo_start_sync_error;
		}

		em_locm.start_eo_elem = eo_elem;
		em_locm.current.q_elem = &tmp_q_elem;
		/* Call the local start on this core */
		ret = eo_elem->start_local_func(eo_elem->eo_ctx, eo);
		/* Restore the original 'current q_elem' */
		em_locm.current.q_elem = save_q_elem;
		em_locm.start_eo_elem = NULL;

		if (unlikely(ret != EM_OK)) {
			INTERNAL_ERROR(ret, EM_ESCOPE_EO_START_SYNC,
				       "EO:%" PRI_EO " local start func fails",
				       eo);
			env_spinlock_unlock(&em_shm->sync_api.lock_caller);
			/* Can't allow user err handler to change error here */
			goto eo_start_sync_error;
		}

		ret = eo_start_sync_local_req(eo_elem);
		if (unlikely(ret != EM_OK)) {
			INTERNAL_ERROR(ret, EM_ESCOPE_EO_START_SYNC,
				       "EO:%" PRI_EO " eo_start_sync_local_req",
				       eo);
			env_spinlock_unlock(&em_shm->sync_api.lock_caller);
			/* Can't allow user err handler to change error here */
			goto eo_start_sync_error;
		}

		/*
		 * Spin on the lock until eo_start_sync_done_callback()
		 * unlocks when the operation has completed.
		 */
		env_spinlock_lock(&em_shm->sync_api.lock_caller);
		env_spinlock_unlock(&em_shm->sync_api.lock_caller);

		/* Send events buffered during the EO-start/local-start funcs */
		eo_start_send_buffered_events(eo_elem);

		env_spinlock_unlock(&em_shm->sync_api.lock_global);
		/*
		 * EO state changed to 'EO_STATE_RUNNING' after successful
		 * completion of EO local starts on all cores.
		 */
		return EM_OK;
	}

	/*
	 * Enable all the EO's queues.
	 * Note: if local start functions are given then enable can be done only
	 *       after they have been run on each core.
	 */
	ret = queue_enable_all(eo_elem);
	if (unlikely(ret != EM_OK))
		goto eo_start_sync_error;

	eo_elem->state = EM_EO_STATE_RUNNING;

	/* Send events buffered during the EO-start/local-start functions */
	eo_start_send_buffered_events(eo_elem);

	env_spinlock_unlock(&em_shm->sync_api.lock_global);
	return EM_OK;

eo_start_sync_error:
	/* roll back state to allow EO delete */
	eo_elem->state = EM_EO_STATE_ERROR;
	env_spinlock_unlock(&em_shm->sync_api.lock_global);

	return ret;
}

em_status_t
em_eo_stop(em_eo_t eo, int num_notif, const em_notif_t *notif_tbl)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	em_status_t ret;

	RETURN_ERROR_IF(eo_elem == NULL || !eo_allocated(eo_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_EO_STOP,
			"Invalid EO:%" PRI_EO "", eo);
	RETURN_ERROR_IF(eo_elem->state != EM_EO_STATE_RUNNING,
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_STOP,
			"EO invalid state, cannot stop:%d", eo_elem->state);
	ret = check_notif_tbl(num_notif, notif_tbl);
	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_STOP,
			"Invalid notif cfg given!");

	eo_elem->state = EM_EO_STATE_STOPPING;

	/*
	 * Disable all queues.
	 * It doesn't matter if some of the queues are already disabled.
	 */
	queue_disable_all(eo_elem);

	/*
	 * Notifications sent when the local start functions
	 * have completed. EO global stop called when all local stops have
	 * been completed. EO state changed to 'stopped' only after completing
	 * the EO global stop function.
	 */
	ret = eo_stop_local_req(eo_elem, num_notif, notif_tbl);

	if (unlikely(ret != EM_OK)) {
		eo_elem->state = EM_EO_STATE_ERROR;
		INTERNAL_ERROR(ret, EM_ESCOPE_EO_STOP,
			       "EO:%" PRI_EO " local stop func fails", eo);
		/* Can't allow user err handler to change error here */
		return ret;
	}

	return EM_OK;
}

em_status_t
em_eo_stop_sync(em_eo_t eo)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	queue_elem_t *const save_q_elem = em_locm.current.q_elem;
	queue_elem_t tmp_q_elem;
	em_status_t ret;
	int lock_taken;

	RETURN_ERROR_IF(eo_elem == NULL || !eo_allocated(eo_elem),
			EM_ERR_BAD_ID, EM_ESCOPE_EO_STOP_SYNC,
			"Invalid EO:%" PRI_EO "", eo);
	RETURN_ERROR_IF(eo_elem->state != EM_EO_STATE_RUNNING,
			EM_ERR_BAD_STATE, EM_ESCOPE_EO_STOP_SYNC,
			"EO invalid state, cannot stop:%d", eo_elem->state);

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_global);
	RETURN_ERROR_IF(!lock_taken, EM_ERR_NOT_FREE,
			EM_ESCOPE_EO_STOP_SYNC,
			"Another sync API function in progress");
	/* locked: */

	lock_taken = env_spinlock_trylock(&em_shm->sync_api.lock_caller);
	if (unlikely(!lock_taken)) {
		ret = EM_ERR_LIB_FAILED;
		goto eo_stop_sync_error;
	}

	eo_elem->state = EM_EO_STATE_STOPPING;

	/*
	 * Disable all queues.
	 * It doesn't matter if some of the queues are already disabled.
	 */
	ret = queue_disable_all(eo_elem);
	if (unlikely(ret != EM_OK)) {
		env_spinlock_unlock(&em_shm->sync_api.lock_caller);
		goto eo_stop_sync_error;
	}

	/*
	 * Use a tmp q_elem as the 'current q_elem' to enable calling
	 * em_eo_current() from the EO stop functions.
	 * Before returning, restore the original 'current q_elem' from
	 * 'save_q_elem'.
	 */
	memset(&tmp_q_elem, 0, sizeof(tmp_q_elem));
	tmp_q_elem.eo = eo;

	if (eo_elem->stop_local_func != NULL) {
		em_locm.current.q_elem = &tmp_q_elem;
		/* Call the local stop on this core */
		ret = eo_elem->stop_local_func(eo_elem->eo_ctx, eo_elem->eo);
		/* Restore the original 'current q_elem' */
		em_locm.current.q_elem = save_q_elem;

		if (unlikely(ret != EM_OK)) {
			env_spinlock_unlock(&em_shm->sync_api.lock_caller);
			goto eo_stop_sync_error;
		}
	}

	/*
	 * Notifications sent when the local stop functions have completed.
	 * EO global stop called when all local stops have been completed.
	 * EO state changed to 'stopped' only after completing the EO global
	 * stop function.
	 */
	ret = eo_stop_sync_local_req(eo_elem);

	if (unlikely(ret != EM_OK)) {
		eo_elem->state = EM_EO_STATE_ERROR;
		INTERNAL_ERROR(ret, EM_ESCOPE_EO_STOP_SYNC,
			       "EO:%" PRI_EO " local stop func fails", eo);
		/* Can't allow user err handler to change error here */
		env_spinlock_unlock(&em_shm->sync_api.lock_caller);
		goto eo_stop_sync_error;
	}

	/*
	 * Spin on the lock until eo_stop_sync_done_callback()
	 * unlocks when the operation has completed.
	 */
	env_spinlock_lock(&em_shm->sync_api.lock_caller);
	env_spinlock_unlock(&em_shm->sync_api.lock_caller);

	/* Change state here to allow em_eo_delete() from EO global stop */
	eo_elem->state = EM_EO_STATE_CREATED; /* == stopped */

	env_spinlock_unlock(&em_shm->sync_api.lock_global);

	em_locm.current.q_elem = &tmp_q_elem;
	/*
	 * Call the Global EO stop function now that all
	 * EO local stop functions are done.
	 */
	ret = eo_elem->stop_func(eo_elem->eo_ctx, eo);
	/* Restore the original 'current q_elem' */
	em_locm.current.q_elem = save_q_elem;

	RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_STOP_SYNC,
			"EO:%" PRI_EO " stop-func failed", eo);
	/*
	 * Note: the EO might not be available after this if the EO global stop
	 * called em_eo_delete()!
	 */
	return EM_OK;

eo_stop_sync_error:
	env_spinlock_unlock(&em_shm->sync_api.lock_global);

	return INTERNAL_ERROR(ret, EM_ESCOPE_EO_STOP_SYNC,
			      "Failure: EO:%" PRI_EO "", eo);
}

em_eo_t
em_eo_current(void)
{
	queue_elem_t *const q_elem = em_locm.current.q_elem;

	if (unlikely(q_elem == NULL))
		return EM_EO_UNDEF;

	return q_elem->eo;
}

void *
em_eo_get_context(em_eo_t eo)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	em_eo_state_t eo_state;

	if (unlikely(eo_elem == NULL || !eo_allocated(eo_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EO_GET_CONTEXT,
			       "Invalid EO:%" PRI_EO "", eo);
		return NULL;
	}

	eo_state = eo_elem->state;
	if (unlikely(eo_state < EM_EO_STATE_CREATED)) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_EO_GET_CONTEXT,
			       "Invalid EO state: EO:%" PRI_EO " state:%d",
				eo, eo_state);
		return NULL;
	}

	return eo_elem->eo_ctx;
}

em_eo_state_t
em_eo_get_state(em_eo_t eo)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);

	if (unlikely(eo_elem == NULL || !eo_allocated(eo_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EO_GET_STATE,
			       "Invalid EO:%" PRI_EO "", eo);
		return EM_EO_STATE_UNDEF;
	}

	return eo_elem->state;
}

em_eo_t
em_eo_get_first(unsigned int *num)
{
	_eo_tbl_iter_idx = 0; /* reset iteration */
	const unsigned int eo_cnt = eo_count();

	if (num)
		*num = eo_cnt;

	if (eo_cnt == 0) {
		_eo_tbl_iter_idx = EM_MAX_EOS; /* UNDEF = _get_next() */
		return EM_EO_UNDEF;
	}

	/* find first */
	while (!eo_allocated(&em_shm->eo_tbl.eo_elem[_eo_tbl_iter_idx])) {
		_eo_tbl_iter_idx++;
		if (_eo_tbl_iter_idx >= EM_MAX_EOS)
			return EM_EO_UNDEF;
	}

	return eo_idx2hdl(_eo_tbl_iter_idx);
}

em_eo_t
em_eo_get_next(void)
{
	if (_eo_tbl_iter_idx >= EM_MAX_EOS - 1)
		return EM_EO_UNDEF;

	_eo_tbl_iter_idx++;

	/* find next */
	while (!eo_allocated(&em_shm->eo_tbl.eo_elem[_eo_tbl_iter_idx])) {
		_eo_tbl_iter_idx++;
		if (_eo_tbl_iter_idx >= EM_MAX_EOS)
			return EM_EO_UNDEF;
	}

	return eo_idx2hdl(_eo_tbl_iter_idx);
}

em_queue_t
em_eo_queue_get_first(unsigned int *num, em_eo_t eo)
{
	eo_elem_t *const eo_elem = eo_elem_get(eo);

	if (unlikely(eo_elem == NULL || !eo_allocated(eo_elem))) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EO_QUEUE_GET_FIRST,
			       "Invalid EO:%" PRI_EO "", eo);
		if (num)
			*num = 0;
		return EM_QUEUE_UNDEF;
	}

	const unsigned int num_queues = env_atomic32_get(&eo_elem->num_queues);

	if (num)
		*num = num_queues;

	if (num_queues == 0) {
		_eo_q_iter_idx = EM_MAX_QUEUES; /* UNDEF = _get_next() */
		return EM_QUEUE_UNDEF;
	}

	/*
	 * An 'eo_elem' contains a linked list with all it's queues. That list
	 * might be modified while processing this iteration, so instead we just
	 * go through the whole queue table.
	 * This is potentially a slow implementation and perhaps worth
	 * re-thinking?
	 */
	queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;

	_eo_q_iter_idx = 0; /* reset list */
	_eo_q_iter_eo = eo;

	/* find first */
	while (!queue_allocated(&queue_tbl->queue_elem[_eo_q_iter_idx]) ||
	       queue_tbl->queue_elem[_eo_q_iter_idx].eo != _eo_q_iter_eo) {
		_eo_q_iter_idx++;
		if (_eo_q_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
	}

	return queue_idx2hdl(_eo_q_iter_idx);
}

em_queue_t
em_eo_queue_get_next(void)
{
	if (_eo_q_iter_idx >= EM_MAX_QUEUES - 1)
		return EM_QUEUE_UNDEF;

	_eo_q_iter_idx++;

	queue_tbl_t *const queue_tbl = &em_shm->queue_tbl;

	/* find next */
	while (!queue_allocated(&queue_tbl->queue_elem[_eo_q_iter_idx]) ||
	       queue_tbl->queue_elem[_eo_q_iter_idx].eo != _eo_q_iter_eo) {
		_eo_q_iter_idx++;
		if (_eo_q_iter_idx >= EM_MAX_QUEUES)
			return EM_QUEUE_UNDEF;
	}

	return queue_idx2hdl(_eo_q_iter_idx);
}
