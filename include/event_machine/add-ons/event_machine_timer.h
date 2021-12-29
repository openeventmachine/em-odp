/*
 *   Copyright (c) 2016-2020, Nokia Solutions and Networks
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

#pragma GCC visibility push(default)

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
 * to enable the application to detect the situation. For a periodic timer,
 * a cancel will stop further timeouts, but may not be able to prevent the
 * latest event from being received.
 *
 * An active timeout cannot be altered without canceling it first.
 *
 * A timeout can be re-used after it has been received or successfylly
 * cancelled. Timeouts need to be deleted after use. Deletion frees the
 * resources reserved during creation.
 *
 * The timeout value is an abstract system and timer dependent tick count.
 * It is assumed that the tick count increases with a static frequency.
 * The frequency can be inquired at runtime for time calculations, e.g. tick
 * frequency divided by 1000 gives ticks for 1ms. Tick frequency is at least
 * equal to the resolution, but can also be higher (implementation can quantize
 * ticks to any underlying implementation). Supported resolution can also be
 * inquired.
 * A clock source can be specified when creating a timer. It defines the time
 * base of the timer for systems with multiple sources implemented (optional).
 * EM_TIMER_CLKSRC_DEFAULT is a portable value that implements a basic
 * monotonic time, that will not wrap back to zero in any reasonable uptime.
 *
 * A periodic timer requires the application to acknowledge each received
 * timeout event after it has been processed. The acknowledgment activates the
 * next timeout and compensates for the processing delay to keep the original
 * interval. This creates a flow control mechanism and also protects the event
 * handling from races if the same event is reused every time - the next
 * timeout will not be sent before the previous has been acknowledged.
 * The event to be received for each periodic timeout can also be different as
 * the next event is given by the application with the acknowledge.
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
 * provide a way to derive the timeout handle from the received timeout event.
 * A typical way is to include the tmo handle within the timeout event.
 * Application also needs to have a mechanism to detect which event is a
 * periodic timeout to be able to call acknowledge.
 *
 * Example usage
 * @code
 *
 *	// This would typically be done at application init.
 *	// Accept all defaults
 *	em_timer_attr_t attr;
 *	em_timer_attr_init(&attr);
 *	strncpy(attr.name, "myTimer", EM_TIMER_NAME_LEN);
 *	em_timer_t tmr = em_timer_create(&attr);
 *	if(tmr == EM_TIMER_UNDEF) {
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
/* Include system specific configuration and types */
#include <event_machine/platform/add-ons/event_machine_timer_hw_specific.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Major API version, marks possibly not backwards compatible changes */
#define EM_TIMER_API_VERSION_MAJOR   2
/** Minor version, backwards compatible changes or additions */
#define EM_TIMER_API_VERSION_MINOR   1

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
 * Type for timer resolution parameters
 *
 * This structure is used to group timer resolution parameters that may
 * affect each other.
 * All time values are ns.
 *
 * @note This is used both as capability and configuration. When used as configuration
 *	 either res_ns or res_hz must be 0 (for em_timer_create).
 * @see em_timer_capability, em_timer_create
 */
typedef struct em_timer_res_param_t {
	/** Clock source (system specific) */
	em_timer_clksrc_t clk_src;
	/** resolution, ns */
	uint64_t res_ns;
	/** resolution, hz */
	uint64_t res_hz;
	/** minimum timeout, ns */
	uint64_t min_tmo;
	/** maximum timeout, ns */
	uint64_t max_tmo;
} em_timer_res_param_t;

/**
 * Structure used to create a timer or inquire its configuration later.
 *
 * This needs to be initialized with em_timer_attr_init(), which fills default
 * values to each field. After that the values can be modified as needed.
 * Values are considered as a requirement, e.g. setting 'resparam.res_ns' to 1000(ns)
 * requires at least 1us resolution. The timer creation will fail if the implementation
 * cannot support such resolution (like only goes down to 1500ns).
 * The implementation is free to provide better than requested, but not worse.
 *
 * To know the implementation specific limits use em_timer_capability and em_timer_res_capability.
 *
 * @note Value 0 as default is no longer supported and use of em_timer_attr_init is mandatory
 * @see em_timer_attr_init, em_timer_capability
 */
