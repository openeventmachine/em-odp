/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2022, Nokia Solutions and Networks
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
 * Simple Load Balanced Packet-IO L2 forward/loopback application.
 *
 * An application (EO) that receives ETH frames and sends them back.
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"
#include "cm_pktio.h"

/*
 * Test configuration
 */

#define MAX_NUM_IF 4 /* max number of used interfaces */
#define MAX_IF_ID  6 /* max interface identifier:[0-MAX], cnt:MAX+1 */

#define NUM_PKTIN_QUEUES          (2 * EM_MAX_CORES)
#define MAX_PKTOUT_QUEUES_PER_IF       EM_MAX_CORES

/**
 * EO context
 */
typedef struct {
	em_eo_t eo;
	char name[32];
	/** interface count as provided by appl_conf to test_start() */
	int if_count;
	/** interface ids as provided via appl_conf_t to test_start() */
	int if_ids[MAX_NUM_IF];
	/** the number of packet output queues to use per interface */
	int pktout_queues_per_if;
	/* pktout queues: accessed by if_id, thus empty middle slots possible */
	em_queue_t pktout_queue[MAX_IF_ID + 1][MAX_PKTOUT_QUEUES_PER_IF];
} eo_context_t;

/**
 * Queue-Context, i.e. queue specific data, each queue has its own instance
 */
typedef struct {
	/** a pktout queue for each interface, precalculated */
	em_queue_t pktout_queue[MAX_IF_ID + 1];
	/** queue handle */
	em_queue_t queue;
} queue_context_t;

/**
 * Packet L2fwd shared memory
 */
typedef struct {
	/** EO (application) context */
	eo_context_t eo_ctx;
	/**
	 * Array containing the contexts of all the queues handled by the EO.
	 * A queue context contains the flow/queue specific data for the
	 * application EO.
	 */
	queue_context_t eo_q_ctx[NUM_PKTIN_QUEUES] ENV_CACHE_LINE_ALIGNED;

	/** Ptr to the cm_pktio shared mem */
	pktio_shm_t *pktio_shm;
} l2fwd_shm_t;

/** EM-core local pointer to shared memory */
static ENV_LOCAL l2fwd_shm_t *l2fwd_shm;

