AC_PREREQ([2.69])
############################
# Version
############################
m4_define([em_version_api_major], [3])
m4_define([em_version_api_minor], [4])
m4_define([em_version_implementation], [0])
m4_define([em_version_fix], [0])

m4_define([em_version_api], [em_version_api_major.em_version_api_minor])
m4_define([em_version], [m4_if(m4_eval(em_version_fix), [0],
	  [em_version_api_major.em_version_api_minor.em_version_implementation],
	  [em_version_api_major.em_version_api_minor.em_version_implementation-em_version_fix])])

m4_define([em_version_lib],
	  m4_join([], em_version_api_major, em_version_api_minor))

m4_if(em_version_fix, [0],
      [m4_append([em_version_lib],
		 m4_combine([], [[]], [:], em_version_implementation, [0]))],
      [m4_append([em_version_lib],
		 m4_combine([], [[]], [:], em_version_implementation,
		 em_version_fix))])

m4_define([em_version_pkgconfig],
	  m4_join([], em_version_api_major, em_version_api_minor))

m4_if(em_version_fix, [0],
      [m4_append([em_version_pkgconfig],
		 m4_combine([], [[]], [.], em_version_implementation, [0]))],
      [m4_append([em_version_pkgconfig],
		 m4_combine([], [[]], [.], em_version_implementation,
		 em_version_fix))])

m4_define([em_version_package],
	  m4_join([.], [em_version_api_major], [em_version_api_minor],
		  [em_version_implementation],[em_version_fix]))

#AC_INIT([EM-ODP],[em_version_package],[EM at nokia.com])
AC_INIT([EM-ODP],[em_version],[EM at nokia.com])

# EM version numbers
EM_VERSION_API_MAJOR=em_version_api_major
AC_SUBST([EM_VERSION_API_MAJOR])
EM_VERSION_API_MINOR=em_version_api_minor
AC_SUBST([EM_VERSION_API_MINOR])
EM_VERSION_IMPLEMENTATION=em_version_implementation
AC_SUBST([EM_VERSION_IMPLEMENTATION])
EM_VERSION_FIX=em_version_fix
AC_SUBST([EM_VERSION_FIX])

# EM version strings
EM_VERSION_PACKAGE=em_version_package
AC_SUBST([EM_VERSION_PACKAGE])
EM_VERSION_API=em_version_api
AC_SUBST([EM_VERSION_API])
EM_VERSION=em_version
AC_SUBST([EM_VERSION])

# EM library and package versions
EM_VERSION_LIBSO=em_version_lib
AC_SUBST([EM_VERSION_LIBSO])
EM_VERSION_PKGCONFIG=em_version_pkgconfig
AC_SUBST([EM_VERSION_PKGCONFIG])

##########################################################################
# Test if user has set CFLAGS. Automake initializes CFLAGS to "-g -O2"
# by default.
##########################################################################
AS_IF([test "$ac_cv_env_CFLAGS_set" = ""], [user_cflags=0], [user_cflags=1])

AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror])
AC_CONFIG_SRCDIR([include/event_machine.h])
AC_CONFIG_HEADERS([config.h])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
AM_PROG_AR

# Use libtool
LT_INIT([])
AC_SUBST([LIBTOOL_DEPS])

m4_ifdef([PKG_PREREQ], [PKG_PREREQ(0.29)],
	 [m4_fatal([install pkg-config 0.29 or later before running autoconf/autogen])])
PKG_PROG_PKG_CONFIG([0.29])

