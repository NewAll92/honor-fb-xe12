//
//  HonorFB v13 — FOUNDATION diagnostic, fixed. v12 found IOPCIDevices=0 but its scan blocked
//  the IOKit config thread in start(), which can PREVENT PCI enumeration (false zero). v13
//  matches IOResources, then spawns a SEPARATE kernel thread that lets boot progress and
//  scans IOPCIDevices every 2s for up to 30s — non-blocking — and panics with the real count
//  and the iGPU id the moment it appears (or the max count seen if it never does).
//
//  iGPU appears  -> panic "FOUND iGPU 8086:46XX ... PCIdevs=N"  => PCI works, we have the id.
//  count>0, no iGPU -> PCI enumerates but the iGPU isn't an 8086:46xx IOPCIDevice.
//  count stays 0 -> macOS genuinely never enumerates PCI on this box (deep platform wall).
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
#include <kern/thread.h>
#include <kern/debug.h>

#define kHonorModeID 1

class HonorFB_v13 : public IOFramebuffer {
    OSDeclareDefaultStructors(HonorFB_v13)
public:
    IOPhysicalAddress64 fbPhys; IOByteCount fbLen;
    uint32_t fbW, fbH, fbRowBytes; IODisplayModeID currentMode; IOIndex currentDepth; uint8_t edid[128];

    virtual bool      start(IOService *provider) override;
    virtual IOReturn  enableController() override;
    virtual IODeviceMemory * getApertureRange(IOPixelAperture aperture) override;
    virtual const char * getPixelFormats() override;
    virtual IOItemCount getDisplayModeCount() override;
    virtual IOReturn  getDisplayModes(IODisplayModeID *allDisplayModes) override;
    virtual IOReturn  getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation *info) override;
    virtual UInt64    getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    virtual IOReturn  getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation *pixelInfo) override;
    virtual IOReturn  getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth) override;
    virtual IOReturn  setDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    virtual IOReturn  getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t *value) override;
    virtual IOReturn  setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value) override;
    virtual IOReturn  connectFlags(IOIndex connectIndex, IODisplayModeID displayMode, IOOptionBits *flags) override;
    virtual bool      hasDDCConnect(IOIndex connectIndex) override;
    virtual IOReturn  getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType, IOOptionBits options, UInt8 *data, IOByteCount *length) override;
};

OSDefineMetaClassAndStructors(HonorFB_v13, IOFramebuffer)

static uint32_t readId(IORegistryEntry *p, const char *key)
{
    OSData *d = p ? OSDynamicCast(OSData, p->getProperty(key)) : nullptr;
    if (d && d->getLength() >= 2) return *(const uint16_t *)d->getBytesNoCopy();
    return 0;
}

static void honorScanThread(void *, wait_result_t)
{
    uint32_t maxN = 0, lastIntel = 0;
    for (int i = 1; i <= 15; i++) {
        IOSleep(2000);   // 2s, on our own thread -> boot/PCI keeps progressing
        OSDictionary *m = IOService::serviceMatching("IOPCIDevice");
        OSIterator *it = m ? IOService::getMatchingServices(m) : nullptr;
        uint32_t n = 0, iv = 0, idv = 0;
        if (it) {
            IORegistryEntry *e;
            while ((e = OSDynamicCast(IORegistryEntry, it->getNextObject()))) {
                n++;
                uint32_t v = readId(e, "vendor-id");
                uint32_t d = readId(e, "device-id");
                if (v == 0x8086) { lastIntel = d; if ((d & 0xFF00) == 0x4600) { iv = v; idv = d; } }
            }
            it->release();
        }
        if (m) m->release();
        if (n > maxN) maxN = n;
        if (idv) panic("HonorFB_v13: FOUND iGPU %04x:%04x at %ds. PCIdevs=%u", (unsigned)iv, (unsigned)idv, i*2, (unsigned)n);
    }
    panic("HonorFB_v13: NO iGPU(46xx) after 30s. maxPCIdevs=%u lastIntelDev=0x%04x", (unsigned)maxN, (unsigned)lastIntel);
}

