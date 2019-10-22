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

#ifndef EVENT_MACHINE_EO_H_
#define EVENT_MACHINE_EO_H_

/**
 * @file
 * @defgroup em_eo Execution objects (EO)
 *
 * Operations on EO
 *
 * Execution objects (EO) are the application building blocks of EM.
 * An EO typically implements one logical function or one stage in a pipeline,
 * but alternatively the whole application could be implemented with one EO.
 * EOs work as servers, queues are the service access points (inputs to the EO).
 *
 * An EO consists of user provided callback functions and context data.
 * The most important function is the receive function, which gets called
 * when an event is received from one of the queues associated with this EO.
 * The EM scheduler selects the next event for processing on a core and the
 * EM dispatcher on that core maps the received event and queue information to
 * an EO receive function to call to process the event.
 * Other EO functions are used to manage start-up and teardown of EOs. See
 * individual EO functions for more details.
 *
 *                       em_eo_create()
 *                             |
 *                             v
 *                      .-------------.
 *          .->.------->|   CREATED   | (new events discarded)
 *          |  |        '-------------'
 *          |  |               | em_eo_start()
 *          |  |               v
 *          |  |        .-------------.
 *          |  |        |   STARTING  | (new events discarded)
 *          |  '        '-------------'
 *          |   \         global start
 *          |    \            THEN
 *          |     \       local start on each core
 *          |      '--- FAIL   OK
 *          |                  | send notifications
 *          |                  v
 *          .           .-------------.
 *          |           |   RUNNING   |
 *          |           '-------------'
 *          |                  | em_eo_stop()
 *          |                  v
 *          '           .-------------.
 *           \          |   STOPPING  | (new events discarded)
 *            \         '-------------'
 *             \               |
 *              \              v
 *               \         local stops on each core
 *                \           THEN
 *                 \       global stops
 *                  \          .
 *                   \        /
 *                    -------' send notifications
 *
 *  @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>
#include <event_machine/platform/event_machine_hw_types.h>
#include <event_machine/api/event_machine_error.h>

/**
 * Execution object (EO) event-receive function
 *
 * An application receives events through queues and these events are passed to
 * the application's EO receive function(s) for processing. The EO receive
 * function implements the main part of the application logic. EM calls the
 * receive function when it has dequeued an event from one of the EO's queues.
 * The application then processes the event and returns immediately in a
 * run-to-completion fashion. There is no pre-emption.
 *
 * On multicore systems, several events (from the same or different queue) may
 * be dequeued in parallel and thus the same receive function may be executed
 * concurrently on several cores. Parallel execution may be limited by queue
 * group setup or by using queues with an atomic scheduling mode.
 *
 * The EO and queue context pointers are user defined. The EO context is given
 * at EO creation and the queue context is set with em_queue_set_context().
 * These contexts may be used in any way needed, the EM implementation will not
 * dereference them. For example, the EO context may be used to store global
 * EO state information, which is common to all queues and events for that EO.
 * In addition, the queue context may be used to store queue specific state data
 * (e.g. user data flow related data). The queue context data for an atomic
 * queue can be freely manipulated in the receive function, since only one event
 * at a time can be under work from that particular atomic queue. For other
 * queue types it is up to the user to synchronize context access. The EO
 * context is protected only if the EO has one queue and it is of type 'atomic'
 * (applies also to several atomic queues that belong to the same atomic group).
 *
 * An event (handle) must be converted to an event structure pointer with
 * em_event_pointer() before accessing any data it may contain.
 * The event type specifies the event structure in memory, which is
 * implementation or application specific.
 * The queue handle specifies the queue where the event was dequeued from.
 *
 * The EO will not receive any events if it has not been successfully started.
 *
 * @param eo_ctx  EO context data. The pointer is passed in em_eo_create(),
 *                EM does not dereference.
 * @param event   Event handle
 * @param type    Event type
 * @param queue   Queue from which the event was dequeued
 * @param q_ctx   Queue context data. The context pointer is set by
 *                em_queue_set_context(), EM does not touch the data.
 *
 * @see em_event_pointer(), em_free(), em_alloc(), em_send(),
 *      em_queue_set_context(), em_eo_create()
 */
