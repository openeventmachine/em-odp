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

#define SETTING_NAME_LEN 64
#define SETTING_PATH_LEN 256

int em_libconfig_init_global(libconfig_t *libconfig)
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
	libconfig->has_cfg_runtime = 0;

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

	libconfig->has_cfg_runtime = 1;
	return 0;
fail:
	EM_PRINT("Config file failure\n");
	config_destroy(config);
	config_destroy(config_rt);
	return -1;
}

int em_libconfig_term_global(libconfig_t *libconfig)
{
	config_destroy(&libconfig->cfg_default);
	config_destroy(&libconfig->cfg_runtime);

	return 0;
}

int em_libconfig_lookup_int(const libconfig_t *libconfig, const char *path,
			    int *value /*out*/)
{
	int ret_def = CONFIG_FALSE;
	int ret_rt = CONFIG_FALSE;

	ret_def = config_lookup_int(&libconfig->cfg_default, path, value);

	/* Runtime option overrides default value */
	ret_rt = config_lookup_int(&libconfig->cfg_runtime, path, value);

	return  (ret_def == CONFIG_TRUE || ret_rt == CONFIG_TRUE) ? 1 : 0;
}

int em_libconfig_lookup_int64(const libconfig_t *libconfig, const char *path,
			      int64_t *value /*out*/)
{
	int ret_def = CONFIG_FALSE;
	int ret_rt = CONFIG_FALSE;
	long long value_ll = 0;

	ret_def = config_lookup_int64(&libconfig->cfg_default, path, &value_ll);

	/* Runtime option overrides default value */
	ret_rt = config_lookup_int64(&libconfig->cfg_runtime, path, &value_ll);

	if (ret_def == CONFIG_TRUE || ret_rt == CONFIG_TRUE) {
		*value = (int64_t)value_ll;
		return 1; /* success! */
	}

	return 0; /* fail */
}

int em_libconfig_lookup_bool(const libconfig_t *libconfig, const char *path,
			     bool *value /*out*/)
{
	int ret_def = CONFIG_FALSE;
	int ret_rt = CONFIG_FALSE;
	int cfg_value = 0;
	int ret_val = 0;

	ret_def = config_lookup_bool(&libconfig->cfg_default, path, &cfg_value);

	/* Runtime option overrides default value */
	ret_rt = config_lookup_bool(&libconfig->cfg_runtime, path, &cfg_value);

	if (ret_def == CONFIG_TRUE || ret_rt == CONFIG_TRUE) {
		*value = cfg_value ? true : false;
		ret_val = 1;
	}

	return  ret_val;
}

int em_libconfig_lookup_string(const libconfig_t *libconfig, const char *path,
			       const char **value /*out*/)
{
	int ret_def = CONFIG_FALSE;
	int ret_rt = CONFIG_FALSE;

	ret_def = config_lookup_string(&libconfig->cfg_default, path, value);

	/* Runtime option overrides default value */
	ret_rt = config_lookup_string(&libconfig->cfg_runtime, path, value);

	return (ret_def == CONFIG_TRUE || ret_rt == CONFIG_TRUE) ? 1 : 0;
}

int em_libconfig_lookup_array(const libconfig_t *libconfig, const char *path,
			      int value[/*out*/], int max_num)
{
	const config_t *config;
	const config_setting_t *setting;
	int num;
	int num_out = 0;

	for (int j = 0; j < 2; j++) {
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

		for (int i = 0; i < num; i++)
			value[i] = config_setting_get_int_elem(setting, i);

		num_out = num;
	}

	/* Number of elements copied */
	return num_out;
}

void em_libconfig_lookup(const libconfig_t *libconfig, const char *path,
			 libconfig_setting_t **setting_default/*out*/,
			 libconfig_setting_t **setting_runtime/*out*/)
{
	*setting_default = config_lookup(&libconfig->cfg_default, path);
	*setting_runtime = config_lookup(&libconfig->cfg_runtime, path);
}

int em_libconfig_setting_lookup_int(const libconfig_setting_t *setting,
				    const char *name, int *value/*out*/)
{
	return config_setting_lookup_int(setting, name, value);
}

const libconfig_list_t *
em_libconfig_setting_get_list(const libconfig_setting_t *setting, const char *name)
{
	const libconfig_list_t *list_setting;

	list_setting = config_setting_get_member(setting, name);

	if (list_setting && config_setting_is_list(list_setting))
		return list_setting;

	return NULL;
}

