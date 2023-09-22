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
 * specific: it can e.g. be done in 'em_init(conf)' with an appropriate default
 * pool configuration, which is either given in the runtime config file through
 * 'startup_pools' option or passed via the 'conf' (em_conf_t) parameter of
 * em_init().
 *
 * In addition to the default pool, startup pools configured in the runtime
 * config file through option 'startup_pools' are also created during em_init().
 *
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
 *	em_pool_cfg_init(&pool_cfg); // init with default values (mandatory)
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

/**
 * EM pool statistic counter options
 *
 * Note that EM does not support core local cache_available corresponding to
 * ODP thread local cache_available. When creating an EM subpool, EM always
 * sets 'odp_pool_stats_opt_t::bit::thread_cache_available' to 0.
 */
typedef union {
	/** Option flags */
	struct {
		/** See em_pool_subpool_stats_t::available */
		uint64_t available          : 1;

		/** See em_pool_subpool_stats_t::alloc_ops */
		uint64_t alloc_ops          : 1;

		/** See em_pool_subpool_stats_t::alloc_fails */
		uint64_t alloc_fails        : 1;

		/** See em_pool_subpool_stats_t::free_ops */
		uint64_t free_ops           : 1;

		/** See em_pool_subpool_stats_t::total_ops */
		uint64_t total_ops          : 1;

		/** See em_pool_subpool_stats_t::cache_available */
		uint64_t cache_available    : 1;

		/** See em_pool_subpool_stats_t::cache_alloc_ops */
		uint64_t cache_alloc_ops    : 1;

		/** See em_pool_subpool_stats_t::cache_free_ops */
		uint64_t cache_free_ops     : 1;

	};

	/** All bits of the bit field structure
	 *
	 *  This field can be used to set/clear all flags, or for bitwise
	 *  operations over the entire structure.
	 */
	uint64_t all;
} em_pool_stats_opt_t;

