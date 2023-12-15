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
 * Event Machine timer ring test for alternative periodic timeouts.
 *
 * see instruction text at timer_test_ring.h.
 *
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
#include <signal.h>
#include <sys/mman.h>

#include <event_machine.h>
#include <event_machine/add-ons/event_machine_timer.h>
#include <odp_api.h>
#include <event_machine/platform/event_machine_odp_ext.h>

#include "cm_setup.h"
#include "cm_error_handler.h"
#include "timer_test_ring.h"

#define VERSION "WIP v0.3"
struct {
	unsigned int	loops;
	em_fract_u64_t  basehz[MAX_TEST_TIMERS];
	uint64_t	multiplier[MAX_TEST_TIMERS];
	uint64_t	res_ns[MAX_TEST_TIMERS];
	uint64_t	max_mul[MAX_TEST_TMO];
	uint64_t	start_offset[MAX_TEST_TMO];
	unsigned int	num_timers;
	unsigned int	looptime;
	bool		recreate;
	bool		reuse_tmo;
	bool		reuse_ev;
	bool		profile;
	unsigned int	num_tmo;
	int64_t		delay_us;
	em_timer_clksrc_t clksrc;
	unsigned int	tracelen;
	char		tracefile[MAX_FILENAME];

} g_options = {
	.loops =	1,
	.basehz =	{ {100, 0, 0 } },	/* per timer */
	.multiplier =	{ 1 },			/* per timerout */
	.res_ns =	{ 0 },			/* per timer */
	.max_mul =	{ 8 },			/* per timer */
	.start_offset =	{ 0 },			/* per timeout */
	.num_timers =	1,
	.looptime =	30,
	.recreate =	false,
	.reuse_tmo =	false,
	.reuse_ev =	false,
	.profile =	false,
	.num_tmo =	1,
	.delay_us =	0,
	.clksrc =	0,
	.tracelen =	0,
	.tracefile =	"stdout"
};

static timer_app_shm_t *m_shm;
static odp_shm_t odp_shm;
static odp_shm_t odp_shm_trace;
static __thread trace_entry_t *m_tracebuf;
static __thread unsigned int m_tracecount;
odp_ticketlock_t tracelock;

/* --------------------------------------- */
static void usage(void);
static int parse_my_args(int first, int argc, char *argv[]);
static em_status_t app_eo_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf);
static em_status_t app_eo_start_local(void *eo_context, em_eo_t eo);
static em_status_t app_eo_stop(void *eo_context, em_eo_t eo);
static em_status_t app_eo_stop_local(void *eo_context, em_eo_t eo);
static void app_eo_receive(void *eo_context, em_event_t event,
			   em_event_type_t type, em_queue_t queue, void *q_context);
static em_status_t my_error_handler(em_eo_t eo, em_status_t error,
				    em_escope_t escope, va_list args);
static bool handle_heartbeat(app_eo_ctx_t *eo_ctx, em_event_t event,
			     app_msg_t *msgin, uint64_t now);
static bool handle_tmo(app_eo_ctx_t *eo_ctx, em_event_t event, uint64_t now);
static void analyze_and_print(app_eo_ctx_t *eo_ctx, int loop);
static void global_summary(app_eo_ctx_t *eo_ctx);
static void print_setup(void);
static void restart(app_eo_ctx_t *eo_ctx, int count);
static void delete_test_timer(app_eo_ctx_t *eo_ctx);
static void create_test_timer(app_eo_ctx_t *eo_ctx);
static int split_list(char *str, uint64_t *list, int maxnum);
static int split_float_list(char *str, em_fract_u64_t *list, int maxnum);
static void approx_fract(double f, em_fract_u64_t *fract);
static void fix_setup(void);
static void create_test_timeouts(app_eo_ctx_t *eo_ctx);
static void delete_test_timeouts(app_eo_ctx_t *eo_ctx, bool force);
static void delete_test_events(app_eo_ctx_t *eo_ctx, bool force);
static void dump_trace(app_eo_ctx_t *eo_ctx);
static void enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
		     em_queue_t *queue, void **q_ctx);
static void exit_cb(em_eo_t eo);
static void extra_delay(rnd_state_t *rnd, int core, unsigned int tmri, unsigned int toi);

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

static void enter_cb(em_eo_t eo, void **eo_ctx, em_event_t events[], int num,
		     em_queue_t *queue, void **q_ctx)
{
	app_eo_ctx_t *const my_eo_ctx = *eo_ctx;
	int core = em_core_id();

	(void)eo;
	(void)queue;
	(void)q_ctx;
	(void)events;
	(void)num;

	if (unlikely(!my_eo_ctx))
		return;

	my_eo_ctx->cdat[core].enter_ns = TEST_TIME_FN();
	if (likely(my_eo_ctx->cdat[core].exit_ns))
		my_eo_ctx->cdat[core].non_eo_ns += my_eo_ctx->cdat[core].enter_ns -
						   my_eo_ctx->cdat[core].exit_ns;
}

static void exit_cb(em_eo_t eo)
{
	app_eo_ctx_t *const my_eo_ctx = em_eo_get_context(eo);
	int core = em_core_id();

	if (unlikely(!my_eo_ctx))
		return;

	my_eo_ctx->cdat[core].exit_ns = TEST_TIME_FN();
	my_eo_ctx->cdat[core].eo_ns += my_eo_ctx->cdat[core].exit_ns -
				       my_eo_ctx->cdat[core].enter_ns;
}

static inline double frac2float(em_fract_u64_t frac)
{
	double f = frac.integer;

	if (frac.numer)
		f += (double)frac.numer / (double)frac.denom;
	return f;
}

static inline void *tmo2ptr(unsigned int tmr, unsigned int tmo)
{
	return (void *)(((uint64_t)tmr << 16) + tmo);
}

static inline void ptr2tmo(void *ptr, unsigned int *tmr, unsigned int *tmo)
{
	uint64_t x = (uint64_t)ptr;

	*tmo = x & 0xFFFF;
	*tmr = x >> 16;
}

static inline void profile_add(uint64_t t1, uint64_t t2, prof_apis api,
			       app_eo_ctx_t *eo_ctx, int core)
{
	uint64_t diff = t2 - t1;

	if (eo_ctx->cdat[core].prof[api].min > diff)
		eo_ctx->cdat[core].prof[api].min = diff;
	if (eo_ctx->cdat[core].prof[api].max < diff)
		eo_ctx->cdat[core].prof[api].max = diff;
	eo_ctx->cdat[core].prof[api].acc += diff;
	eo_ctx->cdat[core].prof[api].num++;
}

