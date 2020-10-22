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

/*
 * EM Error Handling
 */

#include <libgen.h> /* basename() */
#include "em_include.h"

/* Make sure that EM internally used error scopes are "seen" by EM_ESCOPE() */
COMPILE_TIME_ASSERT(EM_ESCOPE(EM_ESCOPE_API_MASK),
		    EM_ESCOPE_API_IS_NOT_PART_EM_ESCOPE__ERROR);
COMPILE_TIME_ASSERT(EM_ESCOPE(EM_ESCOPE_INTERNAL_MASK),
		    EM_ESCOPE_INTERNAL_IS_NOT_PART_OF_EM_ESCOPE__ERROR);

static int error_handler_initialized; /* statics always initialized to 0 */

static early_log_t early_log = {default_log, vdefault_log};

static em_status_t
early_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		    va_list args);

static void
increment_global_err_cnt(void)
{
	env_atomic64_inc(&em_shm->error_handler.global_error_count);
}

uint64_t
load_global_err_cnt(void)
{
	uint64_t cnt =
	env_atomic64_get(&em_shm->error_handler.global_error_count);

	return cnt;
}

ODP_PRINTF_FORMAT(2, 0)
int vdefault_log(em_log_level_t level, const char *fmt, va_list args)
{
	int r;
	FILE *logfd;

	switch (level) {
	case EM_LOG_DBG:
	case EM_LOG_PRINT:
		logfd = stdout;
		break;
	case EM_LOG_ERR:
	default:
		logfd = stderr;
		break;
	}

	r = vfprintf(logfd, fmt, args);
	return r;
}

ODP_PRINTF_FORMAT(2, 3)
int default_log(em_log_level_t level, const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vdefault_log(level, fmt, args);
	va_end(args);

	return r;
}

/**
 * Early EM Error Handler
 *
 * The early error handler is used when reporting errors at startup before
 * the default, application and EO specific error handlers have been set up.
 *
 * @param eo      unused
 * @param error   The error code (reason), see em_status_e
 * @param escope  The error scope from within the error was reported, also
 *                tells whether the error was EM internal or application
 *                specific
 * @param args    va_list of args
 *
 * @return The function may not return depending on implementation / error
 *         code / error scope. If it returns, the return value is the original
 *         (or modified) error code from the caller.
 */
static em_status_t
early_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		    va_list args)
{
	int core_id = odp_cpu_id(); /* em_core_id() does not work here */
	em_log_func_t log_fn = early_log.log_fn;
	em_vlog_func_t vlog_fn = early_log.vlog_fn;

	if (EM_ESCOPE(escope)) {
		/*
		 * va_list contains: __FILE__, __func__, __LINE__, (format),
		 * ## __VA_ARGS__ as reported by the INTERNAL_ERROR macro
		 */
		char *file = va_arg(args, char*);
		const char *func = va_arg(args, const char*);
		const int line = va_arg(args, const int);
		const char *format = va_arg(args, const char*);
		const char *base = basename(file);
		(void)eo;

		log_fn(EM_LOG_ERR, "\n"
		       "EM ERROR:0x%08X  ESCOPE:0x%08X  (Early Error)\n"
		       "core:%02i %s:%i %s()\n",
		       error, escope, core_id, base, line, func);
		vlog_fn(EM_LOG_ERR, format, args);
		log_fn(EM_LOG_ERR, "\n");
	} else {
		/* va_list from application - don't touch. */
		log_fn(EM_LOG_ERR, "\n"
		       "APPL ERROR:0x%08X  ESCOPE:0x%08X  (Early Error)\n"
		       "core:%02i\n",
		       error, escope, core_id);
	}

	if (unlikely(EM_ERROR_IS_FATAL(error))) {
		log_fn(EM_LOG_ERR,
		       "FATAL ERROR:0x%08X (Early Error) - ABORT!\n",
		       error);
		abort();
	}

	return error;
}

/**
 * Default EM Error Handler
 *
 * The default error handler is called upon error if the application(s)
 * have not registered their own global and/or EO-specific error handlers
 *
 * @param eo      EO reporting the error (if applicable)
 * @param error   The error code (reason), see em_status_e
 * @param escope  The error scope from within the error was reported, also
 *                tells whether the error was EM internal or application
 *                specific
 * @param args    va_list of args
 *
 * @return The function may not return depending on implementation / error
 *         code / error scope. If it returns, the return value is the original
 *         (or modified) error code from the caller.
 */
