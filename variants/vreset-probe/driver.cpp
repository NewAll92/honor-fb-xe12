//
//  HonorFB vreset-probe — PROOF-OF-EXECUTION probe (does our start() run at all?).
//
//  On this headless Honor we have no working output channel: no display (that's the
//  whole problem), emulated NVRAM (ApplePanic log is lost on reset), no serial port, no
//  network in Recovery. So every failure mode collapses to the same "frozen screen" and
//  we cannot tell whether our kext even runs.
//
//  This probe reports through the ONE channel that always works and needs no kernel
//  symbol, framebuffer or NVRAM: it hardware-resets the machine via the PCH Reset
//  Control Register (I/O port 0xCF9) the instant start() is entered. Pure CPU `out`
//  instruction — if control reaches here, the box reboots. Period.
//
//  It matches IOResources so start() runs as early as IOKit matching allows.
//
//  Interpretation (user just watches the Honor):
//    * Honor REBOOTS by itself (logo / boot menu reappears within ~1-2 min)
//        -> our kext runs => kernel reaches IOKit driver matching => the black screen is
//           a framebuffer / IONDRVFramebuffer-binding problem, NOT an early kernel hang.
//           Next: recover the real framebuffer base and drive it.
//    * Honor STAYS FROZEN (no reboot after a couple of minutes)
//        -> our kext never runs => the kernel hangs before IOKit, or OpenCore kext
//           injection into the Recovery KC is failing => display work is premature; fix
//           the kernel boot / injection first.
//
#include <IOKit/IOService.h>
#include <libkern/libkern.h>

class HonorFB_vreset : public IOService {
    OSDeclareDefaultStructors(HonorFB_vreset)
public:
    virtual bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(HonorFB_vreset, IOService)

static inline void honor_outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

bool HonorFB_vreset::start(IOService *provider)
{
    // Do the reset as the very first action — before anything that could fault or block —
    // so this is a clean "did control reach start()?" signal with zero dependencies.
    honor_outb(0x0CF9, 0x02);   // arm
    honor_outb(0x0CF9, 0x06);   // hard reset (RST_CPU | SYS_RST)
    honor_outb(0x0CF9, 0x0E);   // full reset (FULL_RST | RST_CPU | SYS_RST)

    for (;;) { }                // in case the reset is momentarily delayed
    return true;                // unreached
}
