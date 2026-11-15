// GX renderer, step 1: fixed-function OpenGL on the thread that owns the window (the game's main thread).
// Vertices are decoded with the CP vertex formats, transformed on the CPU exactly as the GX transform unit does
// (XF position matrix, projection, viewport), and drawn into the window's back buffer, which plays the EFB
// (640x480, scaled to the window). Textures come from the BP texture registers and memory (lib/texture.cpp
// decoders; C4/C8 with TLUTs loaded into a host TMEM). TEV is approximated (vertex colour x texture); blending,
// depth, alpha test and scissor follow the BP registers. A display copy (EFB -> XFB) presents the frame.
// Register layouts follow public GX documentation (YAGCD, Dolphin's VideoCommon); this code is the port's own.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "gxinterp.h"
#include "texture.h"
#include "gxtev.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

extern "C" void sdk_vi_present_now();  // sdk/vi.cpp: swap buffers
extern "C" u32 sdk_vi_retrace_count();
extern "C" void sdk_vi_window_size(int* w, int* h);

namespace {

int g_frameNo;  // display copies so far
// NICECRASH_DRAWLOG=<retrace frame>: log the draws of the first frame presented on or after it.
int g_logFrame = -1;  // display copy being logged
bool LogThisFrame() {
    static int want = getenv("NICECRASH_DRAWLOG") ? atoi(getenv("NICECRASH_DRAWLOG")) : -1;
    if (want >= 0 && g_logFrame < 0 && (int)sdk_vi_retrace_count() >= want) g_logFrame = g_frameNo;
    return g_frameNo == g_logFrame;
}

const int EFB_W = 640, EFB_H = 528;  // EFB is 640 wide, up to 528 lines; the game uses 640x480

inline u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
inline u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }
inline float f32of(u32 v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}
inline float xff(int addr) { return f32of(g_gx.xf[addr]); }

// Physical GX address (as written to CP array bases and BP image addresses) -> host pointer in mapped MEM1/MEM2.
inline const u8* Phys(u32 a) {
    a &= 0x3FFFFFFF;
    return (const u8*)(uintptr_t)(a | 0x80000000u);
}

struct VtxFmt {
    int posCnt, posFmt, posFrac;
    int nrmCnt, nrmFmt, nrmIdx3;
    int colFmt[2];
    int texCnt[8], texFmt[8], texFrac[8];
};

VtxFmt Fmt(int vat) {
    u32 a = g_gx.cp[0x70 + vat], b = g_gx.cp[0x80 + vat], c = g_gx.cp[0x90 + vat];
    VtxFmt f;
    f.posCnt = (a & 1) ? 3 : 2;
    f.posFmt = (a >> 1) & 7;
    f.posFrac = (a >> 4) & 31;
    f.nrmCnt = (a >> 9) & 1 ? 9 : 3;
    f.nrmFmt = (a >> 10) & 7;
    f.nrmIdx3 = (a >> 31) & 1;
    f.colFmt[0] = (a >> 14) & 7;
    f.colFmt[1] = (a >> 18) & 7;
    f.texCnt[0] = (a >> 21) & 1; f.texFmt[0] = (a >> 22) & 7; f.texFrac[0] = (a >> 25) & 31;
    f.texCnt[1] = b & 1;         f.texFmt[1] = (b >> 1) & 7;  f.texFrac[1] = (b >> 4) & 31;
    f.texCnt[2] = (b >> 9) & 1;  f.texFmt[2] = (b >> 10) & 7; f.texFrac[2] = (b >> 13) & 31;
    f.texCnt[3] = (b >> 18) & 1; f.texFmt[3] = (b >> 19) & 7; f.texFrac[3] = (b >> 22) & 31;
    f.texCnt[4] = (b >> 27) & 1; f.texFmt[4] = (b >> 28) & 7; f.texFrac[4] = c & 31;
    f.texCnt[5] = (c >> 5) & 1;  f.texFmt[5] = (c >> 6) & 7;  f.texFrac[5] = (c >> 9) & 31;
    f.texCnt[6] = (c >> 14) & 1; f.texFmt[6] = (c >> 15) & 7; f.texFrac[6] = (c >> 18) & 31;
    f.texCnt[7] = (c >> 23) & 1; f.texFmt[7] = (c >> 24) & 7; f.texFrac[7] = (c >> 27) & 31;
    return f;
}

