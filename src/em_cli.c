/* Copyright (c) 2021-2026, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <odp_api.h>
#include <odp/helper/odph_api.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "em_atomic_group.h"
#include "em_chaining.h"
#include "em_cli.h"
#include "em_cli_types.h"
#include "em_core.h"
#include "em_error.h"
#include "em_event_group.h"
#include "em_eo.h"
#include "em_info.h"
#include "em_init.h"
#include "em_libconfig.h"
#include "em_mem.h"
#include "em_pool.h"
#include "em_queue.h"
#include "em_queue_group.h"
#include "em_timer.h"

#if EM_CLI

#include <errno.h>
#include <math.h>
#include <time.h>

#define OPTPARSE_IMPLEMENTATION
#include "misc/optparse.h"

static atomic_bool stop_top;
static atomic_bool in_top_thr;
static odph_thread_t cli_top_thread;

/* Maximum number of bytes (including terminating null byte) for an EM CLI command */
#define MAX_CMD_LEN 20

/* EM CLI shared memory */
static em_cli_shm_t *cli_shm;

static void sleep_ms(uint64_t ms);

__attribute__((format(printf, 2, 3)))
static int cli_log(em_log_level_t level, const char *fmt, ...)
{
	(void)level;

	va_list args;

	va_start(args, fmt);

	int r = odph_cli_log_va(fmt, args);

	va_end(args);

	return r;
}

static int cli_vlog(em_log_level_t level, const char *fmt, va_list args)
{
	(void)level;

	return odph_cli_log_va(fmt, args);
}

static void print_em_info_help(void)
{
	const char *usage = "Usage: em_info_print [OPTION]\n"
			    "Print EM related information.\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint all EM info\n"
			    "  -p, --cpu-arch\tPrint cpu architecture\n"
			    "  -c, --conf\tPrint default and runtime configurations\n"
			    "  -h, --help\tDisplay this help\n";
	odph_cli_log(usage);
}

