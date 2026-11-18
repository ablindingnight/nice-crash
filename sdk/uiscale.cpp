// Live smoothing for the game's 2D art (graphics.ui_filter): menus, HUD, fonts, the sky and backdrop cards.
//
// The Wii's menu art was made for 640x480. Drawn into a 1080p or 4K window, GL magnifies each texel with a plain
// bilinear ramp: text and button edges go soft and stair-stepped. Here such textures are upscaled once, when they are
// uploaded, and drawn from the bigger image with mip levels (the game and the TEV programs keep using the Wii size).
//
//   smooth  bicubic (Catmull-Rom), each value kept inside its 2x2 source cell (no halos or ringing)
//   sharp   smooth, and where the source has a hard edge (text, outlines, button borders: a big step between
//           neighbouring texels) the ramp is tightened to about 2 output pixels around the cell's own midpoint, so
//           edges stay crisp at any window size. Soft gradients, glows and shading are left as smooth.
//
// Which textures: those the game samples without mip levels (Dolphin's "_m" is absent). In Excite Truck that is the
// 2D art (menus, HUD, fonts, icons) plus the sky and the distant hill cards; the 3D world's textures all use mips.
// Textures the game keeps rewriting (more than 3 uploads) and texture-pack replacements are left alone.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "env.h"
#include "hostcfg.h"
#include "uiscale.h"

