LIB = $(top_builddir)/lib

dist_pkgdata_DATA = $(LIB)/libemodp.la

VPATH = $(srcdir) $(builddir)

lib_LTLIBRARIES = $(LIB)/libemodp.la

AM_CFLAGS += -I$(top_srcdir)/include
AM_CFLAGS += -I$(top_srcdir)/include/event_machine/add-ons
AM_CFLAGS += -I$(top_srcdir)/src/misc

AM_CFLAGS += $(ODP_CFLAGS)
AM_CFLAGS += $(LIBCONFIG_CFLAGS)
AM_CFLAGS += $(VISIBILITY_CFLAGS) # -fvisibility=hidden set if supported

AM_LDFLAGS += -version-number '$(EMODP_LIBSO_VERSION)'

DISTCLEANFILES = $(top_builddir)/include/em_libconfig_config.h
include/em_libconfig_config.h: $(top_builddir)/$(rel_default_config_file) $(top_builddir)/config.status
	cd $(top_builddir) && $(SHELL) ./config.status $@

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

BUILT_SOURCES = include/em_libconfig_config.h

__LIB__libemodp_la_SOURCES = \
event_machine_atomic_group.c \
event_machine_core.c \
event_machine_dispatcher.c \
event_machine_eo.c \
event_machine_error.c \
event_machine_event_group.c \
event_machine_event.c \
event_machine_helper.c \
event_machine_hooks.c \
event_machine_hw_specific.c \
event_machine_init.c \
event_machine_odp_ext.c \
event_machine_pool.c \
event_machine_queue_group.c \
event_machine_queue.c \
event_machine_scheduler.c \
	\
em_atomic_group.c \
em_atomic_group.h \
em_atomic_group_types.h \
	\
em_atomic.h \
	\
em_chaining.c \
em_chaining.h \
em_chaining_types.h \
	\
em_core.c \
em_core.h \
em_core_types.h \
	\
em_daemon_eo.c \
em_daemon_eo.h \
em_daemon_eo_types.h \
	\
em_dispatcher.c \
em_dispatcher.h \
em_dispatcher_types.h \
	\
em_eo.c \
em_eo.h \
em_eo_types.h \
	\
em_error.c \
em_error.h \
em_error_types.h \
	\
em_event.c \
em_event.h \
em_event_types.h \
em_event_state.c \
em_event_state.h \
	\
em_event_group.c \
em_event_group.h \
em_event_group_types.h \
	\
em_hooks.c \
em_hooks.h \
em_hook_types.h \
	\
em_include.h \
	\
em_info.c \
em_info.h \
	\
em_init.c \
em_init.h \
	\
em_internal_event.c \
em_internal_event.h \
em_internal_event_types.h \
	\
em_libconfig.c \
em_libconfig.h \
em_libconfig_types.h \
	\
em_mem.h \
	\
em_pool.c \
em_pool.h \
em_pool_types.h \
	\
em_queue.c \
em_queue.h \
em_queue_types.h \
	\
em_queue_group.c \
em_queue_group.h \
em_queue_group_types.h \
	\
em_sync_api_types.h \
	\
env/env_sharedmem.c \
	\
misc/objpool.c \
misc/objpool.h \
misc/list.h \
	\
add-ons/event_timer/event_machine_timer.c \
add-ons/event_timer/em_timer.c \
add-ons/event_timer/em_timer_conf.h \
add-ons/event_timer/em_timer.h \
add-ons/event_timer/em_timer_types.h

__LIB__libemodp_la_LIBADD = $(ODP_LIBS)
__LIB__libemodp_la_LIBADD += $(LIBCONFIG_LIBS)
