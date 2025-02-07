/*
 *   Copyright (c) 2016-2024, Nokia Solutions and Networks
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
 *  Event Machine timer
 * @defgroup em_timer Event timer
 *  Event Machine timer
 *  @{
 *
 * The timer API can be used to request an event to be sent to a specified
 * queue at a specified time once (one-shot) or at regular intervals (periodic).
 * A timer needs to be created first - it represents a collection of timeouts
 * with certain attributes (e.g. timeout resolution and maximum period).
 * A timer can be mapped to a HW resource on an SoC, thus the number of timers,
 * capabilities and time bases are system specific. Typically only a few timers
 * are supported.
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
 * allocates the resources needed to serve the timeout, but does not arm it.
 * This makes it possible to pre-create timeout(s) and set the expiry
 * later at runtime. This can improve performance but also minimizes the
 * possibility that the runtime call setting the expiry would fail, as resources
 * have already been reserved beforehand.
 *
 * A pending timeout can be cancelled. Note that there is no way to cancel an
 * expired timeout for which the event has already been sent but not yet
 * received by the application. Canceling in this case will return an error
 * to enable the application to detect the situation. For a periodic timer,
 * a cancel will stop further timeouts, but may not be able to prevent the
 * latest event from being received. An active timeout cannot be altered without
 * canceling it first.
 *
 * A timeout can be reused after the timeout event has been received or when
 * successfully cancelled. Timeouts need to be deleted after use. Deletion frees
 * the resources reserved during creation.
 *
 * The timeout value is an abstract system and timer dependent tick count.
 * It is assumed that the tick count increases with a static frequency.
 * The frequency can be inquired at runtime for time calculations, e.g. tick
 * frequency divided by 1000 gives ticks for 1ms. The tick frequency is at least
 * equal to the resolution, but can also be higher (implementation can quantize
 * ticks to any underlying implementation). The supported resolution can also be
 * inquired.
 * A clock source can be specified when creating a timer. It defines the time
 * base of the timer for systems with multiple sources implemented (optional).
 * EM_TIMER_CLKSRC_DEFAULT is a portable value that implements a basic
 * monotonic time, that will not wrap back to zero in any reasonable uptime.
 *
 * Events with major event types EM_EVENT_TYPE_SW, EM_EVENT_TYPE_PACKET and
 * EM_EVENT_TYPE_TIMER can be used as timeout events to indicate expiry. The
 * type EM_EVENT_TYPE_TIMER is an alternative to EM_EVENT_TYPE_SW and works the
 * same way. Additionally, for periodic ring timer only, the type
 * EM_EVENT_TYPE_TIMER_IND is used. This is a special timeout indication event
 * without visible payload.
 *
 * Regular periodic timeouts:
 * (i.e. NOT periodic ring timer timeouts, see differences further down)
 * A periodic timer requires the application to acknowledge each received
 * timeout event after it has been processed. The acknowledgment activates the
 * next timeout and compensates for the processing delay to keep the original
 * interval. This creates a flow control mechanism and also protects the event
 * handling from races if the same event is reused every time - the next
 * timeout will not be sent before the previous has been acknowledged.
 * The event to be received for each periodic timeout can also be different as
 * the next event is given by the application via the acknowledgment.
 * The target queue cannot be modified after the timeout has been created.
 *
 * If the acknowledgment of a periodic timeout is done too late (after the next
 * period has already passed), the default action is to skip the missed timeout
 * slot(s) and arm for the next valid slot. If the application never wants to
 * skip a missed timeout it can set the flag EM_TMO_FLAG_NOSKIP when creating a
 * timeout. This causes each acknowledgment to schedule an immediate timeout
 * event until all the missed time slots have been served. This keeps the number
 * of timeouts as expected but may cause an event storm if a long processing
 * delay has occurred.
 *
 * The timeout handle is needed when acknowledging a periodic timeout event.
 * Because any event can be used for the timeout, the application must itself
 * provide a way to derive the timeout handle from the received timeout event.
 * A typical way is to include the tmo handle within the timeout event.
 * The application also needs to have a mechanism to detect which event is a
 * periodic timeout to be able to acknowledge it via em_tmo_ack().
 *
 * If the requested timeout tick value for a timeout is in the past or is too
 * close to the current time then the error code EM_ERR_TOONEAR is returned.
 * In this case EM will not call the error handler - instead EM lets the
 * application decide whether to treat the situation as an error or to try again
 * with an updated target time.
 *
 * Periodic ring timer:
 * There is also an alternative periodic ring timer. It uses a different
 * abstraction and is created and started via separate ring specific APIs.
 * It has three main differences to the regular periodic timeouts:
 *   1. Only a pre-defined read-only event type can be used and is provided
 *      by the timer (EM_EVENT_TYPE_TIMER_IND).
 *   2. Flow control is not supported. Some implementations may have it,
 *      but the specification does not quarantee any so the user needs to be
 *      prepared to see the same event enqueued multiple times if handling of
 *      the received timeouts is not fast enough.
 *   2. A limited set of period times are supported per timer (the base rate or
 *      an integer multiple thereof).
 *
 * Ring timers can be thought of as a clock face ticking the pointer forward.
 * One cycle around is the base rate (minimum rate). The same timeout can be
 * inserted into multiple locations evenly spread within the clock face thus
 * multiplying the base rate. The starting offset can be adjusted only up to
 * one timeout period.
 * Depending on platform, this mode may provide better integration with HW and
 * thus have less runtime overhead. However, as it exposes a potential queue
 * overflow and a race hazard (race avoidable by using atomic queue as target),
 * regular periodic timeouts are recommended as a default.
 *
 * Example usage
 * @code
 *
 *	// This would typically be done at application init.
 *	// Accept all defaults but change the name
 *	em_timer_attr_t attr;
 *	em_timer_attr_init(&attr);
 *	strncpy(attr.name, "myTimer", EM_TIMER_NAME_LEN);
 *	em_timer_t tmr = em_timer_create(&attr);
 *	if(tmr == EM_TIMER_UNDEF) {
 *		// handle error here or via the error handler
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
 *	// Get the timer tick frequency
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
 */
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

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
	EM_TMO_STATE_INACTIVE = 3  /**< unused state */
} em_tmo_state_t;