static inline void trace_add(uint64_t ts, trace_op_t op, uint32_t val,
			     int64_t arg1, int64_t arg2, void *arg3, void *arg4)
{
	trace_entry_t *tp;

	if (g_options.tracelen == 0)
		return; /* disabled */

	if (unlikely(m_tracecount >= g_options.tracelen)) { /* overflow marker */
		tp = &m_tracebuf[g_options.tracelen - 1];
		tp->ns = TEST_TIME_FN();
		tp->op = TRACE_OP_LAST;
		tp->val = val;
		tp->arg1 = arg1;
		tp->arg2 = arg2;
		tp->arg3 = arg3;
		tp->arg4 = arg4;
		return;
	}

	tp = &m_tracebuf[m_tracecount];
	tp->ns = ts;
	tp->op = op;
	tp->val = val;
	tp->arg1 = arg1;
	tp->arg2 = arg2;
	tp->arg3 = arg3;
	tp->arg4 = arg4;

	m_tracecount++;
}

static void extra_delay(rnd_state_t *rnd, int core, unsigned int tmri, unsigned int toi)
{
	uint64_t t1 = TEST_TIME_FN();
	uint64_t ns;

	if (g_options.delay_us < 0) { /* random */
		int32_t r1;

		random_r(&rnd->rdata, &r1);
		ns = (uint64_t)r1 % (1000 * (labs(g_options.delay_us) + 1));
	} else {
		ns = g_options.delay_us * 1000UL;
	}

	trace_add(TEST_TIME_FN(), TRACE_OP_DELAY, core, tmri, toi, (void *)ns, NULL);
	while (TEST_TIME_FN() < (ns + t1)) {
		/* delay */
	};
}

static void dump_trace(app_eo_ctx_t *eo_ctx)
{
	static bool title = true; /* header once */

	if (g_options.tracelen == 0)
		return;

	FILE *df = stdout;

	if (strcmp(g_options.tracefile, "stdout"))
		df = fopen(g_options.tracefile, title ? "w" : "a");

	if (!df) {
		APPL_PRINT("Failed to open dump file!\n");
		return;
	}

	if (title) {
		fprintf(df, "#BEGIN RING TRACE FORMAT 1\n");
		/* dump setup */
		fprintf(df, "cores,loops,num_timer,num_tmo,recreate_tmr,reuse_tmo,reuse_ev,delay_us,tracelen,ver\n");
		fprintf(df, "%d,%u,%u,%u,%u,%u,%u,%ld,%u,%s\n",
			em_core_count(), g_options.loops, g_options.num_timers, g_options.num_tmo,
					 g_options.recreate, g_options.reuse_tmo,
					 g_options.reuse_ev, g_options.delay_us,
					 g_options.tracelen, VERSION);
		/* dump timeouts */
		fprintf(df, "#TMO:\ntmr,tmo,tick_hz,res_ns,base_hz,mul,startrel\n");
		for (unsigned int tmr = 0; tmr < g_options.num_timers; tmr++)
			for (unsigned int tmo = 0; tmo < g_options.num_tmo; tmo++) {
				fprintf(df, "%u,%u,%lu,%lu,%f,%lu,%lu\n",
					tmr, tmo, eo_ctx->tick_hz[tmr], g_options.res_ns[tmr],
					frac2float(g_options.basehz[tmr]),
					g_options.multiplier[tmo], g_options.start_offset[tmo]);
			}

		/* and then the trace events */
		fprintf(df, "#EVENTS:\n");
		fprintf(df, "core,ns,op,arg1,arg2,arg3,arg4\n");
	}

	for (uint32_t count = 0; count < m_tracecount; count++) {
		trace_entry_t *tp = &m_tracebuf[count];

		test_fatal_if(tp->op > TRACE_OP_LAST, "Invalid trace op %u!", tp->op);

		fprintf(df, "%u,%lu,%s,%ld,%ld,%p,%p\n",
			tp->val, tp->ns, trace_op_labels[tp->op],
			tp->arg1, tp->arg2, tp->arg3, tp->arg4);
	}

	if (df != stdout)
		fclose(df);
	title = false;
}

static void usage(void)
{
	printf("%s\n", instructions);

	printf("Options:\n");
	for (int i = 0; ; i++) {
		if (longopts[i].name == NULL || descopts[i] == NULL)
			break;
		printf("--%s or -%c: %s\n", longopts[i].name, longopts[i].val, descopts[i]);
	}
}

static void print_timers(void)
{
	em_timer_attr_t attr;
	em_timer_t tmr[PRINT_MAX_TMRS];

	int num_timers = em_timer_get_all(tmr, PRINT_MAX_TMRS);

	for (int i = 0; i < (num_timers > PRINT_MAX_TMRS ? PRINT_MAX_TMRS : num_timers); i++) {
		test_fatal_if(em_timer_get_attr(tmr[i], &attr) != EM_OK, "Can't get timer info\n");

		APPL_PRINT("Timer \"%s\" info:\n", attr.name);
		APPL_PRINT("  -resolution: %" PRIu64 " ns\n", attr.resparam.res_ns);
		if (!(attr.flags & EM_TIMER_FLAG_RING))
			APPL_PRINT("  -max_tmo: %" PRIu64 " ms\n", attr.resparam.max_tmo / 1000);
		APPL_PRINT("  -num_tmo: %d\n", attr.num_tmo);
		APPL_PRINT("  -clk_src: %d\n", attr.resparam.clk_src);
		APPL_PRINT("  -tick Hz: %" PRIu64 " hz\n", em_timer_get_freq(tmr[i]));
		APPL_PRINT("  -is ring: ");
		if (attr.flags & EM_TIMER_FLAG_RING) {
			double hz = frac2float(attr.ringparam.base_hz);

			APPL_PRINT(" yes (base_hz %.3f, max_mul %lu)\n",
				   hz, attr.ringparam.max_mul);
		} else {
			APPL_PRINT("no\n");
		}
	}
}

/* adjust timer setup basehz,mul to same lengths */
static void fix_setup(void)
{
	for (unsigned int i = 1; i < g_options.num_timers; i++) {
		if (g_options.basehz[i].integer == 0) {
			g_options.basehz[i].integer = 100;
			g_options.basehz[i].numer = 0;
		}
		if (g_options.max_mul[i] == 0)
			g_options.max_mul[i] = 8;
		/* res 0 is ok = default */
	}
	for (unsigned int i = 1; i < g_options.num_tmo; i++) {
		if (g_options.multiplier[i] == 0)
			g_options.multiplier[i] = 1;
	}

	if (g_options.recreate && g_options.reuse_tmo) {
		APPL_PRINT("\nWARNING: Can't recreate timers AND re-use tmo, re-use disabled\n");
		g_options.reuse_tmo = false;
	}
}

