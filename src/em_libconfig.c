/*
 *   Copyright (c) 2018, Nokia Solutions and Networks
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
 *
 * Copyright (c) 2018, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include "em_include.h"
#include "include/em_libconfig_config.h"

int libconfig_init_global(libconfig_t *libconfig)
{
	const char *filename;
	const char *vers;
	const char *vers_rt;
	const char *impl;
	const char *impl_rt;
	config_t *config = &libconfig->cfg_default;
	config_t *config_rt = &libconfig->cfg_runtime;
	const char *impl_field = "em_implementation";
	const char *vers_field = "config_file_version";

	config_init(config);
	config_init(config_rt);

	if (!config_read_string(config, config_builtin)) {
		EM_PRINT("Failed to read default config: %s(%d): %s\n",
			 config_error_file(config), config_error_line(config),
			 config_error_text(config));
		goto fail;
	}

	filename = getenv("EM_CONFIG_FILE");
	if (filename == NULL)
		return 0;

	EM_PRINT("EM CONFIG FILE: %s\n", filename);

	if (!config_read_file(config_rt, filename)) {
		EM_PRINT("  ERROR: failed to read config file: %s(%d): %s\n\n",
			 config_error_file(config_rt),
			 config_error_line(config_rt),
			 config_error_text(config_rt));
		goto fail;
	}

	/* Check runtime configuration's implementation name and version */
	if (!config_lookup_string(config, impl_field, &impl) ||
	    !config_lookup_string(config_rt, impl_field, &impl_rt)) {
		EM_PRINT("  ERROR: missing mandatory field: %s\n\n",
			 impl_field);
		goto fail;
	}
	if (!config_lookup_string(config, vers_field, &vers) ||
	    !config_lookup_string(config_rt, vers_field, &vers_rt)) {
		EM_PRINT("  ERROR: missing mandatory field: %s\n\n",
			 vers_field);
		goto fail;
	}
	if (strcmp(impl, impl_rt)) {
		EM_PRINT("  ERROR: EM implementation name mismatch:\n"
			 "    Expected: \"%s\"\n"
			 "    Found:    \"%s\"\n\n", impl, impl_rt);
		goto fail;
	}
	if (strcmp(vers, vers_rt)) {
		EM_PRINT("  ERROR: config file version number mismatch:\n"
			 "    Expected: \"%s\"\n"
			 "    Found:    \"%s\"\n\n", vers, vers_rt);
		goto fail;
	}

	return 0;
fail:
	EM_PRINT("Config file failure\n");
	config_destroy(config);
	config_destroy(config_rt);
	return -1;
}

int libconfig_term_global(libconfig_t *libconfig)
{
	config_destroy(&libconfig->cfg_default);
	config_destroy(&libconfig->cfg_runtime);

	return 0;
}

int libconfig_lookup_int(libconfig_t *libconfig, const char *path, int *value)
{
	int ret_def = CONFIG_FALSE;
	int ret_rt = CONFIG_FALSE;

	ret_def = config_lookup_int(&libconfig->cfg_default, path, value);

	/* Runtime option overrides default value */
	ret_rt = config_lookup_int(&libconfig->cfg_runtime, path, value);
	if (ret_rt == CONFIG_TRUE)
		EM_PRINT("%s: %d\n", path, *value);

	return  (ret_def == CONFIG_TRUE || ret_rt == CONFIG_TRUE) ? 1 : 0;
}

int libconfig_lookup_array(libconfig_t *libconfig, const char *path,
			   int value[], int max_num)
{
	const config_t *config;
	config_setting_t *setting;
	int num, i, j;
	int num_out = 0;

	for (j = 0; j < 2; j++) {
		if (j == 0)
			config = &libconfig->cfg_default;
		else
			config = &libconfig->cfg_runtime;

		setting = config_lookup(config, path);

		/* Runtime config may not define the array, whereas
		 * the default config has it always defined. When the array
		 * is defined, it must be correctly formatted.
		 */
		if (setting == NULL)
			continue;

		if (config_setting_is_array(setting) == CONFIG_FALSE)
			return 0;

		num = config_setting_length(setting);

		if (num <= 0 || num > max_num)
			return 0;

		for (i = 0; i < num; i++)
			value[i] = config_setting_get_int_elem(setting, i);

		num_out = num;
	}

	if (setting != NULL) {
		EM_PRINT("%s: ", path);
		for (i = 0; i < num_out; i++)
			EM_PRINT("%d ", value[i]);
		EM_PRINT("\n");
	}

	/* Number of elements copied */
	return num_out;
}

static int lookup_int(config_t *cfg,
		      const char *base_path,
		      const char *local_path,
		      const char *name,
		      int *value)
{
	char path[256];

	if (local_path) {
		snprintf(path, sizeof(path), "%s.%s.%s", base_path,
			 local_path, name);
		if (config_lookup_int(cfg, path, value) == CONFIG_TRUE)
			return 1;
	}

	snprintf(path, sizeof(path), "%s.%s", base_path, name);
	if (config_lookup_int(cfg, path, value) == CONFIG_TRUE)
		return 1;

	return 0;
}

int libconfig_lookup_ext_int(libconfig_t *libconfig, const char *base_path,
			     const char *local_path, const char *name,
			     int *value)
{
	if (lookup_int(&libconfig->cfg_runtime,
		       base_path, local_path, name, value))
		return 1;

	if (lookup_int(&libconfig->cfg_default,
		       base_path, local_path, name, value))
		return 1;

	return 0;
}
