/*
 * libbrlapi — A library providing access to braille terminals for applications.
 *
 * Copyright (C) 2005-2026 by The BRLTTY Developers.
 *
 * libbrlapi comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 */

/*
 * Umbrella header for the CBrlAPI system module. Pulls in brlapi.h plus the
 * keycode / parameter / constants headers so the Swift wrapper sees every
 * symbol it needs. Resolution order is intentionally permissive (system
 * include paths first, then a couple of common install layouts) so the
 * binding works whether brltty was installed via Homebrew, the standard
 * Linux package, or a developer-local prefix.
 */

#ifndef BRLTTY_INCLUDED_SWIFT_CBRLAPI
#define BRLTTY_INCLUDED_SWIFT_CBRLAPI

#if __has_include(<brlapi.h>)
#  include <brlapi.h>
#elif __has_include("brlapi.h")
#  include "brlapi.h"
#elif __has_include(<brltty/brlapi.h>)
#  include <brltty/brlapi.h>
#else
#  error "brlapi.h not found — install libbrlapi-dev (Linux) or brltty (Homebrew)"
#endif

#if __has_include(<brlapi_keycodes.h>)
#  include <brlapi_keycodes.h>
#elif __has_include("brlapi_keycodes.h")
#  include "brlapi_keycodes.h"
#endif

#if __has_include(<brlapi_constants.h>)
#  include <brlapi_constants.h>
#elif __has_include("brlapi_constants.h")
#  include "brlapi_constants.h"
#endif

#endif /* BRLTTY_INCLUDED_SWIFT_CBRLAPI */
