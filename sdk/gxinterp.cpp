// GX FIFO interpreter, step 1: parse the whole command stream with correct sizes (so display lists and draws are
// walked exactly), keep the CP/XF/BP register state, and count work per frame. Rendering builds on this.
#include "gxinterp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

const u8* sdk_fifo_data(size_t* len);
void sdk_fifo_consumed(size_t n);

namespace {

struct State {
    u32 cp[256];
    u32 bp[256];
    u32 xf[0x1100];   // XF memory (matrices 0x000-0x4FF, normals 0x400-, post 0x500-, lights 0x600-) + regs 0x1000-
    u32 lastCopyDst;
} S;

GXFrameStats g_stats;

typedef void (*TokenCallback)(u16 token);
typedef void (*DoneCallback)(void);
TokenCallback g_tokenCb;
DoneCallback g_doneCb;

static inline u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }

// Size in bytes of one vertex for vertex format `vat`, from VCD (CP 0x50/0x60) and VAT (CP 0x70/0x80/0x90 + vat).
static int VertexSize(int vat) {
    u32 lo = S.cp[0x50], hi = S.cp[0x60];
    u32 a = S.cp[0x70 + vat], b = S.cp[0x80 + vat], c = S.cp[0x90 + vat];
    static const int compSize[8] = {1, 1, 2, 2, 4, 0, 0, 0};
    static const int colSize[8] = {2, 3, 4, 2, 3, 4, 0, 0};
    int size = 0;
    size += lo & 1;                                  // position/normal matrix index
    for (int i = 0; i < 8; i++) size += (lo >> (1 + i)) & 1;  // texture matrix indices
    auto attr = [&](int type, int direct) { return type == 0 ? 0 : type == 1 ? direct : type == 2 ? 1 : 2; };
    // Position
    int posCnt = (a & 1) ? 3 : 2, posFmt = (a >> 1) & 7;
    size += attr((lo >> 9) & 3, posCnt * compSize[posFmt]);
    // Normal
    int nrmCnt = (a >> 9) & 1 ? 9 : 3, nrmFmt = (a >> 10) & 7;
    int nrmType = (lo >> 11) & 3;
    if (nrmType >= 2 && ((a >> 31) & 1) && nrmCnt == 9) size += (nrmType == 2 ? 1 : 2) * 3;  // NBT index3
    else size += attr(nrmType, nrmCnt * compSize[nrmFmt]);
    // Colours
    size += attr((lo >> 13) & 3, colSize[(a >> 14) & 7]);
    size += attr((lo >> 15) & 3, colSize[(a >> 18) & 7]);
    // Texture coordinates 0-7
    int texCnt[8], texFmt[8];
    texCnt[0] = (a >> 21) & 1; texFmt[0] = (a >> 22) & 7;
    texCnt[1] = b & 1;         texFmt[1] = (b >> 1) & 7;
    texCnt[2] = (b >> 9) & 1;  texFmt[2] = (b >> 10) & 7;
    texCnt[3] = (b >> 18) & 1; texFmt[3] = (b >> 19) & 7;
    texCnt[4] = (b >> 27) & 1; texFmt[4] = (b >> 28) & 7;
    texCnt[5] = (c >> 5) & 1;  texFmt[5] = (c >> 6) & 7;
    texCnt[6] = (c >> 14) & 1; texFmt[6] = (c >> 15) & 7;
    texCnt[7] = (c >> 23) & 1; texFmt[7] = (c >> 24) & 7;
    for (int i = 0; i < 8; i++) size += attr((hi >> (i * 2)) & 3, (texCnt[i] ? 2 : 1) * compSize[texFmt[i]]);
    return size;
}

static void Run(const u8* p, size_t len, int depth);

