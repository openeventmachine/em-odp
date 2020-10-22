/*
 *   Copyright (c) 2020, Nokia Solutions and Networks
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
  * EM event chaining support
  */

#ifndef EM_CHAINING_H_
#define EM_CHAINING_H_

#ifdef __cplusplus
extern "C" {
#endif

#pragma GCC visibility push(default)
/**
 * This function is declared as a weak symbol, indicating that the user should
 * override it during linking with another implementation if event chaining is
 * used.
 */
__attribute__((weak))
em_status_t event_send_device(em_event_t event, em_queue_t queue);
/**
 * This function is declared as a weak symbol, indicating that the user should
 * override it during linking with another implementation if event chaining is
 * used.
 */
__attribute__((weak))
int event_send_device_multi(const em_event_t events[], int num,
			    em_queue_t queue);
#pragma GCC visibility pop

/**
 * Initialize event chaining during start-up
 */
em_status_t
chaining_init(event_chaining_t *const event_chaining);

/**
 * Terminate event chaining during shut-down
 */
em_status_t
chaining_term(event_chaining_t *const event_chaining);

/**
 * Send an event to out of EM (e.g. to another device) via event-chaining and a
 * user-provided function 'event_send_device()'
 * @see event_send_device()
 */
static inline em_status_t
send_chaining(em_event_t event, event_hdr_t *const ev_hdr,
	      em_queue_t chaining_queue)
{
	const unsigned int num_out = em_shm->event_chaining.num_output_queues;
	const em_sched_context_type_t sched_ctx_type =
		em_locm.current.sched_context_type;

	if (num_out == 0 || sched_ctx_type != EM_SCHED_CONTEXT_TYPE_ORDERED)
		return event_send_device(event, chaining_queue);

	/* store destination event-chaining queue */
	ev_hdr->queue = chaining_queue;

	/* always use the same output queue for each chaining queue */
	const internal_queue_t iq = {.queue = chaining_queue};
	em_queue_t output_queue;
	queue_elem_t *output_q_elem;
	uint32_t idx;

	idx = ((uint32_t)iq.device_id + (uint32_t)iq.queue_id) % num_out;
	output_queue = em_shm->event_chaining.output_queues[idx];
	output_q_elem = queue_elem_get(output_queue);

	return send_output(event, output_q_elem);
}

/**
 * Send 'num' events out of EM (e.g. to another device) via event-chaining and a
 * user-provided function 'event_send_device_multi()'
 * @see event_send_device_multi()
 */
static inline int
send_chaining_multi(const em_event_t events[], event_hdr_t *const ev_hdrs[],
		    const int num, em_queue_t chaining_queue)
{
	const unsigned int num_out = em_shm->event_chaining.num_output_queues;
	const em_sched_context_type_t sched_ctx_type =
		em_locm.current.sched_context_type;

	if (num_out == 0 || sched_ctx_type != EM_SCHED_CONTEXT_TYPE_ORDERED)
		return event_send_device_multi(events, num, chaining_queue);

	/* store destination event chaining queue */
	for (int i = 0; i < num; i++)
		ev_hdrs[i]->queue = chaining_queue;

	/* always use the same output queue for each chaining queue */
	const internal_queue_t iq = {.queue = chaining_queue};
	em_queue_t output_queue;
	queue_elem_t *output_q_elem;
	uint32_t idx;

	idx = ((uint32_t)iq.device_id + (uint32_t)iq.queue_id) % num_out;
	output_queue = em_shm->event_chaining.output_queues[idx];
	output_q_elem = queue_elem_get(output_queue);

	return send_output_multi(events, num, output_q_elem);
}

#ifdef __cplusplus
}
#endif

#endif /* EM_CHAINING_H_ */
