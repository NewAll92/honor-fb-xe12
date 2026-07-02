//
//  HonorFB vpaint-probe — DIAGNOSTIC that uses the SCREEN as its output channel.
//
//  The Honor is headless (no IOLog/ioreg/SSH/serial, and this firmware did not persist
//  an ApplePanic NVRAM log). So instead of reporting through software, this variant
//  reports through the panel itself: it maps the boot GOP framebuffer that XNU recorded
//  in PE_state.video (uncached) and fills it with four unmistakable horizontal bands —
//  WHITE / RED / GREEN / BLUE. Nothing draws those by accident.
//
//  It matches IOResources so start() is GUARANTEED to run early at boot.
//
//  What the outcome tells us (the user just watches the panel):
//    * Screen changes from the frozen boot text to 4 colour bands  ->  our code runs,
//      PE_state.video.v_baseAddr is valid, AND the display is still scanning out that
//      memory. The republish approach is sound; only the IONDRVFramebuffer *binding*
//      is missing -> pursue a real IOFramebuffer subclass.
//    * At least the WHITE band shows but colours look swapped  ->  same success, the
//      pixel format is just RGB-order instead of BGR (good to know for a real driver).
//    * Screen stays exactly the frozen boot text  ->  either start() never runs, or
//      v_baseAddr is wrong / not scanned out -> the republish can't work as-is; pivot to
//      the OpenCore GOP handoff or a completely different display path.
//
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>          // PE_state / PE_Video
#include <libkern/libkern.h>

class HonorFB_vpaint : public IOService {
    OSDeclareDefaultStructors(HonorFB_vpaint)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB_vpaint, IOService)

bool HonorFB_vpaint::start(IOService *provider)
{
    IOService::start(provider);

    if (!PE_state.initialized) {
        IOLog("HonorFB_vpaint: PE_state not initialized — aborting\n");
        return false;
    }

    IOPhysicalAddress fbBase = static_cast<IOPhysicalAddress>(PE_state.video.v_baseAddr & ~3ULL);
    uint32_t w  = static_cast<uint32_t>(PE_state.video.v_width);
    uint32_t h  = static_cast<uint32_t>(PE_state.video.v_height);
    uint32_t rb = static_cast<uint32_t>(PE_state.video.v_rowBytes);
    IOByteCount len = static_cast<IOByteCount>(static_cast<uint64_t>(h) * rb);

    IOLog("HonorFB_vpaint: base=0x%llx %ux%u depth=%lu rowBytes=%u len=0x%llx\n",
          static_cast<unsigned long long>(fbBase), w, h,
          static_cast<unsigned long>(PE_state.video.v_depth), rb,
          static_cast<unsigned long long>(len));

    if (fbBase == 0 || len == 0 || w == 0 || h == 0 || rb == 0) {
        IOLog("HonorFB_vpaint: invalid boot framebuffer — aborting\n");
        return false;
    }

    IOMemoryDescriptor *md =
        IOMemoryDescriptor::withPhysicalAddress(fbBase, len, kIODirectionInOut);
    if (md == nullptr) {
        IOLog("HonorFB_vpaint: withPhysicalAddress failed\n");
        return false;
    }

    // Map uncached so writes hit the framebuffer immediately (no cache stalls the pixels).
    IOMemoryMap *map = md->map(kIOMapInhibitCache);
    if (map == nullptr) {
        IOLog("HonorFB_vpaint: map(kIOMapInhibitCache) failed\n");
        md->release();
        return false;
    }

    volatile uint8_t *fb = reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());

    for (uint32_t y = 0; y < h; y++) {
        uint32_t color;
        if      (y <   h / 4) color = 0xFFFFFFFFu;  // white  (white in ANY 32bpp order)
        else if (y <   h / 2) color = 0x00FF0000u;  // red    in X-R-G-B
        else if (y < 3*h / 4) color = 0x0000FF00u;  // green
        else                  color = 0x000000FFu;  // blue
        volatile uint32_t *row =
            reinterpret_cast<volatile uint32_t *>(fb + static_cast<uint64_t>(y) * rb);
        for (uint32_t x = 0; x < w; x++)
            row[x] = color;
    }

    IOLog("HonorFB_vpaint: painted 4 bands — OK\n");
    // Intentionally keep 'map'/'md' retained: the pixels persist in fb memory regardless,
    // and holding the mapping avoids any teardown racing our writes. Diagnostic build.
    return true;
}
