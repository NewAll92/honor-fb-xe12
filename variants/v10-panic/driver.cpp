//
//  HonorFB v10 — DIAGNOSTIC ONLY. Identical to v9 but panics on the first line of start().
//
//  Purpose: definitively answer "does HonorFB match the masked iGPU IOPCIDevice and run
//  start()?" Its normal IOLog lines scroll off before the GTrace freeze, so we can't tell.
//  A panic() cannot scroll off, flushes immediately, and (with debug=0x100) stays on screen.
//   * If the machine PANICS with "provider=IOPCIDevice" -> the device-id match WORKS, start()
//     ran, and the remaining problem is purely console ADOPTION (after start). Revert to v9
//     and work the adoption path.
//   * If it still FREEZES at "GTrace synchronization point 0" with NO panic -> start() never
//     ran -> the match FAILED (the real iGPU device-id isn't in our IOPCIPrimaryMatch list) ->
//     get the exact device-id.
//  start() runs before open()/kPEDisableScreen, so the console is still enabled -> panic shows.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include <kern/debug.h>            // panic()

#define kHonorModeID 1

class HonorFB_v10 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v10)
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

OSDefineMetaClassAndStructors(HonorFB_v10, IOFramebuffer)

bool HonorFB_v10::start(IOService *provider)
{
    // DIAGNOSTIC: if we got here, HonorFB matched the iGPU IOPCIDevice. Prove it unmissably.
    panic("HonorFB_v10: MATCHED iGPU! provider=%s -- device-id match WORKS. (diagnostic panic)",
          provider ? provider->getMetaClass()->getClassName() : "NULL");
    return false;   // unreachable
}

bool HonorFB_v10::isConsoleDevice(void) { return true; }
IOReturn HonorFB_v10::enableController() { return kIOReturnSuccess; }

IODeviceMemory * HonorFB_v10::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v10::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v10::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v10::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v10::getInformationForDisplayMode(IODisplayModeID displayMode,
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

UInt64 HonorFB_v10::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v10::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
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

IOReturn HonorFB_v10::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v10::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode = displayMode; currentDepth = depth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v10::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v10::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v10::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag) : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v10::hasDDCConnect(IOIndex) { return true; }

IOReturn HonorFB_v10::getDDCBlock(IOIndex, UInt32 blockNumber, IOSelect,
                                  IOOptionBits, UInt8 *data, IOByteCount *length)
{
    if (blockNumber != 1 || !data || !length || *length < 128) return kIOReturnUnsupported;
    for (int i = 0; i < 128; i++) data[i] = edid[i];
    *length = 128;
    return kIOReturnSuccess;
}
