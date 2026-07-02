//
//  HonorFB v2 — IOResources / synthesized-nub variant.
//
//  Instead of relying on our personality being matched as an IOPlatformDevice named
//  "display" (v0/v1), this variant matches the always-present IOResources service so
//  its start() is GUARANTEED to run exactly once, early at boot. It then explicitly
//  synthesizes a fresh IOPlatformDevice nub named "display", attaches the boot
//  framebuffer as its device memory, and registers it in both the IOService and the
//  IODeviceTree planes — mimicking what the platform expert does for a real GPU's
//  display child. IONDRVFramebuffer / IOBootNDRV then bind to the synthesized nub.
//
//  Hypothesis under test: v0/v1 never even run because the AppleACPIPlatformExpert
//  personality match is not triggered on this hardware; decoupling "our code runs"
//  from "the nub matches" removes that failure mode.
//
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOPlatformExpert.h>       // class IOPlatformDevice
#include <IOKit/IODeviceTreeSupport.h>    // gIODTPlane
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>

class HonorFB_v2 : public IOService {
    OSDeclareDefaultStructors(HonorFB_v2)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB_v2, IOService)

bool HonorFB_v2::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        IOLog("HonorFB_v2: IOService::start failed\n");
        return false;
    }

    if (!PE_state.initialized) {
        IOLog("HonorFB_v2: PE_state not initialized — aborting\n");
        return false;
    }

    IOPhysicalAddress fbBase   = static_cast<IOPhysicalAddress>(PE_state.video.v_baseAddr & ~3ULL);
    IOByteCount       fbLength = static_cast<IOByteCount>(PE_state.video.v_height *
                                                          PE_state.video.v_rowBytes);

    IOLog("HonorFB_v2: boot fb base=0x%llx %lux%lu depth=%lu rowBytes=%lu len=0x%llx\n",
          static_cast<unsigned long long>(fbBase),
          static_cast<unsigned long>(PE_state.video.v_width),
          static_cast<unsigned long>(PE_state.video.v_height),
          static_cast<unsigned long>(PE_state.video.v_depth),
          static_cast<unsigned long>(PE_state.video.v_rowBytes),
          static_cast<unsigned long long>(fbLength));

    if (fbBase == 0 || fbLength == 0) {
        IOLog("HonorFB_v2: invalid boot framebuffer — aborting\n");
        return false;
    }

    // Synthesize a fresh "display" nub.
    IOPlatformDevice *nub = OSTypeAlloc(IOPlatformDevice);
    if (nub == nullptr) {
        IOLog("HonorFB_v2: OSTypeAlloc(IOPlatformDevice) failed\n");
        return false;
    }
    if (!nub->init(static_cast<OSDictionary *>(nullptr))) {
        IOLog("HonorFB_v2: nub->init failed\n");
        nub->release();
        return false;
    }

    nub->setName("display");
    nub->setProperty("device_type", "display");
    nub->setProperty("IOName", "display");
    UInt8 bootDisplay[4] = { 0, 0, 0, 0 };
    nub->setProperty("AAPL,boot-display", bootDisplay, sizeof(bootDisplay));

    if (!nub->attach(this)) {
        IOLog("HonorFB_v2: nub->attach failed\n");
        nub->release();
        return false;
    }

    IODeviceMemory::InitElement list[1];
    list[0].start  = fbBase;
    list[0].length = fbLength;
    OSArray *deviceMemory = IODeviceMemory::arrayFromList(list, 1);
    if (deviceMemory != nullptr) {
        nub->setDeviceMemory(deviceMemory);
        deviceMemory->release();
    }

    // Also expose the nub in the device-tree plane.
    IORegistryEntry *dtRoot = IORegistryEntry::fromPath("/", gIODTPlane);
    if (dtRoot != nullptr) {
        if (!nub->attachToParent(dtRoot, gIODTPlane))
            IOLog("HonorFB_v2: nub attachToParent(gIODTPlane) failed (continuing)\n");
        dtRoot->release();
    }

    nub->registerService();
    nub->release();   // the registry planes now retain the nub

    IOLog("HonorFB_v2: synthesized 'display' nub — OK\n");
    return true;
}
