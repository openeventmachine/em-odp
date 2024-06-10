/* Copyright (c) 2023, Nokia
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include "bench_common.h"

#include <event_machine/platform/event_machine_odp_ext.h>
#include <event_machine/platform/env/environment.h>

#include <getopt.h>
#include <unistd.h>

/* Maximum burst size for *_multi() operations */
#define MAX_BURST 64

/* Maximum number if test events */
#define MAX_EVENTS (REPEAT_COUNT * MAX_BURST)

/* User area size in bytes */
#define UAREA_SIZE 8

/* Default event size */
#define EVENT_SIZE 1024

/* Default burst size for *_multi() operations */
#define BURST_SIZE 8

/* Default vector size */
#define VECTOR_SIZE 8

/* Maximum number of retries */
#define MAX_RETRY 1024

/* Command line options specific to this event bench */
typedef struct {
	/* Burst size for *_multi operations */
	int burst_size;

	/* Event size */
	int event_size;

	/* Pool cache size */
	int cache_size;

	/* Vector size */
	int vector_size;
} event_opt_t;

typedef struct {
	run_bench_arg_t run_bench_arg;

	event_opt_t event_opt;

	/* Pools for allocating test events */
	em_pool_t sw_event_pool;
	em_pool_t packet_pool;
	em_pool_t vector_pool;

	/* Test queues */
	em_queue_t unsched_queue;

	/* Test case input / output data */
	em_event_t      event_tbl[MAX_EVENTS];
	em_event_t     event2_tbl[MAX_EVENTS];
	void             *ptr_tbl[MAX_EVENTS];
	uint16_t          u16_tbl[MAX_EVENTS];
	uint32_t          u32_tbl[MAX_EVENTS];
	em_event_type_t    et_tbl[MAX_EVENTS];
	em_pool_t        pool_tbl[MAX_EVENTS];
	odp_event_t odp_event_tbl[MAX_EVENTS];

} gbl_args_t;

static gbl_args_t *gbl_args;

static int create_pools(void)
{
	em_pool_cfg_t pool_conf;
	em_pool_t pool;
	uint32_t num_events;

	/* event_clone() and event_ref() tests require at least 2 x REPEAT_COUNT events */
	num_events = gbl_args->event_opt.burst_size < 2 ? 2 * REPEAT_COUNT :
			gbl_args->event_opt.burst_size * REPEAT_COUNT;

	em_pool_cfg_init(&pool_conf);
	pool_conf.event_type = EM_EVENT_TYPE_SW;
	pool_conf.user_area.in_use = true;
	pool_conf.user_area.size = UAREA_SIZE;
	pool_conf.num_subpools = 1;
	pool_conf.subpool[0].size = gbl_args->event_opt.event_size;
	if (gbl_args->event_opt.cache_size >= 0)
		pool_conf.subpool[0].cache_size = gbl_args->event_opt.cache_size;

	/* This is to make sure that there will be enough events to allocate from
	 * the worker/testing core when ESV preallocation is enabled, in which case
	 * all events in the pool are allocated and freed during pool creation.
	 * Since this function is called from the control core, so cache_size number
	 * of events will be cached in control core, reducing event availability
	 * in the testing core. To avoid this, we add cache_size to num_events,
	 * which is the maximum number of events that will be allocated in the
	 * tests.
	 */
	pool_conf.subpool[0].num = num_events + pool_conf.subpool[0].cache_size;

	pool = em_pool_create("sw_event_pool", EM_POOL_UNDEF, &pool_conf);
	if (pool == EM_POOL_UNDEF) {
		ODPH_ERR("EM SW event pool create failed\n");
		return -1;
	}
	gbl_args->sw_event_pool = pool;

	pool_conf.event_type = EM_EVENT_TYPE_PACKET;
	pool = em_pool_create("packet_pool", EM_POOL_UNDEF, &pool_conf);
	if (pool == EM_POOL_UNDEF) {
		ODPH_ERR("EM packet pool create failed\n");
		return -1;
	}
	gbl_args->packet_pool = pool;

	pool_conf.event_type = EM_EVENT_TYPE_VECTOR;
	pool_conf.subpool[0].size = gbl_args->event_opt.vector_size;
	pool = em_pool_create("vector_pool", EM_POOL_UNDEF, &pool_conf);
	if (pool == EM_POOL_UNDEF) {
		ODPH_ERR("EM vector pool create failed\n");
		return -1;
	}
	gbl_args->vector_pool = pool;

	return 0;
}

static int create_queues(void)
{
	em_queue_t unsched_queue = EM_QUEUE_UNDEF;
	em_queue_conf_t conf = {0};
	const int burst_size = gbl_args->event_opt.burst_size;

	conf.flags = EM_QUEUE_FLAG_DEFAULT;
	conf.min_events = burst_size * REPEAT_COUNT;
	conf.conf_len = 0;

	unsched_queue = em_queue_create("unsch-queue", EM_QUEUE_TYPE_UNSCHEDULED,
					EM_QUEUE_PRIO_UNDEF, EM_QUEUE_GROUP_UNDEF, &conf);
	if (unsched_queue == EM_QUEUE_UNDEF) {
		ODPH_ERR("EM unscheduled queue create failed\n");
		return -1;
	}

	gbl_args->unsched_queue = unsched_queue;

	return 0;
}

static int delete_queues(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t event = EM_EVENT_UNDEF;
	em_status_t err;

	if (unsched_queue == EM_QUEUE_UNDEF)
		return -1;

	do {
		event = em_queue_dequeue(unsched_queue);
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	} while (event != EM_EVENT_UNDEF);

	err = em_queue_delete(unsched_queue);
	if (err != EM_OK) {
		ODPH_ERR("em_queue_delete() fails\n");
		return -1;
	}

	gbl_args->unsched_queue = EM_QUEUE_UNDEF;

	return 0;
}

static void init_test_events(em_event_t event[], int num)
{
	for (int i = 0; i < num; i++) {
		em_status_t ret;

		ret = em_event_uarea_id_set(event[i], i);
		if (ret != EM_OK)
			ODPH_ABORT("Setting event user area ID failed\n");

		gbl_args->odp_event_tbl[i] = em_odp_event2odp(event[i]);
	}
}

