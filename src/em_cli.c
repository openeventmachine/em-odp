/* Copyright (c) 2021, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include "em_include.h"

#if EM_CLI

#define OPTPARSE_IMPLEMENTATION
#include "misc/optparse.h"
#include <errno.h>

/* Maximum number of bytes (including terminating null byte) for an EM CLI command */
#define MAX_CMD_LEN 20

/* EM CLI shared memory */
static em_cli_shm_t *cli_shm;

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
			    "  -p, --cpu-arch\tPrint cpu architure\n"
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
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
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
			pool = (em_pool_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_pool(pool, NULL);
			break;
		case 'n':
			pool = pool_find(options.optarg);
			print_em_pool(pool, options.optarg);
			break;
		case 'h':
			print_em_pool_help();
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
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

/* Parse argument for subpools option -s or --subpools */
static int subpools_str_to_id(char *str, int *num_subpools/*out*/, int *subpools/*out*/)
{
	int i;
	long id;
	const char *token;
	char *saveptr;
	const char *delim = "[,]";

	/* Only one subpool is given */
	if (!strstr(str, "[")) {
		*num_subpools = 1;

		if (str_to_long(str, &id, 10))
			return -1;
		subpools[0] = (int)id;
		return 0;
	}

	token = strtok_r(str, delim, &saveptr);
	if (token == NULL) {
		odph_cli_log("Invalid option argument: %s\n", str);
		return -1;
	}
	if (str_to_long(token, &id, 10))
		return -1;
	subpools[0] = (int)id;

	for (i = 1; i < EM_MAX_SUBPOOLS; i++) {
		token = strtok_r(NULL, delim, &saveptr);
		if (token == NULL)
			break;

		if (str_to_long(token, &id, 10))
			return -1;
		subpools[i] = (int)id;
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
		odph_cli_log("Invalid pool id: %d\n", pool_id);
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
			    "Print EM pool statistics\n"
			    "\n"
			    "Options:\n"
			    "  -i, --id <pool id [:stats opt]>\tPrint statistics of <pool id>\n"
			    "  -n, --name <pool name [:stats opt]>\tPrint statistics of <pool name>\n"
			    "  -s, --subpools <pool:[subpool ids] [:stats opt]>\tPrint statistics of subpools\n"
			    "  -h, --help\tDisplay this help\n\n"
			    "Note stats opt is optional, when not given, it prints statistics from\n"
			    "em_pool_stats(), namely all statistic counters. When it is given,\n"
			    "this command prints selected counters from em_pool_stats_selected().\n"
			    "stats opt here uses one byte to select the counters to be read. One\n"
			    "bit in stats opt selects one counter. MSB represents 'available' and\n"
			    "LSB represents 'cache_free_ops'. For example, stats_opt=0x88 selects\n"
			    "the 'available' and 'total_ops' statistic counters.\n\n"
			    "Example:\n"
			    "  em_pool_stats -i 0x1\n"
			    "  em_pool_stats -i 0x1:0x88\n";

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
			odph_cli_log("Invalid pool id: %d\n", pool_id);
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
			print_pool_stats(options.optarg, true);
			break;
		case 'n':
			print_pool_stats(options.optarg, false);
			break;
		case 's':
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
}

static void print_em_queue_help(void)
{
	const char *usage = "Usage: em_queue_print [OPTION]\n"
			    "Print EM queue information\n"
			    "\n"
			    "Options:\n"
			    "  -c, --capa\tPrint queue capabilities\n"
			    "  -a, --all\tPrint info about all queues\n"
			    "  -h, --help\tDisplay this help\n";
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
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}
}

static void print_em_qgrp_help(void)
{
	const char *usage = "Usage: em_qgrp_print [OPTION]\n"
			    "Print EM queue group information\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all(default)\tPrint info about all EM queue groups\n"
			    "  -i, --id <qgrp id>\tPrint the queue info of <qgrp id>\n"
			    "  -n, --name <qgrp name> \tPrint the queue info of <qgrp name>\n"
			    "  -h, --help\tDisplay this help\n";
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
			qgrp = (em_queue_group_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_qgrp_queues(qgrp, NULL);
			break;
		case 'n':
			qgrp = em_queue_group_find(options.optarg);
			print_em_qgrp_queues(qgrp, options.optarg);
			break;
		case 'h':
			print_em_qgrp_help();
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}
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
	}
}

