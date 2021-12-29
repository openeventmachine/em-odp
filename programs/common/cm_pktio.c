/*
 *   Copyright (c) 2015-2021, Nokia Solutions and Networks
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
  * EM-ODP packet I/O setup
  */
#include <odp_api.h>
#include <odp/helper/odph_api.h>
#include <event_machine.h>
#include <event_machine/platform/event_machine_odp_ext.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_pktio.h"

#define PKTIO_PKT_POOL_NUM_BUFS  (10 * 1024)
#define PKTIO_PKT_POOL_BUF_SIZE  1856

static __thread pktio_shm_t *pktio_shm;
static __thread pktio_locm_t pktio_locm ODP_ALIGNED_CACHE;

static inline int
tx_drain_burst_acquire(tx_burst_t **const tx_drain_burst);

static inline int
pktin_queue_acquire(odp_pktin_queue_t *const pktin_queue);

static inline int pktio_rx(void);
static inline int pktio_tx_drain(void);

/*
 * User provided input poll function,
 * given to EM via 'em_conf.input.input_poll_fn = input_poll;'
 * The function is of type 'em_input_poll_func_t'
 */
int input_poll(void)
{
	int ev_rcv_enq;

	ev_rcv_enq = pktio_rx();

	/* Add further input resources to poll if needed */

	return ev_rcv_enq;
}

/*
 * User provided function to drain buffered output,
 * given to EM via 'em_conf.output.output_drain_fn = output_drain;'
 * The function is of type 'em_output_drain_func_t'
 */
int output_drain(void)
{
	int ev_drain;

	ev_drain = pktio_tx_drain();

	/* Add further output draining functions if needed */

	return ev_drain;
}

