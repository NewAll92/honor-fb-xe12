//
//  HonorFB.cpp
//  "Dumb" boot-framebuffer nub for Honor MagicBook X16 (i5-12450H, Intel Xe Gen12 iGPU).
//
//  The Alder Lake Xe iGPU has NO macOS driver, so WindowServer finds no IOFramebuffer and
//  the screen goes black at the graphics hand-off. This kext takes the framebuffer the
//  firmware/OpenCore already lit (the GOP / boot console framebuffer, recorded by XNU in
//  PE_state.video) and re-publishes it as an IOPlatformDevice nub named "display".
//
//  Apple's own IONDRVFramebuffer (already in the kernel) binds to any IOPlatformDevice /
//  IOPCIDevice named exactly "display", attaches the device memory as VRAM, and drives the
//  whole IOFramebuffer -> WindowServer path in *software* (no acceleration, no Metal).
//
//  This is the approach used in production by acidanthera/UEFIGraphicsFB. We do NOT subclass
//  IOFramebuffer (that is the fallback, see STATUS.md / approach A).
//
//  IMPORTANT: for this to win the display, the real iGPU must be neutralised via OpenCore
//  DeviceProperties (class-code = FFFFFFFF, name = <unused>) so that no IOAccelerator tries
//  to claim the framebuffer and throws SkyLight into a WindowServer relaunch loop.
//
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOPlatformExpert.h>   // also declares class IOPlatformDevice
#include <pexpert/pexpert.h>          // PE_state / PE_Video
#include <libkern/libkern.h>          // IOLog

#define super IOPlatformDevice

class HonorFB : public IOPlatformDevice {
    OSDeclareDefaultStructors(HonorFB)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB, IOPlatformDevice)

bool HonorFB::start(IOService *provider)
{
    if (!PE_state.initialized) {
        IOLog("HonorFB: PE_state not initialized — aborting\n");
        return false;
    }

    IOPhysicalAddress fbBase   = static_cast<IOPhysicalAddress>(PE_state.video.v_baseAddr);
    IOByteCount       fbLength = static_cast<IOByteCount>(PE_state.video.v_height *
                                                          PE_state.video.v_rowBytes);

    IOLog("HonorFB: boot fb base=0x%llx %lux%lu depth=%lu rowBytes=%lu len=0x%llx\n",
          static_cast<unsigned long long>(fbBase),
          static_cast<unsigned long>(PE_state.video.v_width),
          static_cast<unsigned long>(PE_state.video.v_height),
          static_cast<unsigned long>(PE_state.video.v_depth),
          static_cast<unsigned long>(PE_state.video.v_rowBytes),
          static_cast<unsigned long long>(fbLength));

    if (fbBase == 0 || fbLength == 0) {
        IOLog("HonorFB: invalid boot framebuffer (base or length is 0) — aborting\n");
        return false;
    }

    if (!super::start(provider)) {
        IOLog("HonorFB: super::start failed\n");
        return false;
    }

    // Attach the boot framebuffer as the nub's device memory (this becomes the VRAM
    // aperture that IONDRVFramebuffer will scan out).
    IODeviceMemory::InitElement list[1];
    list[0].start  = fbBase;
    list[0].length = fbLength;

    OSArray *deviceMemory = IODeviceMemory::arrayFromList(list, 1);
    if (deviceMemory == nullptr) {
        IOLog("HonorFB: IODeviceMemory::arrayFromList failed\n");
        return false;
    }
    setDeviceMemory(deviceMemory);
    deviceMemory->release();

    // Expose ourselves so IONDRVFramebuffer can match on name == "display".
    registerService();

    IOLog("HonorFB: published 'display' nub with boot framebuffer — OK\n");
    return true;
}
