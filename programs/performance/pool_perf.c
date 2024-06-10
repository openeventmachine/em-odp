/*
 *   Copyright (c) 2023, Nokia Solutions and Networks
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
 * Event Machine performance test tool for memory pool performance.
 *
 * Measures time consumed during calls to allocate and free buffers or packets.
 * Command line arguments can be used to try different aspects.
 */
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdatomic.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>
#include <event_machine/platform/event_machine_odp_ext.h>

#include "cm_setup.h"
#include "cm_error_handler.h"
#include "pool_perf.h"

static em_status_t error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);
static em_status_t start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t stop(void *eo_context, em_eo_t eo);
static void receive_func(void *eo_context, em_event_t event, em_event_type_t type,
			 em_queue_t queue, void *q_context);

static int parse_args(int first, int argc, char *argv[]);
static void usage(void);
static uint64_t try_timestamp_overhead(void);
static void print_setup(void);
static void update_allocs(test_msg *msg, em_pool_t pool, size_t size, odp_pool_t odp_pool);
static bool enqueue_event(test_msg *msg, event_handle_t ev);
static bool dequeue_event(test_msg *msg, event_handle_t *ev);
static void free_ring(test_msg *msg);
static void print_upd_stat(test_msg *msg, uint64_t endt);
static void do_work(uint64_t ns);
static void print_global_stat(void);
static int parse_pool(char *arg);
static em_pool_t create_test_pool(void);
static em_event_t do_em_event_alloc(int64_t *diff, size_t size, em_pool_t pool);
static odp_event_t do_odp_event_alloc(int64_t *diff, size_t size, odp_pool_t pool);
static void do_odp_pktmulti_alloc(test_msg *msg, odp_pool_t odp_pool, size_t size,
				  int num, int64_t *diff);
static void do_odp_bufmulti_alloc(test_msg *msg, odp_pool_t odp_pool, size_t size,
				  int num, int64_t *diff);
static void allocate_events(test_msg *msg, em_pool_t pool, size_t size, odp_pool_t odp_pool,
			    bool skip);
static void allocate_events_multi(test_msg *msg, em_pool_t pool, size_t size, odp_pool_t odp_pool,
				  bool skip);
static void free_events(test_msg *msg, bool skip);
static void free_events_multi(test_msg *msg, bool skip);
static int64_t do_em_event_free(em_event_t ev);
static int64_t do_odp_event_free(odp_event_t ev);
static void add_to_bin(int bin, int64_t ns);
static void flush_data(test_msg *msg);
static void init_bins(void);
static void print_bin(int bin);
static void restart(em_queue_t testq);
static void print_uptime(void);

/* data */
static perf_shm_t *perf_shm;

config_data g_options = {
	.loops	= 1,
	.perloop = EVENTS_PER_LOOP,
	.events	= 1,
	.num_alloc = 1,
	.num_free = 1,
	.window = 2,
	.delay = 0,
	.size = 256,
	.skip = 0,
	.use_multi = false,
	.use_odp = false,
	.no_first = 1,
	.pool = { .num = 0, .size = 0, .cache = 0, .type = EM_EVENT_TYPE_SW },
	.flush = 0
};

const char *g_bin_names[NUM_BIN_INST] = { "alloc", "free" };
test_bin_t g_bins[NUM_BIN_INST][NUM_BINS];
const int64_t g_bin_limits[NUM_BINS] = { 10, 20, 50, 100, 200, 300, 500, 1000, 2000, -1 };

/***********************************************/
void usage(void)
{
	APPL_PRINT("pool_perf %s\n\n%s", VERSION, instructions);

	for (int i = 0; ; i++) {
		if (longopts[i].name == NULL)
			break;
		APPL_PRINT("-%c or --%-16s %s\n", longopts[i].val, longopts[i].name, descopts[i]);
	}
	APPL_PRINT("\n");
}

void init_bins(void)
{
	for (int i = 0; i < NUM_BIN_INST; i++)
		for (int b = 0; b < NUM_BINS; b++)
			g_bins[i][b].upto_ns = g_bin_limits[b];
}

int parse_pool(char *arg)
{
	unsigned int num, size, cache;
	char type[64];

	type[0] = 0;
	int parsed = sscanf(arg, "%u,%u,%u,%s", &num, &size, &cache, type);

	if (parsed != 4 || num == 0 || size == 0 || type[0] == 0)
		return 0;

	g_options.pool.num = num;
	g_options.pool.size = size;
	g_options.pool.cache = cache;

	if (!strcmp(type, "SW"))
		g_options.pool.type = EM_EVENT_TYPE_SW;
	else if (!strcmp(type, "PACKET"))
		g_options.pool.type = EM_EVENT_TYPE_PACKET;
	else
		return 0;

	return 1;
}

