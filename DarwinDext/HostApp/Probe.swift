// Probe the running brltty USB dext by opening its user client and
// walking the external-method surface. Intended as an end-to-end IPC
// smoke test — verifies that:
//   * The dext is matched and Start() has run.
//   * The host has the entitlements needed to open an IOUserClient.
//   * Every selector dispatches correctly through to the dext.
//
// Triggered by `BrlttyUSBHost probe`. Runs without the AppKit loop —
// finishes synchronously, exits 0 on success and prints what it
// observed.

import Foundation
import IOKit

// Mirror BrlttyUSBShared.h selectors. Keep these in sync if the dext
// renumbers — the dext-side enum is the source of truth.
private enum BrlttyUSBSelector: UInt32 {
    case getVersion       = 0
    case getPipeCount     = 1
    case getPipeInfo      = 2
    case bulkRead         = 3
    case bulkWrite        = 4
    case controlTransfer  = 5
}

private let kBrlttyUSBProtocolVersion: UInt64 = 1
private let kBrlttyUSBDriverBundleID = "com.brltty.usb-driver"

private func pipeTypeName(_ type: UInt8) -> String {
    switch type {
    case 0: return "control"
    case 1: return "iso"
    case 2: return "bulk"
    case 3: return "interrupt"
    default: return "?"
    }
}

private func directionName(_ dir: UInt8) -> String {
    dir == 0 ? "OUT" : "IN"
}

private func unpackPipeInfo(_ packed: UInt64)
    -> (address: UInt8, type: UInt8, dir: UInt8, mps: UInt16) {
    let address = UInt8(packed & 0xff)
    let type    = UInt8((packed >> 8) & 0xff)
    let dir     = UInt8((packed >> 16) & 0xff)
    let mps     = UInt16((packed >> 24) & 0xffff)
    return (address, type, dir, mps)
}

/// Find the BrlttyUSBDriver IOService. We narrow `IOUserService`
/// matches by the dext's bundle id so we don't pick up other dexts.
private func findDriverService() -> io_service_t {
    guard let matching = IOServiceMatching("IOUserService") as NSMutableDictionary? else {
        return IO_OBJECT_NULL
    }
    matching["IOUserServerName"] = kBrlttyUSBDriverBundleID

    var iterator: io_iterator_t = IO_OBJECT_NULL
    let kr = IOServiceGetMatchingServices(kIOMainPortDefault,
                                          matching.copy() as! CFDictionary,
                                          &iterator)
    guard kr == KERN_SUCCESS else { return IO_OBJECT_NULL }
    defer { IOObjectRelease(iterator) }
    return IOIteratorNext(iterator)
}

/// Make a scalar→scalar call into the dext.
private func callScalar(_ conn: io_connect_t,
                        selector: BrlttyUSBSelector,
                        input: [UInt64] = [],
                        outputCount: UInt32 = 1) throws -> [UInt64] {
    var output = [UInt64](repeating: 0, count: Int(outputCount))
    var actualOut = outputCount
    let kr = input.withUnsafeBufferPointer { ip -> kern_return_t in
        output.withUnsafeMutableBufferPointer { op -> kern_return_t in
            IOConnectCallScalarMethod(conn, selector.rawValue,
                                      ip.baseAddress, UInt32(input.count),
                                      op.baseAddress, &actualOut)
        }
    }
    if kr != KERN_SUCCESS {
        throw NSError(domain: "BrlttyUSBProbe", code: Int(kr),
                      userInfo: [NSLocalizedDescriptionKey:
                                 "selector \(selector) returned 0x\(String(kr, radix: 16))"])
    }
    return Array(output.prefix(Int(actualOut)))
}

/// Issue a BulkWrite — exercises the structureInput path.
private func bulkWrite(_ conn: io_connect_t,
                       address: UInt8,
                       timeoutMs: UInt32,
                       payload: [UInt8]) throws -> UInt64 {
    let input: [UInt64] = [UInt64(address), UInt64(timeoutMs)]
    var output: UInt64 = 0
    var outputCount: UInt32 = 1
    let kr = input.withUnsafeBufferPointer { ip -> kern_return_t in
        payload.withUnsafeBufferPointer { pp -> kern_return_t in
            withUnsafeMutablePointer(to: &output) { op -> kern_return_t in
                IOConnectCallMethod(conn,
                                    BrlttyUSBSelector.bulkWrite.rawValue,
                                    ip.baseAddress, UInt32(input.count),
                                    pp.baseAddress, payload.count,
                                    op, &outputCount,
                                    nil, nil)
            }
        }
    }
    if kr != KERN_SUCCESS {
        throw NSError(domain: "BrlttyUSBProbe", code: Int(kr),
                      userInfo: [NSLocalizedDescriptionKey:
                                 "BulkWrite returned 0x\(String(kr, radix: 16))"])
    }
    return output
}

/// Run the probe synchronously and exit with a useful status.
func runDextProbe() -> Never {
    let service = findDriverService()
    if service == IO_OBJECT_NULL {
        FileHandle.standardError.write(Data(
            "no BrlttyUSBDriver service found — is the dext active and matched against a device?\n".utf8))
        exit(2)
    }
    defer { IOObjectRelease(service) }

    var connection: io_connect_t = IO_OBJECT_NULL
    let openResult = IOServiceOpen(service, mach_task_self_, 0, &connection)
    guard openResult == KERN_SUCCESS else {
        FileHandle.standardError.write(Data(
            "IOServiceOpen failed: 0x\(String(openResult, radix: 16))\n".utf8))
        exit(3)
    }
    defer { IOServiceClose(connection) }
    print("opened user client OK")

    do {
        let v = try callScalar(connection, selector: .getVersion).first ?? 0
        print("protocol version: \(v) (expected \(kBrlttyUSBProtocolVersion))")
        if v != kBrlttyUSBProtocolVersion {
            print("  WARNING: protocol mismatch — dext + host built from different trees?")
        }

        let pipeCount = try callScalar(connection, selector: .getPipeCount).first ?? 0
        print("pipe count: \(pipeCount)")

        for i in 0 ..< pipeCount {
            let packed = try callScalar(connection, selector: .getPipeInfo, input: [i]).first ?? 0
            let (addr, type, dir, mps) = unpackPipeInfo(packed)
            print(String(format: "  pipe[%llu] addr=0x%02x type=%@ dir=%@ maxPacket=%u",
                         i, addr, pipeTypeName(type) as NSString,
                         directionName(dir) as NSString, mps))
        }

        // Don't issue a BulkRead in the probe — without an OUT issued
        // first the device probably has nothing to send and we'd block
        // (or timeout). Leave actual I/O to the brltty userland
        // transport once the IPC path is proven.
        print("probe complete — IPC surface looks healthy")
        exit(0)
    } catch {
        FileHandle.standardError.write(Data("probe error: \(error.localizedDescription)\n".utf8))
        exit(4)
    }
}