static void print_em_eo_help(void)
{
	const char *usage = "Usage: em_eo_print [OPTION]\n"
			    "Print EO information\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint all EO info\n"
			    "  -i, --id <eo id>\tPrint info about all queues of <eo id>\n"
			    "  -n, --name <eo name>\tPrint info about all queues of <eo name>\n"
			    "  -h, --help\tDisplay this help\n";

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
			eo = (em_eo_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_eo(eo, NULL);
			break;
		case 'n':
			eo = em_eo_find(options.optarg);
			print_em_eo(eo, options.optarg);
			break;
		case 'h':
			print_em_eo_help();
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}
}

static void print_em_agrp_help(void)
{
	const char *usage = "Usage: em_agrp_print [OPTION]\n"
			    "Print info about atomic groups\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all\tPrint info about all atomic groups\n"
			    "  -i, --id <ag id>\tPrint info about all queues of <ag id>\n"
			    "  -n, --name <ag name>\tPrint info about all queues of <ag name>\n"
			    "  -h, --help\tDisplay this help\n";

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
			ag = (em_atomic_group_t)(uintptr_t)(int)strtol(options.optarg, NULL, 0);
			print_em_agrp(ag, NULL);
			break;
		case 'n':
			ag = em_atomic_group_find(options.optarg);
			print_em_agrp(ag, options.optarg);
			break;
		case 'h':
			print_em_agrp_help();
			break;
		case '?':
			odph_cli_log("Error: %s\n", options.errmsg);
			return;
		default:
			odph_cli_log("Unknown Error\n");
			return;
		}
	}
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
	}
}

static int cli_register_em_commands(void)
{
	/* Register em commands */
	if (odph_cli_register_command("em_agrp_print", cmd_em_agrp_print,
				      "[a|i <ag id>|n <ag name>|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_agrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_eo_print", cmd_em_eo_print,
				      "[a|i <eo id>|n <eo name>|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_eo_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_egrp_print", cmd_em_egrp_print, "")) {
		EM_LOG(EM_LOG_ERR, "Registering EM cmd em_egrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_info_print", cmd_em_info_print,
				      "[a|p|c|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_info_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_pool_print", cmd_em_pool_print,
				      "[a|i <pool id>|n <pool name>|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_pool_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_pool_stats", cmd_em_pool_stats,
				      "[i<pool id>|n<pool name>|s<pool id:[subpool ids]>[:o]|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_pool_stats failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_queue_print", cmd_em_queue_print,
				      "[a|c|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_queue_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_qgrp_print", cmd_em_qgrp_print,
				      "[a|i <qgrp id>|n <qgrp name>|h]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_qgrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_core_print", cmd_em_core_print, "")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_core_print failed.\n");
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

#if ODP_VERSION_API_NUM(1, 33, 0) < ODP_VERSION_API
	odp_shm_capability_t shm_capa;
	int ret = odp_shm_capability(&shm_capa);

	if (ret) {
		EM_LOG(EM_LOG_ERR, "shm capability error:%d\n", ret);
		return -1;
	}

	/* No huge pages needed for the CLI shm */
	if (shm_capa.flags & ODP_SHM_NO_HP)
		flags |= ODP_SHM_NO_HP;
#endif
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
	init_ext_thread();

	/* Run CLI server. */
	if (odph_cli_run()) {
		EM_LOG(EM_LOG_ERR, "Failed to start CLI server.\n");
		exit(EXIT_FAILURE);
	}

	/* em_term_core_cli() */
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

	return EM_OK;
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
	if (odph_cli_stop()) {
		EM_LOG(EM_LOG_ERR, "Failed to stop CLI.\n");
		goto error;
	}

	if (odph_thread_join(&cli_shm->em_cli_thread, 1) != 1) {
		EM_LOG(EM_LOG_ERR, "Failed to join server thread.\n");
		goto error;
	}

	if (odph_cli_term()) {
		EM_LOG(EM_LOG_ERR, "Failed to terminate CLI.\n");
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
