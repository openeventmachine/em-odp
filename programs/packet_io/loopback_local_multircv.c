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
 *
 * Simple Load Balanced Packet-IO test application using local queues and
 * capable of receiving multiple events at a time (the EO is created with a
 * multi-event receive function).
 *
 * The application (EO) receives a batch of UDP datagrams and exchanges
 * the src-dst addesses before sending the datagrams back out.
 *
 * Based on lopback_local.c
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

/**
 * Set the used queue type for EM queues receiving packet data.
 *
 * Default: use EM_QUEUE_TYPE_LOCAL for max throughput by skipping
 * load balancing and dynamic scheduling in favor of raw performance.
 *
 * Try also with EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_TYPE_PARALLEL or
 * EM_QUEUE_TYPE_PARALLEL_ORDERED.
 * Alt. set QUEUE_TYPE_MIX to '1' to use all queue types simultaneously.
 */
#define QUEUE_TYPE  EM_QUEUE_TYPE_LOCAL
/* #define QUEUE_TYPE  EM_QUEUE_TYPE_ATOMIC */
/* #define QUEUE_TYPE  EM_QUEUE_TYPE_PARALLEL */
/* #define QUEUE_TYPE  EM_QUEUE_TYPE_PARALLEL_ORDERED */

/**
 * Test with all different queue types simultaneously:
 * LOCAL, ATOMIC, PARALLELL, PARALLEL_ORDERED
 */
#define QUEUE_TYPE_MIX  0 /* 0=False or 1=True */

/**
 * Create an EM queue per UDP/IP flow or use the default queue.
 *
 * If set to '0' then all traffic is routed through one 'default queue'(slow),
 * if set to '1' each traffic flow is routed to its own EM-queue.
 */
#define QUEUE_PER_FLOW  1 /* 0=False or 1=True */

/**
 * Select whether the UDP ports should be unique over all the IP-interfaces
 * (set to 1) or reused per IP-interface (thus each UDP port is configured
 * once for  each IP-interface). Using '0' (not unique) makes it easier to
 * copy traffic generator settings from one IF-port to another as only the
 * dst-IP address has to be changed.
 */
#define UDP_PORTS_UNIQUE  0 /* 0=False or 1=True */

/**
 * Select whether the input and output ports should be cross-connected.
 */
#define X_CONNECT_PORTS  0 /* 0=False or 1=True */

/**
 * Enable per packet error checking
 */
#define ENABLE_ERROR_CHECKS  0 /* 0=False or 1=True */

/**
 * Test em_alloc and em_free per packet
 *
 * Alloc new event, copy event, free old event
 */
#define ALLOC_COPY_FREE  0 /* 0=False or 1=True */

/* Configure the IP addresses and UDP ports that this application will use */
#define NUM_IP_ADDRS      4
#define NUM_PORTS_PER_IP  64

#define IP_ADDR_A  192
#define IP_ADDR_B  168
#define IP_ADDR_C  1
#define IP_ADDR_D  16

#define IP_ADDR_BASE  ((IP_ADDR_A << 24) | (IP_ADDR_B << 16) | \
		       (IP_ADDR_C << 8)  | (IP_ADDR_D))
#define UDP_PORT_BASE  1024
/*
 * IANA Dynamic Ports (Private or Ephemeral Ports),
 * from 49152 to 65535 (never assigned)
 */
/* #define UDP_PORT_BASE  0xC000 */

#define MAX_NUM_IF 4 /* max number of used interfaces */
#define MAX_IF_ID  6 /* max interface identifier:[0-MAX], cnt:MAX+1 */

#define NUM_PKTIN_QUEUES  (NUM_IP_ADDRS * NUM_PORTS_PER_IP)
#define MAX_PKTOUT_QUEUES_PER_IF  EM_MAX_CORES

#define IS_ODD(x)   (((x) & 0x1))
#define IS_EVEN(x)  (!IS_ODD(x))

#define MAX_RCV_FN_EVENTS  256

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
	/** default queue: pkts/events not matching any other input criteria */
	em_queue_t default_queue;
	/** all created input queues */
	em_queue_t queue[NUM_PKTIN_QUEUES];
	/** the number of packet output queues to use per interface */
	int pktout_queues_per_if;
	/* pktout queues: accessed by if_id, thus empty middle slots possible */
	em_queue_t pktout_queue[MAX_IF_ID + 1][MAX_PKTOUT_QUEUES_PER_IF];
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
 * Queue-Context, i.e. queue specific data, each queue has its own instance
 */