int parse_args(int first, int argc, char *argv[])
{
	optind = first + 1; /* skip '--' */
	while (1) {
		int opt;
		int long_index;
		char *endptr;
		int64_t num;

		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 'l': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.loops = (int)num;
		}
		break;

		case 'n': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 2)
				return 0;
			g_options.perloop = (unsigned int)num;
		}
		break;

		case 'e': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.events = (unsigned int)num;
		}
		break;

		case 'a': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num == 0)
				return 0;
			if (labs(num) > MAX_ALLOCS) {
				APPL_PRINT("Max allocs %u\n", MAX_ALLOCS);
				return 0;
			}
			g_options.num_alloc = (int)num;
		}
		break;

		case 'f': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num == 0)
				return 0;
			if (labs(num) > MAX_ALLOCS) {
				APPL_PRINT("Max free %u\n", MAX_ALLOCS);
				return 0;
			}
			g_options.num_free = (int)num;
		}
		break;

		case 'i': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.skip = (unsigned int)num;
		}
		break;

		case 'd': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.delay = (uint64_t)num;
		}
		break;

		case 's': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.size = (uint64_t)num;
		}
		break;

		case 'w': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			if (num > MAX_ALLOCS) {
				APPL_PRINT("Max window %u\n", MAX_ALLOCS);
				return 0;
			}
			g_options.window = (unsigned int)num;
		}
		break;

		case 'c': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.flush = (unsigned int)num; /* kB */
		}
		break;

		case 'p': {
			if (parse_pool(optarg) == 0)
				return 0;
		}
		break;

		case 'm': {
			g_options.use_multi = true;
		}
		break;

		case 't': {
			g_options.no_first = 1;
			if (optarg != NULL) { /* optional arg */
				num = strtol(optarg, &endptr, 0);
				if (*endptr != '\0' || num < 0)
					return 0;
				g_options.no_first = (unsigned int)num;
			}
		}
		break;

		case 'b': {
			g_options.use_odp = true;
		}
		break;

		case 'h':
		default:
			opterr = 0;
			usage();
			return 0;
		}
	}

	optind = 1; /* cm_setup() to parse again */
	return 1;
}

em_status_t error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
	return test_error_handler(eo, error, escope, args);
}

void do_work(uint64_t ns)
{
	uint64_t t1 = odp_time_local_strict_ns();

	/* just loop for given time */
	while ((odp_time_local_ns() - t1) < ns)
		;
}

void test_init(const appl_conf_t *appl_conf)
{
	(void)appl_conf;
	int core = em_core_id();

	if (core == 0) {
		perf_shm = env_shared_reserve("PerfSharedMem", sizeof(perf_shm_t));
		em_register_error_handler(error_handler);
		mlockall(MCL_FUTURE); /* make sure all memory is mapped at start */
	} else {
		perf_shm = env_shared_lookup("PerfSharedMem");
	}

	if (perf_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead,
			   "Test init failed on EM-core: %u\n",
			   core);
	else if (core == 0) {
		memset(perf_shm, 0, sizeof(perf_shm_t));
		APPL_PRINT("%luB Shared memory initialized\n", sizeof(perf_shm_t));
	}
}

uint64_t try_timestamp_overhead(void)
{
#define NUM_TS_TRY 20

	uint64_t oh = UINT64_MAX;

	/* measure time stamping overhead, take min */
	for (int i = 0; i < NUM_TS_TRY; i++) {
		uint64_t t1 = TIME_FN();
		uint64_t t2 = TIME_FN();
		uint64_t t3 = TIME_FN();

		if (t2 - t1 < oh)
			oh = t2 - t1;
		if (t3 - t2 < oh)
			oh = t3 - t2;
	}

	return oh ? oh - 1 : oh;
}

void add_to_bin(int bin, int64_t ns)
{
	for (int i = 0; i < NUM_BINS; i++) {
		if (ns <= g_bins[bin][i].upto_ns || g_bins[bin][i].upto_ns < 0) {
			g_bins[bin][i].count++; /* atomic_uint */
			break;
		}
	}
}

void print_bin(int bin)
{
	APPL_PRINT("\nBin '%s' counts:\n# up to ns\t", g_bin_names[bin]);
	for (int i = 0; i < NUM_BINS; i++)
		APPL_PRINT("%-13ld", g_bins[bin][i].upto_ns);
	APPL_PRINT("\n# \t\t");
	for (int i = 0; i < NUM_BINS; i++)
		APPL_PRINT("%-13lu", g_bins[bin][i].count);
	APPL_PRINT("\n");
}

bool enqueue_event(test_msg *msg, event_handle_t ev)
{
	if (msg->top >= MAX_ALLOCS)
		return false; /* full */

	msg->events[msg->top] = ev;
	msg->top++;
	return true;
}

