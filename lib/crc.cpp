// Port of fn_80132E7C (decomp src/game/crc.cpp): CRC-32, MSB-first, polynomial 0x04C11DB7, the first
// four bytes taken inverted as the initial value (no augmentation), result inverted.
// The table (lbl_802F8F08 in the DOL) is generated here from the polynomial rather than copied.
#include "crc.h"

static u32 g_table[256];
static bool g_ready;

static void BuildTable() {
    for (u32 i = 0; i < 256; i++) {
        u32 c = i << 24;
        for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
        g_table[i] = c;
    }
    g_ready = true;
}

u32 GameCrc32(const void* data, s32 len) {
    if (!g_ready) BuildTable();
    const u8* p = (const u8*)data;
    u32 crc;
    if (len >= 4) {
        crc = ~((u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]);
        p += 4;
        len -= 4;
    } else {
        crc = g_table[128];  // as the original: lbl_802F8F08[128]
    }
    for (s32 i = 0; i < len; i++) crc = (crc << 8 | *p++) ^ g_table[crc >> 24];
    return ~crc;
}
