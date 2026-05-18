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

        /// Compute the BrlAPI tty slot the macOS screen driver will
        /// emit when `bundleID` is frontmost and brltty considers
        /// itself on tab `tab`. The format mirrors the C side:
        /// "<bundleID>:<tab>" hashed with djb2, with `0xFFFFFFFF`
        /// (the BrlAPI sentinel for "no specific tty") folded onto
        /// another value so it can never escape.
        public static func tty(forApp bundleID: String, tab: Int = 1) -> Int32 {
            let composite = "\(bundleID):\(tab)"
            var hash = djb2(composite.utf8)
            if hash == 0xFFFFFFFF { hash ^= 1 }
            // The BrlAPI wire field is an int. The bit pattern is
            // preserved across the cast — the server compares on the
            // raw 32 bits — so a hash >= 0x80000000 becomes a
            // legitimate negative int and still matches what the mo
            // driver emits.
            return Int32(bitPattern: hash)
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
}
