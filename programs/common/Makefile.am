LIB = $(top_builddir)/programs/common

noinst_LTLIBRARIES = $(LIB)/libprgcm.la

AM_CFLAGS += -I$(top_srcdir)/include/
AM_CFLAGS += -I$(top_srcdir)/src
AM_CFLAGS += -I$(top_builddir)/include

AM_CFLAGS += $(ODP_CFLAGS)

__LIB__libprgcm_la_SOURCES = \
cm_error_handler.c cm_error_handler.h \
cm_pktio.c cm_pktio.h \
cm_pool_config.h \
cm_setup.c cm_setup.h