typedef struct {
	/** a pktout queue for each interface, precalculated */
	em_queue_t pktout_queue[MAX_IF_ID + 1];
	/** saved flow params for the EM-queue */
	flow_params_t flow_params;
	/** queue handle */
	em_queue_t queue;
} queue_context_t;

/**
 * Packet Loopback shared memory
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

	/** Queue context for the default queue */
	queue_context_t def_q_ctx;
} packet_loopback_shm_t;

/** EM-core local pointer to shared memory */
static ENV_LOCAL packet_loopback_shm_t *pkt_shm;

static em_status_t
start_eo(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);

static void
create_queue_per_flow(const em_eo_t eo, eo_context_t *const eo_ctx);

static void
set_pktout_queues(em_queue_t queue, eo_context_t *const eo_ctx,
		  em_queue_t pktout_queue[/*out*/]);

static em_status_t
start_eo_local(void *eo_context, em_eo_t eo);

static void
receive_eo_packet_multi(void *eo_context, em_event_t event_tbl[], int num,
			em_queue_t queue, void *queue_context);

static em_status_t
stop_eo(void *eo_context, em_eo_t eo);

static inline int
rx_error_check(eo_context_t *const eo_ctx, const em_event_t event,
	       const em_queue_t queue, queue_context_t *const q_ctx);

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
 * Init of the Packet Loopback test application.
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
		pkt_shm = env_shared_reserve("PktLoopShMem",
					     sizeof(packet_loopback_shm_t));
		em_register_error_handler(test_error_handler);
	} else {
		pkt_shm = env_shared_lookup("PktLoopShMem");
	}

	if (pkt_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Packet Loopback init failed on EM-core: %u",
			   em_core_id());
	else if (core == 0)
		memset(pkt_shm, 0, sizeof(packet_loopback_shm_t));
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
void
test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	em_eo_multircv_param_t eo_param;
	eo_context_t *eo_ctx;
	em_status_t ret, start_fn_ret = EM_ERROR;
	int if_id, i;

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

	test_fatal_if(!pktin_polled_mode(pktin_mode),
		      "Invalid pktin-mode: %s(%i).\n"
		      "Application:%s supports only polled pktin-modes: %s(%i), %s(%i)",
		      pktin_mode_str(pktin_mode), pktin_mode,
		      appl_conf->name,
		      pktin_mode_str(DIRECT_RECV), DIRECT_RECV,
		      pktin_mode_str(PLAIN_QUEUE), PLAIN_QUEUE);

	/*
	 * Create one EO
	 */
	eo_ctx = &pkt_shm->eo_ctx;
	/* Initialize EO context data to '0' */
	memset(eo_ctx, 0, sizeof(eo_context_t));

	/* Init EO params */
	em_eo_multircv_param_init(&eo_param);
	/* Set EO params needed by this application */
	eo_param.start = start_eo;
	eo_param.local_start = start_eo_local;
	eo_param.stop = stop_eo;
	eo_param.receive_multi = receive_eo_packet_multi;
	eo_param.max_events = MAX_RCV_FN_EVENTS;
	eo_param.eo_ctx = eo_ctx;
	eo = em_eo_create_multircv(appl_conf->name, &eo_param);
	test_fatal_if(eo == EM_EO_UNDEF, "em_eo_create() failed");
	eo_ctx->eo = eo;

	/* Store the number of pktio interfaces used */
	eo_ctx->if_count = appl_conf->pktio.if_count;
	/* Store the used interface ids */
	for (i = 0; i < appl_conf->pktio.if_count; i++) {
		if_id = appl_conf->pktio.if_ids[i];
		test_fatal_if(if_id > MAX_IF_ID,
			      "Interface id out of range! %d > %d(MAX)",
			      if_id, MAX_IF_ID);
		eo_ctx->if_ids[i] = if_id;
	}

	/* Start the EO - queues etc. created in the EO start function */
	ret = em_eo_start_sync(eo, &start_fn_ret, NULL);
	test_fatal_if(ret != EM_OK || start_fn_ret != EM_OK,
		      "em_eo_start_sync() failed:%" PRI_STAT " %" PRI_STAT "",
		      ret, start_fn_ret);

	/*
	 * All input & output queues have been created and enabled in the
	 * EO start function, now direct pktio traffic to those queues.
	 */
	for (i = 0; i < NUM_PKTIN_QUEUES; i++) {
		/* Direct ip_addr:udp_port into this queue */
		queue_context_t *q_ctx = &pkt_shm->eo_q_ctx[i];
		uint32_t ip_addr = q_ctx->flow_params.ipv4;
		uint16_t port = q_ctx->flow_params.port;
		uint8_t proto = q_ctx->flow_params.proto;
		em_queue_t queue = q_ctx->queue;
		em_queue_t tmp_q;
		char ip_str[sizeof("255.255.255.255")];

		ipaddr_tostr(ip_addr, ip_str, sizeof(ip_str));

		pktio_add_queue(proto, ip_addr, port, queue);

		/* Sanity checks (lookup what was configured) */
		tmp_q = pktio_lookup_sw(proto, ip_addr, port);
		test_fatal_if(tmp_q == EM_QUEUE_UNDEF || tmp_q != queue,
			      "Lookup fails IP:UDP %s:%d\n"
			      "Q:%" PRI_QUEUE "!=%" PRI_QUEUE "",
			      ip_str, port, queue, tmp_q);
		/* Print first and last mapping */
		if (i == 0 || i == NUM_PKTIN_QUEUES - 1)
			APPL_PRINT("IP:prt->Q  %s:%u->%" PRI_QUEUE "\n",
				   ip_str, port, tmp_q);
	}

	/*
	 * Direct all non-lookup hit packets into this queue.
	 * Note: if QUEUE_PER_FLOW is '0' then ALL packets end up in this queue
	 */
	pktio_default_queue(eo_ctx->default_queue);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	eo_context_t *const eo_ctx = &pkt_shm->eo_ctx;
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
 *
 * The global start function creates the application specific queues and
 * associates the queues with the EO and the packet flows it wants to process.
 */
