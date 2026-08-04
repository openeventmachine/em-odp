/*
 *   Copyright (c) 2015-2026, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_ERROR_H_
#define EVENT_MACHINE_ERROR_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_error Error management
 *  Error/Exception management
 *
 *  EM provides two basic mechanisms to manage operational exceptions
 *
 *  -# Return value
 *    - Most API calls return a status which can be checked by the application
 *
 *  -# Error handler
 *    - An optional error handler function can be registered. The given
 *      function is called by the EM implementation when an error occurs. This
 *      makes it possible to centralize the error management or even modify
 *      some API behavior as an error handler can modify the original API call
 *      return value as well. For instance, a global error handler could modify
 *      the em_send() behavior in such a way that the application never has to
 *      check the return value by catching all errors, fixing them (like
 *      deallocating event on failed send) and returning EM_OK.
 *    - There can be one global error handler and additionally one per each EO.
 * @{
 */

#include <event_machine/api/event_machine_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fatal error mask
 */
#define EM_ERROR_FATAL_MASK  0x80000000

/**
 * Test if error is fatal
 */
#define EM_ERROR_IS_FATAL(error)  (!!(EM_ERROR_FATAL_MASK & (error)))

/**
 * Set the error code as fatal
 */
#define EM_ERROR_SET_FATAL(error) (EM_ERROR_FATAL_MASK | (error))

/**
 * Alias to EM_ERROR_SET_FATAL(), shorter name, backwards compatible
 */
#define EM_FATAL(error)  EM_ERROR_SET_FATAL((error))

/**
 * @typedef em_status_t
 * Error/Status code.
 *
 * EM_OK (0) is the general code for success, other values
 * describe failed operation.
 * There is a generic error code EM_ERROR, but application should
 * normally test for not equal to EM_OK.
 *
 * @see em_error_handler_t(), em_error()
 */
typedef uint32_t em_status_t;
#define PRI_STAT  PRIu32
#define PRIxSTAT  PRIx32

/**
 * @def EM_OK
 * Operation successful
 */
#define EM_OK    0

/**
 * @def EM_ERROR
 * Operation not successful.
 *
 * Generic error code, other error codes are system specific.
 */
#define EM_ERROR 0xffffffff

/**
 * Error/Status codes used by em_status_t
 */
typedef enum em_status_e {
	/** Invalid argument */
	EM_ERR_BAD_ARG          = 0x1,
	/** Illegal state */
	EM_ERR_BAD_STATE        = 0x2,
	/** ID not from a valid range */
	EM_ERR_BAD_ID           = 0x3,
	/** Invalid type */
	EM_ERR_BAD_TYPE         = 0x4,
	/** Illegal context */
	EM_ERR_BAD_CONTEXT      = 0x5,
	/** Pointer from bad memory area (e.g. NULL) */
	EM_ERR_BAD_POINTER      = 0x6,

	/** Resource not created */
	EM_ERR_NOT_CREATED      = 0x7,
	/** Resource already reserved by someone else */
	EM_ERR_NOT_FREE         = 0x8,
	/** Resource not found */
	EM_ERR_NOT_FOUND        = 0x9,
	/** Not properly initialized (e.g. not using provided initializer) */
	EM_ERR_NOT_INITIALIZED  = 0xa,
	/** Implementation missing (placeholder) */
	EM_ERR_NOT_IMPLEMENTED  = 0xb,
	/** Feature or value combination is not supported */
	EM_ERR_NOT_SUPPORTED	= 0xc,

	/** Resource allocation failed */
	EM_ERR_ALLOC_FAILED     = 0xd,
	/** Operation failed */
	EM_ERR_OPERATION_FAILED = 0xe,
	/** Failure in a library function */
	EM_ERR_LIB_FAILED       = 0xf,

	/** Value over the limit */
	EM_ERR_TOO_LARGE        = 0x10,
	/** Value under the limit */
	EM_ERR_TOO_SMALL        = 0x11,

	/** ESV (reserved): Invalid event state detected, e.g. double-free */
	EM_ERR_EVENT_STATE      = 0x12,

	/** Operation timeout (e.g. waiting on a lock) */
	EM_ERR_TIMEOUT          = 0x13,

	/** Operation is too near current time or in past */
	EM_ERR_TOONEAR		= 0x14,
	/** Time target too far, e.g. timeout exceeds maximum supported value */
	EM_ERR_TOOFAR		= 0x15,
	/** Timeout was canceled, e.g. periodic timer */
	EM_ERR_CANCELED		= 0x16,
	/** Resource is busy */
	EM_ERR_BUSY             = 0x17,

	/** Other error. This is the last error code (for bounds checking) */
	EM_ERR
} em_status_e;

/**
 * @typedef em_escope_t
 * Error scope.
 *
 * Identifies the error scope for interpreting error codes and variable
 * arguments.
 *
 * @see em_error_handler_t(), em_error()
 */
typedef uint32_t em_escope_t;
#define PRI_ESCOPE  PRIu32
#define PRIxESCOPE  PRIx32

/**
 * Error handler prototype
 *
 * The error handler is called after EM notices an error or the user has called
 * em_error().
 *
 * The user can register EO specific and/or EM global error handlers. When an
 * error occurs, EM calls the EO specific error handler, if registered. If
 * there's no EO specific handler registered or the error occurs outside of an
 * EO context, EM calls the global error handler.
 *
 * The error handler is called with the original error code (that's about to be
 * returned) from the API call or em_error(). The error scope identifies the
 * source of the error and how the error code and the variable arguments should
 * be interpreted (number of arguments and types) in an implementation specific
 * way.
 *
 * @param eo            Execution object id
 * @param error         The error code
 * @param escope        Error scope. Identifies the scope for interpreting the
 *                      error code and variable arguments.
 * @param args          Variable number and type of arguments
 *
 * @return The function may not return depending on implementation, error code
 *         or error scope. If it returns, it can return the original or
 *         modified error code or even EM_OK if it was able to fix the problem.
 *
 * @see em_register_error_handler(), em_eo_register_error_handler()
 */
typedef em_status_t (*em_error_handler_t)(em_eo_t eo, em_status_t error,
					  em_escope_t escope, va_list args);

