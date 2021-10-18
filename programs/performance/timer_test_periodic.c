/*
 *   Copyright (c) 2020-2021, Nokia Solutions and Networks
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
#include <ctype.h>
#include <sys/mman.h>
#include <signal.h>

#include <event_machine.h>
#include <event_machine/add-ons/event_machine_timer.h>
#include <event_machine/platform/env/environment.h>
#include <odp.h>

#include "cm_setup.h"
#include "cm_error_handler.h"

#include "timer_test_periodic.h"

#define VERSION "WIP v0.8"
struct {
	int num_periodic;
	uint64_t res_ns;
	uint64_t res_hz;
	uint64_t period_ns;
	int64_t first_ns;
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
	int info_only;
	int usehuge;		/* for trace buffer */
	int bg_events;
	uint64_t bg_time_ns;
	int bg_size;
	int bg_chunk;
	int mz_mb;
	int mz_huge;
	uint64_t mz_ns;
	int abort;		/* for testing abnormal exit */

} g_options = { .num_periodic =  1,	/* defaults for basic check */
		.res_ns =        DEF_RES_NS,
		.res_hz =	 0,
		.period_ns =     DEF_PERIOD * DEF_RES_NS,
		.first_ns =	 0,
		.max_period_ns = 0, /* max,min updated in init if not given cmdline */
		.min_period_ns = 0,
		.min_work_ns =   0,
		.max_work_ns =   0,
		.work_prop =     0,
		.clock_src =     EM_TIMER_CLKSRC_DEFAULT,
		.csv =		 NULL,
		.num_runs =	 1,
		.tracebuf =	 DEF_TMO_DATA,
		.stoplim =	 ((STOP_THRESHOLD * DEF_TMO_DATA) / 100),
		.noskip =	 1,
		.profile =	 0,
		.dispatch =	 0,
		.jobs =          0,
		.cpucycles =	 0,
		.info_only =	 0,
		.usehuge =	 0,
		.bg_events =     0,
		.bg_time_ns =	 10000,
		.bg_size =       5000 * 1024,
		.bg_chunk =      50 * 1024,
		.mz_mb =	 0,
		.mz_huge =	 0,
		.mz_ns =	 0,
		.abort =	 0
		};

typedef struct global_stats_t {
	uint64_t num_late;	/* ack late */
	int64_t	 max_dev_ns;	/* +- max deviation form target */
	uint64_t num_tmo;	/* total received tmo count */
	int	 max_cpu;	/* max CPU load % (any single) */
	uint64_t max_dispatch;  /* max EO receive time */
} global_stats_t;

typedef struct app_eo_ctx_t {
	e_state state;
	em_tmo_t heartbeat_tmo;
	em_timer_t test_tmr;
	em_queue_t hb_q;
	em_queue_t test_q;
	em_queue_t stop_q;
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
	void *mz_data;
	uint64_t mz_count;
	int stop_sent;
	em_atomic_group_t agrp;
	global_stats_t global_stat;
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
static int add_trace(app_eo_ctx_t *eo_ctx, int id, e_op op, uint64_t ns, int count);
static uint64_t linux_time_ns(void);
static em_status_t app_eo_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t app_eo_start_local(void *eo_context, em_eo_t eo);
static em_status_t app_eo_stop(void *eo_context, em_eo_t eo);
static em_status_t app_eo_stop_local(void *eo_context, em_eo_t eo);
static void app_eo_receive(void *eo_context, em_event_t event,
			   em_event_type_t type, em_queue_t queue, void *q_context);
static time_stamp get_time(void);
static uint64_t time_to_ns(time_stamp t);
static time_stamp time_diff(time_stamp t2, time_stamp t1);
static time_stamp time_sum(time_stamp t1, time_stamp t2);
static int arg_to_ns(const char *s, int64_t *val);
static void profile_statistics(e_op op, int cores, app_eo_ctx_t *eo_ctx);
static void profile_all_stats(int cores, app_eo_ctx_t *eo_ctx);
static void analyze_measure(app_eo_ctx_t *eo_ctx, uint64_t linuxns,
			    uint64_t tmrtick, time_stamp timetick);
static void timing_statistics(app_eo_ctx_t *eo_ctx);
static void add_prof(app_eo_ctx_t *eo_ctx, time_stamp t1, e_op op, app_msg_t *msg);
static int do_one_tmo(int id, app_eo_ctx_t *eo_ctx,
		      time_stamp *min, time_stamp *max, time_stamp *first, int64_t *tgt_max_ns);
static tmo_trace *find_tmo(app_eo_ctx_t *eo_ctx, int id, int count, int *last);
static uint64_t random_tmo_ns(void);
static uint64_t random_work_ns(rnd_state_t *rng);
static void enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
		     em_queue_t *queue, void **q_ctx);
static void exit_cb(em_eo_t eo);
static void send_bg_events(app_eo_ctx_t *eo_ctx);
static int do_bg_work(em_event_t evt, app_eo_ctx_t *eo_ctx);
static int do_memzero(app_msg_t *msg, app_eo_ctx_t *eo_ctx);
static em_status_t my_error_handler(em_eo_t eo, em_status_t error,
				    em_escope_t escope, va_list args);
static void *allocate_tracebuf(int numbuf, size_t bufsize, size_t *realsize);
static void free_tracebuf(void *ptr, size_t realsize);
static void prefault(void *buf, size_t size);
static void show_global_stats(app_eo_ctx_t *eo_ctx);

