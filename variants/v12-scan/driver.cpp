//
//  HonorFB v12 — FOUNDATION diagnostic. Matches IOResources (proven-reliable load path,
//  same as v3-v7), then from inside start() scans the IOKit registry for IOPCIDevices and
//  panics with: proof-of-execution + PCI device count + the Intel iGPU's real vendor/device id.
//
//  Answers, in ONE boot:
//   * Does our kext actually load + start? (if it panics at all -> yes.)
//   * Is the iGPU enumerated as an IOPCIDevice macOS can see? (iGPU=8086:46xx in the panic -> yes.)
//   * What is the exact device-id? (read from the found device.)
//  v8-v11 matched the iGPU IOPCIDevice directly and never panicked; this sidesteps that by
//  matching IOResources and inspecting PCI from within.
//
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include <kern/debug.h>

#define kHonorModeID 1

class HonorFB_v12 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v12)
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

OSDefineMetaClassAndStructors(HonorFB_v12, IOFramebuffer)

static uint32_t readId(IORegistryEntry *p, const char *key)
{
    OSData *d = p ? OSDynamicCast(OSData, p->getProperty(key)) : nullptr;
    if (d && d->getLength() >= 2) return *(const uint16_t *)d->getBytesNoCopy();
    return 0;
}

bool HonorFB_v12::start(IOService *provider)
{
    // Ensure PCI is enumerated: wait (up to 5s) for any IOPCIDevice to be registered.
    OSDictionary *mw = IOService::serviceMatching("IOPCIDevice");
    IOService *anyPci = mw ? IOService::waitForMatchingService(mw, 5000000000ULL) : nullptr;
    if (anyPci) anyPci->release();

    uint32_t n = 0, iv = 0, idv = 0;
    OSDictionary *m = IOService::serviceMatching("IOPCIDevice");
    OSIterator *it = m ? IOService::getMatchingServices(m) : nullptr;
    if (it) {
        IORegistryEntry *e;
        while ((e = OSDynamicCast(IORegistryEntry, it->getNextObject()))) {
            n++;
            uint32_t v = readId(e, "vendor-id");
            uint32_t d = readId(e, "device-id");
            if (v == 0x8086 && (d & 0xFF00) == 0x4600) { iv = v; idv = d; }  // Alder Lake iGPU
        }
        it->release();
    }
    if (m) m->release();

    panic("HonorFB_v12: RAN via provider=%s. IOPCIDevices=%u. iGPU(8086:46xx) = %04x:%04x",
          provider ? provider->getMetaClass()->getClassName() : "NULL",
          (unsigned)n, (unsigned)iv, (unsigned)idv);
    return false;   // unreachable
}

IOReturn HonorFB_v12::enableController() { return kIOReturnSuccess; }
IODeviceMemory * HonorFB_v12::getApertureRange(IOPixelAperture a)
{ return (a == kIOFBSystemAperture) ? IODeviceMemory::withRange(fbPhys, fbLen) : nullptr; }
const char * HonorFB_v12::getPixelFormats() { static const char pf[] = IO32BitDirectPixels "\0"; return pf; }
IOItemCount HonorFB_v12::getDisplayModeCount() { return 1; }
IOReturn HonorFB_v12::getDisplayModes(IODisplayModeID *a){ if(!a) return kIOReturnBadArgument; a[0]=kHonorModeID; return kIOReturnSuccess; }
IOReturn HonorFB_v12::getInformationForDisplayMode(IODisplayModeID dm, IODisplayModeInformation *info)
{ if(dm!=kHonorModeID||!info) return kIOReturnBadArgument; bzero(info,sizeof(*info));
  info->nominalWidth=fbW; info->nominalHeight=fbH; info->refreshRate=60<<16; info->maxDepthIndex=0;
  info->flags=kDisplayModeValidFlag|kDisplayModeSafeFlag|kDisplayModeDefaultFlag; return kIOReturnSuccess; }
UInt64 HonorFB_v12::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex){ return 0; }
IOReturn HonorFB_v12::getPixelInformation(IODisplayModeID dm, IOIndex depth, IOPixelAperture ap, IOPixelInformation *pi)
{ if(dm!=kHonorModeID||depth!=0||ap!=kIOFBSystemAperture||!pi) return kIOReturnBadArgument; bzero(pi,sizeof(*pi));
  pi->bytesPerRow=fbRowBytes; pi->activeWidth=fbW; pi->activeHeight=fbH; pi->bitsPerPixel=32;
  pi->pixelType=kIORGBDirectPixels; pi->componentCount=3; pi->bitsPerComponent=8;
  pi->componentMasks[0]=0x00FF0000; pi->componentMasks[1]=0x0000FF00; pi->componentMasks[2]=0x000000FF;
  strncpy(pi->pixelFormat, IO32BitDirectPixels, sizeof(pi->pixelFormat)); return kIOReturnSuccess; }
IOReturn HonorFB_v12::getCurrentDisplayMode(IODisplayModeID *dm, IOIndex *d){ if(dm)*dm=currentMode; if(d)*d=currentDepth; return kIOReturnSuccess; }
IOReturn HonorFB_v12::setDisplayMode(IODisplayModeID dm, IOIndex d){ currentMode=dm; currentDepth=d; return kIOReturnSuccess; }
IOReturn HonorFB_v12::getAttributeForConnection(IOIndex ci, IOSelect a, uintptr_t *v)
{ if(a==kConnectionEnable||a==kConnectionCheckEnable){ if(v)*v=1; return kIOReturnSuccess; }
  if(a==kConnectionFlags){ if(v)*v=0; return kIOReturnSuccess; }
  return IOFramebuffer::getAttributeForConnection(ci,a,v); }
IOReturn HonorFB_v12::setAttributeForConnection(IOIndex ci, IOSelect a, uintptr_t v)
{ return IOFramebuffer::setAttributeForConnection(ci,a,v); }
IOReturn HonorFB_v12::connectFlags(IOIndex, IODisplayModeID dm, IOOptionBits *f)
{ if(!f) return kIOReturnBadArgument; *f=(dm==kHonorModeID)?(kDisplayModeValidFlag|kDisplayModeSafeFlag|kDisplayModeDefaultFlag):0; return kIOReturnSuccess; }
bool HonorFB_v12::hasDDCConnect(IOIndex){ return true; }
IOReturn HonorFB_v12::getDDCBlock(IOIndex, UInt32 bn, IOSelect, IOOptionBits, UInt8 *data, IOByteCount *len)
{ if(bn!=1||!data||!len||*len<128) return kIOReturnUnsupported; for(int i=0;i<128;i++) data[i]=edid[i]; *len=128; return kIOReturnSuccess; }
