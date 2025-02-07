/* Copyright (c) 2023, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include "bench_common.h"

#include <getopt.h>
#include <unistd.h>
#include<time.h>

/* User area size in bytes */
#define UAREA_SIZE 8

/* Default event size */
#define EVENT_SIZE 1024

/* Number of events in EM_POOL_DEFAULT */
#define NUM_EVENTS 1024

/* Maximum number of pool statistics to get */
#define MAX_POOL_STATS 1024u

/* Number of EM core count */
#define CORE_COUNT 2

typedef struct {
	/* Command line options and benchmark info */
	run_bench_arg_t run_bench_arg;

	/* Test case input / output data */
	int subpools[EM_MAX_SUBPOOLS];
	em_pool_stats_opt_t stats_opt;
	odp_pool_stats_opt_t stats_opt_odp;
	em_pool_info_t pool_info[MAX_POOL_STATS];
	em_pool_stats_t pool_stats[MAX_POOL_STATS];
	em_pool_subpool_stats_t subpool_stats[MAX_POOL_STATS];
	em_pool_stats_selected_t pool_stats_selected[MAX_POOL_STATS];
	em_pool_subpool_stats_selected_t subpool_stats_selected[MAX_POOL_STATS];

} gbl_args_t;

static gbl_args_t *gbl_args;

ODP_STATIC_ASSERT(REPEAT_COUNT <= MAX_POOL_STATS, "REPEAT_COUNT is bigger than MAX_POOL_STATS\n");

/**
 * Test functions
 */
static int pool_stats(void)
{
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_pool_stats(EM_POOL_DEFAULT, &gbl_args->pool_stats[i]);

	return i;
}

static void set_stats_opt(void)
{
	gbl_args->stats_opt.all = 0;
	gbl_args->stats_opt.available = 1;
	gbl_args->stats_opt.alloc_ops = 1;
	gbl_args->stats_opt.alloc_fails = 1;
	gbl_args->stats_opt.cache_alloc_ops = 1;
	gbl_args->stats_opt.cache_free_ops = 1;
	gbl_args->stats_opt.free_ops = 1;
	gbl_args->stats_opt.total_ops = 1;
	gbl_args->stats_opt.cache_available = 1;
}

/* Don't read statistics about cache_available */
static void set_stats_opt_no_cache_avail(void)
{
	gbl_args->stats_opt.all = 0;
	gbl_args->stats_opt.available = 1;
	gbl_args->stats_opt.alloc_ops = 1;
	gbl_args->stats_opt.alloc_fails = 1;
	gbl_args->stats_opt.cache_alloc_ops = 1;
	gbl_args->stats_opt.cache_free_ops = 1;
	gbl_args->stats_opt.free_ops = 1;
	gbl_args->stats_opt.total_ops = 1;
}

static int pool_stats_selected(void)
{
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_pool_stats_selected(EM_POOL_DEFAULT, &gbl_args->pool_stats_selected[i],
				       &gbl_args->stats_opt);

	return i;
}

static void set_subpools(void)
{
	gbl_args->subpools[0] = 0;
}

static int subpool_stats(void)
{
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_pool_subpool_stats(EM_POOL_DEFAULT, gbl_args->subpools, 1,
				      &gbl_args->subpool_stats[i]);

	return i;
}

static int subpool_stats_selected(void)
{
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_pool_subpool_stats_selected(EM_POOL_DEFAULT, gbl_args->subpools, 1,
					       &gbl_args->subpool_stats_selected[i],
					       &gbl_args->stats_opt);

	return i;
}

static int pool_info(void)
{
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_pool_info(EM_POOL_DEFAULT, &gbl_args->pool_info[i]);

	return i;
}

