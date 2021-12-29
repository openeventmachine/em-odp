/*
 *   Copyright (c) 2015-2021, Nokia Solutions and Networks
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
 * describes a piece of work or data to be processed. The structure of an event
 * is implementation and event type specific: it may be a directly accessible
 * buffer of memory, a descriptor containing a list of buffer pointers,
 * a descriptor of a packet buffer etc.
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
 * Additionally, an event may contain a user area separate from the event
 * payload. The size of the event user area is set when creating the event pool
 * from which the event is allocated. The user area is a fixed size (per pool)
 * data area into which event related state data can be stored without having
 * to access and change the payload. Note that the size of the event user area
 * can be zero(0), depending on event pool configuration.
 * Note that the user area content is not initialized by EM, neither em_alloc()
 * nor em_free() will touch it and thus it might contain old user data set the
 * last time the area was used during a previous allocation of the same event.
 * Since the user area is not part of the event payload, it will not be
 * transmitted as part of a packet etc.
 * A user area ID can further be used to identify the user area contents.
 * The event user area ID is stored outside of the user area itself and is thus
 * always available, even if the size of the user area data is set to zero(0).
 * See em_pool_create(), em_event_uarea_get(), em_event_uarea_id_get/set() and
 * em_event_uarea_info() for more information on the event user area and its
 * associated ID.
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
 * Use em_event_pointer() to convert an event (handle) to a pointer to the
 * event payload. EM does not initialize the payload data.
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
 * @see em_free(), em_send(), em_event_pointer(), em_receive_func_t(),
 *      em_event_clone()
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
 * @brief Returns the EM event-pool the event was allocated from.
 *
 * The EM event-pool for the given event can only be obtained if the event has
 * been allocated from a pool created with em_pool_create(). For other pools,
 * e.g. external (to EM) pktio pools, EM_POOL_UNDEF is returned.
 *
 * @param event         Event handle
 *
 * @return The EM event-pool handle or EM_POOL_UNDEF if no EM pool is found.
 *         EM_POOL_UNDEF is returned also for a valid event that has been
 *         allocated from a pool external to EM (no error is reported).
 */
em_pool_t em_event_get_pool(em_event_t event);

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
 * state before the mark-send function call.
 * Any further usage of the event after em_event_mark_send(), by EM or
 * the user, will result in error when calling em_event_unmark_send() since the
 * state has become unrecoverable.
 * => the only allowed EM API call after em_event_mark_send() is
 *    em_event_unmark_send() if it is certain that the event, due to some
 *    external error, will never be sent into EM again otherwise.
 * Calling em_event_unmark_send() transfers event ownership back to the user
 * again.
 *
 * @note Unmark-send and unmark-free are the only valid cases of using an event
 *       that the user no longer owns - all other such uses leads to fatal error
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
 *		//       uses an obsolete event handle to recover the event.
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
 * @brief Mark the event as "free".
 *
 * Indicates a user-given promise to EM that the event will be freed back into
 * the pool it was allocated from e.g. by HW or device drivers (external to EM).
 * Calling em_event_mark_free() transfers event ownership away from the user,
 * and thus the event must not be used or touched by the user anymore.
 *
 * Example use case:
 * A user provided output-callback function associated with a queue of type
 * 'EM_QUEUE_TYPE_OUTPUT' can use this API when configuring a HW-device or
 * device-driver to free the event (outside of EM) after transmission.
 *
 * EM will, after this API-call, treat the event as "freed" and any further API
 * operations or usage might lead to EM errors (depending on the error-check
 * level), e.g. em_send/free/tmo_set/ack(event) etc. is forbidden after
 * em_event_mark_free(event).
 *
 * @note Registered API-callback hooks for em_free/_multi() (em_api_hook_free_t)
 *       will NOT be called.
 *
 * @param event    Event to be marked as "free"
 *
 * @see em_free(), em_event_unmark_free()
 */
void em_event_mark_free(em_event_t event);

/**
 * @brief Unmark an event previously marked as "free"
 *        (i.e mark as "allocated" again).
 *
 * @note This is for recovery situations only and can potenially crash the
 *       application if used incorrectly! Unmarking the free-state of an event
 *       that has already been freed will lead to fatal error.
 *
 * Revert an event's "free" state, as set by em_event_mark_free(), back to the
 * state before the mark-free function call.
 * Any further usage of the event after em_event_mark_free(), by EM or the user,
 * will result in error when calling em_event_unmark_free() since the state has
 * become unrecoverable.
 * => the only allowed EM API call after em_event_mark_free() (for a certain
 *    event) is em_event_unmark_free() when it is certain that the event, due to
 *    some external error, will not be freed otherwise and must be recovered
 *    back into the EM-domain so that calling em_free() by the user is possible.
 * Calling em_event_unmark_free() transfers event ownership back to the user
 * again.
 *
 * @note Unmark-send and unmark-free are the only valid cases of using an event
 *       that the user no longer owns - all other such uses leads to fatal error
 *
 * @code
 *	em_status_t err;
 *	hw_err_t hw_err;
 *
 *	// 'event' owned by the user
 *	em_event_mark_free(event);
 *	// 'event' no longer owned by the user - don't touch!
 *
 *	hw_err = config_hw_to_transmit_event(...hw-cfg..., event);
 *	if (hw_err) {
 *		// hw config error - the event can be recovered if it is
 *		// certain that the hw won't free that same event.
 *		// note: the user doesn't own the event here and actually
 *		//       uses an obsolete event handle to recover the event.
 *		em_event_unmark_free(event);
 *		// 'event' recovered, again owned by the user
 *		em_free(event);
 *	}
 * @endcode
 *
 * @param event    Event previously marked as "free" with
 *                 em_event_mark_free/_multi(), any other usecase is invalid!
 *
 * @see em_free(), em_event_mark_free()
 */
