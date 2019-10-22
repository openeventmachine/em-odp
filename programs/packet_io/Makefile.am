include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = loopback \
		  loopback_ag \
		  loopback_local \
		  multi_stage \
		  multi_stage_local

loopback_LDFLAGS = $(AM_LDFLAGS) -static
loopback_CFLAGS = $(AM_CFLAGS)
loopback_CFLAGS += -I$(top_srcdir)/src

loopback_ag_LDFLAGS = $(AM_LDFLAGS) -static
loopback_ag_CFLAGS = $(AM_CFLAGS)
loopback_ag_CFLAGS += -I$(top_srcdir)/src

loopback_local_LDFLAGS = $(AM_LDFLAGS) -static
loopback_local_CFLAGS = $(AM_CFLAGS)
loopback_local_CFLAGS += -I$(top_srcdir)/src

multi_stage_LDFLAGS = $(AM_LDFLAGS) -static
multi_stage_CFLAGS = $(AM_CFLAGS)
multi_stage_CFLAGS += -I$(top_srcdir)/src

multi_stage_local_LDFLAGS = $(AM_LDFLAGS) -static
multi_stage_local_CFLAGS = $(AM_CFLAGS)
multi_stage_local_CFLAGS += -I$(top_srcdir)/src

dist_loopback_SOURCES = loopback.c
dist_loopback_ag_SOURCES = loopback_ag.c
dist_loopback_local_SOURCES = loopback_local.c
dist_multi_stage_SOURCES = multi_stage.c
dist_multi_stage_local_SOURCES = multi_stage_local.c
