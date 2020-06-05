/*
 *   Copyright (c) 2016, Nokia Solutions and Networks
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
 *
 *   ---------------------------------------------------------------------
 *   Some notes about the implementation:
 *
 *   EM timer add-on API is close to ODP timer, but there are issues
 *   making this code a bit more complex than it could be:
 *
 *   1) no periodic timer in ODP
 *   2) unless using the pre-defined timeout event there is no way to access
 *      all necessary information runtime to implement a periodic timer
 *
 *   Point 2 is solved by creating a timeout pool. When user allocates
 *   EM timeout, a new minimum size buffer is allocated to store all the needed
 *   information. Timer handle is a pointer to such buffer so all data is
 *   available via the handle (ack() is the most problematic case). This does
 *   create performance penalty, but so far it looks like the penalty is not
 *   too large and does simplify the code otherwise. Also timeouts could be
 *   pre-allocated as the API separates creation and arming.
 *   Most of the syncronization is handled by ODP timer, a ticketlock is used
 *   for high level management API.
 *
 */
#include "em_include.h"
#include <event_machine_timer.h>
#include "em_timer.h"

/* timer handle = index + 1 (UNDEF 0) */
#define TMR_I2H(x) ((em_timer_t)(uintptr_t)((x) + 1))
#define TMR_H2I(x) ((int)((uintptr_t)(x) - 1))

static int is_timer_valid(em_timer_t tmr)
{
	unsigned int i;
	timer_storage_t *const tmrs = &em_shm->timers;

	if (tmr == EM_TIMER_UNDEF)
		return 0;
	i = (unsigned int)TMR_H2I(tmr);
	if (i >= EM_ODP_MAX_TIMERS)
		return 0;

	if (tmrs->timer[i].odp_tmr_pool == ODP_TIMER_POOL_INVALID ||
	    tmrs->timer[i].tmo_pool == ODP_POOL_INVALID)
		return 0;
	return 1;
}