typedef struct em_timer_attr_t {
	/** Resolution parameters */
	em_timer_res_param_t resparam;
	/** Maximum simultaneous timeouts */
	uint32_t num_tmo;
	/** Extra flags. A set flag is a requirement */
	em_timer_flag_t flags;
	/** Optional name for this timer */
	char name[EM_TIMER_NAME_LEN];

	/**
	 * Internal check - don't touch!
	 *
	 * EM will verify that em_timer_attr_init() has been called before
	 * creating a timer
	 */
	uint32_t __internal_check;
} em_timer_attr_t;

/**
 * Timeout statistics counters
 *
 * Some fields relate to periodic timeout only (0 on one-shots) and vice versa.
 * New fields may be added later at the end.
 */
typedef struct em_tmo_stats_t {
	/** number of periodic ack() calls */
	uint64_t num_acks;
	/** number of delayed periodic ack() calls */
	uint64_t num_late_ack;
	/** number of skipped periodic timeslots due to late ack */
	uint64_t num_period_skips;
} em_tmo_stats_t;

/**
 * Timer capability info
 */
typedef struct em_timer_capability_t {
	/** Number of supported timers */
	uint32_t max_timers;
	/** Maximum number of simultanous timeouts. 0 means only limited by memory */
	uint32_t max_num_tmo;
	/** Highest supported resolution and related limits for a timeout */
	em_timer_res_param_t max_res;
	/** Longest supported timeout and related resolution */
	em_timer_res_param_t max_tmo;
} em_timer_capability_t;

/**
 * Initialize em_timer_attr_t
 *
 * Initializes em_timer_attr_t to system specific default values.
 * After initialization user can adjust the values as needed before
 * calling em_timer_create. em_timer_capability() and/or em_timer_res_capability()
 * can optionally be used to find valid values.
 *
 * Always initialize em_timer_attr_t with em_timer_attr_init before any use.
 *
 * This function will not trigger errorhandler calls internally.
 *
 * Example for all defaults
 * @code
 *	em_timer_attr_t tmr_attr;
 *	em_timer_attr_init(&tmr_attr);
 *	em_timer_t tmr = em_timer_create(&tmr_attr);
 * @endcode
 *
 * @param tmr_attr  Pointer to em_timer_attr_t to be initialized
 *
 * @see em_timer_capability, em_timer_create
 */
void em_timer_attr_init(em_timer_attr_t *tmr_attr);

/**
 * Inquire timer capabilities
 *
 * Returns timer capabilities for the given clock source, which is also written
 * to both 'capa->max_res.clk_src' and 'capa->max_tmo.clk_src'.
 * For resolution both 'res_ns' and 'res_hz' are filled.
 *
 * This function will not trigger errorhandler calls internally.
 *
 * @param capa		pointer to em_timer_capability_t to be updated
 *			(does not need to be initialized)
 * @param clk_src	Clock source to use for timer
 *			(EM_TIMER_CLKSRC_DEFAULT for system specific default)
 * @return EM_OK if the given clk_src is supported (capa updated)
 *
 * @see em_timer_capability_t, em_timer_res_capability
 */
em_status_t em_timer_capability(em_timer_capability_t *capa, em_timer_clksrc_t clk_src);

