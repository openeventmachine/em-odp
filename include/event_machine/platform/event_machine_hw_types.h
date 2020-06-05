/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2015, Nokia Solutions and Networks
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

/**
 * @file
 *
 * Event Machine HW specific types
 *
 */

#ifndef EVENT_MACHINE_HW_TYPES_H
#define EVENT_MACHINE_HW_TYPES_H

#include <odp/api/cpumask.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef em_pool_t
 * Memory/Event Pool handle.
 *
 * Defines the memory pool e.g. used in em_alloc().
 * The default pool is defined by EM_POOL_DEFAULT.
 *
 * @see em_alloc(), event_machine_hw_config.h
 */
EM_HANDLE_T(em_pool_t);
/** Undefined EM pool */
#define EM_POOL_UNDEF ((em_pool_t)EM_HDL_UNDEF)
/** em_pool_t printf format */
#define PRI_POOL  PRI_HDL

/**
 * Major event types.
 */
typedef enum em_event_type_major_e {
	EM_EVENT_TYPE_UNDEF  = 0,       /**< Undef                */
	EM_EVENT_TYPE_SW     = 1 << 24, /**< Event from SW (EO)   */
	EM_EVENT_TYPE_PACKET = 2 << 24, /**< Event from packet HW */
	EM_EVENT_TYPE_TIMER  = 3 << 24, /**< Event from timer HW  */
	EM_EVENT_TYPE_CRYPTO = 4 << 24  /**< Event from crypto HW */
} em_event_type_major_e;

/**
 * @enum em_event_type_sw_minor_e
 * Minor event types for the major EM_EVENT_TYPE_SW type.
 */
typedef enum em_event_type_sw_minor_e {
	EM_EVENT_TYPE_SW_DEFAULT = 0
} em_event_type_sw_minor_e;

/**
 * Queue types
 */
typedef enum em_queue_type_e {
	/** Undefined */
	EM_QUEUE_TYPE_UNDEF = 0,
	/**
	 * The application receives events one by one, non-concurrently to
	 * guarantee exclusive processing and ordering
	 */
	EM_QUEUE_TYPE_ATOMIC = 1,
	/**
	 * The application may receive events fully concurrently, egress event
	 * ordering (when processed in parallel) not guaranteed
	 */
	EM_QUEUE_TYPE_PARALLEL = 2,
	/**
	 * The application may receive events concurrently, but the system takes
	 * care of egress order (between two queues)
	 */
	EM_QUEUE_TYPE_PARALLEL_ORDERED = 3,
	/**
	 * A queue which is not connected to scheduling. The application needs
	 * to explicitly dequeue events
	 */
	EM_QUEUE_TYPE_UNSCHEDULED = 4,
	/**
	 * A queue type for local virtual queue not connected to scheduling.
	 */
	EM_QUEUE_TYPE_LOCAL = 5,
	/**
	 * A system specific queue type to abstract output from EM,
	 * e.g. packet output or output towards a HW accelerator.
	 * The application uses em_send() and variants to send an event 'out'.
	 */
	EM_QUEUE_TYPE_OUTPUT = 6
} em_queue_type_e;

/**
 * Portable queue priorities.
 *
 * Never directly use the numerical values as they may change from
 * one platform to the next; always use the enum names instead.
 * @see EM_QUEUE_PRIO_NUM
 */
typedef enum em_queue_prio_e {
	EM_QUEUE_PRIO_UNDEF   = 0xFF, /**< Undefined */
	EM_QUEUE_PRIO_LOWEST  = 0,    /**< Lowest */
	EM_QUEUE_PRIO_LOW     = 2,    /**< Low */
	EM_QUEUE_PRIO_NORMAL  = 4,    /**< Normal */
	EM_QUEUE_PRIO_HIGH    = 6,    /**< High */
	EM_QUEUE_PRIO_HIGHEST = 7     /**< Highest */
} em_queue_prio_e;

/**
 * em_queue_flag_t values (system specific):
 * Only combine with bitwise OR.
 */