static void allocate_test_events(em_pool_t pool, em_event_type_t type, em_event_t event[], int num)
{
	int num_events = 0;
	int num_retries = 0;
	const int size = type == EM_EVENT_TYPE_VECTOR ? gbl_args->event_opt.vector_size :
				gbl_args->event_opt.event_size;

	while (num_events < num) {
		int ret;

		ret = em_alloc_multi(&event[num_events], num - num_events,
				     size, type, pool);
		if (ret < 1) {
			num_retries++;
			if (ret < 0 || num_retries > MAX_RETRY)
				ODPH_ABORT("Allocating test events failed\n");
			continue;
		}
		num_retries = 0;
		num_events += ret;
	}
}

/* Allocate events as if they were from pktio */
static void allocate_test_ext_pktevents(em_pool_t pool, em_event_type_t type,
					em_event_t events[], int num)
{
	int num_events = 0;
	int num_retries = 0;
	const int size = gbl_args->event_opt.event_size;
	odp_pool_t odp_pool = ODP_POOL_INVALID;

	if (unlikely(type != EM_EVENT_TYPE_PACKET))
		ODPH_ABORT("Invalid pool type: %u\n", type);

	int ret = em_odp_pool2odp(pool, &odp_pool, 1);

	if (unlikely(ret != 1))
		ODPH_ABORT("Obtaining ODP pool from EM pool failed\n");

	odp_packet_t odp_pkts[num];
	odp_event_t odp_evs[num];

	while (num_events < num) {
		ret = odp_packet_alloc_multi(odp_pool, size, &odp_pkts[num_events],
					     num - num_events);
		if (ret < 1) {
			num_retries++;
			if (ret < 0 || num_retries > MAX_RETRY)
				ODPH_ABORT("Allocating test events failed\n");
			continue;
		}

		odp_packet_to_event_multi(&odp_pkts[num_events], &odp_evs[num_events], ret);

		num_retries = 0;
		num_events += ret;
	}

	em_odp_events2em(odp_evs, events, num);
}

static void create_packets(void)
{
	allocate_test_events(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET, gbl_args->event_tbl,
			     REPEAT_COUNT);
	init_test_events(gbl_args->event_tbl, REPEAT_COUNT);
}

/* Simulate events/pkts from pktio */
static void create_ext_packets(void)
{
	allocate_test_ext_pktevents(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET,
				    gbl_args->event_tbl, REPEAT_COUNT);
}

static void create_packets_multi(void)
{
	const int num_events = REPEAT_COUNT * gbl_args->event_opt.burst_size;

	allocate_test_events(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET, gbl_args->event_tbl,
			     num_events);
	init_test_events(gbl_args->event_tbl, num_events);
}

static void create_sw_events(void)
{
	allocate_test_events(gbl_args->sw_event_pool, EM_EVENT_TYPE_SW, gbl_args->event_tbl,
			     REPEAT_COUNT);
	init_test_events(gbl_args->event_tbl, REPEAT_COUNT);
}

static void create_sw_events_multi(void)
{
	const int num_events = REPEAT_COUNT * gbl_args->event_opt.burst_size;

	allocate_test_events(gbl_args->sw_event_pool, EM_EVENT_TYPE_SW, gbl_args->event_tbl,
			     num_events);
	init_test_events(gbl_args->event_tbl, num_events);
}

static void create_vectors(void)
{
	allocate_test_events(gbl_args->vector_pool, EM_EVENT_TYPE_VECTOR, gbl_args->event_tbl,
			     REPEAT_COUNT);
	init_test_events(gbl_args->event_tbl, REPEAT_COUNT);
}

static void create_vectors_multi(void)
{
	const int num_events = REPEAT_COUNT * gbl_args->event_opt.burst_size;

	allocate_test_events(gbl_args->vector_pool, EM_EVENT_TYPE_VECTOR, gbl_args->event_tbl,
			     num_events);
	init_test_events(gbl_args->event_tbl, REPEAT_COUNT);
}

static void free_event_tbl(em_event_t event_tbl[], int num)
{
	for (int i = 0; i < num; i++) {
		if (event_tbl[i] != EM_EVENT_UNDEF) {
			em_free(event_tbl[i]);
			event_tbl[i] = EM_EVENT_UNDEF;
		}
	}
}

static void free_events(void)
{
	free_event_tbl(gbl_args->event_tbl, REPEAT_COUNT);
}

static void free_events_multi(void)
{
	free_event_tbl(gbl_args->event_tbl, REPEAT_COUNT * gbl_args->event_opt.burst_size);
}

static void free_vectors(void)
{
	/* Restore correct vector size after event_vector_set_size() test */
	for (int i = 0; i < REPEAT_COUNT; i++)
		em_event_vector_size_set(gbl_args->event_tbl[i], 0);

	free_events();
}

static void free_clone_events(void)
{
	free_event_tbl(gbl_args->event_tbl, REPEAT_COUNT);
	free_event_tbl(gbl_args->event2_tbl, REPEAT_COUNT);
}

/**
 * Test functions
 */

static inline int event_alloc(em_pool_t pool, em_event_type_t type, int event_size)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		event_tbl[i] = em_alloc(event_size, type, pool);

	return i;
}

static int event_sw_alloc(void)
{
	return event_alloc(gbl_args->sw_event_pool, EM_EVENT_TYPE_SW,
			   gbl_args->event_opt.event_size);
}

static int event_pkt_alloc(void)
{
	return event_alloc(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET,
			   gbl_args->event_opt.event_size);
}

static int event_vector_alloc(void)
{
	return event_alloc(gbl_args->vector_pool, EM_EVENT_TYPE_VECTOR,
			   gbl_args->event_opt.vector_size);
}

static inline int event_alloc_multi(em_pool_t pool, em_event_type_t type, int event_size)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_alloc_multi(&event_tbl[i * burst_size], burst_size, event_size,
				      type, pool);

	return ret;
}

