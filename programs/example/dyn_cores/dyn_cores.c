/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2024-2026 Nokia
 */

/**
 * @file
 *
 * EM dynamic core tester.
 *
 * Simple interactive EM dynamic core tester. Can be used to verify the ability
 * to dynamically add and remove EM-cores within an EM-instance or between
 * different EM instances. Acts as a frontend and forks EM applications based on
 * configuration.
 *
 * Use the "--help" option for usage information.
 *
 * Based on the ODP test: odp/test/miscellaneous/odp_dyn_workers.c
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <odp_api.h>
#include <odp/helper/odph_api.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#define S_(x) #x
#define S(x) S_(x)
#define MAX_PROGS 8
#define CMD_DELIMITER ","
#define PROG_NAME "dyn_cores"
#define ADDITION 'a'
#define REMOVAL 'r'
#define DELAY 'd'
#define IDX_DELIMITER ":"
#define MAX_NIBBLE 15
#define MAX_WORKERS MAX_NIBBLE
#define MAX_PATTERN_LEN 32U
#define ENV_PREFIX "EM"
#define ENV_DELIMITER "="
#define UNKNOWN_CMD MAX_NIBBLE
#define EXIT_PROG (UNKNOWN_CMD - 1U)
#define DELAY_PROG (EXIT_PROG - 1U)

ODP_STATIC_ASSERT(MAX_WORKERS <= MAX_NIBBLE, "Too many workers");

typedef enum {
	PRS_OK,
	PRS_NOK,
	PRS_TERM
} parse_result_t;

enum {
	PARENT,
	CHILD
};

typedef enum {
	DOWN,
	UP
} state_t;

enum {
	CONN_ERR = -1,
	PEER_ERR,
	CMD_NOK,
	CMD_SUMMARY,
	CMD_OK
};

enum cmd {
	ADD = 0,
	REM = 1
};

static const char *const cmdstrs[] = {
	"ADD",
	"REM"
};

ODP_STATIC_ASSERT(ODPH_ARRAY_SIZE(cmdstrs) < DELAY_PROG, "Too many commands");

typedef struct {
	uint64_t core_id;
	uint64_t thread_id;
	uint64_t num_handled;
	uint64_t enq_errs;
	uint64_t runtime;
} summary_t;

typedef struct prog_t {
	summary_t summary;
	char *env;
	char *cpumask;
	pid_t pid;
	int socket;
	state_t state;
} prog_t;

typedef struct {
	uint64_t val1;
	uint8_t val2;
	uint8_t op;
} pattern_t;

typedef struct {
	pattern_t pattern[MAX_PATTERN_LEN];
	prog_t progs[MAX_PROGS];
	uint32_t num_p_elems;
	uint32_t num_progs;
	uint32_t max_cmd_len;
	odp_bool_t is_running;
} global_config_t;

typedef struct worker_config_s worker_config_t;

typedef struct worker_config_s {
	odph_thread_t thread;
	odp_barrier_t barrier;
	summary_t summary;
	struct {
		odp_ticketlock_t lock;
		em_queue_group_t qgrp;
		em_eo_t eo;
		em_queue_t queue;
	} locked;
	worker_config_t *configs;
	odp_atomic_u32_t is_running;
	uint8_t idx;
} worker_config_t;

typedef struct {
	worker_config_t worker_config[MAX_WORKERS];
	odp_instance_t instance;
	odp_cpumask_t cpumask;
	em_pool_t pool;
	summary_t *pending_summary;
	uint32_t num_workers;
	int socket;
} prog_config_t;

typedef struct {
	struct {
		uint16_t is_active;
		uint16_t cpu;
		uint32_t thread_id;
	} workers[MAX_WORKERS];
} result_t;

typedef odp_bool_t (*input_fn_t)(global_config_t *config, uint8_t *cmd, uint32_t *prog_idx,
				 uint32_t *worker_idx);
typedef odp_bool_t (*cmd_fn_t)(prog_config_t *config, uint8_t aux);

static global_config_t global_config;
static prog_config_t *prog_conf;

static void enq_to_next_queue(worker_config_t worker_configs[], int idx,
			      em_event_t event, summary_t *summary);

static void terminate(int signal ODP_UNUSED)
{
	global_config.is_running = false;
}

static odp_bool_t setup_signals(void)
{
	struct sigaction action = { .sa_handler = terminate };

	if (sigemptyset(&action.sa_mask) == -1 || sigaddset(&action.sa_mask, SIGINT) == -1 ||
	    sigaddset(&action.sa_mask, SIGTERM) == -1 ||
	    sigaddset(&action.sa_mask, SIGHUP) == -1 || sigaction(SIGINT, &action, NULL) == -1 ||
	    sigaction(SIGTERM, &action, NULL) == -1 || sigaction(SIGHUP, &action, NULL) == -1)
		return false;

	return true;
}

static void init_options(global_config_t *global_config)
{
	uint32_t max_len = 0U, str_len;

	memset(global_config, 0, sizeof(*global_config));

	for (uint32_t i = 0U; i < ODPH_ARRAY_SIZE(cmdstrs); ++i) {
		str_len = strlen(cmdstrs[i]);

		if (str_len > max_len)
			max_len = str_len;
	}

	global_config->max_cmd_len = max_len;
}

static void parse_masks(global_config_t *global_config, const char *optarg)
{
	char *tmp_str = strdup(optarg), *tmp;
	prog_t *prog;

	if (tmp_str == NULL)
		return;

	tmp = strtok(tmp_str, CMD_DELIMITER);

	while (tmp && global_config->num_progs < MAX_PROGS) {
		prog = &global_config->progs[global_config->num_progs];
		prog->cpumask = strdup(tmp);

		if (prog->cpumask != NULL)
			++global_config->num_progs;

		tmp = strtok(NULL, CMD_DELIMITER);
	}

	free(tmp_str);
}

static void parse_pattern(global_config_t *global_config, const char *optarg)
{
	char *tmp_str = strdup(optarg), *tmp, op;
	uint8_t num_elems = 0U;
	pattern_t *pattern;
	uint64_t val1;
	uint32_t val2;
	int ret;

	if (tmp_str == NULL)
		return;

	tmp = strtok(tmp_str, CMD_DELIMITER);

	while (tmp && num_elems < MAX_PATTERN_LEN) {
		pattern = &global_config->pattern[num_elems];
		/* Use invalid values to prevent correct values by chance. */
		val1 = -1;
		val2 = -1;
		ret = sscanf(tmp, "%c%" PRIu64 IDX_DELIMITER "%u", &op, &val1, &val2);

		if ((ret == 2 || ret == 3) && (op == ADDITION || op == REMOVAL || op == DELAY)) {
			pattern->val1 = val1;
			pattern->val2 = val2;
			pattern->op = op;
			++num_elems;
		}

		tmp = strtok(NULL, CMD_DELIMITER);
	}

	free(tmp_str);
	global_config->num_p_elems = num_elems;
}

