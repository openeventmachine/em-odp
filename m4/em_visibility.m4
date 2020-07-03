# EM_VISIBILITY
# --------------
# Enable -fvisibility=hidden if using a gcc that supports it

AC_DEFUN([EM_VISIBILITY], [dnl
	AC_LANG_PUSH([C])
	VISIBILITY_CFLAGS="-fvisibility=hidden"
	AC_CACHE_CHECK([whether $CC supports -fvisibility=hidden],
		       [em_cv_visibility_hidden], [dnl
		OLD_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $VISIBILITY_CFLAGS"
		AC_LINK_IFELSE([AC_LANG_PROGRAM()],
			       [em_cv_visibility_hidden=yes],
			       [em_cv_visibility_hidden=no])
		CFLAGS=$OLD_CFLAGS
	])
	AS_IF([test "x$em_cv_visibility_hidden" != "xyes"],
	      [VISIBILITY_CFLAGS=""])
	AC_LANG_POP([C])
	AC_SUBST(VISIBILITY_CFLAGS)
]) # EM_VISIBILITY
