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
