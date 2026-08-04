/*
 *   Copyright (c) 2016-2026, Nokia Solutions and Networks
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
 *   EM Timer API is close to ODP timer, but there are issues
 *   making this code a bit more complex than it could be:
 *
 *   1) there is no generic periodic timer in ODP
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
 *   Most of the synchronization is handled by ODP timer, a ticketlock is used
 *   for high level management API.
 *
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <odp_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_chaining.h"
#include "em_error.h"
#include "em_event.h"
#include "em_event_inline.h"
#include "em_event_state.h"
#include "em_event_types.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_queue.h"
#include "em_queue_inline.h"
#include "em_queue_types.h"
#include "em_timer.h"
#include "em_timer_types.h"

/* timer handle = index + 1 (UNDEF 0) */
#define TMR_I2H(x) ((em_timer_t)(uintptr_t)((x) + 1))
#define TMR_H2I(x) ((int)((uintptr_t)(x) - 1))

/*
 * em_fract_u64_t is cast to odp_fract_u64_t * when passing freq arrays to ODP.
 * Verify layout compatibility so the cast stays safe if either type changes.
 */
ODP_STATIC_ASSERT(sizeof(em_fract_u64_t) == sizeof(odp_fract_u64_t),
		  "em_fract_u64_t size must match odp_fract_u64_t");
ODP_STATIC_ASSERT(offsetof(em_fract_u64_t, integer) ==
		  offsetof(odp_fract_u64_t, integer),
		  "em_fract_u64_t.integer offset mismatch");
ODP_STATIC_ASSERT(offsetof(em_fract_u64_t, numer) ==
		  offsetof(odp_fract_u64_t, numer),
		  "em_fract_u64_t.numer offset mismatch");
ODP_STATIC_ASSERT(offsetof(em_fract_u64_t, denom) ==
		  offsetof(odp_fract_u64_t, denom),
		  "em_fract_u64_t.denom offset mismatch");

static inline em_status_t timer_rv_odp2em(int odpret)
{
	switch (odpret) {
	case ODP_TIMER_SUCCESS:
		return EM_OK;
	case ODP_TIMER_TOO_NEAR:
		return EM_ERR_TOONEAR;
	case ODP_TIMER_TOO_FAR:
		return EM_ERR_TOOFAR;
#if !PRE_1_50_ODP_TIMER_API
	case ODP_TIMER_BUSY:
		return EM_ERR_BUSY;
#endif
	default:
			break;
	}

	return EM_ERR_LIB_FAILED;
}

static inline int is_queue_valid_type(em_timer_t tmr, const queue_elem_t *q_elem)
{
	unsigned int tmridx = (unsigned int)TMR_H2I(tmr);

	/* implementation specific */
	if (em_shm->timers.timer[tmridx].plain_q_ok &&
	    q_elem->type == EM_QUEUE_TYPE_UNSCHEDULED)
		return 1;

	/* EM assumes scheduled always supported */
	return (q_elem->type == EM_QUEUE_TYPE_ATOMIC ||
		q_elem->type == EM_QUEUE_TYPE_PARALLEL ||
		q_elem->type == EM_QUEUE_TYPE_ORDERED) ? 1 : 0;

	/* LOCAL or OUTPUT queues not supported */
}

static inline bool is_event_type_valid(em_event_type_t evtype_major)
{
	if (likely(evtype_major == EM_EVENT_TYPE_PACKET ||
		   evtype_major == EM_EVENT_TYPE_SW ||
		   evtype_major == EM_EVENT_TYPE_TIMER))
		return true;

	/* limitations mainly set by odp spec, e.g. no vectors */
	return false;
}

/* Helper for em_tmo_type() */
static inline bool can_have_tmo_type(em_event_type_t evtype_major)
{
	if (likely(evtype_major == EM_EVENT_TYPE_PACKET ||
		   evtype_major == EM_EVENT_TYPE_SW ||
		   evtype_major == EM_EVENT_TYPE_TIMER ||
		   evtype_major == EM_EVENT_TYPE_TIMER_IND))
		return true;

	return false;
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

static inline em_status_t ack_ring_timeout_event(em_tmo_t tmo,
						 em_event_t ev,
						 em_tmo_state_t tmo_state,
						 event_hdr_t *ev_hdr,
						 odp_event_t odp_ev)
{
	(void)ev;
	(void)tmo_state;

	if (EM_CHECK_LEVEL > 0 && unlikely(ev_hdr->event_type != EM_EVENT_TYPE_TIMER_IND))
		return INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_RING_ACK,
				      "Invalid event type:%u, expected timer-ring:%u",
				      ev_hdr->event_type, EM_EVENT_TYPE_TIMER_IND);

	if (EM_CHECK_LEVEL > 0 && unlikely(tmo != ev_hdr->tmo))
		return INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_RING_ACK,
				      "Wrong event returned? tmo %p->%p", tmo, ev_hdr->tmo);

	int ret = odp_timer_periodic_ack(tmo->odp_timer, odp_ev);

	if (unlikely(ret < 0)) /* failure */
		return INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_RING_ACK,
				      "Tmo ACK: ring timer odp ack fail, rv %d", ret);

#if PRE_1_50_ODP_TIMER_API
	if (unlikely(ret == 2)) { /* cancelled, no more events coming */
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE; /* allows em_free */
		ev_hdr->tmo = EM_TMO_UNDEF;
		atomic_thread_fence(memory_order_release);
		TMR_DBG_PRINT("last periodic event, deleted %p\n", odp_ev);
		/* Event must be freed internally */
		em_free(ev);
		return EM_ERR_CANCELED;
	}

	/* ret = 1 would mean timer is cancelled, but more coming still.
	 * return ok to make ring and normal periodic behave the same
	 * e.g. CANCELED means tmo can now be deleted
	 */
#else
	/*
	 * Since ODP API >= 1.50: ret == 1 means cancelled, last event (no more coming).
	 * Unlike old API where ret == 1 meant "cancelled, more coming" and
	 * ret == 2 meant "cancelled, last event".
	 */
	if (unlikely(ret == 1)) { /* cancelled, last event - no more coming */
		/*
		 * Don't clear tmo_type: ODP manages the event internally,
		 * it must not be freed by the application.
		 * EM_ERR_CANCELED tells the user the tmo can now be deleted.
		 */
		TMR_DBG_PRINT("last periodic event %p\n", odp_ev);
		return EM_ERR_CANCELED;
	}
#endif

	return EM_OK;
}

static void cleanup_timer_create_fail(event_timer_t *timer)
{
	if (timer->tmo_pool != ODP_POOL_INVALID &&
	    timer->tmo_pool != em_shm->timers.shared_tmo_pool) /* don't kill shared pool */
		odp_pool_destroy(timer->tmo_pool);
	if (timer->odp_tmr_pool != ODP_TIMER_POOL_INVALID)
		odp_timer_pool_destroy(timer->odp_tmr_pool);
	timer->tmo_pool = ODP_POOL_INVALID;
	timer->odp_tmr_pool = ODP_TIMER_POOL_INVALID;
	TMR_DBG_PRINT("cleaned up failed timer create\n");
}

static odp_pool_t create_tmo_handle_pool(uint32_t num_buf, uint32_t cache,
					 const event_timer_t *tmr)
{
	odp_pool_param_t odp_pool_param;
	odp_pool_t pool;
	char tmo_pool_name[ODP_POOL_NAME_LEN];

	odp_pool_param_init(&odp_pool_param);
	odp_pool_param.type = ODP_POOL_BUFFER;
	odp_pool_param.buf.size = sizeof(em_timer_timeout_t);
	odp_pool_param.buf.align = ODP_CACHE_LINE_SIZE;
	odp_pool_param.buf.cache_size = cache;
	odp_pool_param.stats.all = 0;
	TMR_DBG_PRINT("tmo handle pool cache %d\n", cache);

	uint32_t num = num_buf;

	if (cache > 0) {
		/*
		 * Local pool caching may cause an out of bufs situation on
		 * a core. Adjust the number of bufs by taking this into account.
		 */
		const uint32_t max_cores = em_shm->conf.core_count; /* <= EM_MAX_CORES */

		num += (max_cores - 1) * cache;
	}

	if (num_buf != num) {
		TMR_DBG_PRINT("Adjusted pool size %d->%d due to local caching (%d)\n",
			      num_buf, num, odp_pool_param.buf.cache_size);
	}
	odp_pool_param.buf.num = num;
	snprintf(tmo_pool_name, ODP_POOL_NAME_LEN, "Tmo-pool-%d", tmr->idx);
	pool = odp_pool_create(tmo_pool_name, &odp_pool_param);
	if (pool != ODP_POOL_INVALID) {
		TMR_DBG_PRINT("Created ODP-pool: %s for %d timeouts\n",
			      tmo_pool_name, odp_pool_param.buf.num);
	}
	return pool;
}

#if !PRE_1_50_ODP_TIMER_API
/**
 * Callback for ODP to initialize the user area (event_hdr_t) of timeout events
 * allocated internally by odp_timer_periodic_alloc().
 */
static void ring_tmo_uarea_init(void *uarea, uint32_t size, void *args,
				uint32_t index, uint32_t max_num)
{
	(void)size;
	(void)index;
	(void)max_num;

	em_tmo_t tmo = (em_tmo_t)args;
	event_hdr_t *ev_hdr = (event_hdr_t *)uarea;

	ev_hdr->flags.all = 0;
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_PERIODIC;
	ev_hdr->tmo = tmo;
	ev_hdr->event_type = EM_EVENT_TYPE_TIMER_IND;
	ev_hdr->event_size = 0;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	ev_hdr->user_area.all = 0;
	ev_hdr->user_area.isinit = 1;
}
#endif

#if PRE_1_50_ODP_TIMER_API
static inline odp_event_t alloc_odp_timeout(em_tmo_t tmo)
{
	odp_timeout_t odp_tmo = odp_timeout_alloc(tmo->ring_tmo_pool);

	if (unlikely(odp_tmo == ODP_TIMEOUT_INVALID))
		return ODP_EVENT_INVALID;

	/* init EM event header */
	event_hdr_t *const ev_hdr = odp_timeout_user_area(odp_tmo);
	odp_event_t odp_event = odp_timeout_to_event(odp_tmo);
	em_event_t event = event_odp2em(odp_event);

	if (unlikely(!ev_hdr)) {
		odp_timeout_free(odp_tmo);
		return ODP_EVENT_INVALID;
	}

	if (esv_enabled())
		event = evstate_alloc_tmo(event, ev_hdr);
	ev_hdr->flags.all = 0;
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_PERIODIC;
	ev_hdr->tmo = tmo;
	ev_hdr->event_type = EM_EVENT_TYPE_TIMER_IND;
	ev_hdr->event_size = 0;
	ev_hdr->egrp = EM_EVENT_GROUP_UNDEF;
	ev_hdr->user_area.all = 0;
	ev_hdr->user_area.isinit = 1;

	return odp_event;
}

static inline void free_odp_timeout(odp_event_t odp_event)
{
	if (esv_enabled()) {
		em_event_t event = event_odp2em(odp_event);
		event_hdr_t *const ev_hdr = event_to_hdr(event);

		event = ev_hdr->event;
		evstate_free(event, ev_hdr, EVSTATE__TMO_DELETE);
	}

	odp_event_free(odp_event);
}
#endif

static inline em_status_t handle_ack_noskip(em_event_t next_tmo_ev,
					    event_hdr_t *ev_hdr,
					    em_queue_t queue)
{
	if (esv_enabled())
		evstate_usr2em_revert(next_tmo_ev, ev_hdr, EVSTATE__TMO_ACK__NOSKIP);

	em_status_t err = em_send(next_tmo_ev, queue);

	if (unlikely(err != EM_OK)) {
		err = INTERNAL_ERROR(err, EM_ESCOPE_TMO_ACK, "Tmo ACK: noskip em_send fail");
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
		ev_hdr->tmo = EM_TMO_UNDEF;
	}

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
			       "Not initialized: em_timer_attr_init(tmr_attr) not called");
		return false;
	}
	if (unlikely(tmr_attr->resparam.res_ns && tmr_attr->resparam.res_hz)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_CREATE,
			       "Only res_ns OR res_hz allowed");
		return false;
	}
	return true;
}

