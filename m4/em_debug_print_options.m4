#
# EM_DEBUG_PRINT_OPTIONS macro to manage EM debug print configurations.
#
# Debug print configure option summary (configure --enable/--disable-*):
# Defaults (no related options):
#   EM_DEBUG_PRINT_ALL=0, EM_DEBUG_PRINT_TIMER=0, EM_DEBUG_PRINT_BASE=0
#
# --enable-debug-print-all (alias: --enable-debug-print):
#   Sets EM_DEBUG_PRINT_ALL=1 and initializes _TIMER=1, _BASE=1 unless later
#   overridden by per-flag options.
# --disable-debug-print-all:
#   Sets EM_DEBUG_PRINT_ALL=0 (individual flags remain at their current
#   values; if still defaults they stay 0). Does NOT modify any subflag.
#
# --enable-debug-print-timer:
#   Sets EM_DEBUG_PRINT_TIMER=1 (does not modify EM_DEBUG_PRINT_ALL).
# --disable-debug-print-timer:
#   Sets EM_DEBUG_PRINT_TIMER=0 and forces EM_DEBUG_PRINT_ALL=0 (cannot have
#   _ALL=1 while a subflag is disabled).
#
# --enable-debug-print-base:
#   Sets EM_DEBUG_PRINT_BASE=1 (does not modify EM_DEBUG_PRINT_ALL).
# --disable-debug-print-base:
#   Sets EM_DEBUG_PRINT_BASE=0 and forces EM_DEBUG_PRINT_ALL=0.
#
# Ordering & precedence:
#   1) Global _ALL option processed first; it seeds subflags to 1 when enabled.
#   2) Individual per-flag options override the seeded values.
#   3) Disabling any subflag after enabling _ALL resets _ALL to 0 to reflect a
#      non-uniform state.
#
# Conflict:
#   Providing both --enable-debug-print and --enable-debug-print-all with
#   differing yes/no values causes a configure error.
#
# Interpretation:
#   EM_DEBUG_PRINT_ALL=1 means all current debug print subflags are enabled.
#   If any subflag is explicitly disabled, EM_DEBUG_PRINT_ALL is reset to 0.

