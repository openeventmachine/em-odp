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

#ifndef EM_INIT_H_
#define EM_INIT_H_

/**
 * @file
 * EM internal initialization
 */

#include <stdbool.h>

#include <event_machine.h>

#include "em_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Init values for the em_locm_t struct.
 *
 * Set only non-zero init values, other members initialized to '0'
 * as per C standard when used to initialize a global struct.
 *
 * .core_id: 0 to EM_MAX_CORES-1 for EM_CORE_TYPE_WORKER or EM_CORE_TYPE_CONTROL
 *          -1 for EM_CORE_TYPE_EXTERNAL
 *           Undefined for threads/processes not initialized as EM-cores
 *           Set here to -2 to distinguish from EM_CORE_TYPE_EXTERNAL when not
 *           yet initialized via em_init_local()/em_init_core().
 */
#define EM_LOCM_INIT_VALUES {.core_id = -2, .local_queues.empty = 1 /* rest '0' */}

/**
 * Check whether the current thread/process has already completed EM core-local
 * initialization.
 *
 * Works uniformly for both thread-per-core and process-per-core modes:
 * - If locm->init_pid != getpid(), TLS is stale (first use or post-fork child)
 *   and the core is treated as not initialized.
 * - Otherwise returns locm->is_core_local_inited, which is set true by
 *   em_init_local()/em_init_core() after successful core-local initialization.
 *
 * This is a pure check with no side effects. init_thr_mem() sets init_pid and
 * em_init_local()/em_init_core() set is_core_local_inited.
 */
bool is_core_initialized(void);

extern const em_locm_t locm_init_values; /* = EM_LOCM_INIT_VALUES */

/**
 * Set EM core local log function.
 *
 * Called by EM-core (= process, thread or bare metal core) when a
 * different log function than EM internal log is needed.
 *
 */
void core_log_fn_set(em_log_func_t func);

/**
 * Set EM core local log function with va_list.
 *
 * Called by EM-core (= process, thread or bare metal core) when a
 * different log function than EM internal log is needed.
 *
 */
void core_vlog_fn_set(em_vlog_func_t func);

/**
 * Initialize an external thread for the EM CLI.
 *
 * This function makes sure that EM shared memory has been setup properly before
 * an EM external thread is created.
 *
 * Must be called once by non EM core which wants to access EM shared memory or
 * use EM APIs.
 *
 * @return EM_OK if successful.
 */
em_status_t init_cli_thread(void);

em_status_t sync_api_init_local(void);

em_status_t init_local(const em_conf_local_t *conf_local, em_escope_t escope);
em_status_t init_extthr(const em_conf_local_t *conf_local, em_escope_t escope);

void flush_scheduler_events(void);

em_status_t term_core(em_escope_t escope);
em_status_t term_extthr(em_escope_t escope);

void cfgfile_opts_print(void);
void conf_opts_print(void);
void conf_local_opts_print(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_INIT_H_ */
