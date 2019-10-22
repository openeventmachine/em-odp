#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
############################
# Version
############################
m4_define([em_api_major_version], [2])
m4_define([em_api_minor_version], [1])
m4_define([em_api_implementation_version], [0])
m4_define([em_api_implementation_fix], [0])

m4_define([em_api_version], [em_api_major_version.em_api_minor_version])
m4_define([em_version], [m4_if(m4_eval(em_api_implementation_fix), [0],
	  [em_api_major_version.em_api_minor_version.em_api_implementation_version],
	  [em_api_major_version.em_api_minor_version.em_api_implementation_version-em_api_implementation_fix])])

m4_define([em_lib_version], m4_join([], em_api_major_version, em_api_minor_version))
m4_if(em_api_implementation_fix, [0],
      [m4_append([em_lib_version], m4_combine([], [[]], [:], em_api_implementation_version, [0]))],
      [m4_append([em_lib_version], m4_combine([], [[]], [:], em_api_implementation_version, em_api_implementation_fix))])

AC_INIT([EM-ODP],[em_version],[foo at nokia.com])

EM_API_MAJOR_VERSION=em_api_major_version
AC_SUBST(EM_API_MAJOR_VERSION)
EM_API_MINOR_VERSION=em_api_minor_version
AC_SUBST(EM_API_MINOR_VERSION)
EM_API_IMPLEMENTATION_VERSION=em_api_implementation_version
AC_SUBST(EM_API_IMPLEMENTATION_VERSION)

EM_API_VERSION=em_api_version
AC_SUBST(EM_API_VERSION)

EM_VERSION=em_version
AC_SUBST(EMODP_VERSION)

AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror])
AC_CONFIG_SRCDIR([include/event_machine.h])
AC_CONFIG_HEADERS([config.h])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])

############################
# Set em-odp library version
############################
EMODP_LIBSO_VERSION=em_lib_version
AC_SUBST(EMODP_LIBSO_VERSION)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
AM_PROG_AR

# Use libtool
LT_INIT([])
AC_SUBST([LIBTOOL_DEPS])
AM_PROG_LIBTOOL

PKG_PROG_PKG_CONFIG

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
# Check for pthreads availability
##########################################################################

EM_PTHREAD([], [echo "Error! We require pthreads to be available"
		exit -1])
LIBS="$PTHREAD_LIBS $LIBS"
AM_CFLAGS="$AM_CFLAGS $PTHREAD_CFLAGS"
AM_LDFLAGS="$AM_LDFLAGS $PTHREAD_LDFLAGS"
CC="$PTHREAD_CC"

AC_SEARCH_LIBS([timer_create],[rt posix4])

EM_LIBCONFIG([])

##########################################################################
# Default warning setup
##########################################################################
EM_CFLAGS="$EM_CFLAGS -W -Wall -Werror -Wstrict-prototypes -Wmissing-prototypes"
EM_CFLAGS="$EM_CFLAGS -Wmissing-declarations -Wold-style-definition -Wpointer-arith"
EM_CFLAGS="$EM_CFLAGS -Wcast-align -Wnested-externs -Wcast-qual -Wformat-nonliteral"
EM_CFLAGS="$EM_CFLAGS -Wformat-security -Wundef -Wwrite-strings"
EM_CFLAGS="$EM_CFLAGS -std=c99"
# Extra flags for example to suppress certain warning types
EM_CFLAGS="$EM_CFLAGS $EM_CFLAGS_EXTRA"

EM_CPPFLAGS="$EM_CPPFLAGS -DENV_64_BIT -DEM_64_BIT"

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

if test "${ARCH_ABI}" == "undefined";
then
	echo "ARCH_ABI is undefined, please add your ARCH_ABI based on host=${host}"
	exit 1
fi

##########################################################################
# Enable/disable ABI compatible build
##########################################################################
EM_ABI_COMPAT=1
abi_compat=yes
AC_ARG_ENABLE([abi-compat],
	[AS_HELP_STRING([--disable-abi-compat],
			[disables ABI compatible mode])],
	[if test "x$enableval" = "xno"; then
		EM_ABI_COMPAT=0
		abi_compat=no
		AM_CFLAGS="$AM_CFLAGS -march=native"
	fi])