static inline bool check_timer_attr_ring(const em_timer_attr_t *ring_attr)
{
	if (unlikely(ring_attr == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_TIMER_RING_CREATE,
			       "NULL attr given");
		return false;
	}
	if (EM_CHECK_LEVEL > 0 && unlikely(ring_attr->__internal_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TIMER_RING_CREATE,
			       "Not initialized: em_timer_ring_attr_init(ring_attr) not called");
		return false;
	}

	if (EM_CHECK_LEVEL > 1 &&
	    unlikely((ring_attr->ringparam.base_hz.integer < 1 &&
		      (ring_attr->ringparam.base_hz.numer < 1 ||
		       ring_attr->ringparam.base_hz.denom < 1)) ||
		     (ring_attr->ringparam.base_hz.numer > 0 &&
		      ring_attr->ringparam.base_hz.denom == 0) ||
		     ring_attr->ringparam.max_mul < 1 ||
		     (ring_attr->flags & EM_TIMER_FLAG_RING) == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_RING_CREATE,
			       "Invalid attr values for ring timer");
		return false;
	}

	return true;
}

static inline bool check_timer_attr_ring_freq(const em_timer_attr_t *ring_attr)
{
	if (unlikely(ring_attr == NULL)) {
		INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_TIMER_RING_FREQ_CREATE,
			       "NULL attr given");
		return false;
	}
	if (unlikely(ring_attr->__internal_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TIMER_RING_FREQ_CREATE,
			       "Not initialized: em_timer_ring_freq_attr_init() not called");
		return false;
	}
	if (unlikely((ring_attr->flags & EM_TIMER_FLAG_RING_FREQ) == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_RING_FREQ_CREATE,
			       "EM_TIMER_FLAG_RING_FREQ not set");
		return false;
	}
	if (unlikely(ring_attr->freqparam.freq_hz == NULL ||
		     ring_attr->freqparam.num == 0)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_RING_FREQ_CREATE,
			       "Invalid freq params: freq_hz=%p, num=%u",
			       ring_attr->freqparam.freq_hz, ring_attr->freqparam.num);
		return false;
	}
	return true;
}

static inline int find_free_timer_index(void)
{
	/*
	 * Find a free timer-slot.
	 * This linear search should not be a performance problem with only a few timers
	 * available especially when these are typically created at startup.
	 * Assumes context is locked
	 */
	int i;

	for (i = 0; i < EM_ODP_MAX_TIMERS; i++) {
		const event_timer_t *timer = &em_shm->timers.timer[i];

		if (timer->odp_tmr_pool == ODP_TIMER_POOL_INVALID) /* marks unused entry */
			break;
	}
	return i;
}

void em_timer_attr_init(em_timer_attr_t *tmr_attr)
{
	if (unlikely(EM_CHECK_LEVEL > 0 && tmr_attr == NULL))
		return; /* just ignore NULL here */

	/* clear/invalidate unused ring timer */
	memset(&tmr_attr->ringparam, 0, sizeof(em_timer_ring_param_t));
	/* clear/invalidate unused freq ring timer */
	memset(&tmr_attr->freqparam, 0, sizeof(em_timer_ring_freq_param_t));

	/* strategy: first put default resolution, then validate based on that */
	tmr_attr->resparam.res_ns = EM_ODP_TIMER_RESOL_DEF_NS;
	tmr_attr->resparam.res_hz = 0;
	tmr_attr->resparam.clk_src = EM_TIMER_CLKSRC_DEFAULT;
	tmr_attr->flags = EM_TIMER_FLAG_NONE;

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

	TMR_DBG_PRINT("odp says highest res %lu\n", odp_capa.highest_res_ns);
	if (unlikely(odp_capa.highest_res_ns > tmr_attr->resparam.res_ns)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_ATTR_INIT,
			       "Timer capability: maxres %lu req %lu, odp-clksrc:%d!",
			       odp_capa.highest_res_ns, tmr_attr->resparam.res_ns, odp_clksrc);
		return;
	}

	memset(&odp_res_capa, 0, sizeof(odp_timer_res_capability_t));
	odp_res_capa.res_ns = tmr_attr->resparam.res_ns;
	err = odp_timer_res_capability(odp_clksrc, &odp_res_capa);
	if (unlikely(err)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_ATTR_INIT,
			       "Timer res capability failed: ret %d, odp-clksrc:%d, res %lu",
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
	tmr_attr->priority = 0; /* default priority */
	tmr_attr->max_pending_events = 0; /* 0 means default, no flow control */
	tmr_attr->__internal_check = EM_CHECK_INIT_CALLED;
}

em_status_t em_timer_ring_attr_init(em_timer_attr_t *ring_attr,
				    em_timer_clksrc_t clk_src,
				    uint64_t base_hz,
				    uint64_t max_mul,
				    uint64_t res_ns)
{
	/* NULL deref would crash the memset() below regardless of EM_CHECK_LEVEL */
	if (unlikely(ring_attr == NULL))
		return EM_ERR_BAD_ARG;

	/* clear unused fields */
	memset(ring_attr, 0, sizeof(em_timer_attr_t));

	ring_attr->ringparam.base_hz.integer = base_hz;
	ring_attr->ringparam.clk_src = clk_src;
	ring_attr->ringparam.max_mul = max_mul;
	ring_attr->ringparam.res_ns = res_ns; /* 0 is legal and means odp default */
	ring_attr->num_tmo = EM_ODP_DEFAULT_RING_TMOS;
	ring_attr->flags = EM_TIMER_FLAG_RING;
	ring_attr->name[0] = 0; /* default at ring_create, index not known here */
	ring_attr->priority = 0; /* default priority */
	ring_attr->max_pending_events = 0; /* no flow control */

	odp_timer_clk_src_t odp_clksrc;
	odp_timer_capability_t capa;
	int rv = timer_clksrc_em2odp(ring_attr->ringparam.clk_src, &odp_clksrc);

	if (unlikely(rv))
		return EM_ERR_BAD_ARG;
	if (unlikely(odp_timer_capability(odp_clksrc, &capa) != 0)) {
		TMR_DBG_PRINT("odp_timer_capability returned error for clk_src %u\n", odp_clksrc);
		return EM_ERR_BAD_ARG; /* assume clksrc not supported */
	}

	if (capa.periodic.max_pools == 0) /* no odp support */
		return EM_ERR_NOT_IMPLEMENTED;

	if (capa.periodic.max_timers < ring_attr->num_tmo)
		ring_attr->num_tmo = capa.periodic.max_timers;

	odp_timer_periodic_capability_t pcapa;

	pcapa.res_ns = ring_attr->ringparam.res_ns;

#if PRE_1_50_ODP_TIMER_API
	pcapa.base_freq_hz.integer = ring_attr->ringparam.base_hz.integer;
	pcapa.base_freq_hz.numer = ring_attr->ringparam.base_hz.numer;
	pcapa.base_freq_hz.denom = ring_attr->ringparam.base_hz.denom;
	pcapa.max_multiplier = ring_attr->ringparam.max_mul;
	rv = odp_timer_periodic_capability(odp_clksrc, &pcapa);
	ring_attr->ringparam.res_ns = pcapa.res_ns; /* update back */
	ring_attr->ringparam.base_hz.integer = pcapa.base_freq_hz.integer;
	ring_attr->ringparam.base_hz.numer = pcapa.base_freq_hz.numer;
	ring_attr->ringparam.base_hz.denom = pcapa.base_freq_hz.denom;
	if (pcapa.max_multiplier < ring_attr->ringparam.max_mul) /* don't increase here */
		ring_attr->ringparam.max_mul = pcapa.max_multiplier;
#else
	pcapa.type = ODP_TIMER_TYPE_PERIODIC_BASE_MUL;
	pcapa.base_mul.base_freq_hz.integer = ring_attr->ringparam.base_hz.integer;
	pcapa.base_mul.base_freq_hz.numer = ring_attr->ringparam.base_hz.numer;
	pcapa.base_mul.base_freq_hz.denom = ring_attr->ringparam.base_hz.denom;
	pcapa.base_mul.max_multiplier = ring_attr->ringparam.max_mul;
	rv = odp_timer_periodic_capability(odp_clksrc, &pcapa);
	ring_attr->ringparam.res_ns = pcapa.res_ns; /* update back */
	ring_attr->ringparam.base_hz.integer = pcapa.base_mul.base_freq_hz.integer;
	ring_attr->ringparam.base_hz.numer = pcapa.base_mul.base_freq_hz.numer;
	ring_attr->ringparam.base_hz.denom = pcapa.base_mul.base_freq_hz.denom;
	if (pcapa.base_mul.max_multiplier < ring_attr->ringparam.max_mul) /* don't increase here */
		ring_attr->ringparam.max_mul = pcapa.base_mul.max_multiplier;
#endif
	if (rv != 1) /* 1 means all values supported */
		return EM_ERR_BAD_ARG;

	ring_attr->__internal_check = EM_CHECK_INIT_CALLED;
	return EM_OK;
}

