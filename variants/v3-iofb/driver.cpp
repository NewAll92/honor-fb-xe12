//
//  HonorFB v3 — direct IOFramebuffer subclass (with display-connection reporting).
//
//  v0/v1/v2 delegate to IONDRVFramebuffer and never yield a GUI. v3 IS the IOFramebuffer
//  CoreDisplay/WindowServer talk to, scanning out the boot GOP framebuffer (PE_state.video,
//  proven writable by the vpaint probe). Crucially it also reports a *connected, enabled
//  display* (getAttributeForConnection / connectFlags), which is what WindowServer needs to
//  actually create a display and draw on the framebuffer — VM framebuffers do exactly this.
//
//  Matching (Info.plist): IOProviderClass = IOResources; needs IOGraphicsFamily in libs.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

#define kHonorModeID 1

class HonorFB_v3 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v3)
public:
    IOPhysicalAddress64 fbPhys;
    IOByteCount         fbLen;
    uint32_t            fbW, fbH, fbRowBytes;
    IODisplayModeID     currentMode;
    IOIndex             currentDepth;

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

    // --- display connection reporting (the piece WindowServer needs) ---
    virtual IOReturn  getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t *value) override;
    virtual IOReturn  setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t value) override;
    virtual IOReturn  connectFlags(IOIndex connectIndex, IODisplayModeID displayMode,
                                   IOOptionBits *flags) override;
    virtual bool      hasDDCConnect(IOIndex connectIndex) override;
};

OSDefineMetaClassAndStructors(HonorFB_v3, IOFramebuffer)

bool HonorFB_v3::start(IOService *provider)
{
    if (!PE_state.initialized) { IOLog("HonorFB_v3: PE_state not init\n"); return false; }
    fbPhys     = static_cast<IOPhysicalAddress64>(PE_state.video.v_baseAddr & ~3ULL);
    fbW        = static_cast<uint32_t>(PE_state.video.v_width);
    fbH        = static_cast<uint32_t>(PE_state.video.v_height);
    fbRowBytes = static_cast<uint32_t>(PE_state.video.v_rowBytes);
    fbLen      = static_cast<IOByteCount>(static_cast<uint64_t>(fbH) * fbRowBytes);
    currentMode  = kHonorModeID;
    currentDepth = 0;

    IOLog("HonorFB_v3: fb=0x%llx %ux%u rowBytes=%u len=0x%llx\n",
          static_cast<unsigned long long>(fbPhys), fbW, fbH, fbRowBytes,
          static_cast<unsigned long long>(fbLen));

    if (fbPhys == 0 || fbLen == 0 || fbW == 0 || fbH == 0) {
        IOLog("HonorFB_v3: invalid boot framebuffer\n"); return false;
    }
    if (!IOFramebuffer::start(provider)) {
        IOLog("HonorFB_v3: IOFramebuffer::start failed\n"); return false;
    }
    IOLog("HonorFB_v3: IOFramebuffer::start OK — display connected\n");
    return true;
}

IOReturn HonorFB_v3::enableController() { return kIOReturnSuccess; }

IODeviceMemory * HonorFB_v3::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v3::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v3::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v3::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v3::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                  IODisplayModeInformation *info)
{
    if (displayMode != kHonorModeID || !info) return kIOReturnBadArgument;
    bzero(info, sizeof(*info));
    info->nominalWidth  = fbW;
    info->nominalHeight = fbH;
    info->refreshRate   = 60 << 16;
    info->maxDepthIndex = 0;
    info->flags         = kDisplayModeValidFlag | kDisplayModeSafeFlag |
                          kDisplayModeDefaultFlag;
    return kIOReturnSuccess;
}

UInt64 HonorFB_v3::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v3::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
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

IOReturn HonorFB_v3::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v3::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode  = displayMode;
    currentDepth = depth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v3::getAttributeForConnection(IOIndex, IOSelect attribute, uintptr_t *value)
{
    switch (attribute) {
        case kConnectionEnable:                     // display is enabled
        case kConnectionCheckEnable:                // ...and present
            if (value) *value = 1;
            return kIOReturnSuccess;
        case kConnectionFlags:
            if (value) *value = 0;
            return kIOReturnSuccess;
        case kConnectionSupportsAppleSense:
        case kConnectionSupportsLLDDCSense:
        case kConnectionSupportsHLDDCSense:
            return kIOReturnUnsupported;
        default:
            if (value) *value = 0;
            return kIOReturnUnsupported;
    }
}

IOReturn HonorFB_v3::setAttributeForConnection(IOIndex, IOSelect attribute, uintptr_t)
{
    switch (attribute) {
        case kConnectionPower:
        case kConnectionProbe:
        case kConnectionChanged:
            return kIOReturnSuccess;
        default:
            return kIOReturnSuccess;   // accept everything, we are a fixed panel
    }
}

IOReturn HonorFB_v3::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag)
             : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v3::hasDDCConnect(IOIndex) { return false; }   // fixed panel, no DDC/EDID
