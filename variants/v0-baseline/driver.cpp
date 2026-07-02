//
//  HonorFB v0 — baseline (faithful acidanthera/UEFIGraphicsFB clone).
//
//  Publishes the boot GOP framebuffer (recorded by XNU in PE_state.video) as an
//  IOPlatformDevice nub named "display". Apple's in-kernel IONDRVFramebuffer +
//  IOBootNDRV then bind to that nub and drive the IOFramebuffer -> WindowServer
//  path in software (no Metal, no acceleration) — exactly what we need on the
//  Intel Xe Gen12 iGPU, which has NO macOS accelerated driver.
//
//  Matching (see Info.plist): provider = AppleACPIPlatformExpert,
//  IOMatchCategory = IOPlatformDevice, name = "display".
//
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOPlatformExpert.h>   // class IOPlatformDevice
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>          // PE_state / PE_Video
#include <libkern/libkern.h>

#define super IOPlatformDevice

class HonorFB_v0 : public IOPlatformDevice {
    OSDeclareDefaultStructors(HonorFB_v0)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB_v0, IOPlatformDevice)

bool HonorFB_v0::start(IOService *provider)
{
    if (!PE_state.initialized) {
        IOLog("HonorFB_v0: PE_state not initialized — aborting\n");
        return false;
    }

    IOPhysicalAddress fbBase   = static_cast<IOPhysicalAddress>(PE_state.video.v_baseAddr);
    IOByteCount       fbLength = static_cast<IOByteCount>(PE_state.video.v_height *
                                                          PE_state.video.v_rowBytes);

    IOLog("HonorFB_v0: boot fb base=0x%llx %lux%lu depth=%lu rowBytes=%lu len=0x%llx\n",
          static_cast<unsigned long long>(fbBase),
          static_cast<unsigned long>(PE_state.video.v_width),
          static_cast<unsigned long>(PE_state.video.v_height),
          static_cast<unsigned long>(PE_state.video.v_depth),
          static_cast<unsigned long>(PE_state.video.v_rowBytes),
          static_cast<unsigned long long>(fbLength));

    if (fbBase == 0 || fbLength == 0) {
        IOLog("HonorFB_v0: invalid boot framebuffer (base or length is 0) — aborting\n");
        return false;
    }

    if (!super::start(provider)) {
        IOLog("HonorFB_v0: super::start failed\n");
        return false;
    }

    IODeviceMemory::InitElement list[1];
    list[0].start  = fbBase;
    list[0].length = fbLength;

    OSArray *deviceMemory = IODeviceMemory::arrayFromList(list, 1);
    if (deviceMemory == nullptr) {
        IOLog("HonorFB_v0: IODeviceMemory::arrayFromList failed\n");
        return false;
    }
    setDeviceMemory(deviceMemory);
    deviceMemory->release();

    registerService();

    IOLog("HonorFB_v0: published 'display' nub with boot framebuffer — OK\n");
    return true;
}
