include $(top_srcdir)/programs/Makefile.inc

noinst_PROGRAMS = event_group \
		  event_group_assign_end \
		  event_group_chaining \
		  event_group_abort

event_group_LDFLAGS = $(AM_LDFLAGS) -static
event_group_CFLAGS = $(AM_CFLAGS)

event_group_assign_end_LDFLAGS = $(AM_LDFLAGS) -static
event_group_assign_end_CFLAGS = $(AM_CFLAGS)

event_group_chaining_LDFLAGS = $(AM_LDFLAGS) -static
event_group_chaining_CFLAGS = $(AM_CFLAGS)

event_group_abort_LDFLAGS = $(AM_LDFLAGS) -static
event_group_abort_CFLAGS = $(AM_CFLAGS)

dist_event_group_SOURCES = event_group.c
dist_event_group_assign_end_SOURCES = event_group_assign_end.c
dist_event_group_chaining_SOURCES = event_group_chaining.c
dist_event_group_abort_SOURCES = event_group_abort.c
