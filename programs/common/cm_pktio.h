/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   Copyright (c) 2015-2024, Nokia Solutions and Networks
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

#ifndef CM_PKTIO_H
#define CM_PKTIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>

#include <odp/helper/odph_api.h>
#include <event_machine/platform/env/environment.h>
#include <event_machine/platform/event_machine_odp_ext.h>

#include "table.h"
#include "cuckootable.h"

#define IPV4_PROTO_UDP  ODPH_IPPROTO_UDP

/**
 * @def PKTIO_MAX_IN_QUEUES
 * @brief Maximum number of odp pktio input queues per interface
 */
#define PKTIO_MAX_IN_QUEUES   32

/**
 * @def PKTIO_MAX_OUT_QUEUES
 * @brief Maximum number of odp pktio output queues per interface
 */
#define PKTIO_MAX_OUT_QUEUES  16

/**
 * @def MAX_PKT_BURST_RX
 * @brief Maximum number of packets received from a pktio input queue
 *        in one burst in polled pktin-mode (DIRECT_RECV, PLAIN_QUEUE)
 */
#define MAX_PKT_BURST_RX  32

/**
 * @def MAX_PKT_BURST_TX
 * @brief Maximum number of packets bursted onto a pktout queue
 */
#define MAX_PKT_BURST_TX  32

/**
 * @def MAX_TX_BURST_BUFS
 * @brief Maximum number of tx burst buffers per interface
 *
 * Store Tx pkts in output buffers until a buffer has 'MAX_PKT_BURST_TX' pkts,
 * then transmit the whole burst of pkts instead of one by one.
 */
#define MAX_TX_BURST_BUFS EM_MAX_CORES

/**
 * @def MAX_RX_PKT_QUEUES
 * @brief
 */
#define MAX_RX_PKT_QUEUES (4 * 64)

/**
 * @def MAX_RX_POLL_ROUNDS
 * @brief
 */
#define MAX_RX_POLL_ROUNDS 4

/**
 * @def BURST_TX_DRAIN
 * @brief The number of core cycles between timed TX buf drain operations
 */
#define BURST_TX_DRAIN (400000ULL)  /* around 200us at 2 Ghz */

/**
 * @brief pkt header fields to use as hash key
 *
 * Fields from incoming packets used for destination em-odp queue lookup.
 */
struct pkt_dst_tuple {
	/* uint32_t ip_src;*/
	uint32_t ip_dst;
	/* uint16_t port_src;*/
	uint16_t port_dst;
	uint16_t proto;
} __attribute__((__packed__));

/** Use the struct pkt_dst_tuple as hash key for em-odp queue lookups */
typedef struct pkt_dst_tuple pkt_q_hash_key_t;

/* Keep size multiple of 32-bits for faster hash-crc32 calculation*/
ODP_STATIC_ASSERT(sizeof(pkt_q_hash_key_t) % sizeof(uint32_t) == 0,
		  "HASH_KEY_NOT_MULTIP_OF_32__ERROR");

/**
 * @brief Info about em-odp queue to use, returned by hash lookup
 *
 * Information about an em-odp queue used for pktio, stored in a hash table and
 * used when doing a tbl lookup to determine the destination em-odp queue
 * for a received packet.
 */
typedef struct {
	int pos;
	em_queue_t queue;
} rx_pkt_queue_t;

/**
 * @brief Tx pkt burst buffer
 *
 * Buffer up to 'MAX_PKT_BURST_TX' pkts before bursting them all onto
 * the associated 'pktout_queue' at once.
 */
typedef struct tx_burst {
	/** store tx pkts temporarily in 'queue' before bursting onto tx */
	odp_queue_t queue ODP_ALIGNED_CACHE;
	/** count the number of events in 'queue', updated atomically */
	odp_atomic_u64_t cnt;
	/** lock needed when dequeueing from 'queue' */
	odp_spinlock_t lock;
	/** store the output interface port also here for easy access */
	int if_port;
	/** Transmit burst using this pktout_queue */
	odp_queue_t pktout_queue;
} tx_burst_t;

