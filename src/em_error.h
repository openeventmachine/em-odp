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

/**
 * @file
 *
 * Event Machine Error Handler functions
 */

#ifndef EM_ERROR_H
#define EM_ERROR_H

/**
 * Internal error reporting macro
 */
#define INTERNAL_ERROR(error, escope, fmt, ...)		\
	internal_error((error), (escope), __FILE__, __func__,	\
			__LINE__, fmt, ## __VA_ARGS__)

/**
 * Internal macro for return on error
 */
#define RETURN_ERROR_IF(cond, error, escope, fmt, ...) {	  \
	if (unlikely((cond))) {					  \
		return INTERNAL_ERROR((error), (escope),	  \
				      fmt, ## __VA_ARGS__);  \
	}							  \
}

#define EM_LOG(level, fmt, ...) (em_shm->log_fn((level), fmt, ## __VA_ARGS__))

#define EM_VLOG(level, fmt, args) (em_shm->vlog_fn((level), fmt, (args)))

#define EM_PRINT(fmt, ...) EM_LOG(EM_LOG_PRINT, fmt, ## __VA_ARGS__)

/*
 * Print debug message to log (only if EM_DEBUG_PRINT is set)
 */
#define EM_DBG(fmt, ...) {				\
	if (EM_DEBUG_PRINT == 1)			\
		EM_LOG(EM_LOG_DBG, fmt, ##__VA_ARGS__); \
}

/**
 * EM internal error
 * Don't call directly, should _always_ be used from within the error-macros
 */
em_status_t
internal_error(em_status_t error, em_escope_t escope, ...);

em_status_t
early_log_init(em_log_func_t user_log_fn, em_vlog_func_t user_vlog_fn);

void
log_init(void);

void
error_init(void);

em_status_t
default_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		      va_list args);

em_status_t
select_error_handler(em_status_t error, em_escope_t escope, va_list args_list);

uint64_t
load_global_err_cnt(void);

ODP_PRINTF_FORMAT(2, 3)
int default_log(em_log_level_t level, const char *fmt, ...);

int
vdefault_log(em_log_level_t level, const char *fmt, va_list args);

#endif /* EM_ERROR_H_ */
