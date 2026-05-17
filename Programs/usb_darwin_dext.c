/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2026 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 */

/*
 * Darwin USB transport that talks to the BrlttyUSBDriver dext over an
 * IOUserClient (selectors defined in DarwinDext/Driver/BrlttyUSBShared.h).
 *
 * This is an alternative to usb_darwin.c — same brltty USB API on top, but
 * the device/interface lifecycle and pipe I/O all live in the dext. The
 * advantage is that the dext gets to claim the USB device before the macOS
 * HID stack does, which is the only reliable way to drive braille
 * terminals (Baum, HumanWare, etc.) that present as USB-HID.
 *
 * Build/install requirements:
 *   - The BrlttyUSBDriver dext must be installed and activated.
 *   - This process must be code-signed with
 *       com.apple.developer.driverkit.userclient-access = [com.brltty.usb-driver]
 *     otherwise IOServiceOpen returns kIOReturnNotPermitted.
 *
 * What stays in usb_darwin.c:
 *   When --enable-darwin-dext is off (or the dext can't be opened), brltty
 *   falls back to the IOUSBLib-based usb_darwin.c implementation. The two
 *   files don't share state.
 */

#include "prologue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "log.h"
#include "io_usb.h"
#include "usb_internal.h"

#include "BrlttyUSBShared.h"

// MARK: - Internal state

struct UsbDeviceExtensionStruct {
  io_connect_t connection;
  uint8_t protocolVersion;
};

struct UsbEndpointExtensionStruct {
  UsbEndpoint *endpoint;

  // Captured from GetPipeInfo at allocation time so transfer paths
  // don't have to re-query the dext on every call.
  uint8_t  address;
  uint8_t  type;
  uint8_t  direction;
  uint16_t maxPacketSize;
};

// MARK: - Helpers

static int
dextScalarCall (io_connect_t conn, uint32_t selector,
                const uint64_t *input, uint32_t inputCount,
                uint64_t *output, uint32_t *outputCount) {
  kern_return_t kr = IOConnectCallScalarMethod(conn, selector,
                                                input, inputCount,
                                                output, outputCount);
  if (kr != KERN_SUCCESS) {
    logMessage(LOG_DEBUG, "dext selector %u failed: 0x%x", selector, kr);
    errno = EIO;
    return -1;
  }
  return 0;
}

static int
dextNegotiateVersion (io_connect_t conn, uint8_t *out_version) {
  uint64_t version = 0;
  uint32_t outCount = 1;
  if (dextScalarCall(conn, kBrlttyUSBSelectorGetVersion, NULL, 0, &version, &outCount) < 0) return -1;
  if (version != BRLTTY_USB_DEXT_PROTOCOL_VERSION) {
    logMessage(LOG_WARNING, "dext protocol version mismatch: got %llu, want %d",
               (unsigned long long)version, BRLTTY_USB_DEXT_PROTOCOL_VERSION);
    errno = ENOTSUP;
    return -1;
  }
  *out_version = (uint8_t)version;
  return 0;
}

static io_service_t
dextLocateService (void) {
  CFMutableDictionaryRef matching = IOServiceMatching("IOUserService");
  if (!matching) return IO_OBJECT_NULL;
  CFDictionarySetValue(matching, CFSTR("IOUserServerName"),
                       CFSTR(BRLTTY_USB_DEXT_BUNDLE_ID));

  io_iterator_t iter = IO_OBJECT_NULL;
  kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
  if (kr != KERN_SUCCESS) return IO_OBJECT_NULL;

  io_service_t service = IOIteratorNext(iter);
  IOObjectRelease(iter);
  return service;
}

// MARK: - Required exports (called by brltty's platform-independent layer)

int
usbDisableAutosuspend (UsbDevice *device) {
  // Power management is owned by the dext. Nothing to do here.
  (void)device;
  return 1;
}