AM_CONDITIONAL(EM_ABI_COMPAT, [test "x$EM_ABI_COMPAT" = "x1"])

##########################################################################
# Enable/disable link time optimization
##########################################################################
lto_enabled=no
AC_ARG_ENABLE([lto],
	[AS_HELP_STRING([--enable-lto],
			[Enable Link Time Optimization (LTO) in compiler and linker])],
	[if test "x$enableval" = "xyes"; then
		lto_enabled=yes
		# Fat LTO object file contains GIMPLE bytecodes and the usual
		# final code. There are less build problems (e.g. due to older
		# binutils), but object files are larger.
		EM_LTO_FLAGS="-flto -ffat-lto-objects"
	fi])
AC_SUBST(EM_LTO_FLAGS)
AM_CFLAGS="$AM_CFLAGS $EM_LTO_FLAGS"
AM_LDFLAGS="$AM_LDFLAGS $EM_LTO_FLAGS"

##########################################################################
# adding the ODP library (e.g. with static name 'libodp-linux.a')
##########################################################################
# Optional configure parameter for a non-standard install prefix of ODP
AC_ARG_WITH([odp-path],
	[AS_HELP_STRING([--with-odp-path=prefix],
			[non-standard install prefix of odp])],
	[# extend the compiler and linker flags according to the path set
	 AM_CPPFLAGS="$AM_CPPFLAGS -I$with_odp_path/include"
	 AM_CPPFLAGS="$AM_CPPFLAGS -I$with_odp_path/include/odp/arch/$ARCH_ABI"
	 AM_LDFLAGS="$AM_LDFLAGS -L$with_odp_path/lib"
	 export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$with_odp_path/lib/pkgconfig"],
	[])

##########################################################################
# Get configuration from libodp-linux.pc with pkg-config
##########################################################################
AC_CHECK_PROGS([PKGCONFIG], [pkg-config])
if test -z "$PKGCONFIG"; then
	AC_MSG_ERROR([Please install pkg-config])
fi

if test "x$enable_static" = "xyes" ; then
	export PKG_CONFIG="pkg-config --static"
fi

PKG_CHECK_MODULES([ODP], [libodp-linux])
AC_SUBST([ODP_CFLAGS])
AC_SUBST([ODP_LIBS])

PKG_CHECK_MODULES([ODPHELPER], [libodphelper])
AC_SUBST([ODPHELPER_CFLAGS])
AC_SUBST([ODPHELPER_LIBS])

ODP_VERSION=$(pkg-config --modversion libodp-linux)

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
if test -z "$DOXYGEN"; then
	AC_MSG_WARN([Doxygen not found - continue without Doxygen support])
fi

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])

if test "x$enable_doxygen" != "xno"; then
	DX_INIT_DOXYGEN($PACKAGE, ${srcdir}/doc/Doxyfile, ${builddir}/doc/event_machine_api)
	AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/Makefile])])

	DX_HTML_FEATURE(ON)
	DX_PDF_FEATURE(OFF)
	DX_PS_FEATURE(OFF)
fi

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
	EM API version:		${EM_API_VERSION}
	EM ODP version:		${EM_VERSION}
	Library version:	${EMODP_LIBSO_VERSION}

	host:			${host}
	ARCH_ABI		${ARCH_ABI}
	prefix:			${prefix}
	sysconfdir:		${sysconfdir}
	libdir:			${libdir}
	includedir:		${includedir}

	odp_version:		${ODP_VERSION}
	odp_cflags:		${ODP_CFLAGS}
	odp_libs:		${ODP_LIBS}
	odphelper_cflags:	${ODPHELPER_CFLAGS}
	odphelper_libs:		${ODPHELPER_LIBS}

	cc:			${CC}
	cc version:		${CC_VERSION}
	cppflags:		${CPPFLAGS}
	am_cppflags:		${AM_CPPFLAGS}
	cflags:			${CFLAGS}
	am_cflags:		${AM_CFLAGS}
	am_cxxflags:		${AM_CXXFLAGS}
	ldflags:		${LDFLAGS}
	am_ldflags:		${AM_LDFLAGS}
	static libraries:	${enable_static}
	shared libraries:	${enable_shared}
	ABI compatible:		${abi_compat}
	link time optimization:	${lto_enabled}
	])
