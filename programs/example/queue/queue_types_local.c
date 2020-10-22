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

/**
 * @file
 *
 * Event Machine Queue Types test/example with included local queues.
 *
 * Similar to the queue_types_ag.c example but additionally adds local queues
 * between the scheduled queues.
 * See programs/example/queue/queue_types_ag.c
 */

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/* Number of queue type pairs (constant, don't change) */
#define QUEUE_TYPE_PAIRS  10
/*
 * Number of test EOs and queues. Must be an even number.
 * Test has NUM_EO/2 EO pairs, that send ping-pong events.
 * Depending on test dynamics (e.g. single burst in atomic
 * queue) only one EO of a pair might be active at a time.
 */
#define NUM_EO     (2 * 8 * QUEUE_TYPE_PAIRS)
/* Max number of queues supported by the test */
#define MAX_QUEUES (NUM_EO / QUEUE_TYPE_PAIRS * 30)
/* Number of ping-pong events per EO pair */
#define NUM_EVENT  (3 * 32)
/* Number of data bytes in the event */
#define DATA_SIZE  64
/* Max number of cores supported by the test */
#define MAX_CORES  64
/* Print stats when the number of received events reaches this value on a core*/
#define PRINT_COUNT  0x1000000

/** Define how many events are sent per em_send_multi() call */
#define SEND_MULTI_MAX 32

/*
 * Enable atomic access checks.
 * If enabled will crash the application if the atomic-processing context
 * is violated, i.e. checks that events from an atomic queue are being
 * processed one-by-one.
 */
#define VERIFY_ATOMIC_ACCESS  1  /* 0=False or 1=True */
/* Call em_atomic_processing_end every ince in a while */
#define CALL_ATOMIC_PROCESSING_END  1  /* 0=False or 1=True */

/* Return 'TRUE' if the queue pair retains event order */
#define ORDERED_PAIR(q_type_a, q_type_b)  (				\
		    (((q_type_a) == EM_QUEUE_TYPE_ATOMIC) ||		\
		    ((q_type_a) == EM_QUEUE_TYPE_PARALLEL_ORDERED)) &&  \
		    (((q_type_b) == EM_QUEUE_TYPE_ATOMIC) ||		\
		     ((q_type_b) == EM_QUEUE_TYPE_PARALLEL_ORDERED)))

#define ABS(nbr1, nbr2)  (((nbr1) > (nbr2)) ? ((nbr1) - (nbr2)) : \
			  ((nbr2) - (nbr1)))

#define PRINT_CORE_STAT_FMT \
"Core-%02i:\t" \
"A-L-A-L:%6" PRIu64 "  P-L-P-L:%6" PRIu64 "  PO-L-PO-L:%6" PRIu64 "\t" \
"P-L-A-L:%6" PRIu64 "  PO-L-A-L:%6" PRIu64 "  PO-L-P-L:%6" PRIu64 "\t" \
"AG-L-AG-L:%6" PRIu64 "  AG-L-A-L:%6" PRIu64 "\t" \
"AG-L-P-L:%6" PRIu64 "  AG-L-PO-L:%6" PRIu64 "\t" \
"cycles/event:%.0f @%.0fMHz %" PRIu64 "\n"

/**
 * Combinations of Queue Type pairs
 */
#define NO_AG (0)
#define IN_AG (1)
typedef struct queue_type_pairs_ {
	em_queue_type_t q_type[2];
	int in_atomic_group[2];
} queue_type_pair_t;

queue_type_pair_t  queue_type_pairs[QUEUE_TYPE_PAIRS] = {
	/* Ordered Pair */
	{ {EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_ATOMIC}, {NO_AG, NO_AG} },
	{ {EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_TYPE_PARALLEL}, {NO_AG, NO_AG} },
	/* Ordered Pair */
	{ {EM_QUEUE_TYPE_PARALLEL_ORDERED, EM_QUEUE_TYPE_PARALLEL_ORDERED},
	  {NO_AG, NO_AG} },
	{ {EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_TYPE_ATOMIC}, {NO_AG, NO_AG} },
	/* Ordered Pair */
	{ {EM_QUEUE_TYPE_PARALLEL_ORDERED, EM_QUEUE_TYPE_ATOMIC},
	  {NO_AG, NO_AG} },
	{ {EM_QUEUE_TYPE_PARALLEL_ORDERED, EM_QUEUE_TYPE_PARALLEL},
	  {NO_AG, NO_AG} },
	/* With Atomic Groups for atomic queues: */
	/* Ordered Pair */
	{ {EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_ATOMIC}, {IN_AG, IN_AG} },
	/* Ordered Pair */
	{ {EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_ATOMIC}, {IN_AG, NO_AG} },
	{ {EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_PARALLEL}, {IN_AG, NO_AG} },
	/* Ordered Pair */
	{ {EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_PARALLEL_ORDERED},
	  {IN_AG, NO_AG} },
};

COMPILE_TIME_ASSERT(sizeof(queue_type_pairs) ==
		    (QUEUE_TYPE_PAIRS * sizeof(queue_type_pair_t)),
		    QUEUE_TYPE_PAIRS_SIZE_ERROR);

typedef enum {
	PT_ATOMIC_ATOMIC = 0,
	PT_PARALLEL_PARALLEL = 1,
	PT_PARALORD_PARALORD = 2,
	PT_PARALLEL_ATOMIC = 3,
	PT_PARALORD_ATOMIC = 4,
	PT_PARALORD_PARALLEL = 5,
	/* With Atomic Groups (AG) for atomic queues: */
	PT_AG_AG = 6,
	PT_AG_ATOMIC = 7,
	PT_AG_PARALLEL = 8,
	PT_AG_PARALORD = 9,
	PT_UNDEFINED
} pair_type_t;

/**
 * Test statistics (per core)
 */