namespace {

struct Job {
    const u8* src;
    int w, h, k, wrapS, wrapT, mode;
    u8* dst;
    int y0, y1;  // output rows
};

inline int Wrap(int i, int n, int mode) {
    if (mode == 1) return ((i % n) + n) % n;  // repeat
    if (mode == 2) {                           // mirrored repeat
        int p = ((i % (2 * n)) + 2 * n) % (2 * n);
        return p < n ? p : 2 * n - 1 - p;
    }
    return i < 0 ? 0 : i >= n ? n - 1 : i;  // clamp
}

void CatmullRom(float f, float w[4]) {
    w[0] = f * (-0.5f + f * (1.0f - 0.5f * f));
    w[1] = 1.0f + f * f * (-2.5f + 1.5f * f);
    w[2] = f * (0.5f + f * (2.0f - 1.5f * f));
    w[3] = f * f * (-0.5f + 0.5f * f);
}

inline float SmoothStep(float a, float b, float x) {
    float t = (x - a) / (b - a);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

DWORD WINAPI Run(void* p) {
    const Job& j = *(const Job*)p;
    const int k = j.k, w = j.w;
    static const float g = NC_ENV("NICECRASH_UISCALE_GAIN") ? (float)atof(NC_ENV("NICECRASH_UISCALE_GAIN")) : 0.75f;
    const float gain = g * k;  // edge ramp: one source texel becomes ~1.3 output pixels
    for (int oy = j.y0; oy < j.y1; oy++) {
        float sy = (oy + 0.5f) / k - 0.5f;
        int iy = (int)floorf(sy);
        float wy[4];
        CatmullRom(sy - iy, wy);
        int ry[4];
        for (int t = 0; t < 4; t++) ry[t] = Wrap(iy - 1 + t, j.h, j.wrapT);
        u8* out = j.dst + (size_t)oy * w * k * 4;
        for (int ox = 0; ox < w * k; ox++) {
            float sx = (ox + 0.5f) / k - 0.5f;
            int ix = (int)floorf(sx);
            float wx[4];
            CatmullRom(sx - ix, wx);
            int rx[4];
            for (int t = 0; t < 4; t++) rx[t] = Wrap(ix - 1 + t, w, j.wrapS);
            float acc[4] = {0, 0, 0, 0}, lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
            for (int ty = 0; ty < 4; ty++) {
                const u8* row = j.src + (size_t)ry[ty] * w * 4;
                for (int tx = 0; tx < 4; tx++) {
                    const u8* px = row + rx[tx] * 4;
                    float wt = wx[tx] * wy[ty];
                    bool cell = (tx == 1 || tx == 2) && (ty == 1 || ty == 2);
                    for (int c = 0; c < 4; c++) {
                        float v = px[c];
                        acc[c] += v * wt;
                        if (cell) lo[c] = v < lo[c] ? v : lo[c], hi[c] = v > hi[c] ? v : hi[c];
                    }
                }
            }
            float s = 0;
            if (j.mode == 2) {  // hard edge in this cell: the biggest step of any channel decides, so colours stay together
                float range = 0;
                for (int c = 0; c < 4; c++) range = hi[c] - lo[c] > range ? hi[c] - lo[c] : range;
                s = SmoothStep(255 * 0.22f, 255 * 0.55f, range);
            }
            for (int c = 0; c < 4; c++) {
                float v = acc[c];
                if (s > 0) {
                    float mid = 0.5f * (lo[c] + hi[c]);
                    v += s * ((mid + (v - mid) * gain) - v);
                }
                v = v < lo[c] ? lo[c] : v > hi[c] ? hi[c] : v;
                out[ox * 4 + c] = (u8)(v + 0.5f);
            }
        }
    }
    return 0;
}

}  // namespace

bool uiscale_upload(const u8* rgba, int w, int h, u32 mode0, int fmt) {
    int mode = host_cfg().uiFilter;
    if (mode == 0) return false;
    if ((mode0 >> 5) & 3) return false;     // the game uses mip levels: 3D world texture
    if (!((mode0 >> 4) & 1)) return false;  // magnified with nearest on purpose
    (void)fmt;
    int big = w > h ? w : h;
    // Small art is usually drawn bigger than it is (upscaled); big atlases (the 1024-pixel menu fonts) are drawn
    // smaller (~0.45x at 1080p): those only get mip levels and the LOD bias, which is what they need.
    int k = big <= 256 ? 4 : big <= 512 ? 2 : 1;
    if (big > 2048 || w < 4 || h < 4) return false;
    ULONGLONG t0 = GetTickCount64();
    static const GLint wrapM[4] = {0, 1, 2, 1};
    int W = w * k, H = h * k;
    std::vector<u8> dst(k > 1 ? (size_t)W * H * 4 : 0);
    static int ncpu;
    if (!ncpu) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        ncpu = si.dwNumberOfProcessors > 1 ? (int)si.dwNumberOfProcessors : 1;
        if (ncpu > 16) ncpu = 16;
    }
    int n = k == 1 ? 0 : H >= 64 ? ncpu : 1;
    Job jobs[16];
    HANDLE th[16] = {};
    for (int i = 0; i < n; i++) {
        jobs[i] = {rgba, w, h, k, wrapM[mode0 & 3], wrapM[(mode0 >> 2) & 3], mode, dst.data(), H * i / n, H * (i + 1) / n};
        th[i] = i ? CreateThread(nullptr, 0, Run, &jobs[i], 0, nullptr) : nullptr;
        if (i && !th[i]) Run(&jobs[i]);
    }
    if (n) Run(&jobs[0]);
    for (int i = 1; i < n; i++)
        if (th[i]) {
            WaitForSingleObject(th[i], INFINITE);
            CloseHandle(th[i]);
        }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, k > 1 ? dst.data() : rgba);
    typedef void(APIENTRY * GenMip_t)(GLenum);
    static GenMip_t genMip = (GenMip_t)(void*)wglGetProcAddress("glGenerateMipmap");
    if (genMip) genMip(GL_TEXTURE_2D);
    static const float bias = NC_ENV("NICECRASH_UISCALE_BIAS") ? (float)atof(NC_ENV("NICECRASH_UISCALE_BIAS")) : -0.5f;
    // The bigger image is drawn smaller than itself at 1080p (a 4x upscale shown at 2.25x): favour its sharpest level.
    if (bias) glTexParameterf(GL_TEXTURE_2D, 0x8501 /* GL_TEXTURE_LOD_BIAS */, bias);
    static const bool log = NC_ENV("NICECRASH_UISCALE_LOG") != nullptr;
    if (log) fprintf(stderr, "[uiscale] %dx%d fmt %d -> %dx%d in %d ms\n", w, h, fmt, W, H, (int)(GetTickCount64() - t0));
    return true;
}
