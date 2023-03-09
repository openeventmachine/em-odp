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

static inline int is_queue_valid_type(em_timer_t tmr, const queue_elem_t *q_elem)
{
	unsigned int tmridx = (unsigned int)TMR_H2I(tmr);

	/* implementation specific */
	if (em_shm->timers.timer[tmridx].plain_q_ok && q_elem->type == EM_QUEUE_TYPE_UNSCHEDULED)
		return 1;
	/* EM assumes scheduled always supported */
	return (q_elem->type == EM_QUEUE_TYPE_ATOMIC ||
		q_elem->type == EM_QUEUE_TYPE_PARALLEL ||
		q_elem->type == EM_QUEUE_TYPE_PARALLEL_ORDERED) ? 1 : 0;

	/* LOCAL or OUTPUT queues not supported */
}

static inline int is_timer_valid(em_timer_t tmr)
{
	unsigned int i;
	const timer_storage_t *const tmrs = &em_shm->timers;

	if (unlikely(tmr == EM_TIMER_UNDEF))
		return 0;

	i = (unsigned int)TMR_H2I(tmr);
	if (unlikely(i >= EM_ODP_MAX_TIMERS))
		return 0;

	if (unlikely(tmrs->timer[i].odp_tmr_pool == ODP_TIMER_POOL_INVALID ||
		     tmrs->timer[i].tmo_pool == ODP_POOL_INVALID))
		return 0;
	return 1;
}

static inline em_status_t handle_ack_noskip(em_event_t next_tmo_ev,
					    event_hdr_t *ev_hdr,
					    em_queue_t queue)
{
	if (esv_enabled())
		evstate_usr2em_revert(next_tmo_ev, ev_hdr, EVSTATE__TMO_ACK__NOSKIP);

	em_status_t err = em_send(next_tmo_ev, queue);

	if (unlikely(err != EM_OK))
		err = INTERNAL_ERROR(err, EM_ESCOPE_TMO_ACK, "Tmo ACK: noskip em_send fail");

	return err; /* EM_OK or send-failure */
}

static inline void handle_ack_skip(em_tmo_t tmo)
{
	uint64_t odpt = odp_timer_current_tick(tmo->odp_timer_pool);
	uint64_t skips;

	if (odpt > tmo->last_tick) /* late, over next period */
		skips = ((odpt - tmo->last_tick) / tmo->period) + 1;
	else
		skips = 1; /* not yet over next period, but late for setting */

	tmo->last_tick += skips * tmo->period;
	TMR_DBG_PRINT("%lu skips * %lu ticks => new tgt %lu\n",
		      skips, tmo->period, tmo->last_tick);
	if (EM_TIMER_TMO_STATS)
		tmo->stats.num_period_skips += skips;
}

static inline bool check_tmo_flags(em_tmo_flag_t flags)
{
	/* Check for valid tmo flags (oneshot OR periodic mainly) */
	if (unlikely(!(flags & (EM_TMO_FLAG_ONESHOT | EM_TMO_FLAG_PERIODIC))))
		return false;

	if (unlikely((flags & EM_TMO_FLAG_ONESHOT) && (flags & EM_TMO_FLAG_PERIODIC)))
		return false;

	if (EM_CHECK_LEVEL > 1) {
		em_tmo_flag_t inv_flags = ~(EM_TMO_FLAG_ONESHOT | EM_TMO_FLAG_PERIODIC |
					    EM_TMO_FLAG_NOSKIP);
		if (unlikely(flags & inv_flags))
			return false;
	}
	return true;
}

static inline bool check_timer_attr(const em_timer_attr_t *tmr_attr)
{
	if (unlikely(tmr_attr == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_TIMER_CREATE,
			       "NULL ptr given");
		return false;
	}
	if (unlikely(tmr_attr->__internal_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TIMER_CREATE,
			       "em_timer_attr_t not initialized");
		return false;
	}
	if (unlikely(tmr_attr->resparam.res_ns && tmr_attr->resparam.res_hz)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_CREATE,
			       "Only res_ns OR res_hz allowed");
		return false;
	}
	return true;
}