/**
 * Type returned by em_tmo_get_type()
 */
typedef enum em_tmo_type_t {
	EM_TMO_TYPE_NONE     = 0, /**< unknown or not a timer-related event */
	EM_TMO_TYPE_ONESHOT  = 1, /**< event is a oneshot timeout indication */
	EM_TMO_TYPE_PERIODIC = 2, /**< event is a periodic timeout indication */
} em_tmo_type_t;

/**
 * The timer tick has HW and timer specific meaning, but the type is always a
 * 64-bit integer and is normally assumed to be monotonic and not to wrap
 * around. Exceptions with exotic extra timers should be clearly documented.
 */
typedef uint64_t em_timer_tick_t;

/**
 * Fractional 64-bit unsigned value for timer frequency.
 */
typedef struct em_fract_u64_t {
		/** Int */
		uint64_t integer;

		/** Numerator. Set 0 for integers */
		uint64_t numer;

		/** Denominator */
		uint64_t denom;
} em_fract_u64_t;

/**
 * Type for timer resolution parameters.
 *
 * This structure is used to group timer resolution parameters that may affect
 * each other. All time values are in nanoseconds (ns).
 *
 * @note This type used both as capability and configuration. When used as
 *       configuration either res_ns or res_hz must be 0 (for em_timer_create()).
 * @see em_timer_capability(), em_timer_create()
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
 * Periodic timer ring timing parameters.
 */
typedef struct em_timer_ring_param_t {
	/** Clock source (system specific) */
	em_timer_clksrc_t clk_src;
	/** Base rate, i.e. minimum period rate */
	em_fract_u64_t base_hz;
	/** Maximum base rate multiplier needed. 1 for single rate = base_hz */
	uint64_t max_mul;
	/** Resolution */
	uint64_t res_ns;
} em_timer_ring_param_t;

/**
 * EM timer attributes.
 *
 * The type is used when creating a timer or inquiring its configuration later.
 *
 * This needs to be initialized with em_timer_attr_init(), which fills default
 * values to each field. After that the values can be modified as needed.
 * Values set are considered a requirement, e.g. setting 'resparam.res_ns' to
 * 1000(ns) requires the timer to have at least 1us resolution. The timer
 * creation will fail if the implementation cannot support such a resolution
 * (e.g. if it only goes down to 1500ns).
 * The implementation is free to provide better than requested, but not worse.
 *
 * To know the implementation specific limits, use em_timer_capability() and
 * em_timer_res_capability().
 *
 * When creating the alternative periodic ring timer, this type needs to be
 * initialized with em_timer_ring_attr_init() instead. EM_TIMER_FLAG_RING will
 * be set by em_timer_ring_attr_init() so it does not need to be manually set.
 *
 * @see em_timer_attr_init(), em_timer_create(),
 *      em_timer_ring_attr_init(), em_timer_ring_create(),
 *      em_timer_
 */
