/*
 *   Copyright (c) 2015-2022, Nokia Solutions and Networks
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

#define PKTIO_PKT_POOL_NUM_BUFS  (32 * 1024)
#define PKTIO_PKT_POOL_BUF_SIZE  1536
#define PKTIO_VEC_POOL_VEC_SIZE  32
#define PKTIO_VEC_SIZE           PKTIO_VEC_POOL_VEC_SIZE
#define PKTIO_VEC_TMO            ODP_TIME_MSEC_IN_NS

static pktio_shm_t *pktio_shm;
static __thread pktio_locm_t pktio_locm ODP_ALIGNED_CACHE;

static inline tx_burst_t *tx_drain_burst_acquire(void);
static inline int pktin_queue_acquire(odp_pktin_queue_t **pktin_queue_ptr /*out*/);
static inline odp_queue_t plain_queue_acquire(void);

const char *pktin_mode_str(pktin_mode_t in_mode)
{
	const char *str;

	switch (in_mode) {
	case DIRECT_RECV:
		str = "DIRECT_RECV";
		break;
	case PLAIN_QUEUE:
		str = "PLAIN_QUEUE";
		break;
	case SCHED_PARALLEL:
		str = "SCHED_PARALLEL";
		break;
	case SCHED_ATOMIC:
		str = "SCHED_ATOMIC";
		break;
	case SCHED_ORDERED:
		str = "SCHED_ORDERED";
		break;
	default:
		str = "UNKNOWN";
		break;
	}

	return str;
}

bool pktin_polled_mode(pktin_mode_t in_mode)
{
	return in_mode == DIRECT_RECV ||
	       in_mode == PLAIN_QUEUE;
}

bool pktin_sched_mode(pktin_mode_t in_mode)
{
	return in_mode == SCHED_PARALLEL ||
	       in_mode == SCHED_ATOMIC   ||
	       in_mode == SCHED_ORDERED;
}