em_timer_t em_timer_create(const em_timer_attr_t *tmr_attr)
{
	em_timer_attr_t attr = {.flags = EM_TIMER_FLAG_DEFAULT,
				.clk_src = EM_TIMER_CLKSRC_DEFAULT};

	/* use provided timer-attr if given */
	if (tmr_attr != NULL)
		attr = *tmr_attr; /* copy */

	odp_timer_capability_t odp_capa;
	odp_timer_clk_src_t odp_clksrc;
	int err;

	memset(&odp_capa, 0, sizeof(odp_timer_capability_t));

	err = timer_clksrc_em2odp(attr.clk_src, &odp_clksrc);
	if (err) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TIMER_CREATE,
			       "Unsupported EM-timer clock source:%d",
			       attr.clk_src);
		return EM_TIMER_UNDEF;
	}

	err = odp_timer_capability(odp_clksrc, &odp_capa);
	if (err) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_CREATE,
			       "Timer capability:%d failed, odp-clksrc:%d",
			       err, odp_clksrc);
		return EM_TIMER_UNDEF;
	}

	/*
	 * Determine attr.num_tmo
	 */

	/* Account for pool buf-stashing on each core, assume max512 per core */
	const uint32_t default_num_tmo = em_core_count() * 512 + 1024 - 1;

	if (odp_capa.max_timers == 0) {
		/* 0 = limited only by the available pool memory size */
		if (attr.num_tmo == 0)
			attr.num_tmo = default_num_tmo;
		/* else use attr.num_tmo as given */
	} else {
		/* odp has a limit for the number of odp-timers */
		if (attr.num_tmo == 0) {
			attr.num_tmo = MIN(odp_capa.max_timers,
					   default_num_tmo);
		} else if (attr.num_tmo > odp_capa.max_timers) {
			INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_TIMER_CREATE,
				       "Timer capability err, odp-clksrc:%d\n"
				       "  req num_tmo > odp-capa max_timers:\n"
				       "    %" PRIu64 " > %" PRIu64 "",
				       odp_clksrc,
				       attr.num_tmo, odp_capa.max_timers);
			return EM_TIMER_UNDEF;
		}
	}
	/*
	 * Account for the thread local buffer cache used by the the odp-pools:
	 * a core should be able to create attr.num_tmo number of tmos even if
	 * some buffers are cached in other cores.
	 * Here assume the local cache size is max 256.
	 */
	attr.num_tmo += (em_core_count() - 1) * 256;
	/* don't increase above odp limit (if any) */
	if (odp_capa.max_timers > 0 && attr.num_tmo > odp_capa.max_timers)
		attr.num_tmo = odp_capa.max_timers;

	/*
	 * Determine attr.resolution and attr.max_tmo
	 */
	uint64_t default_res =
		odp_capa.max_res.res_ns > EM_ODP_TIMER_RESOL_DEF_NS ?
		odp_capa.max_res.res_ns : EM_ODP_TIMER_RESOL_DEF_NS;
	uint64_t min_tmo = 0;
	odp_timer_res_capability_t odp_res_capa;

	memset(&odp_res_capa, 0, sizeof(odp_timer_res_capability_t));

	if (attr.resolution == 0) {
		if (attr.max_tmo == 0)
			odp_res_capa.res_ns = default_res;
		else
			odp_res_capa.max_tmo = attr.max_tmo;
	} else {
		odp_res_capa.res_ns = attr.resolution; /*verify usr resolution*/
	}
	err = odp_timer_res_capability(odp_clksrc, &odp_res_capa);
	/* verify if user provided both attr.resolution and attr.max_tmo */
	if (!err && attr.resolution > 0 && attr.max_tmo > 0 &&
	    odp_res_capa.max_tmo < attr.max_tmo)
		err = 1; /* combination attr.resolution and .max_tmo invalid */

	if (err) {
		INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
			       EM_ESCOPE_TIMER_CREATE,
			       "Timer resolution-capability:%d odp-clksrc:%d\n"
			       "  res-ns:%" PRIu64 "\n"
			       "  min-tmo:%" PRIu64 " max-tmo%" PRIu64 "\n"
			       "EM-attr: .res:%" PRIu64 " .max_tmo:%" PRIu64 "",
			       err, odp_clksrc, odp_res_capa.res_ns,
			       odp_res_capa.min_tmo, odp_res_capa.max_tmo,
			       attr.resolution, attr.max_tmo);
		return EM_TIMER_UNDEF;
	}
	/* Store the params */
	if (attr.resolution == 0)
		attr.resolution = odp_res_capa.res_ns;
	if (attr.max_tmo == 0)
		attr.max_tmo = odp_res_capa.max_tmo;
	min_tmo = odp_res_capa.min_tmo;

	/*
	 * Set odp_timer_pool_param_t
	 */
	odp_timer_pool_param_t odp_tpool_param;

	memset(&odp_tpool_param, 0, sizeof(odp_timer_pool_param_t));

	odp_tpool_param.res_ns = attr.resolution;
	odp_tpool_param.min_tmo = min_tmo;
	odp_tpool_param.max_tmo = attr.max_tmo;
	odp_tpool_param.num_timers = attr.num_tmo;
	odp_tpool_param.priv = attr.flags & EM_TIMER_FLAG_PRIVATE ? 1 : 0;
	odp_tpool_param.clk_src = odp_clksrc;

	/*
	 * Set odp_pool_param_t for the EM-timer tmo_pool
	 */
	odp_pool_param_t odp_pool_param;

	odp_pool_param_init(&odp_pool_param);
	odp_pool_param.type = ODP_POOL_BUFFER;
	odp_pool_param.buf.num = attr.num_tmo;
	odp_pool_param.buf.size = sizeof(em_timer_timeout_t);
	odp_pool_param.buf.align = ODP_CACHE_LINE_SIZE;

	/*
	 * Find a free timer-slot.
	 * This slow search should not be a problem with only a few timers
	 * especially when these are normally created at startup.
	 */
	event_timer_t *timer;
	int i;

	odp_ticketlock_lock(&em_shm->timers.timer_lock);

	for (i = 0; i < EM_ODP_MAX_TIMERS; i++) {
		timer = &em_shm->timers.timer[i];

		if (timer->odp_tmr_pool != ODP_TIMER_POOL_INVALID)
			continue;

		char timer_pool_name[ODP_TIMER_POOL_NAME_LEN];
		char tmo_pool_name[ODP_POOL_NAME_LEN];
		char *name;

		if (attr.name[0] != '\0') {
			name = attr.name;
		} else {
			snprintf(timer_pool_name, ODP_TIMER_POOL_NAME_LEN,
				 "EM-timer-%d", timer->idx);
			name = timer_pool_name;
		}
		timer->odp_tmr_pool = odp_timer_pool_create(name,
							    &odp_tpool_param);
		if (timer->odp_tmr_pool == ODP_TIMER_POOL_INVALID)
			goto error_locked;
		TMR_DBG_PRINT("%s(): Created timer: %s with idx: %d\n",
			      __func__, name, timer->idx);

		snprintf(tmo_pool_name, ODP_POOL_NAME_LEN, "Tmo-pool-%d",
			 timer->idx);
		timer->tmo_pool = odp_pool_create(tmo_pool_name,
						  &odp_pool_param);
		if (timer->tmo_pool == ODP_POOL_INVALID)
			goto error_locked;
		TMR_DBG_PRINT("%s(): Created ODP-pool: %s for %d timeouts\n",
			      __func__, tmo_pool_name, odp_pool_param.buf.num);

		timer->flags = attr.flags;

		odp_timer_pool_start();
		break;
	}

	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	return i < EM_ODP_MAX_TIMERS ? (em_timer_t)TMR_I2H(i) : EM_TIMER_UNDEF;

