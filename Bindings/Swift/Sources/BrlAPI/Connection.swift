//
// libbrlapi — connection wrapper.
//

import CBrlAPI
import Foundation

extension BrlAPI {

    /// A connection to a running brltty instance. Holds its own
    /// `brlapi_handle_t` so multiple `Connection` instances don't fight over
    /// the global error / settings state — every call uses the per-handle
    /// `brlapi__*` variant under the hood.
    ///
    /// Closes itself on `deinit`; ownership is reference-counted, so passing a
    /// `Connection` to multiple consumers is safe.
    public final class Connection {

        /// Coordinates / shape of the connected braille display.
        public struct DisplaySize: Equatable {
            public let width: Int
            public let height: Int
            public var totalCells: Int { width * height }
        }

        private let handleStorage: UnsafeMutableRawPointer
        private var handle: OpaquePointer { OpaquePointer(handleStorage) }
        private var isOpen: Bool = false
        private var ttyMode: Bool = false

        // MARK: - Lifecycle

        /// Open a connection to brltty. If `settings.host` is `nil` and a local
        /// brltty is running on its default Unix socket this just works — no
        /// authentication needed, no extra setup.
        public init(settings: Settings = Settings()) throws {
            // brlapi exposes the per-handle API via `brlapi__*` calls but
            // makes the caller allocate the storage. The size is reported at
            // runtime — don't hard-code it; new brlapi releases have grown
            // the struct before.
            let size = brlapi_getHandleSize()
            guard let storage = malloc(size) else {
                throw Error.libraryError(code: -1, message: "out of memory allocating brlapi handle")
            }
            self.handleStorage = storage

            // Build the C-side settings struct. Strings have to live until
            // brlapi_openConnection returns; capture them in locals.
            try settings.auth.withOptionalCString { authPtr in
                try settings.host.withOptionalCString { hostPtr in
                    var desired = brlapi_connectionSettings_t(
                        auth: authPtr,
                        host: hostPtr
                    )
                    var actual = brlapi_connectionSettings_t(auth: nil, host: nil)
                    let fd = brlapi__openConnection(handle, &desired, &actual)
                    if fd == BRLAPI_INVALID_FILE_DESCRIPTOR {
                        // All stored properties are assigned by this point, so
                        // deinit will run and free `handleStorage` for us —
                        // an explicit free here would double-free.
                        throw Error.current()
                    }
                    isOpen = true
                }
            }
        }

        deinit {
            close()
            free(handleStorage)
        }

        /// Close the connection eagerly. Idempotent. Most callers should let
        /// `deinit` handle this — explicit closure is here for places that
        /// need the underlying file descriptor released before the instance
        /// goes out of scope.
        public func close() {
            if ttyMode {
                _ = brlapi__leaveTtyMode(handle)
                ttyMode = false
            }
            if isOpen {
                brlapi__closeConnection(handle)
                isOpen = false
            }
        }

        // MARK: - Metadata

        /// File descriptor of the underlying socket — useful when integrating
        /// the connection into a `DispatchSource` / `select` loop.
        public var fileDescriptor: Int32 {
            return brlapi__getFileDescriptor(handle)
        }

        /// Name of the braille driver currently loaded by brltty (e.g.
        /// "Baum", "HumanWare", "NoBraille").
        public var driverName: String {
            var buf = [CChar](repeating: 0, count: 64)
            _ = brlapi__getDriverName(handle, &buf, buf.count)
            return String(cString: buf)
        }

        /// Model identifier string reported by the active driver, when
        /// available. May be empty if the driver doesn't report one.
        public var modelIdentifier: String {
            var buf = [CChar](repeating: 0, count: 64)
            _ = brlapi__getModelIdentifier(handle, &buf, buf.count)
            return String(cString: buf)
        }

        /// Display geometry. `(width: 0, height: 0)` when no display is
        /// attached.
        public var displaySize: DisplaySize {
            var x: UInt32 = 0, y: UInt32 = 0
            _ = brlapi__getDisplaySize(handle, &x, &y)
            return DisplaySize(width: Int(x), height: Int(y))
        }

        // MARK: - TTY mode

        /// Take control of a specific tty so brltty routes keystrokes from
        /// the braille display to this connection. `tty == -1` (the default)
        /// asks brltty to pick the current foreground tty.
        ///
        /// - Parameters:
        ///   - tty: tty number, or `-1` for "current".
        ///   - driver: optional driver-name hint; when non-nil, brltty will
        ///     decode keystrokes through that driver's keymap before handing
        ///     them to `readKey`. When `nil`, raw key codes are returned.
        public func enterTtyMode(tty: Int = Int(BRLAPI_TTY_DEFAULT),
                                 driver: String? = nil) throws {
            let result: Int32
            if let driver = driver {
                result = driver.withCString { ptr in
                    brlapi__enterTtyMode(handle, Int32(tty), ptr)
                }
            } else {
                result = brlapi__enterTtyMode(handle, Int32(tty), nil)
            }
            if result < 0 { throw Error.current() }
            ttyMode = true
        }

