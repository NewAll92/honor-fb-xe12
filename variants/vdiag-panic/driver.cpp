//
//  HonorFB vdiag-panic — DIAGNOSTIC ONLY (does NOT try to light the display).
//
//  Problem: the Honor boots headless (no screen), so we cannot read IOLog / ioreg to
//  find out (a) whether our kext's start() even runs and (b) what boot framebuffer XNU
//  handed us in PE_state.video. This variant extracts that information from a headless
//  machine.
//
//  It matches IOResources so start() is GUARANTEED to run early at boot (no dependency
//  on an ACPI/display personality match). It reads every PE_state.video field, IOLogs
//  them, then DELIBERATELY panic()s with those values embedded in the panic string.
//  With ApplePanic=true in config.plist, OpenCore writes the panic to a file on the ESP
//  on the next boot — so we can read, from the USB, exactly what happened. One boot =
//  real data instead of another blind black-screen test.
//
//  Expected outcomes:
//    * panic file appears with "init=1 base=0x... w=.. h=.. depth=.. rowBytes=.." ->
//      our code runs and the boot framebuffer is valid; the display failure is purely in
//      the IONDRVFramebuffer binding (pursue v0/v1/v2 / a direct IOFramebuffer subclass).
//    * panic file appears with base=0x0 -> XNU never recorded the GOP fb (OpenCore
//      ProvideConsoleGop / console handoff issue) -> no republish can ever work as-is.
//    * NO panic file appears -> either start() never runs (IOResources match failing =
//      very unlikely) or this firmware does not persist the ApplePanic NVRAM variable.
//
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>          // PE_state / PE_Video
#include <libkern/libkern.h>

// Declared here so we do not depend on a specific kernel header being present in the SDK.
extern "C" void panic(const char *string, ...) __attribute__((noreturn));

class HonorFB_vdiag : public IOService {
    OSDeclareDefaultStructors(HonorFB_vdiag)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB_vdiag, IOService)

bool HonorFB_vdiag::start(IOService *provider)
{
    IOService::start(provider);

    int                init = PE_state.initialized ? 1 : 0;
    unsigned long long base = static_cast<unsigned long long>(PE_state.video.v_baseAddr);
    unsigned long      w    = static_cast<unsigned long>(PE_state.video.v_width);
    unsigned long      h    = static_cast<unsigned long>(PE_state.video.v_height);
    unsigned long      dep  = static_cast<unsigned long>(PE_state.video.v_depth);
    unsigned long      rb   = static_cast<unsigned long>(PE_state.video.v_rowBytes);

    IOLog("HonorFB_vdiag: init=%d base=0x%llx %lux%lu depth=%lu rowBytes=%lu\n",
          init, base, w, h, dep, rb);

    // Deliberate diagnostic panic so ApplePanic persists these values to the ESP.
    panic("HonorFB_vdiag DIAG init=%d base=0x%llx w=%lu h=%lu depth=%lu rowBytes=%lu",
          init, base, w, h, dep, rb);

    return true; // unreached
}
