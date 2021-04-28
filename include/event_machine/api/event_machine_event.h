/*
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

#ifndef EVENT_MACHINE_EVENT_H_
#define EVENT_MACHINE_EVENT_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_event Events
 *  Operations on an event.
 * @{
 *
 * All application processing is driven by events in the Event Machine. An event
 * describes a piece of work. The structure of an event is implementation and
 * event type specific: it may be a directly accessible buffer of memory, a
 * descriptor containing a list of buffer pointers, a descriptor of a packet
 * buffer etc.
 *
 * Applications use the event type to interpret the event structure.
 *
 * Events follow message passing semantics: an event has to be allocated using
 * the provided API (em_alloc()) or received through queues by an EO callback
 * function after which the event is owned by the application. Event ownership
 * is transferred back to the system by using em_send() or em_free().
 * An event not owned by the application should not be touched.
 *
 * Since em_event_t may not carry a direct pointer value to the event structure,
 * em_event_pointer() must be used to translate an event to an event structure
 * pointer (for maintaining portability).
 *
 * em_event_t is defined in event_machine_types.h
 *
 * @see em_event_pointer()
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Allocate an event.
 *
 * The memory address of the allocated event is system specific and can depend
 * on the given pool, event size and type. The returned event (handle) may refer
 * to a memory buffer or a HW specific descriptor, i.e. the event structure is
 * system specific.
 *
 * Use em_event_pointer() to convert an event (handle) to a pointer to an event
 * structure.
 *
 * EM_EVENT_TYPE_SW with minor type '0' is reserved for direct portability -
 * it is always guaranteed to produce an event with contiguous payload that can
 * directly be used by the application up to the given size (no HW specific
 * descriptors etc. are visible). This event payload will be 64-bit aligned
 * by default (unless explicitly configured otherwise).
 *
 * EM_POOL_DEFAULT can be used as a pool handle if there's no need to use a
 * specific event pool (up to the size- or event limits of that pool).
 *
 * Additionally it is guaranteed, that two separate buffers never share a cache
 * line (to avoid false sharing).
 *
 * @param size          Event size in octets (size > 0)
 * @param type          Event type to allocate
 * @param pool          Event pool handle
 *
 * @return The allocated event or EM_EVENT_UNDEF on error.
 *
 * @see em_free(), em_send(), em_event_pointer(), em_receive_func_t()
 */
em_event_t em_alloc(size_t size, em_event_type_t type, em_pool_t pool);

/**
 * Allocate multiple events.
 *
 * Similar to em_alloc(), but allows allocation of multiple events, with same
 * properties, with one function call.
 * The em_alloc_multi() API function will try to allocate the requested number
 * ('num') of events but may fail to do so, e.g. if the pool has run out of
 * events, and will return the actual number of events that were successfully
 * allocated from the given pool.
 *
 * @param[out] events  Output event array, events are allocated and filled by
 *                     em_alloc_multi(). The given array must fit 'num' events.
 * @param      num     Number of events to allocate and write into 'events[]'
 * @param      size    Event size in octets (size > 0)
 * @param      type    Event type to allocate
 * @param      pool    Event pool handle
 *
 * @return Number of events actually allocated from the pool (0 ... num) and
 *         written into the output array 'events[]'.
 */
int em_alloc_multi(em_event_t events[/*out*/], int num,
		   size_t size, em_event_type_t type, em_pool_t pool);

/**
 * Free an event.
 *
 * The em_free() function transfers ownership of the event back to the system
 * and the application must not touch the event (or related memory buffers)
 * after calling it.
 *
 * It is assumed that the implementation can detect the event pool that
 * the event was originally allocated from.
 *
 * The application must only free events it owns. For example, the sender must
 * not free an event after sending it.
 *
 * @param event         Event to be freed
 *
 * @see em_alloc(), em_receive_func_t()
 */
void em_free(em_event_t event);

/**
 * Free multiple events.
 *
 * Similar to em_free(), but allows freeing of multiple events with one
 * function call.
 *
 * @param[in] events  Array of events to be freed
 * @param     num     The number of events in the array 'events[]'
 */
void em_free_multi(const em_event_t events[], int num);