/**
 * Register a global error handler.
 *
 * The global error handler is called on EM errors or by em_error() calls,
 * except when running in an EO context with an EO specific error handler
 * registered (in which case the EO specific error handler takes precedence).
 * Note, the provided function will override any previously registered
 * global error handler.
 * The EM default global error handler is used when no user provided global
 * error handler is registered.
 *
 * @param handler  Error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_register_error_handler(), em_unregister_error_handler(),
 *      em_error_handler_t()
 */
em_status_t em_register_error_handler(em_error_handler_t handler);

/**
 * Unregister a global error handler.
 *
 * Unregisters any previously registered global error handler and
 * restores the EM default global error handler into use.
 *
 * @return EM_OK if successful.
 *
 * @see em_register_error_handler()
 */
em_status_t em_unregister_error_handler(void);

/**
 * Report an error.
 *
 * Reported errors are handled by the appropriate (EO specific or global)
 * error handler.
 *
 * Depending on the error/scope/implementation, the function call may not
 * return.
 *
 * @param error         Error code
 * @param escope        Error scope. Identifies the scope for interpreting the
 *                      error code and the variable arguments.
 * @param ...           Variable number and type of arguments
 *
 * @see em_register_error_handler(), em_error_handler_t()
 */
void em_error(em_status_t error, em_escope_t escope, ...);

/**
 * EM log level
 */
typedef enum {
	EM_LOG_DBG,
	EM_LOG_PRINT,
	EM_LOG_ERR
} em_log_level_t;

/**
 * EM log function, variable number of args
 *
 * @note: both 'log()' and 'vlog()' need to be implemented if used.
 */
typedef int (*em_log_func_t)(em_log_level_t level, const char *fmt, ...)
			     __attribute__((format(printf, 2, 3)));

/**
 * EM log function, va_list instead of variable number of args
 *
 * @note: both 'log()' and 'vlog()' need to be implemented if used.
 */
typedef int (*em_vlog_func_t)(em_log_level_t level, const char *fmt,
			      va_list args);

/**
 * All EM error scopes (API and other internal) should have bit 31 set
 *
 * NOTE: High bit is RESERVED for EM escopes and should not be
 * used by the application.
 */
#define EM_ESCOPE_BIT         (0x80000000u)

/**
 * EM API functions error scope
 */
#define EM_ESCOPE_API_TYPE          (0xFFu)
/**
 * EM Add-on API functions error scope
 */
#define EM_ESCOPE_ADDON_API_TYPE    (0xFEu)
/**
 * EM Internal (non-public API) functions error scope
 */
#define EM_ESCOPE_INTERNAL_TYPE     (0xFCu)

/**
 * Mask selects the high byte of the 32-bit escope
 */
#define EM_ESCOPE_MASK  (0xFF000000)
/**
 * EM API functions error mask
 */
#define EM_ESCOPE_API_MASK  (EM_ESCOPE_BIT | (EM_ESCOPE_API_TYPE << 24))
/**
 * EM Add-on API functions error mask
 */
#define EM_ESCOPE_ADDON_API_MASK  (EM_ESCOPE_BIT | (EM_ESCOPE_ADDON_API_TYPE << 24))
/**
 * EM Internal (non-API) functions error mask
 */
#define EM_ESCOPE_INTERNAL_MASK  (EM_ESCOPE_BIT | (EM_ESCOPE_INTERNAL_TYPE << 24))

/**
 * Test if the error scope identifies an EM function - API, add-on API or other internal!
 */
#define EM_ESCOPE(escope)  (EM_ESCOPE_BIT & (escope))
/**
 * Test if the error scope identifies an EM API function
 */
#define EM_ESCOPE_API(escope)  (((escope) & EM_ESCOPE_MASK) == EM_ESCOPE_API_MASK)
/**
 * Test if the error scope identifies an EM API function
 */
#define EM_ESCOPE_ADDON_API(escope)  (((escope) & EM_ESCOPE_MASK) == EM_ESCOPE_ADDON_API_MASK)
/**
 * Test if the error scope identifies an EM Internal function
 */
#define EM_ESCOPE_INTERNAL(escope)  (((escope) & EM_ESCOPE_MASK) == EM_ESCOPE_INTERNAL_MASK)

/******************************************************************************
 * EM API function error scopes:
 ******************************************************************************/
#define EM_ESCOPE_API_FIRST                       (EM_ESCOPE_API_MASK | 0x0000)

/* EM API escopes: Atomic Group */
#define EM_ESCOPE_ATOMIC_GROUP_CREATE              (EM_ESCOPE_API_MASK | 0x0001)
#define EM_ESCOPE_ATOMIC_GROUP_DELETE              (EM_ESCOPE_API_MASK | 0x0002)
#define EM_ESCOPE_ATOMIC_GROUP_FIND                (EM_ESCOPE_API_MASK | 0x0003)
#define EM_ESCOPE_ATOMIC_GROUP_FIRST               (EM_ESCOPE_API_MASK | 0x0004)
#define EM_ESCOPE_ATOMIC_GROUP_NAME                (EM_ESCOPE_API_MASK | 0x0005)
#define EM_ESCOPE_ATOMIC_GROUP_NEXT                (EM_ESCOPE_API_MASK | 0x0006)
#define EM_ESCOPE_ATOMIC_GROUP_QUEUE_FIRST         (EM_ESCOPE_API_MASK | 0x0007)
#define EM_ESCOPE_ATOMIC_GROUP_QUEUE_NEXT          (EM_ESCOPE_API_MASK | 0x0008)
#define EM_ESCOPE_QUEUE_CREATE_AG                  (EM_ESCOPE_API_MASK | 0x0009)
#define EM_ESCOPE_QUEUE_CREATE_STATIC_AG           (EM_ESCOPE_API_MASK | 0x000A)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_ATOMIC_GROUP_GET_FIRST           EM_ESCOPE_ATOMIC_GROUP_FIRST
#define EM_ESCOPE_ATOMIC_GROUP_GET_NAME            EM_ESCOPE_ATOMIC_GROUP_NAME
#define EM_ESCOPE_ATOMIC_GROUP_GET_NEXT            EM_ESCOPE_ATOMIC_GROUP_NEXT
#define EM_ESCOPE_ATOMIC_GROUP_QUEUE_GET_FIRST     EM_ESCOPE_ATOMIC_GROUP_QUEUE_FIRST
#define EM_ESCOPE_ATOMIC_GROUP_QUEUE_GET_NEXT      EM_ESCOPE_ATOMIC_GROUP_QUEUE_NEXT

