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

#ifndef EM_POOL_H_
#define EM_POOL_H_

/**
 * @file
 * EM internal event pool functions
 *
 */

#include <stdint.h>

#include <odp_api.h>

#include <event_machine.h>

#include "em_pool_inline.h"
#include "em_pool_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define valid_pool(pool)   ((unsigned int)pool_hdl2idx((pool)) < \
			    EM_CONFIG_POOLS)
#define invalid_pool(pool) ((unsigned int)pool_hdl2idx((pool)) > \
			    EM_CONFIG_POOLS - 1)

int invalid_pool_cfg(const em_pool_cfg_t *pool_cfg, const char **err_str/*out*/);

int check_pool_uarea_persistence(const em_pool_cfg_t *pool_cfg, const char **err_str/*out*/);

em_status_t
pool_init(mpool_tbl_t *const mpool_tbl, mpool_pool_t *const mpool_pool,
	  const em_pool_cfg_t *default_pool_cfg);

em_status_t
pool_term(const mpool_tbl_t *pool_tbl);

em_pool_t
pool_create(const char *name, em_pool_t req_pool, const em_pool_cfg_t *pool_cfg);

em_status_t
pool_delete(em_pool_t pool);

em_pool_t
pool_find(const char *name);

void pool_info_print_hdr(unsigned int num_pools);
void pool_info_print(em_pool_t pool);
void pool_stats_opt_print_hdr(void);
void pool_stats_opt_print(em_pool_t pool);
void pool_stats_opt_odp2em(em_pool_stats_opt_t *em_stats_opt /*out*/,
			   const odp_pool_stats_opt_t *odp_stats_opt);

void pool_stats_print(em_pool_t pool);

void subpools_stats_print(em_pool_t pool, const int subpools[], int num_subpools);
void pool_stats_selected_print(em_pool_t pool, const em_pool_stats_opt_t *opt);
void subpools_stats_selected_print(em_pool_t pool, const int subpools[],
				   int num_subpools, const em_pool_stats_opt_t *opt);

void print_pool_elem_info(void);

uint32_t align_offset_get(const em_pool_cfg_t *pool_cfg);
uint32_t odp_align_get(const em_pool_cfg_t *pool_cfg);

unsigned int pool_count(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_POOL_H_ */
