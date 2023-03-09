/* Copyright (c) 2020 Nokia Solutions and Networks
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-3-Clause
 */

#include "em_include.h"

em_status_t
poll_drain_mask_check(const em_core_mask_t *logic_mask,
		      const em_core_mask_t *poll_drain_mask)
{
	/* check if mask is zero (all cores, OK) */
	if (em_core_mask_iszero(poll_drain_mask))
		return EM_OK;

	/* check mask validity */
	for (int i = 0; i < EM_MAX_CORES; i++) {
		if (em_core_mask_isset(i, poll_drain_mask) &&
		    !em_core_mask_isset(i, logic_mask))
			return EM_ERR_OPERATION_FAILED;
	}
	return EM_OK;
}

em_status_t
input_poll_init(const em_core_mask_t *logic_mask, const em_conf_t *conf)
{
	return poll_drain_mask_check(logic_mask,
				     &conf->input.input_poll_mask);
}

em_status_t
output_drain_init(const em_core_mask_t *logic_mask, const em_conf_t *conf)
{
	return poll_drain_mask_check(logic_mask,
				     &conf->output.output_drain_mask);
}

em_status_t
poll_drain_mask_set_local(bool *const result /*out*/, int core_id,
			  const em_core_mask_t *mask)
{
	if (em_core_mask_iszero(mask) || em_core_mask_isset(core_id, mask))
		*result = true;
	else
		*result = false;
	return EM_OK;
}

em_status_t
input_poll_init_local(bool *const result /*out*/, int core_id,
		      const em_conf_t *conf)
{
	if (conf->input.input_poll_fn == NULL) {
		*result = false;
		return EM_OK;
	}
	return poll_drain_mask_set_local(result, core_id,
					 &conf->input.input_poll_mask);
}

em_status_t
output_drain_init_local(bool *const result /*out*/, int core_id,
			const em_conf_t *conf)
{
	if (conf->output.output_drain_fn == NULL) {
		*result = false;
		return EM_OK;
	}
	return poll_drain_mask_set_local(result, core_id,
					 &conf->output.output_drain_mask);
}

void core_log_fn_set(em_log_func_t func)
{
	em_locm_t *const locm = &em_locm;

	locm->log_fn = func;
}

em_status_t init_ext_thread(void)
{
	em_locm_t *const locm = &em_locm;
	odp_shm_t shm;
	em_shm_t *shm_addr;
	em_status_t stat = EM_OK;

	/* Make sure that em_shm is available in this external thread */
	shm = odp_shm_lookup("em_shm");
	RETURN_ERROR_IF(shm == ODP_SHM_INVALID,
			EM_ERR_NOT_FOUND, EM_ESCOPE_INIT_CORE,
			"Shared memory lookup failed!");

	shm_addr = odp_shm_addr(shm);
	RETURN_ERROR_IF(shm_addr == NULL, EM_ERR_BAD_POINTER, EM_ESCOPE_INIT_CORE,
			"Shared memory ptr NULL");

	if (shm_addr->conf.process_per_core && em_shm == NULL)
		em_shm = shm_addr;

	RETURN_ERROR_IF(shm_addr != em_shm, EM_ERR_BAD_POINTER, EM_ESCOPE_INIT_CORE,
			"Shared memory init fails: em_shm:%p != shm_addr:%p",
			em_shm, shm_addr);

	stat = emcli_init_local();
	RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_INIT_CORE,
			"Ext emcli_init_local() fails: %" PRI_STAT "", stat);

	/*
	 * Mark that this is an external thread, i.e. not an EM-core and thus
	 * will not participate in EM event dispatching.
	 */
	locm->is_external_thr = true;

	return EM_OK;
}

em_status_t sync_api_init_local(void)
{
	em_locm_t *const locm = &em_locm;
	int core = locm->core_id;
	em_queue_t unsched_queue;
	queue_elem_t *q_elem;

	unsched_queue = queue_id2hdl(FIRST_INTERNAL_UNSCHED_QUEUE + core);
	if (unlikely(unsched_queue == EM_QUEUE_UNDEF))
		return EM_ERR_NOT_FOUND;
	q_elem = queue_elem_get(unsched_queue);
	if (unlikely(!q_elem))
		return EM_ERR_BAD_POINTER;
	locm->sync_api.ctrl_poll.core_unsched_queue = unsched_queue;
	locm->sync_api.ctrl_poll.core_unsched_qelem = q_elem;
	locm->sync_api.ctrl_poll.core_odp_plain_queue = q_elem->odp_queue;

	unsched_queue = queue_id2hdl(SHARED_INTERNAL_UNSCHED_QUEUE);
	if (unlikely(unsched_queue == EM_QUEUE_UNDEF))
		return EM_ERR_NOT_FOUND;
	q_elem = queue_elem_get(unsched_queue);
	if (unlikely(!q_elem))
		return EM_ERR_BAD_POINTER;
	locm->sync_api.ctrl_poll.shared_unsched_queue = unsched_queue;
	locm->sync_api.ctrl_poll.shared_unsched_qelem = q_elem;
	locm->sync_api.ctrl_poll.shared_odp_plain_queue = q_elem->odp_queue;

	locm->sync_api.in_progress = false;

	return EM_OK;
}