# Checks for header files.
AC_CHECK_HEADERS([inttypes.h stdarg.h stddef.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T


##########################################################################
# Enable -fvisibility=hidden if CC supports it
##########################################################################
EM_VISIBILITY
# VISIBILITY_CFLAGS set

##########################################################################
# Check for pthreads availability
##########################################################################
EM_PTHREAD

LIBS="$PTHREAD_LIBS $LIBS"
AM_CFLAGS="$AM_CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_SEARCH_LIBS([timer_create],[rt posix4])

##########################################################################
# EM configuration file
##########################################################################
# Default EM configuration file
default_config_file="${srcdir}/config/em-odp.conf"

AC_CHECK_PROGS([REALPATH], [realpath])
AS_IF([test -z "$REALPATH"], [AC_MSG_ERROR([Could not find 'realpath'])])

# Set optional path for the default configuration file,
# i.e. override the file to be used for generating the built-in default
# EM configuration.
# Note that values set in the run-time config file, if given at startup, will
# overide the corresponding built-in config file values.
AC_ARG_WITH([config-file],
	    [AS_HELP_STRING([--with-config-file=FILE],
			    [Override the default EM configuration file
			     (file must include all EM configuration options)
			     [default=SRCDIR/config/em-odp.conf]])],
	    [default_config_file="${withval/#\~/$HOME}"], dnl replace ~ with $HOME
	    [])

rel_default_config_file=`realpath --relative-to=$(pwd) ${default_config_file}`
AC_SUBST([default_config_file])
AC_SUBST([rel_default_config_file])

EM_LIBCONFIG([$rel_default_config_file])

##########################################################################
# Default warning setup
##########################################################################
EM_CFLAGS="$EM_CFLAGS -W -Wall -Werror"

# Additional warnings:
EM_CHECK_CFLAG([-Wstrict-prototypes])
EM_CHECK_CFLAG([-Wmissing-prototypes])
EM_CHECK_CFLAG([-Wmissing-declarations])
EM_CHECK_CFLAG([-Wold-style-definition])
EM_CHECK_CFLAG([-Wpointer-arith])
EM_CHECK_CFLAG([-Wcast-align])
EM_CHECK_CFLAG([-Wnested-externs])
EM_CHECK_CFLAG([-Wcast-qual])
EM_CHECK_CFLAG([-Wformat-nonliteral])
EM_CHECK_CFLAG([-Wformat-security])
EM_CHECK_CFLAG([-Wundef])
EM_CHECK_CFLAG([-Wwrite-strings])
EM_CHECK_CFLAG([-Wno-vla]) # Allow variable-length arrays (vla), for clang.

# GCC 10,11 sometimes gets confused about object sizes and gives bogus warnings.
# Make the affected warnings generate only warnings, not errors.
AS_IF([test "$GCC" == yes],
      AS_IF([test `$CC -dumpversion | cut -d '.' -f 1` -ge 10],
	    EM_CHECK_CFLAG([-Wno-error=array-bounds])
      )
)

EM_CFLAGS="$EM_CFLAGS -std=c11"

# Extra flags for example to suppress certain warning types
EM_CFLAGS="$EM_CFLAGS $EM_CFLAGS_EXTRA"

EM_CPPFLAGS="$EM_CPPFLAGS -DENV_64_BIT -DEM_64_BIT"

##########################################################################
# Support cmpxchng16 on x86-based architectures
##########################################################################
EM_CHECK_CFLAG([-mcx16])

##########################################################################
# EM (em-odp) version information (if available)
##########################################################################
AS_IF([test -d $srcdir/.git],
      [commit=$(git -C $srcdir describe --long --always --dirty --tags)
       date=$(date '+%F %H:%M')
       build_info="$commit $date"],
      [build_info="Unknown"])
EM_BUILD_INFO="$build_info"
AC_SUBST(EM_BUILD_INFO)

EM_CFLAGS="$EM_CFLAGS -DEM_BUILD_INFO=\"$build_info\""

##########################################################################
# EM (em-odp) compile-time options
##########################################################################

# Override the EM-define value 'EM_CHECK_LEVEL' to 0...3 (yes=3)
# --enable-check-level=0...3     Set 'EM_CHECK_LEVEL' to the given
#                                value 0...3
# --enable-check-level=yes (=3)  Set 'EM_CHECK_LEVEL' to 3
# --enable-check-level           Set 'EM_CHECK_LEVEL' to 3
# --disable-check-level or
#   no option given              Use '#define EM_CHECK_LEVEL 0...3' from
#                                source code
check_level_info="no (EM_CHECK_LEVEL from source code)"
EM_CHECK_LEVEL=""
AC_ARG_ENABLE([check-level],
	      [AS_HELP_STRING([--enable-check-level[[=VAL]]],
			      [Override the 'EM_CHECK_LEVEL' define, valid
			       values are 0...3, yes(=3) or no value(=3)
			       [default=disabled, value from source code]])],
	      [AS_IF([test "$enableval" -ge 0 -a "$enableval" -le 3 2>/dev/null],
		     [check_level_info="yes:$enableval (EM_CHECK_LEVEL=$enableval)"
		      EM_CHECK_LEVEL="-DEM_CHECK_LEVEL=$enableval"],

		     [test "x$enableval" = "xyes"],
		     [check_level_info="yes:3 (EM_CHECK_LEVEL=$enableval)"
		      EM_CHECK_LEVEL="-DEM_CHECK_LEVEL=3"],

		     [test "x$enableval" != "xno"],
		     [AC_MSG_ERROR([bad value ${enableval} for --enable-check-level])]
		)
		EM_CPPFLAGS="$EM_CPPFLAGS $EM_CHECK_LEVEL"
	      ],[])
# Substitute @EM_CHECK_LEVEL@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_CHECK_LEVEL])

#
# Enable 'EM Event State Verification' (ESV)
#
# --enable-esv         Set 'EM_ESV_ENABLE=1' - i.e.  enable ESV
# --disable-esv        Set 'EM_ESV_ENABLE=0' - i.e. disable ESV
#   no option given    Use '#define EM_ESV_ENABLE 0|1' from source code
#
# Note: Using the option --enable-esv OR --disable-esv will _override_ the
#       source code define value for '#define EM_ESV_ENABLE 0|1'
#
esv_info="no (EM_ESV_ENABLE from source code)"
EM_ESV_ENABLE=""
AC_ARG_ENABLE([esv],
	      [AS_HELP_STRING([--enable-esv],
			      [Enable Event State Verification (ESV)
			       [default=value from source code '#define EM_ESV_ENABLE 0|1']])],
	      [AS_IF(dnl --enable-esv[=yes]:
		     [test "x$enableval" = "xyes"],
		     [esv_info="$enableval (EM_ESV_ENABLE=1)"
		      EM_ESV_ENABLE="-DEM_ESV_ENABLE=1"],
		     dnl --disable-esv OR --enable-esv=no:
		     [test "x$enableval" = "xno"],
		     [esv_info="$enableval (EM_ESV_ENABLE=0)"
		      EM_ESV_ENABLE="-DEM_ESV_ENABLE=0"],
		     dnl unsupported value given:
		     [AC_MSG_ERROR([bad value --enable-esv=${enableval}, use yes/no])]
		)
		EM_CPPFLAGS="$EM_CPPFLAGS $EM_ESV_ENABLE"
	      ],[])
# Substitute @EM_ESV_ENABLE@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_ESV_ENABLE])

##########################################################################
# Enable/disable EM_DEBUG_PRINT
##########################################################################
debug_print_info="no (EM_DEBUG_PRINT from source code)"
AC_ARG_ENABLE([debug-print],
	      [AS_HELP_STRING([--enable-debug-print],
			      [Display debugging information [default=disabled]])],
	      [AS_IF(dnl --enable-debug-print[=yes]:
		     [test "x$enableval" = "xyes"],
		     [debug_print_info="$enableval (EM_DEBUG_PRINT=1)"
		      EM_DEBUG_PRINT="-DEM_DEBUG_PRINT=1"],
		     dnl --disable-debug OR --enable-debug-print=no:
		     [test "x$enableval" = "xno"],
		     [debug_print_info="$enableval (EM_DEBUG_PRINT=0)"
		      EM_DEBUG_PRINT="-DEM_DEBUG_PRINT=0"],
		     dnl unsupported value given:
		     [AC_MSG_ERROR([bad value --enable-debug-print=${enableval}, use yes/no])]
		)
		EM_CPPFLAGS="$EM_CPPFLAGS $EM_DEBUG_PRINT"
	      ],[])
# Substitute @EM_DEBUG_PRINT@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_DEBUG_PRINT])

#########################################################################
# Override EM-define value 'EM_DEBUG_TIMESTAMP_ENABLE' to 0...2
# --enable-debug-timestamps=0...2   Set 'EM_DEBUG_TIMESTAMP_ENABLE' to the given
#                                   value (1=low overhead, 2=strict time)
# --enable-debug-timestamps<=yes (=1)  Set 'EM_DEBUG_TIMESTAMP_ENABLE' to 1
# --enable-debug-timestamps           Set 'EM_DEBUG_TIMESTAMP_ENABLE' to 1
# --disable-debug-timestamps or
#   no option given              Use '#define EM_DEBUG_TIMESTAMP_ENABLE 0...2' from
#                                source code (default 0)
debug_timestamps_info="no (EM_DEBUG_TIMESTAMP_ENABLE from source code)"
EM_DEBUG_TIMESTAMP_ENABLE=""
AC_ARG_ENABLE([debug-timestamps],
	      [AS_HELP_STRING([--enable-debug-timestamps[[=VAL]]],
			      [Override the 'EM_DEBUG_TIMESTAMP_ENABLE' define, valid
			       values are 0...2, yes(=1) or no value(=1), 2=strict time
			       [default=disabled, value from source code]])],
	      [AS_IF([test "$enableval" -ge 0 -a "$enableval" -le 2 2>/dev/null],
		     [debug_timestamps_info="yes:$enableval (EM_DEBUG_TIMESTAMP_ENABLE=$enableval)"
		      EM_DEBUG_TIMESTAMP_ENABLE="-DEM_DEBUG_TIMESTAMP_ENABLE=$enableval"],

		     [test "x$enableval" = "xyes"],
		     [debug_timestamps_info="yes:1 (EM_DEBUG_TIMESTAMP_ENABLE=$enableval)"
		      EM_DEBUG_TIMESTAMP_ENABLE="-DEM_DEBUG_TIMESTAMP_ENABLE=1"],

		     [test "x$enableval" != "xno"],
		     [AC_MSG_ERROR([bad value ${enableval} for --enable-debug-timestamps])]
		)
		EM_CPPFLAGS="$EM_CPPFLAGS $EM_DEBUG_TIMESTAMP_ENABLE"
	      ],[])
# Substitute @EM_DEBUG_TIMESTAMP_ENABLE@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_DEBUG_TIMESTAMP_ENABLE])

##########################################################################
# Set optional libcli path
##########################################################################
AC_ARG_WITH([libcli-path],
    [AS_HELP_STRING([--with-libcli-path=DIR],
        [path to libcli libs and headers [default=system]])],
    [libcli_path_given=yes
        LIBCLI_CPPFLAGS="-I$withval/include"
        LIBCLI_LIBS="-L$withval/lib"],
    [])

# CPPFLAGS and LIBS temporarily include the libcli path given above, enabling
# following AC_CHECK_HEADER(libcli.h) on the given non-standard path
OLD_CPPFLAGS=$CPPFLAGS
OLD_LIBS=$LIBS
CPPFLAGS="$LIBCLI_CPPFLAGS $CPPFLAGS"
LIBS="$LIBCLI_LIBS $LIBS"

#########################################################################
# If libcli is available, enable EM_CLI
#########################################################################
em_cli=no
EM_CLI="-DEM_CLI=0"
libodphelper_name=""
AC_CHECK_HEADER(libcli.h,
    [AC_CHECK_LIB(cli, cli_init,
    	[em_cli=yes EM_CLI="-DEM_CLI=1" libodphelper_name="libodphelper"],
	[], [-lcrypt])],
    [AS_IF([test "x$libcli_path_given" = "xyes"],
        [AC_MSG_ERROR([libcli not found at the given path (--with-libcli-path)])])])

# Substitute @EM_CLI@ and @libodphelper_name@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_CLI])
AC_SUBST([libodphelper_name])

EM_CPPFLAGS="$EM_CPPFLAGS $EM_CLI"

# Restore old saved variables
LIBS=$OLD_LIBS
CPPFLAGS=$OLD_CPPFLAGS

#########################################################################
# Enable EM idle hooks
#########################################################################
# --enable-idle-hooks		Set 'EM_IDLE_HOOKS_ENABLE=1'
# --disable-idle-hooks		Set 'EM_IDLE_HOOKS_ENABLE=0'
#   no option given    		Use '#define EM_IDLE_HOOKS_ENABLE 0|1' from source code
#
# Note: Using the option --enable-idle-hooks OR --disable-idle-hooks will _override_
#	the source code define value for '#define EM_IDLE_HOOKS_ENABLE 0|1'
#
idle_hooks_info="no (EM_IDLE_HOOKS_ENABLE from source code)"
EM_IDLE_HOOKS_ENABLE=""
AC_ARG_ENABLE([idle-hooks],
	      [AS_HELP_STRING([--enable-idle-hooks],
			      [Enable Idle Hooks
			       [default=value from source code '#define EM_IDLE_HOOKS_ENABLE 0|1']])],
	      [AS_IF(dnl --enable-idle-hooks[=yes]:
		     [test "x$enableval" = "xyes"],
		     [idle_hooks_info="$enableval (EM_IDLE_HOOKS_ENABLE=1)"
		      EM_IDLE_HOOKS_ENABLE="-DEM_IDLE_HOOKS_ENABLE=1"],
		     dnl --disable-idle-hooks OR --enable-idle-hooks=no:
		     [test "x$enableval" = "xno"],
		     [idle_hooks_info="$enableval (EM_IDLE_HOOKS_ENABLE=0)"
		      EM_IDLE_HOOKS_ENABLE="-DEM_IDLE_HOOKS_ENABLE=0"],
		     dnl unsupported value given:
		     [AC_MSG_ERROR([bad value --enable-idle-hooks=${enableval}, use yes/no])]
		)
		EM_CPPFLAGS="$EM_CPPFLAGS $EM_IDLE_HOOKS_ENABLE"
	      ],[])
# Substitute @EM_IDLE_HOOKS_ENABLE@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_IDLE_HOOKS_ENABLE])

#########################################################################
# Enable EM schedule-wait (scheduler wait for event) functionality.
#
# Enable the EM dispatcher/scheduler to start waiting for events if none are
# immediately available. Waiting for events, rather than busy-waiting, might
# save power.
#########################################################################
# --enable-sched-wait		EM calls odp_schedule_multi(..., sched_wait_ns, ...),
#				where 'sched_wait_ns' is taken from
#				'dispatch.sched_wait_ns = value' in the
#				EM config file (config/em-odp.conf).
#				Set 'EM_SCHED_WAIT_ENABLE=1'
# --disable--sched-wait		EM calls odp_schedule_multi_no_wait().
#				Set 'EM_SCHED_WAIT_ENABLE=0'
#   no option given    		Use '#define EM_SCHED_WAIT_ENABLE 0|1' from source code
#
# Note: Using the option --enable-sched-wait OR --disable-sched-wait will _override_
#	the source code define value for '#define EM_SCHED_WAIT_ENABLE 0|1'
#
sched_wait_info="no (EM_SCHED_WAIT_ENABLE from source code)"
EM_SCHED_WAIT_ENABLE=""
AC_ARG_ENABLE([sched-wait],
	      [AS_HELP_STRING([--enable-sched-wait],
			      [Enable scheduler wait-for-event functionality instead of no-wait
			       [default=value from source code '#define EM_SCHED_WAIT_ENABLE 0|1']])],
	      [AS_IF(dnl --enable-sched-wait[=yes]:
		     [test "x$enableval" = "xyes"],
		     [sched_wait_info="$enableval (EM_SCHED_WAIT_ENABLE=1)"
		      EM_SCHED_WAIT_ENABLE="-DEM_SCHED_WAIT_ENABLE=1"],
		     dnl --disable-sched-wait OR --enable-sched-wait=no:
		     [test "x$enableval" = "xno"],
		     [sched_wait_info="$enableval (EM_SCHED_WAIT_ENABLE=0)"
		      EM_SCHED_WAIT_ENABLE="-DEM_SCHED_WAIT_ENABLE=0"],
		     dnl unsupported value given:
		     [AC_MSG_ERROR([bad value --enable-sched-wait=${enableval}, use yes/no])]
		)
		EM_CPPFLAGS="$EM_CPPFLAGS $EM_SCHED_WAIT_ENABLE"
	      ],[])
# Substitute @EM_SCHED_WAIT_ENABLE@ into the pkgconfig file libemodp.pc.in
AC_SUBST([EM_SCHED_WAIT_ENABLE])

##########################################################################
# Default include setup
##########################################################################
AM_CFLAGS="$AM_CFLAGS $EM_CFLAGS"
AM_CXXFLAGS="-std=c++11"
AM_CPPFLAGS="$AM_CPPFLAGS $EM_CPPFLAGS"

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	programs/Makefile
	programs/bench/Makefile
	programs/common/Makefile
	programs/example/Makefile
	programs/example/hello/Makefile
	programs/example/add-ons/Makefile
	programs/example/api-hooks/Makefile
	programs/example/dispatcher/Makefile
	programs/example/error/Makefile
	programs/example/event_group/Makefile
	programs/example/fractal/Makefile
	programs/example/queue_group/Makefile
	programs/example/queue/Makefile
	programs/example/test/Makefile
	programs/packet_io/Makefile
	programs/performance/Makefile
	])

