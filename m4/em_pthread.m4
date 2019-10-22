#
#   Copyright (c) 2018, Nokia Solutions and Networks
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

AC_DEFUN([EM_PTHREAD], [
AC_LANG_PUSH([C])
em_pthread_status=no

temp_LIBS="$LIBS"

PTHREAD_LIBS="-lpthread"
LIBS="$PTHREAD_LIBS $LIBS"

AC_TRY_LINK_FUNC([pthread_create], [em_pthread_status=yes])
AC_MSG_CHECKING([for pthread])
AC_MSG_RESULT([$em_pthread_status])

LIBS="$temp_LIBS"

test -n "$PTHREAD_CC" || PTHREAD_CC="$CC"

AC_SUBST([PTHREAD_LIBS])

if test "x$em_pthread_status" != "xno"; then
        AC_DEFINE([HAVE_PTHREAD],[1],[POSIX thread libraries are available.])
else
        AC_MSG_RESULT([Error! POSIX thread libraries are not found!])
        exit -1
fi

AC_LANG_POP
])dnl EM_PTHREAD
