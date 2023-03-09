include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = loopback \
		  loopback_multircv \
		  loopback_ag \
		  loopback_local \
		  loopback_local_multircv \
		  multi_stage \
		  multi_stage_local \
		  l2fwd

loopback_LDFLAGS = $(AM_LDFLAGS)
loopback_CFLAGS = $(AM_CFLAGS)
loopback_CFLAGS += -I$(top_srcdir)/src

loopback_multircv_LDFLAGS = $(AM_LDFLAGS)
loopback_multircv_CFLAGS = $(AM_CFLAGS)
loopback_multircv_CFLAGS += -I$(top_srcdir)/src

loopback_ag_LDFLAGS = $(AM_LDFLAGS)
loopback_ag_CFLAGS = $(AM_CFLAGS)
loopback_ag_CFLAGS += -I$(top_srcdir)/src

loopback_local_LDFLAGS = $(AM_LDFLAGS)
loopback_local_CFLAGS = $(AM_CFLAGS)
loopback_local_CFLAGS += -I$(top_srcdir)/src

loopback_local_multircv_LDFLAGS = $(AM_LDFLAGS)
loopback_local_multircv_CFLAGS = $(AM_CFLAGS)
loopback_local_multircv_CFLAGS += -I$(top_srcdir)/src

multi_stage_LDFLAGS = $(AM_LDFLAGS)
multi_stage_CFLAGS = $(AM_CFLAGS)
multi_stage_CFLAGS += -I$(top_srcdir)/src

multi_stage_local_LDFLAGS = $(AM_LDFLAGS)
multi_stage_local_CFLAGS = $(AM_CFLAGS)
multi_stage_local_CFLAGS += -I$(top_srcdir)/src

l2fwd_LDFLAGS = $(AM_LDFLAGS)
l2fwd_CFLAGS = $(AM_CFLAGS)
l2fwd_CFLAGS += -I$(top_srcdir)/src

dist_loopback_SOURCES = loopback.c
dist_loopback_multircv_SOURCES = loopback_multircv.c
dist_loopback_ag_SOURCES = loopback_ag.c
dist_loopback_local_SOURCES = loopback_local.c
dist_loopback_local_multircv_SOURCES = loopback_local_multircv.c
dist_multi_stage_SOURCES = multi_stage.c
dist_multi_stage_local_SOURCES = multi_stage_local.c
dist_l2fwd_SOURCES = l2fwd.c
