/*
 * Minimal probe: capture a USB device away from its current driver (typically
 * AppleUserUSBHostHIDDevice) using IOUSBLib's USBDeviceReEnumerate with the
 * capture-device flag. Hold the device until SIGINT, then release.
 *
 * No DriverKit, no entitlements, no kext. Same mechanism VirtualBox/VMware use.
 *
 * Build:  cc -framework IOKit -framework CoreFoundation -o usb_capture usb_capture.c
 * Run:    sudo ./usb_capture <vid> <pid>          # vid/pid hex without 0x
 *         sudo ./usb_capture 0904 6102            # VarioUltra 40
 *
 * Expected behaviour while running:
 *   - VoiceOver loses braille output (device captured)
 * On Ctrl+C:
 *   - Device released; VoiceOver should pick it up again on next enumeration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USB.h>

#ifndef kUSBReEnumerateCaptureDeviceMask
#define kUSBReEnumerateCaptureDeviceMask (1U << 30)
#endif
#ifndef kUSBReEnumerateReleaseDeviceMask
#define kUSBReEnumerateReleaseDeviceMask (1U << 29)
#endif

static volatile sig_atomic_t gStop = 0;
static IOUSBDeviceInterface500 **gDevice = NULL;

static void on_sigint(int s) {
    (void)s;
    gStop = 1;
}

static int parse_hex_u16(const char *s, uint16_t *out) {
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (*end || v > 0xFFFF) return -1;
    *out = (uint16_t)v;
    return 0;
}

static IOUSBDeviceInterface500 **
open_usb_device(uint16_t vid, uint16_t pid) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) {
        fprintf(stderr, "IOServiceMatching failed\n");
        return NULL;
    }

    CFNumberRef vidNum = CFNumberCreate(NULL, kCFNumberSInt16Type, &vid);
    CFNumberRef pidNum = CFNumberCreate(NULL, kCFNumberSInt16Type, &pid);
    CFDictionarySetValue(match, CFSTR(kUSBVendorID), vidNum);
    CFDictionarySetValue(match, CFSTR(kUSBProductID), pidNum);
    CFRelease(vidNum);
    CFRelease(pidNum);

    io_iterator_t iter = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceGetMatchingServices failed: 0x%x\n", kr);
        return NULL;
    }

    io_service_t service;
    IOUSBDeviceInterface500 **dev = NULL;

    while ((service = IOIteratorNext(iter))) {
        IOCFPlugInInterface **plugin = NULL;
        SInt32 score = 0;
        kr = IOCreatePlugInInterfaceForService(service,
                                               kIOUSBDeviceUserClientTypeID,
                                               kIOCFPlugInInterfaceID,
                                               &plugin, &score);
        IOObjectRelease(service);
        if (kr != KERN_SUCCESS || !plugin) continue;

        HRESULT hr = (*plugin)->QueryInterface(plugin,
            CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500),
            (LPVOID *)&dev);
        IODestroyPlugInInterface(plugin);
        if (hr || !dev) continue;
        break;
    }
    IOObjectRelease(iter);

    return dev;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <vid-hex> <pid-hex>\n", argv[0]);
        return 2;
    }

    uint16_t vid, pid;
    if (parse_hex_u16(argv[1], &vid) || parse_hex_u16(argv[2], &pid)) {
        fprintf(stderr, "invalid hex value\n");
        return 2;
    }

    fprintf(stderr, "Looking for USB device %04x:%04x ...\n", vid, pid);

    IOUSBDeviceInterface500 **dev = open_usb_device(vid, pid);
    if (!dev) {
        fprintf(stderr, "device not found\n");
        return 1;
    }
    gDevice = dev;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    fprintf(stderr, "Found. Requesting capture via USBDeviceReEnumerate(0x%08x)...\n",
            kUSBReEnumerateCaptureDeviceMask);

    IOReturn r = (*dev)->USBDeviceReEnumerate(dev, kUSBReEnumerateCaptureDeviceMask);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "USBDeviceReEnumerate (capture) failed: 0x%x\n", r);
        (*dev)->Release(dev);
        return 1;
    }

    fprintf(stderr, "Capture submitted. The device will detach and re-enumerate.\n");
    fprintf(stderr, "If capture works, VoiceOver braille output will stop now.\n");
    fprintf(stderr, "Press Ctrl+C to release the device back to the system.\n");

    while (!gStop) pause();

    fprintf(stderr, "\nReleasing device via USBDeviceReEnumerate(0x%08x)...\n",
            kUSBReEnumerateReleaseDeviceMask);

    /* The original handle may be stale after re-enumeration; refresh. */
    (*dev)->Release(dev);
    gDevice = NULL;

    dev = open_usb_device(vid, pid);
    if (dev) {
        r = (*dev)->USBDeviceReEnumerate(dev, kUSBReEnumerateReleaseDeviceMask);
        if (r != kIOReturnSuccess) {
            fprintf(stderr, "USBDeviceReEnumerate (release) failed: 0x%x\n", r);
        } else {
            fprintf(stderr, "Released. VoiceOver should regain braille on next match.\n");
        }
        (*dev)->Release(dev);
    } else {
        fprintf(stderr, "Could not reacquire device handle for release.\n");
    }

    return 0;
}