/* separate comma limited integer argument */
static int split_list(char *str, uint64_t *list, int maxnum)
{
	int num = 0;
	char *p = strtok(str, ",");

	while (p) {
		list[num] = (uint64_t)atoll(p);
		num++;
		if (num >= maxnum)
			break;
		p = strtok(NULL, ",");
	}

	return num;
}

/* this could be better, but for now just use fixed point to 100th */
static void approx_fract(double val, em_fract_u64_t *fract)
{
	double intp;
	double p = modf(val, &intp);

	fract->numer = round(100 * p);
	fract->denom = 100;
}

/* separate comma limited float argument */
static int split_float_list(char *str, em_fract_u64_t *list, int maxnum)
{
	int num = 0;
	char *p = strtok(str, ",");

	while (p) {
		list[num].integer = (uint64_t)atoll(p);
		approx_fract(atof(p), &list[num]);
		num++;
		if (num >= maxnum)
			break;
		p = strtok(NULL, ",");
	}

	return num;
}

static int parse_my_args(int first, int argc, char *argv[])
{
	optind = first + 1; /* skip '--' */
	while (1) {
		int opt;
		int long_index;
		char *endptr;
		long num;

		opt = getopt_long(argc, argv, shortopts, longopts, &long_index);

		if (opt == -1)
			break;  /* No more options */

		switch (opt) {
		case 'b': {
			uint64_t hz[MAX_TEST_TIMERS];

			num = split_list(optarg, hz, MAX_TEST_TIMERS);
			if (num < 1)
				return 0;
			for (int i = 0; i < num; i++) {
				g_options.basehz[i].integer = hz[i];
				g_options.basehz[i].numer = 0;
			}
			if (num > g_options.num_timers)
				g_options.num_timers = num;
		}
		break;

		case 'f':
			num = split_float_list(optarg, &g_options.basehz[0], MAX_TEST_TIMERS);
			if (num < 1)
				return 0;
			if (num > g_options.num_timers)
				g_options.num_timers = num;
		break;

		case 'l':
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.loops = (unsigned int)num;
		break;

		case 't':
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.looptime = (int)num;
		break;

		case 'c':
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.clksrc = (em_timer_clksrc_t)num;
		break;

		case 'n':
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 1)
				return 0;
			g_options.num_tmo = (unsigned int)num;
		break;

		case 'T':
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0' || num < 0)
				return 0;
			g_options.tracelen = (unsigned int)num;
		break;

		case 'd':
			num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0')
				return 0;
			g_options.delay_us = (int64_t)num;
		break;

		case 'w': { /* optional arg */
			if (optarg != NULL) {
				if (strlen(optarg) >= MAX_FILENAME)
					return 0;
				strncpy(g_options.tracefile, optarg, MAX_FILENAME);
			}
		}
		break;

		case 'r':
			num = split_list(optarg, &g_options.res_ns[0], MAX_TEST_TIMERS);
			if (num < 1)
				return 0;
			if (num > g_options.num_timers)
				g_options.num_timers = num;
		break;

		case 'm':
			num = split_list(optarg, &g_options.multiplier[0], MAX_TEST_TMO);
			if (num < 1)
				return 0;
		break;

		case 'o':
			num = split_list(optarg, &g_options.start_offset[0], MAX_TEST_TMO);
			if (num < 1)
				return 0;
		break;

		case 'M':
			num = split_list(optarg, &g_options.max_mul[0], MAX_TEST_TIMERS);
			if (num < 1)
				return 0;
			if (num > g_options.num_timers)
				g_options.num_timers = num;
		break;

		case 'R':
			g_options.recreate = true;
		break;

		case 'a':
			g_options.profile = true;
		break;

		case 'N':
			g_options.reuse_tmo = true;
		break;

		case 'E':
			g_options.reuse_ev = true;
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

void print_setup(void)
{
	APPL_PRINT("\nActive run options:\n");
	APPL_PRINT(" - loops:          %d\n", g_options.loops);
	APPL_PRINT(" - looptime:       %d s\n", g_options.looptime);
	APPL_PRINT(" - num timers:     %d\n", g_options.num_timers);
	APPL_PRINT(" - tmo per timer:  %d\n", g_options.num_tmo);
	APPL_PRINT(" - recreate timer: %s\n", g_options.recreate ? "yes" : "no");
	APPL_PRINT(" - reuse tmo:      %s\n", g_options.reuse_tmo ? "yes" : "no");
	APPL_PRINT(" - reuse event:    %s\n", g_options.reuse_ev ? "yes" : "no");
	if (g_options.tracelen) {
		APPL_PRINT(" - tracebuf:       %u\n", g_options.tracelen);
		APPL_PRINT(" - tracefile:      %s\n", g_options.tracefile);
	}
	APPL_PRINT(" - profile APIs:   %s\n", g_options.profile ? "yes" : "no");
	APPL_PRINT(" - extra delay:    %ld us %s\n", labs(g_options.delay_us),
		   g_options.delay_us < 0 ? "(rnd)" : "");

	APPL_PRINT("\nTimer tmo  basehz          max_mul   res_ns         startrel     mul    ->hz\n");

	for (unsigned int i = 0; i < g_options.num_timers; i++) {
		double hz = frac2float(g_options.basehz[i]);

		for (unsigned int t = 0; t < g_options.num_tmo; t++) {
			APPL_PRINT("%-5u %-4u %-15.3f %-9lu %-14lu %-12lu %-8lu %.3f\n",
				   i, t, hz, g_options.max_mul[i],
				   g_options.res_ns[i], g_options.start_offset[t],
				   g_options.multiplier[t], hz * (double)g_options.multiplier[t]);
		}
	}
	APPL_PRINT("\n");
}

static void delete_test_timer(app_eo_ctx_t *eo_ctx)
{
	for (unsigned int i = 0; i < g_options.num_timers; i++) {
		if (eo_ctx->test_tmr[i] != EM_TIMER_UNDEF) {
			trace_add(TEST_TIME_FN(), TRACE_OP_TMR_DELETE, em_core_id(),
				  i, -1, NULL, eo_ctx->test_tmr[i]);
			em_status_t rv = em_timer_delete(eo_ctx->test_tmr[i]);

			test_fatal_if(rv != EM_OK, "Ring timer[%d] delete fail, rv %d!", i, rv);
			APPL_PRINT("Deleted test timer[%d]: %p\n", i, eo_ctx->test_tmr[i]);
			eo_ctx->test_tmr[i] = EM_TIMER_UNDEF;
		}
	}
}

static void create_test_timer(app_eo_ctx_t *eo_ctx)
{
	em_timer_attr_t rattr;

	for (unsigned int i = 0; i < g_options.num_timers; i++) {
		em_status_t stat = em_timer_ring_attr_init(&rattr,
							   g_options.clksrc,
							   g_options.basehz[i].integer,
							   g_options.max_mul[i],
							   g_options.res_ns[i]);

		if (g_options.basehz[i].numer) {
			rattr.ringparam.base_hz.numer = g_options.basehz[i].numer;
			rattr.ringparam.base_hz.denom = g_options.basehz[i].denom;
		}
		if (EXTRA_PRINTS) {
			APPL_PRINT("\nInitialized ring attr:\n");
			APPL_PRINT(" -clksrc:  %u\n", g_options.clksrc);
			APPL_PRINT(" -num_tmo: %u\n", rattr.num_tmo);
			APPL_PRINT(" -base_hz: %" PRIu64 "\n", rattr.ringparam.base_hz.integer);
			APPL_PRINT(" -base_hz n/d: %lu/%lu\n", rattr.ringparam.base_hz.numer,
				   rattr.ringparam.base_hz.denom);
			APPL_PRINT(" -max_mul: %lu\n", rattr.ringparam.max_mul);
			APPL_PRINT(" -res_ns:  %lu\n", rattr.ringparam.res_ns);
		}

		if (stat != EM_OK) {
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Ring parameters not supported, ret %u!", stat);
		}

		if (g_options.basehz[i].numer) { /* re-check values */
			em_timer_ring_param_t ring = rattr.ringparam;

			if (em_timer_ring_capability(&rattr.ringparam) == EM_ERR_NOT_SUPPORTED) {
				APPL_PRINT("WARN: Arguments not exactly supported:\n");

				APPL_PRINT("base_hz:      %lu %lu/%lu -> %lu %lu/%lu\n",
					   ring.base_hz.integer, ring.base_hz.numer,
					   ring.base_hz.denom, rattr.ringparam.base_hz.integer,
					   rattr.ringparam.base_hz.numer,
					   rattr.ringparam.base_hz.denom);
				APPL_PRINT("max_mul:      %lu -> %lu\n",
					   ring.max_mul, rattr.ringparam.max_mul);
				APPL_PRINT("res_ns:       %lu -> %lu\n",
					   ring.res_ns, rattr.ringparam.res_ns);
			}
		}
		strncpy(rattr.name, "RingTmr", EM_TIMER_NAME_LEN);
		em_timer_t rtmr = em_timer_ring_create(&rattr);

		trace_add(TEST_TIME_FN(), TRACE_OP_TMR_CREATE, em_core_id(), i, -1, NULL, rtmr);
		test_fatal_if(rtmr == EM_TIMER_UNDEF, "Ring timer create fail!");
		eo_ctx->test_tmr[i] = rtmr;
		eo_ctx->tick_hz[i] = em_timer_get_freq(rtmr);
		if (EXTRA_PRINTS)
			APPL_PRINT("Created test timer[%d]: %p\n", i, rtmr);
	} /* next timer */
}

static void create_test_timeouts(app_eo_ctx_t *eo_ctx)
{
	/* create test timeout(s) */
	for (unsigned int t = 0; t < g_options.num_timers; t++) {
		for (unsigned int to = 0; to < g_options.num_tmo; to++) {
			em_tmo_args_t args = { .userptr = tmo2ptr(t, to) };

			if (eo_ctx->test_tmo[t][to] == EM_TMO_UNDEF) {
				uint64_t t1 = TEST_TIME_FN();

				eo_ctx->test_tmo[t][to] = em_tmo_create_arg(eo_ctx->test_tmr[t],
									    EM_TMO_FLAG_PERIODIC,
									    eo_ctx->test_q,
									    &args);
				profile_add(t1, TEST_TIME_FN(), PROF_TMO_CREATE,
					    eo_ctx, em_core_id());
				trace_add(t1, TRACE_OP_TMO_CREATE, em_core_id(),
					  t, to, eo_ctx->test_tmr[t], eo_ctx->test_tmo[t][to]);
				test_fatal_if(eo_ctx->test_tmo[t][to] == EM_TMO_UNDEF,
					      "Can't allocate test_tmo!\n");
			}

			uint64_t tick_now = em_timer_current_tick(eo_ctx->test_tmr[t]);
			uint64_t t1 = TEST_TIME_FN();
			uint64_t startabs = 0;

			if (g_options.start_offset[to])
				startabs = tick_now + g_options.start_offset[to];
			trace_add(t1, TRACE_OP_TMO_SET, em_core_id(),
				  t, to, (void *)tick_now, eo_ctx->test_ev[t][to]);
			t1 = TEST_TIME_FN();
			em_status_t stat = em_tmo_set_periodic_ring(eo_ctx->test_tmo[t][to],
								    startabs,
								    g_options.multiplier[to],
								    eo_ctx->test_ev[t][to]);

			profile_add(t1, TEST_TIME_FN(), PROF_TMO_SET, eo_ctx, em_core_id());
			test_fatal_if(stat != EM_OK, "Can't activate test tmo[%d][%d], ret %u!\n",
				      t, to, stat);
			eo_ctx->first_time[t][to] = t1;
			eo_ctx->test_ev[t][to] = EM_EVENT_UNDEF; /* now given to timer */
		}
	}
}

static void delete_test_timeouts(app_eo_ctx_t *eo_ctx, bool force)
{
	int core = em_core_id();

	/* force == true means final cleanup, otherwise may skip if re-use option is active */

	for (unsigned int ti = 0; ti < g_options.num_timers; ti++) {
		for (unsigned int tmoi = 0; tmoi < g_options.num_tmo; tmoi++) {
			if (eo_ctx->test_tmo[ti][tmoi] == EM_TMO_UNDEF)
				continue;

			em_tmo_state_t s = em_tmo_get_state(eo_ctx->test_tmo[ti][tmoi]);

			test_fatal_if(s == EM_TMO_STATE_ACTIVE,
				      "Unexpected tmo state ACTIVE after cancel\n");

			if (!g_options.reuse_tmo || force) {
				em_event_t ev = EM_EVENT_UNDEF;
				uint64_t t1 = TEST_TIME_FN();

				trace_add(t1, TRACE_OP_TMO_DELETE, core,
					  ti, tmoi, NULL, eo_ctx->test_tmo[ti][tmoi]);

				em_status_t rv = em_tmo_delete(eo_ctx->test_tmo[ti][tmoi], &ev);

				profile_add(t1, TEST_TIME_FN(), PROF_TMO_DELETE, eo_ctx, core);
				test_fatal_if(rv != EM_OK, "tmo_delete fail, tmo = %p!",
					      eo_ctx->test_tmo[ti][tmoi]);
				test_fatal_if(ev != EM_EVENT_UNDEF,
					      "Unexpected - tmo delete returned event %p", ev);
				eo_ctx->test_tmo[ti][tmoi] = EM_TMO_UNDEF;
			}
		}
	}
}

static void delete_test_events(app_eo_ctx_t *eo_ctx, bool force)
{
	int core = em_core_id();

	for (unsigned int ti = 0; ti < g_options.num_timers; ti++) {
		for (unsigned int tmoi = 0; tmoi < g_options.num_tmo; tmoi++) {
			if (eo_ctx->test_ev[ti][tmoi] == EM_EVENT_UNDEF)
				continue;
			if (!g_options.reuse_ev || force) {
				trace_add(TEST_TIME_FN(), TRACE_OP_TMO_EV_FREE,
					  core, ti, tmoi, eo_ctx->test_tmo[ti][tmoi],
					  eo_ctx->test_ev[ti][tmoi]);
				em_free(eo_ctx->test_ev[ti][tmoi]);
				eo_ctx->test_ev[ti][tmoi] = EM_EVENT_UNDEF;
			}
		}
	}
}

static void restart(app_eo_ctx_t *eo_ctx, int count)
{
	if (g_options.recreate)
		create_test_timer(eo_ctx);

	/* clear event counts, leave profiles */
	for (int c = 0; c < em_core_count(); c++)
		for (unsigned int t = 0; t < g_options.num_timers; t++)
			for (unsigned int to = 0; to < g_options.num_tmo; to++)
				eo_ctx->cdat[c].count[t][to] = 0;

	eo_ctx->state = STATE_START;
	eo_ctx->next_change = count + 2;
}

static bool handle_heartbeat(app_eo_ctx_t *eo_ctx, em_event_t event, app_msg_t *msgin, uint64_t now)
{
	static unsigned int loops;
	em_event_t ev = EM_EVENT_UNDEF;

	(void)eo_ctx;
	(void)now;

	trace_add(now, TRACE_OP_HB_RX, em_core_id(), msgin->count, eo_ctx->state, NULL, event);

	if (EXTRA_PRINTS)
		APPL_PRINT(".");

	msgin->count++;
	if (msgin->count >= eo_ctx->next_change) { /* time to do something */
		/* State machine for test cycle (loop). Runs on heartbeat timeout every second.
		 * Some time is added between states so startup, printing etc is not causing jitter
		 * to time stamping
		 */
		int state = eo_ctx->state;

		switch (state) {
		case STATE_START:
			if (loops == 0) {
				create_test_timer(eo_ctx);
				print_timers();
			}
			if (EXTRA_PRINTS)
				APPL_PRINT("START\n");

			/* start */
			eo_ctx->start_time = TEST_TIME_FN();
			eo_ctx->state++; /* atomic, go to RUN */
			create_test_timeouts(eo_ctx);
			eo_ctx->next_change = msgin->count + g_options.looptime;
		break;

		case STATE_RUN:
			eo_ctx->state++; /* go to STOP */
			for (unsigned int ti = 0; ti < g_options.num_timers; ti++) {
				for (unsigned int tmoi = 0; tmoi < g_options.num_tmo; tmoi++) {
					em_status_t rv;
					uint64_t t1 = TEST_TIME_FN();

					rv = em_tmo_cancel(eo_ctx->test_tmo[ti][tmoi], &ev);
					profile_add(t1, TEST_TIME_FN(), PROF_TMO_CANCEL,
						    eo_ctx, em_core_id());
					trace_add(t1, TRACE_OP_TMO_CANCEL, em_core_id(),
						  ti, tmoi, ev, eo_ctx->test_tmo[ti][tmoi]);
					test_fatal_if(rv != EM_ERR_TOONEAR,
						      "cancel did not return expected TOONEAR!");
				}
			}
			eo_ctx->next_change = msgin->count + 3;  /* enough to get all remaining */
			eo_ctx->stop_time = TEST_TIME_FN();
		break;

		case STATE_STOP:
			if (EXTRA_PRINTS)
				APPL_PRINT("\nSTOP\n");
			delete_test_timeouts(eo_ctx, false);
			delete_test_events(eo_ctx, false);
			eo_ctx->state++;  /* go to ANALYZE */
		break;

		case STATE_ANALYZE:
			loops++;
			APPL_PRINT("\n\nLoop completed\n");
			analyze_and_print(eo_ctx, loops);

			if (loops >= g_options.loops) { /* all done, cleanup and summary */
				em_status_t rv = em_tmo_cancel(eo_ctx->heartbeat_tmo, &ev);

				test_fatal_if(rv != EM_OK && rv != EM_ERR_TOONEAR, "HB cncl fail");
				test_fatal_if(ev != EM_EVENT_UNDEF,
					      "not expecting event on cancel (at receive)");
				eo_ctx->state++;  /* go to EXIT next */
				delete_test_timeouts(eo_ctx, true);
				delete_test_events(eo_ctx, true);

				global_summary(eo_ctx);

				APPL_PRINT("Done, raising SIGINT!\n");
				trace_add(TEST_TIME_FN(), TRACE_OP_SIGINT, em_core_id(),
					  loops, -1, NULL, NULL);
				raise(SIGINT);
				return false;
			}
			/* next loop, re-start */
			if (g_options.recreate)
				delete_test_timer(eo_ctx);
			restart(eo_ctx, msgin->count);
		break;

		case STATE_EXIT:
			if (EXTRA_PRINTS)
				APPL_PRINT("EXIT\n");
			return false; /* don't ack anymore */

		default:
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "State invalid! %d\n", eo_ctx->state);
		}
	}

	trace_add(TEST_TIME_FN(), TRACE_OP_TMO_ACK, em_core_id(), -1, -1, NULL, event);

	em_status_t stat = em_tmo_ack(msgin->tmo, event);

	if (stat == EM_ERR_CANCELED)
		return false; /* free event */
	if (stat != EM_OK)
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "HB ack failed, ret %u, count %ld", stat, msgin->count);
	return true;
}