typedef struct {
	/**
	 * Event type determines the pool type used:
	 *    - EM_EVENT_TYPE_SW creates subpools of type 'ODP_POOL_BUFFER'
	 *      This kind of EM pool CANNOT be used to create events of major
	 *      type EM_EVENT_TYPE_PACKET.
	 *    - EM_EVENT_TYPE_PACKET creates subpools of type 'ODP_POOL_PACKET'
	 *      This kind of EM pool can be used for events of all kinds.
	 *    - EM_EVENT_TYPE_VECTOR creates subpools of type 'ODP_POOL_VECTOR'
	 *      This kind of EM pool can ONLY be used for creating event vectors
	 * @note Only major types are considered here, setting minor is error
	 */
	em_event_type_t event_type;
	/**
	 * Alignment offset in bytes for the event payload start address
	 * (for all events allocated from this EM pool).
	 *
	 * Only valid for pools with event_type EM_EVENT_TYPE_SW or
	 * EM_EVENT_TYPE_PACKET (i.e. ignored for EM_EVENT_TYPE_VECTOR pools).
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
		 *         use the global default value 'pool.align_offset'
		 *         from the config file.
		 * false: Use 'pool.align_offset' from the config file (default)
		 *  true: Use pool-specific value set below.
		 */
		bool in_use;
		/**
		 * Pool-specific event payload alignment offset value in bytes
		 * (only evaluated if 'in_use=true').
		 * Overrides the config file value 'pool.align_offset' for this
		 * pool.
		 * The given 'value' must be a small power-of-two: 2, 4, or 8
		 * 0: Explicitly set 'No align offset' for the pool.
		 */
		uint32_t value;
	} align_offset;

	/**
	 * Event user area size in bytes.
	 * (for all events allocated from this EM pool).
	 *
	 * The user area is located within the event metadata (hdr) and is not
	 * part of the event payload. The event user area can e.g. be used to
	 * store additional state data related to the payload contents. EM does
	 * not initialize the contents of the user area.
	 *
	 * This option concerns all events allocated from the pool and overrides
	 * the global config file option 'pool.user_area_size' for this pool.
	 */
	struct {
		/**
		 * Select: Use pool-specific event user area 'size' from below
		 *         or use the global default value 'pool.user_area_size'
		 *         from the config file.
		 * false: Use 'pool.user_area_size' from config file (default).
		 *  true: Use pool-specific size set below.
		 */
		bool in_use;
		/**
		 * Pool-specific event user area size in bytes (only evaluated
		 * if 'in_use=true').
		 * Overrides the config file 'pool.user_area_size' for this pool
		 * 0: Explicitly set 'No user area' for the pool.
		 */
		size_t size;
	} user_area;

	/**
	 * Parameters for an EM-pool with '.event_type = EM_EVENT_TYPE_PACKET'
	 * Ignored for other pool types.
	 */
	struct {
		/**
		 * Pool-specific packet minimum headroom
		 *
		 * This option conserns all events allocated from the pool and
		 * overrides the global config file option 'pool.pkt_headroom'
		 * for this pool.
		 */
		struct {
			/**
			 * Select: Use pool-specific packet headroom value from
			 *         below or use the global default value
			 *         'pool.pkt_headroom' from the config file.
			 * false: Use 'pool.pkt_headroom' from the config file
			 *        (default).
			 *  true: Use pool-specific value set below.
			 */
			bool in_use;
			/**
			 * Pool-specific packet minimum headroom in bytes,
			 * each packet must have at least this much headroom.
			 * (only evaluated if 'in_use=true').
			 * Overrides the config file value 'pool.pkt_headroom'
			 * for this pool.
			 * 0: Explicitly set 'No headroom' for the pool.
			 */
			uint32_t value;
		} headroom;
	} pkt;

	/**
	 * Number of subpools within one EM pool, min=1, max=EM_MAX_SUBPOOLS
	 */
	int num_subpools;
	/**
	 * Subpool params array: .subpool[num_subpools]
	 */
	struct {
		/**
		 * .event_type = EM_EVENT_TYPE_SW or EM_EVENT_TYPE_PACKET:
		 *     Event payload size of the subpool (size > 0), bytes(B).
		 *     EM does not initialize the payload data.
		 * .event_type = EM_EVENT_TYPE_VECTOR:
		 *     Max number of events in a vector from the subpool, i.e.
		 *     'number of em_event_t:s in the vector's event-table[]'.
		 *     EM does not initialize the vector.
		 */
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
	 * Pool statistic options for all subpools
	 */
	struct {
		/**
		 * Select: Use pool-specific statistic options from below
		 *         or use the global default value 'pool.statistics'
		 *         from the config file.
		 * false: Use 'pool.statistics' from config file (default).
		 *  true: Use pool-specific statistic options set below.
		 */
		bool in_use;

		em_pool_stats_opt_t opt;
	} stats_opt;

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
	/** Event user area size for events from the pool */
	size_t user_area_size;
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
		 * Only if the 'available' or 'cache_available' is set to true
		 * in 'pool.statistics' of EM config file or in
		 * 'em_pool_cfg_t::stats_opt::opt' given to function
		 * em_pool_create(..., pool_cfg), otherwise .used=0.
		 */
		uint32_t used;
		/**
		 * Number of events free in the subpool.
		 * Only if the 'available' or 'cache_available' is set to true
		 * in 'pool.statistics' of EM config file or in
		 * 'em_pool_cfg_t::stats_opt::opt' given to function
		 * em_pool_create(..., pool_cfg), otherwise .free=0.
		 */
		uint32_t free;
	} subpool[EM_MAX_SUBPOOLS];
} em_pool_info_t;

typedef struct {
	/** The number of available events in the pool */
	uint64_t available;

	/** The number of alloc operations from the pool. Includes both
	 *  successful and failed operations (pool empty).
	 */
	uint64_t alloc_ops;

	/** The number of failed alloc operations (pool empty) */
	uint64_t alloc_fails;

	/** The number of free operations to the pool */
	uint64_t free_ops;

	/** The total number of alloc and free operations. Includes both
	 *  successful and failed operations (pool empty).
	 */
	uint64_t total_ops;

	/** The number of available events in the local caches of all cores */
	uint64_t cache_available;

	/** The number of successful alloc operations from pool caches (returned
	 *  at least one event).
	 */
	uint64_t cache_alloc_ops;

	/** The number of free operations, which stored events to pool caches. */
	uint64_t cache_free_ops;

	/** Internal use - don't touch! */
	uint64_t __internal_use[EM_POOL_SUBPOOL_STAT_INTERNAL];
} em_pool_subpool_stats_t;

typedef struct {
	uint32_t num_subpools;
	em_pool_subpool_stats_t subpool_stats[EM_MAX_SUBPOOLS];
} em_pool_stats_t;

