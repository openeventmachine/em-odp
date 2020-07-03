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
 * Event Machine timer test for periodic timeouts.
 *
 * see instructions - string at timer_test_periodic.h.
 *
 * Exception/error management is simplified and aborts on any error.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>

#include <event_machine.h>
#include <event_machine/add-ons/event_machine_timer.h>
#include <event_machine/platform/env/environment.h>
#include <odp.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

#include "timer_test_periodic.h"

#define VERSION "WIP v0.2"
struct {
	int num_periodic;
	uint64_t res_ns;
	uint64_t period_ns;
	uint64_t max_period_ns;
	uint64_t min_period_ns;
	uint64_t min_work_ns;
	uint64_t max_work_ns;
	unsigned int work_prop;
	int clock_src;
	const char *csv;
	int num_runs;
	int tracebuf;
	int stoplim;
	int noskip;
	int profile;
	int dispatch;
	int jobs;
	long cpucycles;
	int bg_events;
	uint64_t bg_time_ns;
	int bg_size;
	int bg_chunk;

} g_options = { .num_periodic =  1,	/* defaults for basic check */
		.res_ns =        10000000ULL,
		.period_ns =     100000000ULL,
		.max_period_ns = 0,
		.min_period_ns = 0,
		.min_work_ns = 0,
		.max_work_ns = 0,
		.work_prop = 0,
		.clock_src =     EM_TIMER_CLKSRC_DEFAULT,
		.csv =		 NULL,
		.num_runs =	 1,
		.tracebuf =	 DEF_TMO_DATA,
		.stoplim =	 ((STOP_THRESHOLD * DEF_TMO_DATA) / 100),
		.noskip =	 0,
		.profile =	 0,
		.dispatch =	 0,
		.jobs =          0,
		.cpucycles =	 0,
		.bg_events =     0,
		.bg_time_ns =	 2000,
		.bg_size =       1000 * 1024,
		.bg_chunk =      20 * 1024};

typedef struct app_eo_ctx_t {
	e_state state;
	em_tmo_t heartbeat_tmo;
	em_timer_t test_tmr;
	em_queue_t hb_q;
	em_queue_t test_q;
	em_queue_t bg_q;
	int cooloff;
	int last_hbcount;
	uint64_t hb_hz;
	uint64_t test_hz;
	uint64_t time_hz;
	uint64_t meas_test_hz;
	uint64_t meas_time_hz;
	uint64_t linux_hz;
	uint64_t max_period;
	time_stamp started;
	time_stamp stopped;
	void *bg_data;
	tmo_setup *tmo_data;
	core_data cdat[MAX_CORES];
} app_eo_ctx_t;

typedef struct timer_app_shm_t {
	em_pool_t pool;
	app_eo_ctx_t eo_context;
	em_timer_t hb_tmr;
	em_timer_t test_tmr;
} timer_app_shm_t;

#if defined(__aarch64__)
static inline uint64_t get_cpu_cycle(void)
{
	uint64_t r;

	__asm__ volatile ("mrs %0, pmccntr_el0" : "=r"(r) :: "memory");
	return r;
}
#elif defined(__x86_64__)
static inline uint64_t get_cpu_cycle(void)
{
	uint32_t a, d;

	__asm__ volatile ("rdtsc" : "=a"(a), "=d"(d) :: "memory");
	return (uint64_t)a | ((uint64_t)d) << 32;
}
#else
#error "Code supports Aarch64 or x86_64"
#endif

/* EM-thread locals */
static __thread timer_app_shm_t *m_shm;

static void start_periodic(app_eo_ctx_t *eo_context);
static int handle_periodic(app_eo_ctx_t *eo_context, em_event_t event);
static void send_stop(app_eo_ctx_t *eo_context);
static void handle_heartbeat(app_eo_ctx_t *eo_context, em_event_t event);
static void usage(void);
static int parse_my_args(int first, int argc, char *argv[]);
static void analyze(app_eo_ctx_t *eo_ctx);
static void write_trace(app_eo_ctx_t *eo_ctx, const char *name);
static void cleanup(app_eo_ctx_t *eo_ctx);
static int add_trace(app_eo_ctx_t *eo_ctx, int id, e_op op, uint64_t ns,
		     int count);
static uint64_t linux_time_ns(void);
static em_status_t app_eo_start(void *eo_context, em_eo_t eo,
				const em_eo_conf_t *conf);
static em_status_t app_eo_start_local(void *eo_context, em_eo_t eo);
static em_status_t app_eo_stop(void *eo_context, em_eo_t eo);
static em_status_t app_eo_stop_local(void *eo_context, em_eo_t eo);
static void app_eo_receive(void *eo_context, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_context);
static time_stamp get_time(void);
static uint64_t time_to_ns(time_stamp t);
static time_stamp time_diff(time_stamp t2, time_stamp t1);
static time_stamp time_sum(time_stamp t1, time_stamp t2);
static void profile_statistics(e_op op, int cores, app_eo_ctx_t *eo_ctx);
static void profile_all_stats(int cores, app_eo_ctx_t *eo_ctx);
static void analyze_measure(app_eo_ctx_t *eo_ctx, uint64_t linuxns,
			    uint64_t tmrtick, time_stamp timetick);
static void timing_statistics(app_eo_ctx_t *eo_ctx);
static void add_prof(app_eo_ctx_t *eo_ctx, time_stamp t1,
		     e_op op, app_msg_t *msg);
static int do_one_tmo(int id, app_eo_ctx_t *eo_ctx,
		      time_stamp *min, time_stamp *max, time_stamp *first);
static tmo_trace *find_tmo(app_eo_ctx_t *eo_ctx, int id, int count);
static uint64_t random_tmo(void);
static uint64_t random_work_ns(rnd_state_t *rng);
static void enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
		     em_queue_t *queue, void **q_ctx);
static void exit_cb(em_eo_t eo);
static void send_bg_events(app_eo_ctx_t *eo_ctx);
static int do_bg_work(em_event_t evt, app_eo_ctx_t *eo_ctx);
static em_status_t my_error_handler(em_eo_t eo, em_status_t error,
				    em_escope_t escope, va_list args);

/* --------------------------------------- */
em_status_t my_error_handler(em_eo_t eo, em_status_t error,
			     em_escope_t escope, va_list args)
{
	/* todo improve printout */
	if (escope == 0xDEAD) { /* test_fatal_if */
		char *file = va_arg(args, char*);
		const char *func = va_arg(args, const char*);
		const int line = va_arg(args, const int);
		const char *format = va_arg(args, const char*);
		const char *base = basename(file);

		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wformat-nonliteral"
			fprintf(stderr, "FATAL - %s:%d, %s():\n",
				base, line, func);
			vfprintf(stderr, format, args);
		#pragma GCC diagnostic pop
	}
	return test_error_handler(eo, error, escope, args);
}

void enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
	      em_queue_t *queue, void **q_ctx)
{
	static int count;
	app_eo_ctx_t *const my_eo_ctx = *eo_ctx;

	(void)eo;
	(void)queue;
	(void)q_ctx;

	if (!my_eo_ctx)
		return;

	if (g_options.dispatch) {
		for (int i = 0; i < num; i++) {
			app_msg_t *msg = em_event_pointer(events[i]);

			add_trace(my_eo_ctx, msg->id, OP_PROF_ENTER_CB,
				  0, count++);
		}
	}

	my_eo_ctx->cdat[em_core_id()].enter = get_time();
}

void exit_cb(em_eo_t eo)
{
	static int count;
	app_eo_ctx_t *const my_eo_ctx = em_eo_get_context(eo);

	if (!my_eo_ctx)
		return;

	if (g_options.dispatch)
		add_trace(my_eo_ctx, -1, OP_PROF_EXIT_CB, 0, count++);

	core_data *cdat = &my_eo_ctx->cdat[em_core_id()];
	time_stamp took;

	if (__atomic_load_n(&my_eo_ctx->state, __ATOMIC_ACQUIRE) == STATE_RUN) {
		took = time_diff(get_time(), cdat->enter);
		cdat->acc_time = time_sum(cdat->acc_time, took);
	}
}

inline time_stamp get_time(void)
{
	time_stamp t;

	if (g_options.cpucycles)
		t.u64 = get_cpu_cycle();
	else
		t.odp = odp_time_global();
	return t;
}

uint64_t time_to_ns(time_stamp t)
{
	double ns;

	if (g_options.cpucycles) {
		double hz = (double)m_shm->eo_context.time_hz;

		ns = (1000000000.0 / hz) * (double)t.u64;
	} else {
		ns = (double)odp_time_to_ns(t.odp);
	}
	return round(ns);
}

time_stamp time_diff(time_stamp t2, time_stamp t1)
{
	time_stamp t;

	if (g_options.cpucycles)
		t.u64 = t2.u64 - t1.u64;
	else
		t.odp = odp_time_diff(t2.odp, t1.odp);

	return t;
}

time_stamp time_sum(time_stamp t1, time_stamp t2)
{
	time_stamp t;

	if (g_options.cpucycles)
		t.u64 = t1.u64 + t2.u64;
	else
		t.odp = odp_time_sum(t1.odp, t2.odp);
	return t;
}

uint64_t linux_time_ns(void)
{
	struct timespec ts;
	uint64_t ns;

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	ns = ts.tv_nsec + (ts.tv_sec * 1000000000ULL);
	return ns;
}

void send_stop(app_eo_ctx_t *eo_ctx)
{
	em_status_t ret;
	em_event_t event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW,
				    m_shm->pool);

	test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate stop event!\n");

	app_msg_t *msg = em_event_pointer(event);

	msg->command = CMD_DONE;
	msg->id = em_core_id();

	ret = em_send(event, eo_ctx->hb_q);
	test_fatal_if(ret != EM_OK, "em_send():%" PRI_STAT, ret);
}

void cleanup(app_eo_ctx_t *eo_ctx)
{
	time_stamp tz = {0};
	int cores = em_core_count();

	for (int i = 0; i < cores; i++) {
		eo_ctx->cdat[i].count = 0;
		eo_ctx->cdat[i].cancelled = 0;
		eo_ctx->cdat[i].jobs_deleted = 0;
		eo_ctx->cdat[i].jobs = 0;
		eo_ctx->cdat[i].acc_time = tz;
	}

	/* TODO the rest? */
}

void write_trace(app_eo_ctx_t *eo_ctx, const char *name)
{
	int cores = em_core_count();
	FILE *fle = stdout;

	if (strcmp(name, "stdout"))
		fle = fopen(g_options.csv, "w");
	if (fle == NULL) {
		APPL_PRINT("FAILED to open trace file\n");
		return;
	}

	fprintf(fle, "\n\n#BEGIN TRACE FORMAT 1\n");
	fprintf(fle, "resol,period,max_period,clksrc,num_tmo,loops,");
	fprintf(fle, "traces,noskip,SW-ver\n");
	fprintf(fle, "%lu,%lu,%lu,%d,%d,%d,%d,%d,%s\n",
		g_options.res_ns,
		g_options.period_ns,
		g_options.max_period_ns,
		g_options.clock_src,
		g_options.num_periodic,
		g_options.num_runs,
		g_options.tracebuf,
		g_options.noskip,
		VERSION);
	fprintf(fle, "time_hz,meas_time_hz,timer_hz,meas_timer_hz,linux_hz\n");
	fprintf(fle, "%lu,%lu,%lu,%lu,%lu\n",
		eo_ctx->time_hz,
		eo_ctx->meas_time_hz,
		eo_ctx->test_hz,
		eo_ctx->meas_test_hz,
		eo_ctx->linux_hz);

	fprintf(fle, "tmo_id,period_ns,period_ticks,ack_late");
	fprintf(fle, ",start_tick,start_ns\n");
	for (int i = 0; i < g_options.num_periodic; i++) {
		if (USE_TIMER_STAT)
			fprintf(fle, "%d,%lu,%lu,%lu,%lu,%lu\n",
				i, eo_ctx->tmo_data[i].period_ns,
				eo_ctx->tmo_data[i].ticks,
				eo_ctx->tmo_data[i].ack_late,
				eo_ctx->tmo_data[i].start,
				time_to_ns(eo_ctx->tmo_data[i].start_ts));
		else
			fprintf(fle, "%d,%lu,%lu,%s,%lu,%lu\n",
				i, eo_ctx->tmo_data[i].period_ns,
				eo_ctx->tmo_data[i].ticks,
				"",
				eo_ctx->tmo_data[i].start,
				time_to_ns(eo_ctx->tmo_data[i].start_ts));
	}

	fprintf(fle, "id,op,tick,time_ns,linux_time_ns,counter,core\n");
	for (int c = 0; c < cores; c++) {
		for (int i = 0; i < eo_ctx->cdat[c].count; i++) {
			uint64_t ns;

			if (eo_ctx->cdat[c].trc[i].op >= OP_PROF_ACK) {
				/* it's tick diff */
				ns = time_to_ns(eo_ctx->cdat[c].trc[i].linuxt);
			} else { /* it's ns from linux */
				ns = eo_ctx->cdat[c].trc[i].linuxt.u64;
			}

			fprintf(fle, "%d,%s,%lu,%lu,%lu,%d,%d\n",
				eo_ctx->cdat[c].trc[i].id,
				op_labels[eo_ctx->cdat[c].trc[i].op],
				eo_ctx->cdat[c].trc[i].tick,
				time_to_ns(eo_ctx->cdat[c].trc[i].ts),
				ns,
				eo_ctx->cdat[c].trc[i].count,
				c);
		}
	}
	fprintf(fle, "#END TRACE\n\n");
	if (fle != stdout)
		fclose(fle);
}

uint64_t random_tmo(void)
{
	uint64_t r;

	r = random() % (g_options.max_period_ns - g_options.min_period_ns + 1);
	return r + g_options.min_period_ns;
}

uint64_t random_work_ns(rnd_state_t *rng)
{
	uint64_t r;
	int32_t r1;

	random_r(&rng->rdata, &r1);
	r = (uint64_t)r1;
	if (r % 100 >= g_options.work_prop) /* propability of work roughly */
		return 0;

	random_r(&rng->rdata, &r1);
	r = (uint64_t)r1 % (g_options.max_work_ns - g_options.min_work_ns + 1);
	return r + g_options.min_work_ns;
}

