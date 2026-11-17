// GX TEV emulation with generated GLSL programs (sdk/gxtev.cpp).
#pragma once
#include "sdk.h"

// Binds the program for the current TEV state. texOk[m]: texture map m is bound on texture unit m. Returns false
// when shaders are unavailable (the caller falls back to the fixed-function approximation).
bool gxt_begin(const bool texOk[8]);
void gxt_end();
void gxt_active_texture(int unit);
// Per-vertex inputs while a program is bound: texture coordinate set `unit`, colour channel 1.
void gxt_texcoord(int unit, float s, float t);
void gxt_color1(const u8 c[4]);
// Draws batched vertices (layout in gxtev.cpp) with the current program/fixed-function state.
// idx: draw nIdx indexed vertices (the batch keeps strip/fan vertices once) instead of the first n.
void gxt_draw_arrays(unsigned mode, const void* base, int stride, int n, int nTex, const unsigned* idx = nullptr,
                     int nIdx = 0);
// PE blend state (BP 0x41/0x42/0x43) as GL blend/logic-op/colour-mask state.
void gxt_blend(u32 cmode0, u32 cmode1, u32 pe_ctrl);
// EFB -> texture copy on the GPU: binds a program that converts the EFB colour sampled from texture unit 0 into what
// a texture of copy format `real` (BP 0x52) samples as. Draw a quad, then gxt_copy_end(). False: no shaders.
bool gxt_copy_begin(int real, bool intensity, bool srcAlpha);
void gxt_copy_end();
// Movie frames (gxr_host_quad): Catmull-Rom bicubic upscale of the texture on unit 0 (size in texels). End with
// gxt_copy_end(). False: no shaders (draw bilinear).
bool gxt_bicubic_begin(float texW, float texH);
