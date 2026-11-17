// Host SDK: VI (video interface). Owns the game window and the 60 Hz retrace: VIWaitForRetrace interprets the
// frame's GX commands, presents, paces to 59.94 Hz, pumps window messages and runs the retrace callbacks, giving
// the Wii "CPU" to other game threads while it waits (as a thread blocked on retrace does on the Wii).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
#include "gxinterp.h"
#include "xfb.h"
#include "project.h"
#include "sdk.h"
#include "hostcfg.h"
#include <shlobj.h>
#include <string>
#include <vector>
#include "settings.h"
#include "texpack.h"
extern "C" void port_trace(const char* what, unsigned a, unsigned b);  // runtime/trace.cpp (NICECRASH_TRACE)

void sdk_cpu_release();
void sdk_cpu_acquire();
void sdk_ax_tick();
void gxr_drawlog_dump(const char* path);  // sdk/gxrender.cpp (NICECRASH_FLASHDRAWS)

typedef void (*VIRetraceCallback)(u32 retraceCount);

static HWND g_hwnd;
static HDC g_dc;
static HGLRC g_rc;
static int g_w = 640, g_h = 480;
static VIRetraceCallback g_pre, g_post;
static u32 g_retrace;
static u32 g_copyCallsSinceRetrace;  // GXCopyDisp calls since the last retrace (frame pacing)
static u32 g_copiesSinceRetrace;  // display copies since the last retrace (frame pacing, GXFlush)
extern "C" void sdk_vi_note_copy() { g_copiesSinceRetrace++; }  // sdk/gxrender.cpp, at each display copy
static u32 sdk_vi_copies_since_retrace() { return g_copiesSinceRetrace; }
static bool g_black = true;
static void* g_nextFb;
static LARGE_INTEGER g_freq, g_last;

static void ApplyWindowMode(bool windowed);
static bool g_windowed;

void sdk_input_key_down(unsigned vk);  // sdk/input.cpp: a tap between two game frames still counts
bool sdk_input_take_menu_request();    // sdk/input.cpp: Esc, Tab or a Start hold asked for the Nice Crash menu
bool sdk_menu_test_wants_open();
bool sdk_menu_open();       // sdk/menu.cpp (NICECRASH_MENUTEST)
extern "C" void sdk_time_add_pause(long long qpc);  // sdk/os.cpp: the Wii clock stands still while paused
void sdk_menu_begin();
void sdk_menu_end();
bool sdk_menu_frame(int w, int h);
u32 gxi_last_copy_dst();

