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

// The game's main (kernel.cpp) becomes GameMain; the host's runtime/port_main.cpp calls it.
#define main GameMain

// GX FIFO writes in game code go to the host FIFO (see port_fifo.h and prep.py rule wgpipe-*).
#include "port_fifo.h"

// MinGW's stdlib.h defines `environ` as a macro; the game uses it as a member name (race.cpp track table).
#include <stdlib.h>
#undef environ
