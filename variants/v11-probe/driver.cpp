//
//  HonorFB v11 — DIAGNOSTIC. Matches the UNMASKED iGPU by display class and panics printing
//  the real vendor/device id, so we (a) confirm HonorFB can attach to the iGPU IOPCIDevice at
//  all, and (b) read the exact device-id (v10's masked device-id match never fired).
//
//  Pair with DeviceProperties that REMOVE class-code=FFFFFFFF (unmask) so the iGPU exposes its
//  real display class 0x030000 and IOPCIClassMatch can match it. If this panics with a
//  device/vendor id -> matching works and we have the id (build the real masked IOPCIPrimaryMatch
//  fix from it). If it still freezes at GTrace with no panic -> matching fails even unmasked
//  (kext load problem or the iGPU isn't a matchable display IOPCIDevice).
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include <kern/debug.h>

#define kHonorModeID 1

class HonorFB_v11 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v11)
public:
    IOPhysicalAddress64 fbPhys;
    IOByteCount         fbLen;
    uint32_t            fbW, fbH, fbRowBytes;
    IODisplayModeID     currentMode;
    IOIndex             currentDepth;
    uint8_t             edid[128];

    virtual bool      start(IOService *provider) override;
    virtual bool      isConsoleDevice(void) override;
    virtual IOReturn  enableController() override;
    virtual IODeviceMemory * getApertureRange(IOPixelAperture aperture) override;
    virtual const char * getPixelFormats() override;
    virtual IOItemCount getDisplayModeCount() override;
    virtual IOReturn  getDisplayModes(IODisplayModeID *allDisplayModes) override;
    virtual IOReturn  getInformationForDisplayMode(IODisplayModeID displayMode,
                                                   IODisplayModeInformation *info) override;
    virtual UInt64    getPixelFormatsForDisplayMode(IODisplayModeID displayMode,
                                                    IOIndex depth) override;
    virtual IOReturn  getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                          IOPixelAperture aperture,
                                          IOPixelInformation *pixelInfo) override;
    virtual IOReturn  getCurrentDisplayMode(IODisplayModeID *displayMode,
                                            IOIndex *depth) override;
    virtual IOReturn  setDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    virtual IOReturn  getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t *value) override;
    virtual IOReturn  setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t value) override;
    virtual IOReturn  connectFlags(IOIndex connectIndex, IODisplayModeID displayMode,
                                   IOOptionBits *flags) override;
    virtual bool      hasDDCConnect(IOIndex connectIndex) override;
    virtual IOReturn  getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType,
                                  IOOptionBits options, UInt8 *data, IOByteCount *length) override;
};

OSDefineMetaClassAndStructors(HonorFB_v11, IOFramebuffer)

static uint32_t readIdProp(IOService *p, const char *key)
{
    OSData *d = p ? OSDynamicCast(OSData, p->getProperty(key)) : nullptr;
    if (d && d->getLength() >= 2) {
        const uint16_t *v = (const uint16_t *)d->getBytesNoCopy();
        return *v;
    }
    return 0xFFFF;
}

bool HonorFB_v11::start(IOService *provider)
{
    // DIAGNOSTIC: matched the iGPU -> read + report its real ids, then halt unmissably.
    uint32_t vend = readIdProp(provider, "vendor-id");
    uint32_t dev  = readIdProp(provider, "device-id");
    panic("HonorFB_v11 MATCHED iGPU! provider=%s vendor-id=0x%04x device-id=0x%04x (diagnostic)",
          provider ? provider->getMetaClass()->getClassName() : "NULL",
          (unsigned)vend, (unsigned)dev);
    return false;   // unreachable
}

bool HonorFB_v11::isConsoleDevice(void) { return true; }
IOReturn HonorFB_v11::enableController() { return kIOReturnSuccess; }

IODeviceMemory * HonorFB_v11::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v11::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v11::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v11::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v11::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                   IODisplayModeInformation *info)
{
    if (displayMode != kHonorModeID || !info) return kIOReturnBadArgument;
    bzero(info, sizeof(*info));
    info->nominalWidth  = fbW;
    info->nominalHeight = fbH;
    info->refreshRate   = 60 << 16;
    info->maxDepthIndex = 0;
    info->flags         = kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag;
    return kIOReturnSuccess;
}

UInt64 HonorFB_v11::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v11::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                          IOPixelAperture aperture,
                                          IOPixelInformation *pixelInfo)
{
    if (displayMode != kHonorModeID || depth != 0 ||
        aperture != kIOFBSystemAperture || !pixelInfo) return kIOReturnBadArgument;
    bzero(pixelInfo, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow       = fbRowBytes;
    pixelInfo->activeWidth       = fbW;
    pixelInfo->activeHeight      = fbH;
    pixelInfo->bitsPerPixel      = 32;
    pixelInfo->pixelType         = kIORGBDirectPixels;
    pixelInfo->componentCount    = 3;
    pixelInfo->bitsPerComponent  = 8;
    pixelInfo->componentMasks[0] = 0x00FF0000;
    pixelInfo->componentMasks[1] = 0x0000FF00;
    pixelInfo->componentMasks[2] = 0x000000FF;
    strncpy(pixelInfo->pixelFormat, IO32BitDirectPixels, sizeof(pixelInfo->pixelFormat));
    return kIOReturnSuccess;
}

IOReturn HonorFB_v11::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v11::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode = displayMode; currentDepth = depth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v11::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t *value)
{
    switch (attribute) {
        case kConnectionEnable:
        case kConnectionCheckEnable:
            if (value) *value = 1;
            return kIOReturnSuccess;
        case kConnectionFlags:
            if (value) *value = 0;
            return kIOReturnSuccess;
        case kConnectionChanged:
            return kIOReturnSuccess;
        default:
            return IOFramebuffer::getAttributeForConnection(connectIndex, attribute, value);
    }
}

IOReturn HonorFB_v11::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t value)
{
    switch (attribute) {
        case kConnectionPower:
        case kConnectionProbe:
            return kIOReturnSuccess;
        default:
            return IOFramebuffer::setAttributeForConnection(connectIndex, attribute, value);
    }
}

IOReturn HonorFB_v11::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag) : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v11::hasDDCConnect(IOIndex) { return true; }

IOReturn HonorFB_v11::getDDCBlock(IOIndex, UInt32 blockNumber, IOSelect,
                                  IOOptionBits, UInt8 *data, IOByteCount *length)
{
    if (blockNumber != 1 || !data || !length || *length < 128) return kIOReturnUnsupported;
    for (int i = 0; i < 128; i++) data[i] = edid[i];
    *length = 128;
    return kIOReturnSuccess;
}