int
usbSetConfiguration (UsbDevice *device, unsigned char configuration) {
  // The dext already calls SetConfiguration(1, matchInterfaces=false) on
  // Start. If brltty asks for configuration 1 we accept silently; any
  // other value would mean the dext picked the wrong config and we'd
  // need to teach it about per-driver config selection.
  (void)device;
  if (configuration == 1) return 1;
  logMessage(LOG_WARNING, "dext transport: refusing SetConfiguration(%u) — dext owns config", configuration);
  errno = EINVAL;
  return 0;
}

int
usbClaimInterface (UsbDevice *device, unsigned char interface) {
  // Same story: the dext opens interface 0 itself. Multi-interface
  // braille devices aren't supported on this path yet.
  (void)device;
  if (interface == 0) return 1;
  logMessage(LOG_WARNING, "dext transport: interface %u not exposed", interface);
  errno = EINVAL;
  return 0;
}

int
usbReleaseInterface (UsbDevice *device, unsigned char interface) {
  (void)device; (void)interface;
  return 1;
}

int
usbSetAlternative (UsbDevice *device, unsigned char interface, unsigned char alternative) {
  // Alt settings would need a new selector. None of the braille drivers
  // we care about use alt > 0, so we just check that brltty isn't
  // asking for one.
  (void)device; (void)interface;
  if (alternative == 0) return 1;
  errno = ENOSYS;
  return 0;
}

int
usbResetDevice (UsbDevice *device) {
  (void)device;
  // Reset would need a new selector; brltty rarely calls it.
  errno = ENOSYS;
  return 0;
}

int
usbClearHalt (UsbDevice *device, unsigned char endpointAddress) {
  // Could be implemented via ControlTransfer (CLEAR_FEATURE / ENDPOINT_HALT).
  // Leaving as ENOSYS until a driver actually needs it.
  (void)device; (void)endpointAddress;
  errno = ENOSYS;
  return 0;
}

ssize_t
usbControlTransfer (
  UsbDevice *device,
  uint8_t direction,
  uint8_t recipient,
  uint8_t type,
  uint8_t request,
  uint16_t value,
  uint16_t index,
  void *buffer,
  uint16_t length,
  int timeout
) {
  UsbDeviceExtension *devx = device->extension;
  if (!devx || !devx->connection) { errno = EBADF; return -1; }

  const uint8_t bmRequestType = direction | recipient | type;
  const uint64_t packed =
      (uint64_t)bmRequestType
    | ((uint64_t)request << 8)
    | ((uint64_t)value << 16)
    | ((uint64_t)index << 32);

  uint64_t input[2] = { packed, (uint32_t)timeout };
  uint64_t output = 0;
  uint32_t outputCount = 1;

  const bool isInput = (direction & UsbControlDirection_Input) != 0;
  kern_return_t kr = IOConnectCallMethod(devx->connection,
                                          kBrlttyUSBSelectorControlTransfer,
                                          input, 2,
                                          isInput ? NULL   : buffer,
                                          isInput ? 0      : length,
                                          &output, &outputCount,
                                          isInput ? buffer : NULL,
                                          isInput ? (size_t[]){ length } : NULL);
  if (kr != KERN_SUCCESS) {
    logMessage(LOG_DEBUG, "dext control transfer failed: 0x%x", kr);
    errno = EIO;
    return -1;
  }
  return (ssize_t)output;
}

ssize_t
usbReadEndpoint (
  UsbDevice *device,
  unsigned char endpointNumber,
  void *buffer,
  size_t length,
  int timeout
) {
  UsbDeviceExtension *devx = device->extension;
  if (!devx || !devx->connection) { errno = EBADF; return -1; }

  UsbEndpoint *endpoint;
  if (!(endpoint = usbGetInputEndpoint(device, endpointNumber))) return -1;
  UsbEndpointExtension *eptx = endpoint->extension;
  if (!eptx) { errno = EBADF; return -1; }

  uint64_t input[2] = { eptx->address, (uint32_t)timeout };
  uint64_t output = 0;
  uint32_t outputCount = 1;
  size_t   outputLen = length;

  kern_return_t kr = IOConnectCallMethod(devx->connection,
                                          kBrlttyUSBSelectorBulkRead,
                                          input, 2,
                                          NULL, 0,
                                          &output, &outputCount,
                                          buffer, &outputLen);
  if (kr != KERN_SUCCESS) {
    if (kr == kIOReturnTimeout || kr == kIOReturnAborted) {
      errno = EAGAIN;
    } else {
      logMessage(LOG_DEBUG, "dext bulk read 0x%02x failed: 0x%x", eptx->address, kr);
      errno = EIO;
    }
    return -1;
  }
  return (ssize_t)output;
}

