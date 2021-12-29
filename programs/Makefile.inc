LIB   = $(top_builddir)/lib
LIBCM = $(top_builddir)/programs/common
LDADD = $(LIBCM)/libprgcm.la \
        $(LIB)/libemodp.la

AM_CFLAGS += -I$(srcdir) \
	     -I$(top_srcdir)/programs \
	     -I$(top_srcdir)/programs/common \
	     -I$(top_srcdir)/include \
	     -I$(top_builddir)/include

AM_LDFLAGS += -L$(LIBCM) \
	      -L$(LIB)

if STATIC_APPS
# Link em-odp/programs/* statically against libemodp.a and libodp*.a
LDADD += $(ODPHELPER_STATIC_LIBS) \
	 $(ODP_STATIC_LIBS)
AM_LDFLAGS += -static-libtool-libs
AM_CFLAGS += $(ODPHELPER_STATIC_CFLAGS) \
	     $(ODP_STATIC_CFLAGS)
else
# Link em-odp/programs/* dynamically against libemodp.so and libodp*.so
LDADD += $(ODPHELPER_LIBS) \
	 $(ODP_LIBS)
AM_CFLAGS += $(ODPHELPER_CFLAGS) \
	     $(ODP_CFLAGS)
# Build executables in em-odp/programs/* instead of wrapper scripts.
# Wrapper scripts are otherwise produced by libtool to handle e.g. the
# missing _installation_ of libemodp.so.
AM_LDFLAGS += -no-install
endif
