/*
 * Shared definitions between the brltty USB DriverKit dext and the
 * userland transport in brltty itself.
 *
 * IMPORTANT: this header is compiled on both sides of the IPC. Don't
 * pull in DriverKit or IOKit headers here — keep it pure C / POSIX so
 * Programs/usb_darwin_dext.c can include it without dragging in the
 * driver SDK.
 */

#ifndef BRLTTY_USB_SHARED_H
#define BRLTTY_USB_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bundle ID of the dext, used when looking up the IOService from
 * userland with IOServiceMatching("BrlttyUSBClient") or by class name. */
#define BRLTTY_USB_DEXT_BUNDLE_ID "com.brltty.usb-driver"
#define BRLTTY_USB_CLIENT_CLASS   "BrlttyUSBClient"

/* External method selectors. The wire format for each selector is
 * documented inline; keep this stable across releases or bump
 * BRLTTY_USB_DEXT_PROTOCOL_VERSION below. */
enum BrlttyUSBSelector {
    /* Probe round-trip + version negotiation. No inputs, one scalar
     * output carrying BRLTTY_USB_DEXT_PROTOCOL_VERSION. */
    kBrlttyUSBSelectorGetVersion = 0,

    /* Returns the number of endpoints exposed by the matched interface.
     * No inputs, one scalar output. */
    kBrlttyUSBSelectorGetPipeCount = 1,

    /* In: scalarInput[0] = pipe index (0..count-1).
     * Out: scalarOutput[0] = packed {address:8, type:8, direction:8, maxPacketSize:16}. */
    kBrlttyUSBSelectorGetPipeInfo = 2,

    /* In: scalarInput[0] = endpoint address (incl. direction bit),
     *     scalarInput[1] = timeout in milliseconds,
     *     structureOutput = buffer to fill (size determines max read).
     * Out: scalarOutput[0] = bytes actually read. */
    kBrlttyUSBSelectorBulkRead = 3,

    /* In: scalarInput[0] = endpoint address,
     *     scalarInput[1] = timeout in milliseconds,
     *     structureInput = bytes to send.
     * Out: scalarOutput[0] = bytes actually written. */
    kBrlttyUSBSelectorBulkWrite = 4,

    /* In: scalarInput[0] = packed {bmRequestType:8, bRequest:8, wValue:16, wIndex:16},
     *     scalarInput[1] = timeout in milliseconds,
     *     structureInput = OUT data (when wLength > 0 and direction == host-to-device),
     *     structureOutput = IN buffer (when wLength > 0 and direction == device-to-host).
     * Out: scalarOutput[0] = bytes transferred. */
    kBrlttyUSBSelectorControlTransfer = 5,

    kBrlttyUSBSelectorCount
};

/* Pack/unpack helpers for the GetPipeInfo return value. Keeping the
 * value scalar (rather than a struct) avoids the alignment issues that
 * IOConnectCallMethod gets on mixed 32/64 callers. */
static inline uint64_t
BrlttyUSBPackPipeInfo(uint8_t address, uint8_t type, uint8_t direction, uint16_t maxPacketSize)
{
    return ((uint64_t)address)
         | (((uint64_t)type) << 8)
         | (((uint64_t)direction) << 16)
         | (((uint64_t)maxPacketSize) << 24);
}

static inline void
BrlttyUSBUnpackPipeInfo(uint64_t packed,
                        uint8_t *address, uint8_t *type,
                        uint8_t *direction, uint16_t *maxPacketSize)
{
    if (address)        *address       = (uint8_t)(packed & 0xff);
    if (type)           *type          = (uint8_t)((packed >> 8) & 0xff);
    if (direction)      *direction     = (uint8_t)((packed >> 16) & 0xff);
    if (maxPacketSize)  *maxPacketSize = (uint16_t)((packed >> 24) & 0xffff);
}

/* Direction values used by GetPipeInfo (matching USB spec bit 7 of
 * bEndpointAddress, but normalised to 0/1 for clarity). */
enum {
    kBrlttyUSBDirectionOut = 0,
    kBrlttyUSBDirectionIn  = 1,
};

/* USB endpoint transfer types — same values as the USB spec
 * bmAttributes field, kept here so userland clients don't need to
 * pull in IOKit headers. */
enum {
    kBrlttyUSBPipeTypeControl     = 0,
    kBrlttyUSBPipeTypeIsochronous = 1,
    kBrlttyUSBPipeTypeBulk        = 2,
    kBrlttyUSBPipeTypeInterrupt   = 3,
};

/* Bumped whenever the wire format above changes. Userland refuses to
 * talk to a mismatched driver. */
#define BRLTTY_USB_DEXT_PROTOCOL_VERSION 1

#ifdef __cplusplus
}
#endif

#endif /* BRLTTY_USB_SHARED_H */
