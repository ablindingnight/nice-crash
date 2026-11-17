// Host SDK: Wii Remote (WPAD/KPAD) and GameCube pad (PAD). Remote 1 (chan 0) is always connected and driven by
// the keyboard and pad 1; remotes 2-4 appear when pads 2-4 are plugged in (XInput or DirectInput: see Slot).
// Keyboard (Dolphin-like):
//   2 = 2 (confirm / accelerate: the remote is held sideways), 1 = 1 (back / brake), A = A, B = B,
//   = / + = Plus, - = Minus, Enter or Home = HOME (as in Dolphin's default keyboard mapping),
//   arrow keys = D-pad (the remote is held sideways, so the pad is rotated: screen-up is the pad's Right).
//   Tilt: Left/Right arrows also tilt while Shift is held (steering), for early testing.
// NICECRASH_INPUT="<frame>:<button>[,...]" presses buttons on given retrace frames (held for 6 frames), for
// scripted bring-up runs. Buttons: A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT (screen directions).
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sdk.h"
#include "hostcfg.h"

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
    u32 len = 6;  // frames held ("frame+len:buttons")
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
        const char* plus = strchr(tok, '+');  // "4000+1500:2" holds 2 for 1500 retraces
        g_script[g_nscript].len = plus ? (u32)atoi(plus + 1) : 6;
        g_script[g_nscript].bits = ButtonBits(c + 1);
        g_nscript++;
    }
}

static bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// ---- Pads: XInput and DirectInput (both part of Windows, loaded at run time; nothing to install) ----
// Pad slots 0-3 are the connected XInput pads first, then DirectInput game controllers that are not XInput
// devices (those show up in both APIs). Slot n drives remote n+1, held sideways as Excite Truck wants it:
//   A or right trigger = 2 (accelerate / OK), B or left trigger = 1 (brake / back),
//   left stick = tilt (left/right steers, forward/back leans the truck in the air),
//   D-pad = the remote's D-pad (menus), X or RB = turbo (the D-pad's "up"), Start = +, LB = -, Back = HOME.
// A DirectInput pad is read as an XInput one (buttons by position, see g_diMap). The game's rumble
// (WPADControlMotor) runs XInput pads' motors.
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
enum : WORD { XDUP = 0x1, XDDOWN = 0x2, XDLEFT = 0x4, XDRIGHT = 0x8, XSTART = 0x10, XBACK = 0x20, XLB = 0x100,
              XRB = 0x200, XA = 0x1000, XB = 0x2000, XX = 0x4000, XY = 0x8000 };

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
    sdk_log("XInput %s", ok ? "available" : "not found");
    return ok;
}

