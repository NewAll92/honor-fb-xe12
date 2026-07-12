//
//  HonorFB v6 — v3 IOFramebuffer + SYNTHETIC VBL CLOCK (the missing piece).
//
//  Root cause of the WindowServer freeze (Apple logo ~50% / verbose stuck at
//  "CoreAnalyticsHub" + "GTrace synchronization point 1"): HonorFB v3 published a
//  connected display + EDID (so the logo scans out) but NEVER delivered a vertical-blank
//  (VBL) frame clock. WindowServer/CoreDisplay register for kIOFBVBLInterruptType and then
//  BLOCK forever waiting for a vsync "top" that a bare framebuffer never produces.
//
//  Fix: a timer-driven synthetic VBL, ported verbatim from the WORKING SVGAFramebuffer
//  (the VM driver whose VBL is exactly what makes its driverless desktop composite). This
//  is the same trick driverless QEMU/VMware Intel guests use to software-composite a
//  desktop with no accelerator at all.
//
//  RUN ON STOCK macOS. Do NOT apply the OCLP non-Metal accelerator stack: it installs
//  IOAcceleratorFamily2, which then makes WindowServer wait on an accelerator nub a bare
//  framebuffer can never provide (confirmed: it makes the freeze worse, not better). The
//  VBL clock — not an accelerator — is the missing contract.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

// Same declarations the proven SVGAFramebuffer uses (avoids pulling <kern/clock.h>).
extern "C" void clock_get_uptime(uint64_t *result);
extern "C" void nanoseconds_to_absolutetime(uint64_t nanoseconds, uint64_t *result);

#define kHonorModeID 1
#define kHonorHz     60           // synthetic refresh rate = VBL cadence

class HonorFB_v6 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v6)
public:
    IOPhysicalAddress64 fbPhys;
    IOByteCount         fbLen;
    uint32_t            fbW, fbH, fbRowBytes;
    IODisplayModeID     currentMode;
    IOIndex             currentDepth;
    uint8_t             edid[128];

    // --- synthetic VBL clock (ported from SVGAFramebuffer, which works) ---
    IOFBInterruptProc   fVBLProc;
    OSObject           *fVBLTarget;
    void               *fVBLRef;
    bool                fVBLEnabled;
    IOTimerEventSource *fVBLTimer;
    uint64_t            fLastVBLTime;
    uint64_t            fVBLDelta;

    virtual bool      start(IOService *provider) override;
    virtual void      stop(IOService *provider) override;
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

    // --- VBL overrides (THE fix) ---
    virtual IOReturn  registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc,
                        OSObject *target, void *ref, void **interruptRef) override;
    virtual IOReturn  unregisterInterrupt(void *interruptRef) override;
    virtual IOReturn  setInterruptState(void *interruptRef, UInt32 state) override;
    virtual void      getVBLTime(AbsoluteTime *time, AbsoluteTime *delta) override;
    void              vblTimerFired(IOTimerEventSource *sender);
};

OSDefineMetaClassAndStructors(HonorFB_v6, IOFramebuffer)

bool HonorFB_v6::start(IOService *provider)
{
    if (!PE_state.initialized) { IOLog("HonorFB_v6: PE_state not init\n"); return false; }
    fbPhys     = static_cast<IOPhysicalAddress64>(PE_state.video.v_baseAddr & ~3ULL);
    fbW        = static_cast<uint32_t>(PE_state.video.v_width);
    fbH        = static_cast<uint32_t>(PE_state.video.v_height);
    fbRowBytes = static_cast<uint32_t>(PE_state.video.v_rowBytes);
    fbLen      = static_cast<IOByteCount>(static_cast<uint64_t>(fbH) * fbRowBytes);
    currentMode  = kHonorModeID;
    currentDepth = 0;

    // VBL state init. Seed timestamp + period NOW (in start) so getVBLTime() never
    // returns garbage even if it is queried before the timer arms.
    fVBLProc = NULL; fVBLTarget = NULL; fVBLRef = NULL; fVBLEnabled = false;
    fVBLTimer = NULL;
    clock_get_uptime(&fLastVBLTime);
    nanoseconds_to_absolutetime(1000000000ULL / kHonorHz, &fVBLDelta);

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

    IOLog("HonorFB_v6: fb=0x%llx %ux%u rowBytes=%u\n",
          static_cast<unsigned long long>(fbPhys), fbW, fbH, fbRowBytes);
    if (fbPhys == 0 || fbLen == 0 || fbW == 0 || fbH == 0) {
        IOLog("HonorFB_v6: invalid boot framebuffer\n"); return false;
    }
    if (!IOFramebuffer::start(provider)) {
        IOLog("HonorFB_v6: IOFramebuffer::start failed\n"); return false;
    }
    IOLog("HonorFB_v6: start OK — display + EDID published (VBL clock arms in enableController)\n");
    return true;
}

void HonorFB_v6::stop(IOService *provider)
{
    if (fVBLTimer) {
        fVBLEnabled = false;
        fVBLTimer->cancelTimeout();
        if (getWorkLoop()) getWorkLoop()->removeEventSource(fVBLTimer);
        fVBLTimer->release();
        fVBLTimer = NULL;
    }
    IOFramebuffer::stop(provider);
}

