// GX FIFO interpreter, step 1: parse the whole command stream with correct sizes (so display lists and draws are
// walked exactly), keep the CP/XF/BP register state, and count work per frame. Rendering builds on this.
#include "gxinterp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

const u8* sdk_fifo_data(size_t* len);
void sdk_fifo_consumed(size_t n);

GXRegs g_gx;

namespace {

struct State {
    u32 lastCopyDst;
} S;


GXFrameStats g_stats;

typedef void (*TokenCallback)(u16 token);
typedef void (*DoneCallback)(void);
TokenCallback g_tokenCb;
DoneCallback g_doneCb;

static inline u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }

static int VertexSize(int vat) { return gxr_vertex_size(vat); }

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
    g_gx.bp[reg] = (g_gx.bp[reg] & ~mask) | (v & mask);
    mask = 0xFFFFFF;
    if (reg >= 0xE0 && reg <= 0xE7) ((v >> 23) & 1 ? g_gx.tevKonst : g_gx.tevReg)[reg - 0xE0] = v & 0xFFFFFF;
    if (reg == 0x47 || reg == 0x48) {  // PE token (0x48 also raises the token interrupt)
        u16 token = (u16)(v & 0xFFFF);
        *(volatile u16*)(uintptr_t)(0xCC001000u + 7 * 2) = token;  // PE token register, read by GXReadDrawSync
        if (reg == 0x48 && g_tokenCb) g_tokenCb(token);
    }
    if (reg == 0x45 && (v & 2) && g_doneCb) g_doneCb();  // PE "draw done" (finish interrupt)
    if (reg == 0x65) gxr_tlut_load(g_gx.bp[0x64], g_gx.bp[0x65]);  // load TLUT from memory into TMEM
    if (reg == 0x52) {  // PE copy execute: bit 14 = copy to XFB (display copy) vs texture copy
        g_stats.copies++;
        gxr_copy(g_gx.bp[0x52]);
        if (g_gx.bp[0x52] & (1 << 14)) S.lastCopyDst = g_gx.bp[0x4B] << 5;  // XFB address (BP 0x4B, 32-byte units)
    }
}

static void Run(const u8* p, size_t len, int depth) {
    const u8* end = p + len;
    while (p < end) {
        u8 op = *p++;
        if (op == 0x00 || op == 0x48 || op == 0x44) continue;  // NOP, invalidate vertex cache, metrics
        if (op == 0x08) {  // CP register load
            if (end - p < 5) break;
            g_gx.cp[p[0]] = be32(p + 1);
            g_stats.cpLoads++;
            p += 5;
        } else if (op == 0x10) {  // XF register/memory load
            if (end - p < 4) break;
            u32 h = be32(p);
            p += 4;
            u32 n = (h >> 16) + 1, addr = h & 0xFFFF;
            if ((size_t)(end - p) < n * 4) break;
            for (u32 i = 0; i < n; i++)
                if (addr + i < sizeof(g_gx.xf) / 4) g_gx.xf[addr + i] = be32(p + i * 4);
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
            // Only lists inside mapped Wii memory (MEM1 0x80000000+24 MB, MEM2 0x90000000+64 MB); a bad address is
            // reported once (the Wii's GPU would read garbage there too, but not fault the program).
            bool mem1 = addr >= 0x80000000u && addr + size <= 0x81800000u;
            bool mem2 = addr >= 0x90000000u && addr + size <= 0x94000000u;
            if (depth < 4 && (mem1 || mem2)) {
                Run((const u8*)(uintptr_t)addr, size, depth + 1);
            } else if (depth < 4) {
                static int bad;
                if (bad++ < 5) fprintf(stderr, "gxinterp: display list call to unmapped %08x (+%x) skipped\n", addr, size);
            }
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
            if ((size_t)(end - p) < vs * n) break;
            gxr_draw(op & 0xF8, vat, n, p);
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
