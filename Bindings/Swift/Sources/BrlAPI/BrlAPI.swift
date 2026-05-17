//
// libbrlapi — Swift binding entry point.
//
// Copyright (C) 2005-2026 by The BRLTTY Developers.
//
// libbrlapi comes with ABSOLUTELY NO WARRANTY.
//
// This is free software, placed under the terms of the
// GNU Lesser General Public License, as published by the Free Software
// Foundation; either version 2.1 of the License, or (at your option) any
// later version. Please see the file LICENSE-LGPL for details.
//

import CBrlAPI
import Foundation

/// Namespace for the BrlAPI binding. All types are nested under `BrlAPI`
/// (`BrlAPI.Connection`, `BrlAPI.Error`, ...) so application code can use the
/// short module name `BrlAPI` without clashing with other symbols.
public enum BrlAPI {

    /// The version of the underlying libbrlapi that this binding is talking to.
    /// Reported by the dynamically-linked library at runtime — not the version
    /// the Swift wrapper was compiled against.
    public static var libraryVersion: (major: Int, minor: Int, revision: Int) {
        var major: Int32 = 0, minor: Int32 = 0, revision: Int32 = 0
        brlapi_getLibraryVersion(&major, &minor, &revision)
        return (Int(major), Int(minor), Int(revision))
    }

    /// Default port used by the TCP transport (`4101`).
    public static let defaultPort: Int = Int(BRLAPI_SOCKETPORTNUM)
}
