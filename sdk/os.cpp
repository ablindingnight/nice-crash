// Host SDK: OS (init, arenas, reports, panics, time, caches). Replaces the Revolution SDK's OS library.
// Memory comes from runtime/wiimem.cpp, which maps MEM1/MEM2 at their real Wii addresses.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../runtime/wiimem.h"
#include "sdk.h"

// Arena bounds as the game sees them. MEM1's first 16 KB hold the OS globals (as on the Wii); the DOL and its
// BSS do not live in MEM1 on the host, so nearly all of MEM1 is free. MEM2's top 0x00C20000 bytes belong to
// IOS on retail (arena hi 0x933E0000); we keep that limit so the game sees retail-sized arenas.
static void* s_mem1Lo = (void*)(uintptr_t)0x80570000u;  // above the DOL image (bss ends 0x80565240), as on retail
static void* s_mem1Hi = (void*)(uintptr_t)(WII_MEM1_BASE + WII_MEM1_SIZE);
static void* s_mem2Lo = (void*)(uintptr_t)(WII_MEM2_BASE + 0x800);
static void* s_mem2Hi = (void*)(uintptr_t)(WII_MEM2_BASE + 0x033E0000);

extern "C" {

void OSInit(void) { sdk_log("OSInit"); }

void* OSGetMEM1ArenaLo(void) { return s_mem1Lo; }
void* OSGetMEM1ArenaHi(void) { return s_mem1Hi; }
void* OSGetMEM2ArenaLo(void) { return s_mem2Lo; }
void* OSGetMEM2ArenaHi(void) { return s_mem2Hi; }
void OSSetMEM1ArenaLo(void* p) { s_mem1Lo = p; }
void OSSetMEM1ArenaHi(void* p) { s_mem1Hi = p; }
void OSSetMEM2ArenaLo(void* p) { s_mem2Lo = p; }
void OSSetMEM2ArenaHi(void* p) { s_mem2Hi = p; }
void* OSGetArenaLo(void) { return s_mem1Lo; }
void* OSGetArenaHi(void) { return s_mem1Hi; }
void OSSetArenaLo(void* p) { s_mem1Lo = p; }
void OSSetArenaHi(void* p) { s_mem1Hi = p; }

void OSReport(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void OSVReport(const char* fmt, va_list ap) {
    vfprintf(stdout, fmt, ap);
    fflush(stdout);
}

void OSPanic(const char* file, int line, const char* fmt, ...) {
    fprintf(stderr, "[OSPanic] %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(97);
}

void OSFatal(u32 fg, u32 bg, const char* msg) {
    fprintf(stderr, "[OSFatal] %s\n", msg);
    fflush(stderr);
    exit(96);
}

// Time base: the Wii's runs at bus clock / 4 = 60.75 MHz. Derived from the host's performance counter.
void sdk_vi_poll();  // sdk/vi.cpp: the retrace "interrupt"

s64 OSGetTime(void) {
    sdk_vi_poll();
    static LARGE_INTEGER f, t0;
    if (!f.QuadPart) {
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    long double ticks = (long double)(t.QuadPart - t0.QuadPart) * 60750000.0L / (long double)f.QuadPart;
    return (s64)ticks;
}
u32 OSGetTick(void) { return (u32)OSGetTime(); }

// Caches: coherent on the host.
void DCFlushRange(void*, u32) {}
void DCFlushRangeNoSync(void*, u32) {}
void DCStoreRange(void*, u32) {}
void DCStoreRangeNoSync(void*, u32) {}
void DCInvalidateRange(void*, u32) {}
void ICInvalidateRange(void*, u32) {}
void ICFlashInvalidate(void) {}
void LCEnable(void) {}
void LCDisable(void) {}

// Interrupts: single-threaded host for now.
int OSDisableInterrupts(void) { return 1; }
int OSEnableInterrupts(void) { return 1; }
int OSRestoreInterrupts(int level) { return level; }

}  // extern "C"