em_status_t em_timer_ring_freq_attr_init(em_timer_attr_t *ring_attr,
					 em_timer_clksrc_t clk_src,
					 em_fract_u64_t *freq_hz,
					 uint32_t num,
					 uint64_t res_ns)
{
#if PRE_1_50_ODP_TIMER_API
	(void)ring_attr;
	(void)clk_src;
	(void)freq_hz;
	(void)num;
	(void)res_ns;
	return EM_ERR_NOT_IMPLEMENTED;
#else
	/* NULL deref would crash memset()/ODP call below regardless of EM_CHECK_LEVEL */
	if (unlikely(ring_attr == NULL || freq_hz == NULL || num == 0))
		return EM_ERR_BAD_ARG;

	/* clear unused fields */
	memset(ring_attr, 0, sizeof(em_timer_attr_t));

	ring_attr->freqparam.clk_src = clk_src;
	ring_attr->freqparam.freq_hz = freq_hz;
	ring_attr->freqparam.num = num;
	ring_attr->freqparam.res_ns = res_ns; /* 0 is legal and means odp default */
	ring_attr->num_tmo = EM_ODP_DEFAULT_RING_TMOS;
	ring_attr->flags = EM_TIMER_FLAG_RING_FREQ;
	ring_attr->name[0] = 0;
	ring_attr->priority = 0;
	ring_attr->max_pending_events = 0; /* no flow control */

	odp_timer_clk_src_t odp_clksrc;
	odp_timer_capability_t capa;
	int rv = timer_clksrc_em2odp(clk_src, &odp_clksrc);

	if (unlikely(rv))
		return EM_ERR_BAD_ARG;
	if (unlikely(odp_timer_capability(odp_clksrc, &capa) != 0)) {
		TMR_DBG_PRINT("odp_timer_capability returned error for clk_src %u\n", odp_clksrc);
		return EM_ERR_BAD_ARG;
	}

	if (capa.periodic.max_pools == 0 || !capa.periodic.support.freq)
		return EM_ERR_NOT_IMPLEMENTED;

	if (capa.periodic.max_timers < ring_attr->num_tmo)
		ring_attr->num_tmo = capa.periodic.max_timers;

	odp_timer_periodic_capability_t pcapa;

	pcapa.type = ODP_TIMER_TYPE_PERIODIC_FREQ;
	pcapa.res_ns = res_ns;
	pcapa.freq.freq_hz = (odp_fract_u64_t *)freq_hz;
	pcapa.freq.num = num;
	rv = odp_timer_periodic_capability(odp_clksrc, &pcapa);
	ring_attr->freqparam.res_ns = pcapa.res_ns; /* update back */
	/* freq_hz array contents may be updated in-place by ODP */

	if (rv != 1) /* 1 means all values supported */
		return EM_ERR_BAD_ARG;

	ring_attr->__internal_check = EM_CHECK_INIT_CALLED;
	return EM_OK;
#endif /* !PRE_1_50_ODP_TIMER_API */
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
	capa->max_priority = odp_capa.max_priority;

	/* ring timer basic capability */
	capa->ring.max_rings = odp_capa.periodic.max_pools; /* 0 if not supported */
	capa->ring.max_num_tmo = odp_capa.periodic.max_timers;
	capa->ring.max_priority = odp_capa.periodic.max_priority;
	capa->ring.min_base_hz.integer = odp_capa.periodic.min_base_freq_hz.integer;
	capa->ring.min_base_hz.numer = odp_capa.periodic.min_base_freq_hz.numer;
	capa->ring.min_base_hz.denom = odp_capa.periodic.min_base_freq_hz.denom;
	capa->ring.max_base_hz.integer = odp_capa.periodic.max_base_freq_hz.integer;
	capa->ring.max_base_hz.numer = odp_capa.periodic.max_base_freq_hz.numer;
	capa->ring.max_base_hz.denom = odp_capa.periodic.max_base_freq_hz.denom;
#if PRE_1_50_ODP_TIMER_API
	capa->ring.min_pending_events = 0; /* no flow control, no odp capability */
	capa->ring.max_pending_events = 0; /* no flow control, no odp capability */
	capa->ring.min_freq_hz.integer = 0;
	capa->ring.min_freq_hz.numer = 0;
	capa->ring.min_freq_hz.denom = 0;
	capa->ring.max_freq_hz.integer = 0;
	capa->ring.max_freq_hz.numer = 0;
	capa->ring.max_freq_hz.denom = 0;
	capa->ring.support.base_mul = 1; /* base-mul is only supported mode */
	capa->ring.support.freq = 0; /* freq list not supported in old API */
#else
	capa->ring.min_pending_events = odp_capa.periodic.min_pending_tmo;
	capa->ring.max_pending_events = odp_capa.periodic.max_pending_tmo;
	capa->ring.min_freq_hz.integer = odp_capa.periodic.min_freq_hz.integer;
	capa->ring.min_freq_hz.numer = odp_capa.periodic.min_freq_hz.numer;
	capa->ring.min_freq_hz.denom = odp_capa.periodic.min_freq_hz.denom;
	capa->ring.max_freq_hz.integer = odp_capa.periodic.max_freq_hz.integer;
	capa->ring.max_freq_hz.numer = odp_capa.periodic.max_freq_hz.numer;
	capa->ring.max_freq_hz.denom = odp_capa.periodic.max_freq_hz.denom;
	capa->ring.support.base_mul = odp_capa.periodic.support.base_mul;
	capa->ring.support.freq = odp_capa.periodic.support.freq;
#endif
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
	odp_res_capa.res_hz = res->res_hz; /* ODP will check if both were set */
	odp_res_capa.max_tmo = res->max_tmo;
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

em_status_t em_timer_ring_capability(em_timer_ring_param_t *ring)
{
	odp_timer_clk_src_t odp_clksrc;
	odp_timer_periodic_capability_t pcapa;

	if (EM_CHECK_LEVEL > 0 && unlikely(ring == NULL)) {
		EM_LOG(EM_LOG_DBG, "%s: NULL ptr ring\n", __func__);
		return EM_ERR_BAD_POINTER;
	}

	if (unlikely(timer_clksrc_em2odp(ring->clk_src, &odp_clksrc))) {
		EM_LOG(EM_LOG_DBG, "%s: Invalid clk_src %d\n", __func__, ring->clk_src);
		return EM_ERR_BAD_ARG;
	}

	pcapa.res_ns = ring->res_ns;

#if PRE_1_50_ODP_TIMER_API
	pcapa.base_freq_hz.integer = ring->base_hz.integer;
	pcapa.base_freq_hz.numer = ring->base_hz.numer;
	pcapa.base_freq_hz.denom = ring->base_hz.denom;
	pcapa.max_multiplier = ring->max_mul;
	int rv = odp_timer_periodic_capability(odp_clksrc, &pcapa);

	ring->base_hz.integer = pcapa.base_freq_hz.integer;
	ring->base_hz.numer = pcapa.base_freq_hz.numer;
	ring->base_hz.denom = pcapa.base_freq_hz.denom;
	ring->max_mul = pcapa.max_multiplier;
#else
	pcapa.type = ODP_TIMER_TYPE_PERIODIC_BASE_MUL;
	pcapa.base_mul.base_freq_hz.integer = ring->base_hz.integer;
	pcapa.base_mul.base_freq_hz.numer = ring->base_hz.numer;
	pcapa.base_mul.base_freq_hz.denom = ring->base_hz.denom;
	pcapa.base_mul.max_multiplier = ring->max_mul;
	int rv = odp_timer_periodic_capability(odp_clksrc, &pcapa);

	ring->base_hz.integer = pcapa.base_mul.base_freq_hz.integer;
	ring->base_hz.numer = pcapa.base_mul.base_freq_hz.numer;
	ring->base_hz.denom = pcapa.base_mul.base_freq_hz.denom;
	ring->max_mul = pcapa.base_mul.max_multiplier;
#endif
	ring->res_ns = pcapa.res_ns;

	if (unlikely(rv < 0)) {
		EM_LOG(EM_LOG_DBG, "%s: odp failed periodic capability for clk_src %d\n",
		       __func__, ring->clk_src);
		return EM_ERR_LIB_FAILED;
	}
	if (rv == 0)
		return EM_ERR_NOT_SUPPORTED; /* no error, but no exact support */

	return EM_OK; /* meet or exceed */
}

em_status_t em_timer_ring_freq_capability(em_timer_ring_freq_param_t *ring)
{
#if PRE_1_50_ODP_TIMER_API
	(void)ring;
	return EM_ERR_NOT_IMPLEMENTED;
#else
	odp_timer_clk_src_t odp_clksrc;
	odp_timer_periodic_capability_t pcapa;

	if (EM_CHECK_LEVEL > 0 && unlikely(ring == NULL)) {
		EM_LOG(EM_LOG_DBG, "%s: NULL ptr ring\n", __func__);
		return EM_ERR_BAD_POINTER;
	}

	if (EM_CHECK_LEVEL > 0 && unlikely(ring->freq_hz == NULL || ring->num == 0)) {
		EM_LOG(EM_LOG_DBG, "%s: Invalid freq params\n", __func__);
		return EM_ERR_BAD_ARG;
	}

	if (unlikely(timer_clksrc_em2odp(ring->clk_src, &odp_clksrc))) {
		EM_LOG(EM_LOG_DBG, "%s: Invalid clk_src %d\n", __func__, ring->clk_src);
		return EM_ERR_BAD_ARG;
	}

	pcapa.type = ODP_TIMER_TYPE_PERIODIC_FREQ;
	pcapa.res_ns = ring->res_ns;
	pcapa.freq.freq_hz = (odp_fract_u64_t *)ring->freq_hz;
	pcapa.freq.num = ring->num;

	int rv = odp_timer_periodic_capability(odp_clksrc, &pcapa);

	ring->res_ns = pcapa.res_ns;
	/* freq_hz array contents updated in-place by ODP when rv == 0 */

	if (unlikely(rv < 0)) {
		EM_LOG(EM_LOG_DBG, "%s: odp failed periodic capability for clk_src %d\n",
		       __func__, ring->clk_src);
		return EM_ERR_LIB_FAILED;
	}
	if (rv == 0)
		return EM_ERR_NOT_SUPPORTED; /* closest supported values written to freq_hz */

	return EM_OK; /* meet or exceed */
#endif /* !PRE_1_50_ODP_TIMER_API */
}

em_timer_t em_timer_create(const em_timer_attr_t *tmr_attr)
{
	/* timers are initialized? */
	if (unlikely(em_shm->timers.init_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TIMER_CREATE,
			       "Timer is not initialized!");
		return EM_TIMER_UNDEF;
	}

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
	/* check priority support */
	if (unlikely(tmr_attr->priority > capa.max_priority)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_CREATE,
			       "Priority %u exceeds ODP max %u for clk:%d",
			       tmr_attr->priority, capa.max_priority,
			       tmr_attr->resparam.clk_src);
		return EM_TIMER_UNDEF;
	}
	odp_tpool_param.priority = tmr_attr->priority;

	odp_ticketlock_lock(&em_shm->timers.timer_lock);

	int i = find_free_timer_index();

	if (unlikely(i >= EM_ODP_MAX_TIMERS)) {
		odp_ticketlock_unlock(&em_shm->timers.timer_lock);
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TIMER_CREATE,
			       "No more timers available");
		return EM_TIMER_UNDEF;
	}

	event_timer_t *timer = &em_shm->timers.timer[i];
	char timer_pool_name[ODP_TIMER_POOL_NAME_LEN];
	const char *name = tmr_attr->name;
	const char *reason = "";

	if (tmr_attr->name[0] == '\0') { /* replace NULL with default */
		snprintf(timer_pool_name, ODP_TIMER_POOL_NAME_LEN,
			 "EM-timer-%d", timer->idx); /* idx initialized by timer_init */
		name = timer_pool_name;
	}

	TMR_DBG_PRINT("Creating ODP tmr pool: clk %d, res_ns %lu, res_hz %lu, priority %u\n",
		      odp_tpool_param.clk_src, odp_tpool_param.res_ns,
		      odp_tpool_param.res_hz, odp_tpool_param.priority);
	timer->odp_tmr_pool = odp_timer_pool_create(name, &odp_tpool_param);
	if (unlikely(timer->odp_tmr_pool == ODP_TIMER_POOL_INVALID)) {
		reason = "odp_timer_pool_create error";
		goto error_locked;
	}
	TMR_DBG_PRINT("Created timer: %s with idx: %d\n", name, timer->idx);

	/* tmo handle pool can be per-timer or shared */
	if (!em_shm->opt.timer.shared_tmo_pool_enable) { /* per-timer pool */
		odp_pool_t opool = create_tmo_handle_pool(tmr_attr->num_tmo,
							  em_shm->opt.timer.tmo_pool_cache, timer);

		if (unlikely(opool == ODP_POOL_INVALID)) {
			reason = "Tmo handle buffer pool create failed";
			goto error_locked;
		}

		timer->tmo_pool = opool;
		TMR_DBG_PRINT("Created per-timer tmo handle pool\n");
	} else {
		if (em_shm->timers.shared_tmo_pool == ODP_POOL_INVALID) { /* first timer */
			odp_pool_t opool =
				create_tmo_handle_pool(em_shm->opt.timer.shared_tmo_pool_size,
						       em_shm->opt.timer.tmo_pool_cache, timer);

			if (unlikely(opool == ODP_POOL_INVALID)) {
				reason = "Shared tmo handle buffer pool create failed";
				goto error_locked;
			}
			timer->tmo_pool = opool;
			em_shm->timers.shared_tmo_pool = opool;
			TMR_DBG_PRINT("Created shared tmo handle pool for total %u tmos\n",
				      em_shm->opt.timer.shared_tmo_pool_size);
		} else {
			timer->tmo_pool = em_shm->timers.shared_tmo_pool;
		}
	}

	timer->num_tmo_reserve = tmr_attr->num_tmo;
	if (em_shm->opt.timer.shared_tmo_pool_enable) { /* check reservation */
		uint32_t left = em_shm->opt.timer.shared_tmo_pool_size - em_shm->timers.reserved;

		if (timer->num_tmo_reserve > left) {
			TMR_DBG_PRINT("Not enough tmos left in shared pool (%u)\n", left);
			reason = "Not enough tmos left in shared pool";
			goto error_locked;
		}
		em_shm->timers.reserved += timer->num_tmo_reserve;
		TMR_DBG_PRINT("Updated shared tmo reserve by +%u to %u\n",
			      timer->num_tmo_reserve, em_shm->timers.reserved);
	}
	timer->flags = tmr_attr->flags;
	timer->plain_q_ok = capa.queue_type_plain;
	timer->is_ring = false;

	if (odp_timer_pool_start_multi(&timer->odp_tmr_pool, 1) != 1) {
		reason = "odp_timer_pool_start_multi failed";
		goto error_locked;
	}

	em_shm->timers.num_timers++;
	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	TMR_DBG_PRINT("ret %" PRI_TMR ", total timers %u\n", TMR_I2H(i), em_shm->timers.num_timers);
	return TMR_I2H(i);

error_locked:
	cleanup_timer_create_fail(timer);
	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	TMR_DBG_PRINT("ERR odp tmr pool in: clk %u, res %lu, min %lu, max %lu, num %u\n",
		      odp_tpool_param.clk_src, odp_tpool_param.res_ns,
		      odp_tpool_param.min_tmo, odp_tpool_param.max_tmo, odp_tpool_param.num_timers);
	INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_CREATE,
		       "Timer pool create failed: %s", reason);
	return EM_TIMER_UNDEF;
}