typedef void (*em_receive_func_t)(void *eo_ctx,
				  em_event_t event, em_event_type_t type,
				  em_queue_t queue, void *q_ctx);

/**
 * Execution object (EO) start function, global.
 *
 * This EO callback function is called once on one core by em_eo_start().
 * The purpose of this global EO-start is to provide a placeholder for first
 * level EO initialization, e.g. allocating memory and initializing shared data.
 * After this global start returns, the EO core local start function (if given)
 * is called on all cores in this EM instance. If there is no core local start,
 * then event dispatching is enabled as this function returns, otherwise the EO
 * is enabled only when all core local starts have completed successfully on all
 * the cores. If this function does not return EM_OK, the system will not call
 * the core local init and will not enable event dispatching for this EO.
 *
 * Note that events sent to scheduled queues from a start function are
 * buffered. The buffered events will be sent into the queues when the EO start
 * functions have returned - otherwise it would not be possible to send events
 * to the EO's own queues as the EO is not yet in a started state. No buffering
 * is done when sending to queues that are not scheduled.
 *
 * The last argument is an optional startup configuration passed directly
 * from em_eo_start(). If local start functions need the configuration data,
 * it must be saved during the global start.
 *
 * This function should never be directly called from the application,
 * it will be called by em_eo_start(), which maintains state information.
 *
 * @param eo_ctx        Execution object internal state/instance data
 * @param eo            Execution object handle
 * @param conf          Optional startup configuration, NULL ok.
 *
 * @return EM_OK if successful, other values abort EO start
 *
 * @see em_eo_start(), em_eo_create()
 */
typedef em_status_t (*em_start_func_t)(void *eo_ctx, em_eo_t eo,
				       const em_eo_conf_t *conf);

/**
 * Execution object (EO) start function, core local.
 *
 * This is similar to the global start above, but this one is called after the
 * global start has completed and is run on all cores of the EM instance
 * potentially in parallel.
 *
 * The purpose of this optional local start is to work as a placeholder for
 * core local initialization, e.g. allocating core local memory.
 *
 * Note that events sent to scheduled queues from local start functions are
 * buffered. The buffered events will be sent into the queues when the EO start
 * functions have returned - otherwise it would not be possible to send events
 * to the EO's own queues as the EO is not yet in a started state. No buffering
 * is done when sending to queues that are not scheduled.
 *
 * This function should never be directly called from the application,
 * it will be called by em_eo_start(), which maintains state information.
 *
 * Event dispatching is not enabled if this function doesn't return EM_OK on
 * all cores.
 *
 * @param eo_ctx        Execution object internal state/instance data
 * @param eo            Execution object handle
 *
 * @return EM_OK if successful, other values prevent EO start
 *
 * @see em_eo_start(), em_eo_create()
 */
typedef em_status_t (*em_start_local_func_t)(void *eo_ctx, em_eo_t eo);

/**
 * Execution object (EO) stop function, core local.
 *
 * This function is called once on each core of the EM instance before the
 * global stop (reverse order of start). The system disables event dispatching
 * before calling these and also makes sure this does not get called before
 * the core has been notified of the stop condition for this EO (won't dispatch
 * any new events).
 *
 * This function should never be directly called from the application,
 * it will be called by em_eo_stop(), which maintains state information.
 *
 * @param eo_ctx        Execution object internal state data
 * @param eo            Execution object handle
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_create()
 */
typedef em_status_t (*em_stop_local_func_t)(void *eo_ctx, em_eo_t eo);

/**
 * Execution object (EO) stop function, global.
 *
 * The EO global stop function is called once on one core after the optional
 * core local stop functions return on all cores. The system disables event
 * dispatching before calling this function and also makes sure it does not get
 * called before all cores have been notified of the stop condition for this EO
 * (don't dispatch new events).
 *
 * This function should never be directly called from the application,
 * it will be called by em_eo_stop(), which maintains state information.
 *
 * @param eo_ctx        Execution object internal state data
 * @param eo            Execution object handle
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_create()
 */