tmo_trace *find_tmo(app_eo_ctx_t *eo_ctx, int id, int count)
{
	int cores = em_core_count();

	for (int c = 0; c < cores; c++) {
		for (int i = 0; i < g_options.tracebuf; i++) { /* find id */
			if (eo_ctx->cdat[c].trc[i].op == OP_TMO &&
			    eo_ctx->cdat[c].trc[i].id == id &&
			    eo_ctx->cdat[c].trc[i].count == count) {
				return &eo_ctx->cdat[c].trc[i];
			}
		}
	}
	return NULL;
}

int do_one_tmo(int id, app_eo_ctx_t *eo_ctx,
	       time_stamp *min, time_stamp *max, time_stamp *first)
{
	int num = 0;
	time_stamp diff;
	time_stamp prev = {0};
	int last = 0;

	max->u64 = 0;
	min->u64 = UINT64_MAX;

	/* find in sequential order for diff */
	for (int count = 1; count < g_options.tracebuf; count++) {
		tmo_trace *tmo = find_tmo(eo_ctx, id, count);

		if (!tmo) {
			if (last != count - 1)
				APPL_PRINT("MISSING TMO: id %d, count %d\n",
					   id, count);
			return num;
		}
		last++;
		if (!num) { /* skip first for min/max */
			diff = time_diff(tmo->ts,
					 eo_ctx->tmo_data[id].start_ts);
			*first = diff;
			prev = tmo->ts;
			num++;
			continue;
		}
		diff = time_diff(tmo->ts, prev);
		if (diff.u64 > max->u64)
			*max = diff;
		if (diff.u64 < min->u64)
			*min = diff;
		prev = tmo->ts;
		num++;
	}
	return num;
}

void timing_statistics(app_eo_ctx_t *eo_ctx)
{
	time_stamp max_ts = {0}, min_ts = {0}, first_ts = {0};
	const int cores = em_core_count();
	uint64_t system_used = time_to_ns(time_diff(eo_ctx->stopped,
						    eo_ctx->started));

	for (int c = 0; c < cores; c++) {
		core_data *cdat = &eo_ctx->cdat[c];
		uint64_t eo_used = time_to_ns(cdat->acc_time);
		double perc = (double)eo_used / (double)system_used * 100;

		APPL_PRINT("STAT_CORE [%d]: %d tmos, %d jobs, EO used %.1f%% CPU time\n",
			   c, cdat->count, cdat->jobs, perc);
	}

	for (int id = 0; id < g_options.num_periodic; id++) { /* each timeout */
		tmo_setup *tmo_data = &eo_ctx->tmo_data[id];
		int num = do_one_tmo(id, eo_ctx, &min_ts, &max_ts, &first_ts);

		APPL_PRINT("STAT-TMO [%d]: %d tmos, period %luns (",
			   id, num, tmo_data->period_ns);
		if (num > 1)
			APPL_PRINT("%lu ticks), interval %ldns ... %ldns\n",
				   tmo_data->ticks,
				   (int64_t)time_to_ns(min_ts) - (int64_t)tmo_data->period_ns,
				   (int64_t)time_to_ns(max_ts) - (int64_t)tmo_data->period_ns);
		else
			APPL_PRINT("%lu ticks), 1st period %lu\n",
				   tmo_data->ticks, time_to_ns(first_ts));
		if (num == 0)
			APPL_PRINT("	ERROR - no timeouts received\n");
	}

	if (!g_options.dispatch)
		return;

	/*
	 * g_options.dispatch set
	 *
	 * Calculate EO rcv min-max-avg:
	 */
	uint64_t min = UINT64_MAX, max = 0, avg = 0;
	time_stamp prev_ts = { 0 };
	int prev_count = 0;
	int num = 0;

	for (int c = 0; c < cores; c++) {
		for (int i = 0; i < g_options.tracebuf; i++) {
			core_data *cdat = &eo_ctx->cdat[c];

			if (cdat->trc[i].op == OP_PROF_ENTER_CB) {
				prev_ts = cdat->trc[i].ts;
				prev_count = cdat->trc[i].count;
			} else if (cdat->trc[i].op == OP_PROF_EXIT_CB) {
				time_stamp diff_ts;
				uint64_t ns;

				if (prev_count != cdat->trc[i].count)
					APPL_PRINT("No enter cnt=%d\n",
						   prev_count);

				diff_ts = time_diff(cdat->trc[i].ts, prev_ts);
				ns = time_to_ns(diff_ts);

				if (ns < min)
					min = ns;
				if (ns > max)
					max = ns;

				avg += ns;
				num++;
			}
		}
	}

	APPL_PRINT("%d dispatcher enter-exits\n", num);
	APPL_PRINT("PROF-DISPATCH rcv time: min %luns, max %luns, avg %luns\n",
		   min, max, num > 0 ? avg / num : 0);
}

void profile_statistics(e_op op, int cores, app_eo_ctx_t *eo_ctx)
{
	uint64_t min = UINT64_MAX;
	uint64_t max = 0, avg = 0, num = 0;
	uint64_t t;

	for (int c = 0; c < cores; c++) {
		for (int i = 0; i < g_options.tracebuf; i++) {
			if (eo_ctx->cdat[c].trc[i].op == op) {
				t = time_to_ns(eo_ctx->cdat[c].trc[i].linuxt);
				if (min > t)
					min = t;
				if (max < t)
					max = t;
				avg += t;
				num++;
			}
		}
	}
	if (num)
		APPL_PRINT("%s: %lu samples: min %luns, max %luns, avg %luns\n",
			   op_labels[op], num, min, max, avg / num);
}

void profile_all_stats(int cores, app_eo_ctx_t *eo_ctx)
{
	APPL_PRINT("API profile statistics:\n");
	profile_statistics(OP_PROF_CREATE, cores, eo_ctx);
	profile_statistics(OP_PROF_SET, cores, eo_ctx);
	profile_statistics(OP_PROF_ACK, cores, eo_ctx);
	profile_statistics(OP_PROF_DELETE, cores, eo_ctx);
}

void analyze(app_eo_ctx_t *eo_ctx)
{
	int cores = em_core_count();
	int cancelled = 0;
	int job_del = 0;

	/* checks TODO */

	timing_statistics(eo_ctx);

	if (g_options.profile)
		profile_all_stats(cores, eo_ctx);

	for (int c = 0; c < cores; c++) {
		cancelled += eo_ctx->cdat[c].cancelled;
		job_del += eo_ctx->cdat[c].jobs_deleted;
	}

	/* write trace file */
	if (g_options.csv != NULL)
		write_trace(eo_ctx, g_options.csv);

	APPL_PRINT("%d/%d timeouts were cancelled\n",
		   cancelled, g_options.num_periodic);
	if (g_options.bg_events)
		APPL_PRINT("%d/%d bg jobs were deleted\n",
			   job_del, g_options.bg_events);
}