AC_CONFIG_FILES([
	include/event_machine/api/event_machine_version.h
	])

##########################################################################
# Architecture for ABI support
##########################################################################
AS_CASE([$host],
	[x86*],		[ARCH_ABI=x86_64-linux],
	[i686*],	[ARCH_ABI=x86_32-linux],
	[mips64*],	[ARCH_ABI=mips64-linux],
	[powerpc*],	[ARCH_ABI=power64-linux],
	[aarch64*],	[ARCH_ABI=arm64-linux],
	[arm*],		[ARCH_ABI=arm32-linux],
	[ARCH_ABI=undefined]
)
AC_SUBST([ARCH_ABI])

AS_IF([test "${ARCH_ABI}" == "undefined"],
      [AC_MSG_ERROR([ARCH_ABI is undefined,
		     please add your ARCH_ABI based on host=${host}])])

##########################################################################
# Enable/disable ABI compatible build
##########################################################################
EM_ABI_COMPAT=1
abi_compat=yes
AC_ARG_ENABLE([abi-compat],
	      [AS_HELP_STRING([--disable-abi-compat],
			      [disables ABI compatible mode
			       [default=enabled]])],
	      [AS_IF([test "x$enableval" = "xno"],
		     [EM_ABI_COMPAT=0
		      abi_compat=no
		      # do not try -march=native for clang due to possible
		      # failures on clang optimizations
		      $CC --version | grep -q clang
		      AS_IF([test $? -ne 0 -a "$user_cflags" -eq 0],
			    [EM_CHECK_CFLAG([-march=native])])
		     ])
	      ])