bool dequeue_event(test_msg *msg, event_handle_t *ev)
{
	if (msg->top < 1)
		return false; /* empty */
	msg->top--;
	*ev = msg->events[msg->top];
	return true;
}

/* noinline for profiling */
em_event_t __attribute__ ((noinline)) do_em_event_alloc(int64_t *diff, size_t size, em_pool_t pool)
{
	em_event_t ev;
	uint64_t t1 = TIME_FN();

	ev = em_alloc(size, g_options.pool.type, pool);
	*diff = (int64_t)TIME_FN() - (int64_t)t1;
	return ev;
}

odp_event_t __attribute__ ((noinline)) do_odp_event_alloc(int64_t *diff, size_t size,
							  odp_pool_t pool)
{
	odp_event_t ev;
	uint64_t t1;

	if (g_options.pool.type == EM_EVENT_TYPE_PACKET) {
		t1 = TIME_FN();
		odp_packet_t pkt = odp_packet_alloc(pool, (uint32_t)size);
		*diff = (int64_t)TIME_FN() - (int64_t)t1;
		ev = odp_packet_to_event(pkt);
	} else {
		t1 = TIME_FN();
		odp_buffer_t buf = odp_buffer_alloc(pool);
		*diff = (int64_t)TIME_FN() - (int64_t)t1;
		ev = odp_buffer_to_event(buf);
	}

	return ev;
}

void allocate_events(test_msg *msg, em_pool_t pool, size_t size, odp_pool_t odp_pool, bool skip)
{
	unsigned int num = abs(g_options.num_alloc);

	if (g_options.num_alloc < 0) { /* negative means random up to */
		num = rand() % (num + 1);
	}

	for (unsigned int i = 0; i < num; i++) {
		int64_t diff;
		event_handle_t ev;

		if (g_options.use_odp) {
			ev.odp_ev = do_odp_event_alloc(&diff, size, odp_pool);
			if (ev.odp_ev == ODP_EVENT_INVALID) {
				APPL_PRINT("!! allocs %lu, stack top %d\n",
					   msg->allocated, msg->top);
			}
			test_fatal_if(ev.odp_ev == ODP_EVENT_INVALID, "odp alloc failed");
		} else {
			ev.em_ev = do_em_event_alloc(&diff, size, pool);
			test_fatal_if(ev.em_ev == EM_EVENT_UNDEF, "em_alloc failed");
		}

		diff -= perf_shm->ts_overhead;

		if (!skip) { /* instance stats */
			if (diff < msg->times.alloc.min_ns) {
				msg->times.alloc.min_ns = diff;
				msg->times.alloc.min_evt = msg->count;
			}
			if (diff > msg->times.alloc.max_ns) {
				msg->times.alloc.max_ns = diff;
				msg->times.alloc.max_evt = msg->count;
			}

			add_to_bin(BIN_ALLOC, diff);
		}

		test_fatal_if(enqueue_event(msg, ev) == false, "Unexpected: event ring full!");
		msg->allocated++; /* current batch */
		msg->times.alloc.tot_allocs++;
		msg->times.alloc.sum_ns += diff;
		perf_shm->counts[em_core_id()].allocs++;

		if (msg->allocated >= g_options.window)
			break;
	}
}

void do_odp_pktmulti_alloc(test_msg *msg, odp_pool_t odp_pool, size_t size, int num, int64_t *diff)
{
	odp_packet_t pkts[MAX_ALLOCS];
	uint64_t t1 = TIME_FN();
	int got = odp_packet_alloc_multi(odp_pool, (uint32_t)size, pkts, num);

	*diff = (int64_t)TIME_FN() - (int64_t)t1;
	test_fatal_if(num != got, "not enough events from pkt pool");

	for (int i = 0; i < num; i++)
		test_fatal_if(enqueue_event(msg,
					    (event_handle_t)odp_packet_to_event(pkts[i])) == false,
					    "Unexpected: event ring full!");
}

void do_odp_bufmulti_alloc(test_msg *msg, odp_pool_t odp_pool, size_t size, int num, int64_t *diff)
{
	(void)size;

	odp_buffer_t bufs[MAX_ALLOCS];
	uint64_t t1 = TIME_FN();
	int got = odp_buffer_alloc_multi(odp_pool, bufs, num);

	*diff = (int64_t)TIME_FN() - (int64_t)t1;
	test_fatal_if(num != got, "not enough events from bufpool");

	for (int i = 0; i < num; i++)
		test_fatal_if(enqueue_event(msg,
					    (event_handle_t)odp_buffer_to_event(bufs[i])) == false,
					    "Unexpected: event ring full!");
}

