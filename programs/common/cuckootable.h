/*
 *   Copyright (c) 2024, Nokia Solutions and Networks
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
 */

/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2016-2018 Linaro Limited
 */

/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
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
 */

#ifndef CUCKOO_TABLE_H_
#define CUCKOO_TABLE_H_

/**
 * @file
 *
 * Cuckoo Hash Table
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup cuckootable CUCKOO TABLE
 * Cuckoo table
 *
 * @{
 */

/**
 * Create a cuckoo table
 *
 * @param name        Name of the cuckoo table to be created
 * @param capacity    Number of elements table may store
 * @param key_size    Size of the key for each element
 * @param value_size  Size of the value stored for each element
 *
 * @return Handle of created cuckoo table
 * @retval NULL Create failed
 */
table_t cuckoo_table_create(const char *name, uint32_t capacity,
			    uint32_t key_size, uint32_t value_size);

/**
 * Lookup a cuckoo table by name
 *
 * @param name  The name of the table to be located
 *
 * @return Handle of the located cuckoo table
 * @retval NULL No table matching supplied name found
 */
table_t cuckoo_table_lookup(const char *name);

/**
 * Destroy a cuckoo table
 *
 * @param table  The handle of the cuckoo table to be destroyed
 *
 * @retval 0   Success
 * @retval < 0 Failure
 */
int cuckoo_table_destroy(table_t table);

/**
 * Insert a key/value pair into a cuckoo table
 *
 * @param table  The table into which the value is to be stored
 * @param key    Address of an table_t to be used as key
 * @param value  Value to be associated with specified key
 *
 * @retval >= 0 Success
 * @retval < 0  Failure
 */
int cuckoo_table_put_value(table_t table, void *key, void *value);

/**
 * Retrieve a value from a cuckoo table
 *
 * @param table        The table from which the value is to be retrieved
 * @param key          Address of an table_t to be used as key
 * @param[out] buffer  Address of buffer to receive resulting value
 * @param buffer_size  Size of supplied buffer
 *
 * @retval 0   Success
 * @retval 1   Success
 * @retval < 0 Failure
 */
int cuckoo_table_get_value(table_t table, void *key,
			   void *buffer /*out*/, uint32_t buffer_size);

/**
 * Remove a value from a cuckoo table
 *
 * @param table  The table from which the value is to be removed
 * @param key    Address of table_t to be used as key
 *
 * @retval >= 0 Success
 * @retval < 0  Failure
 */
int cuckoo_table_remove_value(table_t table, void *key);

extern table_ops_t cuckoo_table_ops;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* CUCKOO_TABLE_H_ */