void em_timer_attr_init(em_timer_attr_t *tmr_attr)
{
	if (unlikely(EM_CHECK_LEVEL > 0 && tmr_attr == NULL))
		return; /* just ignore NULL here */

	/* strategy: first put default resolution, then validate based on that */
	tmr_attr->resparam.res_ns = EM_ODP_TIMER_RESOL_DEF_NS;
	tmr_attr->resparam.res_hz = 0;
	tmr_attr->resparam.clk_src = EM_TIMER_CLKSRC_DEFAULT;
	tmr_attr->flags = EM_TIMER_FLAG_DEFAULT;

	odp_timer_clk_src_t odp_clksrc;
	odp_timer_capability_t odp_capa;
	odp_timer_res_capability_t odp_res_capa;
	int err;

	err = timer_clksrc_em2odp(tmr_attr->resparam.clk_src, &odp_clksrc);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_ATTR_INIT,
			       "Unsupported EM-timer clock source:%d",
			       tmr_attr->resparam.clk_src);
		return;
	}
	err = odp_timer_capability(odp_clksrc, &odp_capa);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_ATTR_INIT,
			       "Timer capability: ret %d, odp-clksrc:%d",
			       err, odp_clksrc);
		return;
	}

	memset(&odp_res_capa, 0, sizeof(odp_timer_res_capability_t));
	odp_res_capa.res_ns = tmr_attr->resparam.res_ns;
	err = odp_timer_res_capability(odp_clksrc, &odp_res_capa);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_ATTR_INIT,
			       "Timer res capability: ret %d, odp-clksrc:%d, res %lu",
			       err, odp_clksrc, tmr_attr->resparam.res_ns);
		return;
	}

	TMR_DBG_PRINT("res %lu -> ODP says min %lu, max %lu\n",
		      tmr_attr->resparam.res_ns, odp_res_capa.min_tmo,
		      odp_res_capa.max_tmo);

	tmr_attr->num_tmo = EM_ODP_DEFAULT_TMOS;
	if (odp_capa.max_timers && odp_capa.max_timers < EM_ODP_DEFAULT_TMOS)
		tmr_attr->num_tmo = odp_capa.max_timers;

	tmr_attr->resparam.min_tmo = odp_res_capa.min_tmo;
	tmr_attr->resparam.max_tmo = odp_res_capa.max_tmo;
	tmr_attr->name[0] = 0; /* timer_create will add default (no index available here) */
	tmr_attr->__internal_check = EM_CHECK_INIT_CALLED;
}

em_status_t em_timer_capability(em_timer_capability_t *capa, em_timer_clksrc_t clk_src)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(capa == NULL)) {
		EM_LOG(EM_LOG_DBG, "%s(): NULL capa ptr!\n", __func__);
		return EM_ERR_BAD_POINTER;
	}

	odp_timer_clk_src_t odp_clksrc;
	odp_timer_capability_t odp_capa;

	if (unlikely(timer_clksrc_em2odp(clk_src, &odp_clksrc) ||
		     odp_timer_capability(odp_clksrc, &odp_capa))) {
		EM_LOG(EM_LOG_DBG, "%s: Not supported clk_src %d\n", __func__, clk_src);
		return EM_ERR_BAD_ARG;
	}

	capa->max_timers = odp_capa.max_pools < EM_ODP_MAX_TIMERS ?
			   odp_capa.max_pools : EM_ODP_MAX_TIMERS;
	capa->max_num_tmo = odp_capa.max_timers;
	capa->max_res.clk_src = clk_src;
	capa->max_res.res_ns = odp_capa.max_res.res_ns;
	capa->max_res.res_hz = odp_capa.max_res.res_hz;
	capa->max_res.min_tmo = odp_capa.max_res.min_tmo;
	capa->max_res.max_tmo = odp_capa.max_res.max_tmo;
	capa->max_tmo.clk_src = clk_src;
	capa->max_tmo.res_ns = odp_capa.max_tmo.res_ns;
	capa->max_tmo.res_hz = odp_capa.max_tmo.res_hz;
	capa->max_tmo.min_tmo = odp_capa.max_tmo.min_tmo;
	capa->max_tmo.max_tmo = odp_capa.max_tmo.max_tmo;
	return EM_OK;
}

em_status_t em_timer_res_capability(em_timer_res_param_t *res, em_timer_clksrc_t clk_src)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(res == NULL)) {
		EM_LOG(EM_LOG_DBG, "%s: NULL ptr res\n", __func__);
		return EM_ERR_BAD_POINTER;
	}

	odp_timer_clk_src_t odp_clksrc;
	odp_timer_res_capability_t odp_res_capa;
	int err;

	err = timer_clksrc_em2odp(clk_src, &odp_clksrc);
	if (unlikely(err)) {
		EM_LOG(EM_LOG_DBG, "%s: Not supported clk_src %d\n", __func__, clk_src);
		return EM_ERR_BAD_ARG;
	}
	memset(&odp_res_capa, 0, sizeof(odp_timer_res_capability_t));
	odp_res_capa.res_ns = res->res_ns;
	odp_res_capa.res_hz = res->res_hz;
	odp_res_capa.max_tmo = res->max_tmo; /* ODP will check if both were set */
	err = odp_timer_res_capability(odp_clksrc, &odp_res_capa);
	if (unlikely(err)) {
		EM_LOG(EM_LOG_DBG, "%s: ODP res_capability failed (ret %d)!\n", __func__, err);
		return EM_ERR_BAD_ARG;
	}
	res->min_tmo = odp_res_capa.min_tmo;
	res->max_tmo = odp_res_capa.max_tmo;
	res->res_ns = odp_res_capa.res_ns;
	res->res_hz = odp_res_capa.res_hz;
	res->clk_src = clk_src;
	return EM_OK;
}