static em_status_t
start_eo(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static void
set_pktout_queues(em_queue_t queue, eo_context_t *const eo_ctx,
		  em_queue_t pktout_queue[/*out*/]);

static em_status_t
start_eo_local(void *eo_context, em_eo_t eo);

static void
receive_eo_event_multi(void *eo_ctx,
		       em_event_t events[], int num,
		       em_queue_t queue, void *q_ctx);
static inline void
receive_vector(em_event_t vector, const queue_context_t *q_ctx);

static inline void
receive_vector_multi(em_event_t vectors[], int num,
		     const queue_context_t *q_ctx);
static inline void
receive_packet_multi(em_event_t events[], int num,
		     const queue_context_t *q_ctx);

static em_status_t
stop_eo(void *eo_context, em_eo_t eo);

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
 * Init of the Packet Loopback test application.
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_init(void)
{
	int core = em_core_id();

	if (core == 0) {
		l2fwd_shm = env_shared_reserve("PktL2fwdShMem",
					       sizeof(l2fwd_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		l2fwd_shm = env_shared_lookup("PktL2fwdShMem");
	}

	if (l2fwd_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Packet Loopback init failed on EM-core: %u",
			   em_core_id());
	else if (core == 0) {
		memset(l2fwd_shm, 0, sizeof(l2fwd_shm_t));

		odp_shm_t shm = odp_shm_lookup("pktio_shm");
		void *shm_addr = odp_shm_addr(shm);

		test_fatal_if(shm_addr == NULL,
			      "pktio shared mem addr lookup failed");
		l2fwd_shm->pktio_shm = shm_addr;
	}
}

/**
 * Startup of the Packet Loopback test application.
 *
 * @attention Run only on EM core 0.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	eo_context_t *eo_ctx;
	em_status_t ret, start_fn_ret = EM_ERROR;
	int if_id, if_qcnt, i;

	APPL_PRINT("\n"
		   "***********************************************************\n"
		   "EM APPLICATION: '%s' initializing:\n"
		   "  %s: %s() - EM-core:%i\n"
		   "  Application running on %d EM-cores (procs:%d, threads:%d)\n"
		   "***********************************************************\n"
		   "\n",
		   appl_conf->name, NO_PATH(__FILE__), __func__, em_core_id(),
		   em_core_count(),
		   appl_conf->num_procs, appl_conf->num_threads);

	test_fatal_if(appl_conf->pktio.if_count > MAX_NUM_IF ||
		      appl_conf->pktio.if_count <= 0,
		      "Invalid number of interfaces given:%d - need 1-%d(MAX)",
		      appl_conf->pktio.if_count, MAX_NUM_IF);

	pktin_mode_t pktin_mode = appl_conf->pktio.in_mode;

	test_fatal_if(!pktin_sched_mode(pktin_mode),
		      "Invalid pktin-mode: %s(%i).\n"
		      "Application:%s supports only scheduled pktin-modes: %s(%i), %s(%i), %s(%i)",
		      pktin_mode_str(pktin_mode), pktin_mode,
		      appl_conf->name,
		      pktin_mode_str(SCHED_PARALLEL), SCHED_PARALLEL,
		      pktin_mode_str(SCHED_ATOMIC), SCHED_ATOMIC,
		      pktin_mode_str(SCHED_ORDERED), SCHED_ORDERED);

	/*
	 * Create one EO
	 */
	eo_ctx = &l2fwd_shm->eo_ctx;
	/* Initialize EO context data to '0' */
	memset(eo_ctx, 0, sizeof(eo_context_t));

	em_eo_multircv_param_t eo_param;

	/* Init EO params */
	em_eo_multircv_param_init(&eo_param);
	/* Set EO params needed by this application */
	eo_param.start = start_eo;
	eo_param.local_start = start_eo_local;
	eo_param.stop = stop_eo;
	eo_param.receive_multi = receive_eo_event_multi;
	eo_param.max_events = 0; /* use default */
	eo_param.eo_ctx = eo_ctx;

	eo = em_eo_create_multircv(appl_conf->name, &eo_param);
	test_fatal_if(eo == EM_EO_UNDEF, "em_eo_create() failed");
	eo_ctx->eo = eo;

	/* Store the number of pktio interfaces used */
	eo_ctx->if_count = appl_conf->pktio.if_count;
	/* Store the used interface ids, check number of pktin queues */
	for (i = 0; i < appl_conf->pktio.if_count; i++) {
		if_id = appl_conf->pktio.if_ids[i];
		test_fatal_if(if_id > MAX_IF_ID,
			      "Interface id out of range! %d > %d(MAX)",
			      if_id, MAX_IF_ID);
		eo_ctx->if_ids[i] = if_id;

		if_qcnt = l2fwd_shm->pktio_shm->pktin.num_queues[if_id];
		test_fatal_if(if_qcnt > NUM_PKTIN_QUEUES,
			      "Too many Pktin Queues! %d > %s(MAX)",
			      if_qcnt, NUM_PKTIN_QUEUES);
	}

	/* Start the EO - queues etc. created in the EO start function */
	ret = em_eo_start_sync(eo, &start_fn_ret, NULL);
	test_fatal_if(ret != EM_OK || start_fn_ret != EM_OK,
		      "em_eo_start_sync() failed:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_fn_ret);
}

void test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	eo_context_t *const eo_ctx = &l2fwd_shm->eo_ctx;
	em_eo_t eo = eo_ctx->eo;
	em_status_t ret;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
}

void test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(l2fwd_shm);
		em_unregister_error_handler();
	}
}

/**
 * EO start function (run once at startup on ONE core)
 *
 * The global start function creates the application specific queues and
 * associates the queues with the EO and the packet flows it wants to process.
 */