static em_status_t
start_eo(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	em_queue_t def_queue, pktout_queue;
	em_queue_conf_t queue_conf;
	em_output_queue_conf_t output_conf; /* platform specific */
	pktio_tx_fn_args_t pktio_tx_fn_args; /* user defined content */
	em_queue_type_t queue_type;
	em_queue_group_t queue_group;
	em_status_t ret;
	eo_context_t *const eo_ctx = eo_context;
	queue_context_t *defq_ctx;
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
			/* pktout queue tied to interface id 'if_id' */
			pktio_tx_fn_args.if_id = if_id;
			pktout_queue =
			em_queue_create("pktout-queue", EM_QUEUE_TYPE_OUTPUT,
					EM_QUEUE_PRIO_UNDEF,
					EM_QUEUE_GROUP_UNDEF, &queue_conf);
			test_fatal_if(pktout_queue == EM_QUEUE_UNDEF,
				      "Pktout queue create failed:%d,%d", i, j);
			eo_ctx->pktout_queue[if_id][j] = pktout_queue;
		}
	}

	/*
	 * Default queue for all packets not mathing any
	 * specific input queue criteria
	 */
	queue_type = QUEUE_TYPE;
	if (queue_type == EM_QUEUE_TYPE_LOCAL)
		queue_group = EM_QUEUE_GROUP_UNDEF;
	else
		queue_group = EM_QUEUE_GROUP_DEFAULT;
	def_queue = em_queue_create("default", queue_type, EM_QUEUE_PRIO_NORMAL,
				    queue_group, NULL);
	test_fatal_if(def_queue == EM_QUEUE_UNDEF,
		      "Default Queue creation failed");

	/* Store the default queue Id in the EO-context data */
	eo_ctx->default_queue = def_queue;

	/* Associate the queue with this EO */
	ret = em_eo_add_queue_sync(eo, def_queue);
	test_fatal_if(ret != EM_OK,
		      "Add queue failed:%" PRI_STAT "\n"
		      "EO:%" PRI_EO " Queue:%" PRI_QUEUE "",
		      ret, eo, def_queue);

	/* Set queue context for the default queue */
	defq_ctx = &pkt_shm->def_q_ctx;
	ret = em_queue_set_context(eo_ctx->default_queue, defq_ctx);
	test_fatal_if(ret != EM_OK,
		      "Set Q-ctx for the default queue failed:%" PRI_STAT "\n"
		      "default-Q:%" PRI_QUEUE "", ret, def_queue);

	/* Set the pktout queues to use for the default queue, one per if */
	set_pktout_queues(def_queue, eo_ctx, defq_ctx->pktout_queue/*out*/);

	if (QUEUE_PER_FLOW)
		create_queue_per_flow(eo, eo_ctx);

	APPL_PRINT("EO %" PRI_EO " global start done.\n", eo);

	return EM_OK;
}

/**
 * Helper func for EO start() to create a queue per packet flow (if configured)
 */
