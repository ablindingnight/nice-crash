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
void xfb_resolve();                   // before reading the EFB: multisampled drawing -> the colour texture/depth
unsigned xfb_efb_color();             // the EFB's colour texture (GL name)
// Render into texture `tex` (w x h) through a second framebuffer (EFB texture copies on the GPU); false when that
// is not possible. xfb_bind_efb() returns to the EFB.
bool xfb_target_tex(unsigned tex, int w, int h);