bench_info_t test_suite[] = {
	BENCH_INFO(pool_info, NULL, NULL, 0, "em_pool_info"),
	BENCH_INFO(pool_stats, NULL, NULL, 0, "em_pool_stats"),
	BENCH_INFO(subpool_stats, set_subpools, NULL, 0, "em_pool_subpool_stats"),
	BENCH_INFO(pool_stats_selected, set_stats_opt, NULL, 0, "em_pool_stats_selected"),
	BENCH_INFO(pool_stats_selected, set_stats_opt_no_cache_avail, NULL, 0,
		   "em_pool_stats_selected(no cache_availeble)"),
	BENCH_INFO(subpool_stats_selected, set_stats_opt, NULL, 0,
		   "em_pool_subpool_stats_selected"),
	BENCH_INFO(subpool_stats_selected, set_stats_opt_no_cache_avail, NULL, 0,
		   "em_pool_subpool_stats_selected(no cache_available)")
};

/* Print usage information */
static void usage(void)
{
	printf("\n"
	       "EM event API micro benchmarks\n"
	       "\n"
	       "Options:\n"
	       "  -t, --time <opt>        Time measurement.\n"
	       "                          0: measure CPU cycles (default)\n"
	       "                          1: measure time\n"
	       "  -i, --index <idx>       Benchmark index to run indefinitely.\n"
	       "  -r, --rounds <num>      Run each test case 'num' times (default %u).\n"
	       "  -w, --write-csv         Write result to csv files(used in CI) or not.\n"
	       "                          default: not write\n"
	       "  -h, --help              Display help and exit.\n\n"
	       "\n", ROUNDS);
}