/* EM API escopes: Core */
#define EM_ESCOPE_CORE_COUNT                       (EM_ESCOPE_API_MASK | 0x000B)
#define EM_ESCOPE_CORE_ID                          (EM_ESCOPE_API_MASK | 0x000C)

/* EM API escopes: Dispatcher */
#define EM_ESCOPE_DISPATCH                         (EM_ESCOPE_API_MASK | 0x000D)
#define EM_ESCOPE_DISPATCH_DURATION                (EM_ESCOPE_API_MASK | 0x000E)
#define EM_ESCOPE_DISPATCH_EVENTS                  (EM_ESCOPE_API_MASK | 0x000F)
#define EM_ESCOPE_DISPATCH_NS                      (EM_ESCOPE_API_MASK | 0x0010)
#define EM_ESCOPE_DISPATCH_OPT_INIT                (EM_ESCOPE_API_MASK | 0x0011)
#define EM_ESCOPE_DISPATCH_REGISTER_ENTER_CB       (EM_ESCOPE_API_MASK | 0x0012)
#define EM_ESCOPE_DISPATCH_REGISTER_EXIT_CB        (EM_ESCOPE_API_MASK | 0x0013)
#define EM_ESCOPE_DISPATCH_ROUNDS                  (EM_ESCOPE_API_MASK | 0x0014)
#define EM_ESCOPE_DISPATCH_UNREGISTER_ENTER_CB     (EM_ESCOPE_API_MASK | 0x0015)
#define EM_ESCOPE_DISPATCH_UNREGISTER_EXIT_CB      (EM_ESCOPE_API_MASK | 0x0016)

/* EM API escopes: EO */
#define EM_ESCOPE_EO_ADD_QUEUE                     (EM_ESCOPE_API_MASK | 0x0017)
#define EM_ESCOPE_EO_ADD_QUEUE_SYNC                (EM_ESCOPE_API_MASK | 0x0018)
#define EM_ESCOPE_EO_CONTEXT                       (EM_ESCOPE_API_MASK | 0x0019)
#define EM_ESCOPE_EO_CREATE                        (EM_ESCOPE_API_MASK | 0x001A)
#define EM_ESCOPE_EO_CREATE_MULTIRCV               (EM_ESCOPE_API_MASK | 0x001B)
#define EM_ESCOPE_EO_CREATE_PARAM                  (EM_ESCOPE_API_MASK | 0x001C)
#define EM_ESCOPE_EO_CURRENT                       (EM_ESCOPE_API_MASK | 0x001D)
#define EM_ESCOPE_EO_DELETE                        (EM_ESCOPE_API_MASK | 0x001E)
#define EM_ESCOPE_EO_FIND                          (EM_ESCOPE_API_MASK | 0x001F)
#define EM_ESCOPE_EO_FIRST                         (EM_ESCOPE_API_MASK | 0x0020)
#define EM_ESCOPE_EO_MULTIRCV_PARAM_INIT           (EM_ESCOPE_API_MASK | 0x0021)
#define EM_ESCOPE_EO_NAME                          (EM_ESCOPE_API_MASK | 0x0022)
#define EM_ESCOPE_EO_NEXT                          (EM_ESCOPE_API_MASK | 0x0023)
#define EM_ESCOPE_EO_PARAM_INIT                    (EM_ESCOPE_API_MASK | 0x0024)
#define EM_ESCOPE_EO_QUEUE_FIRST                   (EM_ESCOPE_API_MASK | 0x0025)
#define EM_ESCOPE_EO_QUEUE_NEXT                    (EM_ESCOPE_API_MASK | 0x0026)
#define EM_ESCOPE_EO_REGISTER_ERROR_HANDLER        (EM_ESCOPE_API_MASK | 0x0027)
#define EM_ESCOPE_EO_REMOVE_QUEUE                  (EM_ESCOPE_API_MASK | 0x0028)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL              (EM_ESCOPE_API_MASK | 0x0029)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC         (EM_ESCOPE_API_MASK | 0x002A)
#define EM_ESCOPE_EO_REMOVE_QUEUE_SYNC             (EM_ESCOPE_API_MASK | 0x002B)
#define EM_ESCOPE_EO_START                         (EM_ESCOPE_API_MASK | 0x002C)
#define EM_ESCOPE_EO_START_SYNC                    (EM_ESCOPE_API_MASK | 0x002D)
#define EM_ESCOPE_EO_STATE                         (EM_ESCOPE_API_MASK | 0x002E)
#define EM_ESCOPE_EO_STOP                          (EM_ESCOPE_API_MASK | 0x002F)
#define EM_ESCOPE_EO_STOP_SYNC                     (EM_ESCOPE_API_MASK | 0x0030)
#define EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER      (EM_ESCOPE_API_MASK | 0x0031)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_EO_GET_CONTEXT                   EM_ESCOPE_EO_CONTEXT
#define EM_ESCOPE_EO_GET_FIRST                     EM_ESCOPE_EO_FIRST
#define EM_ESCOPE_EO_GET_NAME                      EM_ESCOPE_EO_NAME
#define EM_ESCOPE_EO_GET_NEXT                      EM_ESCOPE_EO_NEXT
#define EM_ESCOPE_EO_GET_STATE                     EM_ESCOPE_EO_STATE
#define EM_ESCOPE_EO_QUEUE_GET_FIRST               EM_ESCOPE_EO_QUEUE_FIRST
#define EM_ESCOPE_EO_QUEUE_GET_NEXT                EM_ESCOPE_EO_QUEUE_NEXT

/* EM API escopes: Error */
#define EM_ESCOPE_ERROR                            (EM_ESCOPE_API_MASK | 0x0032)
#define EM_ESCOPE_REGISTER_ERROR_HANDLER           (EM_ESCOPE_API_MASK | 0x0033)
#define EM_ESCOPE_UNREGISTER_ERROR_HANDLER         (EM_ESCOPE_API_MASK | 0x0034)

