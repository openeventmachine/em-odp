/* Copyright (c) 2020 Nokia Solutions and Networks
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-3-Clause
 */

#include "em_include.h"

em_status_t
poll_drain_mask_check(em_core_mask_t *const logic_mask,
		      em_core_mask_t *const poll_drain_mask)
{
	/* check if mask is zero (all cores, OK) */
	if (em_core_mask_iszero(poll_drain_mask))
		return EM_OK;

	int i;

	/* check mask validity */
	for (i = 0; i < EM_MAX_CORES; i++) {
		if (em_core_mask_isset(i, poll_drain_mask) &&
		    !em_core_mask_isset(i, logic_mask))
			return EM_ERR_OPERATION_FAILED;
	}
	return EM_OK;
}

em_status_t
input_poll_init(em_core_mask_t *const logic_mask, em_conf_t *const conf)
{
	return poll_drain_mask_check(logic_mask,
				     &conf->input.input_poll_mask);
}

em_status_t
output_drain_init(em_core_mask_t *const logic_mask, em_conf_t *const conf)
{
	return poll_drain_mask_check(logic_mask,
				     &conf->output.output_drain_mask);
}

em_status_t
poll_drain_mask_set_local(int *const result, int core_id,
			  em_core_mask_t *const mask)
{
	if (em_core_mask_iszero(mask) || em_core_mask_isset(core_id, mask))
		*result = EM_TRUE;
	else
		*result = EM_FALSE;
	return EM_OK;
}

em_status_t
input_poll_init_local(int *const result, int core_id, em_conf_t *const conf)
{
	if (conf->input.input_poll_fn == NULL) {
		*result = EM_FALSE;
		return EM_OK;
	}
	return poll_drain_mask_set_local(result, core_id,
					 &conf->input.input_poll_mask);
}

em_status_t
output_drain_init_local(int *const result, int core_id, em_conf_t *const conf)
{
	if (conf->output.output_drain_fn == NULL) {
		*result = EM_FALSE;
		return EM_OK;
	}
	return poll_drain_mask_set_local(result, core_id,
					 &conf->output.output_drain_mask);
}
