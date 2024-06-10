/*
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
 * Event Machine performance test for scheduling
 *
 * Measures time consumed during an event send-receive and/or achieved
 * throughout.
 * Command line arguments can be used to try different setups.
 */
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdatomic.h>

#include <event_machine.h>
#include <event_machine/platform/env/environment.h>

#include "cm_setup.h"
#include "cm_error_handler.h"
#include "scheduling_latency.h"
#include "event_machine/helper/event_machine_debug.h"

static em_status_t error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);
static em_status_t start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t stop(void *eo_context, em_eo_t eo);
static void receive_func(void *eo_context, em_event_t event, em_event_type_t type,
			 em_queue_t queue, void *q_context);

static void update_stats(int64_t ts, int64_t diff, uint64_t count);
static void print_stats(int64_t start_time, int64_t loop_start_time, uint64_t count);
static int parse_args(int first, int argc, char *argv[]);
static void usage(void);
static void do_work(test_msg *msg);
static int64_t mask_from_str(const char *optarg, em_core_mask_t *mask);
static const char *queue_type_str(em_queue_type_t type);
static void entry_hook(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
		       em_queue_t *queue, void **q_ctx);
static void exit_hook(em_eo_t eo);
static uint64_t try_timestamp_overhead(void);

/* data */
static perf_shm_t *perf_shm;
static __thread uint64_t entry_ts; /* core local */
static __thread int64_t max_eo_time; /* core local */

config_data g_options = {
	.loops		= 1,
	.queue_type	= EM_QUEUE_TYPE_ATOMIC,
	.lo_events	= 0,
	.work_ns	= 2000,
	.atomic_end	= false,
	.eo_receive	= false
};

/*************************************************************************/
void entry_hook(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
		em_queue_t *queue, void **q_ctx)
{
	(void)eo;
	(void)eo_ctx;
	(void)events;
	(void)num;
	(void)queue;
	(void)q_ctx;

	entry_ts = odp_time_global_strict_ns();
}

void exit_hook(em_eo_t eo)
{
	(void)eo;

	int64_t diff = odp_time_global_strict_ns() - entry_ts;

	if (max_eo_time < diff)
		max_eo_time = diff;
}

const char *queue_type_str(em_queue_type_t type)
{
	switch (type) {
	case EM_QUEUE_TYPE_PARALLEL:
			return "PARALLEL";
		break;
	case EM_QUEUE_TYPE_PARALLEL_ORDERED:
			return "ORDERED";
		break;
	case EM_QUEUE_TYPE_ATOMIC:
			return "ATOMIC";
		break;
	default:
		break;
	}
	return "<?>";
}

int64_t mask_from_str(const char *hex, em_core_mask_t *cmask)
{
	uint64_t mask;

	if (hex == NULL)
		return 0;
	if (sscanf(hex, "%lx", &mask) != 1)
		return 0;

	em_core_mask_set_bits(&mask, 1, cmask);
	return em_core_mask_count(cmask);
}

void update_stats(int64_t now, int64_t diff, uint64_t count)
{
	/* dispatch time */
	int64_t dtime = now - em_debug_timestamp(EM_DEBUG_TSP_SCHED_RETURN) - perf_shm->ts_overhead;

	/* send-receive */
	if (diff < perf_shm->times.mint) {
		perf_shm->times.mint = diff;
		perf_shm->times.minnum = count;
	} else if (diff > perf_shm->times.maxt) {
		perf_shm->times.maxt = diff;
		perf_shm->times.maxnum = count;
		perf_shm->times.maxdisp = dtime;
	}
	perf_shm->times.sum += diff;

	/* dispatch overhead min/max */
	if (dtime < perf_shm->times.disp_min)
		perf_shm->times.disp_min = dtime;
	else if (dtime > perf_shm->times.disp_max)
		perf_shm->times.disp_max = dtime;

	/* EO receive time. Only includes the timing event processing */
	if (g_options.eo_receive && perf_shm->times.max_eo_time < max_eo_time)
		perf_shm->times.max_eo_time = max_eo_time;
}

void do_work(test_msg *msg)
{
	msg->count++;
	if (msg->work == 0)
		return;

	uint64_t t1 = odp_time_global_strict_ns();

	/* just loop for given time */
	while ((odp_time_global_ns() - t1) < msg->work)
		;
}

