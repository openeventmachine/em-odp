LIB   = $(top_builddir)/lib
LIBCM = $(top_builddir)/programs/common
LDADD = $(LIBCM)/libprgcm.la \
        $(LIB)/libemodp.la \
        $(ODPHELPER_LIBS) \
        $(ODP_LIBS)

AM_CFLAGS += -I$(srcdir) \
	     -I$(top_srcdir)/programs \
	     -I$(top_srcdir)/programs/common \
	     -I$(top_srcdir)/include \
	     $(ODP_CFLAGS)

AM_LDFLAGS += -L$(LIBCM) \
	      -L$(LIB)