static bool handle_tmo(app_eo_ctx_t *eo_ctx, em_event_t event, uint64_t now)
{
	(void)eo_ctx;
	(void)now;

	em_tmo_t tmo;
	unsigned int tmri, tmoi;
	int core = em_core_id();

	test_fatal_if(em_tmo_get_type(event, &tmo, false) != EM_TMO_TYPE_PERIODIC, "not a TMO?!");

	/* event has no user specific content, userptr holds encoded indexes */
	ptr2tmo(em_tmo_get_userptr(event, NULL), &tmri, &tmoi);
	test_fatal_if(tmri >= MAX_TEST_TIMERS || tmoi >= MAX_TEST_TMO,
		      "Too large index, event corrupted?");
	test_fatal_if(tmo != eo_ctx->test_tmo[tmri][tmoi],
		      "tmo handle [%u][%u] does not match expected %p->%p\n",
		      tmri, tmoi, eo_ctx->test_tmo[tmri][tmoi], tmo);

	/* use passed rx timestamp for better accuracy. Could still improve by debug timestamps */
	uint64_t tick = em_timer_current_tick(eo_ctx->test_tmr[tmri]);

	trace_add(now, TRACE_OP_TMO_RX, core, tmri, tmoi, (void *)tick, event);
	eo_ctx->cdat[core].count[tmri][tmoi]++;
	trace_add(TEST_TIME_FN(), TRACE_OP_TMO_ACK, core, tmri, tmoi, tmo, event);

	em_status_t stat;
	uint64_t t1 = TEST_TIME_FN();

	stat = em_tmo_ack(tmo, event);
	profile_add(t1, TEST_TIME_FN(), PROF_TMO_ACK, eo_ctx, core);
	if (stat == EM_ERR_CANCELED) { /* last event */
		trace_add(TEST_TIME_FN(), TRACE_OP_TMO_ACK_LAST, core, tmri, tmoi, tmo, event);
		eo_ctx->last_time[tmri][tmoi] = now;
		if (EXTRA_PRINTS)
			APPL_PRINT("last timeout[%u][%u]\n", tmri, tmoi);
		if (g_options.reuse_ev) {
			eo_ctx->test_ev[tmri][tmoi] = event; /* event for tmo re-start */
			return true; /* don't free in receive */
		}
		return false; /* now allowed to free */
	}

	test_fatal_if(stat != EM_OK, "Test tmo[%u][%u] ack returned %u!\n", tmri, tmoi, stat);

	if (g_options.delay_us != 0)
		extra_delay(&eo_ctx->cdat[core].rnd, core, tmri, tmoi);

	return true;
}