/**
 * Pool subpool statistics counters
 *
 * Same as em_pool_subpool_stats_t excluding the __internal_use.
 */
typedef struct {
	/** The number of available events in the pool */
	uint64_t available;

	/** The number of alloc operations from the pool. Includes both
	 *  successful and failed operations (pool empty).
	 */
	uint64_t alloc_ops;

	/** The number of failed alloc operations (pool empty) */
	uint64_t alloc_fails;

	/** The number of free operations to the pool */
	uint64_t free_ops;

	/** The total number of alloc and free operations. Includes both
	 *  successful and failed operations (pool empty).
	 */
	uint64_t total_ops;

	/** The number of available events in the local caches of all cores */
	uint64_t cache_available;

	/** The number of successful alloc operations from pool caches (returned
	 *  at least one event).
	 */
	uint64_t cache_alloc_ops;

	/** The number of free operations, which stored events to pool caches. */
	uint64_t cache_free_ops;
} em_pool_subpool_stats_selected_t;

typedef struct {
	uint32_t num_subpools;
	em_pool_subpool_stats_selected_t subpool_stats[EM_MAX_SUBPOOLS];
} em_pool_stats_selected_t;

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
em_pool_get_name(em_pool_t pool, char *name /*out*/, size_t maxlen);

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
 * @note Set the 'available' or 'cache_available' in 'pool.statistics' of EM
 *       config file or in 'em_pool_cfg_t::stats_opt::opt' given to function
 *       em_pool_create(..., pool_cfg) to true for usage statistics, otherwise,
 *       only basic info is output omitting pool usage information (= all zeros).
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
 * @note Set the 'available' or 'cache_available' in 'pool.statistics' of EM
 *       config file or in 'em_pool_cfg_t::stats_opt::opt' given to function
 *       em_pool_create(..., pool_cfg) to true for usage statistics, otherwise,
 *       only basic info is printed, omitting pool usage information (= all zeros).
 */
void
em_pool_info_print(em_pool_t pool);

/**
 * @brief Return the number of subpools in an EM pool.
 *
 * @param pool        EM pool handle
 *
 * @return            Number of subpools in the given pool(max=EM_MAX_SUBPOOLS)
 *                    or -1 on error
 */
int em_pool_get_num_subpools(em_pool_t pool);

/**
 * Helper function to print EM Pool information for all pools in the system.
 *
 * Uses em_pool_info() when printing the pool information.
 *
 * @note Set the 'available' or 'cache_available' in 'pool.statistics' of EM
 *       config file or in 'em_pool_cfg_t::stats_opt::opt' given to function
 *       em_pool_create(..., pool_cfg) to true for usage statistics, otherwise,
 *       only basic info is printed, omitting pool usage information (= all zeros).
 */
void
em_pool_info_print_all(void);

/**
 * @brief Retrieve statistics about an EM pool.
 *
 * Read the statistic counters enabled in 'em_pool_cfg_t::stats_opt' passed to
 * em_pool_create() or in the 'pool.statistics' of EM config file. Note that
 * there may be some delay until performed pool operations are visible in the
 * statistics.
 *
 * @param         pool           EM pool handle
 * @param[out]    pool_stats     Pointer to pool statistics. A successful call
 *                               writes to this pointer the requested pool statistics.
 *
 * @return EM_OK if the statistics of all subpools of 'pool' are read successfully
 *
 * @note Runtime argument checking is not done unless EM_CHECK_LEVEL > 0.
 *
 * @see em_pool_cfg_t::stats_opt and em_pool_stats_t.
 */
em_status_t em_pool_stats(em_pool_t pool, em_pool_stats_t *pool_stats/*out*/);

/**
 * Reset statistics for an EM pool.
 *
 * Reset all statistic counters in 'em_pool_stats_t::subpool_stats' to zero
 * except:
 *	'em_pool_subpool_stats_t::available'
 *	'em_pool_subpool_stats_t::cache_available',
 *
 * @param pool    EM Pool handle
 *
 * @return EM_OK if successful
 */
em_status_t em_pool_stats_reset(em_pool_t pool);

/**
 * @brief Helper function to print statistics for an EM pool.
 *
 * Note that there may be some delay until performed pool operations are visible
 * in the statistics.
 *
 * @param      pool      EM pool handle
 *
 * Uses em_pool_stats() when printing the pool statistics.
 */
void em_pool_stats_print(em_pool_t pool);