void allocate_events_multi(test_msg *msg, em_pool_t pool, size_t size, odp_pool_t odp_pool,
			   bool skip)
{
	int num = abs(g_options.num_alloc);

	if (g_options.num_alloc < 0) { /* random up to */
		num = rand() % (num + 1);
	}

	/* check max window */
	if (msg->allocated + num > g_options.window)
		num = g_options.window - msg->allocated;
	if (!num)
		return;

	int64_t diff;

	if (g_options.use_odp) { /* bypass, use ODP API */
		if (g_options.pool.type == EM_EVENT_TYPE_PACKET)
			do_odp_pktmulti_alloc(msg, odp_pool, size, num, &diff);
		else
			do_odp_bufmulti_alloc(msg, odp_pool, size, num, &diff);
	} else { /* use EM API */
		em_event_t ev[MAX_ALLOCS];
		uint64_t t1 = TIME_FN();
		int got = em_alloc_multi(ev, num, size, g_options.pool.type, pool);

		diff = (int64_t)TIME_FN() - (int64_t)t1;
		test_fatal_if(num != got, "not enough events from pool");

		for (int i = 0; i < num; i++)
			test_fatal_if(enqueue_event(msg, (event_handle_t)ev[i]) == false,
				      "Unexpected: event ring full!");
	}

	/* TS overhead compensation */
	diff -= perf_shm->ts_overhead;

	if (!skip) { /* per instance stats */
		if (diff < msg->times.alloc.min_ns) {
			msg->times.alloc.min_ns = diff;
			msg->times.alloc.min_evt = msg->count;
			msg->times.alloc.min_burst = num;
		}
		if (diff > msg->times.alloc.max_ns) {
			msg->times.alloc.max_ns = diff;
			msg->times.alloc.max_evt = msg->count;
			msg->times.alloc.max_burst = num;
		}
		add_to_bin(BIN_ALLOC, diff);
	}

	msg->allocated += num; /* current batch */
	msg->times.alloc.tot_allocs += num;
	msg->times.alloc.sum_ns += diff;
	perf_shm->counts[em_core_id()].allocs += num;
}

int64_t __attribute__ ((noinline)) do_em_event_free(em_event_t ev)
{
	test_fatal_if(ev == EM_EVENT_UNDEF, "Unexpected, no em event from ring not empty!");

	uint64_t t1 = TIME_FN();

	em_free(ev);
	return (int64_t)TIME_FN() - (int64_t)t1;
}

int64_t __attribute__ ((noinline)) do_odp_event_free(odp_event_t ev)
{
	test_fatal_if(ev == ODP_EVENT_INVALID, "Unexpected, no odp event from ring not empty!");
	uint64_t t1 = TIME_FN();

	odp_event_free(ev);
	return (int64_t)TIME_FN() - (int64_t)t1;
}

void free_events(test_msg *msg, bool skip)
{
	unsigned int num = abs(g_options.num_free);

	if (g_options.num_free < 0) { /* random up to */
		num = rand() % (num + 1);
	}

	/* there's always at least one to free if we're here */
	for (unsigned int i = 0; i < num; i++) {
		event_handle_t ev;
		int64_t diff;

		test_fatal_if(dequeue_event(msg, &ev) == false, "Unexpected, ring empty!");

		if (g_options.use_odp)
			diff = do_odp_event_free(ev.odp_ev);
		else
			diff = do_em_event_free(ev.em_ev);

		/* TS overhead */
		diff -= perf_shm->ts_overhead;

		if (!skip) { /* instance stats */
			if (diff < msg->times.free.min_ns) {
				msg->times.free.min_ns = diff;
				msg->times.free.min_evt = msg->count;
			}
			if (diff > msg->times.free.max_ns) {
				msg->times.free.max_ns = diff;
				msg->times.free.max_evt = msg->count;
			}
			add_to_bin(BIN_FREE, diff);
		}

		msg->allocated--;
		msg->times.free.tot_free++;
		msg->times.free.sum_ns += diff;
		perf_shm->counts[em_core_id()].frees++;

		if (!msg->allocated)
			break;
	}
}

