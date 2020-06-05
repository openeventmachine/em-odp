# Copyright (c) 2018, Nokia Solutions and Networks
# All rights reserved.
#
# SPDX-License-Identifier:     BSD-3-Clause
#
# EM_PTHREAD
# -----------
# Check for pthreads availability
AC_DEFUN([EM_PTHREAD], [
	AC_LANG_PUSH([C])
	em_pthread_ok=no

	em_pthread_save_CC="$CC"
	em_pthread_save_CFLAGS="$CFLAGS"
	em_pthread_save_LIBS="$LIBS"

	PTHREAD_LIBS="-lpthread"
	PTHREAD_CFLAGS="-pthread"
	AS_IF([test "x$PTHREAD_CC" != "x"], [CC="$PTHREAD_CC"])
	CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
	LIBS="$PTHREAD_LIBS $LIBS"

	AC_MSG_CHECKING([for pthread])
	AC_LINK_IFELSE([AC_LANG_CALL([], [pthread_create])], [em_pthread_ok=yes])
	AC_MSG_RESULT([$em_pthread_ok])

	CC="$em_pthread_save_CC"
	CFLAGS="$em_pthread_save_CFLAGS"
	LIBS="$em_pthread_save_LIBS"

	AS_IF([test "x$em_pthread_ok" != "xno"],
	      [AC_DEFINE([HAVE_PTHREAD],[1],[POSIX thread libraries are available.])],
	      [PTHREAD_LIBS=""
	       PTHREAD_CFLAGS=""
	       AC_MSG_FAILURE([error, POSIX thread libraries are not found!])
	      ])

	test -n "$PTHREAD_CC" || PTHREAD_CC="$CC"

	AC_LANG_POP([C])
	AC_SUBST([PTHREAD_LIBS])
	AC_SUBST([PTHREAD_CFLAGS])
	AC_SUBST([PTHREAD_CC])
])dnl EM_PTHREAD
