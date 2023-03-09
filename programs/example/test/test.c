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
  * EM-ODP test setup
  */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <event_machine.h>
#include <event_machine/helper/event_machine_helper.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"

#define APPL_ESCOPE_TEST (10)

#define PRINT_EVCOUNT (1 * 1000 * 1000)

#define TEST_QUEUE_GROUP_NAME "test-qgroup"

/**
 * The number of test queues created and used by the test EO.
 */
#define NBR_TEST_QUEUES 3

/**
 * Test queue context data
 */
typedef union test_queue_ctx_t {
	struct {
		/** Input queue (this queue) */
		em_queue_t queue;
		/** Queue statistics: events dispatched from queue */
		env_atomic64_t event_count;
	};
	uint8_t u8[ENV_CACHE_LINE_SIZE];
} test_queue_ctx_t ENV_CACHE_LINE_ALIGNED;

/**
 * Core specific stats
 */
typedef union test_core_stat_t {
	struct {
		/** The number of events dispatched on a core */
		uint64_t event_count;
	};
	uint8_t u8[ENV_CACHE_LINE_SIZE];
} test_core_stat_t ENV_CACHE_LINE_ALIGNED;

/**
 * Test EO context data
 */
typedef struct test_eo_ctx_t {
	em_queue_t notif_queue;
	em_queue_t queues[NBR_TEST_QUEUES];
	test_queue_ctx_t queue_ctx[NBR_TEST_QUEUES] ENV_CACHE_LINE_ALIGNED;
	test_core_stat_t core_stat[MAX_THREADS] ENV_CACHE_LINE_ALIGNED;
} test_eo_ctx_t;

/**
 * Test shared data, shared between all worker threads/processes.
 */
typedef struct test_shm_t {
	em_eo_t test_eo;
	test_eo_ctx_t test_eo_ctx;
} test_shm_t;

static ENV_LOCAL test_shm_t *test_shm;

/*
 * Test event content: test event = em_event_pointer(event);
 */
typedef struct test_event_t {
	int event_nbr;
} test_event_t;

static em_status_t
test_eo_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
test_eo_start_local(void *eo_ctx, em_eo_t eo);

static em_status_t
test_eo_stop(void *eo_ctx, em_eo_t eo);
/* @TBD: local stop not supported yet
 * static em_status_t
 * test_eo_stop_local(void *eo_ctx, em_eo_t eo);
 */
static void
setup_test_events(em_queue_t queues[], const int nbr_queues);

static void
test_eo_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		em_queue_t queue, void *q_ctx);

static em_status_t
test_eo_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		      va_list args);

/**
 * Main function
 *
 * Call cm_setup() to perform test & EM setup common for all the
 * test applications.
 *
 * cm_setup() will call test_init() and test_start() and launch
 * the EM dispatch loop on every EM-core.
 */
int main(int argc, char *argv[])
{
	return cm_setup(argc, argv);
}

/**
 * Init of the test application.
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void
test_init(void)
{
	int core = em_core_id();
	char name[] = "TestSharedMem";

	if (core == 0)
		test_shm = env_shared_reserve(name, sizeof(test_shm_t));
	else
		test_shm = env_shared_lookup(name);

	if (test_shm == NULL)
		APPL_EXIT_FAILURE("%s():EM-core:%d", __func__, em_core_id());
	else if (core == 0)
		memset(test_shm, 0, sizeof(test_shm_t));
}

void
test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	em_status_t stat, stat_eo_start = EM_ERROR;
	test_eo_ctx_t *const test_eo_ctx = &test_shm->test_eo_ctx;
	em_notif_t notif_tbl[1];
	em_queue_prio_t queue_prio;

	printf("\n"
	       "**********************************************************\n"
	       "EM APPLICATION: '%s' initializing:\n"
	       "  %s: %s() - EM-core:%i\n"
	       "  Application running on %d EM-cores (procs:%d, threads:%d).\n"
	       "**********************************************************\n"
	       "\n",
	       appl_conf->name,
	       NO_PATH(__FILE__), __func__,
	       em_core_id(),
	       em_core_count(),
	       appl_conf->num_procs,
	       appl_conf->num_threads);

	/* Create 3 test queues, one per scheduled queue type: */
	assert(NBR_TEST_QUEUES >= 3);

	eo = em_eo_create("test-eo", test_eo_start, test_eo_start_local,
			  test_eo_stop, NULL /*stop_local*/,
			  test_eo_receive, test_eo_ctx);
	if (eo == EM_EO_UNDEF)
		APPL_EXIT_FAILURE("test-eo creation failed!");

	/* Store the EO in shared memory */
	test_shm->test_eo = eo;

	em_eo_register_error_handler(eo, test_eo_error_handler);

	memset(notif_tbl, 0, sizeof(notif_tbl));
	notif_tbl[0].event = em_alloc(sizeof(test_event_t), EM_EVENT_TYPE_SW,
				      EM_POOL_DEFAULT);
	notif_tbl[0].queue = em_queue_create("test-q-notif",
					     EM_QUEUE_TYPE_ATOMIC,
					     EM_QUEUE_PRIO_NORMAL,
					     EM_QUEUE_GROUP_DEFAULT, NULL);
	notif_tbl[0].egroup = EM_EVENT_GROUP_UNDEF;

	if (notif_tbl[0].event == EM_EVENT_UNDEF ||
	    notif_tbl[0].queue == EM_QUEUE_UNDEF)
		APPL_EXIT_FAILURE("test-eo start notif setup failed!");

	queue_prio = em_queue_get_priority(notif_tbl[0].queue);
	if (queue_prio != EM_QUEUE_PRIO_NORMAL)
		APPL_EXIT_FAILURE("notif queue priority comparison failed!");

	stat = em_eo_add_queue_sync(eo, notif_tbl[0].queue);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("test-eo add notif queue failed!");

	test_eo_ctx->notif_queue = notif_tbl[0].queue;

	stat = em_eo_start(eo, &stat_eo_start, NULL, 1, notif_tbl);
	if (stat != EM_OK || stat_eo_start != EM_OK)
		APPL_EXIT_FAILURE("test-eo start failed!");
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	const em_eo_t eo = test_shm->test_eo;
	em_status_t stat;

	(void)appl_conf;

	printf("%s() on EM-core %d\n", __func__, core);

	stat = em_eo_stop_sync(eo);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("test-eo stop failed!");
	stat = em_eo_delete(eo);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("test-eo delete failed!");
}

