/*
 *   Copyright (c) 2015-2023, Nokia Solutions and Networks
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
 * A queue can have one of six (6) different scheduling modes / queue types:
 *
 * -# EM_QUEUE_TYPE_ATOMIC
 *   - The atomic queue type limits event scheduling to one event at a time from
 *     the queue. The next event from the same queue can only be scheduled after
 *     the EO returns from processing the earlier event or calls
 *     em_atomic_processing_end() to signal end of atomic processing.
 *     This type is useful to avoid multicore race conditions as only one core
 *     at a time can be working on an event from an atomic queue.
 *     Additionally, the ingress-egress event order is maintained.
 *
 * -# EM_QUEUE_TYPE_PARALLEL
 *   - Parallel queues have no restriction for scheduling, which means that any
 *     amount of events (up to the number of cores in the queue group of the
 *     queue) can be processed simultaneously. This provides the best scaling,
 *     but race conditions need to be avoided and handled by the application.
 *
 * -# EM_QUEUE_TYPE_PARALLEL_ORDERED
 *   - Parallel-Ordered queues have no scheduling restrictions (scheduled like
 *     parallel queues i.e. multiple events can be under work concurrently),
 *     but events coming from an ordered queue and sent forward will be in
 *     original order (as observed at the target queue) even if cores do the
 *     processing out of order. Events can be sent to multiple target queues of
 *     any queue type and still maintain ordering (the order of events in each
 *     target queue, not between different target queues). Note however that if
 *     the target queue type is not atomic and the EO can run on more than one
 *     thread (set by queue group) then it is possible that the target EO may
 *     process the events out of order as it is not limited by atomic
 *     scheduling. A chain of ordered (or atomic) queues will still maintain
 *     the original order.
 *
 *     Conceptually an ordering context is created when the scheduler
 *     dequeues an event from an ordered queue and it represents the sequence
 *     number of that event within that queue. The ordering context is valid
 *     within the EO receive function and ends implicitly when the EO returns
 *     (can be terminated early, see em_ordered_processing_end()). Any number of
 *     events can be sent under that context and all those will maintain order
 *     relative to the current event. The first one sent will take the order
 *     position of the current (just received) event. Other events sent during
 *     the same ordered context will take consecutive positions (before the
 *     next event in the original input queue when it gets scheduled and
 *     processed). The original event does not need to be sent forward to
 *     maintain order, any event sent will inherit the location of the current
 *     ordered context.
 *
 *     If no event is sent during the EO receive function with ordered context
 *     it indicates an implicit skip of that event position in the ordered
 *     sequence.
 *
 *     Atomic queues also maintain ordering, but an ordered queue can increase
 *     the performance as multiple cores can concurrently process the events.
 *
 * -# EM_QUEUE_TYPE_UNSCHEDULED
 *   - Unscheduled queues are not connected to the scheduler but instead the
 *     application needs to dequeue events directly using em_queue_dequeue().
 *     The API function em_send() is used to enqueue events into an unscheduled
 *     queue. All queue types look the same to the sender with the exception
 *     that em_send_group() is not supported for unscheduled queues.
 *     Unscheduled queues cannot be added to an EO.
 *
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

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Create a new queue with a dynamic queue handle (i.e. handle given by EM)
 *
 * The given name string is copied into an EM internal data structure. The
 * maximum string length is EM_QUEUE_NAME_LEN.
 *
 * Create scheduled atomic, parallel or parallel-ordered queues by using the
 * types EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_PARALLEL or
 * EM_QUEUE_TYPE_PARALLEL_ORDERED, respectively.
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
 * Note, that the system may have a limited amount of static handles available,
 * so prefer the use of dynamic queues, unless static handles are really needed.
 * The range of static identifiers/handles is system dependent, but macros
 * EM_QUEUE_STATIC_MIN and EM_QUEUE_STATIC_MAX can be used to abstract actual
 * values, e.g. use EM_QUEUE_STATIC_MIN+x for the application.
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
 * @see em_queue_create()
 */
em_status_t
em_queue_create_static(const char *name, em_queue_type_t type,
		       em_queue_prio_t prio, em_queue_group_t group,
		       em_queue_t queue, const em_queue_conf_t *conf);

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
em_status_t
em_queue_delete(em_queue_t queue);

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
 * @see em_receive_func_t(), em_queue_get_context()
 */
