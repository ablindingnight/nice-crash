// Host side of the HOME button library's resources (homeBtn*.arc): its nw4hbm code reads layout (RLYT),
// animation (RLAN) and font (RFNT) files and TPL textures straight from memory as native structs. They are
// big-endian on disc, so each is byte-swapped in place the first time the library asks for it
// (port_hbm_res_fix, called from ArcResourceAccessor's GetResourceSub: patches/hbm-portability.patch.txt).
// Only multi-byte fields are swapped: names, text bytes, colours stored as bytes, and texel/palette data (the GX
// interpreter reads those as Wii memory) stay as they are. Words that hold colours (vtxCols, textCols) are
// swapped to native values: ut::Color's u32 conversions are explicit big-endian on the host (hbmbuild.py).
// A swapped file is recognised by its byte-order mark (0xFEFF reads natively once swapped), a TPL by its
// version word.
// Also here: TPLBind/TPLGet (the SDK's TPL library, which the decomp has no C source for).
#include <cstdint>
#include <cstring>
#include "sdk.h"

namespace {

inline void S16(void* p) { u16* q = (u16*)p; *q = __builtin_bswap16(*q); }
inline void S32(void* p) { u32* q = (u32*)p; *q = __builtin_bswap32(*q); }
inline void S16n(void* p, u32 n) { for (u32 i = 0; i < n; i++) S16((u8*)p + i * 2); }
inline void S32n(void* p, u32 n) { for (u32 i = 0; i < n; i++) S32((u8*)p + i * 4); }
inline u32 R32(const void* p) { return *(const u32*)p; }
inline u16 R16(const void* p) { return *(const u16*)p; }
inline u32 Sig(const char* s) { return (u32)(u8)s[0] << 24 | (u32)(u8)s[1] << 16 | (u32)(u8)s[2] << 8 | (u8)s[3]; }

// The common binary file header: signature, byte-order mark, version, size, header size, block count. Returns the
// block count, or -1 when the file is already native (or not a Nintendo binary file).
int SwapFileHeader(u8* f) {
    if (f[4] != 0xFE || f[5] != 0xFF) return -1;  // big-endian BOM bytes: not yet swapped
    S32(f);
    S16(f + 4);
    S16(f + 6);
    S32(f + 8);
    S16(f + 12);
    S16(f + 14);
    return R16(f + 14);
}

// Signature bytes of a block before its header is swapped.
u32 BlockKind(const u8* b) { return Sig((const char*)b); }

// ---- RLYT ----
void SwapPaneBase(u8* b) { S32n(b + 0x24, 10); }  // translate(3) rotate(3) scale(2) size(2)

void SwapTexCoords(u8* p, u32 n) { S32n(p, n * 8); }  // n sets of 4 VEC2

void SwapMaterial(u8* m) {
    S16n(m + 0x14, 12);  // tevCols: 3 GXColorS10
    S32(m + 0x3C);      // resNum bits
    u32 bits = R32(m + 0x3C);
    auto get = [&](int pos, int n) { return (bits >> pos) & ((1u << n) - 1); };
    u32 texMap = get(0, 4), texSRT = get(4, 4), texCoordGen = get(8, 4), tevSwap = get(12, 1), indSRT = get(13, 2),
        indStage = get(15, 3), tevStage = get(18, 5), alpComp = get(23, 1), blend = get(24, 1), chan = get(25, 1),
        matCol = get(27, 1);
    (void)alpComp, (void)blend, (void)tevStage;
    u8* p = m + 0x40;
    for (u32 i = 0; i < texMap; i++, p += 4) S16(p);  // TexMap: u16 texIdx, u8 wrapS, u8 wrapT
    S32n(p, texSRT * 5);                             // TexSRT: translate(2) rotate scale(2)
    p += texSRT * 20;
    p += texCoordGen * 4;
    p += chan * 4;
    p += matCol * 4;
    p += tevSwap * 4;
    S32n(p, indSRT * 5);  // indirect matrices: TexSRT
    // indirect stages, TEV stages, alpha compare, blend mode: bytes only
}

void SwapLayout(u8* f) {
    int n = SwapFileHeader(f);
    if (n < 0) return;
    u8* b = f + R16(f + 12);
    for (int i = 0; i < n; i++) {
        u32 kind = BlockKind(b);
        S32(b);
        S32(b + 4);
        u32 size = R32(b + 4);
        switch (kind) {
        case 'lyt1':
            S32n(b + 0x0C, 2);  // layout size
            break;
        case 'txl1':
        case 'fnl1': {
            S16(b + 8);
            u32 num = R16(b + 8);
            for (u32 k = 0; k < num; k++) S32(b + 0x0C + k * 8);  // nameStrOffset
            break;
        }
        case 'mat1': {
            S16(b + 8);
            u32 num = R16(b + 8);
            for (u32 k = 0; k < num; k++) {
                S32(b + 0x0C + k * 4);
                SwapMaterial(b + R32(b + 0x0C + k * 4));
            }
            break;
        }
        case 'pan1':
        case 'bnd1':
            SwapPaneBase(b);
            break;
        case 'pic1':
            SwapPaneBase(b);
            S32n(b + 0x4C, 4);  // vtxCols
            S16(b + 0x5C);      // materialIdx
            SwapTexCoords(b + 0x60, b[0x5E]);
            break;
        case 'txt1': {
            SwapPaneBase(b);
            S16n(b + 0x4C, 4);  // textBufBytes textStrBytes materialIdx fontIdx
            S32(b + 0x58);      // textStrOffset
            S32n(b + 0x5C, 2);  // textCols
            S32n(b + 0x64, 4);  // fontSize(2) charSpace lineSpace
            u32 bytes = R16(b + 0x4E);
            S16n(b + R32(b + 0x58), bytes / 2);  // the text, UTF-16
            break;
        }
        case 'wnd1': {
            SwapPaneBase(b);
            S32n(b + 0x4C, 4);  // inflation
            S32(b + 0x60);      // contentOffset
            S32(b + 0x64);      // frameOffsetTableOffset
            u8* c = b + R32(b + 0x60);
            S32n(c, 4);  // content vtxCols
            S16(c + 0x10);
            SwapTexCoords(c + 0x14, c[0x12]);
            u32* frames = (u32*)(b + R32(b + 0x64));
            for (u32 k = 0; k < b[0x5C]; k++) {
                S32(&frames[k]);
                S16(b + frames[k]);  // WindowFrame.materialIdx
            }
            break;
        }
        case 'grp1':
            S16(b + 0x18);  // paneNum (names follow)
            break;
        default:  // pas1 pae1 grs1 gre1: header only
            break;
        }
        b += size;
    }
}

// ---- RLAN ----
void SwapAnimation(u8* f) {
    int n = SwapFileHeader(f);
    if (n < 0) return;
    u8* b = f + R16(f + 12);
    for (int i = 0; i < n; i++) {
        u32 kind = BlockKind(b);
        S32(b);
        S32(b + 4);
        u32 size = R32(b + 4);
        if (kind == 'pai1') {
            S16(b + 8);     // frameSize
            S16(b + 0x0C);  // fileNum
            S16(b + 0x0E);  // animContNum
            S32(b + 0x10);  // animContOffsetsOffset
            S32n(b + 0x14, R16(b + 0x0C));  // file name offsets
            u32* conts = (u32*)(b + R32(b + 0x10));
            for (u32 c = 0; c < R16(b + 0x0E); c++) {
                S32(&conts[c]);
                u8* ac = b + conts[c];  // name[20], num, type
                u32* infos = (u32*)(ac + 0x18);
                for (u32 k = 0; k < ac[0x14]; k++) {
                    S32(&infos[k]);
                    u8* info = ac + infos[k];  // kind, num
                    S32(info);
                    u32* targets = (u32*)(info + 8);
                    for (u32 t = 0; t < info[4]; t++) {
                        S32(&targets[t]);
                        u8* tg = info + targets[t];  // id target curveType pad keyNum pad keysOffset
                        S16(tg + 4);
                        S32(tg + 8);
                        u8* keys = tg + R32(tg + 8);
                        u32 keyNum = R16(tg + 4);
                        if (tg[2] == 1) {  // step: frame, u16 value, pad
                            for (u32 k2 = 0; k2 < keyNum; k2++) {
                                S32(keys + k2 * 8);
                                S16(keys + k2 * 8 + 4);
                            }
                        } else {  // hermite: frame value slope
                            S32n(keys, keyNum * 3);
                        }
                    }
                }
            }
        } else if (kind == 'pat1') {
            // tag block (groups, frame range): u16 order, u16 groupNum, u32 nameOffset, u32 groupsOffset,
            // s16 start, s16 end, u8 descendingBind
            S16n(b + 8, 2);
            S32n(b + 0x0C, 2);
            S16n(b + 0x14, 2);
        }
        b += size;
    }
}

// ---- RFNT ----
void SwapFont(u8* f) {
    int n = SwapFileHeader(f);
    if (n < 0) return;
    u8* b = f + R16(f + 12);
    for (int i = 0; i < n; i++) {
        u32 kind = BlockKind(b);
        S32(b);
        S32(b + 4);
        u32 size = R32(b + 4);
        u8* d = b + 8;
        switch (kind) {
        case 'FINF':
            S16(d + 2);       // alterCharIndex
            S32n(d + 8, 3);   // pGlyph pWidth pMap (offsets)
            break;
        case 'TGLP':
            S32(d + 4);       // sheetSize
            S16n(d + 8, 6);   // sheetNum format row line width height
            S32(d + 0x14);    // sheetImage
            break;
        case 'CWDH':
            S16n(d, 2);       // indexBegin indexEnd
            S32(d + 4);       // pNext
            break;
        case 'CMAP': {
            S16n(d, 4);       // ccodeBegin ccodeEnd mappingMethod reserved
            S32(d + 8);       // pNext
            u16* info = (u16*)(d + 0x0C);
            u32 method = R16(d + 4);
            if (method == 0) {
                S16(info);
            } else if (method == 1) {
                S16n(info, (u32)R16(d + 2) - R16(d) + 1);
            } else {
                S16(info);
                S16n(info + 1, (u32)info[0] * 2);
            }
            break;
        }
        }
        b += size;
    }
}

}  // namespace