void
test_term(void)
{
	int core = em_core_id();

	printf("%s() on EM-core %d\n", __func__, core);

	if (core == 0)
		env_shared_free(test_shm);
}

static em_status_t
test_eo_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_status_t stat;
	test_eo_ctx_t *const test_eo_ctx = eo_ctx;
	int i;
	em_queue_group_t test_qgrp;
	char test_qgrp_name[sizeof(TEST_QUEUE_GROUP_NAME)];

	(void)eo;
	(void)conf;

	/* For queue group core mask tests: */
	em_core_mask_t mask;
	char mstr[EM_CORE_MASK_STRLEN];
	const int mbits_len = (EM_MAX_CORES + 63) / 64;
	int len;
	uint64_t mbits[mbits_len];
	em_core_mask_t phys_mask;

	/* Queue group core mask tests: */
	stat = em_queue_group_get_mask(EM_QUEUE_GROUP_DEFAULT, &mask);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("em_queue_group_get_mask():%" PRI_STAT "", stat);
	em_core_mask_tostr(mstr, sizeof(mstr), &mask);
	em_core_mask_get_bits(mbits, mbits_len, &mask);
	printf("EM_QUEUE_GROUP_DEFAULT:%s\n", mstr);
	printf("EM_QUEUE_GROUP_DEFAULT bits:");
	for (i = mbits_len - 1; i >= 0; i--)
		printf(" mbits[%d]:0x%" PRIx64 "", i, mbits[i]);
	printf("\n");

	em_core_mask_get_physical(&phys_mask, &mask);
	len = em_core_mask_get_bits(mbits, mbits_len, &phys_mask);
	if (len <= 0)
		APPL_EXIT_FAILURE("em_core_mask_get_bits():%d", len);
	printf("physical core mask bits:");
	for (i = len - 1; i >= 0; i--)
		printf(" mbits[%d]:0x%" PRIx64 "", i, mbits[i]);
	printf("\n");

	for (i = 0; i < mbits_len; i++)
		mbits[i] = 0xabbaacdcdeadbeef;
	em_core_mask_set_bits(mbits, mbits_len, &mask);
	em_core_mask_tostr(mstr, sizeof(mstr), &mask);
	len = em_core_mask_get_bits(mbits, mbits_len, &mask);
	if (len <= 0)
		APPL_EXIT_FAILURE("em_core_mask_get_bits():%d", len);
	printf("core mask test:%s\n", mstr);
	printf("core mask test bits:");
	for (i = len - 1; i >= 0; i--)
		printf(" mbits[%d]:0x%" PRIx64 "", i, mbits[i]);
	printf("\n\n");
	/* end queue group core mask tests */

	/* Create an atomic queue */
	test_eo_ctx->queues[0] = em_queue_create("test-q-atomic",
						 EM_QUEUE_TYPE_ATOMIC,
						 EM_QUEUE_PRIO_NORMAL,
						 EM_QUEUE_GROUP_DEFAULT, NULL);
	if (test_eo_ctx->queues[0] == EM_QUEUE_UNDEF)
		APPL_EXIT_FAILURE("test-q-atomic creation failed!");

	/* Create a parallel queue */
	test_eo_ctx->queues[1] = em_queue_create("test-q-parallel",
						 EM_QUEUE_TYPE_PARALLEL,
						 EM_QUEUE_PRIO_NORMAL,
						 EM_QUEUE_GROUP_DEFAULT, NULL);
	if (test_eo_ctx->queues[1] == EM_QUEUE_UNDEF)
		APPL_EXIT_FAILURE("test-q-parallel creation failed!");

	/* Create a parallel-ordered queue */
	test_eo_ctx->queues[2] = em_queue_create("test-q-parord",
						 EM_QUEUE_TYPE_PARALLEL_ORDERED,
						 EM_QUEUE_PRIO_NORMAL,
						 EM_QUEUE_GROUP_DEFAULT, NULL);
	if (test_eo_ctx->queues[2] == EM_QUEUE_UNDEF)
		APPL_EXIT_FAILURE("test-q-parord creation failed!");

	printf("%s(): Q:%" PRI_QUEUE ", Q:%" PRI_QUEUE ", Q:%" PRI_QUEUE "\n",
	       __func__, test_eo_ctx->queues[0], test_eo_ctx->queues[1],
	       test_eo_ctx->queues[2]);

	stat  = em_queue_set_context(test_eo_ctx->queues[0],
				     &test_eo_ctx->queue_ctx[0]);
	stat |= em_queue_set_context(test_eo_ctx->queues[1],
				     &test_eo_ctx->queue_ctx[1]);
	stat |= em_queue_set_context(test_eo_ctx->queues[2],
				     &test_eo_ctx->queue_ctx[2]);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Queue context set failed!");

	/* Initialize queue context data */
	for (i = 0; i < NBR_TEST_QUEUES; i++) {
		test_queue_ctx_t *const test_queue_ctx =
			em_queue_get_context(test_eo_ctx->queues[i]);

		if (test_queue_ctx == NULL)
			APPL_EXIT_FAILURE("Queue context get failed!");
		/* Store the queue hdl into the queue context */
		test_queue_ctx->queue = test_eo_ctx->queues[i];
		/* Initialize the queue specific event counter */
		env_atomic64_init(&test_queue_ctx->event_count);
	}

	stat  = em_eo_add_queue_sync(eo, test_eo_ctx->queues[0]);
	stat |= em_eo_add_queue_sync(eo, test_eo_ctx->queues[1]);
	stat |= em_eo_add_queue_sync(eo, test_eo_ctx->queues[2]);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO-add-queue failed!");

	em_core_mask_zero(&mask);

	test_qgrp = em_queue_group_create(TEST_QUEUE_GROUP_NAME, &mask, 0,
					  NULL);
	if (test_qgrp == EM_QUEUE_GROUP_UNDEF)
		APPL_EXIT_FAILURE("Test queue group creation failed!");

	size_t sz = em_queue_group_get_name(test_qgrp, test_qgrp_name,
					    sizeof(TEST_QUEUE_GROUP_NAME));
	if (sz == 0)
		APPL_EXIT_FAILURE("em_queue_group_get_name():%zu", sz);
	if (strncmp(test_qgrp_name, TEST_QUEUE_GROUP_NAME,
		    sizeof(TEST_QUEUE_GROUP_NAME)) != 0)
		APPL_EXIT_FAILURE("Test queue group get name failed!");

	stat = em_queue_group_delete(test_qgrp, 0, NULL);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Test queue group delete failed!");

	em_core_mask_zero(&mask);
	em_core_mask_set_count(1, &mask);

	test_qgrp = em_queue_group_create_sync(TEST_QUEUE_GROUP_NAME, &mask);
	if (test_qgrp == EM_QUEUE_GROUP_UNDEF)
		APPL_EXIT_FAILURE("Test queue group creation failed!");

	stat = em_queue_group_delete(test_qgrp, 0, NULL);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Test queue group delete failed!");

	return EM_OK;
}

