/* Copyright (c) 2023, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* Needed for sigaction */
#endif

#include "bench_common.h"
#include <signal.h>

/* Break worker loop if set to 1 */
odp_atomic_u32_t exit_thread;

static void sig_handler(int signo ODP_UNUSED)
{
	odp_atomic_store_u32(&exit_thread, 1);
}

int setup_sig_handler(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = sig_handler;

	/* No additional signals blocked. By default, the signal which triggered
	 * the handler is blocked.
	 */
	if (sigemptyset(&action.sa_mask))
		return -1;

	if (sigaction(SIGINT, &action, NULL))
		return -1;

	return 0;
}

/* Run given benchmark indefinitely */
static void run_indef(const bench_info_t *bench)
{
	const char *desc = bench->desc != NULL ? bench->desc : bench->name;

	printf("Running %s test indefinitely\n", desc);

	while (!odp_atomic_load_u32(&exit_thread)) {
		int ret;

		if (bench->init != NULL)
			bench->init();

		ret = bench->run();

		if (bench->term != NULL)
			bench->term();

		if (!ret)
			ODPH_ABORT("Benchmark %s failed\n", desc);
	}
}

int run_benchmarks(void *arg)
{
	int i, j;
	uint64_t c1 = 0, c2 = 0;
	odp_time_t t1 = ODP_TIME_NULL, t2 = ODP_TIME_NULL;
	run_bench_arg_t *args = arg;
	cmd_opt_t *opt = &args->opt;
	const int meas_time = opt->time;
	int ret = 0;

	/* Init EM */
	if (em_init_core() != EM_OK) {
		ODPH_ERR("EM core init failed\n");
		return -1;
	}

	printf("\nAverage %s per function call\n", meas_time ? "time (nsec)" : "CPU cycles");
	printf("------------------------------------------------------\n");

	/* Run each test twice. Results from the first warm-up round are ignored. */
	for (i = 0; i < 2; i++) {
		uint64_t total = 0;
		uint32_t round = 1;

		for (j = 0; j < args->num_bench && !odp_atomic_load_u32(&exit_thread); round++) {
			const char *desc;
			const bench_info_t *bench = &args->bench[j];
			uint32_t max_rounds = opt->rounds;

			if (bench->max_rounds && max_rounds > bench->max_rounds)
				max_rounds = bench->max_rounds;

			/* Run selected test indefinitely */
			if (opt->bench_idx) {
				if ((j + 1) != opt->bench_idx) {
					j++;
					continue;
				}

				run_indef(bench);
				goto exit;
			}

			desc = bench->desc != NULL ? bench->desc : bench->name;

			if (bench->init)
				bench->init();

			if (meas_time)
				t1 = odp_time_local();
			else
				c1 = odp_cpu_cycles();

			int bench_ret = bench->run();

			if (meas_time)
				t2 = odp_time_local();
			else
				c2 = odp_cpu_cycles();

			if (!bench_ret) {
				ODPH_ERR("Benchmark %s failed: %d\n", desc, bench_ret);
				args->bench_failed = -1;
				ret = -1;
				goto exit;
			}

			if (bench->term)
				bench->term();

			if (meas_time)
				total += odp_time_diff_ns(t2, t1);
			else
				total += odp_cpu_cycles_diff(c2, c1);

			if (round >= max_rounds) {
				/* Each benchmark runs internally REPEAT_COUNT times. */
				args->result[j] = ((double)total) / (max_rounds * REPEAT_COUNT);

				/* No print from warm-up round */
				if (i > 0) {
					if (bench->desc != NULL)
						printf("[%02d] %-50s: %12.2f\n", j + 1, desc,
						       args->result[j]);
					else
						printf("[%02d] em_%-47s: %12.2f\n", j + 1, desc,
						       args->result[j]);
				}

				j++;
				total = 0;
				round = 1;
			}
		}
	}

exit:
	if (em_term_core() != EM_OK)
		ODPH_ERR("EM core terminate failed\n");

	return ret;
}

void fill_time_str(char *time_str/*out*/)
{
	time_t t;
	struct tm tm;

	time(&t);
	tm = *localtime(&t);

	sprintf(time_str, "%d-%d-%d-%d:%d:%d", tm.tm_year + 1900, tm.tm_mon + 1,
		tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}