em_timer_t em_timer_create(const em_timer_attr_t *tmr_attr)
{
	if (EM_CHECK_LEVEL > 0) {
		if (check_timer_attr(tmr_attr) == false)
			return EM_TIMER_UNDEF;
	}

	odp_timer_pool_param_t odp_tpool_param;
	odp_timer_clk_src_t odp_clksrc;

	odp_timer_pool_param_init(&odp_tpool_param);
	odp_tpool_param.res_ns = tmr_attr->resparam.res_ns;
	odp_tpool_param.res_hz = tmr_attr->resparam.res_hz;
	odp_tpool_param.min_tmo = tmr_attr->resparam.min_tmo;
	odp_tpool_param.max_tmo = tmr_attr->resparam.max_tmo;
	odp_tpool_param.num_timers = tmr_attr->num_tmo;
	odp_tpool_param.priv = tmr_attr->flags & EM_TIMER_FLAG_PRIVATE ? 1 : 0;
	if (unlikely(timer_clksrc_em2odp(tmr_attr->resparam.clk_src, &odp_clksrc))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_CREATE,
			       "Unsupported EM-timer clock source:%d",
			       tmr_attr->resparam.clk_src);
		return EM_TIMER_UNDEF;
	}
	odp_tpool_param.clk_src = odp_clksrc;

	/* check queue type support */
	odp_timer_capability_t capa;

	if (unlikely(odp_timer_capability(odp_clksrc, &capa))) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_CREATE,
			       "ODP timer capa failed for clk:%d",
			       tmr_attr->resparam.clk_src);
		return EM_TIMER_UNDEF;
	}
	if (unlikely(!capa.queue_type_sched)) { /* must support scheduled queues */
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_CREATE,
			       "ODP does not support scheduled q for clk:%d",
			       tmr_attr->resparam.clk_src);
		return EM_TIMER_UNDEF;
	}

	/* buffer pool for tmos */
	odp_pool_param_t odp_pool_param;

	odp_pool_param_init(&odp_pool_param);
	odp_pool_param.type = ODP_POOL_BUFFER;
	odp_pool_param.buf.size = sizeof(em_timer_timeout_t);
	odp_pool_param.buf.align = ODP_CACHE_LINE_SIZE;
	if (odp_pool_param.buf.cache_size > EM_ODP_TIMER_CACHE)
		odp_pool_param.buf.cache_size = EM_ODP_TIMER_CACHE;
	TMR_DBG_PRINT("local tmo pool cache %d\n", odp_pool_param.buf.cache_size);

	/* local pool caching may cause out of buffers situation on a core. Adjust,
	 * but not waste too much memory
	 */
	uint32_t num = tmr_attr->num_tmo + ((em_core_count() - 1) * odp_pool_param.buf.cache_size);

	if (tmr_attr->num_tmo < num) {
		TMR_DBG_PRINT("Adjusted pool size %d->%d due to local caching (%d)\n",
			      tmr_attr->num_tmo, num, odp_pool_param.buf.cache_size);
	}
	odp_pool_param.buf.num = num;

	/*
	 * Find a free timer-slot.
	 * This linear search should not be a performance problem with only a few timers
	 * available especially when these are typically created at startup.
	 */
	int i;
	event_timer_t *timer;

	odp_ticketlock_lock(&em_shm->timers.timer_lock);

	for (i = 0; i < EM_ODP_MAX_TIMERS; i++) {
		timer = &em_shm->timers.timer[i];
		if (timer->odp_tmr_pool != ODP_TIMER_POOL_INVALID) /* marks used entry */
			continue;

		char timer_pool_name[ODP_TIMER_POOL_NAME_LEN];
		char tmo_pool_name[ODP_POOL_NAME_LEN];
		const char *name = tmr_attr->name;

		if (tmr_attr->name[0] == '\0') { /* replace NULL with default */
			snprintf(timer_pool_name, ODP_TIMER_POOL_NAME_LEN,
				 "EM-timer-%d", timer->idx); /* idx initialized by timer_init */
			name = timer_pool_name;
		}

		TMR_DBG_PRINT("Creating ODP tmr pool: clk %d, res_ns %lu, res_hz %lu\n",
			      odp_tpool_param.clk_src, odp_tpool_param.res_ns,
			      odp_tpool_param.res_hz);
		timer->odp_tmr_pool = odp_timer_pool_create(name, &odp_tpool_param);
		if (unlikely(timer->odp_tmr_pool == ODP_TIMER_POOL_INVALID))
			goto error_locked;
		TMR_DBG_PRINT("Created timer: %s with idx: %d\n", name, timer->idx);

		snprintf(tmo_pool_name, ODP_POOL_NAME_LEN, "Tmo-pool-%d", timer->idx);
		timer->tmo_pool = odp_pool_create(tmo_pool_name, &odp_pool_param);
		if (unlikely(timer->tmo_pool == ODP_POOL_INVALID))
			goto error_locked;
		TMR_DBG_PRINT("Created ODP-pool: %s for %d timeouts\n",
			      tmo_pool_name, odp_pool_param.buf.num);

		timer->flags = tmr_attr->flags;
		timer->plain_q_ok = capa.queue_type_plain;
		odp_timer_pool_start();
		break;
	}

	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	if (unlikely(i >= EM_ODP_MAX_TIMERS)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TIMER_CREATE,
			       "No more timers available");
		return EM_TIMER_UNDEF;
	}
	TMR_DBG_PRINT("ret %" PRI_TMR "\n", TMR_I2H(i));
	return TMR_I2H(i);

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

	TMR_DBG_PRINT("ERR odp tmr pool in: clk %u, res %lu, min %lu, max %lu, num %u\n",
		      odp_tpool_param.clk_src, odp_tpool_param.res_ns,
		      odp_tpool_param.min_tmo, odp_tpool_param.max_tmo, odp_tpool_param.num_timers);
	INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_CREATE, "Timer pool create failed");
	return EM_TIMER_UNDEF;
}