/**
 * @def EM_QUEUE_FLAG_DEFAULT
 *
 * em_queue_flag_t default value. The EM queues will use implementation specific
 * default values.
 * The default values for this implementation values imply:
 *     EM_QUEUE_FLAG_DEFAULT = MTSAFE and BLOCKING queue implementation
 */
#define EM_QUEUE_FLAG_DEFAULT  0

/**
 * @def EM_QUEUE_FLAG_BLOCKING
 * Blocking queue implementation. A suspeding thread may block all other
 * threads, i.e. no block freedom guarantees.
 * Implied by EM_QUEUE_FLAG_DEFAULT for the implementation on this system.
 */
#define EM_QUEUE_FLAG_BLOCKING  0 /* blocking, fastest (default) */

/**
 * @def EM_QUEUE_FLAG_NONBLOCKING_LF
 *
 * em_queue_flag_t value (system specific). Only combine flags with bitwise OR.
 *
 * Require a non-blocking and lock-free queue implementation.
 * Other threads can make progress while a thread is suspended.
 * Starvation freedom is not guaranteed.
 * Queue creation will fail if set and not supported.
 */
#define EM_QUEUE_FLAG_NONBLOCKING_LF  1 /* non-blocking, lock-free */

/**
 * @def EM_QUEUE_FLAG_NONBLOCKING_WF
 *
 * em_queue_flag_t value (system specific). Only combine flags with bitwise OR.
 *
 * Require a non-blocking and wait-free queue implementation.
 * Other threads can make progress while a thread is suspended.
 * Starvation freedom is guaranteed.
 * Queue creation will fail if set and not supported.
 */
#define EM_QUEUE_FLAG_NONBLOCKING_WF  2 /* non-blocking, wait-free */

/**
 * @def EM_QUEUE_FLAG_ENQ_NOT_MTSAFE
 *
 * em_queue_flag_t value (system specific). Only combine flags with bitwise OR.
 *
 * Default multithread safe enqueue implementation not needed, the application
 * guarantees there is no concurrent accesses in enqueue, i.e. em_send().
 * This can only be used with unscheduled queues and can potentially improve
 * performance. The implementation may choose to ignore this flag.
 * Use with care.
 **/
#define EM_QUEUE_FLAG_ENQ_NOT_MTSAFE  4

/**
 * @def EM_QUEUE_FLAG_DEQ_NOT_MTSAFE
 *
 * em_queue_flag_t value (system specific). Only combine flags with bitwise OR.
 *
 * Default multithread safe dequeue implementation not needed, the application
 * guarantees there is no concurrent accesses in dequeue, i.e.
 * em_queue_dequeue(). This can only be used with unscheduled queues and can
 * potentially improve performance. The implementation may choose to ignore this
 * flag. Use with care.
 **/
#define EM_QUEUE_FLAG_DEQ_NOT_MTSAFE  8

/**
 * Type for queue group core mask.
 * Each bit represents one core, core 0 is the lsb (1 << em_core_id())
 * Note, that EM will enumerate the core identifiers to always start from 0 and
 * be contiguous meaning the core numbers are not necessarily physical.
 * This type can handle up to 64 cores.
 *
 * Use the functions in event_machine_hw_specific.h to manipulate the
 * core masks.
 *
 * @see em_queue_group_create()
 */
typedef struct {
	odp_cpumask_t odp_cpumask;
} em_core_mask_t;

/** Number of chars needed to hold core mask as a string:'0xcoremask' + '\0' */
#define EM_CORE_MASK_STRLEN  ((EM_MAX_CORES + 3) / 4 + 3)

/**
 * @def EM_MAX_SUBPOOLS
 * @brief The number of subpools in each EM pool.
 *        The subpool is a pool with buffers of only one size.
 */
#define EM_MAX_SUBPOOLS  4

/**
 * EM pool configuration
 *
 * Configuration of an EM event pool consisting of up to 'EM_MAX_SUBPOOLS'
 * subpools, each supporting a specific event payload size. Event allocation,
 * i.e. em_alloc(), will use the subpool that provides the best fit for the
 * requested size.
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
	} subpool[EM_MAX_SUBPOOLS];
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
 * Error/Status codes
 */
