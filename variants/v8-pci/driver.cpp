//
//  HonorFB v8 — PCI-attached console framebuffer (the real architecture fix).
//
//  Binary analysis of IOGraphicsFamily (v597) proved the v3/v6/v7 freeze at
//  "GTrace synchronization point 1" is a CONSOLE HAND-OFF failure, not a hang: once no
//  framebuffer is adopted as the console, IOFramebuffer::open() calls setPlatformConsole(
//  kPEDisableScreen) and the screen is only re-enabled (kPEEnableScreen) from findConsole()
//  — which SKIPS any framebuffer whose IOFBController->fDevice is NULL. fDevice is set only
//  by walking the provider chain UP to an IOPCIDevice. v3-v7 matched IOResources (no PCI
//  ancestor) → fDevice NULL → never adopted → screen disabled forever, kernel headless.
//
//  v8 fixes the architecture:
//   * matches the Intel iGPU IOPCIDevice (via Info.plist IOPCIClassMatch) → real IOPCIDevice
//     ancestor → IOFBController->fDevice is populated → findConsole() can adopt us.
//   * overrides isConsoleDevice() → true (base class hard-codes false; IOBootFramebuffer
//     overrides it the same way) so open() sets gIOFBConsoleFramebuffer = this.
//   * still scans out the boot GOP framebuffer (PE_state.video), advertising a mode whose
//     geometry EXACTLY equals the boot console (required by findConsole / initFB).
//   Requires: DeviceProperties AAPL,boot-display=1 on the iGPU, class-code spoof removed,
//   and boot-arg iog=0x400 (kIOGDbgNoWaitQuietController) so the driverless iGPU's
//   waitQuiet() can't timeout-panic. RUN ON STOCK macOS.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

#define kHonorModeID 1

class HonorFB_v8 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v8)
public:
    IOPhysicalAddress64 fbPhys;
    IOByteCount         fbLen;
    uint32_t            fbW, fbH, fbRowBytes;
    IODisplayModeID     currentMode;
    IOIndex             currentDepth;
    uint8_t             edid[128];

    virtual bool      start(IOService *provider) override;
    virtual bool      isConsoleDevice(void) override;          // THE adoption key
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

OSDefineMetaClassAndStructors(HonorFB_v8, IOFramebuffer)

bool HonorFB_v8::start(IOService *provider)
{
    IOLog("HonorFB_v8: start() provider=%s\n",
          provider ? provider->getMetaClass()->getClassName() : "NULL");
    if (!PE_state.initialized) { IOLog("HonorFB_v8: PE_state not init\n"); return false; }
    fbPhys     = static_cast<IOPhysicalAddress64>(PE_state.video.v_baseAddr & ~3ULL);
    fbW        = static_cast<uint32_t>(PE_state.video.v_width);
    fbH        = static_cast<uint32_t>(PE_state.video.v_height);
    fbRowBytes = static_cast<uint32_t>(PE_state.video.v_rowBytes);
    fbLen      = static_cast<IOByteCount>(static_cast<uint64_t>(fbH) * fbRowBytes);
    currentMode  = kHonorModeID;
    currentDepth = 0;

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

    IOLog("HonorFB_v8: fb=0x%llx %ux%u rowBytes=%u len=%llu\n",
          static_cast<unsigned long long>(fbPhys), fbW, fbH, fbRowBytes,
          static_cast<unsigned long long>(fbLen));
    if (fbPhys == 0 || fbLen == 0 || fbW == 0 || fbH == 0) {
        IOLog("HonorFB_v8: invalid boot framebuffer\n"); return false;
    }
    if (!IOFramebuffer::start(provider)) {
        IOLog("HonorFB_v8: IOFramebuffer::start FAILED\n"); return false;
    }
    IOLog("HonorFB_v8: start OK — PCI-attached, console adoption should proceed\n");
    return true;
}

// The base class hard-codes this to false, which is why an IOResources framebuffer can
// never be the console. Returning true (IOBootFramebuffer-style) lets open() set
// gIOFBConsoleFramebuffer = this and keeps the screen enabled.
bool HonorFB_v8::isConsoleDevice(void)
{
    IOLog("HonorFB_v8: isConsoleDevice() -> TRUE\n");
    return true;
}

IOReturn HonorFB_v8::enableController()
{
    IOLog("HonorFB_v8: enableController()\n");
    return kIOReturnSuccess;
}

IODeviceMemory * HonorFB_v8::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    IOLog("HonorFB_v8: getApertureRange(system) fb=0x%llx len=%llu\n",
          static_cast<unsigned long long>(fbPhys), static_cast<unsigned long long>(fbLen));
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v8::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v8::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v8::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v8::getInformationForDisplayMode(IODisplayModeID displayMode,
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

UInt64 HonorFB_v8::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v8::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
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

IOReturn HonorFB_v8::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v8::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode = displayMode; currentDepth = depth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v8::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v8::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v8::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag) : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v8::hasDDCConnect(IOIndex) { return true; }

IOReturn HonorFB_v8::getDDCBlock(IOIndex, UInt32 blockNumber, IOSelect,
                                 IOOptionBits, UInt8 *data, IOByteCount *length)
{
    if (blockNumber != 1 || !data || !length || *length < 128) return kIOReturnUnsupported;
    for (int i = 0; i < 128; i++) data[i] = edid[i];
    *length = 128;
    return kIOReturnSuccess;
}