/**
 * Inquire timer capabilities for a specific resolution or maximum timeout
 *
 * Returns timer capabilities by given resolution or maximum timeout.
 * Set one of resolution (res.res_ns) or maximum timeout (res.max_tmo) to required value
 * and the other to zero and this will fill the other fields with valid limits.
 * Error is returned if the given value is not supported.
 * The given clk_src is used to set the values and also written to 'res->clk_src'.
 * Both 'res_ns' and 'res_hz' are filled, so if this is passed to em_timer_create,
 * one of those must be set to 0.
 *
 * Example for external clock maximum resolution
 * @code
 *	em_timer_attr_t *tmr_attr;
 *	em_timer_capability_t capa;
 *
 *	em_timer_attr_init(&tmr_attr);
 *	if (em_timer_capability(&capa, EM_TIMER_CLKSRC_EXT) != EM_OK) {
 *		// external clock not supported
 *	}
 *	tmr_attr.resparam = capa.max_res;
 *	tmr_attr.resparam.res_hz = 0;
 *	tmr = em_timer_create(&tmr_attr);
 * @endcode
 *
 * This function will not trigger errorhandler calls internally.
 *
 * @param res		Pointer to em_timer_res_param_t with one field set
 * @param clk_src	Clock source to use for timer
 *			(EM_TIMER_CLKSRC_DEFAULT for system specific default)
 * @return EM_OK if the input value is supported (res updated)
 *
 * @see em_timer_capability
 */
em_status_t em_timer_res_capability(em_timer_res_param_t *res, em_timer_clksrc_t clk_src);

/**
 * Create and start a timer resource
 *
 * Required attributes are given via tmr_attr. The given structure must be
 * initialiazed with em_timer_attr_init before setting any field.
 *
 * Timer resolution can be given as time 'res_ns' or frequency 'res_hz'. User
 * must choose which one to use by setting the other one to 0.
 *
 * To use all defaults initialize tmr_attr with em_timer_attr_init() and pass it
 * as is to em_timer_create().
 *
 * @note NULL is no longer supported, must give pointer to initialized em_timer_attr_t
 *
 * @param tmr_attr  Timer parameters to use, pointer to initialized em_timer_attr_t
 *
 * @return Timer handle on success or EM_TIMER_UNDEF on error
 *
 * @see em_timer_attr_init, em_timer_capability
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
 * @param tmr  Timer handle
 *
 * @return Current time in timer specific ticks or 0 on non-existing timer
 */
em_timer_tick_t em_timer_current_tick(em_timer_t tmr);

/**
 * Allocate a new timeout
 *
 * Create a new timeout. Allocates the necessary internal resources from the
 * given timer and prepares for em_tmo_set_abs/rel/periodic().
 *
 * Scheduled queues are always supported. LOCAL or OUTPUT queues can not be
 * used as timeout targets. Support for unscheduled queues is implementation
 * specific.
 *
 * Flags are used to select functionality:
 *   - EM_TMO_FLAG_ONESHOT creates a one-shot timeout and
 *   - EM_TMO_FLAG_PERIODIC creates a periodic timeout.
 *     The flag EM_TMO_FLAG_NOSKIP can, in the periodic case, be 'OR':d into the
 *     flags to make the timeout acknowledgment never skip a missed timeout (the
 *     default is to skip missed time slots).
 *
 * @param tmr        Timer handle
 * @param flags      Functionality flags
 * @param queue      Target queue where the timeout event should be delivered
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
 * not yet received timeout will not be returned. It is the responsibility of
 * the application to handle that case (event will still be received).
 *
 * After and during this call the tmo handle is not valid anymore and must not be used.
 * With periodic timeout means em_tmo_ack must also not be called when tmo is deleted.
 *
 * @param      tmo        Timeout handle
 * @param[out] cur_event  Current event for an active timeout
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_delete(em_tmo_t tmo, em_event_t *cur_event);

/**
 * Activate a oneshot timeout (absolute time)
 *
 * Sets a oneshot timeout to expire at a specific absolute time. The timeout
 * event will be sent to the queue given to em_tmo_create() when the timeout
 * expires.
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
 * Setting 'ticks_abs' to an already passed or too near time returns EM_ERR_TOONEAR.
 * Setting 'ticks_abs' to a time value too far ahead (beoynd maximum timeout)
 * returns EM_ERR_TOOFAR. Both of these situations will not trigger calls to
 * errorhandler and need to be handled using the return code (event not taken).
 * Minimum and maximum timeout can be inquired with em_timer_res_capability.
 *
 * An active timeout can not be modified. The timeout needs to be canceled and
 * then set again (tmo handle can be re-used) with new arguments.
 *
 * An inactive timeout can be re-used by calling em_tmo_set_abs/rel() again
 * after the previous timeout has expired.
 *
 * This function is for activating oneshot timeouts only. To activate
 * a periodic timer use em_tmo_set_periodic() instead.
 *
 * @param tmo        Timeout handle
 * @param ticks_abs  Expiration time in absolute timer specific ticks
 * @param tmo_ev     Timeout event
 *
 * @retval EM_OK		success (event taken)
 * @retval EM_ERR_TOONEAR	failure, tick value is past or too close to current time
 * @retval EM_ERR_TOOFAR	failure, tick value exceeds timer capability (too far ahead)
 * @retval (other_codes)	other failure
 *
 * @see em_timer_res_capability
 */