AM_CONDITIONAL(EM_ABI_COMPAT, [test "x$EM_ABI_COMPAT" = "x1"])

##########################################################################
# Enable/disable link time optimization
##########################################################################
lto_enabled=no
AC_ARG_ENABLE([lto],
	      [AS_HELP_STRING([--enable-lto],
			      [Enable Link Time Optimization (LTO)
			       in compiler and linker
			       [default=disabled]])],
	      [AS_IF([test "x$enableval" = "xyes"],
		     [lto_enabled=yes
		      # Fat LTO object file contains GIMPLE bytecodes and the
		      # usual final code. There are less build problems (e.g.
		      # due to older binutils), but object files are larger.
		      #
		      # -flto=auto and -ffat-lto-objects are currently not supported by clang.
		      $CC --version | grep -q clang
		      if test $? -ne 0; then
			EM_LTO_FLAGS="-flto=auto -ffat-lto-objects"
		      else
			EM_LTO_FLAGS="-flto"
		      fi
		     ])
	      ])
AC_SUBST([EM_LTO_FLAGS])
AM_CFLAGS="$AM_CFLAGS $EM_LTO_FLAGS"
AM_LDFLAGS="$AM_LDFLAGS $EM_LTO_FLAGS"

##########################################################################
# Build without example programs
##########################################################################
AC_ARG_WITH([programs],
	    [AS_HELP_STRING([--without-programs],
			    [build without example programs [default=with]])],
	    [],
	    [with_programs=yes])