void print_stats(int64_t start_time, int64_t loop_start_time, uint64_t count)
{
	double period = (double)odp_time_global_ns() - loop_start_time;
	double runtime = (double)odp_time_global_ns() - start_time;

	period /= 1000000000; /* sec */
	runtime /= 1000000000;

	double rate =  ((count - perf_shm->stat_mcount) / period) / 1000000; /* M/sec */
	uint64_t average = perf_shm->times.sum / (count - START_EVENTS);

	if (em_debug_timestamp(EM_DEBUG_TSP_SCHED_RETURN) == 0)
		perf_shm->times.maxdisp = 0;

	APPL_PRINT(": time(h) cores events(M) rate(M/s) min[ns] max[ns] avg[ns] min ev#     max ev#     max_do[ns] max_eo[ns]\n");
	APPL_PRINT(": %-7.3f %-5d %-9lu %-9.3f %-7lu %-7lu %-7lu %-11lu %-11lu %-10lu %lu\n",
		   runtime / (60 * 60), perf_shm->core_count, count / 1000000, rate,
		   perf_shm->times.mint, perf_shm->times.maxt, average,
		   perf_shm->times.minnum, perf_shm->times.maxnum,
		   perf_shm->times.maxdisp, perf_shm->times.max_eo_time);

	if (g_options.lo_events) {
		double lrate = ((perf_shm->num_lo - perf_shm->stat_lcount) / period) / 1000000;

		APPL_PRINT(": bg events(M) rate(M/s)\n");
		APPL_PRINT(": %-12.3f %.3f\n", ((double)perf_shm->num_lo) / 1000000, lrate);
		perf_shm->stat_lcount = perf_shm->num_lo;
	}
	perf_shm->stat_mcount = count;
}

void usage(void)
{
	APPL_PRINT("scheduling_latency %s\n\n%s", VERSION, instructions);

	for (int i = 0; ; i++) {
		if (longopts[i].name == NULL)
			break;
		APPL_PRINT("-%c or --%-16s %s\n", longopts[i].val, longopts[i].name, descopts[i]);
	}
	APPL_PRINT("\n");
}

int parse_args(int first, int argc, char *argv[])
{
	em_core_mask_zero(&g_options.hgroup);
	em_core_mask_zero(&g_options.lgroup);

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
		case 'a': {
			g_options.atomic_end = true;
		}
		break;
		case 'r': {
			g_options.eo_receive = true;
		}
		break;
		case 'l': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.loops = (uint64_t)num;
		}
		break;
		case 'q': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			switch (num) {
			case 0:
				g_options.queue_type = EM_QUEUE_TYPE_PARALLEL;
				break;
			case 1:
				g_options.queue_type = EM_QUEUE_TYPE_ATOMIC;
				break;
			case 2:
				g_options.queue_type = EM_QUEUE_TYPE_PARALLEL_ORDERED;
				break;

			default: return 0;
			}
		}
		break;
		case 'e': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.lo_events = (uint64_t)num;
		}
		break;
		case 'w': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.work_ns = (uint64_t)num;
		}
		break;
		case 'g': { /* low-prio grp */
			num = mask_from_str(optarg, &g_options.lgroup);
			if (!num)
				return 0;
		}
		break;
		case 't': { /* hi-prio grp */
			num = mask_from_str(optarg, &g_options.hgroup);
			if (!num)
				return 0;
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
#define NUM_TS_TRY 5
	uint64_t oh = UINT64_MAX;

	/* measure time stamping overhead, take min */
	for (int i = 0; i < NUM_TS_TRY; i++) {
		uint64_t t1 = odp_time_global_ns();
		uint64_t t2 = odp_time_global_ns();
		uint64_t t3 = odp_time_global_ns();

		if (t2 - t1 < oh)
			oh = t2 - t1;
		if (t3 - t2 < oh)
			oh = t3 - t2;
	}

	return oh;
}

