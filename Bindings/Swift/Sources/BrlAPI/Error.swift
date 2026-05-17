//
// libbrlapi — error types.
//

import CBrlAPI
import Foundation

extension BrlAPI {

    /// Errors thrown by the BrlAPI binding. Each case maps to the brlapi
    /// error space, with `.libraryError(code:message:)` carrying the raw
    /// `brlapi_error_t` for codes the binding doesn't enumerate explicitly.
    public enum Error: Swift.Error, CustomStringConvertible, Equatable {
        case connectionFailed(String)
        case authenticationFailed(String)
        case notConnected
        case ttyBusy
        case driverError(String)
        case invalidArgument(String)
        case libraryError(code: Int32, message: String)

        /// Snapshot the thread-local `brlapi_error` into a Swift error.
        ///
        /// The C header exposes `brlapi_error` as the macro
        /// `(*brlapi_error_location())` so each thread gets its own copy.
        /// Swift can't import C macros, so we call the location function
        /// directly — which is what the macro expands to anyway.
        static func current() -> Error {
            guard let loc = brlapi_error_location() else {
                return .libraryError(code: -1, message: "unknown brlapi error")
            }
            let message = String(cString: brlapi_strerror(loc))
            let code = loc.pointee.brlerrno
            switch code {
            case BRLAPI_ERROR_TTYBUSY:
                return .ttyBusy
            case BRLAPI_ERROR_CONNREFUSED, BRLAPI_ERROR_NOMEM,
                 BRLAPI_ERROR_LIBCERR, BRLAPI_ERROR_GAIERR:
                return .connectionFailed(message)
            case BRLAPI_ERROR_AUTHENTICATION:
                return .authenticationFailed(message)
            case BRLAPI_ERROR_DRIVERERROR:
                return .driverError(message)
            case BRLAPI_ERROR_INVALID_PARAMETER, BRLAPI_ERROR_INVALID_PACKET:
                return .invalidArgument(message)
            default:
                return .libraryError(code: Int32(code.rawValue), message: message)
            }
        }

        public var description: String {
            switch self {
            case .connectionFailed(let m): return "connection failed: \(m)"
            case .authenticationFailed(let m): return "authentication failed: \(m)"
            case .notConnected: return "not connected to brltty"
            case .ttyBusy: return "tty is busy"
            case .driverError(let m): return "driver error: \(m)"
            case .invalidArgument(let m): return "invalid argument: \(m)"
            case .libraryError(let code, let m): return "libbrlapi error \(code): \(m)"
            }
        }
    }
}