int em_libconfig_list_length(const libconfig_list_t *list)
{
	return config_setting_length(list);
}

static uint32_t path_get_depth(const char *path, char delim)
{
	const char *p = path;
	uint32_t depth = 1; /*Depth is 1 when path contains no delimiter*/

	while (*p) {
		if (*p == delim)
			depth++;
		p++;
	}

	return depth;
}

/* Get second last setting and the last setting name specified in path from the
 * list element at index. More specifically, for path 'a.b.c.d', this function
 * gets second last setting 'c' from list element at index and the last setting
 * name 'd'.
 */
static int setting_get_child(const config_setting_t *parent, const char *path,
			     const char *delim, const uint32_t depth,
			     char *name/*out*/, config_setting_t **child/*out*/)
{
	char *saveptr; /*Used internally by strtok_r()*/
	const char *member_name;
	char path_cp[SETTING_PATH_LEN];

	/* strtok requires non const char pointer */
	strncpy(path_cp, path, SETTING_PATH_LEN - 1);
	path_cp[SETTING_PATH_LEN - 1] = '\0';

	/* Get second last setting */
	member_name = strtok_r(path_cp, delim, &saveptr);
	for (uint32_t i = 0; i < depth - 1; i++) {
		*child = config_setting_get_member(parent, member_name);

		if (!(*child))
			return -1;

		parent = *child;
		member_name = strtok_r(NULL, delim, &saveptr);
	}

	/* Get last setting name */
	strncpy(name, member_name, SETTING_NAME_LEN - 1);
	name[SETTING_NAME_LEN - 1] = '\0';
	return 0;
}

/* Get second last setting and the last setting name specified in path from the
 * list element at index. More specifically, for path 'a.b.c.d', this function
 * gets second last setting 'c' from list element at index and the last setting
 * name 'd'.
 *
 * name[out]	Pointer where last setting name will be stored
 * setting[out]	Ponter where second last setting will be stored
 */
static int list_get_setting(const libconfig_list_t *list, int index,
			    const char *path, char *name/*out*/,
			    config_setting_t **setting/*out*/)
{
	uint32_t depth;
	config_setting_t *element;
	char delim[] = ".";

	element = config_setting_get_elem(list, index);
	if (!element) {
		EM_LOG(EM_LOG_ERR, "List element %d does not exist\n", index);
		return -1;
	}

	depth = path_get_depth(path, delim[0]);
	if (depth < 2) {/*Only one level of setting in path, e.g., 'a'*/
		*setting = element;
		strncpy(name, path, SETTING_NAME_LEN - 1);
		name[SETTING_NAME_LEN - 1] = '\0';
		return 0;
	}

	/*Get second last setting and the last setting name*/
	return setting_get_child(element, path, delim, depth, name, setting);
}

libconfig_group_t *em_libconfig_list_lookup_group(const libconfig_list_t *list,
						  int index, const char *path)
{
	char name[SETTING_NAME_LEN];
	config_setting_t *setting;
	libconfig_group_t *group;

	if (list_get_setting(list, index, path, name, &setting) < 0)
		return NULL;

	group = config_setting_get_member(setting, name);
	if (group && config_setting_is_group(group))
		return group;

	return NULL;
}

int em_libconfig_list_lookup_int(const libconfig_list_t *list, int index,
				 const char *path, int *value/*out*/)
{
	char name[SETTING_NAME_LEN];
	config_setting_t *setting;
	const config_setting_t *member;

	if (list_get_setting(list, index, path, name, &setting) < 0)
		return -1; /*Parent setting not found*/

	member = config_setting_get_member(setting, name);
	if (!member) /*Setting not found*/
		return -1;

	return config_setting_lookup_int(setting, name, value);
}

int em_libconfig_list_lookup_bool(const libconfig_list_t *list, int index,
				  const char *path, bool *value/*out*/)
{
	int cfg_value;
	char name[SETTING_NAME_LEN];
	config_setting_t *setting;
	const config_setting_t *member;

	if (list_get_setting(list, index, path, name, &setting) < 0)
		return -1; /*Parent setting not found*/

	member = config_setting_get_member(setting, name);
	if (!member) /*Setting not found*/
		return -1;

	if (!config_setting_lookup_bool(setting, name, &cfg_value))
		return 0;

	*value = cfg_value ? true : false;
	return 1;
}