void test_start(const appl_conf_t *appl_conf)
{
	/* Store the number of EM-cores running the application */
	perf_shm->core_count = appl_conf->core_count;

	perf_shm->ts_overhead = (int64_t)try_timestamp_overhead();
	APPL_PRINT("odp_time_global_ns pair overhead seems to be %lu ns\n", perf_shm->ts_overhead);

	/* Create EO */
	perf_shm->eo = em_eo_create("perf test eo", start, NULL, stop, NULL, receive_func,
				    &perf_shm->eo_ctx);
	test_fatal_if(perf_shm->eo == EM_EO_UNDEF, "EO create failed");

	/* Queues */
	em_queue_group_t grp = EM_QUEUE_GROUP_DEFAULT;
	char buf[32];

	APPL_PRINT("Using queue type %d (%s) for timing\n",
		   (int)g_options.queue_type, queue_type_str(g_options.queue_type));

	if (em_core_mask_count(&g_options.hgroup)) { /* separate queue group for timing */
		grp = em_queue_group_create_sync("HGRP", &g_options.hgroup);
		test_fatal_if(grp == EM_QUEUE_GROUP_UNDEF, "Can't create hi-prio queue group!");
		em_core_mask_tostr(buf, 32, &g_options.hgroup);
		APPL_PRINT("Coremask for hi-prio events: %s (%d cores)\n",
			   buf, em_core_mask_count(&g_options.hgroup));
	} else {
		APPL_PRINT("Using default queue group for hi-prio\n");
	}

	em_queue_t q = em_queue_create("testQ", g_options.queue_type,
				       EM_QUEUE_PRIO_HIGHEST, grp, NULL);

	test_fatal_if(q == EM_QUEUE_UNDEF, "Q create fail");
	em_eo_add_queue_sync(perf_shm->eo, q);

	/* Low priority background work queue */
	grp = EM_QUEUE_GROUP_DEFAULT;
	if (em_core_mask_count(&g_options.lgroup)) { /* separate queue group for background */
		grp = em_queue_group_create_sync("LGRP", &g_options.lgroup);
		test_fatal_if(grp == EM_QUEUE_GROUP_UNDEF, "Can't create lower-prio queue group!");
		em_core_mask_tostr(buf, 32, &g_options.lgroup);
		APPL_PRINT("Coremask for background events: %s (%d cores)\n",
			   buf, em_core_mask_count(&g_options.lgroup));
	} else {
		APPL_PRINT("Using default queue group for background events\n");
	}

	if (em_core_mask_count(&g_options.lgroup) && em_core_mask_count(&g_options.hgroup)) {
		em_core_mask_t mask;

		em_core_mask_and(&mask, &g_options.lgroup, &g_options.hgroup);
		APPL_PRINT("Queue groups are %soverlapping\n", em_core_mask_count(&mask) ? "" : "not ");
	} else {
		APPL_PRINT("Queue groups are overlapping\n");
	}

	em_queue_t q2 = em_queue_create("testQlo", EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_PRIO_NORMAL,
					grp, NULL);

	test_fatal_if(q2 == EM_QUEUE_UNDEF, "Q create fail");
	em_eo_add_queue_sync(perf_shm->eo, q2);

	if (g_options.lo_events)
		APPL_PRINT("Background work: %lu normal priority events with %.2fus work\n",
			   g_options.lo_events, g_options.work_ns / 1000.0);

	if (g_options.atomic_end)
		APPL_PRINT("Using atomic_processing_end()\n");

	if (g_options.eo_receive) {
		em_status_t stat = em_dispatch_register_enter_cb(entry_hook);

		test_fatal_if(stat != EM_OK, "entry_hook() register failed!");
		stat = em_dispatch_register_exit_cb(exit_hook);
		test_fatal_if(stat != EM_OK, "exit_hook() register failed!");
		APPL_PRINT("entry/exit hooks registered (expect a bit more latency)\n");
	}

	/* Start EOs */
	em_status_t start_ret = EM_ERR, ret;

	perf_shm->eo_ctx.test_q = q;
	perf_shm->eo_ctx.loprio_q = q2;
	perf_shm->eo_ctx.stopping = false;

	ret = em_eo_start_sync(perf_shm->eo, &start_ret, NULL);
	test_fatal_if(ret != EM_OK || start_ret != EM_OK,
		      "EO start:%" PRI_STAT " %" PRI_STAT "", ret, start_ret);
	APPL_PRINT("Starting %lu loops\n", g_options.loops);
}

