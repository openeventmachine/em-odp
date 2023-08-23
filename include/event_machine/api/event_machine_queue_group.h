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

#ifndef EVENT_MACHINE_QUEUE_GROUP_H_
#define EVENT_MACHINE_QUEUE_GROUP_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_queue_group Queue group
 *  Operations on queue groups
 *
 *  A queue group is basically a set of cores (threads) within an EM instance
 *  allowed to receive events from a queue belonging to that queue group.
 *
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>

/**
 * Create a new queue group to control queue to core mapping,
 * asynchronous (non-blocking)
 *
 * Allocates a new queue group handle with a given core mask.
 * Cores added to the queue group can be changed later with
 * em_queue_group_modify().
 *
 * This operation may be asynchronous, i.e. the creation may complete well after
 * this function has returned. Provide notification events, if the application
 * needs to know about the actual completion. EM will send notifications when
 * the operation has completed. Note that using a queue group before the
 * creation has completed may result in undefined behaviour.
 *
 * The core mask is visible through em_queue_group_get_mask() only after the
 * create operation has completed.
 *
 * Note, that the operation can also happen one core at a time, so an
 * intermediate mask may be active momentarily.
 *
 * Only manipulate the core mask with the access macros defined in
 * event_machine_hw_specific.h as the implementation underneath may change.
 *
 * The given name is copied up to the maximum length of EM_QUEUE_GROUP_NAME_LEN.
 * Duplicate names are allowed, but find will then only return the first match.
 * The name "default" is reserved for EM_QUEUE_GROUP_DEFAULT.
 *
 * EM has a default group EM_QUEUE_GROUP_DEFAULT containing all cores running
 * this EM instance. It's named "default".
 *
 * Some systems may have a low number of queue groups available.
 *
 * @attention  Only call em_queue_create() after em_queue_group_create() has
 *             completed - use notifications to synchronize. Alternatively use
 *             em_queue_group_create_sync() to be able to create the queue
 *             directly after creating the queue group in the source code.
 *
 * @param name       Queue group name (optional, NULL ok)
 * @param mask       Core mask for the queue group
 * @param num_notif  Number of entries in notif_tbl (use 0 for no notification)
 * @param notif_tbl  Array of notifications to send as the operation completes
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on error.
 *
 * @see em_queue_group_find(), em_queue_group_modify(), em_queue_group_delete(),
 *      em_queue_group_create_sync()
 */
em_queue_group_t
em_queue_group_create(const char *name, const em_core_mask_t *mask,
		      int num_notif, const em_notif_t notif_tbl[]);

/**
 * Create a new queue group to control queue to core mapping,
 * synchronous (blocking).
 *
 * As em_queue_group_create(), but will not return until the operation is
 * complete.
 *
 * Note that the function is blocking and will not return until the operation
 * has completed across all concerned EM cores.
 * Sync-API calls can block the core for a long (indefinite) time, thus they
 * should not be used to make runtime changes on real time EM cores - consider
 * the async variants of the APIs in these cases instead.
 * While one core is calling a sync-API function, the others must be running the
 * EM dispatch loop to be able to receive and handle the sync-API request events
 * sent internally.
 * Use the sync-APIs mainly to simplify application start-up or teardown.
 *
 * @param name       Queue group name (optional, NULL ok)
 * @param mask       Core mask for the queue group
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on error.
 *
 * @see em_queue_group_create() for an asynchronous version of the API
 */
em_queue_group_t
em_queue_group_create_sync(const char *name, const em_core_mask_t *mask);

/**
 * Delete the queue group, asynchronous (non-blocking)
 *
 * Removes all cores from the queue group and free's the handle for re-use.
 * All queues in the queue group must be deleted with em_queue_delete() before
 * deleting the queue group.
 *
 * @param queue_group  Queue group to delete
 * @param num_notif    Number of entries in notif_tbl (0 for no notification)
 * @param notif_tbl    Array of notifications to send as the operation completes
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_group_modify(), em_queue_delete(),
 *      em_queue_group_delete_sync()
 */
