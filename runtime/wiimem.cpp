// Wii memory at its real addresses. A 32-bit large-address-aware process on 64-bit Windows owns the whole
// 4 GB, so MEM1 (24 MB at 0x80000000) and MEM2 (64 MB at 0x90000000) are reserved and committed at the
// addresses the game was built for. Fixed-address reads in game code (OS globals such as the bus clock at
// 0x800000F8) then work unchanged, and the OS arena functions hand out these ranges to the game's heaps.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "wiimem.h"

static void Put32(u32 addr, u32 v) { *(volatile u32*)(uintptr_t)addr = v; }

bool g_wiimemOk;

__attribute__((constructor(102))) static void MapWiiMemory() {
    void* m1 = VirtualAlloc((void*)(uintptr_t)WII_MEM1_BASE, WII_MEM1_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    void* m2 = VirtualAlloc((void*)(uintptr_t)WII_MEM2_BASE, WII_MEM2_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (m1 != (void*)(uintptr_t)WII_MEM1_BASE || m2 != (void*)(uintptr_t)WII_MEM2_BASE) {
        fprintf(stderr, "[wiimem] could not map MEM1/MEM2 at 0x%08X/0x%08X (got %p/%p, error %lu). "
                        "The exe must be 32-bit, /LARGEADDRESSAWARE, on 64-bit Windows.\n",
                WII_MEM1_BASE, WII_MEM2_BASE, m1, m2, GetLastError());
        exit(98);
    }
    // OS globals the game reads directly (values as on retail Wii hardware).
    Put32(0x80000028, WII_MEM1_SIZE);  // physical MEM1 size
    Put32(0x800000F0, WII_MEM1_SIZE);  // simulated memory size
    Put32(0x800000F8, 243000000);      // bus clock (OS_BUS_CLOCK)
    Put32(0x800000FC, 729000000);      // core clock
    Put32(0x80003118, WII_MEM2_SIZE);  // MEM2 physical size
    g_wiimemOk = true;
}