typedef em_status_t (*em_stop_func_t)(void *eo_ctx, em_eo_t eo);

/**
 * Create Execution Object (EO).
 *
 * Allocate an EO handle and initialize internal data for the new EO.
 * The EO is left in a non-active state, i.e. no events are dispatched before
 * em_eo_start() has been called. Start, stop and receive callback functions
 * are mandatory arguments.
 *
 * The EO name is copied into EO internal data. The maximum length stored is
 * EM_EO_NAME_LEN. Duplicate names are allowed, but find will only match one of
 * them.
 *
 * @param name          Name of the EO (optional, NULL ok)
 * @param start         Start function
 * @param local_start   Core local start function (NULL if no local start)
 * @param stop          Stop function
 * @param local_stop    Core local stop function (NULL if no local stop)
 * @param receive       Receive function
 * @param eo_ctx        User defined EO context data, EM passes the value
 *                      (NULL if no context)
 *
 * @return New EO handle if successful, otherwise EM_EO_UNDEF.
 *
 * @see em_eo_start(), em_eo_delete(), em_queue_create(), em_eo_add_queue()
 * @see em_start_func_t(), em_stop_func_t(), em_receive_func_t()
 */
em_eo_t
em_eo_create(const char *name, em_start_func_t start,
	     em_start_local_func_t local_start,
	     em_stop_func_t stop,
	     em_stop_local_func_t local_stop,
	     em_receive_func_t receive,
	     const void *eo_ctx);

/**
 * Delete Execution Object (EO).
 *
 * Immediately delete the given EO and free the identifier.
 *
 * NOTE, that an EO can only be deleted after it has been stopped using
 * em_eo_stop(), otherwise another core might still access the EO data.
 * All associated queues must be removed before deleting an EO.
 *
 * A sequence of
 * @code
 *	em_eo_stop_sync(eo);
 *	em_eo_remove_queue_all_sync(eo, EM_TRUE);
 *	em_eo_delete(eo);
 * @endcode
 * will cleanly delete an EO from the EM point of view (not including user
 * allocated data).
 *
 * @param eo     EO handle to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_remove_queue()
 */
em_status_t
em_eo_delete(em_eo_t eo);

/**
 * Returns the name given to the EO when it was created.
 *
 * A copy of the name string (up to 'maxlen' characters) is
 * written to the user buffer 'name'.
 * The string is always null terminated - even if the given buffer length
 * is less than the name length.
 *
 * The function returns 0 and writes an empty string if the EO has no name.
 *
 * @param eo            EO handle
 * @param name          Destination buffer
 * @param maxlen        Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0').
 *
 * @see em_eo_create()
 */
size_t
em_eo_get_name(em_eo_t eo, char *name, size_t maxlen);

/**
 * Find EO by name.
 *
 * Finds an EO by the given name (exact match). An empty string will not match
 * anything. The search is case sensitive. This function will return the first
 * match only if there are duplicate names.
 *
 * @param name          the name to look for
 *
 * @return EO handle or EM_EO_UNDEF if not found
 *
 * @see em_eo_create()
 */
em_eo_t
em_eo_find(const char *name);

/**
 * Add a queue to an EO.
 *
 * Add the given queue to the EO and enable scheduling for it. The function
 * returns immediately, but the operation can be asynchronous and only fully
 * complete later. Any notifications given are sent when the operation has
 * completed and the queue is ready to receive events.
 * Note, that the completion notification(s) guarantee that the queue itself is
 * operational, but if the target EO is not yet started then events will still
 * be dropped by dispatcher.
 *
 * @param eo            EO handle
 * @param queue         Queue handle
 * @param num_notif     Number of notification events, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *                      (+ optional event groups to send the events with)
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_create(), em_eo_create(), em_eo_remove_queue()
 */
em_status_t
em_eo_add_queue(em_eo_t eo, em_queue_t queue,
		int num_notif, const em_notif_t *notif_tbl);

