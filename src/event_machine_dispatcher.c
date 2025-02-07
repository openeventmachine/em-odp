/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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
#include "em_dispatcher_inline.h"

static const em_dispatch_opt_t dispatch_opt_default = {
	.burst_size = EM_SCHED_MULTI_MAX_BURST,
	.__internal_check = EM_CHECK_INIT_CALLED
	/* other members initialized to 0 or NULL as per C standard */
};

uint64_t em_dispatch(uint64_t rounds /* 0 = forever */)
{
	uint64_t events;

	em_locm_t *const locm = &em_locm;
	const bool do_input_poll = locm->do_input_poll;
	const bool do_output_drain = locm->do_output_drain;
	const bool do_schedule_pause = em_shm->opt.dispatch.sched_pause;

	if (locm->is_sched_paused) {
		odp_schedule_resume();
		locm->is_sched_paused = false;
	}

	if (do_input_poll || do_output_drain)
		events = dispatch_with_userfn(rounds, do_input_poll, do_output_drain);
	else
		events = dispatch_no_userfn(rounds);

	if (do_schedule_pause) {
		/* pause scheduling before exiting the dispatch loop */
		int round_events;

		odp_schedule_pause();
		locm->is_sched_paused = true;

		/* empty the locally pre-scheduled events (if any) */
		do {
			round_events = dispatch_round(ODP_SCHED_NO_WAIT,
						      EM_SCHED_MULTI_MAX_BURST, NULL);
			events += round_events;
		} while (round_events > 0);
	}

	return events;
}

void em_dispatch_opt_init(em_dispatch_opt_t *opt)
{
	if (unlikely(!opt)) {
		INTERNAL_ERROR(EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_OPT_INIT,
			       "Bad argument, opt=NULL");
		return;
	}

	*opt = dispatch_opt_default;
}

static inline em_status_t
dispatch_duration(const em_dispatch_duration_t *duration,
		  const em_dispatch_opt_t *opt,
		  em_dispatch_results_t *results /*out, optional*/)
{
	em_locm_t *const locm = &em_locm;
	const bool do_input_poll = locm->do_input_poll && !opt->skip_input_poll;
	const bool do_output_drain = locm->do_output_drain && !opt->skip_output_drain;
	const bool do_sched_pause = opt->sched_pause;
	uint64_t events;

	if (locm->is_sched_paused) {
		odp_schedule_resume();
		locm->is_sched_paused = false;
	}

	if (do_input_poll || do_output_drain)
		events = dispatch_duration_with_userfn(duration, opt, results,
						       do_input_poll, do_output_drain);
	else
		events = dispatch_duration_no_userfn(duration, opt, results);

	/* pause scheduling before exiting the dispatch loop */
	if (do_sched_pause) {
		odp_schedule_pause();
		locm->is_sched_paused = true;

		int round_events;
		uint64_t rounds = 0;
		uint16_t burst_size = opt->burst_size;

		/* empty the locally pre-scheduled events (if any) */
		do {
			round_events = dispatch_round(ODP_SCHED_NO_WAIT,
						      burst_size, opt);
			events += round_events;
			rounds++;
		} while (round_events > 0);

		if (results) {
			results->rounds += rounds;
			results->events = events;
		}
	}

	return EM_OK;
}

em_status_t em_dispatch_duration(const em_dispatch_duration_t *duration,
				 const em_dispatch_opt_t *opt /* optional */,
				 em_dispatch_results_t *results /*out, optional*/)
{
	RETURN_ERROR_IF(!duration, EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_DURATION,
			"Bad argument: duration=NULL");

	if (!opt) {
		opt = &dispatch_opt_default;
	} else {
		RETURN_ERROR_IF(opt->__internal_check != EM_CHECK_INIT_CALLED,
				EM_ERR_NOT_INITIALIZED, EM_ESCOPE_DISPATCH_DURATION,
				"Not initialized: em_dispatch_opt_init(opt) not called");
	}

	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(opt->burst_size == 0 || opt->burst_size > EM_SCHED_MULTI_MAX_BURST,
				EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_DURATION,
				"Bad option: 0 < opt.burst_size (%" PRIu64 ") <= %u (max)",
				opt->burst_size, EM_SCHED_MULTI_MAX_BURST);
	}

	if (EM_CHECK_LEVEL > 1) {
		/* _FLAG_LAST is 'pow2 + 1' */
		const em_dispatch_duration_select_t next_pow2 =
			(EM_DISPATCH_DURATION_LAST >> 1) << 2;

		RETURN_ERROR_IF(duration->select >= next_pow2,
				EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_DURATION,
				"Bad option: duration->select=0x%x invalid", duration->select);
		RETURN_ERROR_IF(((duration->select & EM_DISPATCH_DURATION_ROUNDS &&
				  duration->rounds == 0) ||
				 (duration->select & EM_DISPATCH_DURATION_NS &&
				  duration->ns == 0) ||
				 (duration->select & EM_DISPATCH_DURATION_EVENTS &&
				  duration->events == 0) ||
				 (duration->select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS &&
				  duration->no_events.rounds == 0) ||
				 (duration->select & EM_DISPATCH_DURATION_NO_EVENTS_NS &&
				  duration->no_events.ns == 0)),
				EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_DURATION,
				"Bad option: opt.duration is zero(0).");
	}

	return dispatch_duration(duration, opt, results);
}