/**
 * @brief Rx pkt storage for pkts destined to the same em-odp queue
 *
 * Temporary storage for events to be enqueued onto the _same_ queue
 * after receiving a packet burst on Rx
 */
typedef struct {
	int sent;
	int pkt_cnt;
	em_queue_t queue;
	odp_packet_t pkt_tbl[MAX_PKT_BURST_RX];
} rx_queue_burst_t;

/**
 * @brief Pktio shared memory
 *
 * Collection of shared data used by pktio Rx&Tx
 */
typedef struct {
	/** flag set after pktio_start() - prevent pkio rx&tx before started */
	int pktio_started;

	/** Default queue to use for incoming pkts without a dedicated queue */
	em_queue_t default_queue;

	struct {
		/** EM pool for pktio, only used with '--pktpool-em' option */
		em_pool_t pktpool_em;

		/** ODP pool for pktio:
		 *  1. Subpool of 'pktpool_em' when using '--pktpool-em' option
		 *     or
		 *  2. Direct ODP pkt pool when using '--pktpool-odp' option
		 */
		odp_pool_t pktpool_odp;

		/** EM vector pool for pktio, only used with '--pktin-vector' option */
		em_pool_t vecpool_em;
		/** ODP vector pool for pktio:
		 *  1. Subpool of 'vecpool_em' when using '--vecpool-em' option
		 *     or
		 *  2. Direct ODP vector pool when using '--vecpool-odp' option
		 */
		odp_pool_t vecpool_odp;
	} pools;

	/** Packet I/O Interfaces */
	struct {
		/** The number of pktio interfaces used */
		int count;
		/** Interfaces created so far (up to '.count'), startup only */
		int num_created;
		/** Interface indexes used */
		int idx[IF_MAX_NUM];
		/** ODP pktio handles, .pktio_hdl[idx] corresponds to idx=.idx[i] */
		odp_pktio_t pktio_hdl[IF_MAX_NUM];
	} ifs;

	/** Packet input and related resources */
	struct {
		/* Packet input mode */
		pktin_mode_t in_mode;

		/** Number of input queues per interface */
		int num_queues[IF_MAX_NUM];

		/** pktin queues used in DIRECT_RECV-mode, per interface */
		odp_pktin_queue_t pktin_queues[IF_MAX_NUM][PKTIO_MAX_IN_QUEUES];

		/** plain event queues used in PLAIN_QUEUE-mode, per interface */
		odp_queue_t plain_queues[IF_MAX_NUM][PKTIO_MAX_IN_QUEUES];

		/** scheduled event queues used in SCHED_...-mode, per interface */
		odp_queue_t sched_queues[IF_MAX_NUM][PKTIO_MAX_IN_QUEUES];
		/** scheduled EM event queues created from sched_queues[][] above */
		em_queue_t sched_em_queues[IF_MAX_NUM][PKTIO_MAX_IN_QUEUES];

		/** A queue that contains pointers to the shared
		 *  pktin_queues[][] in DIRECT_RECV-mode or to the shared
		 *  plain_queues[][] in PLAIN_QUEUE-mode.
		 *  Each core needs to dequeue one packet input queue to be
		 *  able to use it to receive packets.
		 */
		odp_stash_t pktin_queue_stash;
	} pktin;

	/** Packet output and related resources */
	struct {
		/** Number of pktio output queues per interface */
		int num_queues[IF_MAX_NUM];

		/** All pktio output queues used, per interface */
		odp_queue_t queues[IF_MAX_NUM][PKTIO_MAX_OUT_QUEUES];

		/** A stash that contains the shared tx_burst[][] entries.
		 *  Used when draining the available tx-burst buffers
		 */
		odp_stash_t tx_burst_stash;
	} pktout;

	/** Info about the em-odp queues configured for pktio, store in hash */
	rx_pkt_queue_t rx_pkt_queues[MAX_RX_PKT_QUEUES];

	/** Pkt lookup table, lookup destination em-odp queue for Rx pkts */
	struct {
		table_ops_t ops;
		table_t tbl;
		int tbl_idx;
		odp_ticketlock_t lock;
	} tbl_lookup;

	/** Tx burst buffers per interface  */
	tx_burst_t tx_burst[IF_MAX_NUM][MAX_TX_BURST_BUFS] ODP_ALIGNED_CACHE;
} pktio_shm_t;

