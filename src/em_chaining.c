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

#include "em_include.h"

/* em_output_func_t for event-chaining output*/
int chaining_output(const em_event_t events[], const unsigned int num,
		    const em_queue_t output_queue, void *output_fn_args);

/**
 * This function is declared as a weak symbol in em_chaining.h, meaning that the
 * user can override it during linking with another implementation.
 */
em_status_t
event_send_device(em_event_t event, em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	(void)event;
	return INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED,
			      EM_ESCOPE_EVENT_SEND_DEVICE,
			      "No %s() function given!\t"
			      "device:0x%" PRIx16 " Q-id:0x%" PRIx16 "\n",
			      __func__, iq.device_id, iq.queue_id);
}

/**
 * This function is declared as a weak symbol in em_chaining.h, meaning that the
 * user can override it during linking with another implementation.
 */
int
event_send_device_multi(const em_event_t events[], int num, em_queue_t queue)
{
	internal_queue_t iq = {.queue = queue};

	(void)events;
	(void)num;
	INTERNAL_ERROR(EM_ERR_NOT_IMPLEMENTED,
		       EM_ESCOPE_EVENT_SEND_DEVICE_MULTI,
		       "No %s() function given!\t"
		       "device:0x%" PRIx16 " Q-id:0x%" PRIx16 "\n",
		       __func__, iq.device_id, iq.queue_id);
	return 0;
}

static int
read_config_file(void)
{
	const char *conf_str;
	int val = 0;
	int ret;

	EM_PRINT("EM Event-Chaining config:\n");

	/*
	 * Option: event_chaining.num_order_queues
	 */
	conf_str = "event_chaining.num_order_queues";
	ret = em_libconfig_lookup_int(&em_shm->libconfig, conf_str, &val);
	if (unlikely(!ret)) {
		EM_LOG(EM_LOG_ERR, "Config option '%s' not found.\n", conf_str);
		return -1;
	}
	if (val < 0 || val > MAX_CHAINING_OUTPUT_QUEUES) {
		EM_LOG(EM_LOG_ERR, "Bad config value '%s = %d' (max: %d)\n",
		       conf_str, val, MAX_CHAINING_OUTPUT_QUEUES);
		return -1;
	}
	/* store & print the value */
	em_shm->opt.event_chaining.num_order_queues = val;
	EM_PRINT("  %s: %d (max: %d)\n", conf_str, val,
		 MAX_CHAINING_OUTPUT_QUEUES);

	return 0;
}

em_status_t
chaining_init(event_chaining_t *event_chaining)
{
	em_queue_conf_t queue_conf;
	em_output_queue_conf_t output_conf;
	em_queue_t output_queue;
	unsigned int i;

	if (read_config_file())
		return EM_ERR_LIB_FAILED;

	event_chaining->num_output_queues = 0;
	for (i = 0; i < MAX_CHAINING_OUTPUT_QUEUES; i++)
		event_chaining->output_queues[i] = EM_QUEUE_UNDEF;

	memset(&queue_conf, 0, sizeof(queue_conf));
	memset(&output_conf, 0, sizeof(output_conf));

	queue_conf.flags = EM_QUEUE_FLAG_DEFAULT;
	queue_conf.min_events = 0; /* system default */
	queue_conf.conf_len = sizeof(output_conf);
	queue_conf.conf = &output_conf;
	/* Set output-queue callback function, no args needed */
	output_conf.output_fn = chaining_output;
	output_conf.output_fn_args = NULL;
	output_conf.args_len = 0;

	const unsigned int num = em_shm->opt.event_chaining.num_order_queues;
	unsigned char idx = 0;

	for (i = 0; i < num; i++) {
		char name[EM_QUEUE_NAME_LEN];

		snprintf(name, sizeof(name), "Event-Chaining-Output-%02u", idx);
		idx++;
		name[sizeof(name) - 1] = '\0';

		output_queue = em_queue_create(name, EM_QUEUE_TYPE_OUTPUT,
					       EM_QUEUE_PRIO_UNDEF,
					       EM_QUEUE_GROUP_UNDEF,
					       &queue_conf);
		if (unlikely(output_queue == EM_QUEUE_UNDEF))
			return EM_ERR_ALLOC_FAILED;

		event_chaining->num_output_queues++;
		event_chaining->output_queues[i] = output_queue;
	}

	return EM_OK;
}

em_status_t
chaining_term(const event_chaining_t *event_chaining)
{
	const unsigned int num = event_chaining->num_output_queues;
	em_queue_t output_queue;
	em_status_t stat;

	for (unsigned int i = 0; i < num; i++) {
		output_queue = event_chaining->output_queues[i];
		/* delete the output queues associated with event chaining */
		stat = em_queue_delete(output_queue);
		if (unlikely(stat != EM_OK))
			return stat;
	}

	return EM_OK;
}

/**
 * Output-queue callback function of type 'em_output_func_t' for Event-Chaining.
 */
int
chaining_output(const em_event_t events[], const unsigned int num,
		const em_queue_t output_queue, void *output_fn_args)
{
	em_queue_t chaining_queue;

	(void)output_queue;
	(void)output_fn_args;

	if (unlikely(num <= 0))
		return 0;

	if (num == 1) {
		const event_hdr_t *ev_hdr = event_to_hdr(events[0]);
		em_status_t stat;

		chaining_queue = ev_hdr->queue;
		stat = event_send_device(events[0], chaining_queue);
		if (unlikely(stat != EM_OK))
			return 0;
		return 1;
	}

	/*
	 * num > 1:
	 * Dispatch events in batches. Each batch contains events targeted for
	 * the same chaining queue.
	 */
	event_hdr_t *ev_hdrs[num];
	unsigned int idx = 0; /* index into 'events[]' and 'ev_hdrs[]' */

	event_to_hdr_multi(events, ev_hdrs, num);

	do {
		chaining_queue = ev_hdrs[idx]->queue;
		int batch_cnt = 1;
		int ret;

		for (unsigned int i = idx + 1; i < num &&
		     ev_hdrs[i]->queue == chaining_queue; i++) {
			batch_cnt++;
		}

		ret = event_send_device_multi(&events[idx], batch_cnt,
					      chaining_queue);
		if (unlikely(ret != batch_cnt)) {
			if (ret < 0)
				return idx;
			return idx + ret;
		}
		idx += batch_cnt;
	} while (idx < num);

	return num;
}
