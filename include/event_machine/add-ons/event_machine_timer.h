/*
 *   Copyright (c) 2016, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_TIMER_H_
#define EVENT_MACHINE_TIMER_H_

/**
 * @file
 *  Event Machine timer add-on
 * @defgroup em_timer Event timer add-on
 *  Event Machine timer add-on
 *  @{
 *
 * The timer API can be used to request an event to be sent to a specified
 * queue at a specified time once (one-shot) or at regular intervals (periodic).
 * A timer needs to be created first - it represents a collection of timeouts
 * with certain attributes (e.g. timeout resolution and maximum period).
 * A timer can be mapped to a HW resource on an SoC, thus the number of timers,
 * capabilities and time bases are system specific. Typically only one or a few
 * timers are supported.
 * The application can specify required capabilities when a timer is created.
 * The creation will fail if the implementation cannot fulfill the required
 * values. Timers are typically created once at system startup.
 *
 * A timer is a shared resource with proper synchronization for concurrent
 * multi-thread use. It is possible to exclude all multi-thread protections if a
 * timer is used exclusively by a single thread (for potential performance
 * gains). This is done by setting EM_TIMER_FLAG_PRIVATE when creating a timer.
 * Setting this flag means that the application must ensure that only a single
 * thread is using the timer (this also includes the receiver of periodic
 * timeouts due to the ack-functionality). This private-mode is not necessarily
 * implemented on all systems, in which case the flag is ignored as it will not
 * cause any functional difference.
 *
 * Timeouts (tmo) can be created once a timer exists. Creating a timeout
 * allocates the resources needed to serve the timeout, but does not arm it -
 * this makes it possible to pre-create timeout(s) and set the expiry
 * later at runtime. This can improve performance but also minimizes the
 * possibility that the runtime call setting the expiry would fail, as resources
 * have already been reserved beforehand.
 *
 * A pending timeout can be cancelled. Note that there is no way to cancel an
 * expired timeout for which the event has already been sent but not yet
 * received by the application - canceling, in this case, will return an error
 * to enable the the application to detect the situation. For a periodic timer,
 * a cancel will stop further timeouts, but may not be able to prevent the
 * latest event from being received.
 *
 * An active timeout cannot be altered without canceling it first.
 *
 * Timeouts needs to be deleted after use. Deletion frees the resources reserved
 * during creation.
 *
 * The unit of time (seconds, nanoseconds, cycles of 1.66GHz clock, frame
 * numbers etc.) is defined per timer/clock source and is system dependent.
 * The timeout value is an abstract "tick" defined per timer.
 * A clock source can be specified when creating a timer. It defines the time
 * base of the timer for systems with multiple sources implemented.
 * EM_TIMER_CLKSRC_DEFAULT is a portable value that implements a basic
 * monotonic non-wrapping time, typically based on the CPU clock.
 * It is assumed that the tick count is monotonic and will not wrap around,
 * unless a specific timer uses something exotic like a cyclic short frame
 * number counter.
 * The tick frequency of a timer can be inquired at runtime.
 *
 * A periodic timer requires the application to acknowledge each received
 * timeout event after it has been processed. The acknowledgment activates the
 * next timeout and compensates for the processing delay to keep the original
 * interval. This creates a flow control mechanism and also protects the event
 * handling from races if the same event is reused every time - the next
 * timeout will not be sent before the previous has been acknowledged.
 * The event to be received for each periodic timeout can also be different,
 * because the next event is always given by the application through the
 * acknowledge operation.
 * The target queue cannot be modified after the timeout has been created.
 *
 * If the acknowledgment of a periodic timeout is done too late (after the next
 * period has already passed), the default action is to skip the missed timeout
 * slot(s) and arm for the next valid slot. If the application never wants to
 * skip a missed timeout it can set the flag EM_TMO_FLAG_NOSKIP when creating a
 * timeout, which causes each acknowledgment to schedule an immediate timeout
 * event until all the missed time slots have been served. This keeps the number
 * of timeouts as expected but may cause an event storm if a long processing
 * delay has occurred.
 *
 * The timeout handle is needed when acknowledging a periodic timeout event.
 * Because any event can be used for the timeout, the application must itself
 * provide a way to derive the timeout handle from the received timeout event
 * (a typical way is to include the tmo handle within the timeout event).
 * The application also needs to have a mechanism to detect which event is a
 * periodic timeout to be able to acknowledge.
 *
 * Example usage
 * @code
 *
 *	// This would typically be done at application init.
 *	// Accept all defaults but require at least 100us resolution
 *	// and give a name.
 *	em_timer_attr_t attr;
 *	memset(&attr, 0, sizeof(em_timer_attr_t));
 *	attr.resolution = 100000;
 *	strncpy(attr.name, "100usTimer", EM_TIMER_NAME_LEN);
 *	em_timer_t tmr = em_timer_create(&attr);
 *	if(tmr == EM_TIMER_UNDEF) {
 *		// out of resources or too tight requirements
 *		// handle error here or via error handler
 *	}
 *
 *	// At runtime - create a timeout resource.
 *	// Can be done in advance to save time if the target queue is known.
 *	em_tmo_t tmo = em_tmo_create(tmr, EM_TIMER_FLAG_ONESHOT, target_queue);
 *	if(tmo == EM_TMO_UNDEF) {
 *		// no such timer or out of resources
 *		// handle error here or via error handler
 *	}
 *
 *	// Get the timer frequency
 *	uint64_t hz = em_timer_get_freq(tmr);
 *
 *	// Activate a 10ms timeout from now.
 *	// Very unlikely to fail with valid arguments.
 *	if (em_tmo_set_rel(tmo, hz / 100, my_tmo_event) != EM_OK) {
 *		// handle error here or via error handler
 *	}
 *
 * @endcode
 *
 * At the moment the timer is an 'add-on', i.e. not part of the base API exposed
 * by event_machine.h
 *
 */