/* --------------------------------------- */
em_status_t my_error_handler(em_eo_t eo, em_status_t error,
			     em_escope_t escope, va_list args)
{
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

	if (unlikely(!my_eo_ctx))
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

	if (unlikely(!my_eo_ctx))
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

void prefault(void *buf, size_t size)
{
	uint8_t *ptr = (uint8_t *)buf;

	/* write all pages to allocate and pre-fault (reduce runtime jitter) */
	APPL_PRINT("Pre-faulting %lu bytes at %p (EM core %d)\n", size, buf, em_core_id());
	for (size_t i = 0; i < size; i += 4096)
		*(ptr + i) = (uint8_t)i;
}

void *allocate_tracebuf(int numbuf, size_t bufsize, size_t *realsize)
{
	if (g_options.usehuge) {
		*realsize = (numbuf + 1) * bufsize;
		void *ptr = mmap(NULL, *realsize, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB | MAP_LOCKED,
			    -1, 0);
		if (ptr == MAP_FAILED) {
			APPL_PRINT("Huge page mapping failed for trace buffer (%lu bytes)\n",
				   *realsize);
			return NULL;
		} else {
			return ptr;
		}

	} else {
		void *buf = calloc(numbuf + 1, bufsize);

		*realsize = numbuf * bufsize;
		prefault(buf, *realsize);
		return buf;
	}
}

void free_tracebuf(void *ptr, size_t realsize)
{
	if (g_options.usehuge)
		munmap(ptr, realsize);
	else
		free(ptr);
}

inline time_stamp get_time(void)
{
	time_stamp t;

	if (unlikely(g_options.cpucycles))
		t.u64 = get_cpu_cycle();
	else
		t.odp = odp_time_global();
	return t;
}

uint64_t time_to_ns(time_stamp t)
{
	double ns;

	if (unlikely(g_options.cpucycles)) { /* todo drop cpucycles choice to get rid of this? */
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

	if (unlikely(g_options.cpucycles))
		t.u64 = t2.u64 - t1.u64;
	else
		t.odp = odp_time_diff(t2.odp, t1.odp);

	return t;
}

time_stamp time_sum(time_stamp t1, time_stamp t2)
{
	time_stamp t;

	if (unlikely(g_options.cpucycles))
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

int arg_to_ns(const char *s, int64_t *val)
{
	char *endp;
	int64_t num, mul = 1;

	num = strtol(s, &endp, 0);
	if (num == 0 && *s != '0')
		return 0;

	if (*endp != '\0')
		switch (*endp) {
		case 'n':
			mul = 1; /* ns */
			break;
		case 'u':
			mul = 1000; /* us */
			break;
		case 'm':
			mul = 1000 * 1000; /* ms */
			break;
		case 's':
			mul = 1000 * 1000 * 1000; /* s */
			break;
		default:
			return 0;
		}

	*val = num * mul;
	return 1;
}

void send_stop(app_eo_ctx_t *eo_ctx)
{
	em_status_t ret;

	if (!eo_ctx->stop_sent) { /* in case state change gets delayed on event overload */
		em_event_t event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, m_shm->pool);

		test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate stop event!\n");

		app_msg_t *msg = em_event_pointer(event);

		msg->command = CMD_DONE;
		msg->id = em_core_id();
		ret = em_send(event, eo_ctx->stop_q);
		test_fatal_if(ret != EM_OK, "em_send(): %s %" PRI_STAT, __func__, ret);
		eo_ctx->stop_sent++;
	}
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

	fprintf(fle, "\n\n#BEGIN TRACE FORMAT 1\n"); /* for offline analyzers */
	fprintf(fle, "res_ns,res_hz,period_ns,max_period_ns,clksrc,num_tmo,loops,");
	fprintf(fle, "traces,noskip,SW-ver,bg,mz\n");
	fprintf(fle, "%lu,%lu,%lu,%lu,%d,%d,%d,%d,%d,%s,\"%d/%lu\",\"%d/%lu\"\n",
		g_options.res_ns,
		g_options.res_hz,
		g_options.period_ns,
		g_options.max_period_ns,
		g_options.clock_src,
		g_options.num_periodic,
		g_options.num_runs,
		g_options.tracebuf,
		g_options.noskip,
		VERSION,
		g_options.bg_events, g_options.bg_time_ns / 1000UL,
		g_options.mz_mb, g_options.mz_ns / 1000000UL
		);
	fprintf(fle, "time_hz,meas_time_hz,timer_hz,meas_timer_hz,linux_hz\n");
	fprintf(fle, "%lu,%lu,%lu,%lu,%lu\n",
		eo_ctx->time_hz,
		eo_ctx->meas_time_hz,
		eo_ctx->test_hz,
		eo_ctx->meas_test_hz,
		eo_ctx->linux_hz);

	fprintf(fle, "tmo_id,period_ns,period_ticks,ack_late");
	fprintf(fle, ",start_tick,start_ns,first_ns,first\n");
	for (int i = 0; i < g_options.num_periodic; i++) {
		fprintf(fle, "%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
			i, eo_ctx->tmo_data[i].period_ns,
			eo_ctx->tmo_data[i].ticks,
			eo_ctx->tmo_data[i].ack_late,
			eo_ctx->tmo_data[i].start,
			time_to_ns(eo_ctx->tmo_data[i].start_ts),
			(uint64_t)eo_ctx->tmo_data[i].first_ns,
			eo_ctx->tmo_data[i].first);
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

void show_global_stats(app_eo_ctx_t *eo_ctx)
{
	APPL_PRINT("\nTOTAL STATS:\n");
	APPL_PRINT("  Num tmo:           %lu\n", eo_ctx->global_stat.num_tmo);
	APPL_PRINT("  Num late ack:      %lu", eo_ctx->global_stat.num_late);
	APPL_PRINT(" (%lu%%)\n",
		   (eo_ctx->global_stat.num_late * 100) / eo_ctx->global_stat.num_tmo);
	APPL_PRINT("  Max diff from tgt: %.1fus (res %.1fus)\n",
		   ((double)eo_ctx->global_stat.max_dev_ns) / 1000.0,
		   (double)g_options.res_ns / 1000.0);
	APPL_PRINT("  Max CPU load:      %d%%\n", eo_ctx->global_stat.max_cpu);
	if (eo_ctx->global_stat.max_dispatch)
		APPL_PRINT("  Max EO rcv time:   %luns\n", eo_ctx->global_stat.max_dispatch);
	APPL_PRINT("\n");
}

uint64_t random_tmo_ns(void)
{
	uint64_t r = random() % (g_options.max_period_ns - g_options.min_period_ns + 1);

	return r + g_options.min_period_ns; /* ns between min/max period */
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

tmo_trace *find_tmo(app_eo_ctx_t *eo_ctx, int id, int count, int *last)
{
	int cores = em_core_count();
	tmo_trace *trc = NULL;
	int last_count = 0;

	for (int c = 0; c < cores; c++) {
		for (int i = 0; i < eo_ctx->cdat[c].count; i++) { /* find id */
			if (eo_ctx->cdat[c].trc[i].op == OP_TMO &&
			    eo_ctx->cdat[c].trc[i].id == id) { /* this TMO */
				if (eo_ctx->cdat[c].trc[i].count == count) {
					trc = &eo_ctx->cdat[c].trc[i];
				} else {
					/* always run through for last_count */
					if (eo_ctx->cdat[c].trc[i].count > last_count)
						last_count = eo_ctx->cdat[c].trc[i].count;
				}
			}
		}
	}
	*last = last_count;
	return trc;
}

int do_one_tmo(int id, app_eo_ctx_t *eo_ctx,
	       time_stamp *min, time_stamp *max, time_stamp *first, int64_t *tgt_max)
{
	int num = 0;
	time_stamp diff;
	time_stamp prev = {0};
	int last = 0;
	int last_num;
	uint64_t period_ns = eo_ctx->tmo_data[id].period_ns;
	uint64_t first_ns = time_to_ns(eo_ctx->tmo_data[id].start_ts);
	int64_t max_tgt_diff = 0;

	max->u64 = 0;
	min->u64 = INT64_MAX;

	/* find in sequential order for diff to work. TODO this gets very slow with many tmos */

	for (int count = 1; count < g_options.tracebuf; count++) {
		tmo_trace *tmo = find_tmo(eo_ctx, id, count, &last_num);

		if (!tmo) {
			if (last != count - 1)
				APPL_PRINT("MISSING TMO: id %d, count %d\n", id, count);
			*tgt_max = max_tgt_diff;
			return num;
		}
		last++;
		if (!num) { /* skip first for min/max but store time */
			diff = time_diff(tmo->ts, eo_ctx->tmo_data[id].start_ts);
			*first = diff;
			if (eo_ctx->tmo_data[id].first_ns != eo_ctx->tmo_data[id].period_ns)
				first_ns = time_to_ns(tmo->ts); /* ignore first */

		} else {
			diff = time_diff(tmo->ts, prev);
			if (last_num > count) { /*skip last diff, could be while stopping */
				if (time_to_ns(diff) > time_to_ns(*max))
					*max = diff;
				if (time_to_ns(diff) < time_to_ns(*min))
					*min = diff;

				/* calculate distance to target */
				uint64_t tgt = first_ns + count * period_ns;
				int64_t tgtdiff = (int64_t)time_to_ns(tmo->ts) - (int64_t)tgt;

				if (llabs(max_tgt_diff) < llabs(tgtdiff))
					max_tgt_diff = tgtdiff;
			}
		}
		prev = tmo->ts;
		num++;
	}
	*tgt_max = max_tgt_diff;
	return num;
}

void timing_statistics(app_eo_ctx_t *eo_ctx)
{
	/* todo: a lot to improve here */
	time_stamp max_ts = {0}, min_ts = {0}, first_ts = {0};
	int64_t tgt_max = 0;
	const int cores = em_core_count();
	uint64_t system_used = time_to_ns(time_diff(eo_ctx->stopped, eo_ctx->started));

	for (int c = 0; c < cores; c++) {
		core_data *cdat = &eo_ctx->cdat[c];
		uint64_t eo_used = time_to_ns(cdat->acc_time);
		double perc = (double)eo_used / (double)system_used * 100;

		if (perc > 100)
			perc = 100;
		APPL_PRINT("STAT_CORE [%d]: %d tmos, %d jobs, EO used %.1f%% CPU time\n",
			   c, cdat->count, cdat->jobs, perc);
		if (perc > eo_ctx->global_stat.max_cpu)
			eo_ctx->global_stat.max_cpu = round(perc);
		eo_ctx->global_stat.num_tmo += cdat->count;
	}

	for (int id = 0; id < g_options.num_periodic; id++) { /* each timeout */
		tmo_setup *tmo_data = &eo_ctx->tmo_data[id];
		int num = do_one_tmo(id, eo_ctx, &min_ts, &max_ts, &first_ts, &tgt_max);

		APPL_PRINT("STAT-TMO [%d]: %d tmos, period %luns (", id, num, tmo_data->period_ns);
		if (num > 1) {
			int64_t maxdiff = (int64_t)time_to_ns(max_ts) -
					  (int64_t)tmo_data->period_ns;

			int64_t mindiff = (int64_t)time_to_ns(min_ts) -
					  (int64_t)tmo_data->period_ns;

			APPL_PRINT("%lu ticks), interval %ldns ... +%ldns",
				   tmo_data->ticks, mindiff, maxdiff);
			APPL_PRINT(" (%ldus ... +%ldus)\n", mindiff / 1000, maxdiff / 1000);
			APPL_PRINT("  - Max diff from target %.2fus\n", (double)tgt_max / 1000);
			if (llabs(tgt_max) > llabs(eo_ctx->global_stat.max_dev_ns))
				eo_ctx->global_stat.max_dev_ns = tgt_max;
		} else {
			APPL_PRINT("%lu ticks), 1st period %lu\n",
				   tmo_data->ticks, time_to_ns(first_ts));
		}
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
					APPL_PRINT("No enter cnt=%d\n", prev_count);

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

	APPL_PRINT("%d dispatcher enter-exit samples\n", num);
	APPL_PRINT("PROF-DISPATCH rcv time: min %luns, max %luns, avg %luns\n",
		   min, max, num > 0 ? avg / num : 0);

	if (max > eo_ctx->global_stat.max_dispatch)
		eo_ctx->global_stat.max_dispatch = max;
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

	/* more checks TODO? */

	timing_statistics(eo_ctx);

	if (g_options.profile)
		profile_all_stats(cores, eo_ctx);

	for (int c = 0; c < cores; c++) {
		cancelled += eo_ctx->cdat[c].cancelled;
		job_del += eo_ctx->cdat[c].jobs_deleted;
	}

	show_global_stats(eo_ctx);

	/* write trace file */
	if (g_options.csv != NULL)
		write_trace(eo_ctx, g_options.csv);

	APPL_PRINT("%d/%d timeouts were cancelled\n", cancelled, g_options.num_periodic);

	if (g_options.bg_events)
		APPL_PRINT("%d/%d bg jobs were deleted\n", job_del, g_options.bg_events);
	if (g_options.mz_mb)
		APPL_PRINT("%lu memzeros\n", eo_ctx->mz_count);
	double span = time_to_ns(eo_ctx->stopped) - time_to_ns(eo_ctx->started);

	span /= 1000000000;
	APPL_PRINT("Timer runtime %fs\n", span);

	test_fatal_if(cancelled != g_options.num_periodic,
		      "Not all tmos deleted (did not arrive at all?)\n");
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
		test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate bg event!\n");
		app_msg_t *msg = em_event_pointer(event);

		msg->command = CMD_BGWORK;
		msg->count = 0;
		msg->id = n + 1;
		msg->arg = g_options.bg_time_ns;
		test_fatal_if(em_send(event, eo_ctx->bg_q) != EM_OK, "Can't allocate bg event!\n");
	}
}

void start_periodic(app_eo_ctx_t *eo_ctx)
{
	app_msg_t *msg;
	em_event_t event;
	em_tmo_t tmo;
	em_tmo_flag_t flag = EM_TMO_FLAG_PERIODIC;
	time_stamp t1 = {0};
	uint64_t max_period = 0;

	if (g_options.noskip)
		flag |= EM_TMO_FLAG_NOSKIP;
	eo_ctx->stop_sent = 0;
	eo_ctx->started = get_time();

	for (int i = 0; i < g_options.num_periodic; i++) {
		event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, m_shm->pool);
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

		test_fatal_if(tmo == EM_TMO_UNDEF, "Can't allocate test_tmo!\n");
		msg->tmo = tmo;
		eo_ctx->tmo_data[i].handle = tmo;

		double ns = 1000000000 / (double)eo_ctx->test_hz;
		uint64_t period;
		uint64_t first = 0;
		em_status_t stat;

		if (g_options.period_ns) {
			eo_ctx->tmo_data[i].period_ns = g_options.period_ns;
		} else { /* 0: use random */
			eo_ctx->tmo_data[i].period_ns = random_tmo_ns();
		}
		if (max_period < eo_ctx->tmo_data[i].period_ns)
			max_period = eo_ctx->tmo_data[i].period_ns;
		period = round((double)eo_ctx->tmo_data[i].period_ns / ns);

		if (EXTRA_PRINTS && i == 0) {
			APPL_PRINT("Timer Hz %lu ", eo_ctx->test_hz);
			APPL_PRINT("= Period ns: %f => period %lu ticks\n", ns, period);
		}

		test_fatal_if(period < 1, "timer resolution is too low!\n");

		if (g_options.first_ns < 0) /* use random */
			eo_ctx->tmo_data[i].first_ns = random_tmo_ns();
		else if (g_options.first_ns == 0) /* use period */
			eo_ctx->tmo_data[i].first_ns = eo_ctx->tmo_data[i].period_ns;
		else
			eo_ctx->tmo_data[i].first_ns = g_options.first_ns;

		first = round((double)eo_ctx->tmo_data[i].first_ns / ns);
		if (!first)
			first = 1;
		eo_ctx->tmo_data[i].first = first;

		eo_ctx->tmo_data[i].start_ts = get_time();
		eo_ctx->tmo_data[i].start = em_timer_current_tick(m_shm->test_tmr);
		first += eo_ctx->tmo_data[i].start;
		if (g_options.profile)
			t1 = get_time();
		stat = em_tmo_set_periodic(tmo, first, period, event);
		if (g_options.profile)
			add_prof(eo_ctx, t1, OP_PROF_SET, msg);

		if (unlikely(stat != EM_OK)) {
			if (EXTRA_PRINTS) {
				em_timer_tick_t now = em_timer_current_tick(eo_ctx->test_tmr);

				APPL_PRINT("FAILED to set tmo, stat=%d: first=%lu, ", stat, first);
				APPL_PRINT("now %lu (diff %ld), period=%lu\n",
					   now, (int64_t)first - (int64_t)now, period);
				APPL_PRINT("(first_ns %lu)\n", eo_ctx->tmo_data[i].first_ns);
			}
			test_fatal_if(1, "Can't activate test tmo!\n");
		}

		eo_ctx->tmo_data[i].ack_late = 0;
		eo_ctx->tmo_data[i].ticks = period;
		eo_ctx->max_period = max_period;
		eo_ctx->cooloff = (max_period / 1000000000ULL * 2) + 1;
		if (eo_ctx->cooloff < 4)
			eo_ctx->cooloff = 4; /* HB periods (sec) */
	}
}

void add_prof(app_eo_ctx_t *eo_ctx, time_stamp t1, e_op op, app_msg_t *msg)
{
	time_stamp dif = time_diff(get_time(), t1);

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
	em_tmo_stats_t ctrs = { 0 }; /* init to avoid gcc warning with LTO */
	em_status_t ret;

	msg->count++;

	/* this is to optionally test abnormal exits only */
	if (unlikely(g_options.abort != 0) && abs(g_options.abort) <= msg->count) {
		if (g_options.abort < 0) { /* cause segfault */
			uint64_t *fault = NULL;
			/* coverity[FORWARD_NULL] */
			msg->arg = *fault;
		} else {
			abort();
		}
	}

	if (likely(state == STATE_RUN)) { /* add tmo trace */
		if (!add_trace(eo_ctx, msg->id, OP_TMO, 0, msg->count))
			send_stop(eo_ctx); /* triggers state change */

		if (g_options.work_prop) {
			uint64_t work = random_work_ns(&eo_ctx->cdat[core].rng);

			if (work) { /* add extra delay */
				time_stamp t2;
				uint64_t ns = time_to_ns(get_time());

				do {
					t2 = get_time();
				} while (time_to_ns(t2) < (ns + work));
				add_trace(eo_ctx, msg->id, OP_WORK, work, msg->count);
			}
		}

		/* only ack while in running state */
		add_trace(eo_ctx, msg->id, OP_ACK, 0, msg->count);
		if (g_options.profile)
			t1 = get_time();
		em_status_t stat = em_tmo_ack(msg->tmo, event);

		if (g_options.profile)
			add_prof(eo_ctx, t1, OP_PROF_ACK, msg);
		if (unlikely(stat != EM_OK))
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "ack() fail!\n");

	} else if (state == STATE_COOLOFF) { /* trace, but cancel */
		em_event_t tmo_event = EM_EVENT_UNDEF;

		add_trace(eo_ctx, msg->id, OP_TMO, 0, msg->count);
		em_tmo_get_stats(msg->tmo, &ctrs);
		APPL_PRINT("STAT-ACK [%d]:  %lu acks, %lu late, %lu skips\n",
			   msg->id, ctrs.num_acks, ctrs.num_late_ack, ctrs.num_period_skips);
		eo_ctx->tmo_data[msg->id].ack_late = ctrs.num_late_ack;
		eo_ctx->global_stat.num_late += ctrs.num_late_ack;

		if (g_options.profile)
			t1 = get_time();
		ret = em_tmo_delete(msg->tmo, &tmo_event);
		if (g_options.profile)
			add_prof(eo_ctx, t1, OP_PROF_DELETE, msg);
		test_fatal_if(ret != EM_OK, "tmo_delete failed, ret %" PRI_STAT "!\n", ret);

		if (unlikely(msg->id >= g_options.num_periodic))
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Corrupted tmo msg?\n");

		eo_ctx->tmo_data[msg->id].handle = EM_TMO_UNDEF;
		eo_ctx->cdat[core].cancelled++;
		if (unlikely(tmo_event != EM_EVENT_UNDEF)) { /* not expected */
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "periodic tmo delete returned evt!\n");
		}
		add_trace(eo_ctx, msg->id, OP_CANCEL, 0, msg->count);
		reuse = 0; /* free this last tmo event of deleted tmo */
	} else {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "Timeout in state %s!\n", state_labels[state]);
	}
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
	APPL_PRINT("=> %.1fHz (%.1fMHz). Timer reports %luHz\n", hz, hz / 1000000, eo_ctx->test_hz);
	eo_ctx->meas_test_hz = round(hz);
	hz = 1000000000 / ((double)linux_t2 / (double)time_t2.u64);
	APPL_PRINT("Timestamp measured: %.1fHz (%.1fMHz)\n", hz, hz / 1000000);
	eo_ctx->meas_time_hz = round(hz);

	if (g_options.cpucycles == 1) /* use measured */
		eo_ctx->time_hz = eo_ctx->meas_time_hz;
	if (g_options.cpucycles > 1) /* freq given */
		eo_ctx->time_hz = (uint64_t)g_options.cpucycles;

	test_fatal_if(tmr_t2 < 1, "TIMER SEEMS NOT RUNNING AT ALL!?");
}

int do_memzero(app_msg_t *msg, app_eo_ctx_t *eo_ctx)
{
	static int count;

	add_trace(eo_ctx, -1, OP_MEMZERO, g_options.mz_mb, msg->count);
	if (eo_ctx->mz_data == NULL) { /* first time we only allocate */
		if (g_options.mz_huge) {
			eo_ctx->mz_data = mmap(NULL, g_options.mz_mb * 1024UL * 1024UL,
					       PROT_READ | PROT_WRITE,
					       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE |
					       MAP_HUGETLB | MAP_LOCKED,
					       -1, 0);
			if (eo_ctx->mz_data == MAP_FAILED)
				eo_ctx->mz_data = NULL;
		} else {
			eo_ctx->mz_data = malloc(g_options.mz_mb * 1024UL * 1024UL);
		}
		test_fatal_if(eo_ctx->mz_data == NULL, "mz_mem reserve failed!");
	} else {
		memset(eo_ctx->mz_data, 0, g_options.mz_mb * 1024UL * 1024UL);
		eo_ctx->mz_count++;
	}
	add_trace(eo_ctx, -1, OP_MEMZERO_END, g_options.mz_mb, count);
	__atomic_fetch_add(&count, 1, __ATOMIC_RELAXED);
	return 0;
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
			APPL_PRINT("Deleting job after %u iterations\n", msg->count);
		return 0; /* stop & delete */
	}

	if (g_options.jobs)
		add_trace(eo_ctx, -1, OP_BGWORK, msg->arg, msg->count);

	msg->count++;
	eo_ctx->cdat[core].jobs++;
	int blocks = g_options.bg_size / g_options.bg_chunk;

	random_r(&eo_ctx->cdat[core].rng.rdata, &rnd);
	rnd = rnd % blocks;
	uint64_t *dptr = (uint64_t *)((uintptr_t)eo_ctx->bg_data + rnd * g_options.bg_chunk);
	/* printf("%d: %p - %p\n", rnd, eo_ctx->bg_data, dptr); */

	do {
		/* jump around memory reading from selected chunk */
		random_r(&eo_ctx->cdat[core].rng.rdata, &rnd);
		rnd = rnd % (g_options.bg_chunk / sizeof(uint64_t));
		/* printf("%d: %p - %p\n", rnd, eo_ctx->bg_data, dptr+rnd); */
		sum += *(dptr + rnd);
		ts = time_diff(get_time(), t1);
	} while (time_to_ns(ts) < msg->arg);

	*dptr = sum;

	if (g_options.mz_mb && msg->id == 1) { /* use only one job stream for memzero */
		static time_stamp last_mz = {0};

		if (msg->count < 10)	/* don't do mz before some time */
			last_mz = get_time();
		ts = time_diff(get_time(), last_mz);
		if (time_to_ns(ts) > g_options.mz_ns) {
			do_memzero(msg, eo_ctx);
			last_mz = get_time();
		}
	}

	test_fatal_if(em_send(evt, eo_ctx->bg_q) != EM_OK, "Failed to send BG job event!");
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
	add_trace(eo_ctx, -1, OP_HB, linux_time_ns(), msg->count);

	if (EXTRA_PRINTS)
		APPL_PRINT(".");

	switch (state) {
	case STATE_INIT:
		if (msg->count > eo_ctx->last_hbcount + INIT_WAIT) {
			__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
			eo_ctx->last_hbcount = msg->count;
			APPL_PRINT("ROUND %d\n", runs + 1);
			APPL_PRINT("->Starting tick measurement\n");
		}
		break;

	case STATE_MEASURE:	/* measure timer frequencies */
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
			add_trace(eo_ctx, -1, OP_STATE, linux_time_ns(), eo_ctx->state);
		}
		break;

	case STATE_STABILIZE:	/* give some time to get up */
		if (g_options.bg_events)
			send_bg_events(eo_ctx);
		__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
		add_trace(eo_ctx, -1, OP_STATE, linux_time_ns(), eo_ctx->state);
		if (EXTRA_PRINTS)
			APPL_PRINT("->Starting tmos\n");
		start_periodic(eo_ctx);
		eo_ctx->last_hbcount = msg->count;
		break;

	case STATE_RUN:	/* run the test, avoid prints */
		for (int i = 0; i < cores; i++) {
			if (eo_ctx->cdat[i].count >=
			   g_options.tracebuf) {
				done++;
				break;
			}
		}
		if (done) {
			__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
			add_trace(eo_ctx, -1, OP_STATE, linux_time_ns(), eo_ctx->state);
			eo_ctx->last_hbcount = msg->count;
			if (EXTRA_PRINTS)
				APPL_PRINT("->All cores done\n");
		}
		break;

	case STATE_COOLOFF:	/* stop further timeouts */
		if (msg->count > (eo_ctx->last_hbcount + eo_ctx->cooloff)) {
			__atomic_fetch_add(&eo_ctx->state, 1, __ATOMIC_SEQ_CST);
			add_trace(eo_ctx, -1, OP_STATE, linux_time_ns(), eo_ctx->state);
			eo_ctx->last_hbcount = msg->count;
			if (EXTRA_PRINTS)
				APPL_PRINT("->Starting analyze\n");
		}
		break;

	case STATE_ANALYZE:	/* expected to be stopped, analyze data */
		APPL_PRINT("\n");
		analyze(eo_ctx);
		cleanup(eo_ctx);
		/* re-start test cycle */
		__atomic_store_n(&eo_ctx->state, STATE_INIT, __ATOMIC_SEQ_CST);
		runs++;
		if (runs >= g_options.num_runs && g_options.num_runs != 0) {
			/* terminate test app */
			APPL_PRINT("%d runs done\n", runs);
			raise(SIGINT);
		}
		eo_ctx->last_hbcount = msg->count;
		break;

	default:
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Invalid test state");
	}

	/* heartbeat never stops */
	if (em_tmo_ack(eo_ctx->heartbeat_tmo, event) != EM_OK)
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "HB ack() fail!\n");
}

