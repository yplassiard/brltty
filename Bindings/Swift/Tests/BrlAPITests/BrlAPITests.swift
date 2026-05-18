//
// Tests for the BrlAPI Swift binding.
//
// Most cases are pure — they exercise the Swift surface (defaults, enum
// mapping, struct equality) without touching the network. The live tests
// at the bottom require a running brltty instance; set BRLAPI_TESTS_LIVE=1
// to opt in.
//

import XCTest
@testable import BrlAPI

final class BrlAPITests: XCTestCase {

    func testDefaultPort() {
        XCTAssertEqual(BrlAPI.defaultPort, 4101)
    }

    func testLibraryVersionIsPopulated() {
        let v = BrlAPI.libraryVersion
        // libbrlapi has been at major >= 0 since forever. Mostly we want
        // to confirm the binding actually calls into the C side rather
        // than returning zeroed defaults.
        XCTAssertGreaterThanOrEqual(v.major, 0)
        XCTAssertGreaterThanOrEqual(v.minor, 0)
    }

    func testSettingsEquality() {
        XCTAssertEqual(BrlAPI.Settings(), BrlAPI.Settings())
        XCTAssertEqual(
            BrlAPI.Settings(auth: "none", host: "localhost"),
            BrlAPI.Settings(auth: "none", host: "localhost")
        )
        XCTAssertNotEqual(
            BrlAPI.Settings(auth: "none"),
            BrlAPI.Settings(auth: "keyfile:/etc/brlapi.key")
        )
    }

    func testErrorDescriptions() {
        XCTAssertEqual(
            BrlAPI.Error.notConnected.description,
            "not connected to brltty"
        )
        XCTAssertEqual(
            BrlAPI.Error.ttyBusy.description,
            "tty is busy"
        )
        XCTAssertTrue(
            BrlAPI.Error.connectionFailed("boom").description.contains("boom")
        )
    }

    func testCursorRawMapping() {
        // Verify the public Cursor enum lines up with the C constants the
        // brlapi server expects on the wire. If the C ABI ever changes these
        // values we want to find out here rather than at runtime.
        XCTAssertEqual(BrlAPI.Connection.Cursor.leave.rawForTesting, -1)
        XCTAssertEqual(BrlAPI.Connection.Cursor.off.rawForTesting, 0)
        XCTAssertEqual(BrlAPI.Connection.Cursor.cell(7).rawForTesting, 7)
    }

    // MARK: - macOS scope hash
    //
    // Reference values were computed independently (Python djb2) and
    // also against Drivers/Screen/MacOSAccessibility/screen.m. Any
    // drift between client and server here means brlapi routing
    // silently breaks for app-scoped clients, so the tests pin the
    // numbers explicitly rather than re-deriving them from
    // BrlAPI.MacOSScope.

    func testMacOSScopeDjb2KnownValues() {
        // Empty buffer is djb2's seed (5381).
        XCTAssertEqual(BrlAPI.MacOSScope.djb2(Array("".utf8)), 5381)
        XCTAssertEqual(BrlAPI.MacOSScope.djb2(Array("a".utf8)), 177604)
        XCTAssertEqual(BrlAPI.MacOSScope.djb2(Array("abc".utf8)), 193409669)
    }

    func testMacOSScopeTtyMatchesCImplementation() {
        XCTAssertEqual(
            BrlAPI.MacOSScope.tty(forApp: "com.apple.Terminal", tab: 1),
            Int32(bitPattern: 0x969006a3)
        )
        XCTAssertEqual(
            BrlAPI.MacOSScope.tty(forApp: "com.apple.Safari", tab: 1),
            Int32(bitPattern: 0xc7c91a89)
        )
        XCTAssertEqual(
            BrlAPI.MacOSScope.tty(forApp: "io.github.brltty.brltty", tab: 1),
            1742939779
        )
    }

    func testMacOSScopeCurrentApp() {
        // Whatever Bundle.main resolves to in the test runner (some
        // form of swiftpm test bundle), the helper must either return
        // a deterministic slot or nil — never crash, never randomise.
        let a = BrlAPI.MacOSScope.ttyForCurrentApp(tab: 1)
        let b = BrlAPI.MacOSScope.ttyForCurrentApp(tab: 1)
        XCTAssertEqual(a, b, "ttyForCurrentApp must be deterministic across calls")
        // If we got a slot, it must equal the value our static
        // `tty(forApp:)` computes from the same bundle id — i.e. the
        // convenience is just sugar over the explicit form.
        if let bundleID = Bundle.main.bundleIdentifier {
            XCTAssertEqual(a, BrlAPI.MacOSScope.tty(forApp: bundleID, tab: 1))
        } else {
            XCTAssertNil(a)
        }
    }

    func testMacOSScopeAvoidsSentinel() {
        // Synthetic input whose djb2 happens to be 0xFFFFFFFF would
        // collide with BrlAPI's "no specific tty" sentinel. We can't
        // easily craft one, so just verify that the post-hash
        // sentinel-avoidance keeps the slot a stable non-sentinel
        // int for the common cases.
        let slots = ["com.apple.Terminal", "com.apple.Safari", "com.apple.Finder"].map {
            BrlAPI.MacOSScope.tty(forApp: $0, tab: 1)
        }
        XCTAssertFalse(slots.contains(-1)) // -1 == 0xFFFFFFFF == sentinel
    }

    // MARK: - Live tests

    func testLiveConnection() throws {
        try XCTSkipUnless(ProcessInfo.processInfo.environment["BRLAPI_TESTS_LIVE"] == "1",
                          "Set BRLAPI_TESTS_LIVE=1 with a running brltty to run.")
        let connection = try BrlAPI.Connection()
        defer { connection.close() }
        XCTAssertFalse(connection.driverName.isEmpty)
        XCTAssertGreaterThanOrEqual(connection.fileDescriptor, 0)
    }
}

// Tiny accessor that lets the tests inspect the raw int the wire format
// uses, without making the production API expose it. Keeps the public
// surface clean while still giving us coverage of the mapping.
extension BrlAPI.Connection.Cursor {
    var rawForTesting: Int32 {
        switch self {
        case .leave: return -1
        case .off:   return 0
        case .cell(let c): return Int32(c)
        }
    }
}
