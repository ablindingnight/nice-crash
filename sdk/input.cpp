// Host SDK: Wii Remote (WPAD/KPAD) and GameCube pad (PAD). Remote 1 (chan 0) is always connected and driven by
// the keyboard and pad 1; remotes 2-4 appear when XInput pads 2-4 are plugged in (pad mapping: see PadHold).
// Keyboard (Dolphin-like):
//   2 = 2 (confirm / accelerate: the remote is held sideways), 1 = 1 (back / brake), A = A, B = B,
//   = / + = Plus, - = Minus, Enter or Home = HOME (as in Dolphin's default keyboard mapping),
//   arrow keys = D-pad (the remote is held sideways, so the pad is rotated: screen-up is the pad's Right).
//   Tilt: Left/Right arrows also tilt while Shift is held (steering), for early testing.
// NICECRASH_INPUT="<frame>:<button>[,...]" presses buttons on given retrace frames (held for 6 frames), for
// scripted bring-up runs. Buttons: A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT (screen directions).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
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
    // Script-only stick bits (never reach the game).
    STICK_UP = 0x10000000, STICK_DOWN = 0x20000000, STICK_LEFT = 0x40000000, STICK_RIGHT = 0x80000000,
};

// The game's view of KPADStatus (rjoy.cpp KStatus, 0x88 bytes).
struct KStatus {
    u32 hold, trig, release;  // 0x0
    f32 accX, accY, accZ;     // 0xC
    u8 pad18[0x20 - 0x18];
    f32 posX, posY;           // 0x20: the pointer, -1..1 (x right, y down)
    u8 pad28[0x5C - 0x28];
    s32 devType;              // 0x5C
    u8 pad60[0x84 - 0x60];
    s8 dpdValid;              // 0x84: > 0 when the pointer is on screen
    s8 err;                   // 0x85
    u8 pad86[2];
};
static_assert(sizeof(KStatus) == 0x88, "KStatus layout");

u32 sdk_vi_retrace_count();
bool sdk_vi_has_focus();
bool sdk_vi_pointer(float* x, float* y);
bool sdk_vi_mouse_button();
extern "C" u8 fn_802ECC10(void);  // game/homebtn.cpp: the HOME menu is up

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
        {"HOME", BTN_HOME}, {"STICKUP", STICK_UP}, {"STICKDOWN", STICK_DOWN}, {"STICKLEFT", STICK_LEFT},
        {"STICKRIGHT", STICK_RIGHT}, {"UP", BTN_RIGHT}, {"DOWN", BTN_LEFT}, {"LEFT", BTN_UP}, {"RIGHT", BTN_DOWN}};
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

// ---- XInput pads (part of Windows: xinput1_4.dll, loaded at run time; nothing to install) ----
// Pad n drives remote n, held sideways as Excite Truck wants it:
//   A or right trigger = 2 (accelerate / OK), B or left trigger = 1 (brake / back),
//   left stick = tilt (left/right steers, forward/back leans the truck in the air),
//   D-pad = the remote's D-pad (menus), X or RB = turbo (the D-pad's "up"), Start = +, LB = -, Back = HOME.
// The game's rumble (WPADControlMotor) runs the pad's motors.
struct XState {
    DWORD packet;
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
};
struct XVibration {
    WORD left, right;
};
typedef DWORD(WINAPI* XGetState)(DWORD, XState*);
typedef DWORD(WINAPI* XSetState)(DWORD, XVibration*);
static XGetState p_XInputGetState;
static XSetState p_XInputSetState;

static bool LoadXInput() {
    static int ok = -1;
    if (ok >= 0) return ok;
    HMODULE m = LoadLibraryA("xinput1_4.dll");
    if (!m) m = LoadLibraryA("xinput9_1_0.dll");
    if (m) {
        p_XInputGetState = (XGetState)(void*)GetProcAddress(m, "XInputGetState");
        p_XInputSetState = (XSetState)(void*)GetProcAddress(m, "XInputSetState");
    }
    ok = p_XInputGetState != nullptr;
    sdk_log("XInput %s", ok ? "available" : "not found: keyboard only");
    return ok;
}

static bool PadState(int chan, XState* s) {
    if (!LoadXInput()) return false;
    memset(s, 0, sizeof *s);
    return p_XInputGetState((DWORD)chan, s) == 0;  // ERROR_SUCCESS: a pad is connected
}

