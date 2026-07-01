//
//  kmod_info.c
//  Manual kmod bookkeeping for a cross-built IOKit kext (no Xcode = we declare it ourselves).
//
//  For an IOKit C++ kext, the actual module start/stop is handled by the IOKit runtime
//  (the OSMetaClass machinery in OSDefineMetaClassAndStructors), so _realmain/_antimain are
//  left NULL. We still must emit the kmod_info structure with the generic IOKit _start/_stop.
//
//  The bundle id below MUST match CFBundleIdentifier in Info.plist.
//
#include <mach/mach_types.h>
#include <libkern/libkern.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

KMOD_EXPLICIT_DECL(com.honor.HonorFB, "1.0.0", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t  *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
