#include <getopt.h>

#define APP_EO_NAME	"testEO"
#define DEF_TMO_DATA	100 /* per core, MAX_TMO_DATA * sizeof(tmo_data) */
#define MAX_TMO_BYTES	1000000000ULL /* sanity limit 1GB tracebuf per core */
#define STOP_THRESHOLD	90 /* % of full buffer */
#define MAX_CORES	64
#define INIT_WAIT	5 /* startup wait, HBs (=secs) */
#define MEAS_PERIOD	5 /* freq meas HBs */
#define DEF_RES_NS	1000000ULL /* 1ms seems like a good generic default */
#define DEF_PERIOD	20 /* default period, N * res */
#define DEF_MIN_PERIOD	5 /* min period default, N * res */
#define DEF_MAX_PERIOD	(2 * 1000 * 1000 * 1000ULL) /* 2 sec */
#define EXTRA_PRINTS	0 /* dev option, normally 0 */
#define MAX_TEST_TIMERS 32

const struct option longopts[] = {
	{"num-tmo",		required_argument, NULL, 'n'},
	{"resolution",		required_argument, NULL, 'r'},
	{"res_hz",		required_argument, NULL, 'z'},
	{"period",		required_argument, NULL, 'p'},
	{"first",		required_argument, NULL, 'f'},
	{"max-period",		required_argument, NULL, 'm'},
	{"min-period",		required_argument, NULL, 'l'},
	{"clk",			required_argument, NULL, 'c'},
	{"write",		optional_argument, NULL, 'w'},
	{"num-runs",		required_argument, NULL, 'x'},
	{"tracebuf",		required_argument, NULL, 't'},
	{"extra-work",		required_argument, NULL, 'e'},
	{"background-job",	required_argument, NULL, 'j'},
	{"skip",		no_argument, NULL, 's'},
	{"api-prof",		no_argument, NULL, 'a'},
	{"dispatch-prof",	no_argument, NULL, 'd'},
	{"job-prof",		no_argument, NULL, 'b'},
	{"info",		no_argument, NULL, 'i'},
	{"use-huge",		no_argument, NULL, 'u'},
	{"no-delete",		no_argument, NULL, 'q'},
	{"use-cpu-cycle",	optional_argument, NULL, 'g'},
	{"memzero",		required_argument, NULL, 'o'},
	{"abort",		required_argument, NULL, 'k'},
	{"num-timers",		required_argument, NULL, 'y'},
	{"help",		no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

const char *shortopts = "n:r:p:f:m:l:c:w::x:t:e:j:sadbiuqg::hz:o:k:y:";
/* descriptions for above options, keep in sync! */
const char *descopts[] = {
	"Number of concurrent timeouts to create",
	"Resolution of test timer (ns), Use 0 for highest supported",
	"Resolution of periodic test timer as frequency (Hz). Use either -r or -z",
	"Period of periodic test timer (ns). 0 for random",
	"First period (ns, default 0 = same as period, use -1 for random)",
	"Maximum period (ns)",
	"Minimum period (ns, only used for random tmo)",
	"Clock source (integer. See event_machine_timer_hw_specific.h)",
	"Write raw trace data in csv format to given file e.g.-wtest.csv (default stdout)",
	"Number of test runs, 0 to run forever",
	"Trace buffer size (events per core). Optional stop threshold % e.g. -t100,80 to stop 80% before full",
	"Extra work per tmo: -e1,20,50 e.g. min_us,max_us,propability % of work",
	"Extra background job: -j2,20,500,10 e.g. num,time_us,total_kB,chunk_kB",
	"Create timer without NOSKIP option",
	"Measure API calls",
	"Include dispatcher trace (EO enter-exit, analysis currently broken)",
	"Include bg job profile (note - can fill buffer quickly)",
	"Only print timer capabilities and exit",
	"Use huge page for trace buffer",
	"Don't delete timeouts between runs (if -x)",
	"Use CPU cycles instead of ODP time. Optionally give frequency (hz)",
	"Allocate and clear memory: -o50,100[,1] to clear 50MB (,1 to use huge pg) every 100ms. Special HW test, must also use -j",
	"Abort application after given tmos (test abnormal exit). Use negative count to do segfault instead",
	"Number of timers to use for test. Default 1",
	"Print usage and exit",
	NULL
};

const char *instructions =
"\nMain purpose of this experimental tool is to manually test periodic timer accuracy and\n"
"behaviour optionally under (over)load. Test is controlled by command line arguments.\n"
"Some API overheads can also be measured.\n"
"\nAt least two EM timers are created. One for a heartbeat driving test states. Second\n"
"timer (or multiple) is used for testing the periodic timeouts. It can be created with\n"
"given attributes to also test limits. All the test timers are configured the same way.\n"
"If multiple timers are used the timeouts are randomly placed on those.\n\n"
"Test runs in states:\n"
"	STATE_INIT	let some time pass before starting (to settle down)\n"
"	STATE_MEASURE	measure timer tick frequency against linux clock\n"
"	STATE_STABILIZE finish all prints before starting run\n"
"	STATE_RUN	timeouts created and measured\n"
"	STATE_COOLOFF	first core hitting trace buffer limit sets cooling,\n"
"			i.e. coming timeout(s) are cancelled no more analyzed\n"
"	STATE_ANALYZE	Statistics and trace file generation, restart (if -x)\n"
"\nBy default there is no background load and the handling of incoming\n"
"timeouts (to high priority parallel queue) is minimized. Extra work can be\n"
"added in two ways:\n\n"
"1) --extra-work min us, max us, propability %\n"
"	this will add random delay between min-max before calling ack().\n"
"	Delay is added with the given propability (100 for all tmos)\n"
"	e.g. -e10,100,50 to add random delay between 10 and 100us with 50%\n"
"	propability\n"
"2) --background-job  num,length us,total_kB,chunk_kB\n"
"	this adds background work handled via separate low priority parallel\n"
"	queue. num events are sent at start. Receiving touches given amount\n"
"	of data (chunk at a time) for given amount of time and\n"
"	then sends it again\n"
"	e.g. -j1,10,500,20 adds one event of 10us processing over 20kB data\n"
"	randomly picked from 500kB\n\n"
"Test can write a file of measured timings (-w). It is in CSV format and can\n"
"be imported e.g. to excel for plotting. -w without name prints to stdout\n"
"\nSingle time values can be postfixed with n,u,m,s to indicate nano(default),\n"
"micro, milli or seconds. e.g. -p1m for 1ms. Integers only\n";

typedef enum e_op {
	OP_TMO,
	OP_HB,
	OP_STATE,
	OP_CANCEL,
	OP_WORK,
	OP_ACK,
	OP_BGWORK,
	OP_MEMZERO,
	OP_MEMZERO_END,
	OP_PROF_ACK,	/* linux time used as tick diff for each PROF */
	OP_PROF_DELETE,
	OP_PROF_CANCEL,
	OP_PROF_CREATE,
	OP_PROF_SET,
	OP_PROF_ENTER_CB,
	OP_PROF_EXIT_CB,

	OP_LAST
} e_op;
const char *op_labels[] = {
	"TMO",
	"HB",
	"STATE",
	"CANCEL",
	"WORK",
	"ACK",
	"BG-WORK",
	"MEMZERO-TST",
	"MEMZERO-END",
	"PROF-ACK",
	"PROF-DEL",
	"PROF-CANCEL",
	"PROF-CREATE",
	"PROF-SET",
	"PROF-ENTER_CB",
	"PROF-EXIT_CB"
};

typedef union time_stamp { /* to work around ODP time type vs CPU cycles */
	uint64_t u64;
	odp_time_t odp;
} time_stamp;
typedef struct tmo_trace {
		int id;
		e_op op;
		uint64_t tick;
		time_stamp ts;
		time_stamp linuxt;
		int count;
		int tidx;
} tmo_trace;

#define RND_STATE_BUF   32
typedef struct rnd_state_t {
	struct random_data rdata;
	char rndstate[RND_STATE_BUF];
} rnd_state_t;

typedef struct core_data {
	int count ODP_ALIGNED_CACHE;
	tmo_trace *trc;
	size_t	trc_size;
	int cancelled;
	int jobs;
	int jobs_deleted;
	rnd_state_t rng;
	time_stamp enter;
	time_stamp acc_time;
} core_data;

typedef enum e_cmd {
	CMD_HEARTBEAT,
	CMD_TMO,
	CMD_DONE,
	CMD_BGWORK,

	CMD_LAST
} e_cmd;

typedef struct app_msg_t {
	e_cmd command;
	int count;
	em_tmo_t tmo;
	int tidx;
	int id;
	uint64_t arg;

} app_msg_t;

typedef enum e_state {
	STATE_INIT,	/* before start */
	STATE_MEASURE,	/* measure timer freq */
	STATE_STABILIZE,/* finish all printing before tmo setup */
	STATE_RUN,	/* timers running */
	STATE_COOLOFF,	/* cores cancel timers */
	STATE_ANALYZE,	/* timestamps analyzed */

	STATE_LAST
} e_state;

const char *state_labels[] = {
	"INIT",
	"MEASURE",
	"STABILIZE",
	"RUN",
	"COOLOFF",
	"ANALYZE"
};

typedef struct tmo_setup {
	time_stamp start_ts;
	em_tmo_t handle;
	uint64_t start;
	uint64_t period_ns;
	uint64_t first_ns;
	uint64_t first;
	uint64_t ticks;
	uint64_t ack_late;
	int	 tidx;
} tmo_setup;