typedef enum ring_kind {
	RING_KIND_BASE_MUL,
	RING_KIND_FREQ,
} ring_kind_t;

/*
 * Validate ODP timer capabilities for a ring timer and resolve the ODP clock
 * source. Returns true on success; on failure reports via INTERNAL_ERROR().
 */
static bool ring_check_capa(ring_kind_t kind, const em_timer_attr_t *ring_attr,
			    em_timer_clksrc_t em_clksrc, em_escope_t escope,
			    odp_timer_clk_src_t *odp_clksrc,
			    odp_timer_capability_t *capa)
{
#if PRE_1_50_ODP_TIMER_API
	(void)kind;
#endif
	if (unlikely(timer_clksrc_em2odp(em_clksrc, odp_clksrc))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
			       "Unsupported EM-timer clock source:%d", em_clksrc);
		return false;
	}

	if (unlikely(odp_timer_capability(*odp_clksrc, capa))) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, escope,
			       "ODP timer capa failed for clk:%d", em_clksrc);
		return false;
	}
	if (unlikely(!capa->queue_type_sched)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, escope,
			       "ODP does not support scheduled q for clk:%d", em_clksrc);
		return false;
	}
#if !PRE_1_50_ODP_TIMER_API
	if (kind == RING_KIND_FREQ && unlikely(!capa->periodic.support.freq)) {
		INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, escope,
			       "ODP_TIMER_TYPE_PERIODIC_FREQ not supported");
		return false;
	}
#endif
	if (unlikely(ring_attr->priority > capa->periodic.max_priority)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
			       "Priority %u exceeds ODP max %u for clk:%d",
			       ring_attr->priority, capa->periodic.max_priority, em_clksrc);
		return false;
	}
	return true;
}

/*
 * Fill the type-specific ODP timer pool parameters (base-mul or freq) and the
 * optional flow-control (max pending events) settings for a ring timer.
 * Returns true on success; on failure reports via INTERNAL_ERROR().
 */
static bool ring_fill_tpool_param(ring_kind_t kind, const em_timer_attr_t *ring_attr,
				  const odp_timer_capability_t *capa, em_escope_t escope,
				  odp_timer_pool_param_t *odp_tpool_param)
{
#if PRE_1_50_ODP_TIMER_API
	(void)kind;
	(void)capa;
	/* freq variant is rejected by the wrapper on PRE_1_50, only base_mul reaches here */
	odp_tpool_param->timer_type = ODP_TIMER_TYPE_PERIODIC;
	odp_tpool_param->periodic.base_freq_hz.integer = ring_attr->ringparam.base_hz.integer;
	odp_tpool_param->periodic.base_freq_hz.numer = ring_attr->ringparam.base_hz.numer;
	odp_tpool_param->periodic.base_freq_hz.denom = ring_attr->ringparam.base_hz.denom;
	odp_tpool_param->periodic.max_multiplier = ring_attr->ringparam.max_mul;
	if (ring_attr->max_pending_events > 0) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
			       "Ring flow control not supported with this ODP version");
		return false;
	}
	TMR_DBG_PRINT("Creating ODP periodic tmr pool: clk %d, res_ns %lu, base_hz %lu, priority %u\n",
		      odp_tpool_param->clk_src, odp_tpool_param->res_ns,
		      odp_tpool_param->periodic.base_freq_hz.integer, odp_tpool_param->priority);
#else
	if (kind == RING_KIND_FREQ) {
		odp_tpool_param->timer_type = ODP_TIMER_TYPE_PERIODIC_FREQ;
		odp_tpool_param->periodic.freq.freq_hz =
			(odp_fract_u64_t *)ring_attr->freqparam.freq_hz;
		odp_tpool_param->periodic.freq.num = ring_attr->freqparam.num;
	} else {
		odp_tpool_param->timer_type = ODP_TIMER_TYPE_PERIODIC_BASE_MUL;
		odp_tpool_param->periodic.base_mul.base_freq_hz.integer =
			ring_attr->ringparam.base_hz.integer;
		odp_tpool_param->periodic.base_mul.base_freq_hz.numer =
			ring_attr->ringparam.base_hz.numer;
		odp_tpool_param->periodic.base_mul.base_freq_hz.denom =
			ring_attr->ringparam.base_hz.denom;
		odp_tpool_param->periodic.base_mul.max_multiplier =
			ring_attr->ringparam.max_mul;
	}

	/* flow control (common to both types in new ODP) */
	if (ring_attr->max_pending_events > 0) {
		if (capa->periodic.max_pending_tmo == 0) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
				       "Ring flow control not supported by ODP");
			return false;
		}
		if (ring_attr->max_pending_events > capa->periodic.max_pending_tmo) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
				       "Requested max_pending_events %u exceeds ODP max %u",
				       ring_attr->max_pending_events,
				       capa->periodic.max_pending_tmo);
			return false;
		}
		if (ring_attr->max_pending_events < capa->periodic.min_pending_tmo) {
			INTERNAL_ERROR(EM_ERR_BAD_ARG, escope,
				       "Requested max_pending_events %u less than ODP min %u",
				       ring_attr->max_pending_events,
				       capa->periodic.min_pending_tmo);
			return false;
		}
		odp_tpool_param->periodic.max_pending_tmo = ring_attr->max_pending_events;
	}
	odp_tpool_param->periodic.uarea_size = sizeof(event_hdr_t);
	TMR_DBG_PRINT("Creating ODP periodic %s tmr pool: clk %d, res_ns %lu, priority %u, max_pending_tmo %u\n",
		      kind == RING_KIND_FREQ ? "freq" : "base_mul",
		      odp_tpool_param->clk_src, odp_tpool_param->res_ns,
		      odp_tpool_param->priority, odp_tpool_param->periodic.max_pending_tmo);
#endif
	return true;
}

/*
 * Create or attach the per-timer or shared tmo handle pool for a ring timer.
 * Returns true on success; on failure sets *reason for the caller's error path.
 * Caller must hold timers.timer_lock.
 */
static bool ring_setup_tmo_handle_pool(event_timer_t *timer, uint32_t num_tmo,
				       const char **reason)
{
	if (!em_shm->opt.timer.shared_tmo_pool_enable) {
		odp_pool_t opool = create_tmo_handle_pool(num_tmo,
							  em_shm->opt.timer.tmo_pool_cache,
							  timer);

		if (unlikely(opool == ODP_POOL_INVALID)) {
			*reason = "tmo handle pool creation failed";
			return false;
		}

		timer->tmo_pool = opool;
		TMR_DBG_PRINT("Created per-timer tmo handle pool %p\n", opool);
		return true;
	}

	if (em_shm->timers.shared_tmo_pool == ODP_POOL_INVALID) {
		odp_pool_t opool =
			create_tmo_handle_pool(em_shm->opt.timer.shared_tmo_pool_size,
					       em_shm->opt.timer.tmo_pool_cache, timer);

		if (unlikely(opool == ODP_POOL_INVALID)) {
			*reason = "Shared tmo handle pool creation failed";
			return false;
		}

		timer->tmo_pool = opool;
		em_shm->timers.shared_tmo_pool = opool;
		TMR_DBG_PRINT("Created shared tmo handle pool %p\n", opool);
		return true;
	}

	timer->tmo_pool = em_shm->timers.shared_tmo_pool;
	return true;
}

static em_timer_t ring_create_common(const em_timer_attr_t *ring_attr, ring_kind_t kind)
{
	const em_escope_t escope = (kind == RING_KIND_FREQ) ?
				   EM_ESCOPE_TIMER_RING_FREQ_CREATE :
				   EM_ESCOPE_TIMER_RING_CREATE;
	const em_timer_clksrc_t em_clksrc = (kind == RING_KIND_FREQ) ?
					    ring_attr->freqparam.clk_src :
					    ring_attr->ringparam.clk_src;
	const uint64_t res_ns = (kind == RING_KIND_FREQ) ?
				ring_attr->freqparam.res_ns :
				ring_attr->ringparam.res_ns;

	odp_timer_pool_param_t odp_tpool_param;
	odp_timer_clk_src_t odp_clksrc;
	odp_timer_capability_t capa;

	odp_timer_pool_param_init(&odp_tpool_param);
	odp_tpool_param.exp_mode = ODP_TIMER_EXP_AFTER;
	odp_tpool_param.num_timers = ring_attr->num_tmo;
	odp_tpool_param.priv = ring_attr->flags & EM_TIMER_FLAG_PRIVATE ? 1 : 0;

	if (!ring_check_capa(kind, ring_attr, em_clksrc, escope, &odp_clksrc, &capa))
		return EM_TIMER_UNDEF;

	odp_tpool_param.clk_src = odp_clksrc;
	odp_tpool_param.res_hz = 0;
	odp_tpool_param.res_ns = res_ns;
	odp_tpool_param.priority = ring_attr->priority;

	/* type-specific ODP param fill */
	if (!ring_fill_tpool_param(kind, ring_attr, &capa, escope, &odp_tpool_param))
		return EM_TIMER_UNDEF;

	/* lock context to find free slot and update it */
	timer_storage_t *const tmrs = &em_shm->timers;

	odp_ticketlock_lock(&tmrs->timer_lock);

#if PRE_1_50_ODP_TIMER_API
	/* is there enough events left in shared pool ? */
	{
		uint32_t left = em_shm->opt.timer.ring.timer_event_pool_size - tmrs->ring_reserved;

		if (ring_attr->num_tmo > left) {
			odp_ticketlock_unlock(&tmrs->timer_lock);
			INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, escope,
				       "Too few ring timeout events left (req %u/%u)",
				       ring_attr->num_tmo, left);
			return EM_TIMER_UNDEF;
		}
	}
#endif

	/* allocate timer */
	int i = find_free_timer_index();

	if (unlikely(i >= EM_ODP_MAX_TIMERS)) {
		odp_ticketlock_unlock(&tmrs->timer_lock);
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, escope, "No more timers available");
		return EM_TIMER_UNDEF;
	}

	event_timer_t *timer = &tmrs->timer[i];

	/* then timer pool */
	char timer_pool_name[ODP_TIMER_POOL_NAME_LEN];
	const char *name = ring_attr->name;
	const char *reason = "";

	if (ring_attr->name[0] == '\0') { /* replace empty with default */
		snprintf(timer_pool_name, ODP_TIMER_POOL_NAME_LEN,
			 "EM-timer-%d", timer->idx);
		name = timer_pool_name;
	}

	timer->odp_tmr_pool = odp_timer_pool_create(name, &odp_tpool_param);
	if (unlikely(timer->odp_tmr_pool == ODP_TIMER_POOL_INVALID)) {
		reason = "odp_timer_pool_create failed";
		goto error_locked;
	}
	TMR_DBG_PRINT("Created %s timer: %s with idx: %d\n",
		      kind == RING_KIND_FREQ ? "ring freq" : "ring", name, timer->idx);

	/* tmo handle pool can be per-timer or shared */
	if (!ring_setup_tmo_handle_pool(timer, ring_attr->num_tmo, &reason))
		goto error_locked;

	timer->num_tmo_reserve = ring_attr->num_tmo;
	if (em_shm->opt.timer.shared_tmo_pool_enable) {
		uint32_t left = em_shm->opt.timer.shared_tmo_pool_size - em_shm->timers.reserved;

		if (timer->num_tmo_reserve > left) {
			TMR_DBG_PRINT("Not enough tmos left in shared pool (%u)\n", left);
			reason = "Not enough tmos left in shared pool";
			goto error_locked;
		}
		em_shm->timers.reserved += timer->num_tmo_reserve;
		TMR_DBG_PRINT("Updated shared tmo reserve by +%u to %u\n",
			      timer->num_tmo_reserve, em_shm->timers.reserved);
	}