        /// Release the tty taken by `enterTtyMode`. Called automatically on
        /// `close()` / `deinit`; safe to call multiple times.
        public func leaveTtyMode() throws {
            guard ttyMode else { return }
            if brlapi__leaveTtyMode(handle) < 0 { throw Error.current() }
            ttyMode = false
        }

        // MARK: - Writing

        /// Convenience: write a plain text line, centered on the display.
        /// Pass `cursor = -1` (the default) to leave the cursor hidden,
        /// `0` to keep it where it is, or a 1-based cell index to position it.
        public func writeText(_ text: String,
                              cursor: Int = Int(BRLAPI_CURSOR_LEAVE)) throws {
            let result = text.withCString { ptr in
                brlapi__writeText(handle, Int32(cursor), ptr)
            }
            if result < 0 { throw Error.current() }
        }

        /// Write the display state via the full `brlapi_writeArguments_t`
        /// surface. Mutate the inout struct via the closure; the binding
        /// initialises it to `BRLAPI_WRITEARGUMENTS_INITIALIZER` so callers
        /// only set the fields they care about.
        public func write(_ configure: (inout brlapi_writeArguments_t) -> Void) throws {
            var args = brlapi_writeArguments_t(
                displayNumber: Int32(BRLAPI_DISPLAY_DEFAULT),
                regionBegin: 0,
                regionSize: 0,
                text: nil,
                textSize: -1,
                andMask: nil,
                orMask: nil,
                cursor: Int32(BRLAPI_CURSOR_LEAVE),
                charset: nil
            )
            configure(&args)
            if brlapi__write(handle, &args) < 0 { throw Error.current() }
        }

        // MARK: - Reading

        /// Read the next key event from the device. Blocks indefinitely by
        /// default; pass `wait: false` for non-blocking semantics (returns
        /// `nil` if nothing's available).
        public func readKey(wait: Bool = true) throws -> UInt64? {
            var code: brlapi_keyCode_t = 0
            let result = brlapi__readKey(handle, wait ? 1 : 0, &code)
            if result < 0 { throw Error.current() }
            if result == 0 { return nil }  // non-blocking and no key
            return UInt64(code)
        }

        /// Same as `readKey` but interprets the result through libbrlapi's
        /// "expanded" decoder, returning a structured representation with the
        /// command, argument, and flags split out.
        public func readKeyExpanded(wait: Bool = true) throws -> ExpandedKey? {
            var code: brlapi_keyCode_t = 0
            let result = brlapi__readKey(handle, wait ? 1 : 0, &code)
            if result < 0 { throw Error.current() }
            if result == 0 { return nil }

            var expanded = brlapi_expandedKeyCode_t()
            if brlapi_expandKeyCode(code, &expanded) < 0 { throw Error.current() }

            return ExpandedKey(
                type: ExpandedKey.KeyType(rawValue: expanded.type) ?? .unknown,
                command: expanded.command,
                argument: expanded.argument,
                flags: expanded.flags
            )
        }

        /// Structured key event returned by `readKeyExpanded`.
        public struct ExpandedKey: Equatable {
            /// Matches the top bits of `brlapi_keyCode_t` (see
            /// `BRLAPI_KEY_TYPE_MASK`). `brlapi_expandKeyCode` returns the
            /// type already narrowed to `unsigned int`, so the raw values
            /// here are the 32-bit truncation of the libbrlapi constants.
            public enum KeyType: UInt32 {
                case command = 0x20000000      // BRLAPI_KEY_TYPE_CMD
                case symbol  = 0x00000000      // BRLAPI_KEY_TYPE_SYM
                case unknown = 0xFFFFFFFF
            }

            public let type: KeyType
            public let command: UInt32
            public let argument: UInt32
            public let flags: UInt32
        }

        // MARK: - Suspend / resume

        /// Ask brltty to release the named braille driver so another client
        /// (or the system) can grab it. Pair with `resumeDriver`. The driver
        /// name has to be one brltty actually knows about — pass the same
        /// string you'd give to `-b` on the command line.
        public func suspendDriver(_ driver: String) throws {
            let result = driver.withCString { brlapi__suspendDriver(handle, $0) }
            if result < 0 { throw Error.current() }
        }

        public func resumeDriver() throws {
            if brlapi__resumeDriver(handle) < 0 { throw Error.current() }
        }
    }
}

// MARK: - Helpers

private extension Optional where Wrapped == String {
    /// Like `withCString` but accepts `Optional<String>`: passes `nil` straight
    /// through to the closure when the original is `nil`, so we can build
    /// libbrlapi structs whose fields are optional C strings without
    /// scattering branches in the call site.
    func withOptionalCString<Result>(
        _ body: (UnsafePointer<CChar>?) throws -> Result
    ) rethrows -> Result {
        if let value = self {
            return try value.withCString { try body($0) }
        } else {
            return try body(nil)
        }
    }
}
