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
#include "env.h"
#include "gxinterp.h"
#include "texture.h"
#include "gxtev.h"
#include "xfb.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

extern "C" void sdk_vi_present_now();  // sdk/vi.cpp: swap buffers
extern "C" u32 sdk_vi_retrace_count();
extern "C" void sdk_vi_note_copy();
extern "C" void sdk_vi_window_size(int* w, int* h);

namespace {

int g_frameNo;  // display copies so far
// NICECRASH_DRAWLOG=<retrace frame>: log the draws of the first frame presented on or after it.
int g_logFrame = -1;  // display copy being logged
bool LogThisFrame() {
    static int want = NC_ENV("NICECRASH_DRAWLOG") ? atoi(NC_ENV("NICECRASH_DRAWLOG")) : -1;
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

inline u16 le16(const u8* p) { return (u16)(p[1] << 8 | p[0]); }
inline u32 le32(const u8* p) { return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }

// Vertex arrays (indexed attributes) are in HOST byte order: the game's disc data is little-endian and the port
// skips the Wii loader's byte swaps, and arrays the CPU builds are native too. Direct attributes come through the
// FIFO, which the SDK writes big-endian. NICECRASH_ARRAYBE=1 reads arrays big-endian (as the Wii GPU does).
inline float ReadComp(const u8* p, int fmt, float scale, bool le) {
    switch (fmt) {
    case 0: return p[0] * scale;
    case 1: return (s8)p[0] * scale;
    case 2: return (le ? le16(p) : be16(p)) * scale;
    case 3: return (s16)(le ? le16(p) : be16(p)) * scale;
    default: return f32of(le ? le32(p) : be32(p));
    }
}

inline void ReadColor(const u8* p, int fmt, u8 out[4], bool le) {
    switch (fmt) {
    case 0: {  // RGB565
        u16 v = le ? le16(p) : be16(p);
        out[0] = (u8)(((v >> 11) & 31) * 255 / 31); out[1] = (u8)(((v >> 5) & 63) * 255 / 63);
        out[2] = (u8)((v & 31) * 255 / 31); out[3] = 255;
        break;
    }
    case 1: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255; break;  // RGB8
    case 2:  // RGBX8: a host-order array holds the colour as a native u32 word (bytes reversed)
        if (le) { out[0] = p[3]; out[1] = p[2]; out[2] = p[1]; }
        else { out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; }
        out[3] = 255;
        break;
    case 3: {  // RGBA4
        u16 v = le ? le16(p) : be16(p);
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
    default:  // RGBA8 (word-reversed in a host-order array, as RGBX8)
        if (le) { out[0] = p[3]; out[1] = p[2]; out[2] = p[1]; out[3] = p[0]; }
        else { out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3]; }
        break;
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
    int frame = -1;  // display frame of the last checksum (memory is checked once per frame)
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

// ---- EFB -> texture copies ----
// A texture copy (BP 0x52 without the XFB bit) renders into the EFB and copies a rectangle into RAM as a texture.
// The port keeps the copy as a host texture at window resolution, keyed by its RAM address (Dolphin's "EFB copies
// to texture only"). The RAM is not written: a texture bound at that address uses the host copy while the RAM there
// still holds what it held at copy time (a checksum), so a disc texture loaded over it later wins.
struct EfbCopy {
    GLuint id;
    u32 ramSum;
    size_t bytes;
    int pending = 0;  // GPU copies whose RAM image is still being read back (see ResolveCopies)
    int tw = 0, th = 0;  // host texture size
    int frame = -1;  // display frame the RAM was last found unchanged
};
std::unordered_map<u32, EfbCopy> g_efbCopies;

// Bits per texel of each copy format (BP 0x52 "real format"; colour and Z names share the numbers).
const int kCopyBpp[16] = {4, 8, 8, 16, 16, 16, 32, 8, 8, 8, 8, 16, 16, 16, 16, 16};

// Encode a copy (rgba = what the target format samples as, w x h at EFB resolution) into RAM as GX tiles.
void EncodeCopy(u8* dst, int real, const std::vector<u8>& rgba, int w, int h, u32 strideLines) {
    int bpp = kCopyBpp[real];
    // Fast paths for whole-block RGBA8 and RGB565 copies (the race's reflection and shadow copies).
    if ((real == 6 || real == 4) && w % 4 == 0 && h % 4 == 0) {
        int bx = w / 4, by = h / 4, blockBytes = real == 6 ? 64 : 32;
        size_t rowBytes = strideLines ? (size_t)strideLines * 32 : (size_t)bx * blockBytes;
        const u8* src = rgba.data();
        for (int j = 0; j < by; j++)
            for (int i = 0; i < bx; i++) {
                u8* b = dst + j * rowBytes + (size_t)i * blockBytes;
                for (int y = 0; y < 4; y++) {
                    const u8* o = src + ((size_t)(j * 4 + y) * w + i * 4) * 4;
                    for (int x = 0; x < 4; x++, o += 4) {
                        int k = y * 4 + x;
                        if (real == 6) {
                            b[k * 2] = o[3]; b[k * 2 + 1] = o[0];
                            b[32 + k * 2] = o[1]; b[32 + k * 2 + 1] = o[2];
                        } else {
                            u16 v = (u16)((o[0] >> 3) << 11 | (o[1] >> 2) << 5 | (o[2] >> 3));
                            b[k * 2] = (u8)(v >> 8);
                            b[k * 2 + 1] = (u8)v;
                        }
                    }
                }
            }
        return;
    }
    int bw = bpp == 4 ? 8 : bpp == 8 ? 8 : 4, bh = bpp == 4 ? 8 : 4;  // block size in texels
    int blockBytes = bpp == 32 ? 64 : 32;
    int bx = (w + bw - 1) / bw, by = (h + bh - 1) / bh;
    size_t rowBytes = strideLines ? (size_t)strideLines * 32 : (size_t)bx * blockBytes;
    auto px = [&](int x, int y) -> const u8* {
        static const u8 zero[4] = {0, 0, 0, 0};
        return x < w && y < h ? &rgba[((size_t)y * w + x) * 4] : zero;
    };
    for (int j = 0; j < by; j++) {
        for (int i = 0; i < bx; i++) {
            u8* b = dst + j * rowBytes + (size_t)i * blockBytes;
            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    const u8* o = px(i * bw + x, j * bh + y);
                    int k = y * bw + x;
                    switch (bpp) {
                    case 4:
                        if (x & 1) b[k / 2] = (u8)((b[k / 2] & 0xF0) | (o[0] >> 4));
                        else b[k / 2] = (u8)((b[k / 2] & 0x0F) | (o[0] & 0xF0));
                        break;
                    case 8: b[k] = real == 2 ? (u8)((o[3] & 0xF0) | (o[0] >> 4)) : o[0]; break;
                    case 16: {
                        u16 v;
                        if (real == 4) v = (u16)((o[0] >> 3) << 11 | (o[1] >> 2) << 5 | (o[2] >> 3));
                        else if (real == 5)
                            v = o[3] >= 0xE0 ? (u16)(0x8000 | (o[0] >> 3) << 10 | (o[1] >> 3) << 5 | (o[2] >> 3))
                                             : (u16)((o[3] >> 5) << 12 | (o[0] >> 4) << 8 | (o[1] >> 4) << 4 | (o[2] >> 4));
                        else v = (u16)(o[3] << 8 | o[0]);  // IA8 family: alpha byte first
                        b[k * 2] = (u8)(v >> 8);
                        b[k * 2 + 1] = (u8)v;
                        break;
                    }
                    default:  // RGBA8: AR pairs, then GB pairs
                        b[k * 2] = o[3]; b[k * 2 + 1] = o[0];
                        b[32 + k * 2] = o[1]; b[32 + k * 2 + 1] = o[2];
                        break;
                    }
                }
            }
        }
    }
}

bool BindEfbCopy(u32 addr, const u8* src) {
    auto it = g_efbCopies.find(addr);
    if (it == g_efbCopies.end()) return false;
    if (!it->second.pending && it->second.frame != g_frameNo &&
        Checksum(src, it->second.bytes) != it->second.ramSum) {  // RAM rewritten
        glDeleteTextures(1, &it->second.id);
        g_efbCopies.erase(it);
        return false;
    }
    it->second.frame = g_frameNo;
    glBindTexture(GL_TEXTURE_2D, it->second.id);
    return true;
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
    static const GLint wrapM[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    static const bool noCopyTex = NC_ENV("NICECRASH_NOCOPYTEX") != nullptr;
    if (!noCopyTex && BindEfbCopy(addr, src)) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapM[mode0 & 3]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapM[(mode0 >> 2) & 3]);
        GLint f = (mode0 >> 4) & 1 ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        return true;
    }
    bool ci = fmt >= 8 && fmt <= 10;
    size_t bytes = ci ? (fmt == 8 ? (size_t)((w + 7) & ~7) * ((h + 7) & ~7) / 2
                                  : fmt == 9 ? (size_t)((w + 7) & ~7) * ((h + 3) & ~3)
                                             : (size_t)((w + 3) & ~3) * ((h + 3) & ~3) * 2)
                      : tex::GXImageSize(fmt, w, h);
    if (!bytes) return false;
    u32 tlutOff = (tlut & 0x3FF) << 9, tlutFmt = (tlut >> 10) & 3;
    unsigned long long key = (unsigned long long)addr << 32 ^ (u32)(fmt << 28 | w << 14 | h) ^
                             (ci ? (unsigned long long)tlutOff << 4 : 0);
    GLTex& t = g_texCache[key];
    u32 sum = t.sum;
    if (!t.id || t.frame != g_frameNo) {
        sum = Checksum(src, bytes) ^ (ci ? Checksum(g_tmem + tlutOff, fmt == 8 ? 32 : 512) : 0);
        t.frame = g_frameNo;
    }
    if (!t.id || t.sum != sum) {
        std::vector<u8> rgba;
        if (ci) DecodeCI(fmt, src, w, h, g_tmem + tlutOff, (int)tlutFmt, rgba);
        else if (!tex::DecodeRawGX(fmt, src, w, h, rgba)) return false;
        if (NC_ENV("NICECRASH_TEXDUMP")) {  // each new/changed texture as build/tex/<addr>_<fmt>_<w>x<h>.bmp
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
    GLint mag = (mode0 >> 4) & 1 && !NC_ENV("NICECRASH_NEAREST") ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mag);
    return true;
}

// ---- render state ----
void ApplyState() {
    int ww, wh;
    sdk_vi_window_size(&ww, &wh);
    xfb_bind_efb(ww, wh);  // draw into the off-screen EFB (sdk/xfb.cpp)
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
    // Blend (BP 0x41 PE_CMODE0, 0x42 PE_CMODE1, 0x43 pixel format).
    gxt_blend(g_gx.bp[0x41], g_gx.bp[0x42], g_gx.bp[0x43]);
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
void Transform(const float* p, int pn, float out[4], float* clip = nullptr) {
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
    float sx = xff(0x101A), sy = xff(0x101B), sz = xff(0x101C);
    float ox = xff(0x101D), oy = xff(0x101E), oz = xff(0x101F);
    if (clip) {  // homogeneous EFB coordinates (x, y, z times w, and the signed w): GL clips behind the eye
        clip[0] = (ox - 342.0f) * cw + cx * sx;
        clip[1] = (oy - 342.0f) * cw + cy * sy;
        clip[2] = oz * cw + cz * sz;
        clip[3] = cw;
    }
    if (cw == 0) cw = 1e-6f;
    float nx = cx / cw, ny = cy / cw, nz = cz / cw;
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

// Byte order of a draw's vertex arrays. Model files (.mod) carry GX-ready big-endian arrays; arrays the game builds
// with the CPU (terrain heightfield, water, glare) are in host order, as the port skips the Wii's byte swaps. Float
// positions tell them apart: real coordinates are plausible in one byte order and not in the other. The answer is
// kept per array base (non-float positions use it, or big-endian). NICECRASH_ARRAYBE / NICECRASH_ARRAYLE force one.
static bool PlausibleF32(u32 bits) {
    u32 e = (bits >> 23) & 0xFF;
    if (e == 0) return (bits & 0x7FFFFFFF) == 0;  // zero ok, denormals not
    if (e == 0xFF) return false;
    return e >= 127 - 20 && e <= 127 + 20;  // |v| in ~[1e-6, 1e6]
}

static bool ArraysHostOrder(u32 lo, const VtxFmt& f, int count, const u8* data, int vat) {
    static const int force = NC_ENV("NICECRASH_ARRAYBE") ? 1 : NC_ENV("NICECRASH_ARRAYLE") ? 2 : 0;
    if (force) return force == 2;
    // array base -> host order, and the display frame it was last judged in: judged again once per frame (the
    // memory can be reused for the other kind of array), not at every draw (12 positions scored per draw was ~20%
    // of gxr_draw in the race).
    struct Known { bool host; int frame; };
    static std::unordered_map<u32, Known> known;
    int pt = (lo >> 9) & 3;
    if (pt < 2) return false;  // positions come through the FIFO: no array to judge
    u32 base = g_gx.cp[0xA0], stride = g_gx.cp[0xB0] & 0xFF;
    auto it = known.find(base);
    if (it != known.end() && it->second.frame == g_frameNo) return it->second.host;
    if (f.posFmt != 4) return it != known.end() && it->second.host;
    int off = (lo & 1);
    for (int i = 0; i < 8; i++) off += (lo >> (1 + i)) & 1;
    int vs = gxr_vertex_size(vat);
    int scoreBE = 0, scoreLE = 0;
    for (int n = 0; n < count && n < 12; n++) {
        const u8* v = data + (size_t)n * vs + off;
        u32 idx = pt == 2 ? v[0] : be16(v);
        if ((pt == 2 && idx == 0xFF) || (pt == 3 && idx == 0xFFFF)) continue;
        const u8* a = Phys(base) + idx * stride;
        for (int c = 0; c < f.posCnt; c++) {
            scoreBE += PlausibleF32(be32(a + c * 4));
            scoreLE += PlausibleF32(le32(a + c * 4));
        }
    }
    if (scoreBE == scoreLE) return it != known.end() && it->second.host;
    bool hostOrder = scoreLE > scoreBE;
    known[base] = {hostOrder, g_frameNo};
    return hostOrder;
}

// The draw batch: vertices of consecutive draws with the same GL state, drawn with one call.
struct BV {
    float pos[4];
    u8 c0[4], c1[4];
    float tc[8][2];
};
// Each draw's vertices once, packed with only the batch's texture coordinates (stride = BvStride(g_bNTex)): BV's
// first 24 + 8 * nTex bytes. A full BV is 88 bytes; most batches use one or two coordinate sets.
static std::vector<u8> g_batch;
static size_t g_batchN;  // vertices in g_batch
static std::vector<unsigned> g_batchIdx;  // triangles as indices into g_batch
static unsigned g_nFlush, g_nApply, g_nDraw;  // per display frame (NICECRASH_FRAMETIME)
static bool g_stateDirty = true, g_batchValid, g_bTev, g_bTexOn;
static int g_bNTex;
static inline int BvStride(int nTex) { return 24 + 8 * (nTex > 0 ? nTex : 0); }

void gxr_state_dirty() { g_stateDirty = true; }

void gxr_flush_batch() {
    if (!g_batchN) return;
    g_nFlush++;
    if (!g_batchIdx.empty())
        gxt_draw_arrays(GL_TRIANGLES, g_batch.data(), BvStride(g_bNTex), (int)g_batchN, g_bNTex, g_batchIdx.data(),
                        (int)g_batchIdx.size());
    g_batch.clear();
    g_batchN = 0;
    g_batchIdx.clear();
}

// Draws what is batched and releases the GL state (other code draws next: copies, presentation, the HOME menu).
void gxr_end_batch() {
    gxr_flush_batch();
    if (g_batchValid && g_bTev) gxt_end();
    g_batchValid = false;
    g_stateDirty = true;
}

static u32 g_drawsSinceCopy;  // draws since the last display copy (an empty copy is not shown, see gxr_copy)

void gxr_draw(int prim, int vat, int count, const u8* data) {
    if (!count) return;
    // NICECRASH_SKIPTEX=<hex image address>[,...]: skip draws whose stage-0 texture is one of these (debugging).
    static std::vector<u32> skipTex = [] {
        std::vector<u32> v;
        if (const char* s = NC_ENV("NICECRASH_SKIPTEX"))
            for (char* e; *s; s = *e ? e + 1 : e) v.push_back((u32)strtoul(s, &e, 16));
        return v;
    }();
    // NICECRASH_DRAWMAX=<n>: draw only the first n draws of each frame (bisecting a bad draw).
    static const int drawMax = NC_ENV("NICECRASH_DRAWMAX") ? atoi(NC_ENV("NICECRASH_DRAWMAX")) : -1;
    if (drawMax >= 0 && (int)g_drawsSinceCopy >= drawMax) {
        g_drawsSinceCopy++;
        return;
    }
    if (!skipTex.empty()) {
        u32 a = (g_gx.bp[0x94 + (g_gx.bp[0x28] & 3)] & 0xFFFFFF) << 5;
        for (u32 s : skipTex)
            if (a == s) return;
    }
    g_drawsSinceCopy++;
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
    static std::vector<Vtx> vs;
    if ((int)vs.size() < count) vs.resize(count);
    const u8* p = data;
    bool arraysLE = ArraysHostOrder(lo, f, count, data, vat);
    bool le = false;  // set by attrPtr: the attribute came from a host-order array
    auto attrPtr = [&](int type, int arr, int directSize) -> const u8* {
        le = type >= 2 && arraysLE;
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
        v.pos[0] = v.pos[1] = v.pos[2] = 0.0f;
        memset(v.tex, 0, sizeof v.tex);
        v.pnmtx = mia & 63;
        for (int i = 0; i < 8; i++) v.texmtx[i] = defTex[i];
        if (lo & 1) v.pnmtx = *p++ & 63;
        for (int i = 0; i < 8; i++)
            if ((lo >> (1 + i)) & 1) v.texmtx[i] = *p++ & 63;
        int t = (lo >> 9) & 3;
        if (t) {
            const u8* a = attrPtr(t, 0, f.posCnt * kCompSize[f.posFmt]);
            if (a)
                for (int c = 0; c < f.posCnt; c++) v.pos[c] = ReadComp(a + c * kCompSize[f.posFmt], f.posFmt, posScale, le);
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
                if (a) ReadColor(a, f.colFmt[c], v.col[c], le);
            }
        }
        for (int i = 0; i < 8; i++) {
            t = (hi >> (i * 2)) & 3;
            if (!t) continue;
            int cnt = f.texCnt[i] ? 2 : 1;
            const u8* a = attrPtr(t, 4 + i, cnt * kCompSize[f.texFmt[i]]);
            if (a)
                for (int c = 0; c < cnt; c++) v.tex[i][c] = ReadComp(a + c * kCompSize[f.texFmt[i]], f.texFmt[i], texScale[i], le);
        }
    }

    // Lighting channel 0 colour: vertex or material register (XF 0x100E/0x1010 bit 0 = source).
    u32 c0 = g_gx.xf[0x100E], a0 = g_gx.xf[0x1010], mat = g_gx.xf[0x100C];
    bool colVtx = c0 & 1, alpVtx = a0 & 1;
    int numChans = g_gx.xf[0x1009] & 3;

    // TEV: generated shader (sdk/gxtev.cpp) with every texture map the stages use bound on its own unit;
    // fallback = stage 0 approximation (texture x rasterised colour). The GL state is set up again only when a
    // BP register changed since the last draw (gxr_state_dirty); until then draws are batched.
    u32 order = g_gx.bp[0x28];
    int texMap = order & 7, texCoord = (order >> 3) & 7;
    int nStages = ((g_gx.bp[0x00] >> 10) & 15) + 1, nTexGens = g_gx.bp[0x00] & 15;
    g_nDraw++;
    if (g_stateDirty || !g_batchValid) {
        g_nApply++;
        gxr_flush_batch();
        if (g_bTev) gxt_end();
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
        g_bTev = gxt_begin(texOk);
        if (g_bTev) glDisable(GL_ALPHA_TEST);
        g_bTexOn = !g_bTev && ((order >> 6) & 1) && BindTexMap(texMap);
        if (g_bTev) {
            glDisable(GL_TEXTURE_2D);
        } else if (g_bTexOn) {
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        g_bNTex = g_bTev ? (nTexGens < 8 ? nTexGens : 8) : g_bTexOn ? 1 : 0;
        g_stateDirty = false;
        g_batchValid = true;
    }
    bool tev = g_bTev, texOn = g_bTexOn;

    if (LogThisFrame()) {
        float o[4];
        Transform(vs[0].pos, vs[0].pnmtx, o);
        fprintf(stderr, "[draw] prim %02x vat %d n %d pnmtx %d pos (%.2f %.2f %.2f) -> efb (%.1f %.1f z %.0f w %.3f) "
                        "col %02x%02x%02x%02x chans %d matsrc %d tex %d map %d (img0 %06x img3 %06x) blend %06x z %06x proj %u\n",
                prim, vat, count, vs[0].pnmtx, vs[0].pos[0], vs[0].pos[1], vs[0].pos[2], o[0], o[1], o[2], o[3],
                vs[0].col[0][0], vs[0].col[0][1], vs[0].col[0][2], vs[0].col[0][3], numChans, colVtx, texOn, texMap,
                g_gx.bp[0x88 + texMap] & 0xFFFFFF, g_gx.bp[0x94 + texMap] & 0xFFFFFF, g_gx.bp[0x41] & 0xFFFFFF,
                g_gx.bp[0x40] & 0xFFFFFF, g_gx.xf[0x1026]);
        fprintf(stderr, "    proj %.4f %.4f %.4f %.4f %.4f %.4f viewport %.1f %.1f %.1f %.1f mtx row0 %.4f %.4f %.4f %.2f row1 %.4f %.4f %.4f %.2f row2 %.4f %.4f %.4f %.2f\n",
                xff(0x1020), xff(0x1021), xff(0x1022), xff(0x1023), xff(0x1024), xff(0x1025), xff(0x101A),
                xff(0x101B), xff(0x101D), xff(0x101E), xff(vs[0].pnmtx * 4), xff(vs[0].pnmtx * 4 + 1),
                xff(vs[0].pnmtx * 4 + 2), xff(vs[0].pnmtx * 4 + 3), xff(vs[0].pnmtx * 4 + 4), xff(vs[0].pnmtx * 4 + 5),
                xff(vs[0].pnmtx * 4 + 6), xff(vs[0].pnmtx * 4 + 7), xff(vs[0].pnmtx * 4 + 8), xff(vs[0].pnmtx * 4 + 9),
                xff(vs[0].pnmtx * 4 + 10), xff(vs[0].pnmtx * 4 + 11));
        {
            int pt = (lo >> 9) & 3;
            fprintf(stderr, "    vcd %08x/%08x vat %08x/%08x/%08x posfmt %d cnt %d frac %d postype %d base %08x stride %u data",
                    lo, hi, g_gx.cp[0x70 + vat], g_gx.cp[0x80 + vat], g_gx.cp[0x90 + vat], f.posFmt, f.posCnt, f.posFrac,
                    pt, g_gx.cp[0xA0], g_gx.cp[0xB0] & 0xFF);
            for (int k = 0; k < 16; k++) fprintf(stderr, " %02x", data[k]);
            if (pt >= 2) {
                u32 idx = pt == 2 ? data[(lo & 1) + 0] : be16(data);
                const u8* a = Phys(g_gx.cp[0xA0]) + idx * (g_gx.cp[0xB0] & 0xFF);
                fprintf(stderr, " | arr[%u]", idx);
                for (int k = 0; k < 16; k++) fprintf(stderr, " %02x", a[k]);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "    tev stages %d:", nStages);
        for (int st = 0; st < nStages; st++) fprintf(stderr, " [c %06x a %06x]", g_gx.bp[0xC0 + st * 2] & 0xFFFFFF, g_gx.bp[0xC1 + st * 2] & 0xFFFFFF);
        fprintf(stderr, " swap");
        for (int i = 0; i < 8; i++) fprintf(stderr, " %06x", g_gx.bp[0xF6 + i] & 0xFFFFFF);
        fprintf(stderr, " reg");
        for (int i = 0; i < 8; i++) fprintf(stderr, " %06x", g_gx.tevReg[i]);
        fprintf(stderr, " konst");
        for (int i = 0; i < 8; i++) fprintf(stderr, " %06x", g_gx.tevKonst[i]);
        fprintf(stderr, " order %06x pe_ctrl %06x\n", g_gx.bp[0x28] & 0xFFFFFF, g_gx.bp[0x43] & 0xFFFFFF);
        for (int g = 0; g < nTexGens && g < 8; g++) {
            int m = vs[0].texmtx[g] * 4;
            float st[2];
            TexCoord(vs[0], g, st);
            fprintf(stderr, "    texgen %d info %08x post %08x mtx %d [%.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f] -> (%.4f %.4f)\n",
                    g, g_gx.xf[0x1040 + g], g_gx.xf[0x1050 + g], vs[0].texmtx[g], xff(m), xff(m + 1), xff(m + 2), xff(m + 3),
                    xff(m + 4), xff(m + 5), xff(m + 6), xff(m + 7), xff(m + 8), xff(m + 9), xff(m + 10), xff(m + 11), st[0], st[1]);
        }
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
    // Vertices for the batch: transformed, coloured and texture-mapped on the CPU (as the GX transform unit does).
    // Triangle kinds write straight into the batch (each vertex once; the triangles are indices); lines and points
    // use their own array and are drawn at once.
    int kind = (prim >> 3) & 7;
    bool tri = kind <= 4;
    static std::vector<u8> lineV;
    const int stride = BvStride(g_bNTex);
    size_t base = g_batchN;
    u8* bvBytes;
    if (tri) {
        g_batch.resize((g_batchN + count) * stride);
        bvBytes = &g_batch[g_batchN * stride];
        g_batchN += count;
    } else {
        if (lineV.size() < (size_t)count * stride) lineV.resize((size_t)count * stride);
        bvBytes = lineV.data();
    }
    auto bvAt = [&](int n) -> BV& { return *(BV*)(bvBytes + (size_t)n * stride); };  // only the first `stride` bytes
    bool cv1 = g_gx.xf[0x100F] & 1, av1 = g_gx.xf[0x1011] & 1;
    u32 m1 = g_gx.xf[0x100D];
    for (int n = 0; n < count; n++) {
        Vtx& v = vs[n];
        BV& o = bvAt(n);
        if (numChans == 0) {
            o.c0[0] = o.c0[1] = o.c0[2] = o.c0[3] = 255;
        } else {
            o.c0[0] = colVtx ? v.col[0][0] : (u8)(mat >> 24);
            o.c0[1] = colVtx ? v.col[0][1] : (u8)(mat >> 16);
            o.c0[2] = colVtx ? v.col[0][2] : (u8)(mat >> 8);
            o.c0[3] = alpVtx ? v.col[0][3] : (u8)mat;
        }
        if (numChans < 2) {
            o.c1[0] = o.c1[1] = o.c1[2] = o.c1[3] = 255;
        } else {
            o.c1[0] = cv1 ? v.col[1][0] : (u8)(m1 >> 24);
            o.c1[1] = cv1 ? v.col[1][1] : (u8)(m1 >> 16);
            o.c1[2] = cv1 ? v.col[1][2] : (u8)(m1 >> 8);
            o.c1[3] = av1 ? v.col[1][3] : (u8)m1;
        }
        if (tev) {
            for (int g = 0; g < g_bNTex; g++) TexCoord(v, g, o.tc[g]);
        } else if (texOn) {
            TexCoord(v, texCoord, o.tc[0]);
        }
        float x[4];
        Transform(v.pos, v.pnmtx, x, o.pos);
    }
    {  // NICECRASH_BIGLOG=<n>: log the first n draws with a vertex far outside the screen (bad positions/matrices)
        static int left = NC_ENV("NICECRASH_BIGLOG") ? atoi(NC_ENV("NICECRASH_BIGLOG")) : 0;
        if (left > 0) {
            for (int n = 0; n < count; n++) {
                float ex = bvAt(n).pos[0] / bvAt(n).pos[3], ey = bvAt(n).pos[1] / bvAt(n).pos[3];
                if (ex > -3000 && ex < 3000 && ey > -3000 && ey < 3000) continue;
                left--;
                Vtx& v = vs[n];
                fprintf(stderr, "[big] tex %08x v%d/%d efb (%.0f %.0f) pos (%g %g %g) pnmtx %d vcd %08x/%08x vat %d %08x/%08x/%08x base %08x stride %u le %d mtx [%g %g %g %g / %g %g %g %g / %g %g %g %g]\n",
                        (g_gx.bp[0x94 + (g_gx.bp[0x28] & 3)] & 0xFFFFFF) << 5, n, count, ex, ey, v.pos[0], v.pos[1], v.pos[2], v.pnmtx, lo, hi, vat, g_gx.cp[0x70 + vat],
                        g_gx.cp[0x80 + vat], g_gx.cp[0x90 + vat], g_gx.cp[0xA0], g_gx.cp[0xB0] & 0xFF, (int)arraysLE,
                        xff(v.pnmtx * 4), xff(v.pnmtx * 4 + 1), xff(v.pnmtx * 4 + 2), xff(v.pnmtx * 4 + 3), xff(v.pnmtx * 4 + 4),
                        xff(v.pnmtx * 4 + 5), xff(v.pnmtx * 4 + 6), xff(v.pnmtx * 4 + 7), xff(v.pnmtx * 4 + 8),
                        xff(v.pnmtx * 4 + 9), xff(v.pnmtx * 4 + 10), xff(v.pnmtx * 4 + 11));
                break;
            }
        }
    }
    // Triangles join the batch; lines and points are drawn on their own.
    auto add = [&](int i) { g_batchIdx.push_back((unsigned)(base + i)); };
    switch (kind) {
    case 0: case 1:  // quads
        for (int i = 0; i + 3 < count; i += 4) { add(i); add(i + 1); add(i + 2); add(i); add(i + 2); add(i + 3); }
        break;
    case 2:  // triangles
        for (int i = 0; i + 2 < count; i += 3) { add(i); add(i + 1); add(i + 2); }
        break;
    case 3:  // strip (alternate winding; culling is off)
        for (int i = 0; i + 2 < count; i++) {
            if (i & 1) { add(i + 1); add(i); add(i + 2); }
            else { add(i); add(i + 1); add(i + 2); }
        }
        break;
    case 4:  // fan
        for (int i = 1; i + 1 < count; i++) { add(0); add(i); add(i + 1); }
        break;
    default: {
        gxr_flush_batch();
        static const GLenum kLine[3] = {GL_LINES, GL_LINE_STRIP, GL_POINTS};
        gxt_draw_arrays(kLine[kind - 5], bvBytes, stride, count, g_bNTex);
        break;
    }
    }
    if (g_batchN > 60000) gxr_flush_batch();
}

// ---- EFB -> texture copies on the GPU ----
// A colour copy is converted by a shader straight from the EFB texture into the copy's host texture (window
// resolution), and at EFB resolution into a small texture that is read back through a pixel buffer object; the
// RAM image (GX tiles, for the game's CPU readers such as drtex's sky blends) is written one display frame later,
// when the read has finished, instead of stalling the GPU mid-frame (glReadPixels was ~25% of the race's frame).
// NICECRASH_CPUCOPY=1: the old synchronous path for every copy. Depth copies always take it.
#define GL_PIXEL_PACK_BUFFER_ 0x88EB
#define GL_STREAM_READ_ 0x88E1
#define GL_READ_ONLY_ 0x88B8
typedef void(APIENTRY* GenBuffers_t)(GLsizei, GLuint*);
typedef void(APIENTRY* BindBuffer_t)(GLenum, GLuint);
typedef void(APIENTRY* BufferData_t)(GLenum, ptrdiff_t, const void*, GLenum);
typedef void*(APIENTRY* MapBuffer_t)(GLenum, GLenum);
typedef GLboolean(APIENTRY* UnmapBuffer_t)(GLenum);
GenBuffers_t p_genBuffers;
BindBuffer_t p_bindBuffer;
BufferData_t p_bufferData;
MapBuffer_t p_mapBuffer;
UnmapBuffer_t p_unmapBuffer;

bool LoadPbo() {
    static int ok = -1;
    if (ok < 0) {
        p_genBuffers = (GenBuffers_t)(void*)wglGetProcAddress("glGenBuffers");
        p_bindBuffer = (BindBuffer_t)(void*)wglGetProcAddress("glBindBuffer");
        p_bufferData = (BufferData_t)(void*)wglGetProcAddress("glBufferData");
        p_mapBuffer = (MapBuffer_t)(void*)wglGetProcAddress("glMapBuffer");
        p_unmapBuffer = (UnmapBuffer_t)(void*)wglGetProcAddress("glUnmapBuffer");
        ok = p_genBuffers && p_bindBuffer && p_bufferData && p_mapBuffer && p_unmapBuffer;
    }
    return ok;
}

struct PendingCopy {
    GLuint pbo;
    u32 dst, stride;
    int real, tw, th, frame;
};
std::vector<PendingCopy> g_pendingCopies;
std::vector<GLuint> g_freePbos;

// Write the RAM images of finished GPU copies: those issued before display frame `before` (all with -1).
void ResolveCopies(int before) {
    size_t keep = 0;
    for (size_t i = 0; i < g_pendingCopies.size(); i++) {
        PendingCopy& pc = g_pendingCopies[i];
        if (before >= 0 && pc.frame >= before) {
            g_pendingCopies[keep++] = pc;
            continue;
        }
        p_bindBuffer(GL_PIXEL_PACK_BUFFER_, pc.pbo);
        auto it = g_efbCopies.find(pc.dst);
        if (const u8* m = it != g_efbCopies.end() ? (const u8*)p_mapBuffer(GL_PIXEL_PACK_BUFFER_, GL_READ_ONLY_) : nullptr) {
            std::vector<u8> small(m, m + (size_t)pc.tw * pc.th * 4);
            p_unmapBuffer(GL_PIXEL_PACK_BUFFER_);
            EncodeCopy((u8*)Phys(pc.dst), pc.real, small, pc.tw, pc.th, pc.stride);
            if (--it->second.pending <= 0) {
                it->second.pending = 0;
                it->second.ramSum = Checksum(Phys(pc.dst), it->second.bytes);
            }
        } else if (it != g_efbCopies.end() && --it->second.pending < 0) {
            it->second.pending = 0;
        }
        p_bindBuffer(GL_PIXEL_PACK_BUFFER_, 0);
        g_freePbos.push_back(pc.pbo);
    }
    g_pendingCopies.resize(keep);
}

void QuadFromEfb(float u0, float u1, float vTop, float vBot) {
    glBegin(GL_QUADS);
    glTexCoord2f(u0, vTop); glVertex2f(0, 0);
    glTexCoord2f(u1, vTop); glVertex2f(1, 0);
    glTexCoord2f(u1, vBot); glVertex2f(1, 1);
    glTexCoord2f(u0, vBot); glVertex2f(0, 1);
    glEnd();
}

// Returns false (nothing changed) when the GPU path is not available; the caller then copies on the CPU.
bool GpuCopy(EfbCopy& c, u32 dst, int real, bool inten, int px0, int py0, int pw, int ph, int ww, int wh, int tw,
             int th, bool ram) {
    static const bool cpu = NC_ENV("NICECRASH_CPUCOPY") != nullptr;
    if (cpu || !LoadPbo()) return false;
    bool fresh = !c.id;
    if (fresh) glGenTextures(1, &c.id);
    if (fresh || c.tw != pw || c.th != ph) {
        glBindTexture(GL_TEXTURE_2D, c.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        c.tw = pw;
        c.th = ph;
    }
    if (!xfb_target_tex(c.id, pw, ph)) return false;
    if (!gxt_copy_begin(real, inten, (g_gx.bp[0x43] & 7) == 1)) {
        xfb_bind_efb(ww, wh);
        return false;
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_CULL_FACE);
    glColorMask(1, 1, 1, 1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gxt_active_texture(0);
    glBindTexture(GL_TEXTURE_2D, xfb_efb_color());
    // Output row 0 (texture t = 0) is the copy's top line, as the CPU path uploads it.
    float u0 = (float)px0 / ww, u1 = (float)(px0 + pw) / ww, vTop = 1.0f - (float)py0 / wh,
          vBot = 1.0f - (float)(py0 + ph) / wh;
    QuadFromEfb(u0, u1, vTop, vBot);
    if (ram) {
        static GLuint small;
        static int sw, sh;
        if (!small) glGenTextures(1, &small);
        if (sw != tw || sh != th) {
            glBindTexture(GL_TEXTURE_2D, small);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            sw = tw;
            sh = th;
        }
        if (xfb_target_tex(small, tw, th)) {
            glBindTexture(GL_TEXTURE_2D, xfb_efb_color());
            QuadFromEfb(u0, u1, vTop, vBot);
            GLuint pbo;
            if (!g_freePbos.empty()) {
                pbo = g_freePbos.back();
                g_freePbos.pop_back();
            } else {
                p_genBuffers(1, &pbo);
            }
            p_bindBuffer(GL_PIXEL_PACK_BUFFER_, pbo);
            static std::unordered_map<GLuint, int> sizes;  // storage is (re)specified only when the size changes
            int& sz = sizes[pbo];
            if (sz != tw * th * 4) {
                sz = tw * th * 4;
                p_bufferData(GL_PIXEL_PACK_BUFFER_, (ptrdiff_t)sz, nullptr, GL_STREAM_READ_);
            }
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            p_bindBuffer(GL_PIXEL_PACK_BUFFER_, 0);
            g_pendingCopies.push_back({pbo, dst, g_gx.bp[0x4D] & 0x3FF, real, tw, th, g_frameNo});
            c.pending++;
        }
    }
    gxt_copy_end();
    xfb_bind_efb(ww, wh);
    c.bytes = (size_t)((tw + 7) & ~7) * ((th + 7) & ~7) * kCopyBpp[real] / 8;
    if (!c.pending) c.ramSum = Checksum(Phys(dst), c.bytes);
    g_stateDirty = true;
    return true;
}

void gxr_copy(u32 bp52) {
    gxr_end_batch();
    bool toXfb = (bp52 >> 14) & 1, clear = (bp52 >> 11) & 1;
    if (toXfb && NC_ENV("NICECRASH_COPYLOG")) {
        static int n;
        if (n++ < 3)
            fprintf(stderr, "[copy] src %06x size %06x yscale %06x dst stride %06x bp52 %06x\n", g_gx.bp[0x49] & 0xFFFFFF,
                    g_gx.bp[0x4A] & 0xFFFFFF, g_gx.bp[0x4E] & 0xFFFFFF, g_gx.bp[0x4D] & 0xFFFFFF, bp52 & 0xFFFFFF);
    }
    int ww, wh;
    sdk_vi_window_size(&ww, &wh);
    // Source rectangle (BP 0x49 top-left, 0x4A size - 1) in window pixels; GL rows count from the bottom.
    u32 tl = g_gx.bp[0x49], sz = g_gx.bp[0x4A];
    int ex = tl & 0x3FF, ey = (tl >> 10) & 0x3FF, ew = (int)(sz & 0x3FF) + 1, eh = (int)((sz >> 10) & 0x3FF) + 1;
    int px0 = ex * ww / 640, px1 = (ex + ew) * ww / 640, py0 = ey * wh / 480, py1 = (ey + eh) * wh / 480;
    if (px1 > ww) px1 = ww;
    if (py1 > wh) py1 = wh;
    int pw = px1 - px0, ph = py1 - py0;
    static const bool noEfbCopy = NC_ENV("NICECRASH_NOEFBCOPY") != nullptr;
    if (!toXfb && !noEfbCopy && xfb_active() && pw > 0 && ph > 0) {
        xfb_bind_efb(ww, wh);
        int tpf = (bp52 >> 3) & 15, real = (tpf >> 1) | ((tpf & 1) << 3);
        bool inten = (bp52 >> 15) & 1, half = (bp52 >> 9) & 1;
        bool depth = (g_gx.bp[0x43] & 7) == 3;  // PE_CONTROL pixel format Z24: a depth copy
        u32 dst = (g_gx.bp[0x4B] & 0xFFFFFF) << 5;
        int gtw = half ? ew / 2 : ew, gth = half ? eh / 2 : eh;
        bool gpu = !depth && !NC_ENV("NICECRASH_COPYDUMP") &&
                   GpuCopy(g_efbCopies[dst], dst, real, inten, px0, py0, pw, ph, ww, wh, gtw, gth,
                           gtw > 0 && gth > 0 && dst && !NC_ENV("NICECRASH_NOCOPYRAM"));
        if (gpu) {
            if (NC_ENV("NICECRASH_COPYLOG") || g_frameNo == g_logFrame) {
                static int n;
                if (n++ < 40 || g_frameNo == g_logFrame)
                    fprintf(stderr, "[copy] tex %08x real %d%s%s gpu src (%d,%d %dx%d) -> %dx%d\n", dst, real,
                            inten ? " I" : "", half ? " half" : "", ex, ey, ew, eh, gtw, gth);
            }
        } else {
        std::vector<u8> rgba((size_t)pw * ph * 4);
        std::vector<float> z;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if (depth) {
            z.resize((size_t)pw * ph);
            glReadPixels(px0, wh - py1, pw, ph, GL_DEPTH_COMPONENT, GL_FLOAT, z.data());
        } else {
            glReadPixels(px0, wh - py1, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
        // Convert in place to what a texture of the target format samples as; flip rows (GX row 0 = top).
        std::vector<u8> out((size_t)pw * ph * 4);
        for (int y = 0; y < ph; y++) {
            for (int x = 0; x < pw; x++) {
                size_t si = (size_t)(ph - 1 - y) * pw + x;
                u8* o = &out[((size_t)y * pw + x) * 4];
                u8 r, g, b, a;
                if (depth) {
                    u32 zv = (u32)(z[si] * 16777215.0f + 0.5f);
                    r = (u8)(zv >> 16); g = (u8)(zv >> 8); b = (u8)zv; a = 255;
                } else {
                    const u8* s = &rgba[si * 4];
                    r = s[0]; g = s[1]; b = s[2]; a = (g_gx.bp[0x43] & 7) == 1 ? s[3] : 255;  // alpha only in RGBA6
                }
                u8 i = r;
                if (inten && !depth) i = (u8)(0.257f * r + 0.504f * g + 0.098f * b + 16.0f);
                switch (real) {
                case 0: case 1: case 8: o[0] = o[1] = o[2] = o[3] = i; break;           // I4/I8/R8 (Z4/Z8/Z8M)
                case 2: case 3: o[0] = o[1] = o[2] = i; o[3] = depth ? g : a; break;    // IA4/IA8 (Z16)
                case 7: o[0] = o[1] = o[2] = o[3] = a; break;                           // A8
                case 9: o[0] = o[1] = o[2] = o[3] = g; break;                           // G8 (Z8M)
                case 10: o[0] = o[1] = o[2] = o[3] = b; break;                          // B8 (Z8L)
                case 11: o[0] = o[1] = o[2] = g; o[3] = r; break;                       // RG8 (IA8: A = R)
                case 12: o[0] = o[1] = o[2] = b; o[3] = g; break;                       // GB8
                case 4: o[0] = r; o[1] = g; o[2] = b; o[3] = 255; break;                // RGB565
                default: o[0] = r; o[1] = g; o[2] = b; o[3] = a; break;                 // RGB5A3, RGBA8
                }
            }
        }
        int tw = half ? ew / 2 : ew, th = half ? eh / 2 : eh;
        if (NC_ENV("NICECRASH_COPYDUMP")) {  // each copy target once, as build/tex/copy_<addr>_r<fmt>.bmp
            static std::unordered_map<u32, int> seen;
            if (seen[dst]++ == 30) {
                char path[128];
                CreateDirectoryA("build/tex", nullptr);
                snprintf(path, sizeof path, "build/tex/copy_%08x_r%d%s.bmp", dst, real, depth ? "z" : "");
                if (FILE* fp = fopen(path, "wb")) {
                    BITMAPFILEHEADER fh{};
                    BITMAPINFOHEADER ih{};
                    ih.biSize = sizeof ih; ih.biWidth = pw; ih.biHeight = -ph; ih.biPlanes = 1; ih.biBitCount = 32;
                    fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + pw * ph * 4;
                    fwrite(&fh, sizeof fh, 1, fp); fwrite(&ih, sizeof ih, 1, fp);
                    for (size_t i = 0; i < out.size(); i += 4) {
                        u8 px[4] = {out[i + 2], out[i + 1], out[i], 255};
                        fwrite(px, 4, 1, fp);
                    }
                    fclose(fp);
                }
            }
        }
        EfbCopy& c = g_efbCopies[dst];
        if (!c.id) glGenTextures(1, &c.id);
        glBindTexture(GL_TEXTURE_2D, c.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        // RAM copy at EFB resolution (the game reads some copies with the CPU: drtex blends sky textures).
        if (tw > 0 && th > 0 && dst && !NC_ENV("NICECRASH_NOCOPYRAM")) {
            std::vector<u8> small((size_t)tw * th * 4);
            for (int y = 0; y < th; y++)
                for (int x = 0; x < tw; x++) {
                    int sx = (int)((x + 0.5f) * pw / tw), sy = (int)((y + 0.5f) * ph / th);
                    memcpy(&small[((size_t)y * tw + x) * 4], &out[((size_t)sy * pw + sx) * 4], 4);
                }
            EncodeCopy((u8*)Phys(dst), real, small, tw, th, g_gx.bp[0x4D] & 0x3FF);
        }
        c.bytes = (size_t)((tw + 7) & ~7) * ((th + 7) & ~7) * kCopyBpp[real] / 8;
        c.ramSum = Checksum(Phys(dst), c.bytes);
        if (NC_ENV("NICECRASH_COPYLOG") || g_frameNo == g_logFrame) {
            static int n;
            if (n++ < 40 || g_frameNo == g_logFrame)
                fprintf(stderr, "[copy] tex %08x real %d%s%s%s src (%d,%d %dx%d) -> %dx%d\n", dst, real, inten ? " I" : "",
                        half ? " half" : "", depth ? " Z" : "", ex, ey, ew, eh, tw, th);
        }
        }  // CPU path
    }
    if (toXfb) {
        // NICECRASH_FRAMETIME=1: every 120 display frames, the game's CPU-held time per frame (work, not waiting) as
        // a histogram against the 60 fps (16.7 ms) and 30 fps (33.3 ms) budgets.
        static const bool ft = NC_ENV("NICECRASH_FRAMETIME") != nullptr;
        if (ft) {
            double sdk_cpu_busy_ms();
            static double last = sdk_cpu_busy_ms(), sum;
            static int n, under16, under33, over;
            static unsigned fl, ap, dr;
            fl += g_nFlush; ap += g_nApply; dr += g_nDraw;
            g_nFlush = g_nApply = g_nDraw = 0;
            double now = sdk_cpu_busy_ms(), d = now - last;
            last = now;
            sum += d;
            (d < 16.7 ? under16 : d < 33.3 ? under33 : over)++;
            if (++n == 120) {
                fprintf(stderr, "[ft] per frame: %u draws, %u state applies, %u GL batches\n", dr / n, ap / n, fl / n);
                fl = ap = dr = 0;
                fprintf(stderr, "[ft] retrace %u: work %.1f ms/frame; <16.7 ms %d, <33.3 ms %d, more %d\n",
                        sdk_vi_retrace_count(), sum / n, under16, under33, over);
                n = under16 = under33 = over = 0;
                sum = 0;
            }
        }
        // RAM images of GPU copies issued before the previous display copy have finished reading back by now.
        if (!g_pendingCopies.empty()) ResolveCopies(g_frameNo);
        if (xfb_active()) {  // the EFB goes to its XFB; the VI shows the one the game selects at the retrace
            xfb_bind_efb(ww, wh);
            sdk_vi_note_copy();
            // A copy of an EFB nothing was drawn into since the last copy (the host's timing gives the game the odd
            // empty frame; on the Wii they do not reach the screen) keeps the XFB's previous picture.
            if (g_drawsSinceCopy || NC_ENV("NICECRASH_EMPTYCOPIES")) {
                if (NC_ENV("NICECRASH_XFBLOG")) fprintf(stderr, "[xfb] draws in frame %u\n", g_drawsSinceCopy);
                xfb_copy(g_gx.bp[0x4B] << 5, (int)((g_gx.bp[0x4A] >> 10) & 0x3FF) + 1);
            } else if (NC_ENV("NICECRASH_XFBLOG")) {
                fprintf(stderr, "[xfb] empty copy skipped\n");
            }
            g_drawsSinceCopy = 0;
        } else {
            sdk_vi_present_now();
        }
        g_frameNo++;
    }
    if (clear) {
        xfb_bind_efb(ww, wh);
        u32 ar = g_gx.bp[0x4F], gb = g_gx.bp[0x50], z = g_gx.bp[0x51] & 0xFFFFFF;
        // The clear covers the copied rectangle only (a texture copy in the corner keeps the rest of the EFB).
        if (pw > 0 && ph > 0) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(px0, wh - py1, pw, ph);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        glColorMask(1, 1, 1, 1);
        glDepthMask(1);
        glClearColor(((ar) & 0xFF) / 255.0f, ((gb >> 8) & 0xFF) / 255.0f, (gb & 0xFF) / 255.0f,
                     ((ar >> 8) & 0xFF) / 255.0f);
        glClearDepth(z / 16777215.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        g_stateDirty = true;  // masks and scissor changed under the batch state
    }
}
