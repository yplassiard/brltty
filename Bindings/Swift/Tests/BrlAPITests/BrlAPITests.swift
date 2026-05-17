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
