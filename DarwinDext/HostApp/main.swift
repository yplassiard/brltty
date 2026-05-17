// Headless macOS app that activates / deactivates the brltty USB driver
// system extension. Bundled as a .app so the dext is found at
// Contents/Library/SystemExtensions/. Run as:
//   open BrlttyUSBHost.app --args activate
//   open BrlttyUSBHost.app --args deactivate
//   open BrlttyUSBHost.app --args status
// Or directly:
//   BrlttyUSBHost.app/Contents/MacOS/BrlttyUSBHost activate

import Cocoa
import Foundation
import SystemExtensions

private let driverIdentifier = "com.brltty.usb-driver"

final class Delegate: NSObject, OSSystemExtensionRequestDelegate {
    var exitCode: Int32 = 0

    func request(_ request: OSSystemExtensionRequest,
                 actionForReplacingExtension existing: OSSystemExtensionProperties,
                 withExtension ext: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction {
        NSLog("brltty-host: replacing existing extension %@ with %@", existing.bundleVersion, ext.bundleVersion)
        return .replace
    }

    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        NSLog("brltty-host: system extension requires user approval (Settings → Privacy & Security)")
    }

    func request(_ request: OSSystemExtensionRequest,
                 didFinishWithResult result: OSSystemExtensionRequest.Result) {
        NSLog("brltty-host: request finished: %d", result.rawValue)
        NSApp.terminate(nil)
    }

    func request(_ request: OSSystemExtensionRequest,
                 didFailWithError error: Error) {
        NSLog("brltty-host: request failed: %@", error.localizedDescription)
        exitCode = 1
        NSApp.terminate(nil)
    }
}

class AppDelegate: NSObject, NSApplicationDelegate {
    let helper = Delegate()

    func applicationDidFinishLaunching(_ notification: Notification) {
        let args = CommandLine.arguments
        guard args.count >= 2 else {
            NSLog("brltty-host: usage: BrlttyUSBHost activate|deactivate|status|probe")
            exit(2)
        }

        // Probe runs synchronously and exits — no run loop needed.
        // Handled here (rather than below) so we don't even spin up
        // the AppKit machinery when all we want is an IPC check.
        if args[1] == "probe" {
            runDextProbe()
        }

        let queue = DispatchQueue.main
        let manager = OSSystemExtensionManager.shared

        switch args[1] {
        case "activate":
            let req = OSSystemExtensionRequest.activationRequest(forExtensionWithIdentifier: driverIdentifier, queue: queue)
            req.delegate = helper
            manager.submitRequest(req)
        case "deactivate":
            let req = OSSystemExtensionRequest.deactivationRequest(forExtensionWithIdentifier: driverIdentifier, queue: queue)
            req.delegate = helper
            manager.submitRequest(req)
        case "status":
            let req = OSSystemExtensionRequest.propertiesRequest(forExtensionWithIdentifier: driverIdentifier, queue: queue)
            req.delegate = helper
            manager.submitRequest(req)
        default:
            NSLog("brltty-host: unknown command: %@", args[1])
            exit(2)
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        exit(helper.exitCode)
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