static void print_usage(void)
{
	printf("\n"
	       "Simple interactive EM dynamic core tester. Can be used to verify ability of\n"
	       "an implementation to dynamically add and remove cores from one EM application\n"
	       "to another. Acts as a frontend and forks EM applications based on\n"
	       "configuration.\n"
	       "\n"
	       "Usage: " PROG_NAME " OPTIONS\n"
	       "\n"
	       "  E.g. EM0=MY_ENV=MY_VAL EM1=MY_ENV=MY_VAL " PROG_NAME " -c 0x80,0x80\n"
	       "       ...\n"
	       "       > %s 0 0\n"
	       "       > %s 0 0\n"
	       "       > %s 1 0\n"
	       "       > %s 1 0\n"
	       "       ...\n"
	       "       " PROG_NAME " -c 0x80,0x80 -p %c0%s0%s%c1000000000%s%c0%s0\n"
	       "\n"
	       "Mandatory OPTIONS:\n"
	       "\n"
	       "  -c, --cpumasks CPU masks for to-be-created EM processes, comma-separated, no\n"
	       "                 spaces. CPU mask format should be as expected by\n"
	       "                 'odp_cpumask_from_str()'. Parsed amount of CPU masks will be\n"
	       "                 the number of EM processes to be created. Theoretical maximum\n"
	       "                 number of CPU mask entries (and to-be-created EM processes) is\n"
	       "                 %u. Theoretical maximum number of workers per EM process is\n"
	       "                 %u. These might be further limited by the implementation.\n\n"
	       "                 A single environment variable can be passed to the processes.\n"
	       "                 The format should be: 'EM<x>=<name>=<value>', where <x> is\n"
	       "                 process index, starting from 0.\n"
	       "\n"
	       "Optional OPTIONS:\n"
	       "\n"
	       "  -p, --pattern  Non-interactive mode with a pattern of worker additions,\n"
	       "                 removals and delays, delimited by '%s', no spaces. Additions\n"
	       "                 are indicated with '%c' prefix, removals with '%c' prefix, both\n"
	       "                 followed by process index, starting from 0 and worker thread\n"
	       "                 index within given cpumask delimited by '%s', and delays with\n"
	       "                 '%c' prefix, followed by a delay in nanoseconds. Process\n"
	       "                 indexes are based on the parsed process count of '--cpumasks'\n"
	       "                 option. Additions and removals should be equal in the aggregate\n"
	       "                 and removals should never outnumber additions at any instant.\n"
	       "                 Maximum pattern length is %u.\n"
	       "  -h, --help     This help.\n"
	       "\n", cmdstrs[ADD], cmdstrs[REM], cmdstrs[ADD], cmdstrs[REM],
	       ADDITION, IDX_DELIMITER, CMD_DELIMITER, DELAY, CMD_DELIMITER,
	       REMOVAL, IDX_DELIMITER, MAX_PROGS, MAX_WORKERS, CMD_DELIMITER, ADDITION, REMOVAL,
	       IDX_DELIMITER, DELAY, MAX_PATTERN_LEN);
}

static parse_result_t check_options(const global_config_t *global_config)
{
	const pattern_t *pattern;
	int64_t num_tot = 0U;

	if (global_config->num_progs == 0U || global_config->num_progs > MAX_PROGS) {
		printf("Invalid number of CPU masks: %u\n", global_config->num_progs);
		return PRS_NOK;
	}

	for (uint32_t i = 0U; i < global_config->num_p_elems; ++i) {
		pattern = &global_config->pattern[i];

		if (pattern->op != DELAY) {
			if (pattern->val1 >= global_config->num_progs) {
				ODPH_ERR("Invalid pattern, invalid process index: %" PRIu64 "\n",
					 pattern->val1);
				return PRS_NOK;
			}

			if (pattern->val2 > MAX_WORKERS - 1) {
				ODPH_ERR("Invalid pattern, invalid worker index: %u\n",
					 pattern->val2);
				return PRS_NOK;
			}
		}

		if (pattern->op == ADDITION)
			++num_tot;
		else if (pattern->op == REMOVAL)
			--num_tot;

		if (num_tot < 0) {
			ODPH_ERR("Invalid pattern, removals exceed additions instantaneously\n");
			return PRS_NOK;
		}
	}

	if (num_tot > 0) {
		ODPH_ERR("Invalid pattern, more additions than removals\n");
		return PRS_NOK;
	}

	return PRS_OK;
}

static parse_result_t parse_options(int argc, char **argv, global_config_t *global_config)
{
	int opt, long_index;

	static const struct option longopts[] = {
		{ "cpumasks", required_argument, NULL, 'c' },
		{ "pattern", required_argument, NULL, 'p' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	static const char *shortopts = "c:p:h";

	init_options(global_config);

	while (1) {
		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;

		switch (opt) {
		case 'c':
			parse_masks(global_config, optarg);
			break;
		case 'p':
			parse_pattern(global_config, optarg);
			break;
		case 'h':
			print_usage();
			return PRS_TERM;
		case '?':
		default:
			print_usage();
			return PRS_NOK;
		}
	}

	return check_options(global_config);
}

static odp_bool_t setup_pkill(pid_t ppid)
{
	return prctl(PR_SET_PDEATHSIG, SIGKILL) != -1 && getppid() == ppid;
}

ODP_PRINTF_FORMAT(2, 3)
static int log_fn(odp_log_level_t level, const char *fmt, ...);

static int log_fn(odp_log_level_t level, const char *fmt, ...)
{
	int pri;
	va_list args;

	switch (level) {
	case ODP_LOG_DBG:
	case ODP_LOG_PRINT:
		pri = LOG_INFO;
		break;
	case ODP_LOG_WARN:
		pri = LOG_WARNING;
		break;
	case ODP_LOG_ERR:
	case ODP_LOG_UNIMPLEMENTED:
	case ODP_LOG_ABORT:
		pri = LOG_ERR;
		break;
	default:
		pri = LOG_INFO;
		break;
	}

	va_start(args, fmt);
	vsyslog(pri, fmt, args);
	va_end(args);

	/* Just return something that's not considered an error. */
	return 0;
}

__attribute__((format(printf, 2, 3)))
static int log_fn_em(em_log_level_t level, const char *fmt, ...);

static int log_fn_em(em_log_level_t level, const char *fmt, ...)
{
	int pri;
	va_list args;

	switch (level) {
	case EM_LOG_DBG:
		pri = LOG_DEBUG;
		break;
	case EM_LOG_PRINT:
		pri = LOG_INFO;
		break;
	case EM_LOG_ERR:
		pri = LOG_ERR;
		break;
	default:
		pri = LOG_INFO;
		break;
	}
	va_start(args, fmt);
	vsyslog(pri, fmt, args);
	va_end(args);

	/* Just return something that's not considered an error. */
	return 0;
}

static int vlog_fn_em(em_log_level_t level, const char *fmt, va_list args)
{
	int pri;

	switch (level) {
	case EM_LOG_DBG:
		pri = LOG_DEBUG;
		break;
	case EM_LOG_PRINT:
		pri = LOG_INFO;
		break;
	case EM_LOG_ERR:
		pri = LOG_ERR;
		break;
	default:
		pri = LOG_INFO;
		break;
	}
	vsyslog(pri, fmt, args);

	/* Just return something that's not considered an error. */
	return 0;
}

static odp_bool_t disable_stream(int fd, odp_bool_t read)
{
	const int null_fd = open("/dev/null", read ? O_RDONLY : O_WRONLY);
	odp_bool_t ret = false;

	if (null_fd == -1)
		return ret;

	ret = dup2(null_fd, fd) != -1;
	close(null_fd);

	return ret;
}

static odp_bool_t set_odp_env(char *env)
{
	char *tmp_str = strdup(env);
	odp_bool_t func_ret = false;

	if (tmp_str == NULL)
		return false;

	char *tmp = strtok(tmp_str, ENV_DELIMITER);

	if (tmp != NULL) {
		char *delim = strstr(env, ENV_DELIMITER);

		if (delim) {
			int ret = setenv(tmp, delim + 1U, 1);

			if (ret == -1)
				perror("setenv");
			func_ret = ret != -1;
		}
	}

	free(tmp_str);

	return func_ret;
}

static const char *core_type_str(em_core_type_t core_type)
{
	switch (core_type) {
	case EM_CORE_TYPE_WORKER:
		return "worker";
	case EM_CORE_TYPE_CONTROL:
		return "control";
	case EM_CORE_TYPE_EXTERNAL:
		return "external";
	default:
		return "undef";
	}
}

static em_status_t eo_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	(void)eo_ctx;
	(void)conf;

	char name[EM_EO_NAME_LEN];

	em_eo_name(eo, name, sizeof(name));
	em_core_type_t core_type = em_core_type();
	const char *type_str = core_type_str(core_type);

	log_fn(ODP_LOG_PRINT, "%s(EO:%" PRI_EO "-\"%s\"): EM-core=%d(%s) ODP-thread=%d ODP-cpu=%d\n",
	       __func__, eo, name, em_core_id(), type_str, odp_thread_id(), odp_cpu_id());

	return EM_OK;
}

static em_status_t eo_start_local(void *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;

	char name[EM_EO_NAME_LEN];

	em_eo_name(eo, name, sizeof(name));
	em_core_type_t core_type = em_core_type();
	const char *type_str = core_type_str(core_type);

	log_fn(ODP_LOG_PRINT, "%s(EO:%" PRI_EO "-\"%s\"): EM-core=%d(%s) ODP-thread=%d ODP-cpu=%d\n",
	       __func__, eo, name, em_core_id(), type_str, odp_thread_id(), odp_cpu_id());

	return EM_OK;
}

static em_status_t eo_stop_local(void *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;

	char name[EM_EO_NAME_LEN];

	em_eo_name(eo, name, sizeof(name));
	em_core_type_t core_type = em_core_type();
	const char *type_str = core_type_str(core_type);

	log_fn(ODP_LOG_PRINT, "%s(EO:%" PRI_EO "-\"%s\"): EM-core=%d(%s) ODP-thread=%d ODP-cpu=%d\n",
	       __func__, eo, name, em_core_id(), type_str, odp_thread_id(), odp_cpu_id());

	return EM_OK;
}

static em_status_t eo_stop(void *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;

	char name[EM_EO_NAME_LEN];

	em_eo_name(eo, name, sizeof(name));
	em_core_type_t core_type = em_core_type();
	const char *type_str = core_type_str(core_type);

	log_fn(ODP_LOG_PRINT, "%s(EO:%" PRI_EO "-\"%s\"): EM-core=%d(%s) ODP-thread=%d ODP-cpu=%d\n",
	       __func__, eo, name, em_core_id(), type_str, odp_thread_id(), odp_cpu_id());

	return EM_OK;
}

static void eo_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		       em_queue_t queue, void *q_ctx)
{
	(void)type;
	(void)queue;
	(void)q_ctx;

	worker_config_t *worker_config = eo_ctx;
	worker_config_t *configs = worker_config->configs;
	const uint8_t idx = worker_config->idx;
	summary_t *summary = &worker_config->summary;

	enq_to_next_queue(configs, idx, event, summary);
}

