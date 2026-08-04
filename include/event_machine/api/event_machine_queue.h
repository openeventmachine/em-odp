/*
 *   Copyright (c) 2015-2026, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_QUEUE_H_
#define EVENT_MACHINE_QUEUE_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_queue Queues
 *  Operations on queues
 *  @{
 *
 * Queues are the communication mechanism used by EM. Each queue is associated
 * with one Execution Object (EO) (or HW functionality), but each EO can have
 * multiple queues.
 *
 * A queue can have one of seven (7) different scheduling modes / queue types:
 *
 * @anchor atomic_queue
 * -# EM_QUEUE_TYPE_ATOMIC
 *   - The atomic queue type limits event scheduling to one event at a time from
 *     the queue. The next event from the same queue can only be scheduled after
 *     the EO returns from processing the earlier event or calls
 *     em_atomic_processing_end() to signal end of atomic processing.
 *     This type is useful to avoid multicore race conditions as only one core
 *     at a time can be working on an event from an atomic queue.
 *     Additionally, the ingress-egress event order is maintained.
 *
 * @anchor parallel_queue
 * -# EM_QUEUE_TYPE_PARALLEL
 *   - Parallel queues have no restriction for scheduling, which means that any
 *     amount of events (up to the number of cores in the queue group of the
 *     queue) can be processed simultaneously. This provides the best scaling,
 *     but race conditions need to be avoided and handled by the application.
 *
 * @anchor ordered_queue
 * -# EM_QUEUE_TYPE_ORDERED
 *   - Ordered queues are scheduled like parallel queues — multiple events can
 *     be processed concurrently — but the system preserves egress ordering.
 *     This enables high single-flow throughput without requiring the
 *     application to synchronize for ordering between threads.
 *
 *     Ordering context: When the scheduler dequeues an event (or burst) from
 *     an ordered queue, the receiving thread is associated with an ordering
 *     context that represents the event's sequence position in the queue.
 *     Contexts originating from the same ordered queue keep the mutual order
 *     of their corresponding events. The context is valid during the EO
 *     receive function and ends when the function returns, or earlier if
 *     em_ordered_processing_end() is called.
 *
 *     Sending under an ordering context: Any event sent during the context
 *     inherits its sequence position, including events not received from the
 *     ordered queue (e.g. newly allocated or previously stored). The first
 *     event sent occupies the position of the received event; subsequent
 *     sends occupy the consecutive positions after it. The received event
 *     itself does not need to be forwarded; if no event is sent at all, the
 *     position is implicitly skipped.
 *
 *     Destination queues: Events may be sent to any number of destination
 *     queues of any type. Ordering is enforced per destination queue (not
 *     between different destinations) and follows two rules:
 *       - Events sent by the same thread arrive in send-call order.
 *       - Events sent from different ordering contexts arrive in the order
 *         of those contexts.
 *     A chain of ordered (or atomic) queues preserves ordering end-to-end.
 *
 *     Note that the guarantee covers enqueue, not processing: a non-atomic
 *     destination queue served by a multi-core EO may still process events
 *     out of order.
 *
 *     Compared to atomic queues, which also maintain order, ordered queues
 *     achieve higher throughput by allowing multiple cores to process events
 *     in parallel.
 *
 * @anchor unscheduled_queue
 * -# EM_QUEUE_TYPE_UNSCHEDULED
 *   - Unscheduled queues are not connected to the scheduler but instead the
 *     application needs to dequeue events directly using em_queue_dequeue().
 *     The API function em_send() is used to enqueue events into an unscheduled
 *     queue. All queue types look the same to the sender with the exception
 *     that em_send_group() is not supported for unscheduled queues.
 *     Unscheduled queues cannot be added to an EO.
 *
 * @anchor aggregator_queue
 * -# EM_QUEUE_TYPE_AGGR
 *   - An aggregator queue is a child queue of a scheduled or unscheduled
 *     parent queue. Aggregator queues cannot be created/deleted directly
 *     but are created/deleted as part of the parent queue creation/deletion
 *     (see em_queue_param_t::num_aggr and em_queue_param_t::aggr_conf).
 *     Events sent to an aggregator queue may be bundled into vector events
 *     before being enqueued to the parent queue for delivery.
 *     Events can also be sent directly to the parent queue bypassing the
 *     aggregation.
 *     For unscheduled queues: the application must not dequeue from the
 *     aggregator queues directly, instead they should dequeue from the
 *     parent queue.
 *     For scheduled queues: the queue handle given to the EO receive
 *     function always corresponds to the parent scheduled queue, the
 *     aggregator queue handle is never passed.
 *     The application can send events to both the parent queue and the
 *     aggregator queues, depending on desired (vectorized/non-vectorized)
 *     behaviour.
 *     @note A vector event (major type EM_EVENT_TYPE_VECTOR) must never be
 *           sent to an aggregator queue - no vectors within vectors. A vector
 *           event may still be sent directly to the parent queue of the
 *           aggregators.
 *     @note Sending two events to separate aggregator queues of the same
 *           parent queue will not retain event order even if the parent
 *           queue would guarantee ordering.
 *
 * @anchor local_queue
 * -# EM_QUEUE_TYPE_LOCAL
 *   - Local queues are special virtual queues bypassing the scheduler for
 *     fast core-local pipelining without load balancing or atomic processing.
 *     A local queue is connected (added) to an EO in the same way scheduled
 *     queues are. Events sent to a local queue are added to a per core (local)
 *     storage maintained by the EM dispatcher. This core local event storage is
 *     emptied by the dispatcher after the sending EO returns from the receive
 *     function. The local events are now immediately dispatched on the current
 *     core, i.e. handed to the receive function of the EO that owns the
 *     targeted local queue. Only when all local events have been handled is the
 *     scheduler allowed to schedule new events for the core.
 *     Local queues do not have an explicit ordered or atomic processing
 *     context, instead they inherit the context of the EO under which the event
 *     was sent (i.e. ordering could still be maintained with careful design).
 *     The sending EO's processing context is only released after the local
 *     queue is empty, unless the application explicitly ends the context
 *     earlier, thus effectively making local processing similar to handling
 *     the same function within the sending EO's receive.
 *     A local queue is not associated with a queue group and exists on all
 *     cores of the EM instance - the application must be able to handle
 *     events on all cores (unless sending to the local queue is controlled).
 *
 *     The local queue concept is a performance optimization and a way to
 *     logically split processing into separate EO's but due to the side
 *     effects (may delay context release of the sending EO) and limitations
 *     should not be used without a valid reason. Local queues are mainly
 *     suitable for stateless processing that does not need EM scheduling.
 *
 * @anchor output_queue
 * -# EM_QUEUE_TYPE_OUTPUT
 *   - An output queue is a system specific implementation of a SW-HW interface.
 *     It provides a queue interface for sending events out of EM to a HW
 *     device. It could e.g. be used for packet output or towards HW
 *     accelerators. The application uses em_send() to transmit an event for
 *     output. Typically the needed information to bind a queue to an interface
 *     is provided via the optional conf-argument given during queue creation
 *     (the content of 'conf' is system specific).
 *
 * Currently EM does not define the exact queue behavior except that queues
 * work like FIFOs. This means, e.g. that the maximum length of a queue is
 * system specific (the conf parameter of queue create can be used to provide
 * options)
 *
 * Special queues towards asynchronous HW functions, e.g. a crypto accelerator,
 * should look like any regular queue from the sender's point of view, i.e.
 * em_send() and related functions work.
 */

