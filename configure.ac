# Process this file with autoconf to produce a configure script.
AC_INIT(simpleprof.c)

AC_PROG_CC_C99

AC_USE_SYSTEM_EXTENSIONS

AC_MSG_CHECKING([for $CC option to generate position-independent code])
AC_CACHE_VAL([my_cv_prog_cc_pic],
[saved_CFLAGS=$CFLAGS
my_cv_prog_cc_pic=no
for arg in '' -fpic -fPIC
do
  CFLAGS="$saved_CFLAGS $arg"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([@%:@ifndef __PIC__
choke me!
@%:@endif])], [my_cv_prog_cc_pic=$arg])
  test "X$my_cv_prog_cc_pic" == Xno || break
done
CFLAGS=$saved_CFLAGS])
case X$my_cv_prog_cc_pic in
    X)
	AC_MSG_RESULT([none needed]) ;;
    Xno)
	AC_MSG_RESULT([unsupported])
	AC_MSG_ERROR([no way to generate position-independent code]) ;;
    *)
	CFLAGS="$CFLAGS $my_cv_prog_cc_pic"
	AC_MSG_RESULT([$my_cv_prog_cc_pic]) ;;
esac

AC_MSG_CHECKING([for $CC option to generate position-independent executable])
AC_CACHE_VAL([my_cv_prog_cc_pie],
[saved_LDFLAGS=$LDFLAGS
my_cv_prog_cc_pie=no
LDFLAGS="$LDFLAGS -pie"
AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [my_cv_prog_cc_pie=-pie])
LDFLAGS=$saved_LDFLAGS])
case X$my_cv_prog_cc_pie in
    X)
	AC_MSG_RESULT([none needed]) ;;
    Xno)
	AC_MSG_RESULT([unsupported])
	AC_MSG_ERROR([no way to generate position-independent executable]) ;;
    *)
	LDFLAGS="$LDFLAGS $my_cv_prog_cc_pie"
	AC_MSG_RESULT([$my_cv_prog_cc_pie]) ;;
esac

AC_CHECK_DECLS(LAV_CURRENT,dnl
[],dnl
[AC_MSG_ERROR([auditing API for the dynamic linker not found])],dnl
[@%:@include <link.h>])

AC_CHECK_DECLS(__profile_frequency)

# Checks for library functions.
AC_CHECK_FUNCS(__profile_frequency getauxval)

AC_OUTPUT(Makefile)