static int event_sw_alloc_multi(void)
{
	return event_alloc_multi(gbl_args->sw_event_pool, EM_EVENT_TYPE_SW,
				 gbl_args->event_opt.event_size);
}

static int event_pkt_alloc_multi(void)
{
	return event_alloc_multi(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET,
				 gbl_args->event_opt.event_size);
}

static int event_vector_alloc_multi(void)
{
	return event_alloc_multi(gbl_args->vector_pool, EM_EVENT_TYPE_VECTOR,
				 gbl_args->event_opt.vector_size);
}

static inline int alloc_free(em_pool_t pool, em_event_type_t type, int event_size)
{
	int i;

	for (i = 0; i < REPEAT_COUNT; i++) {
		em_event_t event;

		event = em_alloc(event_size, type, pool);
		if (likely(event != EM_EVENT_UNDEF))
			em_free(event);
	}

	return i;
}

static int event_sw_alloc_free(void)
{
	return alloc_free(gbl_args->sw_event_pool, EM_EVENT_TYPE_SW,
			  gbl_args->event_opt.event_size);
}

static int event_pkt_alloc_free(void)
{
	return alloc_free(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET,
			  gbl_args->event_opt.event_size);
}

static int event_vector_alloc_free(void)
{
	return alloc_free(gbl_args->vector_pool, EM_EVENT_TYPE_VECTOR,
			  gbl_args->event_opt.vector_size);
}

static inline int alloc_free_multi(em_pool_t pool, em_event_type_t type, int event_size)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++) {
		int ret;

		ret = em_alloc_multi(event_tbl, burst_size, event_size, type, pool);
		if (likely(ret > 0))
			em_free_multi(event_tbl, ret);
	}

	return i;
}

static int event_sw_alloc_free_multi(void)
{
	return alloc_free_multi(gbl_args->sw_event_pool, EM_EVENT_TYPE_SW,
				gbl_args->event_opt.event_size);
}

static int event_pkt_alloc_free_multi(void)
{
	return alloc_free_multi(gbl_args->packet_pool, EM_EVENT_TYPE_PACKET,
				gbl_args->event_opt.event_size);
}

static int event_vector_alloc_free_multi(void)
{
	return alloc_free_multi(gbl_args->vector_pool, EM_EVENT_TYPE_VECTOR,
				gbl_args->event_opt.vector_size);
}

static int event_free(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_free(event_tbl[i]);

	return i;
}

static int event_free_multi(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_free_multi(&event_tbl[i * burst_size], burst_size);

	return i;
}

static int event_pointer(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	void **ptr = gbl_args->ptr_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		ptr[i] = em_event_pointer(event_tbl[i]);

	return i;
}

static int event_pointer_and_size(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	void **ptr = gbl_args->ptr_tbl;
	uint32_t *u32 = gbl_args->u32_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		ptr[i] = em_event_pointer_and_size(event_tbl[i], &u32[i]);

	return i;
}

static int event_uarea_get(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	void **ptr = gbl_args->ptr_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		ptr[i] = em_event_uarea_get(event_tbl[i], NULL);

	return i;
}

static int event_uarea_get_size(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	void **ptr = gbl_args->ptr_tbl;
	size_t size = 0;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		ptr[i] = em_event_uarea_get(event_tbl[i], &size);

	return size;
}

static int event_get_size(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	uint32_t *u32 = gbl_args->u32_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		u32[i] = em_event_get_size(event_tbl[i]);

	return i;
}

static int event_get_type(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_type_t *et = gbl_args->et_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		et[i] = em_event_get_type(event_tbl[i]);

	return i;
}

static int event_get_type_multi(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_type_t *et = gbl_args->et_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_event_get_type_multi(&event_tbl[i * burst_size], burst_size,
					       &et[i * burst_size]);

	return ret;
}

static int event_same_type_multi(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_type_t *et = gbl_args->et_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_event_same_type_multi(&event_tbl[i * burst_size], burst_size, &et[i]);

	return ret;
}

static int event_set_type(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_event_set_type(event_tbl[i], EM_EVENT_TYPE_SW + 1);

	return i;
}

static int event_get_pool(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_pool_t *pool = gbl_args->pool_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		pool[i] = em_event_get_pool(event_tbl[i]);

	return i;
}

static int event_get_pool_subpool(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_pool_t *pool = gbl_args->pool_tbl;
	int subpool = 0;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		pool[i] = em_event_get_pool_subpool(event_tbl[i], &subpool);

	return i;
}

static int event_uarea_id_get(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	uint16_t *u16 = gbl_args->u16_tbl;
	bool isset;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_event_uarea_id_get(event_tbl[i], &isset, &u16[i]);

	return i + isset;
}

static int event_uarea_id_set(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_event_uarea_id_set(event_tbl[i], i);

	return i;
}

static int event_uarea_info(void)
{
	em_event_uarea_info_t uarea_info;
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_event_uarea_info(event_tbl[i], &uarea_info /*out*/);

	return i;
}

static int event_has_ref(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_event_has_ref(event_tbl[i]);

	return !ret;
}

static int event_ref(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_t *event2_tbl = gbl_args->event2_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		event2_tbl[i] = em_event_ref(event_tbl[i]);

	return i;
}

static int event_clone(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_t *event2_tbl = gbl_args->event2_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		event2_tbl[i] = em_event_clone(event_tbl[i], EM_POOL_UNDEF);

	return i;
}

static int event_clone_part(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_t *event2_tbl = gbl_args->event2_tbl;
	uint32_t size = em_event_get_size(event_tbl[0]); /* all events are of same size */
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		event2_tbl[i] = em_event_clone_part(event_tbl[i], EM_POOL_UNDEF, 0, size, true);

	return i;
}

static int event_clone_part__no_uarea(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_t *event2_tbl = gbl_args->event2_tbl;
	uint32_t size = em_event_get_size(event_tbl[0]); /* all events are of same size */
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		event2_tbl[i] = em_event_clone_part(event_tbl[i], EM_POOL_UNDEF, 0, size, false);

	return i;
}

static int event_vector_free(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_event_vector_free(event_tbl[i]);

	return i;
}