static odp_bool_t setup_prog_config(prog_config_t *prog_config, odp_instance_t odp_instance,
				    char *cpumask, int socket)
{
	worker_config_t *worker_config;

	memset(prog_config, 0, sizeof(*prog_config));

	prog_config->socket = socket;

	for (uint32_t i = 0U; i < MAX_WORKERS; ++i) {
		worker_config = &prog_config->worker_config[i];
		worker_config->thread.cpu = -1;
		odp_ticketlock_init(&worker_config->locked.lock);
		worker_config->locked.queue = EM_QUEUE_UNDEF;
		worker_config->locked.qgrp = EM_QUEUE_GROUP_UNDEF;
		odp_atomic_init_u32(&worker_config->is_running, 0U);
	}

	prog_config->instance = odp_instance;
	odp_cpumask_from_str(&prog_config->cpumask, cpumask);

	em_pool_cfg_t pool_cfg;
	em_pool_t pool = EM_POOL_UNDEF;

	em_pool_cfg_init(&pool_cfg);
	pool_cfg.event_type = EM_EVENT_TYPE_SW;
	pool_cfg.num_subpools = 1;
	pool_cfg.subpool[0].size = ODP_CACHE_LINE_SIZE;
	pool_cfg.subpool[0].num = 1;
	pool_cfg.subpool[0].cache_size = 0;

	pool = em_pool_create("dyn-cores-pool", EM_POOL_UNDEF, &pool_cfg);
	if (pool == EM_POOL_UNDEF) {
		log_fn(ODP_LOG_ERR, "Error creating EM event pool\n");
		return false;
	}

	prog_config->pool = pool;

	return true;
}

static inline void decode_cmd(uint8_t data, uint8_t *cmd, uint8_t *aux)
{
	/* Actual command will be in the high nibble and worker index in the low nibble. */
	*cmd = data >> 4U;
	*aux = data & 0xF;
}

static void build_result(const prog_config_t *prog_config, result_t *result)
{
	uint32_t num = 0U;
	const worker_config_t *worker_config;

	for (uint32_t i = 0U; i < MAX_WORKERS; ++i) {
		worker_config = &prog_config->worker_config[i];

		if (worker_config->thread.cpu != -1) {
			result->workers[num].is_active = 1;
			result->workers[num].thread_id = worker_config->summary.thread_id;
			result->workers[num].cpu = worker_config->thread.cpu;
			++num;
		}
	}
}

static void run_command(cmd_fn_t cmd_fn, uint8_t aux, prog_config_t *prog_config, int socket)
{
	const odp_bool_t is_ok = cmd_fn(prog_config, aux);
	const summary_t *summary = prog_config->pending_summary;
	uint8_t rep = !is_ok ? CMD_NOK : summary != NULL ? CMD_SUMMARY : CMD_OK;
	result_t result;

	(void)TEMP_FAILURE_RETRY(send(socket, &rep, sizeof(rep), MSG_NOSIGNAL));

	/* Same machine, no internet in-between, just send the structs as is. */
	if (rep == CMD_OK) {
		memset(&result, 0, sizeof(result));
		build_result(prog_config, &result);
		(void)TEMP_FAILURE_RETRY(send(socket, (const void *)&result, sizeof(result),
					      MSG_NOSIGNAL));
	} else if (rep == CMD_SUMMARY) {
		(void)TEMP_FAILURE_RETRY(send(socket, (const void *)summary, sizeof(*summary),
					      MSG_NOSIGNAL));
		prog_config->pending_summary = NULL;
	}
}