static em_status_t
start_eo(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_queue_t pktout_queue;
	em_queue_conf_t queue_conf;
	em_output_queue_conf_t output_conf; /* platform specific */
	pktio_tx_fn_args_t pktio_tx_fn_args; /* user defined content */
	em_status_t ret;
	eo_context_t *const eo_ctx = eo_context;
	int if_id;
	int i, j;

	(void)conf;

	/* Store the EO name in the EO-context data */
	em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));

	APPL_PRINT("EO %" PRI_EO ":'%s' global start, if-count:%d\n",
		   eo, eo_ctx->name, eo_ctx->if_count);

	/*
	 * Create packet output queues.
	 *
	 * Dimension the number of pktout queues to be equal to the number
	 * of EM cores per interface to minimize output resource contention.
	 */
	test_fatal_if(em_core_count() >= MAX_PKTOUT_QUEUES_PER_IF,
		      "No room to store pktout queues");
	eo_ctx->pktout_queues_per_if = em_core_count();

	memset(&queue_conf, 0, sizeof(queue_conf));
	memset(&output_conf, 0, sizeof(output_conf));
	queue_conf.flags = EM_QUEUE_FLAG_DEFAULT;
	queue_conf.min_events = 0; /* system default */
	queue_conf.conf_len = sizeof(output_conf);
	queue_conf.conf = &output_conf;

	/* Output-queue callback function (em_output_func_t) */
	output_conf.output_fn = pktio_tx;
	/* Callback function extra argument, here a 'pktio_tx_fn_args_t' ptr */
	output_conf.output_fn_args = &pktio_tx_fn_args;
	output_conf.args_len = sizeof(pktio_tx_fn_args_t);
	/* Content of 'pktio_tx_fn_args' set in loop */

	/* Create the packet output queues for each interface */
	for (i = 0; i < eo_ctx->if_count; i++) {
		if_id = eo_ctx->if_ids[i];
		for (j = 0; j < eo_ctx->pktout_queues_per_if; j++) {
			char qname[EM_QUEUE_NAME_LEN];

			snprintf(qname, sizeof(qname), "pktout-queue-%d-%d", i, j);

			/* pktout queue tied to interface id 'if_id' */
			pktio_tx_fn_args.if_id = if_id;
			pktout_queue =
			em_queue_create(qname, EM_QUEUE_TYPE_OUTPUT,
					EM_QUEUE_PRIO_UNDEF,
					EM_QUEUE_GROUP_UNDEF, &queue_conf);
			test_fatal_if(pktout_queue == EM_QUEUE_UNDEF,
				      "Pktout queue create failed:%d,%d", i, j);
			eo_ctx->pktout_queue[if_id][j] = pktout_queue;
		}
	}

	/* Add pktin queues to the EO */
	int if_cnt = l2fwd_shm->pktio_shm->ifs.count;
	int q_ctx_idx = 0;

	for (int i = 0; i < if_cnt; i++) {
		int if_idx = l2fwd_shm->pktio_shm->ifs.idx[i];
		int if_qcnt = l2fwd_shm->pktio_shm->pktin.num_queues[if_idx];

		for (int q = 0; q < if_qcnt; q++) {
			em_queue_t in_queue =
				l2fwd_shm->pktio_shm->pktin.sched_em_queues[if_idx][q];
			queue_context_t *q_ctx = &l2fwd_shm->eo_q_ctx[q_ctx_idx];

			q_ctx->queue = in_queue;

			ret = em_queue_set_context(in_queue, q_ctx);
			test_fatal_if(ret != EM_OK,
				      "Set Q-ctx failed:%" PRI_STAT "\n"
				      "EO-q-ctx:%d in-Q:%" PRI_QUEUE "",
				      ret, q_ctx_idx, in_queue);

			ret = em_eo_add_queue_sync(eo, in_queue);
			test_fatal_if(ret != EM_OK,
				      "Add in_queue failed:%" PRI_STAT "\n"
				      "EO:%" PRI_EO " in-Q:%" PRI_QUEUE "",
				      ret, eo, in_queue);
			/*
			 * Set the pktout queues to use for this input queue,
			 * one pktout queue per interface.
			 */
			set_pktout_queues(in_queue, eo_ctx, q_ctx->pktout_queue/*out*/);

			q_ctx_idx++;
		}
	}

	APPL_PRINT("EO %" PRI_EO " global start done.\n", eo);

	return EM_OK;
}

/**
 * Helper func to store the packet output queues for a specific input queue
 */