#include <inttypes.h>
#include <event_machine/add-ons/event_machine_add-on_error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_TIMER_API_VERSION_MAJOR   1
#define EM_TIMER_API_VERSION_MINOR   0

/**
 * @typedef em_timer_t
 * System specific type for a timer handle.
 */

/**
 * @typedef em_tmo_t
 * System specific type for a timeout handle.
 */

/**
 * @typedef em_timer_flag_t
 * System specific type for timer flags.
 * This is system specific, but all implementations must define
 * EM_TIMER_FLAG_DEFAULT and EM_TIMER_FLAG_PRIVATE, of which the latter is used
 * to skip API synchronization for single threaded apps.
 * Flags can be combined by bitwise OR.
 */

/**
 * @typedef em_tmo_flag_t
 * System specific enum type for timeout flags.
 * This is system specific, but all implementations must define
 * EM_TMO_FLAG_ONESHOT, EM_TMO_FLAG_PERIODIC and EM_TMO_FLAG_NOSKIP.
 * Flags can be combined by bitwise OR.
 */

/**
 * @typedef em_timer_clksrc_t
 * System specific enum type for timer clock source.
 * This is system specific, but all implementations must define
 * EM_TIMER_CLKSRC_DEFAULT.
 */

/**
 * Visible state of a timeout
 */
typedef enum em_tmo_state_t {
	EM_TMO_STATE_UNKNOWN  = 0,
	EM_TMO_STATE_IDLE     = 1, /**< just created or canceled */
	EM_TMO_STATE_ACTIVE   = 2, /**< armed */
	EM_TMO_STATE_INACTIVE = 3  /**< oneshot expired */
} em_tmo_state_t;

/**
 * The timer tick has HW and timer specific meaning, but the type is always a
 * 64-bit integer and is normally assumed to be monotonic and not to wrap
 * around. Exceptions with exotic extra timers should be clearly documented.
 */
typedef uint64_t em_timer_tick_t;

/**
 * Include system specific configuration and types
 */
#include <event_machine/platform/add-ons/event_machine_timer_hw_specific.h>

/**
 * Structure used to create a timer (or inquire its capabilities).
 *
 * Setting a field to 0 means that the application accepts the default value.
 * Setting a non-zero value indicates a requirement. E.g. setting 'resolution'
 * to 1000(ns) requires at least 1us resolution. The timer creation will fail if
 * the implementation cannot support such high a resolution .
 * The implementation is free to provide better than requested, but not worse.
 */
typedef struct em_timer_attr_t {
	/** Timeout min resolution, ns. 0 for default */
	uint64_t resolution;
	/** Maximum timeout period, ns. 0 for default */
	uint64_t max_tmo;
	/** Min simultaneous timeouts. 0 for default */
	uint32_t num_tmo;
	/** Extra flags. A set flag is a requirement */
	em_timer_flag_t flags;
	/** Clock source (system specific). 0 for default */
	em_timer_clksrc_t clk_src;
	/** Optional name for this timer */
	char name[EM_TIMER_NAME_LEN];
} em_timer_attr_t;

