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

/* #define TIMER_DEBUG */

#ifdef TIMER_DEBUG
#include <stdio.h>
#define TMR_DBG_PRINT(format, ...) \
	EM_LOG(EM_LOG_DBG, "TMR: " format, __VA_ARGS__)
#else
#define TMR_DBG_PRINT(format, ...) do {} while (0)
#endif

em_status_t timer_init(timer_storage_t *const tmrs);
em_status_t timer_init_local(void);
em_status_t timer_term_local(void);
em_status_t timer_term(void);

static inline int
timer_clksrc_em2odp(em_timer_clksrc_t clksrc_em,
		    odp_timer_clk_src_t *clksrc_odp /* out */)
{
	switch (clksrc_em) {
	case EM_TIMER_CLKSRC_DEFAULT: /* fallthrough */
	case EM_TIMER_CLKSRC_CPU:
		*clksrc_odp = ODP_CLOCK_CPU;
		return 0;
	case EM_TIMER_CLKSRC_EXT:
		*clksrc_odp = ODP_CLOCK_EXT;
		return 0;
	default:
		return -1;
	}
}

static inline int
timer_clksrc_odp2em(odp_timer_clk_src_t clksrc_odp,
		    em_timer_clksrc_t *clksrc_em /* out */)
{
	switch (clksrc_odp) {
	case ODP_CLOCK_CPU:
		*clksrc_em = EM_TIMER_CLKSRC_CPU;
		return 0;
	case ODP_CLOCK_EXT:
		*clksrc_em = EM_TIMER_CLKSRC_EXT;
		return 0;
	default:
		return -1;
	}
}

#endif /* EM_TIMER_H_ */