// game.screenshot_key (F12, F11, PrintScreen or none; read at each press, so a change applies at once): the next
// presented frame is saved as Pictures\Nice Crash\NiceCrash_<date>_<time>.png, the picture as shown in the window.
static volatile bool g_shotWanted;
static unsigned ShotKey() {
    const char* k = Settings_Get("game.screenshot_key");
    if (!k || !_stricmp(k, "F12")) return VK_F12;
    if (!_stricmp(k, "F11")) return VK_F11;
    if (!_stricmp(k, "PrintScreen")) return VK_SNAPSHOT;
    return 0;  // "none"
}
static void SaveScreenshot(int w, int h) {
    g_shotWanted = false;
    if (w <= 0 || h <= 0) return;
    wchar_t pics[MAX_PATH];
    std::wstring dir;
    if (const wchar_t* o = _wgetenv(L"NICECRASH_SHOTDIR")) {  // tests: somewhere other than the player's Pictures
        dir = o;
    } else {
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pics))) return;
        dir = std::wstring(pics) + L"\\Nice Crash";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    std::wstring path;
    for (int n = 0; n < 100; n++) {  // a second shot within the same second gets _2, _3, ...
        swprintf(name, 64, n ? L"\\NiceCrash_%04d-%02d-%02d_%02d-%02d-%02d_%d.png" : L"\\NiceCrash_%04d-%02d-%02d_%02d-%02d-%02d.png",
                 t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, n + 1);
        path = dir + name;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) break;
    }
    std::vector<unsigned char> px((size_t)w * h * 4), rows((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) {  // GL rows start at the bottom
        unsigned char* d = &rows[(size_t)y * w * 4];
        memcpy(d, &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
        for (int x = 0; x < w; x++) d[x * 4 + 3] = 255;  // opaque
    }
    char utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr);
    bool ok = host_save_png(utf8, rows.data(), w, h);
    fprintf(stderr, ok ? "[vi] screenshot saved to %s\n" : "[vi] screenshot failed: %s\n", utf8);
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYUP:  // Print Screen arrives only as a key-up
        if (wp == VK_SNAPSHOT && ShotKey() == VK_SNAPSHOT) g_shotWanted = true;
        break;
    case WM_KEYDOWN:
        if (wp != VK_SNAPSHOT && wp == ShotKey() && !(lp & (1 << 30))) {
            g_shotWanted = true;
            break;
        }
        // A held Esc/Tab auto-repeats: only its first press may open (or, after closing, reopen) the menu.
        if ((wp == VK_ESCAPE || wp == VK_TAB) && (lp & (1 << 30)) && !sdk_menu_open()) break;
        sdk_input_key_down((unsigned)wp);
        break;
    case WM_SYSKEYDOWN:  // Alt+Enter: windowed <-> borderless full screen
        if (wp == VK_RETURN && (HIWORD(lp) & KF_ALTDOWN)) {
            ApplyWindowMode(!g_windowed);
            return 0;
        }
        break;
    case WM_SYSCHAR:  // no beep for Alt+Enter
        if (wp == VK_RETURN) return 0;
        break;
    case WM_SIZE:
        g_w = LOWORD(lp) ? LOWORD(lp) : 1;
        g_h = HIWORD(lp) ? HIWORD(lp) : 1;
        return 0;
    case WM_CLOSE:
        ExitProcess(0);
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

// NICECRASH_GLDEBUG: one driver message (stdcall, as GL calls it on 32-bit Windows).
static void APIENTRY GlDebugMessage(GLenum, GLenum type, GLuint id, GLenum sev, GLsizei, const char* msg, const void*) {
    static int n;
    if (n++ < 200) fprintf(stderr, "[gl] type %04x id %u sev %04x: %s\n", type, id, sev, msg);
    port_trace("gl-debug", type, id);
}

static void CreateGameWindow() {
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));  // assets/icon (title bar, taskbar)
    wc.lpszClassName = "NiceCrashGame";
    RegisterClassA(&wc);
    // The window as the player set it (settings.ini display.*): windowed at its size, or borderless over the
    // whole monitor ("fullscreen" is borderless too: no mode switch). The EFB is rendered at the window's size.
    g_hwnd = CreateWindowA(wc.lpszClassName, PROJECT_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640,
                           480, nullptr, nullptr, wc.hInstance, nullptr);
    ApplyWindowMode(host_cfg().windowMode == 0);
    g_dc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    SetPixelFormat(g_dc, ChoosePixelFormat(g_dc, &pfd), &pfd);
    g_rc = wglCreateContext(g_dc);
    wglMakeCurrent(g_dc, g_rc);
    // NICECRASH_GLDEBUG=1 (debugging): the driver's messages (errors, warnings) on stderr and in the trace ring, as
    // they happen (synchronous), to find what a driver abort follows.
    if (getenv("NICECRASH_GLDEBUG")) {
        typedef void(APIENTRY * DebugProc)(GLenum, GLenum, GLuint, GLenum, GLsizei, const char*, const void*);
        typedef void(APIENTRY * DebugCb)(DebugProc, const void*);
        if (auto cb = (DebugCb)(void*)wglGetProcAddress("glDebugMessageCallback")) {
            glEnable(0x92E0);  // GL_DEBUG_OUTPUT
            glEnable(0x8242);  // GL_DEBUG_OUTPUT_SYNCHRONOUS
            cb(GlDebugMessage, nullptr);
        } else {
            fprintf(stderr, "[gl] no glDebugMessageCallback\n");
        }
    }
    // NICECRASH_NOVSYNC=1: swap without waiting for the monitor (the VI retrace is timed on the performance counter;
    // measured 2026-11-16: no frame-rate difference in the race, so the driver default stays).
    typedef BOOL(WINAPI * SwapIntervalFn)(int);
    // display.vsync (settings.ini; NICECRASH_NOVSYNC=1 also turns it off).
    if (auto si = (SwapIntervalFn)(void*)wglGetProcAddress("wglSwapIntervalEXT"))
        si(getenv("NICECRASH_NOVSYNC") || !host_cfg().vsync ? 0 : 1);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_last);
}