#include <event_machine/api/event_machine_types.h>
#include <event_machine/api/event_machine_error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef em_queue_type_t
 * Queue type.
 *
 * Affects the scheduling principle
 *
 * @see em_queue_type_e, em_queue_create()
 */
typedef uint32_t em_queue_type_t;
/** em_queue_type_t printf format */
#define PRI_QTYPE  PRIu32

/**
 * Queue types used by em_queue_type_t
 */
typedef enum em_queue_type_e {
	/** Undefined */
	EM_QUEUE_TYPE_UNDEF = 0,

	/**
	 * Atomic queue.
	 * The application receives events one by one, non-concurrently to
	 * guarantee exclusive processing and ordering
	 */
	EM_QUEUE_TYPE_ATOMIC = 1,

	/**
	 * Parallel queue.
	 * The application may receive events fully concurrently, egress event
	 * ordering (when processed in parallel) not guaranteed
	 */
	EM_QUEUE_TYPE_PARALLEL = 2,

	/**
	 * Ordered parallel queue.
	 * The application may receive events concurrently, but the system takes
	 * care of egress order (between two queues)
	 */
	EM_QUEUE_TYPE_ORDERED = 3,
	/** backwards compatible alias */
	EM_QUEUE_TYPE_PARALLEL_ORDERED = EM_QUEUE_TYPE_ORDERED,

	/**
	 * A queue which is not connected to scheduling. The application needs
	 * to explicitly dequeue events
	 */
	EM_QUEUE_TYPE_UNSCHEDULED = 4,

	/**
	 * Aggregator queue.
	 * An aggregator queue is a child queue of a scheduled or unscheduled
	 * parent queue.
	 * Events sent to an aggregator queue are (possibly) bundled into vector
	 * events before being enqueued to the parent queue for delivery.
	 * @see @ref aggregator_queue "Aggregator queues"
	 */
	EM_QUEUE_TYPE_AGGR = 5,

	/**
	 * A queue type for local virtual queue not connected to scheduling.
	 */
	EM_QUEUE_TYPE_LOCAL = 6,
	/**
	 * A system specific queue type to abstract output from EM,
	 * e.g. packet output or output towards a HW accelerator.
	 * The application uses em_send() and variants to send an event 'out'.
	 */
	EM_QUEUE_TYPE_OUTPUT = 7
} em_queue_type_e;

/**
 * @typedef em_queue_prio_t
 * Queue priority
 *
 * Queue priority defines implementation specific QoS class for event
 * scheduling. Priority is an integer in range 0 (lowest) to num priorities - 1.
 * Note, that the exact scheduling rules are not defined by EM and all available
 * priorities may not be relative to the adjacent one (e.g. using dynamic
 * priority, rate limiting or other more complex scheduling discipline).
 * There are 5 generic predefined values (em_queue_prio_e) mapped to available
 * runtime priorities for portability.
 *
 * @see em_queue_create(), em_queue_num_prio(), em_queue_prio_e
 */
typedef uint32_t em_queue_prio_t;
#define PRI_QPRIO  PRIu32

/**
 * Portable queue priorities.
 *
 * These are generic portable values to use for priority with em_queue_prio_t.
 *
 * Alternatively application may choose to use numeric values in the valid
 * range (from 0 to em_queue_num_prio() - 1).
 *
 * @see em_queue_prio_t, em_queue_num_prio()
 */
typedef enum em_queue_prio_e {
	EM_QUEUE_PRIO_LOWEST  = 0,    /**< Lowest */
	EM_QUEUE_PRIO_LOW     = 2,    /**< Low */
	EM_QUEUE_PRIO_NORMAL  = 4,    /**< Normal */
	EM_QUEUE_PRIO_HIGH    = 6,    /**< High */
	EM_QUEUE_PRIO_HIGHEST = 7     /**< Highest */
} em_queue_prio_e;

#define EM_QUEUE_PRIO_UNDEF 0xFF      /**< Undefined */

/**
 * Queue flags
 *
 * An unsigned integer with defined queue flags that can be combined by
 * bitwise 'OR' only. EM_QUEUE_FLAG_DEFAULT can be used in most cases.
 * Unused bits must be set to zero. The actual values are system specific, but
 * the implementation need to define at least: EM_QUEUE_FLAG_DEFAULT,
 * EM_QUEUE_FLAG_BLOCKING, EM_QUEUE_FLAG_NONBLOCKING_LF and
 * EM_QUEUE_FLAG_NONBLOCKING_WF even if those would not be supported.
 */
typedef uint32_t em_queue_flag_t;

/**
 * @def EM_QUEUE_FLAG_MASK (em_queue_flag_t mask)
 * The low 16 bits are reserved for EM, the upper bits are free
 * for system-specific use.
 */
#define EM_QUEUE_FLAG_MASK 0x0000FFFF

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
 *
 * em_queue_flag_t value. Only combine flags with bitwise OR.
 *
 * Blocking queue implementation. A suspending thread may block all other
 * threads, i.e. no block freedom guarantees.
 * Implied by EM_QUEUE_FLAG_DEFAULT for the implementation on this system.
 */
#define EM_QUEUE_FLAG_BLOCKING  0 /* blocking, fastest (default) */

/**
 * @def EM_QUEUE_FLAG_NONBLOCKING_LF
 *
 * em_queue_flag_t value. Only combine flags with bitwise OR.
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
 * em_queue_flag_t value. Only combine flags with bitwise OR.
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
 * em_queue_flag_t value. Only combine flags with bitwise OR.
 *
 * Default multithread safe enqueue implementation not needed, the application
 * guarantees there is no concurrent accesses in enqueue, i.e. em_send().
 * This can only be used with unscheduled queues and can potentially improve
 * performance. The implementation may choose to ignore this flag.
 * Use with care.
 */
#define EM_QUEUE_FLAG_ENQ_NOT_MTSAFE  4

