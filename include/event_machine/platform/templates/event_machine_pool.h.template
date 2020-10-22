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
 */

#ifndef EVENT_MACHINE_POOL_H_
#define EVENT_MACHINE_POOL_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_pool Event Pool
 *  Event Machine event pool related services
 * @{
 *
 * EM events are allocated from event pools with em_alloc() and freed back into
 * them with em_free(). The event pools to be allocated from must first be
 * created with em_pool_create().
 *
 * Note that EM should always provide at least one pool, i.e. 'EM_POOL_DEFAULT'
 * that can be used for event allocation. The default pool creation is platform
 * specific: it can e.g. be done in 'em_init(conf)' with an appropriate
 * default pool config passed via the 'conf' (em_conf_t) parameter.
 * Further event pools should be created explicitly with em_pool_create().
 *
 * Event pool APIs for pool deletion, lookup, iteration etc. are listed below.
 *
 * The 'em_pool_cfg_t' type given to em_pool_create() is HW/platform specific
 * and is defined in event_machine_hw_types.h
 *
 * Do not include this from the application, event_machine.h will
 * do it for you.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM pool configuration
 *
 * Configuration of an EM event pool consisting of up to 'EM_MAX_SUBPOOLS'
 * subpools, each supporting a specific event payload size. Event allocation,
 * i.e. em_alloc(), will use the subpool that provides the best fit for the
 * requested size.
 *
 * Example usage:
 * @code
 *	em_pool_cfg_t pool_cfg;
 *
 *	em_pool_cfg_init(&pool_cfg); // init with default values
 *	pool_cfg.event_type = EM_EVENT_TYPE_PACKET;
 *	...
 *	pool_cfg.num_subpools = 4;
 *	pool_cfg.subpool[0].size = X;
 *	pool_cfg.subpool[0].num = Y;
 *	pool_cfg.subpool[0].cache_size = Z;
 *	...
 *	pool = em_pool_create(..., &pool_cfg);
 * @endcode
 */
typedef struct {
	/**
	 * Event type determines the pool type used:
	 *    - EM_EVENT_TYPE_SW creates subpools of type 'ODP_POOL_BUFFER'
	 *      This kind of EM pool CANNOT be used to create events of major
	 *      type EM_EVENT_TYPE_PACKET.
	 *    - EM_EVENT_TYPE_PACKET creates subpools of type 'ODP_POOL_PACKET'
	 *      This kind of EM pool can be used for events of all kinds.
	 * @note Only major types are considered here, setting minor is error
	 */
	em_event_type_t event_type;
	/**
	 * Alignment offset in bytes for the event payload start address
	 * (for all events allocated from this EM pool).
	 *
	 * The default EM event payload start address alignment is a
	 * power-of-two that is at minimum 32 bytes (i.e. 32 B, 64 B, 128 B etc.
	 * depending on e.g. target cache-line size).
	 * The 'align_offset.value' option can be used to fine-tune the
	 * start-address by a small offset to e.g. make room for a small
	 * SW header before the rest of the payload that might need a specific
	 * alignment for direct HW-access.
	 * Example: setting 'align_offset.value = 8' makes sure that the payload
	 * _after_ 8 bytes will be aligned at minimum (2^x) 32 bytes.
	 *
	 * This option conserns all events allocated from the pool and overrides
	 * the global config file option 'pool.align_offset' for this pool.
	 */
	struct {
		/**
		 * Select: Use pool-specific align-offset 'value' from below or
		 *         use the global default value from the config file.
		 * false (0): Use default value from the config file.
		 * true (not 0): Use pool specific value set below.
		 */
		int in_use;
		/**
		 * Pool-specific event payload alignment offset value in bytes
		 * (only evaluated if 'in_use=true').
		 * Overrides the config file value for this pool.
		 * The given 'value' must be a small power-of-two: 2, 4, or 8
		 * 0: Explicitly set 'No align offset' for the pool.
		 */
		uint32_t value;
	} align_offset;
	/**
	 * Number of subpools within one EM pool, max=EM_MAX_SUBPOOLS
	 */
	int num_subpools;
	struct {
		/** Event payload size of the subpool (size > 0)  */
		uint32_t size;
		/** Number of events in the subpool (num > 0) */
		uint32_t num;
		/**
		 * Maximum number of locally cached subpool events per EM-core.
		 *
		 * Allocating or freeing events from a core-local event-cache
		 * can be faster than using the global event subpool. Cached
		 * events are only available on the local core and can reduce
		 * the number of globally free events in the subpool, thus
		 * consider setting 'num > EM-core-count * cache_size'.
		 * The actual used cache_size will be smaller than or equal to
		 * the requested value, depending on the implementation.
		 */
		uint32_t cache_size;
	} subpool[EM_MAX_SUBPOOLS];

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_pool_cfg_init(pool_cfg) has been called before
	 * creating a pool with em_pool_create(..., pool_cfg)
	 */
	uint32_t __internal_check;
} em_pool_cfg_t;

/**
 * EM pool information and usage statistics
 */
typedef struct {
	/* Pool name */
	char name[EM_POOL_NAME_LEN];
	/** EM pool handle */
	em_pool_t em_pool;
	/** Event type of events allocated from the pool */
	em_event_type_t event_type;
	/** Event payload alignment offset for events from the pool */
	uint32_t align_offset;
	/** Number of subpools within one EM pool, max=EM_MAX_SUBPOOLS */
	int num_subpools;
	struct {
		/** Event payload size of the subpool */
		uint32_t size;
		/** Number of events in the subpool */
		uint32_t num;
		/** Max number of locally cached subpool events per EM-core */
		uint32_t cache_size;
		/**
		 * Number of events allocated from the subpool.
		 * Only if EM config file: pool.statistics_enable=true,
		 * otherwise .used=0
		 */
		uint32_t used;
		/**
		 * Number of events free in the subpool.
		 * Only if EM config file: pool.statistics_enable=true,
		 * otherwise .free=0
		 */
		uint32_t free;
	} subpool[EM_MAX_SUBPOOLS];
} em_pool_info_t;

/**
 * Initialize EM-pool configuration parameters for em_pool_create()
 *
 * Initialize em_pool_cfg_t to default values for all fields.
 * After initialization, the user further needs to update the fields of
 * 'em_pool_cfg_t' with appropriate sizing information before calling
 * em_pool_create().
 *
 * Always initialize 'pool_cfg' first with em_pool_cfg_init(pool_cfg) to
 * ensure backwards compatibility with potentially added new options.
 *
 * @param pool_cfg  Address of the em_pool_cfg_t to be initialized
 *
 * @see em_pool_cfg_t and em_pool_create()
 */
void em_pool_cfg_init(em_pool_cfg_t *const pool_cfg);

/**
 * Create a new EM event pool
 *
 * Create an EM event pool that can be used for event allocation. The event pool
 * is created and configured according to the platform/HW specific em_pool_cfg_t
 * given as argument.
 *
 * @param name        Pool name (optional, NULL ok)
 * @param pool        A specific pool handle to be used or EM_POOL_UNDEF to let
 *                    EM decide (i.e. use a free handle).
 * @param pool_cfg    Pointer to the pool config
 *
 * @return EM pool handle or EM_POOL_UNDEF on error
 *
 * @see em_pool_cfg_t and em_pool_cfg_init()
 */
em_pool_t
em_pool_create(const char *name, em_pool_t pool, const em_pool_cfg_t *pool_cfg);

/**
 * Delete an existing EM event pool
 *
 * @param pool    EM event pool handle of the pool to be deleted.
 *
 * @return EM_OK if successful
 */
em_status_t
em_pool_delete(em_pool_t pool);

/**
 * Find an EM event pool by name.
 *
 * Finds a pool by the given name (exact match). An empty string will not match
 * anything. The search is case sensitive. The function will return the first
 * match only if there are duplicate names.
 *
 * @param name    the name to look for
 *
 * @return pool handle or EM_POOL_UNDEF if not found
 *
 * @see em_pool_create()
 */
em_pool_t
em_pool_find(const char *name);

/**
 * Get the name of an EM event pool.
 *
 * A copy of the name string (up to 'maxlen' characters) is written to the user
 * given buffer.
 * The string is always null terminated, even if the given buffer length is less
 * than the name length.
 *
 * If the event pool has no name, the function returns 0 and writes an
 * empty string.
 *
 * @param      pool    EM event pool
 * @param[out] name    Destination buffer
 * @param      maxlen  Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0').
 */
size_t
em_pool_get_name(em_pool_t pool, char *name, size_t maxlen);

/**
 * Initialize event pool iteration and return the first event pool handle.
 *
 * Can be used to initialize the iteration to retrieve all created event pools
 * for debugging or management purposes. Use em_pool_get_next() after this call
 * until it returns EM_POOL_UNDEF.
 * A new call to em_pool_get_first() resets the iteration, which is maintained
 * per core (thread). The operation should be completed in one go before
 * returning from the EO's event receive function (or start/stop).
 *
 * The number of event pools (output arg 'num') may not match the amount of
 * event pools actually returned by iterating using em_pool_get_next()
 * if event pools are added or removed in parallel by another core. The order
 * of the returned event pool handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_pool_t pool = em_pool_get_first(&num);
 *	while (pool != EM_POOL_UNDEF) {
 *		pool = em_pool_get_next();
 *	}
 * @endcode
 *
 * @param[out] num   Pointer to an unsigned int to store the amount of
 *                   event pools into
 * @return The first event pool handle or EM_POOL_UNDEF if none exist
 *
 * @see em_pool_get_next()
 */
em_pool_t
em_pool_get_first(unsigned int *num);

/**
 * Return the next event pool handle.
 *
 * Continues the event pool iteration started by em_pool_get_first()
 * and returns the next event pool handle.
 *
 * @return The next event pool handle or EM_POOL_UNDEF if the atomic
 *         group iteration is completed (i.e. no more event pools available).
 *
 * @see em_pool_get_first()
 */
em_pool_t
em_pool_get_next(void);

/**
 * Retieve information about an EM pool.
 *
 * @param      pool        EM pool handle
 * @param[out] pool_info   Pointer to pool info that will be written
 *
 * @return EM_OK if successful
 *
 * @note EM config file: pool.statistics_enable=true for usage statistics,
 *       otherwise only basic info is output omitting pool usage information
 *       (= all zeros).
 */
em_status_t
em_pool_info(em_pool_t pool, em_pool_info_t *pool_info /*out*/);

/**
 * Helper function to print EM Pool information for a given pool.
 *
 * Uses em_pool_info() when printing the pool information.
 *
 * @param pool             EM pool handle
 *
 * @note EM config file: pool.statistics_enable=true for usage statistics,
 *       otherwise only basic info is output omitting pool usage information
 *       (= all zeros).
 */
void
em_pool_info_print(em_pool_t pool);

/**
 * Helper function to print EM Pool information for all pools in the system.
 *
 * Uses em_pool_info() when printing the pool information.
 *
 * @note EM config file: pool.statistics_enable=true for usage statistics,
 *       otherwise only basic info is output omitting pool usage information
 *       (= all zeros).
 */
void
em_pool_info_print_all(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_POOL_H_ */
