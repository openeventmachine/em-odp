/* Copyright (c) 2021, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include "em_include.h"

#if EM_CLI

/** EM CLI shared memory */
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

static void print_em_info_help(void)
{
	const char *usage = "\n"
			    "Usage: em_info print [name]\n"
			    "\n"
			    "Names available:\n"
			    "  all\tPrint all info\n"
			    "  cpu_arch\tPrint cpu architure\n"
			    "  conf\tPrint default and runtime configurations\n"
			    "  help\tShow available names\n";
	odph_cli_log(usage);
}

static void print_em_info_all(void)
{
	core_log_fn_set(cli_log);
	print_em_info();
	core_log_fn_set(NULL);
}

static void print_em_info_cpu_arch(void)
{
	core_log_fn_set(cli_log);
	print_cpu_arch_info();
	core_log_fn_set(NULL);
}

static void print_em_info_conf(void)
{
	core_log_fn_set(cli_log);
	em_libconfig_print(&em_shm->libconfig);
	core_log_fn_set(NULL);
}

static void cmd_em_info_print(int argc, char *argv[])
{
	/** When no argument is given, print all. */
	if (argc == 0) {
		print_em_info_all();
		return;
	} else if (argc > 1) {
		odph_cli_log("Extra parameter given to command!\n");
		return;
	}

	const char *name = argv[0];

	/** print <name> */
	if (!strcmp(name, "help"))
		print_em_info_help();
	else if (!strcmp(name, "all"))
		print_em_info_all();
	else if (!strcmp(name, "cpu_arch"))
		print_em_info_cpu_arch();
	else if (!strcmp(name, "conf"))
		print_em_info_conf();
	else
		odph_cli_log("Name %s not supported!\n", name);
}

static void print_em_pool_all(void)
{
	core_log_fn_set(cli_log);
	em_pool_info_print_all();
	core_log_fn_set(NULL);
}

static void print_em_pool(const char *pool_name)
{
	em_pool_t em_pool_hdl = em_pool_find(pool_name);

	if (em_pool_hdl == EM_POOL_UNDEF) {
		odph_cli_log("Can't find EM pool %s.\n", pool_name);
		return;
	}

	core_log_fn_set(cli_log);
	pool_info_print_hdr(1);
	pool_info_print(em_pool_hdl);
	core_log_fn_set(NULL);
}

static void print_em_pool_help(void)
{
	const char *usage = "\n"
			    "Usage: em_pool print [name]\n"
			    "\n"
			    "Names available:\n"
			    "  all\tPrint info of all pools\n"
			    "  pool name (e.g. default)\tPrint info of given pool\n"
			    "  help\tShow available names\n";

	odph_cli_log(usage);
}

static void cmd_em_pool_print(int argc, char *argv[])
{
	/** When no argument is given, print all pool info. */
	if (argc == 0) {
		print_em_pool_all();
		return;
	} else if (argc > 1) {
		odph_cli_log("Extra parameter given to command!\n");
		return;
	}

	const char *name = argv[0];

	/** print <name> */
	if (!strcmp(name, "help"))
		print_em_pool_help();
	else if (!strcmp(name, "all"))
		print_em_pool_all();
	else
		print_em_pool(name);
}

static void print_em_queue_help(void)
{
	const char *usage = "\n"
			    "Usage: em_queue print [name]\n"
			    "\n"
			    "Names available:\n"
			    "  info\tPrint queue info\n"
			    "  help\tShow available names\n";
	odph_cli_log(usage);
}

static void print_em_queue_info(void)
{
	core_log_fn_set(cli_log);
	print_queue_info();
	core_log_fn_set(NULL);
}

