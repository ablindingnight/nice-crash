// Host SDK: Wii Remote (WPAD/KPAD) and GameCube pad (PAD). Remote 0 is a connected core remote driven by the
// keyboard (first version of milestone 4, docs/porting.md section 7):
//   2 = 2 (confirm / accelerate: the remote is held sideways), 1 = 1 (back / brake), A = A, B = B,
//   = / + = Plus, - = Minus, Enter or Home = HOME (as in Dolphin's default keyboard mapping),
//   arrow keys = D-pad (the remote is held sideways, so the pad is rotated: screen-up is the pad's Right).
//   Tilt: Left/Right arrows also tilt while Shift is held (steering), for early testing.
// NICECRASH_INPUT="<frame>:<button>[,...]" presses buttons on given retrace frames (held for 6 frames), for
// scripted bring-up runs. Buttons: A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT (screen directions).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include "sdk.h"

typedef void (*WPADConnectCallback)(s32 chan, s32 result);
typedef void (*WPADInfoCallback)(s32 chan, s32 result);

#define WPAD_STATE_SETUP 3
#define WPAD_ERR_NONE 0
#define WPAD_ERR_NO_CONTROLLER (-1)

// WPAD button bits (core remote).
enum : u32 {
    BTN_LEFT = 0x0001, BTN_RIGHT = 0x0002, BTN_DOWN = 0x0004, BTN_UP = 0x0008, BTN_PLUS = 0x0010,
    BTN_2 = 0x0100, BTN_1 = 0x0200, BTN_B = 0x0400, BTN_A = 0x0800, BTN_MINUS = 0x1000, BTN_HOME = 0x8000,
};

// The game's view of KPADStatus (rjoy.cpp KStatus, 0x88 bytes).
struct KStatus {
    u32 hold, trig, release;  // 0x0
    f32 accX, accY, accZ;     // 0xC
    u8 pad18[0x5C - 0x18];
    s32 devType;              // 0x5C
    u8 pad60[0x85 - 0x60];
    s8 err;                   // 0x85
    u8 pad86[2];
};
static_assert(sizeof(KStatus) == 0x88, "KStatus layout");

u32 sdk_vi_retrace_count();
bool sdk_vi_has_focus();

static WPADConnectCallback g_connectCb[4];
static u32 g_prevHold;

struct Scripted {
    u32 frame;
    u32 bits;
};
static Scripted g_script[64];
static int g_nscript = -1;

static u32 ButtonBits(const char* name) {
    struct { const char* n; u32 b; } map[] = {
        {"A", BTN_A}, {"B", BTN_B}, {"1", BTN_1}, {"2", BTN_2}, {"PLUS", BTN_PLUS}, {"MINUS", BTN_MINUS},
        {"HOME", BTN_HOME}, {"UP", BTN_RIGHT}, {"DOWN", BTN_LEFT}, {"LEFT", BTN_UP}, {"RIGHT", BTN_DOWN}};
    for (auto& m : map)
        if (!_stricmp(m.n, name)) return m.b;
    return 0;
}

static void LoadScript() {
    g_nscript = 0;
    const char* s = getenv("NICECRASH_INPUT");
    if (!s) return;
    char buf[1024];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char* tok = strtok(buf, ","); tok && g_nscript < 64; tok = strtok(nullptr, ",")) {
        char* c = strchr(tok, ':');
        if (!c) continue;
        *c = 0;
        g_script[g_nscript].frame = (u32)atoi(tok);
        g_script[g_nscript].bits = ButtonBits(c + 1);
        g_nscript++;
    }
}

static bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static u32 CurrentHold(f32* tilt) {
    if (g_nscript < 0) LoadScript();
    u32 hold = 0;
    u32 now = sdk_vi_retrace_count();
    for (int i = 0; i < g_nscript; i++)
        if (now >= g_script[i].frame && now < g_script[i].frame + 6) hold |= g_script[i].bits;
    *tilt = 0;
    if (sdk_vi_has_focus()) {
        bool shift = Key(VK_SHIFT);
        // Dolphin-like keys: the number keys are the remote's 1 and 2; Enter brings up the HOME menu.
        if (Key('2') || Key(VK_NUMPAD2)) hold |= BTN_2;
        if (Key('1') || Key(VK_NUMPAD1)) hold |= BTN_1;
        if (Key(VK_RETURN)) hold |= BTN_HOME;
        if (Key('A')) hold |= BTN_A;
        if (Key('B')) hold |= BTN_B;
        if (Key(VK_OEM_PLUS) || Key(VK_ADD)) hold |= BTN_PLUS;
        if (Key(VK_OEM_MINUS) || Key(VK_SUBTRACT)) hold |= BTN_MINUS;
        if (Key(VK_HOME)) hold |= BTN_HOME;
        if (Key(VK_UP)) hold |= BTN_RIGHT;  // sideways remote: screen up = pad right
        if (Key(VK_DOWN)) hold |= BTN_LEFT;
        if (!shift) {
            if (Key(VK_LEFT)) hold |= BTN_UP;
            if (Key(VK_RIGHT)) hold |= BTN_DOWN;
        } else {
            if (Key(VK_LEFT)) *tilt -= 0.5f;
            if (Key(VK_RIGHT)) *tilt += 0.5f;
        }
    }
    return hold;
}

extern "C" {

void WPADRegisterAllocator(void* (*)(u32), int (*)(void*)) {}
s32 WPADGetStatus(void) { return WPAD_STATE_SETUP; }

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback cb) {
    if (chan < 0 || chan > 3) return nullptr;
    WPADConnectCallback o = g_connectCb[chan];
    g_connectCb[chan] = cb;
    if (cb && chan == 0) cb(0, WPAD_ERR_NONE);  // remote 0 is connected from the start
    return o;
}

s32 WPADProbe(s32 chan, s32* type) {
    if (chan == 0) {
        if (type) *type = 0;  // WPAD_DEV_CORE
        return WPAD_ERR_NONE;
    }
    if (type) *type = 253;  // WPAD_DEV_NOT_FOUND
    return WPAD_ERR_NO_CONTROLLER;
}

s32 WPADGetInfoAsync(s32 chan, void* info, WPADInfoCallback cb) {
    if (chan != 0) return WPAD_ERR_NO_CONTROLLER;
    if (info) memset(info, 0, 0x18);  // WPADInfo: no extension, battery etc. zero
    if (info) ((u8*)info)[0x10] = 4;   // battery level: full
    if (cb) cb(chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}

s32 KPADRead(s32 chan, void* buf, u32 n) {
    if (chan != 0 || !buf || !n) return 0;
    f32 tilt;
    u32 hold = CurrentHold(&tilt);
    KStatus* k = (KStatus*)buf;
    memset(k, 0, sizeof *k);
    if (hold != g_prevHold) sdk_log("KPADRead: hold %04x at retrace %u", hold, sdk_vi_retrace_count());
    k->hold = hold;
    k->trig = hold & ~g_prevHold;
    k->release = g_prevHold & ~hold;
    g_prevHold = hold;
    // Held sideways and level: gravity along the remote's X axis; steering tilts it (game: steer = -2 * accZ).
    k->accX = -1.0f;
    k->accY = 0.0f;
    k->accZ = -tilt;
    k->devType = 0;  // WPAD_DEV_CORE
    k->err = 0;      // WPAD_ERR_NONE
    return 1;
}

void WPADDisconnect(s32) {}
void WPADControlMotor(s32, u32) {}
u8 WPADGetRadioSensitivity(s32) { return 100; }
void __WPADShutdown(void) {}
void __WPADReconnect(void) {}
void KPADDisableDPD(s32) {}

void PADInit(void) {}
void KPADInit(void) {}

}  // extern "C"
