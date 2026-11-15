#pragma once
#include <cstdint>
typedef uint32_t u32;

#define WII_MEM1_BASE 0x80000000u
#define WII_MEM1_UNCACHED 0xC0000000u
#define WII_MEM1_SIZE 0x01800000u
#define WII_MEM2_BASE 0x90000000u
#define WII_MEM2_UNCACHED 0xD0000000u
#define WII_MEM2_SIZE 0x04000000u

extern bool g_wiimemOk;
