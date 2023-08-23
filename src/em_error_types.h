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
 * Event Machine Error Handler types
 */

#ifndef EM_ERROR_TYPES_H_
#define EM_ERROR_TYPES_H_

typedef struct {
	em_log_func_t log_fn;
	em_vlog_func_t vlog_fn;
} early_log_t;

/**
 * EM Error Handler
 */
typedef union {
	struct {
		/** Global Error Handler (func ptr or NULL) */
		em_error_handler_t em_error_handler ENV_CACHE_LINE_ALIGNED;
		/** Global Error Count */
		env_atomic64_t global_error_count;
		/** Lock */
		env_spinlock_t lock;
		/** Is the error handler initialized? */
		int initialized;
	};
	/* Pads size to 1*cache-line-size */
	uint8_t u8[ENV_CACHE_LINE_SIZE];

} error_handler_t;

COMPILE_TIME_ASSERT(sizeof(error_handler_t) == ENV_CACHE_LINE_SIZE,
		    ERROR_HANDLER_T_SIZE_ERROR);

#endif /* EM_ERROR_TYPES_H_ */