const int kCompSize[8] = {1, 1, 2, 2, 4, 0, 0, 0};
const int kColSize[8] = {2, 3, 4, 2, 3, 4, 0, 0};

inline float ReadComp(const u8* p, int fmt, float scale) {
    switch (fmt) {
    case 0: return p[0] * scale;
    case 1: return (s8)p[0] * scale;
    case 2: return be16(p) * scale;
    case 3: return (s16)be16(p) * scale;
    default: return f32of(be32(p));
    }
}

inline void ReadColor(const u8* p, int fmt, u8 out[4]) {
    switch (fmt) {
    case 0: {  // RGB565
        u16 v = be16(p);
        out[0] = (u8)(((v >> 11) & 31) * 255 / 31); out[1] = (u8)(((v >> 5) & 63) * 255 / 63);
        out[2] = (u8)((v & 31) * 255 / 31); out[3] = 255;
        break;
    }
    case 1: case 2: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255; break;  // RGB8, RGBX8
    case 3: {  // RGBA4
        u16 v = be16(p);
        out[0] = (u8)((v >> 12) * 17); out[1] = (u8)(((v >> 8) & 15) * 17);
        out[2] = (u8)(((v >> 4) & 15) * 17); out[3] = (u8)((v & 15) * 17);
        break;
    }
    case 4: {  // RGBA6
        u32 v = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
        out[0] = (u8)(((v >> 18) & 63) * 255 / 63); out[1] = (u8)(((v >> 12) & 63) * 255 / 63);
        out[2] = (u8)(((v >> 6) & 63) * 255 / 63); out[3] = (u8)((v & 63) * 255 / 63);
        break;
    }
    default: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3]; break;  // RGBA8
    }
}

struct Vtx {
    float pos[3];
    u8 col[2][4];
    float tex[8][2];
    int pnmtx;
    int texmtx[8];
};

// ---- textures ----
u8 g_tmem[1 << 20];  // host TMEM for TLUTs (byte-addressed)

struct GLTex {
    GLuint id;
    u32 sum;
    int w, h;
};
std::unordered_map<unsigned long long, GLTex> g_texCache;

u32 Checksum(const u8* p, size_t n) {
    u32 h = 2166136261u;
    size_t step = n > 4096 ? n / 1024 : 1;
    for (size_t i = 0; i < n; i += step) h = (h ^ p[i]) * 16777619u;
    return h ^ (u32)n;
}

void DecodeCI(int fmt, const u8* src, int w, int h, const u8* tlut, int tlutFmt, std::vector<u8>& out) {
    out.assign((size_t)w * h * 4, 0);
    auto pal = [&](int idx, u8* o) {
        u16 v = be16(tlut + idx * 2);
        if (tlutFmt == 0) {  // IA8
            o[0] = o[1] = o[2] = (u8)(v & 0xFF); o[3] = (u8)(v >> 8);
        } else if (tlutFmt == 1) {  // RGB565
            o[0] = (u8)(((v >> 11) & 31) * 255 / 31); o[1] = (u8)(((v >> 5) & 63) * 255 / 63);
            o[2] = (u8)((v & 31) * 255 / 31); o[3] = 255;
        } else {  // RGB5A3
            if (v & 0x8000) {
                o[0] = (u8)(((v >> 10) & 31) * 255 / 31); o[1] = (u8)(((v >> 5) & 31) * 255 / 31);
                o[2] = (u8)((v & 31) * 255 / 31); o[3] = 255;
            } else {
                o[0] = (u8)(((v >> 8) & 15) * 17); o[1] = (u8)(((v >> 4) & 15) * 17); o[2] = (u8)((v & 15) * 17);
                o[3] = (u8)(((v >> 12) & 7) * 255 / 7);
            }
        }
    };
    auto put = [&](int x, int y, int idx) {
        if (x < w && y < h) pal(idx, &out[((size_t)y * w + x) * 4]);
    };
    if (fmt == 8) {  // C4: 8x8 tiles, high nibble first
        for (int by = 0; by < h; by += 8)
            for (int bx = 0; bx < w; bx += 8)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2, src++) {
                        put(bx + x, by + y, *src >> 4);
                        put(bx + x + 1, by + y, *src & 15);
                    }
    } else if (fmt == 9) {  // C8: 8x4 tiles
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++, src++) put(bx + x, by + y, *src);
    } else {  // C14X2: 4x4 tiles of u16
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 4)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++, src += 2) put(bx + x, by + y, be16(src) & 0x3FFF);
    }
}