/**
 * @brief Pktio core-local memory
 *
 * Collection of core local (not shared) data used by pktio Rx&Tx
 */
typedef struct {
	/** Event contains the currently used pktio input queue */
	odp_event_t pktin_queue_event;
	/** Determine need for timed drain of pktio Tx queues */
	uint64_t tx_prev_cycles;
	/** Array of hash keys for the current received Rx pkt burst */
	pkt_q_hash_key_t keys[MAX_PKT_BURST_RX];
	/** Array of positions into rx_qbursts[], filled from hash lookup  */
	int positions[MAX_PKT_BURST_RX];
	/** Grouping of Rx pkts per destination em-odp queue */
	rx_queue_burst_t rx_qbursts[MAX_RX_PKT_QUEUES + 1]; /* +1=default Q */
	/** Temporary storage of Tx pkt burst */
	odp_event_t ev_burst[MAX_PKT_BURST_TX];
} pktio_locm_t;

/**
 * Reserve shared memory for pktio
 *
 * Must be called once at startup. Additionally each EM-core needs to call the
 * pktio_mem_lookup() function before using any further pktio resources.
 */
void pktio_mem_reserve(void);

/**
 * Lookup shared memory for pktio
 *
 * Must be called once by each EM-core before using any further pktio resources.
 *
 * @param is_thread_per_core  true:  EM running in thread-per-core mode
 *                            false: EM running in process-per-core mode
 */
void pktio_mem_lookup(bool is_thread_per_core);

void pktio_mem_free(void);

void pktio_pool_create(int if_count, bool pktpool_em,
		       bool pktin_vector, bool vecpool_em);
void pktio_pool_destroy(bool pktpool_em, bool pktin_vector, bool vecpool_em);

void pktio_init(const appl_conf_t *appl_conf);
void pktio_deinit(const appl_conf_t *appl_conf);

int pktio_create(const char *dev, pktin_mode_t in_mode, bool pktin_vector,
		 int if_count, int num_workers);
void pktio_start(void);
void pktio_halt(void);
void pktio_stop(void);
void pktio_close(void);

const char *pktin_mode_str(pktin_mode_t in_mode);
bool pktin_polled_mode(pktin_mode_t in_mode);
bool pktin_sched_mode(pktin_mode_t in_mode);

/**
 * @brief Poll input resources for pkts/events in DIRECT_RECV-mode
 *        and enqueue into EM queues.
 *
 * Given to EM via 'em_conf.input.input_poll_fn' - EM will call this on
 * each core in the dispatch loop.
 * The function is of type 'em_input_poll_func_t'
 *
 * @return number of pkts/events received from input and enqueued into EM
 */
int pktin_pollfn_direct(void);

/**
 * @brief Poll input resources for pkts/events in PLAIN_QUEUE-mode
 *        and enqueue into EM queues.
 *
 * Given to EM via 'em_conf.input.input_poll_fn' - EM will call this on
 * each core in the dispatch loop.
 * The function is of type 'em_input_poll_func_t'
 *
 * @return number of pkts/events received from input and enqueued into EM
 */
int pktin_pollfn_plainqueue(void);

