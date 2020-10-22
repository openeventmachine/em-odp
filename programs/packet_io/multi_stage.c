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
 * Load Balanced, multi-staged packet-IO test application.
 *
 * The created UDP flows are received and processed by three (3) chained EOs
 * before sending the datagrams back out. Uses EM queues of different priority
 * and type.
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
#define NUM_IP_ADDRS     4
#define NUM_PORTS_PER_IP 32
#define NUM_FLOWS (NUM_IP_ADDRS * NUM_PORTS_PER_IP)
#define MAX_NUM_IF 4 /* max number of used interfaces */
#define MAX_IF_ID  6 /* max interface identifier:[0-MAX], cnt:MAX+1 */
#define MAX_PKTOUT_QUEUES_PER_IF  EM_MAX_CORES

#define IP_ADDR_A  192
#define IP_ADDR_B  168
#define IP_ADDR_C  1
#define IP_ADDR_D  16

#define IP_ADDR_BASE   ((IP_ADDR_A << 24) | (IP_ADDR_B << 16) | \
			(IP_ADDR_C << 8)  | (IP_ADDR_D))
#define UDP_PORT_BASE  1024

/**
 * The number of different EM queue priority levels to use - fixed.
 */
#define Q_PRIO_LEVELS  1

/**
 * The number of processing stages for a flow, i.e. the number of EO's a
 * packet will go through before being sent back out - fixed.
 */
#define PROCESSING_STAGES  3

/**
 * Test with different scheduled queue types if set to '1':
 * ATOMIC, PARALLELL, PARALLEL_ORDERED
 */
#define QUEUE_TYPE_MIX  1 /* 0=False or 1=True(default) */

/**
 * Set the used Queue-type for the benchmarking cases when using only
 * one queue type, i.e. valid only when QUEUE_TYPE_MIX is '0'
 */
#define QUEUE_TYPE  EM_QUEUE_TYPE_ATOMIC
/* #define QUEUE_TYPE  EM_QUEUE_TYPE_PARALLEL */
/* #define QUEUE_TYPE  EM_QUEUE_TYPE_PARALLEL_ORDERED */

/**
 * The number of Queue Type permutations:
 * Three (3) queue types (ATOMIC, PARALLELL, PARALLEL-ORDERED) in
 * three (3) stages gives 3*3*3 = 27 permutations.
 * Only used if QUEUE_TYPE_MIX is '1'
 */
#define QUEUE_TYPE_PERMUTATIONS  (3 * 3 * 3)

/**
 * Select whether the UDP ports should be unique over all IP-interfaces
 * (set to 1) or reused per IP-interface (thus each UDP port is configured once
 * for each IP-interface). Using '0' (not unique) makes it easier to copy
 * traffic generator settings from one IF-port to another as only the dst-IP
 * address has to be changed.
 */
#define UDP_PORTS_UNIQUE  0 /* 0=False or 1=True */

/** Select whether the input and output ports should be cross-connected. */
#define X_CONNECT_PORTS  0 /* 0=False or 1=True */

/** Enable per packet error checking */
#define ENABLE_ERROR_CHECKS  0 /* 0=False or 1=True */

/**
 * Test em_alloc and em_free per packet
 *
 * Alloc new event, copy event, free old event
 */
#define ALLOC_COPY_FREE 0 /* 0=False or 1=True */

#define IS_ODD(x)   (((x) & 0x1))
#define IS_EVEN(x)  (!IS_ODD(x))

/**
 * EO context, use common struct for all three EOs
 */
typedef struct {
	em_eo_t eo;
	em_queue_t default_queue; /* Only used by the first EO handling pktin */
} eo_context_t;

/**
 * Save the dst IP, protocol and port in the queue-context.
 * Verify (if error checking enabled) that the received packet matches the
 * configuration for the queue.
 */
typedef struct flow_params_ {
	uint32_t ipv4;
	uint16_t port;
	uint8_t proto;
	uint8_t _pad;
} flow_params_t;

/**
 * Queue context, i.e. queue specific data
 */
typedef struct {
	/** saved flow params for the EM-queue */
	flow_params_t flow_params;
	/** The destination queue of the next stage in the pipeline */
	em_queue_t dst_queue;
} queue_context_1st_t;

/**
 * Queue context, i.e. queue specific data
 */
