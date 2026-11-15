// Shared bits of the host SDK layer (port/sdk). Each file replaces one Revolution SDK library with host code;
// the game links against these C symbols exactly as it did against the SDK.
#pragma once
#include <cstdint>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef float f32;
typedef double f64;

// One line per SDK call worth seeing during bring-up (NICECRASH_SDKLOG=1).
void sdk_log(const char* fmt, ...);

// 16:9 output (the console's widescreen setting, as in a widescreen Dolphin); NICECRASH_ASPECT=4:3 turns it off.
bool sdk_widescreen();