void free_events_multi(test_msg *msg, bool skip)
{
	unsigned int num = abs(g_options.num_free);

	if (g_options.num_free < 0) { /* random up to */
		num = rand() % (num + 1);
	}
	if (msg->allocated < num)
		num = msg->allocated;
	if (!num)
		return;

	int64_t diff;

	if (g_options.use_odp) {
		odp_event_t events[MAX_ALLOCS];

		/* collect for multi first */
		for (unsigned int i = 0; i < num; i++)
			test_fatal_if(dequeue_event(msg, (event_handle_t *)&events[i]) == false,
				      "Unexpected, ring empty!");

		uint64_t t1 = TIME_FN();

		odp_event_free_multi(events, num);
		diff = (int64_t)TIME_FN() - (int64_t)t1;
	} else {
		em_event_t events[MAX_ALLOCS];

		for (unsigned int i = 0; i < num; i++)
			test_fatal_if(dequeue_event(msg, (event_handle_t *)&events[i]) == false,
				      "Unexpected, ring empty!");

		uint64_t t1 = TIME_FN();

		em_free_multi(events, num);
		diff = (int64_t)TIME_FN() - (int64_t)t1;
	}

	/* TS overhead */
	diff -= perf_shm->ts_overhead;

	if (!skip) { /* instance stats */
		if (diff < msg->times.free.min_ns) {
			msg->times.free.min_ns = diff;
			msg->times.free.min_evt = msg->count;
			msg->times.free.min_burst = num;
		}
		if (diff > msg->times.free.max_ns) {
			msg->times.free.max_ns = diff;
			msg->times.free.max_evt = msg->count;
			msg->times.free.max_burst = num;
		}
		add_to_bin(BIN_FREE, diff);
	}

	msg->allocated -= num;
	msg->times.free.tot_free += num;
	msg->times.free.sum_ns += diff;
	perf_shm->counts[em_core_id()].frees -= num;
}

void update_allocs(test_msg *msg, em_pool_t pool, size_t size, odp_pool_t odp_pool)
{
	/* operation is per context, event contains all state */

	perf_shm->counts[em_core_id()].events++;

	if (msg->times.alloc.tot_allocs == 0)
		msg->start_ns = TIME_FN(); /* start of cycle */

	bool skip = g_options.skip && (msg->times.alloc.tot_allocs < g_options.skip) ? true : false;

	/* first do some allocations */
	if (!(msg->count & 1)) { /* on even counts */

		if (g_options.no_first &&
		    perf_shm->counts[em_core_id()].events <= g_options.no_first)
			skip = true;

		if (msg->allocated < g_options.window) {
			if (g_options.use_multi)
				allocate_events_multi(msg, pool, size, odp_pool, skip);
			else
				allocate_events(msg, pool, size, odp_pool, skip);
		}
	} else {
		/* then frees on odd counts */
		if (msg->allocated) {
			if (g_options.use_multi)
				free_events_multi(msg, skip);
			else
				free_events(msg, skip);
		}
	}
}

void flush_data(test_msg *msg)
{
	/* simple simulated cache flushing, copy 2nd half of buffer to begin */

	size_t len =  msg->flush_b / 2;
	uint8_t *b1 = (uint8_t *)msg->flush_buf;
	uint8_t *b2 = b1 + len;

	memcpy(b1, b2, len);
}

em_pool_t create_test_pool(void)
{
	em_pool_cfg_t cfg;

	em_pool_cfg_init(&cfg);
	cfg.num_subpools = 1; /* simple single subpool for now */
	cfg.event_type = g_options.pool.type;
	cfg.subpool[0].num = g_options.pool.num;
	cfg.subpool[0].size = g_options.pool.size;
	cfg.subpool[0].cache_size = g_options.pool.cache;

	em_pool_t pool = em_pool_create("TestPool", EM_POOL_UNDEF, &cfg);

	return pool;
}

void free_ring(test_msg *msg)
{
	event_handle_t ev;

	while (dequeue_event(msg, &ev)) {
		if (g_options.use_odp)
			odp_event_free(ev.odp_ev);
		else
			em_free(ev.em_ev);
	}
	msg->allocated = 0;
}

void print_uptime(void)
{
	uint64_t ns = TIME_FN() - perf_shm->start_time;
	uint64_t ms = ns / 1000000ULL;
	uint64_t seconds = ms / 1000ULL;
	uint64_t minutes = seconds / 60;
	uint64_t hours = minutes / 60;

	APPL_PRINT("%lu:%02lu:%02lu.%03lu", hours, minutes % 60, seconds % 60, ms % 1000);
}