AM_CONDITIONAL([WITH_PROGRAMS], [test x$with_programs != xno])

##########################################################################
# Build programs/examples/tests dynamically (static is default)
##########################################################################
AC_ARG_ENABLE([static-applications],
	      [AS_HELP_STRING([--disable-static-applications],
			      [disable static linking of internal
			       em-odp example programs
			       [default=enabled=static]])],
	      [],
	      [enable_static_applications=yes])
AS_IF([test "x$enable_static" != "xno" -a "x$enable_static_applications" != "xno"],
      [enable_static_applications=yes], [enable_static_applications=no])

AM_CONDITIONAL([STATIC_APPS], [test "x$enable_static_applications" != "xno"])

##########################################################################
# ODP library path and name
##########################################################################
# Optional configure parameter for a non-standard install prefix of ODP
AC_ARG_WITH([odp-path],
	    [AS_HELP_STRING([--with-odp-path=prefix],
			    [non-standard install prefix of odp])],
	    [export PKG_CONFIG_PATH="$with_odp_path/lib/pkgconfig:$PKG_CONFIG_PATH"],
	    [])
AM_CONDITIONAL([WITH_ODP_PATH_GIVEN], [test "x$with_odp_path" != "xno"])
AC_SUBST([with_odp_path])

# Optional configure parameter for setting the ODP library name
# (actually the name of the corresponing .pc file).
# Defaults to "libodp-linux" if nothing else given.
libodp_name="libodp-linux"
AC_ARG_WITH([odp-lib],
	    [AS_HELP_STRING([--with-odp-lib=odp-library-name],
			    [name of the odp library (and .pc file)]
			    [[default=libodp-linux]])],
	    [AS_IF([test "x$with_odp_lib" != "xyes"],
		   [libodp_name=$withval])],
	    [])