void test_stop(const appl_conf_t *appl_conf)
{
	const int core = em_core_id();
	em_eo_t eo;
	em_status_t ret;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

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

	if (em_debug_timestamp(EM_DEBUG_TSP_SCHED_RETURN) == 0) /* could be disabled */
		APPL_PRINT("Dispatch timestamps (max_do) NOT available\n");

	for (uint64_t i = 0; i < g_options.lo_events; i++) {
		em_event_t ev = em_alloc(sizeof(test_msg), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

		test_fatal_if(ev == EM_EVENT_UNDEF, "Event alloc fail");
		test_msg *msg = em_event_pointer(ev);

		msg->count = 0;
		msg->work = g_options.work_ns;
		msg->magic = BACKGROUND_MAGIC;
		em_status_t ret = em_send(ev, ((app_eo_ctx *)eo_context)->loprio_q);

		test_fatal_if(ret != EM_OK, "Send fail");
	}
	APPL_PRINT("Sent %lu bg events\n", g_options.lo_events);

	perf_shm->times.mint = INT64_MAX;
	perf_shm->times.disp_min = INT64_MAX;

	em_event_t ev = em_alloc(sizeof(test_msg), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

	test_fatal_if(ev == EM_EVENT_UNDEF, "Event alloc fail");
	test_msg *msg = em_event_pointer(ev);

	msg->count = 0;
	msg->magic = TIMING_MAGIC;
	msg->ts = odp_time_global_ns();
	return em_send(ev, ((app_eo_ctx *)eo_context)->test_q);
}

em_status_t stop(void *eo_context, em_eo_t eo)
{
	(void)eo_context;

	APPL_PRINT("EO %" PRI_EO " stopping\n", eo);
	return EM_OK;
}

void receive_func(void *eo_context, em_event_t event, em_event_type_t type,
		  em_queue_t queue, void *q_context)
{
	uint64_t ts_ns = odp_time_global_strict_ns(); /* first thing to do*/
	app_eo_ctx *ctx = (app_eo_ctx *)eo_context;
	test_msg *msg = em_event_pointer(event);
	em_status_t ret;

	(void)type;
	(void)q_context;

	/* shutdown? */
	if (unlikely(ctx->stopping)) {
		em_free(event);
		return;
	}

	/* background work? */
	if (unlikely(queue == ctx->loprio_q)) {
		do_work(msg);
		perf_shm->num_lo++; /* atomic_uint */
		ret = em_send(event, ctx->loprio_q);
		test_fatal_if(ret != EM_OK, "Event send fail, ret=%u, #=%lu",
			      (unsigned int)ret, msg->count);
		return;
	}

	test_fatal_if(msg->magic != TIMING_MAGIC, "Unexpected event, magic fail (%x/%x)",
		      msg->magic, TIMING_MAGIC);
	test_fatal_if(queue != ctx->test_q, "Timing event from wrong Q??");

	/* timing event, maintain min/max/avg latency */
	int64_t diff = ts_ns - msg->ts - perf_shm->ts_overhead;

	if (unlikely(msg->count < START_EVENTS)) { /* ignore first ones */
		perf_shm->start_time = odp_time_global_ns();
		perf_shm->loop_start_time = perf_shm->start_time;
	} else {
		update_stats(ts_ns, diff, msg->count);

		if (g_options.atomic_end)
			em_atomic_processing_end();

		/* reporting period */
		if (unlikely(!(msg->count % REPORT_PERIOD) && msg->count)) {
			print_stats(perf_shm->start_time, perf_shm->loop_start_time, msg->count);
			perf_shm->loopcount++;
			if (perf_shm->loopcount >= g_options.loops && g_options.loops) {
				ctx->stopping = true;
				em_free(event);
				raise(SIGINT);
				return;
			}
			perf_shm->loop_start_time = odp_time_global_ns();
		}
	}

	msg->count++;
	msg->ts = odp_time_global_strict_ns();
	ret = em_send(event, ctx->test_q);
	test_fatal_if(ret != EM_OK, "Event send fail");
}

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