typedef struct {
	em_queue_t dst_queue;
} queue_context_2nd_t;

/**
 * Queue context, i.e. queue specific data
 */
typedef struct {
	/** a pktout queue for each interface, precalculated */
	em_queue_t pktout_queue[MAX_IF_ID + 1];
} queue_context_3rd_t;

/**
 * Queue types used by the three chained EOs processing a flow
 */
typedef struct {
	em_queue_type_t queue_type_1st;
	em_queue_type_t queue_type_2nd;
	em_queue_type_t queue_type_3rd;
	/* Note: 'queue_type_4th' is always 'EM_QUEUE_TYPE_PKTOUT' */
} queue_type_tuple_t;

/**
 * Packet Multi-Stage shared memory
 * Read-only after start-up, no cache-line separation needed.
 */
typedef struct {
	/** EO (application) contexts */
	eo_context_t eo_ctx[PROCESSING_STAGES];
	/**
	 * Arrays containing the contexts of all the queues handled by the EOs.
	 * A queue context contains the flow/queue specific data for the
	 * application EO.
	 */
	queue_context_1st_t eo_q_ctx_1st[NUM_FLOWS];
	queue_context_2nd_t eo_q_ctx_2nd[NUM_FLOWS];
	queue_context_3rd_t eo_q_ctx_3rd[NUM_FLOWS];
	/* pktout queues: accessed by if_id, thus empty middle slots possible */
	em_queue_t pktout_queue[MAX_IF_ID + 1][MAX_PKTOUT_QUEUES_PER_IF];
	/** the number of packet output queues to use per interface */
	int pktout_queues_per_if;
	/** interface count as provided by appl_conf to test_start() */
	int if_count;
	/** interface ids as provided via appl_conf_t to test_start() */
	int if_ids[MAX_NUM_IF];
	/* All possible permutations of the used queue types */
	queue_type_tuple_t q_type_permutations[QUEUE_TYPE_PERMUTATIONS];
} packet_multi_stage_shm_t;

/* EM-core local pointer to shared memory */
static ENV_LOCAL packet_multi_stage_shm_t *pkt_shm;

static em_status_t
mstage_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		     va_list args);
static em_status_t
start_eo(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static em_status_t
start_eo_local(void *eo_context, em_eo_t eo);

static void
receive_packet_eo_1st(void *eo_context, em_event_t event, em_event_type_t type,
		      em_queue_t queue, void *q_ctx);
static void
receive_packet_eo_2nd(void *eo_context, em_event_t event, em_event_type_t type,
		      em_queue_t queue, void *q_ctx);
static void
receive_packet_eo_3rd(void *eo_context, em_event_t event, em_event_type_t type,
		      em_queue_t queue, void *q_ctx);
static em_status_t
stop_eo(void *eo_context, em_eo_t eo);

static em_status_t
stop_eo_local(void *eo_context, em_eo_t eo);

/*
 * Helpers:
 */
static queue_type_tuple_t*
get_queue_type_tuple(int cnt);

static void
fill_q_type_permutations(void);

static em_queue_type_t
queue_types(int cnt);

static void
set_pktout_queues(int q_idx, em_queue_t pktout_queue[/*out*/]);

static inline em_event_t
alloc_copy_free(em_event_t event);

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
 * Init of the Packet Multi-stage test application.
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
		pkt_shm = env_shared_reserve("PktMStageShMem",
					     sizeof(packet_multi_stage_shm_t));
		em_register_error_handler(mstage_error_handler);
	} else {
		pkt_shm = env_shared_lookup("PktMStageShMem");
	}

	if (pkt_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Packet Multi-Stage init failed on EM-core: %u",
			   em_core_id());
	else if (core == 0)
		memset(pkt_shm, 0, sizeof(packet_multi_stage_shm_t));
}

