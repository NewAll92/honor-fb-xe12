//
//  HonorFB v9 — PCI-attached console framebuffer, matched by exact iGPU device-id.
//
//  Refines v8 per binary analysis of IOGraphicsFamily v597:
//   * The console-adoption architecture (PCI ancestor -> IOFBController->fDevice ->
//     findConsole() adoption -> kPEEnableScreen) is correct and the ONLY path that works.
//   * v8 UNMASKED the iGPU (removed class-code=FFFFFFFF); that let AppleGraphicsDevicePolicy
//     engage the driverless Xe and hang earlier. v9 keeps the iGPU MASKED (class-code
//     FFFFFFFF in DeviceProperties) so Apple's graphics stack ignores it — HonorFB still
//     attaches because IOPCIFamily creates the IOPCIDevice nub regardless of class-code, and
//     IOFBController::init's ancestor walk tests OSDynamicCast(IOPCIDevice), not the class.
//   * A masked iGPU can't be matched by IOPCIClassMatch, so the Info.plist matches the exact
//     iGPU device-id via IOPCIPrimaryMatch (0x<devid>8086).
//   * isConsoleDevice() -> true (base class hard-codes false) so open() adopts us as console.
//   * We advertise ONE mode whose geometry EXACTLY equals the boot console (PE_state.video),
//     which initFB requires for the boot-graphics/logo continuity path.
//   Requires DeviceProperties: class-code=<FFFFFFFF> + AAPL,boot-display=<01000000> on the
//   iGPU, and boot-arg iog=0x400. RUN ON STOCK macOS.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

#define kHonorModeID 1

class HonorFB_v9 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v9)
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

OSDefineMetaClassAndStructors(HonorFB_v9, IOFramebuffer)

bool HonorFB_v9::start(IOService *provider)
{
    IOLog("HonorFB_v9: start() provider=%s\n",
          provider ? provider->getMetaClass()->getClassName() : "NULL");
    if (!PE_state.initialized) { IOLog("HonorFB_v9: PE_state not init\n"); return false; }
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

    IOLog("HonorFB_v9: fb=0x%llx %ux%u rowBytes=%u len=%llu\n",
          static_cast<unsigned long long>(fbPhys), fbW, fbH, fbRowBytes,
          static_cast<unsigned long long>(fbLen));
    if (fbPhys == 0 || fbLen == 0 || fbW == 0 || fbH == 0) {
        IOLog("HonorFB_v9: invalid boot framebuffer\n"); return false;
    }
    if (!IOFramebuffer::start(provider)) {
        IOLog("HonorFB_v9: IOFramebuffer::start FAILED\n"); return false;
    }
    IOLog("HonorFB_v9: start OK — PCI-attached (masked iGPU), console adoption should proceed\n");
    return true;
}

bool HonorFB_v9::isConsoleDevice(void)
{
    IOLog("HonorFB_v9: isConsoleDevice() -> TRUE\n");
    return true;
}

IOReturn HonorFB_v9::enableController()
{
    IOLog("HonorFB_v9: enableController()\n");
    return kIOReturnSuccess;
}

IODeviceMemory * HonorFB_v9::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    IOLog("HonorFB_v9: getApertureRange(system)\n");
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v9::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v9::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v9::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v9::getInformationForDisplayMode(IODisplayModeID displayMode,
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

UInt64 HonorFB_v9::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v9::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
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

IOReturn HonorFB_v9::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v9::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode = displayMode; currentDepth = depth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v9::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v9::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
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

IOReturn HonorFB_v9::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag) : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v9::hasDDCConnect(IOIndex) { return true; }

IOReturn HonorFB_v9::getDDCBlock(IOIndex, UInt32 blockNumber, IOSelect,
                                 IOOptionBits, UInt8 *data, IOByteCount *length)
{
    if (blockNumber != 1 || !data || !length || *length < 128) return kIOReturnUnsupported;
    for (int i = 0; i < 128; i++) data[i] = edid[i];
    *length = 128;
    return kIOReturnSuccess;
}