/* EM API escopes: Event Group */
#define EM_ESCOPE_EVENT_GROUP_ABORT                (EM_ESCOPE_API_MASK | 0x0035)
#define EM_ESCOPE_EVENT_GROUP_APPLY                (EM_ESCOPE_API_MASK | 0x0036)
#define EM_ESCOPE_EVENT_GROUP_ASSIGN               (EM_ESCOPE_API_MASK | 0x0037)
#define EM_ESCOPE_EVENT_GROUP_CREATE               (EM_ESCOPE_API_MASK | 0x0038)
#define EM_ESCOPE_EVENT_GROUP_CURRENT              (EM_ESCOPE_API_MASK | 0x0039)
#define EM_ESCOPE_EVENT_GROUP_DELETE               (EM_ESCOPE_API_MASK | 0x003A)
#define EM_ESCOPE_EVENT_GROUP_FIRST                (EM_ESCOPE_API_MASK | 0x003B)
#define EM_ESCOPE_EVENT_GROUP_INCREMENT            (EM_ESCOPE_API_MASK | 0x003C)
#define EM_ESCOPE_EVENT_GROUP_IS_READY             (EM_ESCOPE_API_MASK | 0x003D)
#define EM_ESCOPE_EVENT_GROUP_NEXT                 (EM_ESCOPE_API_MASK | 0x003E)
#define EM_ESCOPE_EVENT_GROUP_NOTIF                (EM_ESCOPE_API_MASK | 0x003F)
#define EM_ESCOPE_EVENT_GROUP_PROCESSING_END       (EM_ESCOPE_API_MASK | 0x0040)
#define EM_ESCOPE_SEND_GROUP                       (EM_ESCOPE_API_MASK | 0x0041)
#define EM_ESCOPE_SEND_GROUP_MULTI                 (EM_ESCOPE_API_MASK | 0x0042)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_EVENT_GROUP_GET_FIRST            EM_ESCOPE_EVENT_GROUP_FIRST
#define EM_ESCOPE_EVENT_GROUP_GET_NEXT             EM_ESCOPE_EVENT_GROUP_NEXT
#define EM_ESCOPE_EVENT_GROUP_GET_NOTIF            EM_ESCOPE_EVENT_GROUP_NOTIF

/* EM API escopes: Event */
#define EM_ESCOPE_ALLOC                            (EM_ESCOPE_API_MASK | 0x0043)
#define EM_ESCOPE_ALLOC_MULTI                      (EM_ESCOPE_API_MASK | 0x0044)
#define EM_ESCOPE_EVENT_CLONE                      (EM_ESCOPE_API_MASK | 0x0045)
#define EM_ESCOPE_EVENT_CLONE_PART                 (EM_ESCOPE_API_MASK | 0x0046)
#define EM_ESCOPE_EVENT_HAS_REF                    (EM_ESCOPE_API_MASK | 0x0047)
#define EM_ESCOPE_EVENT_MARK_FREE                  (EM_ESCOPE_API_MASK | 0x0048)
#define EM_ESCOPE_EVENT_MARK_FREE_MULTI            (EM_ESCOPE_API_MASK | 0x0049)
#define EM_ESCOPE_EVENT_MARK_SEND                  (EM_ESCOPE_API_MASK | 0x004A)
#define EM_ESCOPE_EVENT_POINTER                    (EM_ESCOPE_API_MASK | 0x004B)
#define EM_ESCOPE_EVENT_POINTER_AND_SIZE           (EM_ESCOPE_API_MASK | 0x004C)
#define EM_ESCOPE_EVENT_POOL                       (EM_ESCOPE_API_MASK | 0x004D)
#define EM_ESCOPE_EVENT_POOL_SUBPOOL               (EM_ESCOPE_API_MASK | 0x004E)
#define EM_ESCOPE_EVENT_REF                        (EM_ESCOPE_API_MASK | 0x004F)
#define EM_ESCOPE_EVENT_SAME_TYPE_MULTI            (EM_ESCOPE_API_MASK | 0x0050)
#define EM_ESCOPE_EVENT_SET_TYPE                   (EM_ESCOPE_API_MASK | 0x0051)
#define EM_ESCOPE_EVENT_SIZE                       (EM_ESCOPE_API_MASK | 0x0052)
#define EM_ESCOPE_EVENT_TYPE                       (EM_ESCOPE_API_MASK | 0x0053)
#define EM_ESCOPE_EVENT_TYPE_MULTI                 (EM_ESCOPE_API_MASK | 0x0054)
#define EM_ESCOPE_EVENT_UAREA                      (EM_ESCOPE_API_MASK | 0x0055)
#define EM_ESCOPE_EVENT_UAREA_ID                   (EM_ESCOPE_API_MASK | 0x0056)
#define EM_ESCOPE_EVENT_UAREA_ID_SET               (EM_ESCOPE_API_MASK | 0x0057)
#define EM_ESCOPE_EVENT_UAREA_INFO                 (EM_ESCOPE_API_MASK | 0x0058)
#define EM_ESCOPE_EVENT_UNMARK_FREE                (EM_ESCOPE_API_MASK | 0x0059)
#define EM_ESCOPE_EVENT_UNMARK_FREE_MULTI          (EM_ESCOPE_API_MASK | 0x005A)
#define EM_ESCOPE_EVENT_UNMARK_SEND                (EM_ESCOPE_API_MASK | 0x005B)
#define EM_ESCOPE_EVENT_VECTOR_FREE                (EM_ESCOPE_API_MASK | 0x005C)
#define EM_ESCOPE_EVENT_VECTOR_INFO                (EM_ESCOPE_API_MASK | 0x005D)
#define EM_ESCOPE_EVENT_VECTOR_MAX_SIZE            (EM_ESCOPE_API_MASK | 0x005E)
#define EM_ESCOPE_EVENT_VECTOR_SIZE                (EM_ESCOPE_API_MASK | 0x005F)
#define EM_ESCOPE_EVENT_VECTOR_SIZE_SET            (EM_ESCOPE_API_MASK | 0x0060)
#define EM_ESCOPE_EVENT_VECTOR_TBL                 (EM_ESCOPE_API_MASK | 0x0061)
#define EM_ESCOPE_EVENT_VECTOR_TYPE                (EM_ESCOPE_API_MASK | 0x0062)
#define EM_ESCOPE_EVENT_VECTOR_TYPE_SET            (EM_ESCOPE_API_MASK | 0x0063)
#define EM_ESCOPE_FREE                             (EM_ESCOPE_API_MASK | 0x0064)
#define EM_ESCOPE_FREE_MULTI                       (EM_ESCOPE_API_MASK | 0x0065)
#define EM_ESCOPE_SEND                             (EM_ESCOPE_API_MASK | 0x0066)
#define EM_ESCOPE_SEND_MULTI                       (EM_ESCOPE_API_MASK | 0x0067)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_EVENT_GET_POOL                   EM_ESCOPE_EVENT_POOL
#define EM_ESCOPE_EVENT_GET_POOL_SUBPOOL           EM_ESCOPE_EVENT_POOL_SUBPOOL
#define EM_ESCOPE_EVENT_GET_SIZE                   EM_ESCOPE_EVENT_SIZE
#define EM_ESCOPE_EVENT_GET_TYPE                   EM_ESCOPE_EVENT_TYPE
#define EM_ESCOPE_EVENT_GET_TYPE_MULTI             EM_ESCOPE_EVENT_TYPE_MULTI
#define EM_ESCOPE_EVENT_UAREA_GET                  EM_ESCOPE_EVENT_UAREA
#define EM_ESCOPE_EVENT_UAREA_ID_GET               EM_ESCOPE_EVENT_UAREA_ID