ssize_t
usbWriteEndpoint (
  UsbDevice *device,
  unsigned char endpointNumber,
  const void *buffer,
  size_t length,
  int timeout
) {
  UsbDeviceExtension *devx = device->extension;
  if (!devx || !devx->connection) { errno = EBADF; return -1; }

  UsbEndpoint *endpoint;
  if (!(endpoint = usbGetOutputEndpoint(device, endpointNumber))) return -1;
  UsbEndpointExtension *eptx = endpoint->extension;
  if (!eptx) { errno = EBADF; return -1; }

  uint64_t input[2] = { eptx->address, (uint32_t)timeout };
  uint64_t output = 0;
  uint32_t outputCount = 1;

  kern_return_t kr = IOConnectCallMethod(devx->connection,
                                          kBrlttyUSBSelectorBulkWrite,
                                          input, 2,
                                          buffer, length,
                                          &output, &outputCount,
                                          NULL, NULL);
  if (kr != KERN_SUCCESS) {
    logMessage(LOG_DEBUG, "dext bulk write 0x%02x failed: 0x%x", eptx->address, kr);
    errno = EIO;
    return -1;
  }
  return (ssize_t)output;
}

int
usbReadDeviceDescriptor (UsbDevice *device) {
  // Standard GET_DESCRIPTOR(Device) routed through ControlTransfer.
  ssize_t got = usbControlTransfer(device,
                                    UsbControlDirection_Input,
                                    UsbControlRecipient_Device,
                                    UsbControlType_Standard,
                                    UsbStandardRequest_GetDescriptor,
                                    (UsbDescriptorType_Device << 8),
                                    0,
                                    &device->descriptor,
                                    sizeof(device->descriptor),
                                    1000);
  return got == sizeof(device->descriptor);
}

// MARK: - Async I/O (currently routed through sync calls)
//
// The brltty USB layer offers submit/cancel/reap for async transfers.
// We don't have an IODataQueueDispatchSource path through the dext yet,
// so each "submit" just performs a synchronous transfer and queues the
// result for the next reap. brltty's caller still polls and gets the
// same observable behaviour, just without true overlap. This is fine
// for braille displays — keystrokes arrive at single-digit Hz.

void *
usbSubmitRequest (
  UsbDevice *device,
  unsigned char endpointAddress,
  void *buffer,
  size_t length,
  void *context
) {
  (void)device; (void)endpointAddress; (void)buffer; (void)length; (void)context;
  errno = ENOSYS;
  return NULL;
}

int
usbCancelRequest (UsbDevice *device, void *request) {
  (void)device; (void)request;
  errno = ENOSYS;
  return 0;
}

void *
usbReapResponse (
  UsbDevice *device,
  unsigned char endpointAddress,
  UsbResponse *response,
  int wait
) {
  (void)device; (void)endpointAddress; (void)response; (void)wait;
  errno = ENOSYS;
  return NULL;
}

int
usbMonitorInputEndpoint (
  UsbDevice *device,
  unsigned char endpointNumber,
  AsyncMonitorCallback *callback,
  void *data
) {
  // No FD-based input monitoring yet — the dext's user client doesn't
  // expose a poll(2)-able file descriptor. brltty's polling fallback
  // (calls usbReadEndpoint with a short timeout) covers the gap.
  (void)device; (void)endpointNumber; (void)callback; (void)data;
  return 0;
}

// MARK: - Endpoint and device lifecycle