static void global_summary(app_eo_ctx_t *eo_ctx)
{
	int cores = em_core_count();

	APPL_PRINT("\nGLOBAL SUMMARY:\n");

	if (g_options.profile) {
		APPL_PRINT("\nTiming profiles:\n");
		APPL_PRINT("api             count           min             max             avg (ns)\n");
		APPL_PRINT("------------------------------------------------------------------------\n");
		for (int p = 0; p < PROF_TMO_LAST; p++) {
			prof_t pdat = { 0 };

			pdat.min = UINT64_MAX;
			for (int c = 0; c < cores; c++) {
				if (eo_ctx->cdat[c].prof[p].min < pdat.min)
					pdat.min = eo_ctx->cdat[c].prof[p].min;
				if (eo_ctx->cdat[c].prof[p].max > pdat.max)
					pdat.max = eo_ctx->cdat[c].prof[p].max;
				pdat.num += eo_ctx->cdat[c].prof[p].num;
				pdat.acc += eo_ctx->cdat[c].prof[p].acc;
			}
			if (pdat.num == 0)
				continue;
			APPL_PRINT("%-15s %-15lu %-15lu %-15lu %-15lu\n",
				   prof_names[p], pdat.num, pdat.min,
				   pdat.max, pdat.acc / pdat.num);
		}
	}

	APPL_PRINT("\ncore   EO utilization\n");
	APPL_PRINT("---------------------\n");
	for (int c = 0; c < cores; c++) {
		double load = (double)eo_ctx->cdat[c].eo_ns /
			      (double)(eo_ctx->cdat[c].non_eo_ns + eo_ctx->cdat[c].eo_ns);

		APPL_PRINT("%-7d%.2f %%\n", c, load * 100);
	}

	/* more analysis from e.g. trace data could be implemented here */

	APPL_PRINT("\n");
}