typedef enum em_status_e {
	/** Illegal context */
	EM_ERR_BAD_CONTEXT      = 1,
	/** Illegal state */
	EM_ERR_BAD_STATE        = 2,
	/** ID not from a valid range */
	EM_ERR_BAD_ID           = 3,
	/** Resource allocation failed */
	EM_ERR_ALLOC_FAILED     = 4,
	/** Resource already reserved by someone else */
	EM_ERR_NOT_FREE         = 5,
	/** Resource not found */
	EM_ERR_NOT_FOUND        = 6,
	/** Value over the limit */
	EM_ERR_TOO_LARGE        = 7,
	/** Value over the limit */
	EM_ERR_TOO_SMALL        = 8,
	/** Operation failed */
	EM_ERR_OPERATION_FAILED = 9,
	/** Failure in a library function */
	EM_ERR_LIB_FAILED       = 10,
	/** Implementation missing (placeholder) */
	EM_ERR_NOT_IMPLEMENTED  = 11,
	/** Pointer from bad memory area (e.g. NULL) */
	EM_ERR_BAD_POINTER      = 12,
	/** Operation timeout (e.g. waiting on a lock) */
	EM_ERR_TIMEOUT          = 13,

	/** Other error. This is the last error code (for bounds checking) */
	EM_ERR
} em_status_e;

/**
 * EM log level
 */
typedef enum {
	EM_LOG_DBG,
	EM_LOG_PRINT,
	EM_LOG_ERR
} em_log_level_t;

/**
 * EM log function, variable number of args
 *
 * @note: both 'log()' and 'vlog()' need to be implemented if used.
 */
typedef int (*em_log_func_t)(em_log_level_t level, const char *fmt, ...)
			     __attribute__((format(printf, 2, 3)));

/**
 * EM log function, va_list instead of variable number of args
 *
 * @note: both 'log()' and 'vlog()' need to be implemented if used.
 */
typedef int (*em_vlog_func_t)(em_log_level_t level, const char *fmt,
			      va_list args);

/**
 * Input poll function - poll various input sources for pkts/events and enqueue
 * into EM.
 *
 * User provided function - EM calls this, if not NULL, in the dispatch loop on
 * each core - set via 'em_conf.input.input_poll_fn'
 *
 * @return number of pkts/events received from input and enqueued into EM
 */
typedef int (*em_input_poll_func_t)(void);

/**
 * 'Periodical' draining of output from EM, if needed.
 *
 * User provided function - EM calls this, if not NULL, in the dispatch loop on
 * each core - set via 'em_conf.output.output_drain_fn'
 *
 * Draining of output events/pkts: EM will every once in a while call this
 * user provided function to ensure that low rate buffered output is eventually
 * sent out. Not needed if your EM output queues (EM_QUEUE_TYPE_OUTPUT) always
 * sends all events out. Useful in situations where output is buffered and sent
 * out in bursts when enough output has been gathered - single events or low
 * rate flows may, without this function, never be sent out (or too late) if the
 * buffering threshold has not been reached.
 *
 * @return number of events successfully drained and sent for output
 */
typedef int (*em_output_drain_func_t)(void);

/**
 * Output function, user provided callback for queues of type
 * EM_QUEUE_TYPE_OUTPUT.
 *
 * This function will be called by em_send*() when sending to a queue of type
 * EM_QUEUE_TYPE_OUTPUT and EM will take care of correct function calling order
 * based on the scheduling context type.
 * The function can use em_sched_context_type_current() if it needs information
 * about e.g. ordering requirements set by the parent scheduled queue.
 *
 * @param events        List of events to be sent out (ptr to array of events)
 * @param num           Number of events (positive integer)
 * @param output_queue  Output queue that the events were sent to (em_send*())
 * @param flags         Output flags/options to indicate e.g. ordering
 *                      requirement of the source context.
 *
 * @return number of events successfully sent (equal to num if all successful)
 */
typedef int (*em_output_func_t)(em_event_t *const events,
				const unsigned int num,
				const em_queue_t output_queue,
				void *output_fn_args);

/**
 * Platform specific output queue conf, replace for your platform.
 * Given to em_queue_create(type=EM_QUEUE_TYPE_OUTPUT) as em_queue_conf_t::conf
 */
