// Force-included before every decomp unit in the port build.
#pragma once
// decomp.h: NONMATCHING turns DECOMP_FORCEACTIVE & co. into nothing (matching-only tricks).
#define NONMATCHING 1

// MWCC intrinsics used directly by game code.
#ifdef __cplusplus
extern "C++" {
static inline double __fabs(double x) { return __builtin_fabs(x); }
static inline float __fabsf(float x) { return __builtin_fabsf(x); }
static inline int __abs(int x) { return x < 0 ? -x : x; }
}
#else
static inline double __fabs(double x) { return __builtin_fabs(x); }
static inline float __fabsf(float x) { return __builtin_fabsf(x); }
static inline int __abs(int x) { return x < 0 ? -x : x; }
#endif

// MWCC __declspec(section ".sdata2") etc.: placement only, no meaning on the host.
#define __declspec(x)

// PowerPC count-leading-zeros intrinsic (MWCC builtin).
static inline unsigned int __cntlzw(unsigned int x) { return x ? (unsigned int)__builtin_clz(x) : 32u; }

// PowerPC srw: shift counts 32-63 give 0 (x86 only uses the low 5 bits of the count, so `x >> 32` keeps x).
// prep.py rule ppc-srw-mask routes the game's `0xFFFFFFFF >> (32 - n)` masks here, where n can be 0 (Bink).
static inline unsigned int port_srw(unsigned int v, unsigned int n) { return (n & 32) ? 0u : v >> (n & 31); }

// Stores of data the GX hardware reads (texture palettes, texels built by the CPU) must be big-endian, as the
// Wii's stores are; on the little-endian host the port patches such stores through these (patches/gx-stores).
static inline unsigned short port_be16(unsigned int v) { return __builtin_bswap16((unsigned short)v); }
static inline unsigned int port_be32(unsigned int v) { return __builtin_bswap32(v); }

// A Nintendo DSP-ADPCM header (0x60 bytes: three u32, two u16, three u32, then u16s) from big-endian file data to
// host order, in place (patches/dsp-adpcm-header).
static inline void port_swap_dspadpcm(void* h) {
    unsigned int* w = (unsigned int*)h;
    unsigned short* s = (unsigned short*)h;
    for (int i = 0; i < 3; i++) w[i] = __builtin_bswap32(w[i]);
    s[6] = __builtin_bswap16(s[6]);
    s[7] = __builtin_bswap16(s[7]);
    for (int i = 4; i < 7; i++) w[i] = __builtin_bswap32(w[i]);
    for (int i = 14; i < 48; i++) s[i] = __builtin_bswap16(s[i]);
}

// The game's main (kernel.cpp) becomes GameMain; the host's runtime/port_main.cpp calls it.
#define main GameMain

// GX FIFO writes in game code go to the host FIFO (see port_fifo.h and prep.py rule wgpipe-*).
#include "port_fifo.h"

// MinGW's stdlib.h defines `environ` as a macro; the game uses it as a member name (race.cpp track table).
#include <stdlib.h>
#undef environ

// More PowerPC intrinsics (Bink): high word of 32x32 multiplies, reciprocal (square root) estimates.
static inline unsigned int __mulhwu(unsigned int a, unsigned int b) { return (unsigned int)(((unsigned long long)a * b) >> 32); }
static inline int __mulhw(int a, int b) { return (int)(((long long)a * b) >> 32); }
static inline double __frsqrte(double x) { return 1.0 / __builtin_sqrt(x); }
static inline float __fres(float x) { return 1.0f / x; }