bool HonorFB_v13::start(IOService *)
{
    thread_t t = nullptr;
    if (kernel_thread_start(&honorScanThread, nullptr, &t) == KERN_SUCCESS && t)
        thread_deallocate(t);
    return false;   // don't pose as a framebuffer; the scan thread does the work + panics
}

IOReturn HonorFB_v13::enableController() { return kIOReturnSuccess; }
IODeviceMemory * HonorFB_v13::getApertureRange(IOPixelAperture a){ return (a==kIOFBSystemAperture)?IODeviceMemory::withRange(fbPhys,fbLen):nullptr; }
const char * HonorFB_v13::getPixelFormats(){ static const char pf[]=IO32BitDirectPixels "\0"; return pf; }
IOItemCount HonorFB_v13::getDisplayModeCount(){ return 1; }
IOReturn HonorFB_v13::getDisplayModes(IODisplayModeID *a){ if(!a) return kIOReturnBadArgument; a[0]=kHonorModeID; return kIOReturnSuccess; }
IOReturn HonorFB_v13::getInformationForDisplayMode(IODisplayModeID dm, IODisplayModeInformation *info){ if(dm!=kHonorModeID||!info) return kIOReturnBadArgument; bzero(info,sizeof(*info)); info->nominalWidth=fbW; info->nominalHeight=fbH; info->refreshRate=60<<16; info->maxDepthIndex=0; info->flags=kDisplayModeValidFlag|kDisplayModeSafeFlag|kDisplayModeDefaultFlag; return kIOReturnSuccess; }
UInt64 HonorFB_v13::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex){ return 0; }
IOReturn HonorFB_v13::getPixelInformation(IODisplayModeID dm, IOIndex depth, IOPixelAperture ap, IOPixelInformation *pi){ if(dm!=kHonorModeID||depth!=0||ap!=kIOFBSystemAperture||!pi) return kIOReturnBadArgument; bzero(pi,sizeof(*pi)); pi->bytesPerRow=fbRowBytes; pi->activeWidth=fbW; pi->activeHeight=fbH; pi->bitsPerPixel=32; pi->pixelType=kIORGBDirectPixels; pi->componentCount=3; pi->bitsPerComponent=8; pi->componentMasks[0]=0x00FF0000; pi->componentMasks[1]=0x0000FF00; pi->componentMasks[2]=0x000000FF; strncpy(pi->pixelFormat,IO32BitDirectPixels,sizeof(pi->pixelFormat)); return kIOReturnSuccess; }
IOReturn HonorFB_v13::getCurrentDisplayMode(IODisplayModeID *dm, IOIndex *d){ if(dm)*dm=currentMode; if(d)*d=currentDepth; return kIOReturnSuccess; }
IOReturn HonorFB_v13::setDisplayMode(IODisplayModeID dm, IOIndex d){ currentMode=dm; currentDepth=d; return kIOReturnSuccess; }
IOReturn HonorFB_v13::getAttributeForConnection(IOIndex ci, IOSelect a, uintptr_t *v){ if(a==kConnectionEnable||a==kConnectionCheckEnable){ if(v)*v=1; return kIOReturnSuccess; } if(a==kConnectionFlags){ if(v)*v=0; return kIOReturnSuccess; } return IOFramebuffer::getAttributeForConnection(ci,a,v); }
IOReturn HonorFB_v13::setAttributeForConnection(IOIndex ci, IOSelect a, uintptr_t v){ return IOFramebuffer::setAttributeForConnection(ci,a,v); }
IOReturn HonorFB_v13::connectFlags(IOIndex, IODisplayModeID dm, IOOptionBits *f){ if(!f) return kIOReturnBadArgument; *f=(dm==kHonorModeID)?(kDisplayModeValidFlag|kDisplayModeSafeFlag|kDisplayModeDefaultFlag):0; return kIOReturnSuccess; }
bool HonorFB_v13::hasDDCConnect(IOIndex){ return true; }
IOReturn HonorFB_v13::getDDCBlock(IOIndex, UInt32 bn, IOSelect, IOOptionBits, UInt8 *data, IOByteCount *len){ if(bn!=1||!data||!len||*len<128) return kIOReturnUnsupported; for(int i=0;i<128;i++) data[i]=edid[i]; *len=128; return kIOReturnSuccess; }
