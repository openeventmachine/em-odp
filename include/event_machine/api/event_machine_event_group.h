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

#ifndef EVENT_MACHINE_EVENT_GROUP_H_
#define EVENT_MACHINE_EVENT_GROUP_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_event_group Event group
 *  Event Machine fork-join helper.
 *  @{
 *
 * An event group can be used to trigger a join of parallel operations in the
 * form of notification events. The number of parallel operations needs to be
 * known in advance by the event group creator, but the separate event handlers
 * don't necessarily need to know anything about the other related events.
 * An event group is functionally a shared atomic counter decremented when each
 * related event has been handled (EO-receive() returns). The notification
 * events are automatically sent once the count reaches zero.
 *
 * There are two separate main usage patterns:
 *
 * Sender originated (original):
 * ----------------------------
 * 1. an event group is allocated with em_event_group_create().
 *
 * 2. the number of parallel events and the notifications are set with
 *    em_event_group_apply().
 *
 * 3. the (parallel) events are sent normally but using em_send_group() instead
 *    of em_send(). This tags the event with the given event group.
 *
 * 4. once received by a core the tag is used to switch core specific current
 *    event group to the one in the tag. The receiver EO handles the event
 *    normally (does not see any difference).
 *
 * 5. as the receive function returns the count of the current event group is
 *    decremented. If the count reaches zero (last event) the related
 *    notification event(s) are sent automatically and can trigger the next
 *    operation for the application.
 *
 * 6. the sequence can continue from step 2 for a new set of events if the
 *    event group is to be reused.
 *
 * Receiver originated (API 1.2):
 * -----------------------------
 * 1. an event group is created with em_event_group_create().
 *
 * 2. the number of parallel events and the notifications are set with
 *    em_event_group_apply().
 *
 * 3. during the processing of any received event that is not already tagged to
 *    belong to an event group, em_event_group_assign() can be used to set the
 *    current event group (a core local value). The rest is then equivalent to
 *    as if the event was originally sent to an event group.
 *
 * 4. as the receive function returns the count of the current event group is
 *    decremented. If the count reaches zero (last event) the related
 *    notification event(s) are sent automatically and can trigger the next
 *    operation for the application.
 *
 * 5. the sequence can continue from step 2 for a new set of events if the
 *    event group is to be reused.
 *
 *
 * From an application (EO) point of view, an event group can get activated
 * either by entering the EO receive with an event tagged to an event group or
 * by explicitly calling em_event_group_assign. The current event group is core
 * local and only one event group can be active (current) at a time.
 * Assigning a received event that already is tagged to an event group, e.g.
 * sent with em_send_group(), is not allowed unless the event group is
 * deactivated first with em_event_group_processing_end().
 * The current event group gets deactivated by exiting the EO receive function
 * or by explicitly calling em_event_group_processing_end(). Deactivation means
 * the count of the event group is decremented and if the count reaches zero
 * the notification events are sent.
 * The current event group is local to a core (dispatcher) and exists only
 * within the EO receive function.
 *
 * Note, that event groups may only work with events that are to be handled by
 * an EO, i.e. SW events.
 *
 * OpenEM implementation should internally use a generation count or other
 * technique to make sure that em_event_group_abort() can stop a problem
 * propagation, i.e. after a group is aborted (and applied a new count) any
 * potential delayed event(s) from the previous cycle will not cause the new
 * count to be decremented.
 * The same should be valid for excess group events, i.e. when sending more
 * than the applied count.
 * To make it possible for the application to properly handle such problems,
 * the implementation should pre-check incoming events and call error handler
 * before giving the event to an EO. This makes it possible for the application
 * to choose whether to drop those events (at the error handler) or let them be
 * processed.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Create a new event group for fork-join.
 *
 * The amount of simultaneous event groups can be limited.
 *
 * @return The new event group or EM_EVENT_GROUP_UNDEF if no event group is
 *         available.
 *
 * @see em_event_group_delete(), em_event_group_apply()
 */
em_event_group_t em_event_group_create(void);

/**
 * Delete (unallocate) an event group.
 *
 * An event group must not be deleted before it has been completed
 * (count reached zero) or aborted. A created but never applied event group
 * can be deleted.
 *
 * @param event_group   Event group to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_event_group_create(), em_event_group_abort()
 */
em_status_t em_event_group_delete(em_event_group_t event_group);

/**
 * Apply event group configuration.
 *
 * This function sets the event count and notification parameters for the event
 * group. After it returns, events sent or assigned to the event group are
 * counted against the current count value. Notification events are sent when
 * all (counted) events have been processed (count is decrement at EO receive
 * return or by calling em_event_group_processing_end()). A new apply call is
 * needed to re-use the event group for another cycle (with a new count and
 * notifications).
 *
 * Notification events can optionally be sent to/tagged with another event
 * group but not with the same event group that triggered the notifications,
 * see em_notif_t for more.
 *
 * @attention em_event_group_apply() can only be used on a newly created event
 * group or when the previous cycle is completed or successfully aborted.
 * Application can use em_event_group_is_ready() to detect whether apply is
 * allowed but would normally use a notification to setup a new cycle
 * (implementation must make sure that when any of the notifications is
 * received the group is ready for new apply).
 *
 * Apply should only be called once per group cycle.
 *
 * @param event_group   Event group
 * @param count         Number of events in the group (positive integer)
 * @param num_notif     Number of notification events to send
 * @param notif_tbl     Table of notifications (events and target queues)
 *
 * @return EM_OK if successful.
 *
 * @see em_event_group_create(), em_send_group(), em_event_group_is_ready(),
 *      em_notif_t
 */
em_status_t em_event_group_apply(em_event_group_t event_group, int count,
				 int num_notif, const em_notif_t notif_tbl[]);

/**
 * Increment the current event group count.
 *
 * Increments the event count of the currently active event group (received or
 * assigned event). Enables sending new events into the current event group.
 * The event count cannot be decremented and this will fail if there is no
 * current event group.
 *
 * @param count   Number of events to add to the event group (positive integer)
 *
 * @return EM_OK if successful.
 *
 * @see em_send_group(), em_event_group_apply()
 */
em_status_t em_event_group_increment(int count);

/**
 * Checks if the event group is ready for 'apply'.
 *
 * Returns EM_TRUE (1) if the given event group is ready, i.e. the user can do
 * em_event_group_apply() again. A better alternative to this is to use a
 * related notification event to re-use the event group (apply can always be
 * used when handling a notification event from the event group).
 *
 * An event group that has been applied a count but no events sent is not
 * considered 'ready for apply'. If a change is needed the group has to be
 * aborted and then re-applied.
 *
 * Return value EM_TRUE does not guarantee all notifications are received nor
 * handled, but the event group count has reached zero and the event group
 * is ready for a new apply.
 *
 * @param event_group   Event group
 *
 * @return EM_TRUE if the given event group is ready for apply
 *
 * @see em_event_group_create(), em_event_group_apply()
 */
int em_event_group_is_ready(em_event_group_t event_group);

/**
 * Return the currently active event group.
 *
 * Returns the current event group or EM_EVENT_GROUP_UNDEF if an event group is
 * not active (i.e. never activated or deactivated using
 * em_event_group_processing_end()).
 *
 * Can only be used within an EO receive function.
 *
 * @return Current event group or EM_EVENT_GROUP_UNDEF
 *
 * @see em_event_group_create()
 */
em_event_group_t em_event_group_current(void);

/**
 * Send event associated with/tagged to an event group.
 *
 * Any valid event and destination queue parameters can be used. The event
 * group indicates which event group the event is tagged to. The event group
 * has to first be created and applied a count.
 * One should always send the correct amount of events to an event group, i.e.
 * matching the applied count.
 *
 * Event group is not supported with unscheduled queues.
 *
 * @param event         Event to send
 * @param queue         Destination queue
 * @param event_group   Event group
 *
 * @return EM_OK if successful.
 *
 * @see em_send(), em_event_group_create(), em_event_group_apply(),
 *      em_event_group_increment()
 */
em_status_t em_send_group(em_event_t event, em_queue_t queue,
			  em_event_group_t event_group);

/**
 * Send multiple events associated with/tagged to an event group.
 *
 * This is like em_send_group, but multiple events can be sent with one call
 * for potential performance gain.
 * The call returns the number of events actually sent. A return value equal to
 * 'num' means that all events were sent. A value less than 'num' means the
 * events at the end of the given event list were not sent and must be handled
 * by the application.
 * The function will not modify the given list of events.
 *
 * Event group is not supported with unscheduled queues.
 *
 * @param events        List of events to send (i.e. ptr to array of events)
 * @param num		Number of events
 * @param queue         Destination queue
 * @param event_group   Event group
 *
 * @return number of events successfully sent (equal to num if all successful)
 *
 * @see em_send_group()
 */
int em_send_group_multi(const em_event_t events[], int num, em_queue_t queue,
			em_event_group_t event_group);

/**
 * Signal early end of processing of the current event group
 *
 * This is an optional call that can be used to move the implicit event group
 * handling (decrementing the count) from exiting event receive function to the
 * point of this call - the current event group count is decremented
 * immediately and if it reaches zero the notifications are also sent. In that
 * case the group will be ready for a new apply after this returns.
 *
 * This impacts the current event group the same way whether it was activated
 * by receiving a tagged event or EO called em_event_group_assign().
 *
 * This call does not change potential atomicity or ordering for the current
 * event and is a no-operation if called while an event group is not active
 * (no current group).
 *
 * Can only be used within the EO receive function.
 */
void em_event_group_processing_end(void);

/**
 * Assign core local current event group.
 *
 * The assign functionality can be used to set the core local current event
 * group. The event group handling after the assign call is identical to
 * the handling of an event group that was originally set by sending an event
 * tagged to that event group, i.e. the core local current event group
 * is active and will be operated on in a normal way.
 * Assign will fail if there already is an active current event group, i.e.
 * only one event group can be active at a time (per core).
 *
 * This needs to use used with care, i.e. match the amount of events applied
 * and assigned.
 *
 * @param event_group   An applied event group to assign to
 *
 * @return EM_OK if assignment was successful
 */
em_status_t em_event_group_assign(em_event_group_t event_group);

/**
 * Abort the ongoing event group.
 *
 * This is a recovery operation to abort an ongoing event group in case it does
 * not get completed. This will reset the group back to a state ready for
 * a new apply. Note, that there is a potential race as the group could get
 * completed on another thread while executing this (e.g. a delayed event is
 * finally received and processed). Implementation will synchronize internal
 * state changes, but this call may succeed or fail depending on timing so
 * abort should be done with care for recovery purpose only.
 *
 * Notification events related to the ongoing (to be aborted) cycle can be
 * managed as follows
 * 1) save possible related notifications using em_event_group_get_notif()
 * 2) call em_event_group_abort()
 * 3) IF em_event_group_abort() returns EM_OK the operation was successfully
 *    completed meaning the earlier notifications will not be sent thus the
 *    saved notifications can be freed or re-used. Otherwise the call was made
 *    too late and the saved notifications must not be touched as they are to
 *    be sent.
 *
 * This means the synchronization point is em_event_group_abort(), not
 * em_event_group_get_notif() which might return notifications that will still
 * be sent.
 *
 * @attention Related notification events will not be automatically freed in
 *			any case and must be handled by the application.
 *
 * @param event_group   Event group to abort and reset
 *
 * @return  EM_OK if the call was made early enough to cleanly abort, i.e.
 *          before the last event was processed. EM_OK also means the
 *          notifications will not be sent.
 */
em_status_t em_event_group_abort(em_event_group_t event_group);

/**
 * Return notification events currently related to an applied event group.
 *
 * This returns the current notifications or none (0) if they were already sent
 * (event group completed).
 *
 * @attention	This is not a synchronization point, which means
 *		em_event_group_get_notif() could return notifications which
 *		are just going to be sent and thus should not be touched.
 *
 * @param      event_group   Event group
 * @param      max_notif     Maximum number of notifications to return
 * @param[out] notif_tbl     Table for notifications to fill
 *
 * @return Number of returned notifications
 *
 * @see em_event_group_apply(), em_event_group_abort()
 */
int em_event_group_get_notif(em_event_group_t event_group,
			     int max_notif, em_notif_t notif_tbl[]);

/**
 * Initialize event group iteration and return the first event group handle.
 *
 * Can be used to initialize the iteration to retrieve all created event groups
 * for debugging or management purposes. Use em_event_group_get_next() after
 * this call until it returns EM_EVENT_GROUP_UNDEF.
 * A new call to em_event_group_get_first() resets the iteration, which is
 * maintained per core (thread). The operation should be completed in one go
 * before returning from the EO's event receive function (or start/stop).
 *
 * The number of event groups (output arg 'num') may not match the amount of
 * event groups actually returned by iterating using em_event_group_get_next()
 * if event groups are added or removed in parallel by another core. The order
 * of the returned event group handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_event_group_t eg = em_event_group_get_first(&num);
 *	while (eg != EM_EVENT_GROUP_UNDEF) {
 *		eg = em_event_group_get_next();
 *	}
 * @endcode
 *
 * @param[out] num   Pointer to an unsigned int to store the amount of
 *                   event groups into
 * @return The first event group handle or EM_EVENT_GROUP_UNDEF if none exist
 *
 * @see em_event_group_get_next()
 **/
em_event_group_t
em_event_group_get_first(unsigned int *num);

/**
 * Return the next event group handle.
 *
 * Continues the event group iteration started by em_event_group_get_first() and
 * returns the next event group handle.
 *
 * @return The next event group handle or EM_EVENT_GROUP_UNDEF if the event
 *         group iteration is completed (i.e. no more event groups available).
 *
 * @see em_event_group_get_first()
 **/
em_event_group_t
em_event_group_get_next(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_EVENT_GROUP_H_ */