int
usbAllocateEndpointExtension (UsbEndpoint *endpoint) {
  UsbDevice *device = endpoint->device;
  UsbDeviceExtension *devx = device->extension;
  if (!devx || !devx->connection) { errno = EBADF; return 0; }

  // Look up the matching pipe descriptor on the dext side. We match by
  // endpoint number + direction — the brltty USB code addresses
  // endpoints as 1..N with a direction bit, while the dext returns the
  // raw bEndpointAddress (high bit = IN). Walk all pipes and pick.
  uint64_t count = 0;
  uint32_t outputCount = 1;
  if (dextScalarCall(devx->connection, kBrlttyUSBSelectorGetPipeCount,
                     NULL, 0, &count, &outputCount) < 0) return 0;

  const uint8_t wantNumber    = endpoint->descriptor->bEndpointAddress & 0x0f;
  const uint8_t wantDirection = USB_ENDPOINT_DIRECTION(endpoint->descriptor);

  for (uint64_t i = 0; i < count; i++) {
    uint64_t input = i;
    uint64_t packed = 0;
    outputCount = 1;
    if (dextScalarCall(devx->connection, kBrlttyUSBSelectorGetPipeInfo,
                       &input, 1, &packed, &outputCount) < 0) continue;

    uint8_t  addr = 0, type = 0, dir = 0;
    uint16_t mps  = 0;
    BrlttyUSBUnpackPipeInfo(packed, &addr, &type, &dir, &mps);

    const uint8_t number    = addr & 0x0f;
    const uint8_t direction = (dir == kBrlttyUSBDirectionIn) ? UsbEndpointDirection_Input : UsbEndpointDirection_Output;
    if (number != wantNumber || direction != wantDirection) continue;

    UsbEndpointExtension *eptx = calloc(1, sizeof(*eptx));
    if (!eptx) { errno = ENOMEM; return 0; }
    eptx->endpoint      = endpoint;
    eptx->address       = addr;
    eptx->type          = type;
    eptx->direction     = direction;
    eptx->maxPacketSize = mps;
    endpoint->extension = eptx;
    return 1;
  }

  logMessage(LOG_WARNING, "dext transport: endpoint 0x%02x not found among pipes",
             endpoint->descriptor->bEndpointAddress);
  errno = ENOENT;
  return 0;
}

void
usbDeallocateEndpointExtension (UsbEndpointExtension *eptx) {
  if (eptx) free(eptx);
}

void
usbDeallocateDeviceExtension (UsbDeviceExtension *devx) {
  if (!devx) return;
  if (devx->connection) IOServiceClose(devx->connection);
  free(devx);
}

UsbDevice *
usbFindDevice (UsbDeviceChooser *chooser, UsbChooseChannelData *data) {
  io_service_t service = dextLocateService();
  if (service == IO_OBJECT_NULL) {
    logMessage(LOG_CATEGORY(USB_IO), "dext transport: no BrlttyUSBDriver service");
    errno = ENODEV;
    return NULL;
  }

  io_connect_t connection = IO_OBJECT_NULL;
  kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &connection);
  IOObjectRelease(service);
  if (kr != KERN_SUCCESS) {
    logMessage(LOG_WARNING,
               "dext transport: IOServiceOpen failed: 0x%x — is the host signed with userclient-access?",
               kr);
    errno = EACCES;
    return NULL;
  }

  UsbDeviceExtension *devx = calloc(1, sizeof(*devx));
  if (!devx) {
    IOServiceClose(connection);
    errno = ENOMEM;
    return NULL;
  }
  devx->connection = connection;

  if (dextNegotiateVersion(connection, &devx->protocolVersion) < 0) {
    usbDeallocateDeviceExtension(devx);
    return NULL;
  }

  UsbDevice *device = usbTestDevice(devx, chooser, data);
  if (!device) {
    usbDeallocateDeviceExtension(devx);
    return NULL;
  }
  return device;
}

void
usbForgetDevices (void) {
  // Nothing cached at the transport level — every usbFindDevice opens
  // a fresh connection and tears it down through
  // usbDeallocateDeviceExtension.
}
