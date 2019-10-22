/*
 *   Copyright (c) 2018, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_INIT_H_
#define EVENT_MACHINE_INIT_H_

/**
 * @file
 * @defgroup init Initialization and termination
 *  Event Machine initialization and termination
 * @{
 *
 * The Event Machine must be initialized before use. One core that will be part
 * of EM calls em_init(). Additionally, after the user has set up the threads,
 * or processes and pinned those to HW-cores, each participating core, i.e.
 * EM-core, needs to run em_init_core(). Only now is an EM-core ready to use the
 * other EM API functions and can finally enter the dispatch-loop via
 * em_dispath() on each core that should handle events.
 *
 * The EM termination sequence runs in the opposite order: each core needs to
 * call em_term_core() before one last call to em_term().
 *
 * The 'em_conf_t' type given to em_init() and em_term() is HW/platform specific
 * and is defined in event_machine_hw_types.h
 *
 * Do not include this from the application, event_machine.h will
 * do it for you.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Event Machine.
 *
 * Must be called once at startup. Additionally each EM-core needs to call the
 * em_init_core() function before using any further EM API functions/resources.
 *
 * @param conf   EM runtime config options,
 *               HW/platform specific: see event_machine_hw_types.h
 *
 * @return EM_OK if successful.
 *
 * @see em_init_core() for EM-core specific init after em_init().
 */
em_status_t
em_init(em_conf_t *conf);

/**
 * Initialize an EM-core.
 *
 * Must be called once by each EM-core (= process, thread or bare metal core).
 * EM queues, EOs, queue groups etc. can be created after a successful return
 * from this function.
 *
 * @return EM_OK if successful.
 *
 * @see em_init()
 */
em_status_t
em_init_core(void);

/**
 * Terminate the Event Machine.
 *
 * Called once at exit. Additionally, before the one call to em_term(),
 * each EM-core needs to call the em_term_core() function to free up local
 * resources.
 *
 * @param conf          EM runtime config options
 *
 * @return EM_OK if successful.
 *
 * @see em_term_core() for EM-core specific termination before em_term().
 */
em_status_t
em_term(em_conf_t *conf);

/**
 * Terminate an EM-core.
 *
 * Called by each EM-core (= process, thread or bare metal core) before
 * the one call call to em_term();
 *
 * @return EM_OK if successful.
 *
 * @see em_term()
 */
em_status_t
em_term_core(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_INIT_H_ */