void pktio_mem_reserve(void)
{
	odp_shm_t shm;
	uint32_t flags = 0;

#if ODP_VERSION_API_NUM(1, 33, 0) > ODP_VERSION_API
	flags |= ODP_SHM_SINGLE_VA;
#else
	odp_shm_capability_t shm_capa;
	int ret = odp_shm_capability(&shm_capa);

	if (unlikely(ret))
		APPL_EXIT_FAILURE("shm capability error:%d", ret);

	if (shm_capa.flags & ODP_SHM_SINGLE_VA)
		flags |= ODP_SHM_SINGLE_VA;
#endif
	/* Reserve packet I/O shared memory */
	shm = odp_shm_reserve("pktio_shm", sizeof(pktio_shm_t),
			      ODP_CACHE_LINE_SIZE, flags);

	if (unlikely(shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("pktio shared mem reserve failed.");

	pktio_shm = odp_shm_addr(shm);
	if (unlikely(pktio_shm == NULL))
		APPL_EXIT_FAILURE("obtaining pktio shared mem addr failed.");

	memset(pktio_shm, 0, sizeof(pktio_shm_t));
}

void pktio_mem_lookup(void)
{
	odp_shm_t shm;

	shm = odp_shm_lookup("pktio_shm");

	pktio_shm = odp_shm_addr(shm);
	if (unlikely(pktio_shm == NULL))
		APPL_EXIT_FAILURE("pktio shared mem addr lookup failed.");
}

void pktio_mem_free(void)
{
	odp_shm_t shm;

	shm = odp_shm_lookup("pktio_shm");
	if (unlikely(shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("pktio shared mem lookup for free failed.");

	if (odp_shm_free(shm) != 0)
		APPL_EXIT_FAILURE("pktio shared mem free failed.");
}

/**
 * Helper to pktio_pool_create(): create the pktio pool as an EM event-pool
 */
static void pktio_pool_create_em(int if_count)
{
	/*
	 * Create the pktio pkt pool used for actual input pkts.
	 * Create the pool as an EM-pool (and convert into an ODP-pool where
	 * needed) to be able to utilize EM's Event State Verification (ESV)
	 * in the 'esv.prealloc_pools = true' mode (see config/em-odp.conf).
	 */
	odp_pool_capability_t pool_capa;
	em_pool_cfg_t pool_cfg;
	em_pool_t pool;

	if (odp_pool_capability(&pool_capa) != 0)
		APPL_EXIT_FAILURE("can't get odp-pool capability");

	em_pool_cfg_init(&pool_cfg);
	pool_cfg.event_type = EM_EVENT_TYPE_PACKET;
	pool_cfg.num_subpools = 1;
	pool_cfg.subpool[0].size = PKTIO_PKT_POOL_BUF_SIZE;
	pool_cfg.subpool[0].num = if_count * PKTIO_PKT_POOL_NUM_BUFS;
	/* Use max thread-local pkt-cache size to speed up pktio allocs */
	pool_cfg.subpool[0].cache_size = pool_capa.pkt.max_cache_size;
	pool = em_pool_create("pktio-pkt-pool", EM_POOL_UNDEF, &pool_cfg);
	if (pool == EM_POOL_UNDEF)
		APPL_EXIT_FAILURE("pktio pool creation failed");

	/* Convert: EM-pool to ODP-pool */
	odp_pool_t odp_pool = ODP_POOL_INVALID;
	int ret = em_odp_pool2odp(pool, &odp_pool, 1);

	if (unlikely(ret != 1))
		APPL_EXIT_FAILURE("EM pktio pool creation failed:%d", ret);

	/* Store the EM pktio pool and the corresponding ODP subpool */
	pktio_shm->pools.pktpool_em = pool;
	pktio_shm->pools.pktpool_odp = odp_pool;

	odp_pool_print(pktio_shm->pools.pktpool_odp);
}

/**
 * Helper to pktio_pool_create(): create the pktio pool as an ODP pkt-pool
 */
static void pktio_pool_create_odp(int if_count)
{
	odp_pool_param_t pool_params;

	odp_pool_param_init(&pool_params);
	pool_params.pkt.num = if_count * PKTIO_PKT_POOL_NUM_BUFS;
	/* pool_params.pkt.max_num = default */
	pool_params.pkt.len = PKTIO_PKT_POOL_BUF_SIZE;
	pool_params.pkt.max_len = PKTIO_PKT_POOL_BUF_SIZE;
	pool_params.pkt.seg_len = PKTIO_PKT_POOL_BUF_SIZE;

	pool_params.type = ODP_POOL_PACKET;
	pool_params.pkt.uarea_size = em_odp_event_hdr_size();

	odp_pool_t odp_pool = odp_pool_create("pktio-pkt-pool", &pool_params);

	if (odp_pool == ODP_POOL_INVALID)
		APPL_EXIT_FAILURE("pktio pool creation failed");

	/* Store the ODP pktio pool */
	pktio_shm->pools.pktpool_odp = odp_pool;
	pktio_shm->pools.pktpool_em = EM_POOL_UNDEF;

	odp_pool_print(pktio_shm->pools.pktpool_odp);
}

/**
 * Create the memory pool used by pkt-io
 */
void pktio_pool_create(int if_count, bool pktpool_em)
{
	/*
	 * Create a ctrl pool for pktio - used for allocation of ctrl data like
	 * Tx-burst structures (tx_burst_t) and queue handles.
	 */
	odp_pool_capability_t pool_capa;
	odp_pool_param_t pool_params;

	if (odp_pool_capability(&pool_capa) != 0)
		APPL_EXIT_FAILURE("can't get odp-pool capability");

	odp_pool_param_init(&pool_params);
	pool_params.type = ODP_POOL_BUFFER;
	pool_params.buf.num = if_count * (PKTIO_MAX_IN_QUEUES +
					  MAX_TX_BURST_BUFS);
	pool_params.buf.size = MAX(sizeof(odp_queue_t), sizeof(tx_burst_t *));
	/* no cache needed, all allocated at startup */
	pool_params.buf.cache_size = 0;
	pool_params.stats.all = pool_capa.buf.stats.all;

	pktio_shm->pools.bufpool_odp = odp_pool_create("pktio-ctrl-pool",
						       &pool_params);
	if (pktio_shm->pools.bufpool_odp == ODP_POOL_INVALID)
		APPL_EXIT_FAILURE("pktio-ctrl-pool creation failed");

	odp_pool_print(pktio_shm->pools.bufpool_odp);

	/*
	 * Create the pktio pkt pool used for actual input pkts.
	 * Create the pool either as an EM- or ODP-pool.
	 */
	if (pktpool_em)
		pktio_pool_create_em(if_count);
	else
		pktio_pool_create_odp(if_count);
}

/**
 * Helper to pktio_pool_destroy(): destroy the EM event-pool used for pktio
 */
static void pktio_pool_destroy_em(void)
{
	APPL_PRINT("%s(): deleting the EM pktio-pool:\n", __func__);
	em_pool_info_print(pktio_shm->pools.pktpool_em);

	if (em_pool_delete(pktio_shm->pools.pktpool_em) != EM_OK)
		APPL_EXIT_FAILURE("EM pktio-pool delete failed.");

	pktio_shm->pools.pktpool_em = EM_POOL_UNDEF;
	pktio_shm->pools.pktpool_odp = ODP_POOL_INVALID;
}

/**
 * Helper to pktio_pool_destroy(): destroy the ODP pkt-pool used for pktio
 */
static void pktio_pool_destroy_odp(void)
{
	APPL_PRINT("%s(): destroying the ODP pktio-pool\n", __func__);
	if (odp_pool_destroy(pktio_shm->pools.pktpool_odp) != 0)
		APPL_EXIT_FAILURE("ODP pktio-pool destroy failed.");

	pktio_shm->pools.pktpool_odp = ODP_POOL_INVALID;
}

/**
 * Destroy the memory pool used by pkt-io
 */
void pktio_pool_destroy(bool pktpool_em)
{
	if (pktpool_em)
		pktio_pool_destroy_em();
	else
		pktio_pool_destroy_odp();

	if (odp_pool_destroy(pktio_shm->pools.bufpool_odp) != 0)
		APPL_EXIT_FAILURE("pktio-ctrl-pool destroy failed.");
	pktio_shm->pools.bufpool_odp = ODP_POOL_INVALID;
}

void pktio_init(const appl_conf_t *appl_conf)
{
	odp_queue_param_t queue_param;
	odp_queue_t odp_queue;
	int i;

	pktio_shm->ifs.count = appl_conf->pktio.if_count;
	pktio_shm->ifs.num_created = 0;
	pktio_shm->default_queue = EM_QUEUE_UNDEF;

	/*
	 * Create a queue to hold the shared pktin-queues in. Each core needs
	 * to dequeue one pktin-queue to be able to use it to receive packets
	 */
	odp_queue_param_init(&queue_param);
	queue_param.type = ODP_QUEUE_TYPE_PLAIN;
	queue_param.enq_mode = ODP_QUEUE_OP_MT;
	queue_param.deq_mode = ODP_QUEUE_OP_MT;
	queue_param.order = ODP_QUEUE_ORDER_IGNORE;

	odp_queue = odp_queue_create("pktin.queues-queue", &queue_param);
	if (unlikely(odp_queue == ODP_QUEUE_INVALID))
		APPL_EXIT_FAILURE("odp_queue_create() fails");
	pktio_shm->pktin.queues_queue = odp_queue;

	/*
	 * Create a queue to hold the shared tx-burst buffers,
	 * used when draining the available tx-burst buffers
	 */
	odp_queue_param_init(&queue_param);
	queue_param.type = ODP_QUEUE_TYPE_PLAIN;
	queue_param.enq_mode = ODP_QUEUE_OP_MT;
	queue_param.deq_mode = ODP_QUEUE_OP_MT;
	queue_param.order = ODP_QUEUE_ORDER_IGNORE;

	odp_queue = odp_queue_create("pktout.tx-bursts-queue", &queue_param);
	if (unlikely(odp_queue == ODP_QUEUE_INVALID))
		APPL_EXIT_FAILURE("odp_queue_create() fails");
	pktio_shm->pktout.tx_bursts_queue = odp_queue;

	for (i = 0; i < MAX_RX_PKT_QUEUES; i++) {
		pktio_shm->rx_pkt_queues[i].pos = i;
		pktio_shm->rx_pkt_queues[i].queue = EM_QUEUE_UNDEF;
	}

	odp_ticketlock_init(&pktio_shm->tbl_lookup.lock);
	pktio_shm->tbl_lookup.tbl_idx = 0;
	pktio_shm->tbl_lookup.ops = odph_cuckoo_table_ops;
	odp_ticketlock_lock(&pktio_shm->tbl_lookup.lock);
	pktio_shm->tbl_lookup.tbl =
	pktio_shm->tbl_lookup.ops.f_create("RX-lookup-tbl", MAX_RX_PKT_QUEUES,
					   sizeof(pkt_q_hash_key_t),
					   sizeof(rx_pkt_queue_t));
	odp_ticketlock_unlock(&pktio_shm->tbl_lookup.lock);
	if (unlikely(pktio_shm->tbl_lookup.tbl == NULL))
		APPL_EXIT_FAILURE("rx pkt lookup table creation fails");
}

void pktio_deinit(const appl_conf_t *appl_conf)
{
	(void)appl_conf;

	odp_queue_destroy(pktio_shm->pktin.queues_queue);
	odp_queue_destroy(pktio_shm->pktout.tx_bursts_queue);

	pktio_shm->tbl_lookup.ops.f_des(pktio_shm->tbl_lookup.tbl);
}

static void pktio_tx_buffering_create(int if_num)
{
	tx_burst_t *tx_burst;
	odp_queue_param_t queue_param;
	odp_queue_t odp_queue;
	int pktout_idx;
	odp_queue_t pktout_queue;
	int ret, i;
	char name[ODP_QUEUE_NAME_LEN];

	const int pktout_num_queues = pktio_shm->pktout.num_queues[if_num];

	for (i = 0; i < MAX_TX_BURST_BUFS; i++) {
		tx_burst = &pktio_shm->tx_burst[if_num][i];

		odp_atomic_init_u64(&tx_burst->cnt, 0);
		odp_spinlock_init(&tx_burst->lock);

		odp_queue_param_init(&queue_param);
		queue_param.type = ODP_QUEUE_TYPE_PLAIN;
		queue_param.enq_mode = ODP_QUEUE_OP_MT;
		queue_param.deq_mode = ODP_QUEUE_OP_MT_UNSAFE;
		/* ignore odp ordering, EM handles output order, just buffer */
		queue_param.order = ODP_QUEUE_ORDER_IGNORE;

		snprintf(name, ODP_QUEUE_NAME_LEN, "tx-burst-if%d-%03d",
			 if_num, i);
		name[ODP_QUEUE_NAME_LEN - 1] = '\0';

		odp_queue = odp_queue_create(name, &queue_param);
		if (unlikely(odp_queue == ODP_QUEUE_INVALID))
			APPL_EXIT_FAILURE("odp_queue_create() fails:if=%i(%i)",
					  if_num, i);
		tx_burst->queue = odp_queue;
		tx_burst->if_port = if_num;

		pktout_idx = i % pktout_num_queues;
		pktout_queue = pktio_shm->pktout.queues[if_num][pktout_idx];
		tx_burst->pktout_queue = pktout_queue;

		odp_event_t event;
		tx_burst_t **tx_burst_ptr;
		odp_buffer_t buf = odp_buffer_alloc(pktio_shm->pools.bufpool_odp);

		if (unlikely(buf == ODP_BUFFER_INVALID)) {
			odp_pool_stats_t stats;
			int ret = odp_pool_stats(pktio_shm->pools.bufpool_odp, &stats);

			if (!ret) {
				APPL_PRINT("bufpool stats{avail=%lu allocs=%lu fails=%lu}\n",
					   stats.available, stats.alloc_ops, stats.alloc_fails);
			}
			APPL_EXIT_FAILURE("buf alloc fails: if_num:%d, i:%d",
					  if_num, i);
		}
		if (unlikely(odp_buffer_size(buf) < sizeof(tx_burst/*ptr*/)))
			APPL_EXIT_FAILURE("buf too small");

		event = odp_buffer_to_event(buf);
		tx_burst_ptr = odp_buffer_addr(buf);
		*tx_burst_ptr = tx_burst;

		ret = odp_queue_enq(pktio_shm->pktout.tx_bursts_queue, event);
		if (unlikely(ret != 0))
			APPL_EXIT_FAILURE("enqueue fails");
	}
}

static void pktio_tx_buffering_destroy(void)
{
	tx_burst_t *tx_burst;
	int num;

	while (tx_drain_burst_acquire(&tx_burst) == 0) {
		do {
			num = odp_queue_deq_multi(tx_burst->queue,
						  pktio_locm.ev_burst,
						  MAX_PKT_BURST_TX);
			if (unlikely(num <= 0))
				break;

			odp_atomic_sub_u64(&tx_burst->cnt, (uint64_t)num);
			odp_event_free_multi(pktio_locm.ev_burst, num);
		} while (num > 0);

		odp_queue_destroy(tx_burst->queue);
		odp_event_free(pktio_locm.tx_burst_timed_event);
	}
}

static inline void
pktin_queue_queueing_create(int if_num)
{
	odp_event_t event;
	odp_pktin_queue_t *pktin_queue;
	odp_buffer_t buf;
	int num_rx;
	int ret, i;

	num_rx = pktio_shm->pktin.num_queues[if_num];

	for (i = 0; i < num_rx; i++) {
		buf = odp_buffer_alloc(pktio_shm->pools.bufpool_odp);
		if (unlikely(buf == ODP_BUFFER_INVALID))
			APPL_EXIT_FAILURE("buf alloc fails");
		if (unlikely(odp_buffer_size(buf) < sizeof(odp_queue_t)))
			APPL_EXIT_FAILURE("buf too small");
		event = odp_buffer_to_event(buf);
		pktin_queue = odp_buffer_addr(buf);
		/* store the pktin-queue handle into the event payload */
		*pktin_queue = pktio_shm->pktin.queues[if_num][i];

		ret = odp_queue_enq(pktio_shm->pktin.queues_queue, event);
		if (unlikely(ret != 0))
			APPL_EXIT_FAILURE("enqueue fails");
	}
}

static inline void
pktin_queue_queueing_destroy(void)
{
	odp_pktin_queue_t pktin_queue;

	while (pktin_queue_acquire(&pktin_queue) == 0)
		odp_event_free(pktio_locm.pktin_queue_event);
}

int /* if_id */
pktio_create(const char *dev, int num_workers)
{
	int if_idx = -1; /* return value */
	odp_pktio_param_t pktio_param;
	odp_pktio_t pktio;
	odp_pktio_capability_t pktio_capa;
	odp_pktio_config_t pktio_config;
	odp_pktin_queue_param_t pktin_queue_param;
	odp_pktout_queue_param_t pktout_queue_param;
	odp_pktio_info_t info;
	odp_pktio_op_mode_t mode_rx;
	odp_pktio_op_mode_t mode_tx;
	int num_rx, num_tx, max;
	int ret;

	odp_pktio_param_init(&pktio_param);

	/* DIRECT mode for Rx */
	pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;
	/* QUEUE mode for Tx to preserve packet order if needed */
	pktio_param.out_mode = ODP_PKTOUT_MODE_QUEUE;

	pktio = odp_pktio_open(dev, pktio_shm->pools.pktpool_odp, &pktio_param);
	if (pktio == ODP_PKTIO_INVALID)
		APPL_EXIT_FAILURE("pktio create failed for %s\n", dev);

	if (odp_pktio_info(pktio, &info))
		APPL_EXIT_FAILURE("pktio info failed %s", dev);

	if_idx = odp_pktio_index(pktio);
	if (if_idx < 0 || if_idx >= IF_MAX_NUM)
		APPL_EXIT_FAILURE("pktio index:%d too large, dev:%s",
				  if_idx, dev);

	APPL_PRINT("\n%s(dev=%s):\n", __func__, dev);
	APPL_PRINT("\tcreated pktio:%" PRIu64 " idx:%d, dev: %s, drv: %s\n",
		   odp_pktio_to_u64(pktio), if_idx, dev, info.drv_name);

	ret = odp_pktio_capability(pktio, &pktio_capa);
	if (ret != 0)
		APPL_EXIT_FAILURE("pktio capability query failed: %s (%i)",
				  dev, ret);

	odp_pktio_config_init(&pktio_config);
	pktio_config.parser.layer = ODP_PKTIO_PARSER_LAYER_NONE;
	/* pktio_config.parser.layer = ODP_PKTIO_PARSER_LAYER_L4; */

	ret = odp_pktio_config(pktio, &pktio_config);
	if (ret != 0)
		APPL_EXIT_FAILURE("pktio config failed: %s (%i)",
				  dev, ret);

	odp_pktin_queue_param_init(&pktin_queue_param);
	odp_pktout_queue_param_init(&pktout_queue_param);

	mode_rx = ODP_PKTIO_OP_MT_UNSAFE;
	mode_tx = ODP_PKTIO_OP_MT;

	num_rx = MIN((int)pktio_capa.max_input_queues, PKTIO_MAX_IN_QUEUES);
	APPL_PRINT("\tmax number of pktio %s input queues:%i, using:%i\n",
		   dev, pktio_capa.max_input_queues, num_rx);

	max = MIN((int)pktio_capa.max_output_queues, PKTIO_MAX_OUT_QUEUES);
	num_tx = MIN(2 * num_workers, max);
	APPL_PRINT("\tmax number of pktio %s output queues:%i, using:%i\n",
		   dev, pktio_capa.max_output_queues, num_tx);

	pktin_queue_param.hash_enable = 1;
	pktin_queue_param.classifier_enable = 0;
	pktin_queue_param.hash_proto.proto.ipv4_udp = 1;
	pktin_queue_param.num_queues = num_rx;
	pktin_queue_param.op_mode = mode_rx;

	pktout_queue_param.num_queues = num_tx;
	pktout_queue_param.op_mode = mode_tx;

	ret = odp_pktin_queue_config(pktio, &pktin_queue_param);
	if (ret < 0)
		APPL_EXIT_FAILURE("pktio input queue config failed %s (%i)",
				  dev, ret);
	ret = odp_pktout_queue_config(pktio, &pktout_queue_param);
	if (ret < 0)
		APPL_EXIT_FAILURE("pktio output queue config failed %s (%i)",
				  dev, ret);

	ret = odp_pktin_queue(pktio, pktio_shm->pktin.queues[if_idx], num_rx);
	if (ret != num_rx || ret > PKTIO_MAX_IN_QUEUES)
		APPL_EXIT_FAILURE("pktio pktin queue query failed %s (%i)",
				  dev, ret);
	pktio_shm->pktin.num_queues[if_idx] = num_rx;

	/*
	 * Store all pktin queues in another queue - core dequeues from this
	 * 'rx access queues' to use an pktin queue.
	 */
	pktin_queue_queueing_create(if_idx);

	ret = odp_pktout_event_queue(pktio, pktio_shm->pktout.queues[if_idx],
				     num_tx);
	if (ret != num_tx || ret > PKTIO_MAX_OUT_QUEUES)
		APPL_EXIT_FAILURE("pktio pktout queue query failed %s (%i)",
				  dev, ret);
	pktio_shm->pktout.num_queues[if_idx] = num_tx;

	/* Create Tx buffers */
	pktio_tx_buffering_create(if_idx);

	/* Start the pktio to complete configuration... */
	ret = odp_pktio_start(pktio);
	if (ret != 0)
		APPL_EXIT_FAILURE("Unable to start %s", dev);
	/*
	 * ...and stop it immediately to block odp_pktin_recv() from receiving
	 * pkts until application setup is ready.
	 * The application will start pktio when ready through pktio_start().
	 */
	ret = odp_pktio_stop(pktio);
	if (ret != 0)
		APPL_EXIT_FAILURE("Unable to stop %s", dev);

	APPL_PRINT("\tcreated pktio %s; direct input mode, queue output mode",
		   dev);
	odp_pktio_print(pktio);

	pktio_shm->ifs.idx[pktio_shm->ifs.num_created] = if_idx;
	pktio_shm->ifs.pktio_hdl[if_idx] = pktio;
	pktio_shm->ifs.num_created++;

	return if_idx;
}

void
pktio_start(void)
{
	if (pktio_shm->ifs.num_created != pktio_shm->ifs.count)
		APPL_EXIT_FAILURE("Pktio IFs created:%d != IF count:%d",
				  pktio_shm->ifs.num_created,
				  pktio_shm->ifs.count);

	for (int i = 0; i < pktio_shm->ifs.count; i++) {
		int if_idx = pktio_shm->ifs.idx[i];
		odp_pktio_t pktio = pktio_shm->ifs.pktio_hdl[if_idx];
		int ret = odp_pktio_start(pktio);

		if (unlikely(ret != 0))
			APPL_EXIT_FAILURE("Unable to start if:%d", if_idx);
		APPL_PRINT("%s(): if:%d\n", __func__, if_idx);
	}

	odp_mb_full();
	pktio_shm->pktio_started = 1;
}

void pktio_halt(void)
{
	pktio_shm->pktio_started = 0;
	odp_mb_full();
	APPL_PRINT("\n%s() on EM-core %d\n", __func__, em_core_id());
}

void pktio_stop(void)
{
	for (int i = 0; i < pktio_shm->ifs.count; i++) {
		int if_idx = pktio_shm->ifs.idx[i];
		odp_pktio_t pktio = pktio_shm->ifs.pktio_hdl[if_idx];
		int ret = odp_pktio_stop(pktio);

		if (unlikely(ret != 0))
			APPL_EXIT_FAILURE("Unable to stop if:%d", if_idx);
		APPL_PRINT("%s(): if:%d\n", __func__, if_idx);
	}
}

void pktio_close(void)
{
	for (int i = 0; i < pktio_shm->ifs.count; i++) {
		int if_idx = pktio_shm->ifs.idx[i];
		odp_pktio_t pktio = pktio_shm->ifs.pktio_hdl[if_idx];
		int ret = odp_pktio_close(pktio);

		if (unlikely(ret != 0))
			APPL_EXIT_FAILURE("pktio close failed for if:%d",
					  if_idx);
			pktio_shm->ifs.pktio_hdl[if_idx] = ODP_PKTIO_INVALID;
	}

	pktin_queue_queueing_destroy();
	pktio_tx_buffering_destroy();
}

static inline int
pktin_queue_acquire(odp_pktin_queue_t *const pktin_queue /*out*/)
{
	odp_event_t pktin_queue_event;
	odp_buffer_t buf;
	odp_pktin_queue_t *pktin_qptr;

	pktin_queue_event = odp_queue_deq(pktio_shm->pktin.queues_queue);
	if (unlikely(pktin_queue_event == ODP_EVENT_INVALID))
		return -1;

	/* store event locally for reuse, i.e. enqueue it back later */
	pktio_locm.pktin_queue_event = pktin_queue_event;

	buf = odp_buffer_from_event(pktin_queue_event);
	pktin_qptr = odp_buffer_addr(buf);
	*pktin_queue = *pktin_qptr;

	return 0;
}

static inline void
pktin_queue_release(void)
{
	const int ret = odp_queue_enq(pktio_shm->pktin.queues_queue,
				      pktio_locm.pktin_queue_event);
	if (unlikely(ret != 0))
		APPL_EXIT_FAILURE("enqueue fails");
}

static inline int
pktio_rx(void)
{
	rx_queue_burst_t *const rx_qbursts = pktio_locm.rx_qbursts;
	odp_pktin_queue_t pktin_queue;
	odp_packet_t pkt_tbl[MAX_PKT_BURST_RX];
	int ret, pkts;
	int poll_rounds = 0;
	int pkts_enqueued = 0; /* return value */

	const odph_table_get_value f_get = pktio_shm->tbl_lookup.ops.f_get;

	if (unlikely(!pktio_shm->pktio_started))
		return 0;

	ret = pktin_queue_acquire(&pktin_queue);
	if (unlikely(ret != 0))
		return 0;

	do {
		int valid_pkts = 0;
		int i;

		pkts = odp_pktin_recv(pktin_queue, pkt_tbl, MAX_PKT_BURST_RX);
		if (unlikely(pkts <= 0))
			goto pktio_rx_end;

		for (i = 0; i < pkts; i++) {
			const odp_packet_t pkt = pkt_tbl[i];
			void *const pkt_data = odp_packet_data(pkt);

			/*
			 * If 'pktio_config.parser.layer =
			 *     ODP_PKTIO_PARSER_LAYER_L4;' then the following
			 *     better checks can be used (is slower though).
			 * if (unlikely(!odp_packet_has_udp(pkt))) {
			 *	odp_packet_free(pkt);
			 *	continue;
			 * }
			 *
			 * pkt_data = odp_packet_data(pkt);
			 * ip = (odph_ipv4hdr_t *)((uintptr_t)pkt_data +
			 *			odp_packet_l3_offset(pkt));
			 * udp = (odph_udphdr_t *)((uintptr_t)pkt_data +
			 *			odp_packet_l4_offset(pkt));
			 */

			/* Note: no actual checks if the headers are present */
			odph_ipv4hdr_t *const ip = (odph_ipv4hdr_t *)
				((uintptr_t)pkt_data + sizeof(odph_ethhdr_t));
			odph_udphdr_t *const udp = (odph_udphdr_t *)
				((uintptr_t)ip + sizeof(odph_ipv4hdr_t));
			/*
			 * NOTE! network-to-CPU conversion not needed here.
			 * Setup stores network-order in hash to avoid
			 * conversion for every packet.
			 */
			pktio_locm.keys[i].ip_dst = ip->dst_addr;
			pktio_locm.keys[i].proto = ip->proto;
			pktio_locm.keys[i].port_dst =
				likely(ip->proto == ODPH_IPPROTO_UDP ||
				       ip->proto == ODPH_IPPROTO_TCP) ?
				       udp->dst_port : 0;
		}

		for (i = 0; i < pkts; i++) {
			const odp_packet_t pkt = pkt_tbl[i];
			rx_pkt_queue_t rx_pkt_queue;
			em_queue_t queue;
			int pos;

			/* table(hash) lookup to find queue */
			ret = f_get(pktio_shm->tbl_lookup.tbl,
				    &pktio_locm.keys[i],
				    &rx_pkt_queue, sizeof(rx_pkt_queue_t));
			if (likely(ret == 0)) {
				/* found */
				pos = rx_pkt_queue.pos;
				queue = rx_pkt_queue.queue;
			} else {
				/* not found, use default queue if set */
				pos = MAX_RX_PKT_QUEUES; /* reserved space +1*/
				queue = pktio_shm->default_queue;
				if (unlikely(queue == EM_QUEUE_UNDEF)) {
					odp_packet_free(pkt);
					continue;
				}
			}

			pktio_locm.positions[valid_pkts++] = pos;
			rx_qbursts[pos].sent = 0;
			rx_qbursts[pos].queue = queue;
			rx_qbursts[pos].pkt_tbl[rx_qbursts[pos].pkt_cnt++] =
				pkt;
		}

		for (i = 0; i < valid_pkts; i++) {
			const int pos = pktio_locm.positions[i];

			if (rx_qbursts[pos].sent)
				continue;

			const int num = rx_qbursts[pos].pkt_cnt;
			const em_queue_t queue = rx_qbursts[pos].queue;

			/* Enqueue pkts into em-odp */
			pkts_enqueued += pkt_enqueue(rx_qbursts[pos].pkt_tbl,
						     num, queue);
			rx_qbursts[pos].sent = 1;
			rx_qbursts[pos].pkt_cnt = 0;
		}

	} while (pkts == MAX_PKT_BURST_RX &&
		 ++poll_rounds < MAX_RX_POLL_ROUNDS);

pktio_rx_end:
	pktin_queue_release();

	return pkts_enqueued;
}

static inline int
pktio_tx_burst(tx_burst_t *const tx_burst)
{
	if (odp_spinlock_is_locked(&tx_burst->lock) ||
	    odp_spinlock_trylock(&tx_burst->lock) == 0)
		return 0;

	const int num = odp_queue_deq_multi(tx_burst->queue,
					    pktio_locm.ev_burst,
					    MAX_PKT_BURST_TX);
	if (unlikely(num <= 0)) {
		odp_spinlock_unlock(&tx_burst->lock);
		return 0;
	}

	odp_atomic_sub_u64(&tx_burst->cnt, (uint64_t)num);

	const odp_queue_t pktout_queue = tx_burst->pktout_queue;
	/* Enqueue a tx burst onto the pktio queue for transmission */
	int ret = odp_queue_enq_multi(pktout_queue, pktio_locm.ev_burst, num);

	odp_spinlock_unlock(&tx_burst->lock);

	if (unlikely(ret != num)) {
		if (ret < 0)
			ret = 0;
		odp_event_free_multi(&pktio_locm.ev_burst[ret], num - ret);
	}

	return ret;
}

/**
 * @brief User provided output-queue callback function (em_output_func_t).
 *
 * Transmit events(pkts) via Eth Tx queues.
 *
 * @return The number of events actually transmitted (<= num)
 */
int pktio_tx(const em_event_t events[], const unsigned int num,
	     const em_queue_t output_queue, void *output_fn_args)
{
	/* Create idx to select tx-burst, always same idx for same em queue */
	const int burst_idx = (int)((uintptr_t)output_queue %
				    MAX_TX_BURST_BUFS);
	pktio_tx_fn_args_t *const args = output_fn_args;
	const int if_port = (int)(args->if_id % IF_MAX_NUM);
	/* Select tx-burst onto which to temporaily store pkt/event until tx */
	tx_burst_t *const tx_burst = &pktio_shm->tx_burst[if_port][burst_idx];
	uint64_t prev_cnt;
	int ret;

	if (unlikely(num == 0 || !pktio_shm->pktio_started))
		return 0;

	/* Convert into ODP-events */
	odp_event_t odp_events[num];

	em_odp_events2odp(events, odp_events, num);

	/*
	 * Mark all events as "free" from EM point of view - ODP will transmit
	 * and free the events (=odp-pkts).
	 */
	em_event_mark_free_multi(events, num);

	/*
	 * 'sched_ctx_type = em_sched_context_type_current(&src_sched_queue)'
	 * could be used to determine the need for maintaining event order for
	 * output. Also em_queue_get_type(src_sched_queue) could further be used
	 * if not caring about a potentially ended sched-context caused by an
	 * earlier call to em_atomic/ordered_processing_end().
	 * Here, none of this is done, since every event will be buffered and
	 * sent out in order regardless of sched context type or queue type.
	 */

	ret = odp_queue_enq_multi(tx_burst->queue, odp_events, num);
	if (unlikely(ret < 0)) {
		/* failure: don't return, see if a burst can be Tx anyway */
		ret = 0;
	}

	prev_cnt = odp_atomic_fetch_add_u64(&tx_burst->cnt, ret);
	if (prev_cnt >= MAX_PKT_BURST_TX - 1)
		(void)pktio_tx_burst(tx_burst);

	if (unlikely(ret < (int)num))
		em_event_unmark_free_multi(&events[ret], num - ret);

	return ret;
}

static inline int
tx_drain_burst_acquire(tx_burst_t **const tx_drain_burst)
{
	odp_event_t tx_burst_timed_event;
	odp_buffer_t buf;
	tx_burst_t **tx_burst_ptr;

	tx_burst_timed_event = odp_queue_deq(pktio_shm->pktout.tx_bursts_queue);
	if (unlikely(tx_burst_timed_event == ODP_EVENT_INVALID))
		return -1;

	/* store event locally for reuse, i.e. enqueue it back later */
	pktio_locm.tx_burst_timed_event = tx_burst_timed_event;

	buf = odp_buffer_from_event(tx_burst_timed_event);
	tx_burst_ptr = odp_buffer_addr(buf);
	*tx_drain_burst = *tx_burst_ptr;

	return 0;
}

static inline void
tx_drain_burst_release(void) {
	const int ret =
	odp_queue_enq(pktio_shm->pktout.tx_bursts_queue,
		      pktio_locm.tx_burst_timed_event);

	if (unlikely(ret != 0))
		APPL_EXIT_FAILURE("enqueue fails");
}

static inline int pktio_tx_drain(void)
{
	const uint64_t curr = odp_cpu_cycles(); /* core-local timestamp */
	const uint64_t prev = pktio_locm.tx_prev_cycles;
	const uint64_t diff = likely(curr >= prev) ?
		curr - prev : UINT64_MAX - prev + curr + 1;
	int ret = 0;

	/* TX burst queue drain */
	if (unlikely(diff > BURST_TX_DRAIN)) {
		tx_burst_t *tx_drain_burst;

		if (tx_drain_burst_acquire(&tx_drain_burst) == 0) {
			ret = pktio_tx_burst(tx_drain_burst);
			/* Update timestamp for next round */
			pktio_locm.tx_prev_cycles = curr;
			tx_drain_burst_release();
		}
	}

	return ret;
}

void pktio_add_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst,
		     em_queue_t queue)
{
	pkt_q_hash_key_t key;
	int ret, idx;

	/* Store in network format to avoid conversion during Rx lookup */
	key.ip_dst = htonl(ipv4_dst);
	key.port_dst = htons(port_dst);
	key.proto = proto;

	odp_ticketlock_lock(&pktio_shm->tbl_lookup.lock);

	idx = pktio_shm->tbl_lookup.tbl_idx;
	if (unlikely(idx != pktio_shm->rx_pkt_queues[idx].pos)) {
		odp_ticketlock_unlock(&pktio_shm->tbl_lookup.lock);
		APPL_EXIT_FAILURE("tbl insertion failed, idx(%i) != pos(%i)",
				  idx, pktio_shm->rx_pkt_queues[idx].pos);
		return;
	}

	if (unlikely(em_queue_get_type(queue) == EM_QUEUE_TYPE_UNDEF)) {
		odp_ticketlock_unlock(&pktio_shm->tbl_lookup.lock);
		APPL_EXIT_FAILURE("Invalid queue:%" PRI_QUEUE "", queue);
		return;
	}

	pktio_shm->rx_pkt_queues[idx].queue = queue;

	ret = pktio_shm->tbl_lookup.ops.f_put(pktio_shm->tbl_lookup.tbl, &key,
					      &pktio_shm->rx_pkt_queues[idx]);
	if (likely(ret == 0))
		pktio_shm->tbl_lookup.tbl_idx++;

	odp_ticketlock_unlock(&pktio_shm->tbl_lookup.lock);

	if (unlikely(ret != 0))
		APPL_EXIT_FAILURE("tbl insertion failed");
}

int pktio_default_queue(em_queue_t queue)
{
	if (unlikely(em_queue_get_type(queue) == EM_QUEUE_TYPE_UNDEF)) {
		APPL_EXIT_FAILURE("Invalid queue:%" PRI_QUEUE "", queue);
		return -1;
	}

	pktio_shm->default_queue = queue;

	return 0;
}

em_queue_t pktio_lookup_sw(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst)
{
	em_queue_t queue;
	rx_pkt_queue_t rx_pkt_queue;
	int ret, pos;
	/* Store in network format to avoid conversion during Rx lookup */
	pkt_q_hash_key_t key = {.ip_dst = htonl(ipv4_dst),
				.port_dst = htons(port_dst),
				.proto = proto};

	/* table(hash) lookup to find queue */
	ret = pktio_shm->tbl_lookup.ops.f_get(pktio_shm->tbl_lookup.tbl,
					      &key, &rx_pkt_queue,
					      sizeof(rx_pkt_queue_t));

	if (likely(ret == 0)) {
		/* found */
		pos = rx_pkt_queue.pos;
		queue = rx_pkt_queue.queue;
		if (unlikely(queue != pktio_shm->rx_pkt_queues[pos].queue)) {
			APPL_EXIT_FAILURE("%" PRI_QUEUE "!= %" PRI_QUEUE "",
					  queue,
					  pktio_shm->rx_pkt_queues[pos].queue);
			return EM_QUEUE_UNDEF;
		}
	} else {
		queue = EM_QUEUE_UNDEF;
	}

	return queue;
}

odp_pool_t pktio_pool_get(void)
{
	return pktio_shm->pools.pktpool_odp;
}
