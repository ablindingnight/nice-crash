// Wii memory at its real addresses. A 32-bit large-address-aware process on 64-bit Windows owns the whole
// 4 GB, so MEM1 (24 MB) and MEM2 (64 MB) are mapped where the game was built to find them:
//   MEM1 cached 0x80000000 and uncached 0xC0000000 (two views of one section: real aliases, as on the Wii)
//   MEM2 cached 0x90000000 and uncached 0xD0000000
// plus the hardware register pages as plain memory (0xCC000000 CP/PE/VI/PI/MI/DSP, 0xCD000000 Hollywood), so
// SDK code that programs registers runs; the host layer interprets what matters (sdk/), the rest is ignored.
// Fixed-address reads in game code (OS globals such as the bus clock at 0x800000F8) work unchanged, and the OS
// arena functions hand out these ranges to the game's heaps.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "wiimem.h"

static void Put32(u32 addr, u32 v) { *(volatile u32*)(uintptr_t)addr = v; }

bool g_wiimemOk;

static void Fail(const char* what, u32 addr) {
    fprintf(stderr, "[wiimem] could not map %s at 0x%08X (error %lu). The exe must be 32-bit, /LARGEADDRESSAWARE, "
                    "/DYNAMICBASE:NO, on 64-bit Windows.\n", what, addr, GetLastError());
    exit(98);
}

static void MapTwice(const char* what, u32 cached, u32 uncached, u32 size) {
    HANDLE sec = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, nullptr);
    if (!sec) Fail(what, cached);
    if (MapViewOfFileEx(sec, FILE_MAP_ALL_ACCESS, 0, 0, size, (void*)(uintptr_t)cached) != (void*)(uintptr_t)cached)
        Fail(what, cached);
    if (MapViewOfFileEx(sec, FILE_MAP_ALL_ACCESS, 0, 0, size, (void*)(uintptr_t)uncached) != (void*)(uintptr_t)uncached)
        Fail(what, uncached);
}

static void MapPlain(const char* what, u32 addr, u32 size) {
    if (VirtualAlloc((void*)(uintptr_t)addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != (void*)(uintptr_t)addr)
        Fail(what, addr);
}

__attribute__((constructor(102))) static void MapWiiMemory() {
    MapTwice("MEM1", WII_MEM1_BASE, WII_MEM1_UNCACHED, WII_MEM1_SIZE);
    MapTwice("MEM2", WII_MEM2_BASE, WII_MEM2_UNCACHED, WII_MEM2_SIZE);
    MapPlain("hardware registers", 0xCC000000u, 0x10000);
    MapPlain("Hollywood registers", 0xCD000000u, 0x10000);
    MapPlain("locked cache", 0xE0000000u, 0x4000);  // the L1 data cache locked as 16 KB scratch (LCEnable)
    // OS globals the game reads directly (values as on retail Wii hardware).
    Put32(0x80000028, WII_MEM1_SIZE);  // physical MEM1 size
    Put32(0x800000F0, WII_MEM1_SIZE);  // simulated memory size
    Put32(0x800000F8, 243000000);      // bus clock (OS_BUS_CLOCK)
    Put32(0x800000FC, 729000000);      // core clock
    Put32(0x80003118, WII_MEM2_SIZE);  // MEM2 physical size
    g_wiimemOk = true;
}