error_locked:
	/* odp_ticketlock_lock(&timer_shm->tlock) */

	/* 'timer' set in loop */
	if (timer->tmo_pool != ODP_POOL_INVALID)
		odp_pool_destroy(timer->tmo_pool);
	if (timer->odp_tmr_pool != ODP_TIMER_POOL_INVALID)
		odp_timer_pool_destroy(timer->odp_tmr_pool);
	timer->tmo_pool = ODP_POOL_INVALID;
	timer->odp_tmr_pool = ODP_TIMER_POOL_INVALID;

	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	INTERNAL_ERROR(EM_ERR_LIB_FAILED,
		       EM_ESCOPE_TIMER_CREATE,
		       "Timer pool create failed");
	return EM_TIMER_UNDEF;
}

em_status_t em_timer_delete(em_timer_t tmr)
{
	timer_storage_t *const tmrs = &em_shm->timers;

	int i = TMR_H2I(tmr);

	odp_ticketlock_lock(&tmrs->timer_lock);

	if (unlikely(!is_timer_valid(tmr))) {
		odp_ticketlock_unlock(&tmrs->timer_lock);
		return INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TIMER_DELETE,
				      "Invalid timer:%" PRI_TMR "", tmr);
	}

	odp_pool_destroy(tmrs->timer[i].tmo_pool);
	tmrs->timer[i].tmo_pool = ODP_POOL_INVALID;
	odp_timer_pool_destroy(tmrs->timer[i].odp_tmr_pool);
	tmrs->timer[i].odp_tmr_pool = ODP_TIMER_POOL_INVALID;

	odp_ticketlock_unlock(&tmrs->timer_lock);

	return EM_OK;
}

em_timer_tick_t em_timer_current_tick(em_timer_t tmr)
{
	timer_storage_t *const tmrs = &em_shm->timers;

	int i = TMR_H2I(tmr);

	if (EM_CHECK_LEVEL > 0) {
		if (!is_timer_valid(tmr))
			return 0;
	}
	return odp_timer_current_tick(tmrs->timer[i].odp_tmr_pool);
}

