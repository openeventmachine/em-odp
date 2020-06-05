/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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

#ifndef EVENT_MACHINE_H
#define EVENT_MACHINE_H

/**
 * @file
 * Event Machine API v2.1
 *
 * This file includes all other needed EM headers
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Major API version number.
 * Step if not backwards compatible
 */
#define EM_API_VERSION_MAJOR 2
/**
 * Minor API version number.
 * Updates and additions
 */
#define EM_API_VERSION_MINOR 2

/** @mainpage
 *
 * @section section_1 General
 * Event Machine (EM) is a framework and an architectural abstraction of an
 * event driven, multicore optimized, processing concept originally developed
 * for the networking data plane. It offers an easy programming concept for
 * scalable and dynamically load balanced multicore applications with a very
 * low overhead run-to-completion principle.
 *
 * Events, queues and execution objects (EO) along with the scheduler and the
 * dispatcher form the main elements of the EM concept. An event is an
 * application specific piece of data (like a message or a network packet)
 * describing work, something to do. All processing in EM must be triggered by
 * an event. Events are sent to asynchronous application specific EM queues.
 * A dispatcher loop is run by a single thread on each core in the EM instance
 * ("core" is used here to refer to a core or one HW thread on multi-threaded
 * cores). The dispatcher on each core interfaces with the scheduler and asks
 * for an event to process. The scheduler then evaluates the state of all the
 * EM queues and gives the highest priority event available to the requesting
 * dispatcher. The dispatcher looks up which EO owns the queue that the event
 * came from and finally calls the EO's registered receive function to deliver
 * the event for processing. When the event has been handled and the EO's
 * receive function returns, it's again time for the dispatcher on that core to
 * request another event from the scheduler and deliver it to the corresponding
 * EO. The aforedescribed scenario happens in parallel on all cores running the
 * EM instance. Events originating from a particular queue might thus be given
 * for processing on any core, decided separately for each event by the
 * scheduler as the dispatcher on a core requests more work - this is per-event
 * dynamic load-balancing. EM contains mechanisms to ensure atomicity and event
 * (re-)ordering.
 *
 * The EM concept has been designed to be highly efficient, operating in a
 * run-to-completion manner on each participating core with neither context
 * switching nor pre-emption slowing down the event processing loops.
 * EM can run on bare metal for best performance or under an operating system
 * with special arrangements (e.g. one thread per core with thread affinity).
 *
 * The concept and the API are intended to allow fairly easy implementations on
 * general purpose or networking oriented multicore packet processing SoCs,
 * which typically also contain accelerators for packet processing needs.
 * Efficient integration with modern HW accelerators has been a major driver of
 * the EM concept.
 *
 * One general principle of the EM API is that the function calls are mostly
 * multicore safe. The application still needs to consider parallel processing
 * data hazards and race conditions unless explicitly documented in the API for
 * the function call in question. For example, one core might ask for a queue
 * context while another core changes it, thus the returned context may be
 * invalid (valid data, but either the old or the new value is returned). Thus
 * modifications of shared state or data should be protected by an atomic
 * context (if load balancing is used) or otherwise synchronized by the
 * application itself. One simple way to achieve atomic processing is to use an
 * atomic queue to serialize the EO's incoming events and perform management
 * operations in the EO's receive function. This serialization limits the
 * throughput of the atomic queue in question to the equivalent throughput of a
 * single core, but since normally EM applications use multiple queues, all
 * cores should get events to process and the total throughput will be relative
 * to the number of cores running the EM instance.
 *
 * EM_64_BIT or EM_32_BIT (needs to be defined in the makefile) defines whether
 * (most of) the types used in the API are 32 or 64 bits wide. NOTE, that this
 * is a major decision, since it may limit value passing between different
 * systems using the defined types directly. Using 64-bits may allow for a more
 * efficient underlying implementation, as e.g. more data can be coded in
 * 64-bit identifiers.
 *
 * @section section_2 Principles
 * - This API attempts to guide towards a portable application architecture,
 * but is not defined for portability by re-compilation. Many things are system
 * specific giving more possibilities for efficient use of HW resources.
 * - EM does not define event content (one exception, see em_alloc()). This is
 * a choice made for performance reasons, since most HW devices use proprietary
 * descriptors. This API enables the usage of those directly.
 * - EM does not define a detailed queue scheduling discipline or an API to set
 *  it up with (or actually anything to configure a system). The priority value
 * in this API is a (mapped) system specific QoS class label only.
 * - In general, EM does not implement a full SW platform or a middleware
 * solution, it implements a subset - a driver level part. For best
 * performance it can be used directly from the applications.
 *
 * @section section_3 Inter-system communication
 * EM does not provide definition on how to communicate with another EM
 * instance or another system transparently (application does not need to know).
 * However this is a typical need and current API does have ways to achieve
 * almost transparent communication between systems ("event chaining"):
 * Since queue identifier is a system specific value, it is easy to code extra
 * information into it in the EM implementation. For instance it could be split
 * into two parts, where lower part is a local queue id or index and higher
 * part, if not zero, points to another system. Then implementation of
 * em_send() can easily detect non-local queue and forward events to the target
 * using any transport mechanism available and once at target instance the
 * lower part is used to map to a local queue. For the application nothing
 * changes. Problem is the lack of shared memory between those systems. Given
 * event can be fully copied, but it may not have any references to sender's
 * local memory. This means it is not fully transparent unless no event
 * contains references to local memory.
 *
 * @section section_4 Files
 * @subsection sub_1 Generic
 * - event_machine.h
 *   - Event Machine main API, application includes only this
 * - event_machine_types.h (included by event_machine.h)
 *   - Event Machine basic types
 * - event_machine_event.h (included by event_machine.h)
 *   - event related functionality
 * - event_machine_eo.h (included by event_machine.h)
 *   - EO related functionality
 * - event_machine_event_group.h (included by event_machine.h)
 *   - event group feature for fork-join type of operations using events
 * - event_machine_atomic_group.h (included by event_machine.h)
 *   - functionality for atomic groups of queues (API 1.1)
 * - event_machine_queue.h (included by event_machine.h)
 *   - queue related functionality
 * - event_machine_queue_group.h (included by event_machine.h)
 *   - queue group related functionality
 * - event_machine_error.h (included by event_machine.h)
 *   - error management related functionality
 * - event_machine_core.h (included by event_machine.h)
 *   - core/thread related functionality
 * - event_machine_scheduler.h (included by event_machine.h)
 *   - scheduling related functionality
 * - event_machine_dispatcher.h (included by event_machine.h)
 *   - dispatching related functionality
 * - event_machine_helper.h
 *   - optional helper routines
 *
 * @subsection sub_2 HW Specific
 * - event_machine_config.h.template
 *   - Event Machine constants and configuration options
 * - event_machine_hw_config.h.template
 *   - HW specific constants and configuration options
 * - event_machine_hw_types.h.template (included by event_machine.h)
 *   - HW specific types
 * - event_machine_init.h.template (included by event_machine.h)
 *   - Event Machine initialization
 * - event_machine_pool.h.template (included by event_machine.h)
 *   - event pool related functionality
 * - event_machine_hw_specific.h.template (included by event_machine.h)
 *   - HW specific functions and macros
 *
 * @example hello.c
 * @example dispatcher_callback.c
 * @example error.c
 * @example event_group.c
 * @example event_group_abort.c
 * @example event_group_assign_end.c
 * @example event_group_chaining.c
 * @example fractal.c
 * @example ordered.c
 * @example queue_types_ag.c
 * @example queue_types_local.c
 * @example queue_group.c
 * add-ons:
 * @example timer_hello.c
 * @example timer_test.c
 * performance:
 * @example atomic_processing_end.c
 * @example pairs.c
 * @example queue_groups.c
 * @example queues.c
 * @example queues_unscheduled.c
 * @example queues_local.c
 * @example send_multi.c
 */