static void print_em_info_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	print_em_info();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_info_cpu_arch(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	print_cpu_arch_info();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_info_conf(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	em_libconfig_print(&em_shm->libconfig);
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void cmd_em_info_print(int argc, char *argv[])
{
	/* All current options accept no argument */
	const int max_args = 1;

	/* When no argument is given, print all EM info */
	if (argc == 0) {
		print_em_info_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Command name*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_info_print";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	int option;
	struct optparse_long longopts[] = {
		{"all", 'a', OPTPARSE_NONE},
		{"cpu-arch", 'p', OPTPARSE_NONE},
		{"conf", 'c', OPTPARSE_NONE},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;
	while (1) {
		option = optparse_long(&options, longopts, NULL);

		if (option == -1)
			break;

		switch (option) {
		case 'a':
			print_em_info_all();
			break;
		case 'p':
			print_em_info_cpu_arch();
			break;
		case 'c':
			print_em_info_conf();
			break;
		case 'h':
			print_em_info_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_info_print does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_info_help();
	}
}

static void print_em_pool_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	em_pool_info_print_all();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_pool(em_pool_t pool, const char *pool_name)
{
	if (pool == EM_POOL_UNDEF) {
		if (pool_name)
			odph_cli_log("Error: can't find EM pool %s.\n", pool_name);
		else
			odph_cli_log("Error: can't find EM pool %" PRI_POOL "\n", pool);
		return;
	}

	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	pool_info_print_hdr(1);
	pool_info_print(pool);
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_pool_help(void)
{
	const char *usage = "Usage: em_pool_print [OPTION]\n"
			    "Print EM pool related information\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint info of all pools\n"
			    "  -i, --id <pool id>\tPrint info of <pool id>\n"
			    "  -n, --name <pool name>\tPrint info of <pool name>\n"
			    "  -h, --help\tDisplay this help\n";

	odph_cli_log(usage);
}

static void cmd_em_pool_print(int argc, char *argv[])
{
	/* Command em_pool_print takes maximum 2 arguments */
	const int max_args = 2;

	/* When no argument is given, print all pool info */
	if (argc == 0) {
		print_em_pool_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd str "em_pool_print"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_pool_print";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	em_pool_t pool;
	int option;
	struct optparse_long longopts[] = {
		{"all", 'a', OPTPARSE_NONE},
		{"id", 'i', OPTPARSE_REQUIRED},
		{"name", 'n', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;
	while (1) {
		option = optparse_long(&options, longopts, NULL);
		if (option == -1) /* No more options */
			break;

		switch (option) {
		case 'a':
			print_em_pool_all();
			break;
		case 'i':
			if (!options.optarg) {
				odph_cli_log("Error: pool ID is required!\n");
				return;
			}
			pool = (em_pool_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_pool(pool, NULL);
			break;
		case 'n':
			if (!options.optarg) {
				odph_cli_log("Error: pool name is required!\n");
				return;
			}
			pool = pool_find(options.optarg);
			print_em_pool(pool, options.optarg);
			break;
		case 'h':
			print_em_pool_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_pool_print does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_pool_help();
	}
}

static void print_em_pool_stats_opt_help(void)
{
	const char *usage = "Usage: em_pool_stats_opt [OPTION]\n"
			    "Print EM pool statistic counter options\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint statistic options of all pools\n"
			    "  -i, --id <pool id>\tPrint statistic options of <pool id>\n"
			    "  -n, --name <pool name>\tPrint statistic options of <pool name>\n"
			    "  -h, --help\tDisplay this help\n";

	odph_cli_log(usage);
}

static void print_em_pool_stats_opt_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	em_pool_stats_opt_print_all();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_pool_stats_opt(em_pool_t pool, const char *pool_name)
{
	if (pool == EM_POOL_UNDEF) {
		if (pool_name)
			odph_cli_log("Error: can't find EM pool %s.\n", pool_name);
		else
			odph_cli_log("Error: can't find EM pool %" PRI_POOL "\n", pool);
		return;
	}

	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	em_pool_stats_opt_print(pool);
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void cmd_em_pool_stats_opt(int argc, char *argv[])
{
	/* Command em_pool_stats_opt takes maximum 2 arguments */
	const int max_args = 2;

	/* When no argument is given, print statistic counter options for all pools */
	if (argc == 0) {
		print_em_pool_stats_opt_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd str "em_pool_stats_opt"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_pool_stats_opt";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	em_pool_t pool;
	int option;
	struct optparse_long longopts[] = {
		{"all", 'a', OPTPARSE_NONE},
		{"id", 'i', OPTPARSE_REQUIRED},
		{"name", 'n', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;
	while (1) {
		option = optparse_long(&options, longopts, NULL);
		if (option == -1) /* No more options */
			break;

		switch (option) {
		case 'a':
			print_em_pool_stats_opt_all();
			break;
		case 'i':
			if (!options.optarg) {
				odph_cli_log("Error: pool ID is required!\n");
				return;
			}
			pool = (em_pool_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_pool_stats_opt(pool, NULL);
			break;
		case 'n':
			if (!options.optarg) {
				odph_cli_log("Error: pool name is required!\n");
				return;
			}
			pool = pool_find(options.optarg);
			print_em_pool_stats_opt(pool, options.optarg);
			break;
		case 'h':
			print_em_pool_stats_opt_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_pool_stats_opt does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_pool_stats_opt_help();
	}
}

static void
print_em_pool_stats(em_pool_t pool, const char *pool_name, const em_pool_stats_opt_t *opt)
{
	if (pool == EM_POOL_UNDEF) {
		if (pool_name)
			odph_cli_log("Error: can't find EM pool %s.\n", pool_name);
		else
			odph_cli_log("Error: can't find EM pool %" PRI_POOL "\n", pool);
		return;
	}

	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);

	if (opt)
		pool_stats_selected_print(pool, opt);
	else
		pool_stats_print(pool);

	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static int str_to_long(const char *str, long *num/*out*/, int base)
{
	char *endptr;

	errno = 0;
	*num = strtol(str, &endptr, base);
	if (errno) {
		odph_cli_log("strtol failed\n");
		return -1;
	}

	if (endptr == str) {
		odph_cli_log("No digit is found in given str: %s\n", str);
		return -1;
	}

	if (*endptr != '\0')
		odph_cli_log("Extra characters not used in str: %s\n", endptr);

	return 0;
}

/* Parse string statistic counter options to options in type em_pool_stats_opt_t */
static void str_to_opt(const char *str_opt, em_pool_stats_opt_t * const opt)
{
	long stats_opt;

	/* Parse statistic counter options */
	if (str_to_long(str_opt, &stats_opt, 16))
		return;

	if (stats_opt & 0x80) {
		odph_cli_log("available is selected\n");
		opt->available = 1;
	}

	if (stats_opt & 0x40) {
		odph_cli_log("alloc_ops is selected\n");
		opt->alloc_ops = 1;
	}

	if (stats_opt & 0x20) {
		odph_cli_log("alloc_fails is selected\n");
		opt->alloc_fails = 1;
	}

	if (stats_opt & 0x10) {
		odph_cli_log("free_ops is selected\n");
		opt->free_ops = 1;
	}

	if (stats_opt & 0x08) {
		odph_cli_log("total_ops is selected\n");
		opt->total_ops = 1;
	}

	if (stats_opt & 0x04) {
		odph_cli_log("cache_available is selected\n");
		opt->cache_available = 1;
	}

	if (stats_opt & 0x02) {
		odph_cli_log("cache_alloc_ops is selected\n");
		opt->cache_alloc_ops = 1;
	}

	if (stats_opt & 0x01) {
		odph_cli_log("cache_free_ops is selected\n");
		opt->cache_free_ops = 1;
	}
}

/* Parse and validate a single subpool id. A subpool id is only valid within
 * the range [0, EM_MAX_SUBPOOLS); reject anything else here so that negative
 * or out-of-range ids are never passed on to the EM subpool stats APIs.
 */
static int str_to_subpool_id(const char *str, int *subpool_id/*out*/)
{
	long id;

	if (str_to_long(str, &id, 10))
		return -1;

	if (id < 0 || id >= EM_MAX_SUBPOOLS) {
		odph_cli_log("Invalid subpool id: %ld (valid range: 0...%d)\n",
			     id, EM_MAX_SUBPOOLS - 1);
		return -1;
	}

	*subpool_id = (int)id;
	return 0;
}

/* Parse argument for subpools option -s or --subpools */
static int subpools_str_to_id(char *str, int *num_subpools/*out*/, int *subpools/*out*/)
{
	int i;
	const char *token;
	char *saveptr;
	const char *delim = "[,]";

	/* Only one subpool is given */
	if (!strstr(str, "[")) {
		*num_subpools = 1;

		if (str_to_subpool_id(str, &subpools[0]))
			return -1;
		return 0;
	}

	token = strtok_r(str, delim, &saveptr);
	if (token == NULL) {
		odph_cli_log("Invalid option argument: %s\n", str);
		return -1;
	}
	if (str_to_subpool_id(token, &subpools[0]))
		return -1;

	for (i = 1; i < EM_MAX_SUBPOOLS; i++) {
		token = strtok_r(NULL, delim, &saveptr);
		if (token == NULL)
			break;

		if (str_to_subpool_id(token, &subpools[i]))
			return -1;
	}

	if (token/*Not break from loop*/ && strtok_r(NULL, delim, &saveptr)) {
		odph_cli_log("Too many subpools, maximum number is: %d\n", EM_MAX_SUBPOOLS);
		return -1;
	}

	*num_subpools = i;
	return 0;
}

static void
print_em_subpools_stats(em_pool_t pool, const int subpools[], int num_subpools,
			const em_pool_stats_opt_t *opt)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);

	if (opt)
		subpools_stats_selected_print(pool, subpools, num_subpools, opt);
	else
		subpools_stats_print(pool, subpools, num_subpools);

	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_subpools_stats(char *arg_subpools)
{
	long pool_id;
	char *saveptr;
	em_pool_t pool;
	int num_subpools;
	char *str_subpools;
	const char *str_stats_opt;
	const char *pool_str;
	int subpools[EM_MAX_SUBPOOLS];
	const char *delim = ":";
	em_pool_stats_opt_t opt = {0};

	pool_str = strtok_r(arg_subpools, delim, &saveptr);
	if (pool_str == NULL) {
		odph_cli_log("Invalid optarg: %s\n", arg_subpools);
		return;
	}

	if (str_to_long(pool_str, &pool_id, 16))
		return;

	/*pool_id = 0 --> EM_POOL_UNDEF*/
	if (!pool_id) {
		odph_cli_log("Invalid pool id: %ld\n", pool_id);
		return;
	}
	pool = (em_pool_t)(uintptr_t)pool_id;

	str_subpools = strtok_r(NULL, delim, &saveptr);
	if (str_subpools == NULL) {
		odph_cli_log("Invalid optarg: %s (subpool IDs are missing)\n", arg_subpools);
		return;
	}

	if (subpools_str_to_id(str_subpools, &num_subpools, subpools))
		return;

	str_stats_opt = strtok_r(NULL, delim, &saveptr);
	/* No stats opt, then print all statistic counters */
	if (str_stats_opt == NULL) {
		print_em_subpools_stats(pool, subpools, num_subpools, NULL);
	} else {
		str_to_opt(str_stats_opt, &opt);
		print_em_subpools_stats(pool, subpools, num_subpools, &opt);
	}
}

static void print_em_pool_stats_help(void)
{
	const char *usage = "Usage: em_pool_stats [OPTION]\n"
			    "\n"
			    "Description:\n"
			    "  Print EM pool statistics\n"
			    "\n"
			    "Options:\n"
			    "  -i, --id <pool id [:stats opt]>\tPrint statistics of <pool id>\n"
			    "  -n, --name <pool name [:stats opt]>\tPrint statistics of <pool name>\n"
			    "  -s, --subpools <pool:[subpool ids] [:stats opt]>\tPrint statistics of subpools\n"
			    "  -h, --help\tDisplay this help\n"
			    "\n"
			    "subpool ids are separated with ',', no space should be used\n"
			    "\n"
			    "'stats opt' is optional, when not given, it prints statistics from\n"
			    "em_pool_stats(), namely all statistic counters. When it is given,\n"
			    "this command prints selected counters from em_pool_stats_selected().\n"
			    "stats opt here uses one byte to select the counters to be read. One\n"
			    "bit in stats opt selects one counter. MSB represents 'available' and\n"
			    "LSB represents 'cache_free_ops'. For example, stats_opt=0x88 selects\n"
			    "the 'available' and 'total_ops' statistic counters.\n"
			    "\n"
			    "Example:\n"
			    "  em_pool_stats -i 0x1\n"
			    "  em_pool_stats -i 0x1:0x88\n"
			    "  em_pool_stats -n default:0x88\n"
			    "  em_pool_stats -s 0x1:[0,1,3]\n";

	odph_cli_log(usage);
}

static void print_pool_stats(char *optarg_str, bool is_id)
{
	long pool_id;
	char *saveptr;
	em_pool_t pool;
	const char *str_opt;
	const char *pool_str;
	const char *delim = ":";
	em_pool_stats_opt_t opt = {0};

	/* Parse string containing pool ID or pool name */
	pool_str = strtok_r(optarg_str, delim, &saveptr);
	if (pool_str == NULL) {
		odph_cli_log("Invalid optarg_str: %s\n", optarg_str);
		return;
	}

	if (is_id) {
		if (str_to_long(pool_str, &pool_id, 16))
			return;

		/*pool_id = 0 --> EM_POOL_UNDEF*/
		if (!pool_id) {
			odph_cli_log("Invalid pool id: %ld\n", pool_id);
			return;
		}
		pool = (em_pool_t)(uintptr_t)pool_id;
	} else {
		pool = pool_find(pool_str);
	}

	/* Parse string for statistic counter options */
	str_opt = strtok_r(NULL, delim, &saveptr);
	if (str_opt == NULL) {
		/* stats opt is missing, then print all statistic counters */
		print_em_pool_stats(pool, is_id ? NULL : pool_str, NULL);
	} else {
		str_to_opt(str_opt, &opt);
		print_em_pool_stats(pool, is_id ? NULL : pool_str, &opt);
	}
}

static void cmd_em_pool_stats(int argc, char *argv[])
{
	/* Command em_pool_stats takes maximum 2 arguments */
	const int max_args = 2;

	if (argc == 0) {
		odph_cli_log("Please specify pool or subpool ids!\n");
		print_em_pool_stats_help();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_pool_stats_help();
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd str "em_pool_stats"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_pool_stats";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	int option;
	struct optparse_long longopts[] = {
		{"id", 'i', OPTPARSE_REQUIRED},
		{"name", 'n', OPTPARSE_REQUIRED},
		{"subpools", 's', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;
	while (1) {
		option = optparse_long(&options, longopts, NULL);
		if (option == -1) /* No more options */
			break;

		switch (option) {
		case 'i':
			if (!options.optarg) {
				odph_cli_log("Error: pool ID is required!\n");
				return;
			}
			print_pool_stats(options.optarg, true);
			break;
		case 'n':
			if (!options.optarg) {
				odph_cli_log("Error: pool name is required!\n");
				return;
			}
			print_pool_stats(options.optarg, false);
			break;
		case 's':
			if (!options.optarg) {
				odph_cli_log("Error: subpool IDs are required!\n");
				return;
			}
			print_subpools_stats(options.optarg);
			break;
		case 'h':
			print_em_pool_stats_help();
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_pool_stats does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_pool_stats_help();
	}
}

static void print_em_queue_help(void)
{
	const char *usage = "Usage: em_queue_print [OPTION]\n"
			    "\n"
			    "Description:\n"
			    "  Print EM queue information\n"
			    "\n"
			    "Options:\n"
			    "  -c, --capa\tPrint queue capabilities\n"
			    "  -a, --all\tPrint info about all queues\n"
			    "  -h, --help\tDisplay this help\n"
			    "\n"
			    "Examples:\n"
			    "  em_queue_print -c\n";
	odph_cli_log(usage);
}

static void print_em_queue_capa(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	print_queue_capa();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_queue_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	print_queue_info();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void cmd_em_queue_print(int argc, char *argv[])
{
	/* All current options accept no argument */
	const int max_args = 1;

	/* When no argument is given, print info about all EM queues */
	if (argc == 0) {
		print_em_queue_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_queue_help();
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd str "em_queue_print"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_queue_print";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	int option;
	struct optparse_long longopts[] = {
		{"capa", 'c', OPTPARSE_NONE},
		{"all", 'a', OPTPARSE_NONE},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;
	while (1) {
		option = optparse_long(&options, longopts, NULL);
		if (option == -1) /* No more options */
			break;

		switch (option) {
		case 'c':
			print_em_queue_capa();
			break;
		case 'a':
			print_em_queue_all();
			break;
		case 'h':
			print_em_queue_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_queue_print does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_queue_help();
	}
}

static void print_em_qgrp_help(void)
{
	const char *usage = "Usage: em_qgrp_print [OPTION]\n"
			    "\n"
			    "Description:\n"
			    "  Print EM queue group information\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all(default)\tPrint info about all EM queue groups\n"
			    "  -i, --id <qgrp id>\tPrint the queue info of <qgrp id>\n"
			    "  -n, --name <qgrp name> \tPrint the queue info of <qgrp name>\n"
			    "  -h, --help\tDisplay this help\n"
			    "\n"
			    "Examples:\n"
			    "  em_qgrp_print -i 0x80\n"
			    "  em_qgrp_print --name default\n";
	odph_cli_log(usage);
}

static void print_em_qgrp_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	queue_group_info_print_all();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_qgrp_queues(const em_queue_group_t qgrp, const char *name)
{
	if (qgrp == EM_QUEUE_GROUP_UNDEF) {
		if (name)
			odph_cli_log("Error: can't find queue group %s!\n", name);
		else
			odph_cli_log("Error: can't find queue group %" PRI_QGRP "!\n", qgrp);
		return;
	}

	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	queue_group_queues_print(qgrp);
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void cmd_em_qgrp_print(int argc, char *argv[])
{
	/* em_qgrp_print takes maximum 2 arguments */
	const int max_args = 2;

	/* When no argument is given, print all EM queue group info */
	if (argc == 0) {
		print_em_qgrp_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_qgrp_help();
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd str "em_qgrp_print"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_qgrp_print";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	em_queue_group_t qgrp;
	int option;
	struct optparse_long longopts[] = {
		{"all", 'a', OPTPARSE_NONE},
		{"id", 'i', OPTPARSE_REQUIRED},
		{"name", 'n', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;
	while (1) {
		option = optparse_long(&options, longopts, NULL);

		if (option == -1)
			break; /* No more options */

		switch (option) {
		case 'a':
			print_em_qgrp_all();
			break;
		case 'i':
			if (!options.optarg) {
				odph_cli_log("Error: queue group ID is required!\n");
				return;
			}
			qgrp = (em_queue_group_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_qgrp_queues(qgrp, NULL);
			break;
		case 'n':
			if (!options.optarg) {
				odph_cli_log("Error: queue group name is required!\n");
				return;
			}
			qgrp = em_queue_group_find(options.optarg);
			print_em_qgrp_queues(qgrp, options.optarg);
			break;
		case 'h':
			print_em_qgrp_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_qgrp_help();
	}
}

static void print_em_core_help(void)
{
	const char *usage = "Usage: em_core_print\n"
			    "\n"
			    "Description:\n"
			    "  Print EM core info\n";

	odph_cli_log(usage);
}

static void cmd_em_core_print(int argc, char *argv[])
{
	(void)argv;
	/* Print EM core map */
	if (argc == 0) {
		core_log_fn_set(cli_log);
		core_vlog_fn_set(cli_vlog);
		print_core_map_info();
		core_log_fn_set(NULL);
		core_vlog_fn_set(NULL);
	} else {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_core_help();
	}
}

static void print_em_cfgfile_opts_help(void)
{
	const char *usage = "Usage: em_cfgfile_opts\n"
			    "\n"
			    "Description:\n"
			    "  Print EM config file options\n";

	odph_cli_log(usage);
}

static void cmd_em_cfgfile_opts(int argc, char *argv[])
{
	(void)argv;

	if (argc == 0) {
		core_log_fn_set(cli_log);
		core_vlog_fn_set(cli_vlog);
		cfgfile_opts_print();
		core_log_fn_set(NULL);
		core_vlog_fn_set(NULL);
	} else {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_cfgfile_opts_help();
	}
}

static void print_em_conf_opts_help(void)
{
	const char *usage = "Usage: em_conf_opts\n"
			    "\n"
			    "Description:\n"
			    "  Print EM runtime configuration options given to em_init()\n";

	odph_cli_log(usage);
}

static void cmd_em_conf_opts(int argc, char *argv[])
{
	(void)argv;

	if (argc == 0) {
		core_log_fn_set(cli_log);
		core_vlog_fn_set(cli_vlog);
		conf_opts_print();
		core_log_fn_set(NULL);
		core_vlog_fn_set(NULL);
	} else {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_conf_opts_help();
	}
}

static void print_em_eo_help(void)
{
	const char *usage = "Usage: em_eo_print [OPTION]\n"
			    "\n"
			    "Description:\n"
			    "  Print EO information\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint all EO info\n"
			    "  -i, --id <eo id>\tPrint info about all queues of <eo id>\n"
			    "  -n, --name <eo name>\tPrint info about all queues of <eo name>\n"
			    "  -h, --help\tDisplay this help\n"
			    "\n"
			    "Examples:\n"
			    "  em_eo_print --id 0x1\n"
			    "  em_eo_print -n 'Control EO'\n";

	odph_cli_log(usage);
}

static void print_em_eo_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	eo_info_print_all();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_eo(const em_eo_t eo, const char *name)
{
	if (eo == EM_EO_UNDEF) {
		if (name)
			odph_cli_log("Error: can't find EO %s\n", name);
		else
			odph_cli_log("Error: can't find EO %" PRI_EO "\n", eo);
		return;
	}

	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	eo_queue_info_print(eo);
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void cmd_em_eo_print(int argc, char *argv[])
{
	/* em_eo_print takes maximum 2 arguments */
	const int max_args = 2;

	/* When no argument is given, print all eo info */
	if (argc == 0) {
		print_em_eo_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_eo_help();
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd str "em_eo_print"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_eo_print";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	em_eo_t eo;
	int option;
	struct optparse_long longopts[] = {
		{"all", 'a', OPTPARSE_NONE},
		{"id", 'i', OPTPARSE_REQUIRED},
		{"name", 'n', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;

	while (1) {
		option = optparse_long(&options, longopts, NULL);
		if (option == -1) /* No more options */
			break;

		switch (option) {
		case 'a':
			print_em_eo_all();
			break;
		case 'i':
			if (!options.optarg) {
				odph_cli_log("Error: EO ID is required!\n");
				return;
			}
			eo = (em_eo_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_eo(eo, NULL);
			break;
		case 'n':
			if (!options.optarg) {
				odph_cli_log("Error: EO name is required!\n");
				return;
			}
			eo = em_eo_find(options.optarg);
			print_em_eo(eo, options.optarg);
			break;
		case 'h':
			print_em_eo_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_eo_print does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_eo_help();
	}
}

static void print_em_agrp_help(void)
{
	const char *usage = "Usage: em_agrp_print [OPTION]\n"
			    "\n"
			    "Description:\n"
			    "  Print info about atomic groups\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint info about all atomic groups\n"
			    "  -i, --id <ag id>\tPrint info about all queues of <ag id>\n"
			    "  -n, --name <ag name>\tPrint info about all queues of <ag name>\n"
			    "  -h, --help\tDisplay this help\n"
			    "\n"
			    "Examples:\n"
			    "  em_agrp_print\n"
			    "  em_agrp_print -n AG-A1\n";

	odph_cli_log(usage);
}

static void print_em_agrp_all(void)
{
	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	print_atomic_group_info();
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void print_em_agrp(em_atomic_group_t ag, const char *ag_name)
{
	if (ag == EM_ATOMIC_GROUP_UNDEF) {
		if (ag_name)
			odph_cli_log("Error: can't find atomic group %s\n", ag_name);
		else
			odph_cli_log("Error: can't find atomic group %" PRI_AGRP "\n", ag);
		return;
	}

	core_log_fn_set(cli_log);
	core_vlog_fn_set(cli_vlog);
	print_atomic_group_queues(ag);
	core_log_fn_set(NULL);
	core_vlog_fn_set(NULL);
}

static void cmd_em_agrp_print(int argc, char *argv[])
{
	/* em_agrp_print takes maximum 2 arguments */
	const int max_args = 2;

	/* When no argument is given, print info about all atomic groups */
	if (argc == 0) {
		print_em_agrp_all();
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_agrp_help();
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd name "em_agrp_print"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "em_agrp_print";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	em_atomic_group_t ag;
	int option;
	struct optparse_long longopts[] = {
		{"all", 'a', OPTPARSE_NONE},
		{"id", 'i', OPTPARSE_REQUIRED},
		{"name", 'n', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;

	while (1) {
		option = optparse_long(&options, longopts, NULL);

		if (option == -1)
			break;

		switch (option) {
		case 'a':
			print_em_agrp_all();
			break;
		case 'i':
			if (!options.optarg) {
				odph_cli_log("Error: atomic group ID is required!\n");
				return;
			}
			ag = (em_atomic_group_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_agrp(ag, NULL);
			break;
		case 'n':
			if (!options.optarg) {
				odph_cli_log("Error: atomic group name is required!\n");
				return;
			}
			ag = em_atomic_group_find(options.optarg);
			print_em_agrp(ag, options.optarg);
			break;
		case 'h':
			print_em_agrp_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command em_agrp_print does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("\033[1;31mError\033[0m: unexpected argument '%s'\n", arg);
		print_em_agrp_help();
	}
}

static void print_em_egrp_help(void)
{
	const char *usage = "Usage: em_egrp_print\n"
			    "\n"
			    "Description:\n"
			    "  Print info about event groups\n";

	odph_cli_log(usage);
}

static void cmd_em_egrp_print(int argc, char *argv[])
{
	(void)argv;
	/* When no argument is given, print info about all event groups */
	if (argc == 0) {
		core_log_fn_set(cli_log);
		core_vlog_fn_set(cli_vlog);
		event_group_info_print();
		core_log_fn_set(NULL);
		core_vlog_fn_set(NULL);
	} else {
		odph_cli_log("Error: extra parameter given to command!\n");
		print_em_egrp_help();
	}
}

static inline void erase_line(uint32_t line)
{
	odph_cli_log("\033[%u;1H", line);
	odph_cli_log("\033[2K");
}

static inline void
read_core_data(const int core, odp_time_t *to_idle /*out*/, odp_time_t *to_active /*out*/,
	       uint64_t *active_sum /*out*/, bool *while_idle /*out*/)
{
	uint32_t seq1 = 0;
	uint32_t seq2 = 0;
	em_cli_top_t *top = &cli_shm->top;

	do {
		seq1 = odp_atomic_load_acq_u32(&top->core_times[core].seq);
		if (seq1 & 1) /* Write in progress, retry */
			continue;

		/* Reading data do not happen before checking seq1 is even */
		*to_idle = top->core_times[core].to_idle;
		*to_active = top->core_times[core].to_active;
		*active_sum = top->core_times[core].active_sum;
		*while_idle = top->core_times[core].while_idle;

		 /* Read seq2 only after reading data is finished */
		seq2 = odp_atomic_load_acq_u32(&top->core_times[core].seq);
	} while (seq1 != seq2); /* Retry if sequence changed during read */
}

static inline double
calculate_top_percentage(uint64_t active_sum, odp_time_t to_active, odp_time_t t_print,
			 uint32_t round, int core)
{
	double per = 0.0;
	uint64_t interval_ns;
	uint64_t active_interval;
	em_cli_top_t *top = &cli_shm->top;
	uint64_t active_current = active_sum;
	uint64_t store_active_sum = active_sum;
	uint64_t prev_active_sum = top->core_times[core].prev_active_sum;

	if (odp_time_cmp(to_active, ODP_TIME_NULL) > 0) {
		store_active_sum += odp_time_diff_ns(t_print, to_active);

		if (odp_time_cmp(to_active, top->t_prev) > 0)
			active_current += odp_time_diff_ns(t_print, to_active);
		else
			per = 1.0;
	}

	top->core_times[core].prev_active_sum = store_active_sum;

	/* Return 1.0 if the core was active during the entire interval */
	if (per == 1.0)
		return per;

	if (round == 1) {
		/* For the first round, use total active time from start */
		active_interval = active_current;
		interval_ns = odp_time_diff_ns(t_print, top->t0);
	} else {
		/* Subsequent rounds: use difference from previous measurement */
		interval_ns = odp_time_diff_ns(t_print, top->t_prev);

		if (active_current > prev_active_sum)
			active_interval = active_current - prev_active_sum;
		else
			/* This might happen when in the previous round, immediately after
			 * top thread has read the core data (e.g. to_active), core goes to
			 * idle and the to_idle is obtained earlier than t_prev, resulting
			 * in prev_active_sum ( = s1 + s2 + s3 + s4) to be bigger than the
			 * active_current (= s1 + s2 + s3 + s5) when s5 is smaller than s4,
			 * as illustrated below.
			 *
			 * Core 0:  |███|______|███|_____|███|_______|███
			 *          │s1 │      │s2 │     │s3 │       |
			 *                     to_active     to_idle |
			 *                                   │       |
			 * Top thr:                          | s4 |  |s5|
			 *                                        t_prev t_print
			 *
			 * Above scenario is more likely to happen when the update rate
			 * is high, e.g. 1ms, resulting in a smaller time to accumulate
			 * active time to cancel the smaller difference between to_idle
			 * and t_prev, namely s4.
			 */
			active_interval = active_current - active_sum;
	}

	if (interval_ns)
		per = (double)active_interval / (double)interval_ns;

	return per;
}

static int top_thread_func(void *arg)
{
	double per;
	bool while_idle = false;
	odp_time_t to_idle = ODP_TIME_NULL;
	odp_time_t to_active = ODP_TIME_NULL;
	uint64_t active_sum = 0;
	uint32_t round = 0;
	int num_line_prev = 0;
	em_cli_top_t *top = &cli_shm->top;
	uint32_t sleep_time = (uint32_t)(uintptr_t)arg;

	in_top_thr = true;

	/* Clear the entire screen and move cursor to top-left */
	odph_cli_log("\033[2J\033[1;1H");
	fflush(stdout);

	odph_cli_log("Top started. Type 'stop' or 's' and Enter key to end it\n");
	odph_cli_log("Update every %.3fs (%u ms)\n\n", (float)sleep_time / 1000, sleep_time);

	/* Wait 500ms for the hooks to gather timing statistics to make the
	 * first round print more accurate.
	 */
	sleep_ms(500);

	while (!stop_top) {
		erase_line(4);
		odph_cli_log("Number of EM cores: %u   Round: %u\n", em_core_count(), round++);

		/* 5th line */
		odph_cli_log("EM-core    %%CPU\n");

		int num_line = 0;
		double per_sum = 0;
		uint32_t core_cnt = 0;
		odp_time_t t_print = ODP_TIME_NULL;
		int core = em_core_id_first(EM_CORE_TYPE_UNDEF, NULL);

		while (core >= 0) {
			core_cnt++;
			read_core_data(core, &to_idle /*out*/, &to_active /*out*/,
				       &active_sum /*out*/, &while_idle /*out*/);
			t_print = odp_time_global();

			if (!while_idle && !odp_time_cmp(to_idle, ODP_TIME_NULL) &&
			    !odp_time_cmp(to_active, ODP_TIME_NULL)) {
				per = 1;
				top->core_times[core].prev_active_sum =
					odp_time_diff_ns(t_print, top->t0);
			} else {
				per = calculate_top_percentage(active_sum,
							       to_active, t_print,
							       round, core);
			}

			while (core - num_line > 0) {
				erase_line(num_line + 6);
				odph_cli_log("-          -\n");
				num_line++;
			}

			/* Clear the line specific to the given core */
			erase_line(core + 6);

			/* Update statistics */
			odph_cli_log("%-11d%-.2f\n", core, per * 100);
			fflush(stdout);

			num_line++;
			per_sum += per;
			core = em_core_id_next();
		}

		/* When cores with higher id are removed, erase extra lines */
		if (num_line_prev > num_line) {
			for (int i = 0; i < num_line_prev - num_line; i++)
				erase_line(num_line + 6 + i);
			fflush(stdout);
		}

		/* Erase previous total and average lines printed only when round > 1 */
		if (round > 1) {
			erase_line(num_line_prev + 6 + 1);
			erase_line(num_line_prev + 6 + 2);
		}

		/* Move the cursor in the terminal to the given line */
		odph_cli_log("\033[%u;1H", num_line + 6 + 1);
		odph_cli_log("Total:     %-.2f(max %u)\n", per_sum * 100, core_cnt * 100);
		/* Move the cursor in the terminal to the given line */
		odph_cli_log("\033[%u;1H", num_line + 6 + 2);
		if (core_cnt)
			odph_cli_log("Average:   %-.2f\n", per_sum * 100 / core_cnt);
		else
			odph_cli_log("Average:   n/a\n");
		fflush(stdout);

		num_line_prev = num_line;
		/* Update previous time for next interval */
		top->t_prev = t_print;

		sleep_ms(sleep_time);
	}

	return 0;
}

static void to_idle_func(uint64_t delay_ns)
{
	(void)delay_ns;
	uint64_t diff;
	const int core = em_core_id();
	em_cli_top_t *top = &cli_shm->top;

	/* Mark write start by making sequence odd */
	odp_atomic_inc_u32(&top->core_times[core].seq);
	odp_mb_acquire();

	odp_time_t t_to_idle = odp_time_global();
	odp_time_t to_active = top->core_times[core].to_active;

	if (odp_time_cmp(to_active, ODP_TIME_NULL) > 0)
		diff = odp_time_diff_ns(t_to_idle, to_active);
	else
		diff = odp_time_diff_ns(t_to_idle, top->t0);

	top->core_times[core].to_active		= ODP_TIME_NULL;
	top->core_times[core].to_idle		= t_to_idle;
	top->core_times[core].active_sum	+= diff;

	/* Mark write complete by making sequence even again, release store
	 * makes sure all previous writes are visible to the reading threads
	 * after the acquire load of following release store.
	 */
	odp_atomic_add_rel_u32(&top->core_times[core].seq, 1);
}

static void while_idle_func(void)
{
	const int core = em_core_id();
	em_cli_top_t *top = &cli_shm->top;

	/* Mark write start by making sequence odd */
	odp_atomic_inc_u32(&top->core_times[core].seq);
	odp_mb_acquire();

	top->core_times[core].while_idle = true;

	/* Mark write complete by making sequence even again */
	odp_atomic_add_rel_u32(&top->core_times[core].seq, 1);
}

static void to_active_func(void)
{
	const int core = em_core_id();
	em_cli_top_t *top = &cli_shm->top;

	/* Mark write start by making sequence odd */
	odp_atomic_inc_u32(&top->core_times[core].seq);
	odp_mb_acquire();

	odp_time_t time_to_active = odp_time_global();

	top->core_times[core].to_active = time_to_active;
	top->core_times[core].while_idle = false;

	/* Mark write complete by making sequence even again */
	odp_atomic_add_rel_u32(&top->core_times[core].seq, 1);
}

/* Unregister idle callback hooks */
static int unregister_hooks(void)
{
	int ret = 0;
	em_status_t status;

	status = em_hooks_unregister_to_idle(to_idle_func);
	if (status != EM_OK) {
		odph_cli_log("Unregistering to_idle hook failed!\n");
		ret = -1;
	}

	status = em_hooks_unregister_while_idle(while_idle_func);
	if (status != EM_OK) {
		odph_cli_log("Unregistering while_idle hook failed!\n");
		ret = -1;
	}

	status = em_hooks_unregister_to_active(to_active_func);
	if (status != EM_OK) {
		odph_cli_log("Unregistering to_active hook failed!\n");
		ret = -1;
	}

	return ret;
}

static void create_top_thread(uint32_t sleep_time)
{
	em_status_t status;
	odp_cpumask_t cpumask;
	odp_instance_t instance;
	odph_thread_param_t thr_param;
	odph_thread_common_param_t thr_common;

	if (odp_cpumask_default_control(&cpumask, 1) != 1) {
		EM_LOG(EM_LOG_ERR, "Failed to get default CPU mask.\n");
		return;
	}

	if (odp_instance(&instance)) {
		EM_LOG(EM_LOG_ERR, "Failed to get odp instance.\n");
		return;
	}

	/* Set cli_shm->top.core_times to 0 */
	memset(&cli_shm->top.core_times, 0, sizeof(cli_shm->top.core_times));
	for (uint32_t i = 0; i < em_core_count(); i++)
		odp_atomic_init_u32(&cli_shm->top.core_times[i].seq, 0);

	cli_shm->top.t0 = odp_time_global();

	/* Register idle callback hooks */
	status = em_hooks_register_to_active(to_active_func);
	if (status != EM_OK) {
		odph_cli_log("Registering to_active hook failed!\n");
		return;
	}

	status = em_hooks_register_to_idle(to_idle_func);
	if (status != EM_OK) {
		em_hooks_unregister_to_active(to_active_func);
		odph_cli_log("Registering to_idle hook failed!\n");
		return;
	}

	status = em_hooks_register_while_idle(while_idle_func);
	if (status != EM_OK) {
		em_hooks_unregister_to_active(to_active_func);
		em_hooks_unregister_to_idle(to_idle_func);
		odph_cli_log("Registering while_idle hook failed!\n");
		return;
	}

	/* Create thread to run CLI top command */
	odph_thread_common_param_init(&thr_common);
	thr_common.instance = instance;
	thr_common.cpumask = &cpumask;
	thr_common.thread_model = 0; /* 0: Use pthread */

	odph_thread_param_init(&thr_param);
	thr_param.thr_type = ODP_THREAD_CONTROL;
	thr_param.start = top_thread_func;
	thr_param.arg = (void *)(uintptr_t)sleep_time;

	/* Create a thread for EM CLI top command to log CPU usages */
	if (odph_thread_create(&cli_top_thread, &thr_common, &thr_param, 1) != 1) {
		odph_cli_log("Failed to create CLI top server thread.\n");
		if (unregister_hooks()) {
			odph_cli_log("Unregistering idle hooks failed\n");
			abort();
		}
	} else {
		stop_top = false; /* Print for multiple top commands */
		odph_cli_log("Top started. Type 'stop' to end it.\n");
	}
}

static void print_em_top_help(void)
{
	const char *usage = "Usage: top [OPTIONS]\n"
			    "\n"
			    "Description:\n"
			    "  Show EM core usage\n"
			    "\n"
			    "Options:\n"
			    "  -t, --time <time>\tSets the update rate in seconds(default 1s)\n"
			    "  -h, --help\t\tDisplay this help\n"
			    "\n"
			    "Examples:\n"
			    "  top -t 0.5\n"
			    "  top --time 2\n";
	odph_cli_log(usage);
}

static void cmd_em_top(int argc, char *argv[])
{
	/* Update rate in ms, update statistics every 1 second by default */
	uint32_t sleep_time = 1000;

	/* top command takes maximum 2 arguments */
	const int max_args = 2;

	/* When no argument is given, use default sleep_time */
	if (argc == 0) {
		create_top_thread(sleep_time);
		return;
	} else if (argc > max_args) {
		odph_cli_log("Error: extra parameter given to command top!\n");
		print_em_top_help();
		return;
	}

	/* Unlike getopt, optparse does not require an argument count as input to
	 * indicate the number of arguments in argv. Instead, it uses NULL pointer
	 * to decide the end of argument array argv.
	 *
	 * argv here contains only CLI command options. To emulate a real command,
	 * argv_new is constructed to include command name.
	 */
	argc += 1/*Cmd name "top"*/ + 1/*Terminating NULL pointer*/;
	char *argv_new[argc];
	char cmd[MAX_CMD_LEN] = "top";

	argv_new[0] = cmd;
	for (int i = 1; i < argc - 1; i++)
		argv_new[i] = argv[i - 1];
	argv_new[argc - 1] = NULL; /*Terminating NULL pointer*/

	int option;
	struct optparse_long longopts[] = {
		{"time", 't', OPTPARSE_REQUIRED},
		{"help", 'h', OPTPARSE_NONE},
		{0}
	};
	struct optparse options;

	optparse_init(&options, argv_new);
	options.permute = 0;

	while (1) {
		option = optparse_long(&options, longopts, NULL);

		if (option == -1)
			break;

		switch (option) {
		case 't': {
			char *endptr = NULL;

			/* A REQUIRED option always sets optarg (a missing value yields '?');
			 * the guard makes that contract explicit for the strtof() call below.
			 */
			if (!options.optarg) {
				odph_cli_log("Error: update rate is required!\n");
				return;
			}

			errno = 0;
			float seconds = strtof(options.optarg, &endptr);

			/* Reject non-numeric input, trailing garbage, range
			 * errors and non-finite (NaN/Inf) values. A bad rate
			 * could otherwise give sleep_time == 0 (busy loop) or
			 * overflow the seconds-to-ms conversion below.
			 */
			if (endptr == options.optarg || *endptr != '\0' ||
			    errno != 0 || !isfinite(seconds) ||
			    seconds < 0.1f || seconds > 3600.0f) {
				odph_cli_log("Error: update rate must be a number within [0.1, 3600]s!\n");
				return;
			}
			sleep_time = (uint32_t)(seconds * 1000); /* Convert seconds to ms */
			create_top_thread(sleep_time);
			break;
		}
		case 'h':
			print_em_top_help();
			return;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}

	/* Command top does not accept non-option arguments */
	char *arg = optparse_arg(&options);

	if (arg) {
		odph_cli_log("Error: unexpected argument '%s'\n", arg);
		print_em_top_help();
	}
}

static void cmd_em_stop(int argc, char *argv[])
{
	(void)argv;
	(void)argc;

	/* Not in top thread */
	if (!in_top_thr) {
		odph_cli_log("stop must be used after top command\n");
		return;
	}

	stop_top = true;

	odph_thread_join_result_t join_res = {0};
	int ret = odph_thread_join_result(&cli_top_thread, &join_res, 1);

	if (ret != 1) {
		odph_cli_log("Failed to join CLI top thread:%d, join_res={is_sig=%d, ret=%d}\n",
			     ret, join_res.is_sig, join_res.ret);
	}

	in_top_thr = false;

	/* When unregistering fails, abort the program, otherwise the core
	 * statistics would go wrong since the core_times are still being
	 * set after top command has been stopped.
	 */
	if (unregister_hooks()) {
		odph_cli_log("Unregistering idle hooks failed\n");
		abort();
	}
}

static int cli_register_em_commands(void)
{
	/* Register em commands */
	if (odph_cli_register_command("em_agrp_print", cmd_em_agrp_print,
				      "[-a|-i <ag id>|-n <ag name>|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_agrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_eo_print", cmd_em_eo_print,
				      "[-a|-i <eo id>|-n <eo name>|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_eo_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_egrp_print", cmd_em_egrp_print, "")) {
		EM_LOG(EM_LOG_ERR, "Registering EM cmd em_egrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_info_print", cmd_em_info_print,
				      "[-a|-p|-c|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_info_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_pool_print", cmd_em_pool_print,
				      "[-a|-i <pool id>|-n <pool name>|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_pool_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_pool_stats_opt", cmd_em_pool_stats_opt,
				      "[-a|-i <pool id>|-n <pool name>|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_pool_stats_opt failed.\n");
		return -1;
	}

#define EM_POOL_STATS_HELP \
"[-i<pool id>[:o]|-n<pool name>[:o]|-s<pool id:[subpool ids]>[:o]|-h]"

	if (odph_cli_register_command("em_pool_stats", cmd_em_pool_stats,
				      EM_POOL_STATS_HELP)) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_pool_stats failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_queue_print", cmd_em_queue_print,
				      "[-a|-c|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_queue_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_qgrp_print", cmd_em_qgrp_print,
				      "[-a|-i <qgrp id>|-n <qgrp name>|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_qgrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_core_print", cmd_em_core_print, "")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_core_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_cfgfile_opts", cmd_em_cfgfile_opts, "")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_cfgfile_opts failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_conf_opts", cmd_em_conf_opts, "")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_conf_opts failed.\n");
		return -1;
	}

	if (odph_cli_register_command("top", cmd_em_top, "[-t <time(s)>|-h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command top failed.\n");
		return -1;
	}

	if (odph_cli_register_command("stop", cmd_em_stop,
				      "Stop the top printing, used after top")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command stop failed.\n");
		return -1;
	}

	return 0;
}

static int read_config_file(void)
{
	/* Conf option: cli.enable - runtime enable/disable cli */
	const char *cli_conf = "cli.enable";
	bool cli_enable = false;
	int ret = em_libconfig_lookup_bool(&em_shm->libconfig, cli_conf,
					   &cli_enable);

	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", cli_conf);
		return -1;
	}

	EM_PRINT("EM CLI config:\n");
	/* store & print the value */
	em_shm->opt.cli.enable = (int)cli_enable;
	EM_PRINT("  %s: %s(%d)\n", cli_conf, cli_enable ? "true" : "false",
		 cli_enable);

	cli_conf = "cli.ip_addr";
	ret = em_libconfig_lookup_string(&em_shm->libconfig, cli_conf,
					 &em_shm->opt.cli.ip_addr);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", cli_conf);
		return -1;
	}
	EM_PRINT("  %s: %s\n", cli_conf, em_shm->opt.cli.ip_addr);

	cli_conf = "cli.port";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, cli_conf,
				      &em_shm->opt.cli.port);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found\n", cli_conf);
		return -1;
	}
	EM_PRINT("  %s: %d\n", cli_conf, em_shm->opt.cli.port);

	return 0;
}

static int cli_shm_setup(void)
{
	if (cli_shm != NULL) {
		EM_LOG(EM_LOG_ERR, "EM CLI shared memory ptr already set!\n");
		return -1;
	}

	/*
	 * Reserve the CLI shared memory once at start-up.
	 */
	uint32_t flags = 0;
	odp_shm_capability_t shm_capa;
	int ret = odp_shm_capability(&shm_capa);

	if (ret) {
		EM_LOG(EM_LOG_ERR, "shm capability error:%d\n", ret);
		return -1;
	}

	/* No huge pages needed for the CLI shm */
	if (shm_capa.flags & ODP_SHM_NO_HP)
		flags |= ODP_SHM_NO_HP;

	odp_shm_t shm = odp_shm_reserve("em_cli", sizeof(em_cli_shm_t),
					ODP_CACHE_LINE_SIZE, flags);

	if (shm == ODP_SHM_INVALID) {
		EM_LOG(EM_LOG_ERR, "EM CLI shared memory reservation failed!\n");
		return -1;
	}

	cli_shm = odp_shm_addr(shm);

	if (cli_shm == NULL) {
		EM_LOG(EM_LOG_ERR, "EM CLI shared memory ptr NULL!\n");
		return -1;
	}

	memset(cli_shm, 0, sizeof(em_cli_shm_t));

	/* Store shm handle, can be used in stop_em_cli() to free the memory */
	cli_shm->this_shm = shm;

	return 0;
}

static int cli_shm_lookup(void)
{
	odp_shm_t shm;
	em_cli_shm_t *shm_addr;

	/* Lookup the EM shared memory on each EM-core */
	shm = odp_shm_lookup("em_cli");
	if (shm == ODP_SHM_INVALID) {
		EM_LOG(EM_LOG_ERR, "Shared memory lookup failed!\n");
		return -1;
	}

	shm_addr = odp_shm_addr(shm);
	if (!shm_addr) {
		EM_LOG(EM_LOG_ERR, "Shared memory ptr NULL\n");
		return -1;
	}

	if (em_shm->conf.process_per_core && cli_shm == NULL)
		cli_shm = shm_addr;

	if (shm_addr != cli_shm) {
		EM_LOG(EM_LOG_ERR, "CLI shared memory init fails: cli_shm:%p != shm_addr:%p\n",
		       cli_shm, shm_addr);
		return -1;
	}

	return 0;
}

static int cli_shm_free(void)
{
	if (odp_shm_free(cli_shm->this_shm)) {
		EM_LOG(EM_LOG_ERR, "Error: odp_shm_free() failed\n");
		return -1;
	}

	/* Set cli_shm = NULL to allow a new call to cli_shm_setup() */
	cli_shm = NULL;

	return 0;
}

static int cli_thr_fn(__attribute__((__unused__)) void *arg)
{
	em_status_t status = init_cli_thread();

	if (unlikely(status != EM_OK)) {
		EM_LOG(EM_LOG_ERR,
		       "Failed to init CLI server: init_cli_thread()=%" PRIxSTAT "\n",
		       status);
		exit(EXIT_FAILURE);
	}

	/* Run CLI server. */
	int ret = odph_cli_run();

	if (ret) {
		EM_LOG(EM_LOG_ERR,
		       "Failed to run CLI server:%d Maybe another instance is already running?\n\n",
		       ret);
		cli_shm->run_failed = true;
		return -1;
	}

	return 0;
}

/*
 * Run EM CLI server
 *
 * When executing this function, the CLI is accepting client connections and
 * running commands from a client, if one is connected.
 *
 * @return EM_OK if successful.
 */
static em_status_t run_em_cli(void)
{
	/* Prepare CLI parameters */
	odph_cli_param_t cli_param = {0};

	odph_cli_param_init(&cli_param);
	cli_param.hostname = "EM-ODP";
	cli_param.address = em_shm->opt.cli.ip_addr;
	cli_param.port = (uint16_t)em_shm->opt.cli.port;

	/* Initialize CLI helper */
	if (odph_cli_init(&cli_param)) {
		EM_LOG(EM_LOG_ERR, "Error: odph_cli_init() failed.\n");
		return EM_ERR_LIB_FAILED;
	}

	/* Register EM CLI commands */
	if (cli_register_em_commands()) {
		EM_LOG(EM_LOG_ERR, "Error: cli_register_em_commands() failed.\n");
		return EM_ERR_LIB_FAILED;
	}

	/* Create thread to run CLI server */
	odp_cpumask_t cpumask;
	odph_thread_common_param_t thr_common;
	odph_thread_param_t thr_param;
	odp_instance_t instance;

	if (odp_cpumask_default_control(&cpumask, 1) != 1) {
		EM_LOG(EM_LOG_ERR, "Failed to get default CPU mask.\n");
		return EM_ERR_LIB_FAILED;
	}

	if (odp_instance(&instance)) {
		EM_LOG(EM_LOG_ERR, "Failed to get odp instance.\n");
		return EM_ERR_LIB_FAILED;
	}

	odph_thread_common_param_init(&thr_common);
	thr_common.instance = instance;
	thr_common.cpumask = &cpumask;
	thr_common.thread_model = 0; /* 0: Use pthread for the CLI */

	odph_thread_param_init(&thr_param);
	thr_param.thr_type = ODP_THREAD_CONTROL;
	thr_param.start = cli_thr_fn;
	thr_param.arg = NULL;

	/* Set up EM CLI shared memory */
	if (cli_shm_setup()) {
		EM_LOG(EM_LOG_ERR, "Error: cli_shm_setup() failed.\n");
		return EM_ERR_ALLOC_FAILED;
	}

	EM_PRINT("Starting CLI server on %s:%d\n", cli_param.address, cli_param.port);

	/* Create EM CLI server thread and store the thread ID to be used in
	 * stop_em_cli() to wait for the thread to exit.
	 */
	if (odph_thread_create(&cli_shm->em_cli_thread, &thr_common,
			       &thr_param, 1) != 1) {
		EM_LOG(EM_LOG_ERR, "Failed to create CLI server thread.\n");
		cli_shm_free();
		return -1;
	}

	cli_shm->server_thr_created = true;

	return EM_OK;
}

static void sleep_ms(uint64_t ms)
{
	struct timespec req;
	struct timespec rem;

	req.tv_sec = ms / 1000;
	req.tv_nsec = (ms % 1000) * 1000000;

	while (nanosleep(&req, &rem) == -1 && errno == EINTR)
		req = rem; /* Resume sleep if interrupted */
}

/*
 * Stop EM CLI server
 *
 * Stop accepting new client connections and disconnect any connected client.
 *
 * @return EM_OK if successful.
 */
static em_status_t stop_em_cli(void)
{
	int err = 0;

	if (!cli_shm->run_failed) {
		const int max_try_cnt = 4;
		int try_cnt = 0;

		/*
		 * Wait for 10,20,30,40ms for the CLI-thread to have time to
		 * start up if the EM runtime was very short, i.e. can't call
		 * odph_cli_stop() before odph_cli_run() has had time to set up.
		 */
		do {
			if (try_cnt > 0) {
				EM_LOG(EM_LOG_ERR, "Failed to stop CLI:%d - Retrying (%d)\n",
				       err, try_cnt);
				sleep_ms(10 * try_cnt);
			}
			err = odph_cli_stop();
			try_cnt++;
		} while (err && try_cnt < max_try_cnt);

		if (err) {
			EM_LOG(EM_LOG_ERR, "Failed to stop CLI:%d Give up with ERROR!\n",
			       err);
			goto error;
		}
	}

	if (!err && cli_shm->server_thr_created) {
		odph_thread_join_result_t join_res = {0};
		int ret = odph_thread_join_result(&cli_shm->em_cli_thread, &join_res, 1);

		if (ret != 1) {
			EM_LOG(EM_LOG_ERR,
			       "Failed to join CLI server thread:%d, join_res={is_sig=%d, ret=%d}\n",
			       ret, join_res.is_sig, join_res.ret);
			goto error;
		}
	}

	err = odph_cli_term();
	if (err) {
		EM_LOG(EM_LOG_ERR, "Failed to terminate CLI:%d\n", err);
		goto error;
	}

	cli_shm_free();
	EM_PRINT("\nCLI server terminated!\n");

	return EM_OK;

error:
	cli_shm_free();
	return EM_ERR_LIB_FAILED;
}

em_status_t emcli_init(void)
{
	em_status_t stat = EM_OK;

	/* Store libconf options to em_shm */
	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	if (em_shm->opt.cli.enable) {
		stat = run_em_cli();

		if (stat != EM_OK) {
			EM_LOG(EM_LOG_ERR, "%s(): run_em_cli() failed:%" PRI_STAT "\n",
			       __func__, stat);
		}
	}

	return stat;
}

em_status_t emcli_init_local(void)
{
	if (!em_shm->opt.cli.enable)
		return EM_OK;

	int ret = cli_shm_lookup();

	if (ret)
		return EM_ERR_LIB_FAILED;

	return EM_OK;
}

em_status_t emcli_term(void)
{
	em_status_t stat = EM_OK;

	if (em_shm->opt.cli.enable) {
		stat = stop_em_cli();

		if (stat != EM_OK) {
			EM_LOG(EM_LOG_ERR, "%s(): stop_em_cli() failed:%" PRI_STAT "\n",
			       __func__, stat);
		}
	}

	return stat;
}

em_status_t emcli_term_local(void)
{
	return EM_OK;
}

#else /* EM_CLI */
/* Dummy functions for building without odph_cli and libcli support */
em_status_t emcli_init(void)
{
	return EM_OK;
}

em_status_t emcli_init_local(void)
{
	return EM_OK;
}

em_status_t emcli_term(void)
{
	return EM_OK;
}

em_status_t emcli_term_local(void)
{
	return EM_OK;
}

#endif /* EM_CLI */
