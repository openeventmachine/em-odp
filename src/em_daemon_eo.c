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

#define DAEMON_ERROR(error, ...) \
	INTERNAL_ERROR((error), EM_ESCOPE_DAEMON, ## __VA_ARGS__)

static em_status_t
daemon_eo_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t
daemon_eo_stop(void *eo_ctx, em_eo_t eo);
static void
daemon_eo_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *q_ctx);

void
daemon_eo_create(void)
{
	em_eo_t eo;
	em_status_t stat;
	em_status_t stat_eo_start = EM_ERROR;

	eo = em_eo_create("daemon-eo", daemon_eo_start, NULL /*start_local*/,
			  daemon_eo_stop, NULL /*stop_local*/,
			  daemon_eo_receive, NULL);
	if (eo == EM_EO_UNDEF)
		DAEMON_ERROR(EM_FATAL(EM_ERR_BAD_ID), "daemon-eo create fail");

	/* Store the EO in shared memory */
	em_shm->daemon.eo = eo;

	stat = em_eo_start(eo, &stat_eo_start, NULL, 0, NULL);
	if (stat != EM_OK || stat_eo_start != EM_OK)
		DAEMON_ERROR(EM_FATAL(EM_ERR_LIB_FAILED),
			     "daemon-eo start failed!");
}

void
daemon_eo_shutdown(void)
{
	const int core = em_core_id();
	const em_eo_t eo = em_shm->daemon.eo;
	eo_elem_t *const eo_elem = eo_elem_get(eo);
	em_status_t stat;

	EM_PRINT("%s() on EM-core %d\n", __func__, core);

	if (unlikely(eo_elem == NULL)) {
		DAEMON_ERROR(EM_FATAL(EM_ERR_BAD_ID),
			     "daemon-eo handle:%" PRI_EO " invalid!", eo);
		return;
	}

	/*
	 * Stop the daemon-EO, i.e. call the daemon-EO global stop func.
	 * Note: cannot call normal API func em_eo_stop() since that would use
	 * internal ctrl events that might not be dispatched during shutdown.
	 */
	/* Change state here to allow em_eo_delete() from EO stop func */
	eo_elem->state = EM_EO_STATE_CREATED; /* == EO_STATE_STOPPED */
	stat = eo_elem->stop_func(eo_elem->eo_ctx, eo);
	if (stat != EM_OK)
		DAEMON_ERROR(EM_FATAL(EM_ERR_LIB_FAILED),
			     "daemon-eo stop/delete failed!");
}

static em_status_t
daemon_eo_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	const int num_cores = em_core_count();
	char qgrp_name[] = EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME;
	char q_name[EM_QUEUE_NAME_LEN] = "EM_Q_INTERNAL_000000";
	em_queue_group_t queue_group;
	em_queue_t queue;
	em_queue_t shared_queue;
	em_core_mask_t mask;
	em_status_t stat;
	const char *err_str = "";
	int i;

	(void)eo_ctx;
	(void)conf;

	EM_PRINT("daemon-eo starting!\n");

	/*
	 * Create static internal queue used for internal EM messaging.
	 * Cannot use em_queue_create_static() here since the requested handle
	 * 'SHARED_INTERNAL_QUEUE' lies outside of the normal static range.
	 */
	shared_queue = queue_id2hdl(SHARED_INTERNAL_QUEUE);
	queue = queue_create("EM_Q_INTERNAL_SHARED", EM_QUEUE_TYPE_ATOMIC,
			     INTERNAL_QUEUE_PRIORITY, EM_QUEUE_GROUP_DEFAULT,
			     shared_queue, EM_ATOMIC_GROUP_UNDEF,
			     NULL /* use default queue config */, &err_str);
	if (queue == EM_QUEUE_UNDEF || queue != shared_queue)
		return EM_FATAL(EM_ERR_NOT_FREE);

	stat = em_eo_add_queue(eo, shared_queue, 0, NULL);
	if (stat != EM_OK)
		return EM_FATAL(stat);

	for (i = 0; i < num_cores; i++) {
		em_queue_t queue_req;

		em_core_mask_zero(&mask);
		em_core_mask_set(i, &mask);
		core_queue_grp_name(qgrp_name, i);

		/*
		 * Create per-core queue groups for core-specific queues
		 */
		queue_group = em_queue_group_create(qgrp_name, &mask, 0, NULL);
		if (unlikely(queue_group == EM_QUEUE_GROUP_UNDEF))
			return EM_FATAL(EM_ERR_ALLOC_FAILED);

		queue_req = queue_id2hdl(FIRST_INTERNAL_QUEUE + i);
		snprintf(&q_name[14], EM_QUEUE_NAME_LEN - 14,
			 "%" PRI_QUEUE "", queue_req);
		q_name[EM_QUEUE_NAME_LEN - 1] = '\0';

		/*
		 * Create static internal per-core queues used for internal
		 * EM messaging. Cannot use em_queue_create_static() here since
		 * the requested handles lies outside of the normal static
		 * range.
		 */
		queue = queue_create(q_name, EM_QUEUE_TYPE_ATOMIC,
				     INTERNAL_QUEUE_PRIORITY, queue_group,
				     queue_req, EM_ATOMIC_GROUP_UNDEF,
				     NULL, /* use default queue config */
				     &err_str);
		if (unlikely(queue == EM_QUEUE_UNDEF || queue != queue_req))
			return EM_FATAL(EM_ERR_NOT_FREE);

		stat = em_eo_add_queue(eo, queue, 0, NULL);
		if (unlikely(stat != EM_OK))
			return EM_FATAL(stat);
	}

	return EM_OK;
}

static em_status_t
daemon_eo_stop(void *eo_ctx, em_eo_t eo)
{
	em_status_t stat = EM_OK;
	eo_elem_t *const eo_elem = eo_elem_get(eo);

	(void)eo_ctx;

	EM_PRINT("%s() on EM-core %d\n", __func__, em_core_id());

	if (unlikely(eo_elem == NULL)) {
		stat = EM_FATAL(EM_ERR_BAD_ID);
		DAEMON_ERROR(stat, "daemon-eo handle:%" PRI_EO " invalid!", eo);
		return stat;
	}

	/* Cannot use API funcs - internal ctrl events might not work */
	stat = queue_disable_all(eo_elem);
	stat |= eo_delete_queue_all(eo_elem);
	/* Finally delete the daemon-eo, API func is ok here */
	stat |= em_eo_delete(eo);

	return stat;
}

static void
daemon_eo_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *q_ctx)
{
	internal_event_receive(eo_ctx, event, type, queue, q_ctx);
}
