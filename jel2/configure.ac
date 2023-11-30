AC_PREREQ([2.61])
AC_INIT([jel], [2.0], [christopher.connolly@sri.com])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([config-aux])

AM_INIT_AUTOMAKE([foreign  nostdinc silent-rules subdir-objects])

AC_CANONICAL_BUILD

AC_PROG_CC    

AC_PROG_RANLIB

AC_ARG_ENABLE(debug,
	AS_HELP_STRING([--enable-debug], [enable debugging, default: no]),
	[case "${enableval}" in
	      yes) debug=true;;
	      no)  debug=false;;
	      *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]);;
	      esac],
	[debug=false])

AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")

AC_ARG_ENABLE(profile,
	AS_HELP_STRING([--enable-profile], [enable profiling, default: no]),
	[case "${enableval}" in
	      yes) profile=true;;
	      no)  profile=false;;
	      *)   AC_MSG_ERROR([bad value ${enableval} for --enable-profile]);;
	      esac],
	[profile=false])

AM_CONDITIONAL(PROFILE, test x"profile" = x"true")


AC_ARG_VAR([JPEGDIR], AS_HELP_STRING([JPEGDIR=dir], [Where jpeg libs and includes can be found.]) )

AM_CONDITIONAL(NO_JPEGDIR, test x"$JPEGDIR" = x"")


AC_CHECK_HEADER(jpeglib.h, [], [
  echo "Error! You need to have jpeglib.h around."
  exit -1
])

AC_CHECK_LIB(jpeg, main, [], [
  echo "Error! You need to have libjpeg around."
  exit -1
])

AC_CHECK_LIB(m, main)

AC_CONFIG_FILES([Makefile jel.pc])

AC_OUTPUT

