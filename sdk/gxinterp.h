// GX FIFO interpreter (milestone 2): consumes the command stream the game and the SDK's GX library write to the
// write-gather pipe and turns it into host rendering. Register meanings follow the GX hardware as documented
// publicly (YAGCD, Dolphin's VideoCommon); the code here is written for this port.
#pragma once
#include "sdk.h"

struct GXFrameStats {
    u32 bytes, bpLoads, cpLoads, xfLoads, idxLoads, displayLists, draws, vertices, copies, unknown;
};

// Interpret everything written since the last call (sdk/gxfifo.cpp's buffer), then reset it.
void gxi_flush();
// Counters since the last call.
GXFrameStats gxi_take_stats();
// Last EFB->XFB copy: the frame to show (0 if none yet).
u32 gxi_last_copy_dst();
