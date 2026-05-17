/*
 * brltty USB user-space driver (DriverKit dext).
 *
 * Matches at the IOUSBHostDevice level (not the interface level) so that we
 * grab the whole device before macOS publishes the IOUSBHostInterface children
 * — that's the only way to keep AppleUserHIDDevice from claiming braille
 * displays via the HID stack. With matchInterfaces=false on SetConfiguration
 * the kernel never even runs interface matching, so there's no race.
 *
 * We then open the first IOUSBHostInterface ourselves and hand it to
 * BrlttyUSBClient, which exposes pipe I/O to brltty userland.
 */

#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOService.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/OSDictionary.h>
#include <USBDriverKit/IOUSBHostInterface.h>
#include <USBDriverKit/IOUSBHostDevice.h>
#include <USBDriverKit/IOUSBHostPipe.h>

#include "BrlttyUSBDriver.h"
#include "BrlttyUSBClient.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "brltty-dext: " fmt, ##__VA_ARGS__)

struct BrlttyUSBDriver_IVars {
    IOUSBHostDevice    *device;     // retained — owns the device
    IOUSBHostInterface *interface;  // retained — first interface, handed to userland
    bool                deviceOpen;
};

bool
BrlttyUSBDriver::init()
{
    if (!super::init()) return false;

    ivars = IONewZero(BrlttyUSBDriver_IVars, 1);
    if (!ivars) return false;

    LOG("init");
    return true;
}

void
BrlttyUSBDriver::free()
{
    LOG("free");
    if (ivars) {
        IOSafeDeleteNULL(ivars, BrlttyUSBDriver_IVars, 1);
    }
    super::free();
}

kern_return_t
IMPL(BrlttyUSBDriver, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        LOG("super Start failed: 0x%x", ret);
        return ret;
    }

    // Provider is the IOUSBHostDevice we matched on (idVendor + idProduct).
    IOUSBHostDevice *dev = OSDynamicCast(IOUSBHostDevice, provider);
    if (!dev) {
        LOG("provider is not IOUSBHostDevice");
        return kIOReturnInvalid;
    }

    // Log what we matched so debugging is easier when a new braille
    // display shows up.
    uint16_t vendor = 0, product = 0;
    const IOUSBDeviceDescriptor *dd = dev->CopyDeviceDescriptor();
    if (dd) {
        vendor  = dd->idVendor;
        product = dd->idProduct;
    }
    LOG("Start: matched USB device vendor=0x%04x product=0x%04x", vendor, product);

    ret = dev->Open(this, 0, 0);
    if (ret != kIOReturnSuccess) {
        LOG("Open(device) failed: 0x%x", ret);
        return ret;
    }
    ivars->deviceOpen = true;

    // The crucial trick: tell the kernel NOT to run interface matching when
    // we set the configuration. Without this, IOUSBHostInterface children
    // would be published and AppleUserHIDDevice would claim the HID one.
    // With matchInterfaces=false we own everything and the HID stack stays
    // out of our way.
    ret = dev->SetConfiguration(1, /* matchInterfaces */ false);
    if (ret != kIOReturnSuccess) {
        LOG("SetConfiguration(1) failed: 0x%x", ret);
        dev->Close(this, 0);
        ivars->deviceOpen = false;
        return ret;
    }

    // Walk the device's interfaces and keep the first one for userland.
    // Braille displays we care about are single-interface (HID) — anything
    // multi-interface (composite serial bridges, etc.) keeps the first
    // interface for now; we can revisit if a specific device needs more.
    uintptr_t iter = 0;
    ret = dev->CreateInterfaceIterator(&iter);
    if (ret != kIOReturnSuccess) {
        LOG("CreateInterfaceIterator failed: 0x%x", ret);
        dev->Close(this, 0);
        ivars->deviceOpen = false;
        return ret;
    }

    IOUSBHostInterface *iface = nullptr;
    ret = dev->CopyInterface(iter, &iface);
    dev->DestroyInterfaceIterator(iter);
    if (ret != kIOReturnSuccess || !iface) {
        LOG("CopyInterface failed: 0x%x (iface=%p)", ret, iface);
        OSSafeReleaseNULL(iface);
        dev->Close(this, 0);
        ivars->deviceOpen = false;
        return ret != kIOReturnSuccess ? ret : kIOReturnNoDevice;
    }

    ret = iface->Open(this, 0, 0);
    if (ret != kIOReturnSuccess) {
        LOG("Open(interface) failed: 0x%x", ret);
        OSSafeReleaseNULL(iface);
        dev->Close(this, 0);
        ivars->deviceOpen = false;
        return ret;
    }

    // Retain the device for the user-client. The interface was returned
    // already retained by CopyInterface, so no extra retain needed.
    dev->retain();
    ivars->device    = dev;
    ivars->interface = iface;

    LOG("Start: device opened, interface 0 claimed");

    ret = RegisterService();
    if (ret != kIOReturnSuccess) {
        LOG("RegisterService failed: 0x%x", ret);
    }
    return ret;
}

kern_return_t
IMPL(BrlttyUSBDriver, Stop)
{
    LOG("Stop");

    if (ivars->interface) {
        ivars->interface->Close(this, 0);
        OSSafeReleaseNULL(ivars->interface);
    }
    if (ivars->device) {
        if (ivars->deviceOpen) {
            ivars->device->Close(this, 0);
            ivars->deviceOpen = false;
        }
        OSSafeReleaseNULL(ivars->device);
    }
    return Stop(provider, SUPERDISPATCH);
}

// MARK: - User-client plumbing

IOUSBHostInterface *
BrlttyUSBDriver::CopyInterface()
{
    IOUSBHostInterface *iface = ivars ? ivars->interface : nullptr;
    if (iface) iface->retain();
    return iface;
}

kern_return_t
IMPL(BrlttyUSBDriver, NewUserClient)
{
    LOG("NewUserClient: type=%u — kernel let the open through", type);
    (void)type;

    IOService *created = nullptr;
    // "UserClientProperties" is the dict in our Info.plist that tells
    // DriverKit which IOUserClass to instantiate and how to bootstrap
    // it. Keeping it in the plist (rather than coded here) means we
    // can ship a single-driver dext that exposes several distinct
    // client classes later without recompiling.
    kern_return_t ret = Create(this, "UserClientProperties", &created);
    if (ret != kIOReturnSuccess) {
        LOG("Create(UserClientProperties) failed: 0x%x", ret);
        return ret;
    }

    *userClient = OSDynamicCast(IOUserClient, created);
    if (!*userClient) {
        LOG("Create() returned non-IOUserClient");
        OSSafeReleaseNULL(created);
        return kIOReturnError;
    }

    return kIOReturnSuccess;
}