em_status_t
default_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		      va_list args)
{
	char eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];
	const int core_id = em_core_id();
	const uint64_t local_err_cnt = em_locm.error_count;
	const uint64_t global_err_cnt = load_global_err_cnt();

	if (eo == EM_EO_UNDEF) {
		eo_str[0] = '\0';
	} else {
		char eo_name[EM_EO_NAME_LEN];
		size_t nlen;

		nlen = em_eo_get_name(eo, eo_name, sizeof(eo_name));
		eo_name[nlen] = '\0';

		snprintf(eo_str, sizeof(eo_str),
			 "EO:%" PRI_EO "-\"%s\"  ", eo, eo_name);
		eo_str[sizeof(eo_str) - 1] = '\0';
	}

	if (EM_ESCOPE(escope)) {
		/*
		 * va_list contains: __FILE__, __func__, __LINE__, (format),
		 * ## __VA_ARGS__ as reported by the INTERNAL_ERROR macro
		 */
		char *file = va_arg(args, char*);
		const char *func = va_arg(args, const char*);
		const int line = va_arg(args, const int);
		const char *format = va_arg(args, const char*);
		const char *base = basename(file);

		EM_LOG(EM_LOG_ERR, "\n"
		       "EM ERROR:0x%08X  ESCOPE:0x%08X  %s\n"
		       "core:%02i ecount:%" PRIu64 "(%" PRIu64 ") %s:%i %s()\n",
		       error, escope, eo_str, core_id,
		       global_err_cnt, local_err_cnt,
		       base, line, func);
		EM_VLOG(EM_LOG_ERR, format, args);
		EM_LOG(EM_LOG_ERR, "\n");
	} else {
		/* va_list from application - don't touch. */
		EM_LOG(EM_LOG_ERR, "\n"
		       "APPL ERROR:0x%08X  ESCOPE:0x%08X  %s\n"
		       "core:%02i ecount:%" PRIu64 "(%" PRIu64 ")\n",
		       error, escope, eo_str, core_id,
		       global_err_cnt, local_err_cnt);
	}

	if (unlikely(EM_ERROR_IS_FATAL(error))) {
		EM_LOG(EM_LOG_ERR, "FATAL ERROR:0x%08X - ABORT!\n", error);
		abort();
	}

	return error;
}

/**
 * Select and call an error handler.
 *
 * @param error         Error code
 * @param escope        Error scope. Identifies the scope for interpreting
 *                      the error code and variable arguments.
 * @param args_list     Variable number and type of arguments
 *
 * @return Returns the 'error' argument given as input if the called error
 *         handler has not changed this value.
 */
em_status_t
select_error_handler(em_status_t error, em_escope_t escope, va_list args_list)
{
	if (unlikely(!error_handler_initialized)) {
		/*
		 * Early errors reported at startup before error handling
		 * is properly initialized.
		 */
		error = early_error_handler(EM_EO_UNDEF, error, escope,
					    args_list);
	} else {
		eo_elem_t *eo_elem = get_current_eo_elem();
		em_eo_t eo = EM_EO_UNDEF;
		em_error_handler_t error_handler = default_error_handler;

		if (em_shm != NULL)
			error_handler = em_shm->error_handler.em_error_handler;

		if (eo_elem != NULL) {
			eo = eo_elem->eo;
			if (eo_elem->error_handler_func)
				error_handler = eo_elem->error_handler_func;
		}

		if (error_handler) {
			/*
			 * Call the selected error handler and possibly
			 * change the error code.
			 */
			error = error_handler(eo, error, escope, args_list);
		}

		if (error != EM_OK) {
			/* Increase the error count, used in logs/printouts */
			increment_global_err_cnt();
			em_locm.error_count += 1;
		}
	}

	/* Return input error or value changed by error_handler */
	return error;
}

/**
 * Called ONLY from INTERNAL_ERROR macro - do not use for anything else!
 * _internal_error((error), (escope), __FILE__, __func__, __LINE__,
 *                 (format), ## __VA_ARGS__)
 */
em_status_t
_internal_error(em_status_t error, em_escope_t escope, ...)
{
	/*
	 * va_list contains:
	 * __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__
	 */
	va_list args;

	va_start(args, escope);

	/* Select and call an error handler. Possibly modifies the error code*/
	error = select_error_handler(error, escope, args);

	va_end(args);

	/* Return input error or value changed by error_handler */
	return error;
}

/**
 * Initialize the EM Error Handling
 */
void
error_init(void)
{
	env_spinlock_init(&em_shm->error_handler.lock);

	em_shm->error_handler.em_error_handler = default_error_handler;

	env_atomic64_init(&em_shm->error_handler.global_error_count);

	em_shm->error_handler.initialized = 1; /* For API functions */
	error_handler_initialized = 1; /* For early error handler select */
}

em_status_t
early_log_init(em_log_func_t user_log_fn, em_vlog_func_t user_vlog_fn)
{
	/* Check that both log fns are either set or both NULL */
	int is_log_null = user_log_fn == NULL;
	int is_vlog_null = user_vlog_fn == NULL;

	if (is_log_null != is_vlog_null)
		return EM_ERR_BAD_POINTER;

	/* store user provided log functions */
	if (user_log_fn != NULL) {
		early_log.log_fn = user_log_fn;
		early_log.vlog_fn = user_vlog_fn;
	}

	return EM_OK;
}

void
log_init(void)
{
	em_shm->log_fn = early_log.log_fn;
	em_shm->vlog_fn = early_log.vlog_fn;
}