em_tmo_t em_tmo_create(em_timer_t tmr, em_tmo_flag_t flags, em_queue_t queue)
{
	int i = TMR_H2I(tmr);
	odp_timer_pool_t odptmr;
	queue_elem_t *const q_elem = queue_elem_get(queue);
	odp_buffer_t tmo_buf;

	if (EM_CHECK_LEVEL > 0) {
		if (!is_timer_valid(tmr)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TMO_CREATE,
				       "Invalid timer:%" PRI_TMR "", tmr);
			return EM_TMO_UNDEF;
		}
		if (q_elem == NULL || !queue_allocated(q_elem)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TMO_CREATE,
				       "Tmr:%" PRI_TMR ": inv Q:%" PRI_QUEUE "",
				       tmr, queue);
			return EM_TMO_UNDEF;
		}
	}

	tmo_buf = odp_buffer_alloc(em_shm->timers.timer[i].tmo_pool);

	if (unlikely(tmo_buf == ODP_BUFFER_INVALID)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": tmo pool exhausted", tmr);
		return EM_TMO_UNDEF;
	}

	em_timer_timeout_t *tmo = odp_buffer_addr(tmo_buf);

	odptmr = em_shm->timers.timer[i].odp_tmr_pool;
	tmo->odp_timer = odp_timer_alloc(odptmr, q_elem->odp_queue, NULL);

	if (unlikely(tmo->odp_timer == ODP_TIMER_INVALID)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": odp_timer_alloc() failed",
			       tmr);
		return EM_TMO_UNDEF;
	}

	/* OK, init state */
	odp_atomic_init_u32(&tmo->state, EM_TMO_STATE_IDLE);
	tmo->period = 0;
	tmo->odp_timer_pool = odptmr;
	tmo->odp_buffer = tmo_buf;
	tmo->flags = flags;
	tmo->queue = queue;
	TMR_DBG_PRINT("%s: ODP tmo %ld\n", __func__,
		      (unsigned long)tmo->odp_timer);
	return tmo;
}

em_status_t em_tmo_delete(em_tmo_t tmo, em_event_t *cur_event)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF, EM_ERR_BAD_ID,
				EM_ESCOPE_TMO_DELETE, "Invalid tmo");
		RETURN_ERROR_IF(cur_event == NULL, EM_ERR_BAD_POINTER,
				EM_ESCOPE_TMO_DELETE, "NULL pointer");
	}

	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo state:%d", tmo_state);
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo buffer");
	}

	odp_event_t odp_evt = odp_timer_free(tmo->odp_timer);

	if (odp_evt != ODP_EVENT_INVALID)
		*cur_event = event_odp2em(odp_evt);
	else
		*cur_event = EM_EVENT_UNDEF;

	odp_buffer_t tmp = tmo->odp_buffer;

	tmo->odp_timer = ODP_TIMER_INVALID;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_UNKNOWN);
	tmo->odp_buffer = ODP_BUFFER_INVALID;
	odp_buffer_free(tmp);

	return EM_OK;
}

em_status_t em_tmo_set_abs(em_tmo_t tmo, em_timer_tick_t ticks_abs,
			   em_event_t tmo_ev)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF || tmo_ev == EM_EVENT_UNDEF,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_ABS,
				"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
				tmo, tmo_ev);
	}

	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo->flags & EM_TMO_FLAG_PERIODIC,
				EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_ABS,
				"Periodic tmo flag set");
	}
	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_ABS,
				"Invalid tmo state:%d", tmo_state);
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_ABS,
				"Invalid tmo buffer");
	}

	odp_event_t odp_ev = event_em2odp(tmo_ev);
	/* set tmo active and arm with absolute time */
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int ret = odp_timer_set_abs(tmo->odp_timer, ticks_abs, &odp_ev);

	TMR_DBG_PRINT("%s(): ODP ret %d\n", __func__, ret);

	if (unlikely(ret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_SET_ABS,
				      "odp_timer_set_abs():%d", ret);
	}

	return EM_OK;
}

