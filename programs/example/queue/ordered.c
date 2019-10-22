/*
 *   Copyright (c) 2012, Nokia Siemens Networks
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

/**
 * @file
 *
 * Event Machine Parallel-Ordered queue test
 */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/*
 * Test configuration
 */

/** Number of test EOs and queues. Must be an even number. */
#define NUM_EO  32

/** Number of initial events per EO pair. */
#define NUM_EVENT  37

/** Number of events that EO-ordered will allocate for each input event */
#define NUM_SUB_EVENT  11

/** Max number of cores */
#define MAX_NBR_OF_CORES  128

/** The number of events to be received before printing a result */
#define PRINT_EVENT_COUNT  0xff0000

/** Print results on all cores */
#define PRINT_ON_ALL_CORES  1 /* 0=False or 1=True */

/** Define how many events are sent per em_send_multi() call */
#define SEND_MULTI_MAX 32

/**
 * Test statistics (per core)
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	struct {
		uint64_t num_events;
		uint64_t begin_cycles;
		uint64_t end_cycles;
		uint64_t print_count;
	};
} test_stat_t;

COMPILE_TIME_ASSERT(sizeof(test_stat_t) == ENV_CACHE_LINE_SIZE,
		    TEST_STAT_T_SIZE_ERROR);

/**
 * Ordered queue context
 */
typedef struct {
	/** Next destination queue */
	em_queue_t dest_queue;
} q_ordered_context_t;

/**
 * Atomic queue context
 */
typedef struct {
	/** Expected sequence number */
	int seq;
	/** Expected sub-sequence number */
	int sub_seq;
	/** Next destination queue */
	em_queue_t dest_queue;
} q_atomic_context_t;

/**
 * Queue context padded to cache line size
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	q_ordered_context_t q_ordered_ctx;
	q_atomic_context_t q_atomic_ctx;
} q_context_array_elem_t;

COMPILE_TIME_ASSERT(sizeof(q_context_array_elem_t) == ENV_CACHE_LINE_SIZE,
		    Q_CONTEXT_SIZE_ERROR);

/**
 * EO-ordered context, i.e. EO with an oredered queue
 */
typedef struct {
	/** This EO */
	em_eo_t hdl;
	/** The EO's queue */
	em_queue_t ordered_queue;
} eo_ordered_context_t;

/**
 * EO-atomic context, i.e. EO with an atomic queue
 */
typedef struct {
	/** This EO */
	em_eo_t hdl;
	/** The EO's queue */
	em_queue_t atomic_queue;
	/** The peer EO's ordered queue */
	em_queue_t peer_ordered_queue;
} eo_atomic_context_t;

/**
 * Queue context padded to cache line size
 */
typedef union {
	uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
	eo_ordered_context_t eo_ordered_ctx;
	eo_atomic_context_t eo_atomic_ctx;
} eo_context_array_elem_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_array_elem_t) == ENV_CACHE_LINE_SIZE,
		    EO_CONTEXT_SIZE_ERROR);

#define EV_ID_ORDERED_EVENT  1
#define EV_ID_START_EVENT    2
/** Ordered event content */
typedef struct {
	/** Event ID */
	int ev_id;
	/** Sequence number */
	int seq;
	/** Sub-sequence number */
	int sub_seq;
	/** Indication from sender that event might be received out of order */
	int out_of_order;
	/** Indication from sender that event is last in order using 'seq' */
	int last_in_order;
	/** Indication from sender that event is a copy of 'original' */
	int is_copy;
	/** If the event is a copy then the original event is sent along */
	em_event_t original;
} ordered_event_t;
/** Startup event content */
typedef struct {
	/** Event ID */
	int ev_id;
	/** Request to allocate and send test events into the 'ordered_queue' */
	em_queue_t ordered_queue;
} start_event_t;
/**
 * Test event, content identified by 'ev_id'
 */
typedef union {
	int ev_id;
	ordered_event_t ordered;
	start_event_t start;
} test_event_t;

/**
 * Test shared memory
 */