int em_libconfig_list_lookup_string(const libconfig_list_t *list, int index,
				    const char *path, const char **value/*out*/)
{
	char name[SETTING_NAME_LEN];
	config_setting_t *setting;
	const config_setting_t *member;

	if (list_get_setting(list, index, path, name, &setting) < 0)
		return -1; /*Parent setting not found*/

	member = config_setting_get_member(setting, name);
	if (!member) /*Setting not found*/
		return -1;

	return config_setting_lookup_string(setting, name, value);
}

/* Get second last setting and the last setting name specified in path from
 * the given group. More specifically, for path 'a.b.c.d', this function
 * gets second last setting 'c' from group and the last setting name 'd'.
 *
 * name[out]	Pointer where last setting name will be stored
 * setting[out]	Ponter where second last setting will be stored
 */
static int group_get_setting(libconfig_list_t *group, const char *path,
			     char *name/*out*/, config_setting_t **setting/*out*/)
{
	uint32_t depth;
	char delim[] = ".";

	depth = path_get_depth(path, delim[0]);
	if (depth < 2) {/*No child setting*/
		*setting = group;
		strncpy(name, path, SETTING_NAME_LEN - 1);
		name[SETTING_NAME_LEN - 1] = '\0';
		return 0;
	}

	/*Get child setting*/
	return setting_get_child(group, path, delim, depth, name, setting);
}

libconfig_group_t
*em_libconfig_group_lookup_group(libconfig_group_t *group, const char *path)
{
	char name[SETTING_NAME_LEN];
	config_setting_t *setting;
	libconfig_group_t *group_out;

	if (group_get_setting(group, path, name, &setting) < 0)
		return NULL;

	group_out = config_setting_get_member(setting, name);
	if (group_out && config_setting_is_group(group_out))
		return group_out;

	return NULL;
}

libconfig_list_t
*em_libconfig_group_lookup_list(libconfig_group_t *group, const char *path)
{
	libconfig_list_t *list;
	config_setting_t *setting;
	char name[SETTING_NAME_LEN];

	if (group_get_setting(group, path, name, &setting) < 0)
		return NULL;

	list = config_setting_get_member(setting, name);
	if (list && config_setting_is_list(list))
		return list;

	return NULL;
}

int em_libconfig_group_lookup_int(const libconfig_group_t *group,
				  const char *name, int *value/*out*/)
{
	return config_setting_lookup_int(group, name, value);
}

int em_libconfig_group_lookup_bool(const libconfig_group_t *group,
				   const char *name, bool *value/*out*/)
{
	int cfg_value;

	if (!config_setting_lookup_bool(group, name, &cfg_value))
		return 0;

	*value = cfg_value ? true : false;
	return 1;
}

int em_libconfig_group_lookup_string(const libconfig_group_t *group,
				     const char *name, const char **value/*out*/)
{
	return config_setting_lookup_string(group, name, value);
}

static int lookup_int(const config_t *cfg,
		      const char *base_path,
		      const char *local_path,
		      const char *name,
		      int *value /*out*/)
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

int em_libconfig_lookup_ext_int(const libconfig_t *libconfig,
				const char *base_path, const char *local_path,
				const char *name, int *value /*out*/)
{
	if (lookup_int(&libconfig->cfg_runtime,
		       base_path, local_path, name, value))
		return 1;

	if (lookup_int(&libconfig->cfg_default,
		       base_path, local_path, name, value))
		return 1;

	return 0;
}

int em_libconfig_print(const libconfig_t *libconfig)
{
	int c;
	/* Temp file for config_write() output. Suppress Coverity warning about tmpfile() usage. */
	/* coverity[secure_temp] */
	FILE *file = tmpfile();

	if (file == NULL)
		return -1;

	if (fprintf(file,
		    "\nEM_CONFIG_FILE default values:\n"
		    "-------------------------------\n\n") < 0)
		goto fail;

	config_write(&libconfig->cfg_default, file);

	if (libconfig->has_cfg_runtime) {
		if (fprintf(file,
			    "\nEM_CONFIG_FILE override values:\n"
			    "--------------------------------\n\n") < 0)
			goto fail;

		config_write(&libconfig->cfg_runtime, file);
	}

	/* Print temp file to the log */
	rewind(file);
	while ((c = fgetc(file)) != EOF)
		EM_PRINT("%c", (char)c);

	fclose(file);
	return 0;

fail:
	fclose(file);
	return -1;
}
