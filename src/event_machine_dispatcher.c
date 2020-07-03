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

uint64_t
em_dispatch(uint64_t rounds)
{
	uint64_t i;
	uint64_t events = 0;
	int rx_events = 0;
	int dispatched_events;
	int num;

	const em_input_poll_func_t input_poll =
		em_shm->conf.input.input_poll_fn;
	const em_output_drain_func_t output_drain =
		em_shm->conf.output.output_drain_fn;

	odp_schedule_resume();

	if (likely(rounds > 0)) {
		if (em_locm.do_input_poll || em_locm.do_output_drain) {
			for (i = 0; i < rounds;) {
				dispatched_events = 0;
				if (em_locm.do_input_poll)
					rx_events = input_poll();
				do {
					num = dispatch_round();
					dispatched_events += num;
					i++; /* inc rounds */
				} while (dispatched_events < rx_events &&
					 num > 0 && i < rounds);
				events += dispatched_events; /* inc ret value*/
				if (em_locm.do_output_drain)
					(void)output_drain();
			}
		} else {
			for (i = 0; i < rounds; i++)
				events += dispatch_round();
		}

		/* pause scheduling before exiting the dispatch loop */
		odp_schedule_pause();
		/* empty the locally pre-scheduled events (if any) */
		do {
			num = dispatch_round();
			events += num;
		} while (num > 0);
	} else {
		/* rounds == 0 (== FOREVER) */
		if (em_locm.do_input_poll || em_locm.do_output_drain) {
			for (;/*ever*/;) {
				dispatched_events = 0;
				if (em_locm.do_input_poll)
					rx_events = input_poll();
				do {
					num = dispatch_round();
					dispatched_events += num;
				} while (dispatched_events < rx_events &&
					 num > 0);
				if (em_locm.do_output_drain)
					(void)output_drain();
			}
		} else {
			for (;/*ever*/;)
				dispatch_round();
		}
	}

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
