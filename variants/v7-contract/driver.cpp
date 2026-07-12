//
//  HonorFB v7 — clean IOFramebuffer with CORRECT attribute delegation.
//
//  Supersedes v6. Research finding (VMsvga2 / IOGraphics source): the WindowServer/kernel
//  freeze right after "GTrace synchronization point 1" is NOT a missing VBL — the
//  IOFramebuffer base class already runs its own software VBL (vblUpdateTimer). v6's
//  synthetic VBL was redundant AND risked blocking the single IOGraphics work loop.
//
//  The real bug shared by v3/v6: getAttributeForConnection returned kIOReturnUnsupported
//  for every selector it didn't explicitly handle. WindowServer queries connection
//  attributes (color modes, color depths, display flags, ...) that the BASE CLASS owns;
//  answering "unsupported" wedges WS during connection setup. The working VMsvga2 driver
//  delegates every unhandled selector to super. v7 does the same, and drops the VBL
//  entirely so nothing perturbs the IOGraphics work loop.
//
//  RUN ON STOCK macOS. No OCLP accelerator stack.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

#define kHonorModeID 1

class HonorFB_v7 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v7)
public:
    IOPhysicalAddress64 fbPhys;
    IOByteCount         fbLen;
    uint32_t            fbW, fbH, fbRowBytes;
    IODisplayModeID     currentMode;
    IOIndex             currentDepth;
    uint8_t             edid[128];

    virtual bool      start(IOService *provider) override;
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

OSDefineMetaClassAndStructors(HonorFB_v7, IOFramebuffer)

bool HonorFB_v7::start(IOService *provider)
{
    if (!PE_state.initialized) { IOLog("HonorFB_v7: PE_state not init\n"); return false; }
    fbPhys     = static_cast<IOPhysicalAddress64>(PE_state.video.v_baseAddr & ~3ULL);
    fbW        = static_cast<uint32_t>(PE_state.video.v_width);
    fbH        = static_cast<uint32_t>(PE_state.video.v_height);
    fbRowBytes = static_cast<uint32_t>(PE_state.video.v_rowBytes);   // REAL GOP stride (padded)
    fbLen      = static_cast<IOByteCount>(static_cast<uint64_t>(fbH) * fbRowBytes);
    currentMode  = kHonorModeID;
    currentDepth = 0;

    // Synthetic EDID 1.3 for a fixed 1920x1200 60Hz internal panel (detailed timing #1).
    static const uint8_t base[128] = {
        0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00, 0x06,0x10, 0x00,0xA0, 0x01,0x01,0x01,0x01,
        0x00, 0x1F, 0x01,0x03, 0x80, 0x22,0x16, 0x78, 0x0A,
        0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,0x0F,0x50,0x54,
        0x00,0x00,0x00,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x28,0x3C,0x80,0xA0,0x70,0xB0,0x23,0x40,0x30,0x20,0x36,0x00,0x58,0xD7,0x10,0x00,0x00,0x1E,
        0x00,0x00,0x00,0xFD,0x00,0x32,0x4B,0x1E,0x5A,0x11,0x00,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,
        0x00,0x00,0x00,0xFC,0x00,0x48,0x6F,0x6E,0x6F,0x72,0x20,0x46,0x42,0x0A,0x20,0x20,0x20,0x20,
        0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,
        0x00
    };
    for (int i = 0; i < 128; i++) edid[i] = base[i];
    uint8_t sum = 0;
    for (int i = 0; i < 127; i++) sum += edid[i];
    edid[127] = static_cast<uint8_t>(0x100 - sum);

    IOLog("HonorFB_v7: fb=0x%llx %ux%u rowBytes=%u len=%llu\n",
          static_cast<unsigned long long>(fbPhys), fbW, fbH, fbRowBytes,
          static_cast<unsigned long long>(fbLen));
    if (fbPhys == 0 || fbLen == 0 || fbW == 0 || fbH == 0) {
        IOLog("HonorFB_v7: invalid boot framebuffer\n"); return false;
    }
    if (!IOFramebuffer::start(provider)) {
        IOLog("HonorFB_v7: IOFramebuffer::start failed\n"); return false;
    }
    IOLog("HonorFB_v7: start OK — display + EDID published (attr delegation fixed, no VBL)\n");
    return true;
}

IOReturn HonorFB_v7::enableController() { return kIOReturnSuccess; }

IODeviceMemory * HonorFB_v7::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    return IODeviceMemory::withRange(fbPhys, fbLen);   // exact GOP scanout, real stride*height
}

const char * HonorFB_v7::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v7::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v7::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v7::getInformationForDisplayMode(IODisplayModeID displayMode,
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

UInt64 HonorFB_v7::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v7::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                         IOPixelAperture aperture,
                                         IOPixelInformation *pixelInfo)
{
    if (displayMode != kHonorModeID || depth != 0 ||
        aperture != kIOFBSystemAperture || !pixelInfo) return kIOReturnBadArgument;
    bzero(pixelInfo, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow       = fbRowBytes;          // REAL stride — matches aperture length
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

IOReturn HonorFB_v7::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v7::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode = displayMode; currentDepth = depth;   // record only — never busy-wait on HW
    return kIOReturnSuccess;
}

// THE FIX: answer the selectors we own, DELEGATE everything else to the base class.
// Returning kIOReturnUnsupported here (v3/v6 bug) for base-owned selectors
// (kConnectionColorModesSupported, kConnectionColorDepthsSupported, kConnectionDisplayFlags, ...)
// wedges WindowServer during connection setup.
IOReturn HonorFB_v7::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v7::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v7::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag) : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v7::hasDDCConnect(IOIndex) { return true; }

IOReturn HonorFB_v7::getDDCBlock(IOIndex, UInt32 blockNumber, IOSelect,
                                 IOOptionBits, UInt8 *data, IOByteCount *length)
{
    if (blockNumber != 1 || !data || !length || *length < 128) return kIOReturnUnsupported;
    for (int i = 0; i < 128; i++) data[i] = edid[i];
    *length = 128;
    return kIOReturnSuccess;
}
