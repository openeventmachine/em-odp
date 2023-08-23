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

#if EM_CLI

typedef struct {
	/** Handle for this shared memory */
	odp_shm_t this_shm;
	/** ODP control thread for running EM-CLI server */
	odph_thread_t em_cli_thread;
} em_cli_shm_t;

#endif /* EM_CLI */

#endif /* EM_CLI_TYPES_H_ */