void em_event_unmark_free(em_event_t event);

/**
 * @brief Mark multiple events as "free".
 *
 * Similar to em_event_mark_free(), but allows the marking of multiple events
 * as "free" with one function call.
 *
 * @note Registered API-callback hooks for em_free/_multi() (em_api_hook_free_t)
 *       will NOT be called.
 *
 * @param[in] events  Array of events to be marked as "free"
 * @param     num     The number of events in the array 'events[]'
 */
void em_event_mark_free_multi(const em_event_t events[], int num);

/**
 * @brief Unmark multiple events previously marked as "free".
 *
 * @note This is for recovery situations only and can potenially crash the
 *       application if used incorrectly!
 *
 * Similar to em_event_unmark_free(), but allows to do the "free"-unmarking of
 * multiple events with one function call.
 *
 * @param[in] events  Events previously marked as "free" with
 *                    em_event_mark_free/_multi(), any other usecase is invalid!
 * @param     num     The number of events in the array 'events[]'
 */
void em_event_unmark_free_multi(const em_event_t events[], int num);

/**
 * @brief Clone an event.
 *
 * Allocate a new event with identical payload to the given event.
 *
 * @note Event metadata, internal headers and state are _NOT_ cloned
 *       (e.g. the event-group of a cloned event is EM_EVENT_GROUP_UNDEF etc).
 *
 * @param    event  Event to be cloned, must be a valid event.
 * @param    pool   Optional event pool to allocate the cloned event from.
 *                  Use 'EM_POOL_UNDEF' to clone from the same pool as 'event'
 *                  was allocated from.
 *                  The event-type of 'event' must be suitable for allocation
 *                  from 'pool' (e.g. EM_EVENT_TYPE_PACKET can not be
 *                  allocated from a pool supporting only EM_EVENT_TYPE_SW)
 *
 * @return The cloned event or EM_EVENT_UNDEF on error.
 *
 * @see em_alloc(), em_free()
 */
em_event_t em_event_clone(em_event_t event, em_pool_t pool/*or EM_POOL_UNDEF*/);

/**
 * @brief Get a pointer to the event user area, optionally along with its size.
 *
 * The event user area is a fixed sized area located within the event metadata
 * (i.e. outside of the event payload) that can be used to store application
 * specific event related data without the need to adjust the payload.
 * The event user area is configured during EM event pool creation and thus the
 * size of the user area is set per pool.
 *
 * Note that the user area content is not initialized by EM, neither em_alloc()
 * nor em_free() will touch it and thus it might contain old user data set the
 * last time the area was used during a previous allocation of the same event.
 * Since the user area is not part of the event payload, it will not be
 * transmitted as part of a packet etc.
 *
 * @param       event  Event handle to get the user area of
 * @param[out]  size   Optional output arg into which the user area size is
 *                     stored. Use 'size=NULL' if no size information is needed.
 *
 * @return a pointer to the event user area
 * @retval NULL on error or if the event contains no user area
 *
 * @see em_pool_create() for pool specific configuration and
 *      the EM runtime config file em-odp.conf for the default value:
 *      'pool.user_area_size'.
 * @see em_event_uarea_info() if both user area ptr and ID is needed
 */
void *em_event_uarea_get(em_event_t event, size_t *size/*out*/);

/**
 * @brief Get the event user area ID along with information if it has been set
 *
 * The event user area can be associated with an optional ID that e.g. can be
 * used to identify the contents of the actual user area data. The ID is stored
 * outside of the actual user area data and is available for use even if the
 * user area size has been set to zero(0) for the pool the event was allocated
 * from.
 *
 * This function is used to determine whether the user area ID has been set
 * earlier and to retrieve the ID in the case it has been set.
 * EM will initialize 'ID isset = false' when allocating a new event (indicating
 * that the ID is not set). Use em_event_uarea_id_set() to set the ID.
 *
 * @param       event  Event handle to get the user area ID and "set"-status of
 * @param[out]  isset  Optional output arg: has the ID been set previously?
 *                     At least one of 'isset' and 'id' must be given (or both).
 * @param[out]  id     Optional output arg into which the user area ID is
 *                     stored if it has been set before. The output arg 'isset'
 *                     should be used to determine whether 'id' has been set.
 *                     Note: 'id' will not be touched if the ID has not been set
 *                     earlier (i.e. when 'isset' is 'false').
 *                     At least one of 'isset' and 'id' must be given (or both).
 *
 * @return EM_OK if successful
 *
 * @see em_event_uarea_id_set(), em_event_uarea_get()
 * @see em_event_uarea_info() if both user area ptr and ID is needed
 */
