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

#ifndef EVENT_MACHINE_CORE_H_
#define EVENT_MACHINE_CORE_H_

#pragma GCC visibility push(default)

/**
 * @file
 * @defgroup em_core EM-core related
 *  core (thread) specific APIs.
 * @{
 * An EM-core is a thread/process initialized through EM init APIs with the
 * core type EM_CORE_TYPE_WORKER or EM_CORE_TYPE_CONTROL and is running the
 * EM dispatch loop.
 *
 * The Event Machine enumerates EM-cores within an EM instance as integers
 * starting from 0. When adding an EM-core by calling em_init_local() on a
 * new thread, the next lowest available integer (>=0) will be assigned to the
 * EM-core as an id. Removing an EM-core from the instance frees the id and can
 * leave a potential gap in the id range. This means that the core id range
 * might not be coutiguous after a removal/termination of an EM-core.
 * The next EM-core added will again acquire the first available core id and
 * potentially fill a gap in the EM-core id range.
 */

#include <event_machine/api/event_machine_core_mask.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EM core type
 */
typedef enum {
	/** Undef EM-core type */
	EM_CORE_TYPE_UNDEF = 0,

	/**
	 * Worker EM-core, has an EM-core ID and must call em_dispatch...().
	 * An EM worker core is pinned to a single CPU by the user before
	 * calling em_init_local() and it runs the EM dispatch loop with
	 * no interrupts.
	 * Meant for real-time processing of events ("RT-core(s)").
	 */
	EM_CORE_TYPE_WORKER,

	/**
	 * Control EM-core, has an EM-core ID and calls dispatch (at least
	 * occasionally).
	 * An EM control core can e.g. function as a non-realtime EM-core that
	 * can participate in dispatching of events, but usually via carefully
	 * selected queues and queue groups to avoid disturbing the EM worker
	 * cores that can have real time workloads.
	 * An EM control core need not be pinned to single CPU, but still
	 * runs the dispatch loop.
	 * Meant for non-real-time processing ("NRT-core(s)").
	 */
	EM_CORE_TYPE_CONTROL,

	/**
	 * External EM thread/process, no EM-core ID, no dispatching allowed.
	 *
	 * An EM external thread is not allowed to participate in event
	 * dispatching (i.e. it must not call any em_dispatch...() functions).
	 * An EM external thread can create/delete EM resources, send events and
	 * poll unscheduled queues etc. but is not considered an EM-core
	 * (hence no bit in an EM coremask) and cannot receive events via
	 * scheduled queues since dispatching is forbidden.
	 * Meant for external control or monitoring of EM.
	 */
	EM_CORE_TYPE_EXTERNAL,

	/** Last, for bounds checking only */
	EM_CORE_TYPE_LAST
} em_core_type_t;

/**
 * Get the EM-core id of the current EM thread/process.
 *
 * Returns the EM-core id (logical id) of the calling thread/process.
 * The Event Machine enumerates EM-cores (i.e. threads/processes of type
 * EM_CORE_TYPE_WORKER or EM_CORE_TYPE_CONTROL running the EM dispatch loop)
 * within an EM instance as integers starting from 0. The core id range can
 * contain gaps if EM-cores have been terminated/removed. Note that external
 * threads/processes of type EM_CORE_TYPE_EXTERNAL do not have an EM-core id and
 * this function will return -1 for those instead.
 * The thread/process must have been initialized as an EM-core with
 * em_init_local() (or the older em_init_core()) to have a valid EM-core id.
 *
 * @note The core id returned for an EM external thread/process of type
 *       EM_CORE_TYPE_EXTERNAL is -1 (i.e. no EM-core id available).
 *
 * @return Current logical EM core id.
 * @retval  0 to EM_MAX_CORES-1 for EM_CORE_TYPE_WORKER or EM_CORE_TYPE_CONTROL
 * @retval -1 for EM_CORE_TYPE_EXTERNAL
 * @retval undefined for threads/processes not initialized as EM-cores
 */
int em_core_id(void);

/**
 * The number of EM-cores currently running within the EM instance.
 *
 * Adding an EM-core with em_init_local() increases the core count by one (1).
 * Removing an EM-core with em_term_local() decreases the core count by one (1).
 *
 * Only EM-cores of the type EM_CORE_TYPE_WORKER and EM_CORE_TYPE_CONTROL are
 * included in the count.
 *
 * Note that the EM-core count is not constant, instead it varies as EM-cores
 * are added or removed.
 * The current EM-core count is limited by em_conf_t::core_count that sets the
 * maximum count for the EM instance.
 *
 * @return The number of currently running EM-cores.
 */
uint32_t em_core_count(void);

/**
 * Maximum number of concurrently running EM-cores allowed for this EM instance.
 *
 * The maximum number of EM-cores is a constant value set at startup and cannot
 * be changed afterwards. The value is the same as the em_conf_t::core_count set
 * by the user and passed to em_init() (if accepted by EM).
 * The user provided maximum EM-core count value is within the range
 * [1, EM_MAX_CORES].
 * @see em_conf_t::core_count
 *
 * The max number of EM-cores is concerned with concurrently running
 * EM-cores of the type EM_CORE_TYPE_CONTROL or EM_CORE_TYPE_WORKER.
 * Additionally the user may create threads/processes of type
 * EM_CORE_TYPE_EXTERNAL that are not included in this max count,
 * see em_core_type_t.
 *
 * @return Maximum number of EM-cores supported by this EM instance.
 */
uint32_t em_core_count_max(void);