/* EM API escopes: Event Packet */
#define EM_ESCOPE_PACKET_HEADROOM                  (EM_ESCOPE_API_MASK | 0x0068)
#define EM_ESCOPE_PACKET_POINTER                   (EM_ESCOPE_API_MASK | 0x0069)
#define EM_ESCOPE_PACKET_POINTER_AND_SIZE          (EM_ESCOPE_API_MASK | 0x006A)
#define EM_ESCOPE_PACKET_PULL_HEAD                 (EM_ESCOPE_API_MASK | 0x006B)
#define EM_ESCOPE_PACKET_PULL_TAIL                 (EM_ESCOPE_API_MASK | 0x006C)
#define EM_ESCOPE_PACKET_PUSH_HEAD                 (EM_ESCOPE_API_MASK | 0x006D)
#define EM_ESCOPE_PACKET_PUSH_TAIL                 (EM_ESCOPE_API_MASK | 0x006E)
#define EM_ESCOPE_PACKET_RESET                     (EM_ESCOPE_API_MASK | 0x006F)
#define EM_ESCOPE_PACKET_RESIZE                    (EM_ESCOPE_API_MASK | 0x0070)
#define EM_ESCOPE_PACKET_SIZE                      (EM_ESCOPE_API_MASK | 0x0071)
#define EM_ESCOPE_PACKET_TAILROOM                  (EM_ESCOPE_API_MASK | 0x0072)
#define EM_ESCOPE_PACKET_CONCAT                    (EM_ESCOPE_API_MASK | 0x0073)

/* EM API escopes: Hooks */
#define EM_ESCOPE_HOOKS_REGISTER_ALLOC             (EM_ESCOPE_API_MASK | 0x0074)
#define EM_ESCOPE_HOOKS_REGISTER_FREE              (EM_ESCOPE_API_MASK | 0x0075)
#define EM_ESCOPE_HOOKS_REGISTER_SEND              (EM_ESCOPE_API_MASK | 0x0076)
#define EM_ESCOPE_HOOKS_REGISTER_TO_ACTIVE         (EM_ESCOPE_API_MASK | 0x0077)
#define EM_ESCOPE_HOOKS_REGISTER_TO_IDLE           (EM_ESCOPE_API_MASK | 0x0078)
#define EM_ESCOPE_HOOKS_REGISTER_WHILE_IDLE        (EM_ESCOPE_API_MASK | 0x0079)
#define EM_ESCOPE_HOOKS_UNREGISTER_ALLOC           (EM_ESCOPE_API_MASK | 0x007A)
#define EM_ESCOPE_HOOKS_UNREGISTER_FREE            (EM_ESCOPE_API_MASK | 0x007B)
#define EM_ESCOPE_HOOKS_UNREGISTER_SEND            (EM_ESCOPE_API_MASK | 0x007C)
#define EM_ESCOPE_HOOKS_UNREGISTER_TO_ACTIVE       (EM_ESCOPE_API_MASK | 0x007D)
#define EM_ESCOPE_HOOKS_UNREGISTER_TO_IDLE         (EM_ESCOPE_API_MASK | 0x007E)
#define EM_ESCOPE_HOOKS_UNREGISTER_WHILE_IDLE      (EM_ESCOPE_API_MASK | 0x007F)

/* EM API escopes: Init / Term */
#define EM_ESCOPE_CONF_INIT                        (EM_ESCOPE_API_MASK | 0x0080)
#define EM_ESCOPE_CONF_LOCAL_INIT                  (EM_ESCOPE_API_MASK | 0x0081)
#define EM_ESCOPE_CONF_OPTS                        (EM_ESCOPE_API_MASK | 0x0082)
#define EM_ESCOPE_CONF_LOCAL_OPTS                  (EM_ESCOPE_API_MASK | 0x0083)
#define EM_ESCOPE_INIT                             (EM_ESCOPE_API_MASK | 0x0084)
#define EM_ESCOPE_INIT_CORE                        (EM_ESCOPE_API_MASK | 0x0085)
#define EM_ESCOPE_INIT_LOCAL                       (EM_ESCOPE_API_MASK | 0x0086)
#define EM_ESCOPE_TERM                             (EM_ESCOPE_API_MASK | 0x0087)
#define EM_ESCOPE_TERM_CORE                        (EM_ESCOPE_API_MASK | 0x0088)
#define EM_ESCOPE_TERM_LOCAL                       (EM_ESCOPE_API_MASK | 0x0089)
#define EM_ESCOPE_TERM_LOCAL_INIT                  (EM_ESCOPE_API_MASK | 0x008A)
#define EM_ESCOPE_CFGFILE_OPTS                     (EM_ESCOPE_API_MASK | 0x008B)

