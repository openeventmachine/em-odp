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

/*
 * em_dispatch() helper: check if the user provided callback functions
 *			 'input_poll' and 'output_drain' should be called in
 *			 this dispatch round
 */
static inline bool
check_poll_drain_round(unsigned int interval, odp_time_t poll_drain_period)
{
	if (interval > 1) {
		em_locm_t *const locm = &em_locm;

		locm->poll_drain_dispatch_cnt--;
		if (locm->poll_drain_dispatch_cnt == 0) {
			odp_time_t now = odp_time_global();
			odp_time_t period;

			period = odp_time_diff(now, locm->poll_drain_dispatch_last_run);
			locm->poll_drain_dispatch_cnt = interval;

			if (odp_time_cmp(poll_drain_period, period) < 0) {
				locm->poll_drain_dispatch_last_run = now;
				return true;
			}
		}
	} else {
		return true;
	}
	return false;
}

/*
 * em_dispatch() helper: dispatch and call the user provided callback functions
 *                       'input_poll' and 'output_drain'
 */
static inline uint64_t
dispatch_with_userfn(uint64_t rounds, bool do_input_poll, bool do_output_drain)
{
	const bool do_forever = rounds == 0 ? true : false;
	const em_input_poll_func_t input_poll =
		em_shm->conf.input.input_poll_fn;
	const em_output_drain_func_t output_drain =
		em_shm->conf.output.output_drain_fn;
	int rx_events = 0;
	int dispatched_events;
	int round_events;
	uint64_t events = 0;
	bool do_poll_drain_round;
	const unsigned int poll_interval = em_shm->opt.dispatch.poll_drain_interval;
	const odp_time_t poll_period = em_shm->opt.dispatch.poll_drain_interval_time;

	for (uint64_t i = 0; do_forever || i < rounds;) {
		dispatched_events = 0;

		do_poll_drain_round = check_poll_drain_round(poll_interval, poll_period);

		if (do_input_poll && do_poll_drain_round)
			rx_events = input_poll();

		do {
			round_events = dispatch_round();
			dispatched_events += round_events;
			i++; /* inc rounds */
		} while (dispatched_events < rx_events &&
			 round_events > 0 && (do_forever || i < rounds));

		events += dispatched_events; /* inc ret value*/
		if (do_output_drain && do_poll_drain_round)
			(void)output_drain();
	}

	return events;
}

/*
 * em_dispatch() helper: dispatch without calling any user provided callbacks
 */
static inline uint64_t
dispatch_no_userfn(uint64_t rounds)
{
	const bool do_forever = rounds == 0 ? true : false;
	uint64_t events = 0;

	if (do_forever) {
		for (;/*ever*/;)
			dispatch_round();
	} else {
		for (uint64_t i = 0; i < rounds; i++)
			events += dispatch_round();
	}

	return events;
}

uint64_t
em_dispatch(uint64_t rounds /* 0 = forever */)
{
	uint64_t events = 0;
	int round_events;

	const em_locm_t *const locm = &em_locm;
	const bool do_input_poll = locm->do_input_poll;
	const bool do_output_drain = locm->do_output_drain;

	odp_schedule_resume();

	if (do_input_poll || do_output_drain)
		events = dispatch_with_userfn(rounds, do_input_poll,
					      do_output_drain);
	else
		events = dispatch_no_userfn(rounds);

	/* pause scheduling before exiting the dispatch loop */
	odp_schedule_pause();
	/* empty the locally pre-scheduled events (if any) */
	do {
		round_events = dispatch_round();
		events += round_events;
	} while (round_events > 0);

	return events;
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