// Bind texture map `m` (0-7) from the BP registers. Returns false when there is nothing usable.
bool BindTexMap(int m) {
    u32 img0 = g_gx.bp[(m < 4 ? 0x88 : 0xA8) + (m & 3)];
    u32 img3 = g_gx.bp[(m < 4 ? 0x94 : 0xB4) + (m & 3)];
    u32 mode0 = g_gx.bp[(m < 4 ? 0x80 : 0xA0) + (m & 3)];
    u32 tlut = g_gx.bp[(m < 4 ? 0x98 : 0xB8) + (m & 3)];
    int w = (img0 & 0x3FF) + 1, h = ((img0 >> 10) & 0x3FF) + 1, fmt = (img0 >> 20) & 15;
    u32 addr = (img3 & 0xFFFFFF) << 5;
    if (!addr) return false;
    const u8* src = Phys(addr);
    bool ci = fmt >= 8 && fmt <= 10;
    size_t bytes = ci ? (fmt == 8 ? (size_t)((w + 7) & ~7) * ((h + 7) & ~7) / 2
                                  : fmt == 9 ? (size_t)((w + 7) & ~7) * ((h + 3) & ~3)
                                             : (size_t)((w + 3) & ~3) * ((h + 3) & ~3) * 2)
                      : tex::GXImageSize(fmt, w, h);
    if (!bytes) return false;
    u32 tlutOff = (tlut & 0x3FF) << 9, tlutFmt = (tlut >> 10) & 3;
    unsigned long long key = (unsigned long long)addr << 32 ^ (u32)(fmt << 28 | w << 14 | h) ^
                             (ci ? (unsigned long long)tlutOff << 4 : 0);
    u32 sum = Checksum(src, bytes) ^ (ci ? Checksum(g_tmem + tlutOff, fmt == 8 ? 32 : 512) : 0);
    GLTex& t = g_texCache[key];
    if (!t.id || t.sum != sum) {
        std::vector<u8> rgba;
        if (ci) DecodeCI(fmt, src, w, h, g_tmem + tlutOff, (int)tlutFmt, rgba);
        else if (!tex::DecodeRawGX(fmt, src, w, h, rgba)) return false;
        if (getenv("NICECRASH_TEXDUMP")) {  // each new/changed texture as build/tex/<addr>_<fmt>_<w>x<h>.bmp
            char path[128];
            CreateDirectoryA("build/tex", nullptr);
            snprintf(path, sizeof path, "build/tex/%08x_f%d_%dx%d_t%05x.bmp", addr, fmt, w, h, ci ? tlutOff : 0);
            if (FILE* fp = fopen(path, "wb")) {
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih{};
                ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = -h; ih.biPlanes = 1; ih.biBitCount = 32;
                fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + w * h * 4;
                fwrite(&fh, sizeof fh, 1, fp); fwrite(&ih, sizeof ih, 1, fp);
                for (size_t i = 0; i < rgba.size(); i += 4) {
                    u8 px[4] = {rgba[i + 2], rgba[i + 1], rgba[i], rgba[i + 3]};
                    fwrite(px, 4, 1, fp);
                }
                fclose(fp);
            }
        }
        if (!t.id) glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        t.sum = sum;
        t.w = w;
        t.h = h;
    }
    glBindTexture(GL_TEXTURE_2D, t.id);
    static const GLint wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap[mode0 & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap[(mode0 >> 2) & 3]);
    GLint mag = (mode0 >> 4) & 1 ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mag);
    return true;
}

