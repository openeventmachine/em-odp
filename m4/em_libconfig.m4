# EM_LIBCONFIG
# -----------------------
AC_DEFUN([EM_LIBCONFIG],
[dnl
##########################################################################
# Check for libconfig availability
##########################################################################
PKG_CHECK_MODULES([LIBCONFIG], [libconfig])

##########################################################################
# Check for od availability
##########################################################################
AC_CHECK_PROGS([OD], [od])
AC_PROG_SED
AS_IF([test -z "$OD"], [AC_MSG_ERROR([Could not find 'od'])])

##########################################################################
# Create a header file em_libconfig_config.h which containins null
# terminated hex dump of em-odp.conf
##########################################################################
AC_CONFIG_COMMANDS([include/em_libconfig_config.h],
[mkdir -p include
   (echo "static const char config_builtin[[]] = {"; \
     $OD -An -v -tx1 < ${srcdir}/config/em-odp.conf | \
     $SED -e 's/[[0-9a-f]]\+/0x\0,/g' ; \
     echo "0x00 };") > \
   include/em_libconfig_config.h],
 [OD=$OD SED=$SED])
]) # EM_LIBCONFIG
