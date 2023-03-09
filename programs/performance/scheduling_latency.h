#include <getopt.h>
#include <inttypes.h>
#include <odp_api.h>

#define VERSION "v0.1 WIP"
#define START_EVENTS	100000	/* ignore first events for startup prints to finish */
#define REPORT_PERIOD	10000000

#define TIMING_MAGIC	0xCAFEBEEF0000CAFE
#define BACKGROUND_MAGIC	0xBEEFBEEFCAFECAFE

void test_init(void);

/* EO context */
typedef struct app_eo_ctx {
	em_queue_t test_q;
	em_queue_t loprio_q;
	bool stopping;
} app_eo_ctx;

/* cmdline options */
typedef struct config_data {
	uint64_t loops;
	em_queue_type_t queue_type;
	uint64_t lo_events;
	uint64_t work_ns;
	em_core_mask_t lgroup;
	em_core_mask_t hgroup;
	bool atomic_end;
	bool eo_receive;

} config_data;

/* shared memory data */
typedef struct perf_shm_t {
	atomic_uint_fast64_t num_lo ODP_ALIGNED_CACHE;

	struct {
		int64_t mint;		/* min ns */
		uint64_t minnum;	/* event # */
		int64_t maxt;		/* max ns */
		uint64_t maxnum;	/* event # */
		int64_t maxdisp;	/* max dispatch overhead ns */
		int64_t sum;		/* for average */
		int64_t disp_min;	/* min dispatch ns , odp->eo */
		int64_t disp_max;	/* max ns */
		int64_t max_eo_time;	/* max EO receive */
	} times ODP_ALIGNED_CACHE;

	em_eo_t eo;
	app_eo_ctx eo_ctx;
	int64_t ts_overhead;	/* stored timestamp overhead */

	uint64_t loopcount ODP_ALIGNED_CACHE;
	int64_t start_time;
	int64_t loop_start_time;
	uint64_t stat_mcount;
	uint64_t stat_lcount;

} perf_shm_t;

typedef struct test_msg {
	uint64_t count;
	uint64_t ts;
	uint64_t work;
	uint64_t magic;
} test_msg;

const struct option longopts[] = {
	{"loops",		required_argument, NULL, 'l'},
	{"levents",		required_argument, NULL, 'e'},
	{"work",		required_argument, NULL, 'w'},
	{"lgroup",		required_argument, NULL, 'g'},
	{"hgroup",		required_argument, NULL, 't'},
	{"atomic-end",		no_argument, NULL, 'a'},
	{"eo-receive",		no_argument, NULL, 'r'},
	{"queue-type",		required_argument, NULL, 'q'},

	{"help",		no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

const char *shortopts = "l:e:hw:g:t:arq:";
/* descriptions for above options, keep in sync! */
const char *descopts[] = {
	"Number of measurement cycles (10M events each)",
	"Number of lower priority background events (default 0)",
	"Amount of time spent for each background event (ns, default 2000)",
	"Coremask for background events, default all (hex, EM core ids)",
	"Coremask for hi-prio timing events, default all (hex, EM core ids)",
	"Use atomic_processing_end (default no)",
	"Include measuring EO receive time (causes extra latency)",
	"Queue type for hi-priority. 0=parallel, 1=atomic (default), 2=ordered",
	"Print usage and exit",
	NULL
};

const char *instructions =
"Simple scheduling latency test. Creates one hi-priority queue and sends one pre-allocated\n"
"event to it. Time from em_send to EO receive is measured and then the event is sent again.\n"
"The reported event rate is the latency limited rate, but also includes test application\n"
"overhead (use -r to check EO time).\n"
"Optionally background event load can be added to another parallel queue (-e).\n"
"By default all cores are load balanced with both queues.\n"
"Note, that when running multiple loops (-l) especially the EO receive time (-r)\n"
"may be affected by the printing happening at every loop cycle end.\n"
"\n"
"time(h)		test runtime\n"
"cores		# cores\n"
"events(M)	# test events\n"
"rate(M/s)	test events per sec\n"
"min[ns]		minimum latency\n"
"max[ns]		maximum latency\n"
"avg[ns]		average latency\n"
"min ev#		event sequence number of min latency\n"
"max ev#		event sequence number of max latency\n"
"max_do[ns]	maximum dispatcher overhead (odp sched -> EO). 0 means debug is disabled\n"
"max_eo[ns]	maximum EO receive. 0 means disabled\n"
"\n";