static em_status_t
test_eo_start_local(void *eo_ctx, em_eo_t eo)
{
	(void)eo_ctx;
	(void)eo;

	printf("%s(EO:%" PRI_EO ") on EM-core%d\n",
	       __func__, eo, em_core_id());

	return EM_OK;
}

static em_status_t
test_eo_stop(void *eo_ctx, em_eo_t eo)
{
	test_eo_ctx_t *const test_eo_ctx = &test_shm->test_eo_ctx;
	em_status_t stat;
	int i;

	(void)eo_ctx;
	(void)eo;

	/* call to em_eo_stop() earlier has already disabled all queues */

	for (i = 0; i < NBR_TEST_QUEUES; i++) {
		test_queue_ctx_t *const queue_ctx = &test_eo_ctx->queue_ctx[i];

		stat = em_eo_remove_queue_sync(eo, queue_ctx->queue);
		if (stat != EM_OK)
			APPL_EXIT_FAILURE("removing queue from eo failed!");
		stat = em_queue_delete(queue_ctx->queue);
		if (stat != EM_OK)
			APPL_EXIT_FAILURE("test-queue deletion failed!");
	}

	return EM_OK;
}

/* @TBD: local stop not supported yet!
 * static em_status_t
 * test_eo_stop_local(void *eo_ctx, em_eo_t eo)
 * {
 *	(void)eo_ctx;
 *	(void)eo;
 *	return EM_OK;
 * }
 */