em_status_t
em_queue_set_context(em_queue_t queue, const void *context);

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
void *
em_queue_get_context(em_queue_t queue);

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
size_t
em_queue_get_name(em_queue_t queue, char *name, size_t maxlen);

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
em_queue_t
em_queue_find(const char *name);

/**
 * Get the queue priority.
 *
 * @param queue         Queue handle
 *
 * @return Priority class or EM_QUEUE_PRIO_UNDEF on an error.
 *
 * @see em_queue_create()
 */
em_queue_prio_t
em_queue_get_priority(em_queue_t queue);

/**
 * Get the queue type.
 *
 * @param queue         Queue handle
 *
 * @return Queue type or EM_QUEUE_TYPE_UNDEF on an error.
 *
 * @see em_queue_create()
 */
em_queue_type_t
em_queue_get_type(em_queue_t queue);

/**
 * Get the queue's queue group
 *
 * @param queue         Queue handle
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on error.
 *
 * @see em_queue_create(), em_queue_group_create(), em_queue_group_modify()
 */
em_queue_group_t
em_queue_get_group(em_queue_t queue);

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
 * asked separately using em_queue_get_context().
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
 * asked separately using em_queue_get_context().
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
em_queue_t
em_queue_current(void);

/**
 * Initialize queue iteration and return the first queue handle.
 *
 * Can be used to initialize the iteration to retrieve all created queues for
 * debugging or management purposes. Use em_queue_get_next() after this call
 * until it returns EM_QUEUE_UNDEF. A new call to em_queue_get_first() resets
 * the iteration, which is maintained per core (thread). The operation should be
 * completed in one go before returning from the EO's event receive function (or
 * start/stop).
 *
 * The number of queues (output arg 'num') may not match the amount of queues
 * actually returned by iterating using em_queue_get_next() if queues are added
 * or removed in parallel by another core. The order of the returned queue
 * handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_queue_t q = em_queue_get_first(&num);
 *	while (q != EM_QUEUE_UNDEF) {
 *		q = em_queue_get_next();
 *	}
 * @endcode
 *
 * @param[out] num   Pointer to an unsigned int to store the amount of queues
 *                   into
 *
 * @return The first queue handle or EM_QUEUE_UNDEF if none exist
 *
 * @see em_queue_get_next()
 */
em_queue_t
em_queue_get_first(unsigned int *num);

/**
 * Return the next queue handle.
 *
 * Continues the queue iteration started by em_queue_get_first() and returns the
 * next queue handle.
 *
 * @return The next queue handle or EM_QUEUE_UNDEF if the queue iteration is
 *         completed (i.e. no more queues available).
 *
 * @see em_queue_get_first()
 */
em_queue_t
em_queue_get_next(void);

/**
 * Get a unique index corresponding to the given EM queue handle.
 *
 * Returns a unique index in the range 0 to em_queue_get_max_num() - 1.
 * The same EM queue handle will always map to the same index.
 *
 * Only meaningful for queues created within the current EM instance.
 *
 * @param queue  EM queue handle
 * @return Index in the range 0 to em_queue_get_max_num() - 1
 */
int em_queue_get_index(em_queue_t queue);

/**
 * Returns the number of queue priorities available.
 *
 * Optionally the amount of actual runtime priorities can be inquired.
 * Valid queue priority range is from 0 (lowest priority) to
 * em_queue_get_num_prio() - 1.
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
int em_queue_get_num_prio(int *num_runtime);

/**
 * Returns the maximum number of queues that EM can support.
 *
 * The max number of EM queues can be configured via EM config file.
 * This contains the number of internal EM ctrl queues and all EM queues
 * (static/dynamic) created by application.
 *
 * @return the max number of EM queues that can be supported
 */
int em_queue_get_max_num(void);

/**
 * Returns the device-id extracted from the given queue handle
 *
 * An EM queue handle consists of a device-id and a queue-id. This function
 * extracts the device-id from an EM queue handle and returns it.
 *
 * @param queue  EM queue handle
 * @return the device-id extracted from the queue handle
 */
uint16_t em_queue_get_device_id(em_queue_t queue);

/**
 * Returns the queue-id extracted from the given queue handle
 *
 * An EM queue handle consists of a device-id and a queue-id. This function
 * extracts the queue-id from an EM queue handle and returns it.
 *
 * @param queue EM queue handle
 * @return the queue-id extracted from the queue handle
 */
uint16_t em_queue_get_qid(em_queue_t queue);

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
void em_queue_get_ids(em_queue_t queue, uint16_t *device_id /*out*/, uint16_t *qid /*out*/);

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
