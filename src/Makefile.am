LIB = $(top_builddir)/lib

dist_pkgdata_DATA = $(LIB)/libemodp.la

VPATH = $(srcdir) $(builddir)

lib_LTLIBRARIES = $(LIB)/libemodp.la

AM_CFLAGS += -I$(top_srcdir)/include
AM_CFLAGS += -I$(top_srcdir)/include/event_machine/add-ons
AM_CFLAGS += -I$(top_srcdir)/src/misc

AM_CFLAGS += $(ODP_CFLAGS)

AM_CFLAGS +=  $(LIBCONFIG_CFLAGS)

AM_LDFLAGS += -version-number '$(EMODP_LIBSO_VERSION)'

include_HEADERS = \
$(top_srcdir)/include/event_machine.h

emincludedir = $(includedir)/event_machine/api
eminclude_HEADERS = \
$(top_srcdir)/include/event_machine/api/event_machine_atomic_group.h \
$(top_srcdir)/include/event_machine/api/event_machine_core.h \
$(top_srcdir)/include/event_machine/api/event_machine_dispatcher.h \
$(top_srcdir)/include/event_machine/api/event_machine_eo.h \
$(top_srcdir)/include/event_machine/api/event_machine_error.h \
$(top_srcdir)/include/event_machine/api/event_machine_event.h \
$(top_srcdir)/include/event_machine/api/event_machine_event_group.h \
$(top_srcdir)/include/event_machine/api/event_machine_queue.h \
$(top_srcdir)/include/event_machine/api/event_machine_queue_group.h \
$(top_srcdir)/include/event_machine/api/event_machine_scheduler.h \
$(top_srcdir)/include/event_machine/api/event_machine_types.h

emhelperincludedir = $(includedir)/event_machine/helper
emhelperinclude_HEADERS = \
$(top_srcdir)/include/event_machine/helper/event_machine_helper.h

emaddonsincludedir = $(includedir)/event_machine/add-ons
emaddonsinclude_HEADERS = \
$(top_srcdir)/include/event_machine/add-ons/event_machine_timer.h \
$(top_srcdir)/include/event_machine/add-ons/event_machine_add-on_error.h

emincludeplatformdir = $(includedir)/event_machine/platform
emincludeplatform_HEADERS = \
$(top_srcdir)/include/event_machine/platform/event_machine_config.h \
$(top_srcdir)/include/event_machine/platform/event_machine_hw_config.h \
$(top_srcdir)/include/event_machine/platform/event_machine_hw_specific.h \
$(top_srcdir)/include/event_machine/platform/event_machine_hw_types.h \
$(top_srcdir)/include/event_machine/platform/event_machine_hooks.h \
$(top_srcdir)/include/event_machine/platform/event_machine_init.h \
$(top_srcdir)/include/event_machine/platform/event_machine_pool.h \
$(top_srcdir)/include/event_machine/platform/event_machine_odp_ext.h

emaddonsincludeplatformdir = $(includedir)/event_machine/platform/add-ons
emaddonsincludeplatform_HEADERS = \
$(top_srcdir)/include/event_machine/platform/add-ons/event_machine_timer_hw_specific.h

emenvincludedir = $(includedir)/event_machine/platform/env
emenvinclude_HEADERS = \
$(top_srcdir)/include/event_machine/platform/env/environment.h \
$(top_srcdir)/include/event_machine/platform/env/env_atomic.h \
$(top_srcdir)/include/event_machine/platform/env/env_barrier.h \
$(top_srcdir)/include/event_machine/platform/env/env_bitmask.h \
$(top_srcdir)/include/event_machine/platform/env/env_conf.h \
$(top_srcdir)/include/event_machine/platform/env/env_macros.h \
$(top_srcdir)/include/event_machine/platform/env/env_sharedmem.h \
$(top_srcdir)/include/event_machine/platform/env/env_spinlock.h \
$(top_srcdir)/include/event_machine/platform/env/env_time.h

__LIB__libemodp_la_SOURCES = \
event_machine_atomic_group.c \
event_machine_core.c \
event_machine_dispatcher.c \
event_machine_eo.c \
event_machine_error.c \
event_machine_event.c \
event_machine_event_group.c \
event_machine_helper.c \
event_machine_hw_specific.c \
event_machine_hooks.c \
event_machine_init.c \
event_machine_pool.c \
event_machine_queue.c \
event_machine_queue_group.c \
event_machine_scheduler.c \
event_machine_odp_ext.c \
	\
em_atomic_group.c \
em_chaining.c \
em_core.c \
em_daemon_eo.c \
em_dispatcher.c \
em_eo.c \
em_error.c \
em_event.c \
em_event_group.c \
em_hooks.c \
em_info.c \
em_internal_event.c \
em_pool.c \
em_queue.c \
em_queue_group.c \
em_libconfig.c \
	\
env/env_sharedmem.c \
misc/objpool.c \
	\
	\
add-ons/event_timer/event_machine_timer.c \
add-ons/event_timer/em_timer.c

__LIB__libemodp_la_LIBADD = $(ODP_LIBS)
__LIB__libemodp_la_LIBADD += $(LIBCONFIG_LIBS)
