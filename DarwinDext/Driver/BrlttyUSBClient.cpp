/*
 * brltty USB DriverKit IOUserClient implementation.
 *
 * Dispatches ExternalMethod calls from userland (brltty's
 * Programs/usb_darwin_dext.c) onto the IOUSBHostInterface owned by the
 * parent BrlttyUSBDriver. Selectors are documented in
 * BrlttyUSBShared.h.
 */

#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/OSData.h>
#include <USBDriverKit/IOUSBHostInterface.h>
#include <USBDriverKit/IOUSBHostDevice.h>
#include <USBDriverKit/IOUSBHostPipe.h>
#include <USBDriverKit/AppleUSBDescriptorParsing.h>

#include "BrlttyUSBClient.h"
#include "BrlttyUSBDriver.h"
#include "BrlttyUSBShared.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "brltty-dext-client: " fmt, ##__VA_ARGS__)

struct BrlttyUSBClient_IVars {
    /* Weak — owned by the driver, lifetime is bounded by Stop(). */
    BrlttyUSBDriver *driver;
    /* Retained for the lifetime of the user-client session. We keep
     * our own reference so an in-flight ExternalMethod can't race a
     * driver-side teardown that nulls out the driver's pointer. */
    IOUSBHostInterface *interface;
    /* Set to true once Stop() has run. Selectors check this and
     * bail with kIOReturnNotReady — the underlying interface may
     * already be torn down on the dispatch queue. */
    bool                stopped;
};

// MARK: - Lifecycle

bool
BrlttyUSBClient::init()
{
    if (!super::init()) return false;
    ivars = IONewZero(BrlttyUSBClient_IVars, 1);
    return ivars != nullptr;
}

void
BrlttyUSBClient::free()
{
    if (ivars) IOSafeDeleteNULL(ivars, BrlttyUSBClient_IVars, 1);
    super::free();
}

kern_return_t
IMPL(BrlttyUSBClient, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        LOG("super Start failed: 0x%x", ret);
        return ret;
    }

    ivars->driver = OSDynamicCast(BrlttyUSBDriver, provider);
    if (!ivars->driver) {
        LOG("provider is not BrlttyUSBDriver");
        return kIOReturnInvalid;
    }

    // The driver hands us a retained reference to its IOUSBHostInterface
    // — we hold it for the lifetime of the user client session so that
    // a driver-side teardown can't yank it from under an in-flight
    // ExternalMethod call.
    ivars->interface = ivars->driver->CopyInterface();
    if (!ivars->interface) {
        LOG("driver has no interface attached");
        return kIOReturnNotReady;
    }

    LOG("user client started");
    return kIOReturnSuccess;
}

kern_return_t
IMPL(BrlttyUSBClient, Stop)
{
    LOG("user client stopping");
    // Set the flag before releasing the interface so any selector
    // dispatched concurrently sees the teardown and bails with a
    // clean error instead of touching a dangling pointer.
    ivars->stopped = true;
    OSSafeReleaseNULL(ivars->interface);
    ivars->driver = nullptr;
    return Stop(provider, SUPERDISPATCH);
}

// Convenience used by every selector — centralises the "is the user
// client still usable?" check so each dispatcher doesn't have to
// repeat the three-line guard.
static inline kern_return_t
GetReadyClient(OSObject *target, BrlttyUSBClient **out_self)
{
    auto *self = OSDynamicCast(BrlttyUSBClient, target);
    if (!self || !self->ivars || self->ivars->stopped || !self->ivars->interface) {
        return kIOReturnNotReady;
    }
    *out_self = self;
    return kIOReturnSuccess;
}

// MARK: - External method dispatch

// Forward decls of static dispatchers — DriverKit's IOUserClientMethodDispatch
// stores a free-function pointer, so member functions are wrapped here.
static kern_return_t SDispatchGetVersion     (OSObject *target, void *reference, IOUserClientMethodArguments *args);
static kern_return_t SDispatchGetPipeCount   (OSObject *target, void *reference, IOUserClientMethodArguments *args);
static kern_return_t SDispatchGetPipeInfo    (OSObject *target, void *reference, IOUserClientMethodArguments *args);
static kern_return_t SDispatchBulkRead       (OSObject *target, void *reference, IOUserClientMethodArguments *args);
static kern_return_t SDispatchBulkWrite      (OSObject *target, void *reference, IOUserClientMethodArguments *args);
static kern_return_t SDispatchControlTransfer(OSObject *target, void *reference, IOUserClientMethodArguments *args);