typedef struct {
	/**
	 * User provided function for sending events out. This function will be
	 * called by em_send*() when sending to a queue of type
	 * EM_QUEUE_TYPE_OUTPUT
	 */
	em_output_func_t output_fn;
	/**
	 * Size of the argument-data passed via 'output_fn_args'.
	 * 'output_fn_args' is ignored, if 'args_len' is 0.
	 **/
	size_t args_len;
	/**
	 * Extra output-function argument that will be passed.
	 */
	void *output_fn_args;
} em_output_queue_conf_t;

/**
 * API-callback hook for em_alloc().
 *
 * The hook will only be called for successful allocs, passing also the
 * newly allocated 'event' to the hook.
 * The state and ownership of the event must not be changed by the hook, e.g.
 * the event must not be freed or sent etc. Calling em_alloc() within the
 * alloc hook leads to hook recursion and must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * @see
 */
typedef void (*em_api_hook_alloc_t)(em_event_t event, size_t size,
				    em_event_type_t type, em_pool_t pool);

/**
 * API-callback hook for em_free().
 *
 * The hook will be called before freeing the actual event, after verifying that
 * the event given to em_free() is valid, thus the hook does not 'see' if the
 * actual free-operation succeeds or fails.
 * The state and ownership of the event must not be changed by the hook, e.g.
 * the event must not be freed or sent etc. Calling em_free() within the
 * free hook leads to hook recursion and must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * @see
 */
typedef void (*em_api_hook_free_t)(em_event_t event);

/**
 * API-callback hook for em_send(), em_send_multi(), em_send_group() and
 * em_send_group_multi().
 *
 * Sending multiple events with an event group is the most generic
 * variant and thus one callback covers all.
 * The hook will be called just before sending the actual event(s), thus
 * the hook does not 'see' if the actual send operation succeeds or
 * fails.
 * The state and ownership of the events must not be changed by the
 * hook, e.g. the events can not be freed or sent etc.
 * Calling em_send...() within the send hook leads to hook recursion and
 * must be avoided.
 *
 * API-callback hook functions can be called concurrently from different cores.
 *
 * @see
 */
typedef void (*em_api_hook_send_t)(em_event_t *const events, int num,
				   em_queue_t queue,
				   em_event_group_t event_group);

/**
 * API-callback hooks provided by the user at start-up (init)
 *
 * EM API functions will call an API hook if given by the user through this
 * struct to em_init(). E.g. em_alloc() will call api_hooks->alloc(...) if
 * api_hooks->alloc != NULL. Not all hooks need to be provided, use NULL for
 * unsused hooks.
 *
 * @note Not all EM API funcs have associated hooks, only the most used
 *       functions (in the fast path) are included.
 *       Notice that extensive usage or heavy processing in the hooks might
 *       significantly impact performance since each API call (that has a hook)
 *       will execute the extra code in the user provided hook.
 *
 * @note Only used if EM_API_HOOKS_ENABLE != 0
 */
typedef struct {
	/**
	 * API callback hook for em_alloc().
	 * Initialize to NULL if unused.
	 */
	em_api_hook_alloc_t alloc_hook;

	/**
	 * API callback hook for em_free().
	 * Initialize to NULL if unused.
	 */
	em_api_hook_free_t free_hook;

	/**
	 * API callback hook used for _all_ send-variants:
	 * em_send(), em_send_multi(), em_send_group() and em_send_group_multi()
	 * Initialize to NULL if unused.
	 */
	em_api_hook_send_t send_hook;
} em_api_hooks_t;

/**
 * Event Machine run-time configuration options given at startup to em_init()
 *
 * Content is copied into EM.
 *
 * @note Several EM options are configured through compile-time defines.
 *       Run-time options allow using the same EM-lib with different configs.
 *
 * @see em_init()
 */
