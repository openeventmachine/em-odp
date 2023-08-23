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

#ifndef CM_SETUP_H
#define CM_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <odp/helper/odph_api.h>
#include <event_machine/platform/env/environment.h>

#define APPL_NAME_LEN  (64)

#define APPL_POOLS_MAX (16)

#define PLAT_PARAM_SIZE  (8)

#define MAX_THREADS (128)

#define IF_NAME_LEN  (16)

#define IF_MAX_NUM  (8)

/** Get rid of path in filename - only for unix-type paths using '/' */
#define NO_PATH(file_name) (strrchr((file_name), '/') ? \
			    strrchr((file_name), '/') + 1 : (file_name))

#define APPL_LOG(level, ...)         appl_log((level), ## __VA_ARGS__)
#define APPL_VLOG(level, fmt, args)  appl_vlog((level), (fmt), (args))
#define APPL_PRINT(...)              APPL_LOG(EM_LOG_PRINT, ## __VA_ARGS__)

/** Simple appl error handling: log & exit */
#define APPL_EXIT_FAILURE(...)  do {			 \
	appl_log(EM_LOG_ERR,				 \
		 "Appl Error: %s:%i, %s() - ",		 \
		 NO_PATH(__FILE__), __LINE__, __func__); \
	appl_log(EM_LOG_ERR, ## __VA_ARGS__);		 \
	appl_log(EM_LOG_ERR, "\n\n");			 \
	exit(EXIT_FAILURE);				 \
} while (0)

#define APPL_ERROR(...)		do {			 \
	appl_log(EM_LOG_ERR,				 \
		 "Appl Error: %s:%i, %s() - ",		 \
		 NO_PATH(__FILE__), __LINE__, __func__); \
	appl_log(EM_LOG_ERR, ## __VA_ARGS__);		 \
	appl_log(EM_LOG_ERR, "\n\n");			 \
} while (0)

/**
 * Application synchronization
 */
typedef struct {
	/** Startup synchronization barrier */
	odp_barrier_t start_barrier;
	/** Exit / termination synchronization barrier */
	odp_barrier_t exit_barrier;
	/** Enter counter for tracking core / odp-thread startup */
	env_atomic64_t enter_count;
	/** Exit counter for tracking core / odp-thread exit */
	env_atomic64_t exit_count;
} sync_t;

/**
 * @brief Application startup mode
 *
 * Enables testing of different startup scenarios.
 */
typedef enum startup_mode {
	/**
	 * Start up & initialize all EM cores before setting up the
	 * application using EM APIs. The em_init() function has been run and
	 * all EM-cores have run em_init_core() before application setup.
	 * Option: -s, --startup-mode = 0 (All EM-cores before application)
	 */
	STARTUP_ALL_CORES = 0,
	/**
	 * Start up & initialize only one EM core before setting up the
	 * application using EM APIs. The em_init() function has been run and
	 * only one EM-core has run em_init_core() before application setup.
	 * Option: -s, --startup-mode = 1 (One EM-core before application...))
	 */
	STARTUP_ONE_CORE_FIRST
} startup_mode_t;

/**
 * @brief Packet input mode
 *
 * Enables testing different packet-IO input modes
 */
typedef enum pktin_mode_t {
	DIRECT_RECV,
	PLAIN_QUEUE,
	SCHED_PARALLEL,
	SCHED_ATOMIC,
	SCHED_ORDERED
} pktin_mode_t;

/**
 * @brief Application packet I/O configuration
 */
typedef struct {
	/** Packet input mode */
	pktin_mode_t in_mode;
	/** Interface count */
	int if_count;
	/** Interface names + placeholder for '\0' */
	char if_name[IF_MAX_NUM][IF_NAME_LEN + 1];
	/** Interface identifiers corresponding to 'if_name[]' */
	int if_ids[IF_MAX_NUM];
	/**
	 * Pktio is setup with an EM event-pool: 'true'
	 * Pktio is setup with an ODP pkt-pool:  'false'
	 */
	bool pktpool_em;

	/** Packet input vectors enabled (true/false) */
	bool pktin_vector;
	/**
	 * If pktin_vector:
	 * Pktio is setup with an EM vector-pool:  'true'
	 * Pktio is setup with an ODP vector-pool: 'false'
	 */
	bool vecpool_em;
} pktio_conf_t;

/**
 * @brief  Application configuration
 */
typedef struct {
	/** application name */
	char name[APPL_NAME_LEN];
	/** number of processes */
	unsigned int num_procs;
	/** number of threads */
	unsigned int num_threads;
	/** dispatch rounds before returning */
	uint32_t dispatch_rounds;
	/** Start-up mode */
	startup_mode_t startup_mode;

	/** number of memory pools set up for the application */
	unsigned int num_pools;
	/** pool ids of the created application pools */
	em_pool_t pools[APPL_POOLS_MAX];

	/** Packet I/O parameters */
	pktio_conf_t pktio;
} appl_conf_t;

/** Application shared memory - allocate in single chunk */
typedef struct {
	/** EM configuration*/
	em_conf_t em_conf;
	/** Application configuration */
	appl_conf_t appl_conf;
	/** Exit the EM-core dispatch loop if set to 1, set by SIGINT handler */
	sig_atomic_t exit_flag;
	/** ODP-thread table (from shared memory for process-per-core mode) */
	odph_thread_t thread_tbl[MAX_THREADS];
	/** Application synchronization vars */
	sync_t sync ENV_CACHE_LINE_ALIGNED;
	/* Pad size to a multiple of cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} appl_shm_t;

/**
 * Global pointer to common application shared memory
 */
extern appl_shm_t *appl_shm;

/**
 * Common setup function for the appliations,
 * usually called directly from main().
 */
int cm_setup(int argc, char *argv[]);

/**
 * All examples implement the test_init(), test_start(), test_stop() and
 * test_term() functions to keep common main() function.
 */
void test_init(void);

void test_start(appl_conf_t *const appl_conf);

void test_stop(appl_conf_t *const appl_conf);

void test_term(void);

int appl_vlog(em_log_level_t level, const char *fmt, va_list args);

__attribute__((format(printf, 2, 3)))
int appl_log(em_log_level_t level, const char *fmt, ...);

void delay_spin(const uint64_t spin_count);

#ifdef __cplusplus
}
#endif

#endif
