//
// libbrlapi — connection settings.
//

import CBrlAPI
import Foundation

extension BrlAPI {

    /// Authentication / transport parameters for `Connection`. Matches the
    /// underlying `brlapi_connectionSettings_t` but with Swift-friendly
    /// defaults — `nil` for either field means "let libbrlapi pick".
    public struct Settings: Equatable {
        /// Authentication string. Examples:
        ///   - `"none"` — no authentication (works against a local instance)
        ///   - `"keyfile:/etc/brlapi.key"` — shared-secret keyfile
        ///   - `"+polkit"` — append PolicyKit fallback to whatever else
        /// Pass `nil` to inherit the default chain
        /// (`BRLAPI_DEFAUTH`, typically the system keyfile).
        public var auth: String?

        /// Host to connect to.
        ///   - `nil` — Unix socket on the local machine.
        ///   - `"hostname"` — TCP on the default port (`4101`).
        ///   - `"hostname:port"` — explicit TCP port.
        ///   - `":port"` — local instance on a non-default port.
        public var host: String?

        public init(auth: String? = nil, host: String? = nil) {
            self.auth = auth
            self.host = host
        }
    }
}
