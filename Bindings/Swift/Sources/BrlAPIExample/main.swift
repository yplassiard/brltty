//
// BrlAPIExample — minimal CLI demonstrating the Swift binding.
//
// Run this against a live brltty:
//
//     swift run BrlAPIExample
//
// It opens a connection, prints display geometry + driver info, enters
// tty mode, writes "Hello, BrlAPI" to the display, then loops reading
// keys until you press a switch on the braille terminal.
//

import BrlAPI
import Foundation

func run() throws {
    let version = BrlAPI.libraryVersion
    print("libbrlapi \(version.major).\(version.minor).\(version.revision)")

    let connection = try BrlAPI.Connection()
    defer { connection.close() }

    let size = connection.displaySize
    print("driver: \(connection.driverName)")
    print("model:  \(connection.modelIdentifier)")
    print("display: \(size.width) x \(size.height) (\(size.totalCells) cells)")

    if size.totalCells == 0 {
        print("no display attached — nothing to write to.")
        return
    }

    // tty == -1 means "any tty" — the client receives focus regardless
    // of which terminal is in the foreground. Use a specific number if
    // you want focus to be scoped to one tty only.
    try connection.enterTtyMode(tty: -1)
    defer { try? connection.leaveTtyMode() }

    try connection.writeText("Hello, BrlAPI", cursor: 1)

    print("Press any key on the braille display (Ctrl-C to exit)…")
    while let key = try connection.readKeyExpanded() {
        print("key: type=\(key.type) command=0x\(String(key.command, radix: 16)) arg=\(key.argument) flags=0x\(String(key.flags, radix: 16))")
    }
}

do {
    try run()
} catch {
    FileHandle.standardError.write(Data("error: \(error)\n".utf8))
    exit(1)
}