static void
setup_test_events(em_queue_t queues[], const int nbr_queues)
{
	int i, j;
	em_status_t stat;

	/* Send test events to the test queues */
	for (i = 0; i < nbr_queues; i++) {
		for (j = 0; j < nbr_queues; j++) {
			em_event_t event;
			test_event_t *test_event;
			const uint32_t event_size = sizeof(test_event_t);

			event = em_alloc(event_size, EM_EVENT_TYPE_SW,
					 EM_POOL_DEFAULT);
			if (event == EM_EVENT_UNDEF)
				APPL_EXIT_FAILURE("event alloc failed!");

			if (event_size != em_event_get_size(event))
				APPL_EXIT_FAILURE("event alloc size error!");

			/* Print event size info for the first alloc */
			if (i == 0 && j == 0)
				printf("%s(): size:em_alloc(%u)=actual:%u\n",
				       __func__, event_size,
				       em_event_get_size(event));

			test_event = em_event_pointer(event);
			test_event->event_nbr = j;

			stat = em_send(event, queues[i]);
			if (stat != EM_OK)
				APPL_EXIT_FAILURE("event send failed!");
		}
	}
}

static void
test_eo_receive(void *eo_ctx, em_event_t event, em_event_type_t type,
		em_queue_t queue, void *q_ctx)
{
	const int core_id = em_core_id();
	test_eo_ctx_t *const test_eo_ctx = eo_ctx;
	test_event_t *const test_event = em_event_pointer(event);
	test_queue_ctx_t *const test_queue_ctx = (test_queue_ctx_t *)q_ctx;
	em_queue_t queue_out;
	em_status_t stat;
	uint64_t core_evcnt;
	uint64_t queue_evcnt;
	int idx;

	(void)type;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (unlikely(queue == test_eo_ctx->notif_queue)) {
		printf("%s(): EO start-local notif, cores ready: ", __func__);
		setup_test_events(test_eo_ctx->queues, NBR_TEST_QUEUES);
		em_free(event);
		return;
	}

	queue_evcnt = env_atomic64_add_return(&test_queue_ctx->event_count, 1);
	core_evcnt = ++test_eo_ctx->core_stat[core_id].event_count;

	idx = test_event->event_nbr % NBR_TEST_QUEUES;
	test_event->event_nbr += 1;
	queue_out = test_eo_ctx->queues[idx];

	if (queue_evcnt % PRINT_EVCOUNT == 1) {
		em_queue_type_t queue_type = em_queue_get_type(queue);
		const char *qtype_name;
		char qname[EM_QUEUE_NAME_LEN];
		size_t len;

		len = em_queue_get_name(queue, qname, sizeof(qname));
		if (len == 0) /* all test queues have names */
			APPL_EXIT_FAILURE("queue name error!");

		switch (queue_type) {
		case EM_QUEUE_TYPE_ATOMIC:
			qtype_name = "type:atomic  ";
			break;
		case EM_QUEUE_TYPE_PARALLEL:
			qtype_name = "type:parallel";
			break;
		case EM_QUEUE_TYPE_PARALLEL_ORDERED:
			qtype_name = "type:ordered ";
			break;
		default:
			qtype_name = "type:undef   ";
			break;
		}

		printf("%s:%" PRI_QUEUE "\t%s %10" PRIu64 " events\t"
		       "|  Core%02d:%10" PRIu64 " events\t"
		       "|  this event scheduled:%10d times\n",
		       qname, queue, qtype_name, queue_evcnt,
		       core_id, core_evcnt, test_event->event_nbr);
	}

	stat = em_send(event, queue_out);
	if (unlikely(stat != EM_OK)) {
		em_free(event);
		if (!appl_shm->exit_flag)
			APPL_EXIT_FAILURE("event send failed!");
	}
}

static em_status_t
test_eo_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		      va_list args)
{
	const char *str;

	str = va_arg(args, const char*);

	printf("%s  EO %" PRI_EO "  error 0x%08X  escope 0x%X  core %d\n",
	       str, eo, error, escope, em_core_id());

	return error;
}