typedef struct {
	/** Event pool used by this application */
	em_pool_t pool;
	/** Ordered queue context array */
	q_context_array_elem_t q_ordered_ctx[NUM_EO / 2]
		ENV_CACHE_LINE_ALIGNED;
	/** Atomic queue context array */
	q_context_array_elem_t q_atomic_ctx[NUM_EO / 2]
		ENV_CACHE_LINE_ALIGNED;
	/** EO context array  for EOs with ordered queue */
	eo_context_array_elem_t eo_ordered_ctx[NUM_EO / 2]
		ENV_CACHE_LINE_ALIGNED;
	/** EO context array  for EOs with atomic queue */
	eo_context_array_elem_t eo_atomic_ctx[NUM_EO / 2]
		ENV_CACHE_LINE_ALIGNED;
	/** Array of core specific data accessed by using core index. */
	test_stat_t core_stat[MAX_NBR_OF_CORES] ENV_CACHE_LINE_ALIGNED;
} test_shm_t;

/** EM-core local pointer to shared memory */
static ENV_LOCAL test_shm_t *test_shm;

static em_status_t
eo_ordered_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t
eo_ordered_stop(void *eo_context, em_eo_t eo);
static void
eo_ordered_receive(void *eo_context, em_event_t event, em_event_type_t type,
		   em_queue_t queue, void *queue_ctx);