AS_IF([test "x$libodp_name" = "xno"],
      [AC_MSG_ERROR([--with-odp-lib: Please provide ODP lib name])])
AC_SUBST([libodp_name])

##########################################################################
# Get configuration for libodp with pkg-config
##########################################################################
AC_CHECK_PROGS([PKGCONFIG], [pkg-config])
AS_IF([test -z "$PKGCONFIG"],
      [AC_MSG_ERROR([Please install pkg-config])])

PKG_CHECK_MODULES([ODP], [$libodp_name])
AC_SUBST([ODP_CFLAGS])
AC_SUBST([ODP_LIBS])

PKG_CHECK_MODULES([ODPHELPER], [libodphelper])
AC_SUBST([ODPHELPER_CFLAGS])
AC_SUBST([ODPHELPER_LIBS])

PKG_CHECK_MODULES_STATIC([ODP_STATIC], [$libodp_name])
AC_SUBST([ODP_STATIC_CFLAGS])
AC_SUBST([ODP_STATIC_LIBS])

PKG_CHECK_MODULES_STATIC([ODPHELPER_STATIC], [libodphelper])
AC_SUBST([ODPHELPER_STATIC_CFLAGS])
AC_SUBST([ODPHELPER_STATIC_LIBS])

ODP_VERSION=$(pkg-config --modversion $libodp_name)