typedef struct em_timer_attr_t {
	/**
	 * Resolution parameters for em_timer_create().
	 * Used when creating normal one shot or periodic timers, but not when
	 * creating periodic ring timers (see ringparam below instead).
	 * (cleared by em_timer_ring_attr_init() when using a ring timer)
	 */
	em_timer_res_param_t resparam;

	/** Maximum simultaneous timeouts */
	uint32_t num_tmo;
	/** Extra flags. A set flag is a requirement */
	em_timer_flag_t flags;
	/** Optional name for this timer */
	char name[EM_TIMER_NAME_LEN];

	/**
	 * Parameters specifically for em_timer_ring_create().
	 * Used when creating an alternative periodic ring timer.
	 * (cleared by em_timer_attr_init() since not needed in that case)
	 */
	em_timer_ring_param_t ringparam;

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
	/** number of delayed periodic ack() calls. 0 with ring timer */
	uint64_t num_late_ack;
	/** number of skipped periodic timeslots due to late ack. 0 with ring timer */
	uint64_t num_period_skips;
} em_tmo_stats_t;

/**
 * Timer capability info
 */
typedef struct em_timer_capability_t {
	/** Number of supported timers of all types */
	uint32_t max_timers;
	/** Maximum number of simultaneous timeouts. 0 means only limited by memory */
	uint32_t max_num_tmo;
	/** Highest supported resolution and related limits for a timeout */
	em_timer_res_param_t max_res;
	/** Longest supported timeout and related resolution */
	em_timer_res_param_t max_tmo;

	/** alternate periodic ring */
	struct {
		/** Maximum ring timers */
		uint32_t max_rings;
		/** Maximum simultaneous ring timeouts */
		uint32_t max_num_tmo;
		/** Minimum base_hz */
		em_fract_u64_t min_base_hz;
		/** Minimum base_hz */
		em_fract_u64_t max_base_hz;
	} ring;

} em_timer_capability_t;

/**
 * tmo optional extra arguments
 *
 */
typedef struct em_tmo_args_t {
	/** can be used with ring timer, see em_tmo_get_userptr */
	void *userptr;
} em_tmo_args_t;

/**
 * Initialize em_timer_attr_t for normal timers (i.e. NOT periodic ring timers).
 *
 * Initializes em_timer_attr_t to system specific default values.
 * The user can after initialization adjust the values as needed before
 * calling em_timer_create(). The functions em_timer_capability() and/or
 * em_timer_res_capability() can optionally be used to find valid values.
 *
 * Always initialize em_timer_attr_t with em_timer_attr_init() before use.
 *
 * This function will not trigger EM error handler calls internally.
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
 * Initialize em_timer_attr_t for periodic ring timers.
 *
 * Initializes em_timer_ring_attr_t according to given values.
 * After successful return, the attributes can be given to em_timer_ring_create().
 * Note, that if the implementation cannot use the exact given combination it may
 * update the ring_attr values, but always to meet or exceed the given values.
 * The user can read the new values to determine if they were modified.
 * An error is returned if the given values cannot be met.
 *
 * Before creating the ring timer, other values like num_tmo and name can be
 * adjusted as needed. Also, if a non-integer frequency is needed, the base_hz
 * fractional part can be adjusted before em_timer_ring_create().
 *
 * This function will not trigger error handler calls.
 *
 * @param[out]  ring_attr  Pointer to em_timer_attr_t to be initialized
 * @param       clk_src    Clock source to use (system specific or portable
 *                         EM_TIMER_CLKSRC_DEFAULT)
 * @param       base_hz    Base rate of the ring (minimum rate i.e. longest period)
 * @param       max_mul    Maximum multiplier (maximum rate = base_hz * max_mul)
 * @param       res_ns     Required resolution of the timer or 0 to accept default
 *
 * @return EM_OK if the given clk_src and other values are supported
 *
 * @see em_timer_ring_capability(), em_timer_ring_create()
 */
em_status_t em_timer_ring_attr_init(em_timer_attr_t *ring_attr,
				    em_timer_clksrc_t clk_src,
				    uint64_t base_hz,
				    uint64_t max_mul,
				    uint64_t res_ns);

/**
 * Inquire timer capabilities
 *
 * Returns timer capabilities for the given clock source, which is also written
 * to both 'capa->max_res.clk_src' and 'capa->max_tmo.clk_src'.
 * For resolution both 'res_ns' and 'res_hz' are filled.
 *
 * This function will not trigger error handler calls internally.
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
 * Returns timer capabilities by the given resolution or maximum timeout.
 * Set either the resolution (res.res_ns) or the maximum timeout (res.max_tmo)
 * to the required value and the other to zero, and the function will fill the
 * other fields with valid limits.
 * An error is returned if the given value is not supported.
 * The given clk_src is used to set the values and also written to 'res->clk_src'.
 * Both 'res_ns' and 'res_hz' are filled, so if passed further to em_timer_create(),
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
 * This function will not trigger error handler calls internally.
 *
 * @param res      Pointer to em_timer_res_param_t with one field set
 * @param clk_src  Clock source to use for timer
 *                 (EM_TIMER_CLKSRC_DEFAULT for system specific default)
 * @return EM_OK if the input value is supported (res updated)
 *
 * @see em_timer_capability
 */