void print_setup(void)
{
	APPL_PRINT("\n Cores:             %u\n", perf_shm->core_count);
	APPL_PRINT(" Loops:             %u\n", g_options.loops);
	APPL_PRINT(" Batch per loop:    %u\n", g_options.perloop);
	APPL_PRINT(" Parallel events:   %u\n", g_options.events);
	APPL_PRINT(" Num burst alloc:   %u%s\n",
		   abs(g_options.num_alloc), g_options.num_alloc < 0 ? "(max rnd)" : "");
	APPL_PRINT(" Num burst free:    %u%s\n",
		   abs(g_options.num_free), g_options.num_free < 0 ? "(max rnd)" : "");
	APPL_PRINT(" Alloc window:      %u\n", g_options.window);
	APPL_PRINT(" Delay after alloc: %luns\n", g_options.delay);
	APPL_PRINT(" Event size:        %u\n", g_options.size);
	APPL_PRINT(" Bypass EM:         %s\n", g_options.use_odp ? "yes" : "no");
	APPL_PRINT(" Flush/copy:        %ukB\n", g_options.flush);
	if (g_options.skip)
		APPL_PRINT(" Skip first:        %u\n", g_options.skip);
	APPL_PRINT(" Skip N per core:   %u\n", g_options.no_first);
	if (g_options.pool.num) {
		char type[10] = {0};

		if (g_options.pool.type == EM_EVENT_TYPE_SW)
			strncpy(type, "SW", sizeof(type));
		else
			strncpy(type, "PACKET", sizeof(type));
		APPL_PRINT(" Test pool:         %u * %uB, cache %u, %s\n",
			   g_options.pool.num, g_options.pool.size,
			   g_options.pool.cache, type);
	}
	APPL_PRINT(" Use _multi:        %s\n\n", g_options.use_multi ? "yes" : "no");

	APPL_PRINT("Using test pool:\n");
	em_pool_info_print(perf_shm->eo_ctx.test_pool);
	APPL_PRINT("\n");
}

void restart(em_queue_t testq)
{
	/* start new round */
	perf_shm->events_left = g_options.events;

	for (unsigned int i = 0; i < g_options.events; i++) {
		em_event_t ev = em_alloc(sizeof(test_msg), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

		test_fatal_if(ev == EM_EVENT_UNDEF, "Event alloc fail");
		test_msg *msg = em_event_pointer(ev);

		memset(msg, 0, sizeof(test_msg));
		for (int j = 0; j < MAX_ALLOCS; j++) {
			if (g_options.use_odp)
				msg->events[j].odp_ev = ODP_EVENT_INVALID;
			else
				msg->events[j].em_ev = EM_EVENT_UNDEF;
		}
		msg->times.alloc.min_ns = INT64_MAX;
		msg->times.alloc.max_ns = INT64_MIN;
		msg->instance = i + 1;
		msg->flush_b = g_options.flush * 1024;
		msg->flush_buf = NULL;
		if (g_options.flush) {
			msg->flush_buf = malloc(msg->flush_b);
			test_fatal_if(msg->flush_buf == NULL, "Malloc fail");
		}

		em_status_t ret = em_send(ev, testq);

		test_fatal_if(ret != EM_OK, "Send fail");
	}

	APPL_PRINT("Start, sent %u context events\n", g_options.events);
}

void test_start(const appl_conf_t *appl_conf)
{
	test_fatal_if(appl_conf->core_count > MAX_CORES, "Too many cores");

	/* Store the number of EM-cores running the application */
	perf_shm->core_count = appl_conf->core_count;

	perf_shm->ts_overhead = try_timestamp_overhead();
	APPL_PRINT("time stamp pair overhead seems to be %lu ns\n",
		   perf_shm->ts_overhead);

	init_bins();

	/* Create EO */
	perf_shm->eo = em_eo_create("pool test eo", start, NULL, stop, NULL, receive_func,
				    &perf_shm->eo_ctx);
	test_fatal_if(perf_shm->eo == EM_EO_UNDEF, "EO create failed");

	/* Queues */
	em_queue_t q = em_queue_create("testQ", EM_QUEUE_TYPE_PARALLEL,
				       EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT, NULL);

	test_fatal_if(q == EM_QUEUE_UNDEF, "Q create fail");
	em_eo_add_queue_sync(perf_shm->eo, q);

	/* Pool */
	if (g_options.pool.num) { /* create specific test pool */
		em_pool_t pool = create_test_pool();

		test_fatal_if(pool == EM_POOL_UNDEF, "Can't create test pool!");
		perf_shm->eo_ctx.test_pool = pool;
	} else {
		if (appl_conf->num_pools >= 1)
			perf_shm->eo_ctx.test_pool = appl_conf->pools[0];
		else
			perf_shm->eo_ctx.test_pool = EM_POOL_DEFAULT;
	}

	odp_pool_t opool;

	test_fatal_if(em_odp_pool2odp(perf_shm->eo_ctx.test_pool, &opool, 1) < 1,
		      "Unexpected: can't get odp pool from test pool");
	perf_shm->eo_ctx.odp_pool = opool; /* ignores subpools */

	/* adjust window to fit allocs */
	unsigned int maxval = abs(g_options.num_alloc);

	if ((unsigned int)abs(g_options.num_free) > maxval)
		maxval = abs(g_options.num_free);
	if (maxval > g_options.window) {
		g_options.window = maxval;
		APPL_PRINT("Increased alloc window to %u\n", g_options.window);
	}

	print_setup();

	/* Start EO */
	em_status_t start_ret = EM_ERR, ret;

	perf_shm->eo_ctx.test_q = q;
	perf_shm->stopping = false;
	odp_spinlock_init(&perf_shm->lock);
	perf_shm->times.min_alloc = INT64_MAX;
	perf_shm->times.min_free = INT64_MAX;

	ret = em_eo_start_sync(perf_shm->eo, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO start:%" PRI_STAT " %" PRI_STAT "", ret, start_ret);
}

void test_stop(const appl_conf_t *appl_conf)
{
	em_eo_t eo;
	em_status_t ret;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, em_core_id());

	/* Stop EOs */
	eo = perf_shm->eo;
	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);

	/* Remove and delete all of the EO's queues, then delete the EO */
	eo = perf_shm->eo;
	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "", ret, eo);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);
}