/**
 * The number of EM-cores of the specified type currently running within the
 * EM instance.
 *
 * Similar to em_core_count(), but only counts EM-cores of the specified type.
 *
 * @param core_type  The type of the EM-cores to include in the count.
 *                   The core type can be one of the following:
 *                   - EM_CORE_TYPE_WORKER: only include worker EM-cores
 *                   - EM_CORE_TYPE_CONTROL: only include control EM-cores
 *                   - EM_CORE_TYPE_UNDEF: include both worker and control EM-cores
 *                     (special usage for the '_UNDEF' type here)
 *                   - EM_CORE_TYPE_EXTERNAL: only include EM external
 *                     threads/processes (EM external threads/processes are not
 *                     considered EM-cores since they do not have an EM-core id)
 *
 * Note that the EM-core count is not constant, instead it varies as EM-cores
 * are added or removed.
 *
 * @return The number of currently running EM-cores.
 */
uint32_t em_core_type_count(em_core_type_t core_type);

/**
 * Initialize EM-core id iteration and get the first EM-core id.
 *
 * Start a new iteration over EM-core ids and get the EM-core id of the first
 * (=lowest id) EM-core available at the time of the call.
 * Optionally outputs the current number of EM-cores via the 'num' output
 * parameter.
 * The iteration can be continued with em_core_id_next() until all EM-core
 * ids have been returned. The iteration stops when either of the functions
 * return -1.
 *
 * Only EM-cores of the type EM_CORE_TYPE_WORKER and/or EM_CORE_TYPE_CONTROL are
 * included in the iteration, as set by the 'core_type' parameter.
 *
 * The EM-core id range may not be contiguous if EM-cores have been removed
 * after startup. The iteration will return the available EM-core ids in
 * ascending order. The iteration functions always represent a snapshot of the
 * available core-ids at a certain time - if EM-cores are added or removed just
 * after any of the calls then the returned ids might be not be up-to-date.
 *
 * Example usage:
 * @code
 *	uint32_t num;
 *	int core_id = em_core_id_first(EM_CORE_TYPE_WORKER, &num);
 *	while (core_id >= 0) {
 *		...
 *		core_id = em_core_id_next();
 *	}
 * @endcode
 *
 * @param core_type  EM-core type to include in the iteration.
 *                   The core type can be one of the following:
 *                   - EM_CORE_TYPE_WORKER: only include worker EM-cores
 *                   - EM_CORE_TYPE_CONTROL: only include control EM-cores
 *                   - EM_CORE_TYPE_UNDEF: include both worker and control EM-cores
 *                     (special usage for the '_UNDEF' type here)
 * @param[out] num   Optional output parameter to store the current
 *                   number of EM-cores.
 *
 * @note the 'num' output parameter only represent the number of EM-cores at the
 *       time of the call and thus the number of iterations with
 *       em_core_id_next() until no more EM-cores are available may differ.
 *
 * @return the first available EM-core id or -1 if no EM-cores exist
 * @retval -1 if no EM-cores exist (of type worker or control)
 *
 * @see em_core_id_next()
 */
int em_core_id_first(em_core_type_t core_type, uint32_t *num /*out*/);

/**
 * Continue iteration over EM-core ids and get the next EM-core id available.
 *
 * Continue the EM-core iteration started by em_core_id_first() and get the
 * next EM-core id available at the time of the call. The iteration can be
 * continued with successive calls to em_core_id_next() until all EM-core
 * ids have been returned. The iteration stops when the function returns -1.
 *
 * Only EM-cores of the type EM_CORE_TYPE_WORKER and/or EM_CORE_TYPE_CONTROL are
 * included in the iteration, as set by the 'core_type' parameter in the
 * em_core_id_first() call.
 *
 * @note This API function must only be used after first successfully calling
 *       em_core_id_first() to initialize the iteration.
 *
 * @return the next available EM-core id or -1 if no more EM-cores exist
 *
 * @see em_core_id_first()
 */
int em_core_id_next(void);

/**
 * Retrieve the core mask for all available EM-cores of the specified core type.
 *
 * This function populates the provided core mask with the currently running
 * EM-cores that match the specified core type and returns the number of
 * EM-cores of the given type set in the mask.
 *
 * @param core_type  The type of the EM-cores to include in the mask.
 *                   The core type can be one of the following:
 *                   - EM_CORE_TYPE_WORKER: only include worker EM-cores
 *                   - EM_CORE_TYPE_CONTROL: only include control EM-cores
 *                   - EM_CORE_TYPE_UNDEF: include both worker and control EM-cores
 *                     (special usage for the '_UNDEF' type here)
 * @param core_mask  Pointer to the core mask to be populated.
 *                   The core mask is cleared before the EM-cores are set, thus
 *                   also in error cases the mask will be zeroed (e.g. when an
 *                   invalid 'core_type' is given).
 *
 * @return the number of EM-cores of the specified type set in the mask
 * @retval 0 if no EM-cores of the given type available
 */
uint32_t em_core_ids_available(em_core_type_t core_type,
			       em_core_mask_t *core_mask /*out*/);

/**
 * EM-core type of the calling EM thread/process.
 *
 * Return the EM-core type of the calling thread/process.
 * The thread/process must have been initialized as an EM-core with
 * em_init_local() (or the older em_init_core()) to have a valid EM-core type.
 *
 * @return EM-core type.
 * @retval undefined for threads/processes not initialized as EM-cores
 */
em_core_type_t em_core_type(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_CORE_H_ */