em_status_t em_tmo_set_rel(em_tmo_t tmo, em_timer_tick_t ticks_rel,
			   em_event_t tmo_ev)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF || tmo_ev == EM_EVENT_UNDEF,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_REL,
				"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
				tmo, tmo_ev);
	}

	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_REL,
				"Invalid tmo state:%d", tmo_state);
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_REL,
				"Invalid tmo buffer");
	}

	odp_event_t odp_ev = event_em2odp(tmo_ev);
	/* set tmo active and arm with relative time */
	tmo->period = ticks_rel;
	tmo->last_tick = odp_timer_current_tick(tmo->odp_timer_pool);
	TMR_DBG_PRINT("%s: last_tick %lu\n", __func__, tmo->last_tick);
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int ret = odp_timer_set_rel(tmo->odp_timer, ticks_rel, &odp_ev);

	TMR_DBG_PRINT("%s: ODP ret %ld\n", __func__, (unsigned long)ret);

	if (unlikely(ret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_SET_REL,
				      "odp_timer_set_rel():%d", ret);
	}

	return EM_OK;
}

em_status_t em_tmo_cancel(em_tmo_t tmo, em_event_t *cur_event)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF, EM_ERR_BAD_ID,
				EM_ESCOPE_TMO_CANCEL, "Invalid tmo");
		RETURN_ERROR_IF(cur_event == NULL, EM_ERR_BAD_POINTER,
				EM_ESCOPE_TMO_CANCEL, "NULL pointer");
	}

	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo state:%d", tmo_state);
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo buffer");
	}

	/* cancel and set tmo idle */
	odp_event_t odp_ev = ODP_EVENT_INVALID;
	int ret = odp_timer_cancel(tmo->odp_timer, &odp_ev);

	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
	if (ret == 0) {
		*cur_event = event_odp2em(odp_ev);
		return EM_OK;
	}

	*cur_event = EM_EVENT_UNDEF;
	return EM_ERR_BAD_STATE; /* too late to cancel */
}

em_status_t em_tmo_ack(em_tmo_t tmo, em_event_t next_tmo_ev)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF ||
				next_tmo_ev == EM_EVENT_UNDEF,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_ACK,
				"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
				tmo, next_tmo_ev);
		RETURN_ERROR_IF(!(tmo->flags & EM_TMO_FLAG_PERIODIC),
				EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_ACK,
				"Tmo ACK: Not a periodic tmo");
	}

	em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);
	/*
	 * If tmo cancelled:
	 * Return an error so the application can free the given event.
	 */
	RETURN_ERROR_IF(tmo_state != EM_TMO_STATE_ACTIVE,
			EM_ERR_BAD_STATE, EM_ESCOPE_TMO_ACK,
			"Tmo ACK: invalid tmo state:%d", tmo_state);

	if (EM_CHECK_LEVEL > 1) {
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_ACK,
				"Tmo ACK: invalid tmo buffer");
	}

	/*
	 * The periodic timer will silently stop if ack fails! Attempt to
	 * handle exceptions and, if the tmo cannot be renewed, call
	 * the errorhandler so the application may recover.
	 */
	tmo->last_tick += tmo->period; /* maintain absolute time */
	odp_event_t odp_ev = event_em2odp(next_tmo_ev);
	int ret;

	do {
		/* ask new timeout for next period */
		ret = odp_timer_set_abs(tmo->odp_timer,
					tmo->last_tick + tmo->period,
					&odp_ev);
		/*
		 * Calling ack() was delayed over one period if 'ret' is
		 * ODP_TIMER_TOOEARLY, i.e. now in past. Other errors
		 * should not happen.
		 */
		if (likely(ret != ODP_TIMER_TOOEARLY))
			break;

		/* ack() delayed beyond next time slot */
		if (tmo->flags & EM_TMO_FLAG_NOSKIP) {
			/* can't skip, send immediately */
			em_status_t ret = em_send(next_tmo_ev, tmo->queue);

			RETURN_ERROR_IF(ret != EM_OK, EM_ERR_OPERATION_FAILED,
					EM_ESCOPE_TMO_ACK,
					"Tmo ACK: failed to send");
			return EM_OK;
		}

		do { /* catch up time */
			tmo->last_tick += tmo->period;
		} while (tmo->last_tick + tmo->period <
			 odp_timer_current_tick(tmo->odp_timer_pool));
		/* if this takes too much time we can hit TOOEARLY again */

	} while (ret != ODP_TIMER_SUCCESS);

	RETURN_ERROR_IF(ret != ODP_TIMER_SUCCESS, EM_ERR_LIB_FAILED,
			EM_ESCOPE_TMO_ACK, "Tmo ACK: failed to renew");
	return EM_OK;
}