/**
 * @def EM_QUEUE_FLAG_DEQ_NOT_MTSAFE
 *
 * em_queue_flag_t value. Only combine flags with bitwise OR.
 *
 * Default multithread safe dequeue implementation not needed, the application
 * guarantees there is no concurrent accesses in dequeue, i.e.
 * em_queue_dequeue(). This can only be used with unscheduled queues and can
 * potentially improve performance. The implementation may choose to ignore this
 * flag. Use with care.
 */
#define EM_QUEUE_FLAG_DEQ_NOT_MTSAFE  8

/**
 * Queue configuration data for queue-create APIs. The use of this conf is
 * optional, but provides a standard way to pass extra parameters or specify
 * extra requirements.
 */
typedef struct {
	/**
	 * Extra flags. See em_queue_flag_t for choices.
	 * EM_QUEUE_FLAG_DEFAULT is defined by all systems and indicates a
	 * default multithread-safe queue without any special guarantees.
	 */
	em_queue_flag_t flags;
	/**
	 * Request for a minimum amount of events the queue can hold or use
	 * 0 for EM default value. Queue creation will fail, if the system
	 * cannot support the requested amount.
	 */
	unsigned int min_events;
	/**
	 * Size of the data passed via 'conf'. 'conf' is ignored,
	 * if 'conf_len' is 0.
	 */
	size_t conf_len;
	/**
	 * Extra queue configuration data. This can also work
	 * as a placeholder for directly attached extra data.
	 */
	void *conf;
} em_queue_conf_t;

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
 * @param events         List of events to be sent out (ptr to array of events)
 * @param num            Number of events (positive integer)
 * @param output_queue   Output queue that the events were sent to (em_send*())
 * @param output_fn_args Extra arguments to indicate e.g. ordering requirement
 *                       of the source context.
 *
 * @return number of events successfully sent (equal to num if all successful)
 */
typedef int (*em_output_func_t)(const em_event_t events[],
				const unsigned int num,
				const em_queue_t output_queue,
				void *output_fn_args);

/**
 * EM output queue configuration.
 * Given to em_queue_create(type=EM_QUEUE_TYPE_OUTPUT) as em_queue_conf_t::conf
 * or part of em_queue_param_t::output_conf when using em_queue_create_param().
 */
typedef struct {
	/**
	 * User provided function for sending events out. This function will be
	 * called by em_send*() when sending to a queue of type
	 * EM_QUEUE_TYPE_OUTPUT
	 */
	em_output_func_t output_fn;
	/**
	 * Extra output-function argument that will be passed.
	 */
	void *output_fn_args;
	/**
	 * Size of the argument-data passed via 'output_fn_args'.
	 * 'output_fn_args' is ignored, if 'args_len' is 0.
	 */
	size_t args_len;
} em_output_queue_conf_t;

/**
 * Create a new queue with a dynamic queue handle (i.e. handle given by EM)
 *
 * The given name string is copied into an EM internal data structure. The
 * maximum string length is EM_QUEUE_NAME_LEN.
 *
 * Create scheduled atomic, parallel or ordered queues by using the types
 * EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_PARALLEL or EM_QUEUE_TYPE_ORDERED.
 *
 * To create an unscheduled queue, use the type EM_QUEUE_TYPE_UNSCHEDULED.
 * The prio and queue group are not relevant, but need to be set to
 * EM_QUEUE_PRIO_UNDEF and EM_QUEUE_GROUP_UNDEF. Unscheduled queues can't be
 * associated with an EO (em_eo_add_queue() fails).
 *
 * To create a local queue, use type EM_QUEUE_TYPE_LOCAL. The queue group is not
 * relevant and must be set to EM_QUEUE_GROUP_UNDEF. The virtual local queue
 * is created for all cores in this EM instance. Note also that the
 * implementation may not implement priorities for local queues.
 *
 * To create an output queue, use the type EM_QUEUE_TYPE_OUTPUT.
 * Pass the needed information to bind a queue with an interface via the
 * conf-argument (content is system and output-type specific).
 * The queue group is not relevant and must be set to EM_QUEUE_GROUP_UNDEF.
 * Note also that the implementation may not implement priorities for output
 * queues.
 *
 * The 'conf' argument is optional and can be used to pass extra attributes
 * (e.g. require non-blocking behaviour, if supported) to the system specific
 * implementation.
 *
 * @param name          Queue name (optional, NULL ok)
 * @param type          Queue type
 * @param prio          Queue priority class
 * @param group         Queue group for this queue
 * @param conf          Optional configuration data, NULL for defaults
 *
 * @return New queue handle or EM_QUEUE_UNDEF on an error.
 *
 * @see em_queue_group_create(), em_queue_delete(), em_queue_conf_t
 */
em_queue_t
em_queue_create(const char *name, em_queue_type_t type, em_queue_prio_t prio,
		em_queue_group_t group, const em_queue_conf_t *conf);

/**
 * Create a new queue with a static queue handle (i.e. given by the user).
 *
 * Note that the number of static queues is provided by the user through
 * 'queue.num_static' in em-odp.conf at runtime. A valid static queue handle
 * can be derived as follows:
 * @code
 *	em_status_t status;
 *	em_queue_t static_queue = em_queue_static_handle(x);
 *	status = em_queue_create_static(NULL, EM_QUEUE_TYPE_ATOMIC,
 *					EM_QUEUE_PRIO_NORMAL,
 *					EM_QUEUE_GROUP_DEFAULT,
 *					static_queue, conf);
 * @endcode
 *
 * The 'x' in above pseudocode must be in range 0 to 'queue.num_static' - 1.
 *
 * Otherwise like em_queue_create().
 *
 * @param name          Queue name (optional, NULL ok)
 * @param type          Queue scheduling type
 * @param prio          Queue priority
 * @param group         Queue group for this queue
 * @param queue         Requested queue handle from the static range
 * @param conf          Optional configuration data, NULL for defaults
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_create(), em_queue_static_handle()
 */
em_status_t
em_queue_create_static(const char *name, em_queue_type_t type,
		       em_queue_prio_t prio, em_queue_group_t group,
		       em_queue_t queue, const em_queue_conf_t *conf);

/**
 * Aggregator queue configuration parameters.
 *
 * Aggregator queues try to bundle multiple events into vector events
 * before enqueuing the events or vector events to the parent scheduled
 * or unscheduled queue.
 *
 * Always initialize with em_queue_aggr_conf_init() before setting fields
 * to ensure forward compatibility with potentially added new options.
 *
 * @see em_queue_aggr_conf_init()
 */
