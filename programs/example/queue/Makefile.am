include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = queue_types_ag \
		  queue_types_local \
		  ordered

queue_types_ag_LDFLAGS = $(AM_LDFLAGS) -static
queue_types_ag_CFLAGS = $(AM_CFLAGS)

queue_types_local_LDFLAGS = $(AM_LDFLAGS) -static
queue_types_local_CFLAGS = $(AM_CFLAGS)

ordered_LDFLAGS = $(AM_LDFLAGS) -static
ordered_CFLAGS = $(AM_CFLAGS)

dist_queue_types_ag_SOURCES = queue_types_ag.c
dist_queue_types_local_SOURCES = queue_types_local.c
dist_ordered_SOURCES = ordered.c
