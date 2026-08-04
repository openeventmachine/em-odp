# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Nokia Solutions and Networks
#

# EM_CHECK_ODP([LIBODP_NAME])
# ---------------------------
# Check that the installed ODP meets the minimum API version requirement,
# and configure pkg-config modules for both shared and static linking.
#
# LIBODP_NAME: name of the ODP pkg-config module (e.g. "libodp-linux").
#
# Sets / AC_SUBSTs:
#   EM_REQ_ODP_MIN_VERSION,
#   ODP_CFLAGS, ODP_LIBS, ODP_STATIC_CFLAGS, ODP_STATIC_LIBS,
#   ODPHELPER_CFLAGS, ODPHELPER_LIBS,
#   ODPHELPER_STATIC_CFLAGS, ODPHELPER_STATIC_LIBS
AC_DEFUN([EM_CHECK_ODP], [dnl

m4_ifblank([$1], [m4_fatal([EM_CHECK_ODP requires LIBODP_NAME argument])])
odp_lib_name="$1"

# Minimum supported ODP API version (generation.major.minor)
odp_min_version_generation=1
odp_min_version_major=48
odp_min_version_minor=0

odp_min_version="${odp_min_version_generation}.${odp_min_version_major}.${odp_min_version_minor}"

ODP_VERSION=$(pkg-config --modversion $odp_lib_name 2>/dev/null)
AS_IF([test -z "$ODP_VERSION"],
      [AC_MSG_ERROR([Unable to determine ODP version via pkg-config])])

AC_MSG_CHECKING([ODP API version (>= ${odp_min_version} required)])
AC_MSG_RESULT([${ODP_VERSION}])

# Pass 'minimum ODP version required by EM'-defines into config.h
AC_DEFINE_UNQUOTED([EM_REQ_ODP_API_GENERATION], [$odp_min_version_generation],
		   [Minimum required ODP API generation version])
AC_DEFINE_UNQUOTED([EM_REQ_ODP_API_MAJOR], [$odp_min_version_major],
		   [Minimum required ODP API major version])
AC_DEFINE_UNQUOTED([EM_REQ_ODP_API_MINOR], [$odp_min_version_minor],
		   [Minimum required ODP API minor version])

EM_REQ_ODP_MIN_VERSION="${odp_min_version}"

AC_SUBST([EM_REQ_ODP_MIN_VERSION])

# --- ODP shared library pkg-config ---
PKG_CHECK_MODULES([ODP], [$odp_lib_name >= $odp_min_version])
AC_SUBST([ODP_CFLAGS])
AC_SUBST([ODP_LIBS])

PKG_CHECK_MODULES([ODPHELPER], [libodphelper])
AC_SUBST([ODPHELPER_CFLAGS])
AC_SUBST([ODPHELPER_LIBS])

# --- ODP static library pkg-config ---
PKG_CHECK_MODULES_STATIC([ODP_STATIC], [$odp_lib_name >= $odp_min_version])
AC_SUBST([ODP_STATIC_CFLAGS])
AC_SUBST([ODP_STATIC_LIBS])

PKG_CHECK_MODULES_STATIC([ODPHELPER_STATIC], [libodphelper])
AC_SUBST([ODPHELPER_STATIC_CFLAGS])
AC_SUBST([ODPHELPER_STATIC_LIBS])
])