int add_trace(app_eo_ctx_t *eo_ctx, int id, e_op op, uint64_t ns, int count)
{
	int core = em_core_id();
	tmo_trace *tmo = &eo_ctx->cdat[core].trc[eo_ctx->cdat[core].count];

	if (eo_ctx->cdat[core].count < g_options.tracebuf) {
		if (op < OP_PROF_ACK) /* to be a bit faster for profiling */
			tmo->tick = em_timer_current_tick(eo_ctx->test_tmr);
		tmo->op = op;
		tmo->id = id;
		tmo->ts = get_time();
		tmo->linuxt.u64 = ns;
		tmo->count = count;
		eo_ctx->cdat[core].count++;
	}
	if (eo_ctx->cdat[core].count >= g_options.stoplim)
		return 0;
	else
		return 1;
}

void send_bg_events(app_eo_ctx_t *eo_ctx)
{
	for (int n = 0; n < g_options.bg_events; n++) {
		em_event_t event = em_alloc(sizeof(app_msg_t),
					    EM_EVENT_TYPE_SW, m_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF,
			      "Can't allocate bg event!\n");
		app_msg_t *msg = em_event_pointer(event);

		msg->command = CMD_BGWORK;
		msg->count = 0;
		msg->id = -1;
		msg->arg = g_options.bg_time_ns;
		test_fatal_if(em_send(event, eo_ctx->bg_q) != EM_OK,
			      "Can't allocate bg event!\n");
	}
}

void start_periodic(app_eo_ctx_t *eo_ctx)
{
	app_msg_t *msg;
	em_event_t event;
	em_tmo_t tmo;
	uint64_t max_period = 0;
	em_tmo_flag_t flag = EM_TMO_FLAG_PERIODIC;
	time_stamp t1 = {0};

	if (g_options.noskip)
		flag |= EM_TMO_FLAG_NOSKIP;
	eo_ctx->started = get_time();

	for (int i = 0; i < g_options.num_periodic; i++) {
		event = em_alloc(sizeof(app_msg_t),
				 EM_EVENT_TYPE_SW, m_shm->pool);
		test_fatal_if(event == EM_EVENT_UNDEF,
			      "Can't allocate test event (%ldB)!\n",
			      sizeof(app_msg_t));

		msg = em_event_pointer(event);
		msg->command = CMD_TMO;
		msg->count = 0;
		msg->id = i;
		if (g_options.profile)
			t1 = get_time();
		tmo = em_tmo_create(m_shm->test_tmr, flag, eo_ctx->test_q);
		if (g_options.profile)
			add_prof(eo_ctx, t1, OP_PROF_CREATE, msg);

		test_fatal_if(tmo == EM_TMO_UNDEF,
			      "Can't allocate test_tmo!\n");
		msg->tmo = tmo;

		double ns = 1000000000 / (double)eo_ctx->test_hz;
		uint64_t period;

		if (g_options.period_ns) {
			period = round((double)g_options.period_ns / ns);
			if (max_period < g_options.period_ns)
				max_period = g_options.period_ns;
			eo_ctx->tmo_data[i].period_ns = g_options.period_ns;
		} else { /* use random */
			eo_ctx->tmo_data[i].period_ns = random_tmo();
			period = round((double)eo_ctx->tmo_data[i].period_ns /
				       ns);
			if (max_period < eo_ctx->tmo_data[i].period_ns)
				max_period = eo_ctx->tmo_data[i].period_ns;
		}
		if (EXTRA_PRINTS && i == 0) {
			APPL_PRINT("Timer Hz %lu ", eo_ctx->test_hz);
			APPL_PRINT("= Period ns: %f => period %lu ticks\n",
				   ns, period);
		}

		test_fatal_if(period < 1, "timer resolution is too low!\n");

		eo_ctx->tmo_data[i].start_ts = get_time();
		/* replace with abs tick when EM API has start time */
		eo_ctx->tmo_data[i].start =
			em_timer_current_tick(m_shm->test_tmr);
		if (g_options.profile)
			t1 = get_time();
		em_status_t stat = em_tmo_set_rel(tmo, period, event);

		if (g_options.profile)
			add_prof(eo_ctx, t1, OP_PROF_SET, msg);

		test_fatal_if(stat != EM_OK, "Can't activate test tmo!\n");

		eo_ctx->tmo_data[i].ack_late = 0;
		eo_ctx->tmo_data[i].ticks = period;
		eo_ctx->max_period = max_period;
		eo_ctx->cooloff = max_period / 1000000000ULL * 2;
		if (eo_ctx->cooloff < 4)
			eo_ctx->cooloff = 4; /* HB periods (sec) */
	}
}

void add_prof(app_eo_ctx_t *eo_ctx, time_stamp t1, e_op op, app_msg_t *msg)
{
	time_stamp t2, dif;

	t2 = get_time();
	dif = time_diff(t2, t1);
	add_trace(eo_ctx, msg->id, op, dif.u64, msg->count);
	/* if this filled the buffer it's handled on next tmo */
}

int handle_periodic(app_eo_ctx_t *eo_ctx, em_event_t event)
{
	int core = em_core_id();
	app_msg_t *msg = (app_msg_t *)em_event_pointer(event);
	int reuse = 1;
	e_state state = __atomic_load_n(&eo_ctx->state, __ATOMIC_ACQUIRE);
	time_stamp t1 = {0};

	msg->count++;
	if (likely(state == STATE_RUN)) {
		if (!add_trace(eo_ctx, msg->id, OP_TMO, 0, msg->count))
			send_stop(eo_ctx);

		if (g_options.work_prop) {
			uint64_t work = random_work_ns(&eo_ctx->cdat[core].rng);

			if (work) {
				time_stamp now, t2;
				uint64_t ns;

				now = get_time();
				ns = time_to_ns(now);
				do {
					t2 = get_time();
				} while (time_to_ns(t2) < (ns + work));
				add_trace(eo_ctx, msg->id, OP_WORK,
					  work, msg->count);
			}
		}
	} else if (state == STATE_COOLOFF) { /* trace, but cancel */
		em_event_t tmo_event = EM_EVENT_UNDEF;

		add_trace(eo_ctx, msg->id, OP_TMO,
			  0, msg->count);

		#if (USE_TIMER_STAT)
		const em_timer_tmo_stat_t *stat = em_tmo_get_pstat(msg->tmo);

		APPL_PRINT("STAT-ACK [%d]:  %lu acks, %lu late, %lu err\n",
			   msg->id, stat->acks, stat->late, stat->error);
		eo_ctx->tmo_data[msg->id].ack_late = stat->late;
		#endif
		if (g_options.profile)
			t1 = get_time();
		em_tmo_delete(msg->tmo, &tmo_event);
		if (g_options.profile)
			add_prof(eo_ctx, t1, OP_PROF_DELETE, msg);

		eo_ctx->cdat[core].cancelled++;
		if (tmo_event != EM_EVENT_UNDEF)
			em_free(tmo_event);

		add_trace(eo_ctx, msg->id, OP_CANCEL,
			  0, msg->count);
		return 1;
	}

	add_trace(eo_ctx, msg->id, OP_ACK, 0, msg->count);

	if (g_options.profile)
		t1 = get_time();
	if (em_tmo_ack(msg->tmo, event) != EM_OK)
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "ack() fail!\n");
	if (g_options.profile)
		add_prof(eo_ctx, t1, OP_PROF_ACK, msg);

	return reuse;
}