/* EM config & types */
#include <event_machine/platform/event_machine_config.h>
#include <event_machine/api/event_machine_types.h>

/* HW specific EM config & types */
#include <event_machine/platform/event_machine_hw_config.h>
#include <event_machine/platform/event_machine_hw_types.h>

/* EM error management */
#include <event_machine/api/event_machine_error.h>
/* EM Execution Object (EO) related functions */
#include <event_machine/api/event_machine_eo.h>
/* EM Queue functions */
#include <event_machine/api/event_machine_queue.h>
/* EM Queue Group functions */
#include <event_machine/api/event_machine_queue_group.h>
/* EM Core functions*/
#include <event_machine/api/event_machine_core.h>
/* EM Event functions */
#include <event_machine/api/event_machine_event.h>
/* EM Atomic Group functions */
#include <event_machine/api/event_machine_atomic_group.h>
/* EM Event Group functions */
#include <event_machine/api/event_machine_event_group.h>
/* EM Scheduler functions */
#include <event_machine/api/event_machine_scheduler.h>
/* EM Dispatcher functions */
#include <event_machine/api/event_machine_dispatcher.h>

/* EM initialization and termination */
#include <event_machine/platform/event_machine_init.h>
/* EM Event Pool functions */
#include <event_machine/platform/event_machine_pool.h>
/* EM API hooks */
#include <event_machine/platform/event_machine_hooks.h>
/* Other HW/Platform specific functions */
#include <event_machine/platform/event_machine_hw_specific.h>

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_H */