void test_term(const appl_conf_t *appl_conf)
{
	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, em_core_id());
	em_unregister_error_handler();
	env_shared_free(perf_shm);
}

em_status_t start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	(void)conf;

	APPL_PRINT("EO %" PRI_EO " starting\n", eo);

	perf_shm->start_time = TIME_FN();
	restart(((app_eo_ctx *)eo_context)->test_q);
	return EM_OK;
}

void print_global_stat(void)
{
	APPL_PRINT("Test time ");
	print_uptime();
	APPL_PRINT("\nGLOBAL stats, loops completed %d/%d:\n",
		   perf_shm->loopcount, g_options.loops);
	APPL_PRINT("# global min alloc: %ld ns", perf_shm->times.min_alloc);
	if (perf_shm->times.min_alloc <= perf_shm->ts_overhead)
		APPL_PRINT(" (oh comp. %ld)", perf_shm->ts_overhead);
	APPL_PRINT("\n# global max alloc: %ld ns\n", perf_shm->times.max_alloc);

	int64_t avg = perf_shm->times.time_allocs / perf_shm->times.num_alloc;
	double run_us = (double)(TIME_FN() - perf_shm->start_time) / 1000.0;

	APPL_PRINT("# global avg alloc: %ld ns (per buf/pkt)\n", avg);
	APPL_PRINT("# global alloc rate: %f M alloc/s\n\n",
		   (double)perf_shm->times.num_alloc / (double)run_us);

	APPL_PRINT("# global min free: %ld ns", perf_shm->times.min_free);
	if (perf_shm->times.min_free <= perf_shm->ts_overhead)
		APPL_PRINT(" (oh comp. %ld)", perf_shm->ts_overhead);
	APPL_PRINT("\n# global max free: %ld ns\n", perf_shm->times.max_free);

	avg = perf_shm->times.time_free / perf_shm->times.num_free;
	APPL_PRINT("# global avg free: %ld ns (per buf/pkt)\n\n", avg);

	print_bin(BIN_ALLOC);
	print_bin(BIN_FREE);

	/* per core */
	APPL_PRINT("\ncore#   #allocs     #frees      #events     #burst avg\n");
	APPL_PRINT("------------------------------------------------------\n");
	for (unsigned int i = 0; i < perf_shm->core_count; i++)
		APPL_PRINT("%-8d%-12lu%-12lu%-12lu%-12.2f\n",
			   i, perf_shm->counts[i].allocs,
			   perf_shm->counts[i].frees,
			   perf_shm->counts[i].events,
			   (double)perf_shm->counts[i].allocs /
			   (double)perf_shm->counts[i].events * 2);
	APPL_PRINT("\n");
}

