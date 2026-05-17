// swift-tools-version:5.7
//
// libbrlapi — A library providing access to braille terminals for applications.
//
// Copyright (C) 2005-2026 by The BRLTTY Developers.
//
// libbrlapi comes with ABSOLUTELY NO WARRANTY.
//
// This is free software, placed under the terms of the
// GNU Lesser General Public License, as published by the Free Software
// Foundation; either version 2.1 of the License, or (at your option) any
// later version. Please see the file LICENSE-LGPL for details.

import PackageDescription

let package = Package(
    name: "BrlAPI",
    products: [
        .library(name: "BrlAPI", targets: ["BrlAPI"]),
        .executable(name: "BrlAPIExample", targets: ["BrlAPIExample"]),
    ],
    targets: [
        // System module that wraps brlapi.h and links against libbrlapi.
        // Relies on pkg-config when available; otherwise the library is
        // resolved via the system linker's default search paths.
        .systemLibrary(
            name: "CBrlAPI",
            path: "Sources/CBrlAPI",
            pkgConfig: "brlapi",
            providers: [
                .apt(["libbrlapi-dev"]),
                .brew(["brltty"]),
            ]
        ),
        // The idiomatic Swift wrapper exposed to client applications.
        .target(
            name: "BrlAPI",
            dependencies: ["CBrlAPI"],
            path: "Sources/BrlAPI"
        ),
        // Minimal CLI demonstrating connection, write, and key read.
        // Not part of the library product — opt-in via `swift run`.
        .executableTarget(
            name: "BrlAPIExample",
            dependencies: ["BrlAPI"],
            path: "Sources/BrlAPIExample"
        ),
        .testTarget(
            name: "BrlAPITests",
            dependencies: ["BrlAPI"],
            path: "Tests/BrlAPITests"
        ),
    ]
)