##########################################################################
# Pkg-config configuration for libemodp.pc
##########################################################################
AM_CONDITIONAL([HAVE_PKGCONFIG], [test -n "$PKGCONFIG"])
AM_COND_IF([HAVE_PKGCONFIG], [AC_CONFIG_FILES([pkgconfig/Makefile])])
AM_COND_IF([HAVE_PKGCONFIG], [AC_CONFIG_FILES([pkgconfig/libemodp.pc])])

##########################################################################
# Doxygen Configuration
##########################################################################
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AS_IF([test -z "$DOXYGEN"],
      [AC_MSG_WARN([Doxygen not found - continue without Doxygen support])])

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])

AS_IF([test "x$enable_doxygen" != "xno"],
      [DX_HTML_FEATURE(ON)
       DX_PDF_FEATURE(OFF)
       DX_PS_FEATURE(OFF)
       DX_INIT_DOXYGEN($PACKAGE_NAME, ${srcdir}/doc/Doxyfile, ${builddir}/doc/event_machine_api)
       AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/Makefile])])
      ])

##########################################################################
# distribute the changed variables among the Makefiles
##########################################################################

AC_SUBST([LIBS])
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

CC_VERSION=$($CC --version | head -n 1)

AC_OUTPUT
AC_MSG_RESULT([
	$PACKAGE $VERSION
	=================
	EM API version:		${EM_VERSION_API}
	EM (em-odp) version:	${EM_VERSION}
	Library version:	${EM_VERSION_LIBSO}

	host:			${host}
	ARCH_ABI		${ARCH_ABI}
	prefix:			${prefix}
	sysconfdir:		${sysconfdir}
	libdir:			${libdir}
	includedir:		${includedir}

	odp-library:		${libodp_name}
	odp-version:		${ODP_VERSION}

	odp-cflags:		${ODP_CFLAGS}
	odp-libs:		${ODP_LIBS}
	odphelper-cflags:	${ODPHELPER_CFLAGS}
	odphelper-libs:		${ODPHELPER_LIBS}

	odp-static-cflags:	${ODP_STATIC_CFLAGS}
	odp-static-libs:	${ODP_STATIC_LIBS}
	odphelper-static-cflags:${ODPHELPER_STATIC_CFLAGS}
	odphelper-static-libs:	${ODPHELPER_STATIC_LIBS}

	cc:			${CC}
	cc version:		${CC_VERSION}
	cppflags:		${CPPFLAGS}
	am_cppflags:		${AM_CPPFLAGS}
	cflags:			${CFLAGS}
	am_cflags:		${AM_CFLAGS}
	am_cxxflags:		${AM_CXXFLAGS}
	ld:			${LD}
	ldflags:		${LDFLAGS}
	am_ldflags:		${AM_LDFLAGS}
	libs:			${LIBS}
	defs:			${DEFS}
	static libraries:	${enable_static}
	shared libraries:	${enable_shared}
	ABI compatible:		${abi_compat}
	link time optimization:	${lto_enabled}
	example programs:	${with_programs}
	example static linkage:	${enable_static_applications}
	default config-file:	${default_config_file}

	EM Compile Time Options
	=======================
	EM check level:		${check_level_info}
	EM ESV:			${esv_info}
	EM debug print:		${debug_print_info}
	EM debug timestamps:	${debug_timestamps_info}
	EM CLI:			${em_cli}
	EM idle hooks:		${idle_hooks_info}
	EM scheduler wait:	${sched_wait_info}
	])
