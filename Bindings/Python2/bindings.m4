###############################################################################
# libbrlapi - A library providing access to braille terminals for applications.
#
# Copyright (C) 2005-2018 by
#   Alexis Robert <alexissoft@free.fr>
#   Samuel Thibault <Samuel.Thibault@ens-lyon.org>
#
# libbrlapi comes with ABSOLUTELY NO WARRANTY.
#
# This is free software, placed under the terms of the
# GNU Lesser General Public License, as published by the Free Software
# Foundation; either version 2.1 of the License, or (at your option) any
# later version. Please see the file LICENSE-LGPL for details.
#
# Web Page: http://brltty.app/
#
# This software is maintained by Dave Mielke <dave@mielke.cc>.
###############################################################################

AC_DEFUN([BRLTTY_PYTHON2_BINDINGS], [dnl
PYTHON2_OK=true

# Suppress a new warning introduced in Python 3.6:
#
#    Python runtime initialized with LC_CTYPE=C
#    (a locale with default ASCII encoding),
#    which may cause Unicode compatibility problems.
#    Using C.UTF-8, C.utf8, or UTF-8 (if available)
#    as alternative Unicode-compatible locales is recommended.
#
export PYTHONCOERCECLOCALE=0

AC_PATH_PROG([PYTHON2], [python2])
if test -z "${PYTHON2}"
then
   AC_MSG_WARN([Python interpreter not found])
   PYTHON2_OK=false
else
   PYTHON2_PROLOGUE=""
   for python_module in sys distutils.sysconfig
   do
      if test -n "`${PYTHON2} -c "import ${python_module};" 2>&1`"
      then
         AC_MSG_WARN([Python module not found: ${python_module}])
         PYTHON2_OK=false
      else
         PYTHON2_PROLOGUE="${PYTHON2_PROLOGUE}import ${python_module}; "
      fi
   done
   AC_SUBST([PYTHON2_PROLOGUE])

   if "${PYTHON2_OK}"
   then
      if test -z "${PYTHON2_VERSION}"
      then
         [PYTHON2_VERSION="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print(distutils.sysconfig.get_config_vars('VERSION')[0]);"`"]
         if test -z "${PYTHON2_VERSION}"
         then
            [PYTHON2_VERSION="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print('.'.join(sys.version.split()[0].split('.')[:2]));"`"]
            if test -z "${PYTHON2_VERSION}"
            then
               AC_MSG_WARN([Python version not defined])
            fi
         fi
      fi
      AC_SUBST([PYTHON2_VERSION])

      if test -z "${PYTHON2_CPPFLAGS}"
      then
         [python_include_directory="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print(distutils.sysconfig.get_python_inc());"`"]
         if test -z "${python_include_directory}"
         then
            AC_MSG_WARN([Python include directory not found])
            PYTHON2_OK=false
         else
            PYTHON2_CPPFLAGS="-I${python_include_directory}"

            if test ! -f "${python_include_directory}/Python.h"
            then
               AC_MSG_WARN([Python developer environment not installed])
               PYTHON2_OK=false
            fi
         fi
      fi
      AC_SUBST([PYTHON2_CPPFLAGS])

      if test -z "${PYTHON2_LIBS}"
      then
         PYTHON2_LIBS="-lpython${PYTHON2_VERSION}"

         [python_library_directory="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print(distutils.sysconfig.get_python_lib(0,1));"`"]
         if test -z "${python_library_directory}"
         then
            AC_MSG_WARN([Python library directory not found])
         else
            PYTHON2_LIBS="-L${python_library_directory}/config ${PYTHON2_LIBS}"
         fi
      fi
      AC_SUBST([PYTHON2_LIBS])

      if test -z "${PYTHON2_EXTRA_LIBS}"
      then
         [PYTHON2_EXTRA_LIBS="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print(distutils.sysconfig.get_config_var('LOCALMODLIBS'), distutils.sysconfig.get_config_var('LIBS'));"`"]
      fi
      AC_SUBST([PYTHON2_EXTRA_LIBS])

      if test -z "${PYTHON2_EXTRA_LDFLAGS}"
      then
         [PYTHON2_EXTRA_LDFLAGS="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print(distutils.sysconfig.get_config_var('LINKFORSHARED'));"`"]
      fi
      AC_SUBST([PYTHON2_EXTRA_LDFLAGS])

      if test -z "${PYTHON2_SITE_PKG}"
      then
         [PYTHON2_SITE_PKG="`${PYTHON2} -c "${PYTHON2_PROLOGUE} print(distutils.sysconfig.get_python_lib(1,0));"`"]
         if test -z "${PYTHON2_SITE_PKG}"
         then
            AC_MSG_WARN([Python package directory not found])
         fi
      fi
      AC_SUBST([PYTHON2_SITE_PKG])
   fi
fi

AC_PATH_PROGS([CYTHON], [cython])
if test -z "${CYTHON}"
then
   AC_MSG_WARN([Cython compiler not found])
   PYTHON2_OK=false
fi

if test "${GCC}" = "yes"
then
   CYTHON_CFLAGS="-Wno-parentheses -Wno-unused -fno-strict-aliasing -U_POSIX_C_SOURCE -U_XOPEN_SOURCE"
else
   case "${host_os}"
   in
      *)
         CYTHON_CFLAGS=""
         ;;
   esac
fi
AC_SUBST([CYTHON_CFLAGS])

AC_SUBST([PYTHON2_OK])
])
