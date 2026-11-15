// Host port of the Monster Games RST0 resource system (decomp src/game/res.cpp).
//
// Same behaviour as the game for the parts phase 0 needs:
//   - TocLib: "tocres.res" is mounted first; sets whose header flag 1 is set get their TOC from the
//     'ETOC' resource "<set>.toc" inside it (res.cpp TocLib ctor, GetToc, fn_8012ACA0).
//   - LoadSet: read the 0x34 header, check it, read loadSize bytes from baseOffset ("resident" part),
//     check the TOC CRC (internal TOCs) and every resident resource's CRC (fn_8012AFE0, fn_8012AA60,
//     fn_8012A470).
//   - Get: "set/name" lookup by case-insensitive name, type check, resident data or an on-demand
//     extraction read for resources beyond loadSize (fn_8012C370 -> fn_8012B85C -> fn_8012B534,
//     fn_8012B6FC).
// Differences, on purpose: synchronous loads (the game's fn_8012DDA8/fn_8012E81C are async request +
// wait); host-side structs instead of overlaying ResEntry (0x44 bytes with 32-bit pointers) on the data.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "bytes.h"

namespace res {

// ResSetHeader (0x34), res.cpp. Little-endian on disc.
struct SetHeader {
    u32 magic, headerSize, version, flags, fileSize, createTime;
    s32 baseOffset;
    u32 unk1C;
    s32 resCount, loadSize;
    u32 unk28, tocCrc, unk30;
};

// ResEntry (0x44 on disc), res.cpp. Runtime fields kept host-side.
struct Entry {
    std::string name;  // offset 0x0, char[0x20]
    u32 type;          // offset 0x20 (FourCC)
    u32 info;          // offset 0x24 (& 0xFFFF after fn_8012A388; the version for most types)
    s32 size;          // offset 0x28
    s32 offset;        // offset 0x2C (relative to baseOffset)
    u32 crc;           // offset 0x30
    u32 flags = 0;     // 1 got, 2 referenced, 4 extracted, 8 resident
    s32 refs = 0;
    u8* body = nullptr;            // resident data
    std::vector<u8> extracted;     // fn_8012B6FC's own buffer
};

struct Set {
    std::string name, path;
    SetHeader hdr{};
    std::vector<u8> mem;  // the resident image: loadSize bytes from baseOffset
    std::vector<Entry> entries;
};

class System {
public:
    // discRoot: the folder holding the disc's files (race.res, tocres.res ...). Paths the game opens as
    // "\\name" (res.cpp lbl_80559984 + name) resolve under it.
    bool Init(const std::string& discRoot);  // fn_8012BE90 (TocLib = tocres.res)
    Set* LoadSet(const std::string& name);   // fn_8012BEF0 / fn_8012B32C / fn_8012AFE0
    // fn_8012C370. Returns the resource body or nullptr. got = first ResourceGet ever (the game swaps
    // the data on this one), first = no current references.
    u8* Get(const std::string& name, u32 type, u32* info, s32* size, bool* first, bool* got);
    Set* FindSet(const std::string& name);
    void UnloadSet(const std::string& name);  // fn_8012BA1C-style, without the reference checks

    std::vector<std::string> log;  // what the game would print via fn_802E30C8 / fn_802E317C
    std::vector<std::unique_ptr<Set>>& Sets() { return sets_; }

private:
    std::string Path(const std::string& name) const;
    Set* Lookup(const std::string& name, int* idx, bool quiet);       // fn_8012B534
    bool MakeResident(Set* set, int idx);                             // fn_8012B6FC
    bool FixupEntries(Set* set);                                      // fn_8012AA60 / fn_8012ACA0
    void Log(const char* fmt, ...);

    std::string root_;
    std::vector<std::unique_ptr<Set>> sets_;
};

}  // namespace res
