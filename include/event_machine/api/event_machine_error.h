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

#ifndef EVENT_MACHINE_ERROR_H_
#define EVENT_MACHINE_ERROR_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_error Error management
 *  Error/Exception management
 *
 *  OpenEM provides two basic mechanisms to manage operational exceptions
 *
 *  -# Return value
 *    - Most API calls return a status which can be checked by the application
 *
 *  -# Error handler
 *    - An optional error handler function can be registered. The given
 *      function is called by the EM implementation as an error occurs. This
 *      makes it possible to centralize the error management or even modify
 *      some API behavior as an error handler can modify the original API call
 *      return value as well. For instance, a global error handler could modify
 *      the em_send() behavior in such a way that the application never has to
 *      check the return value by catching all errors, fixing them (like
 *      deallocating event on failed send) and returning EM_OK.
 *    - There can be one global error handler and additionally one per each EO.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>

/**
 * Error handler prototype
 *
 * The error handler is called after EM notices an error or the user has called
 * em_error().
 *
 * The user can register EO specific and/or EM global error handlers. When an
 * error occurs, EM calls the EO specific error handler, if registered. If
 * there's no EO specific handler registered or the error occurs outside of an
 * EO context, EM calls the global error handler. If no error handlers are
 * found, EM just returns the error code depending on the API function.
 *
 * The error handler is called with the original error code (that's about to be
 * returned) from the API call or em_error(). The error scope identifies the
 * source of the error and how the error code and the variable arguments should
 * be interpreted (number of arguments and types) in an implementation specific
 * way. For each API call scope the list starts with the arguments given to the
 * API called. Each EM implementation should provide a list of arguments for
 * each error scope.
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
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_ERROR_H_ */
