include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = bench_event bench_pool

bench_event_LDFLAGS = $(AM_LDFLAGS)
bench_event_CFLAGS = $(AM_CFLAGS)

bench_pool_LDFLAGS = $(AM_LDFLAGS)
bench_pool_CFLAGS = $(AM_CFLAGS)

dist_bench_event_SOURCES = bench_common.h bench_common.c bench_event.c
dist_bench_pool_SOURCES = bench_common.h bench_common.c bench_pool.c
