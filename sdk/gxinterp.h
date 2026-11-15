// GX FIFO interpreter (milestone 2): consumes the command stream the game and the SDK's GX library write to the
// write-gather pipe and turns it into host rendering. Register meanings follow the GX hardware as documented
// publicly (YAGCD, Dolphin's VideoCommon); the code here is written for this port.
#pragma once
#include "sdk.h"

// GPU register state the interpreter keeps (CP vertex formats, BP raster/TEV/texture, XF transform).
struct GXRegs {
    u32 cp[256];
    u32 bp[256];
    u32 xf[0x1100];  // XF memory (pos/tex matrices 0x000-, normals 0x400-, post 0x500-, lights 0x600-) + regs 0x1000-
    // TEV registers BP 0xE0-0xE7 (RA/BG pairs for PREV, C0, C1, C2): bit 23 of a write selects the konst bank.
    u32 tevReg[8], tevKonst[8];
};
extern GXRegs g_gx;

// Renderer hooks (sdk/gxrender.cpp), called by the interpreter.
int gxr_vertex_size(int vat);
void gxr_draw(int prim, int vat, int count, const u8* data);
void gxr_copy(u32 bp52);
void gxr_tlut_load(u32 bp64, u32 bp65);

struct GXFrameStats {
    u32 bytes, bpLoads, cpLoads, xfLoads, idxLoads, displayLists, draws, vertices, copies, unknown;
};

// Interpret everything written since the last call (sdk/gxfifo.cpp's buffer), then reset it.
void gxi_flush();
// Counters since the last call.
GXFrameStats gxi_take_stats();
// Last EFB->XFB copy: the frame to show (0 if none yet).
u32 gxi_last_copy_dst();