extern "C" void port_hbm_res_fix(void* p, u32 size) {
    if (!p || size < 16) return;
    u8* f = (u8*)p;
    switch (Sig((const char*)f)) {
    case 'RLYT': SwapLayout(f); break;
    case 'RLAN': SwapAnimation(f); break;
    case 'RFNT': SwapFont(f); break;
    default: break;  // TPL: TPLBind; native files (already swapped): their signature reads reversed
    }
}

// ---- TPL ----
namespace {
struct TPLHeader {
    u16 height, width;
    u32 format;
    u8* data;
    u32 wrapS, wrapT, minFilter, magFilter;
    f32 LODBias;
    u8 edgeLODEnable, minLOD, maxLOD, unpacked;
};
struct TPLClutHeader {
    u16 numEntries;
    u8 unpacked, pad8;
    u32 format;
    u8* data;
};
struct TPLDescriptor {
    TPLHeader* textureHeader;
    TPLClutHeader* CLUTHeader;
};
struct TPLPalette {
    u32 versionNumber;
    u32 numDescriptors;
    TPLDescriptor* descriptorArray;
};
constexpr u32 kTplVersion = 2142000;  // 0x0020AF30
}  // namespace

extern "C" {

// As the SDK's TPLBind: offsets -> pointers; on the host the headers are byte-swapped first. A bound palette
// (native version word) is left alone.
void TPLBind(TPLPalette* pal) {
    if (pal->versionNumber != __builtin_bswap32(kTplVersion)) return;
    u8* base = (u8*)pal;
    S32n(pal, 3);
    pal->descriptorArray = (TPLDescriptor*)(base + (uintptr_t)pal->descriptorArray);
    for (u32 i = 0; i < pal->numDescriptors; i++) {
        TPLDescriptor* d = &pal->descriptorArray[i];
        S32n(d, 2);
        if (d->textureHeader) {
            d->textureHeader = (TPLHeader*)(base + (uintptr_t)d->textureHeader);
            TPLHeader* h = d->textureHeader;
            if (!h->unpacked) {
                S16n(h, 2);
                S32n(&h->format, 7);  // format data wrapS wrapT minFilter magFilter LODBias
                h->data = base + (uintptr_t)h->data;
                h->unpacked = 1;
            }
        }
        if (d->CLUTHeader) {
            d->CLUTHeader = (TPLClutHeader*)(base + (uintptr_t)d->CLUTHeader);
            TPLClutHeader* c = d->CLUTHeader;
            if (!c->unpacked) {
                S16(&c->numEntries);
                S32n(&c->format, 2);
                c->data = base + (uintptr_t)c->data;
                c->unpacked = 1;
            }
        }
    }
}

TPLDescriptor* TPLGet(TPLPalette* pal, u32 id) { return &pal->descriptorArray[id % pal->numDescriptors]; }

}  // extern "C"

// home.csv: UTF-16 big-endian with a byte-order mark. Swap it to native up to its terminating zero, once (a
// swapped text starts with the native mark 0xFEFF).
extern "C" void port_hbm_text_fix(void* text) {
    u16* t = (u16*)text;
    if (!t || t[0] != 0xFFFE) return;
    for (; *t; t++) S16(t);
}
