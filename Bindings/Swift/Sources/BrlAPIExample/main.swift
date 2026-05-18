//
// BrlAPIExample — minimal CLI demonstrating the Swift binding.
//
// Usage:
//
//     swift run BrlAPIExample                       # broadcast (tty = -1)
//     swift run BrlAPIExample --self                # scope to *this* binary
//     swift run BrlAPIExample com.apple.Terminal    # scope to another app
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
    //   "--self"             scope to Bundle.main (the running binary)
    //   <bundle-id> [<tab>]  scope to another app (and optional tab)
    //   (none)               broadcast (tty = -1)
    let args = CommandLine.arguments.dropFirst()
    let firstArg = args.first

    var scopeLabel = "any tty (broadcast)"
    let bundleID: String?

    if firstArg == "--self" {
        try connection.enterTtyModeForCurrentApp()
        bundleID = Bundle.main.bundleIdentifier
        scopeLabel = bundleID.map { "self (\($0))" } ?? "self (no bundle id)"
    } else if let bid = firstArg {
        let tab = args.dropFirst().first.flatMap(Int.init) ?? 1
        try connection.enterTtyMode(forApp: bid, tab: tab)
        let slot = BrlAPI.MacOSScope.tty(forApp: bid, tab: tab)
        bundleID = bid
        scopeLabel = "\(bid) tab \(tab) — tty slot 0x\(String(UInt32(bitPattern: slot), radix: 16))"
    } else {
        try connection.enterTtyMode(tty: -1)
        bundleID = nil
    }
    print("scoped to \(scopeLabel)")
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
