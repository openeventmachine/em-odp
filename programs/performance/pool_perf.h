#include <getopt.h>
#include <inttypes.h>
#include <odp_api.h>
#include <stdatomic.h>

#define VERSION "v0.2 WIP"

#define MAX_ALLOCS	128	/* max events allocated per context, pwr 2 */
#define MAX_CORES	64	/* space for per-core tables */
#define EVENTS_PER_LOOP	10000000
#define TIME_FN		odp_time_global_strict_ns /* or odp_time_global_ns */

#define NUM_BINS	10
#define NUM_BIN_INST	2
#define BIN_ALLOC	0
#define BIN_FREE	1

#if ((MAX_ALLOCS & (MAX_ALLOCS - 1)) != 0)
#error "MAX_ALLOCS need to be pwr of 2"
#endif

void test_init(void);

/* EO context */
typedef struct app_eo_ctx {
	em_queue_t test_q;
	em_pool_t test_pool;
	odp_pool_t odp_pool;
} app_eo_ctx;

/* cmdline options */
typedef struct config_data {
	int loops;
	unsigned int perloop;
	unsigned int events;
	int num_alloc;
	int num_free;
	unsigned int window;
	unsigned int size;
	unsigned int skip;
	uint64_t delay;
	bool	use_multi;
	bool	use_odp;
	unsigned int no_first;
	struct {
		unsigned int num;
		unsigned int size;
		unsigned int cache;
		em_event_type_t type;
	} pool;
	unsigned int flush;

} config_data;

/* shared memory data */
typedef struct perf_shm_t {
	atomic_int	events_left;
	atomic_bool	stopping;

	struct {
		int64_t min_alloc;
		int64_t max_alloc;
		uint64_t num_alloc;
		int64_t time_allocs;

		int64_t min_free;
		int64_t max_free;
		uint64_t num_free;
		int64_t time_free;

		uint64_t num_updates;

	} times ODP_ALIGNED_CACHE;

	struct {
		uint64_t allocs;
		uint64_t frees;
		uint64_t events;

	} counts[MAX_CORES];

	em_eo_t eo;
	app_eo_ctx eo_ctx;
	int64_t ts_overhead;	/* stored timestamp overhead */

	atomic_int loopcount ODP_ALIGNED_CACHE;
	uint64_t start_time;
	odp_spinlock_t lock; /* used with global stats update */

} perf_shm_t;

typedef union {
	em_event_t em_ev;
	odp_event_t odp_ev;
} event_handle_t;

/* test event. Holds all data needed to run one instance */
typedef struct test_msg {
	uint64_t instance;
	uint64_t count;		/* seq # */
	uint64_t allocated;	/* currently allocated for this context */
	uint64_t alloc_threshold;
	uint64_t free_threshold;
	size_t flush_b;		/* size of cache flush buffer */
	void  *flush_buf;

	/* times recorded */
	struct {
		struct {
			int64_t min_ns;
			uint64_t min_evt;
			uint64_t min_burst;
			int64_t max_ns;
			uint64_t max_evt;
			uint64_t max_burst;
			int64_t sum_ns;
			uint64_t tot_allocs;
		} alloc;
		struct {
			int64_t min_ns;
			uint64_t min_evt;
			uint64_t min_burst;
			int64_t max_ns;
			uint64_t max_evt;
			uint64_t max_burst;
			int64_t sum_ns;
			uint64_t tot_free;
		} free;
	} times;

	uint64_t start_ns;

	/* stack for event handles allocated */
	event_handle_t events[MAX_ALLOCS];
	int top;

} test_msg;

/* timing bin data */
typedef struct {
	int64_t upto_ns;
	atomic_uint_least64_t count;
} test_bin_t;

/* cmdline arguments */

const struct option longopts[] = {
	{"loops",		required_argument, NULL, 'l'},
	{"num",			required_argument, NULL, 'n'},
	{"events",		required_argument, NULL, 'e'},
	{"allocs",		required_argument, NULL, 'a'},
	{"frees",		required_argument, NULL, 'f'},
	{"window",		required_argument, NULL, 'w'},
	{"delay",		required_argument, NULL, 'd'},
	{"size",		required_argument, NULL, 's'},
	{"pool",		required_argument, NULL, 'p'},
	{"ignore",		required_argument, NULL, 'i'},
	{"cache-flush",		required_argument, NULL, 'c'},
	{"use-multi",		no_argument, NULL, 'm'},
	{"bypass",		no_argument, NULL, 'b'},
	{"no-first",		optional_argument, NULL, 't'},

	{"help",		no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

const char *shortopts = "l:e:ha:f:w:md:s:p:i:n:bc:t::";
/* descriptions for above options, keep in sync! */
const char *descopts[] = {
	"Number of measurement cycles (-n events each), default 1",
	"Number alloc and free batches per cycle, default 10M",
	"Number of events (parallel ops), default 1",
	"Number of burst allocs, default 1. Negative for random",
	"Number of burst frees, default 1. Negative for random",
	"Window of buffers (maximum allocated), default from -a",
	"Add delay after alloc burst, ns. Default 0",
	"Size of event to allocate, bytes. Default 256",
	"Pool setup. -p10000,512,32,SW means create pool with 10000 events of 512B, local cache 32, event type SW (or PACKET)",
	"Ignore given amount of allocations at start (skip timing first N total allocs)",
	"Flush cpu HW caches after each alloc burst by modifying given amount of data (kB). Default 0=no",
	"Use _multi - variants. Default no",
	"Bypass EM and use ODP pool API. Default no",
	"Skip timing first N allocs counted per core, default 1",
	"Print usage and exit",
	NULL
};

const char *instructions =
"Simple memory pool performance test\n\n"
"Test runs event allocation and free and measures min, max and average time of the API call.\n"
"Trigger event(s) are sent at start. Once received given number of allocations are done and then\n"
"event is sent again. Next time given amount of free is done. When enough these are done,\n"
"statistics are calculated and if number of loops is more than one then test is repeated.\n"
"Number of events can be specified, which can enable concurrent operations.\n\n"

"Test tries to measure timestamping overhead at startup and subtracts that from measured times.\n"
"However if the measured time is equal or less than (may happen with short times) then time is negative\n"
"and the measured overhead is also printed for reference.\n\n"
"Min and max times are per API call (either single or multi flavor), but global average is per buffer/packet.\n"
"The printed timing bins are per loop, not accumulating global.\n"
"The printed rates (allocs/sec) are only informational, those include the overheads of keeping and\n"
"printing statistics plus double schedule round per burst.\n"
"\n";