static int event_vector_tbl(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_t *ev_tbl;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_event_vector_tbl(event_tbl[i], &ev_tbl);

	return !ret;
}

static int event_vector_size(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_event_vector_size(event_tbl[i]);

	return !ret;
}

static int event_vector_max_size(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_event_vector_max_size(event_tbl[i]);

	return ret;
}

static int event_vector_size_set(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		/* Against strict API. Size is fixed in free_vectors(). */
		em_event_vector_size_set(event_tbl[i], 1);

	return i;
}

static int event_vector_info(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_event_vector_info_t vector_info;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_event_vector_info(event_tbl[i], &vector_info);

	return i;
}

static int core_id(void)
{
	uint32_t *u32 = gbl_args->u32_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		u32[i] = em_core_id();

	return i;
}

static int core_count(void)
{
	uint32_t *u32 = gbl_args->u32_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		u32[i] = em_core_count();

	return i;
}

static int odp_event2odp(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	odp_event_t *odp_event = gbl_args->odp_event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		odp_event[i] = em_odp_event2odp(event_tbl[i]);

	return i;
}

static int odp_events2odp(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	odp_event_t *odp_event = gbl_args->odp_event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_odp_events2odp(&event_tbl[i * burst_size], &odp_event[i * burst_size],
				  burst_size);

	return i;
}

static int odp_event2em(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	odp_event_t *odp_event = gbl_args->odp_event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		event_tbl[i] = em_odp_event2em(odp_event[i]);

	return i;
}

static int odp_events2em(void)
{
	em_event_t *event_tbl = gbl_args->event_tbl;
	odp_event_t *odp_event = gbl_args->odp_event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++)
		em_odp_events2em(&odp_event[i * burst_size], &event_tbl[i * burst_size],
				 burst_size);

	return i;
}

static int unsched_send(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_status_t err;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++) {
		err = em_send(event_tbl[i], unsched_queue);
		if (unlikely(err != EM_OK))
			return 0; /* error */
	}

	return i;
}

static inline int unsched_send_multi(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t *event_tbl = gbl_args->event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_send_multi(&event_tbl[i * burst_size], burst_size, unsched_queue);

	if (unlikely(ret != burst_size * REPEAT_COUNT))
		return 0; /* error */

	return ret;
}

static int unsched_dequeue(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t *event_tbl = gbl_args->event_tbl;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++) {
		event_tbl[i] = em_queue_dequeue(unsched_queue);
		if (unlikely(event_tbl[i] == EM_EVENT_UNDEF))
			return 0; /* error */
	}

	return i;
}

static int unsched_dequeue_multi(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t *event_tbl = gbl_args->event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int ret = 0;

	for (int i = 0; i < REPEAT_COUNT; i++)
		ret += em_queue_dequeue_multi(unsched_queue,
					      &event_tbl[i * burst_size],
					      burst_size);

	if (unlikely(ret != burst_size * REPEAT_COUNT))
		return 0; /* error */

	return ret;
}

static int unsched_send_dequeue(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t *event_tbl = gbl_args->event_tbl;
	em_status_t err;
	int i;

	for (i = 0; i < REPEAT_COUNT; i++) {
		err = em_send(event_tbl[i], unsched_queue);
		if (unlikely(err != EM_OK))
			return 0; /* error */
		event_tbl[i] = em_queue_dequeue(unsched_queue);
		if (unlikely(event_tbl[i] == EM_EVENT_UNDEF))
			return 0; /* error */
	}

	return i;
}

static int unsched_send_dequeue_multi(void)
{
	em_queue_t unsched_queue = gbl_args->unsched_queue;
	em_event_t *event_tbl = gbl_args->event_tbl;
	const int burst_size = gbl_args->event_opt.burst_size;
	int ret_send = 0;
	int ret_deq = 0;

	for (int i = 0; i < REPEAT_COUNT; i++) {
		ret_send += em_send_multi(&event_tbl[i * burst_size],
					  burst_size, unsched_queue);
		ret_deq += em_queue_dequeue_multi(unsched_queue,
						  &event_tbl[i * burst_size],
						  burst_size);
	}

	if (unlikely(ret_send != burst_size * REPEAT_COUNT ||
		     ret_deq != burst_size * REPEAT_COUNT))
		return 0; /* error */

	return ret_deq;
}

static void unsched_dequeue_free(void)
{
	unsched_dequeue();
	free_events();
}

static void unsched_dequeue_free_multi(void)
{
	unsched_dequeue_multi();
	free_events_multi();
}

static void create_send_unsched_sw_events(void)
{
	create_sw_events();
	unsched_send();
}

static void create_send_unsched_sw_events_multi(void)
{
	create_sw_events_multi();
	unsched_send_multi();
}

