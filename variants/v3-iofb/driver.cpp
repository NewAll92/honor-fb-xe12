//
//  HonorFB v3 — direct IOFramebuffer subclass.
//
//  v0/v1/v2 publish a "display" nub and rely on Apple's IONDRVFramebuffer binding to it.
//  On modern macOS (Metal-era WindowServer) that binding does not yield a usable display
//  (boot reaches the WindowServer hand-off and stays on the verbose console). This variant
//  removes that dependency: it IS the IOFramebuffer that CoreDisplay/WindowServer talk to.
//  It scans out the boot GOP framebuffer recorded in PE_state.video (proven writable — the
//  vpaint probe painted the internal panel), exposing exactly one mode = the boot
//  resolution, 32-bit direct pixels, no acceleration.
//
//  Matching (Info.plist): IOProviderClass = IOResources so start() always runs; it needs
//  com.apple.iokit.IOGraphicsFamily in OSBundleLibraries (IOFramebuffer lives there).
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
};

OSDefineMetaClassAndStructors(HonorFB_v3, IOFramebuffer)

bool HonorFB_v3::start(IOService *provider)
{
    if (!PE_state.initialized) {
        IOLog("HonorFB_v3: PE_state not initialized\n");
        return false;
    }
    fbPhys     = static_cast<IOPhysicalAddress64>(PE_state.video.v_baseAddr & ~3ULL);
    fbW        = static_cast<uint32_t>(PE_state.video.v_width);
    fbH        = static_cast<uint32_t>(PE_state.video.v_height);
    fbRowBytes = static_cast<uint32_t>(PE_state.video.v_rowBytes);
    fbLen      = static_cast<IOByteCount>(static_cast<uint64_t>(fbH) * fbRowBytes);

    IOLog("HonorFB_v3: fb=0x%llx %ux%u rowBytes=%u len=0x%llx\n",
          static_cast<unsigned long long>(fbPhys), fbW, fbH, fbRowBytes,
          static_cast<unsigned long long>(fbLen));

    if (fbPhys == 0 || fbLen == 0 || fbW == 0 || fbH == 0) {
        IOLog("HonorFB_v3: invalid boot framebuffer\n");
        return false;
    }

    if (!IOFramebuffer::start(provider)) {
        IOLog("HonorFB_v3: IOFramebuffer::start failed\n");
        return false;
    }
    IOLog("HonorFB_v3: IOFramebuffer::start OK\n");
    return true;
}

IOReturn HonorFB_v3::enableController()
{
    return kIOReturnSuccess;
}

IODeviceMemory * HonorFB_v3::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture)
        return nullptr;
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v3::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v3::getDisplayModeCount()
{
    return 1;
}

IOReturn HonorFB_v3::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes)
        return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v3::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                  IODisplayModeInformation *info)
{
    if (displayMode != kHonorModeID || !info)
        return kIOReturnBadArgument;
    bzero(info, sizeof(*info));
    info->nominalWidth  = fbW;
    info->nominalHeight = fbH;
    info->refreshRate   = 60 << 16;   // 60 Hz, 16.16 fixed
    info->maxDepthIndex = 0;
    return kIOReturnSuccess;
}

UInt64 HonorFB_v3::getPixelFormatsForDisplayMode(IODisplayModeID /*displayMode*/,
                                                 IOIndex /*depth*/)
{
    return 0;   // deprecated, must return 0
}

IOReturn HonorFB_v3::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                         IOPixelAperture aperture,
                                         IOPixelInformation *pixelInfo)
{
    if (displayMode != kHonorModeID || depth != 0 ||
        aperture != kIOFBSystemAperture || !pixelInfo)
        return kIOReturnBadArgument;

    bzero(pixelInfo, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow       = fbRowBytes;
    pixelInfo->bytesPerPlane     = 0;
    pixelInfo->activeWidth       = fbW;
    pixelInfo->activeHeight      = fbH;
    pixelInfo->bitsPerPixel      = 32;
    pixelInfo->pixelType         = kIORGBDirectPixels;
    pixelInfo->componentCount    = 3;
    pixelInfo->bitsPerComponent  = 8;
    pixelInfo->componentMasks[0] = 0x00FF0000;   // R
    pixelInfo->componentMasks[1] = 0x0000FF00;   // G
    pixelInfo->componentMasks[2] = 0x000000FF;   // B
    strncpy(pixelInfo->pixelFormat, IO32BitDirectPixels, sizeof(pixelInfo->pixelFormat));
    return kIOReturnSuccess;
}

IOReturn HonorFB_v3::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = kHonorModeID;
    if (depth)       *depth       = 0;
    return kIOReturnSuccess;
}