typedef union {
	uint8_t u8[2 * ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;

	struct {
		uint64_t events;
		uint64_t begin_cycles;
		uint64_t end_cycles;
		uint64_t print_count;
		/*
		 * Pair-Type count, i.e. the number of events belonging to
		 * a certain pair-type on this core
		 */
		uint64_t pt_count[QUEUE_TYPE_PAIRS];
	};
} core_stat_t;

COMPILE_TIME_ASSERT(sizeof(core_stat_t) % ENV_CACHE_LINE_SIZE == 0,
		    CORE_STAT_T__SIZE_ERROR);

/**
 * Test EO context
 */
typedef struct {
	em_eo_t eo_hdl;
	/* EO pair retains order? 0/1 */
	int ordered_pair;
	pair_type_t pair_type;
	int owns_ag_queues;
	em_atomic_group_t agrp_hdl;
	int peer_owns_ag_queues;
	/* Atomic group is also set as queue type atomic */
	em_queue_type_t q_type;
	env_spinlock_t verify_atomic_access;

	void *end[0] ENV_CACHE_LINE_ALIGNED;
} eo_context_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_t) % ENV_CACHE_LINE_SIZE == 0,
		    EO_CTX_T__SIZE_ERROR);

/**
 * Test Queue context
 */
typedef struct {
	em_queue_t q_hdl;
	em_queue_type_t q_type;
	unsigned int idx;

	union {
		struct {
			/* Associated local queue */
			em_queue_t local_q_hdl;
			int in_atomic_group;
			uint64_t seqno;
			/* Total number of events handled from the queue */
			env_atomic64_t num_events;
			/* Number of events at the previous check-point  */
			uint64_t prev_events;
		} sched;

		struct {
			/* Total number of events handled from the queue */
			uint64_t num_events;
			/* Number of events at the previous check-point  */
			uint64_t prev_events;
		} local;
	};

	void *end[0] ENV_CACHE_LINE_ALIGNED;
} queue_context_t;

COMPILE_TIME_ASSERT(sizeof(queue_context_t) % ENV_CACHE_LINE_SIZE == 0,
		    Q_CTX_T__SIZE_ERROR);

#define EV_ID_DATA_EVENT  1
#define EV_ID_START_EVENT 2
/** Data event content */
typedef struct {
	int ev_id;
	/* Next destination queue */
	em_queue_t dest;
	em_queue_t src;
	/* Sequence number */
	uint64_t seqno;
	/* Test data */
	uint8_t data[DATA_SIZE];
} data_event_t;
/** Startup event content */
typedef struct {
	int ev_id;

	int in_atomic_group_a;
	int src_q_cnt;
	em_queue_t src_queues[3];

	int in_atomic_group_b;
	int dst_q_cnt;
	em_queue_t dst_queues[3];
} start_event_t;
/**
 * Test event, content identified by 'ev_id'
 */
typedef union {
	int ev_id;
	data_event_t data;
	start_event_t start;
} test_event_t;

/**
 * Queue Types test shared memory
 */
typedef struct {
	core_stat_t core_stat[MAX_CORES] ENV_CACHE_LINE_ALIGNED;

	eo_context_t eo_context[NUM_EO] ENV_CACHE_LINE_ALIGNED;

	queue_context_t queue_context[MAX_QUEUES] ENV_CACHE_LINE_ALIGNED;

	unsigned num_queues ENV_CACHE_LINE_ALIGNED;

	em_pool_t pool;

	int teardown_in_progress;
} qtypes_shm_t;

COMPILE_TIME_ASSERT(sizeof(qtypes_shm_t) % ENV_CACHE_LINE_SIZE == 0,
		    QTYPES_SHM_T__SIZE_ERROR);

/* EM-core local pointer to shared memory */
static ENV_LOCAL qtypes_shm_t *qtypes_shm;

/**
 * Local Function Prototypes
 */
static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t
start_locq(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t
start_local(void *eo_ctx, em_eo_t eo);
static em_status_t
start_local_locq(void *eo_ctx, em_eo_t eo);

static em_status_t
stop(void *eo_context, em_eo_t eo);
static em_status_t
stop_locq(void *eo_context, em_eo_t eo);
static em_status_t
stop_local(void *eo_ctx, em_eo_t eo);
static em_status_t
stop_local_locq(void *eo_ctx, em_eo_t eo);

static void
receive_a(void *eo_context, em_event_t event, em_event_type_t type,
	  em_queue_t queue, void *q_ctx);
static void
receive_b(void *eo_context, em_event_t event, em_event_type_t type,
	  em_queue_t queue, void *q_ctx);
static void
receive_locq(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *q_ctx);

static pair_type_t
get_pair_type(queue_type_pair_t *queue_type_pair);

static inline void
verify_seqno(eo_context_t *const eo_ctx, queue_context_t *const q_ctx,
	     uint64_t seqno);

static void
verify_all_queues_get_events(void);

static inline void
verify_atomic_access__begin(eo_context_t *const eo_ctx);

static inline void
verify_atomic_access__end(eo_context_t *const eo_ctx);

static void
print_core_stats(core_stat_t *const cstat, uint64_t print_events);

static void
print_event_msg_string(void);

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
 * Init of the Queue Types test application.
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
		qtypes_shm = env_shared_reserve("QueueTypesSharedMem",
						sizeof(qtypes_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		qtypes_shm = env_shared_lookup("QueueTypesSharedMem");
	}

	if (qtypes_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(__LINE__), 0xdead,
			   "Queue Types test init failed on EM-core: %u\n",
			   em_core_id());
	} else if (core == 0) {
		memset(qtypes_shm, 0, sizeof(qtypes_shm_t));
	}
}