/* EM API escopes: Pool */
#define EM_ESCOPE_POOL_CFG_INIT                    (EM_ESCOPE_API_MASK | 0x008C)
#define EM_ESCOPE_POOL_CREATE                      (EM_ESCOPE_API_MASK | 0x008D)
#define EM_ESCOPE_POOL_DELETE                      (EM_ESCOPE_API_MASK | 0x008E)
#define EM_ESCOPE_POOL_FIND                        (EM_ESCOPE_API_MASK | 0x008F)
#define EM_ESCOPE_POOL_FIRST                       (EM_ESCOPE_API_MASK | 0x0090)
#define EM_ESCOPE_POOL_INFO                        (EM_ESCOPE_API_MASK | 0x0091)
#define EM_ESCOPE_POOL_NAME                        (EM_ESCOPE_API_MASK | 0x0092)
#define EM_ESCOPE_POOL_NEXT                        (EM_ESCOPE_API_MASK | 0x0093)
#define EM_ESCOPE_POOL_NUM_SUBPOOLS                (EM_ESCOPE_API_MASK | 0x0094)
#define EM_ESCOPE_POOL_STATS                       (EM_ESCOPE_API_MASK | 0x0095)
#define EM_ESCOPE_POOL_STATS_RESET                 (EM_ESCOPE_API_MASK | 0x0096)
#define EM_ESCOPE_POOL_STATS_SELECTED              (EM_ESCOPE_API_MASK | 0x0097)
#define EM_ESCOPE_POOL_SUBPOOL_STATS               (EM_ESCOPE_API_MASK | 0x0098)
#define EM_ESCOPE_POOL_SUBPOOL_STATS_RESET         (EM_ESCOPE_API_MASK | 0x0099)
#define EM_ESCOPE_POOL_SUBPOOL_STATS_SELECTED      (EM_ESCOPE_API_MASK | 0x009A)
#define EM_ESCOPE_POOL_STATS_OPT                   (EM_ESCOPE_API_MASK | 0x009B)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_POOL_GET_FIRST                   EM_ESCOPE_POOL_FIRST
#define EM_ESCOPE_POOL_GET_NAME                    EM_ESCOPE_POOL_NAME
#define EM_ESCOPE_POOL_GET_NEXT                    EM_ESCOPE_POOL_NEXT

/* EM API escopes: Queue Group */
#define EM_ESCOPE_QUEUE_GROUP_CREATE               (EM_ESCOPE_API_MASK | 0x009C)
#define EM_ESCOPE_QUEUE_GROUP_CREATE_SYNC          (EM_ESCOPE_API_MASK | 0x009D)
#define EM_ESCOPE_QUEUE_GROUP_DELETE               (EM_ESCOPE_API_MASK | 0x009E)
#define EM_ESCOPE_QUEUE_GROUP_DELETE_SYNC          (EM_ESCOPE_API_MASK | 0x009F)
#define EM_ESCOPE_QUEUE_GROUP_FIND                 (EM_ESCOPE_API_MASK | 0x00A0)
#define EM_ESCOPE_QUEUE_GROUP_FIRST                (EM_ESCOPE_API_MASK | 0x00A1)
#define EM_ESCOPE_QUEUE_GROUP_MASK                 (EM_ESCOPE_API_MASK | 0x00A2)
#define EM_ESCOPE_QUEUE_GROUP_MODIFY               (EM_ESCOPE_API_MASK | 0x00A3)
#define EM_ESCOPE_QUEUE_GROUP_MODIFY_SYNC          (EM_ESCOPE_API_MASK | 0x00A4)
#define EM_ESCOPE_QUEUE_GROUP_NAME                 (EM_ESCOPE_API_MASK | 0x00A5)
#define EM_ESCOPE_QUEUE_GROUP_NEXT                 (EM_ESCOPE_API_MASK | 0x00A6)
#define EM_ESCOPE_QUEUE_GROUP_QUEUE_FIRST          (EM_ESCOPE_API_MASK | 0x00A7)
#define EM_ESCOPE_QUEUE_GROUP_QUEUE_NEXT           (EM_ESCOPE_API_MASK | 0x00A8)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_QUEUE_GROUP_GET_FIRST            EM_ESCOPE_QUEUE_GROUP_FIRST
#define EM_ESCOPE_QUEUE_GROUP_GET_NAME             EM_ESCOPE_QUEUE_GROUP_NAME
#define EM_ESCOPE_QUEUE_GROUP_GET_NEXT             EM_ESCOPE_QUEUE_GROUP_NEXT
#define EM_ESCOPE_QUEUE_GROUP_QUEUE_GET_FIRST      EM_ESCOPE_QUEUE_GROUP_QUEUE_FIRST
#define EM_ESCOPE_QUEUE_GROUP_QUEUE_GET_NEXT       EM_ESCOPE_QUEUE_GROUP_QUEUE_NEXT