em_status_t em_timer_res_capability(em_timer_res_param_t *res, em_timer_clksrc_t clk_src);

/**
 * @brief Check periodic ring timer capability.
 *
 * Returns the ring timer capability based on the given input values.
 * The parameter 'ring' must be initialized with the values required by the user.
 * The ring.res_ns can be 0 and gets replaced by the system default.
 * The values are updated during the call. If EM_OK is returned then the
 * combination of given values are all supported (or exceeded, e.g. better
 * resolution), otherwise values are updated with the closest supported.
 *
 * As em_timer_ring_attr_init() only takes integer base_hz, this can also be
 * used to verify valid values for modified fractional frequencies to avoid
 * error handler calls from em_timer_ring_create().
 *
 * This function will not trigger error handler calls.
 *
 * @param ring[in,out]  timer ring parameters to check
 *
 * @retval EM_OK                 Parameter combination is supported
 * @retval EM_ERR_NOT_SUPPORTED  Parameters not supported, values updated to closest
 * @retval (other error)         Unsupported arguments
 */
em_status_t em_timer_ring_capability(em_timer_ring_param_t *ring);

/**
 * Create and start a timer resource
 *
 * Required attributes are given via tmr_attr. The given structure must be
 * initialized with em_timer_attr_init() before setting any field.
 *
 * Timer resolution can be given as time 'res_ns' or frequency 'res_hz'.
 * The user must choose which one to use by setting the other one to 0.
 *
 * To use all defaults, initialize tmr_attr with em_timer_attr_init() and pass
 * it as is to em_timer_create().
 *
 * @param tmr_attr  Timer parameters to use, pointer to an initialized em_timer_attr_t
 * @note NULL is no longer supported, pointer must be to an initialized em_timer_attr_t
 *
 * @return Timer handle on success or EM_TIMER_UNDEF on error
 *
 * @see em_timer_attr_init(), em_timer_capability()
 */
em_timer_t em_timer_create(const em_timer_attr_t *tmr_attr);

/**
 * Create and start a periodic timer ring (alternative periodic timer)
 *
 * The required attributes are given via ring_attr, which must have been
 * initialized with em_timer_ring_attr_init() and optionally adjusted for the
 * required timing constraints.
 *
 * A periodic ring timer is a bit different and will only send
 * EM_EVENT_TYPE_TIMER_IND timeout events, which are automatically provided and
 * cannot be modified. These events can be allocated only via timer APIs.
 *
 * Example for 1ms ... 125us periodic ring timer (base 1000 hz, multiplier up to 8):
 * @code
 *	em_timer_ring_attr_t attr;
 *	if (em_timer_ring_attr_init(&attr, EM_TIMER_CLKSRC_DEFAULT, 1000, 8, 0) != EM_OK) {
 *		// given values not supported
 *	}
 *
 *	em_timer_t tmr = em_timer_ring_create(&attr);
 *	if (tmr == EM_TIMER_UNDEF) {
 *		// handle error here or via error handler
 *	}
 * @endcode
 *
 * @param ring_attr  Timer ring parameters to use
 *
 * @return Timer handle on success or EM_TIMER_UNDEF on error
 *
 * @see em_timer_ring_attr_init
 */
em_timer_t em_timer_ring_create(const em_timer_attr_t *ring_attr);

/**
 * Stop and delete a timer
 *
 * Delete a timer, free all resources.
 * All timeouts for this timer must have been cancelled and deleted first.
 *
 * @param tmr  Timer handle
 *
 * @return EM_OK on success
 */
em_status_t em_timer_delete(em_timer_t tmr);

/**
 * Return the current tick value of the given timer
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
 * Scheduled queues are always supported as timeout event destinations. LOCAL or
 * OUTPUT queues can not be used as timeout targets. Support for unscheduled
 * queues is implementation specific.
 *
 * Flags are used to select functionality:
 *   - EM_TMO_FLAG_ONESHOT creates a one-shot timeout and
 *   - EM_TMO_FLAG_PERIODIC creates a periodic timeout.
 *     The flag EM_TMO_FLAG_NOSKIP can, in the periodic case, be 'OR':d into the
 *     flags to make the timeout acknowledgment never skip a missed timeout (the
 *     default is to skip missed time slots).
 *
 * The NOSKIP flag is ignored if used timer is a periodic timer ring.
 *
 * @param tmr    Timer handle
 * @param flags  Functionality flags
 * @param queue  Target queue where the timeout event should be delivered
 *
 * @return Timeout handle on success or EM_TMO_UNDEF on failure
 */
