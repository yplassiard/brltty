# BrlAPI — Swift binding

Swift bindings for libbrlapi, the client library that lets applications
read keystrokes from and write text to a braille display managed by
[brltty](https://brltty.app/). The binding wraps the per-handle
(`brlapi__*`) C API so multiple `Connection` instances can coexist
safely in the same process.

## Requirements

- Swift 5.7 or later (Swift Package Manager)
- libbrlapi installed and discoverable through `pkg-config`
  - macOS (Homebrew): `brew install brltty`
  - Debian/Ubuntu: `sudo apt install libbrlapi-dev`
- A running brltty instance, configured to accept BrlAPI connections

## Quick start

Add the package to your `Package.swift`:

```swift
.package(url: "https://github.com/brltty/brltty.git", branch: "master"),
```

Then depend on the `BrlAPI` product:

```swift
.target(name: "MyApp", dependencies: [
    .product(name: "BrlAPI", package: "brltty"),
])
```

Open a connection and write to the display:

```swift
import BrlAPI

let connection = try BrlAPI.Connection()
defer { connection.close() }

try connection.enterTtyMode()
try connection.writeText("Hello, braille!", cursor: 1)

while let key = try connection.readKeyExpanded() {
    print("\(key.type): cmd=\(key.command) arg=\(key.argument)")
}
```

If brltty is running on a different host, or you need a non-default
authentication scheme, pass a `Settings` value:

```swift
let connection = try BrlAPI.Connection(settings: .init(
    auth: "keyfile:/etc/brlapi.key",
    host: "braille.local:4101"
))
```

## API surface

- `BrlAPI.Connection` — main type. Open, close, write, read keys, enter
  / leave tty mode, suspend / resume the driver.
- `BrlAPI.Connection.Cursor` — `.leave`, `.off`, or `.cell(n)` for
  placing the cursor on writes.
- `BrlAPI.Connection.ExpandedKey` — structured key event with the
  type, command, argument, and flags fields split out.
- `BrlAPI.Settings` — authentication and host configuration. Defaults to
  "local Unix socket, system keyfile" which works for most setups.
- `BrlAPI.Error` — Swift-native error type. Carries a textual message
  from `brlapi_strerror` plus a tag mapping to the BrlAPI error code.

## Running the example

```bash
cd Bindings/Swift
swift run BrlAPIExample
```

The example prints display geometry, writes a greeting, and loops
reading keys until you hit Ctrl-C.

## Running the tests

```bash
cd Bindings/Swift
swift test
```

Live integration tests are skipped by default. To run them against a
real brltty instance:

```bash
BRLAPI_TESTS_LIVE=1 swift test
```

## Build integration

The binding is wired into the autoconf build via
`Bindings/Swift/bindings.m4`. Enable / disable it like the other
bindings:

```bash
./configure --disable-swift-bindings   # opt out
./configure --enable-swift-bindings    # default when swift is found
make
```

Under the hood `make all` in this directory delegates to
`swift build`. Most consumers should use Swift Package Manager directly
rather than installing into a system prefix.

## Threading

`BrlAPI.Connection` is **not** thread-safe — wrap it in your own
serialization mechanism if you plan to call it from multiple queues.
The underlying `brlapi__*` API uses a per-handle storage block, so
distinct `Connection` instances on different threads are safe and
don't share error state.

## License

LGPL-2.1-or-later, matching the rest of libbrlapi.