em_status_t em_tmo_set_abs(em_tmo_t tmo, em_timer_tick_t ticks_abs,
			   em_event_t tmo_ev);

/**
 * Activate a timeout (relative time)
 *
 * Similar to em_tmo_set_abs(), but instead of an absolute time uses a timeout
 * 'ticks' value relative to the moment of the call.
 *
 * The timeout event will be sent to the queue given to em_tmo_create() when the
 * timeout expires.
 *
 * Using ticks value too small or too large is considered an error here
 * (em_timer_res_capability can be used to check the limits).
 *
 * This function is primarily meant for activating oneshot timeouts but can
 * still, for backwards compatibility reasons, be used for activating periodic
 * timeouts, in which case the given 'ticks' is the timeout period (a periodic
 * timeout is selected with em_tmo_create() flag EM_TMO_FLAG_PERIODIC).
 * Prefer em_tmo_set_periodic() for activating periodic timeouts.
 *
 * @param tmo        Timeout handle
 * @param ticks_rel  Expiration time in relative timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @return EM_OK on success
 *
 * @deprecated Do not use for periodic timeouts
 * @see em_tmo_set_periodic
 */
em_status_t em_tmo_set_rel(em_tmo_t tmo, em_timer_tick_t ticks_rel,
			   em_event_t tmo_ev);

/**
 * Activate a periodic timeout
 *
 * Used to activate periodic timeouts. The first period can be different than
 * the repetitive period by providing an absolute start time 'start_abs' (the
 * first period starts from that moment). Use 0 as start time if the period
 * can start from the moment of the call, i.e. the first period is relative and
 * same as the rest.
 * If a different relative time is needed for the first timeout then use
 * 'em_timer_current_tick() + period' as the start time.
 *
 * The timeout event will be sent to the queue given to em_tmo_create() when the
 * timeout expires (em_tmo_ack() can recycle the same event or provide a new
 * one for the next period).
 *
 * This function can only be used with periodic timeouts (created with flag
 * EM_TMO_FLAG_PERIODIC).
 *
 * @param tmo        Timeout handle
 * @param start_abs  Absolute start time (or 0 for period starting at call)
 * @param period     Period in timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @retval EM_OK		success
 * @retval EM_ERR_TOONEAR	failure, start tick value is past or too close to current time
 * @retval EM_ERR_TOOFAR	failure, start tick value exceeds timer capability (too far ahead)
 * @retval (other_codes)	failure
 */
