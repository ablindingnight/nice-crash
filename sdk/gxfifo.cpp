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

// The interpreter consumes [g_read, limit): limit is the GP breakpoint when one is set (the game stalls the GPU
// at the current write position with GXEnableBreakPt and moves it each frame, so the GPU runs a frame behind),
// else everything written so far.
static size_t g_read;
static bool g_bpOn;
static size_t g_bp;

const u8* sdk_fifo_data(size_t* len) {
    size_t limit = g_bpOn && g_bp < g_len ? g_bp : g_len;
    *len = limit > g_read ? limit - g_read : 0;
    return g_buf + g_read;
}

void sdk_fifo_consumed(size_t n) {
    g_read += n;
    if (g_read == g_len) g_read = g_len = 0;  // all consumed: start over at the front
    else if (g_read > (8u << 20)) {           // keep the buffer from growing forever
        memmove(g_buf, g_buf + g_read, g_len - g_read);
        if (g_bpOn) g_bp -= g_read;
        g_len -= g_read;
        g_read = 0;
    }
}

extern "C" {
// GP breakpoints in terms of the host FIFO: the position is "everything written so far".
void GXEnableBreakPt(void* wr) {
    g_bpOn = true;
    g_bp = g_len;
}
void GXDisableBreakPt(void) { g_bpOn = false; }
// The SDK's FIFO pointers are meaningless on the host; report an empty, consistent FIFO.
void GXGetFifoPtrs(void* fifo, void** rd, void** wr) {
    if (rd) *rd = nullptr;
    if (wr) *wr = nullptr;
}
}