void analyze_measure(app_eo_ctx_t *eo_ctx, uint64_t linuxns, uint64_t tmrtick,
		     time_stamp timetick)
{
	uint64_t linux_t2 = linux_time_ns();
	time_stamp time_t2 = get_time();
	uint64_t tmr_t2 = em_timer_current_tick(eo_ctx->test_tmr);

	linux_t2 = linux_t2 - linuxns;
	time_t2 = time_diff(time_t2, timetick);
	tmr_t2 = tmr_t2 - tmrtick;
	APPL_PRINT("%lu timer ticks in %luns (linux time) ", tmr_t2, linux_t2);
	double hz = 1000000000 /
		    ((double)linux_t2 / (double)tmr_t2);
	APPL_PRINT("=> %.1fHz (%.1fMHz). Timer reports %luHz\n",
		   hz, hz / 1000000, eo_ctx->test_hz);
	eo_ctx->meas_test_hz = round(hz);
	hz = 1000000000 / ((double)linux_t2 / (double)time_t2.u64);
	APPL_PRINT("Timestamp measured: %.1fHz (%.1fMHz)\n",
		   hz, hz / 1000000);
	eo_ctx->meas_time_hz = round(hz);

	if (g_options.cpucycles == 1) /* use measured */
		eo_ctx->time_hz = eo_ctx->meas_time_hz;
	if (g_options.cpucycles > 1) /* freq given */
		eo_ctx->time_hz = (uint64_t)g_options.cpucycles;

	test_fatal_if(tmr_t2 < 1, "TIMER SEEMS NOT RUNNING AT ALL!?");
}

int do_bg_work(em_event_t evt, app_eo_ctx_t *eo_ctx)
{
	app_msg_t *msg = (app_msg_t *)em_event_pointer(evt);
	time_stamp t1 = get_time();
	time_stamp ts;
	int32_t rnd;
	int core = em_core_id();
	uint64_t sum = 0;

	if (__atomic_load_n(&eo_ctx->state, __ATOMIC_ACQUIRE) != STATE_RUN) {
		eo_ctx->cdat[core].jobs_deleted++;
		if (EXTRA_PRINTS)
			APPL_PRINT("Deleting job after %u iterations\n",
				   msg->count);
		return 0; /* stop & delete */
	}

	if (g_options.jobs)
		add_trace(eo_ctx, -1, OP_BGWORK, msg->arg, msg->count);

	msg->count++;
	eo_ctx->cdat[core].jobs++;
	int blocks = g_options.bg_size / g_options.bg_chunk;

	random_r(&eo_ctx->cdat[core].rng.rdata, &rnd);
	rnd = rnd % blocks;
	uint64_t *dptr = (uint64_t *)((uintptr_t)eo_ctx->bg_data +
				      rnd * g_options.bg_chunk);
	/* printf("%d: %p - %p\n", rnd, eo_ctx->bg_data, dptr); */

	do {
		/* jump around reading from selected chunk */
		random_r(&eo_ctx->cdat[core].rng.rdata, &rnd);
		rnd = rnd % (g_options.bg_chunk / sizeof(uint64_t));
		/* printf("%d: %p - %p\n", rnd, eo_ctx->bg_data, dptr+rnd); */
		sum += *(dptr + rnd);
		ts = time_diff(get_time(), t1);
	} while (time_to_ns(ts) < msg->arg);

	*dptr = sum;
	test_fatal_if(em_send(evt, eo_ctx->bg_q) != EM_OK,
		      "Failed to send BG job event!");
	return 1;
}

void handle_heartbeat(app_eo_ctx_t *eo_ctx, em_event_t event)
{
	app_msg_t *msg = (app_msg_t *)em_event_pointer(event);
	int cores = em_core_count();
	int done = 0;
	e_state state = __atomic_load_n(&eo_ctx->state, __ATOMIC_SEQ_CST);
	static int runs;
	static uint64_t linuxns;
	static uint64_t tmrtick;
	static time_stamp timetick;

	/* heartbeat runs states of the test */

	msg->count++;
	add_trace(eo_ctx, -1, OP_HB,
		  linux_time_ns(), msg->count);

	if (EXTRA_PRINTS)
		APPL_PRINT(".");

	switch (state) {
	case STATE_INIT:
		if (msg->count > eo_ctx->last_hbcount + INIT_WAIT) {
			__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
			eo_ctx->last_hbcount = msg->count;
			APPL_PRINT("Starting tick measurement\n");
		}
		break;

	case STATE_MEASURE:	/* measure frequencies */
		if (linuxns == 0) {
			linuxns = linux_time_ns();
			timetick = get_time();
			tmrtick = em_timer_current_tick(eo_ctx->test_tmr);
		}
		if (msg->count > eo_ctx->last_hbcount + MEAS_PERIOD) {
			analyze_measure(eo_ctx, linuxns, tmrtick, timetick);
			linuxns = 0;
			/* start new run */
			if (g_options.num_runs > 1)
				APPL_PRINT("** Round %d\n", runs + 1);
			__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
			add_trace(eo_ctx, -1, OP_STATE,
				  linux_time_ns(), eo_ctx->state);
		}
		break;

	case STATE_STABILIZE:
		if (g_options.bg_events)
			send_bg_events(eo_ctx);
		__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
		add_trace(eo_ctx, -1, OP_STATE,
			  linux_time_ns(), eo_ctx->state);
		start_periodic(eo_ctx);
		eo_ctx->last_hbcount = msg->count;
		break;

	case STATE_RUN:
		for (int i = 0; i < cores; i++) {
			if (eo_ctx->cdat[i].count >=
			   g_options.tracebuf) {
				done++;
				break;
			}
		}
		if (done) {
			__atomic_fetch_add(&eo_ctx->state, 1,
					   __ATOMIC_SEQ_CST);
			add_trace(eo_ctx, -1, OP_STATE, linux_time_ns(),
				  eo_ctx->state);
			eo_ctx->last_hbcount = msg->count;
		}
		break;

	case STATE_COOLOFF:
		if (msg->count > (eo_ctx->last_hbcount + eo_ctx->cooloff)) {
			__atomic_fetch_add(&eo_ctx->state, 1,
					   __ATOMIC_SEQ_CST);
			add_trace(eo_ctx, -1, OP_STATE,
				  linux_time_ns(), eo_ctx->state);
			eo_ctx->last_hbcount = msg->count;
		}
		break;

	case STATE_ANALYZE:
		APPL_PRINT("\n");
		analyze(eo_ctx);
		cleanup(eo_ctx);
		__atomic_store_n(&eo_ctx->state, STATE_INIT,
				 __ATOMIC_SEQ_CST);
		runs++;
		if (runs >= g_options.num_runs && g_options.num_runs != 0) {
			/* terminate test app */
			APPL_PRINT("%d runs done\n", runs);
			raise(SIGINT);
		}
		eo_ctx->last_hbcount = msg->count;
		break;

	default:
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Invalid test state");
	}

	if (em_tmo_ack(eo_ctx->heartbeat_tmo, event) != EM_OK)
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "HB ack() fail!\n");
}