typedef struct {
	/**
	 * Event vector pool
	 *
	 * Pool from which to allocate event vectors when aggregating events.
	 * The pool must have been created with the EM_EVENT_TYPE_VECTOR type.
	 *
	 * EM pins the aggregator to the first subpool whose
	 * 'size' >= em_queue_aggr_conf_t::max_size at queue creation time.
	 * All vector events for the aggregator come from that one subpool.
	 * Different aggregators can target different subpools by using
	 * different 'max_size' values.
	 *
	 * Set to EM_POOL_UNDEF by em_queue_aggr_conf_init().
	 */
	em_pool_t pool;

	/**
	 * Maximum time to wait for events to fill a vector
	 *
	 * Maximum time in nanoseconds for event aggregation to form an
	 * event vector.
	 * A value of zero (0) means there is no timeout and events may await
	 * aggregation indefinitely.
	 * Set to 0 by em_queue_aggr_conf_init().
	 */
	uint64_t max_tmo_ns;

	/**
	 * Maximum number of events in vector
	 *
	 * Event aggregation forms an event vector event after '.max_size'
	 * events have been collected or '.max_tmo_ns' has passed.
	 *
	 * '.max_size' also determines which subpool of the vector pool
	 * ('.pool') is used: EM selects the first subpool whose
	 * em_pool_cfg_t::subpool[x].size >= '.max_size' at queue creation
	 * time, and all vector events for this aggregator are allocated from
	 * that subpool (see em_pool_cfg_t::subpool[].size for details).
	 *
	 * Must be non-zero; the default value of 0 set by
	 * em_queue_aggr_conf_init() is rejected at queue creation time.
	 */
	uint32_t max_size;

	/**
	 * Event type eligible for aggregation into vector events
	 *
	 * Only events of this type are collected into the vector.
	 * Use EM_EVENT_TYPE_ANY to allow all event types except event vectors.
	 *
	 * Regardless of 'event_type', a vector event (major type
	 * EM_EVENT_TYPE_VECTOR) must never be sent to an aggregator queue
	 * (no vectors within vectors), see @ref aggregator_queue.
	 *
	 * Set to EM_EVENT_TYPE_UNDEF by em_queue_aggr_conf_init(); must be set
	 * to a valid type before calling em_queue_create_param().
	 */
	em_event_type_t event_type;

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_queue_aggr_conf_init(conf)
	 * has been called before use.
	 */
	uint32_t __internal_check;
} em_queue_aggr_conf_t;

/**
 * Initialize configuration parameters for aggregator queue(s).
 *
 * Initialize an array of em_queue_aggr_conf_t:s to default values.
 * Always call this before setting fields to ensure compatibility with
 * potentially added new options.
 * EM will enforce this and report an error if not done.
 *
 * @param num_aggr  Number of elements in aggr_conf[] (1..EM_QUEUE_MAX_AGGR)
 * @param aggr_conf Address of the em_queue_aggr_conf_t array to initialize
 *
 * @see em_queue_aggr_conf_t, em_queue_create_param()
 */
void em_queue_aggr_conf_init(uint32_t num_aggr,
			     em_queue_aggr_conf_t aggr_conf[/*num_aggr*/]);

/**
 * EM queue creation parameters.
 *
 * Used with em_queue_create_param() and is able to replace several
 * queue-create APIs including:
 * em_queue_create(), em_queue_create_static(), em_queue_create_ag() and
 * em_queue_create_static_ag().
 *
 * @note em_queue_param_init() must be called to initialize the parameters to
 *       default values before filling in the desired values and calling
 *       em_queue_create_param() to ensure backwards compatibility with
 *       potentially added new options. EM will enforce this and report an error
 *       if not done.
 *
 * Create scheduled atomic, parallel or ordered queues by using the '.type'
 * values EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_PARALLEL or EM_QUEUE_TYPE_ORDERED.
 *
 * To create an unscheduled queue, use the '.type' EM_QUEUE_TYPE_UNSCHEDULED.
 * The '.prio' and '.queue_group' are not relevant, but need to be set to
 * EM_QUEUE_PRIO_UNDEF and EM_QUEUE_GROUP_UNDEF. Unscheduled queues can't be
 * associated with an EO (em_eo_add_queue() fails).
 *
 * To create a local queue, use '.type' EM_QUEUE_TYPE_LOCAL. The '.queue_group'
 * is not relevant and must be set to EM_QUEUE_GROUP_UNDEF. The virtual local
 * queue is created for all cores in this EM instance. Note also that the
 * implementation may not implement (all) priorities for local queues.
 *
 * To create an output queue (i.e. output from EM), use the '.type'
 * EM_QUEUE_TYPE_OUTPUT.
 * Pass the needed information via the '.output_conf' field.
 * The '.queue_group' is not relevant and must be set to EM_QUEUE_GROUP_UNDEF.
 * Note also that the implementation may not implement priorities for output
 * queues.
 *
 * @code
 * em_queue_param_t param;
 * em_queue_aggr_conf_t aggr_conf[num_aggr];  (optional)
 *
 * em_queue_param_init(&param);
 * em_queue_aggr_conf_init(num_aggr, aggr_conf);
 *
 * param.type = EM_QUEUE_TYPE_ATOMIC;
 * ...
 *
 * for (i = 0; i < num_aggr; i++) {
 *     aggr_conf[i].pool = my_vector_pool;
 *     ...
 * }
 * param.num_aggr = num_aggr;
 * param.aggr_conf = aggr_conf;
 * ...
 * queue = em_queue_create_param("my-queue", &param);
 * @endcode
 */
