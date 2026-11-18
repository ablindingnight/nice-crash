// Host SDK: the system calls the real HOME Menu (HBM) and the game's HOME paths make, which were unimplemented stubs
// (runtime/unimpl.cpp ends the game on the first one: pressing HOME exited with code 99, 2026-11-17). Found by
// opening and closing HOME with NICECRASH_UNIMPL=continue; ranked by G7's stub audit
// (handoff/dossier/port_support/G7_STUBS_TUTORIAL.md in the matching repo).
//
// The port's audio mixer (sdk/ax.cpp) has no aux buses or effects, so the aux and reverb calls keep what they are
// given and report it back; HOME's sounds play dry, as before. Alarms are recorded but never fire (no caller in
// normal play needs them to). Wii Menu / Reset / power off end the game cleanly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <map>
#include "sdk.h"

extern "C" {

// AI (audio interface): HBM's AxManager panics "not initialized AI" without it.
int AICheckInit(void) { return 1; }

// AX aux buses A/B/C: callbacks and return volumes, kept and handed back.
static void* g_auxCb[3];
static void* g_auxCtx[3];
static u16 g_auxRet[3] = {0x8000, 0x8000, 0x8000};
static void SetAux(int i, void* cb, void* ctx) {
    g_auxCb[i] = cb;
    g_auxCtx[i] = ctx;
}
static void GetAux(int i, void** cb, void** ctx) {
    if (cb) *cb = g_auxCb[i];
    if (ctx) *ctx = g_auxCtx[i];
}
void AXRegisterAuxACallback(void* cb, void* ctx) { SetAux(0, cb, ctx); }
void AXRegisterAuxBCallback(void* cb, void* ctx) { SetAux(1, cb, ctx); }
void AXRegisterAuxCCallback(void* cb, void* ctx) { SetAux(2, cb, ctx); }
void AXGetAuxACallback(void** cb, void** ctx) { GetAux(0, cb, ctx); }
void AXGetAuxBCallback(void** cb, void** ctx) { GetAux(1, cb, ctx); }
void AXGetAuxCCallback(void** cb, void** ctx) { GetAux(2, cb, ctx); }
u16 AXGetAuxAReturnVolume(void) { return g_auxRet[0]; }
u16 AXGetAuxBReturnVolume(void) { return g_auxRet[1]; }
u16 AXGetAuxCReturnVolume(void) { return g_auxRet[2]; }
void AXSetAuxAReturnVolume(u16 v) { g_auxRet[0] = v; }
void AXSetAuxBReturnVolume(u16 v) { g_auxRet[1] = v; }
void AXSetAuxCReturnVolume(u16 v) { g_auxRet[2] = v; }

// Per-voice priority and low-pass filter: the host mixer has neither.
void AXSetVoicePriority(void*, u32) {}
void AXSetVoiceLpf(void*, void*) {}

// AXFX memory hooks and the HOME reverb (dry on the host).
static void* g_fxAlloc;
static void* g_fxFree;
void AXFXSetHooks(void* alloc, void* free) {
    g_fxAlloc = alloc;
    g_fxFree = free;
}
void AXFXGetHooks(void** alloc, void** free) {
    if (alloc) *alloc = g_fxAlloc;
    if (free) *free = g_fxFree;
}
int AXFXReverbHiInit(void*) { return 1; }
void AXFXReverbHiShutdown(void*) {}

// Alarms: user data per alarm; set/cancel recorded, never fired.
static std::map<void*, void*> g_alarmData;
void OSSetAlarmUserData(void* alarm, void* data) { g_alarmData[alarm] = data; }
void* OSGetAlarmUserData(void* alarm) {
    auto it = g_alarmData.find(alarm);
    return it == g_alarmData.end() ? nullptr : it->second;
}
void OSCancelAlarm(void*) {}
void OSSetAlarm(void*, s64, void*) {
    static bool told;
    if (!told) sdk_log("OSSetAlarm: alarms do not fire on the host");
    told = true;
}

// Wii remote settings HOME shows: extension callback, speaker volume, rumble, saving them.
void* WPADSetExtensionCallback(s32, void*) { return nullptr; }
static u8 g_spkVol = 0x40;
u8 WPADGetSpeakerVolume(void) { return g_spkVol; }
void WPADSetSpeakerVolume(u8 v) { g_spkVol = v; }
static int g_motor = 1;
int WPADIsMotorEnabled(void) { return g_motor; }
void WPADEnableMotor(int on) { g_motor = on != 0; }
int WPADSaveConfig(void (*cb)(s32, s32)) {
    if (cb) cb(0, 0);  // WPAD_ERR_NONE
    return 1;
}

// HOME's "Wii Menu" / "Reset" and power off: end the game normally.
static void EndGame(const char* why) {
    sdk_log("%s: closing the game", why);
    fflush(stdout);
    fflush(stderr);
    ExitProcess(0);
}
void OSReturnToMenu(void) { EndGame("OSReturnToMenu"); }
void OSRestart(u32) { EndGame("OSRestart"); }
void OSShutdownSystem(void) { EndGame("OSShutdownSystem"); }

}  // extern "C"
