// Host SDK: NAND (the Wii's internal save memory). The game's home directory is a folder on the PC,
// %LOCALAPPDATA%\NiceCrash\nand\ (NICECRASH_NAND overrides it). Paths are relative to the home directory, as the
// game uses them ("banner.bin", the save files). Operations complete synchronously; the async forms call their
// callback before returning. Result codes follow the SDK (revolution/nand/nand.h).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "portconfig.h"
#include "sdk.h"

namespace {

enum {
    NAND_RESULT_OK = 0,
    NAND_RESULT_ACCESS = -1,
    NAND_RESULT_EXISTS = -6,
    NAND_RESULT_INVALID = -8,
    NAND_RESULT_MAXFD = -10,
    NAND_RESULT_NOEXISTS = -12,
};

// The SDK structs, as far as the host uses them (layout per revolution/nand/nand.h).
struct NANDFileInfo {
    s32 fd;
    s32 tempFd;
    char openPath[64];
    char tempPath[64];
    u8 access;
    u8 stage;
    u8 mark;
};
struct NANDCommandBlock;
typedef void (*NANDAsyncCallback)(s32 result, NANDCommandBlock* block);
struct NANDCommandBlock {
    void* userData;
    NANDAsyncCallback callback;
    NANDFileInfo* info;
};

FILE* g_fds[16];

std::string Home() {
    static std::string home;
    if (home.empty()) {
        const char* e = getenv("NICECRASH_NAND");
        home = e ? e : portcfg::Dir() + "\\nand";
        CreateDirectoryA(home.c_str(), nullptr);
    }
    return home;
}

std::string HostPath(const char* path) {
    std::string p = Home();
    if (*path != '/') p += '\\';
    for (const char* c = path; *c; c++) p += (*c == '/') ? '\\' : *c;
    return p;
}

s32 Done(s32 r, NANDAsyncCallback cb, NANDCommandBlock* block, NANDFileInfo* info = nullptr) {
    if (block) {
        block->callback = cb;
        if (info) block->info = info;
    }
    if (cb) cb(r, block);
    return NAND_RESULT_OK;  // the request was accepted
}

}  // namespace

extern "C" {

s32 NANDInit(void) { return NAND_RESULT_OK; }

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    std::string p = HostPath(path);
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return NAND_RESULT_EXISTS;
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return NAND_RESULT_ACCESS;
    fclose(f);
    sdk_log("NANDCreate %s", path);
    return NAND_RESULT_OK;
}
s32 NANDCreateAsync(const char* path, u8 perm, u8 attr, NANDAsyncCallback cb, NANDCommandBlock* block) {
    return Done(NANDCreate(path, perm, attr), cb, block);
}

s32 NANDDelete(const char* path) {
    std::string p = HostPath(path);
    if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return NAND_RESULT_NOEXISTS;
    if (!DeleteFileA(p.c_str())) return NAND_RESULT_ACCESS;
    sdk_log("NANDDelete %s", path);
    return NAND_RESULT_OK;
}
s32 NANDDeleteAsync(const char* path, NANDAsyncCallback cb, NANDCommandBlock* block) {
    return Done(NANDDelete(path), cb, block);
}

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 mode) {
    std::string p = HostPath(path);
    if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return NAND_RESULT_NOEXISTS;
    int fd = 0;
    while (fd < 16 && g_fds[fd]) fd++;
    if (fd == 16) return NAND_RESULT_MAXFD;
    FILE* f = fopen(p.c_str(), mode == 1 ? "rb" : "r+b");  // 1 = read, 2 = write, 3 = read/write
    if (!f) return NAND_RESULT_ACCESS;
    g_fds[fd] = f;
    memset(info, 0, sizeof *info);
    info->fd = fd;
    info->access = mode;
    snprintf(info->openPath, sizeof info->openPath, "%s", path);
    return NAND_RESULT_OK;
}
s32 NANDOpenAsync(const char* path, NANDFileInfo* info, u8 mode, NANDAsyncCallback cb, NANDCommandBlock* block) {
    return Done(NANDOpen(path, info, mode), cb, block, info);
}

s32 NANDClose(NANDFileInfo* info) {
    if (!info || info->fd < 0 || info->fd >= 16 || !g_fds[info->fd]) return NAND_RESULT_INVALID;
    fclose(g_fds[info->fd]);
    g_fds[info->fd] = nullptr;
    info->fd = -1;
    return NAND_RESULT_OK;
}
s32 NANDCloseAsync(NANDFileInfo* info, NANDAsyncCallback cb, NANDCommandBlock* block) {
    return Done(NANDClose(info), cb, block, info);
}

// Read/Write return the byte count (>= 0) or an error.
s32 NANDRead(NANDFileInfo* info, void* buf, u32 len) {
    if (!info || info->fd < 0 || info->fd >= 16 || !g_fds[info->fd]) return NAND_RESULT_INVALID;
    return (s32)fread(buf, 1, len, g_fds[info->fd]);
}
s32 NANDReadAsync(NANDFileInfo* info, void* buf, u32 len, NANDAsyncCallback cb, NANDCommandBlock* block) {
    return Done(NANDRead(info, buf, len), cb, block, info);
}

s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 len) {
    if (!info || info->fd < 0 || info->fd >= 16 || !g_fds[info->fd]) return NAND_RESULT_INVALID;
    s32 n = (s32)fwrite(buf, 1, len, g_fds[info->fd]);
    fflush(g_fds[info->fd]);
    return n;
}
s32 NANDWriteAsync(NANDFileInfo* info, const void* buf, u32 len, NANDAsyncCallback cb, NANDCommandBlock* block) {
    return Done(NANDWrite(info, buf, len), cb, block, info);
}

s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) {
    if (!info || info->fd < 0 || info->fd >= 16 || !g_fds[info->fd]) return NAND_RESULT_INVALID;
    if (fseek(g_fds[info->fd], offset, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END)) return NAND_RESULT_INVALID;
    return (s32)ftell(g_fds[info->fd]);
}

s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    if (!info || info->fd < 0 || info->fd >= 16 || !g_fds[info->fd]) return NAND_RESULT_INVALID;
    FILE* f = g_fds[info->fd];
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    *length = (u32)ftell(f);
    fseek(f, pos, SEEK_SET);
    return NAND_RESULT_OK;
}

// Free space: the PC always has room (answer 0 = no shortage).
s32 NANDCheck(u32 neededBlocks, u32 neededFiles, u32* answer) {
    if (answer) *answer = 0;
    return NAND_RESULT_OK;
}

}  // extern "C"