typedef struct {
	/**
	 * Requested queue handle for static queues,
	 * use EM_QUEUE_UNDEF for dynamic queues.
	 *
	 * Dynamic queue: use EM_QUEUE_UNDEF and EM will allocate a queue handle
	 *                for the new queue (similar to em_queue_create/_ag()).
	 * Static queue:  use a valid free static queue handle (derived e.g.
	 *                with em_queue_static_handle()) for the new queue
	 *                (similar to em_queue_create_static/_ag()).
	 * Set to EM_QUEUE_UNDEF by em_queue_param_init(), which means
	 * a dynamic queue will be created unless changed by the user.
	 */
	em_queue_t queue;

	/**
	 * Queue type, mandatory.
	 * Set to EM_QUEUE_TYPE_UNDEF by em_queue_param_init(), but the user
	 * needs to change it to a valid type.
	 */
	em_queue_type_t type;

	/**
	 * Queue priority, mandatory for certain queue types.
	 * Set to EM_QUEUE_PRIO_UNDEF by em_queue_param_init(), but the user
	 * needs to change it to a valid priority, or EM_QUEUE_PRIO_UNDEF
	 * if not relevant for the queue type.
	 */
	em_queue_prio_t prio;

	/**
	 * Queue group for this queue, mandatory for certain queue types.
	 * Set to EM_QUEUE_GROUP_UNDEF by em_queue_param_init(), but the user
	 * needs to change it to a valid queue group, or EM_QUEUE_GROUP_UNDEF
	 * if not relevant for the queue type.
	 */
	em_queue_group_t queue_group;

	/**
	 * Atomic group for the queue, only use when wanting the new queue to be
	 * part of an existing atomic group (see the EM atomic group APIs).
	 * Note: can only be used when .type = EM_QUEUE_TYPE_ATOMIC.
	 * Set to EM_ATOMIC_GROUP_UNDEF by em_queue_param_init(), but the user
	 * needs to change it to a valid atomic group, or EM_ATOMIC_GROUP_UNDEF
	 * if not relevant for the queue.
	 */
	em_atomic_group_t atomic_group;

	/**
	 * Extra flags. See em_queue_flag_t for choices.
	 * EM_QUEUE_FLAG_DEFAULT indicates a default multithread-safe queue
	 * without any special guarantees.
	 * Set to EM_QUEUE_FLAG_DEFAULT by em_queue_param_init().
	 */
	em_queue_flag_t flags;

	/**
	 * Request for a minimum amount of events the queue can hold or use
	 * 0 for the EM default value. Queue creation will fail, if the system
	 * cannot support the requested amount.
	 * Set to 0 by em_queue_param_init().
	 */
	unsigned int min_events;

	/**
	 * EM output queue configuration.
	 * Only evaluated when .type = EM_QUEUE_TYPE_OUTPUT.
	 */
	em_output_queue_conf_t output_conf;

	/**
	 * Number of aggregator queues to create for this queue,
	 * use zero (0) for no aggregation.
	 *
	 * Aggregator queues try to aggregate multiple events into vector events
	 * before enqueuing to the 'parent' queue.
	 *
	 * Two events enqueued through different aggregators may appear in any
	 * order when dequeued.
	 *
	 * When >= 1, configuration must be provided for each aggregator
	 * through the 'aggr_conf' array.
	 * Must be smaller or equal to EM_QUEUE_MAX_AGGR.
	 *
	 * The default value is zero.
	 */
	uint32_t num_aggr;

	/** Aggregator queue configuration parameters
	 *
	 * When 'num_aggr' is non-zero, 'aggr_conf' must point to an array
	 * of size 'num_aggr'.
	 *
	 * The 'aggr_conf' array, if given, must be initialized with
	 * em_queue_aggr_conf_init(num_aggr, aggr_conf) before setting fields
	 * to ensure compatibility with potentially added new options.
	 *
	 * The default value is null.
	 */
	em_queue_aggr_conf_t *aggr_conf;

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_queue_param_init(param)
	 * has been called before use.
	 */
	uint32_t __internal_check;
} em_queue_param_t;

/**
 * Initialize parameters for EM queue creation.
 *
 * Initialize em_queue_param_t to default values for all fields.
 * After initialization, the user further needs to set the mandatory fields of
 * 'em_queue_param_t' before calling em_queue_create_param().
 * Always initialize 'param' first with em_queue_param_init(&param) to
 * ensure backwards compatibility with potentially added new options. EM will
 * enforce this initialization and report an error if not done.
 *
 * @param param  Address of the em_queue_param_t to be initialized
 *
 * @see em_queue_create_param()
 */
void em_queue_param_init(em_queue_param_t *param);

/**
 * Create a new EM queue according to the given parameters.
 *
 * The queue name is copied into queue internal data. The maximum length stored
 * is EM_QUEUE_NAME_LEN. Duplicate names are allowed, but find will only match
 * one of them.
 *
 * Always initialize 'param' first with em_queue_param_init(&param) to ensure
 * backwards compatibility and default values for all fields before setting your
 * own params and calling em_queue_create_param():
 * @code
 *	em_queue_param_t param;
 *	em_queue_t queue;
 *
 *	em_queue_param_init(&param);   - Set default values for all fields
 *	param.queue = EM_QUEUE_UNDEF;  - Request a dynamic queue
 *	param.type = EM_QUEUE_TYPE_ATOMIC;
 *	param.prio = EM_QUEUE_PRIO_NORMAL;
 *	...
 *	queue = em_queue_create_param("my-queue", &param);
 *	if (unlikely(queue == EM_QUEUE_UNDEF))
 *		report_error();
 * @endcode
 *
 * @param name   Name of the queue (optional, NULL ok)
 * @param param  Queue parameters
 *
 * @return New queue handle if successful, otherwise EM_QUEUE_UNDEF.
 * @retval EM_QUEUE_UNDEF on error
 */
em_queue_t em_queue_create_param(const char *name,
				 const em_queue_param_t *param);

/**
 * Delete a queue.
 *
 * Unallocates the queue handle. This is an immediate deletion and can only
 * be done after the queue has been removed from scheduling using
 * em_eo_remove_queue().
 *
 * @param queue         Queue handle to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_remove_queue(), em_queue_create(), em_queue_create_static()
 */
em_status_t em_queue_delete(em_queue_t queue);

/**
 * Set queue specific (application) context.
 *
 * This is a single pointer associated with a queue. The application can use it
 * to access some context data quickly (without a lookup). The context is given
 * as an argument to the EO receive function. EM does not dereference it.
 *
 * @param queue         Queue to which associate the context
 * @param context       Context pointer
 *
 * @return EM_OK if successful.
 *
 * @see em_receive_func_t(), em_queue_context()
 */
em_status_t em_queue_set_context(em_queue_t queue, const void *context);

/**
 * Get queue specific (application) context.
 *
 * Returns the value application has earlier set with em_queue_set_context().
 *
 * @param queue         Queue for which the context is requested
 *
 * @return Queue specific context pointer or NULL on error.
 *
 * @see em_queue_set_context()
 */
