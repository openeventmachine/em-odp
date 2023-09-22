/* Copyright (c) 2023, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <inttypes.h>
#include <stdlib.h>
#include <event_machine.h>
#include <odp_api.h>
#include <odp/helper/odph_api.h>

/* Number of API function calls per test case */
#define REPEAT_COUNT 1000

/* Default number of rounds per test case */
#define ROUNDS 1000u

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define BENCH_INFO(run, init, term, max, name) \
	{#run, run, init, term, max, name}

/* Initialize benchmark resources */
typedef void (*bench_init_fn_t)(void);

/* Run benchmark, returns >0 on success */
typedef int (*bench_run_fn_t)(void);

/* Release benchmark resources */
typedef void (*bench_term_fn_t)(void);

/* Benchmark data */
typedef struct {
	/* Default test name */
	const char *name;

	/* Test function to run */
	bench_run_fn_t run;

	/* Initialize test */
	bench_init_fn_t init;

	/* Terminate test */
	bench_term_fn_t term;

	/* Test specific limit for rounds (tuning for slow implementation) */
	uint32_t max_rounds;

	/* Override default test name */
	const char *desc;

} bench_info_t;

/* Common command line options */
typedef struct {
	/* Measure time vs CPU cycles */
	int time;

	/* Benchmark index to run indefinitely */
	int bench_idx;

	/* Rounds per test case */
	uint32_t rounds;

	/* Write result to a csv file(mainly used in CI) or not */
	int write_csv;
} cmd_opt_t;

typedef struct {
	cmd_opt_t opt;

	/* Benchmark functions */
	bench_info_t *bench;

	/* Number of benchmark functions */
	int num_bench;

	/* Result of the test e.g. CPU cycles per run */
	double *result;

	/* Benchmark run failed */
	int bench_failed;

} run_bench_arg_t;

extern odp_atomic_u32_t exit_thread;

int setup_sig_handler(void);
int run_benchmarks(void *arg);
void fill_time_str(char *time_str/*out*/);

#endif /* BENCH_COMMON_H */