// ---- render state ----
void ApplyState() {
    int ww, wh;
    sdk_vi_window_size(&ww, &wh);
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 640, 480, 0, 0, -16777215.0);  // EFB pixels; depth = GX z / 2^24
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // Scissor (BP 0x20/0x21, +342 offset).
    u32 tl = g_gx.bp[0x20], br = g_gx.bp[0x21];
    int x0 = (int)((tl >> 12) & 0x7FF) - 342, y0 = (int)(tl & 0x7FF) - 342;
    int x1 = (int)((br >> 12) & 0x7FF) - 342 + 1, y1 = (int)(br & 0x7FF) - 342 + 1;
    if (x1 > x0 && y1 > y0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x0 * ww / 640, (480 - y1) * wh / 480, (x1 - x0) * ww / 640, (y1 - y0) * wh / 480);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    // Blend (BP 0x41 PE_CMODE0).
    u32 cm = g_gx.bp[0x41];
    static const GLenum srcF[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    static const GLenum dstF[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    if (cm & 1) {
        glEnable(GL_BLEND);
        glBlendFunc(srcF[(cm >> 8) & 7], dstF[(cm >> 5) & 7]);
    } else {
        glDisable(GL_BLEND);
    }
    glColorMask((cm >> 3) & 1, (cm >> 3) & 1, (cm >> 3) & 1, (cm >> 4) & 1);
    // Depth (BP 0x40 PE_ZMODE).
    u32 zm = g_gx.bp[0x40];
    static const GLenum zf[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    if (zm & 1) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(zf[(zm >> 1) & 7]);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask((zm >> 4) & 1);
    // Alpha compare (BP 0xF3), first comparison only.
    u32 ac = g_gx.bp[0xF3];
    int comp0 = (ac >> 16) & 7;
    if (comp0 != 7) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(zf[comp0], (ac & 0xFF) / 255.0f);
    } else {
        glDisable(GL_ALPHA_TEST);
    }
    glDisable(GL_CULL_FACE);
}

// Transform one position by the XF matrix at row `pn`, the projection and the viewport -> EFB x, y, z, w.
void Transform(const float* p, int pn, float out[4]) {
    int m = pn * 4;
    float x = xff(m + 0) * p[0] + xff(m + 1) * p[1] + xff(m + 2) * p[2] + xff(m + 3);
    float y = xff(m + 4) * p[0] + xff(m + 5) * p[1] + xff(m + 6) * p[2] + xff(m + 7);
    float z = xff(m + 8) * p[0] + xff(m + 9) * p[1] + xff(m + 10) * p[2] + xff(m + 11);
    float pr[6];
    for (int i = 0; i < 6; i++) pr[i] = xff(0x1020 + i);
    float cx, cy, cz, cw;
    if (g_gx.xf[0x1026] == 0) {  // perspective
        cx = pr[0] * x + pr[1] * z;
        cy = pr[2] * y + pr[3] * z;
        cz = pr[4] * z + pr[5];
        cw = -z;
    } else {  // orthographic
        cx = pr[0] * x + pr[1];
        cy = pr[2] * y + pr[3];
        cz = pr[4] * z + pr[5];
        cw = 1.0f;
    }
    if (cw == 0) cw = 1e-6f;
    float nx = cx / cw, ny = cy / cw, nz = cz / cw;
    float sx = xff(0x101A), sy = xff(0x101B), sz = xff(0x101C);
    float ox = xff(0x101D), oy = xff(0x101E), oz = xff(0x101F);
    out[0] = ox + nx * sx - 342.0f;
    out[1] = oy + ny * sy - 342.0f;
    out[2] = oz + nz * sz;
    out[3] = cw;
}

void TexCoord(const Vtx& v, int gen, float out[2]) {
    u32 info = g_gx.xf[0x1040 + gen];
    int row = (info >> 7) & 31;
    bool stq = (info >> 1) & 1;
    float in[4] = {0, 0, 1, 1};
    if (row >= 5 && row <= 12) {
        in[0] = v.tex[row - 5][0];
        in[1] = v.tex[row - 5][1];
    } else if (row == 0) {
        in[0] = v.pos[0]; in[1] = v.pos[1]; in[2] = v.pos[2];
    }
    int m = v.texmtx[gen] * 4;
    float s = xff(m + 0) * in[0] + xff(m + 1) * in[1] + xff(m + 2) * in[2] + xff(m + 3) * in[3];
    float t = xff(m + 4) * in[0] + xff(m + 5) * in[1] + xff(m + 6) * in[2] + xff(m + 7) * in[3];
    if (stq) {
        float q = xff(m + 8) * in[0] + xff(m + 9) * in[1] + xff(m + 10) * in[2] + xff(m + 11) * in[3];
        if (q != 0) {
            s /= q;
            t /= q;
        }
    }
    out[0] = s;
    out[1] = t;
}

}  // namespace