AC_DEFUN([EM_DEBUG_PRINT_OPTIONS],[

# Set default values for all EM_DEBUG_PRINT_... flags
EM_DEBUG_PRINT_ALL=0   # default:disabled
EM_DEBUG_PRINT_TIMER=0 # default:disabled
EM_DEBUG_PRINT_BASE=0  # default:disabled

##########################################################################
# Enable/disable EM_DEBUG_PRINT_ALL
#
# Sets all EM Debug Print flags on or off unless explicitly overridden,
# see event_machine_config.h/.in
##########################################################################

# Alias: --enable-debug-print is an alias for --enable-debug-print-all for
#          backwards compatibility
AC_ARG_ENABLE([debug-print],
	      [AS_HELP_STRING([--enable-debug-print],
			      [Alias for --enable-debug-print-all])],
	      [enable_debug_print="$enableval"])

# Main: --enable-debug-print-all
AC_ARG_ENABLE([debug-print-all],
	      [AS_HELP_STRING([--enable-debug-print-all],
			      [Print all debug information [default=disabled]])],
	      [enable_debug_print_all="$enableval"])

# Conflict check & normalization:
# 1) If both debug-print and debug-print-all given and differ -> error
AS_IF([test "x$enable_debug_print" != "x" &&
       test "x$enable_debug_print_all" != "x"],
      [AS_IF([test "x$enable_debug_print" != "x$enable_debug_print_all"],
	     [AC_MSG_ERROR([conflicting values:
			    --enable-debug-print=$enable_debug_print
			    --enable-debug-print-all=$enable_debug_print_all])])
      ])
# 2) If only alias given -> propagate to main variable
AS_IF([test "x$enable_debug_print" != "x" &&
       test "x$enable_debug_print_all" = "x"],
      [enable_debug_print_all="$enable_debug_print"])

# Process enable_debug_print_all (empty -> keep default 0)
AS_IF([test "x$enable_debug_print_all" = "x"],
      dnl empty: leave EM_DEBUG_PRINT_ALL at default value
      [],
      [AS_IF([test "x$enable_debug_print_all" = "xyes"],
	     [EM_DEBUG_PRINT_ALL=1],
	     [test "x$enable_debug_print_all" = "xno"],
	     [EM_DEBUG_PRINT_ALL=0],
	     [AC_MSG_ERROR([bad value '$enable_debug_print_all' for \
			    --enable-debug-print/-all (use yes|no)])]
	    )
      ])
# Call AC_SUBST([EM_DEBUG_PRINT_ALL]) at the end after possible overrides

# If _ALL enabled, force _TIMER & _BASE to 1
# Note that this override happens before individual options below,
# so that user can still disable them individually after enabling _ALL.
AS_IF([test $EM_DEBUG_PRINT_ALL -eq 1],
      [EM_DEBUG_PRINT_TIMER=1; EM_DEBUG_PRINT_BASE=1])

##########################################################################
# Enable/disable EM_DEBUG_PRINT_TIMER
#
# Note: Implicitly enabled if EM_DEBUG_PRINT_ALL is enabled,
#       explicitly disabling EM_DEBUG_PRINT_TIMER also disables
#       EM_DEBUG_PRINT_ALL.
# See event_machine_config.h/.in
##########################################################################
AC_ARG_ENABLE([debug-print-timer],
	      [AS_HELP_STRING([--enable-debug-print-timer],
			      [Print timer debug information [default=disabled]])],
	      [AS_IF(dnl --enable-debug-print-timer[=yes]:
		     [test "x$enableval" = "xyes"],
		     [EM_DEBUG_PRINT_TIMER=1],
		     dnl --disable-debug-print-timer OR --enable-debug-print-timer=no:
		     dnl note: also disable _ALL if _TIMER is disabled
		     [test "x$enableval" = "xno"],
		     [EM_DEBUG_PRINT_TIMER=0; EM_DEBUG_PRINT_ALL=0],
		     dnl unsupported value given:
		     [AC_MSG_ERROR([bad value --enable-debug-print-timer=${enableval}, use yes/no])]
		)
	      ],[])
# Call AC_SUBST([EM_DEBUG_PRINT_TIMER]) at the end after possible overrides

##########################################################################
# Enable/disable EM_DEBUG_PRINT_BASE
#
# Note: Implicitly enabled if EM_DEBUG_PRINT_ALL is enabled,
#       explicitly disabling EM_DEBUG_PRINT_BASE also disables
#       EM_DEBUG_PRINT_ALL.
# See event_machine_config.h/.in
##########################################################################
AC_ARG_ENABLE([debug-print-base],
	      [AS_HELP_STRING([--enable-debug-print-base],
			      [Print base debug information [default=disabled]])],
	      [AS_IF(dnl --enable-debug-print-base[=yes]:
		     [test "x$enableval" = "xyes"],
		     [EM_DEBUG_PRINT_BASE=1],
		     dnl --disable-debug-base OR --enable-debug-print-base=no:
		     dnl note: also disable _ALL if _BASE is disabled
		     [test "x$enableval" = "xno"],
		     [EM_DEBUG_PRINT_BASE=0; EM_DEBUG_PRINT_ALL=0],
		     dnl unsupported value given:
		     [AC_MSG_ERROR([bad value --enable-debug-print-base=${enableval}, use yes/no])]
		)
	      ],[])
# Call AC_SUBST([EM_DEBUG_PRINT_BASE]) at the end after possible overrides

# Update info strings after possible _ALL override
debug_print_all_info="EM_DEBUG_PRINT_ALL=$EM_DEBUG_PRINT_ALL"
debug_print_timer_info="EM_DEBUG_PRINT_TIMER=$EM_DEBUG_PRINT_TIMER"
debug_print_base_info="EM_DEBUG_PRINT_BASE=$EM_DEBUG_PRINT_BASE"

# Substitute all @EM_DEBUG_PRINT_...@ defines into event_machine_config.h.in
AC_SUBST([EM_DEBUG_PRINT_ALL])
AC_SUBST([EM_DEBUG_PRINT_TIMER])
AC_SUBST([EM_DEBUG_PRINT_BASE])
])