/**
 * Add a queue to an EO, synchronous
 *
 * As em_eo_add_queue(), but does not return until the queue is ready to
 * receive events.
 *
 * @param eo            EO handle
 * @param queue         Queue handle
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_create(), em_eo_create(), em_eo_remove_queue()
 */
em_status_t
em_eo_add_queue_sync(em_eo_t eo, em_queue_t queue);

/**
 * Removes a queue from an EO.
 *
 * Disables queue scheduling and removes the queue from the EO. The function
 * returns immediately, but the operation can be asynchronous and only fully
 * complete later. Any notifications given are sent when the operation has
 * completed and no event from this queue is no longer under work.
 * Use notifications to know when the operation has fully completed and the
 * queue can safely be deleted.
 *
 * @param eo            EO handle
 * @param queue         Queue handle to remove
 * @param num_notif     Number of notification events, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *                      (+ optional event groups to send the events with)
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_add_queue(), em_eo_remove_queue_sync()
 */
em_status_t
em_eo_remove_queue(em_eo_t eo, em_queue_t queue,
		   int num_notif, const em_notif_t *notif_tbl);

/**
 * Removes a queue from an EO, synchronous
 *
 * As em_eo_remove_queue(), but will not return until the queue is disabled and
 * no more event processing from this queue is under work.
 *
 * @param eo            EO handle
 * @param queue         Queue handle to remove
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_remove_queue()
 */
em_status_t
em_eo_remove_queue_sync(em_eo_t eo, em_queue_t queue);

/**
 * Removes all queues from an EO.
 *
 * Like em_eo_remove_queue(), but removes all queues currently associated with
 * the EO.
 * The argument 'delete_queues' can be used to automatically delete all queues
 * by setting it to EM_TRUE (EM_FALSE otherwise).
 * Note: any allocated queue contexts will still need to be handled elsewhere.
 *
 * @param eo             EO handle
 * @param delete_queues  delete the EO's queues if set to EM_TRUE
 * @param num_notif      Number of notification events, 0 for no notification
 * @param notif_tbl      Array of pairs of event and queue identifiers
 *                       (+ optional event groups to send the events with)
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_add_queue(), em_eo_remove_queue_sync()
 */
em_status_t
em_eo_remove_queue_all(em_eo_t eo, int delete_queues,
		       int num_notif, const em_notif_t *notif_tbl);

/**
 * Removes all queues from an EO, synchronous.
 *
 * As em_eo_remove_queue_all(), but does not return until all queues have
 * been removed.
 *
 * @param eo              EO handle
 * @param delete_queues   delete the EO's queues if set to EM_TRUE
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_remove_queue_all()
 */
em_status_t
em_eo_remove_queue_all_sync(em_eo_t eo, int delete_queues);

/**
 * Register an EO specific error handler.
 *
 * The EO specific error handler is called if an error occurs or em_error() is
 * called in the context of the EO. Note, the function will override any
 * previously registered error handler.
 *
 * @param eo            EO handle
 * @param handler       New error handler
 *
 * @return EM_OK if successful.
 *
 * @see em_register_error_handler(), em_error_handler_t()
 */
em_status_t
em_eo_register_error_handler(em_eo_t eo, em_error_handler_t handler);

/**
 * Unregister an EO specific error handler.
 *
 * Removes a previously registered EO specific error handler.
 *
 * @param eo            EO handle
 *
 * @return EM_OK if successful.
 */
em_status_t
em_eo_unregister_error_handler(em_eo_t eo);