/**
 * Send an event to a queue.
 *
 * The event must have been allocated with em_alloc(), or received via an EO
 * receive-function. The sender must not touch the event after calling em_send()
 * as the ownership has been transferred to the system or possibly to the next
 * receiver. If the return status is *not* EM_OK, the ownership has not been
 * transferred and the application is still responsible for the event (e.g. may
 * free it).
 *
 * EM does not currently define guaranteed event delivery, i.e. EM_OK return
 * value only means the event was accepted for delivery. It could still be lost
 * during delivery (e.g. due to a removed queue or system congestion, etc).
 *
 * @param event         Event to be sent
 * @param queue         Destination queue
 *
 * @return EM_OK if successful (accepted for delivery).
 *
 * @see em_alloc()
 */
em_status_t em_send(em_event_t event, em_queue_t queue);

/**
 * Send multiple events to a queue.
 *
 * As em_send, but multiple events can be sent with one call for potential
 * performance gain.
 * The function returns the number of events actually sent. A return value equal
 * to the given 'num' means that all events were sent. A return value less than
 * 'num' means that only the first 'num' events were sent and the rest must be
 * handled by the application.
 *
 * @param events        Array of events to send
 * @param num           Number of events.
 *                      The array 'events[]' must contain 'num' entries.
 * @param queue         Destination queue
 *
 * @return number of events successfully sent (equal to num if all successful)
 *
 * @see em_send()
 */
int em_send_multi(const em_event_t events[], int num, em_queue_t queue);

/**
 * Get a pointer to the event structure
 *
 * Returns a pointer to the event structure or NULL. The event structure is
 * implementation and event type specific. It may be a directly accessible
 * buffer of memory, a descriptor containing a list of buffer pointers,
 * a descriptor of a packet buffer, etc.
 *
 * @param event         Event from receive/alloc
 *
 * @return Event pointer or NULL
 */
void *
em_event_pointer(em_event_t event);

/**
 * Returns the size of the given event
 *
 * The event content is not defined by the OpenEM API, thus this returns an
 * event type specific value (the exception and a defined case is
 * EM_EVENT_TYPE_SW + minor 0, in which case the usable size of the allocated
 * contiguous memory buffer is returned).
 *
 * @param event         Event handle
 *
 * @return Event type specific value typically payload size (bytes).
 */
size_t em_event_get_size(em_event_t event);

/**
 * Set the event type of an event
 *
 * This will not create a new event but the existing event might be modified.
 * The operation may fail if the new type is not compatible with the old one.
 * As event content is not defined by the OpenEM API the compatibility is
 * system specific.
 *
 * @param event         Event handle
 * @param newtype	New type for the event
 *
 * @return EM_OK on success
 *
 * @see em_alloc()
 */
em_status_t em_event_set_type(em_event_t event, em_event_type_t newtype);

/**
 * Get the event type of an event
 *
 * Returns the type of the given event.
 *
 * @param event         Event handle
 *
 * @return event type, EM_EVENT_TYPE_UNDEF on error
 */
em_event_type_t em_event_get_type(em_event_t event);

/**
 * Get the event types of multiple events
 *
 * Writes the event type of each given event into an output type-array and
 * returns the number of entries written.
 * Note, if 'events[num]' are all of the same type then 'types[num]' will
 * contain 'num' same entries.
 *
 * @param      events  Event handles: events[num]
 * @param[out] types   Event types (output array): types[num]
 *                     (types[i] is the type of events[i])
 * @param      num     Number of events and output types.
 *                     The array 'events[]' must contain 'num' entries and the
 *                     output array 'types[]' must have room for 'num' entries.
 *
 * @return Number of event types (0...num) written into 'types[]'.
 *         The return value (always >=0) is usually 'num' and thus '<num' is
 *         only seen in error scenarios when the type of event[i] could not be
 *         obtained. The return value will be '0' in error cases or if the given
 *         'num=0'. The function stops and returns on the first error and will
 *         not fill the rest of 'types[]'.
 */
int em_event_get_type_multi(const em_event_t events[], int num,
			    em_event_type_t types[/*out:num*/]);

