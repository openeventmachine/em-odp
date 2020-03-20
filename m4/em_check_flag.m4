# Copyright (c) 2020 Nokia Solutions and Networks
# Copyright (c) 2018, Linaro Limited
# All rights reserved.
#
# SPDX-License-Identifier:     BSD-3-Clause
#

dnl Use -Werror in the checks below since Clang emits a warning instead of
dnl an error when it encounters an unknown warning option.

# EM_CHECK_CFLAG(FLAG)
# ---------------------
# Add FLAG to EM_CFLAGS if compiler supports that option
AC_DEFUN([EM_CHECK_CFLAG],
[
	  AC_MSG_CHECKING([if $CC supports $1])
	  AC_LANG_PUSH([C])
	  saved_cflags="$CFLAGS"
	  CFLAGS="-W -Wall -Werror $EM_CFLAGS $1"
	  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
		[AC_MSG_RESULT([yes])
		EM_CFLAGS="$EM_CFLAGS $1"],
		[AC_MSG_RESULT([no])])
	  CFLAGS="$saved_cflags"
	  AC_LANG_POP([C])])

# EM_CHECK_CXXFLAG(FLAG)
# ---------------------
AC_DEFUN([EM_CHECK_CXXFLAG],
[	  AC_MSG_CHECKING([if $CXX supports $1])
	  AC_LANG_PUSH([C++])
	  saved_cxxflags="$CXXFLAGS"
	  CXXFLAGS="-W -Wall -Werror $EM_CXXFLAGS $1"
	  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
		[AC_MSG_RESULT([yes])
		EM_CXXFLAGS="$EM_CXXFLAGS $1"],
		[AC_MSG_RESULT([no])])
	  CXXFLAGS="$saved_cxxflags"
	  AC_LANG_POP([C++])])