/**
 * @brief Retrieve statistics about subpool(s) of an EM pool.
 *
 * Read the subpool statistic counters set in 'em_pool_cfg_t::stats_opt' passed
 * to em_pool_create() or in the 'pool.statistics' of EM config file. Note that
 * there may be some delay until performed pool operations are visible in the
 * statistics.
 *
 * The function returns the number of subpool statistics actually retrieved. A
 * return value equal to 'num_subpools' means that the subpool statistics for
 * given indices in 'subpools' are all retrieved successfully. A value less than
 * 'num_subpools' means that the statistics for subpools whose indices are given
 * at the end of 'subpools' can not be fetched. The function will not modify
 * corresponding 'subpool_stats'.
 *
 * @param         pool          EM pool handle
 * @param         subpools      Array of subpool indices, must contain
 *                              'num_subpools' valid subpool-indices.
 *                              0 <= indices < number of subpools 'pool' has.
 * @param         num_subpools  Number of subpools to retrieve statistics for.
 *                              0 < num_subpools <= number of subpools 'pool' has.
 * @param[out] subpool_stats    Array of subpool statistics, must have room for
 *                              'num_subpools' entries of subpool statistics.
 *                              A successful call writes to this array the requested
 *                              subpool statistics [out].
 *
 * @return number of stats successfully fetched (equal to 'num_subpools' if all
 *         successful) or 0 on error.
 *
 * @code
 *	em_pool_t pool = 1;
 *	int num = 3;
 *	int subpools[3] = [0, 3, 2];
 *	em_pool_subpool_stats_t stats[3];
 *	int ret = em_pool_subpool_stats(pool, subpools, num, stats);
 * @endcode
 *
 * The mapping between stats and subpools is as follows:
 *	stats[0] <-> subpools[0]
 *	stats[1] <-> subpools[1]
 *	...
 *	stats[num_subpools - 1] <-> subpools[num_subpools - 1]
 * So in above code, stats[1] stores statistics for the subpool whose index is 3.
 *
 * @note Runtime argument checking is not done unless EM_CHECK_LEVEL > 0.
 *
 * @see em_pool_cfg_t::stats_opt and em_pool_subpool_stats_t.
 */
int
em_pool_subpool_stats(em_pool_t pool, const int subpools[], int num_subpools,
		      em_pool_subpool_stats_t subpool_stats[]/*out*/);

/**
 * Reset statistics for subpool(s) of an EM pool.
 *
 * Reset all statistics counters in given subpools of an EM pool to zero except:
 *	'em_pool_subpool_stats_t::available'
 *	'em_pool_subpool_stats_t::cache_available'
 *
 * @param         pool          EM pool handle
 * @param         subpools      Array of subpool indices
 *                              0 <= indices < number of subpools pool has
 * @param         num_subpools  Number of subpools to reset statistics for
 *                              0 < num_subpools <= number of subpools pool has
 *
 * @return EM_OK if successful
 */
em_status_t
em_pool_subpool_stats_reset(em_pool_t pool, const int subpools[], int num_subpools);

/**
 * @brief Helper function to print statistics for subpool(s) of an EM pool.
 *
 * Note that there may be some delay until performed pool operations are visible
 * in the statistics.
 *
 * @param      pool          EM pool handle
 * @param      subpools      Array of subpool indices
 *                           0 <= indices < number of subpools pool has
 * @param      num_subpools  Number of subpools to print statistics for
 *                           0 < num_subpools <= number of subpools pool has
 *
 * Uses em_pool_subpool_stats() when printing the subpool statistics.
 */
void em_pool_subpool_stats_print(em_pool_t pool, const int subpools[], int num_subpools);

/**
 * @brief Retrieve selected statistics about an EM pool.
 *
 * Read the selected statistic counters specified in 'em_pool_stats_opt_t'. The
 * selected counters must have been enabled in 'em_pool_cfg_t::stats_opt' passed
 * to em_pool_create() or in the 'pool.statistics' of EM config file. Values of
 * the unselected counters are undefined. Note that there may be some delay until
 * performed pool operations are visible in the statistics.
 *
 * @param         pool           EM pool handle
 * @param[out]    pool_stats     Pointer to pool statistics. A successful call
 *                               writes to this pointer the requested pool statistics.
 * @opt           opt            Used to select the statistic counters to read
 *
 * @return EM_OK if the selected statistics of all subpools of 'pool' are read
 * successfully
 *
 * @note Runtime argument checking is not done unless EM_CHECK_LEVEL > 0.
 *
 * @see em_pool_cfg_t::stats_opt, em_pool_stats_selected_t and em_pool_stats_opt_t.
 */