IOReturn HonorFB_v6::enableController()
{
    // Arm the synthetic VBL clock. WindowServer/CoreDisplay block on this vsync top;
    // once it ticks, they schedule frames and the software compositor comes alive.
    IOWorkLoop *wl = getWorkLoop();
    if (wl && !fVBLTimer) {
        fVBLTimer = IOTimerEventSource::timerEventSource(this,
            OSMemberFunctionCast(IOTimerEventSource::Action, this, &HonorFB_v6::vblTimerFired));
        if (fVBLTimer && wl->addEventSource(fVBLTimer) == kIOReturnSuccess) {
            clock_get_uptime(&fLastVBLTime);
            fVBLTimer->setTimeoutUS(1000000 / kHonorHz);
            setProperty("HonorFB_VBL", true);
            IOLog("HonorFB_v6: synthetic VBL clock armed @ %uHz\n", (unsigned)kHonorHz);
        } else {
            IOLog("HonorFB_v6: WARNING could not arm VBL timer\n");
        }
    }
    return kIOReturnSuccess;
}

// Timer top: timestamp the VBL, wake WindowServer's registered proc, re-arm. This is the
// heartbeat CoreDisplay waits on — identical shape to SVGAFramebuffer::presentTimerFired.
void HonorFB_v6::vblTimerFired(IOTimerEventSource *sender)
{
    clock_get_uptime(&fLastVBLTime);
    nanoseconds_to_absolutetime(1000000000ULL / kHonorHz, &fVBLDelta);
    if (fVBLProc && fVBLEnabled) fVBLProc(fVBLTarget, fVBLRef);
    if (sender) sender->setTimeoutUS(1000000 / kHonorHz);
}

IODeviceMemory * HonorFB_v6::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    return IODeviceMemory::withRange(fbPhys, fbLen);
}

const char * HonorFB_v6::getPixelFormats()
{
    static const char pf[] = IO32BitDirectPixels "\0";
    return pf;
}

IOItemCount HonorFB_v6::getDisplayModeCount() { return 1; }

IOReturn HonorFB_v6::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = kHonorModeID;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v6::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                  IODisplayModeInformation *info)
{
    if (displayMode != kHonorModeID || !info) return kIOReturnBadArgument;
    bzero(info, sizeof(*info));
    info->nominalWidth  = fbW;
    info->nominalHeight = fbH;
    info->refreshRate   = kHonorHz << 16;
    info->maxDepthIndex = 0;
    info->flags         = kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag;
    return kIOReturnSuccess;
}

UInt64 HonorFB_v6::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex) { return 0; }

IOReturn HonorFB_v6::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
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

IOReturn HonorFB_v6::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode) *displayMode = currentMode;
    if (depth)       *depth       = currentDepth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v6::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    currentMode = displayMode; currentDepth = depth;
    return kIOReturnSuccess;
}

IOReturn HonorFB_v6::getAttributeForConnection(IOIndex, IOSelect attribute, uintptr_t *value)
{
    switch (attribute) {
        case kConnectionEnable:
        case kConnectionCheckEnable:
            if (value) *value = 1;
            return kIOReturnSuccess;
        case kConnectionFlags:
            if (value) *value = 0;
            return kIOReturnSuccess;
        default:
            if (value) *value = 0;
            return kIOReturnUnsupported;
    }
}

IOReturn HonorFB_v6::setAttributeForConnection(IOIndex, IOSelect, uintptr_t)
{
    return kIOReturnSuccess;
}

IOReturn HonorFB_v6::connectFlags(IOIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (!flags) return kIOReturnBadArgument;
    *flags = (displayMode == kHonorModeID)
             ? (kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag) : 0;
    return kIOReturnSuccess;
}

bool HonorFB_v6::hasDDCConnect(IOIndex) { return true; }

IOReturn HonorFB_v6::getDDCBlock(IOIndex, UInt32 blockNumber, IOSelect,
                                 IOOptionBits, UInt8 *data, IOByteCount *length)
{
    if (blockNumber != 1 || !data || !length || *length < 128) return kIOReturnUnsupported;
    for (int i = 0; i < 128; i++) data[i] = edid[i];
    *length = 128;
    return kIOReturnSuccess;
}

// ===================== THE FIX: synthetic VBL =====================
// Verbatim shape from SVGAFramebuffer (proven to unblock WindowServer on a soft compositor).

IOReturn HonorFB_v6::registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc,
    OSObject *target, void *ref, void **interruptRef)
{
    if (interruptType == kIOFBVBLInterruptType) {
        fVBLProc = proc; fVBLTarget = target; fVBLRef = ref; fVBLEnabled = true;
        if (interruptRef) *interruptRef = &fVBLProc;   // sentinel = our VBL storage
        setProperty("HonorFB_VBL_Registered", true);
        return kIOReturnSuccess;
    }
    return IOFramebuffer::registerForInterruptType(interruptType, proc, target, ref, interruptRef);
}

IOReturn HonorFB_v6::unregisterInterrupt(void *interruptRef)
{
    if (interruptRef == &fVBLProc) { fVBLEnabled = false; fVBLProc = NULL; return kIOReturnSuccess; }
    return IOFramebuffer::unregisterInterrupt(interruptRef);
}

IOReturn HonorFB_v6::setInterruptState(void *interruptRef, UInt32 state)
{
    if (interruptRef == &fVBLProc) { fVBLEnabled = (state != 0); return kIOReturnSuccess; }
    return IOFramebuffer::setInterruptState(interruptRef, state);
}

void HonorFB_v6::getVBLTime(AbsoluteTime *time, AbsoluteTime *delta)
{
    if (time)  *time  = fLastVBLTime;
    if (delta) *delta = fVBLDelta;
}