em_status_t em_timer_delete(em_timer_t tmr)
{
	timer_storage_t *const tmrs = &em_shm->timers;
	int i = TMR_H2I(tmr);

	odp_ticketlock_lock(&tmrs->timer_lock);
	/* take lock before checking so nothing can change */
	if (unlikely(!is_timer_valid(tmr))) {
		odp_ticketlock_unlock(&tmrs->timer_lock);
		return INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_DELETE,
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
	const timer_storage_t *const tmrs = &em_shm->timers;
	int i = TMR_H2I(tmr);

	if (EM_CHECK_LEVEL > 0 && !is_timer_valid(tmr))
		return 0;

	return odp_timer_current_tick(tmrs->timer[i].odp_tmr_pool);
}

em_tmo_t em_tmo_create(em_timer_t tmr, em_tmo_flag_t flags, em_queue_t queue)
{
	const queue_elem_t *const q_elem = queue_elem_get(queue);

	if (EM_CHECK_LEVEL > 0) {
		if (unlikely(!is_timer_valid(tmr))) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CREATE,
				       "Invalid timer:%" PRI_TMR "", tmr);
			return EM_TMO_UNDEF;
		}
		if (unlikely(q_elem == NULL || !queue_allocated(q_elem))) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CREATE,
				       "Tmr:%" PRI_TMR ": inv.Q:%" PRI_QUEUE "",
				       tmr, queue);
			return EM_TMO_UNDEF;
		}
		if (unlikely(!is_queue_valid_type(tmr, q_elem))) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CREATE,
				       "Tmr:%" PRI_TMR ": inv.Q (type):%" PRI_QUEUE "",
				       tmr, queue);
			return EM_TMO_UNDEF;
		}
		if (unlikely(!check_tmo_flags(flags))) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CREATE,
				       "Tmr:%" PRI_TMR ": inv. tmo-flags:0x%x",
				       tmr, flags);
			return EM_TMO_UNDEF;
		}
	}

	int i = TMR_H2I(tmr);
	odp_buffer_t tmo_buf = odp_buffer_alloc(em_shm->timers.timer[i].tmo_pool);

	if (unlikely(tmo_buf == ODP_BUFFER_INVALID)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": tmo pool exhausted", tmr);
		return EM_TMO_UNDEF;
	}

	em_timer_timeout_t *tmo = odp_buffer_addr(tmo_buf);
	odp_timer_pool_t odptmr = em_shm->timers.timer[i].odp_tmr_pool;

	tmo->odp_timer = odp_timer_alloc(odptmr, q_elem->odp_queue, NULL);
	if (unlikely(tmo->odp_timer == ODP_TIMER_INVALID)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": odp_timer_alloc() failed", tmr);
		odp_buffer_free(tmo_buf); /* free tmo buf before return */
		return EM_TMO_UNDEF;
	}

	/* OK, init state */
	tmo->period = 0;
	tmo->odp_timer_pool = odptmr;
	tmo->odp_buffer = tmo_buf;
	tmo->flags = flags;
	tmo->queue = queue;
	if (EM_TIMER_TMO_STATS)
		memset(&tmo->stats, 0, sizeof(em_tmo_stats_t));
	odp_atomic_init_u32(&tmo->state, EM_TMO_STATE_IDLE);
	TMR_DBG_PRINT("ODP tmo %ld allocated\n", (unsigned long)tmo->odp_timer);

	return tmo;
}

