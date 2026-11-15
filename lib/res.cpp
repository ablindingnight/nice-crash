// Host port of the RST0 resource system. See res.h; function names refer to decomp src/game/res.cpp.
#include "res.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include "crc.h"

namespace res {

static const u32 kRST0 = FOURCC("RST0");
static const u32 kETOC = FOURCC("ETOC");

void System::Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log.emplace_back(buf);
}

std::string System::Path(const std::string& name) const {
    // fn_8012B2C4: "\\" + name, opened through the DVD layer from the disc root.
    std::string p = root_;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '\\';
    return p + name;
}

static bool ReadAll(const std::string& path, long offset, long size, std::vector<u8>& out, long* fileSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long fs = ftell(f);
    if (fileSize) *fileSize = fs;
    if (size < 0) size = fs - offset;
    if (offset < 0 || offset + size > fs) {
        fclose(f);
        return false;
    }
    out.resize((size_t)size);
    fseek(f, offset, SEEK_SET);
    size_t got = size ? fread(out.data(), 1, (size_t)size, f) : 0;
    fclose(f);
    return got == (size_t)size;
}

// fn_8012A80C (swap) + fn_8012A6C8 (check).
static bool ParseHeader(const u8* b, SetHeader& h) {
    u32 w[13];
    for (int i = 0; i < 13; i++) w[i] = rd_le32(b + i * 4);
    h.magic = w[0]; h.headerSize = w[1]; h.version = w[2]; h.flags = w[3]; h.fileSize = w[4];
    h.createTime = w[5]; h.baseOffset = (s32)w[6]; h.unk1C = w[7]; h.resCount = (s32)w[8];
    h.loadSize = (s32)w[9]; h.unk28 = w[10]; h.tocCrc = w[11]; h.unk30 = w[12];
    return true;
}

// fn_8012A388: swap one 0x44-byte entry, clear runtime fields, info &= 0xFFFF.
static Entry ParseEntry(const u8* e) {
    Entry r;
    r.name.assign((const char*)e, strnlen((const char*)e, 0x20));
    r.type = rd_le32(e + 0x20);
    r.info = rd_le32(e + 0x24) & 0xFFFF;
    r.size = (s32)rd_le32(e + 0x28);
    r.offset = (s32)rd_le32(e + 0x2C);
    r.crc = rd_le32(e + 0x30);
    return r;
}

bool System::Init(const std::string& discRoot) {
    root_ = discRoot;
    // fn_8012BE90 -> new TocLib -> fn_8012BEF0("tocres.res")
    return LoadSet("tocres.res") != nullptr;
}

Set* System::FindSet(const std::string& name) {
    for (auto& s : sets_)
        if (!strcasecmp(s->name.c_str(), name.c_str())) return s.get();
    return nullptr;
}

// fn_8012A470 for each entry that lies in the resident image: point it at the data, check its CRC.
bool System::FixupEntries(Set* set) {
    for (size_t i = 0; i < set->entries.size(); i++) {
        Entry& e = set->entries[i];
        if ((int64_t)e.offset + e.size <= set->hdr.loadSize) {
            e.body = set->mem.data() + e.offset;
            if (e.crc != GameCrc32(e.body, e.size)) {
                Log("res: crc failure on %s[%d]/%s", set->name.c_str(), (int)i, e.name.c_str());
                return false;
            }
            e.flags |= 8;
        }
    }
    return true;
}

