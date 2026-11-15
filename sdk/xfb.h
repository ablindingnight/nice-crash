// The EFB as an off-screen framebuffer and the XFBs as textures (sdk/xfb.cpp).
#pragma once
#include "sdk.h"

bool xfb_active();                    // framebuffer objects available (else the old present-at-copy path)
void xfb_bind_efb(int w, int h);      // make the EFB (window-sized) the render target
void xfb_copy(u32 addr, int lines);   // display copy: EFB -> the XFB at this (physical) address; lines copied
// At a retrace: draw the XFB at `addr` (or black) into the window's back buffer. Returns false when there is
// nothing to show yet. The caller swaps buffers and then calls xfb_rebind_efb().
bool xfb_present(u32 addr, bool black, int ww, int wh);
void xfb_rebind_efb();
