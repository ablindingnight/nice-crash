// GX FIFO interpreter, step 1: parse the whole command stream with correct sizes (so display lists and draws are
// walked exactly), keep the CP/XF/BP register state, and count work per frame. Rendering builds on this.
#include "gxinterp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

static size_t Run(const u8* p, size_t len, int depth);

static void LoadBP(u32 v) {
    g_stats.bpLoads++;
    u32 reg = v >> 24;
    // BP masked writes: register 0xFE sets the mask for the next write.
    static u32 mask = 0xFFFFFF;
    if (reg == 0xFE) {
        mask = v & 0xFFFFFF;
        return;
    }
    u32 nv = (g_gx.bp[reg] & ~mask) | (v & mask);
    if (nv != g_gx.bp[reg]) gxr_state_dirty();
    g_gx.bp[reg] = nv;
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

// Returns the bytes consumed: a command cut off at the end (the FIFO was flushed in the middle of it) is left for
// the next flush.
static size_t Run(const u8* p, size_t len, int depth) {
    const u8* end = p + len;
    const u8* start = p;
    const u8* hist[8] = {};  // recent command starts (desync diagnostics)
    int nh = 0;
    while (p < end) {
        hist[nh++ & 7] = p;
        u8 op = *p++;
        if (op == 0x00 || op == 0x48 || op == 0x44) continue;  // NOP, invalidate vertex cache, metrics
        if (op == 0x08) {  // CP register load
            if (end - p < 5) return (size_t)(hist[(nh - 1) & 7] - start);
            g_gx.cp[p[0]] = be32(p + 1);
            g_stats.cpLoads++;
            p += 5;
        } else if (op == 0x10) {  // XF register/memory load
            if (end - p < 4) return (size_t)(hist[(nh - 1) & 7] - start);
            u32 h = be32(p);
            p += 4;
            u32 n = (h >> 16) + 1, addr = h & 0xFFFF;
            if ((size_t)(end - p) < n * 4) return (size_t)(hist[(nh - 1) & 7] - start);
            for (u32 i = 0; i < n; i++)
                if (addr + i < sizeof(g_gx.xf) / 4) g_gx.xf[addr + i] = be32(p + i * 4);
            p += n * 4;
            g_stats.xfLoads++;
        } else if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {  // indexed XF loads (A-D)
            if (end - p < 4) return (size_t)(hist[(nh - 1) & 7] - start);
            // u32: index (31-16), word count - 1 (15-12), XF address (11-0); the words come from CP array 12-15
            // (position, normal, texture, light matrices) at base + index * stride.
            u32 v = be32(p);
            p += 4;
            g_stats.idxLoads++;
            int arr = 12 + ((op >> 3) & 3);
            u32 index = v >> 16, n = ((v >> 12) & 15) + 1, addr = v & 0xFFF;
            u32 base = g_gx.cp[0xA0 + arr] & 0x3FFFFFFF, stride = g_gx.cp[0xB0 + arr] & 0xFF;
            u32 at = base + index * stride;
            if (at < 0x01800000u || (at >= 0x10000000u && at < 0x14000000u)) {
                const u8* src = (const u8*)(uintptr_t)(at | 0x80000000u);
                // Matrices the CPU writes are in host byte order (the port skips the Wii's swaps); a matrix from a
                // big-endian file would read as implausible floats, so pick the order whose floats look real.
                auto plausible = [](u32 b) {
                    u32 e = (b >> 23) & 0xFF;
                    return e == 0 ? (b & 0x7FFFFFFF) == 0 : e != 0xFF && e >= 107 && e <= 147;
                };
                int sLE = 0, sBE = 0;
                for (u32 i = 0; i < n; i++) {
                    u32 le = (u32)src[i * 4] | (u32)src[i * 4 + 1] << 8 | (u32)src[i * 4 + 2] << 16 | (u32)src[i * 4 + 3] << 24;
                    sLE += plausible(le);
                    sBE += plausible(be32(src + i * 4));
                }
                bool hostOrder = sLE >= sBE;
                static int logn = getenv("NICECRASH_IDXLOG") ? atoi(getenv("NICECRASH_IDXLOG")) : 0;
                if (logn > 0 && logn--) {
                    fprintf(stderr, "[idx] op %02x index %u n %u xf %03x base %08x stride %u le %d:", op, index, n, addr, base,
                            stride, (int)hostOrder);
                    for (u32 i = 0; i < n && i < 12; i++) {
                        u32 le = (u32)src[i * 4] | (u32)src[i * 4 + 1] << 8 | (u32)src[i * 4 + 2] << 16 | (u32)src[i * 4 + 3] << 24;
                        float a, b;
                        u32 bb = be32(src + i * 4);
                        memcpy(&a, &le, 4);
                        memcpy(&b, &bb, 4);
                        fprintf(stderr, " %g/%g", a, b);
                    }
                    fprintf(stderr, "\n");
                }
                for (u32 i = 0; i < n; i++) {
                    if (addr + i >= sizeof(g_gx.xf) / 4) break;
                    g_gx.xf[addr + i] = hostOrder ? (u32)src[i * 4] | (u32)src[i * 4 + 1] << 8 | (u32)src[i * 4 + 2] << 16 |
                                                        (u32)src[i * 4 + 3] << 24
                                                  : be32(src + i * 4);
                }
            }
        } else if (op == 0x40) {  // call display list
            if (end - p < 8) return (size_t)(hist[(nh - 1) & 7] - start);
            u32 addr = be32(p), size = be32(p + 4);
            p += 8;
            g_stats.displayLists++;
            {  // NICECRASH_DLLOG=<n>: log the first n display-list calls
                static int left = getenv("NICECRASH_DLLOG") ? atoi(getenv("NICECRASH_DLLOG")) : 0;
                if (left > 0 && left--) fprintf(stderr, "[dl] call %08x size %x depth %d\n", addr, size, depth);
            }
            // Only lists inside mapped Wii memory (MEM1 0x80000000+24 MB, MEM2 0x90000000+64 MB); a bad address is
            // reported once (the Wii's GPU would read garbage there too, but not fault the program).
            bool mem1 = addr >= 0x80000000u && addr < 0x81800000u && size <= 0x81800000u - addr;
            bool mem2 = addr >= 0x90000000u && addr < 0x94000000u && size <= 0x94000000u - addr;
            if (depth < 4 && (mem1 || mem2)) {
                Run((const u8*)(uintptr_t)addr, size, depth + 1);
            } else if (depth < 4) {
                static int bad;
                if (bad++ < 5) {
                    fprintf(stderr, "gxinterp: display list call to unmapped %08x (+%x) skipped (depth %d, at +%x of %x)\n",
                            addr, size, depth, (unsigned)(p - 9 - start), (unsigned)len);
                    for (int k = 1; k <= 8 && k <= nh; k++) {
                        const u8* h = hist[(nh - k) & 7];
                        fprintf(stderr, "    cmd -%d at +%x:", k, (unsigned)(h - start));
                        for (int b = 0; b < 12; b++) fprintf(stderr, " %02x", h[b]);
                        fprintf(stderr, "  vtxsize %d vcd %08x/%08x\n", gxr_vertex_size(h[0] & 7), g_gx.cp[0x50], g_gx.cp[0x60]);
                    }
                }
            }
        } else if (op == 0x61) {  // BP register load
            if (end - p < 4) return (size_t)(hist[(nh - 1) & 7] - start);
            LoadBP(be32(p));
            p += 4;
        } else if (op >= 0x80 && op < 0xC0) {  // draw primitive
            if (end - p < 2) return (size_t)(hist[(nh - 1) & 7] - start);
            int vat = op & 7;
            u16 n = be16(p);
            p += 2;
            size_t vs = VertexSize(vat);
            if ((size_t)(end - p) < vs * n) return (size_t)(hist[(nh - 1) & 7] - start);
            gxr_draw(op & 0xF8, vat, n, p);
            p += vs * n;
            g_stats.draws++;
            g_stats.vertices += n;
        } else {
            g_stats.unknown++;
            static int shown;
            if (shown++ < 3) {
                fprintf(stderr, "[gx] unknown FIFO opcode 0x%02x (depth %d, at +%x of %x) before:", op, depth,
                        (unsigned)(p - 1 - start), (unsigned)len);
                for (int b = -24; b < 0; b++) fprintf(stderr, " %02x", start[b]);
                fprintf(stderr, "\n");
                for (int k = 2; k <= 8 && k <= nh; k++) {
                    const u8* h = hist[(nh - k) & 7];
                    fprintf(stderr, "    cmd -%d at +%x:", k - 1, (unsigned)(h - start));
                    for (int b = 0; b < 16; b++) fprintf(stderr, " %02x", h[b]);
                    fprintf(stderr, "  vtxsize(vat %d) %d vcd %08x/%08x\n", h[0] & 7, gxr_vertex_size(h[0] & 7),
                            g_gx.cp[0x50], g_gx.cp[0x60]);
                }
            }
        }
    }
    return len;
}

}  // namespace

// The interpreter can call back into the game (draw-done and token callbacks), and the game may then write more
// commands or flush again. So: no nested flushes (the outer one picks up what was added once it is done), and the
// stream is parsed from a copy, because new writes may move the FIFO buffer.
void gxi_flush() {
    static bool busy;
    static std::vector<u8> chunk;
    if (busy) return;
    busy = true;
    // OpenGL is used outside the draw batch only by copies and the presentation, which release the batch state
    // themselves (gxr_end_batch); a flush alone no longer forces the state to be set up again (it did ~2400 times
    // a race frame). NICECRASH_FLUSHDIRTY=1: the old behaviour.
    static const bool flushDirty = getenv("NICECRASH_FLUSHDIRTY") != nullptr;
    if (flushDirty) gxr_state_dirty();
    for (int pass = 0; pass < 8; pass++) {
        size_t len;
        const u8* src = sdk_fifo_data(&len);
        if (!len) break;
        chunk.assign(src, src + len);
        const u8* d = chunk.data();
        size_t used = len;
        if (true) {
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
            used = Run(d, len, 0);
        }
        sdk_fifo_consumed(used);
        if (used < len) break;  // a command cut off at the end waits for its remaining bytes
    }
    gxr_end_batch();
    busy = false;
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