void usage(void)
{
	printf("%s\n", instructions);

	printf("Usage:\n");
	for (int i = 0; ; i++) {
		if (longopts[i].name == NULL || descopts[i] == NULL)
			break;
		printf("--%s or -%c: %s\n", longopts[i].name, longopts[i].val,
		       descopts[i]);
	}
}

int parse_my_args(int first, int argc, char *argv[])
{
	optind = first + 1; /* skip '--' */
	while (1) {
		int opt;
		int long_index;
		char *endptr;
		long num;

		opt = getopt_long(argc, argv, shortopts,
				  longopts, &long_index);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 's': {
			g_options.noskip = 1;
		}
		break;
		case 'a': {
			g_options.profile = 1;
		}
		break;
		case 'b': {
			g_options.jobs = 1;
		}
		break;
		case 'd': {
			g_options.dispatch = 1;
		}
		break;
		case 'g': {
			g_options.cpucycles = 1;
			if (optarg != NULL) {
				num = strtol(optarg, &endptr, 0);
				if (*endptr != '\0' || num < 2)
					return 0;
				g_options.cpucycles = num;
			}
		}
		break;
		case 'w': {
			g_options.csv = "stdout";
			if (optarg != NULL)
				g_options.csv = optarg;
		}
		break;
		case 'm': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.max_period_ns = num;
		}
		break;
		case 'l': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.min_period_ns = num;
		}
		break;
		case 't': {
			unsigned long size, perc;

			num = sscanf(optarg, "%lu,%lu", &size, &perc);
			if (num == 0 || size < 10 ||
			    sizeof(tmo_trace) * size > MAX_TMO_BYTES)
				return 0;
			g_options.tracebuf = size;
			if (num == 2 && perc > 100)
				return 0;
			if (num == 2)
				g_options.stoplim = ((perc * size) / 100);
			else
				g_options.stoplim =
					((STOP_THRESHOLD * size) / 100);
		}
		break;
		case 'e': {
			unsigned int min_us, max_us, prop;

			if (sscanf(optarg, "%u,%u,%u",
				   &min_us, &max_us, &prop) != 3)
				return 0;
			if (prop > 100 || max_us < 1)
				return 0;
			g_options.min_work_ns = 1000ULL * min_us;
			g_options.max_work_ns = 1000ULL * max_us;
			g_options.work_prop = prop;
		}
		break;
		case 'j': {
			unsigned int evts, us, kb, chunk;

			num = sscanf(optarg, "%u,%u,%u,%u",
				     &evts, &us, &kb, &chunk);
			if (num == 0 || evts < 1)
				return 0;
			g_options.bg_events = evts;
			if (num > 1 && us)
				g_options.bg_time_ns = us * 1000ULL;
			if (num > 2 && kb)
				g_options.bg_size = kb * 1024;
			if (num > 3 && chunk)
				g_options.bg_chunk = chunk * 1024;
			if (g_options.bg_chunk > g_options.bg_size)
				return 0;
		}
		break;
		case 'n': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.num_periodic = num;
		}
		break;
		case 'p': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.period_ns = num;
		}
		break;
		case 'c': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.clock_src = num;
		}
		break;
		case 'r': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.res_ns = num;
		}
		break;
		case 'x': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.num_runs = num;
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

/**
 * Before EM - Init of the test application.
 *
 * The shared memory is needed if EM instance runs on multiple processes.
 * Doing it like this makes it possible to run the app both as threads (-t)
 * as well as processes (-p).
 *
 * @attention Run on all cores.
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_init(void)
{
	int core = em_core_id();

	/* first core creates ShMem */
	if (core == 0) {
		m_shm = env_shared_reserve("Timer_test",
					   sizeof(timer_app_shm_t));
		/* initialize it */
		if (m_shm)
			memset(m_shm, 0, sizeof(timer_app_shm_t));

		APPL_PRINT("%ldk shared memory for app context\n",
			   sizeof(timer_app_shm_t) / 1000);

	} else {
		m_shm = env_shared_lookup("Timer_test");
	}

	if (m_shm == NULL) {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "ShMem init failed on EM-core: %u",
			   em_core_id());
	}

	APPL_PRINT("core %d: %s done\n", core, __func__);
}

/**
 * Startup of the timer test EM application.
 *
 * At this point EM is up, but no EOs exist. EM API can be used to create
 * queues, EOs etc.
 *
 * @attention Run only on one EM core.
 *
 * @param appl_conf Application configuration
 *
 * @see cm_setup() for setup and dispatch.
 */
