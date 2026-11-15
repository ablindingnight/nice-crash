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