static em_status_t
eo_atomic_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t
eo_atomic_stop(void *eo_context, em_eo_t eo);
static void
eo_atomic_receive(void *eo_context, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *queue_ctx);
static void
initialize_events(start_event_t *const start_event);
static void
print_result(test_stat_t *const test_stat);
static int
get_queue_priority(const int index);

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

	if (core == 0) {
		test_shm = env_shared_reserve("TestSharedMem",
					      sizeof(test_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		test_shm = env_shared_lookup("TestSharedMem");
	}

	if (test_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Test init failed on EM-core:%u", em_core_id());
	else if (core == 0)
		memset(test_shm, 0, sizeof(test_shm_t));
}

/**
 * Startup of the test application.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void
test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo_a, eo_b;
	em_queue_t queue_a, queue_b;
	em_status_t ret;
	eo_ordered_context_t *eo_ordered_ctx;
	eo_atomic_context_t *eo_atomic_ctx;
	q_ordered_context_t *q_ordered_ctx;
	q_atomic_context_t *q_atomic_ctx;
	int i;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		test_shm->pool = appl_conf->pools[0];
	else
		test_shm->pool = EM_POOL_DEFAULT;

	APPL_PRINT("\n"
		   "***********************************************************\n"
		   "EM APPLICATION: '%s' initializing:\n"
		   "  %s: %s() - EM-core:%i\n"
		   "  Application running on %d EM-cores (procs:%d, threads:%d)\n"
		   "  using event pool:%" PRI_POOL "\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads,
		   test_shm->pool);

	test_fatal_if(test_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	/*
	 * Create and start EO's & queues
	 */
	for (i = 0; i < NUM_EO / 2; i++) {
		eo_ordered_ctx = &test_shm->eo_ordered_ctx[i].eo_ordered_ctx;
		eo_atomic_ctx = &test_shm->eo_atomic_ctx[i].eo_atomic_ctx;
		q_ordered_ctx = &test_shm->q_ordered_ctx[i].q_ordered_ctx;
		q_atomic_ctx = &test_shm->q_atomic_ctx[i].q_atomic_ctx;

		/* Create EO with ordered queue */
		eo_a = em_eo_create("eo-ordered", eo_ordered_start, NULL,
				    eo_ordered_stop, NULL, eo_ordered_receive,
				    eo_ordered_ctx);
		queue_a = em_queue_create("ordered",
					  EM_QUEUE_TYPE_PARALLEL_ORDERED,
					  get_queue_priority(i),
					  EM_QUEUE_GROUP_DEFAULT, NULL);

		ret = em_queue_set_context(queue_a, q_ordered_ctx);
		test_fatal_if(ret != EM_OK,
			      "Queue set context:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      ret, eo_a, queue_a);

		ret = em_eo_add_queue_sync(eo_a, queue_a);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      ret, eo_a, queue_a);

		eo_ordered_ctx->hdl = eo_a;
		eo_ordered_ctx->ordered_queue = queue_a;

		/* Create EO with an atomic queue */
		eo_b = em_eo_create("eo-atomic", eo_atomic_start, NULL,
				    eo_atomic_stop, NULL, eo_atomic_receive,
				    eo_atomic_ctx);
		queue_b = em_queue_create("atomic",
					  EM_QUEUE_TYPE_ATOMIC,
					  get_queue_priority(i),
					  EM_QUEUE_GROUP_DEFAULT, NULL);

		ret = em_queue_set_context(queue_b, q_atomic_ctx);
		test_fatal_if(ret != EM_OK,
			      "Queue set context:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
			      ret, eo_b, queue_b);

		ret = em_eo_add_queue_sync(eo_b, queue_b);
		test_fatal_if(ret != EM_OK,
			      "EO add queue:%" PRI_STAT "\n"
			      "EO:%" PRI_EO " queue:%" PRI_QUEUE "",
			      ret, eo_b, queue_b);
		eo_atomic_ctx->hdl = eo_b;
		eo_atomic_ctx->atomic_queue = queue_b;
		eo_atomic_ctx->peer_ordered_queue = queue_a;

		/* Initialize queue context data */
		q_ordered_ctx->dest_queue = queue_b;
		q_atomic_ctx->seq = 0;
		q_atomic_ctx->sub_seq = 0;
		q_atomic_ctx->dest_queue = queue_a;

		/* Start the EO's */
		ret = em_eo_start_sync(eo_a, NULL, NULL);
		test_fatal_if(ret != EM_OK,
			      "EO start:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo_a);
		ret = em_eo_start_sync(eo_b, NULL, NULL);
		test_fatal_if(ret != EM_OK,
			      "EO start:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo_b);
	}
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo;
	em_status_t ret;
	int i;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	/* stop all EOs */
	for (i = 0; i < NUM_EO / 2; i++) {
		eo = test_shm->eo_atomic_ctx[i].eo_atomic_ctx.hdl;
		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO stop:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo);

		eo = test_shm->eo_ordered_ctx[i].eo_ordered_ctx.hdl;
		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO stop:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo);
	}
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(test_shm);
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t
eo_ordered_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	(void)eo_context;
	(void)conf;

	APPL_PRINT("EO %" PRI_EO " starting.\n", eo);

	return EM_OK;
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t
eo_atomic_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_atomic_context_t *const eo_ctx = eo_context;
	em_status_t ret;

	(void)conf;

	APPL_PRINT("EO %" PRI_EO " starting.\n", eo);

	/*
	 * Allocate and send the startup event to the atomic EO of the pair.
	 */
	em_event_t event = em_alloc(sizeof(start_event_t), EM_EVENT_TYPE_SW,
				    test_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event alloc fails");
	start_event_t *start_event = em_event_pointer(event);

	start_event->ev_id = EV_ID_START_EVENT;
	start_event->ordered_queue = eo_ctx->peer_ordered_queue;

	ret = em_send(event, eo_ctx->atomic_queue);
	test_fatal_if(ret != EM_OK, "start event send:%" PRI_STAT "");

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t
eo_ordered_stop(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	APPL_PRINT("EO %" PRI_EO " stopping.\n", eo);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* delete the EO at the end of the stop-function */
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO delete:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t
eo_atomic_stop(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	APPL_PRINT("EO %" PRI_EO " stopping.\n", eo);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* delete the EO at the end of the stop-function */
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO delete:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

/**
 * @private
 *
 * EO receive function for EO A.
 *
 * Loops back events and calculates the event rate.
 */
static void
eo_ordered_receive(void *eo_context, em_event_t event, em_event_type_t type,
		   em_queue_t queue, void *queue_ctx)
{
	q_ordered_context_t *const q_ctx = queue_ctx;
	test_event_t *const test_event = em_event_pointer(event);
	ordered_event_t *ordered;
	em_status_t ret;
	int interleave;
	int out_of_order = 0;
	int sub_seq;
	int i;

	(void)eo_context;
	(void)type;
	(void)queue;

	test_fatal_if(test_event->ev_id != EV_ID_ORDERED_EVENT,
		      "Unexpected ev-id:%d", test_event->ev_id);
	ordered = &test_event->ordered;

	ordered->out_of_order = 0;
	ordered->last_in_order = 0;
	ordered->is_copy = 0;

	/* interleave the input event in between the output events */
	interleave = ordered->seq % (NUM_SUB_EVENT + 1);

	for (i = 0, sub_seq = 0; i < NUM_SUB_EVENT; i++, sub_seq++) {
		/* allocate sub-events to send in the same ordered context */
		em_event_t sub_event = em_alloc(sizeof(ordered_event_t),
						EM_EVENT_TYPE_SW,
						test_shm->pool);

		test_fatal_if(sub_event == EM_EVENT_UNDEF,
			      "Sub-event alloc failed:%i", i);

		ordered_event_t *const sub_ordered =
			em_event_pointer(sub_event);

		sub_ordered->ev_id = EV_ID_ORDERED_EVENT;
		sub_ordered->seq = ordered->seq;

		if (interleave == i) {
			ordered->sub_seq = sub_seq;
			sub_seq++;
			ordered->last_in_order = 1;

			em_event_t copy_event =
				em_alloc(sizeof(ordered_event_t),
					 EM_EVENT_TYPE_SW, test_shm->pool);
			test_fatal_if(copy_event == EM_EVENT_UNDEF,
				      "Copy-event alloc failed:%i", i);
			ordered_event_t *const copy_ordered =
				em_event_pointer(copy_event);
			memcpy(copy_ordered, ordered, sizeof(ordered_event_t));
			copy_ordered->is_copy = 1;
			copy_ordered->original = event; /* store original */

			ret = em_send(copy_event, q_ctx->dest_queue);
			test_fatal_if(ret != EM_OK, "event send:%" PRI_STAT "");
			out_of_order = 1;
			em_ordered_processing_end();
		}

		sub_ordered->sub_seq = sub_seq;
		sub_ordered->out_of_order = out_of_order;
		sub_ordered->last_in_order = 0;
		sub_ordered->is_copy = 0;

		ret = em_send(sub_event, q_ctx->dest_queue);
		test_fatal_if(ret != EM_OK, "event send:%" PRI_STAT "");
	}

	if (interleave == i) {
		ordered->sub_seq = sub_seq;
		ordered->out_of_order = 0;
		ordered->last_in_order = 1;
		ret = em_send(event, q_ctx->dest_queue);
		test_fatal_if(ret != EM_OK, "event send:%" PRI_STAT "");
	}
}

/**
 * @private
 *
 * EO receive function for EO B.
 *
 * Loops back events.
 */
static void
eo_atomic_receive(void *eo_context, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *queue_ctx)
{
	eo_atomic_context_t *const eo_ctx = eo_context;
	q_atomic_context_t *const q_ctx = queue_ctx;
	test_event_t *const test_event = em_event_pointer(event);
	const int core = em_core_id();
	ordered_event_t *ordered;
	em_status_t ret;
	uint64_t num_events;
	int seq, sub_seq;
	int out_of_order, last_in_order;

	(void)type;

	if (unlikely(test_event->ev_id == EV_ID_START_EVENT)) {
		/*
		 * Start-up only, one time: initialize the test event sending.
		 * Called from EO-receive to avoid mixing up events & sequence
		 * numbers in start-up for ordered EO-pairs (sending from the
		 * start functions could mess up the seqno:s since all the
		 * cores are already in the dispatch loop).
		 */
		initialize_events(&test_event->start);
		em_free(event);
		return;
	}

	test_fatal_if(test_event->ev_id != EV_ID_ORDERED_EVENT,
		      "Unexpected ev-id:%d", test_event->ev_id);

	ordered = &test_event->ordered;
	seq = ordered->seq;
	sub_seq = ordered->sub_seq;
	out_of_order = ordered->out_of_order;
	last_in_order = ordered->last_in_order;

	if (ordered->is_copy)
		em_free(ordered->original);

	/* Check the sequence number for events that should be in order */
	if (!out_of_order &&
	    unlikely(seq != q_ctx->seq || sub_seq != q_ctx->sub_seq))
		APPL_EXIT_FAILURE("Bad seqnbr EO:%" PRI_EO " Q:%" PRI_QUEUE "\t"
				  "expected:%i-%i event-seq:%i-%i core:%d\n",
				  eo_ctx->hdl, queue, q_ctx->seq,
				  q_ctx->sub_seq, seq, sub_seq, core);

	if (out_of_order) {
		em_free(event);
	} else if (last_in_order) {
		ordered->seq = q_ctx->seq + NUM_EVENT;
		ordered->sub_seq = 0;
		q_ctx->seq++;
		q_ctx->sub_seq = 0;
		ret = em_send(event, q_ctx->dest_queue);
		test_fatal_if(ret != EM_OK, "event send:%" PRI_STAT "");
	} else if (!out_of_order) {
		q_ctx->sub_seq++;
		em_free(event);
	}

	num_events = test_shm->core_stat[core].num_events;

	/* Update the cycle count and print results when necessary */
	if (unlikely(num_events == 0)) {
		test_shm->core_stat[core].begin_cycles = env_get_cycle();
		num_events = 1;
	} else if (unlikely(num_events > PRINT_EVENT_COUNT)) {
		test_shm->core_stat[core].end_cycles = env_get_cycle();
		test_shm->core_stat[core].print_count += 1;

		/* Print measurement result */
		if (PRINT_ON_ALL_CORES)
			print_result(&test_shm->core_stat[core]);
		else if (core == 0)
			print_result(&test_shm->core_stat[core]);
		/* Restart the measurement */
		test_shm->core_stat[core].begin_cycles = env_get_cycle();
		num_events = 0;
	} else {
		num_events += 1;
	}

	test_shm->core_stat[core].num_events = num_events;
}

/**
 * @private
 *
 * Initialize test events. Allocate and send the test events to an EO-pair.
 */
static void
initialize_events(start_event_t *const start_event)
{
	em_event_t events[NUM_EVENT];
	ordered_event_t *ordered;
	int num_sent = 0;
	int i, j;

	/* Alloc and send test events */
	for (i = 0; i < NUM_EVENT; i++) {
		events[i] = em_alloc(sizeof(ordered_event_t), EM_EVENT_TYPE_SW,
				     test_shm->pool);
		test_fatal_if(events[i] == EM_EVENT_UNDEF,
			      "Event allocation failed:%i", i);

		ordered = em_event_pointer(events[i]);
		ordered->ev_id = EV_ID_ORDERED_EVENT;
		ordered->seq = i;
		ordered->sub_seq = 0;
	}

	/* Send in bursts of 'SEND_MULTI_MAX' events */
	const int send_rounds = NUM_EVENT / SEND_MULTI_MAX;
	const int left_over = NUM_EVENT % SEND_MULTI_MAX;

	for (i = 0, j = 0; i < send_rounds; i++, j += SEND_MULTI_MAX) {
		num_sent += em_send_multi(&events[j], SEND_MULTI_MAX,
					  start_event->ordered_queue);
	}
	if (left_over) {
		num_sent += em_send_multi(&events[j], left_over,
					  start_event->ordered_queue);
	}
	test_fatal_if(num_sent != NUM_EVENT,
		      "Event send multi failed:%d (%d)\n"
		      "Q:%" PRI_QUEUE "",
		      num_sent, NUM_EVENT, start_event->ordered_queue);
}

/**
 * Get queue priority value based on the index number.
 *
 * @param Queue index
 *
 * @return Queue priority value
 *
 * @note Priority distribution: 40% LOW, 40% NORMAL, 20% HIGH
 */
static int
get_queue_priority(const int queue_index)
{
	int remainder = queue_index % 5;

	if (remainder <= 1)
		return EM_QUEUE_PRIO_LOW;
	else if (remainder <= 3)
		return EM_QUEUE_PRIO_NORMAL;
	else
		return EM_QUEUE_PRIO_HIGH;
}

/**
 * Prints test measurement result
 */
static void
print_result(test_stat_t *const test_stat)
{
	uint64_t diff;
	uint32_t hz;
	double mhz;
	double cycles_per_event;
	uint64_t print_count;

	if (likely(test_stat->end_cycles > test_stat->begin_cycles))
		diff = test_stat->end_cycles - test_stat->begin_cycles;
	else
		diff = UINT64_MAX - test_stat->begin_cycles +
		       test_stat->end_cycles + 1;

	print_count = test_stat->print_count;
	cycles_per_event = ((double)diff) / ((double)test_stat->num_events);

	hz = env_core_hz();
	mhz = ((double)hz) / 1000000.0;

	APPL_PRINT("cycles per event %.2f  @%.2f MHz (core-%02i %" PRIu64 ")\n",
		   cycles_per_event, mhz, em_core_id(), print_count);
}
