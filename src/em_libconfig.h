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

#ifndef EM_LIBCONFIG_H_
#define EM_LIBCONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reads the default config and runtime config (if given) to shm and checks
 * its mandatory fields.
 *
 * @param libconfig	Pointer to shared libconfig data
 * @return int		0 on success, -1 on error
 */
int em_libconfig_init_global(libconfig_t *libconfig);

/**
 * Destroys the configs.
 *
 * @param libconfig	Pointer to shared libconfig data
 * @return int		0
 */
int em_libconfig_term_global(libconfig_t *libconfig);

/**
 * Reads integer from runtime config if given, otherwise default config.
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @param      path		Path to value
 * @param[out] value		Pointer where read value will be stored
 * @return int			1 on success, 0 otherwise
 */
int em_libconfig_lookup_int(const libconfig_t *libconfig, const char *path,
			    int *value /*out*/);

int em_libconfig_lookup_int64(const libconfig_t *libconfig, const char *path,
			      int64_t *value /*out*/);

/**
 * Reads a boolean from runtime config if given, otherwise default config.
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @param      path		Path to value
 * @param[out] value		Pointer where read value will be stored
 * @return int			1 on success, 0 otherwise
 */
int em_libconfig_lookup_bool(const libconfig_t *libconfig,
			     const char *path, bool *value /*out*/);

/**
 * Reads a string from runtime config if given, otherwise default config.
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @param      path		Path to value
 * @param[out] value		Pointer where read value will be stored
 * @return int			1 on success, 0 otherwise
 */
int em_libconfig_lookup_string(const libconfig_t *libconfig, const char *path,
			       const char **value /*out*/);

/**
 * Reads an arrays of integers from runtime config if given, otherwise from
 * default config.
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @param      path		Path to value
 * @param[out] value		Pointer where read array will be stored
 * @param      max_num		Max number of elements in the array
 * @return int			Number of read elements
 */
int em_libconfig_lookup_array(const libconfig_t *libconfig, const char *path,
			      int value[/*out*/], int max_num);

/**
 * Reads integer from runtime config if given, otherwise default config. Path
 * to config variable is assumed to be base_path.local_path.name.
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @param      base_path	Basepath to value
 * @param      local_path	Localpath to value
 * @param      name		Value name
 * @param[out] value		Pointer where read value will be stored
 * @return int			1 on success, 0 otherwise
 */
int em_libconfig_lookup_ext_int(const libconfig_t *libconfig,
				const char *base_path, const char *local_path,
				const char *name, int *value /*out*/);

/**
 * Read setting specified by 'path' from both default and runtime config.
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @param      path		Path to setting
 * @param[out] setting_default	Pointer where setting from default conf file will be stored
 * @param[out] setting_runtime	Pointer where setting from runtime conf file will be stored
 */
void em_libconfig_lookup(const libconfig_t *libconfig, const char *path,
			 libconfig_setting_t **setting_default/*out*/,
			 libconfig_setting_t **setting_runtime/*out*/);

/**
 * Read an integer named 'name' from a setting.
 *
 * @param      setting		Pointer to the setting where integer is read
 * @param      name		Value name
 * @param[out] value		Pointer where read integer value will be stored
 * @return int			1 on success, 0 otherwise
 */
int em_libconfig_setting_lookup_int(const libconfig_setting_t *setting,
				    const char *name, int *value/*out*/);

/**
 * Fetch a list named 'name' from a setting.
 *
 * @param      setting		Pointer to the setting where list is fetched
 * @param      name		List name
 * @return			Requested list on success, NULL otherwise
 */
const libconfig_list_t
*em_libconfig_setting_get_list(const libconfig_setting_t *setting, const char *name);

/**
 * Return the number of elements in a list.
 *
 * @param list		Pointer to list
 * @return int		The number of elements in a list
 */
int em_libconfig_list_length(const libconfig_list_t *list);

/**
 * Get a group setting from a list.
 *
 * @param list		Pointer to list where group is fetched
 * @param index		Index to list element
 * @param path		Path to the group setting
 * @return		Requested group on success, NULL otherwise
 */
libconfig_group_t *em_libconfig_list_lookup_group(const libconfig_list_t *list,
						  int index, const char *path);

/**
 * Read an integer from a list.
 *
 * @param list		Pointer to list where integer is read
 * @param index		Index to list element
 * @param path		Path to integer value
 * @param value[out]	Pointer where read value will be stored
 * @return int		1 on success, 0 wrong type, -1 not found
 */
int em_libconfig_list_lookup_int(const libconfig_list_t *list, int index,
				 const char *path, int *value/*out*/);

/**
 * Read a bool from a list.
 *
 * @param list		Pointer to list
 * @param index		Index to list element
 * @param path		Path to boolean value
 * @param value[out]	Pointer where read value will be stored
 * @return int		1 on success, 0 wrong type, -1 not found
 */
int em_libconfig_list_lookup_bool(const libconfig_list_t *list, int index,
				  const char *path, bool *value/*out*/);

/**
 * Read string from a list.
 *
 * @param list		Pointer to list
 * @param index		Index to list element
 * @param path		Path to string value
 * @param value[out]	Pointer where read value will be stored
 * @return int		1 on success, 0 wrong type, -1 not found
 */
int em_libconfig_list_lookup_string(const libconfig_list_t *list, int index,
				    const char *path, const char **value/*out*/);

/**
 * Get a group setting from a group.
 *
 * @param group		Pointer to group
 * @param path		Path to the group to be fetched
 * @return		Requested group on success, NULL otherwise
 */
libconfig_group_t
*em_libconfig_group_lookup_group(libconfig_group_t *group, const char *path);

/**
 * Fetch a list from a group.
 *
 * @param group		Pointer to group
 * @param path		Path to the list to be fetched
 * @return		Requested list on success, NULL otherwise
 */
libconfig_list_t
*em_libconfig_group_lookup_list(libconfig_list_t *group, const char *path);

/**
 * Read an integer from a group.
 *
 * @param group		Pointer to group
 * @param name		Name of integer value
 * @param value[out]	Pointer where read value will be stored
 * @return int		1 on success, 0 otherwise
 */
int em_libconfig_group_lookup_int(const libconfig_group_t *group,
				  const char *name, int *value/*out*/);

/**
 * Read a bool from a group.
 *
 * @param group		Pointer to group
 * @param name		Name of boolean value
 * @param value[out]	Pointer where read value will be stored
 * @return int		1 on success, 0 otherwise
 */
int em_libconfig_group_lookup_bool(const libconfig_group_t *group,
				   const char *name, bool *value/*out*/);

/**
 * Read string from a group.
 *
 * @param group		Pointer to group
 * @param name		Name of the string to be fetched
 * @param value[out]	Pointer where read value will be stored
 * @return int		1 on success, 0 otherwise
 */
int em_libconfig_group_lookup_string(const libconfig_group_t *group,
				     const char *name, const char **value/*out*/);

/**
 * Prints default config and runtime config (if given).
 *
 * @param      libconfig	Pointer to shared libconfig data
 * @return int			1 on success, 0 otherwise
 */
int em_libconfig_print(const libconfig_t *libconfig);

#ifdef __cplusplus
}
#endif

#endif
