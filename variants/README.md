# HonorFB framebuffer variants

Goal: give macOS a visible screen on the **Honor MagicBook X16 (i5-12450H, Intel Xe
Gen12 iGPU)**, which has **no macOS accelerated driver**. Each variant republishes the
boot GOP framebuffer (that OpenCore lit and XNU recorded in `PE_state.video`) so that
Apple's in-kernel `IONDRVFramebuffer` / `IOBootNDRV` can scan it out to WindowServer in
software (no Metal, no acceleration).

We cannot fully test the *binding* until bare-metal (a VM has its own emulated GPU, so
there is no black-screen scenario to reproduce). So we ship several variants that each
attack a different failure hypothesis, and we try them one at a time on the Honor.

| Variant | Matches | Mechanism | Hypothesis it tests |
|---------|---------|-----------|---------------------|
| **v0-baseline** | `AppleACPIPlatformExpert`, name `display` | Faithful clone of acidanthera/UEFIGraphicsFB: sets `deviceMemory` to the boot fb, `registerService()`. | The reference approach just works. |
| **v1-devicetree** | `AppleACPIPlatformExpert`, name `display` | v0 **plus** attaches the nub into `gIODTPlane`, sets `device_type=display` + `AAPL,boot-display`, masks low bits of the fb base. | v0 fails because the nub isn't a device-tree "display" node. |
| **v2-ioresources** | `IOResources` (always present) | Guaranteed to run; **synthesizes** a fresh `IOPlatformDevice` named `display` with the fb memory, registered in the IOService + DeviceTree planes. | v0/v1 never run because the ACPI personality match isn't triggered. |

All three build to real `MH_KEXT_BUNDLE` kexts via `.github/workflows/build-kext.yml`
(GitHub macOS runner). Artifacts: `HonorFB-v0-baseline.kext`, `HonorFB-v1-devicetree.kext`,
`HonorFB-v2-ioresources.kext`.

## Prerequisites on the Honor (OpenCore)

The real iGPU must be **neutralised** so no half-loaded Intel driver fights for the
framebuffer and throws WindowServer into a relaunch loop. In `config.plist`
`DeviceProperties` for the iGPU (`PciRoot(0x0)/Pci(0x2,0x0)`):

- `disable-gpu` / class-code override so `AppleIntelFramebuffer` never attaches, **or**
- boot-arg `-igfxvesa` (variant A EFI) to keep everything on the GOP framebuffer.

Keep `SecureBootModel=Disabled` and SIP off (`csr-active-config=03000000`) while testing
so unsigned kexts load.

## How to test a variant (one at a time)

1. Put exactly **one** `HonorFB-vX.kext` in `EFI/OC/Kexts/` and add it to `Kernel > Add`
   in `config.plist` (or `kmutil load -p HonorFB-vX.kext` from a booted/rescue system).
   Never install two at once — they all publish a `display` nub and would collide.
2. Boot `-v keepsyms=1 debug=0x100`. Watch for the variant's `IOLog` line
   (`HonorFB_vX: ... OK`) and, crucially, whether the internal panel lights up.
3. If it stays black, capture `ioreg -lw0 -c IONDRVFramebuffer` and the boot log over
   SSH/serial, then try the next variant.

Success = the internal display shows the macOS UI (or at least a lit framebuffer that
WindowServer draws into).