void usage(void)
{
	printf("%s\n", instructions);

	printf("Usage:\n");
	for (int i = 0; ; i++) {
		if (longopts[i].name == NULL || descopts[i] == NULL)
			break;
		printf("--%s or -%c: %s\n", longopts[i].name, longopts[i].val, descopts[i]);
	}
}

int parse_my_args(int first, int argc, char *argv[])
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
		case 'i': {
			g_options.info_only = 1;
		}
		break;
		case 'u': {
			g_options.usehuge = 1;
		}
		break;
		case 'g': {
			g_options.cpucycles = 1;
			if (optarg != NULL) { /* optional arg */
				num = strtol(optarg, &endptr, 0);
				if (*endptr != '\0' || num < 2)
					return 0;
				g_options.cpucycles = num;
			}
		}
		break;
		case 'w': { /* optional arg */
			g_options.csv = "stdout";
			if (optarg != NULL)
				g_options.csv = optarg;
		}
		break;
		case 'm': {
			if (!arg_to_ns(optarg, &num))
				return 0;
			if (num < 1)
				return 0;
			g_options.max_period_ns = (uint64_t)num;
		}
		break;
		case 'l': {
			if (!arg_to_ns(optarg, &num))
				return 0;
			if (num < 1)
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
				g_options.stoplim = ((STOP_THRESHOLD * size) / 100);
		}
		break;
		case 'e': {
			unsigned int min_us, max_us, prop;

			if (sscanf(optarg, "%u,%u,%u", &min_us, &max_us, &prop) != 3)
				return 0;
			if (prop > 100 || max_us < 1)
				return 0;
			g_options.min_work_ns = 1000ULL * min_us;
			g_options.max_work_ns = 1000ULL * max_us;
			g_options.work_prop = prop;
		}
		break;
		case 'o': {
			unsigned int mb;
			uint64_t ms;
			unsigned int hp = 0;

			if (sscanf(optarg, "%u,%lu,%u", &mb, &ms, &hp) < 2)
				return 0;
			if (mb < 1 || ms < 1)
				return 0;
			g_options.mz_mb = mb;
			g_options.mz_ns = ms * 1000UL * 1000UL;
			if (hp)
				g_options.mz_huge = 1;
		}
		break;
		case 'j': {
			unsigned int evts, us, kb, chunk;

			num = sscanf(optarg, "%u,%u,%u,%u", &evts, &us, &kb, &chunk);
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
			if (!arg_to_ns(optarg, &num))
				return 0;
			if (num < 0)
				return 0;
			g_options.period_ns = num;
		}
		break;
		case 'f': {
			if (!arg_to_ns(optarg, &num))
				return 0;
			if (num < -1)
				return 0;
			g_options.first_ns = num;
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
			if (!arg_to_ns(optarg, &num))
				return 0;
			if (num < 0)
				return 0;
			g_options.res_ns = num;
		}
		break;
		case 'z': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.res_hz = num;
			g_options.res_ns = 0;
		}
		break;
		case 'x': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.num_runs = num;
		}
		break;
		case 'k': {
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0')
				return 0;
			g_options.abort = num;
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
 * Before EM - Init
 */
void test_init(void)
{
	int core = em_core_id();

	/* first core creates ShMem */
	if (core == 0) {
		m_shm = env_shared_reserve("Timer_test", sizeof(timer_app_shm_t));
		/* initialize it */
		if (m_shm)
			memset(m_shm, 0, sizeof(timer_app_shm_t));

		APPL_PRINT("%ldk shared memory for app context\n", sizeof(timer_app_shm_t) / 1000);

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
 * Startup of the timer test EM application
 */
void test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	em_timer_attr_t attr;
	em_queue_t queue;
	em_status_t stat;
	app_eo_ctx_t *eo_ctx;
	em_timer_res_param_t res_capa;
	em_timer_capability_t capa = { 0 }; /* init to avoid gcc warning with LTO */
	em_core_mask_t mask;
	em_queue_group_t grp;
	em_atomic_group_t agrp;

	if (appl_conf->num_procs > 1) {
		APPL_PRINT("\n!! Multiple PROCESS MODE NOT SUPPORTED !!\n\n");
		raise(SIGINT);
		return;
	}

	if (appl_conf->num_pools >= 1)
		m_shm->pool = appl_conf->pools[0];
	else
		m_shm->pool = EM_POOL_DEFAULT;

	eo_ctx = &m_shm->eo_context;
	memset(eo_ctx, 0, sizeof(app_eo_ctx_t));
	eo_ctx->tmo_data = calloc(g_options.num_periodic, sizeof(tmo_setup));
	test_fatal_if(eo_ctx->tmo_data == NULL, "Can't alloc tmo_setups");

	eo = em_eo_create(APP_EO_NAME, app_eo_start, app_eo_start_local,
			  app_eo_stop, app_eo_stop_local, app_eo_receive,
			  eo_ctx);
	test_fatal_if(eo == EM_EO_UNDEF, "Failed to create EO!");

	stat = em_register_error_handler(my_error_handler);
	test_fatal_if(stat != EM_OK, "Failed to register error handler");

	/* Create atomic group and queues for control messages */
	stat = em_queue_group_get_mask(EM_QUEUE_GROUP_DEFAULT, &mask);
	test_fatal_if(stat != EM_OK, "Failed to get default Q grp mask!");

	grp = em_queue_group_create_sync("CTRL_GRP", &mask);
	test_fatal_if(grp == EM_QUEUE_GROUP_UNDEF, "Failed to create Q grp!");
	agrp = em_atomic_group_create("CTRL_AGRP", grp);
	test_fatal_if(agrp == EM_ATOMIC_GROUP_UNDEF, "Failed to create atomic grp!");
	eo_ctx->agrp = agrp;

	queue = em_queue_create_ag("Control Q", EM_QUEUE_PRIO_NORMAL, agrp, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create hb queue!");
	eo_ctx->hb_q = queue;

	queue = em_queue_create_ag("Stop Q", EM_QUEUE_PRIO_HIGHEST, agrp, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create stop queue!");
	eo_ctx->stop_q = queue;

	/* parallel high priority for timeout handling*/
	queue = em_queue_create("Tmo Q",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_HIGH,
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
	em_timer_attr_init(&attr);
	strncpy(attr.name, "HBTimer", EM_TIMER_NAME_LEN);
	m_shm->hb_tmr = em_timer_create(&attr);
	test_fatal_if(m_shm->hb_tmr == EM_TIMER_UNDEF,
		      "Failed to create HB timer!");

	/* test timer */
	test_fatal_if(g_options.res_ns && g_options.res_hz, "Give resolution in ns OR hz!");

	em_timer_attr_init(&attr);
	stat = em_timer_capability(&capa, g_options.clock_src);

	APPL_PRINT("Timer capability for clksrc %d:\n", g_options.clock_src);
	APPL_PRINT(" max_res %luns %luhz min_tmo %lu max_tmo %lu\n",
		   capa.max_res.res_ns, capa.max_res.res_hz,
		   capa.max_res.min_tmo, capa.max_res.max_tmo);
	APPL_PRINT(" max_tmo %luns %luhz min_tmo %lu max_tmo %lu\n",
		   capa.max_tmo.res_ns, capa.max_tmo.res_hz,
		   capa.max_tmo.min_tmo, capa.max_tmo.max_tmo);

	test_fatal_if(stat != EM_OK, "Given clk_src is not supported\n");
	memset(&res_capa, 0, sizeof(em_timer_res_param_t));
	if (!g_options.res_hz) {
		res_capa.res_ns = g_options.res_ns == 0 ? capa.max_res.res_ns : g_options.res_ns;
		APPL_PRINT("Trying %lu ns resolution capability on clk %d\n",
			   res_capa.res_ns, g_options.clock_src);
	} else {
		res_capa.res_hz = g_options.res_hz;
		APPL_PRINT("Trying %lu Hz resolution capability on clk %d\n",
			   res_capa.res_hz, g_options.clock_src);
	}

	APPL_PRINT("Asking timer capability for clksrc %d:\n", g_options.clock_src);
	APPL_PRINT("%luns %luhz min_tmo %lu max_tmo %lu\n",
		   res_capa.res_ns, res_capa.res_hz,
		   res_capa.min_tmo, res_capa.max_tmo);
	stat = em_timer_res_capability(&res_capa, g_options.clock_src);
	APPL_PRINT("-> Timer res_capability:\n");
	APPL_PRINT("max_res %luns %luhz min_tmo %lu max_tmo %lu\n",
		   res_capa.res_ns, res_capa.res_hz,
		   res_capa.min_tmo, res_capa.max_tmo);
	test_fatal_if(stat != EM_OK, "Given resolution is not supported (ret %d)\n", stat);

	if (!g_options.max_period_ns) {
		g_options.max_period_ns = DEF_MAX_PERIOD;
		if (g_options.max_period_ns > res_capa.max_tmo)
			g_options.max_period_ns = res_capa.max_tmo;
	}
	if (!g_options.min_period_ns) {
		g_options.min_period_ns = res_capa.res_ns * DEF_MIN_PERIOD;
		if (g_options.min_period_ns < res_capa.min_tmo)
			g_options.min_period_ns = res_capa.min_tmo;
	}

	if (g_options.info_only) { /* stop here */
		raise(SIGINT);
	} else {
		strncpy(attr.name, "TestTimer", EM_TIMER_NAME_LEN);
		attr.resparam = res_capa;
		if (g_options.res_hz) /* can only have one */
			attr.resparam.res_ns = 0;
		else
			attr.resparam.res_hz = 0;
		attr.num_tmo = g_options.num_periodic;
		attr.resparam.max_tmo = g_options.max_period_ns; /* don't need more */
		m_shm->test_tmr = em_timer_create(&attr);
		test_fatal_if(m_shm->test_tmr == EM_TIMER_UNDEF, "Failed to create test timer!");
		eo_ctx->test_tmr = m_shm->test_tmr;
		g_options.res_ns = attr.resparam.res_ns;
	}

	/* Start EO */
	stat = em_eo_start_sync(eo, NULL, NULL);
	test_fatal_if(stat != EM_OK, "Failed to start EO!");

	mlockall(MCL_FUTURE);
}

void
test_stop(appl_conf_t *const appl_conf)
{
	const int core = em_core_id();
	em_status_t ret;
	em_eo_t eo;

	if (appl_conf->num_procs > 1) {
		APPL_PRINT("%s(): skip\n", __func__);
		return;
	}

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	eo = em_eo_find(APP_EO_NAME);
	test_fatal_if(eo == EM_EO_UNDEF, "Could not find EO:%s", APP_EO_NAME);

	ret = em_eo_stop_sync(eo);
	test_fatal_if(ret != EM_OK, "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);
	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK, "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);

	ret = em_timer_delete(m_shm->hb_tmr);
	test_fatal_if(ret != EM_OK, "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      m_shm->hb_tmr, ret);
	if (m_shm->test_tmr != EM_TIMER_UNDEF) {
		ret = em_timer_delete(m_shm->test_tmr);
		test_fatal_if(ret != EM_OK, "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
			      m_shm->test_tmr, ret);
	}
	free(m_shm->eo_context.tmo_data);
}

void test_term(void)
{
	int core = em_core_id();

	APPL_PRINT("%s() on EM-core %d\n", __func__, core);

	if (m_shm != NULL) {
		em_unregister_error_handler();
		env_shared_free(m_shm);
		m_shm = NULL;
	}
}

static em_status_t app_eo_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
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

	if (g_options.info_only)
		return EM_OK;

	num_timers = em_timer_get_all(tmr, PRINT_MAX_TMRS);

	for (int i = 0;
	     i < (num_timers > PRINT_MAX_TMRS ? PRINT_MAX_TMRS : num_timers);
	     i++) {
		if (em_timer_get_attr(tmr[i], &attr) != EM_OK) {
			APPL_ERROR("Can't get timer info\n");
			return EM_ERR_BAD_ID;
		}
		APPL_PRINT("Timer \"%s\" info:\n", attr.name);
		APPL_PRINT("  -resolution: %" PRIu64 " ns\n", attr.resparam.res_ns);
		APPL_PRINT("  -max_tmo: %" PRIu64 " ms\n", attr.resparam.max_tmo / 1000);
		APPL_PRINT("  -num_tmo: %d\n", attr.num_tmo);
		APPL_PRINT("  -clk_src: %d\n", attr.resparam.clk_src);
		APPL_PRINT("  -tick Hz: %" PRIu64 " hz\n",
			   em_timer_get_freq(tmr[i]));
	}

	APPL_PRINT("\nActive run options:\n");
	APPL_PRINT(" num timers:   %d\n", g_options.num_periodic);
	if (g_options.res_hz) {
		APPL_PRINT(" resolution:   %lu Hz (%f MHz)\n", g_options.res_hz,
			   (double)g_options.res_hz / 1000000);
	} else {
		APPL_PRINT(" resolution:   %lu ns (%fs)\n", g_options.res_ns,
			   (double)g_options.res_ns / 1000000000);
	}
	if (g_options.period_ns == 0)
		APPL_PRINT(" period:       random\n");
	else
		APPL_PRINT(" period:       %lu ns (%fs%s)\n", g_options.period_ns,
			   (double)g_options.period_ns / 1000000000,
			   g_options.period_ns == 0 ? " (random)" : "");
	if (g_options.first_ns == -1)
		APPL_PRINT(" first period: random\n");
	else
		APPL_PRINT(" first period: %ld ns (%fs%s)\n", g_options.first_ns,
			   (double)g_options.first_ns / 1000000000,
			   g_options.first_ns == 0 ? " (=period)" : "");
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
		APPL_PRINT(" bg work:      %luus\n", g_options.bg_time_ns / 1000);
		APPL_PRINT(" bg data:      %ukiB\n", g_options.bg_size / 1024);
		APPL_PRINT(" bg chunk:     %ukiB (%u blks)\n",
			   g_options.bg_chunk / 1024,
			   g_options.bg_size / g_options.bg_chunk);
		APPL_PRINT(" bg trace:     %s\n", g_options.jobs ? "yes" : "no");

		eo_ctx->bg_data = malloc(g_options.bg_size);
		test_fatal_if(eo_ctx->bg_data == NULL,
			      "Can't allocate bg work data (%dkiB)!\n",
			      g_options.bg_size / 1024);
	}
	APPL_PRINT(" memzero:      ");
	if (g_options.mz_mb)
		APPL_PRINT("%uMB %severy %lums\n",
			   g_options.mz_mb,
			   g_options.mz_huge ? "(mmap huge) " : "",
			   g_options.mz_ns / 1000000UL);
	else
		APPL_PRINT("no\n");

	if (g_options.abort != 0) {
		APPL_PRINT(" abort after:  ");
		if (g_options.abort)
			APPL_PRINT("%d%s\n",
				   g_options.abort, g_options.abort < 0 ? "(segfault)" : "");
		else
			APPL_PRINT("0 (no)\n");
	}

	APPL_PRINT("\nTracing first %d tmo events\n", g_options.tracebuf);

	if (g_options.bg_events)
		prefault(eo_ctx->bg_data, g_options.bg_size);

	/* create periodic timeout for heartbeat */
	eo_ctx->heartbeat_tmo = em_tmo_create(m_shm->hb_tmr, EM_TMO_FLAG_PERIODIC, eo_ctx->hb_q);
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
		APPL_ERROR("WARNING: HB timer hz very low!\n");
	else
		APPL_PRINT("HB timer frequency is %lu\n", eo_ctx->hb_hz);

	period = eo_ctx->hb_hz; /* 1s */
	test_fatal_if(period < 1, "timer resolution is too low!\n");

	/* linux time check */
	test_fatal_if(clock_getres(CLOCK_MONOTONIC, &ts) != 0,
		      "clock_getres() failed!\n");

	period = ts.tv_nsec + (ts.tv_sec * 1000000000ULL);
	eo_ctx->linux_hz = 1000000000ULL / period;
	APPL_PRINT("Linux reports clock running at %" PRIu64 " hz\n", eo_ctx->linux_hz);
	APPL_PRINT("ODP says time_global runs at %luHz\n", odp_time_global_res());
	if (!g_options.cpucycles)
		eo_ctx->time_hz = odp_time_global_res();

	/* start heartbeat */
	__atomic_store_n(&eo_ctx->state, STATE_INIT, __ATOMIC_SEQ_CST);

	em_status_t stat = em_tmo_set_periodic(eo_ctx->heartbeat_tmo, 0, eo_ctx->hb_hz, event);

	if (EXTRA_PRINTS && stat != EM_OK)
		APPL_PRINT("FAILED to set HB tmo, stat=%d: period=%lu\n", stat, eo_ctx->hb_hz);
	test_fatal_if(stat != EM_OK, "Can't activate heartbeat tmo!\n");

	eo_ctx->test_hz = em_timer_get_freq(m_shm->test_tmr);
	test_fatal_if(eo_ctx->test_hz == 0,
		      "get_freq() failed, timer:%" PRI_TMR "", m_shm->test_tmr);

	stat = em_dispatch_register_enter_cb(enter_cb);
	test_fatal_if(stat != EM_OK, "enter_cb() register failed!");
	stat = em_dispatch_register_exit_cb(exit_cb);
	test_fatal_if(stat != EM_OK, "exit_cb() register failed!");

	srandom(time(NULL));
	if (g_options.max_work_ns > RAND_MAX ||
	    g_options.max_period_ns > RAND_MAX) {
		double s = (double)RAND_MAX / (double)eo_ctx->test_hz;

		APPL_PRINT("WARNING: rnd number range is less than max values (up to %.4fs)\n", s);
	}
	if (EXTRA_PRINTS)
		APPL_PRINT("WARNING: extra prints enabled, expect some jitter\n");

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

	if (EXTRA_PRINTS)
		APPL_PRINT("EO local start\n");
	test_fatal_if(core >= MAX_CORES, "Too many cores!");
	eo_ctx->cdat[core].trc = allocate_tracebuf(g_options.tracebuf, sizeof(tmo_trace),
						   &eo_ctx->cdat[core].trc_size);
	test_fatal_if(eo_ctx->cdat[core].trc == NULL, "Failed to allocate trace buffer!");
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

	if (EXTRA_PRINTS)
		APPL_PRINT("EO stop\n");

	if (eo_ctx->heartbeat_tmo != EM_TMO_UNDEF) {
		em_tmo_delete(eo_ctx->heartbeat_tmo, &event);
		eo_ctx->heartbeat_tmo = EM_TMO_UNDEF;
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	}

	/* cancel all test timers in case test didn't complete */
	int dcount = 0;

	for (int i = 0; i < g_options.num_periodic; i++) {
		if (eo_ctx->tmo_data[i].handle != EM_TMO_UNDEF) {
			event = EM_EVENT_UNDEF;
			em_tmo_delete(eo_ctx->tmo_data[i].handle, &event);
			eo_ctx->tmo_data[i].handle = EM_TMO_UNDEF;
			if (event != EM_EVENT_UNDEF)
				em_free(event);
			dcount++;
		}
	}
	if (dcount)
		APPL_PRINT("NOTE: deleted %d still active tmos\n", dcount);

	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE); /* remove and delete */
	test_fatal_if(ret != EM_OK,
		      "EO remove queue all:%" PRI_STAT " EO:%" PRI_EO "", ret, eo);

	ret = em_atomic_group_delete(((app_eo_ctx_t *)eo_context)->agrp);
	test_fatal_if(ret != EM_OK,
		      "EO remove atomic grp:%" PRI_STAT " EO:%" PRI_EO "", ret, eo);

	if (!g_options.info_only) {
		ret = em_dispatch_unregister_enter_cb(enter_cb);
		test_fatal_if(ret != EM_OK, "enter_cb() unregister:%" PRI_STAT, ret);
		ret = em_dispatch_unregister_exit_cb(exit_cb);
		test_fatal_if(ret != EM_OK, "exit_cb() unregister:%" PRI_STAT, ret);
	}

	if (eo_ctx->bg_data != NULL)
		free(eo_ctx->bg_data);
	eo_ctx->bg_data = NULL;
	if (eo_ctx->mz_data != NULL) {
		if (g_options.mz_huge)
			munmap(eo_ctx->mz_data, g_options.mz_mb * 1024UL * 1024UL);
		else
			free(eo_ctx->mz_data);

		eo_ctx->mz_data = NULL;
	}

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

	if (EXTRA_PRINTS)
		APPL_PRINT("EO local stop\n");
	free_tracebuf(eo_ctx->cdat[core].trc, eo_ctx->cdat[core].trc_size);
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
			e_state state = __atomic_load_n(&eo_ctx->state, __ATOMIC_ACQUIRE);

			/* only do this once */
			if (state == STATE_RUN && queue == eo_ctx->stop_q) {
				__atomic_store_n(&eo_ctx->state, STATE_COOLOFF, __ATOMIC_SEQ_CST);
				add_trace(eo_ctx, -1, OP_STATE, linux_time_ns(), STATE_COOLOFF);
				eo_ctx->last_hbcount = last_count;
				eo_ctx->stopped = get_time();
				APPL_PRINT("Core %d reported DONE\n", msgin->id);
			}
		}
		break;

		default:
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Invalid event!\n");
		}
	} else {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Invalid event type!\n");
	}

	if (!reuse)
		em_free(event);
}

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
