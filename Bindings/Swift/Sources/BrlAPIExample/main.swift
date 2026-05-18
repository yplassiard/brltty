//
// BrlAPIExample — minimal CLI demonstrating the Swift binding.
//
// Usage:
//
//     swift run BrlAPIExample                    # broadcast mode (tty = -1)
//     swift run BrlAPIExample com.apple.Terminal # scope to a specific Mac app
//     swift run BrlAPIExample com.apple.Terminal 2  # …on its tab 2
//
// Opens a connection to a running brltty, prints display geometry +
// driver info, enters tty mode, writes a greeting to the display,
// then loops reading keys until you press a switch on the braille
// terminal (or Ctrl-C).
//
// The optional bundle-id argument exercises
// Connection.enterTtyMode(forApp:tab:) — on macOS the brltty
// MacOSAccessibility screen driver routes keystrokes to whichever
// client claimed the matching app+tab scope.
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

    // CLI args:
    //   argv[1] = bundle id (e.g. "com.apple.Terminal"), optional
    //   argv[2] = tab number (Int), optional, defaults to 1
    let args = CommandLine.arguments.dropFirst()
    let bundleID = args.first
    let tab = args.dropFirst().first.flatMap(Int.init) ?? 1

    if let bundleID {
        // Scoped: brltty will only route keystrokes here when this
        // Mac app is frontmost (and the mo driver agrees on the tab
        // counter — see Drivers/Screen/MacOSAccessibility/screen.m).
        try connection.enterTtyMode(forApp: bundleID, tab: tab)
        let slot = BrlAPI.MacOSScope.tty(forApp: bundleID, tab: tab)
        print("scoped to \(bundleID) tab \(tab) — tty slot 0x\(String(UInt32(bitPattern: slot), radix: 16))")
    } else {
        // Broadcast: receive every keystroke regardless of which app
        // is focused. Convenient for ad-hoc testing.
        try connection.enterTtyMode(tty: -1)
        print("scoped to any tty (broadcast)")
    }
    defer { try? connection.leaveTtyMode() }

    let greeting = bundleID.map { "Hello, BrlAPI — \($0)" } ?? "Hello, BrlAPI"
    try connection.writeText(greeting, cursor: 1)

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
