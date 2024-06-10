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
 * capabilities and time bases are system specific. Typically only a few
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
 * A timeout can be re-used after it has been received or successfully
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
 * The major event types EM_EVENT_TYPE_SW, EM_EVENT_TYPE_PACKET and
 * EM_EVENT_TYPE_TIMER can be used as a timeout indication. The type
 * EM_EVENT_TYPE_TIMER is alternative to EM_EVENT_TYPE_SW and works the same
 * way. Additionally for ring timer only, the type EM_EVENT_TYPE_TIMER_IND
 * is used. This is a special indication event without visible payload.
 *
 * A periodic timer requires the application to acknowledge each received
 * timeout event after it has been processed. The acknowledgment activates the
 * next timeout and compensates for the processing delay to keep the original
 * interval. This creates a flow control mechanism and also protects the event
 * handling from races if the same event is reused every time, the next
 * timeout will not be sent before the previous has been acknowledged.
 * The event to be received for each periodic timeout can also be different as
 * the next event is given by the application with the acknowledge.
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
 * Application also needs to have a mechanism to detect which event is a
 * periodic timeout to be able to call acknowledge.
 *
 * If the timeout tick value given to timeout start points to the past or is too
 * close to current time then error code EM_ERR_TOONEAR is returned. In this
 * case EM will not call error handler to let application decide whether
 * it is an error or if it will try again with updated target time.
 *
 * There is an alternative periodic ring timer. As it uses different abstraction
 * it is created and started via separate ring specific APIs. It has three main
 * differencies to the regular periodic timeouts:
 *	1. Only a pre-defined read-only event type can be used and is provided
 *	   by the timer (EM_EVENT_TYPE_TIMER_IND).
 *	2. Flow control is not supported. Some implementations may have it,
 *	   but the specification does not quarantee any so the user needs to be
 *	   prepared to see the same event enqueued multiple times if handling of
 *	   the received timeouts is not fast enough
 *	2. A limited set of period times are supported per timer (base rate or
 *	   an integer multiple of it only)
 *
 * Ring timers can be abstracted as a clock face ticking the pointer forward.
 * One cycle around is the base rate (minimum rate). The same timeout can be
 * inserted into multiple locations evenly spread within the clock face thus
 * multiplying the base rate. The starting offset can be adjusted only up to
 * one timeout period.
 * Depending on platform, this mode may provide better integration with HW and
 * thus have less runtime overhead. However, as it exposes a potential queue
 * overflow and a race hazard (race avoidable by using atomic queue as target),
 * the regular periodic timer is recommended as a default.
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

/** Deprecated
 *  Major EM Timer API version. Marks possibly backwards incompatible changes.
 *  EM timer is now part of EM API. Use EM_API_VERSION_MAJOR instead.
 */
#define EM_TIMER_API_VERSION_MAJOR	EM_API_VERSION_MAJOR
/** Deprecated
 *  Minor EM Timer API version. Marks possibly backwards incompatible changes.
 *  EM Timer is now part of EM API. Use EM_API_VERSION_MINOR instead.
 */
#define EM_TIMER_API_VERSION_MINOR	EM_API_VERSION_MINOR

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
 * Type returned by em_tmo_get_type()
 */
typedef enum em_tmo_type_t {
	EM_TMO_TYPE_NONE	= 0,	/**< unknown or not timer-related event */
	EM_TMO_TYPE_ONESHOT	= 1,	/**< event is oneshot timeout indication */
	EM_TMO_TYPE_PERIODIC	= 2,	/**< event is periodic timeout indication */
} em_tmo_type_t;

/**
 * The timer tick has HW and timer specific meaning, but the type is always a
 * 64-bit integer and is normally assumed to be monotonic and not to wrap
 * around. Exceptions with exotic extra timers should be clearly documented.
 */
typedef uint64_t em_timer_tick_t;

/**
 * Fractional 64-bit unsigned value for timer frequency.
 *
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
 * Timer ring timing parameters.
 *
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
 * Structure used to create a timer or inquire its configuration later.
 *
 * This needs to be initialized with em_timer_attr_init(), which fills default
 * values to each field. After that the values can be modified as needed.
 * Values are considered a requirement, e.g. setting 'resparam.res_ns' to 1000(ns)
 * requires at least 1us resolution. The timer creation will fail if the implementation
 * cannot support such resolution (like only goes down to 1500ns).
 * The implementation is free to provide better than requested, but not worse.
 *
 * To know the implementation specific limits use em_timer_capability and em_timer_res_capability.
 *
 * When creating the alternative periodic ring timer, this needs to be initialized
 * with em_timer_ring_attr_init instead. EM_TIMER_FLAG_RING will be set by
 * em_timer_ring_attr_init so it does not need to be manually set.
 *
 * @see em_timer_attr_init, em_timer_ring_attr_init, em_timer_capability
 */
