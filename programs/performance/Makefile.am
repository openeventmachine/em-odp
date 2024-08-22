include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = atomic_processing_end \
		  pairs \
		  loop \
		  loop_multircv \
		  loop_refs \
		  loop_vectors \
		  queue_groups \
		  queues \
		  queues_unscheduled \
		  queues_local \
		  queues_output \
		  send_multi \
		  timer_test_periodic \
		  scheduling_latency \
		  pool_perf \
		  timer_test_ring \
		  timer_test

atomic_processing_end_LDFLAGS = $(AM_LDFLAGS)
atomic_processing_end_CFLAGS = $(AM_CFLAGS)

pairs_LDFLAGS = $(AM_LDFLAGS)
pairs_CFLAGS = $(AM_CFLAGS)

loop_LDFLAGS = $(AM_LDFLAGS)
loop_CFLAGS = $(AM_CFLAGS)

loop_multircv_LDFLAGS = $(AM_LDFLAGS)
loop_multircv_CFLAGS = $(AM_CFLAGS)

loop_refs_LDFLAGS = $(AM_LDFLAGS)
loop_refs_CFLAGS = $(AM_CFLAGS)

loop_vectors_LDFLAGS = $(AM_LDFLAGS)
loop_vectors_CFLAGS = $(AM_CFLAGS)

queue_groups_LDFLAGS = $(AM_LDFLAGS)
queue_groups_CFLAGS = $(AM_CFLAGS)

queues_LDFLAGS = $(AM_LDFLAGS)
queues_CFLAGS = $(AM_CFLAGS)

queues_unscheduled_LDFLAGS = $(AM_LDFLAGS)
queues_unscheduled_CFLAGS = $(AM_CFLAGS)

queues_local_LDFLAGS = $(AM_LDFLAGS)
queues_local_CFLAGS = $(AM_CFLAGS)

queues_output_LDFLAGS = $(AM_LDFLAGS)
queues_output_CFLAGS = $(AM_CFLAGS)

send_multi_LDFLAGS = $(AM_LDFLAGS)
send_multi_CFLAGS = $(AM_CFLAGS)

timer_test_periodic_LDFLAGS = $(AM_LDFLAGS) -lm
timer_test_periodic_CFLAGS = $(AM_CFLAGS)

timer_test_ring_LDFLAGS = $(AM_LDFLAGS) -lm
timer_test_ring_CFLAGS = $(AM_CFLAGS)

timer_test_LDFLAGS = $(AM_LDFLAGS) -lm
timer_test_CFLAGS = $(AM_CFLAGS)

scheduling_latency_LDFLAGS = $(AM_LDFLAGS) -lm
scheduling_latency_CFLAGS = $(AM_CFLAGS)

pool_perf_LDFLAGS = $(AM_LDFLAGS) -lm
pool_perf_CFLAGS = $(AM_CFLAGS)

dist_atomic_processing_end_SOURCES = atomic_processing_end.c
dist_pairs_SOURCES = pairs.c
dist_loop_SOURCES = loop.c
dist_loop_multircv_SOURCES = loop_multircv.c
dist_loop_refs_SOURCES = loop_refs.c
dist_loop_vectors_SOURCES = loop_vectors.c
dist_queue_groups_SOURCES = queue_groups.c
dist_queues_SOURCES = queues.c
dist_queues_unscheduled_SOURCES = queues_unscheduled.c
dist_queues_local_SOURCES = queues_local.c
dist_queues_output_SOURCES = queues_output.c
dist_send_multi_SOURCES = send_multi.c
dist_timer_test_periodic_SOURCES = timer_test_periodic.c timer_test_periodic.h
dist_timer_test_ring_SOURCES = timer_test_ring.c timer_test_ring.h
dist_timer_test_SOURCES = timer_test.c
dist_scheduling_latency_SOURCES = scheduling_latency.c scheduling_latency.h
dist_pool_perf_SOURCES = pool_perf.c pool_perf.h
