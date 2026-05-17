###############################################################################
# libbrlapi - A library providing access to braille terminals for applications.
#
# Copyright (C) 2005-2026 by The BRLTTY Developers.
#
# libbrlapi comes with ABSOLUTELY NO WARRANTY.
#
# This is free software, placed under the terms of the
# GNU Lesser General Public License, as published by the Free Software
# Foundation; either version 2.1 of the License, or (at your option) any
# later version. Please see the file LICENSE-LGPL for details.
#
# Web Page: http://brltty.app/
###############################################################################

AC_DEFUN([BRLTTY_SWIFT_BINDINGS], [dnl
SWIFT_OK=false
SWIFT=""
SWIFT_VERSION=""

# We rely on the Swift Package Manager to drive the build; the only thing
# autoconf needs to know is whether `swift` is on PATH and modern enough
# to honour the Package.swift in this tree (swift-tools-version 5.7).
AC_PATH_PROG([SWIFT], [swift])

if test -n "${SWIFT}"
then
   AC_MSG_NOTICE([Swift compiler: ${SWIFT}])
   SWIFT_VERSION=`"${SWIFT}" --version 2>/dev/null | head -n 1`
   AC_MSG_NOTICE([Swift version: ${SWIFT_VERSION}])

   # Probe with `swift package describe` against the Bindings/Swift tree.
   # Doing it here means missing pkg-config / brlapi headers fail fast at
   # configure time rather than at `make all`.
   if "${SWIFT}" package --package-path "${srcdir}/Bindings/Swift" describe >/dev/null 2>&1
   then
      SWIFT_OK=true
   else
      AC_MSG_WARN([Swift package description failed - check that libbrlapi headers are reachable via pkg-config])
   fi
else
   AC_MSG_WARN([Swift compiler not found - skipping Swift bindings])
fi

AC_SUBST([SWIFT])
AC_SUBST([SWIFT_VERSION])
AC_SUBST([SWIFT_OK])
])
