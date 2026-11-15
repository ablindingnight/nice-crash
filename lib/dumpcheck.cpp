// SHA-1 through Windows CNG (bcrypt.dll, part of Windows), no third-party code.
#define WIN32_LEAN_AND_MEAN
#include "dumpcheck.h"
#include <windows.h>
#include <bcrypt.h>
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