/**
 * Start an Execution Object (EO).
 *
 * Start and enable a previously created EO.
 * The em_eo_start() function will first call the user provided global EO start
 * function. If that global start function returns EM_OK then events to trigger
 * the (optional) user provided local start function are sent to all cores.
 * The em_eo_start() function returns immediately after the global start
 * returns, which means that the action only fully completes later.
 * Notifications should be used if the caller needs to know when the EO start
 * has fully completed. The given notification event(s) will be sent to the
 * given queue(s) when the start is completed on all cores.
 *
 * Local start is not called and event dispatching is not enabled for this EO if
 * the global start function does not return EM_OK.
 *
 * The notification(s) are sent when the global start function returns if a
 * local start function hasn't been provided.
 * Use '0' as 'num_notif' if notifications are not needed. Be aware of,
 * is this case, that the EO may not immediately be ready to handle events.
 *
 * Note that events sent to scheduled queues from a user provided EO global or
 * local start function are buffered. The buffered events will be sent into the
 * queues when the EO start functions have all returned - otherwise it would not
 * be possible to send events to the EO's own queues as the EO is not yet in a
 * started state. No buffering is done when sending to queues that are
 * not scheduled.
 *
 * The optional conf-argument can be used to pass applification specific
 * information (e.g. configuration data) to the EO.
 *
 * @param eo         EO handle
 * @param result     Optional pointer to em_status_t, which gets updated to the
 *                   return value of the actual user provided EO global start
 *                   function.
 * @param conf       Optional startup configuration, NULL ok.
 * @param num_notif  If not 0, defines the number of events to send when all
 *                   cores have returned from the start function (in notif_tbl)
 * @param notif_tbl  Array of em_notif_t, the optional notification events
 *                   (array data is copied)
 *
 * @return EM_OK if successful.
 *
 * @see em_start_func_t(), em_start_local_func_t(), em_eo_stop()
 */
em_status_t
em_eo_start(em_eo_t eo, em_status_t *result, const em_eo_conf_t *conf,
	    int num_notif, const em_notif_t *notif_tbl);

/**
 * Start Execution Object (EO), synchronous
 *
 * As em_eo_start(), but will not return until the operation is complete.
 *
 * @param eo         EO handle
 * @param result     Optional pointer to em_status_t, which gets updated to the
 *                   return value of the actual user provided EO global start
 *                   function.
 * @param conf	     Optional startup configuration, NULL ok.
 *
 * @return EM_OK if successful.
 *
 * @see em_start_func_t(), em_start_local_func_t(), em_eo_stop()
 */
em_status_t
em_eo_start_sync(em_eo_t eo, em_status_t *result, const em_eo_conf_t *conf);

/**
 * Stop Execution Object (EO).
 *
 * Disables event dispatch from all related queues, calls core local stop
 * on all cores and finally calls the global stop function of the EO when all
 * cores have returned from the (optional) core local stop.
 * The call to the global EO stop is asynchronous and only done when all cores
 * have completed processing of the receive function and/or core local stop.
 * This guarantees no other core is accessing EO data during the EO global stop
 * function.
 *
 * This function returns immediately, but may only fully complete later. If the
 * caller needs to know when the EO stop has actually completed, the num_notif
 * and notif_tbl should be used. The given notification event(s) will be sent to
 * given queue(s) when the stop operation actually completes.
 * If such notifications are not needed, use '0' as 'num_notif'.
 *
 * When the EO has stopped it can be started again with em_eo_start().
 *
 * @param eo            EO handle
 * @param num_notif     Number of notification events, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *                      (+ optional event groups to send the events with)
 *
 * @return EM_OK if successful.
 *
 * @see em_stop_func_t(), em_stop_local_func_t(), em_eo_start()
 */
em_status_t
em_eo_stop(em_eo_t eo, int num_notif, const em_notif_t *notif_tbl);

/**
 * Stop Execution Object (EO), synchronous
 *
 * As em_eo_stop(), but will not return until the operation is complete.
 *
 * @param eo            EO handle
 *
 * @return EM_OK if successful.
 *
 * @see em_stop_func_t(), em_stop_local_func_t(), em_eo_start()
 */
em_status_t
em_eo_stop_sync(em_eo_t eo);

/**
 * Return the currently active EO
 *
 * Returns the EO handle associated with the currently running EO function.
 * Only valid if called within an EO-context, will return EM_EO_UNDEF otherwise.
 * Can be called from the EO-receive or EO-start/stop functions (or subfunctions
 * thereof).
 * Note that calling em_eo_current() from e.g. an EO-start function that was
 * launched from within another EO's receive will return the EO handle of the
 * EO being started - i.e. always returns the 'latest' current EO.
 *
 * @return The current EO or EM_EO_UNDEF if no current EO (or error)
 */