static f32 Axis(SHORT v, int dead) {
    int a = v < 0 ? -v : v;
    if (a <= dead) return 0;
    f32 t = (f32)(a - dead) / (32767.0f - dead);
    if (t > 1) t = 1;
    return v < 0 ? -t : t;
}

// Buttons and the stick of pad `chan` as remote bits and a tilt (steer: -1..1 right; lean: -1..1 back/forward).
static u32 PadHold(int chan, f32* steer, f32* lean) {
    XState s;
    *steer = *lean = 0;
    if (!PadState(chan, &s)) return 0;
    enum { DUP = 0x1, DDOWN = 0x2, DLEFT = 0x4, DRIGHT = 0x8, START = 0x10, BACK = 0x20, LB = 0x100, RB = 0x200,
           XA = 0x1000, XB = 0x2000, XX = 0x4000, XY = 0x8000 };
    u32 hold = 0;
    WORD b = s.buttons;
    if ((b & XA) || s.rt > 64) hold |= BTN_2;
    if ((b & XB) || s.lt > 64) hold |= BTN_1;
    if (b & START) hold |= BTN_PLUS;
    if (b & LB) hold |= BTN_MINUS;
    if (b & BACK) hold |= BTN_HOME;
    // The remote is held sideways: screen-up is the pad's Right (as for the keyboard's arrows).
    if (b & DUP) hold |= BTN_RIGHT;
    if (b & DDOWN) hold |= BTN_LEFT;
    if (b & DLEFT) hold |= BTN_UP;
    if (b & DRIGHT) hold |= BTN_DOWN;
    if ((b & XX) || (b & RB)) hold |= BTN_RIGHT;  // turbo
    *steer = Axis(s.lx, 7849);
    *lean = Axis(s.ly, 7849);
    return hold;
}

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
    XState s;
    if (chan == 0 || (chan > 0 && chan < 4 && PadState(chan, &s))) {  // remote 1 always (keyboard); 2-4 with a pad
        if (type) *type = 0;  // WPAD_DEV_CORE
        return WPAD_ERR_NONE;
    }
    if (type) *type = 253;  // WPAD_DEV_NOT_FOUND
    return WPAD_ERR_NO_CONTROLLER;
}