em_status_t em_tmo_delete(em_tmo_t tmo, em_event_t *cur_event)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF || cur_event == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_DELETE,
				"Invalid args: tmo:%" PRI_TMO " cur_event:%p",
				tmo, cur_event);
	}
	if (EM_CHECK_LEVEL > 1) {
		/* check that tmo buf is valid before accessing other struct members */
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo buffer");

		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo state:%d", tmo_state);
	}
	if (EM_CHECK_LEVEL > 2) {
		RETURN_ERROR_IF(tmo->odp_timer == ODP_TIMER_INVALID,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo odp_timer");
	}

	TMR_DBG_PRINT("ODP tmo %ld\n", (unsigned long)tmo->odp_timer);

	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_UNKNOWN);

	odp_event_t odp_evt = odp_timer_free(tmo->odp_timer);
	odp_buffer_t tmp = tmo->odp_buffer;
	em_event_t tmo_ev = EM_EVENT_UNDEF;

	tmo->odp_timer = ODP_TIMER_INVALID;
	tmo->odp_buffer = ODP_BUFFER_INVALID;
	odp_buffer_free(tmp);

	if (odp_evt != ODP_EVENT_INVALID) {
		RETURN_ERROR_IF(EM_CHECK_LEVEL > 2 && !odp_event_is_valid(odp_evt),
				EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo event");

		tmo_ev = event_odp2em(odp_evt);
		if (esv_enabled())
			tmo_ev = evstate_em2usr(tmo_ev, event_to_hdr(tmo_ev),
						EVSTATE__TMO_DELETE);
	}

	*cur_event = tmo_ev;
	return EM_OK;
}

em_status_t em_tmo_set_abs(em_tmo_t tmo, em_timer_tick_t ticks_abs,
			   em_event_t tmo_ev)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(tmo == EM_TMO_UNDEF || tmo_ev == EM_EVENT_UNDEF),
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_ABS,
			"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
			tmo, tmo_ev);
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_ABS,
			"Invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(tmo->flags & EM_TMO_FLAG_PERIODIC),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_ABS,
			"Cannot set periodic tmo, use _set_periodic()");
	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_ABS,
				"Invalid tmo state:%d", tmo_state);
	}
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 2 &&
			tmo->odp_timer == ODP_TIMER_INVALID,
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_ABS,
			"Invalid tmo odp_timer");

	event_hdr_t *ev_hdr = NULL;
	odp_event_t odp_ev = event_em2odp(tmo_ev);
	bool esv_ena = esv_enabled();
	odp_timer_start_t startp;

	if (esv_ena) {
		ev_hdr = event_to_hdr(tmo_ev);
		evstate_usr2em(tmo_ev, ev_hdr, EVSTATE__TMO_SET_ABS);
	}

	/* set tmo active and arm with absolute time */
	startp.tick_type = ODP_TIMER_TICK_ABS;
	startp.tick = ticks_abs;
	startp.tmo_ev = odp_ev;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int ret = odp_timer_start(tmo->odp_timer, &startp);

	if (unlikely(ret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		if (esv_ena)
			evstate_usr2em_revert(tmo_ev, ev_hdr, EVSTATE__TMO_SET_ABS__FAIL);
		if (ret == ODP_TIMER_TOO_FAR)
			return EM_ERR_TOOFAR;
		else if (ret == ODP_TIMER_TOO_NEAR)
			return EM_ERR_TOONEAR;
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_SET_ABS,
				      "odp_timer_start():%d", ret);
	}
	TMR_DBG_PRINT("OK\n");
	return EM_OK;
}