static void cmd_em_queue_print(int argc, char *argv[])
{
	/** When no argument is given, print queue info. */
	if (argc == 0) {
		print_em_queue_info();
		return;
	} else if (argc > 1) {
		odph_cli_log("Extra parameter given to command!\n");
		return;
	}

	const char *name = argv[0];

	if (!strcmp(name, "help"))
		print_em_queue_help();
	else if (!strcmp(name, "info"))
		print_em_queue_info();
	else
		odph_cli_log("Name %s not supported!\n", name);
}

static void print_em_qgrp_help(void)
{
	const char *usage = "\n"
			    "Usage: em_qgrp print [name]\n"
			    "\n"
			    "Names available:\n"
			    "  info\tPrint queue group info\n"
			    "  help\tShow available names\n";
	odph_cli_log(usage);
}

static void print_em_qgrp_info(void)
{
	core_log_fn_set(cli_log);
	queue_group_info_print_all();
	core_log_fn_set(NULL);
}

static void cmd_em_qgrp_print(int argc, char *argv[])
{
	/** When no argument is given, print em queue group info. */
	if (argc == 0) {
		print_em_qgrp_info();
		return;
	} else if (argc > 1) {
		odph_cli_log("Extra parameter given to command!\n");
		return;
	}

	const char *name = argv[0];

	if (!strcmp(name, "help"))
		print_em_qgrp_help();
	else if (!strcmp(name, "info"))
		print_em_qgrp_info();
	else
		odph_cli_log("Name %s not supported!\n", name);
}

static void print_em_core_help(void)
{
	const char *usage = "\n"
			    "Usage: em_core print [name]\n"
			    "\n"
			    "Names available:\n"
			    "  map\tPrint core map\n"
			    "  help\tShow available names\n";
	odph_cli_log(usage);
}

static void print_em_core_map(void)
{
	core_log_fn_set(cli_log);
	print_core_map_info();
	core_log_fn_set(NULL);
}

static void cmd_em_core_print(int argc, char *argv[])
{
	/** When no argument is given, print core map. */
	if (argc == 0) {
		print_em_core_map();
		return;
	} else if (argc > 1) {
		odph_cli_log("Extra parameter given to command!\n");
		return;
	}

	const char *name = argv[0];

	if (!strcmp(name, "help"))
		print_em_core_help();
	else if (!strcmp(name, "map"))
		print_em_core_map();
	else
		odph_cli_log("Name %s not supported!\n", name);
}

static int cli_register_em_commands(void)
{
	/* Register em commands */
	if (odph_cli_register_command("em_info_print", cmd_em_info_print,
				      "[Name: all|cpu_arch|conf|help]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_info_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_pool_print", cmd_em_pool_print,
				      "[Name: all|help]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_pool_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_queue_print", cmd_em_queue_print,
				      "[Name: info|help]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_queue_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_qgrp_print", cmd_em_qgrp_print,
				      "[Name: info|help]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_qgrp_print failed.\n");
		return -1;
	}

	if (odph_cli_register_command("em_core_print", cmd_em_core_print,
				      "[Name: map|help]")) {
		EM_LOG(EM_LOG_ERR, "Registering EM command em_core_print failed.\n");
		return -1;
	}

	return 0;
}

static int read_config_file(void)
{
	/** Conf option: cli.enable - runtime enable/disable cli */
	const char *cli_conf = "cli.enable";
	bool cli_enable = false;
	int ret = em_libconfig_lookup_bool(&em_shm->libconfig, cli_conf,
					   &cli_enable);

	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found", cli_conf);
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
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found", cli_conf);
		return -1;
	}
	EM_PRINT("  %s: %s\n", cli_conf, em_shm->opt.cli.ip_addr);

	cli_conf = "cli.port";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, cli_conf,
				      &em_shm->opt.cli.port);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found", cli_conf);
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
		EM_LOG(EM_LOG_ERR, "CLI shared memory init fails: cli_shm:%p != shm_addr:%p",
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

/**
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
	cli_param.port = em_shm->opt.cli.port;

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

	/** Create EM CLI server thread and store the thread ID to be used in
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

/**
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
