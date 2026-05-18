//
// Tty-scope helpers for platform-specific BrlAPI routing.
//
// On Linux the `tty` int that BrlAPI carries is just the kernel vt
// number. On macOS there is no vt system, so the brltty
// MacOSAccessibility screen driver synthesises one: it hashes the
// frontmost application's bundle id together with brltty's internal
// tab counter into the 32-bit slot BrlAPI uses as the routing key.
// To target a specific app from a Mac client, we need to compute
// exactly the same hash. The algorithm has to stay bit-for-bit
// identical to mo_scope_hash in Drivers/Screen/MacOSAccessibility
// /screen.m — change it in one place, change it in both.
//

import Foundation

extension BrlAPI {

    /// Hash and identifier helpers that mirror what brltty's macOS
    /// screen driver puts on the wire for the BrlAPI tty slot.
    ///
    /// Most callers should use `Connection.enterTtyMode(forApp:tab:)`
    /// rather than touching this directly; the static helpers are
    /// exposed so they can be unit-tested and so apps that want to
    /// pre-compute a slot (e.g. to publish it to other processes)
    /// can do so without opening a connection.
    public enum MacOSScope {

        /// djb2 over a UTF-8 byte sequence. Matches mo_scope_hash in
        /// the screen driver byte-for-byte. The choice of djb2 is
        /// part of the public contract — never change it without a
        /// coordinated update on the brltty side.
        public static func djb2(_ bytes: some Sequence<UInt8>) -> UInt32 {
            var h: UInt32 = 5381
            for b in bytes {
                h = (h &* 33) ^ UInt32(b)
            }
            return h
        }

        /// Compute the BrlAPI tty slot the macOS screen driver emits
        /// when `bundleID` is frontmost and brltty considers itself
        /// on tab `tab`. The wire format mirrors the C side exactly:
        ///
        ///     bits 31..16 = djb2(bundleID) & 0xFFFF
        ///     bits 15.. 0 = tab counter (clamped to 0..0xFFFF)
        ///
        /// Splitting bundle and tab into separate halves matters
        /// because brltty's "next vt" command does `current + 1` on
        /// the server side — see
        /// Drivers/Screen/MacOSAccessibility/screen.m for the
        /// matching derivation. `0xFFFFFFFF` is reserved (BrlAPI's
        /// "no specific tty" sentinel) so we fold any combo that
        /// lands there onto another value.
        public static func tty(forApp bundleID: String, tab: Int = 1) -> Int32 {
            let bundleHash16 = djb2(bundleID.utf8) & 0xFFFF
            let tab16 = UInt32(tab & 0xFFFF)
            var combined = (bundleHash16 << 16) | tab16
            if combined == 0xFFFFFFFF { combined ^= 1 }
            // BrlAPI carries the value as `int`. The bit pattern is
            // preserved across the cast — the server compares on the
            // raw 32 bits — so a combined value >= 0x80000000 becomes
            // a negative int and still matches what the mo driver
            // emits.
            return Int32(bitPattern: combined)
        }

        /// Slot for the bundle the calling binary belongs to, if any.
        ///
        /// Returns `nil` when `Bundle.main` has no `CFBundleIdentifier`
        /// — typical for raw CLI tools and SwiftPM executables built
        /// without an `Info.plist`. Add one (or fall back to the
        /// explicit `tty(forApp:)`) when you hit this case.
        public static func ttyForCurrentApp(tab: Int = 1) -> Int32? {
            guard let bundleID = Bundle.main.bundleIdentifier else { return nil }
            return tty(forApp: bundleID, tab: tab)
        }
    }
}

extension BrlAPI.Connection {

    /// Enter tty mode scoped to a macOS application (and optionally a
    /// specific tab within it). Equivalent to calling
    /// `enterTtyMode(tty: BrlAPI.MacOSScope.tty(forApp:tab:))` but
    /// reads better and keeps the hash algorithm in one place.
    ///
    /// Has no special meaning on Linux or Windows — the same hash
    /// will be claimed on the wire but the server's
    /// `currentVirtualTerminal()` returns a different convention
    /// there (vt number / window handle), so no keystrokes will
    /// actually route to this client. Use the plain `enterTtyMode`
    /// overload on those platforms.
    public func enterTtyMode(forApp bundleID: String,
                             tab: Int = 1,
                             driver: String? = nil) throws {
        let scope = BrlAPI.MacOSScope.tty(forApp: bundleID, tab: tab)
        try enterTtyMode(tty: Int(scope), driver: driver)
    }

    /// Convenience for the common case where an app wants braille
    /// scoped to itself — pulls the bundle id from `Bundle.main`.
    ///
    /// Throws `BrlAPI.Error.invalidArgument` if the calling binary
    /// has no `CFBundleIdentifier`. CLI tools and bare SwiftPM
    /// executables hit this; either give them an `Info.plist` or
    /// call `enterTtyMode(forApp:)` with an explicit id.
    public func enterTtyModeForCurrentApp(tab: Int = 1,
                                          driver: String? = nil) throws {
        guard let slot = BrlAPI.MacOSScope.ttyForCurrentApp(tab: tab) else {
            throw BrlAPI.Error.invalidArgument(
                "Bundle.main has no CFBundleIdentifier; use enterTtyMode(forApp:) instead"
            )
        }
        try enterTtyMode(tty: Int(slot), driver: driver)
    }
}