#if PRE_1_50_ODP_TIMER_API
	/* odp timeout event pool for ring tmo events is shared for all ring timers.
	 * New ODP API creates event pools internally during odp_timer_pool_create().
	 */
	if (tmrs->ring_tmo_pool == ODP_POOL_INVALID) {
		odp_pool_param_t odp_tmo_pool_param;
		char pool_name[ODP_POOL_NAME_LEN];

		odp_pool_param_init(&odp_tmo_pool_param);
		odp_tmo_pool_param.type = ODP_POOL_TIMEOUT;
		odp_tmo_pool_param.tmo.cache_size = em_shm->opt.timer.ring.timer_event_pool_cache;
		odp_tmo_pool_param.tmo.num = em_shm->opt.timer.ring.timer_event_pool_size;
		odp_tmo_pool_param.tmo.uarea_size = sizeof(event_hdr_t);
		odp_tmo_pool_param.stats.all = 0;
		snprintf(pool_name, ODP_POOL_NAME_LEN, "Ring-%d-tmo-pool", timer->idx);
		tmrs->ring_tmo_pool = odp_pool_create(pool_name, &odp_tmo_pool_param);
		if (unlikely(tmrs->ring_tmo_pool == ODP_POOL_INVALID)) {
			reason = "odp timeout event pool creation failed";
			goto error_locked;
		}
		TMR_DBG_PRINT("Created ODP-timeout event pool %p: '%s'\n",
			      tmrs->ring_tmo_pool, pool_name);
	}

	tmrs->ring_reserved += ring_attr->num_tmo;
	TMR_DBG_PRINT("Updated ring reserve by +%u to %u\n", ring_attr->num_tmo,
		      tmrs->ring_reserved);
#endif

	tmrs->num_rings++;
	tmrs->num_timers++;
	timer->num_ring_reserve = ring_attr->num_tmo;
	timer->flags = ring_attr->flags;
	timer->plain_q_ok = capa.queue_type_plain;
	timer->is_ring = true;
	tmrs->num_ring_create_calls++;

	if (odp_timer_pool_start_multi(&timer->odp_tmr_pool, 1) != 1) {
		reason = "odp_timer_pool_start_multi failed";
		goto error_locked;
	}

	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	TMR_DBG_PRINT("ret %" PRI_TMR ", total timers %u\n", TMR_I2H(i), tmrs->num_timers);
	return TMR_I2H(i);

error_locked:
	cleanup_timer_create_fail(timer);
	odp_ticketlock_unlock(&tmrs->timer_lock);
	INTERNAL_ERROR(EM_ERR_LIB_FAILED, escope,
		       "Ring%s timer create failed: %s",
		       kind == RING_KIND_FREQ ? " freq" : "", reason);
	return EM_TIMER_UNDEF;
}

em_timer_t em_timer_ring_create(const em_timer_attr_t *ring_attr)
{
	/* timers are initialized? */
	if (unlikely(em_shm->timers.init_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TIMER_CREATE,
			       "Timer is disabled!");
		return EM_TIMER_UNDEF;
	}

	if (EM_CHECK_LEVEL > 0 && unlikely(check_timer_attr_ring(ring_attr) == false)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_RING_CREATE,
			       "NULL or incorrect attribute");
		return EM_TIMER_UNDEF;
	}

	return ring_create_common(ring_attr, RING_KIND_BASE_MUL);
}

em_timer_t em_timer_ring_freq_create(const em_timer_attr_t *ring_attr)
{
#if PRE_1_50_ODP_TIMER_API
	(void)ring_attr;
	INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED, EM_ESCOPE_TIMER_RING_FREQ_CREATE,
		       "Freq-based ring timer not supported by this ODP version");
	return EM_TIMER_UNDEF;
#else
	/* timers are initialized? */
	if (unlikely(em_shm->timers.init_check != EM_CHECK_INIT_CALLED)) {
		INTERNAL_ERROR(EM_ERR_NOT_INITIALIZED, EM_ESCOPE_TIMER_RING_FREQ_CREATE,
			       "Timer is disabled!");
		return EM_TIMER_UNDEF;
	}

	if (EM_CHECK_LEVEL > 0 && unlikely(check_timer_attr_ring_freq(ring_attr) == false))
		return EM_TIMER_UNDEF;

	return ring_create_common(ring_attr, RING_KIND_FREQ);
#endif /* !PRE_1_50_ODP_TIMER_API */
}

em_status_t em_timer_delete(em_timer_t tmr)
{
	timer_storage_t *const tmrs = &em_shm->timers;
	int i = TMR_H2I(tmr);
	em_status_t rv = EM_OK;
	odp_pool_t pool_fail = ODP_POOL_INVALID;

	/* take lock before checking so nothing can change */
	odp_ticketlock_lock(&tmrs->timer_lock);
	if (unlikely(!is_timer_valid(tmr))) {
		odp_ticketlock_unlock(&tmrs->timer_lock);
		return INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_DELETE,
				      "Invalid timer:%" PRI_TMR "", tmr);
	}

	if (tmrs->timer[i].tmo_pool != tmrs->shared_tmo_pool) { /* don't delete shared pool */
		if (unlikely(odp_pool_destroy(tmrs->timer[i].tmo_pool) != 0)) {
			rv = EM_ERR_LIB_FAILED;
			pool_fail = tmrs->timer[i].tmo_pool;
		} else {
			TMR_DBG_PRINT("Deleted odp pool %p\n", tmrs->timer[i].tmo_pool);
		}
	}
	tmrs->timer[i].tmo_pool = ODP_POOL_INVALID;
	odp_timer_pool_destroy(tmrs->timer[i].odp_tmr_pool);
	tmrs->timer[i].odp_tmr_pool = ODP_TIMER_POOL_INVALID;

	/* Ring delete. ODP timeout pool is destroyed with the timer pool. */
	if (tmrs->timer[i].is_ring && tmrs->num_rings) {
		tmrs->num_rings--;
		if (tmrs->num_rings < 1)
			TMR_DBG_PRINT("Last ring deleted\n");
#if PRE_1_50_ODP_TIMER_API
		tmrs->ring_reserved -= tmrs->timer[i].num_ring_reserve;
		TMR_DBG_PRINT("Updated ring reserve by -%u to %u\n",
			      tmrs->timer[i].num_ring_reserve, tmrs->ring_reserved);
#endif
		tmrs->timer[i].num_ring_reserve = 0;
	}

	tmrs->num_timers--;
	if (tmrs->shared_tmo_pool != ODP_POOL_INVALID) { /* shared pool in use */
		tmrs->reserved -= tmrs->timer[i].num_tmo_reserve;
		TMR_DBG_PRINT("Updated tmo reserve by -%u to %u\n",
			      tmrs->timer[i].num_tmo_reserve, tmrs->reserved);
		tmrs->timer[i].num_tmo_reserve = 0;
	}
	if (tmrs->num_timers == 0 && tmrs->shared_tmo_pool != ODP_POOL_INVALID) {
		/* no more timers, delete shared tmo pool */
		if (unlikely(odp_pool_destroy(tmrs->shared_tmo_pool) != 0)) {
			rv = EM_ERR_LIB_FAILED;
			pool_fail = tmrs->shared_tmo_pool;
		} else {
			TMR_DBG_PRINT("Deleted shared tmo pool %p\n", tmrs->shared_tmo_pool);
			tmrs->shared_tmo_pool = ODP_POOL_INVALID;
		}
	}

	odp_ticketlock_unlock(&tmrs->timer_lock);
	if (unlikely(rv != EM_OK)) {
		return INTERNAL_ERROR(rv, EM_ESCOPE_TIMER_DELETE,
				      "timer %p delete fail, odp pool %p fail\n", tmr, pool_fail);
	}
	TMR_DBG_PRINT("ok, deleted timer %p, num_timers %u\n", tmr, tmrs->num_timers);
	return rv;
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
	return em_tmo_create_arg(tmr, flags, queue, NULL);
}

em_tmo_t em_tmo_create_arg(em_timer_t tmr, em_tmo_flag_t flags,
			   em_queue_t queue, em_tmo_args_t *args)
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

	if (EM_CHECK_LEVEL > 1 &&
	    em_shm->timers.timer[i].is_ring &&
	    !(flags & EM_TMO_FLAG_PERIODIC)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": asking oneshot with ring timer!",
			       tmr);
		return EM_TMO_UNDEF;
	}

	odp_buffer_t tmo_buf = odp_buffer_alloc(em_shm->timers.timer[i].tmo_pool);

	if (unlikely(tmo_buf == ODP_BUFFER_INVALID)) {
		INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": tmo pool exhausted", tmr);
		return EM_TMO_UNDEF;
	}

	em_timer_timeout_t *tmo = odp_buffer_addr(tmo_buf);
	odp_timer_pool_t odptmr = em_shm->timers.timer[i].odp_tmr_pool;

	const void *userptr = NULL;

	if (args != NULL)
		userptr = args->userptr;

#if PRE_1_50_ODP_TIMER_API
	tmo->odp_timer = odp_timer_alloc(odptmr, q_elem->odp_queue, userptr);
	if (unlikely(tmo->odp_timer == ODP_TIMER_INVALID)) {
		INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CREATE,
			       "Tmr:%" PRI_TMR ": odp_timer_alloc() failed", tmr);
		odp_buffer_free(tmo_buf);
		return EM_TMO_UNDEF;
	}

#else
	/*
	 * Ring timers use odp_timer_periodic_alloc() which requires
	 * freq_multiplier. Defer allocation to em_tmo_set_periodic_ring().
	 */
	if (!em_shm->timers.timer[i].is_ring) {
		tmo->odp_timer = odp_timer_alloc(odptmr, q_elem->odp_queue, userptr);
		if (unlikely(tmo->odp_timer == ODP_TIMER_INVALID)) {
			INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CREATE,
				       "Tmr:%" PRI_TMR ": odp_timer_alloc() failed", tmr);
			odp_buffer_free(tmo_buf);
			return EM_TMO_UNDEF;
		}
	} else {
		tmo->odp_timer = ODP_TIMER_INVALID;
	}
#endif

	/* OK, init state. Some values copied for faster access runtime */
	tmo->period = 0;
	tmo->odp_timer_pool = odptmr;
	tmo->timer = tmr;
	tmo->odp_buffer = tmo_buf;
	tmo->flags = flags;
	tmo->queue = queue;
	tmo->is_ring = em_shm->timers.timer[i].is_ring;
	tmo->odp_timeout = ODP_EVENT_INVALID;

#if PRE_1_50_ODP_TIMER_API
	tmo->ring_tmo_pool = em_shm->timers.ring_tmo_pool;

	if (tmo->is_ring) { /* pre-allocate timeout event to save time at start */
		odp_event_t odp_tmo_event = alloc_odp_timeout(tmo);

		if (unlikely(odp_tmo_event == ODP_EVENT_INVALID)) {
			INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TMO_CREATE,
				       "Ring: odp timeout event allocation failed");
			odp_timer_free(tmo->odp_timer);
			odp_buffer_free(tmo_buf);
			return EM_TMO_UNDEF;
		}
		tmo->odp_timeout = odp_tmo_event;
		TMR_DBG_PRINT("Ring: allocated odp timeout ev %p\n", tmo->odp_timeout);
	}
#else
	tmo->ring_tmo_pool = ODP_POOL_INVALID;
	tmo->user_ptr = userptr;
	tmo->multiplier = 0;
	tmo->freq_hz = (em_fract_u64_t){0};
#endif

	if (EM_TIMER_TMO_STATS)
		memset(&tmo->stats, 0, sizeof(em_tmo_stats_t));

	odp_atomic_init_u32(&tmo->state, EM_TMO_STATE_IDLE);
	TMR_DBG_PRINT("ODP timer %p allocated\n", tmo->odp_timer);
	TMR_DBG_PRINT("tmo %p created\n", tmo);
	return tmo;
}

em_status_t em_tmo_delete(em_tmo_t tmo)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_DELETE,
				"Invalid args: tmo:%" PRI_TMO, tmo);
	}

	em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

	if (EM_CHECK_LEVEL > 1) {
		/* check that tmo buf is valid before accessing other struct members */
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo buffer");

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo state:%d", tmo_state);

#if PRE_1_50_ODP_TIMER_API
		RETURN_ERROR_IF(tmo->odp_timer == ODP_TIMER_INVALID,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo odp_timer, deleted?");
#else
		/* Ring timers defer ODP allocation to set_periodic_ring() */
		RETURN_ERROR_IF(!tmo->is_ring && tmo->odp_timer == ODP_TIMER_INVALID,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_DELETE,
				"Invalid tmo odp_timer, deleted?");
#endif
	}

	TMR_DBG_PRINT("ODP timer %p\n", tmo->odp_timer);

	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_UNKNOWN);

