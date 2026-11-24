// Loads the player's main.dol DATA sections at their original addresses in the mapped MEM1 (as the Wii's
// apploader does), so data that only exists in the matching build's assembly (the asm-only labels, defined as
// absolute symbols at their Wii addresses in build/stubs.s) reads real retail bytes, including code that indexes
// past a label. Scalars among those labels (4- and 8-byte values listed in port_dolfix_table) are byte-swapped
// once here; everything else (strings, byte tables) is used as it is. Text sections (PowerPC code) are not loaded.
// The decomp's DUMP_DATA tables and the MP3 decoder's tables live here too (tools/gametrial/dumptables.py): the
// sources see them at their Wii addresses and their multi-byte fields are among port_dolfix_table's runs, so the
// game exe holds no bytes from main.dol.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "portconfig.h"
#include "wiimem.h"

struct DolFix {
    u32 addr, size, elem;  // swap each `elem`-byte element of [addr, addr + size)
};
extern "C" const DolFix port_dolfix_table[];  // build/stubs.s, terminated by {0, 0, 0}
struct DolPtr {
    u32 addr, host;
};
extern "C" const DolPtr port_dolptr_table[];  // build/stubs.s, terminated by {0, 0}

static u32 BE(const unsigned char* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static void Swap(unsigned char* p, int n) {
    for (int i = 0; i < n / 2; i++) {
        unsigned char t = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = t;
    }
}

__attribute__((constructor(104))) static void LoadDolData() {
    std::string path;
    portcfg::Values v;
    if (portcfg::Read(portcfg::DumpIniPath(), v)) path = v["dol"];
    if (const char* e = getenv("NICECRASH_DOL")) path = e;
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[dol] cannot open main.dol (%s): run build/import.exe on your disc first\n", path.c_str());
        exit(94);
    }
    unsigned char h[0x100];
    if (fread(h, 1, sizeof h, f) != sizeof h) exit(94);
    for (int i = 7; i < 18; i++) {  // data sections D0-D10
        u32 off = BE(h + i * 4), addr = BE(h + 0x48 + i * 4), size = BE(h + 0x90 + i * 4);
        if (!size) continue;
        if (addr < WII_MEM1_BASE || addr + size > WII_MEM1_BASE + WII_MEM1_SIZE) continue;
        fseek(f, (long)off, SEEK_SET);
        if (fread((void*)(uintptr_t)addr, 1, size, f) != size) {
            fprintf(stderr, "[dol] short read in data section %d\n", i - 7);
            exit(94);
        }
    }
    fclose(f);
    // Pointer words (link.py): a host function for retail code pointers, else a byte-swap of a data pointer
    // (the data image sits at its retail addresses, so the value stays the same once it is native).
    for (const DolPtr* p = port_dolptr_table; p->addr; p++) {
        if (p->host) *(u32*)(uintptr_t)p->addr = p->host;
        else Swap((unsigned char*)(uintptr_t)p->addr, 4);
    }
    for (const DolFix* d = port_dolfix_table; d->addr; d++)
        if (d->elem == 2 || d->elem == 4 || d->elem == 8)
            for (u32 k = 0; k + d->elem <= d->size; k += d->elem) Swap((unsigned char*)(uintptr_t)(d->addr + k), (int)d->elem);
}