/**
 * Startup of the Packet Multi-stage test application.
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
	em_eo_t eo_1st, eo_2nd, eo_3rd;
	em_queue_t default_queue, pktout_queue;
	em_queue_t queue_1st, queue_2nd, queue_3rd;
	em_queue_t tmp_q;
	em_queue_conf_t queue_conf;
	em_output_queue_conf_t output_conf; /* platform specific */
	pktio_tx_fn_args_t pktio_tx_fn_args; /* user defined content */
	queue_context_1st_t *q_ctx_1st;
	queue_context_2nd_t *q_ctx_2nd;
	queue_context_3rd_t *q_ctx_3rd;
	queue_type_tuple_t *q_type_tuple;
	em_status_t ret, start_fn_ret = EM_ERROR;
	uint16_t port_offset = (uint16_t)-1;
	int q_ctx_idx = 0;
	int if_id, i, j;

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

	/* Store the number of pktio interfaces used */
	pkt_shm->if_count = appl_conf->pktio.if_count;
	/* Store the used interface ids */
	for (i = 0; i < appl_conf->pktio.if_count; i++) {
		if_id = appl_conf->pktio.if_ids[i];
		test_fatal_if(if_id > MAX_IF_ID,
			      "Interface id out of range! %d > %d(MAX)",
			      if_id, MAX_IF_ID);
		pkt_shm->if_ids[i] = if_id;
	}

	/* Use different prios for the queues */
	const em_queue_prio_t q_prio[Q_PRIO_LEVELS] = {EM_QUEUE_PRIO_NORMAL};

	/* Initialize the Queue-type permutations array */
	fill_q_type_permutations();

	/*
	 * Create packet output queues.
	 *
	 * Dimension the number of pktout queues to be equal to the number
	 * of EM cores per interface to minimize output resource contention.
	 */
	test_fatal_if(em_core_count() >= MAX_PKTOUT_QUEUES_PER_IF,
		      "No room to store pktout queues");
	pkt_shm->pktout_queues_per_if = em_core_count();

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
	for (i = 0; i < pkt_shm->if_count; i++) {
		if_id = pkt_shm->if_ids[i];
		for (j = 0; j < pkt_shm->pktout_queues_per_if; j++) {
			/* pktout queue tied to interface id 'if_id' */
			pktio_tx_fn_args.if_id = if_id;
			pktout_queue =
			em_queue_create("pktout-queue", EM_QUEUE_TYPE_OUTPUT,
					EM_QUEUE_PRIO_UNDEF,
					EM_QUEUE_GROUP_UNDEF, &queue_conf);
			test_fatal_if(pktout_queue == EM_QUEUE_UNDEF,
				      "Pktout queue create failed:%d,%d", i, j);
			pkt_shm->pktout_queue[if_id][j] = pktout_queue;
		}
	}

	/* Create EOs, 3 stages of processing for each flow */
	memset(pkt_shm->eo_ctx, 0, sizeof(pkt_shm->eo_ctx));
	eo_1st = em_eo_create("packet_mstage_1st", start_eo, start_eo_local,
			      stop_eo, stop_eo_local, receive_packet_eo_1st,
			      &pkt_shm->eo_ctx[0]);
	eo_2nd = em_eo_create("packet_mstage_2nd", start_eo, start_eo_local,
			      stop_eo, stop_eo_local, receive_packet_eo_2nd,
			      &pkt_shm->eo_ctx[1]);
	eo_3rd = em_eo_create("packet_mstage_3rd", start_eo, start_eo_local,
			      stop_eo, stop_eo_local, receive_packet_eo_3rd,
			      &pkt_shm->eo_ctx[2]);

	/* Start the EOs */
	ret = em_eo_start_sync(eo_3rd, &start_fn_ret, NULL);
	test_fatal_if(ret != EM_OK || start_fn_ret != EM_OK,
		      "em_eo_start_sync() failed:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_fn_ret);

	ret = em_eo_start_sync(eo_2nd, &start_fn_ret, NULL);
	test_fatal_if(ret != EM_OK || start_fn_ret != EM_OK,
		      "em_eo_start_sync() failed:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_fn_ret);

	ret = em_eo_start_sync(eo_1st, &start_fn_ret, NULL);
	test_fatal_if(ret != EM_OK || start_fn_ret != EM_OK,
		      "em_eo_start_sync() failed:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_fn_ret);

	/*
	 * Default queue for all packets, handled by EO 1, receives all
	 * unwanted packets (EO 1 drops them)
	 * Note: The queue type is EM_QUEUE_TYPE_PARALLEL !
	 */
	default_queue = em_queue_create("default", EM_QUEUE_TYPE_PARALLEL,
					EM_QUEUE_PRIO_LOWEST,
					EM_QUEUE_GROUP_DEFAULT, NULL);
	test_fatal_if(default_queue == EM_QUEUE_UNDEF,
		      "Default Queue creation failed!");

	/* Store the default queue Id on the EO-context data */
	pkt_shm->eo_ctx[0].default_queue = default_queue;

	/* Associate the queue with EO 1 */
	ret = em_eo_add_queue_sync(eo_1st, default_queue);
	test_fatal_if(ret != EM_OK,
		      "Add queue failed:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      ret, eo_1st, default_queue);

	/* Zero the Queue context arrays */
	memset(pkt_shm->eo_q_ctx_1st, 0, sizeof(pkt_shm->eo_q_ctx_1st));
	memset(pkt_shm->eo_q_ctx_2nd, 0, sizeof(pkt_shm->eo_q_ctx_2nd));
	memset(pkt_shm->eo_q_ctx_3rd, 0, sizeof(pkt_shm->eo_q_ctx_3rd));

	/* Create queues for the input packet flows */
	for (i = 0; i < NUM_IP_ADDRS; i++) {
		char ip_str[sizeof("255.255.255.255")];
		uint32_t ip_addr = IP_ADDR_BASE + i;

		ipaddr_tostr(ip_addr, ip_str, sizeof(ip_str));

		for (j = 0; j < NUM_PORTS_PER_IP; j++) {
			uint16_t udp_port;
			em_queue_prio_t prio;
			em_queue_type_t queue_type;
			em_queue_group_t queue_group = EM_QUEUE_GROUP_DEFAULT;

			if (UDP_PORTS_UNIQUE) /* Every UDP-port is different */
				port_offset++;
			else /* Same UDP-ports per IP-interface */
				port_offset = j;

			udp_port = UDP_PORT_BASE + port_offset;
			/* Get the queue types for this 3-tuple */
			q_type_tuple = get_queue_type_tuple(q_ctx_idx);
			/* Get the queue priority for this 3-tuple */
			prio = q_prio[q_ctx_idx % Q_PRIO_LEVELS];

			/*
			 * Create the packet-IO (input/Rx) queue
			 * for 'eo_1st' for this flow
			 */
			queue_type = q_type_tuple->queue_type_1st;
			queue_1st = em_queue_create("udp_port", queue_type,
						    prio, queue_group, NULL);
			test_fatal_if(queue_1st == EM_QUEUE_UNDEF,
				      "1.Queue create fail: UDP-port %d",
				      udp_port);

			q_ctx_1st = &pkt_shm->eo_q_ctx_1st[q_ctx_idx];
			ret = em_queue_set_context(queue_1st, q_ctx_1st);
			test_fatal_if(ret != EM_OK,
				      "Queue-ctx set failed:%" PRI_STAT "\n"
				      "EO-q-ctx:%d Q:%" PRI_QUEUE "",
				      ret, q_ctx_idx, queue_1st);

			ret = em_eo_add_queue_sync(eo_1st, queue_1st);
			test_fatal_if(ret != EM_OK,
				      "Add queue failed:%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
				      ret, eo_1st, queue_1st);

			/*
			 * Create the middle queue for 'eo_2nd' for this flow
			 */
			queue_type = q_type_tuple->queue_type_2nd;
			queue_2nd = em_queue_create("udp_port", queue_type,
						    prio, queue_group, NULL);
			test_fatal_if(queue_2nd == EM_QUEUE_UNDEF,
				      "2.Queue create fail: UDP-port %d",
				      udp_port);

			q_ctx_2nd = &pkt_shm->eo_q_ctx_2nd[q_ctx_idx];
			ret = em_queue_set_context(queue_2nd, q_ctx_2nd);
			test_fatal_if(ret != EM_OK,
				      "Q-ctx set failed:%" PRI_STAT "\n"
				      "EO-q-ctx:%d Q:%" PRI_QUEUE "",
				      ret, q_ctx_idx, queue_2nd);

			ret = em_eo_add_queue_sync(eo_2nd, queue_2nd);
			test_fatal_if(ret != EM_OK,
				      "Add queue failed:%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
				      ret, eo_2nd, queue_2nd);

			/* Save stage1 dst queue */
			q_ctx_1st->dst_queue = queue_2nd;

			/*
			 * Create the last queue for 'eo_3rd' for this flow,
			 * eo-3rd sends the event/packet out to where it
			 * originally came from
			 */
			queue_type = q_type_tuple->queue_type_3rd;
			queue_3rd = em_queue_create("udp_port", queue_type,
						    prio, queue_group, NULL);
			test_fatal_if(queue_3rd == EM_QUEUE_UNDEF,
				      "3.Queue create fail: UDP-port %d",
				      udp_port);

			q_ctx_3rd = &pkt_shm->eo_q_ctx_3rd[q_ctx_idx];
			ret = em_queue_set_context(queue_3rd, q_ctx_3rd);
			test_fatal_if(ret != EM_OK,
				      "Q-ctx set failed:%" PRI_STAT "\n"
				      "EO-q-ctx:%d Q:%" PRI_QUEUE "",
				      ret, q_ctx_idx, queue_3rd);

			ret = em_eo_add_queue_sync(eo_3rd, queue_3rd);
			test_fatal_if(ret != EM_OK,
				      "Add queue failed:%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
				      ret, eo_3rd, queue_3rd);

			 /* Save stage2 dst queue */
			q_ctx_2nd->dst_queue = queue_3rd;

			/*
			 * Set the pktout queues to use for this queue,
			 * one pktout queue per interface.
			 */
			set_pktout_queues(q_ctx_idx,
					  q_ctx_3rd->pktout_queue/*out*/);

			/*
			 * Direct this ip_addr:udp_port into the first queue
			 */
			pktio_add_queue(IPV4_PROTO_UDP, ip_addr, udp_port,
					queue_1st);

			/* Save the flow params for debug checks in Rx */
			q_ctx_1st->flow_params.ipv4 = ip_addr;
			q_ctx_1st->flow_params.port = udp_port;
			q_ctx_1st->flow_params.proto = IPV4_PROTO_UDP;

			/* Sanity checks (lookup what was configured above) */
			tmp_q = pktio_lookup_sw(IPV4_PROTO_UDP,
						ip_addr, udp_port);
			test_fatal_if(tmp_q == EM_QUEUE_UNDEF ||
				      tmp_q != queue_1st,
				      "Lookup fails IP:UDP %s:%d\n"
				      "Q:%" PRI_QUEUE "!=%" PRI_QUEUE "",
				      ip_str, udp_port, queue_1st,
				      tmp_q);
			/* Print first and last mapping */
			if (q_ctx_idx == 0 ||
			    q_ctx_idx == (NUM_IP_ADDRS * NUM_PORTS_PER_IP - 1))
				APPL_PRINT("IP:prt->Q  %s:%u->%" PRI_QUEUE "\n",
					   ip_str, udp_port, tmp_q);

			/* Update the Queue Context Index for the next round*/
			q_ctx_idx++;
		}
	}

	/*
	 * Direct all non-lookup hit packets into this queue.
	 * Note: if QUEUE_PER_FLOW is '0' then ALL packets end up in this queue
	 */
	pktio_default_queue(default_queue);

	env_sync_mem();
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_status_t ret;
	em_queue_t pktout_queue;
	int if_id;
	int i, j;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	for (i = 0; i < PROCESSING_STAGES; i++) {
		em_eo_t eo = pkt_shm->eo_ctx[i].eo;

		ret = em_eo_stop_sync(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);

		ret = em_eo_delete(eo);
		test_fatal_if(ret != EM_OK,
			      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
	}

	for (i = 0; i < pkt_shm->if_count; i++) {
		if_id = pkt_shm->if_ids[i];
		for (j = 0; j < pkt_shm->pktout_queues_per_if; j++) {
			/* pktout queue tied to interface id 'if_id' */
			pktout_queue = pkt_shm->pktout_queue[if_id][j];
			test_fatal_if(pktout_queue == EM_QUEUE_UNDEF,
				      "Pktout queue undef:%d,%d", i, j);
			ret = em_queue_delete(pktout_queue);
			test_fatal_if(ret != EM_OK,
				      "Pktout queue delete failed:%d,%d", i, j);
		}
	}
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (core == 0) {
		env_shared_free(pkt_shm);
		em_unregister_error_handler();
	}
}

/**
 * EO start function (run once at startup on ONE core)
 */
static em_status_t
start_eo(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	eo_context_t *eo_ctx = eo_context;

	(void)conf;

	APPL_PRINT("EO %" PRI_EO " starting.\n", eo);

	eo_ctx->eo = eo;
	/* eo_ctx->default_queue = Stored earlier in packet_multi_stage_start*/

	env_sync_mem();

	return EM_OK;
}

/**
 * EO Local start function (run once at startup on EACH core)
 */
static em_status_t
start_eo_local(void *eo_context, em_eo_t eo)
{
	(void)eo_context;

	APPL_PRINT("Core%i: EO %" PRI_EO " local start.\n", em_core_id(), eo);

	return EM_OK;
}

/**
 * EO stop function
 */
static em_status_t
stop_eo(void *eo_context, em_eo_t eo)
{
	em_status_t ret;

	(void)eo_context;

	APPL_PRINT("EO %" PRI_EO " stopping.\n", eo);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	return EM_OK;
}

/**
 * EO local stop function
 */
static em_status_t
stop_eo_local(void *eo_context, em_eo_t eo)
{
	(void)eo_context;

	APPL_PRINT("Core%i: EO %" PRI_EO " local stop.\n", em_core_id(), eo);

	return EM_OK;
}

/**
 * EO_1st receive function
 */
static void
receive_packet_eo_1st(void *eo_context, em_event_t event, em_event_type_t type,
		      em_queue_t queue, void *queue_context)
{
	eo_context_t *const eo_ctx = eo_context;
	queue_context_1st_t *const q_ctx = queue_context;
	em_status_t status;

	(void)type;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	/* Drop everything from the default queue */
	if (unlikely(queue == eo_ctx->default_queue)) {
		static ENV_LOCAL uint64_t drop_cnt = 1;
		uint8_t proto;
		uint32_t ipv4_dst;
		uint16_t port_dst;
		char ip_str[sizeof("255.255.255.255")];

		pktio_get_dst(event, &proto, &ipv4_dst, &port_dst);
		ipaddr_tostr(ipv4_dst, ip_str, sizeof(ip_str));

		APPL_PRINT("Pkt recv(%s:%u), def.port, core%d, drop#%" PRIu64 "\n",
			   ip_str, port_dst, em_core_id(), drop_cnt++);

		pktio_drop(event);
		return;
	}

	if (ENABLE_ERROR_CHECKS) { /* Check IP address and port */
		uint8_t proto;
		uint32_t ipv4_dst;
		uint16_t port_dst;
		flow_params_t *const fp = &q_ctx->flow_params;

		pktio_get_dst(event, &proto, &ipv4_dst, &port_dst);

		test_fatal_if(fp->ipv4 != ipv4_dst ||
			      fp->port != port_dst || fp->proto != proto,
			      "Q:%" PRI_QUEUE " received illegal packet!\n"
			      "rcv: IP:0x%" PRIx32 ":%" PRIu16 ".%" PRIu8 "\n"
			      "cfg: IP:0x%" PRIx32 ":%" PRIu16 ".%" PRIu8 "\n"
			      "Abort!", queue, ipv4_dst, port_dst, proto,
			      fp->ipv4, fp->port, fp->proto);
	}

	/* Send to the next stage for further processing. */
	status = em_send(event, q_ctx->dst_queue);

	if (unlikely(status != EM_OK))
		em_free(event);
}

/**
 * EO_2nd receive function
 */
static void
receive_packet_eo_2nd(void *eo_context, em_event_t event, em_event_type_t type,
		      em_queue_t queue, void *queue_context)
{
	queue_context_2nd_t *const q_ctx = queue_context;
	em_status_t status;

	(void)type;
	(void)eo_context;
	(void)queue;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	/* Send to the next stage for further processing. */
	status = em_send(event, q_ctx->dst_queue);

	if (unlikely(status != EM_OK))
		em_free(event);
}

/**
 * EO_3rd receive function
 */
static void
receive_packet_eo_3rd(void *eo_context, em_event_t event, em_event_type_t type,
		      em_queue_t queue, void *queue_context)
{
	queue_context_3rd_t *const q_ctx = queue_context;
	int in_port;
	int out_port;
	em_queue_t pktout_queue;
	em_status_t status;

	(void)type;
	(void)eo_context;
	(void)queue;

	if (unlikely(appl_shm->exit_flag)) {
		em_free(event);
		return;
	}

	in_port = pktio_input_port(event);

	if (X_CONNECT_PORTS)
		out_port = IS_EVEN(in_port) ? in_port + 1 : in_port - 1;
	else
		out_port = in_port;

	pktout_queue = q_ctx->pktout_queue[out_port];

	/* Touch packet. Swap MAC, IP-addrs and UDP-ports: scr<->dst */
	pktio_swap_addrs(event);

	if (ALLOC_COPY_FREE)
		event = alloc_copy_free(event);

	/*
	 * Send the packet buffer back out via the pktout queue through
	 * the 'out_port'
	 */
	status = em_send(event, pktout_queue);
	if (unlikely(status != EM_OK))
		em_free(event);
}

/**
 * Alloc a new event, copy the contents&header into the new event
 * and finally free the original event. Returns a pointer to the new event.
 *
 * Used for testing the performance impact of alloc-copy-free operations.
 */
static inline em_event_t
alloc_copy_free(em_event_t event)
{
	/* Copy the packet event */
	em_event_t new_event = pktio_copy_event(event);

	/* Free old event */
	em_free(event);

	return new_event;
}

/**
 * Helper func to determine queue types at startup
 */
static queue_type_tuple_t *
get_queue_type_tuple(int cnt)
{
	if (!QUEUE_TYPE_MIX) /* Always return the same kind of Queue types */
		return &pkt_shm->q_type_permutations[0];

	/* Spread out over the different queue-types */
	const int idx = cnt % QUEUE_TYPE_PERMUTATIONS;

	return &pkt_shm->q_type_permutations[idx];
}

/**
 * Helper func to initialize the Queue Type permutations array
 *
 * 3 queue types gives 3*3*3=27 permutations - store these.
 */
static void
fill_q_type_permutations(void)
{
	queue_type_tuple_t *tuple;

	if (!QUEUE_TYPE_MIX) {
		tuple = &pkt_shm->q_type_permutations[0];
		/* Use the same type of queues everywhere. */
		tuple->queue_type_1st = QUEUE_TYPE;
		tuple->queue_type_2nd = QUEUE_TYPE;
		tuple->queue_type_3rd = QUEUE_TYPE;
		return;
	}

	int i, j, k;
	em_queue_type_t queue_type_1st, queue_type_2nd, queue_type_3rd;
	int nbr_q = 0;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 3; k++, nbr_q++) {
				queue_type_1st = queue_types(i);
				queue_type_2nd = queue_types(j);
				queue_type_3rd = queue_types(k);

				tuple = &pkt_shm->q_type_permutations[nbr_q];
				tuple->queue_type_1st = queue_type_1st;
				tuple->queue_type_2nd = queue_type_2nd;
				tuple->queue_type_3rd = queue_type_3rd;
			}
		}
	}
}

/**
 * Helper func, returns a Queue Type based on the input count.
 */
static em_queue_type_t
queue_types(int cnt)
{
	switch (cnt % 3) {
	case 0:
		return EM_QUEUE_TYPE_ATOMIC;
	case 1:
		return EM_QUEUE_TYPE_PARALLEL;
	default:
		return EM_QUEUE_TYPE_PARALLEL_ORDERED;
	}
}

/**
 * Helper func to store the packet output queues for a specific input queue
 */
static void
set_pktout_queues(int q_idx, em_queue_t pktout_queue[/*out*/])
{
	int if_count = pkt_shm->if_count;
	int pktout_idx = q_idx % pkt_shm->pktout_queues_per_if;
	int id, i;

	for (i = 0; i < if_count; i++) {
		id = pkt_shm->if_ids[i];
		pktout_queue[id] = pkt_shm->pktout_queue[id][pktout_idx];
	}
}

static em_status_t
mstage_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope,
		     va_list args)
{
	/*
	 * Don't report/log/print em_send() errors, instead return the error
	 * code and let the application free the event that failed to be sent.
	 * This avoids a print/log storm in an overloaded situation, i.e. when
	 * sending input packets at a higher rate that can be sustained.
	 */
	if (!EM_ERROR_IS_FATAL(error) &&
	    (escope == EM_ESCOPE_SEND || escope == EM_ESCOPE_SEND_MULTI))
		return error;

	return test_error_handler(eo, error, escope, args);
}