#if PRE_1_50_ODP_TIMER_API
	int fret = odp_timer_free(tmo->odp_timer);

	RETURN_ERROR_IF(fret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_DELETE,
			"odp timer free failed!?, rv %d\n", fret);

	/* Old API: manually free pre-allocated ring timeout event.
	 * New API: odp_timer_free() releases the internally managed event.
	 */
	if (tmo->is_ring && tmo->odp_timeout != ODP_EVENT_INVALID) {
		TMR_DBG_PRINT("ring: free unused ODP timeout ev %p\n", tmo->odp_timeout);
		free_odp_timeout(tmo->odp_timeout);
		tmo->odp_timeout = ODP_EVENT_INVALID;
	}
#else
	/* Ring timers may not have an ODP timer if never started */
	if (tmo->odp_timer != ODP_TIMER_INVALID) {
		int fret = odp_timer_free(tmo->odp_timer);

		RETURN_ERROR_IF(fret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_DELETE,
				"odp timer free failed!?, rv %d\n", fret);
	}
#endif
	odp_buffer_t tmp = tmo->odp_buffer;

	tmo->odp_timer = ODP_TIMER_INVALID;
	tmo->odp_buffer = ODP_BUFFER_INVALID;

	odp_buffer_free(tmp);

	TMR_DBG_PRINT("tmo %p delete ok\n", tmo);

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

	event_hdr_t *ev_hdr = event_to_hdr(tmo_ev);
	odp_event_t odp_ev = event_em2odp(tmo_ev);
	bool esv_ena = esv_enabled();
	odp_timer_start_t startp;

	if (EM_CHECK_LEVEL > 0) {
		em_event_type_t evtype_major = em_event_type_major(ev_hdr->event_type);

		RETURN_ERROR_IF(EM_CHECK_LEVEL > 2 &&
				!is_event_type_valid(evtype_major),
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_ABS,
				"Invalid event type:%u", evtype_major);
		RETURN_ERROR_IF(evtype_major == EM_EVENT_TYPE_TIMER_IND,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_ABS,
				"Invalid event type: timer-ring");
	}

	if (esv_ena)
		evstate_usr2em(tmo_ev, ev_hdr, EVSTATE__TMO_SET_ABS);

	/* set tmo active and arm with absolute time */
	startp.tick_type = ODP_TIMER_TICK_ABS;
	startp.tick = ticks_abs;
	startp.tmo_ev = odp_ev;
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_ONESHOT;
	ev_hdr->tmo = tmo;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int odpret = odp_timer_start(tmo->odp_timer, &startp);

	if (unlikely(odpret != ODP_TIMER_SUCCESS)) {
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
		ev_hdr->tmo = EM_TMO_UNDEF;
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		if (esv_ena)
			evstate_usr2em_revert(tmo_ev, ev_hdr, EVSTATE__TMO_SET_ABS__FAIL);

		em_status_t retval = timer_rv_odp2em(odpret);

		if (retval == EM_ERR_TOONEAR) { /* skip errorhandler */
			TMR_DBG_PRINT("TOONEAR, skip ErrH\n");
			return retval;
		}
		if (retval == EM_ERR_BUSY) { /* skip errorhandler */
			TMR_DBG_PRINT("BUSY, skip ErrH\n");
			return retval;
		}
		return INTERNAL_ERROR(retval, EM_ESCOPE_TMO_SET_ABS,
				      "odp_timer_start():%d", odpret);
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

		RETURN_ERROR_IF(tmo->flags & EM_TMO_FLAG_PERIODIC,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_REL,
				"%s: Periodic no longer supported", __func__);
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

	event_hdr_t *ev_hdr = event_to_hdr(tmo_ev);
	odp_event_t odp_ev = event_em2odp(tmo_ev);
	bool esv_ena = esv_enabled();
	odp_timer_start_t startp;

	if (EM_CHECK_LEVEL > 0) {
		em_event_type_t evtype_major = em_event_type_major(ev_hdr->event_type);

		RETURN_ERROR_IF(EM_CHECK_LEVEL > 2 &&
				!is_event_type_valid(evtype_major),
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_REL,
				"Invalid event type:%u", evtype_major);
		RETURN_ERROR_IF(evtype_major == EM_EVENT_TYPE_TIMER_IND,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_REL,
				"Invalid event type: timer-ring");
	}

	if (esv_ena)
		evstate_usr2em(tmo_ev, ev_hdr, EVSTATE__TMO_SET_REL);

	/* set tmo active and arm with relative time */
	startp.tick_type = ODP_TIMER_TICK_REL;
	startp.tick = ticks_rel;
	startp.tmo_ev = odp_ev;
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_ONESHOT;
	ev_hdr->tmo = tmo;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int odpret = odp_timer_start(tmo->odp_timer, &startp);

	if (unlikely(odpret != ODP_TIMER_SUCCESS)) {
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
		ev_hdr->tmo = EM_TMO_UNDEF;
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		if (esv_ena)
			evstate_usr2em_revert(tmo_ev, ev_hdr, EVSTATE__TMO_SET_REL__FAIL);

		em_status_t retval = timer_rv_odp2em(odpret);

		if (retval == EM_ERR_TOONEAR) { /* skip errorhandler */
			TMR_DBG_PRINT("TOONEAR, skip ErrH\n");
			return retval;
		}
		if (retval == EM_ERR_BUSY) { /* skip errorhandler */
			TMR_DBG_PRINT("BUSY, skip ErrH\n");
			return retval;
		}
		return INTERNAL_ERROR(retval, EM_ESCOPE_TMO_SET_REL,
				      "odp_timer_start():%d", odpret);
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

	event_hdr_t *ev_hdr = event_to_hdr(tmo_ev);
	odp_event_t odp_ev = event_em2odp(tmo_ev);
	bool esv_ena = esv_enabled();
	odp_timer_start_t startp;

	if (EM_CHECK_LEVEL > 0) {
		em_event_type_t evtype_major = em_event_type_major(ev_hdr->event_type);

		RETURN_ERROR_IF(EM_CHECK_LEVEL > 2 &&
				!is_event_type_valid(evtype_major),
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC,
				"invalid event type");
		RETURN_ERROR_IF(evtype_major == EM_EVENT_TYPE_TIMER_IND,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC,
				"Invalid event type: timer-ring");
	}

	if (esv_ena)
		evstate_usr2em(tmo_ev, ev_hdr, EVSTATE__TMO_SET_PERIODIC);

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
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_PERIODIC;
	ev_hdr->tmo = tmo;
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int odpret = odp_timer_start(tmo->odp_timer, &startp);

	if (unlikely(odpret != ODP_TIMER_SUCCESS)) {
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
		ev_hdr->tmo = EM_TMO_UNDEF;
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);
		if (esv_ena)
			evstate_usr2em_revert(tmo_ev, ev_hdr, EVSTATE__TMO_SET_PERIODIC__FAIL);

		TMR_DBG_PRINT("diff to tmo %ld\n",
			      (int64_t)tmo->last_tick -
			      (int64_t)odp_timer_current_tick(tmo->odp_timer_pool));

		em_status_t retval = timer_rv_odp2em(odpret);

		if (retval == EM_ERR_TOONEAR) { /* skip errorhandler */
			TMR_DBG_PRINT("TOONEAR, skip ErrH\n");
			return retval;
		}
		if (retval == EM_ERR_BUSY) { /* skip errorhandler */
			TMR_DBG_PRINT("BUSY, skip ErrH\n");
			return retval;
		}
		return INTERNAL_ERROR(retval,
				      EM_ESCOPE_TMO_SET_PERIODIC,
				      "odp_timer_start():%d", odpret);
	}
	TMR_DBG_PRINT("OK\n");
	return EM_OK;
}

em_status_t em_tmo_set_periodic_ring(em_tmo_t tmo,
				     em_timer_tick_t start_abs,
				     uint64_t multiplier)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && tmo == EM_TMO_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC_RING,
			"Inv.args: tmo UNDEF");
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_PERIODIC_RING,
			"Invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !(tmo->flags & EM_TMO_FLAG_PERIODIC),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_PERIODIC_RING,
			"Not periodic tmo");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(em_shm->timers.timer[TMR_H2I(tmo->timer)].flags & EM_TIMER_FLAG_RING_FREQ),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_PERIODIC_RING,
			"Timer created with EM_TIMER_FLAG_RING_FREQ, use em_tmo_set_periodic_ring_freq()");
	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN ||
				tmo_state == EM_TMO_STATE_ACTIVE,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_PERIODIC_RING,
				"Invalid tmo state:%d", tmo_state);
	}

#if PRE_1_50_ODP_TIMER_API
	odp_timer_periodic_start_t startp;
	odp_event_t odp_ev = tmo->odp_timeout; /* pre-allocated */

	tmo->odp_timeout = ODP_EVENT_INVALID; /* now used */

	if (odp_ev == ODP_EVENT_INVALID) { /* re-start, pre-alloc used */
		odp_event_t odp_tmo_event = alloc_odp_timeout(tmo);

		if (unlikely(odp_tmo_event == ODP_EVENT_INVALID))
			return INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_TMO_SET_PERIODIC_RING,
					"Ring: odp timeout event allocation failed");
		odp_ev = odp_tmo_event;
	}

	TMR_DBG_PRINT("ring tmo start_abs %lu, M=%lu, odp ev=%p\n", start_abs, multiplier, odp_ev);
	startp.first_tick = start_abs;
	startp.freq_multiplier = multiplier;
	startp.tmo_ev = odp_ev;
#else
	/*
	 * ODP periodic timers are allocated with odp_timer_periodic_alloc() which
	 * sets the freq_multiplier. If the timer is not yet allocated or the
	 * multiplier changed, (re-)allocate.
	 */
	if (tmo->odp_timer != ODP_TIMER_INVALID && tmo->multiplier != multiplier) {
		/* multiplier changed, free old timer and re-allocate */
		em_tmo_state_t cur_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(cur_state == EM_TMO_STATE_ACTIVE,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_PERIODIC_RING,
				"Cannot change multiplier while tmo is active");
		odp_timer_free(tmo->odp_timer);
		tmo->odp_timer = ODP_TIMER_INVALID;
	}

	if (tmo->odp_timer == ODP_TIMER_INVALID) {
		const queue_elem_t *q_elem = queue_elem_get(tmo->queue);

		if (EM_CHECK_LEVEL > 1) {
			RETURN_ERROR_IF(q_elem == NULL || !queue_allocated(q_elem),
					EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC_RING,
					"Invalid queue:%" PRI_QUEUE "", tmo->queue);
		}

		odp_timer_periodic_param_t pparams;

		odp_timer_periodic_param_init(&pparams);
		pparams.queue = q_elem->odp_queue;
		pparams.user_ptr = tmo->user_ptr;
		pparams.base_mul.multiplier = multiplier;
		pparams.uarea_init.init_fn = ring_tmo_uarea_init;
		pparams.uarea_init.args = tmo;

		tmo->odp_timer = odp_timer_periodic_alloc(tmo->odp_timer_pool, &pparams);
		if (unlikely(tmo->odp_timer == ODP_TIMER_INVALID))
			return INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_SET_PERIODIC_RING,
					      "odp_timer_periodic_alloc() failed");
		tmo->multiplier = multiplier;
		TMR_DBG_PRINT("Allocated periodic ODP timer %p, M=%lu\n",
			      tmo->odp_timer, multiplier);
	}

	TMR_DBG_PRINT("ring tmo start_abs %lu, M=%lu\n", start_abs, multiplier);

	odp_timer_periodic_start_t startp;

	startp.first_tick = start_abs;
#endif
	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int odpret = odp_timer_periodic_start(tmo->odp_timer, &startp);

	if (unlikely(odpret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);

		em_status_t retval = timer_rv_odp2em(odpret);

		if (retval == EM_ERR_TOONEAR) { /* skip errorhandler */
			TMR_DBG_PRINT("TOONEAR, skip ErrH\n");
			return retval;
		}
		if (retval == EM_ERR_BUSY) { /* skip errorhandler */
			TMR_DBG_PRINT("BUSY, skip ErrH\n");
			return retval;
		}
		return INTERNAL_ERROR(retval,
				      EM_ESCOPE_TMO_SET_PERIODIC_RING,
				      "odp_timer_periodic_start(): ret %d", odpret);
	}
	/* ok */
	TMR_DBG_PRINT("OK\n");
	return EM_OK;
}

