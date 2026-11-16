// Host SDK: VI (video interface). Owns the game window and the 60 Hz retrace: VIWaitForRetrace interprets the
// frame's GX commands, presents, paces to 59.94 Hz, pumps window messages and runs the retrace callbacks, giving
// the Wii "CPU" to other game threads while it waits (as a thread blocked on retrace does on the Wii).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "gxinterp.h"
#include "xfb.h"
#include "project.h"
#include "sdk.h"

void sdk_cpu_release();
void sdk_cpu_acquire();
void sdk_ax_tick();

typedef void (*VIRetraceCallback)(u32 retraceCount);

static HWND g_hwnd;
static HDC g_dc;
static HGLRC g_rc;
static int g_w = 640, g_h = 480;
static VIRetraceCallback g_pre, g_post;
static u32 g_retrace;
static u32 g_copiesSinceRetrace;  // display copies since the last retrace (frame pacing, GXFlush)
extern "C" void sdk_vi_note_copy() { g_copiesSinceRetrace++; }  // sdk/gxrender.cpp, at each display copy
static u32 sdk_vi_copies_since_retrace() { return g_copiesSinceRetrace; }
static bool g_black = true;
static void* g_nextFb;
static LARGE_INTEGER g_freq, g_last;

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        g_w = LOWORD(lp) ? LOWORD(lp) : 1;
        g_h = HIWORD(lp) ? HIWORD(lp) : 1;
        return 0;
    case WM_CLOSE:
        ExitProcess(0);
    }
    return DefWindowProcA(hw, msg, wp, lp);
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
    g_w = sdk_widescreen() ? 854 : 640;  // the 640-wide EFB is stretched to 16:9, as a widescreen TV does
    RECT r{0, 0, g_w, g_h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowA(wc.lpszClassName, PROJECT_NAME, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                           CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
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
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_last);
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
    fprintf(stderr, "[lum] retrace %u mean %ld\n", g_retrace, n ? sum / n : 0);
}

static void MaybeShot() {
    static int frame = g_retrace;
    static int want = -1;
    static char path[260];
    if (want == -1) {
        const char* s = getenv("NICECRASH_SHOT");
        want = s ? atoi(s) : -2;
        const char* c = s ? strchr(s, ':') : nullptr;
        if (c) snprintf(path, sizeof path, "%s", c + 1);
    }
    frame = (int)g_retrace;
    if (want < 0 || frame < want || !path[0]) return;
    want = -3;  // once
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

void sdk_vi_present_now() {
    if (!g_dc) return;
    LumLog();
    MaybeShot();
    SwapBuffers(g_dc);
}
void sdk_vi_window_size(int* w, int* h) {
    *w = g_w;
    *h = g_h;
}

static void Present() {
    gxi_flush();  // drain the FIFO: draws land in the EFB, display copies in their XFBs (sdk/xfb.cpp)
    // The TV shows the XFB the game selected (VISetNextFrameBuffer), once per retrace, as the VI does.
    if (xfb_active() && g_dc) {
        u32 addr = g_nextFb ? (u32)(uintptr_t)g_nextFb : gxi_last_copy_dst();
        if (xfb_present(addr, g_black && getenv("NICECRASH_HONOR_BLACK"), g_w, g_h)) {
            LumLog();
            MaybeShot();
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

// The VI retrace interrupt: every 1/59.94 s. Called from OSGetTime (polled by every wait in the game) and
// VIWaitForRetrace, on the thread that holds the Wii CPU, like an interrupt arriving between instructions.
void sdk_vi_poll() {
    static bool inside;
    if (!g_hwnd || inside) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double frame = (double)g_freq.QuadPart * 1001.0 / 60000.0;
    double since = (double)(now.QuadPart - g_last.QuadPart);
    if (since < frame) return;
    inside = true;
    g_last.QuadPart = since > 4 * frame ? now.QuadPart : g_last.QuadPart + (LONGLONG)frame;
    Pump();
    g_retrace++;
    g_copiesSinceRetrace = 0;
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
void GXDrawDone(void) { gxi_flush(); }
void GXWaitDrawDone(void) { gxi_flush(); }
// Frame pacing: the GPU finishes a frame's display copy no sooner than the next retrace (the game keeps it a frame
// behind with breakpoints, and the VI shows one XFB per retrace). The host interprets the FIFO at once, which let
// menus render ~10 frames per retrace, some of them empty (black flashes). So the first flush after a display copy
// waits for the next retrace, as the game's own waits on the GPU would on the Wii. (Deferring the FIFO to the
// retrace instead deadlocks: the game busy-waits on a draw-sync flag without polling the clock.)
// NICECRASH_NOPACE turns it off.
void GXFlush(void) {
    static int pace = getenv("NICECRASH_NOPACE") == nullptr;
    if (pace && sdk_vi_copies_since_retrace() > 0) VIWaitForRetrace();
    gxi_flush();
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
