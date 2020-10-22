ACLOCAL_AMFLAGS=-I m4
AUTOMAKE_OPTIONS = foreign

# make distcheck: run with configured and existing ODP installation
AM_DISTCHECK_CONFIGURE_FLAGS = "--with-odp-lib=$(libodp_name)"
if WITH_ODP_PATH_GIVEN
AM_DISTCHECK_CONFIGURE_FLAGS += "--with-odp-path=$(with_odp_path)"
endif

SUBDIRS = src

if WITH_PROGRAMS
  SUBDIRS += programs
endif

if HAVE_PKGCONFIG
  SUBDIRS += pkgconfig
endif

if HAVE_DOXYGEN
  SUBDIRS += doc

  MOSTLYCLEANFILES = $(DX_CLEANFILES)
  @DX_RULES@

endif

EXTRA_DIST = bootstrap CHANGE_NOTES README
EXTRA_DIST += $(top_srcdir)/config/em-odp.conf

configdir = $(sysconfdir)/
config_DATA = $(top_srcdir)/config/em-odp.conf