typedef struct {
	/**
	 * EM device id - use different device ids for each EM instance or
	 * remote EM device that need to communicate with each other.
	 */
	uint16_t device_id;

	/** Event Timer: enable=1, disable=0 */
	int event_timer;

	/** RunMode: EM run with one process per core */
	int process_per_core;

	/** RunMode: EM run with one thread per core */
	int thread_per_core;

	/** Number of EM-cores (== number of EM-threads or EM-processes) */
	int core_count;

	/** Physical core mask */
	em_core_mask_t phys_mask;

	/** Pool configuration for the EM default pool (EM_POOL_DEFAULT) */
	em_pool_cfg_t default_pool_cfg;

	/** EM log functions - both log_fn AND vlog_fn MUST be set if used! */
	struct {
		/** EM log function, user overridable, variable number of args*/
		em_log_func_t log_fn;
		/** EM log function, user overridable, va_list */
		em_vlog_func_t vlog_fn;
	} log;

	/** EM event/pkt input related functions and config */
	struct {
		/**
		 * User provided function for polling various input sources for
		 * events/pkts and enqueue into EM.
		 * Set to 'NULL' if not needed.
		 */
		em_input_poll_func_t input_poll_fn;
	} input;

	/** EM event/pkt output related functions and config */
	struct {
		/**
		 * User provided function for 'periodical' draining of buffered
		 * output - make sure buffered output events/pkts are eventually
		 * sent out even if the rate is low.
		 * Set to 'NULL' if not needed.
		 */
		em_output_drain_func_t output_drain_fn;
	} output;

	/**
	 * User provided API callback hooks.
	 * Set only the needed hooks to avoid performance degradation.
	 * Only used if EM_API_HOOKS_ENABLE != 0
	 */
	em_api_hooks_t api_hooks;

	/* Add further as needed. */
} em_conf_t;

/**
 * @def EM_ERROR_FATAL_MASK
 * Fatal error mask
 */
#define EM_ERROR_FATAL_MASK  0x80000000
/**
 * @def EM_ERROR_IS_FATAL
 * Test if error is fatal
 */
#define EM_ERROR_IS_FATAL(error)  (!!(EM_ERROR_FATAL_MASK & (error)))
/**
 * @def EM_ERROR_SET_FATAL
 * Set a fatal error code
 */
#define EM_ERROR_SET_FATAL(error) (EM_ERROR_FATAL_MASK | (error))
/* Alias, shorter name, backwards compatible */
#define EM_FATAL(error)  EM_ERROR_SET_FATAL((error))

/**
 * @def EM_ESCOPE_INTERNAL_TYPE
 * EM Internal (non-public API) functions error scope
 *
 * @see EM_ESCOPE_API_TYPE and EM_ESCOPE_API_MASK used by the public EM API.
 */
#define EM_ESCOPE_INTERNAL_TYPE     (0xFEu)
/**
 * @def EM_ESCOPE_INTERNAL_MASK
 * EM Internal (non-public API) functions error mask
 *
 * @see EM_ESCOPE_API_TYPE and EM_ESCOPE_API_MASK used by the public EM API.
 */
#define EM_ESCOPE_INTERNAL_MASK     (EM_ESCOPE_BIT | \
				    (EM_ESCOPE_INTERNAL_TYPE << 24))
/**
 * @def EM_ESCOPE_INTERNAL
 * Test if the error scope identifies an EM Internal function
 */
#define EM_ESCOPE_INTERNAL(escope)  (((escope) & EM_ESCOPE_MASK) \
				     == EM_ESCOPE_INTERNAL_MASK)

/**
 * @def EM_ESCOPE_INIT
 * EM internal escope: initialize the Event Machine
 */
#define EM_ESCOPE_INIT                       (EM_ESCOPE_INTERNAL_MASK | 0x0001)
/**
 * @def EM_ESCOPE_INIT_CORE
 * EM internal escope: initialize an Event Machine core
 */
#define EM_ESCOPE_INIT_CORE                  (EM_ESCOPE_INTERNAL_MASK | 0x0002)
/**
 * @def EM_ESCOPE_TERM
 * EM internal escope: terminate the Event Machine
 */
#define EM_ESCOPE_TERM                       (EM_ESCOPE_INTERNAL_MASK | 0x0003)
/**
 * @def EM_ESCOPE_TERM_CORE
 * EM internal escope: terminate an Event Machine core
 */
#define EM_ESCOPE_TERM_CORE                  (EM_ESCOPE_INTERNAL_MASK | 0x0004)