em_eo_t
em_eo_current(void);

/**
 * Get EO specific (application) context.
 *
 * Returns the EO context pointer that the application has earlier provided via
 * em_eo_create().
 *
 * @param eo         EO for which the context is requested
 *
 * @return EO specific context pointer or NULL if no context (or error)
 */
void *
em_eo_get_context(em_eo_t eo);

/**
 * Return the EO state.
 *
 * Returns the current state of the given EO.
 *
 * @return The current EO state or EM_EO_STATE_UNDEF if never created.
 */
em_eo_state_t
em_eo_get_state(em_eo_t eo);

/**
 * Initialize EO iteration and return the first EO handle.
 *
 * Can be used to initialize the iteration to retrieve all created EOs for
 * debugging or management purposes. Use em_eo_get_next() after this call until
 * it returns EM_EO_UNDEF. A new call to em_eo_get_first() resets the iteration,
 * which is maintained per core (thread). The operation should be completed in
 * one go before returning from the EO's event receive function (or start/stop).
 *
 * The number of EOs (output arg 'num') may not match the amount of EOs actually
 * returned by iterating using em_eo_get_next() if EOs are added or removed in
 * parallel by another core. The order of the returned EO handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_eo_t eo = em_eo_get_first(&num);
 *	while (eo != EM_EO_UNDEF) {
 *		eo = em_eo_get_next();
 *	}
 * @endcode
 *
 * @param num [out]  Pointer to an unsigned int to store the amount of EOs into
 * @return The first EO handle or EM_EO_UNDEF if none exist
 *
 * @see em_eo_get_next()
 */
em_eo_t
em_eo_get_first(unsigned int *num);

/**
 * Return the next EO handle.
 *
 * Continues the EO iteration started by em_eo_get_first() and returns the next
 * EO handle.
 *
 * @return The next EO handle or EM_EO_UNDEF if the EO iteration is completed
 *         (i.e. no more EO's available).
 *
 * @see em_eo_get_first()
 */
em_eo_t
em_eo_get_next(void);

/**
 * Initialize iteration of an EO's queues and return the first queue handle.
 *
 * Can be used to initialize the iteration to retrieve all queues associated
 * with the given EO for debugging or management purposes.
 * Use em_eo_queue_get_next() after this call until it returns EM_QUEUE_UNDEF.
 * A new call to em_eo_queue_get_first() resets the iteration, which is
 * maintained per core (thread). The operation should be started and completed
 * in one go before returning from the EO's event receive function (or
 * start/stop).
 *
 * The number of queues owned by the EO (output arg 'num') may not match the
 * amount of queues actually returned by iterating using em_eo_queue_get_next()
 * if queues are added or removed in parallel by another core. The order of
 * the returned queue handles is undefined.
 *
 * Simplified example:
 * @code
 *	unsigned int num;
 *	em_queue_t q = em_eo_queue_get_first(&num, eo);
 *	while (q != EM_QUEUE_UNDEF) {
 *		q = em_eo_queue_get_next();
 *	}
 * @endcode
 *
 * @param num [out]  Pointer to an unsigned int to store the amount of queues
 *                   into.
 * @param eo         EO handle
 *
 * @return The first queue handle or EM_QUEUE_UNDEF if none exist or the EO
 *         is invalid.
 *
 * @see em_eo_queue_get_next()
 **/
em_queue_t
em_eo_queue_get_first(unsigned int *num, em_eo_t eo);

/**
 * Return the EO's next queue handle.
 *
 * Continues the queue iteration started by em_eo_queue_get_first() and returns
 * the next queue handle owned by the EO.
 *
 * @return The next queue handle or EM_QUEUE_UNDEF if the queue iteration is
 *         completed (i.e. no more queues available for this EO).
 *
 * @see em_eo_queue_get_first()
 **/
em_queue_t
em_eo_queue_get_next(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_EO_H_ */