em_status_t em_tmo_set_rel(em_tmo_t tmo, em_timer_tick_t ticks_rel,
			   em_event_t tmo_ev)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF || tmo_ev == EM_EVENT_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_REL,
				"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
				tmo, tmo_ev);
	}
	if (EM_CHECK_LEVEL > 1) {
		/* check that tmo buf is valid before accessing other struct members */
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_REL,
				"Invalid tmo buffer");

		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_REL,
				"Invalid tmo state:%d", tmo_state);
	}

	event_hdr_t *ev_hdr = NULL;
	odp_event_t odp_ev = event_em2odp(tmo_ev);
	bool esv_ena = esv_enabled();
	odp_timer_start_t startp;

	if (esv_ena) {
		ev_hdr = event_to_hdr(tmo_ev);
		evstate_usr2em(tmo_ev, ev_hdr, EVSTATE__TMO_SET_REL);
	}

	/* set tmo active and arm with relative time */
	tmo->period = ticks_rel;
	if (unlikely(tmo->flags & EM_TMO_FLAG_PERIODIC)) {
		tmo->last_tick = odp_timer_current_tick(tmo->odp_timer_pool) +
				 ticks_rel;
	}
	TMR_DBG_PRINT("last_tick %lu\n", tmo->last_tick);
	startp.tick_type = ODP_TIMER_TICK_REL;
	startp.tick = ticks_rel;
	startp.tmo_ev = odp_ev;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int ret = odp_timer_start(tmo->odp_timer, &startp);

	if (unlikely(ret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		if (esv_ena)
			evstate_usr2em_revert(tmo_ev, ev_hdr, EVSTATE__TMO_SET_REL__FAIL);
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_SET_REL,
				      "odp_timer_start():%d", ret);
	}
	TMR_DBG_PRINT("OK\n");
	return EM_OK;
}

em_status_t em_tmo_set_periodic(em_tmo_t tmo,
				em_timer_tick_t start_abs,
				em_timer_tick_t period,
				em_event_t tmo_ev)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(tmo == EM_TMO_UNDEF || tmo_ev == EM_EVENT_UNDEF),
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC,
			"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
			tmo, tmo_ev);
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_PERIODIC,
			"Invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !(tmo->flags & EM_TMO_FLAG_PERIODIC),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_PERIODIC,
			"Not periodic tmo");
	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_PERIODIC,
				"Invalid tmo state:%d", tmo_state);
	}

	event_hdr_t *ev_hdr = NULL;
	odp_event_t odp_ev = event_em2odp(tmo_ev);
	bool esv_ena = esv_enabled();
	odp_timer_start_t startp;

	if (esv_ena) {
		ev_hdr = event_to_hdr(tmo_ev);
		evstate_usr2em(tmo_ev, ev_hdr, EVSTATE__TMO_SET_PERIODIC);
	}

	TMR_DBG_PRINT("start %lu, period %lu\n", start_abs, period);

	tmo->period = period;
	if (start_abs == 0)
		start_abs = odp_timer_current_tick(tmo->odp_timer_pool) + period;
	tmo->last_tick = start_abs;
	TMR_DBG_PRINT("last_tick %lu, now %lu\n", tmo->last_tick,
		      odp_timer_current_tick(tmo->odp_timer_pool));

	/* set tmo active and arm with absolute time */
	startp.tick_type = ODP_TIMER_TICK_ABS;
	startp.tick = start_abs;
	startp.tmo_ev = odp_ev;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int ret = odp_timer_start(tmo->odp_timer, &startp);

	if (unlikely(ret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		if (esv_ena)
			evstate_usr2em_revert(tmo_ev, ev_hdr, EVSTATE__TMO_SET_PERIODIC__FAIL);
		TMR_DBG_PRINT("diff to tmo %ld\n",
			      (int64_t)tmo->last_tick -
			      (int64_t)odp_timer_current_tick(tmo->odp_timer_pool));
		if (ret == ODP_TIMER_TOO_FAR)
			return EM_ERR_TOOFAR;
		else if (ret == ODP_TIMER_TOO_NEAR)
			return EM_ERR_TOONEAR;
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED,
				      EM_ESCOPE_TMO_SET_PERIODIC,
				      "odp_timer_start():%d", ret);
	}
	return EM_OK;
}