// DirectInput 8 (dinput8.dll). GUIDs and the joystick data format are spelled out here (no dxguid/dinput8 libs).
static const GUID kIID_IDirectInput8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID kGuidX = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidY = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidZ = {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidRx = {0xA36D02F4, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidRy = {0xA36D02F5, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidRz = {0xA36D02E3, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidSlider = {0xA36D02E4, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kGuidPOV = {0xA36D02F2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

static DIOBJECTDATAFORMAT g_joyObjs[44];
static DIDATAFORMAT g_joyFormat;
static void BuildJoyFormat() {  // the layout of c_dfDIJoystick: 6 axes, 2 sliders, 4 POVs, 32 buttons -> DIJOYSTATE
    const DWORD axis = 0x80000000 | DIDFT_AXIS | DIDFT_ANYINSTANCE;
    const GUID* axes[8] = {&kGuidX, &kGuidY, &kGuidZ, &kGuidRx, &kGuidRy, &kGuidRz, &kGuidSlider, &kGuidSlider};
    int n = 0;
    for (int i = 0; i < 8; i++) g_joyObjs[n++] = {axes[i], (DWORD)(i * 4), axis, DIDOI_ASPECTPOSITION};
    for (int i = 0; i < 4; i++)
        g_joyObjs[n++] = {&kGuidPOV, (DWORD)(32 + i * 4), 0x80000000 | DIDFT_POV | DIDFT_ANYINSTANCE, 0};
    for (int i = 0; i < 32; i++)
        g_joyObjs[n++] = {nullptr, (DWORD)(48 + i), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0};
    g_joyFormat = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, sizeof(DIJOYSTATE), 44, g_joyObjs};
}

void* sdk_vi_hwnd();
static IDirectInput8A* g_di;
struct DiPad {
    IDirectInputDevice8A* dev;
    GUID inst;
    bool lost;
};
static DiPad g_diPads[4];
static int g_nDi;

static bool LoadDInput() {
    static int ok = -1;
    if (ok >= 0) return ok;
    ok = 0;
    if (getenv("NICECRASH_NODINPUT")) return false;
    HMODULE m = LoadLibraryA("dinput8.dll");
    typedef HRESULT(WINAPI * Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    Create_t create = m ? (Create_t)(void*)GetProcAddress(m, "DirectInput8Create") : nullptr;
    if (create && SUCCEEDED(create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, kIID_IDirectInput8A, (void**)&g_di,
                                   nullptr))) {
        BuildJoyFormat();
        ok = 1;
    }
    sdk_log("DirectInput %s", ok ? "available" : "not found");
    return ok;
}

// XInput devices also enumerate through DirectInput; their device path carries "IG_".
static bool IsXInputDevice(IDirectInputDevice8A* d) {
    DIPROPGUIDANDPATH gp{};
    gp.diph.dwSize = sizeof gp;
    gp.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    gp.diph.dwHow = DIPH_DEVICE;
    if (FAILED(d->GetProperty(DIPROP_GUIDANDPATH, &gp.diph))) return false;
    for (const WCHAR* p = gp.wszPath; p[0] && p[1] && p[2]; p++)
        if ((p[0] == 'I' || p[0] == 'i') && (p[1] == 'G' || p[1] == 'g') && p[2] == '_') return true;
    return false;
}

static BOOL CALLBACK EnumPad(LPCDIDEVICEINSTANCEA inst, LPVOID) {
    if (g_nDi >= 4) return DIENUM_STOP;
    for (int i = 0; i < g_nDi; i++)
        if (IsEqualGUID(g_diPads[i].inst, inst->guidInstance)) return DIENUM_CONTINUE;
    IDirectInputDevice8A* d = nullptr;
    if (FAILED(g_di->CreateDevice(inst->guidInstance, &d, nullptr))) return DIENUM_CONTINUE;
    if (IsXInputDevice(d) || FAILED(d->SetDataFormat(&g_joyFormat))) {
        d->Release();
        return DIENUM_CONTINUE;
    }
    if (HWND hw = (HWND)sdk_vi_hwnd()) d->SetCooperativeLevel(hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    DIPROPRANGE r{};  // every axis -32768..32767, as XInput's sticks
    r.diph.dwSize = sizeof r;
    r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    r.diph.dwHow = DIPH_DEVICE;
    r.lMin = -32768;
    r.lMax = 32767;
    d->SetProperty(DIPROP_RANGE, &r.diph);
    d->Acquire();
    g_diPads[g_nDi++] = {d, inst->guidInstance, false};
    sdk_log("DirectInput pad %d: %s", g_nDi, inst->tszProductName);
    return DIENUM_CONTINUE;
}

// DirectInput button numbers for A B X Y LB RB LT RT Back Start. Default = the common DirectInput layout
// (Logitech D mode, DualShock 4, most generic pads: 1/X/Square, 2/A/Cross, 3/B/Circle, 4/Y/Triangle, then L1 R1
// L2 R2 Select Start). NICECRASH_DIBUTTONS="1,2,0,3,4,5,6,7,8,9" (0-based, in that order) remaps.
static int g_diMap[10] = {1, 2, 0, 3, 4, 5, 6, 7, 8, 9};
static void LoadDiMap() {
    const char* s = getenv("NICECRASH_DIBUTTONS");
    for (int i = 0; s && *s && i < 10; i++) {
        g_diMap[i] = atoi(s);
        s = strchr(s, ',');
        if (s) s++;
    }
}

static bool DiRead(int i, XState* x) {
    DiPad& p = g_diPads[i];
    DIJOYSTATE js;
    p.dev->Poll();
    HRESULT hr = p.dev->GetDeviceState(sizeof js, &js);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        p.dev->Acquire();
        p.dev->Poll();
        hr = p.dev->GetDeviceState(sizeof js, &js);
    }
    if (FAILED(hr)) {
        p.lost = true;
        return false;
    }
    memset(x, 0, sizeof *x);
    static const WORD bits[10] = {XA, XB, XX, XY, XLB, XRB, 0, 0, XBACK, XSTART};
    for (int k = 0; k < 10; k++) {
        int b = g_diMap[k];
        if (b < 0 || b >= 32 || !(js.rgbButtons[b] & 0x80)) continue;
        if (k == 6) x->lt = 255;
        else if (k == 7) x->rt = 255;
        else x->buttons |= bits[k];
    }
    DWORD pov = js.rgdwPOV[0];
    if (LOWORD(pov) != 0xFFFF) {  // hundredths of a degree, clockwise from up
        if (pov >= 31500 || pov <= 4500) x->buttons |= XDUP;
        if (pov >= 4500 && pov <= 13500) x->buttons |= XDRIGHT;
        if (pov >= 13500 && pov <= 22500) x->buttons |= XDDOWN;
        if (pov >= 22500 && pov <= 31500) x->buttons |= XDLEFT;
    }
    auto clampS = [](LONG v) { return (SHORT)(v < -32767 ? -32767 : v > 32767 ? 32767 : v); };
    x->lx = clampS(js.lX);
    x->ly = clampS(-js.lY);  // DirectInput Y grows downwards
    x->rx = clampS(js.lZ);   // the right stick is Z/Rz on most DirectInput pads
    x->ry = clampS(-js.lRz);
    return true;
}

// Pad thread: finding pads is slow (DirectInput enumeration takes hundreds of milliseconds on some PCs, and
// XInputGetState on an absent pad is slow too), so a background thread rebuilds the slot table about once a second
// and polls the pads every few milliseconds; the game thread only copies the latest states.
struct Slot {
    int kind;  // 0 none, 1 XInput, 2 DirectInput
    int index;
};
static Slot g_slots[4];             // pad thread's table
static Slot g_slotsPub[4];          // published copy (under g_padLock)
static XState g_padState[4];        // latest states (under g_padLock)
static bool g_padOn[4];
static CRITICAL_SECTION g_padLock;

static void RefreshSlots() {
    LARGE_INTEGER t0, t1, fq;
    QueryPerformanceCounter(&t0);
    int n = 0;
    if (LoadXInput()) {
        for (int i = 0; i < 4; i++) {
            XState s;
            if (p_XInputGetState((DWORD)i, &s) == 0) g_slots[n++] = {1, i};
        }
    }
    if (LoadDInput()) {
        for (int i = 0; i < g_nDi;) {  // drop unplugged devices
            if (g_diPads[i].lost) {
                sdk_log("DirectInput pad %d gone", i + 1);
                g_diPads[i].dev->Unacquire();
                g_diPads[i].dev->Release();
                g_diPads[i] = g_diPads[--g_nDi];
            } else {
                i++;
            }
        }
        g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumPad, nullptr, DIEDFL_ATTACHEDONLY);
        for (int i = 0; i < g_nDi && n < 4; i++) g_slots[n++] = {2, i};
    }
    while (n < 4) g_slots[n++] = {0, 0};
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&fq);
    static int logged;
    if (logged++ < 2)
        sdk_log("pad slots refreshed in %.2f ms: %d/%d/%d/%d", (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart,
                g_slots[0].kind, g_slots[1].kind, g_slots[2].kind, g_slots[3].kind);
}

static DWORD WINAPI PadThread(LPVOID) {
    LoadDiMap();
    DWORD lastScan = 0;
    bool scanned = false;
    for (;;) {
        DWORD now = GetTickCount();
        if (!scanned || now - lastScan >= 1000) {
            RefreshSlots();
            lastScan = GetTickCount();
            scanned = true;
        }
        XState st[4];
        bool on[4];
        bool rescan = false;
        for (int c = 0; c < 4; c++) {
            memset(&st[c], 0, sizeof st[c]);
            on[c] = false;
            if (g_slots[c].kind == 1) {
                on[c] = p_XInputGetState((DWORD)g_slots[c].index, &st[c]) == 0;
                if (!on[c]) rescan = true;  // unplugged: renumber now
            } else if (g_slots[c].kind == 2) {
                on[c] = DiRead(g_slots[c].index, &st[c]);
                if (!on[c]) rescan = true;
            }
        }
        EnterCriticalSection(&g_padLock);
        memcpy(g_padState, st, sizeof st);
        memcpy(g_padOn, on, sizeof on);
        memcpy(g_slotsPub, g_slots, sizeof g_slots);
        LeaveCriticalSection(&g_padLock);
        if (rescan) scanned = false;
        Sleep(2);
    }
    return 0;
}

static bool PadState(int chan, XState* s) {
    static bool started;
    if (!started) {
        started = true;
        InitializeCriticalSection(&g_padLock);
        if (HANDLE h = CreateThread(nullptr, 0, PadThread, nullptr, 0, nullptr)) CloseHandle(h);
    }
    memset(s, 0, sizeof *s);
    if (chan < 0 || chan > 3) return false;
    EnterCriticalSection(&g_padLock);
    bool on = g_padOn[chan];
    if (on) *s = g_padState[chan];
    LeaveCriticalSection(&g_padLock);
    return on;
}

static f32 Axis(SHORT v, int dead) {
    // controls.deadzone (percent of the travel) replaces XInput's suggested values for the steering stick.
    dead = 32767 * host_cfg().deadzone / 100;
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
    enum { DUP = XDUP, DDOWN = XDDOWN, DLEFT = XDLEFT, DRIGHT = XDRIGHT, START = XSTART, BACK = XBACK, LB = XLB,
           RB = XRB };
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
    if (host_cfg().invertSteer) *steer = -*steer;  // controls.invert_steer
    *lean = Axis(s.ly, 7849);
    return hold;
}

static u32 CurrentHold(f32* tilt) {
    if (g_nscript < 0) LoadScript();
    u32 hold = 0;
    u32 now = sdk_vi_retrace_count();
    for (int i = 0; i < g_nscript; i++)
        if (now >= g_script[i].frame && now < g_script[i].frame + g_script[i].len) hold |= g_script[i].bits;
    *tilt = 0;
    if (sdk_vi_has_focus()) {
        bool shift = Key(VK_SHIFT);
        // The remote's 1 and 2: the number keys (Dolphin-like), and . and / for the right hand while WASD steers.
        // Enter brings up the HOME menu; Space is the remote's A, the B key its B.
        if (Key('2') || Key(VK_NUMPAD2) || Key(VK_OEM_2 /* / */)) hold |= BTN_2;
        if (Key('1') || Key(VK_NUMPAD1) || Key(VK_OEM_PERIOD)) hold |= BTN_1;
        if (Key(VK_RETURN)) hold |= BTN_HOME;
        if (Key(VK_SPACE)) hold |= BTN_A;
        if (Key('B')) hold |= BTN_B;
        if (Key(VK_OEM_PLUS) || Key(VK_ADD)) hold |= BTN_PLUS;
        if (Key(VK_OEM_MINUS) || Key(VK_SUBTRACT)) hold |= BTN_MINUS;
        if (Key(VK_HOME)) hold |= BTN_HOME;
        // Directions: the arrows or WASD (screen directions; the remote is held sideways, so screen up = pad right).
        if (Key(VK_UP) || Key('W')) hold |= BTN_RIGHT;  // up = turbo in a race
        if (Key(VK_DOWN) || Key('S')) hold |= BTN_LEFT;
        // Left/Right steer (the remote's tilt; KPADRead doubles *tilt, so 0.5 = full lock) and are also the D-pad
        // for menus (the races don't use those D-pad directions). The steering ramps in over ~0.15 s and back out,
        // so a tap is a small correction; Shift halves it.
        bool l = Key(VK_LEFT) || Key('A'), r = Key(VK_RIGHT) || Key('D');
        if (l) hold |= BTN_UP;
        if (r) hold |= BTN_DOWN;
        static f32 steer;
        static u32 lastFrame;
        f32 want = (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
        f32 step = 0.11f * (f32)(now != lastFrame ? now - lastFrame : 0);
        if (step > 1.0f) step = 1.0f;
        lastFrame = now;
        steer = want > steer ? (steer + step < want ? steer + step : want) : (steer - step > want ? steer - step : want);
        *tilt = (shift ? 0.25f : 0.5f) * steer;
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

// NICECRASH_FPTRAP=<retrace frame>: NaN hunting (runtime/crash.cpp): re-arm the trap once per frame from then on.
extern "C" void port_fptrap_rearm();
static void FpTrapCheck() {
    static int want = -2;
    if (want == -2) {
        const char* s = getenv("NICECRASH_FPTRAP");
        want = s ? atoi(s) : -1;
    }
    if (want >= 0 && (int)sdk_vi_retrace_count() >= want) port_fptrap_rearm();
}

s32 KPADRead(s32 chan, void* buf, u32 n) {
    if (chan == 0) FpTrapCheck();
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
    if (chan < 0 || chan > 3) return;
    if (on && !host_cfg().rumble) on = 0;  // controls.rumble
    XState unused;
    PadState(chan, &unused);  // starts the pad thread
    EnterCriticalSection(&g_padLock);
    Slot sl = g_slotsPub[chan];
    LeaveCriticalSection(&g_padLock);
    if (sl.kind != 1 || !p_XInputSetState) return;  // DirectInput pads: no rumble yet
    XVibration v{on ? (WORD)36000 : (WORD)0, on ? (WORD)36000 : (WORD)0};
    p_XInputSetState((DWORD)sl.index, &v);
}
u8 WPADGetRadioSensitivity(s32) { return 100; }
void __WPADShutdown(void) {}
void __WPADReconnect(void) {}
void KPADEnableDPD(s32) {}   // the pointer is always on (the mouse)
void KPADDisableDPD(s32) {}

void PADInit(void) {}
void KPADInit(void) {}

}  // extern "C"