em_status_t
em_queue_group_delete(em_queue_group_t queue_group,
		      int num_notif, const em_notif_t notif_tbl[]);

/**
 * Delete the queue group, synchronous (blocking).
 *
 * As em_queue_group_delete(), but will not return until the operation is
 * complete.
 *
 * Note that the function is blocking and will not return until the operation
 * has completed across all concerned EM cores.
 * Sync-API calls can block the core for a long (indefinite) time, thus they
 * should not be used to make runtime changes on real time EM cores - consider
 * the async variants of the APIs in these cases instead.
 * While one core is calling a sync-API function, the others must be running the
 * EM dispatch loop to be able to receive and handle the sync-API request events
 * sent internally.
 * Use the sync-APIs mainly to simplify application start-up or teardown.
 *
 * @param queue_group  Queue group to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_delete() for an asynchronous version of the API
 */
em_status_t
em_queue_group_delete_sync(em_queue_group_t queue_group);

/**
 * Modify the core mask of an existing queue group, asynchronous (non-blocking)
 *
 * The function compares the new core mask to the current mask and changes the
 * core mapping for the given queue group accordingly.
 *
 * This operation may be asynchronous, i.e. the change may complete well after
 * this function has returned. Provide notification events, if the application
 * needs to know about the actual completion. EM will send notifications when
 * the operation has completed.
 *
 * The new core mask is visible through em_queue_group_get_mask() only after
 * the modify operation has completed.
 *
 * Note, that depending on the system, the change can also happen one core at
 * a time, so an intermediate mask may be active momentarily.
 *
 * Only manipulate core mask with the access macros defined in
 * event_machine_hw_specific.h as the implementation underneath may change.
 *
 * @param queue_group  Queue group to modify
 * @param new_mask     New core mask
 * @param num_notif    Number of entries in notif_tbl (0 for no notification)
 * @param notif_tbl    Array of notifications to send as the operation completes
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_group_find(), em_queue_group_delete()
 *      em_queue_group_get_mask(), em_queue_group_modify_sync()
 */
em_status_t
em_queue_group_modify(em_queue_group_t queue_group,
		      const em_core_mask_t *new_mask,
		      int num_notif, const em_notif_t notif_tbl[]);

/**
 * Modify core mask of an existing queue group, synchronous (blocking).
 *
 * As em_queue_group_modify(), but will not return until the operation is
 * complete.
 *
 * Note that the function is blocking and will not return until the operation
 * has completed across all concerned EM cores.
 * Sync-API calls can block the core for a long (indefinite) time, thus they
 * should not be used to make runtime changes on real time EM cores - consider
 * the async variants of the APIs in these cases instead.
 * While one core is calling a sync-API function, the others must be running the
 * EM dispatch loop to be able to receive and handle the sync-API request events
 * sent internally.
 * Use the sync-APIs mainly to simplify application start-up or teardown.
 *
 * @param queue_group  Queue group to modify
 * @param new_mask     New core mask
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_modify() for an asynchronous version of the API
 */
em_status_t
em_queue_group_modify_sync(em_queue_group_t queue_group,
			   const em_core_mask_t *new_mask);

/**
 * Finds a queue group by name.
 *
 * Finds a queue group by the given name (exact match). An empty string will not
 * match anything. The search is case sensitive. If there are duplicate names,
 * this will return the first match only.
 *
 * @param name          Name of the queue qroup to find
 *
 * @return  Queue group or EM_QUEUE_GROUP_UNDEF if not found
 *
 * @see em_queue_group_create()
 */
em_queue_group_t
em_queue_group_find(const char *name);

/**
 * Get the current core mask for a queue group.
 *
 * This returns the situation at the moment of the inquiry. The result may not
 * be up-to-date if another core is modifying the queue group at the same time.
 * The application may need to synchronize group modifications.
 *
 * @param queue_group  Queue group
 * @param mask         Core mask for the queue group
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_group_modify()
 */
em_status_t
em_queue_group_get_mask(em_queue_group_t queue_group, em_core_mask_t *mask);