void test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	em_timer_attr_t attr;
	em_queue_t queue;
	em_status_t stat;
	app_eo_ctx_t *eo_ctx;

	if (appl_conf->num_pools >= 1)
		m_shm->pool = appl_conf->pools[0];
	else
		m_shm->pool = EM_POOL_DEFAULT;

	if (g_options.max_period_ns < g_options.period_ns &&
	    g_options.max_period_ns)
		g_options.max_period_ns = g_options.period_ns;

	if (g_options.min_period_ns == 0)
		g_options.min_period_ns = DEF_MIN_PERIOD * g_options.res_ns;

	int64_t diff = (int64_t)g_options.max_period_ns -
		       (int64_t)g_options.min_period_ns;

	if (diff < 100000) {
		APPL_PRINT("NOTE: max-min timeout too small, max adjusted\n");
		g_options.max_period_ns = g_options.min_period_ns + 100000;
	}

	eo_ctx = &m_shm->eo_context;
	eo_ctx->tmo_data = calloc(g_options.num_periodic, sizeof(tmo_setup));
	test_fatal_if(eo_ctx->tmo_data == NULL, "Can't alloc tmo_setups");

	eo = em_eo_create(APP_EO_NAME, app_eo_start, app_eo_start_local,
			  app_eo_stop, app_eo_stop_local, app_eo_receive,
			  eo_ctx);
	test_fatal_if(eo == EM_EO_UNDEF, "Failed to create EO!");

	stat = em_register_error_handler(my_error_handler);
	test_fatal_if(stat != EM_OK, "Failed to register error handler");

	/* atomic queue for control */
	queue = em_queue_create("Control Q",
				EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create queue!");
	eo_ctx->hb_q = queue;

	/* another parallel high priority for timeout handling*/
	queue = em_queue_create("Tmo Q",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_HIGHEST,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create queue!");
	eo_ctx->test_q = queue;

	/* another parallel low priority for background work*/
	queue = em_queue_create("BG Q",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_LOWEST,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to add queue!");
	eo_ctx->bg_q = queue;

	/* create two timers so HB and tests can be independent */
	memset(&attr, 0, sizeof(em_timer_attr_t));
	strncpy(attr.name, "HBTimer", EM_TIMER_NAME_LEN);
	attr.resolution = 100ULL * 1000ULL * 1000ULL; /* 100ms */
	m_shm->hb_tmr = em_timer_create(&attr);
	test_fatal_if(m_shm->hb_tmr == EM_TIMER_UNDEF,
		      "Failed to create HB timer!");

	memset(&attr, 0, sizeof(em_timer_attr_t));
	strncpy(attr.name, "TestTimer", EM_TIMER_NAME_LEN);
	attr.num_tmo = g_options.num_periodic + 1;
	attr.resolution = g_options.res_ns;
	attr.clk_src = (em_timer_clksrc_t)g_options.clock_src;
	attr.max_tmo = g_options.max_period_ns;
	m_shm->test_tmr = em_timer_create(&attr);
	test_fatal_if(m_shm->test_tmr == EM_TIMER_UNDEF,
		      "Failed to create test timer!");
	eo_ctx->test_tmr = m_shm->test_tmr;

	/* Start EO */
	stat = em_eo_start_sync(eo, NULL, NULL);
	test_fatal_if(stat != EM_OK, "Failed to start EO!");
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_status_t ret;
	em_eo_t eo;

	(void)appl_conf;

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	eo = em_eo_find(APP_EO_NAME);
	test_fatal_if(eo == EM_EO_UNDEF,
		      "Could not find EO:%s", APP_EO_NAME);

	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK,
		      "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);

	ret = em_timer_delete(m_shm->hb_tmr);
	test_fatal_if(ret != EM_OK,
		      "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      m_shm->hb_tmr, ret);
	ret = em_timer_delete(m_shm->test_tmr);
	test_fatal_if(ret != EM_OK,
		      "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      m_shm->test_tmr, ret);

	free(m_shm->eo_context.tmo_data);
}

void
test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (m_shm != NULL) {
		env_shared_free(m_shm);
		m_shm = NULL;
		em_unregister_error_handler();
	}
}

static em_status_t app_eo_start(void *eo_context, em_eo_t eo,
				const em_eo_conf_t *conf)
{
	#define PRINT_MAX_TMRS 4
	em_timer_attr_t attr;
	em_timer_t tmr[PRINT_MAX_TMRS];
	int num_timers;
	app_msg_t *msg;
	struct timespec ts;
	uint64_t period;
	em_event_t event;
	app_eo_ctx_t *eo_ctx = (app_eo_ctx_t *)eo_context;

	(void)eo;
	(void)conf;

	APPL_PRINT("EO start\n");

	num_timers = em_timer_get_all(tmr, PRINT_MAX_TMRS);

	for (int i = 0;
	     i < (num_timers > PRINT_MAX_TMRS ? PRINT_MAX_TMRS : num_timers);
	     i++) {
		if (em_timer_get_attr(tmr[i], &attr) != EM_OK) {
			APPL_ERROR("Can't get timer info\n");
			return EM_ERR_BAD_ID;
		}
		APPL_PRINT("Timer \"%s\" info:\n", attr.name);
		APPL_PRINT("  -resolution: %" PRIu64 " ns\n", attr.resolution);
		APPL_PRINT("  -max_tmo: %" PRIu64 " ms\n", attr.max_tmo / 1000);
		APPL_PRINT("  -num_tmo: %d\n", attr.num_tmo);
		APPL_PRINT("  -clk_src: %d\n", attr.clk_src);
		APPL_PRINT("  -tick Hz: %" PRIu64 " hz\n",
			   em_timer_get_freq(tmr[i]));
	}

	if (g_options.max_period_ns == 0) {
		em_timer_get_attr(eo_ctx->test_tmr, &attr);
		g_options.max_period_ns = attr.max_tmo;
	}

	APPL_PRINT("\nActive run options:\n");
	APPL_PRINT(" num timers:   %d\n", g_options.num_periodic);
	APPL_PRINT(" resolution:   %luns (%fs)\n", g_options.res_ns,
		   (double)g_options.res_ns / 1000000000);
	APPL_PRINT(" period:       %luns (%fs%s)\n", g_options.period_ns,
		   (double)g_options.period_ns / 1000000000,
		   g_options.period_ns == 0 ? " (random)" : "");
	APPL_PRINT(" max period:   %luns (%fs)\n", g_options.max_period_ns,
		   (double)g_options.max_period_ns / 1000000000);
	APPL_PRINT(" min period:   %luns (%fs)\n", g_options.min_period_ns,
		   (double)g_options.min_period_ns / 1000000000);
	APPL_PRINT(" csv:          %s\n",
		   g_options.csv == NULL ? "(no)" : g_options.csv);
	APPL_PRINT(" tracebuffer:  %d tmo events (%luKiB)\n",
		   g_options.tracebuf,
		   g_options.tracebuf * sizeof(tmo_trace) / 1024);
	APPL_PRINT(" stop limit:   %d tmo events\n", g_options.stoplim);
	APPL_PRINT(" use NOSKIP:   %s\n", g_options.noskip ? "yes" : "no");
	APPL_PRINT(" profile API:  %s\n", g_options.profile ? "yes" : "no");
	APPL_PRINT(" dispatch prof:%s\n", g_options.dispatch ? "yes" : "no");
	APPL_PRINT(" time stamps:  %s\n", g_options.cpucycles ?
		   "CPU cycles" : "odp_time()");
	APPL_PRINT(" work propability:%u%%\n", g_options.work_prop);
	if (g_options.work_prop) {
		APPL_PRINT(" min_work:     %luns\n", g_options.min_work_ns);
		APPL_PRINT(" max_work:     %luns\n", g_options.max_work_ns);
	}
	APPL_PRINT(" bg events:    %u\n", g_options.bg_events);
	eo_ctx->bg_data = NULL;
	if (g_options.bg_events) {
		APPL_PRINT(" bg work:      %luus\n",
			   g_options.bg_time_ns / 1000);
		APPL_PRINT(" bg data:      %ukiB\n", g_options.bg_size / 1024);
		APPL_PRINT(" bg chunk:     %ukiB (%u blks)\n",
			   g_options.bg_chunk / 1024,
			   g_options.bg_size / g_options.bg_chunk);
		APPL_PRINT(" bg trace:     %s\n",
			   g_options.jobs ? "yes" : "no");

		eo_ctx->bg_data = malloc(g_options.bg_size);
		test_fatal_if(eo_ctx->bg_data == NULL,
			      "Can't allocate bg work data (%dkiB)!\n",
			      g_options.bg_size / 1024);
	}

	APPL_PRINT("\nTracing first %d tmo events\n", g_options.tracebuf);

	/* create periodic timeout for heartbeat */
	eo_ctx->heartbeat_tmo = em_tmo_create(m_shm->hb_tmr,
					      EM_TMO_FLAG_PERIODIC,
					      eo_ctx->hb_q);
	test_fatal_if(eo_ctx->heartbeat_tmo == EM_TMO_UNDEF,
		      "Can't allocate heartbeat_tmo!\n");

	event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, m_shm->pool);
	test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate event (%ldB)!\n",
		      sizeof(app_msg_t));

	msg = em_event_pointer(event);
	msg->command = CMD_HEARTBEAT;
	msg->count = 0;
	msg->id = -1;
	eo_ctx->hb_hz = em_timer_get_freq(m_shm->hb_tmr);
	if (eo_ctx->hb_hz < 10)
		APPL_ERROR("WARNING - HB timer hz very low!\n");
	else
		APPL_PRINT("HB timer frequency is %lu\n", eo_ctx->hb_hz);

	period = eo_ctx->hb_hz; /* 1s */
	test_fatal_if(period < 1, "timer resolution is too low!\n");

	/* linux time check */
	test_fatal_if(clock_getres(CLOCK_MONOTONIC, &ts) != 0,
		      "clock_getres() failed!\n");

	period = ts.tv_nsec + (ts.tv_sec * 1000000000ULL);
	eo_ctx->linux_hz = 1000000000ULL / period;
	APPL_PRINT("Linux reports clock running at %" PRIu64 " hz\n",
		   eo_ctx->linux_hz);

	APPL_PRINT("ODP says time_global runs at %luHz\n",
		   odp_time_global_res());
	if (!g_options.cpucycles)
		eo_ctx->time_hz = odp_time_global_res();

	/* start heartbeat */
	__atomic_store_n(&eo_ctx->state, STATE_INIT, __ATOMIC_SEQ_CST);

	em_status_t stat = em_tmo_set_rel(eo_ctx->heartbeat_tmo, eo_ctx->hb_hz,
					  event);

	test_fatal_if(stat != EM_OK, "Can't activate heartbeat tmo!\n");
	eo_ctx->test_hz = em_timer_get_freq(m_shm->test_tmr);

	stat = em_dispatch_register_enter_cb(enter_cb);
	test_fatal_if(stat != EM_OK, "enter_cb() register failed!");
	stat = em_dispatch_register_exit_cb(exit_cb);
	test_fatal_if(stat != EM_OK, "exit_cb() register failed!");

	srandom(time(NULL));
	if (g_options.max_work_ns > RAND_MAX ||
	    g_options.max_period_ns > RAND_MAX)
		APPL_PRINT("WARN - rnd number range is less than max values\n");
	if (EXTRA_PRINTS)
		APPL_PRINT("WARN - extra prints enabled, expect some jitter\n");

	return EM_OK;
}

/**
 * @private
 *
 * EO per thread start function.
 */
static em_status_t app_eo_start_local(void *eo_context, em_eo_t eo)
{
	app_eo_ctx_t *const eo_ctx = eo_context;
	int core = em_core_id();

	(void)eo;

	APPL_PRINT("EO local start\n");
	test_fatal_if(core >= MAX_CORES, "Too many cores!");
	eo_ctx->cdat[core].trc = calloc(g_options.tracebuf, sizeof(tmo_trace));
	test_fatal_if(eo_ctx->cdat[core].trc == NULL,
		      "Failed to allocate trace buffer!");
	eo_ctx->cdat[core].count = 0;
	eo_ctx->cdat[core].cancelled = 0;
	eo_ctx->cdat[core].jobs_deleted = 0;
	eo_ctx->cdat[core].jobs = 0;

	memset(&eo_ctx->cdat[core].rng, 0, sizeof(rnd_state_t));
	initstate_r(time(NULL), eo_ctx->cdat[core].rng.rndstate, RND_STATE_BUF,
		    &eo_ctx->cdat[core].rng.rdata);
	srandom_r(time(NULL), &eo_ctx->cdat[core].rng.rdata);
	return EM_OK;
}

/**
 * @private
 *
 * EO stop function.
 */
static em_status_t app_eo_stop(void *eo_context, em_eo_t eo)
{
	app_eo_ctx_t *const eo_ctx = eo_context;
	em_event_t event = EM_EVENT_UNDEF;
	em_status_t ret;

	APPL_PRINT("EO stop\n");

	if (eo_ctx->heartbeat_tmo != EM_TMO_UNDEF) {
		em_tmo_delete(eo_ctx->heartbeat_tmo, &event);
		eo_ctx->heartbeat_tmo = EM_TMO_UNDEF;
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	}

	/* TODO cancel all test timers */

	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "",
		      ret, eo);

	ret = em_dispatch_unregister_enter_cb(enter_cb);
	test_fatal_if(ret != EM_OK, "enter_cb() unregister:%" PRI_STAT, ret);
	ret = em_dispatch_unregister_exit_cb(exit_cb);
	test_fatal_if(ret != EM_OK, "exit_cb() unregister:%" PRI_STAT, ret);

	if (eo_ctx->bg_data != NULL)
		free(eo_ctx->bg_data);
	eo_ctx->bg_data = NULL;
	return EM_OK;
}

