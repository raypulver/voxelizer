AC_INIT([voxelizer], [0.1.0])

AC_CONFIG_SRCDIR(./)

AM_INIT_AUTOMAKE

PNG_REQUIRED_VERSION=1.0
ZLIB_REQUIRED_VERSION=1.0
AC_PROG_CC
PKG_CHECK_MODULES(libpng, libpng >= $PNG_REQUIRED_VERSION)
PKG_CHECK_MODULES(zlib, zlib >= $ZLIB_REQUIRED_VERSION)
AC_SUBST(libpng_CFLAGS)
AC_SUBST(libpng_LIBS)
AC_SUBST(zlib_CFLAGS)
AC_SUBST(zlib_LIBS)
AC_PROG_CXX
AC_OUTPUT(Makefile)
