// Host SDK: ARC (U8 archives held in memory), used by the HOME button library for homeBtn*.arc and SpeakerSe.arc.
// The whole library lives here: the decomp's partial reconstruction in game/unk_80116BD4.c is renamed out of the
// way (patches/arc-host.patch.txt).
//
// U8 layout: header (magic 0x55AA382D, FST offset, FST size, file data offset), then the FST: 12-byte nodes
//   word0 = isDir << 24 | name offset into the string table that follows the nodes,
//   word1 = parent entry (directory) or data offset from the archive start (file),
//   word2 = index past the directory's last descendant (directory) or length (file);
// node 0 is the root, its word2 the number of entries. Everything is big-endian on disc: ARCInitHandle swaps the
// header words and the node table in place, once (the swapped magic marks an archive already done), so the
// functions below read native words. File data and names are left alone.
#include <cctype>
#include <cstdint>
#include <cstring>
#include "sdk.h"

typedef int BOOL;
#define TRUE 1
#define FALSE 0

namespace {
struct ARCHandle {
    void* archiveStartAddr;
    void* FSTStart;
    void* fileStart;
    u32 entryNum;
    char* FSTStringStart;
    u32 FSTLength;
    u32 currDir;
};
struct ARCFileInfo {
    ARCHandle* handle;
    u32 startOffset;
    u32 length;
};
struct ARCDir {
    ARCHandle* handle;
    u32 entryNum;
    u32 location;
    u32 next;
};
struct ARCDirEntry {
    ARCHandle* handle;
    u32 entryNum;
    BOOL isDir;
    char* name;
};

constexpr u32 kMagic = 0x55AA382D;  // "U.8-"
inline u32 be(u32 v) { return __builtin_bswap32(v); }

inline const u32* Node(const ARCHandle* h, u32 i) { return (const u32*)h->FSTStart + i * 3; }
inline bool IsDir(const ARCHandle* h, u32 i) { return (Node(h, i)[0] & 0xFF000000) != 0; }
inline const char* Name(const ARCHandle* h, u32 i) { return h->FSTStringStart + (Node(h, i)[0] & 0xFFFFFF); }

// Case-insensitive match of the path component [name, end) against an entry name.
bool Match(const char* name, const char* end, const char* entry) {
    while (name < end) {
        if (!*entry || tolower((u8)*name) != tolower((u8)*entry)) return false;
        name++;
        entry++;
    }
    return *entry == 0;
}
}  // namespace

extern "C" {

BOOL ARCInitHandle(void* arcStart, ARCHandle* handle) {
    u32* h = (u32*)arcStart;
    if (h[0] == be(kMagic)) {
        for (int i = 1; i < 4; i++) h[i] = be(h[i]);
        u32* fst = (u32*)((u8*)arcStart + h[1]);
        u32 entries = be(fst[2]);
        for (u32 i = 0; i < entries * 3; i++) fst[i] = be(fst[i]);
        h[0] = kMagic;
    } else if (h[0] != kMagic) {
        sdk_log("ARCInitHandle: %p is not a U8 archive", arcStart);
        return FALSE;
    }
    u32* fst = (u32*)((u8*)arcStart + h[1]);
    handle->archiveStartAddr = arcStart;
    handle->FSTStart = fst;
    handle->fileStart = (u8*)arcStart + h[3];
    handle->entryNum = fst[2];
    handle->FSTStringStart = (char*)(fst + fst[2] * 3);
    handle->FSTLength = h[2];
    handle->currDir = 0;
    return TRUE;
}

s32 ARCConvertPathToEntrynum(ARCHandle* handle, const char* path) {
    u32 dir = handle->currDir;
    for (;;) {
        if (*path == 0) return (s32)dir;
        if (*path == '/') {
            dir = 0;
            path++;
            continue;
        }
        const char* end = path;
        while (*end && *end != '/') end++;
        u32 len = (u32)(end - path);
        if (len == 1 && path[0] == '.') {
            // stay
        } else if (len == 2 && path[0] == '.' && path[1] == '.') {
            dir = Node(handle, dir)[1];
        } else {
            u32 i = dir + 1, last = Node(handle, dir)[2];
            while (i < last) {
                if (Match(path, end, Name(handle, i))) break;
                i = IsDir(handle, i) ? Node(handle, i)[2] : i + 1;
            }
            if (i >= last) return -1;
            if (*end == 0) return (s32)i;
            if (!IsDir(handle, i)) return -1;
            dir = i;
        }
        path = *end ? end + 1 : end;
    }
}

BOOL ARCFastOpen(ARCHandle* handle, s32 entrynum, ARCFileInfo* info) {
    if (entrynum < 0 || (u32)entrynum >= handle->entryNum || IsDir(handle, (u32)entrynum)) return FALSE;
    info->handle = handle;
    info->startOffset = Node(handle, (u32)entrynum)[1];
    info->length = Node(handle, (u32)entrynum)[2];
    return TRUE;
}

BOOL ARCOpen(ARCHandle* handle, const char* path, ARCFileInfo* info) {
    return ARCFastOpen(handle, ARCConvertPathToEntrynum(handle, path), info);
}

void* ARCGetStartAddrInMem(ARCFileInfo* info) { return (u8*)info->handle->archiveStartAddr + info->startOffset; }
u32 ARCGetStartOffset(ARCFileInfo* info) { return info->startOffset; }
u32 ARCGetLength(ARCFileInfo* info) { return info->length; }
BOOL ARCClose(ARCFileInfo*) { return TRUE; }

BOOL ARCChangeDir(ARCHandle* handle, const char* path) {
    s32 e = ARCConvertPathToEntrynum(handle, path);
    if (e < 0 || !IsDir(handle, (u32)e)) return FALSE;
    handle->currDir = (u32)e;
    return TRUE;
}

BOOL ARCOpenDir(ARCHandle* handle, const char* path, ARCDir* dir) {
    s32 e = ARCConvertPathToEntrynum(handle, path);
    if (e < 0 || !IsDir(handle, (u32)e)) return FALSE;
    dir->handle = handle;
    dir->entryNum = (u32)e;
    dir->location = (u32)e + 1;
    dir->next = Node(handle, (u32)e)[2];
    return TRUE;
}

BOOL ARCReadDir(ARCDir* dir, ARCDirEntry* entry) {
    ARCHandle* h = dir->handle;
    u32 loc = dir->location;
    if (loc <= dir->entryNum || loc >= dir->next) return FALSE;
    entry->handle = h;
    entry->entryNum = loc;
    entry->isDir = IsDir(h, loc);
    entry->name = (char*)Name(h, loc);
    dir->location = entry->isDir ? Node(h, loc)[2] : loc + 1;
    return TRUE;
}

BOOL ARCCloseDir(ARCDir*) { return TRUE; }

}  // extern "C"
