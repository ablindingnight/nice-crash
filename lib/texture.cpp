// Host port of the tt_texture decode path. Every field comes from the decompiled loader:
//   texture.cpp fn_802CA658  ResourceGet(name, 'TEX ') and the version-15 check
//   texture.cpp fn_802CA57C  swap data/w/h/zhint, data offset -> pointer (h + data)
//   texture.cpp fn_802CA3F4  TTHeader -> GXTexDesc (format = flags & 0x7F, lods = byte 4, w, h, data)
//   drtex.cpp   fn_802D8DCC  TexImage offsets -> pointers (relative to the TexImage); swizzle C4/C8
//   drtex.cpp   drCreateTexture  format code -> creator -> GX texture format
// The texel layouts themselves are the GX hardware formats (tiles, big-endian texels); they are
// documented publicly (e.g. Dolphin's TextureDecoder, YAGCD) and implemented here from that description.
#include "texture.h"
#include <cstring>

namespace tex {

static inline u8 X5(u32 v) { return (u8)(v << 3 | v >> 2); }
static inline u8 X6(u32 v) { return (u8)(v << 2 | v >> 4); }
static inline u8 X4(u32 v) { return (u8)(v * 17); }
static inline u8 X3(u32 v) { return (u8)(v << 5 | v << 2 | v >> 1); }

static void Put(Decoded& o, int x, int y, u8 r, u8 g, u8 b, u8 a) {
    if (x >= o.w || y >= o.h) return;
    u8* p = &o.rgba[((size_t)y * o.w + x) * 4];
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static void From565(u16 c, u8* rgb) {
    rgb[0] = X5(c >> 11 & 31);
    rgb[1] = X6(c >> 5 & 63);
    rgb[2] = X5(c & 31);
}

static int Pad(int v, int m) { return (v + m - 1) / m * m; }

// Bytes needed for LOD 0 of a GX format (tiles padded to whole blocks), GXGetTexBufferSize-style.
static size_t GxSize(int gxfmt, int w, int h) {
    switch (gxfmt) {
    case 0: case 8: case 14: return (size_t)Pad(w, 8) * Pad(h, 8) / 2;  // I4, C4, CMPR
    case 1: case 2: case 9: return (size_t)Pad(w, 8) * Pad(h, 4);      // I8, IA4, C8
    case 3: case 4: case 5: return (size_t)Pad(w, 4) * Pad(h, 4) * 2;  // IA8, RGB565, RGB5A3
    case 6: return (size_t)Pad(w, 4) * Pad(h, 4) * 4;                  // RGBA8
    }
    return 0;
}

// GX tiled formats -> RGBA. gxfmt is the GXTexFmt value drtex passes to GXInitTexObj.
static void DecodeGx(int gxfmt, const u8* s, Decoded& o) {
    int w = o.w, h = o.h;
    switch (gxfmt) {
    case 0:  // I4: 8x8 tiles, 4 bits, high nibble first
        for (int by = 0; by < h; by += 8)
            for (int bx = 0; bx < w; bx += 8)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2, s++) {
                        u8 a = X4(*s >> 4), b = X4(*s & 15);
                        Put(o, bx + x, by + y, a, a, a, a);
                        Put(o, bx + x + 1, by + y, b, b, b, b);
                    }
        break;
    case 1:  // I8: 8x4 tiles
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++, s++) Put(o, bx + x, by + y, *s, *s, *s, *s);
        break;
    case 2:  // IA4: 8x4 tiles, alpha high nibble, intensity low nibble
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++, s++) {
                        u8 i = X4(*s & 15);
                        Put(o, bx + x, by + y, i, i, i, X4(*s >> 4));
                    }
        break;
    case 3:  // IA8: 4x4 tiles, alpha byte then intensity byte
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 4)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++, s += 2) Put(o, bx + x, by + y, s[1], s[1], s[1], s[0]);
        break;
    case 4:  // RGB565: 4x4 tiles
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 4)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++, s += 2) {
                        u8 c[3];
                        From565(rd_be16(s), c);
                        Put(o, bx + x, by + y, c[0], c[1], c[2], 255);
                    }
        break;
    case 5:  // RGB5A3: 4x4 tiles; top bit set = RGB555 opaque, else A3 RGB444
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 4)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++, s += 2) {
                        u16 v = rd_be16(s);
                        if (v & 0x8000)
                            Put(o, bx + x, by + y, X5(v >> 10 & 31), X5(v >> 5 & 31), X5(v & 31), 255);
                        else
                            Put(o, bx + x, by + y, X4(v >> 8 & 15), X4(v >> 4 & 15), X4(v & 15), X3(v >> 12 & 7));
                    }
        break;
    case 6:  // RGBA8: 4x4 tiles of 64 bytes, 16 AR pairs then 16 GB pairs
        for (int by = 0; by < h; by += 4)
            for (int bx = 0; bx < w; bx += 4, s += 64)
                for (int i = 0; i < 16; i++)
                    Put(o, bx + (i & 3), by + (i >> 2), s[i * 2 + 1], s[32 + i * 2], s[32 + i * 2 + 1], s[i * 2]);
        break;
    case 14:  // CMPR: 8x8 tiles of four 4x4 DXT1-style blocks (2x2), big-endian colours, 2-bit MSB-first
        for (int by = 0; by < h; by += 8)
            for (int bx = 0; bx < w; bx += 8)
                for (int sb = 0; sb < 4; sb++, s += 8) {
                    u16 c0 = rd_be16(s), c1 = rd_be16(s + 2);
                    u8 pal[4][4];
                    From565(c0, pal[0]);
                    From565(c1, pal[1]);
                    pal[0][3] = pal[1][3] = 255;
                    for (int k = 0; k < 3; k++) {
                        if (c0 > c1) {
                            pal[2][k] = (u8)((2 * pal[0][k] + pal[1][k]) / 3);
                            pal[3][k] = (u8)((pal[0][k] + 2 * pal[1][k]) / 3);
                        } else {
                            pal[2][k] = (u8)((pal[0][k] + pal[1][k]) / 2);
                            pal[3][k] = 0;
                        }
                    }
                    pal[2][3] = 255;
                    pal[3][3] = c0 > c1 ? 255 : 0;
                    int ox = bx + (sb & 1) * 4, oy = by + (sb >> 1) * 4;
                    for (int y = 0; y < 4; y++) {
                        u8 bits = s[4 + y];
                        for (int x = 0; x < 4; x++) {
                            const u8* c = pal[bits >> (6 - x * 2) & 3];
                            Put(o, ox + x, oy + y, c[0], c[1], c[2], c[3]);
                        }
                    }
                }
        break;
    }
}