/**
 * @private
 *
 * EO stop local function.
 */
static em_status_t app_eo_stop_local(void *eo_context, em_eo_t eo)
{
	int core = em_core_id();
	app_eo_ctx_t *const eo_ctx = eo_context;

	(void)eo;

	APPL_PRINT("EO local stop\n");

	free(eo_ctx->cdat[core].trc);
	eo_ctx->cdat[core].trc = NULL;
	return EM_OK;
}

/**
 * @private
 *
 * EO receive function
 */
static void app_eo_receive(void *eo_context, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_context)
{
	app_eo_ctx_t *const eo_ctx = eo_context;
	int reuse = 0;
	static int last_count;

	(void)q_context;

	if (type == EM_EVENT_TYPE_SW) {
		app_msg_t *msgin = (app_msg_t *)em_event_pointer(event);

		switch (msgin->command) {
		case CMD_TMO:
			reuse = handle_periodic(eo_ctx, event);
			break;

		case CMD_HEARTBEAT: /* uses atomic queue */
			handle_heartbeat(eo_ctx, event);
			last_count = msgin->count;
			reuse = 1;
			break;

		case CMD_BGWORK:
			reuse = do_bg_work(event, eo_ctx);
			break;

		case CMD_DONE:	/* HB atomic queue */ {
			e_state state = __atomic_load_n(&eo_ctx->state,
							__ATOMIC_SEQ_CST);

				if (state == STATE_RUN &&
				    queue == eo_ctx->hb_q) {
					__atomic_store_n(&eo_ctx->state,
							 STATE_COOLOFF,
							 __ATOMIC_SEQ_CST);
					add_trace(eo_ctx, -1, OP_STATE,
						  linux_time_ns(),
						  STATE_COOLOFF);
					eo_ctx->last_hbcount = last_count;
					eo_ctx->stopped = get_time();
					APPL_PRINT("Core %d DONE\n", msgin->id);
				}
			}
			break;

		default:
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Invalid event!\n");
		}
	} else {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Invalid event type!\n");
	}

	if (!reuse)
		em_free(event);
}

/**
 * Main function
 *
 * Call cm_setup() to perform test & EM setup common for all the
 * test applications.
 *
 * cm_setup() will call test_init() and test_start() and launch
 * the EM dispatch loop on every EM-core.
 *
 * We're using command line arguments and pick those up before cm_setup
 * since argv is not yet available in test_init().
 */
int main(int argc, char *argv[])
{
	/* pick app-specific arguments after '--' */
	int i;

	APPL_PRINT("EM periodic timer test %s\n\n", VERSION);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			break;
	}
	if (i < argc) {
		if (!parse_my_args(i, argc, argv)) {
			APPL_PRINT("Invalid application arguments\n");
			return 1;
		}
	}

	return cm_setup(argc, argv);
}