static const IOUserClientMethodDispatch sMethods[kBrlttyUSBSelectorCount] = {
    [kBrlttyUSBSelectorGetVersion] = {
        .function                 = &SDispatchGetVersion,
        .checkCompletionExists    = false,
        .checkScalarInputCount    = 0,
        .checkStructureInputSize  = 0,
        .checkScalarOutputCount   = 1,
        .checkStructureOutputSize = 0,
    },
    [kBrlttyUSBSelectorGetPipeCount] = {
        .function                 = &SDispatchGetPipeCount,
        .checkCompletionExists    = false,
        .checkScalarInputCount    = 0,
        .checkStructureInputSize  = 0,
        .checkScalarOutputCount   = 1,
        .checkStructureOutputSize = 0,
    },
    [kBrlttyUSBSelectorGetPipeInfo] = {
        .function                 = &SDispatchGetPipeInfo,
        .checkCompletionExists    = false,
        .checkScalarInputCount    = 1,
        .checkStructureInputSize  = 0,
        .checkScalarOutputCount   = 1,
        .checkStructureOutputSize = 0,
    },
    [kBrlttyUSBSelectorBulkRead] = {
        .function                 = &SDispatchBulkRead,
        .checkCompletionExists    = false,
        .checkScalarInputCount    = 2,
        .checkStructureInputSize  = 0,
        .checkScalarOutputCount   = 1,
        // Variable-sized output — set to 0xFFFFFFFF to disable strict checking.
        .checkStructureOutputSize = 0xFFFFFFFF,
    },
    [kBrlttyUSBSelectorBulkWrite] = {
        .function                 = &SDispatchBulkWrite,
        .checkCompletionExists    = false,
        .checkScalarInputCount    = 2,
        .checkStructureInputSize  = 0xFFFFFFFF,
        .checkScalarOutputCount   = 1,
        .checkStructureOutputSize = 0,
    },
    [kBrlttyUSBSelectorControlTransfer] = {
        .function                 = &SDispatchControlTransfer,
        .checkCompletionExists    = false,
        .checkScalarInputCount    = 2,
        .checkStructureInputSize  = 0xFFFFFFFF,
        .checkScalarOutputCount   = 1,
        .checkStructureOutputSize = 0xFFFFFFFF,
    },
};

kern_return_t
BrlttyUSBClient::ExternalMethod(uint64_t selector,
                                IOUserClientMethodArguments *arguments,
                                const IOUserClientMethodDispatch *dispatch,
                                OSObject *target,
                                void *reference)
{
    if (selector >= kBrlttyUSBSelectorCount) {
        return kIOReturnBadArgument;
    }
    // ExternalMethod is LOCALONLY in IOUserClient.iig — iig generates no
    // IPC dispatch for it, so this is a plain C++ override (no IMPL()).
    dispatch = &sMethods[selector];
    target   = this;
    return super::ExternalMethod(selector, arguments, dispatch, target, reference);
}

// MARK: - Selector implementations
//
// Each selector pulls the IOUSBHostInterface out of the client's ivars
// and performs a synchronous transfer. Async I/O via IODataQueueDispatchSource
// is the obvious follow-up — see notes in BrlttyUSBShared.h.

static kern_return_t
SDispatchGetVersion(OSObject *target, void *reference, IOUserClientMethodArguments *args)
{
    (void)target; (void)reference;
    args->scalarOutput[0] = BRLTTY_USB_DEXT_PROTOCOL_VERSION;
    return kIOReturnSuccess;
}

static kern_return_t
SDispatchGetPipeCount(OSObject *target, void *reference, IOUserClientMethodArguments *args)
{
    (void)reference;
    BrlttyUSBClient *self = nullptr;
    kern_return_t guard = GetReadyClient(target, &self);
    if (guard != kIOReturnSuccess) return guard;

    const IOUSBConfigurationDescriptor *config = self->ivars->interface->CopyConfigurationDescriptor();
    if (!config) return kIOReturnNoDevice;

    const IOUSBInterfaceDescriptor *iface = self->ivars->interface->GetInterfaceDescriptor(config);
    if (!iface) return kIOReturnNoDevice;

    args->scalarOutput[0] = iface->bNumEndpoints;
    return kIOReturnSuccess;
}

static kern_return_t
SDispatchGetPipeInfo(OSObject *target, void *reference, IOUserClientMethodArguments *args)
{
    (void)reference;
    BrlttyUSBClient *self = nullptr;
    kern_return_t guard = GetReadyClient(target, &self);
    if (guard != kIOReturnSuccess) return guard;

    const uint64_t index = args->scalarInput[0];

    const IOUSBConfigurationDescriptor *config = self->ivars->interface->CopyConfigurationDescriptor();
    if (!config) return kIOReturnNoDevice;

    const IOUSBInterfaceDescriptor *iface = self->ivars->interface->GetInterfaceDescriptor(config);
    if (!iface || index >= iface->bNumEndpoints) return kIOReturnBadArgument;

    // Walk endpoint descriptors that belong to the matched interface.
    const IOUSBEndpointDescriptor *ep = nullptr;
    const IOUSBDescriptorHeader *current = nullptr;
    for (uint64_t found = 0; found <= index; found++) {
        ep = IOUSBGetNextEndpointDescriptor(config, iface, current);
        if (!ep) return kIOReturnNotFound;
        current = (const IOUSBDescriptorHeader *)ep;
    }

    const uint8_t  address = IOUSBGetEndpointAddress(ep);
    const uint8_t  dir     = (IOUSBGetEndpointDirection(ep) == kIOUSBEndpointDescriptorDirectionIn)
                               ? kBrlttyUSBDirectionIn : kBrlttyUSBDirectionOut;
    const uint8_t  type    = IOUSBGetEndpointType(ep);
    // Use the raw wMaxPacketSize bits rather than IOUSBGetEndpointMaxPacketSize,
    // which wants the device speed and would force us to thread that through.
    // brltty only cares about the basic packet size for buffer sizing.
    const uint16_t mps     = ep->wMaxPacketSize & 0x07ff;

    args->scalarOutput[0] = BrlttyUSBPackPipeInfo(address, type, dir, mps);
    return kIOReturnSuccess;
}