void print_upd_stat(test_msg *msg, uint64_t endt)
{
	APPL_PRINT("# Instance #%lu:\n", msg->instance);

	if (g_options.use_multi)
		APPL_PRINT("MULTI alloc mode (times are one burst!)\n");

	/* alloc */
	if (msg->times.alloc.min_ns <= perf_shm->ts_overhead)
		APPL_PRINT("# Min alloc: %ld ns (oh compensation -%ld)\n",
			   msg->times.alloc.min_ns, perf_shm->ts_overhead);
	else
		APPL_PRINT("# Min alloc: %ld ns\n", msg->times.alloc.min_ns);
	APPL_PRINT("# Min alloc evt#: %lu\n", msg->times.alloc.min_evt);
	if (g_options.use_multi)
		APPL_PRINT("# Min alloc burst#: %lu\n", msg->times.alloc.min_burst);

	APPL_PRINT("# Max alloc: %ld ns\n", msg->times.alloc.max_ns);
	APPL_PRINT("# Max alloc evt#: %lu\n", msg->times.alloc.max_evt);
	if (g_options.use_multi)
		APPL_PRINT("# Max alloc burst#: %lu\n", msg->times.alloc.max_burst);

	int64_t avg = msg->times.alloc.sum_ns / msg->times.alloc.tot_allocs;

	APPL_PRINT("# Avg alloc: %ld ns\n", avg);
	APPL_PRINT("Total allocs: %lu\n", msg->times.alloc.tot_allocs);
	APPL_PRINT("Total events: %lu\n", msg->count);

	/* free */
	if (msg->times.free.min_ns <= perf_shm->ts_overhead)
		APPL_PRINT("# Min free: %ld ns (oh compensation -%ld)\n",
			   msg->times.free.min_ns, perf_shm->ts_overhead);
	else
		APPL_PRINT("# Min free: %ld ns\n", msg->times.free.min_ns);
	APPL_PRINT("# Min free evt#: %lu\n", msg->times.free.min_evt);

	APPL_PRINT("# Max free: %ld ns\n", msg->times.free.max_ns);
	APPL_PRINT("# Max free evt#: %lu\n", msg->times.free.max_evt);

	avg = msg->times.free.sum_ns / msg->times.free.tot_free;
	APPL_PRINT("# Avg free: %ld ns\n", avg);
	APPL_PRINT("Total free: %lu\n", msg->times.free.tot_free);

	double secs = (double)(endt - msg->start_ns) / 1000000000.0;
	double rate = (double)msg->times.alloc.tot_allocs / secs;

	APPL_PRINT("Run time, s: %.6f\n", secs);
	APPL_PRINT("# Alloc rate (M a/sec): %.5f\n\n", rate / 1000000);

	/* update global stats */
	odp_spinlock_lock(&perf_shm->lock);

	if (msg->times.alloc.min_ns < perf_shm->times.min_alloc)
		perf_shm->times.min_alloc = msg->times.alloc.min_ns;

	if (msg->times.alloc.max_ns > perf_shm->times.max_alloc)
		perf_shm->times.max_alloc = msg->times.alloc.max_ns;

	perf_shm->times.num_alloc += msg->times.alloc.tot_allocs;
	perf_shm->times.time_allocs += msg->times.alloc.sum_ns;
	perf_shm->times.num_updates++;

	if (msg->times.free.min_ns < perf_shm->times.min_free)
		perf_shm->times.min_free = msg->times.free.min_ns;

	if (msg->times.free.max_ns > perf_shm->times.max_free)
		perf_shm->times.max_free = msg->times.free.max_ns;
	perf_shm->times.num_free += msg->times.free.tot_free;
	perf_shm->times.time_free += msg->times.free.sum_ns;
	perf_shm->times.num_updates++;

	odp_spinlock_unlock(&perf_shm->lock);
}

em_status_t stop(void *eo_context, em_eo_t eo)
{
	(void)eo_context;

	perf_shm->stopping = true;
	APPL_PRINT("EO %" PRI_EO " stopping\n", eo);
	return EM_OK;
}

void receive_func(void *eo_context, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *q_context)
{
	app_eo_ctx *ctx = (app_eo_ctx *)eo_context;
	test_msg *msg = em_event_pointer(event);
	em_status_t ret;

	(void)type;
	(void)q_context;
	(void)queue;

	if (perf_shm->stopping) {
		if (msg->flush_buf)
			free(msg->flush_buf);
		free_ring(msg);
		em_free(event);
		return;
	}

	/* allocate or free some buffers */
	update_allocs(msg, ctx->test_pool, g_options.size, ctx->odp_pool);
	msg->count++;

	 /* optional simulated processing time */
	if (g_options.delay)
		do_work(g_options.delay);

	/* optional cache flush simulation */
	if (msg->flush_buf)
		flush_data(msg);

	/* one instance loop done? -> collect statistics */
	if (msg->count >= g_options.perloop) {
		uint64_t endt = TIME_FN();

		APPL_PRINT("Loop complete for instance #%lu at core %d\n",
			   msg->instance, em_core_id());
		print_upd_stat(msg, endt);
		if (msg->flush_buf)
			free(msg->flush_buf);
		free_ring(msg);
		em_free(event);

		if (atomic_fetch_sub(&perf_shm->events_left, 1) == 1) {
			/* last instance now done */
			perf_shm->loopcount++;
			print_global_stat();
			if (g_options.loops && perf_shm->loopcount >= g_options.loops) {
				perf_shm->stopping = true;
				raise(SIGINT);
				return;
			}
			restart(ctx->test_q);
		}
		return;
	}

	/* not done, keep going */
	ret = em_send(event, ctx->test_q);
	test_fatal_if(ret != EM_OK, "Event send fail");
}

/***** MAIN *****/
int main(int argc, char *argv[])
{
	/* pick app-specific arguments after '--' */
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			break;
	}
	if (i < argc) {
		if (!parse_args(i, argc, argv)) {
			APPL_PRINT("Invalid application arguments\n");
			return 1;
		}
	}

	return cm_setup(argc, argv);
}