bool Decode(const u8* body, s32 size, u32 version, Decoded& o) {
    if (size < 0x18) { o.error = "short header"; return false; }
    // fn_802CA658: "tt_texture(%s) bad version (%d)" unless 15.
    if (version != 15) { o.error = "bad version " + std::to_string(version); return false; }
    Header& h = o.hdr;
    h.w = rd_le16(body + 0x0);
    h.h = rd_le16(body + 0x2);
    h.lods = body[0x4];
    h.flags = body[0x5];
    h.wrapS = body[0x6];
    h.wrapT = body[0x7];
    h.prio = body[0x8];
    h.prio2 = body[0x9];
    h.unkA = body[0xA];
    h.mips = body[0xB];
    h.zhint = rd_le16(body + 0xC);
    h.data = rd_le32(body + 0x14);
    o.format = h.flags & 0x7F;  // fn_802CA3F4: d->format = h->flags & 0x7F
    o.w = h.w;
    o.h = h.h;
    if (h.data + 12 > (u32)size) { o.error = "TexImage out of range"; return false; }

    // drtex FixTexImage: three offsets relative to the TexImage.
    const u8* img = body + h.data;
    Image im{rd_le32(img), rd_le32(img + 4), rd_le32(img + 8)};
    auto at = [&](u32 off, size_t need) -> const u8* {
        if (!off || h.data + (size_t)off + need > (size_t)size) return nullptr;
        return img + off;
    };
    o.rgba.assign((size_t)o.w * o.h * 4, 0);

    switch (o.format) {  // drCreateTexture
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x45: case 0x46: {  // create_generic_texture
        static const int gx[] = {4, 5, -1, 0, 1, 2, 3};               // 0x45..0x4B
        static const char* nm[] = {"RGB565", "RGB5A3", "", "I4", "I8", "IA4", "IA8"};
        int f = gx[o.format - 0x45];
        o.gxName = nm[o.format - 0x45];
        const u8* t = at(im.tex, GxSize(f, o.w, o.h));
        if (!t) { o.error = "texel data out of range"; return false; }
        DecodeGx(f, t, o);
        return true;
    }
    case 0: case 1: case 0x40: case 0x41: {  // fn_802D7B5C (CI4/CI8 + RGB565/IA8 TLUTs)
        bool ci4 = o.format == 0 || o.format == 0x40;
        int n = ci4 ? 16 : 256;
        o.gxName = ci4 ? "C4 (TLUT RGB565 + IA8 alpha)" : "C8 (TLUT RGB565 + IA8 alpha)";
        const u8* pal = at(im.pal, (size_t)n * 4);
        const u8* t = at(im.tex, ci4 ? (size_t)o.w * o.h / 2 : (size_t)o.w * o.h);
        if (!pal || !t) { o.error = "palette/texels out of range"; return false; }
        // fn_802D81C4 / fn_802D86BC swap each palette word, so it is little-endian on disc;
        // fn_802D78D4 then reads its bytes in big-endian memory order: R, G, B, A (high byte first).
        // RGB goes through RGB565 (To565) and alpha through an IA8 TLUT (alpha << 8).
        u8 lut[256][4];
        for (int i = 0; i < n; i++) {
            u32 c = rd_le32(pal + i * 4);
            u16 v = (u16)((c >> 16 & 0xF800) | (c >> 13 & 0x7E0) | (c >> 11 & 0x1F));
            From565(v, lut[i]);
            lut[i][3] = (u8)c;
        }
        // The texels are linear on disc; the game tiles them in place (fn_802D81C4 / fn_802D86BC).
        // C4: the game swaps each byte's nibbles first, so the LOW nibble is the left pixel on disc.
        for (int y = 0; y < o.h; y++)
            for (int x = 0; x < o.w; x++) {
                int k = ci4 ? (t[(y * o.w + x) / 2] >> ((x & 1) * 4) & 15) : t[y * o.w + x];
                Put(o, x, y, lut[k][0], lut[k][1], lut[k][2], lut[k][3]);
            }
        return true;
    }
    case 0x10: case 0x17: case 0x42: case 0x43: case 0x11: case 0x44: {  // fn_802D8A84 / fn_802D8C00
        const u8* t = at(im.tex, GxSize(14, o.w, o.h));
        if (!t) { o.error = "CMPR data out of range"; return false; }
        DecodeGx(14, t, o);
        o.gxName = "CMPR";
        if (o.format == 0x11 || o.format == 0x44) {
            // fn_802D8C00: a second GXTexObj, I4 (format 0) at img->alpha, same size: the alpha channel.
            const u8* a = at(im.alpha, GxSize(0, o.w, o.h));
            if (!a) { o.error = "alpha plane out of range"; return false; }
            Decoded tmp;
            tmp.w = o.w;
            tmp.h = o.h;
            tmp.rgba.assign(o.rgba.size(), 0);
            DecodeGx(0, a, tmp);
            for (size_t i = 0; i < o.rgba.size(); i += 4) o.rgba[i + 3] = tmp.rgba[i];
            o.gxName = "CMPR + I4 alpha";
        }
        return true;
    }
    case 2: case 3: case 0x47: {  // fn_802D7D98: 0x47/3 -> RGBA8 (6), 2 -> RGB565 (4)
        int f = o.format == 2 ? 4 : 6;
        o.gxName = f == 6 ? "RGBA8" : "RGB565 (raw)";
        const u8* t = at(im.tex, GxSize(f, o.w, o.h));
        if (!t) { o.error = "raw data out of range"; return false; }
        DecodeGx(f, t, o);
        return true;
    }
    }
    o.error = "drCreateTexture: unknown type 0x" + std::to_string(o.format);
    return false;
}

}  // namespace tex

namespace tex {
bool DecodeRawGX(int gxfmt, const u8* data, int w, int h, std::vector<u8>& rgba) {
    if (GxSize(gxfmt, w, h) == 0) return false;
    Decoded o;
    o.w = w;
    o.h = h;
    o.rgba.assign((size_t)w * h * 4, 0);
    DecodeGx(gxfmt, data, o);
    rgba.swap(o.rgba);
    return true;
}
size_t GXImageSize(int gxfmt, int w, int h) { return GxSize(gxfmt, w, h); }
}  // namespace tex