void *em_queue_context(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_context em_queue_context

/**
 * Get the queue name.
 *
 * Returns the name given to a queue when it was created.
 * A copy of the queue name string (up to 'maxlen' characters) is written to the
 * user given buffer.
 * The string is always null terminated even if the given buffer length is less
 * than the name length.
 *
 * The function returns '0' and writes an empty string if the queue has no name.
 *
 * @param      queue   Queue handle
 * @param[out] name    Destination buffer
 * @param      maxlen  Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0').
 *
 * @see em_queue_create()
 */
size_t em_queue_name(em_queue_t queue, char *name, size_t maxlen);

/* Backwards compatible naming ("get") */
#define em_queue_get_name em_queue_name

/**
 * Find a queue by name.
 *
 * Finds a queue by the given name (exact match). An empty string will not match
 * anything. The search is case sensitive. The function will return the first
 * match only if there are duplicate names,
 * Be aware of that the search may take a long time if there are many queues.
 *
 * @param name          name to look for
 *
 * @return queue handle or EM_QUEUE_UNDEF if not found
 *
 * @see em_queue_create()
 */
em_queue_t em_queue_find(const char *name);

/**
 * Get the queue priority.
 *
 * @param queue         Queue handle
 *
 * @return Priority class or EM_QUEUE_PRIO_UNDEF on an error.
 *
 * @see em_queue_create()
 */
em_queue_prio_t em_queue_priority(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_priority em_queue_priority

/**
 * Get the queue type.
 *
 * @param queue         Queue handle
 *
 * @return Queue type or EM_QUEUE_TYPE_UNDEF on an error.
 *
 * @see em_queue_create()
 */
em_queue_type_t em_queue_type(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_type em_queue_type

/**
 * Construct a static queue handle from a given static offset.
 *
 * The first static queue handle is em_queue_static_handle(0), and the last
 * one is em_queue_static_handle(em_queue_static_max_num() - 1).
 * The static offset must be in range 0 .. em_queue_static_max_num() - 1,
 * otherwise EM_QUEUE_UNDEF is returned.
 *
 * Use the constructed handle with em_queue_create_static() to create the
 * corresponding queue if it has not been created already.
 *
 * @param static_offset  offset from the first static queue ID. Must be in
 *                       range 0 to em_queue_static_max_num() - 1
 *
 * @note The device id is not affected by this function and remains the same as
 *       in the first static queue handle. More specifically, the same device id
 *       as this EM instance is used.
 * @note This function only constructs a static queue handle based on the
 *       provided offset. It does not verify whether the corresponding static
 *       queue has been created. The returned value is a handle, which may or
 *       may not refer to an existing queue.
 *
 * @return A valid static queue handle or EM_QUEUE_UNDEF on error.
 *
 * @see em_queue_create_static(), em_queue_static_max_num()
 */
em_queue_t em_queue_static_handle(uint16_t static_offset);

/**
 * Get the associated queue group of the given queue.
 *
 * @param queue         Queue handle
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on error.
 *
 * @see em_queue_create(), em_queue_group_create(), em_queue_group_modify()
 */
em_queue_group_t em_queue_qgroup(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_group em_queue_qgroup

/**
 * Get the associated atomic group (if any) of the given queue.
 *
 * Returns the atomic group of the given queue.
 *
 * @param queue         Queue handle
 *
 * @return The atomic group the queue belongs to or EM_ATOMIC_GROUP_UNDEF if
 *         the given queue is not valid or doesn't belong to an atomic group.
 *
 * @see em_atomic_group_create()
 */
em_atomic_group_t em_queue_agroup(em_queue_t queue);

/* Backwards compatible naming ("get") - moved from the atomic group API */
#define em_atomic_group_get em_queue_agroup

/**
 * Dequeue an event from an unscheduled queue
 *
 * This can only be used with unscheduled queues created with the type
 * EM_QUEUE_TYPE_UNSCHEDULED. Events are added to these queues with em_send(),
 * similar to queues of other types, but applications needs to explicitly
 * dequeue the event(s). Unscheduled queues are general purpose FIFOs, i.e.
 * send(enqueue) to tail and dequeue from head. The maximum length of an
 * unscheduled queue is system specific.
 *
 * An unscheduled queue can also have a context, but if used it needs to be
 * asked separately using em_queue_context().
 *
 * @param queue    Unscheduled queue handle
 *
 * @return Event from head of queue or EM_EVENT_UNDEF if there was no events
 *         or an error occurred.
 */
em_event_t em_queue_dequeue(em_queue_t queue);

/**
 * Dequeue multiple events from an unscheduled queue
 *
 * This can only be used with unscheduled queues created with the type
 * EM_QUEUE_TYPE_UNSCHEDULED. Events are added to these queues with em_send(),
 * similar to queues of other types, but applications needs to explicitly
 * dequeue the event(s). Unscheduled queues are general purpose FIFOs, i.e.
 * send(enqueue) to tail and dequeue from head. The maximum length of an
 * unscheduled queue is system specific.
 *
 * An unscheduled queue can also have a context, but needs to be
 * asked separately using em_queue_context().
 *
 * @param      queue    Unscheduled queue handle
 * @param[out] events   Array of event handles for output
 * @param      num      Maximum number of events to dequeue
 *
 * @return Number of successfully dequeued events (0 to num)
 */
int em_queue_dequeue_multi(em_queue_t queue,
			   em_event_t events[/*out*/], int num);

/**
 * Returns the current active queue
 *
 * The 'current active queue' is the queue that delivered the input event to
 * the EO-receive that is currently being run.
 *
 * Only valid if called within an EO-receive context, will return EM_QUEUE_UNDEF
 * otherwise, i.e. can be called from the EO-receive functions or subfunctions
 * thereof.
 * Note that calling em_queue_current() from an EO-start/stop function that was
 * launched from within an EO's receive function will return EM_QUEUE_UNDEF.
 *
 * @return The current queue or EM_QUEUE_UNDEF if no current queue (or error)
 */
em_queue_t em_queue_current(void);

/**
 * Initialize queue iteration and return the first queue handle.
 *
 * Can be used to initialize the iteration to retrieve all created queues for
 * debugging or management purposes. Use em_queue_next() after this call
 * until it returns EM_QUEUE_UNDEF. A new call to em_queue_first() resets
 * the iteration, which is maintained per core (thread). The operation should be
 * completed in one go before returning from the EO's event receive function (or
 * start/stop).
 *
 * The number of queues (output arg 'num') may not match the amount of queues
 * actually returned by iterating using em_queue_next() if queues are added
 * or removed in parallel by another core. The order of the returned queue
 * handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_queue_t q = em_queue_first(&num);
 *	while (q != EM_QUEUE_UNDEF) {
 *		q = em_queue_next();
 *	}
 * @endcode
 *
 * @param[out] num   Pointer to an unsigned int to store the amount of queues
 *                   into
 *
 * @return The first queue handle or EM_QUEUE_UNDEF if none exist
 *
 * @see em_queue_next()
 */
em_queue_t em_queue_first(unsigned int *num);

/* Backwards compatible naming ("get") */
#define em_queue_get_first em_queue_first

/**
 * Return the next queue handle.
 *
 * Continues the queue iteration started by em_queue_first() and returns the
 * next queue handle.
 *
 * @return The next queue handle or EM_QUEUE_UNDEF if the queue iteration is
 *         completed (i.e. no more queues available).
 *
 * @see em_queue_first()
 */
em_queue_t em_queue_next(void);
/* Backwards compatible naming ("get") */
#define em_queue_get_next em_queue_next

/**
 * Get a unique index corresponding to the given EM queue handle.
 *
 * Returns a unique index in the range 0 to em_queue_max_num() - 1.
 * The same EM queue handle will always map to the same index.
 *
 * Only meaningful for queues created within the current EM instance.
 *
 * @param queue  EM queue handle
 * @return Index in the range 0 to em_queue_max_num() - 1
 */
int em_queue_index(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_index em_queue_index

/**
 * Returns the number of queue priorities available.
 *
 * Optionally the amount of actual runtime priorities can be inquired.
 * Valid queue priority range is from 0 (lowest priority) to
 * em_queue_num_prio() - 1.
 *
 * Runtime environment may provide different amount of levels. In that case EM
 * priorities are mapped to the runtime values depending on mapping mode
 * selected in the runtime configuration file.
 *
 * @param[out] num_runtime	Pointer to an int to receive the number of
 *				actual runtime priorities. Set to NULL if
 *				not needed.
 *
 * @return	number of queue priorities
 *
 * @see em-odp.conf
 */
int em_queue_num_prio(int *num_runtime);

/* Backwards compatible naming ("get") */
#define em_queue_get_num_prio em_queue_num_prio

/**
 * Returns the maximum number of EM queues that can be created.
 *
 * The maximum number of EM queues contains all EM queues (static and dynamic)
 * created by the application as well as the internal EM queues.
 * The number of static and dynamic queues can be configured via
 * 'queue.num_static' and 'queue.num_dynamic' in em-odp.conf.
 *
 * Note that the function returns the maximum number of queues that _can_ be
 * created, not the actual number of queues currently created nor the max number
 * of queues that has been created at any point in time in the EM instance.
 * The actual number of queues currently created can be obtained by calling
 * em_queue_first(&num).
 *
 * @return the maximum number of EM queues of any type that can be created in
 *         the EM instance
 *
 * @see em_queue_internal_max_num(), em_queue_static_max_num(),
 *      em_queue_dynamic_max_num()
 */
int em_queue_max_num(void);

/* Backwards compatible naming ("get") */
#define em_queue_get_max_num em_queue_max_num

/**
 * Get the maximum number of EM internal queues that EM might internally create.
 *
 * EM reserves a number of internal queues for its own use (core-local and
 * shared internal queues) - this function returns that number.
 * Note that this is a maximum number and that the actual number of internal
 * queues may be less depending on the EM configuration and runtime environment.
 *
 * @return the maximum number of internal queues reserved by EM
 *
 * @see em_queue_max_num()
 */
int em_queue_internal_max_num(void);

/**
 * Get the maximum number of EM static queues that can be created.
 *
 * The maximum number of EM static queues that can be created are configured
 * via 'queue.num_static' in em-odp.conf.
 *
 * @return Number of static queues configured, or 0 if none configured
 *
 * @see em_queue_max_num(), em_queue_internal_max_num(),
 *      em_queue_dynamic_max_num(), em_queue_aggr_max_num()
 */
int em_queue_static_max_num(void);

/**
 * Get the maximum number of EM dynamic queues that can be created.
 *
 * The maximum number of dynamic queues that can be created is configured via
 * 'queue.num_dynamic' in em-odp.conf.
 *
 * @return The maximum number of dynamic queues that can be created in the
 *         EM instance.
 *
 * @see em_queue_max_num(), em_queue_internal_max_num(),
 *      em_queue_static_max_num(), em_queue_aggr_max_num()
 */
int em_queue_dynamic_max_num(void);

/**
 * Get the maximum number of EM aggregator queues that can be created.
 *
 * The maximum number of aggregator queues that can be created is configured via
 * 'queue.num_aggr' in em-odp.conf.
 *
 * @return The maximum number of aggregator queues that can be created over all
 *         parent (scheduled and unscheduled) queues in the EM instance.
 *
 * @see em_queue_max_num(), em_queue_internal_max_num(),
 *      em_queue_static_max_num(), em_queue_dynamic_max_num()
 */
int em_queue_aggr_max_num(void);

/**
 * Get the number of aggregator queues for the given parent queue.
 *
 * The parent queue can be a scheduled or unscheduled queue, but not an
 * aggregator queue itself.
 *
 * @param parent_queue  Parent queue handle
 *
 * @return Number of aggregator queues configured for 'parent_queue', or 0 if
 *         no aggregator queues are configured (or on an error).
 *
 * @see em_queue_aggr(), em_queue_aggr_info(), em_queue_aggr_list()
 */
uint32_t em_queue_aggr_num(em_queue_t parent_queue);

/**
 * Get an aggregator queue handle by index for the given parent queue.
 *
 * The parent queue can be a scheduled or unscheduled queue, but not an
 * aggregator queue itself. The returned aggregator queue is associated with
 * the parent queue and can be used as destination queue for sending/enqueuing
 * events.
 * Events sent to this queue may be aggregated into vector events before being
 * enqueued to the parent queue.
 * The parent queue must have been configured with aggregator queues when
 * created with em_queue_create_param() to get any aggregator queue returned by
 * this function.
 *
 * @param parent_queue  Parent queue handle
 * @param aggr_index    Aggregator queue index, range 0 to
 *                      em_queue_aggr_num(parent_queue) - 1.
 *
 * @return Aggregator queue handle or EM_QUEUE_UNDEF if not found.
 * @retval EM_QUEUE_UNDEF  Invalid 'parent_queue' (error reported).
 * @retval EM_QUEUE_UNDEF  'aggr_index' is out of range (no error).
 *
 * @see em_queue_create_param(), em_queue_aggr_conf_t,
 *      em_queue_aggr_num(), em_queue_aggr_info(), em_queue_aggr_list()
 */
em_queue_t em_queue_aggr(em_queue_t parent_queue, uint32_t aggr_index);

/**
 * Get aggregator queue handles for a given parent queue.
 *
 * This function outputs the total number of aggregator queues configured for
 * the given parent queue via the output parameter 'num_aggr' and writes queue
 * handles to 'aggr_list[]' up to the given 'max' length (<= EM_QUEUE_MAX_AGGR).
 *
 * The parent queue can be a scheduled or unscheduled queue, but not an
 * aggregator queue itself. The returned aggregator queues are associated with
 * the parent queue and can be used as destination queues for sending/enqueuing
 * events.
 * Events sent to these queues may be aggregated into vector events before
 * being enqueued to the parent queue.
 * The parent queue must have been configured with aggregator queues when
 * created with em_queue_create_param() to get any aggregator queues returned by
 * this function.
 *
 * To only get the number of aggregator queues without retrieving handles:
 * @code
 *	uint32_t num_aggr = 0;
 *
 *	(void)em_queue_aggr_list(parent_queue, NULL, 0, &num_aggr);
 * @endcode
 *
 * To get all aggregator queue handles for a parent queue:
 * @code
 *	em_queue_t aggr_list[EM_QUEUE_MAX_AGGR];
 *	uint32_t num_aggr = 0;
 *	uint32_t num_out = em_queue_aggr_list(parent_queue, aggr_list,
 *					      EM_QUEUE_MAX_AGGR, &num_aggr);
 *	for (uint32_t i = 0; i < num_out; i++)
 *		use_aggr(aggr_list[i]);
 * @endcode
 *
 * @param      parent_queue  Parent queue handle for which to list the
 *                           aggregator queues.
 * @param[out] aggr_list     Pointer to an aggregator queue handle array that
 *                           can fit 'max' amount of handles.
 *                           Use NULL if only interested in 'num_aggr'.
 * @param      max           Max number of handles that can be written into
 *                           'aggr_list[]', ignored if 'aggr_list' is NULL.
 *                           'max' is in the range 0 .. EM_QUEUE_MAX_AGGR.
 *                           'max' is capped to EM_QUEUE_MAX_AGGR if greater.
 * @param[out] num_aggr      Output for the total number of configured
 *                           aggregator queues for 'parent_queue'.
 *                           Optional, can be NULL if not needed.
 *
 * @return Number of handles written into 'aggr_list[]' (in the range
 *         0 to min(num_aggr, max)).
 *
 * @see em_queue_create_param(), em_queue_aggr_conf_t
 */
uint32_t em_queue_aggr_list(em_queue_t parent_queue,
			    em_queue_t aggr_list[/*out:max*/], uint32_t max,
			    uint32_t *num_aggr /*out*/);

/**
 * Aggregator queue information.
 *
 * Contains the aggregator queue handle and the configuration that was
 * used when the parent queue was created with em_queue_create_param().
 * Filled by em_queue_aggr_info() for each aggregator queue of a parent queue.
 *
 * @see em_queue_aggr_info(), em_queue_aggr_conf_t
 */
typedef struct {
	/** Aggregator queue handle */
	em_queue_t queue;
	/** Event vector pool used for aggregation */
	em_pool_t pool;
	/** Maximum time in nanoseconds to wait for aggregation (0 = no limit) */
	uint64_t max_tmo_ns;
	/** Maximum number of events in vector */
	uint32_t max_size;
	/** Aggregation and vector event type */
	em_event_type_t event_type;
} em_queue_aggr_info_t;

/**
 * Get aggregator queue information for the given parent queue.
 *
 * Returns information about the aggregator queues configured for the given
 * parent queue. Each element in the output array 'aggr_info[]' contains the
 * aggregator queue handle and the configuration that was used when the parent
 * queue was created.
 *
 * The parent queue can be a scheduled or unscheduled queue, but not an
 * aggregator queue itself.
 *
 * This is a heavier operation than em_queue_aggr() and em_queue_aggr_list() and
 * should only be used when the additional information about the aggregator
 * queues is needed.
 *
 * @code
 *	em_queue_aggr_info_t info[EM_QUEUE_MAX_AGGR];
 *	uint32_t num_aggr = 0;
 *	uint32_t num = em_queue_aggr_info(parent_queue, info,
 *					  EM_QUEUE_MAX_AGGR, &num_aggr);
 *	for (uint32_t i = 0; i < num; i++)
 *		use_aggr(info[i].queue, info[i].pool, ...);
 * @endcode
 *
 * @param      parent_queue  Parent queue handle
 * @param[out] aggr_info     Array for aggregator queue information output,
 *                           must have room for 'max' entries.
 *                           Use NULL if only interested in 'num_aggr'.
 * @param      max           Maximum number of entries to write into
 *                           'aggr_info[]', ignored if 'aggr_info' is NULL.
 *                           'max' is in the range 0 .. EM_QUEUE_MAX_AGGR.
 * @param[out] num_aggr      Output for the total number of configured
 *                           aggregator queues for 'parent_queue'.
 *                           Optional, can be NULL if not needed.
 *
 * @return Number of entries written into 'aggr_info[]' (in the range
 *         0 to min(num_aggr, max)), or 0 on error.
 *
 * @see em_queue_create_param(), em_queue_aggr_conf_t,
 *      em_queue_aggr_num(), em_queue_aggr(), em_queue_aggr_list()
 */
uint32_t em_queue_aggr_info(em_queue_t parent_queue,
			    em_queue_aggr_info_t aggr_info[/*out*/],
			    uint32_t max, uint32_t *num_aggr /*out*/);

/**
 * Returns the device-id extracted from the given queue handle
 *
 * An EM queue handle consists of a device-id and a queue-id. This function
 * extracts the device-id from an EM queue handle and returns it.
 *
 * @param queue  EM queue handle
 * @return the device-id extracted from the queue handle
 */
uint16_t em_queue_device_id(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_device_id em_queue_device_id

/**
 * Returns the queue-id extracted from the given queue handle
 *
 * An EM queue handle consists of a device-id and a queue-id. This function
 * extracts the queue-id from an EM queue handle and returns it.
 *
 * @param queue EM queue handle
 * @return the queue-id extracted from the queue handle
 */
uint16_t em_queue_qid(em_queue_t queue);

/* Backwards compatible naming ("get") */
#define em_queue_get_qid em_queue_qid

/**
 * Extract and output both the device-id and the queue-id from the given
 * queue handle.
 *
 * An EM queue handle consists of a device-id and a queue-id. This function
 * extracts both the device-id and the queue-id from an EM queue handle and
 * returns them to the caller via the output arguments 'device_id' and 'qid'.
 *
 * @param       queue      EM queue handle
 * @param[out]  device_id  device-id
 * @param[out]  qid        queue-id
 */
void em_queue_ids(em_queue_t queue, uint16_t *device_id /*out*/,
		  uint16_t *qid /*out*/);

/* Backwards compatible naming ("get") */
#define em_queue_get_ids em_queue_ids

/**
 * Construct a raw EM queue handle from the provided device-id and queue-id.
 *
 * An EM queue handle consists of a device-id and a queue-id. This function
 * constructs an EM queue handle by combining the device-id and queue-id
 * together into an EM queue handle.
 *
 * @note No checks for the validity of the provided device-id or queue-id are
 * done. Thus the constructed EM queue handle is a raw value that may not refer
 * to any existing queue on this EM instance or on another. Be careful.
 *
 * @param device_id
 * @param qid
 * @return raw EM queue handle created from the given arguments
 */
em_queue_t em_queue_handle_raw(uint16_t device_id, uint16_t qid);

/**
 * Convert an queue handle to an unsigned integer
 *
 * @param queue  queue handle to be converted
 * @return       uint32_t value that can be used to print/display the handle
 *
 * @note This routine is intended to be used for diagnostic purposes
 * to enable applications to e.g. generate a printable value that represents
 * an em_queue_t handle.
 *
 * @note Unlike other "EM handle to unsigned integer" conversion functions,
 * the queue handle is converted to a uint32_t (instead of a uint64_t) since
 * the handle consists of a 16-bit device-id and a 16-bit queue-id.
 */
uint32_t em_queue_to_u32(em_queue_t queue);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_QUEUE_H_ */