static void
create_queue_per_flow(const em_eo_t eo, eo_context_t *const eo_ctx)
{
	uint16_t port_offset = (uint16_t)-1;
	uint32_t q_ctx_idx = 0;
	queue_context_t *q_ctx;
	em_queue_type_t qtype;
	em_queue_group_t queue_group;
	em_queue_t queue;
	em_status_t ret;
	int i, j;

	memset(pkt_shm->eo_q_ctx, 0, sizeof(pkt_shm->eo_q_ctx));

	for (i = 0; i < NUM_IP_ADDRS; i++) {
		char ip_str[sizeof("255.255.255.255")];
		uint32_t ip_addr = IP_ADDR_BASE + i;

		ipaddr_tostr(ip_addr, ip_str, sizeof(ip_str));

		for (j = 0; j < NUM_PORTS_PER_IP; j++) {
			uint16_t udp_port;

			if (UDP_PORTS_UNIQUE) /* Every UDP-port is different */
				port_offset++;
			else /* Same UDP-ports per IP-interface */
				port_offset = j;

			udp_port = UDP_PORT_BASE + port_offset;

			if (!QUEUE_TYPE_MIX) {
				/* Use only queues of a single type */
				qtype = QUEUE_TYPE;
			} else {
				/* Spread out over the 4 diff queue-types */
				int nbr_q = ((i * NUM_PORTS_PER_IP) + j) % 4;

				if (nbr_q == 0)
					qtype = EM_QUEUE_TYPE_LOCAL;
				else if (nbr_q == 1)
					qtype = EM_QUEUE_TYPE_ATOMIC;
				else if (nbr_q == 2)
					qtype = EM_QUEUE_TYPE_PARALLEL;
				else
					qtype = EM_QUEUE_TYPE_PARALLEL_ORDERED;
			}

			/* Create a queue */
			if (qtype == EM_QUEUE_TYPE_LOCAL)
				queue_group = EM_QUEUE_GROUP_UNDEF;
			else
				queue_group = EM_QUEUE_GROUP_DEFAULT;
			queue = em_queue_create("udp-flow", qtype,
						EM_QUEUE_PRIO_NORMAL,
						queue_group, NULL);
			test_fatal_if(queue == EM_QUEUE_UNDEF,
				      "Queue create failed: UDP-port %d",
				      udp_port);
			/*
			 * Store the id of the created queue into the
			 * application specific EO-context
			 */
			eo_ctx->queue[q_ctx_idx] = queue;

			/* Set queue specific appl (EO) context */
			q_ctx = &pkt_shm->eo_q_ctx[q_ctx_idx];
			/* Save flow params */
			q_ctx->flow_params.ipv4 = ip_addr;
			q_ctx->flow_params.port = udp_port;
			q_ctx->flow_params.proto = IPV4_PROTO_UDP;
			q_ctx->queue = queue;

			ret = em_queue_set_context(queue, q_ctx);
			test_fatal_if(ret != EM_OK,
				      "Set Q-ctx failed:%" PRI_STAT "\n"
				      "EO-q-ctx:%d Q:%" PRI_QUEUE "",
				      ret, q_ctx_idx, queue);

			/* Add the queue to the EO */
			ret = em_eo_add_queue_sync(eo, queue);
			test_fatal_if(ret != EM_OK,
				      "Add queue failed:%" PRI_STAT "\n"
				      "EO:%" PRI_EO " Q:%" PRI_QUEUE "",
				      ret, eo, queue);

			/*
			 * Set the pktout queues to use for this input queue,
			 * one pktout queue per interface.
			 */
			set_pktout_queues(queue, eo_ctx,
					  q_ctx->pktout_queue/*out*/);

			/* Update the Queue Context Index */
			q_ctx_idx++;
			test_fatal_if(q_ctx_idx > NUM_PKTIN_QUEUES,
				      "Too many queues!");
		}
	}
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
	em_status_t ret;
	em_queue_t pktout_queue;
	int if_id;
	int i, j;

	APPL_PRINT("EO %" PRI_EO ":%s stopping\n", eo, eo_ctx->name);

	/* remove and delete all of the EO's queues */
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	/* Delete the packet output queues created for each interface */
	for (i = 0; i < eo_ctx->if_count; i++) {
		if_id = eo_ctx->if_ids[i];
		for (j = 0; j < eo_ctx->pktout_queues_per_if; j++) {
			/* pktout queue tied to interface id 'if_id' */
			pktout_queue = eo_ctx->pktout_queue[if_id][j];
			test_fatal_if(pktout_queue == EM_QUEUE_UNDEF,
				      "Pktout queue undef:%d,%d", i, j);
			ret = em_queue_delete(pktout_queue);
			test_fatal_if(ret != EM_OK,
				      "Pktout queue delete failed:%d,%d", i, j);
		}
	}

	return EM_OK;
}