/**
 * @def EM_ESCOPE_POOL_CREATE
 * EM internal escope: create an event pool
 */
#define EM_ESCOPE_POOL_CREATE                (EM_ESCOPE_INTERNAL_MASK | 0x0101)
/**
 * @def EM_ESCOPE_POOL_DELETE
 * EM internal escope: delete an event pool
 */
#define EM_ESCOPE_POOL_DELETE                (EM_ESCOPE_INTERNAL_MASK | 0x0102)
/**
 * @def EM_ESCOPE_POOL_FIND
 * EM internal escope: find an event pool by name
 */
#define EM_ESCOPE_POOL_FIND                  (EM_ESCOPE_INTERNAL_MASK | 0x0103)
/**
 * @def EM_ESCOPE_POOL_GET_NAME
 * EM internal escope: get an event pool name
 */
#define EM_ESCOPE_POOL_GET_NAME              (EM_ESCOPE_INTERNAL_MASK | 0x0104)
/**
 * @def EM_ESCOPE_POOL_GET_FIRST
 * EM internal escope: event pool iteration - get first of iteration
 */
#define EM_ESCOPE_POOL_GET_FIRST             (EM_ESCOPE_INTERNAL_MASK | 0x0105)
/**
 * @def EM_ESCOPE_POOL_GET_NEXT
 * EM internal escope: event pool iteration - get next of iteration
 */
#define EM_ESCOPE_POOL_GET_NEXT              (EM_ESCOPE_INTERNAL_MASK | 0x0106)
/**
 * @def EM_ESCOPE_POOL_INFO
 * EM internal escope: event pool info & statistics
 */
#define EM_ESCOPE_POOL_INFO                  (EM_ESCOPE_INTERNAL_MASK | 0x0107)
/**
 * @def EM_ESCOPE_HOOKS_REGISTER_ALLOC
 * EM internal escope: register API callback hook for em_alloc()
 */
#define EM_ESCOPE_HOOKS_REGISTER_ALLOC       (EM_ESCOPE_INTERNAL_MASK | 0x0201)
/**
 * @def EM_ESCOPE_HOOKS_UNREGISTER_ALLOC
 * EM internal escope: unregister API callback hook for em_alloc()
 */
#define EM_ESCOPE_HOOKS_UNREGISTER_ALLOC     (EM_ESCOPE_INTERNAL_MASK | 0x0202)
/**
 * @def EM_ESCOPE_HOOKS_REGISTER_FREE
 * EM internal escope: register API callback hook for em_free()
 */
#define EM_ESCOPE_HOOKS_REGISTER_FREE        (EM_ESCOPE_INTERNAL_MASK | 0x0203)
/**
 * @def EM_ESCOPE_HOOKS_UNREGISTER_FREE
 * EM internal escope: unregister API callback hook for em_free()
 */
#define EM_ESCOPE_HOOKS_UNREGISTER_FREE      (EM_ESCOPE_INTERNAL_MASK | 0x0204)
/**
 * @def EM_ESCOPE_HOOKS_REGISTER_SEND
 * EM internal escope: register API callback hook for em_send-variants
 */
#define EM_ESCOPE_HOOKS_REGISTER_SEND        (EM_ESCOPE_INTERNAL_MASK | 0x0205)
/**
 * @def EM_ESCOPE_HOOKS_UNREGISTER_SEND
 * EM internal escope: unregister API callback hook for em_send-variants
 */
#define EM_ESCOPE_HOOKS_UNREGISTER_SEND      (EM_ESCOPE_INTERNAL_MASK | 0x0206)
/**
 * @def EM_ESCOPE_EVENT_SEND_DEVICE
 * EM internal escope: send event to another device
 */
#define EM_ESCOPE_EVENT_SEND_DEVICE          (EM_ESCOPE_INTERNAL_MASK | 0x0301)
/**
 * @def EM_ESCOPE_EVENT_SEND_DEVICE_MULTI
 * EM internal escope: send event(s) to another device
 */
#define EM_ESCOPE_EVENT_SEND_DEVICE_MULTI    (EM_ESCOPE_INTERNAL_MASK | 0x0302)