bench_info_t test_suite[] = {
	BENCH_INFO(event_sw_alloc, NULL, free_events, 0,
		   "em_event_alloc(sw)"),
	BENCH_INFO(event_pkt_alloc, NULL, free_events, 0,
		   "em_event_alloc(pkt)"),
	BENCH_INFO(event_vector_alloc, NULL, free_events, 0,
		   "em_event_alloc(vect)"),
	BENCH_INFO(event_sw_alloc_multi, NULL, free_events_multi, 0,
		   "em_event_alloc_multi(sw)"),
	BENCH_INFO(event_pkt_alloc_multi, NULL, free_events_multi, 0,
		   "em_event_alloc_multi(pkt)"),
	BENCH_INFO(event_vector_alloc_multi, NULL, free_events_multi, 0,
		   "em_event_alloc_multi(vect)"),
	BENCH_INFO(event_free, create_sw_events, NULL, 0,
		   "em_free(sw)"),
	BENCH_INFO(event_free, create_packets, NULL, 0,
		   "em_free(pkt)"),
	BENCH_INFO(event_free, create_vectors, NULL, 0,
		   "em_free(vect)"),
	BENCH_INFO(event_free_multi, create_sw_events_multi, NULL, 0,
		   "em_free_multi(sw)"),
	BENCH_INFO(event_free_multi, create_packets_multi, NULL, 0,
		   "em_free_multi(pkt)"),
	BENCH_INFO(event_free_multi, create_vectors_multi, NULL, 0,
		   "em_free_multi(vect)"),
	BENCH_INFO(event_vector_free, create_vectors, NULL, 0, NULL),
	BENCH_INFO(event_sw_alloc_free, NULL, NULL, 0,
		   "event_alloc_free(sw)"),
	BENCH_INFO(event_pkt_alloc_free, NULL, NULL, 0,
		   "event_alloc_free(pkt)"),
	BENCH_INFO(event_vector_alloc_free, NULL, NULL, 0,
		   "event_alloc_free(vect)"),
	BENCH_INFO(event_sw_alloc_free_multi, NULL, NULL, 0,
		   "event_alloc_free_multi(sw)"),
	BENCH_INFO(event_pkt_alloc_free_multi, NULL, NULL, 0,
		   "event_alloc_free_multi(pkt)"),
	BENCH_INFO(event_vector_alloc_free_multi, NULL, NULL, 0,
		   "event_alloc_free_multi(vect)"),
	BENCH_INFO(unsched_send, create_sw_events, unsched_dequeue_free, 0,
		   "em_send(unsched-Q)"),
	BENCH_INFO(unsched_send_multi, create_sw_events_multi, unsched_dequeue_free_multi, 0,
		   "em_send_multi(unsched-Q)"),
	BENCH_INFO(unsched_dequeue, create_send_unsched_sw_events, free_events, 0,
		   "em_queue_dequeue(unsched-Q)"),
	BENCH_INFO(unsched_dequeue_multi, create_send_unsched_sw_events_multi, free_events_multi, 0,
		   "em_queue_dequeue_multi(unsched-Q)"),
	BENCH_INFO(unsched_send_dequeue, create_sw_events, free_events, 0,
		   "event_send_dequeue(unsched-Q)"),
	BENCH_INFO(unsched_send_dequeue_multi, create_sw_events_multi, free_events_multi, 0,
		   "event_send_dequeue_multi(unsched-Q)"),
	BENCH_INFO(event_clone, create_sw_events, free_clone_events, 0,
		   "em_event_clone(sw)"),
	BENCH_INFO(event_clone, create_packets, free_clone_events, 0,
		   "em_event_clone(pkt)"),
	BENCH_INFO(event_clone_part, create_sw_events, free_clone_events, 0,
		   "em_event_clone_part(sw)"),
	BENCH_INFO(event_clone_part, create_packets, free_clone_events, 0,
		   "em_event_clone_part(pkt)"),
	BENCH_INFO(event_clone_part__no_uarea, create_sw_events, free_clone_events, 0,
		   "em_event_clone_part(sw, no uarea)"),
	BENCH_INFO(event_clone_part__no_uarea, create_packets, free_clone_events, 0,
		   "em_event_clone_part(pkt, no-uarea)"),
	BENCH_INFO(event_has_ref, create_packets, free_events, 0,
		   "em_event_has_ref(pkt)"),
	BENCH_INFO(event_ref, create_packets, free_clone_events, 0,
		   "em_event_ref(pkt)"),
	BENCH_INFO(event_pointer, create_sw_events, free_events, 0,
		   "em_event_pointer(sw)"),
	BENCH_INFO(event_pointer, create_packets, free_events, 0,
		   "em_event_pointer(pkt)"),
	BENCH_INFO(event_pointer_and_size, create_sw_events, free_events, 0,
		   "em_event_pointer_and_size(sw)"),
	BENCH_INFO(event_pointer_and_size, create_packets, free_events, 0,
		   "em_event_pointer_and_size(pkt)"),
	BENCH_INFO(event_get_size, create_sw_events, free_events, 0,
		   "em_event_get_size(sw)"),
	BENCH_INFO(event_get_size, create_packets, free_events, 0,
		   "em_event_get_size(pkt)"),
	BENCH_INFO(event_get_type, create_sw_events, free_events, 0,
		   "em_event_get_type(sw)"),
	BENCH_INFO(event_get_type, create_packets, free_events, 0,
		   "em_event_get_type(pkt)"),
	BENCH_INFO(event_get_type_multi, create_sw_events_multi, free_events_multi, 0,
		   "em_event_get_type_multi(sw)"),
	BENCH_INFO(event_get_type_multi, create_packets_multi, free_events_multi, 0,
		   "em_event_get_type_multi(pkt)"),
	BENCH_INFO(event_same_type_multi, create_sw_events_multi, free_events_multi, 0,
		   "em_event_same_type_multi(sw)"),
	BENCH_INFO(event_same_type_multi, create_packets_multi, free_events_multi, 0,
		   "em_event_same_type_multi(pkt)"),
	BENCH_INFO(event_set_type, create_sw_events, free_events, 0,
		   "em_event_set_type(sw)"),
	BENCH_INFO(event_set_type, create_packets, free_events, 0,
		   "em_event_set_type(pkt)"),
	BENCH_INFO(event_get_pool, create_sw_events, free_events, 0,
		   "em_event_get_pool(sw)"),
	BENCH_INFO(event_get_pool, create_packets, free_events, 0,
		   "em_event_get_pool(pkt)"),
	BENCH_INFO(event_get_pool_subpool, create_sw_events, free_events, 0,
		   "em_event_get_pool_subpool(sw)"),
	BENCH_INFO(event_get_pool_subpool, create_packets, free_events, 0,
		   "em_event_get_pool_subpool(pkt)"),
	BENCH_INFO(event_uarea_get, create_sw_events, free_events, 0,
		   "em_event_uarea_get(sw, null)"),
	BENCH_INFO(event_uarea_get, create_packets, free_events, 0,
		   "em_event_uarea_get(pkt, null)"),
	BENCH_INFO(event_uarea_get, create_ext_packets, free_events, 0,
		   "em_event_uarea_get(ext-pkt, null)"),
	BENCH_INFO(event_uarea_get_size, create_sw_events, free_events, 0,
		   "em_event_uarea_get(sw, size)"),
	BENCH_INFO(event_uarea_get_size, create_packets, free_events, 0,
		   "em_event_uarea_get(pkt, size)"),
	BENCH_INFO(event_uarea_get_size, create_ext_packets, free_events, 0,
		   "em_event_uarea_get(ext-pkt, size)"),
	BENCH_INFO(event_uarea_id_get, create_sw_events, free_events, 0,
		   "em_event_uarea_id_get(sw)"),
	BENCH_INFO(event_uarea_id_get, create_packets, free_events, 0,
		   "em_event_uarea_id_get(pkt)"),
	BENCH_INFO(event_uarea_id_get, create_ext_packets, free_events, 0,
		   "em_event_uarea_id_get(ext-pkt)"),
	BENCH_INFO(event_uarea_id_set, create_sw_events, free_events, 0,
		   "em_event_uarea_id_set(sw)"),
	BENCH_INFO(event_uarea_id_set, create_packets, free_events, 0,
		   "em_event_uarea_id_set(pkt)"),
	BENCH_INFO(event_uarea_id_set, create_ext_packets, free_events, 0,
		   "em_event_uarea_id_set(ext-pkt)"),
	BENCH_INFO(event_uarea_info, create_sw_events, free_events, 0,
		   "event_uarea_info(sw)"),
	BENCH_INFO(event_uarea_info, create_packets, free_events, 0,
		   "event_uarea_info(pkt)"),
	BENCH_INFO(event_uarea_info, create_ext_packets, free_events, 0,
		   "event_uarea_info(ext-pkt)"),
	BENCH_INFO(event_vector_tbl, create_vectors, free_events, 0, NULL),
	BENCH_INFO(event_vector_size, create_vectors, free_events, 0, NULL),
	BENCH_INFO(event_vector_max_size, create_vectors, free_events, 0, NULL),
	BENCH_INFO(event_vector_size_set, create_vectors, free_vectors, 0, NULL),
	BENCH_INFO(event_vector_info, create_vectors, free_vectors, 0, NULL),
	BENCH_INFO(core_id, NULL, NULL, 0, NULL),
	BENCH_INFO(core_count, NULL, NULL, 0, NULL),
	BENCH_INFO(odp_event2odp, create_sw_events, free_events, 0,
		   "em_odp_event2odp(sw)"),
	BENCH_INFO(odp_event2odp, create_packets, free_events, 0,
		   "em_odp_event2odp(pkt)"),
	BENCH_INFO(odp_events2odp, create_sw_events_multi, free_events_multi, 0,
		   "em_odp_events2odp(sw)"),
	BENCH_INFO(odp_events2odp, create_packets_multi, free_events_multi, 0,
		   "em_odp_events2odp(pkt)"),
	BENCH_INFO(odp_event2em, create_sw_events, free_events, 0,
		   "em_odp_event2em(sw)"),
	BENCH_INFO(odp_event2em, create_packets, free_events, 0,
		   "em_odp_event2em(pkt)"),
	BENCH_INFO(odp_events2em, create_sw_events_multi, free_events_multi, 0,
		   "em_odp_events2em(sw)"),
	BENCH_INFO(odp_events2em, create_packets_multi, free_events_multi, 0,
		   "em_odp_events2em(pkt)")
};