void pktio_mem_reserve(void)
{
	odp_shm_t shm;
	uint32_t flags = 0;

	/* Sanity check: em_shm should not be set yet */
	if (unlikely(pktio_shm != NULL))
		APPL_EXIT_FAILURE("pktio shared memory ptr set - already initialized?");

	odp_shm_capability_t shm_capa;
	int ret = odp_shm_capability(&shm_capa);

	if (unlikely(ret))
		APPL_EXIT_FAILURE("shm capability error:%d", ret);

	if (shm_capa.flags & ODP_SHM_SINGLE_VA)
		flags |= ODP_SHM_SINGLE_VA;

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

void pktio_mem_lookup(bool is_thread_per_core)
{
	odp_shm_t shm;
	pktio_shm_t *shm_addr;

	shm = odp_shm_lookup("pktio_shm");

	shm_addr = odp_shm_addr(shm);
	if (unlikely(shm_addr == NULL))
		APPL_EXIT_FAILURE("pktio shared mem addr lookup failed.");

	/*
	 * Set pktio_shm in process-per-core mode, each process has own pointer.
	 */
	if (!is_thread_per_core && pktio_shm != shm_addr)
		pktio_shm = shm_addr;
}

void pktio_mem_free(void)
{
	odp_shm_t shm;

	shm = odp_shm_lookup("pktio_shm");
	if (unlikely(shm == ODP_SHM_INVALID))
		APPL_EXIT_FAILURE("pktio shared mem lookup for free failed.");

	if (odp_shm_free(shm) != 0)
		APPL_EXIT_FAILURE("pktio shared mem free failed.");
	pktio_shm = NULL;
}

/**
 * Helper to pktio_pool_create(): create the pktio pool as an EM event-pool
 */
static void pktio_pool_create_em(int if_count, const odp_pool_capability_t *pool_capa)
{
	/*
	 * Create the pktio pkt pool used for actual input pkts.
	 * Create the pool as an EM-pool (and convert into an ODP-pool where
	 * needed) to be able to utilize EM's Event State Verification (ESV)
	 * in the 'esv.prealloc_pools = true' mode (see config/em-odp.conf).
	 */
	em_pool_cfg_t pool_cfg;
	em_pool_t pool;

	em_pool_cfg_init(&pool_cfg);
	pool_cfg.event_type = EM_EVENT_TYPE_PACKET;
	pool_cfg.num_subpools = 1;
	pool_cfg.subpool[0].size = PKTIO_PKT_POOL_BUF_SIZE;
	pool_cfg.subpool[0].num = if_count * PKTIO_PKT_POOL_NUM_BUFS;
	/* Use max thread-local pkt-cache size to speed up pktio allocs */
	pool_cfg.subpool[0].cache_size = pool_capa->pkt.max_cache_size;
	pool = em_pool_create("pktio-pool-em", EM_POOL_UNDEF, &pool_cfg);
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
static void pktio_pool_create_odp(int if_count, const odp_pool_capability_t *pool_capa)
{
	odp_pool_param_t pool_params;

	(void)pool_capa;

	odp_pool_param_init(&pool_params);
	pool_params.pkt.num = if_count * PKTIO_PKT_POOL_NUM_BUFS;
	/* pool_params.pkt.max_num = default */
	pool_params.pkt.len = PKTIO_PKT_POOL_BUF_SIZE;
	pool_params.pkt.max_len = PKTIO_PKT_POOL_BUF_SIZE;
	pool_params.pkt.seg_len = PKTIO_PKT_POOL_BUF_SIZE;

	pool_params.type = ODP_POOL_PACKET;
	pool_params.pkt.uarea_size = em_odp_event_hdr_size();

	odp_pool_t odp_pool = odp_pool_create("pktio-pool-odp", &pool_params);

	if (odp_pool == ODP_POOL_INVALID)
		APPL_EXIT_FAILURE("pktio pool creation failed");

	/* Store the ODP pktio pool */
	pktio_shm->pools.pktpool_odp = odp_pool;
	pktio_shm->pools.pktpool_em = EM_POOL_UNDEF;

	odp_pool_print(pktio_shm->pools.pktpool_odp);
}

static void pktio_vectorpool_create_em(int if_count, const odp_pool_capability_t *pool_capa)
{
	if (unlikely(pool_capa->vector.max_pools == 0 ||
		     pool_capa->vector.max_size == 0))
		APPL_EXIT_FAILURE("ODP pktin vectors not supported!");

	uint32_t vec_size = PKTIO_VEC_POOL_VEC_SIZE;
	uint32_t num_pkt = PKTIO_PKT_POOL_NUM_BUFS * if_count;
	uint32_t num_vec = num_pkt; /* worst case: 1 pkt per vector */

	if (vec_size > pool_capa->vector.max_size) {
		vec_size = pool_capa->vector.max_size;
		APPL_PRINT("\nWarning: pktin vector size reduced to %u\n\n",
			   vec_size);
	}

	if (pool_capa->vector.max_num /* 0=limited only by pool memsize */ &&
	    num_vec > pool_capa->vector.max_num) {
		num_vec = pool_capa->vector.max_num;
		APPL_PRINT("\nWarning: pktin number of vectors reduced to %u\n\n",
			   num_vec);
	}

	em_pool_cfg_t pool_cfg;

	em_pool_cfg_init(&pool_cfg);
	pool_cfg.event_type = EM_EVENT_TYPE_VECTOR;
	pool_cfg.num_subpools = 1;

	pool_cfg.subpool[0].size = vec_size; /* nbr of events in vector */
	pool_cfg.subpool[0].num = num_vec;
	/* Use max thread-local pkt-cache size to speed up pktio allocs */
	pool_cfg.subpool[0].cache_size = pool_capa->pkt.max_cache_size;

	em_pool_t vector_pool = em_pool_create("vector-pool-em", EM_POOL_UNDEF, &pool_cfg);

	if (vector_pool == EM_POOL_UNDEF)
		APPL_EXIT_FAILURE("EM vector pool create failed");

	/* Convert: EM-pool to ODP-pool */
	odp_pool_t odp_vecpool = ODP_POOL_INVALID;
	int ret = em_odp_pool2odp(vector_pool, &odp_vecpool, 1);

	if (unlikely(ret != 1))
		APPL_EXIT_FAILURE("EM pktio pool creation failed:%d", ret);

	/* Store the EM pktio pool and the corresponding ODP subpool */
	pktio_shm->pools.vecpool_em = vector_pool;
	pktio_shm->pools.vecpool_odp = odp_vecpool;

	odp_pool_print(odp_vecpool);
}

static void pktio_vectorpool_create_odp(int if_count, const odp_pool_capability_t *pool_capa)
{
	odp_pool_param_t pool_params;

	odp_pool_param_init(&pool_params);

	pool_params.type = ODP_POOL_VECTOR;

	if (unlikely(pool_capa->vector.max_pools == 0 ||
		     pool_capa->vector.max_size == 0))
		APPL_EXIT_FAILURE("ODP pktin vectors not supported!");

	uint32_t vec_size = PKTIO_VEC_POOL_VEC_SIZE;
	uint32_t num_pkt = PKTIO_PKT_POOL_NUM_BUFS * if_count;
	uint32_t num_vec = num_pkt; /* worst case: 1 pkt per vector */

	if (vec_size > pool_capa->vector.max_size) {
		vec_size = pool_capa->vector.max_size;
		APPL_PRINT("\nWarning: pktin vector size reduced to %u\n\n",
			   vec_size);
	}

	if (pool_capa->vector.max_num /* 0=limited only by pool memsize */ &&
	    num_vec > pool_capa->vector.max_num) {
		num_vec = pool_capa->vector.max_num;
		APPL_PRINT("\nWarning: pktin number of vectors reduced to %u\n\n",
			   num_vec);
	}

	pool_params.vector.num = num_vec;
	pool_params.vector.max_size = vec_size;
	pool_params.vector.uarea_size = em_odp_event_hdr_size();

	odp_pool_t vector_pool = odp_pool_create("vector-pool-odp", &pool_params);

	if (vector_pool == ODP_POOL_INVALID)
		APPL_EXIT_FAILURE("ODP vector pool create failed");

	pktio_shm->pools.vecpool_odp = vector_pool;

	odp_pool_print(vector_pool);
}

/**
 * Create the memory pool used by pkt-io
 */
void pktio_pool_create(int if_count, bool pktpool_em,
		       bool pktin_vector, bool vecpool_em)
{
	odp_pool_capability_t pool_capa;

	if (odp_pool_capability(&pool_capa) != 0)
		APPL_EXIT_FAILURE("Can't get odp-pool capability");
	/*
	 * Create the pktio pkt pool used for actual input pkts.
	 * Create the pool either as an EM- or ODP-pool.
	 */
	if (pktpool_em)
		pktio_pool_create_em(if_count, &pool_capa);
	else
		pktio_pool_create_odp(if_count, &pool_capa);

	if (pktin_vector) {
		if (vecpool_em)
			pktio_vectorpool_create_em(if_count, &pool_capa);
		else
			pktio_vectorpool_create_odp(if_count, &pool_capa);
	}
}

/**
 * Helper to pktio_pool_destroy(): destroy the EM event-pool used for pktio
 */
static void pktio_pool_destroy_em(void)
{
	APPL_PRINT("\n%s(): deleting the EM pktio-pool:\n", __func__);
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
	APPL_PRINT("\n%s(): destroying the ODP pktio-pool\n", __func__);
	if (odp_pool_destroy(pktio_shm->pools.pktpool_odp) != 0)
		APPL_EXIT_FAILURE("ODP pktio-pool destroy failed.");

	pktio_shm->pools.pktpool_odp = ODP_POOL_INVALID;
}

/**
 * Helper to pktio_pool_destroy(): destroy the pktin EM vector pool
 */
static void pktio_vectorpool_destroy_em(void)
{
	APPL_PRINT("\n%s(): deleting the EM vector-pool:\n", __func__);
	em_pool_info_print(pktio_shm->pools.vecpool_em);

	if (em_pool_delete(pktio_shm->pools.vecpool_em) != EM_OK)
		APPL_EXIT_FAILURE("EM pktio-pool delete failed.");

	pktio_shm->pools.vecpool_em = EM_POOL_UNDEF;
	pktio_shm->pools.vecpool_odp = ODP_POOL_INVALID;
}

/**
 * Helper to pktio_pool_destroy(): destroy the ODP pktin vector pool
 */
static void pktio_vectorpool_destroy_odp(void)
{
	APPL_PRINT("\n%s(): destroying the ODP pktin vector-pool\n", __func__);
	if (odp_pool_destroy(pktio_shm->pools.vecpool_odp) != 0)
		APPL_EXIT_FAILURE("ODP pktin vector-pool destroy failed.");

	pktio_shm->pools.vecpool_odp = ODP_POOL_INVALID;
}

/**
 * Destroy the memory pool used by pkt-io
 */
void pktio_pool_destroy(bool pktpool_em, bool pktin_vector, bool vecpool_em)
{
	if (pktpool_em)
		pktio_pool_destroy_em();
	else
		pktio_pool_destroy_odp();

	if (pktin_vector) {
		if (vecpool_em)
			pktio_vectorpool_destroy_em();
		else
			pktio_vectorpool_destroy_odp();
	}
}

void pktio_init(const appl_conf_t *appl_conf)
{
	pktin_mode_t in_mode = appl_conf->pktio.in_mode;
	odp_stash_capability_t stash_capa;
	odp_stash_param_t stash_param;
	odp_stash_t stash;
	int ret;

	pktio_shm->ifs.count = appl_conf->pktio.if_count;
	pktio_shm->ifs.num_created = 0;
	pktio_shm->default_queue = EM_QUEUE_UNDEF;

	pktio_shm->pktin.in_mode = in_mode;
	pktio_shm->pktin.pktin_queue_stash = ODP_STASH_INVALID;

	ret = odp_stash_capability(&stash_capa, ODP_STASH_TYPE_FIFO);
	if (ret != 0)
		APPL_EXIT_FAILURE("odp_stash_capability() fails:%d", ret);

	if (pktin_polled_mode(in_mode)) {
		/*
		 * Create a stash to hold the shared queues used in pkt input. Each core
		 * needs to get one queue to be able to use it to receive packets.
		 * DIRECT_RECV-mode: the stash contains pointers to odp_pktin_queue_t:s
		 * PLAIN_QUEUE-mode: the stash contains odp_queue_t:s
		 */
		odp_stash_param_init(&stash_param);
		stash_param.type = ODP_STASH_TYPE_FIFO;
		stash_param.put_mode = ODP_STASH_OP_MT;
		stash_param.get_mode = ODP_STASH_OP_MT;
		stash_param.num_obj = PKTIO_MAX_IN_QUEUES * IF_MAX_NUM;
		if (stash_param.num_obj > stash_capa.max_num_obj)
			APPL_EXIT_FAILURE("Unsupported odp-stash number of objects:%" PRIu64 "",
					  stash_param.num_obj);
		stash_param.obj_size = MAX(sizeof(odp_queue_t), sizeof(odp_pktin_queue_t *));
		if (!POWEROF2(stash_param.obj_size) ||
		    stash_param.obj_size != sizeof(uintptr_t) ||
		    stash_param.obj_size > stash_capa.max_obj_size) {
			APPL_EXIT_FAILURE("Unsupported odp-stash object handle size:%u, max:%u",
					  stash_param.obj_size, stash_capa.max_obj_size);
		}
		stash_param.cache_size = 0; /* No core local caching */

		stash = odp_stash_create("pktin.pktin_queue_stash", &stash_param);
		if (stash == ODP_STASH_INVALID)
			APPL_EXIT_FAILURE("odp_stash_create() fails");

		pktio_shm->pktin.pktin_queue_stash = stash;
	}

	/*
	 * Create a stash to hold the shared tx-burst buffers,
	 * used when draining the available tx-burst buffers
	 */
	odp_stash_param_init(&stash_param);
	stash_param.type = ODP_STASH_TYPE_FIFO;
	stash_param.put_mode = ODP_STASH_OP_MT;
	stash_param.get_mode = ODP_STASH_OP_MT;
	stash_param.num_obj = MAX_TX_BURST_BUFS * IF_MAX_NUM;
	if (stash_param.num_obj > stash_capa.max_num_obj)
		APPL_EXIT_FAILURE("Unsupported odp-stash number of objects:%" PRIu64 "",
				  stash_param.num_obj);
	stash_param.obj_size = sizeof(tx_burst_t *); /* stash pointers */
	if (!POWEROF2(stash_param.obj_size) ||
	    stash_param.obj_size != sizeof(uintptr_t) ||
	    stash_param.obj_size > stash_capa.max_obj_size) {
		APPL_EXIT_FAILURE("Unsupported odp-stash object handle size:%u",
				  stash_param.obj_size);
	}
	stash_param.cache_size = 0; /* No core local caching */

	stash = odp_stash_create("pktout.tx-burst-stash", &stash_param);
	if (stash == ODP_STASH_INVALID)
		APPL_EXIT_FAILURE("odp_stash_create() fails");
	pktio_shm->pktout.tx_burst_stash = stash;

	/* Misc inits: */
	for (int i = 0; i < MAX_RX_PKT_QUEUES; i++) {
		pktio_shm->rx_pkt_queues[i].pos = i;
		pktio_shm->rx_pkt_queues[i].queue = EM_QUEUE_UNDEF;
	}

	odp_ticketlock_init(&pktio_shm->tbl_lookup.lock);
	pktio_shm->tbl_lookup.tbl_idx = 0;
	pktio_shm->tbl_lookup.ops = cuckoo_table_ops;
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

	if (pktin_polled_mode(appl_conf->pktio.in_mode))
		odp_stash_destroy(pktio_shm->pktin.pktin_queue_stash);
	odp_stash_destroy(pktio_shm->pktout.tx_burst_stash);

	pktio_shm->tbl_lookup.ops.f_des(pktio_shm->tbl_lookup.tbl);
}

static void pktio_tx_buffering_create(int if_num)
{
	tx_burst_t *tx_burst;
	odp_queue_param_t queue_param;
	odp_queue_t odp_queue;
	int pktout_idx;
	odp_queue_t pktout_queue;
	char name[ODP_QUEUE_NAME_LEN];

	const int pktout_num_queues = pktio_shm->pktout.num_queues[if_num];

	for (int i = 0; i < MAX_TX_BURST_BUFS; i++) {
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
			APPL_EXIT_FAILURE("odp_queue_create() fails:if=%d(%d)",
					  if_num, i);
		tx_burst->queue = odp_queue;
		tx_burst->if_port = if_num;

		pktout_idx = i % pktout_num_queues;
		pktout_queue = pktio_shm->pktout.queues[if_num][pktout_idx];
		tx_burst->pktout_queue = pktout_queue;

		/*
		 * Store each tx burst into the tx_burst_stash, stash used when
		 * draining the available tx-burst buffers.
		 */
		uintptr_t tx_burst_uintptr = (uintptr_t)tx_burst;
		int ret = odp_stash_put_ptr(pktio_shm->pktout.tx_burst_stash,
					    &tx_burst_uintptr, 1);
		if (unlikely(ret != 1))
			APPL_EXIT_FAILURE("enqueue fails");
	}
}

static void pktio_tx_buffering_destroy(void)
{
	tx_burst_t *tx_burst;
	int num;

	while ((tx_burst = tx_drain_burst_acquire()) != NULL) {
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
	}
}

static inline void
pktin_queue_stashing_create(int if_num, pktin_mode_t in_mode)
{
	int num_rx = pktio_shm->pktin.num_queues[if_num];
	uintptr_t uintptr;
	int ret;

	for (int i = 0; i < num_rx; i++) {
		if (in_mode == PLAIN_QUEUE) {
			odp_queue_t queue;

			queue = pktio_shm->pktin.plain_queues[if_num][i];
			uintptr = (uintptr_t)queue;
		} else /* DIRECT_RECV*/ {
			odp_pktin_queue_t *pktin_qptr;

			pktin_qptr = &pktio_shm->pktin.pktin_queues[if_num][i];
			uintptr = (uintptr_t)pktin_qptr;
		}

		/*
		 * Store the queue or the pktin_queue-ptr as an 'uintptr_t'
		 * in the stash.
		 */
		ret = odp_stash_put_ptr(pktio_shm->pktin.pktin_queue_stash,
					&uintptr, 1);
		if (unlikely(ret != 1))
			APPL_EXIT_FAILURE("stash-put fails:%d", ret);
	}
}

static inline void
pktin_queue_queueing_destroy(void)
{
	pktin_mode_t in_mode = pktio_shm->pktin.in_mode;

	if (in_mode == PLAIN_QUEUE) {
		while (plain_queue_acquire() != ODP_QUEUE_INVALID)
			; /* empty stash */
	} else if (in_mode == DIRECT_RECV) {
		odp_pktin_queue_t *pktin_queue_ptr;

		while (pktin_queue_acquire(&pktin_queue_ptr) == 0)
			; /* empty stash */
	}
}

static void
set_pktin_vector_params(odp_pktin_queue_param_t *pktin_queue_param,
			odp_pool_t vec_pool,
			const odp_pktio_capability_t *pktio_capa)
{
	uint32_t vec_size = PKTIO_VEC_SIZE;
	uint64_t vec_tmo_ns = PKTIO_VEC_TMO;

	pktin_queue_param->vector.enable = true;
	pktin_queue_param->vector.pool = vec_pool;

	if (vec_size > pktio_capa->vector.max_size ||
	    vec_size < pktio_capa->vector.min_size) {
		vec_size = (vec_size > pktio_capa->vector.max_size) ?
			pktio_capa->vector.max_size : pktio_capa->vector.min_size;
		APPL_PRINT("\nWarning: Modified vector size to %u\n\n", vec_size);
	}
	pktin_queue_param->vector.max_size = vec_size;

	if (vec_tmo_ns > pktio_capa->vector.max_tmo_ns ||
	    vec_tmo_ns < pktio_capa->vector.min_tmo_ns) {
		vec_tmo_ns = (vec_tmo_ns > pktio_capa->vector.max_tmo_ns) ?
			pktio_capa->vector.max_tmo_ns : pktio_capa->vector.min_tmo_ns;
		APPL_PRINT("\nWarning: Modified vector timeout to %" PRIu64 "\n\n", vec_tmo_ns);
	}
	pktin_queue_param->vector.max_tmo_ns = vec_tmo_ns;
}

/** Helper to pktio_create() for packet input configuration */
static void pktin_config(const char *dev, int if_idx, odp_pktio_t pktio,
			 const odp_pktio_capability_t *pktio_capa,
			 int if_count, int num_workers, pktin_mode_t in_mode,
			 bool pktin_vector)
{
	odp_pktin_queue_param_t pktin_queue_param;
	int num_rx, max;
	int ret;

	odp_pktin_queue_param_init(&pktin_queue_param);

	max = MIN((int)pktio_capa->max_input_queues, PKTIO_MAX_IN_QUEUES);
	num_rx = 2 * (ROUND_UP(num_workers, if_count) / if_count);
	num_rx = MIN(max, num_rx);

	APPL_PRINT("\tmax number of pktio dev:'%s' input queues:%d, using:%d\n",
		   dev, pktio_capa->max_input_queues, num_rx);

	pktin_queue_param.hash_enable = 1;
	pktin_queue_param.classifier_enable = 0;
	pktin_queue_param.hash_proto.proto.ipv4_udp = 1;
	pktin_queue_param.num_queues = num_rx;

	if (pktin_polled_mode(in_mode)) {
		pktin_queue_param.op_mode = ODP_PKTIO_OP_MT_UNSAFE;
	} else if (pktin_sched_mode(in_mode)) {
		pktin_queue_param.queue_param.type = ODP_QUEUE_TYPE_SCHED;
		pktin_queue_param.queue_param.sched.prio = odp_schedule_default_prio();
		if (in_mode == SCHED_PARALLEL)
			pktin_queue_param.queue_param.sched.sync = ODP_SCHED_SYNC_PARALLEL;
		else if (in_mode == SCHED_ATOMIC)
			pktin_queue_param.queue_param.sched.sync = ODP_SCHED_SYNC_ATOMIC;
		else /* in_mode == SCHED_ORDERED */
			pktin_queue_param.queue_param.sched.sync = ODP_SCHED_SYNC_ORDERED;

		pktin_queue_param.queue_param.sched.group = em_odp_qgrp2odp(EM_QUEUE_GROUP_DEFAULT);

		if (pktin_vector) {
			if (!pktio_capa->vector.supported)
				APPL_EXIT_FAILURE("pktin, dev:'%s': input vectors not supported",
						  dev);
			set_pktin_vector_params(&pktin_queue_param,
						pktio_shm->pools.vecpool_odp,
						pktio_capa);
		}
	}

	ret = odp_pktin_queue_config(pktio, &pktin_queue_param);
	if (ret < 0)
		APPL_EXIT_FAILURE("pktin, dev:'%s': input queue config failed: %d",
				  dev, ret);

	if (in_mode == PLAIN_QUEUE) {
		ret = odp_pktin_event_queue(pktio, pktio_shm->pktin.plain_queues[if_idx]/*out*/,
					    num_rx);
		if (ret != num_rx)
			APPL_EXIT_FAILURE("pktin, dev:'%s': plain event queue query failed: %d",
					  dev, ret);
	} else if (pktin_sched_mode(in_mode)) {
		odp_queue_t *pktin_sched_queues = &pktio_shm->pktin.sched_queues[if_idx][0];
		em_queue_t *pktin_sched_em_queues = &pktio_shm->pktin.sched_em_queues[if_idx][0];

		ret = odp_pktin_event_queue(pktio, pktin_sched_queues/*[out]*/, num_rx);
		if (ret != num_rx)
			APPL_EXIT_FAILURE("pktin, dev:'%s': odp_pktin_event_queue():%d",
					  dev, ret);
		/*
		 * Create EM queues mapped to the ODP scheduled pktin event queues
		 */
		ret = em_odp_pktin_event_queues2em(pktin_sched_queues/*[in]*/,
						   pktin_sched_em_queues/*[out]*/,
						   num_rx);
		if (ret != num_rx)
			APPL_EXIT_FAILURE("pktin, dev:'%s': em_odp_pktin_queues2em():%d",
					  dev, ret);
	} else /* DIRECT_RECV */ {
		ret = odp_pktin_queue(pktio, pktio_shm->pktin.pktin_queues[if_idx]/*[out]*/,
				      num_rx);
		if (ret != num_rx)
			APPL_EXIT_FAILURE("pktin, dev:'%s': direct queue query failed: %d",
					  dev, ret);
	}

	pktio_shm->pktin.num_queues[if_idx] = num_rx;

	if (pktin_polled_mode(in_mode)) {
		/*
		 * Store all pktin queues in a stash - each core 'gets' acquires
		 * a pktin queue to use from this stash.
		 */
		pktin_queue_stashing_create(if_idx, in_mode);
	}
}

/** Helper to pktio_create() for packet output configuration */
static void pktout_config(const char *dev, int if_idx, odp_pktio_t pktio,
			  const odp_pktio_capability_t *pktio_capa,
			  int num_workers)
{
	odp_pktout_queue_param_t pktout_queue_param;
	odp_pktio_op_mode_t mode_tx;
	int num_tx, max;
	int ret;

	odp_pktout_queue_param_init(&pktout_queue_param);
	mode_tx = ODP_PKTIO_OP_MT;
	max = MIN((int)pktio_capa->max_output_queues, PKTIO_MAX_OUT_QUEUES);
	num_tx = MIN(2 * num_workers, max);
	APPL_PRINT("\tmax number of pktio dev:'%s' output queues:%d, using:%d\n",
		   dev, pktio_capa->max_output_queues, num_tx);

	pktout_queue_param.num_queues = num_tx;
	pktout_queue_param.op_mode = mode_tx;

	ret = odp_pktout_queue_config(pktio, &pktout_queue_param);
	if (ret < 0)
		APPL_EXIT_FAILURE("pktio output queue config failed dev:'%s' (%d)",
				  dev, ret);

	ret = odp_pktout_event_queue(pktio, pktio_shm->pktout.queues[if_idx],
				     num_tx);
	if (ret != num_tx || ret > PKTIO_MAX_OUT_QUEUES)
		APPL_EXIT_FAILURE("pktio pktout queue query failed dev:'%s' (%d)",
				  dev, ret);
	pktio_shm->pktout.num_queues[if_idx] = num_tx;

	/* Create Tx buffers */
	pktio_tx_buffering_create(if_idx);
}

int /* if_id */
pktio_create(const char *dev, pktin_mode_t in_mode, bool pktin_vector,
	     int if_count, int num_workers)
{
	int if_idx = -1; /* return value */
	odp_pktio_param_t pktio_param;
	odp_pktio_t pktio;
	odp_pktio_capability_t pktio_capa;
	odp_pktio_config_t pktio_config;
	odp_pktio_info_t info;
	int ret;

	odp_pktio_param_init(&pktio_param);

	/* Packet input mode */
	if (in_mode == DIRECT_RECV)
		pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;
	else if (in_mode == PLAIN_QUEUE)
		pktio_param.in_mode = ODP_PKTIN_MODE_QUEUE;
	else if (pktin_sched_mode(in_mode))
		pktio_param.in_mode = ODP_PKTIN_MODE_SCHED;
	else
		APPL_EXIT_FAILURE("dev:'%s': unsupported pktin-mode:%d\n",
				  dev, in_mode);

	/* Packet output mode: QUEUE mode to preserve packet order if needed */
	pktio_param.out_mode = ODP_PKTOUT_MODE_QUEUE;

	pktio = odp_pktio_open(dev, pktio_shm->pools.pktpool_odp, &pktio_param);
	if (pktio == ODP_PKTIO_INVALID)
		APPL_EXIT_FAILURE("pktio create failed for dev:'%s'\n", dev);

	if (odp_pktio_info(pktio, &info))
		APPL_EXIT_FAILURE("pktio info failed dev:'%s'", dev);

	if_idx = odp_pktio_index(pktio);
	if (if_idx < 0 || if_idx >= IF_MAX_NUM)
		APPL_EXIT_FAILURE("pktio index:%d too large, dev:'%s'",
				  if_idx, dev);

	APPL_PRINT("\n%s(dev=%s):\n", __func__, dev);
	APPL_PRINT("\tcreated pktio:%" PRIu64 " idx:%d, dev:'%s', drv:%s\n",
		   odp_pktio_to_u64(pktio), if_idx, dev, info.drv_name);

	ret = odp_pktio_capability(pktio, &pktio_capa);
	if (ret != 0)
		APPL_EXIT_FAILURE("pktio capability query failed: dev:'%s' (%d)",
				  dev, ret);

	odp_pktio_config_init(&pktio_config);
	pktio_config.parser.layer = ODP_PROTO_LAYER_NONE;
	/* Provide hint to pktio that packet references are not used */
	pktio_config.pktout.bit.no_packet_refs = 1;

	ret = odp_pktio_config(pktio, &pktio_config);
	if (ret != 0)
		APPL_EXIT_FAILURE("pktio config failed: dev:'%s' (%d)",
				  dev, ret);

	/* Pktin (Rx) config */
	pktin_config(dev, if_idx, pktio, &pktio_capa,
		     if_count, num_workers, in_mode, pktin_vector);

	/* Pktout (Tx) config */
	pktout_config(dev, if_idx, pktio, &pktio_capa, num_workers);

	APPL_PRINT("\tcreated pktio dev:'%s' - input mode:%s, output mode:QUEUE",
		   dev, pktin_mode_str(in_mode));

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
			APPL_EXIT_FAILURE("pktio close failed for if:%d", if_idx);

		pktio_shm->ifs.pktio_hdl[if_idx] = ODP_PKTIO_INVALID;
	}

	if (pktin_polled_mode(pktio_shm->pktin.in_mode))
		pktin_queue_queueing_destroy();
	pktio_tx_buffering_destroy();
}

static inline int
pktin_queue_acquire(odp_pktin_queue_t **pktin_queue_ptr /*out*/)
{
	odp_pktin_queue_t *pktin_qptr;
	uintptr_t pktin_qptr_uintptr;

	int ret = odp_stash_get_ptr(pktio_shm->pktin.pktin_queue_stash,
				    &pktin_qptr_uintptr, 1);

	if (unlikely(ret != 1))
		return -1;

	pktin_qptr = (odp_pktin_queue_t *)pktin_qptr_uintptr;

	*pktin_queue_ptr = pktin_qptr;
	return 0;
}

static inline void
pktin_queue_release(odp_pktin_queue_t *pktin_queue_ptr)
{
	uintptr_t pktin_qptr_uintptr;

	/* store the pointer as an 'uintptr_t' in the stash */
	pktin_qptr_uintptr = (uintptr_t)pktin_queue_ptr;

	int ret = odp_stash_put_ptr(pktio_shm->pktin.pktin_queue_stash,
				    &pktin_qptr_uintptr, 1);
	if (unlikely(ret != 1))
		APPL_EXIT_FAILURE("stash-put fails:%d", ret);
}

static inline odp_queue_t
plain_queue_acquire(void)
{
	odp_queue_t queue;
	uintptr_t queue_uintptr;

	int ret = odp_stash_get_ptr(pktio_shm->pktin.pktin_queue_stash,
				    &queue_uintptr, 1);
	if (unlikely(ret != 1))
		return ODP_QUEUE_INVALID;

	queue = (odp_queue_t)queue_uintptr;

	return queue;
}

static inline void
plain_queue_release(odp_queue_t queue)
{
	uintptr_t queue_uintptr;

	/* store the queue as an 'uintptr_t' in the stash */
	queue_uintptr = (uintptr_t)queue;

	int ret = odp_stash_put_ptr(pktio_shm->pktin.pktin_queue_stash,
				    &queue_uintptr, 1);
	if (unlikely(ret != 1))
		APPL_EXIT_FAILURE("stash-put fails:%d", ret);
}

/*
 * Helper to the pktin_pollfn_...() functions.
 */
static inline int /* nbr of pkts enqueued */
pktin_lookup_enqueue(odp_packet_t pkt_tbl[], int pkts)
{
	const table_get_value f_get = pktio_shm->tbl_lookup.ops.f_get;
	rx_queue_burst_t *const rx_qbursts = pktio_locm.rx_qbursts;
	int pkts_enqueued = 0; /* return value */
	int valid_pkts = 0;

	for (int i = 0; i < pkts; i++) {
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

	for (int i = 0; i < pkts; i++) {
		const odp_packet_t pkt = pkt_tbl[i];
		rx_pkt_queue_t rx_pkt_queue;
		em_queue_t queue;
		int pos;

		/* table(hash) lookup to find queue */
		int ret = f_get(pktio_shm->tbl_lookup.tbl,
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
		rx_qbursts[pos].pkt_tbl[rx_qbursts[pos].pkt_cnt++] = pkt;
	}

	for (int i = 0; i < valid_pkts; i++) {
		const int pos = pktio_locm.positions[i];

		if (rx_qbursts[pos].sent)
			continue;

		const int num = rx_qbursts[pos].pkt_cnt;
		const em_queue_t queue = rx_qbursts[pos].queue;

		/* Enqueue pkts into em-odp */
		pkts_enqueued += em_odp_pkt_enqueue(rx_qbursts[pos].pkt_tbl,
						    num, queue);
		rx_qbursts[pos].sent = 1;
		rx_qbursts[pos].pkt_cnt = 0;
	}

	return pkts_enqueued;
}

/*
 * User provided function to poll for packet input in DIRECT_RECV-mode,
 * given to EM via 'em_conf.input.input_poll_fn = pktin_pollfn_direct;'
 * The function is of type 'em_input_poll_func_t'. See .h file.
 */
int pktin_pollfn_direct(void)
{
	odp_pktin_queue_t *pktin_queue_ptr;
	odp_packet_t pkt_tbl[MAX_PKT_BURST_RX];
	int ret, pkts;
	int poll_rounds = 0;
	int pkts_enqueued = 0; /* return value */

	if (unlikely(!pktio_shm->pktio_started))
		return 0;

	ret = pktin_queue_acquire(&pktin_queue_ptr /*out*/);
	if (unlikely(ret != 0))
		return 0;

	do {
		pkts = odp_pktin_recv(*pktin_queue_ptr, pkt_tbl, MAX_PKT_BURST_RX);
		if (unlikely(pkts <= 0))
			goto pktin_poll_end;

		pkts_enqueued += pktin_lookup_enqueue(pkt_tbl, pkts);

	} while (pkts == MAX_PKT_BURST_RX &&
		 ++poll_rounds < MAX_RX_POLL_ROUNDS);

pktin_poll_end:
	pktin_queue_release(pktin_queue_ptr);

	return pkts_enqueued;
}

/*
 * User provided function to poll for packet input in PLAIN_QUEUE-mode,
 * given to EM via 'em_conf.input.input_poll_fn = pktin_pollfn_plainqueue;'
 * The function is of type 'em_input_poll_func_t'. See .h file.
 */
int pktin_pollfn_plainqueue(void)
{
	odp_queue_t plain_queue;
	odp_event_t ev_tbl[MAX_PKT_BURST_RX];
	odp_packet_t pkt_tbl[MAX_PKT_BURST_RX];
	int pkts;
	int poll_rounds = 0;
	int pkts_enqueued = 0; /* return value */

	if (unlikely(!pktio_shm->pktio_started))
		return 0;

	plain_queue = plain_queue_acquire();
	if (unlikely(plain_queue == ODP_QUEUE_INVALID))
		return 0;

	do {
		pkts = odp_queue_deq_multi(plain_queue, ev_tbl, MAX_PKT_BURST_RX);
		if (unlikely(pkts <= 0))
			goto pktin_poll_end;

		odp_packet_from_event_multi(pkt_tbl, ev_tbl, pkts);

		pkts_enqueued += pktin_lookup_enqueue(pkt_tbl, pkts);

	} while (pkts == MAX_PKT_BURST_RX &&
		 ++poll_rounds < MAX_RX_POLL_ROUNDS);

pktin_poll_end:
	plain_queue_release(plain_queue);

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

static inline tx_burst_t *
tx_drain_burst_acquire(void)
{
	tx_burst_t *tx_burst;
	uintptr_t tx_burst_uintptr;

	int ret = odp_stash_get_ptr(pktio_shm->pktout.tx_burst_stash,
				    &tx_burst_uintptr, 1);
	if (unlikely(ret != 1))
		return NULL;

	tx_burst = (tx_burst_t *)tx_burst_uintptr;
	return tx_burst;
}

static inline void
tx_drain_burst_release(tx_burst_t *tx_burst) {
	uintptr_t tx_burst_uintptr = (uintptr_t)tx_burst;

	int ret = odp_stash_put_ptr(pktio_shm->pktout.tx_burst_stash,
				    &tx_burst_uintptr, 1);
	if (unlikely(ret != 1))
		APPL_EXIT_FAILURE("stash-put fails:%d", ret);
}

/*
 * User provided function to drain buffered output,
 * given to EM via 'em_conf.output.output_drain_fn = pktout_drainfn;'
 * The function is of type 'em_output_drain_func_t'
 */
int pktout_drainfn(void)
{
	const uint64_t curr = odp_cpu_cycles(); /* core-local timestamp */
	const uint64_t prev = pktio_locm.tx_prev_cycles;
	const uint64_t diff = likely(curr >= prev) ?
		curr - prev : UINT64_MAX - prev + curr + 1;
	int ret = 0;

	/* TX burst queue drain */
	if (unlikely(diff > BURST_TX_DRAIN)) {
		tx_burst_t *tx_drain_burst = tx_drain_burst_acquire();

		if (tx_drain_burst) {
			ret = pktio_tx_burst(tx_drain_burst);
			/* Update timestamp for next round */
			pktio_locm.tx_prev_cycles = curr;
			tx_drain_burst_release(tx_drain_burst);
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
		APPL_EXIT_FAILURE("tbl insertion failed, idx(%d) != pos(%d)",
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
