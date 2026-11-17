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

// NICECRASH_NANSCAN=1 (debugging, patches/nanscan): the first non-finite or huge (|x| > 1e7) float in `bytes` of a
// car's solver tables; logged for the first few hits (car, before/after the solver, byte offset, value).
extern "C" int port_nanscan(int car, const char* when, const void* p, int bytes) {
    static const bool on = getenv("NICECRASH_NANSCAN") != nullptr;
    static int left = 40;
    if (!on || left <= 0) return 0;
    const float* f = (const float*)p;
    for (int i = 0; i < bytes / 4; i++) {
        float v = f[i];
        if (!(v == v) || v > 1e7f || v < -1e7f) {
            u32 bits;
            memcpy(&bits, &v, 4);
            fprintf(stderr, "[nanscan] car %d %s solver: offset 0x%x = %g (%08x)\n", car, when, i * 4, v, bits);
            left--;
            return 1;
        }
    }
    return 0;
}
extern "C" void port_setup_log(const char* path, const float* bump, const float* spring, const float* rebound, float mass) {
    if (!getenv("NICECRASH_NANSCAN")) return;
    fprintf(stderr, "[setup] %s: mass %g wheel0 spring %g bump %g rebound %g\n", path, mass, *spring, *bump, *rebound);
}
extern "C" void port_value_log(const char* key, const char* text, float parsed) {
    if (!getenv("NICECRASH_NANSCAN")) return;
    fprintf(stderr, "[value] %s = \"%s\" -> %g\n", key, text ? text : "(null)", parsed);
}
// Debugging: how many x87 registers are in use (should be 0 between calls; a float-returning function called as void
// leaks one each time, and after 8 the next load gives NaN).
extern "C" int port_x87_depth(void) {
    unsigned short env[14];
    __asm__ volatile("fnstenv %0\n\tfldenv %0" : "=m"(env));
    unsigned tag = env[4];
    int used = 0;
    for (int i = 0; i < 8; i++)
        if (((tag >> (2 * i)) & 3) != 3) used++;
    return used;
}
// PORT_CFLAGS=-finstrument-functions (debugging): the i386 ABI wants the x87 stack empty at every call, so the first
// function entered with x87 registers in use names the call that leaked (a float result left unpopped, or a float
// function that fell off its end). Each hit is logged, then the stack is cleared to catch the next one.
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_enter(void* fn, void* site) {
    static int left = 200;
    if (left <= 0) return;
    unsigned short env[14];
    __asm__ volatile("fnstenv %0" : "=m"(env));
    unsigned tag = env[4];
    int used = 0;
    for (int i = 0; i < 8; i++)
        if (((tag >> (2 * i)) & 3) != 3) used++;
    if (used) {
        static void* seen[256];
        static int nseen;
        bool dup = false;
        for (int i = 0; i < nseen; i++) dup |= seen[i] == site;
        if (!dup && nseen < 256) {  // each call site once
            seen[nseen++] = site;
            left--;
            u32 sdk_vi_retrace_count();
            fprintf(stderr, "[x87] %d in use entering %p (called from %p) retrace %u\n", used, fn, site, sdk_vi_retrace_count());
        }
        env[4] = 0xFFFF;  // all empty
        __asm__ volatile("fldenv %0" : : "m"(env));
    } else {
        __asm__ volatile("fldenv %0" : : "m"(env));
    }
}
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_exit(void*, void*) {}