em_tmo_t em_tmo_create(em_timer_t tmr, em_tmo_flag_t flags, em_queue_t queue);

/**
 * Allocate a new timeout with extra arguments
 *
 * Similar to em_tmo_create() but with an additional 'args' pointer. This API
 * can be used with any timer type, but 'args->userptr' is only meaningful for
 * ring timers using events of type EM_EVENT_TYPE_TIMER_IND that can carry a
 * 'userptr'.
 *
 * @param tmr        Timer handle
 * @param flags      Functionality flags
 * @param queue      Target queue where the timeout event should be delivered
 * @param args       Optional pointer holding extra arguments e.g. userptr for
 *		     ring timers. NULL ok.
 *
 * @return Timeout handle on success or EM_TMO_UNDEF on failure
 * @see em_tmo_create
 */
em_tmo_t em_tmo_create_arg(em_timer_t tmr, em_tmo_flag_t flags, em_queue_t queue,
			   em_tmo_args_t *args);

/**
 * Delete a timeout
 *
 * The deleted timeout must be inactive i.e. it must be successfully canceled or
 * the last timeout event must have been received (following too late a cancel).
 * A periodic or a periodic ring timeout can be deleted after a successful
 * cancel or after em_tmo_ack() returned EM_ERR_CANCELED. This indicates that
 * the acknowledged timeout is canceled and that it was the last timeout event
 * coming for that periodic timeout.
 *
 * After and during this call, the tmo handle is not valid anymore and must not
 * be used by or passed to other timer APIs.
 *
 * @param tmo  Timeout handle
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_delete(em_tmo_t tmo);

/**
 * Activate a oneshot timeout with absolute time.
 *
 * Activates a oneshot timeout to expire at a specific absolute time. The given
 * timeout event will be sent to the queue given to em_tmo_create() when the
 * timeout expires.
 *
 * It is not possible to send timeouts with an event group, but the application
 * can assign the event group when receiving the timeout event, see
 * em_event_group_assign().
 *
 * The timeout event should not be accessed after it has been given to the
 * timer, similar to sending an event.
 *
 * Even if not guaranteed, the implementation should make sure that this call
 * can fail only in exceptional situations (em_tmo_create() should pre-allocate
 * needed resources).
 *
 * The allowed minimum and maximum timeouts can be inquired with
 * em_timer_res_capability().
 *
 * An active timeout can not be modified. The timeout needs to be canceled and
 * then set again with new arguments.
 *
 * An inactive timeout can be reused by calling em_tmo_set_abs/rel() again. The
 * timeout becomes inactive after the oneshot timeout event has been received
 * or after it has been successfully cancelled.
 *
 * This function is for activating oneshot timeouts only. To activate
 * periodic timeouts use em_tmo_set_periodic() (or em_tmo_set_periodic_ring()).
 *
 * @param tmo        Timeout handle
 * @param ticks_abs  Expiration time in absolute timer specific ticks
 * @param tmo_ev     Timeout event
 *
 * @retval EM_OK           Success, event taken.
 * @retval EM_ERR_TOONEAR  Failure, the tick value is in past or too close to the
 *                         current time. Error handler not called, event not taken.
 * @retval (other_codes)   Failure, event not taken.
 *
 * @see em_timer_res_capability()
 */
em_status_t em_tmo_set_abs(em_tmo_t tmo, em_timer_tick_t ticks_abs,
			   em_event_t tmo_ev);

/**
 * Activate a timeout with a relative time.
 *
 * Similar to em_tmo_set_abs(), but instead of an absolute time uses a timeout
 * value relative to the moment of the call.
 *
 * This function is for activating oneshot timeouts only. To activate
 * periodic timeouts use em_tmo_set_periodic() (or em_tmo_set_periodic_ring()).
 *
 * @param tmo        Timeout handle
 * @param ticks_rel  Expiration time in relative timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @retval EM_OK           Success, event taken.
 * @retval EM_ERR_TOONEAR  Failure, the tick value is too low.
 *                         Error handler not called, event not taken.
 * @retval (other_codes)   Failure, event not taken.
 *
 * @see em_tmo_set_abs(), em_tmo_set_periodic()
 */
em_status_t em_tmo_set_rel(em_tmo_t tmo, em_timer_tick_t ticks_rel,
			   em_event_t tmo_ev);

/**
 * Activate a periodic timeout
 *
 * Used to activate periodic timeouts. The first period can be different from
 * the repetitive period by providing an absolute start time.
 * Set 'start_abs' to 0 if the repetitive period can start from the moment of
 * the call.
 *
 * The timeout event will be sent to the queue given to em_tmo_create() when the
 * first timeout expires. The receiver then needs to call em_tmo_ack() to allow
 * the timer to send the next event for the following period.
 *
 * This function can only be used with periodic timeouts (created with flag
 * EM_TMO_FLAG_PERIODIC).
 *
 * @param tmo        Timeout handle
 * @param start_abs  Absolute start time (or 0 for period starting at call time)
 * @param period     Period in timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @retval EM_OK           Success, event taken
 * @retval EM_ERR_TOONEAR  Failure, the tick value is in past or too close to
 *                         the current time.
 *                         Error handler not called, event not taken.
 * @retval (other_codes)   Failure, event not taken.
 *
 * @see em_tmo_ack()
 */
