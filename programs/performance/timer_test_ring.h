#include <getopt.h>
#include <stdatomic.h>

#define APP_EO_NAME	"ringEO"
#define SHM_NAME	"TimerRing-test"
#define SHM_TRACE_NAME	"TimerRing-trace"
#define MAX_CORES	64
#define EXTRA_PRINTS	0 /* dev option, normally 0 */
#define MAX_TEST_TIMERS 15
#define MAX_TEST_TMO	100
#define TEST_TIME_FN	odp_time_global_ns /* OR odp_time_global_strict_ns */
#define MAX_FILENAME	100
#define PRINT_MAX_TMRS  10

const struct option longopts[] = {
	{"loops",		required_argument, NULL, 'l'},
	{"basehz",		required_argument, NULL, 'b'},
	{"basehz-float",	required_argument, NULL, 'f'},
	{"multiplier",		required_argument, NULL, 'm'},
	{"max-multiplier",	required_argument, NULL, 'M'},
	{"resolution",		required_argument, NULL, 'r'},
	{"clksrc",		required_argument, NULL, 'c'},
	{"looptime",		required_argument, NULL, 't'},
	{"tracebuf",		required_argument, NULL, 'T'},
	{"writefile",		optional_argument, NULL, 'w'},
	{"num-tmo",		required_argument, NULL, 'n'},
	{"start_offset",	required_argument, NULL, 'o'},
	{"delay",		required_argument, NULL, 'd'},
	{"recreate-timer",	no_argument, NULL, 'R'},
	{"reuse-tmo",		no_argument, NULL, 'N'},
	{"reuse-event",		no_argument, NULL, 'E'},
	{"api-profile",		no_argument, NULL, 'a'},
	{"help",		no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

const char *shortopts = "hRNEal:m:t:b:r:M:n:c:f:T:w::o:d:";
/* descriptions for above options, keep in sync! */
const char *descopts[] = {
	"Number of test loops, default 1",
	"Base hz as integer, default 100. Can be comma separated list.",
	"Base hz as float, default 100. Approximated to fractions. Can be comma separated list.",
	"Base rate multiplier, default 1. Can be comma separated list",
	"Maximum multiplier, default 8. Can be comma separated list",
	"Resolution, ns. Default 0 (system default).  Can be comma separated list",
	"Clock source for timer",
	"Loop time, i.e. how long to run per loop. Seconds, default 30",
	"Trace buffer size (number of traces per core). Default 0",
	"Write trace dump to csv file at exit. Argument is file name. Default stdout",
	"Number of timeouts per timer, default 1.",
	"Relative start offset in timer ticks. Default 0. Can be comma separated list.",
	"Add extra processing delay per tmo receive, ns. Default 0. Negative is random up to",
	"Delete and re-create timers every loop, default no",
	"Re-use tmo handles (new loop), default no",
	"Re-use tmo event (new loop), default no",
	"Profile API call times, default no",
	"Print usage and exit",
	NULL
};

const char *instructions =
"\nMain purpose of this tool is to test periodic ring timer functionality.\n"
"At least two EM timers are created. One standard timer for a heartbeat\n"
"driving test states. Second timer(s) is periodic ring for testing.\n"
"Multiple test timers are created if -b,M or r specify comma separated list.\n"
"Arguments to control EM setup and app itself are separated with -- e.g.:\n"
"./timer_test_ring -c0x30 -t -- -a -l2 -N\n\n"
"By default (no app options) this runs a simple test once.\n";

#define RND_STATE_BUF   32
typedef struct rnd_state_t {
	struct random_data rdata;
	char rndstate[RND_STATE_BUF];
} rnd_state_t;

typedef struct prof_t {
	uint64_t min;
	uint64_t max;
	uint64_t acc;
	uint64_t num;
} prof_t;

#define NUM_PROFILES 5

typedef enum prof_apis {
	PROF_TMO_CREATE,
	PROF_TMO_SET,
	PROF_TMO_ACK,
	PROF_TMO_CANCEL,
	PROF_TMO_DELETE,

	PROF_TMO_LAST
} prof_apis;

const char *prof_names[] = {
	"TMO_CREATE",
	"TMO_SET",
	"TMO_ACK",
	"TMO_CANCEL",
	"TMO_DELETE"
};

typedef struct core_data {
	unsigned int count[MAX_TEST_TIMERS][MAX_TEST_TMO];
	prof_t prof[NUM_PROFILES];
	uint64_t enter_ns;
	uint64_t exit_ns;
	uint64_t non_eo_ns;
	uint64_t eo_ns;
	rnd_state_t rnd;
} core_data;

typedef enum emsgtype {
	MSGTYPE_HB,
	MSGTYPE_TMO
} emsgtype;

typedef struct app_msg_t {
	emsgtype type;
	unsigned int count;
	em_tmo_t tmo;
	int tidx;
} app_msg_t;

typedef struct tmo_setup {
	em_tmo_t handle;
} tmo_setup;

typedef enum app_state_t {
	STATE_UNDEF,
	STATE_START,
	STATE_RUN,
	STATE_STOP,
	STATE_ANALYZE,
	STATE_EXIT
} app_state_t;

typedef struct app_eo_ctx_t {
	atomic_int state;

	em_tmo_t heartbeat_tmo;
	em_timer_t test_tmr[MAX_TEST_TIMERS];
	uint64_t tick_hz[MAX_TEST_TIMERS];
	em_queue_t hb_q;
	em_queue_t test_q;
	unsigned int next_change;
	uint64_t start_time;
	uint64_t stop_time;
	uint64_t first_time[MAX_TEST_TIMERS][MAX_TEST_TMO];
	uint64_t last_time[MAX_TEST_TIMERS][MAX_TEST_TMO];
	em_tmo_t test_tmo[MAX_TEST_TIMERS][MAX_TEST_TMO];
	em_event_t test_ev[MAX_TEST_TIMERS][MAX_TEST_TMO];
	core_data cdat[MAX_CORES];
} app_eo_ctx_t;

typedef struct timer_app_shm_t {
	/* Number of EM cores running the application */
	unsigned int core_count;
	em_pool_t pool;
	app_eo_ctx_t eo_context;
	em_timer_t hb_tmr;
} timer_app_shm_t;

typedef struct trace_entry_t {
	uint64_t ns;
	uint32_t op;
	uint32_t val;
	int64_t arg1;
	int64_t arg2;
	void   *arg3;
	void   *arg4;
} trace_entry_t;

typedef enum trace_op_t {
	TRACE_OP_START,
	TRACE_OP_TMR_CREATE,
	TRACE_OP_TMO_CREATE,
	TRACE_OP_TMO_SET,
	TRACE_OP_TMO_RX,
	TRACE_OP_HB_RX,
	TRACE_OP_TMO_CANCEL,
	TRACE_OP_TMO_DELETE,
	TRACE_OP_TMR_DELETE,
	TRACE_OP_TMO_ACK,
	TRACE_OP_TMO_ACK_LAST,
	TRACE_OP_TMO_EV_FREE,
	TRACE_OP_DELAY,
	TRACE_OP_SIGINT,
	TRACE_OP_END,

	TRACE_OP_LAST

} trace_op_t;

const char *trace_op_labels[] = {
	"START",
	"TMR_CREATE",
	"TMO_CREATE",
	"TMO_SET",
	"TMO_RX",
	"HB_RX",
	"TMO_CANCEL",
	"TMO_DELETE",
	"TMR_DELETE",
	"TMO_ACK",
	"TMO_LAST",
	"TMO_EV_FREE",
	"DELAY",
	"SIGINT",
	"END",
	"OVERFLOW"
};
