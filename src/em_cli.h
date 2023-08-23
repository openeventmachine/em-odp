/* Copyright (c) 2021, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef EM_CLI_H_
#define EM_CLI_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup EM CLI (Command Line Interface) related
 *  EM CLI specific helper APIs.
 * @{
 *
 * These helper APIs can be used to start/stop an EM CLI socket server providing
 * a list of CLI commands, which can be used to get info about EM (e.g. pools,
 * queues, core map and CPU arch) and info about the underlying ODP instance.
 * The EM CLI server may be connected to with a telnet client.
 *
 * Note that these EM CLI helper APIs must be called after em_init().
 *
 * Note that the dependent libcli must be installed to be able to use EM CLI.
 * libcli can be installed with "sudo apt-get install libcli-dev"
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the EM CLI (if enabled)
 *
 * @return EM_OK if successful.
 */
em_status_t emcli_init(void);

/**
 * @brief Initialize the EM CLI locally on an EM core (if enabled)
 *
 * @return EM_OK if successful
 */
em_status_t emcli_init_local(void);

/**
 * @brief Terminate the EM CLI (if enabled)
 *
 * @return EM_OK if successful.
 */
em_status_t emcli_term(void);

/**
 * @brief Terminate the EM CLI locally on an EM core (if enabled)
 *
 * @return EM_OK if successful
 */
em_status_t emcli_term_local(void);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EM_CLI_H_ */