/**
 * Create and start a timer resource
 *
 * Required attributes can be given via tmr_attr. The given attr structure
 * should be cleared (written with 0) before setting any value fields for
 * backwards compatibility.
 *
 * @param tmr_attr  Timer requirement parameters. NULL ok (accept all defaults)
 *
 * @return Timer handle on success or EM_TIMER_UNDEF on error
 */
em_timer_t em_timer_create(const em_timer_attr_t *tmr_attr);

/**
 * Stop and delete a timer
 *
 * Delete a timer, frees all resources.
 * All timeouts for this timer must have been deleted first.
 *
 * @param tmr  Timer handle
 *
 * @return EM_OK on success
 */
em_status_t em_timer_delete(em_timer_t tmr);

/**
 * Returns current tick value of the given timer
 *
 * This can be used for calculating absolute timeouts.
 *
 * @param tmr	Timer handle
 *
 * @return Current time in timer specific ticks or 0 on non-existing timer
 */
em_timer_tick_t em_timer_current_tick(em_timer_t tmr);

/**
 * Allocate a new timeout
 *
 * Create a new timeout. Allocates the necessary internal resources from the
 * given timer and prepares for em_tmo_set_abs/rel().
 *
 * Flags are used to select functionality:
 *   - EM_TMO_FLAG_ONESHOT creates a one-shot timeout and
 *   - EM_TMO_FLAG_PERIODIC creates a periodic timeout.
 *     The flag EM_TMO_FLAG_NOSKIP can, in the periodic case, be 'OR':d into the
 *     flags to make the timeout acknowledgment never skip a missed timeout (the
 *     default is to skip missed time slots).
 *
 * @param tmr        Timer handle
 * @param flags	     Functionality flags
 * @param queue	     Target queue where the timeout event should be delivered
 *
 * @return Timeout handle on success or EM_TMO_UNDEF on failure
 */
em_tmo_t em_tmo_create(em_timer_t tmr, em_tmo_flag_t flags, em_queue_t queue);

