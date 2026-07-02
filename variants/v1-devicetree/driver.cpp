//
//  HonorFB v1 — device-tree variant.
//
//  Same core idea as v0 (publish PE_state.video framebuffer as an IOPlatformDevice
//  named "display"), but ALSO make the nub look like a real boot-display node in the
//  IODeviceTree plane (gIODTPlane), where IONDRVFramebuffer / IOBootNDRV historically
//  look for the console display:
//    * setName("display") + "device_type" = "display"
//    * "AAPL,boot-display" hint property
//    * masks the low 2 bits of the framebuffer physical base (v_baseAddr & ~3), which
//      is the address IOBootNDRV compares against.
//
//  Hypothesis under test: v0 fails to bind because the nub is absent from the device
//  tree / lacks the "display" device_type that IONDRVFramebuffer keys on.
//
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>   // gIODTPlane
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

#define super IOPlatformDevice

class HonorFB_v1 : public IOPlatformDevice {
    OSDeclareDefaultStructors(HonorFB_v1)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB_v1, IOPlatformDevice)

bool HonorFB_v1::start(IOService *provider)
{
    if (!PE_state.initialized) {
        IOLog("HonorFB_v1: PE_state not initialized — aborting\n");
        return false;
    }

    IOPhysicalAddress fbBase   = static_cast<IOPhysicalAddress>(PE_state.video.v_baseAddr & ~3ULL);
    IOByteCount       fbLength = static_cast<IOByteCount>(PE_state.video.v_height *
                                                          PE_state.video.v_rowBytes);

    IOLog("HonorFB_v1: boot fb base=0x%llx %lux%lu depth=%lu rowBytes=%lu len=0x%llx\n",
          static_cast<unsigned long long>(fbBase),
          static_cast<unsigned long>(PE_state.video.v_width),
          static_cast<unsigned long>(PE_state.video.v_height),
          static_cast<unsigned long>(PE_state.video.v_depth),
          static_cast<unsigned long>(PE_state.video.v_rowBytes),
          static_cast<unsigned long long>(fbLength));

    if (fbBase == 0 || fbLength == 0) {
        IOLog("HonorFB_v1: invalid boot framebuffer — aborting\n");
        return false;
    }

    if (!super::start(provider)) {
        IOLog("HonorFB_v1: super::start failed\n");
        return false;
    }

    IODeviceMemory::InitElement list[1];
    list[0].start  = fbBase;
    list[0].length = fbLength;

    OSArray *deviceMemory = IODeviceMemory::arrayFromList(list, 1);
    if (deviceMemory == nullptr) {
        IOLog("HonorFB_v1: IODeviceMemory::arrayFromList failed\n");
        return false;
    }
    setDeviceMemory(deviceMemory);
    deviceMemory->release();

    // Make the nub resemble a real boot-display device-tree node.
    setName("display");
    setProperty("device_type", "display");
    UInt8 bootDisplay[4] = { 0, 0, 0, 0 };
    setProperty("AAPL,boot-display", bootDisplay, sizeof(bootDisplay));

    // Attach into the device-tree plane where the boot console lives.
    IORegistryEntry *dtRoot = IORegistryEntry::fromPath("/", gIODTPlane);
    if (dtRoot != nullptr) {
        if (!attachToParent(dtRoot, gIODTPlane))
            IOLog("HonorFB_v1: attachToParent(gIODTPlane) failed (continuing)\n");
        dtRoot->release();
    } else {
        IOLog("HonorFB_v1: could not resolve gIODTPlane root (continuing)\n");
    }

    registerService();

    IOLog("HonorFB_v1: published 'display' nub in device-tree plane — OK\n");
    return true;
}
