/*
 *   Copyright (c) 2014, Nokia Solutions and Networks
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

#ifndef EVENT_MACHINE_ATOMIC_GROUP_H_
#define EVENT_MACHINE_ATOMIC_GROUP_H_

/**
 * @file
 * @defgroup em_atomic_group Atomic group
 *  Event Machine atomic queue group (new to API 1.1)
 * @{
 *
 * An atomic group combines multiple atomic queues in such a way that only one
 * event from any of the included queues can be scheduled to the application at
 * any one time, effectively scheduling several queues like a single atomic one.
 * The main use case is to provide a protected race free context by atomic
 * scheduling but allow multiple priorities (all of the queue contexts for
 * queues in one atomic group are protected. The related EO context is also
 * protected, if all queues of that EO are in one atomic group only, thus
 * effectively creating an atomic EO).
 *
 * All queues in an atomic group belong to the same queue group given when
 * creating a new atomic group. Queues for an atomic group must be created
 * using _ag - functions, but can be deleted normally using em_queue_delete().
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event_machine/api/event_machine_types.h>

/**
 * Create a new atomic group
 *
 * Some systems may have a limited number of atomic groups available or a
 * limited number of queues per atomic group.
 *
 * The given name string is copied into an EM internal data structure. The
 * maximum string length is EM_ATOMIC_GROUP_NAME_LEN. Duplicate names are
 * allowed, but find will only match one of them.
 *
 * @param name           Atomic group name (optional, NULL ok)
 * @param queue_group    Existing queue group to use for this atomic group
 *
 * @return Atomic group or EM_ATOMIC_GROUP_UNDEF on error.
 */
em_atomic_group_t
em_atomic_group_create(const char *name, em_queue_group_t queue_group);

/**
 * Delete an atomic group.
 *
 * @attention An atomic group can only be deleted after all queues belonging
 * to it have been removed from the EOs and deleted.
 *
 * @param atomic_group    Atomic group to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_atomic_group_create()
 */
em_status_t
em_atomic_group_delete(em_atomic_group_t atomic_group);

/**
 * Create a new queue with a dynamic queue handle belonging to an atomic group.
 *
 * Queues created with this are always of type EM_QUEUE_TYPE_ATOMIC.
 *
 * The given name string is copied to EM internal data structure. The maximum
 * string length is EM_QUEUE_NAME_LEN.
 *
 * The 'conf' argument is optional and can be used to pass extra attributes
 * (e.g. require non-blocking behaviour, if supported) to the system specific
 * implementation.
 *
 * @param name          Queue name (optional, NULL ok)
 * @param prio          Queue priority
 * @param atomic_group  Existing atomic group for this queue
 * @param conf          Optional configuration data, NULL for defaults
 *
 * @return New queue handle or EM_QUEUE_UNDEF on an error.
 *
 * @see em_atomic_group_create(), em_queue_delete(), em_queue_create()
 */
em_queue_t
em_queue_create_ag(const char *name, em_queue_prio_t prio,
		   em_atomic_group_t atomic_group, const em_queue_conf_t *conf);

/**
 * Create a new queue with a static queue handle belonging to an atomic group.
 *
 * Otherwise equivalent to em_queue_create_ag().
 *
 * Note, that the system may have a limited amount of static handles available,
 * so prefer the use of dynamic queues, unless static handles are really needed.
 * The range of static identifiers/handles is system dependent, but macros
 * EM_QUEUE_STATIC_MIN and EM_QUEUE_STATIC_MAX can be used to abstract actual
 * values, e.g. use EM_QUEUE_STATIC_MIN+x for the application.
 *
 * @param name          Queue name (optional, NULL ok)
 * @param prio          Queue priority
 * @param atomic_group  Existing atomic group for this queue
 * @param queue         Requested queue handle from the static range
 * @param conf          Optional configuration data, NULL for defaults
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_create_ag(), em_atomic_group_create(), em_queue_delete()
 */
em_status_t
em_queue_create_static_ag(const char *name, em_queue_prio_t prio,
			  em_atomic_group_t atomic_group, em_queue_t queue,
			  const em_queue_conf_t *conf);

/**
 * Get the associated atomic group of the given queue.
 *
 * Returns the atomic group of the given queue.
 *
 * @param queue         Queue for the query
 *
 * @return Queue specific atomic group or EM_ATOMIC_GROUP_UNDEF if
 * the given queue is not valid or belong to an atomic group.
 *
 * @see em_atomic_group_create()
 */
em_atomic_group_t
em_atomic_group_get(em_queue_t queue);

