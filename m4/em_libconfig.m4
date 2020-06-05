##########################################################################
# Configuration file version
##########################################################################
m4_define([_em_config_version_generation], [0])
m4_define([_em_config_version_major], [0])
m4_define([_em_config_version_minor], [6])

m4_define([_em_config_version],
	  [_em_config_version_generation._em_config_version_major._em_config_version_minor])

# EM_LIBCONFIG(CONFIG-FILE-PATH)
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
# Check default configuration file
##########################################################################
AS_IF([test -z "$1"] || [test ! -f $1],
      [AC_MSG_ERROR([Default configuration file not found])], [])

conf_ver=_em_config_version
file_ver=`$SED 's/ //g' $1 | $GREP -oP '(?<=config_file_version=").*?(?=")'`

AS_IF([test "x$conf_ver" = "x$file_ver"], [],
      [AC_MSG_ERROR([Configuration file version mismatch:
                     invalid EM config file version:$file_ver from $1,
		     expected version:$conf_ver)])])

##########################################################################
# Create a header file em_libconfig_config.h which containins null
# terminated hex dump of em-odp.conf
##########################################################################
AC_CONFIG_COMMANDS([include/em_libconfig_config.h],
[mkdir -p include
   (echo "static const char config_builtin[[]] = {"; \
     $OD -An -v -tx1 < $CONFIG_FILE | \
     $SED -e 's/[[0-9a-f]]\+/0x\0,/g' ; \
     echo "0x00 };") > \
   include/em_libconfig_config.h],
 [OD=$OD SED=$SED CONFIG_FILE=$1])
]) # EM_LIBCONFIG
