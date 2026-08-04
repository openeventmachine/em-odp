/*
 *   Copyright (c) 2015-2024, Nokia Solutions and Networks
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

#ifndef EM_CORE_H_
#define EM_CORE_H_

/**
 * @file
 * EM internal core-related functions
 */

#include <stdio.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_core_types.h"
#include "em_internal_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

em_status_t core_map_init(core_map_t *const core_map);
em_status_t core_map_init_local(core_map_t *const core_map, const em_conf_local_t *conf_local);
em_status_t core_map_init_extthr(core_map_t *const core_map);

void i_event__term_thread_req(const internal_event_t *i_ev);

int core_map_term_local(core_map_t *const core_map, em_escope_t escope);
int core_map_term_extthr(core_map_t *const core_map, em_escope_t escope);

void mask_em2odp__rwlocked(const em_core_mask_t *const core_mask,
			   odp_thrmask_t *const odp_thrmask /*out*/);
void mask_em2odp(const em_core_mask_t *const em_core_mask,
		 odp_thrmask_t *const odp_thrmask /*out*/);
int read_proc_cpuid(FILE *proc_tid_stat_file);

#ifdef __cplusplus
}
#endif

#endif /* EM_CORE_H_ */
