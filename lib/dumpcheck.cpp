// SHA-1 through Windows CNG (bcrypt.dll, part of Windows), no third-party code.
#define WIN32_LEAN_AND_MEAN
#include "dumpcheck.h"
#include <windows.h>
#include <bcrypt.h>
#include <cctype>
#include <cstdio>
#include <vector>
#include "project.h"

static std::string Sha1File(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<unsigned char> buf(1 << 16);
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) BCryptHashData(h, buf.data(), (ULONG)n, 0);
        unsigned char d[20];
        if (BCryptFinishHash(h, d, sizeof d, 0) == 0) {
            char s[41];
            for (int i = 0; i < 20; i++) snprintf(s + i * 2, 3, "%02x", d[i]);
            hex = s;
        }
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    return hex;
}

DumpCheck CheckDump(const std::string& discFiles, const std::string& dolPath) {
    DumpCheck r;
    if (!dolPath.empty()) {
        r.dolPath = dolPath;
    } else {
        std::string p = discFiles;
        while (!p.empty() && (p.back() == '\\' || p.back() == '/')) p.pop_back();
        r.dolPath = p + "\\..\\sys\\main.dol";
    }
    r.sha1 = Sha1File(r.dolPath);
    r.verified = r.sha1 == DUMP_DOL_SHA1;
    return r;
}

static bool IsFile(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::string Full(const std::string& p) {
    char b[MAX_PATH * 4];
    DWORD n = GetFullPathNameA(p.c_str(), sizeof b, b, nullptr);
    std::string r = n ? std::string(b, n) : p;
    while (r.size() > 3 && (r.back() == '\\' || r.back() == '/')) r.pop_back();
    return r;
}
static std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool LocateDump(const std::string& dump, std::string& files, std::string& dol, std::string& why) {
    std::string d = Full(dump);
    if (IsFile(d)) {
        size_t dot = d.find_last_of('.');
        std::string ext = Lower(d.substr(dot == std::string::npos ? d.size() : dot));
        why = (ext == ".iso" || ext == ".rvz" || ext == ".wbfs" || ext == ".ciso" || ext == ".wia")
                  ? "disc images (" + ext + ") are not supported yet; extract the disc to a folder first "
                    "(e.g. Dolphin: right-click the game > Properties > Filesystem > Extract Entire Disc)"
                  : "not a folder or a known disc image: " + d;
        return false;
    }
    const char* tries[][2] = {{"\\DATA\\files", "\\DATA\\sys\\main.dol"},
                              {"\\files", "\\sys\\main.dol"},
                              {"", "\\..\\sys\\main.dol"}};
    for (auto& t : tries) {
        std::string f = d + t[0];
        if (IsFile(f + "\\tocres.res")) {
            files = f;
            if (dol.empty()) dol = Full(d + t[1]);
            return true;
        }
    }
    why = "no tocres.res found under " + d + " (expected DATA\\files, files\\, or the files folder itself)";
    return false;
}

std::string DumpGameId(const std::string& files) {
    FILE* f = fopen((files + "\\..\\sys\\boot.bin").c_str(), "rb");
    if (!f) return "";
    char id[7] = {};
    size_t n = fread(id, 1, 6, f);
    fclose(f);
    if (n != 6) return "";
    for (int i = 0; i < 6; i++)
        if (!isalnum((unsigned char)id[i])) return "";
    return id;
}