static void analyze_and_print(app_eo_ctx_t *eo_ctx, int loop)
{
	APPL_PRINT("Analysis for loop %u :\n", loop);

	int cores = em_core_count();
	uint64_t counts[MAX_TEST_TIMERS][MAX_TEST_TMO];

	memset(counts, 0, sizeof(counts));
	for (int i = 0; i < cores ; i++)
		for (unsigned int t = 0; t < g_options.num_timers; t++)
			for (unsigned int to = 0; to < g_options.num_tmo; to++)
				counts[t][to] += eo_ctx->cdat[i].count[t][to];

	uint64_t total = 0;

	APPL_PRINT("tmr   tmo   secs       tmos       ->hz           setup_hz     error %%\n");
	APPL_PRINT("---------------------------------------------------------------------\n");
	for (unsigned int t = 0; t < g_options.num_timers; t++) {
		for (unsigned int to = 0; to < g_options.num_tmo; to++) {
			int64_t ttime = (int64_t)eo_ctx->last_time[t][to] -
					(int64_t)eo_ctx->first_time[t][to];
			double secs = (double)ttime / 1000000000;
			double tested_hz = ((double)(counts[t][to] - 1)) / fabs(secs);
			double setup_hz = frac2float(g_options.basehz[t]);

			setup_hz *= g_options.multiplier[to];
			double errorp = ((tested_hz - setup_hz) / setup_hz) * 100;

			APPL_PRINT("%-5u %-5u %-10.4f %-12lu %-12.4f %-12.4f %-12.3f\n",
				   t, to, secs, counts[t][to], tested_hz, setup_hz, errorp);
			total += counts[t][to];

			/* calculations are invalid if last event was not received */
			if ((int64_t)eo_ctx->last_time[t][to] -
			    (int64_t)eo_ctx->first_time[t][to] < 1)
				APPL_PRINT("WARN: last event for tmo[%u][%u] not received?\n",
					   t, to);
		}
	}

	double runsecs = (double)(eo_ctx->stop_time - eo_ctx->start_time) / 1000000000;

	APPL_PRINT("\n%lu total timeouts received in %.3f s -> %.4f M tmo / sec\n\n",
		   total, runsecs, ((double)total / runsecs) / 1000000);
}

