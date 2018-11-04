# Process this file with autoconf to produce a configure script.
AC_INIT(nbtty, 0.4.1, fhunleth@troodon-software.com)
AC_PREREQ(2.60)
AM_INIT_AUTOMAKE([-Wall foreign])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB(util, forkpty)

# Checks for header files.
AC_CHECK_HEADERS(fcntl.h sys/select.h sys/socket.h)
AC_CHECK_HEADERS(sys/ioctl.h pty.h termios.h)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_TYPE_SIGNAL
AC_CHECK_FUNCS(atexit dup2 memset)
AC_CHECK_FUNCS(select socket strerror)
AC_CHECK_FUNCS(forkpty)

AC_CONFIG_FILES([Makefile config.h])
AC_OUTPUT