/* EM API escopes: Queue */
#define EM_ESCOPE_QUEUE_AGGR_CONF_INIT             (EM_ESCOPE_API_MASK | 0x00A9)
#define EM_ESCOPE_QUEUE_AGGR                       (EM_ESCOPE_API_MASK | 0x00AA)
#define EM_ESCOPE_QUEUE_AGGR_INFO                  (EM_ESCOPE_API_MASK | 0x00AB)
#define EM_ESCOPE_QUEUE_AGGR_LIST                  (EM_ESCOPE_API_MASK | 0x00AC)
#define EM_ESCOPE_QUEUE_AGGR_NUM                   (EM_ESCOPE_API_MASK | 0x00AD)
#define EM_ESCOPE_QUEUE_AGROUP                     (EM_ESCOPE_API_MASK | 0x00AE)
#define EM_ESCOPE_QUEUE_CONTEXT                    (EM_ESCOPE_API_MASK | 0x00AF)
#define EM_ESCOPE_QUEUE_CREATE                     (EM_ESCOPE_API_MASK | 0x00B0)
#define EM_ESCOPE_QUEUE_CREATE_STATIC              (EM_ESCOPE_API_MASK | 0x00B1)
#define EM_ESCOPE_QUEUE_CREATE_PARAM               (EM_ESCOPE_API_MASK | 0x00B2)
#define EM_ESCOPE_QUEUE_CURRENT                    (EM_ESCOPE_API_MASK | 0x00B3)
#define EM_ESCOPE_QUEUE_DELETE                     (EM_ESCOPE_API_MASK | 0x00B4)
#define EM_ESCOPE_QUEUE_DEQUEUE                    (EM_ESCOPE_API_MASK | 0x00B5)
#define EM_ESCOPE_QUEUE_DEQUEUE_MULTI              (EM_ESCOPE_API_MASK | 0x00B6)
#define EM_ESCOPE_QUEUE_FIND                       (EM_ESCOPE_API_MASK | 0x00B7)
#define EM_ESCOPE_QUEUE_FIRST                      (EM_ESCOPE_API_MASK | 0x00B8)
#define EM_ESCOPE_QUEUE_INDEX                      (EM_ESCOPE_API_MASK | 0x00B9)
#define EM_ESCOPE_QUEUE_NAME                       (EM_ESCOPE_API_MASK | 0x00BA)
#define EM_ESCOPE_QUEUE_NEXT                       (EM_ESCOPE_API_MASK | 0x00BB)
#define EM_ESCOPE_QUEUE_NUM_PRIO                   (EM_ESCOPE_API_MASK | 0x00BC)
#define EM_ESCOPE_QUEUE_PARAM_INIT                 (EM_ESCOPE_API_MASK | 0x00BD)
#define EM_ESCOPE_QUEUE_PRIORITY                   (EM_ESCOPE_API_MASK | 0x00BE)
#define EM_ESCOPE_QUEUE_QGROUP                     (EM_ESCOPE_API_MASK | 0x00BF)
#define EM_ESCOPE_QUEUE_SET_CONTEXT                (EM_ESCOPE_API_MASK | 0x00C0)
#define EM_ESCOPE_QUEUE_TYPE                       (EM_ESCOPE_API_MASK | 0x00C1)
#define EM_ESCOPE_QUEUE_STATIC_HANDLE              (EM_ESCOPE_API_MASK | 0x00C2)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_ATOMIC_GROUP_GET                 EM_ESCOPE_QUEUE_AGROUP
#define EM_ESCOPE_QUEUE_GET_CONTEXT                EM_ESCOPE_QUEUE_CONTEXT
#define EM_ESCOPE_QUEUE_GET_FIRST                  EM_ESCOPE_QUEUE_FIRST
#define EM_ESCOPE_QUEUE_GET_GROUP                  EM_ESCOPE_QUEUE_QGROUP
#define EM_ESCOPE_QUEUE_GET_INDEX                  EM_ESCOPE_QUEUE_INDEX
#define EM_ESCOPE_QUEUE_GET_NAME                   EM_ESCOPE_QUEUE_NAME
#define EM_ESCOPE_QUEUE_GET_NEXT                   EM_ESCOPE_QUEUE_NEXT
#define EM_ESCOPE_QUEUE_GET_NUM_PRIO               EM_ESCOPE_QUEUE_NUM_PRIO
#define EM_ESCOPE_QUEUE_GET_PRIORITY               EM_ESCOPE_QUEUE_PRIORITY
#define EM_ESCOPE_QUEUE_GET_TYPE                   EM_ESCOPE_QUEUE_TYPE

/* EM API escopes: Scheduler */
#define EM_ESCOPE_ATOMIC_PROCESSING_END            (EM_ESCOPE_API_MASK | 0x00C3)
#define EM_ESCOPE_ORDERED_PROCESSING_END           (EM_ESCOPE_API_MASK | 0x00C4)
#define EM_ESCOPE_PRESCHEDULE                      (EM_ESCOPE_API_MASK | 0x00C5)
#define EM_ESCOPE_SCHED_CONTEXT_TYPE_CURRENT       (EM_ESCOPE_API_MASK | 0x00C6)

/* EM API escopes: Timer */
#define EM_ESCOPE_TIMER_ALL                        (EM_ESCOPE_API_MASK | 0x00C7)
#define EM_ESCOPE_TIMER_ATTR                       (EM_ESCOPE_API_MASK | 0x00C8)
#define EM_ESCOPE_TIMER_ATTR_INIT                  (EM_ESCOPE_API_MASK | 0x00C9)
#define EM_ESCOPE_TIMER_CAPABILITY                 (EM_ESCOPE_API_MASK | 0x00CA)
#define EM_ESCOPE_TIMER_CREATE                     (EM_ESCOPE_API_MASK | 0x00CB)
#define EM_ESCOPE_TIMER_CUR_TICK                   (EM_ESCOPE_API_MASK | 0x00CC)
#define EM_ESCOPE_TIMER_DELETE                     (EM_ESCOPE_API_MASK | 0x00CD)
#define EM_ESCOPE_TIMER_FREQ                       (EM_ESCOPE_API_MASK | 0x00CE)
#define EM_ESCOPE_TIMER_NS_TO_TICK                 (EM_ESCOPE_API_MASK | 0x00CF)
#define EM_ESCOPE_TIMER_RES_CAPABILITY             (EM_ESCOPE_API_MASK | 0x00D0)
#define EM_ESCOPE_TIMER_RING_CREATE                (EM_ESCOPE_API_MASK | 0x00D1)
#define EM_ESCOPE_TIMER_RING_FREQ_CREATE           (EM_ESCOPE_API_MASK | 0x00D2)
#define EM_ESCOPE_TIMER_TICK_TO_NS                 (EM_ESCOPE_API_MASK | 0x00D3)
#define EM_ESCOPE_TMO_ACK                          (EM_ESCOPE_API_MASK | 0x00D4)
#define EM_ESCOPE_TMO_CANCEL                       (EM_ESCOPE_API_MASK | 0x00D5)
#define EM_ESCOPE_TMO_CREATE                       (EM_ESCOPE_API_MASK | 0x00D6)
#define EM_ESCOPE_TMO_CREATE_ARG                   (EM_ESCOPE_API_MASK | 0x00D7)
#define EM_ESCOPE_TMO_DELETE                       (EM_ESCOPE_API_MASK | 0x00D8)
#define EM_ESCOPE_TMO_SET_ABS                      (EM_ESCOPE_API_MASK | 0x00D9)
#define EM_ESCOPE_TMO_SET_PERIODIC                 (EM_ESCOPE_API_MASK | 0x00DA)
#define EM_ESCOPE_TMO_SET_PERIODIC_RING            (EM_ESCOPE_API_MASK | 0x00DB)
#define EM_ESCOPE_TMO_SET_REL                      (EM_ESCOPE_API_MASK | 0x00DC)
#define EM_ESCOPE_TMO_STATE                        (EM_ESCOPE_API_MASK | 0x00DD)
#define EM_ESCOPE_TMO_STATS                        (EM_ESCOPE_API_MASK | 0x00DE)
#define EM_ESCOPE_TMO_TIMER                        (EM_ESCOPE_API_MASK | 0x00DF)
#define EM_ESCOPE_TMO_TYPE                         (EM_ESCOPE_API_MASK | 0x00E0)
#define EM_ESCOPE_TMO_USERPTR                      (EM_ESCOPE_API_MASK | 0x00E1)
#define EM_ESCOPE_TMO_RING_ACK                     (EM_ESCOPE_API_MASK | 0x00E2)
#define EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ       (EM_ESCOPE_API_MASK | 0x00E3)
/* Backwards compatible naming ("get") */
#define EM_ESCOPE_TIMER_GET_ALL                    EM_ESCOPE_TIMER_ALL
#define EM_ESCOPE_TIMER_GET_ATTR                   EM_ESCOPE_TIMER_ATTR
#define EM_ESCOPE_TIMER_GET_FREQ                   EM_ESCOPE_TIMER_FREQ
#define EM_ESCOPE_TMO_GET_STATE                    EM_ESCOPE_TMO_STATE
#define EM_ESCOPE_TMO_GET_STATS                    EM_ESCOPE_TMO_STATS
#define EM_ESCOPE_TMO_GET_TIMER                    EM_ESCOPE_TMO_TIMER
#define EM_ESCOPE_TMO_GET_TYPE                     EM_ESCOPE_TMO_TYPE
#define EM_ESCOPE_TMO_GET_USERPTR                  EM_ESCOPE_TMO_USERPTR
/* Keep last */
#define EM_ESCOPE_API_LAST                         (EM_ESCOPE_API_MASK | 0x00FF)