em_status_t em_tmo_cancel(em_tmo_t tmo, em_event_t *cur_event)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF || cur_event == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CANCEL,
				"Invalid args: tmo:%" PRI_TMO " cur_event:%p",
				tmo, cur_event);
	}
	if (EM_CHECK_LEVEL > 1) {
		/* check that tmo buf is valid before accessing other struct members */
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo buffer");

		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo state:%d", tmo_state);
		RETURN_ERROR_IF(tmo->odp_timer == ODP_TIMER_INVALID,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo odp_timer");
	}

	TMR_DBG_PRINT("ODP tmo %ld\n", (unsigned long)tmo->odp_timer);

	/* cancel and set tmo idle */
	odp_event_t odp_ev = ODP_EVENT_INVALID;

	/* this will stop periodic latest at next ack */
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
	int ret = odp_timer_cancel(tmo->odp_timer, &odp_ev);

	if (ret != 0) {
		*cur_event = EM_EVENT_UNDEF;
		if (EM_CHECK_LEVEL > 1) {
			RETURN_ERROR_IF(odp_ev != ODP_EVENT_INVALID,
					EM_ERR_BAD_STATE, EM_ESCOPE_TMO_CANCEL,
					"Bug? ODP timer cancel fail but return event!");
		}
		return EM_ERR_BAD_STATE; /* too late to cancel or already canceled */
	}

	/*
	 * Cancel successful (ret == 0): odp_ev contains the canceled tmo event
	 */

	if (EM_CHECK_LEVEL > 2) {
		RETURN_ERROR_IF(!odp_event_is_valid(odp_ev),
				EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo event");
	}

	em_event_t tmo_ev = event_odp2em(odp_ev);

	if (esv_enabled())
		tmo_ev = evstate_em2usr(tmo_ev, event_to_hdr(tmo_ev), EVSTATE__TMO_CANCEL);

	*cur_event = tmo_ev;
	return EM_OK;
}

em_status_t em_tmo_ack(em_tmo_t tmo, em_event_t next_tmo_ev)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(tmo == EM_TMO_UNDEF || next_tmo_ev == EM_EVENT_UNDEF),
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_ACK,
			"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
			tmo, next_tmo_ev);
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_ACK,
			"Tmo ACK: invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !(tmo->flags & EM_TMO_FLAG_PERIODIC),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_ACK,
			"Tmo ACK: Not a periodic tmo");

	if (EM_TIMER_TMO_STATS)
		tmo->stats.num_acks++;

	em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);
	/*
	 * If tmo cancelled:
	 * Return an error so the application can free the given event.
	 */
	if (tmo_state == EM_TMO_STATE_IDLE) /* canceled, no errorhandler */
		return EM_ERR_CANCELED;

	RETURN_ERROR_IF(tmo_state != EM_TMO_STATE_ACTIVE,
			EM_ERR_BAD_STATE, EM_ESCOPE_TMO_ACK,
			"Tmo ACK: invalid tmo state:%d", tmo_state);

	event_hdr_t *ev_hdr = NULL;
	odp_event_t odp_ev = event_em2odp(next_tmo_ev);
	bool esv_ena = esv_enabled();

	if (esv_ena) {
		ev_hdr = event_to_hdr(next_tmo_ev);
		evstate_usr2em(next_tmo_ev, ev_hdr, EVSTATE__TMO_ACK);
	}

	/*
	 * The periodic timer will silently stop if ack fails! Attempt to
	 * handle exceptions and if the tmo cannot be renewed, call
	 * the errorhandler so the application may recover.
	 */
	tmo->last_tick += tmo->period; /* maintain absolute time */
	int ret;
	int tries = EM_TIMER_ACK_TRIES;
	em_status_t err;
	odp_timer_start_t startp;

	startp.tick_type = ODP_TIMER_TICK_ABS;
	startp.tmo_ev = odp_ev;

	/* try to set tmo EM_TIMER_ACK_TRIES times */
	do {
		/* ask new timeout for next period */
		startp.tick = tmo->last_tick;
		ret = odp_timer_start(tmo->odp_timer, &startp);
		/*
		 * Calling ack() was delayed over next period if 'ret' is
		 * ODP_TIMER_TOO_NEAR, i.e. now in past. Other errors
		 * should not happen, fatal for this tmo
		 */
		if (likely(ret != ODP_TIMER_TOO_NEAR)) {
			if (ret != ODP_TIMER_SUCCESS) {
				TMR_DBG_PRINT("ODP return %d\n"
					      "tmo tgt/tick now %lu/%lu\n",
					      ret, tmo->last_tick,
					      odp_timer_current_tick(tmo->odp_timer_pool));
			}
			break;
		}

		/* ODP_TIMER_TOO_NEAR: ack() delayed beyond next time slot */
		if (EM_TIMER_TMO_STATS)
			tmo->stats.num_late_ack++;
		TMR_DBG_PRINT("late, tgt/now %lu/%lu\n", tmo->last_tick,
			      odp_timer_current_tick(tmo->odp_timer_pool));

		if (tmo->flags & EM_TMO_FLAG_NOSKIP) /* not allowed to skip, next immediately */
			return handle_ack_noskip(next_tmo_ev, ev_hdr, tmo->queue);

		/* skip already passed periods */
		handle_ack_skip(tmo);

		tries--;
		if (unlikely(tries < 1)) {
			err = INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
					     EM_ESCOPE_TMO_ACK,
					     "Tmo ACK: too many retries:%u",
					     EM_TIMER_ACK_TRIES);
			goto ack_err;
		}
	} while (ret != ODP_TIMER_SUCCESS);

	if (unlikely(ret != ODP_TIMER_SUCCESS)) {
		err = INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_ACK,
				     "Tmo ACK: failed to renew tmo (odp ret %d)",
				     ret);
		goto ack_err;
	}
	return EM_OK;