em_status_t em_tmo_set_periodic(em_tmo_t tmo,
				em_timer_tick_t start_abs,
				em_timer_tick_t period,
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
 * timeout activation (em_tmo_set_).
 *
 * With periodic timeout cancel may also fail, if done too late for the given
 * period. This is considered normal and indicates that one more timeout will be
 * received. The next call to em_tmo_ack will then return EM_ERR_CANCELED.
 *
 * @param      tmo        Timeout handle
 * @param[out] cur_event  Event handle pointer to return the pending
 *                        timeout event or EM_EVENT_UNDEF if cancel fails
 *                        (e.g. called too late)
 *
 * @return EM_OK on success
 * @see em_tmo_set_abs, em_tmo_set_rel, em_tmo_set_periodic
 */
em_status_t em_tmo_cancel(em_tmo_t tmo, em_event_t *cur_event);

/**
 * Acknowledge a periodic timeout
 *
 * All received periodic timeout events must be acknowledged with em_tmo_ack().
 * No further timeout event(s) will be sent before the user has acknowledged
 * the previous one.
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
 * number of timeouts have been unacknowledged for a longer time. Timing problems
 * will not cause errorhandler calls.
 *
 * If the timer has been canceled, but the cancel happened too late for the
 * current period the timeout will be delivered. If application then calls em_tmo_ack,
 * it returns EM_ERR_CANCELED and does not call errorhandler.
 *
 * The application may re-use the same timeout event that was received or
 * provide a new one for the next timeout.
 *
 * The given event should not be touched after calling this function until it
 * has been received again (or after the timeout is successfully cancelled)
 *
 * The given event is not taken and must be handled by the application if an
 * error is returned.
 * The periodic timeout will stop if em_tmo_ack() returns an error.
 * The implementation will call errorhandler in this case (unless timer was
 * canceled), so the exception can be handled also there.
 *
 * em_tmo_ack() can only be used with periodic timeouts and will fail for
 * one-shot timeouts.
 *
 * @param tmo          Timeout handle
 * @param next_tmo_ev  Next timeout event handle (can be the current one)
 *
 * @retval EM_OK		success
 * @retval EM_ERR_CANCELED	timer has been cancelled
 * @retval (other_codes)	failure
 */
em_status_t em_tmo_ack(em_tmo_t tmo, em_event_t next_tmo_ev);

/**
 * Get a list of currently active timers.
 *
 * Returned timer handles can be used to query more information or to
 * destroy all existing timers.
 *
 * The actual number of timers is always returned but 'tmr_list' will only be
 * written up to the given 'max' length.
 *
 * @param [out] tmr_list  Pointer to array of timer handles
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
 * Can be used to convert real time to timer specific tick.
 *
 * @param tmr  Timer handle
 *
 * @return ticks per second (Hz), or 0 for non-existing timer
 */
uint64_t em_timer_get_freq(em_timer_t tmr);

/**
 * Convert timer tick to ns
 *
 * @param tmr		Valid timer handle
 * @param ticks		Timer specific ticks to convert
 *
 * @return converted amount in ns
 */
uint64_t em_timer_tick_to_ns(em_timer_t tmr, em_timer_tick_t ticks);

/**
 * Convert ns to timer tick
 *
 * @param tmr		Valid timer handle
 * @param ns		ns value to convert
 *
 * @return converted amount in timer ticks
 */
em_timer_tick_t em_timer_ns_to_tick(em_timer_t tmr, uint64_t ns);

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
 * Returns the statistic counters for a timeout.
 *
 * Returns a snapshot of the current counters of the given timeout.
 * Statistics can be accessed while the timeout is valid, i.e. tmo created but
 * not deleted.
 *
 * Counter support is optional. If counters are not supported the function
 * returns EM_ERR_NOT_IMPLEMENTED.
 * A quick way to detect whether counters are supported is to call the function
 * with stat=NULL and check the return value.
 *
 * @param tmo         Timeout handle
 * @param [out] stat  Pointer to em_tmo_stats_t to receive the values (NULL ok)
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_get_stats(em_tmo_t tmo, em_tmo_stats_t *stat);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_TIMER_H_ */