static void LoadBP(u32 v) {
    g_stats.bpLoads++;
    u32 reg = v >> 24;
    // BP masked writes: register 0xFE sets the mask for the next write.
    static u32 mask = 0xFFFFFF;
    if (reg == 0xFE) {
        mask = v & 0xFFFFFF;
        return;
    }
    S.bp[reg] = (S.bp[reg] & ~mask) | (v & mask);
    mask = 0xFFFFFF;
    if (reg == 0x47 || reg == 0x48) {  // PE token (0x48 also raises the token interrupt)
        u16 token = (u16)(v & 0xFFFF);
        *(volatile u16*)(uintptr_t)(0xCC001000u + 7 * 2) = token;  // PE token register, read by GXReadDrawSync
        if (reg == 0x48 && g_tokenCb) g_tokenCb(token);
    }
    if (reg == 0x45 && (v & 2) && g_doneCb) g_doneCb();  // PE "draw done" (finish interrupt)
    if (reg == 0x52) {  // PE copy execute: bit 14 = copy to XFB (display copy) vs texture copy
        g_stats.copies++;
        if (S.bp[0x52] & (1 << 14)) S.lastCopyDst = S.bp[0x4B] << 5;  // XFB address (BP 0x4B, 32-byte units)
    }
}

static void Run(const u8* p, size_t len, int depth) {
    const u8* end = p + len;
    while (p < end) {
        u8 op = *p++;
        if (op == 0x00 || op == 0x48 || op == 0x44) continue;  // NOP, invalidate vertex cache, metrics
        if (op == 0x08) {  // CP register load
            if (end - p < 5) break;
            S.cp[p[0]] = be32(p + 1);
            g_stats.cpLoads++;
            p += 5;
        } else if (op == 0x10) {  // XF register/memory load
            if (end - p < 4) break;
            u32 h = be32(p);
            p += 4;
            u32 n = (h >> 16) + 1, addr = h & 0xFFFF;
            if ((size_t)(end - p) < n * 4) break;
            for (u32 i = 0; i < n; i++)
                if (addr + i < sizeof(S.xf) / 4) S.xf[addr + i] = be32(p + i * 4);
            p += n * 4;
            g_stats.xfLoads++;
        } else if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {  // indexed XF loads (A-D)
            p += 4;
            g_stats.idxLoads++;
        } else if (op == 0x40) {  // call display list
            if (end - p < 8) break;
            u32 addr = be32(p), size = be32(p + 4);
            p += 8;
            g_stats.displayLists++;
            if (depth < 4 && addr >= 0x80000000u) Run((const u8*)(uintptr_t)addr, size, depth + 1);
        } else if (op == 0x61) {  // BP register load
            if (end - p < 4) break;
            LoadBP(be32(p));
            p += 4;
        } else if (op >= 0x80 && op < 0xC0) {  // draw primitive
            if (end - p < 2) break;
            int vat = op & 7;
            u16 n = be16(p);
            p += 2;
            size_t vs = VertexSize(vat);
            p += vs * n;
            g_stats.draws++;
            g_stats.vertices += n;
        } else {
            g_stats.unknown++;
            if (g_stats.unknown < 8) fprintf(stderr, "[gx] unknown FIFO opcode 0x%02x\n", op);
        }
    }
}

}  // namespace

void gxi_flush() {
    size_t len;
    const u8* d = sdk_fifo_data(&len);
    if (len) {
        g_stats.bytes += (u32)len;
        // NICECRASH_FIFODUMP=<file>: append each flush as [u32 length][bytes] for offline analysis (first 4 MB).
        static FILE* dump = getenv("NICECRASH_FIFODUMP") ? fopen(getenv("NICECRASH_FIFODUMP"), "wb") : nullptr;
        static size_t dumped;
        if (dump && dumped < (4u << 20)) {
            u32 n = (u32)len;
            fwrite(&n, 4, 1, dump);
            fwrite(d, 1, len, dump);
            fflush(dump);
            dumped += len;
        }
        Run(d, len, 0);
    }
    sdk_fifo_consumed(len);
}

GXFrameStats gxi_take_stats() {
    GXFrameStats s = g_stats;
    memset(&g_stats, 0, sizeof(g_stats));
    return s;
}

u32 gxi_last_copy_dst() { return S.lastCopyDst; }

// GPU interrupt callbacks: on the host they fire while the interpreter reaches the token / draw-done commands.
extern "C" TokenCallback GXSetDrawSyncCallback(TokenCallback cb) {
    TokenCallback o = g_tokenCb;
    g_tokenCb = cb;
    return o;
}
extern "C" DoneCallback GXSetDrawDoneCallback(DoneCallback cb) {
    DoneCallback o = g_doneCb;
    g_doneCb = cb;
    return o;
}
