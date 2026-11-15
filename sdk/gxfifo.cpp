// Host GX FIFO: collects the command stream written to the write-gather pipe (game code via port_wgpipe; later
// the SDK's own GX code), in the GPU's big-endian byte order. Milestone 2 interprets it (BP/CP/XF loads, draws).
// Plain malloc/realloc: the game replaces the global operator new, and the FIFO must not eat the game's heaps.
#include <cstdlib>
#include <cstring>
#include "sdk.h"

static u8* g_buf;
static size_t g_len, g_cap;

static inline void Put(u8 v) {
    if (g_len == g_cap) {
        g_cap = g_cap ? g_cap * 2 : (1 << 20);
        g_buf = (u8*)realloc(g_buf, g_cap);
    }
    g_buf[g_len++] = v;
}

extern "C" {
void port_fifo_u8(u8 v) { Put(v); }
void port_fifo_u16(u16 v) {
    Put(v >> 8);
    Put((u8)v);
}
void port_fifo_u32(u32 v) {
    Put(v >> 24);
    Put(v >> 16);
    Put(v >> 8);
    Put(v);
}
void port_fifo_f32(f32 f) {
    u32 v;
    memcpy(&v, &f, 4);
    port_fifo_u32(v);
}
}

// The interpreter (milestone 2) consumes and resets the buffer once per frame.
const u8* sdk_fifo_data(size_t* len) {
    *len = g_len;
    return g_buf;
}
void sdk_fifo_reset() { g_len = 0; }
