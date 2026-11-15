// Host SDK: Wii Remote (WPAD/KPAD) and GameCube pad (PAD). For now the Bluetooth stack reports "set up" with
// no remotes connected; milestone 4 maps keyboard/gamepad to a remote here (docs/porting.md section 7).
#include "sdk.h"

typedef void (*WPADConnectCallback)(s32 chan, s32 result);
typedef void (*WPADInfoCallback)(s32 chan, s32 result);

#define WPAD_STATE_SETUP 3
#define WPAD_ERR_NO_CONTROLLER (-1)

extern "C" {

void WPADRegisterAllocator(void* (*)(u32), int (*)(void*)) {}
s32 WPADGetStatus(void) { return WPAD_STATE_SETUP; }
WPADConnectCallback WPADSetConnectCallback(s32, WPADConnectCallback) { return nullptr; }
s32 WPADProbe(s32, s32* type) {
    if (type) *type = 253;  // WPAD_DEV_NOT_FOUND
    return WPAD_ERR_NO_CONTROLLER;
}
void WPADDisconnect(s32) {}
void WPADControlMotor(s32, u32) {}
u8 WPADGetRadioSensitivity(s32) { return 0; }
void __WPADShutdown(void) {}
void __WPADReconnect(void) {}

void PADInit(void) {}
void KPADInit(void) {}

}  // extern "C"