int gxr_vertex_size(int vat) {
    u32 lo = g_gx.cp[0x50], hi = g_gx.cp[0x60];
    VtxFmt f = Fmt(vat);
    auto attr = [](int type, int direct) { return type == 0 ? 0 : type == 1 ? direct : type == 2 ? 1 : 2; };
    int size = (lo & 1);
    for (int i = 0; i < 8; i++) size += (lo >> (1 + i)) & 1;
    size += attr((lo >> 9) & 3, f.posCnt * kCompSize[f.posFmt]);
    int nrmType = (lo >> 11) & 3;
    if (nrmType >= 2 && f.nrmIdx3 && f.nrmCnt == 9) size += (nrmType == 2 ? 1 : 2) * 3;
    else size += attr(nrmType, f.nrmCnt * kCompSize[f.nrmFmt]);
    size += attr((lo >> 13) & 3, kColSize[f.colFmt[0]]);
    size += attr((lo >> 15) & 3, kColSize[f.colFmt[1]]);
    for (int i = 0; i < 8; i++) size += attr((hi >> (i * 2)) & 3, (f.texCnt[i] ? 2 : 1) * kCompSize[f.texFmt[i]]);
    return size;
}

void gxr_tlut_load(u32 bp64, u32 bp65) {
    const u8* src = Phys((bp64 & 0xFFFFFF) << 5);
    u32 dst = (bp65 & 0x3FF) << 9, n = (bp65 & 0x7FFC00) >> 5;
    if (dst + n <= sizeof(g_tmem)) memcpy(g_tmem + dst, src, n);
}

