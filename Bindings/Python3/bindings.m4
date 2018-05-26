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

AC_DEFUN([BRLTTY_PYTHON3_BINDINGS], [dnl
PYTHON3_OK=true

# Suppress a new warning introduced in Python 3.6:
#
#    Python runtime initialized with LC_CTYPE=C
#    (a locale with default ASCII encoding),
#    which may cause Unicode compatibility problems.
#    Using C.UTF-8, C.utf8, or UTF-8 (if available)
#    as alternative Unicode-compatible locales is recommended.
#
export PYTHONCOERCECLOCALE=0

AC_PATH_PROG([PYTHON3], [python3])
if test -z "${PYTHON3}"
then
   AC_MSG_WARN([Python interpreter not found])
   PYTHON3_OK=false
else
   PYTHON3_PROLOGUE=""
   for python_module in sys distutils.sysconfig
   do
      if test -n "`${PYTHON3} -c "import ${python_module};" 2>&1`"
      then
         AC_MSG_WARN([Python module not found: ${python_module}])
         PYTHON3_OK=false
      else
         PYTHON3_PROLOGUE="${PYTHON3_PROLOGUE}import ${python_module}; "
      fi
   done
   AC_SUBST([PYTHON3_PROLOGUE])

   if "${PYTHON3_OK}"
   then
      if test -z "${PYTHON3_VERSION}"
      then
         [PYTHON3_VERSION="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print(distutils.sysconfig.get_config_vars('VERSION')[0]);"`"]
         if test -z "${PYTHON3_VERSION}"
         then
            [PYTHON3_VERSION="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print('.'.join(sys.version.split()[0].split('.')[:2]));"`"]
            if test -z "${PYTHON3_VERSION}"
            then
               AC_MSG_WARN([Python version not defined])
            fi
         fi
      fi
      AC_SUBST([PYTHON3_VERSION])

      if test -z "${PYTHON3_CPPFLAGS}"
      then
         [python_include_directory="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print(distutils.sysconfig.get_python_inc());"`"]
         if test -z "${python_include_directory}"
         then
            AC_MSG_WARN([Python include directory not found])
            PYTHON3_OK=false
         else
            PYTHON3_CPPFLAGS="-I${python_include_directory}"

            if test ! -f "${python_include_directory}/Python.h"
            then
               AC_MSG_WARN([Python developer environment not installed])
               PYTHON3_OK=false
            fi
         fi
      fi
      AC_SUBST([PYTHON3_CPPFLAGS])

      if test -z "${PYTHON3_LIBS}"
      then
         PYTHON3_LIBS="-lpython${PYTHON3_VERSION}"

         [python_library_directory="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print(distutils.sysconfig.get_python_lib(0,1));"`"]
         if test -z "${python_library_directory}"
         then
            AC_MSG_WARN([Python library directory not found])
         else
            PYTHON3_LIBS="-L${python_library_directory}/config ${PYTHON3_LIBS}"
         fi
      fi
      AC_SUBST([PYTHON3_LIBS])

      if test -z "${PYTHON3_EXTRA_LIBS}"
      then
         [PYTHON3_EXTRA_LIBS="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print(distutils.sysconfig.get_config_var('LOCALMODLIBS'), distutils.sysconfig.get_config_var('LIBS'));"`"]
      fi
      AC_SUBST([PYTHON3_EXTRA_LIBS])

      if test -z "${PYTHON3_EXTRA_LDFLAGS}"
      then
         [PYTHON3_EXTRA_LDFLAGS="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print(distutils.sysconfig.get_config_var('LINKFORSHARED'));"`"]
      fi
      AC_SUBST([PYTHON3_EXTRA_LDFLAGS])

      if test -z "${PYTHON3_SITE_PKG}"
      then
         [PYTHON3_SITE_PKG="`${PYTHON3} -c "${PYTHON3_PROLOGUE} print(distutils.sysconfig.get_python_lib(1,0));"`"]
         if test -z "${PYTHON3_SITE_PKG}"
         then
            AC_MSG_WARN([Python package directory not found])
         fi
      fi
      AC_SUBST([PYTHON3_SITE_PKG])
   fi
fi

AC_PATH_PROGS([CYTHON], [cython])
if test -z "${CYTHON}"
then
   AC_MSG_WARN([Cython compiler not found])
   PYTHON3_OK=false
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

AC_SUBST([PYTHON3_OK])
])