em_status_t em_tmo_set_periodic(em_tmo_t tmo,
				em_timer_tick_t start_abs,
				em_timer_tick_t period,
				em_event_t tmo_ev);

/**
 * Activate a periodic timeout on a periodic ring timer
 *
 * Use 'start_abs' value 0 to start the timer relative to current time. To
 * adjust the offset of timeouts, an absolute tick can also be given, but the
 * maximum distance from the current time can only be up to one period.
 * The periodic rate of the timeout event is 'base_hz' (given when creating the
 * timer) multiplied by the given 'multiplier'. For example 1000Hz 'base_hz'
 * with a 'multiplier' of 8 will give a 125us period.
 *
 * A timeout event of type EM_EVENT_TYPE_TIMER_IND is automatically allocated,
 * if not provided, and will be sent to the queue given to em_tmo_create() when
 * the timeout expires. The user needs to call em_tmo_ack() when receiving the
 * timeout event, similar as with a regular periodic timeout. However, with a
 * ring timer there is no guaranteed flow control - new events may be sent even
 * before user has called em_tmo_ack(). This means that the same event may be in
 * the input queue multiple times if the application can not keep up with the
 * period rate. If the destination queue is not atomic, the same event can also
 * be concurrently received by multiple cores. This is a race hazard the user
 * must prepare for. Additionally, the used timeout event can not be changed via
 * em_tmo_ack(), the actual received event must always be passed to it.
 *
 * The last argument 'tmo_ev' is normally 'EM_EVENT_UNDEF' when activating a new
 * periodic ring timeout. The implementation will in this case use a
 * pre-allocated event. The exception case concerns reuse of a canceled ring
 * timeout event (when em_tmo_ack() returns 'EM_ERR_CANCELED', the event stays
 * with the user and can be reused). Such an event can be recycled via 'tmo_ev'
 * to avoid an extra event free and alloc during reactivation.
 *
 * This function can only be used with periodic timeouts from a ring timer.
 * The timeout indication event is read-only and can be accessed only via
 * accessor APIs.
 *
 * @param tmo        Timeout handle
 * @param start_abs  Absolute start time (or 0 for period starting at call time)
 * @param multiplier Rate multiplier (period rate = multiplier * timer base_hz)
 * @param tmo_ev     Event of type EM_EVENT_TYPE_TIMER_IND to reuse.
 *                   Normally EM_EVENT_UNDEF.
 *
 * @retval EM_OK		Success
 * @retval EM_ERR_TOONEAR	Failure, start tick value is past or too close
 *				to current time or multiplier is too high.
 * @retval EM_ERR_TOOFAR	Failure, start tick value exceeds one period.
 * @retval (other_codes)	Failure
 *
 * @see em_tmo_get_user_ptr(), em_tmo_get_type(), em_timer_create_ring()
 */
em_status_t em_tmo_set_periodic_ring(em_tmo_t tmo,
				     em_timer_tick_t start_abs,
				     uint64_t multiplier,
				     em_event_t tmo_ev);

/**
 * Cancel a timeout
 *
 * Cancels a timeout preventing future expiration. Returns the timeout event
 * if the timeout has not expired.
 * A timeout that has already expired, or just is about to, is too late to be
 * cancelled and the timeout event will be delivered to the destination queue.
 * In this case the error 'EM_ERR_TOONEAR' is returned - no EM error handler is
 * called.
 *
 * Periodic timeout: cancel may fail if attempted too close to the next period.
 * This can be considered normal and indicates that at least one more timeout
 * event will be delivered to the user. In this case, the error 'EM_ERR_TOONEAR'
 * is returned and no valid event is output. The EM error handler is not called
 * is this scenario.
 * The user calls em_tmo_ack() for each received periodic timeout event. The
 * em_tmo_ack() function returns 'EM_ERR_CANCELED' for the last timeout event
 * from the cancelled periodic timeout to let the user know that it is now OK to
 * e.g. delete the timeout.
 *
 * @param      tmo         Timeout handle
 * @param[out] cur_event   Event handle pointer to return the pending
 *                         timeout event for a successful cancel or
 *                         EM_EVENT_UNDEF if cancel fails (e.g. called too late)
 *
 * @retval EM_OK           Cancel successful, timeout event returned.
 * @retval EM_ERR_TOONEAR  Timeout already expired, too late to cancel.
 *                         EM error handler is not called.
 * @retval (other_codes)   Failure
 *
 * @see em_tmo_set_abs(), em_tmo_set_rel(), em_tmo_set_periodic(),
 *      em_tmo_set_periodic_ring()
 * @see em_tmo_ack() for periodic timeouts
 */