void gxr_draw(int prim, int vat, int count, const u8* data) {
    if (!count) return;
    u32 lo = g_gx.cp[0x50], hi = g_gx.cp[0x60];
    VtxFmt f = Fmt(vat);
    float posScale = f.posFmt == 4 ? 1.0f : 1.0f / (float)(1 << f.posFrac);
    float nrmScale = f.nrmFmt == 1 ? 1.0f / 64 : f.nrmFmt == 3 ? 1.0f / 16384 : 1.0f;
    float texScale[8];
    for (int i = 0; i < 8; i++) texScale[i] = f.texFmt[i] == 4 ? 1.0f : 1.0f / (float)(1 << f.texFrac[i]);
    u32 mia = g_gx.cp[0x30], mib = g_gx.cp[0x40];
    int defTex[8] = {(int)((mia >> 6) & 63), (int)((mia >> 12) & 63), (int)((mia >> 18) & 63), (int)((mia >> 24) & 63),
                     (int)(mib & 63), (int)((mib >> 6) & 63), (int)((mib >> 12) & 63), (int)((mib >> 18) & 63)};

    // Decode all vertices.
    std::vector<Vtx> vs(count);
    const u8* p = data;
    auto attrPtr = [&](int type, int arr, int directSize) -> const u8* {
        if (type == 1) {
            const u8* r = p;
            p += directSize;
            return r;
        }
        u32 idx = type == 2 ? *p : be16(p);
        p += type == 2 ? 1 : 2;
        if ((type == 2 && idx == 0xFF) || (type == 3 && idx == 0xFFFF)) return nullptr;
        return Phys(g_gx.cp[0xA0 + arr]) + idx * (g_gx.cp[0xB0 + arr] & 0xFF);
    };
    for (int n = 0; n < count; n++) {
        Vtx& v = vs[n];
        memset(&v, 0, sizeof(v));
        v.pnmtx = mia & 63;
        for (int i = 0; i < 8; i++) v.texmtx[i] = defTex[i];
        if (lo & 1) v.pnmtx = *p++ & 63;
        for (int i = 0; i < 8; i++)
            if ((lo >> (1 + i)) & 1) v.texmtx[i] = *p++ & 63;
        int t = (lo >> 9) & 3;
        if (t) {
            const u8* a = attrPtr(t, 0, f.posCnt * kCompSize[f.posFmt]);
            if (a)
                for (int c = 0; c < f.posCnt; c++) v.pos[c] = ReadComp(a + c * kCompSize[f.posFmt], f.posFmt, posScale);
        }
        t = (lo >> 11) & 3;
        if (t) {
            if (t >= 2 && f.nrmIdx3 && f.nrmCnt == 9) p += (t == 2 ? 1 : 2) * 3;
            else attrPtr(t, 1, f.nrmCnt * kCompSize[f.nrmFmt]);
            (void)nrmScale;
        }
        for (int c = 0; c < 2; c++) {
            t = (lo >> (13 + c * 2)) & 3;
            v.col[c][0] = v.col[c][1] = v.col[c][2] = v.col[c][3] = 255;
            if (t) {
                const u8* a = attrPtr(t, 2 + c, kColSize[f.colFmt[c]]);
                if (a) ReadColor(a, f.colFmt[c], v.col[c]);
            }
        }
        for (int i = 0; i < 8; i++) {
            t = (hi >> (i * 2)) & 3;
            if (!t) continue;
            int cnt = f.texCnt[i] ? 2 : 1;
            const u8* a = attrPtr(t, 4 + i, cnt * kCompSize[f.texFmt[i]]);
            if (a)
                for (int c = 0; c < cnt; c++) v.tex[i][c] = ReadComp(a + c * kCompSize[f.texFmt[i]], f.texFmt[i], texScale[i]);
        }
    }

    // Lighting channel 0 colour: vertex or material register (XF 0x100E/0x1010 bit 0 = source).
    u32 c0 = g_gx.xf[0x100E], a0 = g_gx.xf[0x1010], mat = g_gx.xf[0x100C];
    bool colVtx = c0 & 1, alpVtx = a0 & 1;
    int numChans = g_gx.xf[0x1009] & 3;

    // TEV: generated shader (sdk/gxtev.cpp) with every texture map the stages use bound on its own unit;
    // fallback = stage 0 approximation (texture x rasterised colour).
    u32 order = g_gx.bp[0x28];
    int texMap = order & 7, texCoord = (order >> 3) & 7;
    int nStages = ((g_gx.bp[0x00] >> 10) & 15) + 1, nTexGens = g_gx.bp[0x00] & 15;
    bool texOk[8] = {};
    bool tried[8] = {};
    for (int st = 0; st < nStages; st++) {
        u32 o = g_gx.bp[0x28 + st / 2] >> ((st & 1) * 12);
        int m = o & 7;
        if (!((o >> 6) & 1) || tried[m]) continue;
        tried[m] = true;
        gxt_active_texture(m);
        texOk[m] = BindTexMap(m);
    }
    for (int i = 0, nInd = (g_gx.bp[0x00] >> 16) & 7; i < nInd && i < 4; i++) {  // indirect stages' maps
        int m = (g_gx.bp[0x27] >> (6 * i)) & 7;
        if (tried[m]) continue;
        tried[m] = true;
        gxt_active_texture(m);
        texOk[m] = BindTexMap(m);
    }
    gxt_active_texture(0);
    ApplyState();
    bool tev = gxt_begin(texOk);
    if (tev) glDisable(GL_ALPHA_TEST);
    bool texOn = !tev && ((order >> 6) & 1) && BindTexMap(texMap);
    if (tev) {
        glDisable(GL_TEXTURE_2D);
    } else if (texOn) {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    if (LogThisFrame()) {
        float o[4];
        Transform(vs[0].pos, vs[0].pnmtx, o);
        fprintf(stderr, "[draw] prim %02x vat %d n %d pnmtx %d pos (%.2f %.2f %.2f) -> efb (%.1f %.1f z %.0f w %.3f) "
                        "col %02x%02x%02x%02x chans %d matsrc %d tex %d map %d (img0 %06x img3 %06x) blend %06x z %06x proj %u\n",
                prim, vat, count, vs[0].pnmtx, vs[0].pos[0], vs[0].pos[1], vs[0].pos[2], o[0], o[1], o[2], o[3],
                vs[0].col[0][0], vs[0].col[0][1], vs[0].col[0][2], vs[0].col[0][3], numChans, colVtx, texOn, texMap,
                g_gx.bp[0x88 + texMap] & 0xFFFFFF, g_gx.bp[0x94 + texMap] & 0xFFFFFF, g_gx.bp[0x41] & 0xFFFFFF,
                g_gx.bp[0x40] & 0xFFFFFF, g_gx.xf[0x1026]);
        for (int k = 0; k < count && k < 6; k++)
            fprintf(stderr, "    v%d pos (%.2f %.2f %.2f) tex (%.4f %.4f) col %02x%02x%02x%02x\n", k, vs[k].pos[0], vs[k].pos[1],
                    vs[k].pos[2], vs[k].tex[0][0], vs[k].tex[0][1], vs[k].col[0][0], vs[k].col[0][1], vs[k].col[0][2], vs[k].col[0][3]);
        if (texOn) {
            float st[2];
            TexCoord(vs[0], texCoord, st);
            int m = vs[0].texmtx[texCoord] * 4;
            fprintf(stderr, "    tex raw (%.4f %.4f) gen %d info %08x mtx %d [%.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f] -> st (%.4f %.4f) vcd %08x/%08x vat %08x\n",
                    vs[0].tex[0][0], vs[0].tex[0][1], texCoord, g_gx.xf[0x1040 + texCoord], vs[0].texmtx[texCoord],
                    xff(m), xff(m + 1), xff(m + 2), xff(m + 3), xff(m + 4), xff(m + 5), xff(m + 6), xff(m + 7), st[0], st[1],
                    g_gx.cp[0x50], g_gx.cp[0x60], g_gx.cp[0x70 + vat]);
        }
    }
    static const GLenum kPrim[8] = {GL_QUADS, GL_QUADS, GL_TRIANGLES, GL_TRIANGLE_STRIP,
                                    GL_TRIANGLE_FAN, GL_LINES, GL_LINE_STRIP, GL_POINTS};
    glBegin(kPrim[(prim >> 3) & 7]);
    for (int n = 0; n < count; n++) {
        Vtx& v = vs[n];
        u8 col[4];
        if (numChans == 0) {
            col[0] = col[1] = col[2] = col[3] = 255;
        } else {
            col[0] = colVtx ? v.col[0][0] : (u8)(mat >> 24);
            col[1] = colVtx ? v.col[0][1] : (u8)(mat >> 16);
            col[2] = colVtx ? v.col[0][2] : (u8)(mat >> 8);
            col[3] = alpVtx ? v.col[0][3] : (u8)mat;
        }
        glColor4ub(col[0], col[1], col[2], col[3]);
        if (tev) {
            u8 c1[4];
            if (numChans < 2) {
                c1[0] = c1[1] = c1[2] = c1[3] = 255;
            } else {
                bool cv = g_gx.xf[0x100F] & 1, av = g_gx.xf[0x1011] & 1;
                u32 m1 = g_gx.xf[0x100D];
                c1[0] = cv ? v.col[1][0] : (u8)(m1 >> 24);
                c1[1] = cv ? v.col[1][1] : (u8)(m1 >> 16);
                c1[2] = cv ? v.col[1][2] : (u8)(m1 >> 8);
                c1[3] = av ? v.col[1][3] : (u8)m1;
            }
            gxt_color1(c1);
            for (int g = 0; g < nTexGens && g < 8; g++) {
                float st[2];
                TexCoord(v, g, st);
                gxt_texcoord(g, st[0], st[1]);
            }
        } else if (texOn) {
            float st[2];
            TexCoord(v, texCoord, st);
            glTexCoord2f(st[0], st[1]);
        }
        float o[4];
        Transform(v.pos, v.pnmtx, o);
        float w = o[3] > 0 ? o[3] : -o[3];
        if (w < 1e-6f) w = 1e-6f;
        glVertex4f(o[0] * w, o[1] * w, o[2] * w, w);
    }
    glEnd();
    if (tev) gxt_end();
}

void gxr_copy(u32 bp52) {
    bool toXfb = (bp52 >> 14) & 1, clear = (bp52 >> 11) & 1;
    if (toXfb) {
        sdk_vi_present_now();
        g_frameNo++;
    }
    if (clear) {
        u32 ar = g_gx.bp[0x4F], gb = g_gx.bp[0x50], z = g_gx.bp[0x51] & 0xFFFFFF;
        glDisable(GL_SCISSOR_TEST);
        glColorMask(1, 1, 1, 1);
        glDepthMask(1);
        glClearColor(((ar) & 0xFF) / 255.0f, ((gb >> 8) & 0xFF) / 255.0f, (gb & 0xFF) / 255.0f,
                     ((ar >> 8) & 0xFF) / 255.0f);
        glClearDepth(z / 16777215.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}