static odp_bool_t setup_worker_config(worker_config_t *worker_config /* in/out */)
{
	em_queue_group_t qgrp = EM_QUEUE_GROUP_UNDEF;
	em_eo_t eo = EM_EO_UNDEF;
	em_queue_t queue = EM_QUEUE_UNDEF;
	em_status_t status = EM_ERR;
	const uint8_t idx = worker_config->idx;
	em_core_mask_t zero_mask;

	size_t maxlen;

	maxlen = MAX(EM_QUEUE_GROUP_NAME_LEN, EM_QUEUE_NAME_LEN);
	maxlen = MAX(maxlen, EM_EO_NAME_LEN);

	char name[maxlen];

	em_core_mask_zero(&zero_mask);
	snprintf(name, maxlen, "dyn-core-qgrp%02u", idx);
	name[maxlen - 1] = '\0';

	qgrp = em_queue_group_create_sync(name, &zero_mask);
	if (odp_unlikely(qgrp == EM_QUEUE_GROUP_UNDEF)) {
		log_fn(ODP_LOG_ERR, "Error creating EM-queue-group\n");
		goto exit_error;
	}

	em_eo_param_t eo_param;

	em_eo_param_init(&eo_param);
	eo_param.start = eo_start;
	eo_param.local_start = eo_start_local;
	eo_param.local_start_mode = EM_EO_START_LOCAL_MODE_INIT_RUN;
	eo_param.stop = eo_stop;
	eo_param.local_stop = eo_stop_local;
	eo_param.local_stop_mode = EM_EO_STOP_LOCAL_MODE_TERM_RUN;
	eo_param.receive = eo_receive;
	eo_param.eo_ctx = worker_config;

	snprintf(name, maxlen, "dyn-core-eo%02u", idx);
	name[maxlen - 1] = '\0';

	eo = em_eo_create_param(name, &eo_param);
	if (odp_unlikely(eo == EM_EO_UNDEF)) {
		log_fn(ODP_LOG_ERR, "Error creating EM EO\n");
		goto exit_error;
	}

	em_status_t start_status = EM_ERR;

	status = em_eo_start_sync(eo, &start_status, NULL);
	if (odp_unlikely(status != EM_OK || start_status != EM_OK)) {
		log_fn(ODP_LOG_ERR, "em_eo_start_sync():%" PRIxSTAT " start-fn:%" PRIxSTAT "\n",
		       status, start_status);
		goto exit_error;
	}

	snprintf(name, maxlen, "dyn-core-queue%02u", idx);
	name[maxlen - 1] = '\0';

	queue = em_queue_create(name, EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	if (odp_unlikely(queue == EM_QUEUE_UNDEF)) {
		log_fn(ODP_LOG_ERR, "Error creating EM-queue\n");
		goto exit_error;
	}

	status = em_eo_add_queue_sync(eo, queue);
	if (odp_unlikely(status != EM_OK)) {
		log_fn(ODP_LOG_ERR, "Error adding queue to EO!\n");
		goto exit_error;
	}

	odp_ticketlock_lock(&worker_config->locked.lock);
	worker_config->locked.qgrp = qgrp;
	worker_config->locked.eo = eo;
	worker_config->locked.queue = queue;
	odp_ticketlock_unlock(&worker_config->locked.lock);

	return true;

exit_error:
	if (queue != EM_QUEUE_UNDEF)
		(void)em_queue_delete(queue);
	if (qgrp != EM_QUEUE_GROUP_UNDEF)
		(void)em_queue_group_delete(qgrp, 0, NULL);
	return false;
}

static inline int get_cpu(odp_cpumask_t *mask, int idx)
{
	int cpu = odp_cpumask_first(mask);

	while (idx--) {
		cpu = odp_cpumask_next(mask, cpu);

		if (cpu < 0)
			break;
	}

	return cpu;
}

static odp_bool_t signal_ready(int socket)
{
	uint8_t cmd = CMD_OK;
	ssize_t ret;

	ret = TEMP_FAILURE_RETRY(send(socket, &cmd, sizeof(cmd), MSG_NOSIGNAL));

	if (ret != 1) {
		log_fn(ODP_LOG_ERR, "Error signaling process readiness: %s\n", strerror(errno));
		return false;
	}

	return true;
}

static void enq_to_next_queue(worker_config_t worker_configs[], int idx,
			      em_event_t event, summary_t *summary)
{
	worker_config_t *next_worker_config;

	for (uint32_t i = 1U; i <= MAX_WORKERS; ++i) {
		next_worker_config = &worker_configs[(idx + i) % MAX_WORKERS];
		odp_ticketlock_lock(&next_worker_config->locked.lock);

		em_queue_t queue = next_worker_config->locked.queue;

		if (queue == EM_QUEUE_UNDEF) {
			odp_ticketlock_unlock(&next_worker_config->locked.lock);
			continue;
		}

		em_status_t status = em_send(event, queue);

		++summary->num_handled;
		if (status != EM_OK)
			++summary->enq_errs;

		odp_ticketlock_unlock(&next_worker_config->locked.lock);
		return;
	}

	em_free(event);
}

/**
 * Dispatch duration in nanoseconds for em_dispatch_duration() during program
 * execution to regularly return from dispatch and inspect the
 * 'is_running' flag value. Program termination will begin once an unset
 * 'is_running' has been noticed.
 */
#define EXIT_CHECK_DISPATCH_DURATION_NS 1000000000 /* 1s */

/**
 * Dispatch with options (via cmd line arguments), i.e. use em_dispatch_duration()
 */
static void run_core_dispatch_duration(const em_dispatch_duration_t *duration,
				       const em_dispatch_opt_t *opt,
				       worker_config_t *worker_config)
{
	const em_dispatch_duration_select_t select = duration->select;
	em_dispatch_duration_t exit_check_duration = *duration;
	em_dispatch_results_t results = {0};
	em_dispatch_results_t results_tot = {0};
	em_status_t status;

	int64_t rounds_left = 0;
	int64_t ns_left = 0;
	int64_t events_left = 0;
	int64_t noevents_rounds_left = 0;
	int64_t noevents_ns_left = 0;

	if (select & EM_DISPATCH_DURATION_ROUNDS)
		rounds_left = duration->rounds;
	if (select & EM_DISPATCH_DURATION_NS)
		ns_left = duration->ns;
	if (select & EM_DISPATCH_DURATION_EVENTS)
		events_left = duration->events;
	if (select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS)
		noevents_rounds_left = duration->no_events.rounds;
	if (select & EM_DISPATCH_DURATION_NO_EVENTS_NS)
		noevents_ns_left = duration->no_events.ns;

	/*
	 * Dispatch in chunks of 1s (to check the exit_flag)
	 */
	exit_check_duration.select |= EM_DISPATCH_DURATION_NS;
	exit_check_duration.ns = EXIT_CHECK_DISPATCH_DURATION_NS;
	if (select & EM_DISPATCH_DURATION_NS)
		exit_check_duration.ns = ODPH_MIN(exit_check_duration.ns, duration->ns);

	odp_time_t t1 = odp_time_local();

	do {
		status = em_dispatch_duration(&exit_check_duration, opt, &results);
		if (odp_unlikely(status != EM_OK))
			break;

		results_tot.rounds += results.rounds;
		results_tot.ns += results.ns;
		results_tot.events += results.events;

		if (select & EM_DISPATCH_DURATION_ROUNDS) {
			rounds_left -= results.rounds;
			if (odp_unlikely(rounds_left <= 0))
				break;
			exit_check_duration.rounds = rounds_left;
		}
		if (select & EM_DISPATCH_DURATION_NS) {
			ns_left -= results.ns;
			if (odp_unlikely(ns_left <= 0))
				break;
			if (ns_left < EXIT_CHECK_DISPATCH_DURATION_NS)
				exit_check_duration.ns = ns_left;
		}
		if (select & EM_DISPATCH_DURATION_EVENTS) {
			events_left -= results.events;
			if (odp_unlikely(events_left <= 0))
				break;
			exit_check_duration.events = events_left;
		}

		/*
		 * The 'no-events' updates to '.rounds' and '.ns' are
		 * approximations only since it is not known if 'no-events'
		 * could have started in the middle of the last dispatch.
		 * Can only check against events == 0 here.
		 */
		if (select & EM_DISPATCH_DURATION_NO_EVENTS_ROUNDS) {
			if (results.events == 0) {
				noevents_rounds_left -= results.rounds;
				if (odp_unlikely(noevents_rounds_left <= 0))
					break;
				exit_check_duration.no_events.rounds = noevents_rounds_left;
			}
		}
		if (select & EM_DISPATCH_DURATION_NO_EVENTS_NS) {
			if (results.events == 0) {
				noevents_ns_left -= results.ns;
				if (odp_unlikely(noevents_ns_left <= 0))
					break;
				exit_check_duration.no_events.ns = noevents_ns_left;
			}
		}
	} while (status == EM_OK && odp_atomic_load_u32(&worker_config->is_running));

	odp_time_t t2 = odp_time_local();
	uint64_t diff_ns = odp_time_diff_ns(t2, t1);
	double diff_sec = (double)diff_ns / 1.0e9;

	log_fn(ODP_LOG_PRINT,
	       "EM-core:%02d dispatched for %g s (%" PRIu64 " ns)\n"
	       "           total: rounds=%" PRIu64 " ns=%" PRIu64 " events=%" PRIu64 "\n",
	       em_core_id(), diff_sec, diff_ns,
	       results_tot.rounds, results_tot.ns, results_tot.events);
}

static int run_worker(void *args)
{
	odp_time_t tm;
	const int thread_id = odp_thread_id();
	worker_config_t *worker_config = args;
	summary_t *summary = &worker_config->summary;

	tm = odp_time_local_strict();

	odp_ticketlock_lock(&worker_config->locked.lock);

	em_queue_t queue = worker_config->locked.queue;
	em_queue_group_t qgrp = worker_config->locked.qgrp;

	if (odp_unlikely(queue == EM_QUEUE_UNDEF || qgrp == EM_QUEUE_GROUP_UNDEF)) {
		odp_ticketlock_unlock(&worker_config->locked.lock);
		return 0;
	}

	odp_ticketlock_unlock(&worker_config->locked.lock);

	/*
	 * Initialize this thread of execution (proc, thread), i.e. EM-core
	 */
	em_conf_local_t conf_local;

	em_conf_local_init(&conf_local);
	conf_local.core_type = EM_CORE_TYPE_WORKER;

	em_status_t stat = em_init_local(&conf_local);

	if (stat != EM_OK) {
		log_fn(ODP_LOG_ERR,
		       "em_init_local(EM_CORE_TYPE_WORKER):%" PRIxSTAT ", EM-core:%02d",
		       stat, em_core_id());
		abort();
	}

	int core = em_core_id();
	em_core_mask_t mask;

	/*  store EM-core and ODP thread id for later printout */
	summary->core_id = core;
	summary->thread_id = thread_id;

	em_core_mask_zero(&mask);
	em_core_mask_set(core, &mask);

	log_fn(ODP_LOG_PRINT,
	       "em_queue_group_modify(): qgrp:%" PRI_QGRP " EM-core:%02d",
	       qgrp, em_core_id());
	em_queue_group_modify_sync(qgrp, &mask);

	odp_barrier_wait(&worker_config->barrier);

	em_dispatch_duration_t duration;
	em_dispatch_opt_t opt;

	em_dispatch_opt_init(&opt);
	opt.skip_input_poll = true;
	opt.skip_output_drain = true;
	opt.sched_pause = false;

	duration.select = EM_DISPATCH_DURATION_FOREVER;

	run_core_dispatch_duration(&duration, &opt, worker_config);

	summary->runtime = odp_time_diff_ns(odp_time_local_strict(), tm);

	log_fn(ODP_LOG_PRINT,
	       "em_queue_group_modify(): qgrp:%" PRI_QGRP " EM-core:%02d",
	       qgrp, em_core_id());
	em_core_mask_zero(&mask);
	em_queue_group_modify_sync(qgrp, &mask);

	stat = em_term_core();
	if (stat != EM_OK) {
		log_fn(ODP_LOG_ERR, "em_term_core():%" PRIxSTAT ", EM-core:%02d",
		       stat, core);
		abort();
	}

	return 0;
}

static void shutdown_worker(worker_config_t *worker_config /* in/out */)
{
	em_queue_group_t qgrp;
	em_eo_t eo;
	em_queue_t queue;

	odp_ticketlock_lock(&worker_config->locked.lock);
	qgrp = worker_config->locked.qgrp;
	eo = worker_config->locked.eo;
	queue = worker_config->locked.queue;
	worker_config->locked.qgrp = EM_QUEUE_GROUP_UNDEF;
	worker_config->locked.eo = EM_EO_UNDEF;
	worker_config->locked.queue = EM_QUEUE_UNDEF;
	odp_ticketlock_unlock(&worker_config->locked.lock);

	odp_atomic_store_u32(&worker_config->is_running, 0U);
	(void)odph_thread_join(&worker_config->thread, 1);

	if (odp_unlikely(em_eo_remove_queue_sync(eo, queue) != EM_OK))
		log_fn(ODP_LOG_ERR, "em_eo_remove_queue_sync() failed");
	if (odp_unlikely(em_queue_delete(queue) != EM_OK))
		log_fn(ODP_LOG_ERR, "em_queue_delete() failed");
	if (odp_unlikely(em_eo_stop_sync(eo) != EM_OK))
		log_fn(ODP_LOG_ERR, "em_eo_stop_sync() failed");
	if (odp_unlikely(em_eo_delete(eo) != EM_OK))
		log_fn(ODP_LOG_ERR, "em_eo_delete() failed");
	if (odp_unlikely(em_queue_group_delete_sync(qgrp) != EM_OK))
		log_fn(ODP_LOG_ERR, "em_queue_group_delete_sync() failed");
}

static odp_bool_t bootstrap_scheduling(prog_config_t *prog_config, worker_config_t *worker_config)
{
	em_event_t event = em_alloc(1, EM_EVENT_TYPE_SW, prog_config->pool);

	if (event == EM_EVENT_UNDEF) {
		/* Event still in circulation, only 1 in pool */
		return true;
	}

	odp_ticketlock_lock(&worker_config->locked.lock);

	em_status_t status = em_send(event, worker_config->locked.queue);

	odp_ticketlock_unlock(&worker_config->locked.lock);

	if (status != EM_OK) {
		log_fn(ODP_LOG_ERR, "Error sending bootstrap event:%" PRIxSTAT "\n", status);
		em_free(event);
		shutdown_worker(worker_config);
		return false;
	}

	return true;
}

static odp_bool_t add_worker(prog_config_t *prog_config, uint8_t idx)
{
	worker_config_t *worker_config;
	odph_thread_common_param_t thr_common;
	int set_cpu;
	odp_cpumask_t cpumask;
	odph_thread_param_t thr_param;

	if (prog_config->num_workers == MAX_WORKERS) {
		log_fn(ODP_LOG_WARN, "Maximum number of workers already created\n");
		return false;
	}

	if (idx >= MAX_WORKERS) {
		log_fn(ODP_LOG_ERR, "Worker index out of bounds: %u\n", idx);
		return false;
	}

	worker_config = &prog_config->worker_config[idx];

	if (worker_config->thread.cpu != -1) {
		log_fn(ODP_LOG_WARN, "Worker already created: %u\n", idx);
		return false;
	}

	set_cpu = get_cpu(&prog_config->cpumask, idx);

	if (set_cpu < 0) {
		log_fn(ODP_LOG_ERR, "No CPU found for index: %u\n", idx);
		return false;
	}

	memset(&worker_config->summary, 0, sizeof(worker_config->summary));

	worker_config->configs = prog_config->worker_config;
	worker_config->idx = idx;

	if (!setup_worker_config(worker_config))
		return false;

	odph_thread_common_param_init(&thr_common);
	thr_common.instance = prog_config->instance;

	odp_cpumask_zero(&cpumask);
	odp_cpumask_set(&cpumask, set_cpu);
	thr_common.cpumask = &cpumask;
	odph_thread_param_init(&thr_param);
	thr_param.start = run_worker;
	thr_param.thr_type = ODP_THREAD_WORKER;
	thr_param.arg = worker_config;
	odp_atomic_store_u32(&worker_config->is_running, 1U);
	/* Control thread + worker thread = barrier count */
	odp_barrier_init(&worker_config->barrier, 2);

	if (odph_thread_create(&worker_config->thread, &thr_common, &thr_param, 1) != 1) {
		log_fn(ODP_LOG_ERR, "Error creating worker\n");

		odp_ticketlock_lock(&worker_config->locked.lock);

		em_queue_t queue = worker_config->locked.queue;
		em_queue_group_t qgrp = worker_config->locked.qgrp;

		worker_config->locked.queue = EM_QUEUE_UNDEF;
		worker_config->locked.qgrp = EM_QUEUE_GROUP_UNDEF;

		if (queue != EM_QUEUE_UNDEF)
			(void)em_queue_delete(queue);
		if (qgrp != EM_QUEUE_GROUP_UNDEF)
			(void)em_queue_group_delete_sync(qgrp);

		odp_ticketlock_unlock(&worker_config->locked.lock);
		return false;
	}

	odp_barrier_wait(&worker_config->barrier);
	++prog_config->num_workers;

	if (prog_config->num_workers == 1U &&
	    !bootstrap_scheduling(prog_config, worker_config))
		return false;

	return true;
}

static odp_bool_t remove_worker(prog_config_t *prog_config, uint8_t idx)
{
	worker_config_t *worker_config;

	if (prog_config->num_workers == 0U) {
		log_fn(ODP_LOG_WARN, "No more workers to remove\n");
		return false;
	}

	if (idx >= MAX_WORKERS) {
		log_fn(ODP_LOG_ERR, "Worker index out of bounds: %u\n", idx);
		return false;
	}

	worker_config = &prog_config->worker_config[idx];

	if (worker_config->thread.cpu == -1) {
		log_fn(ODP_LOG_WARN, "Worker already removed: %u\n", idx);
		return false;
	}

	shutdown_worker(worker_config);
	--prog_config->num_workers;
	worker_config->thread.cpu = -1;
	prog_config->pending_summary = &worker_config->summary;

	return true;
}

static odp_bool_t do_exit(prog_config_t *prog_config, uint8_t aux ODP_UNUSED)
{
	for (uint32_t i = 0U; i < MAX_WORKERS; ++i)
		remove_worker(prog_config, i);

	return true;
}

static void run_prog(prog_config_t *prog_config)
{
	odp_bool_t is_running = true;
	int socket = prog_config->socket;
	ssize_t ret;
	uint8_t data, cmd, aux;

	while (is_running) {
		ret = TEMP_FAILURE_RETRY(recv(socket, &data, sizeof(data), 0));

		if (ret != 1)
			continue;

		decode_cmd(data, &cmd, &aux);

		switch (cmd) {
		case ADD:
			run_command(add_worker, aux, prog_config, socket);
			break;
		case REM:
			run_command(remove_worker, aux, prog_config, socket);
			break;
		case EXIT_PROG:
			run_command(do_exit, aux, prog_config, socket);
			is_running = false;
			break;
		default:
			break;
		}
	}
}

static void teardown_prog(prog_config_t *prog_config)
{
	(void)em_pool_delete(prog_config->pool);
	prog_config->pool = EM_POOL_UNDEF;
}

static int init_em(em_conf_t *em_conf /*out*/, const char *cpumask)
{
	em_status_t stat;

	em_conf_init(em_conf);

	/* Set EM configuration based on parsed cmd line arguments */
	em_conf->device_id = 0;
	em_conf->thread_per_core = 1;
	em_conf->process_per_core = 0;

	/* em_conf->phys_mask: */
	int err = em_core_mask_set_str(cpumask, &em_conf->phys_mask);

	if (err) {
		log_fn(ODP_LOG_ERR,
		       "em_core_mask_set_str(%s):%d\n", cpumask, err);
		return -1;
	}

	em_conf->core_count = em_core_mask_count(&em_conf->phys_mask);
	if (em_conf->core_count == 0) {
		log_fn(ODP_LOG_ERR, "em_core_mask_count(%s):%d\n",
		       cpumask, em_conf->core_count);
		return -1;
	}

	/* Event-Timer: disable=0, enable=1 */
	em_conf->event_timer = 1;

	/*
	 * Set the default pool config in em_conf, needed internally by EM
	 * at startup. Note that if default pool configuration is provided
	 * in em-odp.conf at runtime through option 'startup_pools', this
	 * default pool config will be overridden and thus ignored.
	 */
	em_pool_cfg_t default_pool_cfg;

	em_pool_cfg_init(&default_pool_cfg); /* mandatory */
	default_pool_cfg.event_type = EM_EVENT_TYPE_SW;
	default_pool_cfg.align_offset.in_use = true; /* override config file */
	default_pool_cfg.align_offset.value = 0; /* set explicit '0 bytes' */
	default_pool_cfg.user_area.in_use = true; /* override config file */
	default_pool_cfg.user_area.size = 0; /* set explicit '0 bytes' */
	default_pool_cfg.num_subpools = 4;
	default_pool_cfg.subpool[0].size = 256;
	default_pool_cfg.subpool[0].num = 16384;
	default_pool_cfg.subpool[0].cache_size = 64;
	default_pool_cfg.subpool[1].size = 512;
	default_pool_cfg.subpool[1].num = 1024;
	default_pool_cfg.subpool[1].cache_size = 32;
	default_pool_cfg.subpool[2].size = 1024;
	default_pool_cfg.subpool[2].num =  1024;
	default_pool_cfg.subpool[2].cache_size = 16;
	default_pool_cfg.subpool[3].size = 2048;
	default_pool_cfg.subpool[3].num = 1024;
	default_pool_cfg.subpool[3].cache_size = 8;

	em_conf->default_pool_cfg = default_pool_cfg;

	/*
	 * User can override the EM default log functions by giving logging
	 * funcs of their own - here we just use the default (shown explicitly)
	 */
	em_conf->log.log_fn = log_fn_em;
	em_conf->log.vlog_fn = vlog_fn_em;

	/*
	 * Initialize the Event Machine.
	 */
	stat = em_init(em_conf);
	if (stat != EM_OK) {
		log_fn(ODP_LOG_ERR, "em_init() failed: %" PRIxSTAT "\n", stat);
		return -1;
	}

	em_conf_local_t conf_local;

	em_conf_local_init(&conf_local);
	conf_local.core_type = EM_CORE_TYPE_EXTERNAL;

	stat = em_init_local(&conf_local);
	if (stat != EM_OK) {
		log_fn(ODP_LOG_ERR,
		       "em_init_local(EM_CORE_TYPE_EXTERNAL) failed: %" PRIxSTAT "\n",
		       stat);
		return -1;
	}

	return 0;
}

static void run_odp(char *cpumask, int socket)
{
	odp_instance_t odp_instance;
	odp_init_t param;
	odp_shm_t shm_cfg = ODP_SHM_INVALID;

	odp_init_param_init(&param);
	param.log_fn = log_fn;

	if (odp_init_global(&odp_instance, &param, NULL)) {
		log_fn(ODP_LOG_ERR, "ODP global init failed\n");
		return;
	}

	if (odp_init_local(odp_instance, ODP_THREAD_CONTROL)) {
		log_fn(ODP_LOG_ERR, "ODP local init failed\n");
		return;
	}

	shm_cfg = odp_shm_reserve(NULL, sizeof(prog_config_t), ODP_CACHE_LINE_SIZE, 0U);

	if (shm_cfg == ODP_SHM_INVALID) {
		log_fn(ODP_LOG_ERR, "Error reserving shared memory\n");
		return;
	}

	prog_conf = odp_shm_addr(shm_cfg);

	if (prog_conf == NULL) {
		log_fn(ODP_LOG_ERR, "Error resolving shared memory address\n");
		return;
	}

	if (odp_schedule_config(NULL) < 0) {
		log_fn(ODP_LOG_ERR, "Error configuring scheduler\n");
		return;
	}

	em_conf_t em_conf;

	if (init_em(&em_conf, cpumask)) {
		log_fn(ODP_LOG_ERR, "EM initialization failed\n");
		return;
	}

	if (!setup_prog_config(prog_conf, odp_instance, cpumask, socket))
		return;

	if (!signal_ready(prog_conf->socket))
		return;

	run_prog(prog_conf);

	teardown_prog(prog_conf);

	(void)odp_shm_free(shm_cfg);

	em_status_t stat = EM_OK;
	int rc = 0;

	em_term_local_t term_local;

	em_term_local_init(&term_local);

	stat = em_term_local(&term_local);
	if (stat != EM_OK) {
		log_fn(ODP_LOG_ERR, "em_term_local() failed: %" PRIxSTAT "\n", stat);
		return;
	}

	stat = em_term();
	if (stat != EM_OK) {
		log_fn(ODP_LOG_ERR, "em_term() failed: %" PRIxSTAT "\n", stat);
		return;
	}

	rc = odp_term_local();
	if (rc) {
		log_fn(ODP_LOG_ERR, "odp_term_local() failed: %d\n", rc);
		return;
	}

	rc = odp_term_global(odp_instance);
	if (rc) {
		log_fn(ODP_LOG_ERR, "odp_term_global() failed: %d\n", rc);
		return;
	}
}

static odp_bool_t wait_process_ready(int socket)
{
	uint8_t data;
	ssize_t ret;

	ret = TEMP_FAILURE_RETRY(recv(socket, &data, sizeof(data), 0));

	if (ret <= 0) {
		if (ret < 0)
			perror("recv");

		return false;
	}

	return true;
}

static inline odp_bool_t is_interactive(const global_config_t *global_config)
{
	return global_config->num_p_elems == 0U;
}

static void print_cli_usage(void)
{
	printf("\nValid commands are:\n\n");

	for (uint32_t i = 0U; i < ODPH_ARRAY_SIZE(cmdstrs); ++i)
		printf("    %s <process index> <worker index>\n", cmdstrs[i]);

	printf("\n");
}

static char *get_format_str(uint32_t max_cmd_len)
{
	const int cmd_len = snprintf(NULL, 0U, "%u", max_cmd_len);
	uint32_t str_len;

	if (cmd_len <= 0)
		return NULL;

	str_len = strlen("%s %u %u") + cmd_len + 1U;

	char fmt[str_len];

	snprintf(fmt, str_len, "%%%ds %%u %%u", max_cmd_len);

	return strdup(fmt);
}

static uint8_t map_str_to_command(const char *cmdstr, uint32_t len)
{
	for (uint32_t i = 0U; i < ODPH_ARRAY_SIZE(cmdstrs); ++i)
		if (strncmp(cmdstr, cmdstrs[i], len) == 0)
			return i;

	return UNKNOWN_CMD;
}

static odp_bool_t get_stdin_command(global_config_t *global_config, uint8_t *cmd,
				    uint32_t *prog_idx, uint32_t *worker_idx)
{
	char *input, cmdstr[global_config->max_cmd_len + 1U], *fmt;
	size_t size;
	ssize_t ret;

	input = NULL;
	memset(cmdstr, 0, sizeof(cmdstr));
	printf("> ");
	ret = getline(&input, &size, stdin);

	if (ret == -1)
		return false;

	fmt = get_format_str(global_config->max_cmd_len);

	if (fmt == NULL) {
		printf("Unable to parse command\n");
		return false;
	}

	ret = sscanf(input, fmt, cmdstr, prog_idx, worker_idx);
	free(input);
	free(fmt);

	if (ret == EOF)
		return false;

	if (ret != 3) {
		printf("Unable to parse command\n");
		return false;
	}

	*cmd = map_str_to_command(cmdstr, global_config->max_cmd_len);
	return true;
}

static uint8_t map_char_to_command(char cmdchar)
{
	switch (cmdchar) {
	case ADDITION:
		return ADD;
	case REMOVAL:
		return REM;
	case DELAY:
		return DELAY_PROG;
	default:
		return UNKNOWN_CMD;
	}
}

static odp_bool_t get_pattern_command(global_config_t *global_config, uint8_t *cmd,
				      uint32_t *prog_idx, uint32_t *worker_idx)
{
	static uint32_t i;
	const pattern_t *pattern;
	struct timespec ts;

	if (i == global_config->num_p_elems) {
		global_config->is_running = false;
		return false;
	}

	pattern = &global_config->pattern[i++];
	*cmd = map_char_to_command(pattern->op);

	if (*cmd == DELAY_PROG) {
		ts.tv_sec  = pattern->val1 / ODP_TIME_SEC_IN_NS;
		ts.tv_nsec = pattern->val1 % ODP_TIME_SEC_IN_NS;
		nanosleep(&ts, NULL);
		return false;
	}

	*prog_idx = pattern->val1;
	*worker_idx = pattern->val2;

	return true;
}

static inline uint8_t encode_cmd(uint8_t cmd, uint8_t worker_idx)
{
	/* Actual command will be in the high nibble and worker index in the low nibble. */
	cmd <<= 4U;
	cmd |= worker_idx;

	return cmd;
}

static odp_bool_t is_peer_down(int error)
{
	return error == ECONNRESET || error == EPIPE || error == ETIMEDOUT;
}

static int send_command(int socket, uint8_t cmd)
{
	uint8_t data;
	ssize_t ret;
	odp_bool_t is_down;

	ret = TEMP_FAILURE_RETRY(send(socket, &cmd, sizeof(cmd), MSG_NOSIGNAL));

	if (ret != 1) {
		is_down = is_peer_down(errno);
		perror("send");
		return is_down ? PEER_ERR : CONN_ERR;
	}

	ret = TEMP_FAILURE_RETRY(recv(socket, &data, sizeof(data), 0));

	if (ret <= 0) {
		is_down = ret == 0 || is_peer_down(errno);

		if (ret < 0)
			perror("recv");

		return is_down ? PEER_ERR : CONN_ERR;
	}

	return data;
}

static odp_bool_t recv_summary(int socket, summary_t *summary)
{
	const ssize_t size = sizeof(*summary),
	ret = TEMP_FAILURE_RETRY(recv(socket, summary, size, 0));

	return ret == size;
}

static void dump_summary(pid_t pid, const summary_t *summary)
{
	printf("\nremoved worker summary:\n"
	       "    EM-core ID:     %" PRIu64 "\n"
	       "    ODP thread ID:  %" PRIu64 "\n"
	       "    process ID:     %d\n"
	       "    events handled: %" PRIu64 "\n"
	       "    enqueue errors: %" PRIu64 "\n"
	       "    runtime:        %" PRIu64 " (ns)\n"
	       "    event rate:     %.1f (events/s)\n\n",
	       summary->core_id, summary->thread_id, pid, summary->num_handled,
	       summary->enq_errs, summary->runtime,
	       (double)(summary->num_handled * ODP_TIME_SEC_IN_NS) / summary->runtime);
}

static odp_bool_t check_summary(const summary_t *summary)
{
	if (summary->num_handled == 0U) {
		printf("Summary check failure: no events handled\n");
		return false;
	}

	if (summary->enq_errs > 0U) {
		printf("Summary check failure: enqueue errors\n");
		return false;
	}

	if (summary->runtime == 0U) {
		printf("Summary check failure: no run time recorded\n");
		return false;
	}

	return true;
}

static void dump_result(int socket, pid_t pid)
{
	result_t result;
	const ssize_t size = sizeof(result),
	ret = TEMP_FAILURE_RETRY(recv(socket, &result, size, 0));

	if (ret != size)
		return;

	printf("\nODP process %d:\n"
	       "|\n", pid);

	for (uint32_t i = 0U; i < MAX_WORKERS; i++)
		if (result.workers[i].is_active)
			printf("|--- Worker thread ID %u on CPU %u\n",
			       result.workers[i].thread_id, result.workers[i].cpu);

	printf("\n");
}

static odp_bool_t run_global(global_config_t *global_config)
{
	input_fn_t input_fn;
	uint32_t prog_idx, worker_idx;
	uint8_t cmd;
	prog_t *prog;
	ssize_t ret;
	odp_bool_t is_recv, func_ret = true;

	print_cli_usage();
	input_fn = is_interactive(global_config) ? get_stdin_command : get_pattern_command;
	global_config->is_running = true;

	while (global_config->is_running) {
		if (!input_fn(global_config, &cmd, &prog_idx, &worker_idx))
			continue;

		if (cmd == UNKNOWN_CMD) {
			printf("Unrecognized command\n");
			continue;
		}

		if (prog_idx >= global_config->num_progs) {
			printf("Invalid process index: %u\n", prog_idx);
			continue;
		}

		prog = &global_config->progs[prog_idx];

		if (prog->state == DOWN) {
			printf("ODP process index %u has already exited\n", prog_idx);
			continue;
		}

		ret = send_command(prog->socket, encode_cmd(cmd, worker_idx));

		if (ret == CONN_ERR) {
			printf("Fatal connection error, aborting\n");
			abort();
		}

		if (ret == PEER_ERR) {
			printf("ODP process index %u has exited\n", prog_idx);
			prog->state = DOWN;
			continue;
		}

		if (ret == CMD_NOK) {
			printf("ODP process index %u was unable to execute the command\n",
			       prog_idx);
			continue;
		}

		if (ret == CMD_SUMMARY) {
			is_recv = recv_summary(prog->socket, &prog->summary);

			if (is_recv)
				dump_summary(prog->pid, &prog->summary);

			if (!is_interactive(global_config) &&
			    !(is_recv && check_summary(&prog->summary))) {
				global_config->is_running = false;
				func_ret = false;
			}

			continue;
		}

		if (ret == CMD_OK)
			dump_result(prog->socket, prog->pid);
	}

	for (uint32_t i = 0U; i < global_config->num_progs; ++i) {
		prog = &global_config->progs[i];

		if (prog->state == UP) {
			for (uint32_t j = 0U; j < MAX_WORKERS; ++j) {
				ret = send_command(prog->socket, encode_cmd(REM, j));

				if (ret == CONN_ERR || ret == PEER_ERR)
					break;

				if (ret != CMD_SUMMARY)
					continue;

				if (recv_summary(prog->socket, &prog->summary))
					dump_summary(prog->pid, &prog->summary);
			}

			(void)send_command(prog->socket, encode_cmd(EXIT_PROG, 0));
			(void)TEMP_FAILURE_RETRY(waitpid(prog->pid, NULL, 0));
		}
	}

	return func_ret;
}

static void teardown_global(const global_config_t *global_config)
{
	const prog_t *prog;

	for (uint32_t i = 0U; i < global_config->num_progs; ++i) {
		prog = &global_config->progs[i];
		if (prog->env)
			free(prog->env);
		close(prog->socket);
	}
}

int main(int argc, char **argv)
{
	parse_result_t res;
	int ret, func_ret = EXIT_SUCCESS;
	prog_t *prog;
	pid_t pid, ppid;
	const size_t envsize = sizeof(ENV_PREFIX S(UINT32_MAX)) + 1U;
	char *env, prog_env[envsize];

	if (!setup_signals()) {
		printf("Error setting up signals, exiting\n");
		return EXIT_FAILURE;
	}

	res = parse_options(argc, argv, &global_config);

	if (res == PRS_NOK)
		return EXIT_FAILURE;

	if (res == PRS_TERM)
		return EXIT_SUCCESS;

	printf("*** EM dynamic core tester ***\n\n");

	for (uint32_t i = 0U; i < global_config.num_progs; ++i) {
		int sockets[2U];

		ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);

		if (ret == -1) {
			perror("socketpair");
			return EXIT_FAILURE;
		}

		prog = &global_config.progs[i];
		snprintf(prog_env, envsize, "%s%" PRIu32 "", ENV_PREFIX, i);
		prog_env[envsize - 1] = '\0';
		env = getenv(prog_env);

		if (env != NULL)
			prog->env = strdup(env);

		prog->socket = sockets[PARENT];
		ppid = getpid();
		/*
		 * Flush stdio, stderr and other streams before a fork so the
		 * same buffered data isn't output by both parent and child.
		 */
		fflush(NULL);
		pid = fork();

		if (pid == -1) {
			perror("fork");
			return EXIT_FAILURE;
		}

		if (pid == 0) {
			/* Child */
			close(sockets[PARENT]);

			if (!setup_pkill(ppid)) {
				log_fn(ODP_LOG_ERR, "Error setting up pdeath signal, exiting\n");
				return EXIT_FAILURE;
			}

			if (!disable_stream(STDIN_FILENO, true) ||
			    !disable_stream(STDERR_FILENO, false) ||
			    !disable_stream(STDOUT_FILENO, false)) {
				log_fn(ODP_LOG_ERR, "Error disabling streams, exiting\n");
				return EXIT_FAILURE;
			}

			if (prog->env != NULL && !set_odp_env(prog->env)) {
				log_fn(ODP_LOG_ERR, "Error setting up environment, exiting\n");
				return EXIT_FAILURE;
			}

			run_odp(prog->cpumask, sockets[CHILD]);

			if (prog->env)
				free(prog->env);
			goto exit;
		} else {
			/* Parent */
			close(sockets[CHILD]);
			prog->pid = pid;

			if (!wait_process_ready(prog->socket)) {
				printf("Error launching process: %d, exiting\n", prog->pid);
				return EXIT_FAILURE;
			}

			prog->state = UP;
			printf("Created ODP process, pid: %d, CPU mask: %s, process index: %u\n",
			       prog->pid, prog->cpumask, i);
		}
	}

	func_ret = run_global(&global_config) ? EXIT_SUCCESS : EXIT_FAILURE;
	teardown_global(&global_config);

exit:
	return func_ret;
}
