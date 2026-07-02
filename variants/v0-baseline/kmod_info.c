//
//  kmod bookkeeping for HonorFB v0. Bundle id MUST match CFBundleIdentifier in Info.plist.
//
#include <mach/mach_types.h>
#include <libkern/libkern.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

KMOD_EXPLICIT_DECL(com.honor.HonorFB.v0, "1.0.0", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t  *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