static kern_return_t
SDispatchBulkRead(OSObject *target, void *reference, IOUserClientMethodArguments *args)
{
    (void)reference;
    BrlttyUSBClient *self = nullptr;
    kern_return_t guard = GetReadyClient(target, &self);
    if (guard != kIOReturnSuccess) return guard;

    const uint8_t  address   = (uint8_t)(args->scalarInput[0] & 0xff);
    const uint32_t timeoutMs = (uint32_t)(args->scalarInput[1] & 0xffffffff);

    IOMemoryDescriptor *output = args->structureOutputDescriptor;
    if (!output) return kIOReturnBadArgument;

    IOUSBHostPipe *pipe = nullptr;
    kern_return_t ret = self->ivars->interface->CopyPipe(address, &pipe);
    if (ret != kIOReturnSuccess) return ret;

    uint64_t length = 0;
    output->GetLength(&length);

    uint32_t bytesTransferred = 0;
    ret = pipe->IO(output, (uint32_t)length, &bytesTransferred, timeoutMs);
    args->scalarOutput[0] = bytesTransferred;

    OSSafeReleaseNULL(pipe);
    return ret;
}

static kern_return_t
SDispatchBulkWrite(OSObject *target, void *reference, IOUserClientMethodArguments *args)
{
    (void)reference;
    BrlttyUSBClient *self = nullptr;
    kern_return_t guard = GetReadyClient(target, &self);
    if (guard != kIOReturnSuccess) return guard;

    const uint8_t  address   = (uint8_t)(args->scalarInput[0] & 0xff);
    const uint32_t timeoutMs = (uint32_t)(args->scalarInput[1] & 0xffffffff);

    IOMemoryDescriptor *input = args->structureInputDescriptor;
    if (!input) return kIOReturnBadArgument;

    IOUSBHostPipe *pipe = nullptr;
    kern_return_t ret = self->ivars->interface->CopyPipe(address, &pipe);
    if (ret != kIOReturnSuccess) return ret;

    uint64_t length = 0;
    input->GetLength(&length);

    uint32_t bytesTransferred = 0;
    ret = pipe->IO(input, (uint32_t)length, &bytesTransferred, timeoutMs);
    args->scalarOutput[0] = bytesTransferred;

    OSSafeReleaseNULL(pipe);
    return ret;
}

static kern_return_t
SDispatchControlTransfer(OSObject *target, void *reference, IOUserClientMethodArguments *args)
{
    (void)reference;
    BrlttyUSBClient *self = nullptr;
    kern_return_t guard = GetReadyClient(target, &self);
    if (guard != kIOReturnSuccess) return guard;

    const uint64_t packed    = args->scalarInput[0];
    const uint32_t timeoutMs = (uint32_t)(args->scalarInput[1] & 0xffffffff);

    const uint8_t  bmRequestType = (uint8_t)(packed & 0xff);
    const uint8_t  bRequest      = (uint8_t)((packed >> 8) & 0xff);
    const uint16_t wValue        = (uint16_t)((packed >> 16) & 0xffff);
    const uint16_t wIndex        = (uint16_t)((packed >> 32) & 0xffff);

    // wLength is implied by whichever descriptor (input or output) is
    // non-null. Userland always passes exactly one direction; the dext
    // doesn't try to second-guess bmRequestType.
    IOMemoryDescriptor *buffer = args->structureInputDescriptor
                                   ? args->structureInputDescriptor
                                   : args->structureOutputDescriptor;
    uint64_t length = 0;
    if (buffer) buffer->GetLength(&length);
    const uint16_t wLength = (uint16_t)length;

    IOUSBHostDevice *dev = nullptr;
    kern_return_t ret = self->ivars->interface->CopyDevice(&dev);
    if (ret != kIOReturnSuccess) return ret;

    uint16_t bytesTransferred = 0;
    ret = dev->DeviceRequest(self, bmRequestType, bRequest, wValue, wIndex, wLength,
                             buffer, &bytesTransferred, timeoutMs);
    args->scalarOutput[0] = bytesTransferred;

    OSSafeReleaseNULL(dev);
    return ret;
}