void test_init(void)
{
	int core = em_core_id();

	/* first core creates shared memory */
	if (core == 0) {
		odp_shm = odp_shm_reserve(SHM_NAME, sizeof(timer_app_shm_t), 64, 0);
		if (odp_shm == ODP_SHM_INVALID) {
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "shm init failed on EM-core: %u", core);
		}
		m_shm = odp_shm_addr(odp_shm);

		/* initialize it */
		if (m_shm)
			memset(m_shm, 0, sizeof(timer_app_shm_t));

		if (EXTRA_PRINTS)
			APPL_PRINT("%luk shared memory for app context\n",
				   sizeof(timer_app_shm_t) / 1024);

		if (g_options.tracelen) {
			size_t tlen = em_core_count() * g_options.tracelen * sizeof(trace_entry_t);

			odp_shm_trace = odp_shm_reserve(SHM_TRACE_NAME, tlen, 64, 0);
			if (odp_shm_trace == ODP_SHM_INVALID) {
				test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
					   "trace shm init failed on EM-core: %u", core);
			}
			m_tracebuf = odp_shm_addr(odp_shm_trace);
			if (m_tracebuf)
				memset(m_tracebuf, 0, tlen);
			if (EXTRA_PRINTS)
				APPL_PRINT("%luk shared memory for trace\n", tlen / 1024);
		} else {
			odp_shm_trace = ODP_SHM_INVALID;
		}
	} else {
		/* lookup memory from core 0 init */
		odp_shm = odp_shm_lookup(SHM_NAME);
		test_fatal_if(odp_shm == ODP_SHM_INVALID, "shared mem lookup fail");

		if (g_options.tracelen) {
			odp_shm_trace = odp_shm_lookup(SHM_TRACE_NAME);
			test_fatal_if(odp_shm_trace == ODP_SHM_INVALID,
				      "trace shared mem lookup fail");
		}

		m_shm = odp_shm_addr(odp_shm);
	}

	if (m_shm == NULL)
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
			   "ShMem init failed on EM-core: %u", core);

	if (EXTRA_PRINTS)
		APPL_PRINT("Shared mem at %p on core %d\n", m_shm, core);

	if (g_options.tracelen) {
		m_tracebuf = odp_shm_addr(odp_shm_trace);
		if (m_tracebuf == NULL)
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF,
				   "Trace ShMem adj failed on EM-core: %u", core);
		m_tracebuf += g_options.tracelen * core;
		if (EXTRA_PRINTS)
			APPL_PRINT("Trace buffer at %p on core %d\n", m_tracebuf, core);
	}

	mlockall(MCL_FUTURE);
	if (EXTRA_PRINTS)
		APPL_PRINT("core %d: %s done, shm @%p\n", core, __func__, m_shm);
}

/**
 * Startup of the timer ring test EM application
 */
