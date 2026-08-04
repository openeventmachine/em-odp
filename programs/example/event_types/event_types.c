/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Nokia
 */

/**
 * @file
 *
 * Event types test application.
 *
 * Test sending both EM events and non-EM events (i.e. native ODP events not
 * wrapped by EM) through various queue types.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>
#include <event_machine/platform/event_machine_odp_ext.h>
#include <odp_api.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

/**
 * The event_types event
 */
typedef struct {
	/* Sequence number */
	unsigned int seq;
} event_types_event_t;

/**
 * EO context in the event_types test
 */
typedef struct {
	/* Init before start */
	em_eo_t this_eo;
	/* Init in start */
	char name[EM_EO_NAME_LEN];
	/* Scheduled queues */
	em_queue_t queue_atomic;
	em_queue_t queue_parallel;
	em_queue_t queue_ordered;
	/* Unscheduled queue */
	em_queue_t queue_unsched;
	/* Local queue */
	em_queue_t queue_local;
	/* Atomic group & queues */
	em_atomic_group_t atomic_group;
	em_queue_t queue_agrp1;
	em_queue_t queue_agrp2;
	/* Output queue */
	em_queue_t queue_output;
	/* Pad to cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} my_eo_context_t;

typedef struct {
	my_eo_context_t *eo_ctx;
} my_output_fn_args_t;

/**
 * Event_types shared memory
 */
typedef struct {
	odp_pool_t ml_pool;
	odp_pool_t dma_pool;
	/* Event pool used by this application */
	em_pool_t pool_sw;
	em_pool_t pool_pkt;
	/* EM Atomic Group */
	em_atomic_group_t atomic_group;
	/* EO contexts */
	my_eo_context_t eo_context[2];
	/* Pad to cache line size */
	void *end[0] ENV_CACHE_LINE_ALIGNED;
} event_types_shm_t;

COMPILE_TIME_ASSERT((sizeof(event_types_shm_t) % ENV_CACHE_LINE_SIZE) == 0,
		    EVENT_TYPES_SHM_T__SIZE_ERROR);

/* EM-core local pointer to shared memory */
static ENV_LOCAL event_types_shm_t *event_types_shm;

static em_status_t eo_start(void *eo_ctx, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t eo_stop(void *eo_ctx, em_eo_t eo);
static void eo_single_receive(void *eo_ctx, em_event_t event,
			      em_event_type_t type, em_queue_t queue, void *q_ctx);
static void eo_multi_receive(void *eo_ctx, em_event_t events[], int num,
			     em_queue_t queue, void *q_ctx);
static int output_queue_fn(const em_event_t events[], const unsigned int num,
			   const em_queue_t output_queue, void *output_fn_args);

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
 * Init of the event_types test application.
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_init(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	int core = em_core_id();

	if (core == 0) {
		event_types_shm = env_shared_reserve("Event_typesSharedMem",
						     sizeof(event_types_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		event_types_shm = env_shared_lookup("Event_typesSharedMem");
	}

	if (event_types_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "event_types init failed on EM-core: %u",
			   em_core_id());
	} else if (core == 0) {
		memset(event_types_shm, 0, sizeof(event_types_shm_t));
	}
}

/**
 * Startup of the event_types test application.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_start(const appl_conf_t *appl_conf)
{
	em_eo_t eo_a;
	em_eo_t eo_b;
	em_status_t ret;
	em_status_t start_ret = EM_ERROR;

	odp_ml_compl_pool_param_t ml_pool_params;
	odp_pool_t ml_pool;
	odp_dma_pool_param_t dma_pool_params;
	odp_pool_t dma_pool;

	/*
	 * Store the event pool to use, use the EM default pool if no other
	 * pool is provided through the appl_conf.
	 */
	em_pool_t pool1;

	if (appl_conf->num_pools >= 1)
		pool1 = appl_conf->pools[0];
	else
		pool1 = EM_POOL_DEFAULT;

	em_pool_info_t pool_info;
	em_status_t status = em_pool_info(pool1, &pool_info);

	test_fatal_if(status != EM_OK,
		      "Pool info fails, pool:%" PRI_POOL " status:%" PRI_STAT "",
		      pool1, status);

	em_pool_t pool2;
	em_event_type_t pool2_type;

	if (pool_info.event_type == EM_EVENT_TYPE_SW) {
		event_types_shm->pool_sw = pool1;
		pool2_type = EM_EVENT_TYPE_PACKET;
	} else {
		event_types_shm->pool_pkt = pool1;
		pool2_type = EM_EVENT_TYPE_SW;
	}
	em_pool_cfg_t pool_cfg;

	em_pool_cfg_init(&pool_cfg);
	pool_cfg.event_type = pool2_type;
	pool_cfg.num_subpools = 1;
	pool_cfg.subpool[0].size = 64; /* 64 bytes payload */
	pool_cfg.subpool[0].num = 1000; /* 1000 events */
	pool_cfg.subpool[0].cache_size = 100; /* 100 events cached locally */

	pool2 = em_pool_create("Pool-2", EM_POOL_UNDEF, &pool_cfg);
	test_fatal_if(pool2 == EM_POOL_UNDEF, "Pool-2 creation failed!");

	if (pool2_type == EM_EVENT_TYPE_SW)
		event_types_shm->pool_sw = pool2;
	else
		event_types_shm->pool_pkt = pool2;

	APPL_PRINT("\n"
		   "***********************************************************\n"
		   "EM APPLICATION: '%s' initializing:\n"
		   "  %s: %s() - EM-core:%d\n"
		   "  Application running on %u EM-cores (procs:%u, threads:%u)\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   appl_conf->core_count, appl_conf->num_procs, appl_conf->num_threads);

	/* check if ODP ML COMPL pool can be created */
	odp_ml_capability_t capa_ml;
	int rv;

	rv = odp_ml_capability(&capa_ml);
	if (rv != 0)
		APPL_EXIT_FAILURE("odp_ml_capability() did not succeed");

	if (capa_ml.pool.max_num > 0) {
		/* create ODP ML COMPL pool with user area */
		odp_ml_compl_pool_param_init(&ml_pool_params);
		ml_pool_params.num = 100;
		ml_pool_params.uarea_size = em_odp_event_hdr_size();
		ml_pool = odp_ml_compl_pool_create("MLComplPool", &ml_pool_params);

		if (ml_pool == ODP_POOL_INVALID)
			APPL_EXIT_FAILURE("ODP MLcompl pool create failed");
		event_types_shm->ml_pool = ml_pool;
	} else {
		APPL_PRINT("ODP MLcompl pool not supported\n");
		event_types_shm->ml_pool = ODP_POOL_INVALID;
	}

	/* check if ODP DMA COMPL pool can be created */
	odp_dma_capability_t capa_dma;

	rv = odp_dma_capability(&capa_dma);
	if (rv != 0)
		APPL_EXIT_FAILURE("odp_dma_capability() did not succeed");

	if (capa_dma.pool.max_num > 0) {
		/* create DMA COMPL pool without user area */
		odp_dma_pool_param_init(&dma_pool_params);
		dma_pool_params.num = 100;
		dma_pool = odp_dma_pool_create("DMAComplPool", &dma_pool_params);

		if (dma_pool == ODP_POOL_INVALID)
			APPL_EXIT_FAILURE("ODP DMAcompl pool create failed");
		event_types_shm->dma_pool = dma_pool;
	} else {
		APPL_PRINT("ODP DMAcompl pool not supported\n");
		event_types_shm->dma_pool = ODP_POOL_INVALID;
	}

	/* Create an Atomic Group */
	em_atomic_group_t atomic_group = em_atomic_group_create("AtomicGroup",
								EM_QUEUE_GROUP_DEFAULT);
	test_fatal_if(atomic_group == EM_ATOMIC_GROUP_UNDEF, "Atomic Group create failed!");
	event_types_shm->atomic_group = atomic_group;

	/* Create single-receive EO */
	eo_a = em_eo_create("EO A", eo_start, NULL,
			    eo_stop, NULL,
			    eo_single_receive, &event_types_shm->eo_context[0]);

	test_fatal_if(eo_a == EM_EO_UNDEF, "EO A creation failed!");

	event_types_shm->eo_context[0].this_eo = eo_a;

	ret = em_eo_start_sync(eo_a, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO A start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);

	/* Create multi-receive EO */
	em_eo_multircv_param_t eo_param;

	/* Init EO params */
	em_eo_multircv_param_init(&eo_param);
	/* Set EO params needed by this application */
	eo_param.start = eo_start;
	eo_param.local_start = NULL;
	eo_param.stop = eo_stop;
	eo_param.receive_multi = eo_multi_receive;
	eo_param.max_events = EM_EO_MULTIRCV_MAX_EVENTS;
	eo_param.eo_ctx = &event_types_shm->eo_context[1];

	eo_b = em_eo_create_multircv("EO B", &eo_param);
	test_fatal_if(eo_b == EM_EO_UNDEF, "EO B creation failed!");

	/* Init EO contexts */
	event_types_shm->eo_context[1].this_eo = eo_b;

	/* Start EO B */
	ret = em_eo_start_sync(eo_b, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO B start:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_ret);
}

void test_stop(const appl_conf_t *appl_conf)
{
	int core = em_core_id();
	em_eo_t eo_a = event_types_shm->eo_context[0].this_eo;
	em_eo_t eo_b = event_types_shm->eo_context[1].this_eo;
	em_atomic_group_t atomic_group = event_types_shm->atomic_group;

	em_status_t stat;
	int err;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	/*
	 * Stop both EOs, this will also disable all added queues.
	 * The EO-stop function 'eo_stop()' will be called for each EO
	 * once the asynchronous em_eo_stop() operation has been completed.
	 */
	stat = em_eo_stop_sync(eo_a);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO A stop failed!");

	stat = em_eo_stop_sync(eo_b);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO B stop failed!");

	stat = em_atomic_group_delete(atomic_group);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Atomic group delete failed!");

	if (event_types_shm->ml_pool != ODP_POOL_INVALID) {
		err = odp_pool_destroy(event_types_shm->ml_pool);
		if (err)
			APPL_EXIT_FAILURE("ODP ML-completion pool destroy failed!");
	}

	if (event_types_shm->dma_pool != ODP_POOL_INVALID) {
		err = odp_pool_destroy(event_types_shm->dma_pool);
		if (err)
			APPL_EXIT_FAILURE("ODP DMA-completion pool destroy failed!");
	}

	stat = em_pool_delete(event_types_shm->pool_sw);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Pool-2 delete failed!");
	stat = em_pool_delete(event_types_shm->pool_pkt);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Pool-2 delete failed!");
}

void test_term(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(event_types_shm);
		em_unregister_error_handler();
	}
}

/**
 * @private
 *
 * EO start function.
 */
static em_status_t eo_start(void *eo_ctx_, em_eo_t eo, const em_eo_conf_t *conf)
{
	my_eo_context_t *eo_ctx = eo_ctx_;
	em_status_t status;
	const char *queue_name;
	em_queue_t queue_atomic;
	em_queue_t queue_parallel;
	em_queue_t queue_ordered;
	em_queue_t queue_unsched;
	em_queue_t queue_local;
	em_queue_t queue_agrp1;
	em_queue_t queue_agrp2;
	em_queue_t queue_output;

	(void)conf;

	/* Copy EO name */
	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	queue_name = "queue-atomic";
	queue_atomic = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC,
				       EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT, NULL);
	test_fatal_if(queue_atomic == EM_QUEUE_UNDEF, "%s creation failed!", queue_name);

	queue_name = "queue-parallel";
	queue_parallel = em_queue_create(queue_name, EM_QUEUE_TYPE_PARALLEL,
					 EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT, NULL);
	test_fatal_if(queue_parallel == EM_QUEUE_UNDEF, "%s creation failed!", queue_name);

	queue_name = "queue-ordered";
	queue_ordered = em_queue_create(queue_name, EM_QUEUE_TYPE_ORDERED,
					EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT, NULL);
	test_fatal_if(queue_ordered == EM_QUEUE_UNDEF, "%s creation failed!", queue_name);

	status = em_eo_add_queue_sync(eo, queue_atomic);
	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue_atomic);
	status = em_eo_add_queue_sync(eo, queue_parallel);
	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue_parallel);
	status = em_eo_add_queue_sync(eo, queue_ordered);
	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue_ordered);

	queue_name = "queue-unscheduled";
	queue_unsched = em_queue_create(queue_name, EM_QUEUE_TYPE_UNSCHEDULED,
					EM_QUEUE_PRIO_UNDEF, EM_QUEUE_GROUP_UNDEF, NULL);
	test_fatal_if(queue_unsched == EM_QUEUE_UNDEF, "%s creation failed!", queue_name);

	queue_name = "queue-local";
	queue_local = em_queue_create(queue_name, EM_QUEUE_TYPE_LOCAL,
				      EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_UNDEF, NULL);
	test_fatal_if(queue_local == EM_QUEUE_UNDEF, "%s creation failed!", queue_name);

	status = em_eo_add_queue_sync(eo, queue_local);
	test_fatal_if(status != EM_OK, "EO add queue:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue_local);

	/* Create Atomic group queue(s) */
	em_atomic_group_t atomic_group = event_types_shm->atomic_group;

	queue_agrp1 = em_queue_create_ag("queue-agrp1", EM_QUEUE_PRIO_HIGHEST, atomic_group, NULL);
	test_fatal_if(queue_agrp1 == EM_QUEUE_UNDEF, "Queue-agrp1 creation failed!");
	queue_agrp2 = em_queue_create_ag("queue-agrp2", EM_QUEUE_PRIO_LOWEST, atomic_group, NULL);
	test_fatal_if(queue_agrp2 == EM_QUEUE_UNDEF, "Queue-agrp2 creation failed!");

	status = em_eo_add_queue_sync(eo, queue_agrp1);
	test_fatal_if(status != EM_OK, "EO add queue-agrp1:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue_agrp1);
	status = em_eo_add_queue_sync(eo, queue_agrp2);
	test_fatal_if(status != EM_OK, "EO add queue-agrp2:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "", status, eo, queue_agrp2);

	/* Create an EM output queue */
	em_queue_conf_t queue_conf;
	em_output_queue_conf_t output_conf;
	my_output_fn_args_t output_args;

	memset(&queue_conf, 0, sizeof(queue_conf));
	memset(&output_conf, 0, sizeof(output_conf));
	queue_conf.flags = EM_QUEUE_FLAG_DEFAULT;
	queue_conf.min_events = 0; /* system default */
	queue_conf.conf_len = sizeof(output_conf);
	queue_conf.conf = &output_conf;

	/* Output-queue callback function (em_output_func_t) */
	output_conf.output_fn = output_queue_fn;
	/* Callback function argument, pass the eo_ctx pointer */
	output_args.eo_ctx = eo_ctx;
	output_conf.output_fn_args = &output_args;
	output_conf.args_len = sizeof(output_args);

	queue_output = em_queue_create("queue-output", EM_QUEUE_TYPE_OUTPUT,
				       EM_QUEUE_PRIO_UNDEF, EM_QUEUE_GROUP_UNDEF,
				       &queue_conf);
	test_fatal_if(queue_output == EM_QUEUE_UNDEF, "Queue-output creation failed!");

	/* Save the queue handles */
	eo_ctx->queue_atomic = queue_atomic;
	eo_ctx->queue_parallel = queue_parallel;
	eo_ctx->queue_ordered = queue_ordered;
	eo_ctx->queue_unsched = queue_unsched;
	eo_ctx->queue_local = queue_local;
	eo_ctx->queue_agrp1 = queue_agrp1;
	eo_ctx->queue_agrp2 = queue_agrp2;
	eo_ctx->queue_output = queue_output;

	/* Allocate and send an ODP ML completion event if pool exists */
	if (event_types_shm->ml_pool != ODP_POOL_INVALID) {
		odp_ml_compl_t mlcompl;

		mlcompl = odp_ml_compl_alloc(event_types_shm->ml_pool);
		test_fatal_if(mlcompl == ODP_ML_COMPL_INVALID, "odp_ml_compl_alloc() failed!\n");
		APPL_PRINT("%s: ML event allocated!\n", eo_ctx->name);

		em_event_t mlcomp_event;

		mlcomp_event = em_odp_event2em(odp_ml_compl_to_event(mlcompl));
		status = em_send(mlcomp_event, eo_ctx->queue_atomic);
		test_fatal_if(status != EM_OK, "em_send():%" PRI_STAT "\n"
			"EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			status, eo, eo_ctx->queue_atomic);
		APPL_PRINT("%s: ML event sent!\n", eo_ctx->name);
	}

	/* Allocate and send an ODP DMA completion event if pool exists */
	if (event_types_shm->dma_pool != ODP_POOL_INVALID) {
		odp_dma_compl_t dmacompl;

		dmacompl = odp_dma_compl_alloc(event_types_shm->dma_pool);

		APPL_PRINT("%s: DMA event allocated!\n", eo_ctx->name);
		em_event_t dmacomp_event;

		dmacomp_event = em_odp_event2em(odp_dma_compl_to_event(dmacompl));
		status = em_send(dmacomp_event, eo_ctx->queue_atomic);
		test_fatal_if(status != EM_OK, "em_send():%" PRI_STAT "\n"
			"EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			status, eo, eo_ctx->queue_atomic);
		APPL_PRINT("%s: DMA event sent!\n", eo_ctx->name);
	}

	em_event_t event;
	event_types_event_t *test_event;

	/* Allocate and send an EM SW event */
	event = em_alloc(sizeof(event_types_event_t), EM_EVENT_TYPE_SW,
			 event_types_shm->pool_sw);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");

	test_event = em_event_pointer(event);
	test_event->seq = 0;
	status = em_send(event, eo_ctx->queue_atomic);
	test_fatal_if(status != EM_OK, "em_send():%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      status, eo, eo_ctx->queue_atomic);
	APPL_PRINT("%s: SW event sent!\n", eo_ctx->name);

	/* Allocate and send an EM PACKET event */
	event = em_alloc(sizeof(event_types_event_t), EM_EVENT_TYPE_PACKET,
			 event_types_shm->pool_pkt);
	test_fatal_if(event == EM_EVENT_UNDEF, "Event allocation failed!");

	test_event = em_event_pointer(event);
	test_event->seq = 0;
	status = em_send(event, eo_ctx->queue_atomic);
	test_fatal_if(status != EM_OK, "em_send():%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      status, eo, eo_ctx->queue_atomic);
	APPL_PRINT("%s: PACKET event sent!\n", eo_ctx->name);

	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t eo_stop(void *eo_ctx_, em_eo_t eo)
{
	my_eo_context_t *eo_ctx = eo_ctx_;
	em_status_t stat = EM_OK;

	APPL_PRINT("event_types stop on EM-core %d (%s, eo id %" PRI_EO ")\n",
		   em_core_id(), eo_ctx->name, eo);

	stat = em_eo_remove_queue_all_sync(eo, EM_TRUE /*delete Qs*/);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO remove queue all sync failed!");

	stat = em_queue_delete(eo_ctx->queue_unsched);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Unsched queue delete failed!");

	stat = em_queue_delete(eo_ctx->queue_output);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("Output queue delete failed!");

	stat = em_eo_delete(eo);
	if (stat != EM_OK)
		APPL_EXIT_FAILURE("EO delete failed!");

	return stat;
}

/**
 * @private
 *
 * EO receive function.
 *
 * Print "event_types" and send back to the sender of the event.
 */
static void eo_single_receive(void *eo_ctx_, em_event_t event, em_event_type_t type,
			      em_queue_t queue, void *q_ctx)
{
	my_eo_context_t *eo_ctx = eo_ctx_;

	(void)type;
	(void)q_ctx;

	int core = em_core_id();
	em_status_t status;
	const char *queue_info = "";
	em_queue_t queue_next = EM_QUEUE_UNDEF;
	bool is_scheduled = true;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	if (queue == eo_ctx->queue_atomic) {
		queue_info = "atomic";
		queue_next = eo_ctx->queue_parallel;
	} else if (queue == eo_ctx->queue_parallel) {
		queue_info = "parallel";
		queue_next = eo_ctx->queue_ordered;
	} else if (queue == eo_ctx->queue_ordered) {
		queue_info = "ordered";
		queue_next = eo_ctx->queue_agrp1;
	} else if (queue == eo_ctx->queue_agrp1) {
		queue_info = "agrp:Q1";
		queue_next = eo_ctx->queue_agrp2;
	} else if (queue == eo_ctx->queue_agrp2) {
		queue_info = "agrp:Q2";
		queue_next = eo_ctx->queue_local;
		is_scheduled = false;
	} else if (queue == eo_ctx->queue_local) {
		queue_info = "local";
		queue_next = eo_ctx->queue_output;
		is_scheduled = false;
	} else {
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "%s(): Unknown Q:%" PRI_QUEUE "", __func__, queue);
	}

	em_event_type_t event_type = em_event_get_type(event);
	em_event_type_t major_type = em_event_type_major(event_type);

	switch (major_type) {
	case EM_EVENT_TYPE_ODP: {
		odp_event_t odp_event = em_odp_event2odp(event);
		odp_event_type_t odp_type = odp_event_type(odp_event);

		if (odp_type == ODP_EVENT_ML_COMPL) {
			printf("EM-core%02d, %s: Event from Q:%" PRI_QUEUE "(%s):\tODP ML COMPL\n",
			       core, eo_ctx->name, queue, queue_info);
		} else if (odp_type == ODP_EVENT_DMA_COMPL) {
			printf("EM-core%02d, %s: Event from Q:%" PRI_QUEUE "(%s):\tODP DMA COMPL\n",
			       core, eo_ctx->name, queue, queue_info);
		} else {
			test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
				   "Unexpected ODP event type: %u", odp_type);
		}
		break;
	}

	case EM_EVENT_TYPE_SW:
		printf("EM-core%02d, %s: Event from Q:%" PRI_QUEUE "(%s):\tEM SW event\n",
		       core, eo_ctx->name, queue, queue_info);
		break;

	case EM_EVENT_TYPE_PACKET:
		printf("EM-core%02d, %s: Event from Q:%" PRI_QUEUE "(%s):\tEM PACKET event\n",
		       core, eo_ctx->name, queue, queue_info);
		break;
	default:
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Unexpected EM event type: %u", event_type);
	}

	em_queue_t queue_unsched = eo_ctx->queue_unsched;

	status = em_send(event, queue_unsched);
	test_fatal_if(status != EM_OK,
		      "em_send():%" PRI_STAT " to unscheduled queue failed!\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      status, eo_ctx->this_eo, queue_unsched);

	odp_time_wait_ns(ODP_TIME_SEC_IN_NS / 10); /* 0.1 seconds */

	em_event_t event_deq = em_queue_dequeue(queue_unsched);

	test_fatal_if(event_deq == EM_EVENT_UNDEF,
		      "em_queue_dequeue() from unscheduled queue failed!");

	if (is_scheduled) {
		status = em_event_mark_send(event_deq, queue_next);
		test_fatal_if(status != EM_OK,
			      "em_event_mark_send():%" PRI_STAT " failed!\n"
			      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
			      status, eo_ctx->this_eo, queue_next);
		status = em_event_unmark_send(event_deq);
		test_fatal_if(status != EM_OK,
			      "em_event_unmark_send():%" PRI_STAT " failed!\n"
			      "EO:%" PRI_EO "", status, eo_ctx->this_eo);
	}

	em_event_mark_free(event_deq);
	em_event_unmark_free(event_deq);
	em_event_mark_free_multi(&event_deq, 1);
	em_event_unmark_free_multi(&event_deq, 1);

	status = em_send(event_deq, queue_next);

	if (unlikely(status != EM_OK)) {
		if (em_event_get_type(event_deq) == EM_EVENT_TYPE_SW) {
			em_free(event_deq);
			test_fatal_if(!appl_shm->exit_flag,
				      "em_send():%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
				      status, eo_ctx->this_eo, queue_next);
		}
	}
}

static void eo_multi_receive(void *eo_ctx_, em_event_t events[], int num,
			     em_queue_t queue, void *q_ctx)
{
	for (int i = 0; i < num; i++) {
		em_event_t event = events[i];
		em_event_type_t type = em_event_get_type(event);

		eo_single_receive(eo_ctx_, event, type, queue, q_ctx);
	}
}

/* Output function prototype of type 'em_output_func_t'  */
static int output_queue_fn(const em_event_t events[], const unsigned int num,
			   const em_queue_t output_queue, void *output_fn_args)
{
	(void)events;
	(void)num;
	(void)output_queue;
	my_output_fn_args_t *output_args = (my_output_fn_args_t *)output_fn_args;

	my_eo_context_t *eo_ctx = output_args->eo_ctx;

	printf("EM-core%02d, %s: Event from Q:%" PRI_QUEUE "(output)\n",
	       em_core_id(), eo_ctx->name, output_queue);

	int sent = em_send_multi(events, num, eo_ctx->queue_atomic);

	return sent;
}