/**
 * @def EM_ESCOPE_DAEMON
 * EM internal escope: EO Daemon
 */
#define EM_ESCOPE_DAEMON                     (EM_ESCOPE_INTERNAL_MASK | 0x0401)

/**
 * @def EM_ESCOPE_EVENT_GROUP_UPDATE
 * EM internal esope: Update the event group count
 */
#define EM_ESCOPE_EVENT_GROUP_UPDATE         (EM_ESCOPE_INTERNAL_MASK | 0x0501)

/* EM internal escopes: Queue */
#define EM_ESCOPE_QUEUE_ENABLE               (EM_ESCOPE_INTERNAL_MASK | 0x0601)
#define EM_ESCOPE_QUEUE_ENABLE_ALL           (EM_ESCOPE_INTERNAL_MASK | 0x0602)
#define EM_ESCOPE_QUEUE_DISABLE              (EM_ESCOPE_INTERNAL_MASK | 0x0603)
#define EM_ESCOPE_QUEUE_DISABLE_ALL          (EM_ESCOPE_INTERNAL_MASK | 0x0604)
#define EM_ESCOPE_QUEUE_STATE_CHANGE         (EM_ESCOPE_INTERNAL_MASK | 0x0605)

/* EM internal escopes: Queue Groups */
#define EM_ESCOPE_QUEUE_GROUP_INIT           (EM_ESCOPE_INTERNAL_MASK | 0x0701)
#define EM_ESCOPE_QUEUE_GROUP_INIT_LOCAL     (EM_ESCOPE_INTERNAL_MASK | 0x0702)
#define EM_ESCOPE_QUEUE_GROUP_DEFAULT        (EM_ESCOPE_INTERNAL_MASK | 0x0703)

/* Other internal escopes */
#define EM_ESCOPE_EO_START_DONE_CB           (EM_ESCOPE_INTERNAL_MASK | 0x0801)
#define EM_ESCOPE_EO_START_SYNC_DONE_CB      (EM_ESCOPE_INTERNAL_MASK | 0x0802)
#define EM_ESCOPE_EO_STOP_DONE_CB            (EM_ESCOPE_INTERNAL_MASK | 0x0803)
#define EM_ESCOPE_EO_STOP_SYNC_DONE_CB       (EM_ESCOPE_INTERNAL_MASK | 0x0804)
#define EM_ESCOPE_EO_REMOVE_QUEUE_DONE_CB    (EM_ESCOPE_INTERNAL_MASK | 0x0805)
#define EM_ESCOPE_EO_REMOVE_QUEUE_SYNC_DONE_CB       (EM_ESCOPE_INTERNAL_MASK |\
									0x0806)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL_DONE_CB        (EM_ESCOPE_INTERNAL_MASK |\
									0x0807)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC_DONE_CB   (EM_ESCOPE_INTERNAL_MASK |\
									0x0808)
#define EM_ESCOPE_EO_LOCAL_FUNC_CALL_REQ     (EM_ESCOPE_INTERNAL_MASK | 0x0809)
#define EM_ESCOPE_INTERNAL_NOTIF             (EM_ESCOPE_INTERNAL_MASK | 0x080A)
#define EM_ESCOPE_INTERNAL_EVENT_RECV_FUNC   (EM_ESCOPE_INTERNAL_MASK | 0x080B)
#define EM_ESCOPE_EVENT_INTERNAL_DONE        (EM_ESCOPE_INTERNAL_MASK | 0x080C)
#define EM_ESCOPE_EVENT_INTERNAL_LFUNC_CALL  (EM_ESCOPE_INTERNAL_MASK | 0x080D)
#define EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ  (EM_ESCOPE_INTERNAL_MASK | 0x080E)

/* EM internal escopes: Event */
#define EM_ESCOPE_EVENT_FREE_MULTI           (EM_ESCOPE_INTERNAL_MASK | 0x0901)

/**
 * @def EM_ESCOPE_ODP_EXT
 * EM ODP extensions error scope
 */
#define EM_ESCOPE_ODP_EXT                    (EM_ESCOPE_INTERNAL_MASK | 0x1000)

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_HW_TYPES_H */
