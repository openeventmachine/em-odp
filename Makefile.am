ACLOCAL_AMFLAGS=-I m4
AUTOMAKE_OPTIONS = foreign
SUBDIRS = src programs

if HAVE_PKGCONFIG
  SUBDIRS += pkgconfig
endif

if HAVE_DOXYGEN
  SUBDIRS += doc

  MOSTLYCLEANFILES = $(DX_CLEANFILES)
  @DX_RULES@

endif

configdir = $(sysconfdir)/
config_DATA = $(top_srcdir)/config/em-odp.conf
