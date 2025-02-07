/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2015-2024, Nokia Solutions and Networks
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

#pragma GCC visibility push(default)

/**
 * @file
 * Event Machine API
 *
 * This file includes all other needed EM headers
 */

#ifdef __cplusplus
extern "C" {
#endif

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
 * EM_64_BIT or EM_32_BIT (needs to be defined by the build) defines whether
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
 * EM does not define how to communicate with another EM instance or another
 * system transparently. However, this is a typical need and the current API
 * does have ways to achieve almost transparent communication between systems
 * ("event chaining"):
 * Since the queue identifier is a system specific value, it is easy to encode
 * extra information into it in the EM implementation. For instance it could be
 * split into two parts, where the lower part is a local queue id or index and
 * the higher part, if not zero, points to another system. The implementation
 * of em_send() can detect a non-local queue and forward events to the target
 * using any transport mechanism available and once at the target instance the
 * lower part is used to map to a local queue. For the application nothing
 * changes. The problem is the lack of shared memory between those systems.
 * The given event can be fully copied, but it should not have any references to
 * sender's local memory. Thus it is not fully transparent if the event contains
 * references to local memory (e.g. pointers).
 *
 * @section section_4 Files
 * @subsection sub_1 Generic
 * - event_machine.h
 *   - Event Machine API
 *     The application should include this file only.
 *
 * Files included by event_machine.h:
 * - event_machine_version.h
 *   - Event Machine version defines, macros and APIs
 * - event_machine_deprecated.h
 *   - EM API deprecation defines & macros
 * - event_machine_types.h
 *   - Event Machine basic types
 * - event_machine_event.h
 *   - event related functionality
 * - event_machine_packet.h
 *   - packet event related functionality
 * - event_machine_eo.h
 *   - EO related functionality
 * - event_machine_event_group.h
 *   - event group feature for fork-join type of operations using events
 * - event_machine_atomic_group.h
 *   - functionality for atomic groups of queues (API 1.1)
 * - event_machine_queue.h
 *   - queue related functionality
 * - event_machine_queue_group.h
 *   - queue group related functionality
 * - event_machine_error.h
 *   - error management related functionality
 * - event_machine_core.h
 *   - core/thread related functionality
 * - event_machine_scheduler.h
 *   - scheduling related functionality
 * - event_machine_dispatcher.h
 *   - dispatching related functionality
 * - event_machine_timer.h
 *   - timer APIs
 *
 * @subsection sub_2 Platform Specific
 * (also included by event_machine.h)
 * - event_machine_config.h
 *   - Event Machine constants and configuration options
 * - event_machine_hw_config.h
 *   - HW specific constants and configuration options
 * - event_machine_hw_specific.h
 *   - HW specific functions and macros
 * - event_machine_hw_types.h
 *   - HW specific types
 * - event_machine_hooks.h
 *   - API-hooks and idle-hooks
 * - event_machine_init.h
 *   - Event Machine initialization
 * - event_machine_pool.h
 *   - event pool related functionality
 * - event_machine_timer_hw_specific.h
 *   - Platform specific timer definitions
 *
 * @subsection sub_3 Helper
 * These files must be separately included by the application on a need basis.
 * - event_machine_helper.h
 *   - optional helper routines
 * - event_machine_debug.h
 *   - optional debug helpers (only for debug use)
 *
 * @subsection sub_4 Extensions
 * These files must be separately included by the application on a need basis.
 * - event_machine_odp_ext.h
 *   - EM <-> ODP conversion functions and ODP related helpers
 *
 * @example hello.c
 * @example api_hooks.c
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
 * @example timer_hello.c
 * performance:
 * @example atomic_processing_end.c
 * @example loop.c
 * @example loop_multircv.c
 * @example loop_refs.c
 * @example loop_vectors.c
 * @example loop_united.c
 * @example pairs.c
 * @example pool_perf.c
 * @example queue_groups.c
 * @example queues.c
 * @example queues_local.c
 * @example queues_output.c
 * @example queues_unscheduled.c
 * @example scheduling_latency.c
 * @example send_multi.c
 * @example timer_test.c
 * @example timer_test_periodic.c
 * @example timer_test_ring.c
 * bench:
 * @example bench_event.c
 * @example bench_pool.c
 */

/* EM deprecated */
#include <event_machine/api/event_machine_deprecated.h>

/* EM version */
#include <event_machine/api/event_machine_version.h>

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
/* EM Packet Event functions */
#include <event_machine/api/event_machine_packet.h>
/* EM Atomic Group functions */
#include <event_machine/api/event_machine_atomic_group.h>
/* EM Event Group functions */
#include <event_machine/api/event_machine_event_group.h>
/* EM Scheduler functions */
#include <event_machine/api/event_machine_scheduler.h>
/* EM Dispatcher functions */
#include <event_machine/api/event_machine_dispatcher.h>

/* EM Event Pool functions */
#include <event_machine/platform/event_machine_pool.h>
/* EM API hooks */
#include <event_machine/platform/event_machine_hooks.h>
/* EM initialization and termination */
#include <event_machine/platform/event_machine_init.h>
/* Other HW/Platform specific functions */
#include <event_machine/platform/event_machine_hw_specific.h>
/* EM Timer HW/Platform specific */
#include <event_machine/platform/event_machine_timer_hw_specific.h>
/* EM Timer */
#include <event_machine/api/event_machine_timer.h>

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop
#endif /* EVENT_MACHINE_H */
