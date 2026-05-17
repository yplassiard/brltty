/*
 * Standalone test tool for the brltty DriverKit dext.
 *
 * Opens an IOUserClient against the running BrlttyUSBDriver dext and walks
 * the full external-method surface — version handshake, pipe enumeration,
 * one bulk read attempt, one bulk write attempt. Prints what comes back so
 * we can verify the IPC end-to-end before wiring it into brltty proper.
 *
 * Build:
 *     clang -framework IOKit -framework CoreFoundation -O2 \
 *           -I../Driver dext_test.c -o dext_test
 *
 * Run:
 *     ./dext_test                  # talk to the running dext
 *     ./dext_test --write <hex>    # send hex bytes to the first OUT pipe
 *     ./dext_test --read <ms>      # read from the first IN pipe with timeout
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include "BrlttyUSBShared.h"

static const char *
PipeTypeName(uint8_t type)
{
    switch (type) {
    case kBrlttyUSBPipeTypeControl:     return "control";
    case kBrlttyUSBPipeTypeIsochronous: return "iso";
    case kBrlttyUSBPipeTypeBulk:        return "bulk";
    case kBrlttyUSBPipeTypeInterrupt:   return "interrupt";
    default:                            return "?";
    }
}

static io_connect_t
OpenDext(void)
{
    // Match our IOUserService by class name (BrlttyUSBClient is the
    // IOUserClass exposed via UserClientProperties in the dext's
    // Info.plist; from userland we look for the parent driver class).
    CFMutableDictionaryRef matching = IOServiceMatching("IOUserService");
    if (!matching) {
        fprintf(stderr, "IOServiceMatching failed\n");
        return IO_OBJECT_NULL;
    }

    // Narrow the match: only the BrlttyUSBDriver instance, not other
    // generic IOUserService objects published by other dexts on the
    // system.
    CFDictionarySetValue(matching, CFSTR("IOUserServerName"),
                         CFSTR(BRLTTY_USB_DEXT_BUNDLE_ID));

    io_iterator_t iter = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceGetMatchingServices failed: 0x%x\n", kr);
        return IO_OBJECT_NULL;
    }

    io_service_t service = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "no BrlttyUSBDriver service found — is the dext running?\n");
        return IO_OBJECT_NULL;
    }

    io_connect_t connection = IO_OBJECT_NULL;
    kr = IOServiceOpen(service, mach_task_self(), 0, &connection);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceOpen failed: 0x%x\n", kr);
        return IO_OBJECT_NULL;
    }
    return connection;
}

static kern_return_t
CallGetVersion(io_connect_t conn, uint64_t *out_version)
{
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr = IOConnectCallScalarMethod(conn, kBrlttyUSBSelectorGetVersion,
                                                  NULL, 0, output, &outputCount);
    if (kr == KERN_SUCCESS) *out_version = output[0];
    return kr;
}

static kern_return_t
CallGetPipeCount(io_connect_t conn, uint64_t *out_count)
{
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr = IOConnectCallScalarMethod(conn, kBrlttyUSBSelectorGetPipeCount,
                                                  NULL, 0, output, &outputCount);
    if (kr == KERN_SUCCESS) *out_count = output[0];
    return kr;
}

static kern_return_t
CallGetPipeInfo(io_connect_t conn, uint64_t index, uint64_t *out_packed)
{
    uint64_t input[1] = { index };
    uint64_t output[1] = {0};
    uint32_t outputCount = 1;
    kern_return_t kr = IOConnectCallScalarMethod(conn, kBrlttyUSBSelectorGetPipeInfo,
                                                  input, 1, output, &outputCount);
    if (kr == KERN_SUCCESS) *out_packed = output[0];
    return kr;
}

static kern_return_t
CallBulkRead(io_connect_t conn, uint8_t address, uint32_t timeoutMs,
             void *buf, size_t bufLen, size_t *out_bytes)
{
    uint64_t input[2]   = { address, timeoutMs };
    uint64_t output[1]  = {0};
    uint32_t outputCount = 1;
    size_t   outputBufLen = bufLen;
    kern_return_t kr = IOConnectCallMethod(conn, kBrlttyUSBSelectorBulkRead,
                                            input, 2,
                                            NULL, 0,
                                            output, &outputCount,
                                            buf, &outputBufLen);
    if (kr == KERN_SUCCESS) *out_bytes = output[0];
    return kr;
}

static kern_return_t
CallBulkWrite(io_connect_t conn, uint8_t address, uint32_t timeoutMs,
              const void *buf, size_t bufLen, size_t *out_bytes)
{
    uint64_t input[2]   = { address, timeoutMs };
    uint64_t output[1]  = {0};
    uint32_t outputCount = 1;
    kern_return_t kr = IOConnectCallMethod(conn, kBrlttyUSBSelectorBulkWrite,
                                            input, 2,
                                            buf, bufLen,
                                            output, &outputCount,
                                            NULL, NULL);
    if (kr == KERN_SUCCESS) *out_bytes = output[0];
    return kr;
}

static int
HexParseByte(const char *p, uint8_t *out)
{
    char hex[3] = { p[0], p[1], 0 };
    char *end = NULL;
    long v = strtol(hex, &end, 16);
    if (end != hex + 2 || v < 0 || v > 255) return -1;
    *out = (uint8_t)v;
    return 0;
}

static ssize_t
HexParseBytes(const char *str, uint8_t *out, size_t outLen)
{
    size_t inLen = strlen(str);
    if (inLen % 2 != 0) return -1;
    size_t bytes = inLen / 2;
    if (bytes > outLen) return -1;
    for (size_t i = 0; i < bytes; i++) {
        if (HexParseByte(str + i * 2, &out[i]) < 0) return -1;
    }
    return (ssize_t)bytes;
}

int
main(int argc, const char *argv[])
{
    io_connect_t conn = OpenDext();
    if (conn == IO_OBJECT_NULL) return 1;
    fprintf(stderr, "opened dext user client\n");

    // 1) Version handshake — proves IPC round-trip works.
    uint64_t version = 0;
    kern_return_t kr = CallGetVersion(conn, &version);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "GetVersion failed: 0x%x\n", kr);
        goto out;
    }
    printf("protocol version: %" PRIu64 " (expected %d)\n",
           version, BRLTTY_USB_DEXT_PROTOCOL_VERSION);

    // 2) Walk pipes — proves descriptor enumeration works dext-side.
    uint64_t pipeCount = 0;
    kr = CallGetPipeCount(conn, &pipeCount);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "GetPipeCount failed: 0x%x\n", kr);
        goto out;
    }
    printf("pipes on interface 0: %" PRIu64 "\n", pipeCount);

    uint8_t firstInAddress  = 0;
    uint8_t firstOutAddress = 0;
    for (uint64_t i = 0; i < pipeCount; i++) {
        uint64_t packed = 0;
        kr = CallGetPipeInfo(conn, i, &packed);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "  pipe[%" PRIu64 "]: error 0x%x\n", i, kr);
            continue;
        }
        uint8_t address = 0, type = 0, dir = 0;
        uint16_t mps = 0;
        BrlttyUSBUnpackPipeInfo(packed, &address, &type, &dir, &mps);
        printf("  pipe[%" PRIu64 "] addr=0x%02x type=%s dir=%s maxPacket=%u\n",
               i, address, PipeTypeName(type),
               (dir == kBrlttyUSBDirectionIn) ? "IN" : "OUT", mps);

        if (dir == kBrlttyUSBDirectionIn  && firstInAddress  == 0) firstInAddress  = address;
        if (dir == kBrlttyUSBDirectionOut && firstOutAddress == 0) firstOutAddress = address;
    }

    // 3) Optional --read / --write — actual data path.
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--read") == 0 && i + 1 < argc) {
            uint32_t timeoutMs = (uint32_t)strtoul(argv[++i], NULL, 10);
            if (firstInAddress == 0) {
                fprintf(stderr, "no IN endpoint discovered, can't --read\n");
                continue;
            }
            uint8_t buf[512];
            size_t bytes = 0;
            kr = CallBulkRead(conn, firstInAddress, timeoutMs, buf, sizeof buf, &bytes);
            if (kr != KERN_SUCCESS) {
                fprintf(stderr, "BulkRead 0x%02x failed: 0x%x\n", firstInAddress, kr);
                continue;
            }
            printf("read %zu bytes from 0x%02x:", bytes, firstInAddress);
            for (size_t b = 0; b < bytes; b++) printf(" %02x", buf[b]);
            putchar('\n');
        } else if (strcmp(argv[i], "--write") == 0 && i + 1 < argc) {
            const char *hex = argv[++i];
            uint8_t buf[512];
            ssize_t n = HexParseBytes(hex, buf, sizeof buf);
            if (n < 0) {
                fprintf(stderr, "bad hex string: %s\n", hex);
                continue;
            }
            if (firstOutAddress == 0) {
                fprintf(stderr, "no OUT endpoint discovered, can't --write\n");
                continue;
            }
            size_t bytes = 0;
            kr = CallBulkWrite(conn, firstOutAddress, /* timeoutMs */ 1000,
                               buf, (size_t)n, &bytes);
            if (kr != KERN_SUCCESS) {
                fprintf(stderr, "BulkWrite 0x%02x failed: 0x%x\n", firstOutAddress, kr);
                continue;
            }
            printf("wrote %zu / %zd bytes to 0x%02x\n", bytes, n, firstOutAddress);
        }
    }

out:
    IOServiceClose(conn);
    return (kr == KERN_SUCCESS) ? 0 : 2;
}