ack_err:
	if (esv_ena)
		evstate_usr2em_revert(next_tmo_ev, ev_hdr, EVSTATE__TMO_ACK__FAIL);
	return err;
}

int em_timer_get_all(em_timer_t *tmr_list, int max)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(tmr_list == NULL || max < 1))
		return 0;

	int num = 0;

	odp_ticketlock_lock(&em_shm->timers.timer_lock);
	for (int i = 0; i < EM_ODP_MAX_TIMERS; i++) {
		if (em_shm->timers.timer[i].odp_tmr_pool != ODP_TIMER_POOL_INVALID) {
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
				EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_GET_ATTR,
				"Inv.args: timer:%" PRI_TMR " tmr_attr:%p",
				tmr, tmr_attr);

	/* get current values from ODP */
	ret = odp_timer_pool_info(em_shm->timers.timer[i].odp_tmr_pool, &poolinfo);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_GET_ATTR,
			"ODP timer pool info failed");

	tmr_attr->resparam.res_ns = poolinfo.param.res_ns;
	tmr_attr->resparam.max_tmo = poolinfo.param.max_tmo;
	tmr_attr->resparam.min_tmo = poolinfo.param.min_tmo;
	tmr_attr->num_tmo = poolinfo.param.num_timers;
	tmr_attr->flags = em_shm->timers.timer[i].flags;
	timer_clksrc_odp2em(poolinfo.param.clk_src, &tmr_attr->resparam.clk_src);
	sz = sizeof(tmr_attr->name);
	strncpy(tmr_attr->name, poolinfo.name, sz - 1);
	tmr_attr->name[sz - 1] = '\0';

	return EM_OK;
}

uint64_t em_timer_get_freq(em_timer_t tmr)
{
	const timer_storage_t *const tmrs = &em_shm->timers;

	if (EM_CHECK_LEVEL > 0 && !is_timer_valid(tmr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_GET_FREQ,
			       "Invalid timer:%" PRI_TMR "", tmr);
		return 0;
	}

	return odp_timer_ns_to_tick(tmrs->timer[TMR_H2I(tmr)].odp_tmr_pool,
				    1000ULL * 1000ULL * 1000ULL); /* 1 sec */
}

uint64_t em_timer_tick_to_ns(em_timer_t tmr, em_timer_tick_t ticks)
{
	const timer_storage_t *const tmrs = &em_shm->timers;

	if (EM_CHECK_LEVEL > 0 && !is_timer_valid(tmr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_TICK_TO_NS,
			       "Invalid timer:%" PRI_TMR "", tmr);
		return 0;
	}

	return odp_timer_tick_to_ns(tmrs->timer[TMR_H2I(tmr)].odp_tmr_pool, ticks);
}

em_timer_tick_t em_timer_ns_to_tick(em_timer_t tmr, uint64_t ns)
{
	const timer_storage_t *const tmrs = &em_shm->timers;

	if (EM_CHECK_LEVEL > 0 && !is_timer_valid(tmr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_NS_TO_TICK,
			       "Invalid timer:%" PRI_TMR "", tmr);
		return 0;
	}

	return odp_timer_ns_to_tick(tmrs->timer[TMR_H2I(tmr)].odp_tmr_pool, ns);
}

em_tmo_state_t em_tmo_get_state(em_tmo_t tmo)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(tmo == EM_TMO_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_GET_STATE, "Invalid tmo");
		return EM_TMO_STATE_UNKNOWN;
	}
	if (EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TMO_GET_STATE, "Invalid tmo buffer");
		return EM_TMO_STATE_UNKNOWN;
	}

	return odp_atomic_load_acq_u32(&tmo->state);
}

em_status_t em_tmo_get_stats(em_tmo_t tmo, em_tmo_stats_t *stat)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && tmo == EM_TMO_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_GET_STATS,
			"Invalid tmo");
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_GET_STATS,
			"Invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && tmo->odp_timer == ODP_TIMER_INVALID,
			EM_ERR_BAD_STATE, EM_ESCOPE_TMO_GET_STATS,
			"tmo deleted?");

	if (EM_TIMER_TMO_STATS) {
		if (stat)
			*stat = tmo->stats;
	} else {
		return EM_ERR_NOT_IMPLEMENTED;
	}

	return EM_OK;
}