/**
 * Get the number of events that have the same event type.
 *
 * Returns the number of consecutive events from the start of the array
 * 'events[]' that have the same event type. Outputs that same event type.
 * Useful for iterating through an event-array and grouping by event type.
 *
 * @param       events     Event handles: events[num]
 * @param       num        Number of events.
 *                         The array 'events[]' must contain 'num' entries.
 * @param[out]  same_type  Event type pointer for output
 *
 * @return Number of consecutive events (0...num) with the same event type
 *         (return value always >=0), includes and starts from events[0].
 *         The return value is usually '>=1' and thus '0' is only seen in
 *         error scenarios when the type of the first event could not be
 *         obtained or if the given 'num=0'.
 *         The function stops and returns on the first error.
 */
int em_event_same_type_multi(const em_event_t events[], int num,
			     em_event_type_t *same_type /*out*/);

/**
 * Mark the event as "sent".
 *
 * Indicates a user-given promise to EM that the event will later appear into
 * 'queue' by some means other than an explicit user call to em_send...().
 * Calling em_event_mark_send() transfers event ownership away from the user,
 * and thus the event must not be used or touched by the user anymore (the only
 * exception is (hw) error recovery where the "sent" state can be cancelled by
 * using em_event_unmark_send() - dangerous!).
 *
 * Example use case:
 * A user provided output-callback function associated with a queue of type
 * 'EM_QUEUE_TYPE_OUTPUT' can use this API when configuring a HW-device to
 * deliver the event back into EM. The HW will eventually "send" the event and
 * it will "somehow" again appear into EM for the user to process.
 *
 * EM will, after this API-call, treat the event as "sent" and any further API
 * operations or usage might lead to EM errors (depending on the error-check
 * level), e.g. em_send/free/tmo_set/ack(event) etc. is forbidden after
 * em_event_mark_send(event).
 *
 * @note Registered API-callback hooks for em_send...() (em_api_hook_send_t)
 *       will NOT be called.
 * @note Marking an event "sent" with an event group (corresponding to
 *       em_send_group()) is currrently NOT supported.
 *
 * @param event    Event to be marked as "sent"
 * @param queue    Destination queue (must be scheduled, i.e. atomic,
 *                                    parallel or parallel-ordered)
 *
 * @return EM_OK if successful
 *
 * @see em_send(), em_event_unmark_send()
 */
em_status_t em_event_mark_send(em_event_t event, em_queue_t queue);

/**
 * Unmark an event previously marked as "sent" (i.e mark as "unsent")
 *
 * @note This is for recovery situations only and can potenially crash the
 *       application if used incorrectly!
 *
 * Revert an event's "sent" state, as set by em_event_mark_send(), back to the
 * state before that function call.
 * Any further usage of the event after em_event_mark_send(), by EM or
 * the user, will result in error when calling em_event_unmark_send() since the
 * state has become unrecoverable.
 * => the only allowed EM API call after em_event_mark_send() is
 *    em_event_unmark_send() if it is certain that the event, due to some
 *    external error, will never be sent into EM again otherwise.
 * Calling em_event_unmark_send() transfers event ownership back to the user
 * again.
 *
 * @note This is the only valid case of using an event that the user no
 *       longer owns - all other such uses leads to fatal error.
 *
 * @code
 *	em_status_t err;
 *	hw_err_t hw_err;
 *
 *	// 'event' owned by the user
 *	err = em_event_mark_send(event, queue);
 *	if (err != EM_OK)
 *		return err; // NOK
 *	// 'event' no longer owned by the user - don't touch!
 *
 *	hw_err = config_hw_to_send_event(...hw-cfg..., event, queue);
 *	if (hw_err) {
 *		// hw config error - the event can be recovered if it is
 *		// certain that the hw won't send that same event.
 *		// note: the user doesn't own the event here and actually
 *		//       uses an obsolete event handle to recover the event...
 *		err = em_event_unmark_send(event);
 *		if (err != EM_OK)
 *			return err; // NOK
 *		// 'event' recovered, again owned by the user
 *		em_free(event);
 *	}
 * @endcode
 *
 * @param event    Event previously marked as "sent" with em_event_mark_send(),
 *                 any other case will be invalid!
 *
 * @return EM_OK if successful
 *
 * @see em_send(), em_event_mark_send()
 */
em_status_t em_event_unmark_send(em_event_t event);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_EVENT_H_ */