em_status_t em_tmo_set_periodic_ring_freq(em_tmo_t tmo,
					  em_timer_tick_t start_abs,
					  em_fract_u64_t freq_hz)
{
#if PRE_1_50_ODP_TIMER_API
	(void)tmo;
	(void)start_abs;
	(void)freq_hz;
	return EM_ERR_NOT_IMPLEMENTED;
#else
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && tmo == EM_TMO_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
			"Inv.args: tmo UNDEF");
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
			"Invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !(tmo->flags & EM_TMO_FLAG_PERIODIC),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
			"Not periodic tmo");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			!(em_shm->timers.timer[TMR_H2I(tmo->timer)].flags &
			EM_TIMER_FLAG_RING_FREQ),
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
			"Timer not created with EM_TIMER_FLAG_RING_FREQ");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && (freq_hz.numer != 0 && freq_hz.denom == 0),
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
			"Invalid freq_hz, denom cannot be 0 if numer is not 0");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && freq_hz.integer == 0 && freq_hz.numer == 0,
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
			"freq_hz is 0");
	if (EM_CHECK_LEVEL > 1) {
		em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(tmo_state == EM_TMO_STATE_UNKNOWN ||
				tmo_state == EM_TMO_STATE_ACTIVE,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
				"Invalid tmo state:%d", tmo_state);
	}

	/*
	 * ODP periodic timers are allocated with odp_timer_periodic_alloc() which
	 * sets the freq_hz. If the timer is not yet allocated or the freq changed,
	 * (re-)allocate.
	 */
	if (tmo->odp_timer != ODP_TIMER_INVALID &&
	    (tmo->freq_hz.integer != freq_hz.integer ||
	     tmo->freq_hz.numer != freq_hz.numer ||
	     tmo->freq_hz.denom != freq_hz.denom)) {
		/* freq changed, free old timer and re-allocate */
		em_tmo_state_t cur_state = odp_atomic_load_acq_u32(&tmo->state);

		RETURN_ERROR_IF(cur_state == EM_TMO_STATE_ACTIVE,
				EM_ERR_BAD_STATE, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
				"Cannot change freq while tmo is active");
		odp_timer_free(tmo->odp_timer);
		tmo->odp_timer = ODP_TIMER_INVALID;
	}

	if (tmo->odp_timer == ODP_TIMER_INVALID) {
		const queue_elem_t *q_elem = queue_elem_get(tmo->queue);

		if (EM_CHECK_LEVEL > 1) {
			RETURN_ERROR_IF(q_elem == NULL || !queue_allocated(q_elem),
					EM_ERR_BAD_ARG, EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
					"Invalid queue:%" PRI_QUEUE "", tmo->queue);
		}

		odp_timer_periodic_param_t pparams;

		odp_timer_periodic_param_init(&pparams);
		pparams.queue = q_elem->odp_queue;
		pparams.user_ptr = tmo->user_ptr;
		pparams.freq.freq_hz.integer = freq_hz.integer;
		pparams.freq.freq_hz.numer = freq_hz.numer;
		pparams.freq.freq_hz.denom = freq_hz.denom;
		pparams.uarea_init.init_fn = ring_tmo_uarea_init;
		pparams.uarea_init.args = tmo;

		tmo->odp_timer = odp_timer_periodic_alloc(tmo->odp_timer_pool, &pparams);
		if (unlikely(tmo->odp_timer == ODP_TIMER_INVALID))
			return INTERNAL_ERROR(EM_ERR_LIB_FAILED,
					      EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
					      "odp_timer_periodic_alloc() failed");
		tmo->freq_hz.integer = freq_hz.integer;
		tmo->freq_hz.numer = freq_hz.numer;
		tmo->freq_hz.denom = freq_hz.denom;
		TMR_DBG_PRINT("Allocated periodic freq ODP timer %p, freq=%lu %lu/%lu.\n",
			      tmo->odp_timer, freq_hz.integer, freq_hz.numer, freq_hz.denom);
	}

	TMR_DBG_PRINT("ring freq tmo start_abs %lu, freq=%lu %lu/%lu\n",
		      start_abs, freq_hz.integer, freq_hz.numer, freq_hz.denom);

	odp_timer_periodic_start_t startp;

	startp.first_tick = start_abs;

	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_ACTIVE);
	int odpret = odp_timer_periodic_start(tmo->odp_timer, &startp);

	if (unlikely(odpret != ODP_TIMER_SUCCESS)) {
		odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);

		em_status_t retval = timer_rv_odp2em(odpret);

		if (retval == EM_ERR_TOONEAR) { /* skip errorhandler */
			TMR_DBG_PRINT("TOONEAR, skip ErrH\n");
			return retval;
		}
		if (retval == EM_ERR_BUSY) { /* skip errorhandler */
			TMR_DBG_PRINT("BUSY, skip ErrH\n");
			return retval;
		}
		return INTERNAL_ERROR(retval,
				      EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ,
				      "odp_timer_periodic_start(): ret %d", odpret);
	}
	/* ok */
	TMR_DBG_PRINT("OK\n");
	return EM_OK;
#endif /* !PRE_1_50_ODP_TIMER_API */
}

em_status_t em_tmo_cancel(em_tmo_t tmo, em_event_t *cur_event)
{
	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(tmo == EM_TMO_UNDEF || cur_event == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_TMO_CANCEL,
				"Invalid args: tmo:%" PRI_TMO " cur_event:%p",
				tmo, cur_event);
	}
	*cur_event = EM_EVENT_UNDEF;
	if (EM_CHECK_LEVEL > 1) {
		RETURN_ERROR_IF(!odp_buffer_is_valid(tmo->odp_buffer),
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo buffer");
		RETURN_ERROR_IF(tmo->odp_timer == ODP_TIMER_INVALID,
				EM_ERR_BAD_ID, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo odp_timer");
	}

	/* check state: EM_TMO_STATE_UNKNOWN | EM_TMO_STATE_IDLE | EM_TMO_STATE_ACTIVE */
	em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);

	RETURN_ERROR_IF(tmo_state != EM_TMO_STATE_ACTIVE,
			EM_ERR_BAD_STATE, EM_ESCOPE_TMO_CANCEL,
			"Invalid tmo state:%d (!%d)", tmo_state, EM_TMO_STATE_ACTIVE);

	TMR_DBG_PRINT("ODP tmo %p\n", tmo->odp_timer);

	odp_atomic_store_rel_u32(&tmo->state, EM_TMO_STATE_IDLE);

	if (tmo->is_ring) { /* periodic ring never returns event here */
		RETURN_ERROR_IF(odp_timer_periodic_cancel(tmo->odp_timer) != 0,
				EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CANCEL,
				"odp periodic cancel fail");
		return EM_ERR_TOONEAR; /* ack will tell when no more coming */
	}

	/* not ring, cancel*/
	odp_event_t odp_ev = ODP_EVENT_INVALID;
	int ret = odp_timer_cancel(tmo->odp_timer, &odp_ev);

	if (ret == ODP_TIMER_TOO_NEAR) {
		if (EM_CHECK_LEVEL > 1) {
			RETURN_ERROR_IF(odp_ev != ODP_EVENT_INVALID,
					EM_ERR_BAD_STATE, EM_ESCOPE_TMO_CANCEL,
					"ODP timer cancel return TOONEAR but return event!");
		}
		TMR_DBG_PRINT("ODP returned TOONEAR\n");
		return EM_ERR_TOONEAR;
	}

	RETURN_ERROR_IF(ret == ODP_TIMER_FAIL,
			EM_ERR_BAD_STATE, EM_ESCOPE_TMO_CANCEL,
			"ODP timer cancel fail!");

	/*
	 * Cancel successful (ret == ODP_TIMER_SUCCESS): odp_ev contains the canceled tmo event
	 */

	if (EM_CHECK_LEVEL > 2) {
		RETURN_ERROR_IF(!odp_event_is_valid(odp_ev),
				EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_CANCEL,
				"Invalid tmo event from odp_timer_cancel");
	}

	em_event_t tmo_ev = event_odp2em(odp_ev);
	event_hdr_t *ev_hdr = event_to_hdr(tmo_ev);

	/* successful cancel also resets the event tmo type */
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
	ev_hdr->tmo = EM_TMO_UNDEF;

	if (esv_enabled())
		tmo_ev = evstate_em2usr(tmo_ev, ev_hdr, EVSTATE__TMO_CANCEL);

	*cur_event = tmo_ev;
	TMR_DBG_PRINT("OK\n");
	return EM_OK;
}

/*
 * Re-arm a periodic (non-ring) tmo for its next period, retrying on late/busy
 * timeouts. Returns EM_OK on success, or an error after restoring event state.
 * Kept 'static inline' so the compiler still inlines it into em_tmo_ack() and
 * the generated code (and performance) stays equivalent to the original.
 */
static inline em_status_t tmo_ack_rearm(em_tmo_t tmo, em_event_t next_tmo_ev,
					event_hdr_t *ev_hdr, odp_event_t odp_ev,
					bool esv_ena)
{
	int ret;
	int tries = EM_TIMER_ACK_TRIES;
	em_status_t err;
	odp_timer_start_t startp;

	startp.tick_type = ODP_TIMER_TICK_ABS;
	startp.tmo_ev = odp_ev;
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_PERIODIC; /* could be new event */
	ev_hdr->tmo = tmo;

	/* try to set tmo EM_TIMER_ACK_TRIES times */
	do {
		/* ask new timeout for next period */
		startp.tick = tmo->last_tick;
		ret = odp_timer_start(tmo->odp_timer, &startp);

		if (likely(ret == ODP_TIMER_SUCCESS))
			break; /* ok */

#if !PRE_1_50_ODP_TIMER_API
		/*
		 * ODP_TIMER_BUSY: the timer is being expired concurrently,
		 * retry the same start (no skip) until it succeeds or the
		 * retry limit is reached.
		 */
		if (unlikely(ret == ODP_TIMER_BUSY)) {
			if (unlikely(--tries < 1)) {
				err = INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
						     EM_ESCOPE_TMO_ACK,
						     "Tmo ACK: too many retries (busy):%u",
						     EM_TIMER_ACK_TRIES);
				goto ack_err;
			}
			TMR_DBG_PRINT("BUSY, retry\n");
			continue;
		}
#endif

		/*
		 * Calling ack() was delayed over next period if 'ret' is
		 * ODP_TIMER_TOO_NEAR, i.e. now in past. Any other error is
		 * unexpected and fatal for this tmo.
		 */
		if (ret != ODP_TIMER_TOO_NEAR) {
			TMR_DBG_PRINT("ODP return %d\n"
				      "tmo tgt/tick now %lu/%lu\n",
				      ret, tmo->last_tick,
				      odp_timer_current_tick(tmo->odp_timer_pool));
			err = INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_TMO_ACK,
					     "Tmo ACK: failed to renew tmo (odp ret %d)",
					     ret);
			goto ack_err;
		}

		/* ODP_TIMER_TOO_NEAR: ack() delayed beyond next time slot */
		if (EM_TIMER_TMO_STATS)
			tmo->stats.num_late_ack++;
		TMR_DBG_PRINT("late, tgt/now %lu/%lu\n", tmo->last_tick,
			      odp_timer_current_tick(tmo->odp_timer_pool));

		if (tmo->flags & EM_TMO_FLAG_NOSKIP) /* not allowed to skip, send immediately */
			return handle_ack_noskip(next_tmo_ev, ev_hdr, tmo->queue);

		/* skip already passed periods and try again */
		handle_ack_skip(tmo);

		if (unlikely(--tries < 1)) {
			err = INTERNAL_ERROR(EM_ERR_OPERATION_FAILED,
					     EM_ESCOPE_TMO_ACK,
					     "Tmo ACK: too many retries:%u",
					     EM_TIMER_ACK_TRIES);
			goto ack_err;
		}
	} while (ret != ODP_TIMER_SUCCESS);

	return EM_OK;

