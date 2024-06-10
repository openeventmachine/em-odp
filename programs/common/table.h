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
 * Copyright (c) 2015-2018 Linaro Limited
 */

/**
 * @file
 *
 * Table lookup interface for EM examples
 */

#ifndef TABLE_H_
#define TABLE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup table_interface TABLES
 * Lookup table interface
 *
 * @details
 * This file contains a simple interface for creating and using a lookup table.
 * The table contains many entries, each consist of a key and associated data.
 *  All lookup tables have the common operations:
 *    - create a table, destroy a table
 *    - add: key + associated data,
 *    - look up the associated data via the key
 *    - delete key
 * @{
 */

#include <stdint.h>

/**
 * @def TABLE_NAME_LEN
 * Max length of table name
 */
#define TABLE_NAME_LEN  32

/** table handle */
typedef ODPH_HANDLE_T(table_t);

/**
 * Create a table
 * Generally, tables only support key-value pairs with fixed size
 *
 * @param name
 *    The name of this table, max TABLE_NAME_LEN - 1
 *    May be specified as NULL for anonymous table
 * @param capacity
 *    Max memory usage this table use, in MBytes
 * @param key_size
 *    Fixed size of the 'key' in bytes.
 * @param value_size
 *    Fixed size of the 'value' in bytes.
 * @return
 *   Handle to table instance or NULL if failed
 * @note
 */
typedef table_t (*table_create)(const char *name,
				uint32_t capacity,
				uint32_t key_size,
				uint32_t value_size);

/**
 * Find a table by name
 *
 * @param name  The name of the table
 *
 * @return Handle of found table
 * @retval NULL  table could not be found
 *
 * @note This routine cannot be used to look up an anonymous
 *       table (one created with no name).
 *       This API supports Multiprocess
 */
typedef table_t (*table_lookup)(const char *name);

/**
 * Destroy a table previously created by table_create()
 *
 * @param table  Handle of the table to be destroyed
 *
 * @retval  0 Success
 * @retval -1 Failure
 *
 * @note This routine destroys a previously created pool
 *       also should free any memory allocated at creation
 *
 */
typedef int (*table_destroy)(table_t table);

/**
 * Add (key,associated data) pair into the specific table.
 * When no associated data is currently associated with key,
 * then the (key,assocatied data) association is created.
 * When key is already associated with data0, then association (key, data0)
 * will be removed and association (key, associated data) is created.
 *
 * @param table  Handle of the table that the element be added
 *
 * @param key    The address of 'key' in key-value pair.
 *               User should make sure the address and 'key_size'
 *               bytes after are accessible.
 * @param value  The address of 'value' in key-value pair
 *               User should make sure the address and 'value_size'
 *               bytes after are accessible.
 * @retval  0 Success
 * @retval -1 Failure
 * @note  Add a same key again with a new value, the older one will
 *        be covered.
 */
typedef int (*table_put_value)(table_t table, void *key, void *value);

/**
 * Lookup the associated data via specific key.
 * When no value is currently associated with key, then this operation
 * restuns <0 to indicate the lookup miss.
 * When key is associated with value,
 * then this operation returns value.
 * The (key,value) association won't change.
 *
 * @param table  Handle of the table that the element be added
 *
 * @param key   address of 'key' in key-value pair
 *              User should make sure the address and key_size bytes after
 *              are accessible
 *
 * @param buffer   output The buffer address to the 'value'
 *                 After successfully found, the content of 'value' will be
 *                 copied to this address
 *                 User should make sure the address and value_size bytes
 *                 after are accessible
 * @param buffer_size  size of the buffer
 *                     should be equal or bigger than value_size
 * @retval 0 Success
 * @retval -1 Failure
 *
 * @note
 */
typedef int (*table_get_value)(table_t table, void *key,
						void *buffer,
						uint32_t buffer_size);
/**
 * Delete the association specified by key
 * When no data is currently associated with key, this operation
 * has no effect. When key is already associated data ad0,
 * then (key,ad0) pair is deleted.
 *
 * @param table Handle of the table that the element will be removed from
 *
 * @param key   address of 'key' in key-value pair
 *              User should make sure the address and key_size bytes after
 *              are accessible
 *
 * @retval 0 Success
 * @retval -1 Failure
 *
 * @note
 */
typedef int (*table_remove_value)(table_t table, void *key);

/**
 * Table interface set. Defining the table operations.
 */
typedef struct table_ops_t {
	/** Table Create */
	table_create f_create;
	/** Table Lookup */
	table_lookup f_lookup;
	/** Table Destroy */
	table_destroy f_des;
	/** add (key,associated data) pair into the specific table */
	table_put_value f_put;
	/** lookup the associated data via specific key */
	table_get_value f_get;
	/** delete the association specified by key */
	table_remove_value f_remove;
} table_ops_t;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