typedef struct em_timer_attr_t {
	/** Resolution parameters. Set when not creating periodic ring.
	 * This gets cleared by em_timer_ring_attr_init
	 */
	em_timer_res_param_t resparam;
	/** Maximum simultaneous timeouts */
	uint32_t num_tmo;
	/** Extra flags. A set flag is a requirement */
	em_timer_flag_t flags;
	/** Optional name for this timer */
	char name[EM_TIMER_NAME_LEN];

	/**
	 * used when creating alternative periodic ring timer.
	 * Cleared by em_timer_attr_init
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
 * Initialize em_timer_ring_attr_t
 *
 * Initializes em_timer_ring_attr_t according to given values.
 * After successful return the attributes can be given to em_timer_ring_create.
 * Note, that if the implementation cannot use exact given combination it may
 * update the ring_attr values, but always to meet or exceed given value.
 * User can read the new values to determine if they were modified.
 * Error is returned if given values cannot be met.
 *
 * Before creating the ring timer other values like num_tmo and name can be
 * adjusted as needed. Also if non-integer frequency is needed the base_hz
 * fractional part can be adjusted before timer_ring_create.
 *
 * This function will not trigger errorhandler calls.
 *
 * @param [out] ring_attr  Pointer to em_timer_attr_t to be initialized
 * @param clk_src	Clock source to use (system specific or portable
 *			EM_TIMER_CLKSRC_DEFAULT)
 * @param base_hz	Base rate of the ring (minimum rate i.e. longest period)
 * @param max_mul	Maximum multiplier (maximum rate = base_hz * max_mul)
 * @param res_ns	Required resolution of the timing or 0 to accept default
 *
 * @return EM_OK if the given clk_src and other values are supported
 *
 * @see em_timer_ring_capability, em_timer_ring_create
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
 * @brief Check periodic ring timer capability.
 *
 * Returns ring timer capability from given input values. On input parameter
 * ring must be initialized with the required values. res_ns can be 0,
 * which gets replaced by the system default.
 * During the call values are updated. If this returns EM_OK then the combination
 * of given values are all supported (or exceeded e.g. better resolution),
 * otherwise values are updated with the closest supported.
 *
 * As em_timer_ring_attr_init only takes integer base_hz, this can also be used
 * to verify valid values for modified fractional frequencies to avoid
 * errorhandler call from timer_ring_create().
 *
 * This function will not trigger errorhandler calls.
 *
 * @param ring [in,out]	timer ring parameters to check
 *
 * @retval EM_OK	Parameter combination is supported
 * @retval EM_ERR_NOT_SUPPORTED Parameters not supported, values updated to closest
 * @retval (other error) Unsupported arguments
 */
em_status_t em_timer_ring_capability(em_timer_ring_param_t *ring);

/**
 * Create and start a timer resource
 *
 * Required attributes are given via tmr_attr. The given structure must be
 * initialized with em_timer_attr_init before setting any field.
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
 * Create and start a timer ring (alternative periodic timer)
 *
 * Required attributes are given via ring_attr, which must have been initialized
 * with em_timer_ring_attr_init and optionally adjusted for the required timing
 * constraints.
 *
 * A periodic ring timer is different and will only send EM_EVENT_TYPE_TIMER_IND
 * events, which are automatically provided and cannot be modified. These events
 * can be allocated only via timer APIs.
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
 * If used timer is timer ring the NOSKIP flag is ignored.
 *
 * @param tmr        Timer handle
 * @param flags      Functionality flags
 * @param queue      Target queue where the timeout event should be delivered
 *
 * @return Timeout handle on success or EM_TMO_UNDEF on failure
 */
em_tmo_t em_tmo_create(em_timer_t tmr, em_tmo_flag_t flags, em_queue_t queue);

/**
 * Allocate a new timeout with extra arguments
 *
 * Like em_tmo_create but with additional argument. This can be used with any
 * timer type, but e.g. the userptr argument is only used with ring timers
 * using events of type EM_EVENT_TYPE_TIMER_IND that can carry userptr.
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
 * Free a timeout
 *
 * Free (destroy) a timeout.
 * The user provided timeout event for an active timeout will be returned via
 * cur_event and the timeout is cancelled. The timeout event for an expired, but
 * not yet received timeout will not be returned. It is the responsibility of
 * the application to handle that case (event will still be received).
 *
 * After and during this call the tmo handle is not valid anymore and must not
 * be used. With periodic timeout means em_tmo_ack must also not be called when
 * tmo is deleted.
 *
 * @param      tmo        Timeout handle
 * @param[out] cur_event  Current event for an active timeout
 *
 * @return EM_OK on success
 */
em_status_t em_tmo_delete(em_tmo_t tmo, em_event_t *cur_event);

/**
 * Activate a oneshot timeout with absolute time.
 *
 * Activates oneshot timeout to expire at specific absolute time. The given
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
 * can fail only in exceptional situations (em_tmo_create() should pre-
 * allocate needed resources).
 *
 * Allowed minimum and maximum timeout can be inquired with
 * em_timer_res_capability.
 *
 * An active timeout can not be modified. The timeout needs to be canceled and
 * then set again with new arguments.
 *
 * An inactive timeout can be re-used by calling em_tmo_set_abs/rel() again
 * after the previous timeout was received or was cancelled successfully.
 *
 * This function is for activating oneshot timeouts only. To activate
 * a periodic timer use em_tmo_set_periodic() instead.
 *
 * @param tmo        Timeout handle
 * @param ticks_abs  Expiration time in absolute timer specific ticks
 * @param tmo_ev     Timeout event
 *
 * @retval EM_OK		success (event taken)
 * @retval EM_ERR_TOONEAR	failure, tick value is in past or too close to
 *				current time. Errorhandler not called, event
 *				not taken
 * @retval (other_codes)	failure, event not taken
 *
 * @see em_timer_res_capability
 */
em_status_t em_tmo_set_abs(em_tmo_t tmo, em_timer_tick_t ticks_abs,
			   em_event_t tmo_ev);

/**
 * Activate a timeout with relative time.
 *
 * Similar to em_tmo_set_abs(), but instead of an absolute time uses timeout
 * value relative to the moment of the call.
 *
 * This function is for activating oneshot timeouts only. To activate
 * a periodic timer use em_tmo_set_periodic() instead.
 *
 * @param tmo        Timeout handle
 * @param ticks_rel  Expiration time in relative timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @retval EM_OK		success (event taken)
 * @retval EM_ERR_TOONEAR	failure, tick value is too low. Errorhandler not
 *				called, event not taken
 * @retval (other_codes)	failure, event not taken
 *
 * @deprecated Do not use for periodic timeouts
 *
 * @see em_tmo_set_abs, em_tmo_set_periodic
 */
em_status_t em_tmo_set_rel(em_tmo_t tmo, em_timer_tick_t ticks_rel,
			   em_event_t tmo_ev);

/**
 * Activate a periodic timeout
 *
 * Used to activate periodic timeouts. The first period can be different from
 * the repetitive period by providing an absolute start time e.g. the first period
 * starts from that moment. Use 0 as start time if the period can start from the
 * moment of the call (relative).
 *
 * The timeout event will be sent to the queue given to em_tmo_create() when the
 * first timeout expires. Receiver then need to call em_tmo_ack() to allow
 * sending next event.
 *
 * This function can only be used with periodic timeouts (created with flag
 * EM_TMO_FLAG_PERIODIC).
 *
 * @param tmo        Timeout handle
 * @param start_abs  Absolute start time (or 0 for period starting at call time)
 * @param period     Period in timer specific ticks
 * @param tmo_ev     Timeout event handle
 *
 * @retval EM_OK		success (event taken)
 * @retval EM_ERR_TOONEAR	failure, tick value is in past or too close to
 *				current time. Errorhandler not called, event
 *				not taken
 * @retval (other_codes)	failure, event not taken
 *
 * @see em_tmo_ack
 */
em_status_t em_tmo_set_periodic(em_tmo_t tmo,
				em_timer_tick_t start_abs,
				em_timer_tick_t period,
				em_event_t tmo_ev);

/**
 * Activate a periodic timeout on a periodic ring timer
 *
 * Use start_abs value 0 to start the timer relative to current time. To adjust
 * the offset of timeouts an absolute tick can also be given, but the maximum
 * distance from current time can only be up to one period.
 * Periodic rate of the timeout event is base_hz (given when creating the timer)
 * multiplied by the given multiplier. For example 1000Hz base_hz with multiplier
 * of 8 will give 125us period.
 *
 * Timeout event of type EM_EVENT_TYPE_TIMER_IND is automatically allocated if
 * not provided and will be sent to the queue given to em_tmo_create() when the
 * timeout expires. User then needs to call em_timer_ack like with normal
 * periodic timeout. With ring timer however there is no guaranteed flow control,
 * new events may be sent even before user has called ack. This means the same
 * event may be in the input queue multiple times if the application can not
 * keep up the period rate.
 * If the destination queue is not atomic the same event can then also be
 * concurrently received by multiple cores. This is a race hazard to prepare for.
 * Additionally the used event can not change via em_tmo_ack, the received event
 * must always be returned.
 *
 * The last argument tmo_ev is normally EM_EVENT_UNDEF for a new timeout start.
 * Then the implementation will use pre-allocated event. Exception is re-use of
 * canceled ring timeout event (when ack returns EM_ERR_CANCELED the event stays
 * with user and can be re-used). Such event can be recycled here to avoid extra
 * free and alloc.
 *
 * This function can only be used with periodic timeouts with a ring timer.
 * The timeout indication event is read-only and can be accessed only via
 * accessor APIs.
 *
 * @param tmo        Timeout handle
 * @param start_abs  Absolute start time (or 0 for period starting at call time)
 * @param multiplier Rate multiplier (period rate = multiplier * timer base_hz)
 * @param tmo_ev     Event of type EM_EVENT_TYPE_TIMER_IND to re-use.
 *                   Normally EM_EVENT_UNDEF.
 *
 * @retval EM_OK		success
 * @retval EM_ERR_TOONEAR	failure, start tick value is past or too close
 *				to current time or multiplier is too high
 * @retval EM_ERR_TOOFAR	failure, start tick value exceeds one period
 * @retval (other_codes)	failure
 *
 * @see em_tmo_get_user_ptr, em_tmo_get_type, em_timer_create_ring
 */
em_status_t em_tmo_set_periodic_ring(em_tmo_t tmo,
				     em_timer_tick_t start_abs,
				     uint64_t multiplier,
				     em_event_t tmo_ev);

/**
 * Cancel a timeout
 *
 * Cancels a timeout preventing future expiration. Returns the timeout event
 * in case the timeout was not expired. A timeout that has already expired or
 * just about to cannot be cancelled and the timeout event will be delivered to
 * the destination queue. In this case cancel will return an error as it was
 * too late to cancel. Errorhandler is not called if failure is due to expired
 * timeout only.
 *
 * Periodic timeout: cancel may fail if attempted too close to the next period.
 * This can be considered normal and indicates that one more timeout will be
 * received. In this case errorhandler is not called, error status
 * EM_ERR_TOONEAR returned and no event returned. When em_tmo_ack is then
 * called on the canceled timeout event receive it will return EM_ERR_CANCELED
 * to indicate this is the last event coming for this timeout.
 *
 * @param      tmo        Timeout handle
 * @param[out] cur_event  Event handle pointer to return the pending
 *                        timeout event or EM_EVENT_UNDEF if cancel fails
 *                        (e.g. called too late)
 *
 * @retval EM_OK		success, event returned
 * @retval EM_ERR_TOONEAR	already expired (too late to cancel).
 *				Errorhandler not called
 * @retval (other_codes)	failure
 *
 * @see em_tmo_set_abs, em_tmo_set_rel, em_tmo_set_periodic, em_tmo_set_periodic_ring
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
 * to prevent race conditions (e.g. if the same event is re-used for the next
 * timeout period also). The implementation will adjust for the processing delay
 * so that the time slot will not drift over time.
 *
 * If em_tmo_ack() is called too late, e.g. the next period(s) is already
 * passed, the implementation by default will skip all the missed time slots and
 * arm for the next future one keeping the original start offset. Application
 * can alter this behaviour with the flag EM_TMO_FLAG_NOSKIP when creating a
 * timeout. Then no past timeout is skipped and each late acknowledgment will
 * immediately trigger sending the next timeout event until current time has
 * been reached.
 * Note that using EM_TMO_FLAG_NOSKIP may result in an event storm if a large
 * number of timeouts have been unacknowledged for a longer time (limited by
 * application response latency). Timing problems will not call errorhandler.
 *
 * If the timer has been canceled, but the cancel happened too late for the
 * current period the timeout will be delivered. If application then calls
 * em_tmo_ack it returns EM_ERR_CANCELED and does not call errorhandler. This is
 * to signal it was the last timeout coming for that tmo.
 *
 * Application may re-use the same received timeout event or provide a new one
 * for the next timeout. With ring timer the received event must be returned.
 *
 * The given event should not be touched after calling this function until it
 * has been received again or after the timeout is successfully cancelled and
 * event returned.
 *
 * Periodic timeout will stop if em_tmo_ack() returns an error other than
 * timing related. The implementation will call errorhandler in this case
 * unless timer was canceled, so the exception can be handled also there.
 *
 * em_tmo_ack() can only be used with periodic timeouts.
 *
 * @param tmo          Timeout handle
 * @param next_tmo_ev  Next timeout event handle (can be the received one)
 *
 * @retval EM_OK		success (event taken)
 * @retval EM_ERR_CANCELED	timer has been cancelled, no more coming, not taken
 * @retval (other_codes)	failure, event not taken
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
 * Ask if given event is currently used as timeout indication.
 *
 * This can be used with any valid event handle to ask if it is used as a
 * timeout indication event.
 * Events are updated for tmo type when going through timer API.
 * @note As a received event is owned by the application and not necessarily
 * passing through timer API anymore this type will not reset until event is
 * freed, re-used as another timeout or explicitly reset by setting the reset
 * argument to true. This reset should be done if re-using the received tmo event
 * for something else than timeout to avoid expired value being returned in case
 * someone later calls tmo_get_type.
 *
 * Successful timeout cancel (event returned) will reset the event type to
 * EM_TMO_TYPE_NONE.
 *
 * @note The reset argument is ignored if the given event is of type
 * EM_EVENT_TYPE_TIMER_IND.
 *
 * The related tmo handle can also be retrieved via parameter tmo. This
 * can be useful to call em_tmo_ack() for periodic timeouts:
 * @code
 * em_tmo_t tmo;
 *
 * if (em_tmo_get_type(event, &tmo, false) == EM_TMO_TYPE_PERIODIC)
 *	retval = em_tmo_ack(tmo, event);
 * @endcode
 *
 * @param event		event handle to check
 * @param [out] tmo	pointer to em_tmo_t to receive related tmo handle (NULL ok)
 * @param reset		set true to reset tmo type to EM_TMO_TYPE_NONE for non-timer re-use
 *
 * @return type of timeout use or EM_TMO_TYPE_NONE if event is not related to a timeout
 * @see em_tmo_type_t
 */
em_tmo_type_t em_tmo_get_type(em_event_t event, em_tmo_t *tmo, bool reset);

/**
 * Returns the optional user pointer for a periodic ring timeout
 *
 * Can only be used with an event received as a timeout for a periodic ring,
 * i.e. EM_EVENT_TYPE_TIMER_IND only. Other event types will return NULL.
 *
 * @param event       Event received as timeout
 * @param [out] tmo   Optionally returns associated tmo handle. NULL ok.
 *
 * @return A pointer given when creating the associated tmo or
 *         NULL if the event is not ring timeout
 */
void *em_tmo_get_userptr(em_event_t event, em_tmo_t *tmo);

/**
 * Returns the associated timer handle from a timeout handle
 *
 * Associated timer handle is returned from a valid timeout. Can be used to for
 * instance read the current timer tick without having the timer handle:
 * @code
 * em_timer_tick_t tick = em_timer_current_tick(em_tmo_get_timer(tmo));
 * @endcode
 *
 * @param tmo	valid timeout handle
 *
 * @return associated timer handle or EM_TIMER_UNDEF if tmo is not valid
 *
 */
em_timer_t em_tmo_get_timer(em_tmo_t tmo);

/**
 * Convert a timer handle to an unsigned integer
 *
 * @param timer  timer handle to be converted
 * @return       uint64_t value that can be used to print/display the handle
 *
 * @note This routine is intended to be used for diagnostic purposes
 * to enable applications to e.g. generate a printable value that represents
 * an em_timer_t handle.
 */
uint64_t em_timer_to_u64(em_timer_t timer);

/**
 * Convert a timeout handle to an unsigned integer
 *
 * @param tmo  timeout handle to be converted
 * @return     uint64_t value that can be used to print/display the handle
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