// The monitor display.monitor names (0 = primary), or the primary one.
struct MonitorPick {
    int want, at;
    RECT r;
    bool found;
};
static BOOL CALLBACK PickMonitor(HMONITOR m, HDC, LPRECT, LPARAM p) {
    MonitorPick* k = (MonitorPick*)p;
    if (k->at++ != k->want) return TRUE;
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoA(m, &mi)) {
        k->r = mi.rcMonitor;
        k->found = true;
    }
    return FALSE;
}
static RECT MonitorRect() {
    MonitorPick k{host_cfg().monitor, 0, {}, false};
    if (k.want > 0) EnumDisplayMonitors(nullptr, nullptr, PickMonitor, (LPARAM)&k);
    if (!k.found) {
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoA(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        k.r = mi.rcMonitor;
    }
    return k.r;
}

static void ApplyWindowMode(bool windowed) {
    g_windowed = windowed;
    // NICECRASH_HIDEWINDOW=1: never shown (the launcher's movie frame dump renders off screen).
    // NICECRASH_HIDEWINDOW=offscreen: shown (so it renders and screenshots work) but far off the desktop, never
    // focused: test runs that don't get in the player's way.
    static const char* hideMode = getenv("NICECRASH_HIDEWINDOW");
    if (hideMode && !strcmp(hideMode, "offscreen")) {
        RECT r{0, 0, host_cfg().windowW, host_cfg().windowH};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_BOTTOM, -20000, -20000, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    if (hideMode) {
        SetWindowPos(g_hwnd, nullptr, 0, 0, host_cfg().windowW, host_cfg().windowH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        return;
    }
    RECT mon = MonitorRect();
    if (windowed) {
        const HostCfg& c = host_cfg();
        RECT r{0, 0, c.windowW, c.windowH};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        int w = r.right - r.left, h = r.bottom - r.top;
        int x = c.windowX != INT_MIN ? c.windowX : mon.left + (mon.right - mon.left - w) / 2;
        int y = c.windowY != INT_MIN ? c.windowY : mon.top + (mon.bottom - mon.top - h) / 2;
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, mon.left, mon.top, mon.right - mon.left, mon.bottom - mon.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

static void Pump() {
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

extern "C" {

void VIInit(void) {
    sdk_log("VIInit");
    if (!g_hwnd) CreateGameWindow();
}

void VIConfigure(const void*) {}
void VIFlush(void) {}
void VISetNextFrameBuffer(void* fb) { g_nextFb = fb; }
void VISetBlack(int black) { g_black = black != 0; }
u32 VIGetTvFormat(void) { return 0; }   // VI_NTSC
u32 VIGetDTVStatus(void) { return 0; }  // no component cable
u32 VIGetNextField(void) { return g_retrace & 1; }
u32 VIGetRetraceCount(void) { return g_retrace; }
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback o = g_pre;
    g_pre = cb;
    return o;
}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback o = g_post;
    g_post = cb;
    return o;
}

// A display copy (EFB -> XFB, sdk/gxrender.cpp) finishes a frame: show the back buffer.
// NICECRASH_SHOT=<frame>:<file.bmp>: save the back buffer at the first display copy on or after that retrace
// frame (the same clock as NICECRASH_INPUT; a menu can present many copies per retrace).
// NICECRASH_LUMLOG=<retrace>:<count>: from that retrace on, log the mean brightness of each presented frame (and
// the retrace it belongs to), to find flicker: a flash is a frame far brighter or darker than its neighbours.
static void LumLog() {
    static int start = -2, count;
    if (start == -2) {
        const char* s = getenv("NICECRASH_LUMLOG");
        start = s ? atoi(s) : -1;
        const char* c = s ? strchr(s, ':') : nullptr;
        count = c ? atoi(c + 1) : 120;
    }
    if (start < 0 || (int)g_retrace < start || count <= 0) return;
    count--;
    // One read of the whole frame (per-pixel reads stalled the GPU ~1500 times a frame and slowed the game).
    static unsigned char* px;
    static int cap;
    if (cap < g_w * g_h * 4) {
        free(px);
        cap = g_w * g_h * 4;
        px = (unsigned char*)malloc(cap);
    }
    glReadPixels(0, 0, g_w, g_h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    long sum = 0, n = 0;
    for (int i = 0; i < g_w * g_h; i += 97, n += 3) sum += px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2];
    // Flat green: green well above red and blue, the same colour as the pixel 4 to the right (tree billboards
    // drawn as solid blocks). A frame that jumps above the last one is saved as build/t3/flash_<retrace>.bmp.
    long flat = 0, cnt = 0;
    for (int y = 0; y < g_h; y += 2)
        for (int x = 0; x + 4 < g_w; x += 2, cnt++) {
            const unsigned char* p = px + (y * g_w + x) * 4;
            if (p[1] > p[0] + 30 && p[1] > p[2] + 30 &&
                abs(p[16] - p[0]) + abs(p[17] - p[1]) + abs(p[18] - p[2]) <= 3)
                flat++;
        }
    double green = cnt ? (double)flat / cnt : 0;
    static double lastGreen = -1;
    static int saved;
    bool jump = lastGreen >= 0 && green > lastGreen * 1.5 + 0.005;
    fprintf(stderr, "[lum] retrace %u mean %ld green %.4f%s\n", g_retrace, n ? sum / n : 0, green, jump ? " JUMP" : "");
    if (jump && saved < 8) {
        saved++;
        char path[64];
        snprintf(path, sizeof path, "build/t3/flash_%u.txt", g_retrace);
        gxr_drawlog_dump(path);  // the draws of the frames before (sdk/gxrender.cpp)
        snprintf(path, sizeof path, "build/t3/flash_%u.bmp", g_retrace);
        BITMAPFILEHEADER fh{};
        BITMAPINFOHEADER ih{};
        ih.biSize = sizeof ih; ih.biWidth = g_w; ih.biHeight = g_h; ih.biPlanes = 1; ih.biBitCount = 32;
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + g_w * g_h * 4;
        if (FILE* f = fopen(path, "wb")) {
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&ih, sizeof ih, 1, f);
            for (int i = 0; i < g_w * g_h; i++) {
                unsigned char q[4] = {px[i * 4 + 2], px[i * 4 + 1], px[i * 4], 255};
                fwrite(q, 4, 1, f);
            }
            fclose(f);
        }
    }
    lastGreen = green;
}

static void MaybeShot() {
    // NICECRASH_SHOT may list several shots: "<frame>:<file>[,<frame>:<file>...]", in increasing frame order.
    static int frame = g_retrace;
    static int want = -1;
    static char path[260];
    static const char* next;
    if (want == -1) next = getenv("NICECRASH_SHOT"), want = -2;
    if (want == -2 && next && *next) {
        want = atoi(next);
        const char* c = strchr(next, ':');
        const char* e = strchr(next, ',');
        if (c && (!e || c < e)) {
            int len = (int)(e ? e - c - 1 : strlen(c + 1));
            snprintf(path, sizeof path, "%.*s", len, c + 1);
        } else {
            path[0] = 0;
        }
        next = e ? e + 1 : nullptr;
    }
    frame = (int)g_retrace;
    if (want < 0 || frame < want || !path[0]) return;
    want = -2;  // this one is done; the next listed shot (if any) is parsed on the following call
    int w = g_w, h = g_h, stride = (w * 3 + 3) & ~3;
    unsigned char* px = (unsigned char*)malloc((size_t)stride * h);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, 0x80E0 /* GL_BGR */, GL_UNSIGNED_BYTE, px);
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biSizeImage = stride * h;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    if (FILE* f = fopen(path, "wb")) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&ih, sizeof ih, 1, f);
        fwrite(px, 1, (size_t)stride * h, f);
        fclose(f);
        fprintf(stderr, "[vi] saved frame %d to %s\n", frame, path);
    }
    free(px);
}

// NICECRASH_HEAPCHECK=1 (debugging): validate every Windows heap once per presented frame and report the first frame
// where one is damaged, to find host-side buffer overruns (exit 0xC0000374 only shows up later, at a free).
static void HeapCheck() {
    static const bool on = getenv("NICECRASH_HEAPCHECK") != nullptr;
    static bool reported;
    if (!on || reported) return;
    HANDLE heaps[64];
    DWORD n = GetProcessHeaps(64, heaps);
    for (DWORD i = 0; i < n && i < 64; i++)
        if (!HeapValidate(heaps[i], 0, nullptr)) {
            fprintf(stderr, "[heapcheck] heap %lu (%p) damaged at retrace %u\n", (unsigned long)i, heaps[i], (unsigned)g_retrace);
            reported = true;
        }
}

void sdk_vi_present_now() {
    if (!g_dc) return;
    LumLog();
    MaybeShot();
    SwapBuffers(g_dc);
}
// The EFB's size in pixels: the window's (graphics.internal_scale = window), or 480 x n lines at the window's shape.
void sdk_vi_window_size(int* w, int* h) {
    int s = host_cfg().internalScale;
    if (!s) {
        *w = g_w;
        *h = g_h;
        return;
    }
    *h = 480 * s;
    *w = (int)(480.0 * s * g_w / (g_h ? g_h : 1) + 0.5);
}

static void Present() {
    gxi_flush();  // drain the FIFO: draws land in the EFB, display copies in their XFBs (sdk/xfb.cpp)
    // The TV shows the XFB the game selected (VISetNextFrameBuffer), once per retrace, as the VI does.
    if (xfb_active() && g_dc) {
        // The retrace can land in the middle of the game's frame: draw what is batched and release the game's GL
        // state (TEV program, logic op) so the presentation quad is drawn plainly; the next draw re-applies it all.
        // (Before this, every third race frame came out black: presented while a TEV program and logic op were live.)
        gxr_end_batch();
        glDisable(GL_COLOR_LOGIC_OP);
        u32 addr = g_nextFb ? (u32)(uintptr_t)g_nextFb : gxi_last_copy_dst();
        port_trace("present", addr, g_retrace);
        HeapCheck();
        if (xfb_present(addr, g_black && getenv("NICECRASH_HONOR_BLACK"), g_w, g_h)) {
            LumLog();
            MaybeShot();
            if (g_shotWanted) SaveScreenshot(g_w, g_h);
            SwapBuffers(g_dc);
        }
        xfb_rebind_efb();
    }
    static int frames;
    if (getenv("NICECRASH_GXSTATS") && ++frames % 60 == 0) {
        GXFrameStats s = gxi_take_stats();
        fprintf(stderr, "[gx] 60 frames: %u bytes, %u draws, %u verts, %u DLs, %u copies, bp %u cp %u xf %u, unk %u\n",
                s.bytes, s.draws, s.vertices, s.displayLists, s.copies, s.bpLoads, s.cpLoads, s.xfLoads, s.unknown);
    }
}

static void RunMenu() {
    sdk_menu_begin();
    LARGE_INTEGER t;
    for (;;) {
        QueryPerformanceCounter(&t);
        Pump();
        bool shown = false;
        if (xfb_active() && g_dc) {
            gxr_end_batch();
            glDisable(GL_COLOR_LOGIC_OP);
            xfb_force_next();
            u32 addr = g_nextFb ? (u32)(uintptr_t)g_nextFb : gxi_last_copy_dst();
            xfb_present(addr, false, g_w, g_h);
            shown = sdk_menu_frame(g_w, g_h);
            if (shown && g_shotWanted) SaveScreenshot(g_w, g_h);
            if (shown) SwapBuffers(g_dc);
            xfb_rebind_efb();
        }
        if (!shown) break;
        // ~60 menu frames a second when v-sync doesn't pace the swaps.
        LARGE_INTEGER e;
        QueryPerformanceCounter(&e);
        double ms = (double)(e.QuadPart - t.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
        if (ms < 15.0) Sleep((DWORD)(15.0 - ms));
    }
    sdk_menu_end();
}

// The in-game menu changed display.*: apply the window mode, size and v-sync now.
void sdk_vi_apply_display() {
    if (!g_hwnd) return;
    ApplyWindowMode(host_cfg().windowMode == 0);
    typedef BOOL(WINAPI * SwapIntervalFn)(int);
    if (auto si = (SwapIntervalFn)(void*)wglGetProcAddress("wglSwapIntervalEXT"))
        si(getenv("NICECRASH_NOVSYNC") || !host_cfg().vsync ? 0 : 1);
}

// The VI retrace interrupt: every 1/59.94 s. Called from OSGetTime (polled by every wait in the game) and
// VIWaitForRetrace, on the thread that holds the Wii CPU, like an interrupt arriving between instructions.
void sdk_vi_poll() {
    static bool inside;
    if (!g_hwnd || inside) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double frame = (double)g_freq.QuadPart * (host_cfg().frame60 ? 1000.0 : 1001.0) / 60000.0;  // display.frame_rate
    double since = (double)(now.QuadPart - g_last.QuadPart);
    if (since < frame) return;
    inside = true;
    g_last.QuadPart = since > 4 * frame ? now.QuadPart : g_last.QuadPart + (LONGLONG)frame;
    Pump();
    // game.pause_unfocused (launched runs): the game stops while another window has the focus.
    if (host_cfg().pauseUnfocused && (GetForegroundWindow() != g_hwnd || IsIconic(g_hwnd))) {
        LARGE_INTEGER p0;
        QueryPerformanceCounter(&p0);
        while (GetForegroundWindow() != g_hwnd || IsIconic(g_hwnd)) {
            Pump();
            Sleep(30);
        }
        QueryPerformanceCounter(&g_last);  // no catch-up burst afterwards
        sdk_time_add_pause(g_last.QuadPart - p0.QuadPart);
    }
    // The Nice Crash menu (sdk/menu.cpp): the game thread waits here, at its retrace, while the menu runs over the
    // last picture; audio underruns to silence (AX isn't ticked). The clock restarts afterwards (no catch-up).
    if (sdk_input_take_menu_request() | sdk_menu_test_wants_open()) {
        LARGE_INTEGER p0;
        QueryPerformanceCounter(&p0);
        RunMenu();
        QueryPerformanceCounter(&now);
        g_last = now;
        sdk_time_add_pause(now.QuadPart - p0.QuadPart);
    }
    // graphics.show_fps: the frame rate (display copies per second) in the title bar.
    if (host_cfg().showFps) {
        static u32 frames, lastFrames;
        static LARGE_INTEGER t0;
        frames += g_copiesSinceRetrace ? 1 : 0;
        if (!t0.QuadPart) t0 = now;
        if (now.QuadPart - t0.QuadPart >= g_freq.QuadPart) {
            char t[96];
            snprintf(t, sizeof t, "%s - %u fps", PROJECT_NAME, frames - lastFrames);
            SetWindowTextA(g_hwnd, t);
            lastFrames = frames;
            t0 = now;
        }
    }
    g_retrace++;
    g_copiesSinceRetrace = 0;
    g_copyCallsSinceRetrace = 0;
    if (g_pre) g_pre(g_retrace);
    Present();
    sdk_ax_tick();
    if (g_post) g_post(g_retrace);
    inside = false;
}

void VIWaitForRetrace(void) {
    if (!g_hwnd) CreateGameWindow();
    u32 start = g_retrace;
    while (g_retrace == start) {
        // Give the Wii CPU to other game threads while this one is blocked on the retrace.
        sdk_cpu_release();
        Sleep(1);
        sdk_cpu_acquire();
        sdk_vi_poll();
    }
}

// GX synchronisation the SDK implements with GPU interrupts: on the host the interpreter runs synchronously, so
// "draw done" is true as soon as the FIFO has been interpreted.
void GXDrawDone(void) {
    gxi_flush();
    gxr_resolve_copies();
}
void GXWaitDrawDone(void) {
    gxi_flush();
    gxr_resolve_copies();
}
// Frame pacing: the GPU finishes a frame's display copy no sooner than the next retrace (the game keeps it a frame
// behind with breakpoints, and the VI shows one XFB per retrace). The host interprets the FIFO at once, which let
// menus render ~10 frames per retrace, some of them empty (black flashes). So the first flush after a display copy
// waits for the next retrace, as the game's own waits on the GPU would on the Wii. (Deferring the FIFO to the
// retrace instead deadlocks: the game busy-waits on a draw-sync flag without polling the clock.)
// NICECRASH_NOPACE turns it off.
// Like the SDK's GXFlush, pending ("dirty") GX state is written to the pipe first: inside a display list this is
// how the vertex descriptor gets into the list (GXEndDisplayList -> GXGetCPUFifo -> GXFlush on the Wii).
void __sdk_unused_GXFlush(void);
bool sdk_fifo_capturing();
void GXFlush(void) {
    __sdk_unused_GXFlush();
    if (sdk_fifo_capturing()) return;
    // NICECRASH_FLUSHPACE=1: the old pacing (the first flush after a display copy waited for the retrace; it could
    // land in the middle of the next frame and hold the race at 30 fps).
    static const bool flushPace = getenv("NICECRASH_FLUSHPACE") != nullptr && getenv("NICECRASH_NOPACE") == nullptr;
    if (flushPace && sdk_vi_copies_since_retrace() > 0) VIWaitForRetrace();
    gxi_flush();
}

// Frame pacing: the GPU shows at most one new picture per retrace. A second display copy requested in the same
// retrace interval (the title and menus would otherwise run ~100 frames a retrace) waits for the next retrace;
// a frame that already waited (the race syncs to the retrace itself) is not held back. NICECRASH_NOPACE=1: off.
void __sdk_unused_GXCopyDisp(void* dest, u8 clear);  // GXBool
void GXCopyDisp(void* dest, u8 clear) {
    static const bool pace = getenv("NICECRASH_NOPACE") == nullptr && getenv("NICECRASH_FLUSHPACE") == nullptr;
    if (pace && g_copyCallsSinceRetrace > 0) VIWaitForRetrace();
    g_copyCallsSinceRetrace++;
    __sdk_unused_GXCopyDisp(dest, clear);
}

}  // extern "C"

u32 sdk_vi_retrace_count() { return g_retrace; }

// The mouse as the Wii Remote pointer: its position over the picture as -1..1 (x right, y down), as KPAD reports
// the pointer. False when the window is not focused or the cursor is outside the picture.
bool sdk_vi_pointer(float* x, float* y) {
    if (!g_hwnd || GetForegroundWindow() != g_hwnd) return false;
    POINT p;
    if (!GetCursorPos(&p) || !ScreenToClient(g_hwnd, &p)) return false;
    // The picture: the XFB's 448 of 480 lines, centred (xfb_present), across the whole width.
    float top = g_h * (480.0f - 448.0f) / 2.0f / 480.0f, height = g_h * 448.0f / 480.0f;
    *x = 2.0f * p.x / g_w - 1.0f;
    *y = 2.0f * (p.y - top) / height - 1.0f;
    return *x >= -1.0f && *x <= 1.0f && *y >= -1.0f && *y <= 1.0f;
}
bool sdk_vi_mouse_button() { return g_hwnd && GetForegroundWindow() == g_hwnd && (GetAsyncKeyState(VK_LBUTTON) & 0x8000); }
bool sdk_vi_has_focus() { return g_hwnd && GetForegroundWindow() == g_hwnd; }
void* sdk_vi_hwnd() { return g_hwnd; }

// Direct EFB pixel access. The Wii maps the EFB at 0x08000000 (uncached) for these; the host EFB is an OpenGL
// framebuffer. Peeks answer "far" / black (nothing in front: occlusion tests such as the sun's lens flare see a clear
// sky); pokes are dropped. TODO: read the GL depth/colour attachment when something needs real values.
extern "C" {
void GXPeekZ(u16 x, u16 y, u32* z) {
    (void)x, (void)y;
    if (z) *z = 0x00FFFFFF;
}
void GXPokeZ(u16, u16, u32) {}
void GXPeekARGB(u16 x, u16 y, u32* color) {
    (void)x, (void)y;
    if (color) *color = 0;
}
void GXPokeARGB(u16, u16, u32) {}
}