/**
 * Startup of the Queue Types test application.
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
	em_atomic_group_t atomic_group;
	em_eo_t eo, eo_locq;
	em_queue_t queue_a, queue_b;
	em_queue_t queue_ag_a1, queue_ag_a2, queue_ag_a3;
	em_queue_t queue_ag_b1, queue_ag_b2, queue_ag_b3;
	em_queue_t queue_local_a, queue_local_b;
	em_queue_type_t q_type_a, q_type_b;
	em_status_t ret, start_ret = EM_ERROR;
	eo_context_t *eo_ctx;
	queue_context_t *q_ctx;
	pair_type_t pair_type;
	unsigned int qcnt = 0; /* queue context index */
	unsigned int eocnt = 0; /* eo context index */
	int in_atomic_group_a, in_atomic_group_b;
	int ordered_pair;
	char ag_name[EM_ATOMIC_GROUP_NAME_LEN];
	int i;

	queue_a = EM_QUEUE_UNDEF, queue_b = EM_QUEUE_UNDEF;
	queue_ag_a1 = EM_QUEUE_UNDEF, queue_ag_a2 = EM_QUEUE_UNDEF;
	queue_ag_a3 = EM_QUEUE_UNDEF;
	queue_ag_b1 = EM_QUEUE_UNDEF, queue_ag_b2 = EM_QUEUE_UNDEF;
	queue_ag_b3 = EM_QUEUE_UNDEF;
	queue_local_a = EM_QUEUE_UNDEF, queue_local_b = EM_QUEUE_UNDEF;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	if (appl_conf->num_pools >= 1)
		qtypes_shm->pool = appl_conf->pools[0];
	else
		qtypes_shm->pool = EM_POOL_DEFAULT;

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
		   qtypes_shm->pool);

	test_fatal_if(qtypes_shm->pool == EM_POOL_UNDEF,
		      "Undefined application event pool!");

	test_fatal_if(em_eo_current() != EM_EO_UNDEF,
		      "Invalid current EO");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");

	qtypes_shm->num_queues = 0;
	qtypes_shm->teardown_in_progress = EM_FALSE;

	/* Create and start application pipelines. Send initial test events */
	for (i = 0; i < (NUM_EO / 4); i++) {
		q_type_a = queue_type_pairs[i % QUEUE_TYPE_PAIRS].q_type[0];
		in_atomic_group_a =
		    queue_type_pairs[i % QUEUE_TYPE_PAIRS].in_atomic_group[0];

		q_type_b = queue_type_pairs[i % QUEUE_TYPE_PAIRS].q_type[1];
		in_atomic_group_b =
		    queue_type_pairs[i % QUEUE_TYPE_PAIRS].in_atomic_group[1];

		ordered_pair = ORDERED_PAIR(q_type_a, q_type_b);

		pair_type =
			get_pair_type(&queue_type_pairs[i % QUEUE_TYPE_PAIRS]);
		test_fatal_if(pair_type == PT_UNDEFINED,
			      "Queue Pair Type UNDEFINED! (%u, %u)",
			      q_type_a, q_type_b);

		/* Create EO "local-A" */
		eo_ctx = &qtypes_shm->eo_context[eocnt++];
		eo_ctx->agrp_hdl = EM_ATOMIC_GROUP_UNDEF;

		eo_locq = em_eo_create("EO-local-A",
				       start_locq, start_local_locq,
				       stop_locq, stop_local_locq,
				       receive_locq, eo_ctx);
		queue_local_a = em_queue_create("queue-local-A",
						EM_QUEUE_TYPE_LOCAL,
						EM_QUEUE_PRIO_NORMAL,
						EM_QUEUE_GROUP_UNDEF, NULL);
		ret = em_eo_add_queue_sync(eo_locq, queue_local_a);
		test_fatal_if(ret != EM_OK, "EO-local-A setup failed!");
		q_ctx = &qtypes_shm->queue_context[qcnt];
		q_ctx->q_hdl = queue_local_a;
		q_ctx->q_type = EM_QUEUE_TYPE_LOCAL;
		q_ctx->idx = qcnt++;

		ret = em_queue_set_context(queue_local_a, q_ctx);
		test_fatal_if(ret != EM_OK, "EO-local-A setup failed!");

		ret = em_eo_start_sync(eo_locq, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO-local-A setup:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);

		/* Create EO "A" */
		eo_ctx = &qtypes_shm->eo_context[eocnt++];
		eo_ctx->ordered_pair = ordered_pair;
		eo_ctx->pair_type = pair_type;
		eo_ctx->q_type = q_type_a;
		eo_ctx->owns_ag_queues = in_atomic_group_a;
		eo_ctx->agrp_hdl = EM_ATOMIC_GROUP_UNDEF;
		eo_ctx->peer_owns_ag_queues = in_atomic_group_b;

		eo = em_eo_create("EO-A", start, NULL, stop, NULL, receive_a,
				  eo_ctx);

		if (in_atomic_group_a && q_type_a == EM_QUEUE_TYPE_ATOMIC) {
			atomic_group =
				em_atomic_group_create("group_a",
						       EM_QUEUE_GROUP_DEFAULT);
			test_fatal_if(atomic_group == EM_ATOMIC_GROUP_UNDEF,
				      "Atomic group creation failed!");

			if (em_atomic_group_get_name(atomic_group, ag_name,
						     EM_ATOMIC_GROUP_NAME_LEN))
				APPL_PRINT("New atomic group:%s for EO:\t"
					   "%" PRI_EO "\n", ag_name, eo);

			eo_ctx->agrp_hdl = atomic_group;

			queue_ag_a1 = em_queue_create_ag("AG:Q A1",
							 EM_QUEUE_PRIO_NORMAL,
							 atomic_group, NULL);
			queue_ag_a2 = em_queue_create_ag("AG:Q A2",
							 EM_QUEUE_PRIO_NORMAL,
							 atomic_group, NULL);
			queue_ag_a3 = em_queue_create_ag("AG:Q A3",
							 EM_QUEUE_PRIO_NORMAL,
							 atomic_group, NULL);

			ret = em_eo_add_queue_sync(eo, queue_ag_a1);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");

			ret = em_eo_add_queue_sync(eo, queue_ag_a2);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");

			ret = em_eo_add_queue_sync(eo, queue_ag_a3);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_ag_a1;
			q_ctx->q_type = q_type_a;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_a;
			q_ctx->sched.in_atomic_group = in_atomic_group_a;

			ret = em_queue_set_context(queue_ag_a1, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_ag_a2;
			q_ctx->q_type = q_type_a;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_a;
			q_ctx->sched.in_atomic_group = in_atomic_group_a;

			ret = em_queue_set_context(queue_ag_a2, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_ag_a3;
			q_ctx->q_type = q_type_a;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_a;
			q_ctx->sched.in_atomic_group = in_atomic_group_a;

			ret = em_queue_set_context(queue_ag_a3, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");
		} else {
			queue_a = em_queue_create("queue A", q_type_a,
						  EM_QUEUE_PRIO_NORMAL,
						  EM_QUEUE_GROUP_DEFAULT, NULL);
			ret = em_eo_add_queue_sync(eo, queue_a);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_a;
			q_ctx->q_type = q_type_a;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_a;
			q_ctx->sched.in_atomic_group = in_atomic_group_a;

			ret = em_queue_set_context(queue_a, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-A setup failed!");
		}

		/* Start EO-A */
		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO-A setup failed:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);

		/* Create EO "local-B" */
		eo_ctx = &qtypes_shm->eo_context[eocnt++];
		eo_ctx->agrp_hdl = EM_ATOMIC_GROUP_UNDEF;

		eo_locq = em_eo_create("EO-local-B", start_locq, NULL,
				       stop_locq, NULL, receive_locq, eo_ctx);
		queue_local_b = em_queue_create("queue-local-B",
						EM_QUEUE_TYPE_LOCAL,
						EM_QUEUE_PRIO_NORMAL,
						EM_QUEUE_GROUP_UNDEF, NULL);
		ret = em_eo_add_queue_sync(eo_locq, queue_local_b);
		test_fatal_if(ret != EM_OK, "EO-local-B setup failed!");

		q_ctx = &qtypes_shm->queue_context[qcnt];
		q_ctx->q_hdl = queue_local_b;
		q_ctx->q_type = EM_QUEUE_TYPE_LOCAL;
		q_ctx->idx = qcnt++;

		ret = em_queue_set_context(queue_local_b, q_ctx);
		test_fatal_if(ret != EM_OK, "EO-local-B setup failed!");

		ret = em_eo_start_sync(eo_locq, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO-local-B setup:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);

		test_fatal_if(ret != EM_OK, "EO-local-B setup failed!");

		/* Create EO "B" */
		eo_ctx = &qtypes_shm->eo_context[eocnt++];
		eo_ctx->ordered_pair = ordered_pair;
		eo_ctx->pair_type = pair_type;
		eo_ctx->q_type = q_type_b;
		eo_ctx->owns_ag_queues = in_atomic_group_b;
		eo_ctx->agrp_hdl = EM_ATOMIC_GROUP_UNDEF;
		eo_ctx->peer_owns_ag_queues = in_atomic_group_a;

		eo = em_eo_create("EO-B", start, start_local, stop, stop_local,
				  receive_b, eo_ctx);

		if (in_atomic_group_b && q_type_b == EM_QUEUE_TYPE_ATOMIC) {
			atomic_group =
				em_atomic_group_create("group_b",
						       EM_QUEUE_GROUP_DEFAULT);
			test_fatal_if(atomic_group == EM_ATOMIC_GROUP_UNDEF,
				      "Atomic group creation failed!");

			if (em_atomic_group_get_name(atomic_group, ag_name,
						     EM_ATOMIC_GROUP_NAME_LEN))
				APPL_PRINT("New atomic group:%s for EO:\t"
					   "%" PRI_EO "\n", ag_name, eo);

			eo_ctx->agrp_hdl = atomic_group;

			queue_ag_b1 = em_queue_create_ag("AG:Q B1",
							 EM_QUEUE_PRIO_NORMAL,
							 atomic_group, NULL);
			queue_ag_b2 = em_queue_create_ag("AG:Q B2",
							 EM_QUEUE_PRIO_NORMAL,
							 atomic_group, NULL);
			queue_ag_b3 = em_queue_create_ag("AG:Q B3",
							 EM_QUEUE_PRIO_NORMAL,
							 atomic_group, NULL);

			ret = em_eo_add_queue_sync(eo, queue_ag_b1);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");

			ret = em_eo_add_queue_sync(eo, queue_ag_b2);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");

			ret = em_eo_add_queue_sync(eo, queue_ag_b3);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_ag_b1;
			q_ctx->q_type = q_type_b;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_b;
			q_ctx->sched.in_atomic_group = in_atomic_group_b;
			ret = em_queue_set_context(queue_ag_b1, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_ag_b2;
			q_ctx->q_type = q_type_b;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_b;
			q_ctx->sched.in_atomic_group = in_atomic_group_b;
			ret = em_queue_set_context(queue_ag_b2, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_ag_b3;
			q_ctx->q_type = q_type_b;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_b;
			q_ctx->sched.in_atomic_group = in_atomic_group_b;
			ret = em_queue_set_context(queue_ag_b3, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");
		} else {
			queue_b = em_queue_create("queue B", q_type_b,
						  EM_QUEUE_PRIO_NORMAL,
						  EM_QUEUE_GROUP_DEFAULT, NULL);
			ret = em_eo_add_queue_sync(eo, queue_b);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");

			q_ctx = &qtypes_shm->queue_context[qcnt];
			q_ctx->q_hdl = queue_b;
			q_ctx->q_type = q_type_b;
			q_ctx->idx = qcnt++;
			q_ctx->sched.local_q_hdl = queue_local_b;
			q_ctx->sched.in_atomic_group = in_atomic_group_b;
			ret = em_queue_set_context(queue_b, q_ctx);
			test_fatal_if(ret != EM_OK, "EO-B setup failed!");
		}

		ret = em_eo_start_sync(eo, &start_ret, NULL);
		test_fatal_if(ret != EM_OK || start_ret != EM_OK,
			      "EO-B setup failed:%" PRI_STAT " %" PRI_STAT "",
			      ret, start_ret);

		/* update qcnt each round to avoid == 0 in recv-func */
		qtypes_shm->num_queues = qcnt;

		APPL_PRINT("\n");
		/*
		 * Allocate and send the startup event to the first EO of the
		 * pair of this round.
		 */
		em_event_t event = em_alloc(sizeof(start_event_t),
					    EM_EVENT_TYPE_SW,
					    qtypes_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF, "Event alloc fails");
		start_event_t *start_event = em_event_pointer(event);

		start_event->ev_id = EV_ID_START_EVENT;

		start_event->in_atomic_group_a = in_atomic_group_a;
		if (in_atomic_group_a) {
			start_event->src_q_cnt = 3;
			start_event->src_queues[0] = queue_ag_a1;
			start_event->src_queues[1] = queue_ag_a2;
			start_event->src_queues[2] = queue_ag_a3;
		} else {
			start_event->src_q_cnt = 1;
			start_event->src_queues[0] = queue_a;
		}

		start_event->in_atomic_group_b = in_atomic_group_b;
		if (in_atomic_group_b) {
			start_event->dst_q_cnt = 3;
			start_event->dst_queues[0] = queue_ag_b1;
			start_event->dst_queues[1] = queue_ag_b2;
			start_event->dst_queues[2] = queue_ag_b3;
		} else {
			start_event->dst_q_cnt = 1;
			start_event->dst_queues[0] = queue_b;
		}

		ret = em_send(event, start_event->src_queues[0]);
		test_fatal_if(ret != EM_OK, "Event send:%" PRI_STAT "", ret);
	}

	APPL_PRINT("\n\nQs:%i MAX:%i\n", qcnt, MAX_QUEUES);
	APPL_PRINT("EOs:%i MAX:%i\n\n", eocnt, NUM_EO);
	qtypes_shm->num_queues = qcnt;

	test_fatal_if(qcnt > MAX_QUEUES, "Queue context number too high!");
}

/**
 * Test stop function
 *
 * @attention Run only on one EM core
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and teardown.
 */
void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo;
	em_status_t ret;
	eo_context_t *eo_ctx;
	int i;

	(void)appl_conf;

	/* mark 'teardown in progress' to avoid errors seq.nbr check errors */
	qtypes_shm->teardown_in_progress = EM_TRUE;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	/* stop all EOs */
	for (i = 0; i < NUM_EO; i++) {
		eo_ctx = &qtypes_shm->eo_context[i];
		eo = eo_ctx->eo_hdl;
		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO stop:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo);
	}
}

/**
 * Termination of the 'Queue Types Local' test application.
 *
 * @attention Run on one EM core only
 *
 * @see cm_setup() for setup and teardown.
 */
void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(qtypes_shm);
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t
start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_context_t *eo_ctx = eo_context;

	(void)conf;

	APPL_PRINT("EO %" PRI_EO " starting ", eo);

	eo_ctx->eo_hdl = eo;

	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");

	if (VERIFY_ATOMIC_ACCESS)
		env_spinlock_init(&eo_ctx->verify_atomic_access);

	return EM_OK;
}

/**
 * @private
 *
 * EO local-start function.
 */
static em_status_t
start_local(void *eo_context, em_eo_t eo)
{
	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");
	return EM_OK;
}

/**
 * @private
 *
 * EO start function for EOs handling the local queues.
 */
static em_status_t
start_locq(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_context_t *eo_ctx = eo_context;

	(void)conf;

	APPL_PRINT("EO-locq %" PRI_EO " starting ", eo);

	eo_ctx->eo_hdl = eo;

	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");

	return EM_OK;
}

/**
 * @private
 *
 * EO local-start function for EOs handling the local queues.
 */
static em_status_t
start_local_locq(void *eo_context, em_eo_t eo)
{
	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");
	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t
stop(void *eo_context, em_eo_t eo)
{
	eo_context_t *const eo_ctx = (eo_context_t *)eo_context;
	em_status_t ret;

	APPL_PRINT("EO %" PRI_EO " stopping.\n", eo);

	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	if (eo_ctx->agrp_hdl != EM_ATOMIC_GROUP_UNDEF) {
		ret = em_atomic_group_delete(eo_ctx->agrp_hdl);
		test_fatal_if(ret != EM_OK,
			      "AGrp delete:%" PRI_STAT " EO:%" PRI_EO "",
			      ret, eo);
	}

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
 * EO local-stop function.
 */
static em_status_t
stop_local(void *eo_context, em_eo_t eo)
{
	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");
	return EM_OK;
}

/**
 * @private
 *
 * EO stop function for EOs handling the local queues.
 */
static em_status_t
stop_locq(void *eo_context, em_eo_t eo)
{
	(void)eo_context;

	APPL_PRINT("EO-locq %" PRI_EO " stopping.\n", eo);

	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");

	return EM_OK;
}

/**
 * @private
 *
 * EO local-stop function for EOs handling the local queues.
 */
static em_status_t
stop_local_locq(void *eo_context, em_eo_t eo)
{
	/*
	 * Test: Verify that EO & queue _current() and
	 *       _get_context() APIs work as expected.
	 */
	test_fatal_if(em_eo_current() != eo, "Invalid current EO");
	test_fatal_if(em_eo_get_context(eo) != eo_context,
		      "Invalid current EO context");
	test_fatal_if(em_queue_current() != EM_QUEUE_UNDEF,
		      "Invalid current queue");
	return EM_OK;
}

static void
initialize_events(start_event_t *const start)
{
	/*
	 * Allocate and send test events to the EO-pair of this round
	 */
	const int max_q_cnt = start->src_q_cnt > start->dst_q_cnt ?
			      start->src_q_cnt : start->dst_q_cnt;
	/* tmp storage for all events to send this round */
	em_event_t all_events[max_q_cnt][NUM_EVENT];
	/* number of events for a queue in all_events[Q][events] */
	int ev_cnt[max_q_cnt];
	uint64_t seqno = 0;
	int j, x, y;

	for (x = 0; x < max_q_cnt; x++)
		ev_cnt[x] = 0;

	for (j = 0; j < NUM_EVENT;) {
		for (x = 0, y = 0; x < max_q_cnt; x++, y++, j++) {
			em_event_t event = em_alloc(sizeof(test_event_t),
						    EM_EVENT_TYPE_SW,
						    qtypes_shm->pool);
			test_fatal_if(event == EM_EVENT_UNDEF,
				      "Event alloc fails");

			test_event_t *const test_event =
				em_event_pointer(event);

			memset(test_event, 0, sizeof(test_event_t));
			test_event->ev_id = EV_ID_DATA_EVENT;

			if (start->in_atomic_group_b)
				test_event->data.dest = start->dst_queues[y];
			else
				test_event->data.dest = start->dst_queues[0];

			test_event->data.src = start->src_queues[x];

			if (start->in_atomic_group_a ==
			    start->in_atomic_group_b) {
				/* verify seqno (symmetric EO-pairs)*/
				test_event->data.seqno = seqno;
			}

			all_events[x][ev_cnt[x]] = event;
			ev_cnt[x] += 1;
		}
		seqno += 1;
	}

	/* Send events to EO A */
	for (x = 0; x < max_q_cnt; x++) {
		int n, m;
		int num_sent = 0;

		/* Send in bursts of 'SEND_MULTI_MAX' events */
		const int send_rounds = ev_cnt[x] / SEND_MULTI_MAX;
		const int left_over = ev_cnt[x] % SEND_MULTI_MAX;

		for (n = 0, m = 0; n < send_rounds;
		     n++, m += SEND_MULTI_MAX) {
			num_sent += em_send_multi(&all_events[x][m],
						  SEND_MULTI_MAX,
						  start->src_queues[x]);
		}
		if (left_over) {
			num_sent += em_send_multi(&all_events[x][m], left_over,
						  start->src_queues[x]);
		}
		test_fatal_if(num_sent != ev_cnt[x],
			      "Event send multi failed:%d (%d)\n"
			      "Q:%" PRI_QUEUE "",
			      num_sent, ev_cnt[x], start->src_queues[x]);
	}
}

/**
 * @private
 *
 * EO receive function for EO A.
 *
 * Forwards events to the next processing stage (EO)
 * and calculates the event rate.
 */
static void
receive_a(void *eo_context, em_event_t event, em_event_type_t type,
	  em_queue_t queue, void *queue_context)
{
	eo_context_t *const eo_ctx = eo_context;
	queue_context_t *const q_ctx = queue_context;
	test_event_t *const test_event = em_event_pointer(event);
	data_event_t *data_event;
	core_stat_t *cstat;
	em_queue_t dest_queue;
	int core;
	uint64_t core_events, print_events = 0;
	uint64_t seqno;
	em_status_t ret;

	(void)type;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

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

	if (VERIFY_ATOMIC_ACCESS)
		verify_atomic_access__begin(eo_ctx);

	test_fatal_if(test_event->ev_id != EV_ID_DATA_EVENT,
		      "Unexpected ev-id:%d", test_event->ev_id);
	data_event = &test_event->data;

	core = em_core_id();
	cstat = &qtypes_shm->core_stat[core];

	core_events = cstat->events;
	seqno = data_event->seqno;

	/* Increment Q specific event counter (parallel Qs req atomic inc:s)*/
	env_atomic64_inc(&q_ctx->sched.num_events);

	test_fatal_if(data_event->src != queue,
		      "EO-A queue mismatch:%" PRI_QUEUE "!=%" PRI_QUEUE "",
		      data_event->src, queue);

	if (unlikely(core_events == 0)) {
		cstat->begin_cycles = env_get_cycle();
		core_events += 1;
		cstat->pt_count[eo_ctx->pair_type] += 1;
	} else if (unlikely(core_events > PRINT_COUNT)) {
		cstat->end_cycles = env_get_cycle();
		/* indicate that statistics should be printed this round: */
		print_events = core_events;
		core_events = 0;
	} else {
		core_events += 1;
		cstat->pt_count[eo_ctx->pair_type] += 1;
	}

	if (eo_ctx->ordered_pair && eo_ctx->q_type == EM_QUEUE_TYPE_ATOMIC) {
		/* Verify the seq nbr to make sure event order is maintained*/
		verify_seqno(eo_ctx, q_ctx, seqno);
	}

	dest_queue = q_ctx->sched.local_q_hdl;
	data_event->src = queue;

	cstat->events = core_events;

	ret = em_send(event, dest_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag, "EO-A em_send failure");
	}

	if (VERIFY_ATOMIC_ACCESS)
		verify_atomic_access__end(eo_ctx);

	/* Print core specific statistics */
	if (unlikely(print_events)) {
		int i;

		if (core == 0)
			verify_all_queues_get_events();

		print_core_stats(cstat, print_events);

		for (i = 0; i < QUEUE_TYPE_PAIRS; i++)
			cstat->pt_count[i] = 0;

		/*
		 * Test: Verify that EO & queue _current() and
		 *       _get_context() APIs work as expected.
		 */
		test_fatal_if(em_eo_current() != eo_ctx->eo_hdl,
			      "Invalid current EO");
		test_fatal_if(em_eo_get_context(eo_ctx->eo_hdl) != eo_context,
			      "Invalid current EO context");
		test_fatal_if(em_queue_current() != queue,
			      "Invalid current queue");
		test_fatal_if(em_queue_get_context(queue) != queue_context,
			      "Invalid current EO context");

		cstat->begin_cycles = env_get_cycle();
	}
}

/**
 * @private
 *
 * EO receive function for EO B.
 *
 * Forwards events to the next processing stage (EO).
 */
static void
receive_b(void *eo_context, em_event_t event, em_event_type_t type,
	  em_queue_t queue, void *queue_context)
{
	eo_context_t *const eo_ctx = eo_context;
	queue_context_t *const q_ctx = queue_context;
	core_stat_t *cstat;
	em_queue_t dest_queue;
	test_event_t *test_event;
	data_event_t *data_event;
	int core;
	uint64_t core_events;
	em_status_t ret;
	(void)type;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (VERIFY_ATOMIC_ACCESS)
		verify_atomic_access__begin(eo_ctx);

	test_event = em_event_pointer(event);
	test_fatal_if(test_event->ev_id != EV_ID_DATA_EVENT,
		      "Unexpected ev-id:%d", test_event->ev_id);
	data_event = &test_event->data;

	core = em_core_id();
	cstat = &qtypes_shm->core_stat[core];
	core_events = cstat->events;

	/* Increment Q specific event counter (parallel Qs req atomic inc:s)*/
	env_atomic64_inc(&q_ctx->sched.num_events);

	test_fatal_if(data_event->src != queue,
		      "EO-B queue mismatch:%" PRI_QUEUE "!=%" PRI_QUEUE "",
		      data_event->src, queue);

	if (eo_ctx->ordered_pair && eo_ctx->q_type == EM_QUEUE_TYPE_ATOMIC) {
		/* Verify the seq nbr to make sure event order is maintained*/
		verify_seqno(eo_ctx, q_ctx, data_event->seqno);
	}

	dest_queue = q_ctx->sched.local_q_hdl;
	data_event->src = queue;

	if (unlikely(core_events == 0))
		cstat->begin_cycles = env_get_cycle();
	core_events++;

	cstat->events = core_events;
	cstat->pt_count[eo_ctx->pair_type] += 1;

	ret = em_send(event, dest_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag, "EO-B em_send failure");
	}

	if (VERIFY_ATOMIC_ACCESS)
		verify_atomic_access__end(eo_ctx);
}

/**
 * @private
 *
 * EO receive function for EOs handling the local queues.
 *
 * Forwards events to the next processing stage (EO).
 */
static void
receive_locq(void *eo_context, em_event_t event, em_event_type_t type,
	     em_queue_t queue, void *queue_context)
{
	eo_context_t *const eo_ctx = eo_context;
	queue_context_t *const q_ctx = queue_context;
	test_event_t *const test_event = em_event_pointer(event);
	data_event_t *data_event;
	em_queue_t dest_queue;
	em_status_t ret;
	uint64_t queue_events;

	(void)type;
	(void)queue;
	(void)eo_ctx;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	test_fatal_if(test_event->ev_id != EV_ID_DATA_EVENT,
		      "Unexpected ev-id:%d", test_event->ev_id);
	data_event = &test_event->data;

	queue_events = q_ctx->local.num_events++;

	dest_queue = data_event->dest;

	data_event->dest = data_event->src;
	data_event->src = dest_queue;

	ret = em_send(event, dest_queue);
	if (unlikely(ret != EM_OK)) {
		em_free(event);
		test_fatal_if(!appl_shm->exit_flag, "EO-local em_send failure");
	}

	if (CALL_ATOMIC_PROCESSING_END) {
		/* Call em_atomic_processing_end() every once in a while */
		if (queue_events % qtypes_shm->num_queues == q_ctx->idx)
			em_atomic_processing_end();
	}
}

static pair_type_t
get_pair_type(queue_type_pair_t *queue_type_pair)
{
	em_queue_type_t qt1 = queue_type_pair->q_type[0];
	em_queue_type_t qt2 = queue_type_pair->q_type[1];
	int in_ag1 = queue_type_pair->in_atomic_group[0];
	int in_ag2 = queue_type_pair->in_atomic_group[1];

	switch (qt1) {
	case EM_QUEUE_TYPE_ATOMIC:
		switch (qt2) {
		case EM_QUEUE_TYPE_ATOMIC:
			if (in_ag1 && in_ag2)
				return PT_AG_AG;
			else if (in_ag1 || in_ag2)
				return PT_AG_ATOMIC;
			else
				return PT_ATOMIC_ATOMIC;

		case EM_QUEUE_TYPE_PARALLEL:
			if (in_ag1)
				return PT_AG_PARALLEL;
			else
				return PT_PARALLEL_ATOMIC;

		case EM_QUEUE_TYPE_PARALLEL_ORDERED:
			if (in_ag1)
				return PT_AG_PARALORD;
			else
				return PT_PARALORD_ATOMIC;
		}
		break;

	case EM_QUEUE_TYPE_PARALLEL:
		switch (qt2) {
		case EM_QUEUE_TYPE_ATOMIC:
			if (in_ag2)
				return PT_AG_PARALLEL;
			else
				return PT_PARALLEL_ATOMIC;

		case EM_QUEUE_TYPE_PARALLEL:
			return PT_PARALLEL_PARALLEL;

		case EM_QUEUE_TYPE_PARALLEL_ORDERED:
			return PT_PARALORD_PARALLEL;
		}
		break;

	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
		switch (qt2) {
		case EM_QUEUE_TYPE_ATOMIC:
			if (in_ag2)
				return PT_AG_PARALORD;
			else
				return PT_PARALORD_ATOMIC;

		case EM_QUEUE_TYPE_PARALLEL:
			return PT_PARALORD_PARALLEL;

		case EM_QUEUE_TYPE_PARALLEL_ORDERED:
			return PT_PARALORD_PARALORD;
		}
		break;
	}

	return PT_UNDEFINED;
}

static inline void
verify_seqno(eo_context_t *const eo_ctx, queue_context_t *const q_ctx,
	     uint64_t seqno)
{
	if (unlikely(qtypes_shm->teardown_in_progress))
		return;

	if (eo_ctx->owns_ag_queues == eo_ctx->peer_owns_ag_queues) {
		const uint64_t max_seqno = (eo_ctx->owns_ag_queues) ?
					   NUM_EVENT / 3 - 1 : NUM_EVENT - 1;

		if (q_ctx->sched.seqno != seqno) {
			test_error((em_status_t)__LINE__, 0xdead,
				   "SEQUENCE ERROR A:\t"
				   "queue=%" PRI_QUEUE " Q-seqno=%" PRIu64 "\t"
				   "Event-seqno=%" PRIu64 " PT:%i",
				   q_ctx->q_hdl, q_ctx->sched.seqno, seqno,
				   eo_ctx->pair_type);
			exit(EXIT_FAILURE);
		}

		if (q_ctx->sched.seqno < max_seqno)
			q_ctx->sched.seqno++;
		else
			q_ctx->sched.seqno = 0;
	}
}

/**
 * Verifies that each queue processes all its events at least once per
 * statistics round.
 */
static void
verify_all_queues_get_events(void)
{
	const unsigned int num_queues = qtypes_shm->num_queues;
	unsigned int i, first = 1, q_evcnt_low = 0;
	uint64_t curr, prev, diff;

	for (i = 0; i < num_queues; i++) {
		queue_context_t *const tmp_qctx =
			&qtypes_shm->queue_context[i];
		const uint64_t min_events = (tmp_qctx->sched.in_atomic_group) ?
					    NUM_EVENT / 3 : NUM_EVENT;

		if (tmp_qctx->q_type == EM_QUEUE_TYPE_LOCAL) {
			curr = tmp_qctx->local.num_events;
			prev = tmp_qctx->local.prev_events;
			tmp_qctx->local.prev_events = curr;
		} else {
			curr = env_atomic64_get(&tmp_qctx->sched.num_events);
			prev = tmp_qctx->sched.prev_events;
			tmp_qctx->sched.prev_events = curr;
		}

		diff = (curr >= prev) ?
			curr - prev : UINT64_MAX - prev + curr + 1;

		if (unlikely(diff < min_events)) {
			const char *q_type_str;

			q_evcnt_low++;
			if (first) {
				first = 0;
				print_event_msg_string();
			}

			switch (tmp_qctx->q_type) {
			case EM_QUEUE_TYPE_ATOMIC:
				if (tmp_qctx->sched.in_atomic_group)
					q_type_str = "AG";
				else
					q_type_str = "A ";
				break;
			case EM_QUEUE_TYPE_PARALLEL:
				q_type_str = "P ";
				break;
			case EM_QUEUE_TYPE_PARALLEL_ORDERED:
				q_type_str = "PO";
				break;
			case EM_QUEUE_TYPE_LOCAL:
				q_type_str = "L ";
				break;
			default:
				q_type_str = "??";
				break;
			}

			APPL_PRINT("Q=%3" PRI_QUEUE "(%s cnt:%" PRIu64 ") %c",
				   tmp_qctx->q_hdl, q_type_str, diff,
				   (q_evcnt_low % 8 == 0) ? '\n' : ' ');
		}
	}

	if (!first)
		APPL_PRINT("\nQueue count with too few events:%u\n\n",
			   q_evcnt_low);
}

/**
 * Try to take a spinlock and if it fails we know that another core is
 * processing an event from the same atomic queue or atomic group, which
 * should never happen => fatal error! The lock is for verification only,
 * no sync purpose whatsoever.
 */
static inline void
verify_atomic_access__begin(eo_context_t *const eo_ctx)
{
	if (unlikely(eo_ctx->q_type == EM_QUEUE_TYPE_ATOMIC &&
		     !env_spinlock_trylock(&eo_ctx->verify_atomic_access)))
		test_error(EM_ERROR_SET_FATAL(__LINE__), 0xdead,
			   "EO Atomic context lost!");
}

/**
 *  Release the verification lock
 */
static inline void
verify_atomic_access__end(eo_context_t *const eo_ctx)
{
	if (unlikely(eo_ctx->q_type == EM_QUEUE_TYPE_ATOMIC))
		env_spinlock_unlock(&eo_ctx->verify_atomic_access);
}

/**
 * Print core specific statistics
 */
static void
print_core_stats(core_stat_t *const cstat, uint64_t print_events)
{
	uint64_t diff;
	uint32_t hz;
	double mhz;
	double cycles_per_event;
	uint64_t print_count;

	if (cstat->end_cycles > cstat->begin_cycles)
		diff = cstat->end_cycles - cstat->begin_cycles;
	else
		diff = UINT64_MAX - cstat->begin_cycles + cstat->end_cycles + 1;

	print_count = cstat->print_count++;
	cycles_per_event = (double)diff / (double)print_events;

	hz = env_core_hz();
	mhz = ((double)hz) / 1000000.0;

	APPL_PRINT(PRINT_CORE_STAT_FMT, em_core_id(),
		   cstat->pt_count[0], cstat->pt_count[1], cstat->pt_count[2],
		   cstat->pt_count[3], cstat->pt_count[4], cstat->pt_count[5],
		   cstat->pt_count[6], cstat->pt_count[7], cstat->pt_count[8],
		   cstat->pt_count[9], cycles_per_event, mhz, print_count);
}

static void
print_event_msg_string(void)
{
	APPL_PRINT("\nToo few events detected for the following queues:\n");
}