/**
 * @brief Drain buffered output - ensure low rate flows are also sent out.
 *
 * Useful in situations where output is buffered and sent out in bursts when
 * enough output has been gathered - single events or low rate flows may,
 * without this function, never be sent out (or too late) if the buffering
 * threshold has not been reached.
 *
 * Given to EM via 'em_conf.output.output_drain_fn' - EM will call this on
 * each core in the dispatch loop.
 * The function is of type 'em_output_drain_func_t'
 *
 * @return number of events successfully drained and sent for output
 */
int pktout_drainfn(void);

/**
 * @brief User provided EM output-queue callback function ('em_output_func_t')
 *
 * Transmit events(pkts) using the given config onto Eth-tx
 *
 * Buffers the given 'events' in a Tx burst buffer and when full transmits
 * the whole burst from the buffer at once.
 *
 * @param events[]        Events to be sent
 * @param num             Number of entries in 'events[]'
 * @param output_queue    EM output queue the events were sent into (em_send*())
 * @param output_fn_args  Function args specific to the output-queue
 *                        Note: here it will be a 'pktio_tx_fn_args_t' pointer
 *
 * @return number of events successfully sent (equal to num if all successful)
 */
int pktio_tx(const em_event_t events[], const unsigned int num,
	     const em_queue_t output_queue, void *output_fn_args);
/**
 * @typedef pktio_tx_fn_args_t
 * User defined arguments to the EM output queue callback function
 */
typedef struct {
	/** Pktio Tx interface ID */
	int if_id;
	/* add more if needed */
} pktio_tx_fn_args_t;

/**
 * Associate an EM-queue with a packet-I/O flow.
 *
 * Received packets matching the set destination IP-addr/port
 * will end up in the EM-queue 'queue'.
 */
void pktio_add_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t l4_port_dst,
		     em_queue_t queue);

/**
 * Remove the association between a packet-IO flow and an EM-queue.
 *
 * No further received frames will end up in the EM-queue 'queue'
 */
void pktio_rem_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t l4_port_dst,
		     em_queue_t queue);

/**
 * Set the default EM-queue for packet I/O
 */
int pktio_default_queue(em_queue_t queue);

/**
 * Provide applications a way to do a hash-lookup (e.g. sanity check etc.)
 */
em_queue_t pktio_lookup_sw(uint8_t proto, uint32_t ipv4_dst,
			   uint16_t l4_port_dst);

odp_pool_t pktio_pool_get(void);

static inline int
pktio_input_port(em_event_t event)
{
	const odp_event_t odp_event = em_odp_event2odp(event);
	const odp_packet_t pkt = odp_packet_from_event(odp_event);
	const int input_port = odp_packet_input_index(pkt);

	if (unlikely(input_port < 0))
		return 0;

	return input_port;
}

/**
 * Get the protocol, IPv4 destination address and destination L4 port the
 * packet-event was sent to.
 */
static inline void
pktio_get_dst(em_event_t pktev, uint8_t *proto__out,
	      uint32_t *ipv4_dst__out, uint16_t *l4_port_dst__out)
{
	/* if (odp_packet_has_ipv4(pkt)) {
	 *	ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);
	 *	*proto__out = ip->proto;
	 *	*ipv4_dst__out = ntohl(ip->dst_addr);
	 * } else {
	 *	*proto__out = 0;
	 *	*ipv4_dst__out = 0;
	 * }
	 *
	 * if (odp_packet_has_udp(pkt)) {
	 *	udp = (odph_udphdr_t *)odp_packet_l4_ptr(pkt, NULL);
	 *	*port_dst__out = ntohs(udp->dst_port);
	 * } else {
	 *	*port_dst__out = 0;
	 * }
	 */

	/* Note: no actual checks if the headers are present */
	void *pkt_data = em_packet_pointer(pktev);
	odph_ipv4hdr_t *ip = (odph_ipv4hdr_t *)((uintptr_t)pkt_data + sizeof(odph_ethhdr_t));
	odph_udphdr_t *udp = (odph_udphdr_t *)((uintptr_t)ip + sizeof(odph_ipv4hdr_t));

	*proto__out = ip->proto;
	*ipv4_dst__out = ntohl(ip->dst_addr);
	*l4_port_dst__out = ntohs(udp->dst_port);
}