em_status_t em_event_uarea_id_get(em_event_t event, bool *isset /*out*/,
				  uint16_t *id /*out*/);

/**
 * @brief Set the event user area ID
 *
 * The event user area can be associated with an optional ID that e.g. can be
 * used to identify the contents of the actual user area data. The ID is stored
 * outside of the actual user area data and is available for use even if the
 * user area size has been set to 0 for the pool the event was allocated from.
 *
 * This function is used to set the event user area ID for the given event.
 * The 'set' operation overwrites any ID stored earlier.
 * Use em_event_uarea_id_get() to check whether an ID has been set earlier and
 * to retrieve the ID.
 *
 * @param event  Event handle for which to set the user area ID
 * @param id     The user area ID to set
 *
 * @return EM_OK if successful
 *
 * @see em_event_uarea_id_get(), em_event_uarea_get(), em_event_uarea_info()
 */
em_status_t em_event_uarea_id_set(em_event_t event, uint16_t id);

/**
 * @brief Event user area information filled by em_event_uarea_info()
 *
 * Output structure for obtaining information about an event's user area.
 * Information related to the user area will be filled into this struct by
 * the em_event_uarea_info() API function.
 *
 * A user area is only present if the EM pool the event was allocated from
 * was created with user area size > 0, see em_pool_cfg_t and em_pool_create().
 * The user area ID can always be used (set/get), even when the size of the
 * user area is zero(0).
 *
 * @see em_event_uarea_info(), em_event_uarea_id_set()
 */
typedef struct {
	/** Pointer to the event user area, NULL if event has no user area */
	void *uarea;
	/** Size of the event user area, zero(0) if event has no user area */
	size_t size;

	/** Event user area ID (ID can be set/get even when no uarea present) */
	struct {
		/** Boolean: has the ID been set previously? true/false */
		bool isset;
		/** Value of the user area ID, if (and only if) set before.
		 *  Only inspect '.id.value' when '.id.isset=true' indicating
		 *  that ID has been set earlier by em_event_uarea_id_set().
		 */
		uint16_t value;
	} id;
} em_event_uarea_info_t;

/**
 * @brief Get the event user area information for a given event.
 *
 * Obtain information about the event user area for a certain given event.
 * Information containing the user area pointer, size, as well as the ID is
 * output via the 'uarea_info' struct.
 * This API function combines the functionality of em_event_uarea_get() and
 * em_event_uarea_id_get() for use cases where both the user area pointer as
 * well as the ID is needed. Calling one API function instead of two might be
 * faster due to a fewer checks and internal conversions.
 *
 * The event user area is a fixed sized area located within the event metadata
 * (i.e. outside of the event payload) that can be used to store application
 * specific event related data without the need to adjust the payload.
 * The event user area is configured during EM event pool creation and thus the
 * size of the user area is set per pool.
 *
 * Note that the user area content is not initialized by EM, neither em_alloc()
 * nor em_free() will touch it and thus it might contain old user data set the
 * last time the area was used during a previous allocation of the same event.
 * Since the user area is not part of the event payload, it will not be
 * transmitted as part of a packet etc.
 *
 * The event user area can be associated with an optional ID that can be used to
 * identify the contents of the actual user area data. The ID is stored
 * outside of the actual user area data and is available for use even if the
 * user area size has been set to zero(0) for the pool the event was allocated
 * from. EM will initialize 'uarea_info.id.isset = false' when allocating
 * a new event (indicating that the ID is not set).
 *
 * @param       event       Event handle to get the user area information of.
 * @param[out]  uarea_info  Output struct into which the user area information
 *                          is stored.
 *
 * @return EM status code incidating success or failure of the operation.
 * @retval EM_OK  Operation successful.
 * @retval Other  Operation FAILED and no valid user area info could
 *                be obtained, 'uarea_info' is all NULL/zero(0) in this case.
 *
 * @see em_pool_create() for pool specific configuration and
 *      the EM runtime config file em-odp.conf for the default value:
 *      'pool.user_area_size'.
 * @see em_event_uarea_get(), em_event_uarea_id_get()
 */
em_status_t em_event_uarea_info(em_event_t event,
				em_event_uarea_info_t *uarea_info /*out*/);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_EVENT_H_ */