em_status_t em_tmo_cancel(em_tmo_t tmo, em_event_t *cur_event);

/**
 * Acknowledge a periodic timeout
 *
 * All received periodic timeout events must be acknowledged with em_tmo_ack().
 * No further timeout event(s) will be sent before the user has acknowledged
 * the previous one unless a ring timer is used.
 *
 * Timeout acknowledgment is usually done at the end of the EO-receive function
 * to prevent race conditions (e.g. if the same event is reused for the next
 * timeout period also). The implementation will adjust for the processing delay
 * so that the time slot will not drift over time.
 *
 * If em_tmo_ack() is called too late, e.g. the next period(s) is already
 * passed, the implementation by default will skip all the missed time slots and
 * arm for the next future one keeping the original start offset. The
 * application can alter this behaviour with the flag 'EM_TMO_FLAG_NOSKIP' when
 * creating a timeout: no past timeout will be skipped and each late
 * acknowledgment will immediately trigger sending the next timeout event until
 * the current time has been reached.
 * Note that using 'EM_TMO_FLAG_NOSKIP' may result in an event storm if a large
 * number of timeouts have been unacknowledged for a longer time (limited by
 * application response latency). Timing problems will not call the EM error
 * handler.
 *
 * If the timeout has been canceled, but the cancel happened too late for the
 * current period, the timeout event will still be delivered. The em_tmo_ack()
 * call for this event will return 'EM_ERR_CANCELED' and does not call the error
 * handler. This error code signals that the timeout event was the last one
 * coming for that, now cancelled, timeout.
 *
 * The application may reuse the same received timeout event or provide a new
 * one for the next timeout via 'next_tmo_ev'. With a periodic ring timer, the
 * actual received event must be always be passed via 'next_tmo_ev'.
 *
 * The given event should not be touched after calling this function until it
 * has been received again or after the timeout is successfully cancelled and
 * event returned.
 *
 * A regular periodic timeout (i.e. not a ring one) will stop if em_tmo_ack()
 * returns an error other than related to timing. Unless the timeout was
 * canceled, the implementation will call the EM error handler in this case
 * (the error/exception can be handled also there).
 *
 * em_tmo_ack() can only be used with periodic timeouts.
 *
 * @param tmo          Timeout handle
 * @param next_tmo_ev  Next timeout event handle.
 *                     Can be the received one for regular periodic timeouts.
 *                     Must be the received one for periodic ring timeouts.
 *
 * @retval EM_OK            Success, event taken.
 * @retval EM_ERR_CANCELED  Timer cancelled, last event - no further timeout
 *                          events coming, event not taken.
 * @retval (other_codes)    Failure, event not taken.
 */
em_status_t em_tmo_ack(em_tmo_t tmo, em_event_t next_tmo_ev);

/**
 * Get a list of currently active timers.
 *
 * The timer handles returned via 'tmr_list' can be used for further timer
 * queries or to destroy existing timers.
 *
 * The return value always reflects the actual number of timers in the
 * EM instance but the output parameter 'tmr_list' is only written up to the
 * given 'max' length.
 *
 * Note that the return value (number of timers) can be greater than the given
 * 'max'. It is the user's responsibility to check the return value against the
 * given 'max'.
 *
 * To only get the current number of active timers, without any timer handles
 * output, use the following: num_timers = em_timer_get_all(NULL, 0);
 *
 * @param[out] tmr_list  Pointer to an array of timer handles.
 *                       Use NULL if only interested in the return value.
 * @param      max       Max number of handles that can be written into
 *                       'tmr_list'. 'max' is ignored if 'tmr_list' is NULL.
 *
 * @return The number of active timers
 */
int em_timer_get_all(em_timer_t *tmr_list, int max);

/**
 * Get timer attributes
 *
 * Returns the actual capabilities of the given timer.
 *
 * @param      tmr       Timer handle
 * @param[out] tmr_attr  Pointer to em_timer_attr_t to fill
 *
 * @return EM_OK on success
 */
em_status_t em_timer_get_attr(em_timer_t tmr, em_timer_attr_t *tmr_attr);

/**
 * Returns the timer frequency, i.e. ticks per second, for the given timer.
 *
 * Can be used to convert real time to timer specific ticks.
 *
 * @param tmr  Timer handle
 *
 * @return ticks per second (Hz), or 0 for non-existing timer
 */
uint64_t em_timer_get_freq(em_timer_t tmr);

