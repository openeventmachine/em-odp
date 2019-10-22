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
int libconfig_init_global(libconfig_t *libconfig);

/**
 * Destroys the configs.
 *
 * @param libconfig	Pointer to shared libconfig data
 * @return int		0
 */
int libconfig_term_global(libconfig_t *libconfig);

/**
 * Reads integer from runtime config if given, otherwise default config.
 *
 * @param libconfig	Pointer to shared libconfig data
 * @param path		Path to value
 * @param value		Pointer where read value will be stored
 * @return int		1 on success, 0 otherwise
 */
int libconfig_lookup_int(libconfig_t *libconfig, const char *path, int *value);

/**
 * Reads an arrays of integers from runtime config if given, otherwise from
 * default config.
 *
 * @param libconfig	Pointer to shared libconfig data
 * @param path		Path to value
 * @param value		Pointer where read array will be stored
 * @param max_num	Max number of elements in the array
 * @return int		Number of read elements
 */
int libconfig_lookup_array(libconfig_t *libconfig, const char *path,
			   int value[], int max_num);

/**
 * Reads integer from runtime config if given, otherwise default config. Path
 * to config variable is assumed to be base_path.local_path.name.
 *
 * @param libconfig	Pointer to shared libconfig data
 * @param base_path	Basepath to value
 * @param local_path	Localpath to value
 * @param name		Value name
 * @param value		Pointer where read value will be stored
 * @return int		1 on success, 0 otherwise
 */
int libconfig_lookup_ext_int(libconfig_t *libconfig, const char *base_path,
			     const char *local_path, const char *name,
			     int *value);

#ifdef __cplusplus
}
#endif

#endif