/* Parse command line arguments */
static int parse_args(int argc, char *argv[], int num_bench, cmd_opt_t *cmd_opt/*out*/)
{
	int opt;
	int long_index;
	static const struct option longopts[] = {
		{"time", required_argument, NULL, 't'},
		{"index", required_argument, NULL, 'i'},
		{"rounds", required_argument, NULL, 'r'},
		{"write-csv", no_argument, NULL, 'w'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	static const char *shortopts =  "t:i:r:wh";

	cmd_opt->time = 0; /* Measure CPU cycles */
	cmd_opt->bench_idx = 0; /* Run all benchmarks */
	cmd_opt->rounds = ROUNDS;
	cmd_opt->write_csv = 0; /* Do not write result to csv files */

	while (1) {
		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;	/* No more options */

		switch (opt) {
		case 't':
			cmd_opt->time = atoi(optarg);
			break;
		case 'i':
			cmd_opt->bench_idx = atoi(optarg);
			break;
		case 'r':
			cmd_opt->rounds = atoi(optarg);
			break;
		case 'w':
			cmd_opt->write_csv = 1;
			break;
		case 'h':
			usage();
			return 1;
		default:
			ODPH_ERR("Bad option. Use -h for help.\n");
			return -1;
		}
	}

	if (cmd_opt->rounds < 1) {
		ODPH_ERR("Invalid test cycle repeat count: %u\n", cmd_opt->rounds);
		return -1;
	}

	if (cmd_opt->bench_idx < 0 || cmd_opt->bench_idx > num_bench) {
		ODPH_ERR("Bad bench index %i\n", cmd_opt->bench_idx);
		return -1;
	}

	optind = 1; /* Reset 'extern optind' from the getopt lib */

	return 0;
}

/* Print system and application info */
static void print_info(const char *cpumask_str, const cmd_opt_t *com_opt)
{
	odp_sys_info_print();

	printf("\n"
	       "bench_pool options\n"
	       "-------------------\n");

	printf("Worker CPU mask:   %s\n", cpumask_str);
	printf("Measurement unit:  %s\n", com_opt->time ? "nsec" : "CPU cycles");
	printf("Test rounds:       %u\n", com_opt->rounds);
	printf("\n");
}

static void init_default_pool_config(em_pool_cfg_t *pool_conf)
{
	em_pool_cfg_init(pool_conf);

	pool_conf->event_type = EM_EVENT_TYPE_SW;
	pool_conf->user_area.in_use = true;
	pool_conf->user_area.size = UAREA_SIZE;
	pool_conf->num_subpools = 1;
	pool_conf->subpool[0].size = EVENT_SIZE;
	pool_conf->subpool[0].num = NUM_EVENTS;
	pool_conf->subpool[0].cache_size = 0;
}

/* Allocate and free events to create more realistic statistics than a band new pool */
static void alloc_free_event(void)
{
	/* Alloc 10 extra events than the pool has to create some statistics about
	 * alloc_fails, so 10 EM ERROR prints about em_alloc() are expected.
	 */
	const int event_tbl_size = NUM_EVENTS + 10;
	em_event_t event_tbl[event_tbl_size];
	int i;

	for (i = 0; i < event_tbl_size; i++) {
		event_tbl[i] = EM_EVENT_UNDEF;
		event_tbl[i] = em_alloc(EVENT_SIZE, EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
	}

	/* Free all allocated events */
	for (i = 0; i < event_tbl_size; i++) {
		if (event_tbl[i] != EM_EVENT_UNDEF)
			em_free(event_tbl[i]);
	}
}

/* Write selected pool stats without cache_available counters to a different csv
 * file than the csv file for pool stats with cache_available. since selected
 * pool stats with and without cache_available are in different scale. Different
 * file means they will be plotted in different charts in our benchmark website.
 */
static void write_result_to_csv(void)
{
	FILE *file;
	char time_str[72] = {0};
	char bench4_desc[60] = {0};
	char bench5_desc[60] = {0};
	char bench6_desc[60] = {0};
	double *result = gbl_args->run_bench_arg.result;
	bench_info_t *bench = gbl_args->run_bench_arg.bench;

	fill_time_str(time_str);

	file = fopen("em_pool.csv", "w");
	if (file == NULL) {
		perror("Failed to open file em_pool.csv");
		return;
	}

	/* Remove substring from long desc so it can be fit in the website chart */
	strncpy(bench4_desc, bench[4].desc, 22); /*em_pool_stats_selected(no cache_availeble)*/
	strncpy(bench5_desc, bench[5].desc + 8, 22);/*em_pool_subpool_stats_selected*/
	/* em_pool_subpool_stats_selected(no cache_available) */
	strncpy(bench6_desc, bench[6].desc + 8, 22);

	fprintf(file, "Date,%s,%s,%s,%s,%s\n"
		"%s,%.2f,%.2f,%.2f,%.2f,%.2f\n",
		bench[0].desc, bench[1].desc, bench[2].desc, bench[3].desc, bench5_desc,
		time_str, result[0], result[1], result[2], result[3], result[5]);

	fclose(file);

	file = fopen("em_pool_no_cache_available.csv", "w");
	if (file == NULL) {
		perror("Failed to open file em_pool_no_cache_available.csv");
		return;
	}

	fprintf(file, "Date,%s,%s\n%s,%.2f,%.2f\n", bench4_desc, bench6_desc,
		time_str, result[4], result[6]);
	fclose(file);
}

int main(int argc, char *argv[])
{
	em_conf_t conf;
	cmd_opt_t cmd_opt;
	em_pool_cfg_t pool_conf;
	em_core_mask_t core_mask;
	odph_helper_options_t helper_options;
	odph_thread_t worker_thread;
	odph_thread_common_param_t thr_common;
	odph_thread_param_t thr_param;
	odp_shm_t shm;
	odp_cpumask_t cpumask, worker_mask;
	odp_instance_t instance;
	odp_init_t init_param;
	int worker_cpu;
	char cpumask_str[ODP_CPUMASK_STR_SIZE];
	int ret = 0;
	int num_bench = ARRAY_SIZE(test_suite);
	double result[ARRAY_SIZE(test_suite)] = {0};

	/* Let helper collect its own arguments (e.g. --odph_proc) */
	argc = odph_parse_options(argc, argv);
	if (odph_options(&helper_options)) {
		ODPH_ERR("Reading ODP helper options failed\n");
		exit(EXIT_FAILURE);
	}

	/* Parse and store the application arguments */
	ret = parse_args(argc, argv, num_bench, &cmd_opt);
	if (ret)
		exit(EXIT_FAILURE);

	odp_init_param_init(&init_param);
	init_param.mem_model = helper_options.mem_model;

	/* Init ODP before calling anything else */
	if (odp_init_global(&instance, &init_param, NULL)) {
		ODPH_ERR("Global init failed\n");
		exit(EXIT_FAILURE);
	}

	/* Init this thread */
	if (odp_init_local(instance, ODP_THREAD_CONTROL)) {
		ODPH_ERR("Local init failed\n");
		exit(EXIT_FAILURE);
	}

	odp_schedule_config(NULL);

	/* Get worker CPU */
	if (odp_cpumask_default_worker(&worker_mask, 1) != 1) {
		ODPH_ERR("Unable to allocate worker thread\n");
		goto odp_term;
	}
	worker_cpu = odp_cpumask_first(&worker_mask);
	(void)odp_cpumask_to_str(&worker_mask, cpumask_str, ODP_CPUMASK_STR_SIZE);

	print_info(cpumask_str, &cmd_opt);

	/* Init EM */
	em_core_mask_zero(&core_mask);
	em_core_mask_set(odp_cpu_id(), &core_mask);
	em_core_mask_set(worker_cpu, &core_mask);
	if (odp_cpumask_count(&core_mask.odp_cpumask) != CORE_COUNT)
		goto odp_term;

	init_default_pool_config(&pool_conf);

	em_conf_init(&conf);
	if (helper_options.mem_model == ODP_MEM_MODEL_PROCESS)
		conf.process_per_core = 1;
	else
		conf.thread_per_core = 1;
	conf.default_pool_cfg = pool_conf;
	conf.core_count = CORE_COUNT;
	conf.phys_mask = core_mask;

	if (em_init(&conf) != EM_OK) {
		ODPH_ERR("EM init failed\n");
		exit(EXIT_FAILURE);
	}

	if (em_init_core() != EM_OK) {
		ODPH_ERR("EM core init failed\n");
		exit(EXIT_FAILURE);
	}

	if (setup_sig_handler()) {
		ODPH_ERR("Signal handler setup failed\n");
		exit(EXIT_FAILURE);
	}

	/* Reserve memory for args from shared mem */
	shm = odp_shm_reserve("shm_args", sizeof(gbl_args_t), ODP_CACHE_LINE_SIZE, 0);
	if (shm == ODP_SHM_INVALID) {
		ODPH_ERR("Shared mem reserve failed\n");
		exit(EXIT_FAILURE);
	}

	gbl_args = odp_shm_addr(shm);
	if (gbl_args == NULL) {
		ODPH_ERR("Shared mem alloc failed\n");
		exit(EXIT_FAILURE);
	}

	odp_atomic_init_u32(&exit_thread, 0);

	memset(gbl_args, 0, sizeof(gbl_args_t));
	gbl_args->run_bench_arg.bench = test_suite;
	gbl_args->run_bench_arg.num_bench = num_bench;
	gbl_args->run_bench_arg.opt = cmd_opt;
	gbl_args->run_bench_arg.result = result;

	alloc_free_event();

	memset(&worker_thread, 0, sizeof(odph_thread_t));
	odp_cpumask_zero(&cpumask);
	odp_cpumask_set(&cpumask, worker_cpu);

	odph_thread_common_param_init(&thr_common);
	thr_common.instance = instance;
	thr_common.cpumask = &cpumask;
	thr_common.share_param = 1;

	odph_thread_param_init(&thr_param);
	thr_param.start = run_benchmarks;
	thr_param.arg = &gbl_args->run_bench_arg;
	thr_param.thr_type = ODP_THREAD_WORKER;

	odph_thread_create(&worker_thread, &thr_common, &thr_param, 1);

	odph_thread_join(&worker_thread, 1);

	ret = gbl_args->run_bench_arg.bench_failed;

	if (cmd_opt.write_csv)
		write_result_to_csv();

	if (em_term_core() != EM_OK)
		ODPH_ERR("EM core terminate failed\n");

	if (em_term(&conf) != EM_OK)
		ODPH_ERR("EM terminate failed\n");

	if (odp_shm_free(shm)) {
		ODPH_ERR("Shared mem free failed\n");
		exit(EXIT_FAILURE);
	}

odp_term:
	if (odp_term_local()) {
		ODPH_ERR("Local term failed\n");
		exit(EXIT_FAILURE);
	}

	if (odp_term_global(instance)) {
		ODPH_ERR("Global term failed\n");
		exit(EXIT_FAILURE);
	}

	if (ret < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