/**
 * Convert timer ticks to nanoseconds (ns)
 *
 * @param tmr    Valid timer handle
 * @param ticks  Timer specific ticks to convert
 *
 * @return converted amount in ns
 */
uint64_t em_timer_tick_to_ns(em_timer_t tmr, em_timer_tick_t ticks);

/**
 * Convert nanoseconds (ns) to timer ticks
 *
 * @param tmr  Valid timer handle
 * @param ns   ns value to convert
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
 * Counter support is optional. If counters are not supported, the function
 * returns 'EM_ERR_NOT_IMPLEMENTED'.
 * A quick way to detect whether counters are supported is to call the function
 * with 'stat=NULL' and check the return value.
 *
 * @param tmo        Timeout handle
 * @param[out] stat  Pointer to em_tmo_stats_t to receive the values (NULL ok)
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_get_stats(em_tmo_t tmo, em_tmo_stats_t *stat);

/**
 * Ask if the given event is currently used as a timeout indication event.
 *
 * This function can be used with any valid event handle to ask if it is used as
 * a timeout indication event.
 * Events are updated to a tmo-type when going through the timer API.
 * @note Because a received timeout event is owned by the application, and not
 * necessarily passing through the timer API anymore, this type will not be
 * reset until the event is freed, reused as another timeout or explicitly reset
 * by setting the 'reset' argument to true. This reset should be done if
 * re-using the received timeout event for something else than a timeout to
 * avoid wrong interpretations.
 *
 * A successful timeout cancel (event returned) will reset the event type to
 * 'EM_TMO_TYPE_NONE'.
 *
 * @note The 'reset' argument is ignored if the given event is of type
 * 'EM_EVENT_TYPE_TIMER_IND'.
 *
 * The related tmo handle can be retrieved via the 'tmo' argument. This
 * can be useful when calling em_tmo_ack() for periodic timeouts:
 * @code
 * em_tmo_t tmo;
 *
 * if (em_tmo_get_type(event, &tmo, false) == EM_TMO_TYPE_PERIODIC)
 *	retval = em_tmo_ack(tmo, event);
 * @endcode
 *
 * @param      event  Event handle to check.
 * @param[out] tmo    em_tmo_t pointer to output the related tmo handle.
 *                    Use NULL if not interested in the tmo handle.
 * @param      reset  Set to 'true' to reset the event's tmo type to
 *                    'EM_TMO_TYPE_NONE' to e.g. enable non-timer related reuse
 *                    of the event.
 *
 * @return The type of the timeout or 'EM_TMO_TYPE_NONE' if event is not related
 *         to a timeout
 * @see em_tmo_type_t
 */
em_tmo_type_t em_tmo_get_type(em_event_t event, em_tmo_t *tmo, bool reset);

/**
 * Returns the optional user pointer for a periodic ring timeout.
 *
 * Can only be used with an event received as a timeout event for a periodic
 * ring, i.e. for events of type 'EM_EVENT_TYPE_TIMER_IND' only. Other event
 * types will return NULL.
 *
 * @param      event  Event received as timeout
 * @param[out] tmo    Optionally returns associated tmo handle. NULL ok.
 *
 * @return A pointer given when creating the associated tmo or
 *         NULL if the event is not a ring timeout event.
 */
void *em_tmo_get_userptr(em_event_t event, em_tmo_t *tmo);

/**
 * Returns the associated timer handle from a timeout handle
 *
 * The associated timer handle is returned from a valid timeout. Can be used to
 * e.g. read the current timer tick without having the timer handle:
 * @code
 * em_timer_tick_t tick = em_timer_current_tick(em_tmo_get_timer(tmo));
 * @endcode
 *
 * @param tmo  Valid timeout handle
 *
 * @return The associated timer handle or
 *         'EM_TIMER_UNDEF' if the tmo is not valid
 */
em_timer_t em_tmo_get_timer(em_tmo_t tmo);

/**
 * Convert a timer handle to an unsigned integer.
 *
 * @param timer  Timer handle to be converted.
 * @return       A 'uint64_t' value that can be used to print/display the handle
 *
 * @note This routine is intended to be used for diagnostic purposes
 * to enable applications to e.g. generate a printable value that represents
 * an em_timer_t handle.
 */
uint64_t em_timer_to_u64(em_timer_t timer);

/**
 * Convert a timeout handle to an unsigned integer.
 *
 * @param tmo  Timeout handle to be converted.
 * @return     A 'uint64_t' value that can be used to print/display the handle.
 *
 * @note This routine is intended to be used for diagnostic purposes
 * to enable applications to e.g. generate a printable value that represents
 * an em_tmo_t handle.
 */
uint64_t em_tmo_to_u64(em_tmo_t tmo);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_TIMER_H_ */