s32 WPADGetInfoAsync(s32 chan, void* info, WPADInfoCallback cb) {
    XState s;
    if (chan != 0 && !(chan > 0 && chan < 4 && PadState(chan, &s))) return WPAD_ERR_NO_CONTROLLER;
    if (info) memset(info, 0, 0x18);  // WPADInfo: no extension, battery etc. zero
    if (info) {  // WPADInfo: battery level 0x14 (4 = full), player LED 0x15
        ((u8*)info)[0x14] = 4;
        ((u8*)info)[0x15] = (u8)(1 << chan);
    }
    if (cb) cb(chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}

s32 KPADRead(s32 chan, void* buf, u32 n) {
    if (chan < 0 || chan > 3 || !buf || !n) return 0;
    f32 steer = 0, lean = 0;
    u32 hold = 0;
    if (chan == 0) {  // the keyboard (and the bring-up script) drive remote 1, together with pad 1
        f32 tilt;
        hold = CurrentHold(&tilt);
        steer = tilt * 2.0f;
    }
    f32 ps, pl;
    if (sdk_vi_has_focus() || chan != 0) hold |= PadHold(chan, &ps, &pl);
    else ps = pl = 0;
    if (ps != 0) steer = ps;
    lean = pl;
    // Scripted stick (NICECRASH_INPUT STICKUP/STICKDOWN/STICKLEFT/STICKRIGHT): full deflection, for tests.
    if (hold & STICK_UP) lean = 1;
    if (hold & STICK_DOWN) lean = -1;
    if (hold & STICK_LEFT) steer = -1;
    if (hold & STICK_RIGHT) steer = 1;
    hold &= ~(u32)(STICK_UP | STICK_DOWN | STICK_LEFT | STICK_RIGHT);
    // The pointer (the HOME menu points and presses A). Each pad's right stick moves its remote's pointer; on
    // remote 1 the mouse does too, and whichever moved last wins. A left click is A; in the HOME menu the pad's A
    // is the remote's A as well (elsewhere it stays button 2).
    static f32 padX[4], padY[4];
    static bool padPointing[4], mouseLast;
    static float lastMouseX = -9, lastMouseY = -9;
    f32 px = 0, py = 0;
    bool pointing = false;
    XState pad;
    bool havePad = (sdk_vi_has_focus() || chan != 0) && PadState(chan, &pad);
    if (havePad) {
        f32 rx = Axis(pad.rx, 8689), ry = Axis(pad.ry, 8689);
        if (fn_802ECC10()) {  // in the HOME menu the left stick and the D-pad move the pointer too
            if (rx == 0 && ry == 0) {
                rx = Axis(pad.lx, 7849);
                ry = Axis(pad.ly, 7849);
            }
            if (pad.buttons & 0x4) rx = -1;  // XINPUT_GAMEPAD_DPAD_LEFT
            if (pad.buttons & 0x8) rx = 1;   // RIGHT
            if (pad.buttons & 0x1) ry = 1;   // UP
            if (pad.buttons & 0x2) ry = -1;  // DOWN
        }
        if (rx != 0 || ry != 0) {
            const f32 speed = 0.035f;  // full deflection crosses the screen in about a second
            padX[chan] = fminf(1.0f, fmaxf(-1.0f, padX[chan] + rx * speed));
            padY[chan] = fminf(1.0f, fmaxf(-1.0f, padY[chan] - ry * speed));  // stick up = pointer up
            padPointing[chan] = true;
            if (chan == 0) mouseLast = false;
        }
        if (fn_802ECC10() && (pad.buttons & 0x1000)) hold |= BTN_A;  // XINPUT_GAMEPAD_A
    }
    if (chan == 0) {
        f32 mx, my;
        if (sdk_vi_pointer(&mx, &my)) {
            if (mx != lastMouseX || my != lastMouseY) mouseLast = true;
            lastMouseX = mx;
            lastMouseY = my;
            if (mouseLast || !padPointing[0]) {
                px = mx;
                py = my;
                pointing = true;
            }
        }
        if (sdk_vi_mouse_button()) hold |= BTN_A;
    }
    if (!pointing && padPointing[chan] && havePad) {
        px = padX[chan];
        py = padY[chan];
        pointing = true;
    }
    XState probe;
    if (chan != 0 && !PadState(chan, &probe)) return 0;  // no pad: no remote
    static u32 prev[4];
    KStatus* k = (KStatus*)buf;
    memset(k, 0, sizeof *k);
    if (hold != prev[chan]) sdk_log("KPADRead %d: hold %04x at retrace %u", chan, hold, sdk_vi_retrace_count());
    k->hold = hold;
    k->trig = hold & ~prev[chan];
    k->release = prev[chan] & ~hold;
    prev[chan] = hold;
    if (chan == 0) g_prevHold = hold;
    // The remote held sideways and level reads gravity on -Y (the game: steer = -2 * accZ, lean from atan2(accX,
    // -accY), 0 when level). Steering rolls gravity into Z (full lock at accZ = -/+0.5), leaning pitches it into X
    // (up to 67.5 degrees: full stick = full lean).
    f32 roll = 0.5f * steer, pitch = 1.178097f * lean;  // full lean = 67.5 degrees, where rjoy.cpp clamps throttle to 1
    k->accZ = -roll;
    k->accX = sinf(pitch);
    k->accY = -sqrtf(fmaxf(0.0f, 1.0f - roll * roll)) * cosf(pitch);
    k->devType = 0;  // WPAD_DEV_CORE
    k->err = 0;      // WPAD_ERR_NONE
    k->posX = px;
    k->posY = py;
    k->dpdValid = pointing ? 1 : 0;
    return 1;
}

void WPADDisconnect(s32) {}
// The game's rumble on remote `chan` -> that pad's motors.
void WPADControlMotor(s32 chan, u32 on) {
    if (chan < 0 || chan > 3 || !LoadXInput() || !p_XInputSetState) return;
    XVibration v{on ? (WORD)36000 : (WORD)0, on ? (WORD)36000 : (WORD)0};
    p_XInputSetState((DWORD)chan, &v);
}
u8 WPADGetRadioSensitivity(s32) { return 100; }
void __WPADShutdown(void) {}
void __WPADReconnect(void) {}
void KPADEnableDPD(s32) {}   // the pointer is always on (the mouse)
void KPADDisableDPD(s32) {}

void PADInit(void) {}
void KPADInit(void) {}

}  // extern "C"
