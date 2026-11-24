// Host SDK: VF, the SD card's FAT file system (game/sdmp3.cpp's custom soundtrack), served from a music folder.
//
// The Wii game plays MP3s from an SD card: an "EXCITE TRUCK" folder first, else the whole card (8 folders deep),
// up to 100 tracks, decoded by the SDK MP3 decoder (tools/gametrial/mp3build.py). On the PC the card is a folder:
// audio.music_folder, default %USERPROFILE%\Music\Nice Crash (NICECRASH_MUSIC_DIR overrides it for tests). The card
// counts as inserted when the folder exists.
//
// The game keeps FAT 8.3 short names (12 characters) and opens files and folders by them, so each listed entry gets
// a stand-in short name ("T0001.MP3", "D0001") mapped back to its real path; the long name (UTF-8) is what the game
// shows. Paths arrive as "<dir>/<short name>" with dir "EXCITE TRUCK", "" or a stand-in folder name.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "sdk.h"
#include "settings.h"

namespace {

std::wstring g_root;               // the music folder (no trailing slash)
std::map<std::string, std::wstring> g_short;  // stand-in short name -> real path
int g_nextFile, g_nextDir;

std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string Utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

const std::wstring& Root() {
    if (g_root.empty()) {
        const char* env = getenv("NICECRASH_MUSIC_DIR");
        std::string set = env ? env : Settings_Get("audio.music_folder");
        if (!set.empty()) {
            g_root = Wide(set);
        } else {
            const char* home = getenv("USERPROFILE");
            g_root = Wide(home ? home : ".") + L"\\Music\\Nice Crash";
        }
        while (!g_root.empty() && (g_root.back() == L'\\' || g_root.back() == L'/')) g_root.pop_back();
    }
    return g_root;
}

bool Inserted() {
    DWORD a = GetFileAttributesW(Root().c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// "<dir>/<name>" as the game writes it -> a real path. dir: "" (the root), "EXCITE TRUCK" (case-insensitive on
// Windows anyway) or a stand-in folder name; name: a stand-in short name, or nothing.
std::wstring Resolve(const char* path) {
    std::string p = path ? path : "";
    for (char& c : p)
        if (c == '\\') c = '/';
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    std::wstring out = Root();
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string part = p.substr(i, j - i);
        if (!part.empty()) {
            auto it = g_short.find(part);
            out = it != g_short.end() ? it->second : out + L"\\" + Wide(part);
        }
        i = j + 1;
    }
    return out;
}

// The game's FindData (sdmp3.cpp): 0x22C bytes of the SDK's own state (the host's search handle goes there), then
// the attribute byte, the 8.3 short name and the long name.
struct FindData {
    HANDLE h;
    u8 pad[0x22C - sizeof(HANDLE)];
    u8 attr;
    char shortName[0xD];
    char longName[0x20E];
};
static_assert(sizeof(HANDLE) == 4, "32-bit host");

bool Fill(FindData* fd, const WIN32_FIND_DATAW& w, const std::wstring& dir) {
    bool isDir = (w.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    std::wstring name = w.cFileName;
    if (name == L"." || name == L"..") {
        fd->attr = 0x10;
        snprintf(fd->shortName, sizeof fd->shortName, "%s", Utf8(name).c_str());
        snprintf(fd->longName, sizeof fd->longName, "%s", Utf8(name).c_str());
        return true;
    }
    char sn[0xD];
    if (isDir) snprintf(sn, sizeof sn, "D%04d", ++g_nextDir % 10000);
    else snprintf(sn, sizeof sn, "T%04d.MP3", ++g_nextFile % 10000);
    g_short[sn] = dir + L"\\" + name;
    fd->attr = isDir ? 0x10 : 0x20;
    memcpy(fd->shortName, sn, sizeof sn);
    std::string ln = Utf8(name);
    snprintf(fd->longName, sizeof fd->longName, "%s", ln.c_str());
    return true;
}

}  // namespace

extern "C" {

s32 VFInit(u32) { return 0; }

static void (*g_insertCb)(int);
s32 VFSetMediaCallback(s32, void (*ins)(int), void (*rem)(int)) {
    g_insertCb = ins;
    (void)rem;
    return 0;
}

// The game reads the drive's "inserted" bit (flags bit 4) right after attaching.
s32 VFAttachDrive(s32, s32, s32, void* drive) {
    u8* d = (u8*)drive;
    d[8] = 'A';
    if (Inserted()) d[9] |= 0x10;
    else d[9] &= ~0x10;
    sdk_log("VF: music folder %s %s", Utf8(Root()).c_str(), Inserted() ? "found" : "not found (no custom soundtrack)");
    return 0;
}
s32 VFUnmountDrive(char, s32) { return 0; }
s32 VFDetachDrive(char) { return 0; }

s32 VFFindFirst(const char* pattern, s32, void* fdp) {
    FindData* fd = (FindData*)fdp;
    std::string pat = pattern ? pattern : "";
    size_t star = pat.rfind("/*");
    std::string dir = star == std::string::npos ? pat : pat.substr(0, star);
    std::wstring wdir = Resolve(dir.c_str());
    WIN32_FIND_DATAW w;
    HANDLE h = FindFirstFileW((wdir + L"\\*").c_str(), &w);
    if (h == INVALID_HANDLE_VALUE) return -1;
    fd->h = h;
    // Remember the folder for FindNext in the padding after the handle (a wide path; the area is 0x228 bytes).
    size_t room = (sizeof fd->pad / sizeof(wchar_t)) - 1;
    std::wstring keep = wdir.substr(0, room);
    memset(fd->pad, 0, sizeof fd->pad);
    memcpy(fd->pad, keep.c_str(), keep.size() * sizeof(wchar_t));
    Fill(fd, w, wdir);
    return 0;
}

s32 VFFindNext(void* fdp) {
    FindData* fd = (FindData*)fdp;
    WIN32_FIND_DATAW w;
    if (!fd->h || !FindNextFileW(fd->h, &w)) {
        if (fd->h) FindClose(fd->h);
        fd->h = nullptr;
        return -1;
    }
    Fill(fd, w, std::wstring((const wchar_t*)fd->pad));
    return 0;
}

void* VFOpenFile(const char* path, const char* mode) {
    (void)mode;
    std::wstring p = Resolve(path);
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) sdk_log("VF: can't open %s", Utf8(p).c_str());
    return f;
}
s32 VFCloseFile(void* fp) { return fp ? fclose((FILE*)fp) : -1; }

// info[0] = the file size (the rest of the SDK's file info is unused).
s32 VFGetFileInfo(void* fp, void* info) {
    if (!fp) return -1;
    FILE* f = (FILE*)fp;
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    ((u32*)info)[0] = (u32)ftell(f);
    fseek(f, cur, SEEK_SET);
    return 0;
}
s32 VFSeekFile(void* fp, s32 pos, s32 origin) {
    return fp && fseek((FILE*)fp, pos, origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET) == 0 ? 0 : -1;
}
s32 VFReadFile(void* buf, u32 size, u32 n, void* fp) { return fp ? (s32)fread(buf, size, n, (FILE*)fp) : 0; }

}  // extern "C"
