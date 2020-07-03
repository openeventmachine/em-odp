include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = timer_hello timer_test timer_test_periodic

timer_hello_LDFLAGS = $(AM_LDFLAGS) -static
timer_test_LDFLAGS = $(AM_LDFLAGS) -static
timer_test_periodic_LDFLAGS = $(AM_LDFLAGS) -static -lm

timer_hello_CFLAGS = $(AM_CFLAGS)
timer_test_CFLAGS = $(AM_CFLAGS)
timer_test_periodic_CFLAGS = $(AM_CFLAGS)

dist_timer_hello_SOURCES = timer_hello.c
dist_timer_test_SOURCES = timer_test.c
dist_timer_test_periodic_SOURCES = timer_test_periodic.c