/* Print usage information */
static void usage(void)
{
	printf("\n"
	       "EM event API micro benchmarks\n"
	       "\n"
	       "Options:\n"
	       "  -b, --burst <num>       Test burst size for *_multi() tests (default %u).\n"
	       "  -c, --cache_size <num>  Pool cache size.\n"
	       "                          -1: use pool default value (default)\n"
	       "  -e, --event_size <num>  Test event size in bytes (default %u).\n"
	       "  -t, --time <opt>        Time measurement.\n"
	       "                          0: measure CPU cycles (default)\n"
	       "                          1: measure time\n"
	       "  -i, --index <idx>       Benchmark index to run indefinitely.\n"
	       "  -r, --rounds <num>      Run each test case 'num' times (default %u).\n"
	       "  -w, --write-csv         Write result to csv files(used in CI) or not.\n"
	       "                          default: not write\n"
	       "  -v, --vector_size <num> Test vector size (default %u).\n"
	       "  -h, --help              Display help and exit.\n\n"
	       "\n", BURST_SIZE, EVENT_SIZE, ROUNDS, VECTOR_SIZE);
}

/* Parse command line arguments */
static int parse_args(int argc, char *argv[], int num_bench, cmd_opt_t *com_opt/*out*/,
		      event_opt_t *event_opt/*out*/)
{
	int opt;
	int long_index;
	static const struct option longopts[] = {
		{"burst", required_argument, NULL, 'b'},
		{"cache_size", required_argument, NULL, 'c'},
		{"event_size", required_argument, NULL, 'e'},
		{"time", required_argument, NULL, 't'},
		{"index", required_argument, NULL, 'i'},
		{"rounds", required_argument, NULL, 'r'},
		{"write-csv", no_argument, NULL, 'w'},
		{"vector_size", required_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	static const char *shortopts =  "b:c:e:t:i:r:wv:h";

	event_opt->burst_size = BURST_SIZE;
	com_opt->time = 0; /* Measure CPU cycles */
	com_opt->bench_idx = 0; /* Run all benchmarks */
	com_opt->rounds = ROUNDS;
	com_opt->write_csv = 0; /* Do not write result to csv files */
	event_opt->cache_size = -1;
	event_opt->event_size = EVENT_SIZE;
	event_opt->vector_size = VECTOR_SIZE;

	while (1) {
		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;	/* No more options */

		switch (opt) {
		case 'b':
			event_opt->burst_size = atoi(optarg);
			break;
		case 'c':
			event_opt->cache_size = atoi(optarg);
			break;
		case 'e':
			event_opt->event_size = atoi(optarg);
			break;
		case 't':
			com_opt->time = atoi(optarg);
			break;
		case 'i':
			com_opt->bench_idx = atoi(optarg);
			break;
		case 'r':
			com_opt->rounds = atoi(optarg);
			break;
		case 'w':
			com_opt->write_csv = 1;
			break;
		case 'v':
			event_opt->vector_size = atoi(optarg);
			break;
		case 'h':
			usage();
			return 1;
		default:
			ODPH_ERR("Bad option. Use -h for help.\n");
			return -1;
		}
	}

	if (event_opt->burst_size < 1 ||
	    event_opt->burst_size > MAX_BURST) {
		ODPH_ERR("Invalid burst size (max %d)\n", MAX_BURST);
		exit(EXIT_FAILURE);
	}

	if (com_opt->rounds < 1) {
		ODPH_ERR("Invalid test cycle repeat count: %u\n", com_opt->rounds);
		return -1;
	}

	if (com_opt->bench_idx < 0 || com_opt->bench_idx > num_bench) {
		ODPH_ERR("Bad bench index %i\n", com_opt->bench_idx);
		return -1;
	}

	optind = 1; /* Reset 'extern optind' from the getopt lib */

	return 0;
}

/* Print system and application info */
static void print_info(const char *cpumask_str, const event_opt_t *event_opt,
		       const cmd_opt_t *com_opt)
{
	odp_sys_info_print();

	printf("\n"
	       "bench_events options\n"
	       "-------------------\n");

	printf("Burst size:        %d\n", event_opt->burst_size);
	printf("CPU mask:          %s\n", cpumask_str);
	printf("Event size:        %d\n", event_opt->event_size);
	printf("Measurement unit:  %s\n", com_opt->time ? "nsec" : "CPU cycles");
	if (event_opt->cache_size < 0)
		printf("Pool cache size:   default\n");
	else
		printf("Pool cache size:   %d\n", event_opt->cache_size);
	printf("Test rounds:       %u\n", com_opt->rounds);
	printf("Vector size:       %d\n", event_opt->vector_size);
	printf("\n");
}

static void init_default_pool_config(em_pool_cfg_t *pool_conf)
{
	em_pool_cfg_init(pool_conf);

	pool_conf->event_type = EM_EVENT_TYPE_SW;
	pool_conf->user_area.in_use = true;
	pool_conf->user_area.size = UAREA_SIZE;
	pool_conf->num_subpools = 1;
	pool_conf->subpool[0].size = EVENT_SIZE;
	pool_conf->subpool[0].num = 10;
	pool_conf->subpool[0].cache_size = 0;
}

#define CLONE_PART_FMT \
"Date,clone_part(sw),clone_part(pkt),clone_part(sw no-uarea),clone_part(pkt no-uarea)\n" \
"%s,%.2f,%.2f,%.2f,%.2f\n"

static void write_result_to_csv(void)
{
	FILE *file;
	char file_name[32] = {0};
	char time_str[72] = {0};
	double *result = gbl_args->run_bench_arg.result;

	fill_time_str(time_str);

	file = fopen("em_alloc.csv", "w");
	if (file == NULL) {
		perror("Failed to file em_alloc.csv");
		return;
	}

	fprintf(file, "Date,em_alloc(sw),em_alloc(pkt),em_alloc(vec)\n"
		"%s,%.2f,%.2f,%.2f\n", time_str, result[0], result[1], result[2]);

	fclose(file);

	/* em_alloc_multi */
	sprintf(file_name, "em_alloc_multi_%d.csv", gbl_args->event_opt.burst_size);
	file = fopen(file_name, "w");
	if (file == NULL) {
		perror("Failed to file em_alloc_multi_*.csv");
		return;
	}

	fprintf(file, "Date,em_alloc_multi(sw),em_alloc_multi(pkt),em_alloc_multi(vec)\n"
		"%s,%.2f,%.2f,%.2f\n", time_str, result[3], result[4], result[5]);
	fclose(file);

	/* em_free */
	file = fopen("em_free.csv", "w");
	if (file == NULL) {
		perror("Failed to file em_free.csv");
		return;
	}

	fprintf(file, "Date,em_free(sw),em_free(pkt),em_free(vec)\n"
		"%s,%.2f,%.2f,%.2f\n", time_str, result[6], result[7], result[8]);

	fclose(file);

	/* em_free_multi */
	memset(file_name, 0, sizeof(file_name));
	sprintf(file_name, "em_free_multi_%d.csv", gbl_args->event_opt.burst_size);
	file = fopen(file_name, "w");
	if (file == NULL) {
		perror("Failed to file bench_em_alloc_multi_*.csv");
		return;
	}

	fprintf(file, "Date,em_free_multi(sw),em_free_multi(pkt),em_free_multi(vec)\n"
		"%s,%.2f,%.2f,%.2f\n", time_str, result[9], result[10], result[11]);
	fclose(file);

	/* em_event_clone */
	file = fopen("em_event_clone.csv", "w");
	if (file == NULL) {
		perror("Failed to file em_event_clone.csv");
		return;
	}

	fprintf(file, "Date,em_event_clone(sw),em_event_clone(pkt)\n"
		"%s,%.2f,%.2f\n", time_str, result[25], result[26]);
	fclose(file);

	/* em_event_clone_part */
	file = fopen("em_event_clone_part.csv", "w");
	if (file == NULL) {
		perror("Failed to file em_event_clone_part.csv");
		return;
	}

	fprintf(file, CLONE_PART_FMT, time_str, result[27], result[28],
		result[29], result[30]);
	fclose(file);
}

int main(int argc, char *argv[])
{
	em_conf_t conf;
	cmd_opt_t com_opt;
	event_opt_t event_opt;
	em_pool_cfg_t pool_conf;
	em_core_mask_t core_mask;
	odph_helper_options_t helper_options;
	odph_thread_t worker_thread;
	odph_thread_common_param_t thr_common;
	odph_thread_param_t thr_param;
	odp_shm_t shm;
	odp_cpumask_t cpumask, default_mask;
	odp_instance_t instance;
	odp_init_t init_param;
	int worker_cpu;
	/* CPU mask as string */
	char cpumask_str[ODP_CPUMASK_STR_SIZE];
	int ret = 0;
	int num_bench = ARRAY_SIZE(test_suite);
	double result[ARRAY_SIZE(test_suite)] = {0};

	/* Let helper collect its own arguments (e.g. --odph_proc) */
	argc = odph_parse_options(argc, argv);
	if (odph_options(&helper_options)) {
		ODPH_ERR("Reading ODP helper options failed\n");
		exit(EXIT_FAILURE);
	}

	/* Parse and store the application arguments */
	ret = parse_args(argc, argv, num_bench, &com_opt, &event_opt);
	if (ret)
		exit(EXIT_FAILURE);

	odp_init_param_init(&init_param);
	init_param.mem_model = helper_options.mem_model;

	/* Init ODP before calling anything else */
	if (odp_init_global(&instance, &init_param, NULL)) {
		ODPH_ERR("Global init failed\n");
		exit(EXIT_FAILURE);
	}

	/* Init this thread */
	if (odp_init_local(instance, ODP_THREAD_CONTROL)) {
		ODPH_ERR("Local init failed\n");
		exit(EXIT_FAILURE);
	}

	odp_schedule_config(NULL);

	/* Get worker CPU */
	if (odp_cpumask_default_worker(&default_mask, 1) != 1) {
		ODPH_ERR("Unable to allocate worker thread\n");
		exit(EXIT_FAILURE);
	}
	worker_cpu = odp_cpumask_first(&default_mask);

	/* Init EM */
	em_core_mask_zero(&core_mask);
	em_core_mask_set(odp_cpu_id(), &core_mask);
	em_core_mask_set(worker_cpu, &core_mask);

	init_default_pool_config(&pool_conf);

	em_conf_init(&conf);
	if (helper_options.mem_model == ODP_MEM_MODEL_PROCESS)
		conf.process_per_core = 1;
	else
		conf.thread_per_core = 1;
	conf.default_pool_cfg = pool_conf;
	conf.core_count = 2;
	conf.phys_mask = core_mask;

	if (em_init(&conf) != EM_OK) {
		ODPH_ERR("EM init failed\n");
		exit(EXIT_FAILURE);
	}

	if (em_init_core() != EM_OK) {
		ODPH_ERR("EM core init failed\n");
		exit(EXIT_FAILURE);
	}

	if (setup_sig_handler()) {
		ODPH_ERR("Signal handler setup failed\n");
		exit(EXIT_FAILURE);
	}

	/* Reserve memory for args from shared mem */
	shm = odp_shm_reserve("shm_args", sizeof(gbl_args_t), ODP_CACHE_LINE_SIZE, 0);
	if (shm == ODP_SHM_INVALID) {
		ODPH_ERR("Shared mem reserve failed\n");
		exit(EXIT_FAILURE);
	}

	gbl_args = odp_shm_addr(shm);
	if (gbl_args == NULL) {
		ODPH_ERR("Shared mem alloc failed\n");
		exit(EXIT_FAILURE);
	}

	odp_atomic_init_u32(&exit_thread, 0);

	memset(gbl_args, 0, sizeof(gbl_args_t));
	gbl_args->sw_event_pool = EM_POOL_UNDEF;
	gbl_args->packet_pool = EM_POOL_UNDEF;
	gbl_args->unsched_queue = EM_QUEUE_UNDEF;

	gbl_args->event_opt = event_opt;
	gbl_args->run_bench_arg.opt = com_opt;
	gbl_args->run_bench_arg.bench = test_suite;
	gbl_args->run_bench_arg.num_bench = num_bench;
	gbl_args->run_bench_arg.result = result;

	for (int i = 0; i < MAX_EVENTS; i++) {
		gbl_args->event_tbl[i] = EM_EVENT_UNDEF;
		gbl_args->event2_tbl[i] = EM_EVENT_UNDEF;
		gbl_args->ptr_tbl[i] = NULL;
		gbl_args->u16_tbl[i] = 0;
		gbl_args->u32_tbl[i] = 0;
		gbl_args->et_tbl[i] = EM_EVENT_TYPE_UNDEF;
		gbl_args->pool_tbl[i] = EM_POOL_UNDEF;
		gbl_args->odp_event_tbl[i] = ODP_EVENT_INVALID;
	}

	(void)odp_cpumask_to_str(&default_mask, cpumask_str, sizeof(cpumask_str));

	print_info(cpumask_str, &event_opt, &com_opt);

	if (create_queues())
		goto exit;

	/* Create test event pools here to handle ESV preallocation */
	if (create_pools())
		goto exit;

	memset(&worker_thread, 0, sizeof(odph_thread_t));
	odp_cpumask_zero(&cpumask);
	odp_cpumask_set(&cpumask, worker_cpu);

	odph_thread_common_param_init(&thr_common);
	thr_common.instance = instance;
	thr_common.cpumask = &cpumask;
	thr_common.share_param = 1;

	odph_thread_param_init(&thr_param);
	thr_param.start = run_benchmarks;
	thr_param.arg = &gbl_args->run_bench_arg;
	thr_param.thr_type = ODP_THREAD_WORKER;

	odph_thread_create(&worker_thread, &thr_common, &thr_param, 1);

	odph_thread_join(&worker_thread, 1);

	ret = gbl_args->run_bench_arg.bench_failed;

	if (com_opt.write_csv)
		write_result_to_csv();

exit:
	if (gbl_args->sw_event_pool != EM_POOL_UNDEF)
		em_pool_delete(gbl_args->sw_event_pool);

	if (gbl_args->packet_pool != EM_POOL_UNDEF)
		em_pool_delete(gbl_args->packet_pool);

	if (gbl_args->vector_pool != EM_POOL_UNDEF)
		em_pool_delete(gbl_args->vector_pool);

	if (delete_queues())
		ODPH_ERR("Deleting queues failed\n");

	if (em_term_core() != EM_OK)
		ODPH_ERR("EM core terminate failed\n");

	if (em_term(&conf) != EM_OK)
		ODPH_ERR("EM terminate failed\n");

	if (odp_shm_free(shm)) {
		ODPH_ERR("Shared mem free failed\n");
		exit(EXIT_FAILURE);
	}

	if (odp_term_local()) {
		ODPH_ERR("Local term failed\n");
		exit(EXIT_FAILURE);
	}

	if (odp_term_global(instance)) {
		ODPH_ERR("Global term failed\n");
		exit(EXIT_FAILURE);
	}

	if (ret < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