int em_timer_get_all(em_timer_t *tmr_list, int max)
{
	if (EM_CHECK_LEVEL > 0) {
		if (tmr_list == NULL || max < 1)
			return 0;
	}

	int num = 0;

	odp_ticketlock_lock(&em_shm->timers.timer_lock);
	for (int i = 0; i < EM_ODP_MAX_TIMERS; i++) {
		if (em_shm->timers.timer[i].odp_tmr_pool !=
		    ODP_TIMER_POOL_INVALID) {
			tmr_list[num] = TMR_I2H(i);
			num++;
			if (num >= max)
				break;
		}
	}
	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	return num;
}

em_status_t em_timer_get_attr(em_timer_t tmr, em_timer_attr_t *tmr_attr)
{
	odp_timer_pool_info_t poolinfo;
	int i = TMR_H2I(tmr);
	int ret;
	size_t sz;

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(!is_timer_valid(tmr) || tmr_attr == NULL,
				EM_ERR_BAD_ID, EM_ESCOPE_TIMER_GET_ATTR,
				"Inv.args: timer:%" PRI_TMR " tmr_attr:%p",
				tmr, tmr_attr);

	ret = odp_timer_pool_info(em_shm->timers.timer[i].odp_tmr_pool,
				  &poolinfo);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_GET_ATTR,
			"ODP timer pool info failed");

	tmr_attr->resolution = poolinfo.param.res_ns;
	tmr_attr->max_tmo = poolinfo.param.max_tmo;
	tmr_attr->num_tmo = poolinfo.param.num_timers;
	tmr_attr->flags = em_shm->timers.timer[i].flags;
	timer_clksrc_odp2em(poolinfo.param.clk_src, &tmr_attr->clk_src);
	sz = sizeof(tmr_attr->name);
	strncpy(tmr_attr->name, poolinfo.name, sz - 1);
	tmr_attr->name[sz - 1] = '\0';

	return EM_OK;
}

uint64_t em_timer_get_freq(em_timer_t tmr)
{
	timer_storage_t *const tmrs = &em_shm->timers;

	if (EM_CHECK_LEVEL > 0) {
		if (!is_timer_valid(tmr)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TIMER_GET_FREQ,
				       "Invalid timer:%" PRI_TMR "", tmr);
			return 0;
		}
	}

	return odp_timer_ns_to_tick(tmrs->timer[TMR_H2I(tmr)].odp_tmr_pool,
				    1000ULL * 1000ULL * 1000ULL);
}

em_tmo_state_t em_tmo_get_state(em_tmo_t tmo)
{
	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(tmo == EM_TMO_UNDEF)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TMO_GET_STATE,
				       "Invalid tmo");
			return EM_TMO_STATE_UNKNOWN;
		}
	}

	if (EM_CHECK_LEVEL > 1) {
		if (!odp_buffer_is_valid(tmo->odp_buffer)) {
			INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TMO_GET_STATE,
				       "Invalid tmo buffer");
			return EM_TMO_STATE_UNKNOWN;
		}
	}
	return odp_atomic_load_u32(&tmo->state);
}
