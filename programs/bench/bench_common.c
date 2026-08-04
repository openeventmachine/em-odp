/* Copyright (c) 2023-2025, Nokia
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

/* Run one benchmark once */
static inline int run_bench(const bench_info_t *bench, const cmd_opt_t *opt,
			    uint64_t *result /*out*/)
{
	const char *desc = bench->desc != NULL ? bench->desc : bench->name;
	const int meas_time = opt->time;
	uint64_t c1 = 0, c2 = 0;
	odp_time_t t1 = ODP_TIME_NULL, t2 = ODP_TIME_NULL;

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
		return -1;
	}

	if (bench->term)
		bench->term();

	if (meas_time)
		*result = odp_time_diff_ns(t2, t1);
	else
		*result = odp_cpu_cycles_diff(c2, c1);

	return 0;
}

int run_benchmarks(void *arg)
{
	run_bench_arg_t *args = arg;
	const cmd_opt_t *opt = &args->opt;
	const int meas_time = opt->time;
	int ret = 0;

	/* Init EM worker */
	em_conf_local_t conf_local;

	em_conf_local_init(&conf_local);
	conf_local.core_type = EM_CORE_TYPE_WORKER;

	if (em_init_local(&conf_local) != EM_OK) {
		ODPH_ERR("EM local init failed\n");
		return -1;
	}

	printf("\nAverage %s per function call\n", meas_time ? "time (nsec)" : "CPU cycles");
	printf("------------------------------------------------------\n");

	uint64_t total = 0;
	uint32_t round = 0; /* round '0' is the warm-up round excluded from results */
	int i = 0;
	bool run_indef = false; /* run one given test case indefinitely */

	if (opt->bench_idx) {
		/* Run selected test indefinitely */
		i = opt->bench_idx - 1;
		run_indef = true;
	}

	while (i < args->num_bench && !odp_atomic_load_u32(&exit_thread)) {
		const bench_info_t *bench = &args->bench[i];
		const char *desc = bench->desc != NULL ? bench->desc : bench->name;
		uint32_t max_rounds = opt->rounds;
		uint64_t result = 0;

		if (bench->max_rounds && max_rounds > bench->max_rounds)
			max_rounds = bench->max_rounds;

		/* run one benchmark once */
		int err = run_bench(bench, opt, &result);

		if (err) {
			ODPH_ERR("Benchmark %s failed: %d\n", desc, err);
			args->bench_failed = -1;
			ret = -1;
			goto exit;
		}

		if (round > 0) /* don't include warm-up round (0) in the results */
			total += result;

		if (unlikely(round >= max_rounds)) {
			/* Each benchmark runs internally REPEAT_COUNT times. */
			args->result[i] = ((double)total) / (max_rounds * REPEAT_COUNT);

			if (bench->desc != NULL)
				printf("[%03d] %-50s: %12.2f\n", i + 1, desc, args->result[i]);
			else
				printf("[%03d] em_%-47s: %12.2f\n", i + 1, desc, args->result[i]);

			total = 0;
			round = 0; /* Start with warm up round (0) */

			if (!run_indef)
				i++; /* Move to next benchmark */
		} else {
			round++;
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