/**
 * Free a timeout
 *
 * Free (destroy) a timeout.
 * The user provided timeout event for an active timeout will be returned via
 * cur_event and the timeout is cancelled. The timeout event for an expired, but
 * not yet received timeout, will not be returned. It is the responsibility of
 * the application to handle that case.
 *
 * @param tmo              Timeout handle
 * @param [out] cur_event  Current event for an active timeout
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_delete(em_tmo_t tmo, em_event_t *cur_event);

/**
 * Activate a timeout (absolute time) with a user-provided timeout event
 *
 * Sets the timeout to expire at a specific absolute time. The timeout event
 * will be sent to the queue given to em_tmo_create() when the timeout expires.
 *
 * It is not possible to send timeouts with an event group (but the application
 * can assign the event group when receiving the timeout event,
 * see em_event_group_assign()).
 *
 * The timeout event should not be accessed after it has been given to the
 * timer.
 *
 * Even if not guaranteed, the implementation should make sure that this call
 * can fail only in highly exceptional situations (em_tmo_create() should pre-
 * allocate needed resources).
 *
 * Setting 'ticks' to an already passed time value is considered an error.
 *
 * An active timeout can not be modified. The timeout needs to be canceled and
 * then set again (tmo handle can be re-used) with new arguments.
 *
 * An inactive timeout can be re-used by calling em_tmo_set_abs/rel() again
 * after the previous timeout has expired.
 *
 * This function cannot be used to activate a periodic timer, instead use
 * em_tmo_set_rel().
 *
 * @param tmo        Timeout handle
 * @param ticks_abs  Expiration time in absolute timer specific ticks
 * @param tmo_ev     Timeout event
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_set_abs(em_tmo_t tmo, em_timer_tick_t ticks_abs,
			   em_event_t tmo_ev);

/**
 * Activate a timeout (relative time) with a user-provided timeout event
 *
 * Similar to em_tmo_set_abs() but, instead of an absolute time, uses a timeout
 * 'ticks' value relative to the moment of the call.
 *
 * The timeout event will be sent to the queue given to em_tmo_create() when the
 * timeout expires.
 *
 * This function is also used for activating a periodic timeout, in which case
 * the given 'ticks' is the timeout period (a periodic timeout is selected with
 * em_tmo_create(flags:EM_TMO_FLAG_PERIODIC).
 *
 * @param tmo        Timeout handle
 * @param ticks_rel  Expiration time in relative timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_set_rel(em_tmo_t tmo, em_timer_tick_t ticks_rel,
			   em_event_t tmo_ev);

/**
 * Cancel a timeout
 *
 * Cancels a timeout, preventing future expiration. Returns the timeout event,
 * given to em_tmo_set_abs/rel(), in case the timeout is still active.
 *
 * A timeout that has already expired cannot be cancelled and the timeout event
 * will be delivered to the destination queue. In this case, cancel will return
 * an error as it was too late to cancel. Cancel also fails if attempted before
 * timeout activation.
 *
 * @param tmo              Timeout handle
 * @param [out] cur_event  Event handle pointer to return the pending
 *                         timeout event or EM_EVENT_UNDEF if cancel fails
 *                         (e.g. called too late)
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_cancel(em_tmo_t tmo, em_event_t *cur_event);

/**
 * Acknowledge a periodic timeout.
 *
 * All received periodic timeout events must be acknowledged with em_tmo_ack().
 * No further timeout event(s) will be sent before the user has acknowledged
 * the previous ones.
 *
 * Timeout acknowledgment is usually done at the end of the EO-receive function
 * to prevent race conditions (e.g. if the same event is re-used for the next
 * timeout period also). The implementation will adjust for the processing delay
 * so that the time slot will not slip/drift over time.
 *
 * If em_tmo_ack() is called "too late", e.g. the next period(s) are already
 * passed, the implementation will by default skip all the missed time slots and
 * arm for the next future one. The application can, if skipping is undesired,
 * alter this behaviour with the flag EM_TMO_FLAG_NOSKIP when creating a
 * timeout - here no past timeout is skipped and each acknowledgment will
 * immediately trigger the next timeout until the current time has been reached.
 * Note that using EM_TMO_FLAG_NOSKIP may result in an event storm if a large
 * number of timeouts have been unacknowledged for a longer time.
 *
 * The application may re-use the same timeout event that was received or
 * provide a new one for the next timeout.
 * An error will be returned if the corresponding timeout has been cancelled.
 *
 * The given event should not be touched after calling this until it has been
 * received again.
 *
 * The given event must be handled by the application if an error is returned.
 * The periodic timeout will stop if em_tmo_ack() returns an error.
 *
 * em_tmo_ack() can only be used with periodic timeouts and will fail for
 * one-shot timeouts.
 *
 * @param tmo          Timeout handle
 * @param next_tmo_ev  Next timeout event handle (can be the current one)
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_ack(em_tmo_t tmo, em_event_t next_tmo_ev);

/**
 * Get a list of currently active timers
 *
 * Returned timer handles can be used to query for more information or to
 * destroy all existing timers.
 *
 * The actual number of timers is returned but 'tmr_list' will only be written
 * up to the given 'max' length (if there are more timers than the given 'max').
 *
 * @param [out] tmr_list  Pointer to a space for timer handles
 * @param max             Max number of handles that can written into tmr_list
 *
 * @return number of active timers
 */
int em_timer_get_all(em_timer_t *tmr_list, int max);

/**
 * Get timer attributes
 *
 * Returns the actual capabilities of the given timer.
 *
 * @param tmr             Timer handle
 * @param [out] tmr_attr  Pointer to em_timer_attr_t to fill
 *
 * @return EM_OK on success
 */
em_status_t em_timer_get_attr(em_timer_t tmr, em_timer_attr_t *tmr_attr);

/**
 * Returns the timer frequency, i.e. ticks per second for the given timer.
 *
 * Can be used to convert real time to timer specific tick unit.
 *
 * @param tmr  Timer handle
 *
 * @return ticks per second (Hz), or 0 for non-existing timer
 */
uint64_t em_timer_get_freq(em_timer_t tmr);

/**
 * Returns the current state of the given timeout.
 *
 * Note that the returned state may change at any time if the timeout expires
 * or is manipulated by other threads.
 *
 * @param tmo  Timeout handle
 *
 * @return current timeout state (EM_TMO_STATE_UNKNOWN on error)
 *
 * @see em_tmo_state_t
 */
em_tmo_state_t em_tmo_get_state(em_tmo_t tmo);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_TIMER_H_ */