/**
 * Get the name of an atomic group.
 *
 * A copy of the name string (up to 'maxlen' characters) is written to the user
 * given buffer.
 * The string is always null terminated, even if the given buffer length is less
 * than the name length.
 *
 * If the atomic group has no name, the function returns 0 and writes an
 * empty string.
 *
 * @param atomic_group  Atomic group
 * @param name          Destination buffer
 * @param maxlen        Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0').
 */
size_t
em_atomic_group_get_name(em_atomic_group_t atomic_group,
			 char *name, size_t maxlen);

/**
 * Find atomic group by name.
 *
 * Finds an atomic group by the given name (exact match). An empty string will
 * not match anything. The search is case sensitive. If there are duplicate
 * names, this will return the first match only.
 *
 * @param name          the name to look for
 *
 * @return atomic group or EM_ATOMIC_GROUP_UNDEF if not found.
 */
em_atomic_group_t
em_atomic_group_find(const char *name);

/**
 * Initialize atomic group iteration and return the first atomic group handle.
 *
 * Can be used to initialize the iteration to retrieve all created atomic groups
 * for debugging or management purposes. Use em_atomic_group_get_next() after
 * this call until it returns EM_ATOMIC_GROUP_UNDEF.
 * A new call to em_atomic_group_get_first() resets the iteration, which is
 * maintained per core (thread). The operation should be completed in one go
 * before returning from the EO's event receive function (or start/stop).
 *
 * The number of atomic groups (output arg 'num') may not match the amount of
 * atomic groups actually returned by iterating using em_atomic_group_get_next()
 * if atomic groups are added or removed in parallel by another core. The order
 * of the returned atomic group handles is undefined.
 *
 * @code
 *	unsigned int num;
 *	em_atomic_group_t ag = em_atomic_group_get_first(&num);
 *	while (ag != EM_ATOMIC_GROUP_UNDEF) {
 *		ag = em_atomic_group_get_next();
 *	}
 * @endcode
 *
 * @param num [out]  Pointer to an unsigned int to store the amount of
 *                   atomic groups into
 * @return The first atomic group handle or EM_ATOMIC_GROUP_UNDEF if none exist
 *
 * @see em_atomic_group_get_next()
 */
em_atomic_group_t
em_atomic_group_get_first(unsigned int *num);

/**
 * Return the next atomic group handle.
 *
 * Continues the atomic group iteration started by em_atomic_group_get_first()
 * and returns the next atomic group handle.
 *
 * @return The next atomic group handle or EM_ATOMIC_GROUP_UNDEF if the atomic
 *         group iteration is completed (i.e. no more atomic groups available).
 *
 * @see em_atomic_group_get_first()
 */
em_atomic_group_t
em_atomic_group_get_next(void);

/**
 * Initialize iteration of an atomic group's queues and return the first
 * queue handle.
 *
 * Can be used to initialize the iteration to retrieve all queues associated
 * with the given atomic group for debugging or management purposes.
 * Use em_atomic_group_queue_get_next() after this call until it returns
 * EM_QUEUE_UNDEF.
 * A new call to em_atomic_group_queue_get_first() resets the iteration, which
 * is maintained per core (thread). The operation should be started and
 * completed in one go before returning from the EO's event receive function (or
 * start/stop).
 *
 * The number of queues in the atomic group (output arg 'num') may not match the
 * amount of queues actually returned by iterating using
 * em_atomic_group_queue_get_next() if queues are added or removed in parallel
 * by another core. The order of the returned queue handles is undefined.
 *
 * Simplified example:
 * @code
 *	unsigned int num;
 *	em_queue_t q = em_atomic_group_queue_get_first(&num, atomic_group);
 *	while (q != EM_QUEUE_UNDEF) {
 *		q = em_atomic_group_queue_get_next();
 *	}
 * @endcode
 *
 * @param num [out]    Pointer to an unsigned int to store the amount of queues
 *                     into.
 * @param queue_group  Atomic group handle
 *
 * @return The first queue handle or EM_QUEUE_UNDEF if none exist or the
 *         atomic group is invalid.
 *
 * @see em_atomic_group_queue_get_next()
 */
em_queue_t
em_atomic_group_queue_get_first(unsigned int *num,
				em_atomic_group_t atomic_group);

/**
 * Return the atomic group's next queue handle.
 *
 * Continues the queue iteration started by em_atomic_group_queue_get_first()
 * and returns the next queue handle in the atomic group.
 *
 * @return The next queue handle or EM_QUEUE_UNDEF if the queue iteration is
 *         completed (i.e. no more queues available for this atomic group).
 *
 * @see em_atomic_group_queue_get_first()
 */
em_queue_t
em_atomic_group_queue_get_next(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_ATOMIC_GROUP_H_ */
