// NICECRASH_TRACE=<file>: the renderer's recent steps in a memory-mapped ring (sdk calls port_trace), which the
// operating system keeps on disk even when the process is ended by a fast fail that no handler sees.
// Read it with tools/gametrial/traceread.py <file>.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
const int kSlots = 512, kSlot = 64;
struct Ring {
    volatile LONG next;
    char pad[60];
    char slot[kSlots][kSlot];
};
Ring* g_ring;
bool g_tried;
}  // namespace


extern "C" void port_trace(const char* what, unsigned a, unsigned b) {
    if (!g_tried) {
        g_tried = true;
        const char* path = getenv("NICECRASH_TRACE");
        if (path) {
            HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE m = f != INVALID_HANDLE_VALUE ? CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, sizeof(Ring), nullptr)
                                                 : nullptr;
            if (m) g_ring = (Ring*)MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, sizeof(Ring));
        }
    }
    if (!g_ring) return;
    LONG i = InterlockedIncrement(&g_ring->next) - 1;
    char* s = g_ring->slot[i % kSlots];
    _snprintf(s, kSlot - 1, "%ld t%lu %s %08x %08x", (long)i, GetCurrentThreadId(), what, a, b);
    s[kSlot - 1] = 0;
}