em_status_t
em_pool_stats_selected(em_pool_t pool, em_pool_stats_selected_t *pool_stats/*out*/,
		       const em_pool_stats_opt_t *opt);

/**
 * @brief Helper function to print selected statistics for an EM pool.
 *
 * Note that there may be some delay until performed pool operations are visible
 * in the statistics.
 *
 * @param      pool      EM pool handle
 * @opt        opt       Used to select the statistic counters to print
 *
 * Uses em_pool_stats_selected() when printing the selected pool statistics.
 */
void em_pool_stats_selected_print(em_pool_t pool, const em_pool_stats_opt_t *opt);

/**
 * @brief Retrieve selected statistics about subpool(s) of an EM pool.
 *
 * Read the selected subpool statistic counters given in 'em_pool_stats_opt_t'.
 * The selected counters must have been enabled in 'em_pool_cfg_t::stats_opt'
 * passed to em_pool_create() or in the 'pool.statistics' of EM config file.
 * Values of the unselected counters are undefined. Note that there may be some
 * delay until performed pool operations are visible in the statistics.
 *
 * The function returns the number of selected subpool statistics actually read.
 * A return value of 'num_subpools' means that the selected subpool statistics
 * for the given indices in 'subpools' are all retrieved successfully. A value
 * less than 'num_subpools' means that the selected statistics for subpools whose
 * indices are given at the end of 'subpools' can not be fetched. The function
 * will not modify corresponding 'subpool_stats'.
 *
 * @param         pool          EM pool handle
 * @param         subpools      Array of subpool indices, must contain
 *                              'num_subpools' valid subpool-indices.
 *                              0 <= indices < number of subpools 'pool' has.
 * @param         num_subpools  Number of subpools to retrieve statistics for.
 *                              0 < num_subpools <= number of subpools 'pool' has.
 * @param[out] subpool_stats    Array of subpool statistics, must have room for
 *                              'num_subpools' entries of subpool statistics.
 *                              A successful call writes to this array the requested
 *                              subpool statistics [out].
 * @opt               opt       Used to select the statistic counters to read
 *
 * @return number of subpool_stats successfully fetched (equal to 'num_subpools'
 *         if all successful) or 0 on error.
 *
 * @code
 *	em_pool_t pool = 1;
 *	int num = 3;
 *	int subpools[3] = [0, 3, 2];
 *	em_pool_subpool_stats_selected_t stats[3];
 *	em_pool_stats_opt_t opt = {.bit.available = 1};
 *	int ret = em_pool_subpool_stats_selected(pool, subpools, num, stats, &opt);
 * @endcode
 *
 * The mapping between stats and subpools is as follows:
 *	stats[0] <-> subpools[0]
 *	stats[1] <-> subpools[1]
 *	...
 *	stats[num_subpools - 1] <-> subpools[num_subpools - 1]
 * So in above code, stats[1] stores selected statistics for the subpool whose
 * index is 3.
 *
 * @note Runtime argument checking is not done unless EM_CHECK_LEVEL > 0.
 *
 * @see em_pool_cfg_t::stats_opt, em_pool_subpool_stats_selected_t and em_pool_stats_opt_t.
 */
int em_pool_subpool_stats_selected(em_pool_t pool, const int subpools[], int num_subpools,
				   em_pool_subpool_stats_selected_t subpool_stats[]/*out*/,
				   const em_pool_stats_opt_t *opt);

/**
 * @brief Helper function to print selected statistics for subpool(s) of an EM pool.
 *
 * Note that there may be some delay until performed pool operations are visible
 * in the statistics.
 *
 * @param      pool          EM pool handle
 * @param      subpools      Array of subpool indices
 *                           0 <= indices < number of subpools pool has
 * @param      num_subpools  Number of subpools to print statistics for
 *                           0 < num_subpools <= number of subpools pool has
 * @opt        opt           Used to select the statistic counters to print
 *
 * Uses em_pool_subpool_stats_selected() when printing the selected subpool statistics.
 */
void em_pool_subpool_stats_selected_print(em_pool_t pool, const int subpools[],
					  int num_subpools, const em_pool_stats_opt_t *opt);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_POOL_H_ */