/******************************************************************************
 * EM Add-on API function error scopes:
 ******************************************************************************/
/* Add-on APIs have a separate escope file but define a base here */
#define EM_ESCOPE_ADD_ON_API_BASE            (EM_ESCOPE_ADDON_API_MASK | 0x0000)

/******************************************************************************
 * EM internal (non-API) error scopes:
 ******************************************************************************/
#define EM_ESCOPE_INTERNAL_FIRST              (EM_ESCOPE_INTERNAL_MASK | 0x0000)

/* EM internal escopes: Event Chaining */
#define EM_ESCOPE_EVENT_SEND_DEVICE           (EM_ESCOPE_INTERNAL_MASK | 0x0001)
#define EM_ESCOPE_EVENT_SEND_DEVICE_MULTI     (EM_ESCOPE_INTERNAL_MASK | 0x0002)

/* EM internal escopes: Event Group */
#define EM_ESCOPE_EVENT_GROUP_UPDATE          (EM_ESCOPE_INTERNAL_MASK | 0x0003)
#define EM_ESCOPE_EVENT_GROUP_UPDATE_PRECNT   (EM_ESCOPE_INTERNAL_MASK | 0x0004)
#define EM_ESCOPE_EVENT_GROUP_UPDATE_POSTCNT  (EM_ESCOPE_INTERNAL_MASK | 0x0005)

/* EM internal escopes: EO */
#define EM_ESCOPE_EO_LOCAL_FUNC_CALL_REQ           (EM_ESCOPE_INTERNAL_MASK | 0x0006)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL_DONE_CB      (EM_ESCOPE_INTERNAL_MASK | 0x0007)
#define EM_ESCOPE_EO_REMOVE_QUEUE_ALL_SYNC_DONE_CB (EM_ESCOPE_INTERNAL_MASK | 0x0008)
#define EM_ESCOPE_EO_REMOVE_QUEUE_DONE_CB          (EM_ESCOPE_INTERNAL_MASK | 0x0009)
#define EM_ESCOPE_EO_REMOVE_QUEUE_SYNC_DONE_CB     (EM_ESCOPE_INTERNAL_MASK | 0x000A)
#define EM_ESCOPE_EO_START_DONE_CB                 (EM_ESCOPE_INTERNAL_MASK | 0x000B)
#define EM_ESCOPE_EO_START_SYNC_DONE_CB            (EM_ESCOPE_INTERNAL_MASK | 0x000C)
#define EM_ESCOPE_EO_STOP_DONE_CB                  (EM_ESCOPE_INTERNAL_MASK | 0x000D)
#define EM_ESCOPE_EO_STOP_SYNC_DONE_CB             (EM_ESCOPE_INTERNAL_MASK | 0x000E)

/* EM internal escopes: Queue */
#define EM_ESCOPE_QUEUE_DISABLE               (EM_ESCOPE_INTERNAL_MASK | 0x000F)
#define EM_ESCOPE_QUEUE_DISABLE_ALL           (EM_ESCOPE_INTERNAL_MASK | 0x0010)
#define EM_ESCOPE_QUEUE_ENABLE                (EM_ESCOPE_INTERNAL_MASK | 0x0011)
#define EM_ESCOPE_QUEUE_ENABLE_ALL            (EM_ESCOPE_INTERNAL_MASK | 0x0012)
#define EM_ESCOPE_QUEUE_STATE_CHANGE          (EM_ESCOPE_INTERNAL_MASK | 0x0013)

/* EM internal escopes: Queue Groups */
#define EM_ESCOPE_QUEUE_GROUP_ADD_CORE        (EM_ESCOPE_INTERNAL_MASK | 0x0014)
#define EM_ESCOPE_QUEUE_GROUP_REM_CORE        (EM_ESCOPE_INTERNAL_MASK | 0x0015)

/* EM internal escopes: Other */
#define EM_ESCOPE_DELETE_CTRL_QUEUES          (EM_ESCOPE_INTERNAL_MASK | 0x0016)
#define EM_ESCOPE_EVENT_INTERNAL_DONE         (EM_ESCOPE_INTERNAL_MASK | 0x0017)
#define EM_ESCOPE_EVENT_INTERNAL_LFUNC_CALL   (EM_ESCOPE_INTERNAL_MASK | 0x0018)
#define EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ   (EM_ESCOPE_INTERNAL_MASK | 0x0019)
#define EM_ESCOPE_INTERNAL_EVENT_RECV_FUNC    (EM_ESCOPE_INTERNAL_MASK | 0x001A)
#define EM_ESCOPE_INTERNAL_NOTIF              (EM_ESCOPE_INTERNAL_MASK | 0x001B)
#define EM_ESCOPE_SEND_CTRL_QUEUE             (EM_ESCOPE_INTERNAL_MASK | 0x001C)

/**
 * EM ODP extensions error scope
 */
#define EM_ESCOPE_ODP_EXT                    (EM_ESCOPE_INTERNAL_MASK | 0x001D)

/* Keep last */
#define EM_ESCOPE_INTERNAL_LAST              (EM_ESCOPE_INTERNAL_MASK | 0x001E)

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_ERROR_H_ */
