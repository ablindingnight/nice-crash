// Host SDK: DVD. Serves the disc's file system from the player's own extracted disc (the files/ folder recorded
// by the dump importer in %LOCALAPPDATA%\NiceCrash\dump.ini). Reads complete synchronously; "async" requests are
// done by the time they return, so DVDGetCommandBlockStatus always reports DVD_STATE_END (0).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "portconfig.h"
#include "sdk.h"

// The game's view of DVDFileInfo (filecd.cpp, SDK dvd.h): command block 0x30, then start offset and length.
struct DVDFileInfo {
    u8 block[0x30];
    u32 startAddr;  // offset 0x30
    u32 length;     // offset 0x34
    void* callback; // offset 0x38
};

struct DvdFile {
    DVDFileInfo* info;
    HANDLE h;
};
static DvdFile g_files[64];

static std::string g_root;

static const std::string& Root() {
    if (g_root.empty()) {
        portcfg::Values v;
        if (const char* e = getenv("EXCITE_TRUCK_DISC")) g_root = e;
        else if (portcfg::Read(portcfg::DumpIniPath(), v)) g_root = v["disc"];
        if (g_root.empty()) {
            fprintf(stderr, "[dvd] no disc configured: run build/import.exe <your extracted disc folder> first\n");
            exit(95);
        }
    }
    return g_root;
}

static DvdFile* Find(DVDFileInfo* info) {
    for (auto& f : g_files)
        if (f.info == info) return &f;
    return nullptr;
}

extern "C" {

int DVDOpen(const char* path, DVDFileInfo* info) {
    std::string p = Root();
    if (*path != '/' && *path != '\\') p += '\\';
    for (const char* c = path; *c; c++) p += (*c == '/') ? '\\' : *c;
    HANDLE h = CreateFileA(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        sdk_log("DVDOpen %s: not found", path);
        return 0;
    }
    DvdFile* slot = Find(info);
    if (!slot) slot = Find(nullptr);
    if (!slot) {
        CloseHandle(h);
        return 0;
    }
    slot->info = info;
    slot->h = h;
    memset(info, 0, sizeof(*info));
    info->length = GetFileSize(h, nullptr);
    sdk_log("DVDOpen %s (%u bytes)", path, info->length);
    return 1;
}

int DVDClose(DVDFileInfo* info) {
    DvdFile* f = Find(info);
    if (!f) return 0;
    CloseHandle(f->h);
    f->info = nullptr;
    f->h = nullptr;
    return 1;
}

s32 DVDReadPrio(DVDFileInfo* info, void* dst, s32 size, s32 offset, s32 prio) {
    DvdFile* f = Find(info);
    if (!f) return -1;
    LARGE_INTEGER li;
    li.QuadPart = offset;
    SetFilePointerEx(f->h, li, nullptr, FILE_BEGIN);
    DWORD got = 0;
    if (!ReadFile(f->h, dst, (DWORD)size, &got, nullptr)) return -1;
    return (s32)got;
}

int DVDReadAsyncPrio(DVDFileInfo* info, void* dst, s32 size, s32 offset, void (*cb)(s32, DVDFileInfo*), s32 prio) {
    s32 r = DVDReadPrio(info, dst, size, offset, prio);
    if (r < 0) return 0;
    if (cb) cb(r, info);
    return 1;
}

s32 DVDGetCommandBlockStatus(const void*) { return 0; }  // DVD_STATE_END
s32 DVDGetDriveStatus(void) { return 0; }                // DVD_STATE_END: idle, disc present
int DVDCancelAll(void) { return 1; }

}  // extern "C"