static void
set_pktout_queues(em_queue_t queue, eo_context_t *const eo_ctx,
		  em_queue_t pktout_queue[/*out*/])
{
	int if_count = eo_ctx->if_count;
	int pktout_idx = (uintptr_t)queue % eo_ctx->pktout_queues_per_if;
	int id, i;

	for (i = 0; i < if_count; i++) {
		id = eo_ctx->if_ids[i];
		pktout_queue[id] = eo_ctx->pktout_queue[id][pktout_idx];
	}
}

/**
 * EO Local start function (run once at startup on EACH core)

 * Not really needed in this application, but included
 * to demonstrate usage.
 */
static em_status_t
start_eo_local(void *eo_context, em_eo_t eo)
{
	eo_context_t *eo_ctx = eo_context;

	APPL_PRINT("EO %" PRI_EO ":%s local start on EM-core%u\n",
		   eo, eo_ctx->name, em_core_id());

	return EM_OK;
}

/**
 * EO stop function
 */
static em_status_t
stop_eo(void *eo_context, em_eo_t eo)
{
	eo_context_t *eo_ctx = eo_context;
	em_status_t err;
	em_queue_t pktout_queue;
	int if_id;
	int i, j;

	APPL_PRINT("EO %" PRI_EO ":%s stopping\n", eo, eo_ctx->name);

	/* remove and delete all of the EO's queues */
	err = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(err != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      err, eo);

	/* Delete the packet output queues created for each interface */
	for (i = 0; i < eo_ctx->if_count; i++) {
		if_id = eo_ctx->if_ids[i];
		for (j = 0; j < eo_ctx->pktout_queues_per_if; j++) {
			/* pktout queue tied to interface id 'if_id' */
			pktout_queue = eo_ctx->pktout_queue[if_id][j];
			test_fatal_if(pktout_queue == EM_QUEUE_UNDEF,
				      "Pktout queue undef:%d,%d", i, j);
			err = em_queue_delete(pktout_queue);
			test_fatal_if(err != EM_OK,
				      "Pktout queue delete failed:%d,%d", i, j);
		}
	}

	return EM_OK;
}

static inline void
receive_packet_multi(em_event_t events[], int num,
		     const queue_context_t *q_ctx)
{
	int port = pktio_input_port(events[0]); /* same port for all from same queue */
	em_queue_t pktout_queue = q_ctx->pktout_queue[port];

	/* Touch packet. Swap MAC addresses: scr<->dst */
	for (int i = 0; i < num; i++)
		pktio_swap_eth_addrs(events[i]);

	int sent = em_send_multi(events, num, pktout_queue);

	if (unlikely(sent < num))
		em_free_multi(&events[sent], num - sent);
}

static inline void
receive_vector(em_event_t vector, const queue_context_t *q_ctx)
{
	em_event_t *ev_tbl;

	uint32_t num = em_event_vector_tbl(vector, &ev_tbl);

	if (unlikely(num == 0)) {
		em_event_vector_free(vector);
		return;
	}

	receive_packet_multi(ev_tbl, (int)num, q_ctx);

	em_event_vector_free(vector);
}

static inline void
receive_vector_multi(em_event_t vectors[], int num,
		     const queue_context_t *q_ctx)
{
	for (int i = 0; i < num; i++)
		receive_vector(vectors[i], q_ctx);
}

/**
 * EO's event receive-multi function
 */
static void
receive_eo_event_multi(void *eo_ctx,
		       em_event_t events[], int num,
		       em_queue_t queue, void *queue_context)
{
	const queue_context_t *q_ctx = queue_context;

	(void)eo_ctx;
	(void)queue;

	if (unlikely(appl_shm->exit_flag)) {
		em_free_multi(events, num);
		return;
	}

	em_event_type_t same_type = EM_EVENT_TYPE_UNDEF;
	int num_same;

	for (int i = 0; i < num &&
	     (num_same = em_event_same_type_multi(&events[i], num - i, &same_type)) > 0;
	     i += num_same) {
		em_event_type_t major_type = em_event_type_major(same_type);

		if (likely(major_type == EM_EVENT_TYPE_VECTOR))
			receive_vector_multi(&events[i], num_same, q_ctx);
		else if (likely(major_type == EM_EVENT_TYPE_PACKET))
			receive_packet_multi(&events[i], num_same, q_ctx);
		else
			em_free_multi(&events[i], num_same);
	}
}