ack_err:
	/* fail, restore event state */
	ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
	ev_hdr->tmo = EM_TMO_UNDEF;
	if (esv_ena)
		evstate_usr2em_revert(next_tmo_ev, ev_hdr, EVSTATE__TMO_ACK__FAIL);
	return err;
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
	event_hdr_t *ev_hdr = event_to_hdr(next_tmo_ev);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_ODP,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_ACK,
			"Tmo ACK: Given tmo event is not an EM event");

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_TIMER_IND,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_ACK,
			"Tmo ACK: Given tmo event is a timer-ring event");

	odp_event_t odp_ev = event_em2odp(next_tmo_ev);

	/* not periodic ring, set next timeout */
	if (unlikely(tmo_state != EM_TMO_STATE_ACTIVE)) {
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
		ev_hdr->tmo = EM_TMO_UNDEF;

		if (tmo_state == EM_TMO_STATE_IDLE) /* canceled, skip errorhandler */
			return EM_ERR_CANCELED;

		return INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_TMO_ACK,
				      "Tmo ACK: invalid tmo state:%d", tmo_state);
	}

	bool esv_ena = esv_enabled();

	if (esv_ena)
		evstate_usr2em(next_tmo_ev, ev_hdr, EVSTATE__TMO_ACK);
	/*
	 * The periodic timer will silently stop if ack fails! Attempt to
	 * handle exceptions and if the tmo cannot be renewed, call
	 * the errorhandler so the application may recover.
	 */
	tmo->last_tick += tmo->period; /* maintain absolute time */

	return tmo_ack_rearm(tmo, next_tmo_ev, ev_hdr, odp_ev, esv_ena);
}

em_status_t em_tmo_ring_ack(em_tmo_t tmo, em_event_t tmo_ev)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 &&
			(tmo == EM_TMO_UNDEF || tmo_ev == EM_EVENT_UNDEF),
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_RING_ACK,
			"Inv.args: tmo:%" PRI_TMO " ev:%" PRI_EVENT "",
			tmo, tmo_ev);
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_RING_ACK,
			"Tmo ring ACK: invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && !tmo->is_ring,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_RING_ACK,
			"Tmo ring ACK: Not a ring tmo");

	if (EM_TIMER_TMO_STATS)
		tmo->stats.num_acks++;

	em_tmo_state_t tmo_state = odp_atomic_load_acq_u32(&tmo->state);
	event_hdr_t *ev_hdr = event_to_hdr(tmo_ev);

	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && ev_hdr->event_type == EM_EVENT_TYPE_ODP,
			EM_ERR_BAD_CONTEXT, EM_ESCOPE_TMO_RING_ACK,
			"Tmo ring ACK: Given tmo event is not an EM event");

	/*
	 * No ESV (event state verification) tracking for ring timer events:
	 * ODP manages the timeout event internally, so the application does
	 * not own it and must not free or modify it.
	 */
	odp_event_t odp_ev = event_em2odp(tmo_ev);

	return ack_ring_timeout_event(tmo, tmo_ev, tmo_state, ev_hdr, odp_ev);
}

int em_timer_list(em_timer_t tmr_list[], int max)
{
	odp_ticketlock_lock(&em_shm->timers.timer_lock);

	const uint32_t num_timers = em_shm->timers.num_timers;

	if (tmr_list && max > 0 && num_timers > 0) {
		int num = 0;

		for (int i = 0; i < EM_ODP_MAX_TIMERS; i++) {
			if (em_shm->timers.timer[i].odp_tmr_pool != ODP_TIMER_POOL_INVALID) {
				tmr_list[num] = TMR_I2H(i);
				num++;
				if (num >= max)
					break;
			}
		}
	}

	odp_ticketlock_unlock(&em_shm->timers.timer_lock);

	return num_timers;
}

em_status_t em_timer_attr(em_timer_t tmr, em_timer_attr_t *tmr_attr)
{
	odp_timer_pool_info_t poolinfo;
	int i = TMR_H2I(tmr);
	int ret;
	em_timer_clksrc_t clk = EM_TIMER_CLKSRC_DEFAULT;

	if (EM_CHECK_LEVEL > 0)
		RETURN_ERROR_IF(!is_timer_valid(tmr) || tmr_attr == NULL,
				EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_ATTR,
				"Inv.args: timer:%" PRI_TMR " tmr_attr:%p",
				tmr, tmr_attr);

	/* get current values from ODP */
	ret = odp_timer_pool_info(em_shm->timers.timer[i].odp_tmr_pool, &poolinfo);
	RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_TIMER_ATTR,
			"ODP timer pool info failed");

	timer_clksrc_odp2em(poolinfo.param.clk_src, &clk);

	if (poolinfo.param.timer_type == ODP_TIMER_TYPE_SINGLE) {
		tmr_attr->resparam.res_ns = poolinfo.param.res_ns;
		tmr_attr->resparam.res_hz = poolinfo.param.res_hz;
		tmr_attr->resparam.max_tmo = poolinfo.param.max_tmo;
		tmr_attr->resparam.min_tmo = poolinfo.param.min_tmo;
		tmr_attr->resparam.clk_src = clk;
		tmr_attr->max_pending_events = 0;
		memset(&tmr_attr->ringparam, 0, sizeof(em_timer_ring_param_t));
		memset(&tmr_attr->freqparam, 0, sizeof(em_timer_ring_freq_param_t));
#if PRE_1_50_ODP_TIMER_API
	} else {
		tmr_attr->ringparam.base_hz.integer = poolinfo.param.periodic.base_freq_hz.integer;
		tmr_attr->ringparam.base_hz.numer = poolinfo.param.periodic.base_freq_hz.numer;
		tmr_attr->ringparam.base_hz.denom = poolinfo.param.periodic.base_freq_hz.denom;
		tmr_attr->ringparam.max_mul = poolinfo.param.periodic.max_multiplier;
		tmr_attr->ringparam.res_ns = poolinfo.param.res_ns;
		tmr_attr->ringparam.clk_src = clk;
		tmr_attr->max_pending_events = 0;
		memset(&tmr_attr->resparam, 0, sizeof(em_timer_res_param_t));
		memset(&tmr_attr->freqparam, 0, sizeof(em_timer_ring_freq_param_t));
#else
	} else if (poolinfo.param.timer_type == ODP_TIMER_TYPE_PERIODIC_BASE_MUL) {
		tmr_attr->ringparam.base_hz.integer =
			poolinfo.param.periodic.base_mul.base_freq_hz.integer;
		tmr_attr->ringparam.base_hz.numer =
			poolinfo.param.periodic.base_mul.base_freq_hz.numer;
		tmr_attr->ringparam.base_hz.denom =
			poolinfo.param.periodic.base_mul.base_freq_hz.denom;
		tmr_attr->ringparam.max_mul =
			poolinfo.param.periodic.base_mul.max_multiplier;
		tmr_attr->ringparam.res_ns = poolinfo.param.res_ns;
		tmr_attr->ringparam.clk_src = clk;
		tmr_attr->max_pending_events = poolinfo.param.periodic.max_pending_tmo;
		memset(&tmr_attr->resparam, 0, sizeof(em_timer_res_param_t));
		memset(&tmr_attr->freqparam, 0, sizeof(em_timer_ring_freq_param_t));
	} else { /* ODP_TIMER_TYPE_PERIODIC_FREQ */
		tmr_attr->freqparam.freq_hz =
			(em_fract_u64_t *)(uintptr_t)poolinfo.param.periodic.freq.freq_hz;
		tmr_attr->freqparam.num = poolinfo.param.periodic.freq.num;
		tmr_attr->freqparam.res_ns = poolinfo.param.res_ns;
		tmr_attr->freqparam.clk_src = clk;
		tmr_attr->max_pending_events = poolinfo.param.periodic.max_pending_tmo;
		memset(&tmr_attr->resparam, 0, sizeof(em_timer_res_param_t));
		memset(&tmr_attr->ringparam, 0, sizeof(em_timer_ring_param_t));
#endif
	}

	tmr_attr->num_tmo = poolinfo.param.num_timers;
	tmr_attr->flags = em_shm->timers.timer[i].flags;
	tmr_attr->priority = poolinfo.param.priority;

	strncpy(tmr_attr->name, poolinfo.name, EM_TIMER_NAME_LEN - 1);
	tmr_attr->name[EM_TIMER_NAME_LEN - 1] = '\0';
	return EM_OK;
}

uint64_t em_timer_freq(em_timer_t tmr)
{
	const timer_storage_t *const tmrs = &em_shm->timers;

	if (EM_CHECK_LEVEL > 0 && !is_timer_valid(tmr)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TIMER_FREQ,
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

em_tmo_state_t em_tmo_state(em_tmo_t tmo)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(tmo == EM_TMO_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_STATE, "Invalid tmo");
		return EM_TMO_STATE_UNKNOWN;
	}
	if (EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer)) {
		INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_TMO_STATE, "Invalid tmo buffer");
		return EM_TMO_STATE_UNKNOWN;
	}

	return odp_atomic_load_acq_u32(&tmo->state);
}

em_status_t em_tmo_stats(em_tmo_t tmo, em_tmo_stats_t *stat)
{
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && tmo == EM_TMO_UNDEF,
			EM_ERR_BAD_ARG, EM_ESCOPE_TMO_STATS,
			"Invalid tmo");
	/* check that tmo buf is valid before accessing other struct members */
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer),
			EM_ERR_BAD_ID, EM_ESCOPE_TMO_STATS,
			"Invalid tmo buffer");
	RETURN_ERROR_IF(EM_CHECK_LEVEL > 0 && tmo->odp_timer == ODP_TIMER_INVALID,
			EM_ERR_BAD_STATE, EM_ESCOPE_TMO_STATS,
			"tmo deleted?");

	if (EM_TIMER_TMO_STATS) {
		if (stat)
			*stat = tmo->stats;
	} else {
		return EM_ERR_NOT_IMPLEMENTED;
	}

	return EM_OK;
}

em_tmo_type_t em_tmo_type(em_event_t event, em_tmo_t *tmo, bool reset)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_STATE, "Invalid event given");
		return EM_TMO_TYPE_NONE;
	}

	event_hdr_t *ev_hdr = event_to_hdr(event);
	em_event_type_t evtype_major = em_event_type_major(ev_hdr->event_type);
	em_tmo_type_t tmo_type = (em_tmo_type_t)ev_hdr->flags.tmo_type;

	if (EM_CHECK_LEVEL > 1 && unlikely(!can_have_tmo_type(evtype_major))) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_STATE,
			       "Invalid event type:%u", evtype_major);
		return EM_TMO_TYPE_NONE;
	}

	if (EM_CHECK_LEVEL > 2 && unlikely(tmo_type > EM_TMO_TYPE_PERIODIC)) {
		INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_TMO_STATE,
			       "Invalid tmo event type, header corrupted?");
		return EM_TMO_TYPE_NONE;
	}

	if (tmo)
		*tmo = (tmo_type == EM_TMO_TYPE_NONE) ? EM_TMO_UNDEF : ev_hdr->tmo;

	if (reset && evtype_major != EM_EVENT_TYPE_TIMER_IND) {
		ev_hdr->flags.tmo_type = EM_TMO_TYPE_NONE;
		ev_hdr->tmo = EM_TMO_UNDEF;
	}

	return tmo_type;
}

void *em_tmo_userptr(em_event_t event, em_tmo_t *tmo)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(event == EM_EVENT_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_USERPTR, "Invalid event given");
		return NULL;
	}

	odp_event_t odp_event = event_em2odp(event);
	odp_event_type_t evtype = odp_event_type(odp_event);

	if (unlikely(evtype != ODP_EVENT_TIMEOUT)) /* no errorhandler for other events */
		return NULL;

	event_hdr_t *ev_hdr = event_to_hdr(event);

	if (tmo) /* always periodic timeout here */
		*tmo = ev_hdr->tmo;

	return odp_timeout_user_ptr(odp_timeout_from_event(odp_event));
}

em_timer_t em_tmo_timer(em_tmo_t tmo)
{
	if (EM_CHECK_LEVEL > 0 && unlikely(tmo == EM_TMO_UNDEF)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_TIMER, "Invalid tmo given");
		return EM_TIMER_UNDEF;
	}
	if (EM_CHECK_LEVEL > 1 && !odp_buffer_is_valid(tmo->odp_buffer)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_TMO_TIMER, "Corrupted tmo?");
		return EM_TIMER_UNDEF;
	}

	return tmo->timer;
}

uint64_t em_timer_to_u64(em_timer_t timer)
{
	return (uint64_t)timer;
}

uint64_t em_tmo_to_u64(em_tmo_t tmo)
{
	return (uint64_t)tmo;
}
