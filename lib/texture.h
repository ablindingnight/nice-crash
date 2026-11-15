// Host port of the tt_texture ("TEX " resource, version 15) loader and the GX texture formats it sets up.
// Sources: decomp src/game/texture.cpp (header, descriptor) and src/game/drtex.cpp (format dispatch,
// palette conversion, raw-image swizzles, GXInitTexObj formats). Output is linear RGBA8 for the host GPU.
#pragma once
#include <string>
#include <vector>
#include "bytes.h"

namespace tex {

// TTHeader (texture.cpp), 0x18 bytes, little-endian u16/u32 on disc (fn_802CA57C swaps w, h, zhint, data).
struct Header {
    u16 w;        // 0x0
    u16 h;        // 0x2
    u8 lods;      // 0x4  texture.cpp calls it `fmt`; fn_802CA3F4 copies it to GXTexDesc::fmt/lods (0x20),
                  //      which drtex.cpp uses as the LOD count (MipMapped(n) = n > 2, maxLod = n - 1)
    u8 flags;     // 0x5  & 0x7F = drtex format code (fn_802CA3F4 d->format), 0x80 = fixed up in memory
    u8 wrapS;     // 0x6  -> GXTexDesc 0x10 (drtex: bit0 S repeat, bit1 T repeat; fn_802D7780)
    u8 wrapT;     // 0x7  -> GXTexDesc 0x14 (drtex: render flags; fn_802D7780)
    u8 prio;      // 0x8
    u8 prio2;     // 0x9
    u8 unkA;      // 0xA  -> GXTexDesc 0x1C
    u8 mips;      // 0xB  -> GXTexDesc 0x18 (bit1: keep filtering, bit2: flag 0x40; fn_802D7780)
    u16 zhint;    // 0xC
    u32 data;     // 0x14 offset of the TexImage from the header start (fn_802CA57C: h + h->data)
};

// TexImage (drtex.cpp), 3 little-endian offsets relative to the TexImage itself (drtex Fix / FixTexImage).
struct Image {
    u32 pal, tex, alpha;  // 0 = absent
};

struct Decoded {
    int w = 0, h = 0;
    std::vector<u8> rgba;  // w*h*4, row-major, top row first
    Header hdr{};
    int format = -1;       // drtex format code (hdr.flags & 0x7F)
    const char* gxName = "?";
    std::string error;
};

// Parse a TEX resource body (the data fn_8012C370 returns) and decode LOD 0 to RGBA8.
bool Decode(const u8* body, s32 size, u32 version, Decoded& out);

}  // namespace tex