/**
 * Get the name of a queue group.
 *
 * A copy of the name string (up to 'maxlen' characters) is written to the user
 * given buffer. The string is always null terminated, even if the given buffer
 * length is less than the name length.
 *
 * The function returns '0' and writes an empty string if the queue group has
 * no name.
 *
 * @param      queue_group  Queue group id
 * @param[out] name         Destination buffer
 * @param      maxlen       Maximum length (including the terminating '\0')
 *
 * @return Number of characters written (excludes the terminating '\0').
 */
size_t
em_queue_group_get_name(em_queue_group_t queue_group,
			char *name, size_t maxlen);

/**
 * Initialize queue group iteration and return the first queue group handle.
 *
 * Can be used to initialize the iteration to retrieve all created queue groups
 * for debugging or management purposes. Use em_queue_group_get_next() after
 * this call until it returns EM_QUEUE_GROUP_UNDEF.
 * A new call to em_queue_group_get_first() resets the iteration, which is
 * maintained per core (thread). The operation should be completed in one go
 * before returning from the EO's event receive function (or start/stop).
 *
 * The number of queue groups (output arg 'num') may not match the amount of
 * queue groups actually returned by iterating using em_event_group_get_next()
 * if queue groups are added or removed in parallel by another core. The order
 * of the returned queue group handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_queue_group_t qg = em_queue_group_get_first(&num);
 *	while (qg != EM_QUEUE_GROUP_UNDEF) {
 *		qg = em_queue_group_get_next();
 *	}
 * @endcode
 *
 * @param[out] num   Pointer to an unsigned int to store the amount of
 *                   queue groups into
 * @return The first queue group handle or EM_QUEUE_GROUP_UNDEF if none exist
 *
 * @see em_queue_group_get_next()
 **/
em_queue_group_t
em_queue_group_get_first(unsigned int *num);

/**
 * Continues the queue group iteration started by em_queue_group_get_first() and
 * returns the next queue group handle.
 *
 * @return The next queue group handle or EM_QUEUE_GROUP_UNDEF if the queue
 *         group iteration is completed (i.e. no more queue groups available).
 *
 * @see em_queue_group_get_first()
 **/
em_queue_group_t
em_queue_group_get_next(void);

/**
 * Initialize iteration of a queue group's queues and return the first
 * queue handle.
 *
 * Can be used to initialize the iteration to retrieve all queues associated
 * with the given queue group for debugging or management purposes.
 * Use em_queue_group_queue_get_next() after this call until it returns
 * EM_QUEUE_UNDEF.
 * A new call to em_queue_group_queue_get_first() resets the iteration, which is
 * maintained per core (thread). The operation should be started and completed
 * in one go before returning from the EO's event receive function (or
 * start/stop).
 *
 * The number of queues in the queue group (output arg 'num') may not match the
 * amount of queues actually returned by iterating using
 * em_queue_group_queue_get_next() if queues are added or removed in parallel by
 * another core. The order of the returned queue handles is undefined.
 *
 * Simplified example:
 * @code
 *	unsigned int num;
 *	em_queue_t q = em_queue_group_queue_get_first(&num, queue_group);
 *	while (q != EM_QUEUE_UNDEF) {
 *		q = em_queue_group_queue_get_next();
 *	}
 * @endcode
 *
 * @param[out] num          Pointer to an unsigned int to store the amount of
 *                          queue groups into.
 * @param      queue_group  Queue group handle
 *
 * @return The first queue handle or EM_QUEUE_UNDEF if none exist or the
 *         queue group is invalid.
 *
 * @see em_queue_group_queue_get_next()
 **/
em_queue_t
em_queue_group_queue_get_first(unsigned int *num, em_queue_group_t queue_group);

/**
 * Return the queue group's next queue handle.
 *
 * Continues the queue iteration started by em_queue_group_queue_get_first() and
 * returns the next queue handle in the queue group.
 *
 * @return The next queue handle or EM_QUEUE_UNDEF if the queue iteration is
 *         completed (i.e. no more queues available for this queue group).
 *
 * @see em_queue_group_queue_get_first()
 **/
em_queue_t
em_queue_group_queue_get_next(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_QUEUE_GROUP_H_ */
