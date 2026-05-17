/*
 * brltty USB user-space driver (DriverKit dext).
 *
 * This driver matches USB interfaces of every braille display known to brltty
 * with a higher IOProbeScore than AppleUserUSBHostHIDDevice, so we claim the
 * USB interface before the macOS HID stack does. The interface is then exposed
 * to brltty userland through an IOUserClient (added in a follow-up commit).
 */

#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOService.h>
#include <USBDriverKit/IOUSBHostInterface.h>
#include <USBDriverKit/IOUSBHostDevice.h>
#include <USBDriverKit/IOUSBHostPipe.h>

#include "BrlttyUSBDriver.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "brltty-dext: " fmt, ##__VA_ARGS__)

struct BrlttyUSBDriver_IVars {
    IOUSBHostInterface *interface;
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

    IOUSBHostInterface *iface = OSDynamicCast(IOUSBHostInterface, provider);
    if (!iface) {
        LOG("provider is not IOUSBHostInterface");
        return kIOReturnInvalid;
    }

    ivars->interface = iface;

    uint16_t vendor = 0, product = 0;

    IOUSBHostDevice *dev = nullptr;
    iface->CopyDevice(&dev);
    if (dev) {
        const IOUSBDeviceDescriptor *dd = dev->CopyDeviceDescriptor();
        if (dd) {
            vendor = dd->idVendor;
            product = dd->idProduct;
        }
    }

    LOG("Start: claimed USB interface vendor=0x%04x product=0x%04x", vendor, product);

    OSSafeReleaseNULL(dev);

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
    ivars->interface = nullptr;
    return Stop(provider, SUPERDISPATCH);
}
