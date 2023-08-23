/*
 *   Copyright (c) 2017, Nokia Solutions and Networks
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
#ifndef EM_TIMER_H_
#define EM_TIMER_H_

#include "em_timer_types.h"

#define TMR_DBG_PRINT(fmt, ...) \
	EM_DBG("TMRDBG: %s(): " fmt, __func__, ## __VA_ARGS__)

em_status_t timer_init(timer_storage_t *const tmrs);
em_status_t timer_init_local(void);
em_status_t timer_term_local(void);
em_status_t timer_term(void);

static inline int
timer_clksrc_em2odp(em_timer_clksrc_t clksrc_em,
		    odp_timer_clk_src_t *clksrc_odp /* out */)
{
	switch (clksrc_em) {
	case EM_TIMER_CLKSRC_0:
		*clksrc_odp = ODP_CLOCK_SRC_0;
		break;
	case EM_TIMER_CLKSRC_1:
		*clksrc_odp = ODP_CLOCK_SRC_1;
		break;
	case EM_TIMER_CLKSRC_2:
		*clksrc_odp = ODP_CLOCK_SRC_2;
		break;
	case EM_TIMER_CLKSRC_3:
		*clksrc_odp = ODP_CLOCK_SRC_3;
		break;
	case EM_TIMER_CLKSRC_4:
		*clksrc_odp = ODP_CLOCK_SRC_4;
		break;
	case EM_TIMER_CLKSRC_5:
		*clksrc_odp = ODP_CLOCK_SRC_5;
		break;
	default:
		return -1;
	}
	return 0;
}

static inline int
timer_clksrc_odp2em(odp_timer_clk_src_t clksrc_odp,
		    em_timer_clksrc_t *clksrc_em /* out */)
{
	switch (clksrc_odp) {
	case ODP_CLOCK_SRC_0:
		*clksrc_em = EM_TIMER_CLKSRC_0;
		break;
	case ODP_CLOCK_SRC_1:
		*clksrc_em = EM_TIMER_CLKSRC_1;
		break;
	case ODP_CLOCK_SRC_2:
		*clksrc_em = EM_TIMER_CLKSRC_2;
		break;
	case ODP_CLOCK_SRC_3:
		*clksrc_em = EM_TIMER_CLKSRC_3;
		break;
	case ODP_CLOCK_SRC_4:
		*clksrc_em = EM_TIMER_CLKSRC_4;
		break;
	case ODP_CLOCK_SRC_5:
		*clksrc_em = EM_TIMER_CLKSRC_5;
		break;
	default:
		return -1;
	}
	return 0;
}

#endif /* EM_TIMER_H_ */
