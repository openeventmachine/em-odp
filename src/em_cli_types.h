/* Copyright (c) 2021, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 * EM Command Line Interface support
 */

#ifndef EM_CLI_TYPES_H_
#define EM_CLI_TYPES_H_

#include <stdbool.h>
#include <stddef.h> /* offsetof */
#include <stdint.h>

#include <odp_api.h>
#include <odp/helper/odph_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#ifdef __cplusplus
extern "C" {
#endif

#if EM_CLI
typedef struct {
	struct {
		odp_atomic_u32_t seq;

		bool while_idle;
		odp_time_t to_active;
		odp_time_t to_idle;
		uint64_t active_sum;

		/** Previous active sum for this core */
		uint64_t prev_active_sum;

		/** pad size to a multiple of cache line size */
		void *end[0] ENV_CACHE_LINE_ALIGNED;
	} core_times[EM_MAX_CORES] ENV_CACHE_LINE_ALIGNED;

	/** Start time for calcuting core usage */
	odp_time_t t0;

	/** Previous measure time */
	odp_time_t t_prev;
} em_cli_top_t;

/* Verify that the core_times starts at a cache lines */
COMPILE_TIME_ASSERT(offsetof(em_cli_top_t, core_times) % ENV_CACHE_LINE_SIZE == 0,
		    OFFSETOF_CORE_TIMES_ERROR);

/* Verify that the core_times[1] starts at a separate cache lines */
COMPILE_TIME_ASSERT(offsetof(em_cli_top_t, core_times[1]) % ENV_CACHE_LINE_SIZE == 0,
		    OFFSETOF_CORE_TIMES_1_ERROR);

/* Verify that the core_times member size is a multiple of cache lines */
COMPILE_TIME_ASSERT((offsetof(em_cli_top_t, core_times[1]) -
		     offsetof(em_cli_top_t, core_times[0])) %
		    ENV_CACHE_LINE_SIZE == 0,
		    EM_CLI_TOP_CORE_TIMES_SIZE_ERROR);
typedef struct {
	/** Handle for this shared memory */
	odp_shm_t this_shm;
	/** ODP control thread for running EM-CLI server */
	odph_thread_t em_cli_thread;
	/** Has the EM-CLI server thread been created? Is a join() needed or not */
	bool server_thr_created;
	/** Did odph_cli_run() fail? Should odph_cli_stop() be called or not? */
	bool run_failed;

	/** Data used by cli top command */
	em_cli_top_t top;
} em_cli_shm_t;

#endif /* EM_CLI */

#ifdef __cplusplus
}
#endif

#endif /* EM_CLI_TYPES_H_ */