// fn_8012AFE0 (fn_8012B32C tries only path index 0).
Set* System::LoadSet(const std::string& name) {
    if (Set* s = FindSet(name)) return s;
    Log("res: loading %s", name.c_str());
    std::string path = Path(name);
    std::vector<u8> hb;
    long fileSize = 0;
    if (!ReadAll(path, 0, 0x34, hb, &fileSize)) {
        Log("res: can't open %s", path.c_str());
        return nullptr;
    }
    auto set = std::make_unique<Set>();
    set->name = name;
    set->path = path;
    SetHeader& h = set->hdr;
    ParseHeader(hb.data(), h);
    // fn_8012A6C8 (its checksum call on the header has no effect on the result).
    if (h.magic != kRST0) { Log("res: bad set header on %s: bad cookie '0x%x'", name.c_str(), h.magic); return nullptr; }
    if (h.version != 8) { Log("res: bad set header on %s: bad version '%d' != '%d'", name.c_str(), h.version, 8); return nullptr; }
    if (h.headerSize != 0x34) { Log("res: bad set header on %s: bad header size '%d'", name.c_str(), h.headerSize); return nullptr; }
    if (h.fileSize != (u32)fileSize) { Log("res: bad set header on %s: bad file size '%d' != '%d'", name.c_str(), h.fileSize, (int)fileSize); return nullptr; }

    if (!ReadAll(path, h.baseOffset, h.loadSize, set->mem, nullptr)) {
        Log("res: short read on %s", name.c_str());
        return nullptr;
    }
    bool ok;
    if (h.flags & 1) {
        // fn_8012ACA0: TOC from the library (tocres.res), resource "<set minus ext>.toc", type 'ETOC'.
        std::string tn = name.substr(0, name.rfind('.')) + ".toc";
        for (auto& c : tn) c = (char)tolower((u8)c);  // strlwr
        u32 info;
        s32 tsz = 0;
        bool got;
        const u8* toc = Get(tn, kETOC, &info, &tsz, nullptr, &got);
        if (!toc) { Log("res(%s): Library has no TOC for set.", name.c_str()); return nullptr; }
        // ResToc: version, size, createTime, count, loadSize, then the entries (GetToc).
        u32 tver = rd_le32(toc), tsize = rd_le32(toc + 4), tctime = rd_le32(toc + 8), tcount = rd_le32(toc + 12),
            tload = rd_le32(toc + 16);
        if (tver != 0 || tsize != 0x14) Log("res: Bad external toc version/size (%d != %d || %d != %d)", tver, 0, tsize, 0x14);
        if (h.createTime != tctime) Log("res(%s): TOC mismatch (create_time)", name.c_str());
        if ((u32)h.resCount != tcount) Log("res(%s): TOC mismatch (res_count)", name.c_str());
        if ((u32)h.loadSize != tload) Log("res(%s): TOC mismatch (load_size)", name.c_str());
        if (0x14 + (int64_t)tcount * 0x44 > tsz) { Log("res(%s): TOC truncated", name.c_str()); return nullptr; }
        for (u32 i = 0; i < tcount; i++) set->entries.push_back(ParseEntry(toc + 0x14 + i * 0x44));
        ok = FixupEntries(set.get());
    } else {
        // fn_8012AA60: the TOC is the start of the resident image; its CRC covers count * 0x44 bytes.
        if ((int64_t)h.resCount * 0x44 > h.loadSize) { Log("res: TOC beyond load size on %s", name.c_str()); return nullptr; }
        ok = h.tocCrc == GameCrc32(set->mem.data(), h.resCount * 0x44);
        if (!ok) Log("res: Bad TOC crc on %s", name.c_str());
        for (s32 i = 0; ok && i < h.resCount; i++) set->entries.push_back(ParseEntry(set->mem.data() + i * 0x44));
        if (ok) ok = FixupEntries(set.get());
    }
    if (!ok) return nullptr;
    sets_.push_back(std::move(set));
    return sets_.back().get();
}

// fn_8012B534 with flags 0: "set/name" or "name" (any set), optional leading '*'.
Set* System::Lookup(const std::string& full, int* idx, bool quiet) {
    std::string n = full;
    bool star = !n.empty() && n[0] == '*';
    if (star) n.erase(0, 1);
    std::string setName, resName;
    size_t slash = n.rfind('/');  // fn_8012B3D4
    if (slash != std::string::npos) {
        setName = n.substr(0, slash);
        resName = n.substr(slash + 1);
    } else {
        resName = n;
    }
    *idx = -1;
    Set* found = nullptr;
    for (auto& s : sets_) {
        if (!setName.empty() && strcasecmp(s->name.c_str(), setName.c_str())) continue;
        for (size_t i = 0; i < s->entries.size(); i++) {
            if (!strcasecmp(s->entries[i].name.c_str(), resName.c_str()) && *idx == -1) {
                found = s.get();
                *idx = (int)i;
            }
        }
    }
    // A plain lookup only accepts resident resources; '*' allows on-demand extraction.
    // The port allows extraction for every name so that .tm0 streams (ResourceGetStream) work too.
    if (!found && !quiet) Log("res: can't find %s", full.c_str());
    return found;
}

// fn_8012B6FC: read a non-resident resource from the set file and check its CRC.
bool System::MakeResident(Set* set, int idx) {
    Entry& e = set->entries[idx];
    if (!(e.flags & 8)) {
        if (!ReadAll(set->path, set->hdr.baseOffset + e.offset, e.size, e.extracted, nullptr)) {
            Log("res: read failed on extraction: %s", e.name.c_str());
            return false;
        }
        if (e.crc == GameCrc32(e.extracted.data(), e.size)) {
            e.flags |= 0xC;
            e.body = e.extracted.data();
            Log("EXTRACTION: LOADED %s/%s", set->name.c_str(), e.name.c_str());
        } else {
            Log("res: crc failure on extraction: %s", e.name.c_str());
            e.extracted.clear();
        }
    }
    return (e.flags & 8) != 0;
}

// fn_8012C370 -> fn_8012B85C.
u8* System::Get(const std::string& name, u32 type, u32* info, s32* size, bool* first, bool* got) {
    int idx;
    Set* set = Lookup(name, &idx, false);
    if (!set) return nullptr;
    if (!MakeResident(set, idx)) {
        Log("res: Can't make %s resident.", name.c_str());
        return nullptr;
    }
    Entry& e = set->entries[idx];
    if (type != e.type) {
        Log("res: ResourceGet(\"%s\"): Expected type 0x%08x, got 0x%08x", name.c_str(), type, e.type);
        return nullptr;
    }
    e.flags |= 2;
    if (got) {
        *got = !(e.flags & 1);
        e.flags |= 1;
    }
    if (size) *size = e.size;
    if (first) *first = e.refs == 0;
    e.refs++;
    *info = e.info;
    return e.body;
}

}  // namespace res

namespace res {
void System::UnloadSet(const std::string& name) {
    for (size_t i = 0; i < sets_.size(); i++)
        if (!strcasecmp(sets_[i]->name.c_str(), name.c_str())) {
            sets_.erase(sets_.begin() + i);
            return;
        }
}
}  // namespace res