void test_start(appl_conf_t *const appl_conf)
{
	em_eo_t eo;
	em_timer_attr_t attr;
	em_queue_t queue;
	em_status_t stat;
	app_eo_ctx_t *eo_ctx;

	if (appl_conf->num_procs > 1) {
		APPL_PRINT("\nPROCESS MODE is not yet supported!\n");
		abort();
	}

	fix_setup();

	eo_ctx = &m_shm->eo_context;
	memset(eo_ctx, 0, sizeof(app_eo_ctx_t));

	eo = em_eo_create(APP_EO_NAME, app_eo_start, app_eo_start_local,
			  app_eo_stop, app_eo_stop_local, app_eo_receive,
			  eo_ctx);
	test_fatal_if(eo == EM_EO_UNDEF, "Failed to create EO!");

	stat = em_register_error_handler(my_error_handler);
	test_fatal_if(stat != EM_OK, "Failed to register error handler");

	/* parallel high priority for timeout handling*/
	queue = em_queue_create("Tmo Q",
				EM_QUEUE_TYPE_PARALLEL,
				EM_QUEUE_PRIO_HIGHEST,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to create test queue!");
	eo_ctx->test_q = queue;

	/* another normal priority for heartbeat */
	queue = em_queue_create("HB Q",
				EM_QUEUE_TYPE_ATOMIC,
				EM_QUEUE_PRIO_NORMAL,
				EM_QUEUE_GROUP_DEFAULT, NULL);
	stat = em_eo_add_queue_sync(eo, queue);
	test_fatal_if(stat != EM_OK, "Failed to add HB queue!");
	eo_ctx->hb_q = queue;

	/* create HB timer */
	em_timer_attr_init(&attr);
	strncpy(attr.name, "HBTimer", EM_TIMER_NAME_LEN);
	m_shm->hb_tmr = em_timer_create(&attr);
	test_fatal_if(m_shm->hb_tmr == EM_TIMER_UNDEF,
		      "Failed to create HB timer!");

	trace_add(TEST_TIME_FN(), TRACE_OP_TMR_CREATE, em_core_id(), -1, -1, NULL, m_shm->hb_tmr);

	em_timer_capability_t capa = { 0 };

	stat = em_timer_capability(&capa, g_options.clksrc);
	test_fatal_if(stat != EM_OK, "em_timer_capability returned error for clk %u\n",
		      g_options.clksrc);
	test_fatal_if(capa.ring.max_rings == 0, "Ring timers not supported!");

	APPL_PRINT("Timer ring capability for clksrc %d:\n", g_options.clksrc);
	APPL_PRINT(" maximum timers: %d\n", capa.ring.max_rings);

	double hz = frac2float(capa.ring.min_base_hz);

	APPL_PRINT(" minimum base_hz: %.3f\n", hz);
	hz = frac2float(capa.ring.max_base_hz);
	APPL_PRINT(" maximum base_hz: %.3f\n", hz);

	/* Start EO */
	stat = em_eo_start_sync(eo, NULL, NULL);
	test_fatal_if(stat != EM_OK, "Failed to start EO!");
}

void test_stop(appl_conf_t *const appl_conf)
{
	if (appl_conf->num_procs > 1) {
		APPL_PRINT("%s(): skip\n", __func__);
		return;
	}

	em_eo_t eo = em_eo_find(APP_EO_NAME);

	test_fatal_if(eo == EM_EO_UNDEF, "Could not find EO:%s", APP_EO_NAME);

	em_status_t ret = em_eo_stop_sync(eo);

	test_fatal_if(ret != EM_OK, "EO:%" PRI_EO " stop:%" PRI_STAT "", eo, ret);

	ret = em_timer_delete(m_shm->hb_tmr);
	test_fatal_if(ret != EM_OK, "Timer:%" PRI_TMR " delete:%" PRI_STAT "",
		      m_shm->hb_tmr, ret);
	m_shm->hb_tmr = EM_TIMER_UNDEF;

	ret = em_eo_remove_queue_all_sync(eo, EM_TRUE);
	test_fatal_if(ret != EM_OK, "EO:%" PRI_EO " delete Qs:%" PRI_STAT "", eo, ret);

	ret = em_eo_delete(eo);
	test_fatal_if(ret != EM_OK, "EO:%" PRI_EO " delete:%" PRI_STAT "", eo, ret);

	em_unregister_error_handler();
	APPL_PRINT("test_stopped\n");
}

void test_term(void)
{
	if (m_shm != NULL) {
		odp_shm_free(odp_shm);
		m_shm = NULL;
		odp_shm = ODP_SHM_INVALID;
	}
	if (odp_shm_trace != ODP_SHM_INVALID) {
		odp_shm_free(odp_shm_trace);
		odp_shm_trace = ODP_SHM_INVALID;
	}
}

static em_status_t app_eo_start(void *eo_context, em_eo_t eo, const em_eo_conf_t *conf)
{
	app_msg_t *msg;
	app_eo_ctx_t *eo_ctx = (app_eo_ctx_t *)eo_context;

	(void)eo;
	(void)conf;

	odp_ticketlock_init(&tracelock);
	print_setup();

	for (unsigned int t = 0; t < g_options.num_timers; t++)
		for (unsigned int to = 0; to < g_options.num_tmo; to++) {
			eo_ctx->test_tmo[t][to] = EM_TMO_UNDEF;
			eo_ctx->test_ev[t][to] = EM_EVENT_UNDEF;
		}

	/* create periodic timeout for heartbeat */
	eo_ctx->heartbeat_tmo = em_tmo_create(m_shm->hb_tmr, EM_TMO_FLAG_PERIODIC, eo_ctx->hb_q);
	test_fatal_if(eo_ctx->heartbeat_tmo == EM_TMO_UNDEF,
		      "Can't allocate heartbeat_tmo!\n");

	em_event_t event = em_alloc(sizeof(app_msg_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

	test_fatal_if(event == EM_EVENT_UNDEF, "Can't allocate event (%ldB)!\n",
		      sizeof(app_msg_t));

	msg = em_event_pointer(event);
	msg->count = 0;
	msg->type = MSGTYPE_HB;
	msg->tmo = eo_ctx->heartbeat_tmo;
	uint64_t hb_hz = em_timer_get_freq(m_shm->hb_tmr);

	if (hb_hz < 10)
		APPL_ERROR("WARNING: HB timer hz very low!?\n");

	em_timer_tick_t period = hb_hz; /* 1s HB */

	test_fatal_if(period < 1, "HB timer resolution is too low!\n");

	eo_ctx->state = STATE_START;
	eo_ctx->next_change = 2;

	if (g_options.profile) {
		for (int c = 0; c < em_core_count(); c++)
			for (int p = 0; p < NUM_PROFILES; p++)
				eo_ctx->cdat[c].prof[p].min = UINT64_MAX;
	}

	/* start heartbeat */
	em_status_t stat = em_tmo_set_periodic(eo_ctx->heartbeat_tmo, 0, period, event);

	test_fatal_if(stat != EM_OK, "Can't activate heartbeat tmo!\n");
	trace_add(TEST_TIME_FN(), TRACE_OP_TMO_SET, em_core_id(),
		  -1, -1, NULL, eo_ctx->heartbeat_tmo);

	if (EXTRA_PRINTS)
		APPL_PRINT("WARNING: extra prints enabled, expect some timing jitter\n");

	stat = em_dispatch_register_enter_cb(enter_cb);
	test_fatal_if(stat != EM_OK, "enter_cb() register failed!");
	stat = em_dispatch_register_exit_cb(exit_cb);
	test_fatal_if(stat != EM_OK, "exit_cb() register failed!");

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
	(void)eo_ctx;

	test_fatal_if(core >= MAX_CORES, "Too many cores!");

	memset(&eo_ctx->cdat[core].rnd, 0, sizeof(rnd_state_t));
	initstate_r(time(NULL), eo_ctx->cdat[core].rnd.rndstate, RND_STATE_BUF,
		    &eo_ctx->cdat[core].rnd.rdata);
	srandom(time(NULL));
	m_tracecount = 0;
	trace_add(TEST_TIME_FN(), TRACE_OP_START, core, -1, -1, NULL, NULL);
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

	(void)eo;

	if (EXTRA_PRINTS)
		APPL_PRINT("EO stop\n");

	if (eo_ctx->heartbeat_tmo != EM_TMO_UNDEF) {
		em_tmo_delete(eo_ctx->heartbeat_tmo, &event);
		eo_ctx->heartbeat_tmo = EM_TMO_UNDEF;
		if (event != EM_EVENT_UNDEF)
			em_free(event);
	}

	delete_test_timer(eo_context);

	em_status_t ret = em_dispatch_unregister_enter_cb(enter_cb);

	test_fatal_if(ret != EM_OK, "enter_cb() unregister:%" PRI_STAT, ret);
	ret = em_dispatch_unregister_exit_cb(exit_cb);
	test_fatal_if(ret != EM_OK, "exit_cb() unregister:%" PRI_STAT, ret);

	if (EXTRA_PRINTS)
		APPL_PRINT("EO stop done\n");
	return EM_OK;
}

static em_status_t app_eo_stop_local(void *eo_context, em_eo_t eo)
{
	(void)eo;

	trace_add(TEST_TIME_FN(), TRACE_OP_END, em_core_id(), -1, -1, NULL, NULL);

	/* dump trace */
	if (g_options.tracelen) {
		odp_ticketlock_lock(&tracelock); /* serialize printing */
		dump_trace(eo_context);
		odp_ticketlock_unlock(&tracelock);
	}

	return EM_OK;
}

/* EO receive function */
static void app_eo_receive(void *eo_context, em_event_t event,
			   em_event_type_t type, em_queue_t queue,
			   void *q_context)
{
	uint64_t now = TEST_TIME_FN();
	app_eo_ctx_t *const eo_ctx = eo_context;
	bool reuse = false;

	(void)q_context;
	(void)queue;

	/* heartbeat */
	if (type == EM_EVENT_TYPE_SW) {
		app_msg_t *msgin = (app_msg_t *)em_event_pointer(event);

		switch (msgin->type) {
		case MSGTYPE_HB: /* uses atomic queue */
			reuse = handle_heartbeat(eo_ctx, event, msgin, now);
			break;

		default:
			test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Invalid msg received!\n");
		}
	} else if (type == EM_EVENT_TYPE_TIMER_IND) { /* test timeout */
		reuse = handle_tmo(eo_ctx, event, now); /* uses parallel queue */
	} else {
		test_error(EM_ERROR_SET_FATAL(0xDEAD), 0xBEEF, "Invalid event type %u!\n", type);
	}

	if (!reuse) {
		if (type == EM_EVENT_TYPE_TIMER_IND) { /* extra trace */
			unsigned int tmri, tmoi;

			ptr2tmo(em_tmo_get_userptr(event, NULL), &tmri, &tmoi);
			trace_add(TEST_TIME_FN(), TRACE_OP_TMO_EV_FREE, em_core_id(),
				  tmri, tmoi, eo_ctx->test_tmo[tmri][tmoi], event);
			eo_ctx->test_ev[tmri][tmoi] = EM_EVENT_UNDEF;
		}
		em_free(event);
	}
}

int main(int argc, char *argv[])
{
	/* pick app-specific arguments after '--' */
	int i;

	APPL_PRINT("EM periodic ring timer test %s\n\n", VERSION);

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
