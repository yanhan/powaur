#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])

# Name, version, email
AC_INIT([powaur], [0.1], [powaurdev@gmail.com])

# Unique file in directory
AC_CONFIG_SRCDIR([powaur.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CC([cc gcc])
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_LIB([alpm], [alpm_initialize], ,
	AC_MSG_ERROR([libalpm is needed to compile powaur]))

# Check for cURL
AC_CHECK_LIB([curl], [curl_easy_init], ,
	AC_MSG_ERROR([libcurl is needed to compile powaur]))

# Check for libarchive
AC_CHECK_LIB([archive], [archive_write_new], ,
	AC_MSG_ERROR([libarchive is needed to compile powaur]))

# Check for yajl
AC_CHECK_LIB([yajl], [yajl_alloc], ,
	AC_MSG_ERROR([yajl is needed to compile powaur]))

# TODO: Make this optional?
AC_CHECK_LIB([pthread], [pthread_create], ,
	AC_MSG_ERROR([pthread is needed to compile powaur]))

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getcwd localtime_r memset mkdir realpath strchr strdup strndup \
				strcasecmp])

AC_OUTPUT