/**
 * EO event receive function
 */
static void
receive_eo_packet_multi(void *eo_context, em_event_t event_tbl[], int num,
			em_queue_t queue, void *queue_context)
{
	queue_context_t *const q_ctx = queue_context;
	int in_port;
	int out_port;
	em_queue_t pktout_queue;
	int ret, i;

	if (unlikely(appl_shm->exit_flag)) {
		em_free_multi(event_tbl, num);
		return;
	}

	in_port = pktio_input_port(event_tbl[0]);

	if (X_CONNECT_PORTS)
		out_port = IS_EVEN(in_port) ? in_port + 1 : in_port - 1;
	else
		out_port = in_port;

	pktout_queue = q_ctx->pktout_queue[out_port];

	if (ENABLE_ERROR_CHECKS) {
		eo_context_t *const eo_ctx = eo_context;

		for (i = 0; i < num; i++)
			if (rx_error_check(eo_ctx, event_tbl[i],
					   queue, q_ctx) != 0)
				return;
	}

	/* Touch packet. Swap MAC, IP-addrs and UDP-ports: scr<->dst */
	for (i = 0; i < num; i++)
		pktio_swap_addrs(event_tbl[i]);

	if (ALLOC_COPY_FREE) /* alloc event, copy contents & free original */
		for (i = 0; i < num; i++)
			event_tbl[i] = alloc_copy_free(event_tbl[i]);

	/*
	 * Send the packet buffer back out via the pktout queue through
	 * the 'out_port'
	 */
	ret = em_send_multi(event_tbl, num, pktout_queue);
	if (unlikely(ret != num))
		em_free_multi(&event_tbl[ret], num - ret);
}

static inline int
rx_error_check(eo_context_t *const eo_ctx, const em_event_t event,
	       const em_queue_t queue, queue_context_t *const q_ctx)
{
	static ENV_LOCAL uint64_t drop_cnt = 1;
	uint8_t proto;
	uint32_t ipv4_dst;
	uint16_t port_dst;

	pktio_get_dst(event, &proto, &ipv4_dst, &port_dst);

	if (QUEUE_PER_FLOW) {
		flow_params_t *fp;

		/* Drop everything from the default queue */
		if (unlikely(queue == eo_ctx->default_queue)) {
			char ip_str[sizeof("255.255.255.255")];

			ipaddr_tostr(ipv4_dst, ip_str, sizeof(ip_str));

			APPL_PRINT("Pkt %s:%" PRIu16 " defQ drop-%d-#%" PRIu64 "\n",
				   ip_str, port_dst, em_core_id(), drop_cnt++);

			em_free(event);
			return -1;
		}

		/*
		 * Check IP address and port: compare packet against the stored
		 * values in the queue context
		 */
		fp = &q_ctx->flow_params;
		test_fatal_if(fp->ipv4 != ipv4_dst ||
			      fp->port != port_dst || fp->proto != proto,
			      "Q:%" PRI_QUEUE " received illegal packet!\n"
			      "rcv: IP:0x%" PRIx32 ":%" PRIu16 ".%" PRIu8 "\n"
			      "cfg: IP:0x%" PRIx32 ":%" PRIu16 ".%" PRIu8 "\n"
			      "Abort!", queue, ipv4_dst, port_dst, proto,
			      fp->ipv4, fp->port, fp->proto);
	} else {
		if (unlikely(proto != IPV4_PROTO_UDP)) {
			APPL_PRINT("Pkt: defQ, not UDP drop-%d-#%" PRIu64 "\n",
				   em_core_id(), drop_cnt++);
			em_free(event);
			return -1;
		}

		test_fatal_if(ipv4_dst < (uint32_t)IP_ADDR_BASE ||
			      ipv4_dst >=
			      (uint32_t)(IP_ADDR_BASE + NUM_IP_ADDRS) ||
			      port_dst < UDP_PORT_BASE ||
			      port_dst >= (UDP_PORT_BASE + NUM_PKTIN_QUEUES) ||
			      proto != IPV4_PROTO_UDP,
			      "Q:%" PRI_QUEUE " received illegal packet!\n"
			      "rcv: IP:0x%" PRIx32 ":%" PRIu16 ".%" PRIu8 "\n"
			      "Values not in the configurated range!\n"
			      "Abort!",
			      queue, ipv4_dst, port_dst, proto);
	}

	/* Everything OK, return zero */
	return 0;
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