static inline void
pktio_swap_eth_addrs(em_event_t pktev)
{
	odph_ethhdr_t *const eth = em_packet_pointer(pktev);
	const odph_ethaddr_t eth_tmp_addr = eth->dst;

	eth->dst = eth->src;
	eth->src = eth_tmp_addr;
}

static inline void
pktio_swap_addrs(em_event_t pktev)
{
	/*
	 * Needs odp_pktio_config_t::parser.layer = ODP_PROTO_LAYER_L2
	 * if (odp_packet_has_eth(pkt)) {
	 *	eth = (odph_ethhdr_t *)odp_packet_l2_ptr(pkt, NULL);
	 *	eth_tmp_addr = eth->dst;
	 *	eth->dst = eth->src;
	 *	eth->src = eth_tmp_addr;
	 * }
	 *
	 * Needs odp_pktio_config_t::parser.layer = ODP_PROTO_LAYER_L3
	 * if (odp_packet_has_ipv4(pkt)) {
	 *	ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);
	 *	ip_tmp_addr = ip->src_addr;
	 *	ip->src_addr = ip->dst_addr;
	 *	ip->dst_addr = ip_tmp_addr;
	 * }
	 *
	 * Needs odp_pktio_config_t::parser.layer = ODP_PROTO_LAYER_L4
	 * if (odp_packet_has_udp(pkt)) {
	 *	udp = (odph_udphdr_t *)odp_packet_l4_ptr(pkt, NULL);
	 *	udp_tmp_port = udp->src_port;
	 *	udp->src_port = udp->dst_port;
	 *	udp->dst_port = udp_tmp_port;
	 * }
	 */

	/* Note: no actual checks if headers are present */
	void *pkt_data = em_packet_pointer(pktev);
	odph_ethhdr_t *eth = (odph_ethhdr_t *)pkt_data;
	odph_ipv4hdr_t *ip = (odph_ipv4hdr_t *)((uintptr_t)pkt_data + sizeof(odph_ethhdr_t));
	odph_udphdr_t *udp = (odph_udphdr_t *)((uintptr_t)ip + sizeof(odph_ipv4hdr_t));

	odph_ethaddr_t eth_tmp_addr = eth->dst;
	odp_u32be_t ip_tmp_addr = ip->src_addr;
	odp_u16be_t udp_tmp_port = udp->src_port;

	eth->dst = eth->src;
	eth->src = eth_tmp_addr;

	ip->src_addr = ip->dst_addr;
	ip->dst_addr = ip_tmp_addr;

	udp->src_port = udp->dst_port;
	udp->dst_port = udp_tmp_port;
}

static inline em_event_t
pktio_copy_event(em_event_t event)
{
	return em_event_clone(event, EM_POOL_UNDEF);
}

/**
 * Convert an IP-address to ascii string format.
 */
static inline void
ipaddr_tostr(uint32_t ip_addr, char *const ip_addr_str__out, int strlen)
{
	unsigned char *const ucp = (unsigned char *)&ip_addr;

#if ODP_BYTE_ORDER == ODP_LITTLE_ENDIAN
	snprintf(ip_addr_str__out, strlen, "%d.%d.%d.%d",
		 ucp[3] & 0xff, ucp[2] & 0xff, ucp[1] & 0xff, ucp[0] & 0xff);
#elif ODP_BYTE_ORDER == ODP_BIG_ENDIAN
	snprintf(ip_addr_str__out, strlen, "%d.%d.%d.%d",
		 ucp[0] & 0xff, ucp[1] & 0xff, ucp[2] & 0xff, ucp[3] & 0xff);
#else
	#error ODP_BYTE_ORDER invalid
#endif

	ip_addr_str__out[strlen - 1] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* CM_PKTIO_H */