em_status_t em_dispatch_ns(uint64_t ns,
			   const em_dispatch_opt_t *opt,
			   em_dispatch_results_t *results /*out*/)
{
	RETURN_ERROR_IF(ns == 0, EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_NS,
			"Bad argument: ns=0");

	if (!opt) {
		opt = &dispatch_opt_default;
	} else {
		RETURN_ERROR_IF(opt->__internal_check != EM_CHECK_INIT_CALLED,
				EM_ERR_NOT_INITIALIZED, EM_ESCOPE_DISPATCH_NS,
				"Not initialized: em_dispatch_opt_init(opt) not called");
	}

	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(opt->burst_size == 0 || opt->burst_size > EM_SCHED_MULTI_MAX_BURST,
				EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_NS,
				"Bad option: 0 < opt.burst_size (%" PRIu64 ") <= %u (max)",
				opt->burst_size, EM_SCHED_MULTI_MAX_BURST);
	}

	const em_dispatch_duration_t duration = {
		.select = EM_DISPATCH_DURATION_NS,
		.ns = ns
	};

	return dispatch_duration(&duration, opt, results);
}

em_status_t em_dispatch_events(uint64_t events,
			       const em_dispatch_opt_t *opt,
			       em_dispatch_results_t *results /*out*/)
{
	RETURN_ERROR_IF(events == 0, EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_EVENTS,
			"Bad argument: events=0");

	if (!opt) {
		opt = &dispatch_opt_default;
	} else {
		RETURN_ERROR_IF(opt->__internal_check != EM_CHECK_INIT_CALLED,
				EM_ERR_NOT_INITIALIZED, EM_ESCOPE_DISPATCH_EVENTS,
				"Not initialized: em_dispatch_opt_init(opt) not called");
	}

	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(opt->burst_size == 0 || opt->burst_size > EM_SCHED_MULTI_MAX_BURST,
				EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_EVENTS,
				"Bad option: 0 < opt.burst_size (%" PRIu64 ") <= %u (max)",
				opt->burst_size, EM_SCHED_MULTI_MAX_BURST);
	}

	const em_dispatch_duration_t duration = {
		.select = EM_DISPATCH_DURATION_EVENTS,
		.events = events
	};

	return dispatch_duration(&duration, opt, results);
}

em_status_t em_dispatch_rounds(uint64_t rounds,
			       const em_dispatch_opt_t *opt,
			       em_dispatch_results_t *results /*out*/)
{
	RETURN_ERROR_IF(rounds == 0, EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_ROUNDS,
			"Bad argument: rounds=0");

	if (!opt) {
		opt = &dispatch_opt_default;
	} else {
		RETURN_ERROR_IF(opt->__internal_check != EM_CHECK_INIT_CALLED,
				EM_ERR_NOT_INITIALIZED, EM_ESCOPE_DISPATCH_ROUNDS,
				"Not initialized: em_dispatch_opt_init(opt) not called");
	}

	if (EM_CHECK_LEVEL > 0) {
		RETURN_ERROR_IF(opt->burst_size == 0 || opt->burst_size > EM_SCHED_MULTI_MAX_BURST,
				EM_ERR_BAD_ARG, EM_ESCOPE_DISPATCH_ROUNDS,
				"Bad option: 0 < opt.burst_size (%" PRIu64 ") <= %u (max)",
				opt->burst_size, EM_SCHED_MULTI_MAX_BURST);
	}

	const em_dispatch_duration_t duration = {
		.select = EM_DISPATCH_DURATION_ROUNDS,
		.rounds = rounds
	};

	return dispatch_duration(&duration, opt, results);
}

em_status_t
em_dispatch_register_enter_cb(em_dispatch_enter_func_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_DISPATCH_CALLBACKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_DISPATCH_REGISTER_ENTER_CB,
			"EM dispatch callbacks disabled");

	hook_fn.disp_enter = func;
	stat = hook_register(DISPATCH_CALLBACK_ENTER, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat,
			EM_ESCOPE_DISPATCH_REGISTER_ENTER_CB,
			"Dispatch callback register failed");

	return EM_OK;
}

em_status_t
em_dispatch_unregister_enter_cb(em_dispatch_enter_func_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_DISPATCH_CALLBACKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_DISPATCH_UNREGISTER_ENTER_CB,
			"EM dispatch callbacks disabled");

	hook_fn.disp_enter = func;
	stat = hook_unregister(DISPATCH_CALLBACK_ENTER, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat,
			EM_ESCOPE_DISPATCH_UNREGISTER_ENTER_CB,
			"Dispatch callback unregister failed");

	return EM_OK;
}

em_status_t
em_dispatch_register_exit_cb(em_dispatch_exit_func_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_DISPATCH_CALLBACKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_DISPATCH_REGISTER_EXIT_CB,
			"EM dispatch callbacks disabled");

	hook_fn.disp_exit = func;
	stat = hook_register(DISPATCH_CALLBACK_EXIT, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat,
			EM_ESCOPE_DISPATCH_REGISTER_EXIT_CB,
			"Dispatch callback register failed");
	return EM_OK;
}

em_status_t
em_dispatch_unregister_exit_cb(em_dispatch_exit_func_t func)
{
	hook_fn_t hook_fn;
	em_status_t stat;

	RETURN_ERROR_IF(!EM_DISPATCH_CALLBACKS_ENABLE, EM_ERR_NOT_IMPLEMENTED,
			EM_ESCOPE_DISPATCH_UNREGISTER_EXIT_CB,
			"EM dispatch callbacks disabled");

	hook_fn.disp_exit = func;
	stat = hook_unregister(DISPATCH_CALLBACK_EXIT, hook_fn);
	RETURN_ERROR_IF(stat != EM_OK, stat,
			EM_ESCOPE_DISPATCH_UNREGISTER_EXIT_CB,
			"Dispatch callback unregister failed");
	return EM_OK;
}
