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

#ifndef EVENT_MACHINE_SCHEDULER_H_
#define EVENT_MACHINE_SCHEDULER_H_

/**
 * @file
 * @defgroup em_scheduler Scheduler
 *  Event Machine scheduling related services (new to API 1.2)
 * @{
 *
 * Most of the scheduling is a system dependent implementation but the common
 * services here can be used for performance tuning etc.
 *
 * Do not include this from the application, event_machine.h will
 * do it for you.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A hint to release an atomic processing context.
 *
 * This function can be used to release the atomic context before returning from
 * the EO receive function.
 * When an event has been received from an atomic queue, the scheduler is
 * allowed to schedule another event from the same atomic queue to another,
 * or same, core after the call. This increases parallelism and may improve
 * performance - however, the exclusive processing and ordering might be lost.
 * Note, however, that this is a hint only, the scheduler is still allowed to
 * keep the atomic context until scheduling the next event.
 *
 * Can only be called from within the EO receive function.
 *
 * The call is ignored if the current event was not received from an atomic
 * queue.
 *
 * Pseudo-code example:
 * @code
 *  receive_func(void* eo_ctx, em_event_t event, em_event_type_t type,
 *               em_queue_t queue, void* q_ctx)
 *  {
 *      if(is_my_atomic_queue(q_ctx))
 *      {
 *          // this needs to be done atomically:
 *          update_sequence_number(event);
 *
 *          em_atomic_processing_end();
 *          // do other processing (potentially) in parallel:
 *          do_long_parallel_work();
 *      }
 *   }
 * @endcode
 *
 * @see em_receive_func_t(), event_machine_queue.h
 */
void em_atomic_processing_end(void);

/**
 * A hint to allow release of the ordered processing context.
 *
 * This function can be used to tell the scheduler that all events to be sent
 * under the current ordering context are already sent. Events sent after this
 * are no longer required to be kept in order. This may increase system
 * performance. It permits early release of the ordering context, but an
 * EM implementation is still allowed to keep it until scheduling the next
 * incoming event.
 *
 * Can only be called from within the EO receive function. The call is ignored,
 * if the current event was not received from an ordered queue.
 *
 * The ordering context cannot be resumed after it has been released.
 *
 * @see em_receive_func_t(), event_machine_queue.h
 */
void em_ordered_processing_end(void);

/**
 * A hint to start scheduling for this core
 *
 * This is a performance optimization hint with no functional effect. A hint is
 * given to the scheduler to start scheduling the next event for the calling
 * core as it is about to end processing of the current event. This can be used
 * to reduce the latency of scheduling.
 * Depending on the actual scheduler implementation, this may be a no-operation.
 */
void em_preschedule(void);

/**
 * Return the currently active scheduling context type
 *
 * Returns the current scheduling context type (none, ordered, atomic) and
 * optionally the input queue that determines the context. Note, that this is
 * not the same as queue type since the context could have been released.
 *
 * This function is mainly for handling local queues that inherit the scheduling
 * context that was active for the sending EO. The scheduling context can be
 * unpredictable unless the processing chain is carefully crafted.
 * This function will return the active scheduling context type and queue of the
 * last event from the scheduler (i.e. the input queue of the EO that sent the
 * event to a local queue).
 *
 * @param[out] queue  if not NULL, set to the queue that determines the current
 *                    sched context
 *
 * @return current context type
 *
 * @see em_queue_create(), em_sched_context_type_t
 */
em_sched_context_type_t
em_sched_context_type_current(em_queue_t *queue);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_SCHEDULER_H_ */
