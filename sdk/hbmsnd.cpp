// The HOME menu's sound archive (homeButton2/HomeButtonSe.brsar, held in memory: MemorySoundArchive) to host byte
// order, in place, before the HOME button library's nw4hbm::snd code reads it as native structs (headers in
// libs/msc .../nw4hbm/snd: SoundArchiveFile.h, BankFile.h, SeqFile.h, WaveFile.h).
//   RSAR: header, SYMB (string table + four patricia trees), INFO (sound/bank/player/file/group tables), and the
//         files of each group: RSEQ (sequence: MML byte code stays as is), RBNK (instruments, wave infos);
//   wave sample data (DSP-ADPCM nibbles / PCM) stays as is: the AX mixer reads it as Wii memory.
// DataRef = {u8 refType, u8 dataType, u16 reserved, u32 value}; with refType 1 the value is an offset from the
// table's base (INFO data for RSAR, the DATA/WAVE tables for RBNK). Util::ReadBigEndian is the identity in the
// library, so swapped fields read correctly everywhere. Done once: the swapped byte-order mark marks it.
#include <cstdint>
#include <cstring>
#include "sdk.h"

namespace {

inline void S16(void* p) { u16* q = (u16*)p; *q = __builtin_bswap16(*q); }
inline void S32(void* p) { u32* q = (u32*)p; *q = __builtin_bswap32(*q); }
inline void S16n(void* p, u32 n) { for (u32 i = 0; i < n; i++) S16((u8*)p + i * 2); }
inline void S32n(void* p, u32 n) { for (u32 i = 0; i < n; i++) S32((u8*)p + i * 4); }
inline u32 R32(const void* p) { return *(const u32*)p; }
inline u16 R16(const void* p) { return *(const u16*)p; }

bool FileHeader(u8* f) {  // the common Nintendo binary file header; false if not big-endian
    if (f[4] != 0xFE || f[5] != 0xFF) return false;
    S32(f);
    S16n(f + 4, 2);
    S32(f + 8);
    S16n(f + 12, 2);
    return true;
}
void BlockHeader(u8* b) { S32n(b, 2); }

// Swap a DataRef in place; return the address it refers to (offset refs only), or null.
u8* Ref(u8* r, u8* base) {
    S16(r + 2);
    S32(r + 4);
    u32 v = R32(r + 4);
    if (r[0] != 1 || v == 0) return nullptr;  // address refs are not used in files; 0 = none
    return base + v;
}
// A Util::Table<DataRef>: count, then refs. Returns the count; refs are swapped by the caller.
u32 Table(u8* t) {
    S32(t);
    return R32(t);
}

// ---- RSAR ----
void StringTree(u8* t) {
    S32(t);  // rootIdx
    u32 n = Table(t + 4);
    for (u32 i = 0; i < n; i++) {
        u8* node = t + 8 + i * 0x14;
        S16n(node, 2);
        S32n(node + 4, 4);
    }
}

void Symbols(u8* symb) {
    BlockHeader(symb);
    u8* base = symb + 8;  // StringChunk: table, sound/player/group/bank trees (offsets from base)
    S32n(base, 5);
    u8* table = base + R32(base);
    u32 n = Table(table);
    S32n(table + 4, n);  // string offsets (from base)
    for (int k = 1; k < 5; k++)
        if (R32(base + k * 4)) StringTree(base + R32(base + k * 4));
}

void WaveInfo(u8* w) {  // WaveFile::WaveInfo and its channel infos / ADPCM parameters
    S16(w + 4);          // sampleRate
    S32n(w + 8, 5);      // loopStart loopEnd channelInfoTableOffset dataOffset reserved
    u32* chans = (u32*)(w + R32(w + 0x10));
    for (u32 c = 0; c < w[2]; c++) {
        S32(&chans[c]);
        u8* ci = w + chans[c];
        S32n(ci, 7);  // channelDataOffset adpcmOffset volumes(4) reserved
        if (R32(ci + 4)) S16n(w + R32(ci + 4), 0x30 / 2);  // AdpcmInfo: coefs, gain, ps, yn1, yn2, loop ps/yn1/yn2
    }
}

void InstRegion(u8* ref, u8* base, int depth);

void InstParam(u8* p) {
    S32(p);          // waveIndex
    S16n(p + 8, 2);  // hold, padding
    S32(p + 0x10);   // tune
    Ref(p + 0x14, nullptr);
    Ref(p + 0x1C, nullptr);
    Ref(p + 0x24, nullptr);
    S32(p + 0x2C);
}

// A BankFile::DataRegion: InstParam (1), RangeTable (2) or IndexTable (3), nested.
void InstRegion(u8* ref, u8* base, int depth) {
    u8 type = ref[1];
    u8* p = Ref(ref, base);
    if (!p || depth > 4) return;
    if (type == 1) {
        InstParam(p);
    } else if (type == 2) {  // RangeTable: size, keys[size], then refs at the next 4-byte boundary
        u32 n = p[0];
        u8* refs = p + ((1 + n + 3) & ~3u);
        for (u32 i = 0; i < n; i++) InstRegion(refs + i * 8, base, depth + 1);
    } else if (type == 3) {  // IndexTable: min, max, u16, refs[max - min + 1]
        S16(p + 2);
        u32 n = (u32)p[1] - p[0] + 1;
        for (u32 i = 0; i < n; i++) InstRegion(p + 4 + i * 8, base, depth + 1);
    }
}

void Bank(u8* f) {
    if (!FileHeader(f)) return;
    S32n(f + 0x10, 4);  // data/wave block offsets and sizes
    u8* data = f + R32(f + 0x10);
    BlockHeader(data);
    u8* instBase = data + 8;
    u32 n = Table(instBase);
    for (u32 i = 0; i < n; i++) InstRegion(instBase + 4 + i * 8, instBase, 0);
    u8* wave = f + R32(f + 0x18);
    BlockHeader(wave);
    u8* waveBase = wave + 8;
    u32 w = Table(waveBase);
    for (u32 i = 0; i < w; i++)
        if (u8* wi = Ref(waveBase + 4 + i * 8, waveBase)) WaveInfo(wi);
}

void Sequence(u8* f) {
    if (!FileHeader(f)) return;
    S32n(f + 0x10, 4);  // data/label block offsets and sizes
    u8* data = f + R32(f + 0x10);
    BlockHeader(data);
    S32(data + 8);  // baseOffset (MML byte code follows; bytes)
    if (R32(f + 0x18)) {
        u8* labl = f + R32(f + 0x18);
        BlockHeader(labl);
        u32 n = Table(labl + 8);
        S32n(labl + 12, n);  // label offsets (from labl + 8)
        for (u32 i = 0; i < n; i++) S32n(labl + 8 + R32(labl + 12 + i * 4), 2);  // dataOffset, nameLen
    }
}

void File(u8* f) {
    static u8* done[64];  // a file listed in two groups is converted once
    for (u8* d : done)
        if (d == f) return;
    for (u8*& d : done)
        if (!d) {
            d = f;
            break;
        }
    if (!memcmp(f, "RBNK", 4)) Bank(f);
    else if (!memcmp(f, "RSEQ", 4)) Sequence(f);
    else sdk_log("hbmsnd: file type %.4s not converted", (const char*)f);
}

void Info(u8* rsar, u8* infoBlock) {
    BlockHeader(infoBlock);
    u8* I = infoBlock + 8;  // DataRef base for everything below
    u8* sounds = Ref(I, I);
    u8* banks = Ref(I + 8, I);
    u8* players = Ref(I + 0x10, I);
    u8* files = Ref(I + 0x18, I);
    u8* groups = Ref(I + 0x20, I);
    u8* playerInfo = Ref(I + 0x28, I);
    if (sounds) {
        u32 n = Table(sounds);
        for (u32 i = 0; i < n; i++) {
            u8* s = Ref(sounds + 4 + i * 8, I);
            if (!s) continue;
            S32n(s, 3);  // stringId fileId playerId
            if (u8* p3 = Ref(s + 0x0C, I)) S32(p3);  // Sound3DParam.flags
            u8 type = s[0x16];
            u8* spec = Ref(s + 0x18, I);
            S32n(s + 0x20, 2);  // userParam
            if (spec && type == 1) S32n(spec, 3);       // SeqSoundInfo: dataOffset bankId allocTrack
            else if (spec && type == 3) S32n(spec, 2);  // WaveSoundInfo: subNo allocTrack
        }
    }
    if (banks) {
        u32 n = Table(banks);
        for (u32 i = 0; i < n; i++)
            if (u8* b = Ref(banks + 4 + i * 8, I)) S32n(b, 2);
    }
    if (players) {
        u32 n = Table(players);
        for (u32 i = 0; i < n; i++)
            if (u8* p = Ref(players + 4 + i * 8, I)) {
                S32(p);
                S32(p + 8);
            }
    }
    if (files) {
        u32 n = Table(files);
        for (u32 i = 0; i < n; i++) {
            u8* fi = Ref(files + 4 + i * 8, I);
            if (!fi) continue;
            S32n(fi, 3);  // fileSize waveDataSize entryNum
            Ref(fi + 0x0C, I);
            if (u8* pos = Ref(fi + 0x14, I)) {
                u32 m = Table(pos);
                for (u32 k = 0; k < m; k++)
                    if (u8* fp = Ref(pos + 4 + k * 8, I)) S32n(fp, 2);  // FilePos: groupId index
            }
        }
    }
    if (groups) {
        u32 n = Table(groups);
        for (u32 i = 0; i < n; i++) {
            u8* g = Ref(groups + 4 + i * 8, I);
            if (!g) continue;
            S32n(g, 2);  // stringId entryNum
            Ref(g + 8, I);
            S32n(g + 0x10, 4);  // offset size waveDataOffset waveDataSize
            u8* items = Ref(g + 0x20, I);
            if (!items) continue;
            u32 m = Table(items);
            for (u32 k = 0; k < m; k++) {
                u8* it = Ref(items + 4 + k * 8, I);
                if (!it) continue;
                S32n(it, 5);  // fileId offset size waveDataOffset waveDataSize
                if (R32(it + 8)) File(rsar + R32(g + 0x10) + R32(it + 4));
            }
        }
    }
    if (playerInfo) S16n(playerInfo, 7);
}

}  // namespace

// Called by the glue before the archive is handed to the library (sdk/hbmglue.cpp).
extern "C" void port_hbm_rsar_fix(void* p) {
    u8* f = (u8*)p;
    if (!f || memcmp(f, "RSAR", 4)) return;  // a converted archive's signature reads reversed
    if (!FileHeader(f)) return;
    S32n(f + 0x10, 6);  // symbol / info / file image offsets and sizes
    if (R32(f + 0x10)) Symbols(f + R32(f + 0x10));
    Info(f, f + R32(f + 0x18));
    sdk_log("hbmsnd: HomeButtonSe.brsar converted");
}
