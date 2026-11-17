// Texture packs: replacement textures by content, in Dolphin's custom-texture naming, so packs and tools made for
// Dolphin work unchanged (and "Enhance my game" can make its own).
//
//   name = tex1_<w>x<h>[_m]_<XXH64 of the level-0 data>[_<XXH64 of the palette entries used>]_<GX format>
//   (Dolphin VideoCommon/TextureInfo.cpp CalculateTextureName: the data hashed is the texture's size rounded up
//   to whole blocks; "_m" when the sampler uses mip levels; for C4/C8/C14X2 the palette range min..max index).
//
// * Loading (graphics.enhanced_assets on): every .png under %LOCALAPPDATA%\NiceCrash\textures\REXE01\ (any
//   subfolders, as Dolphin's Load\Textures\REXE01) is indexed by file name. A texture whose name matches is
//   uploaded from that image at the image's own size (4K is fine) with mip levels; the game and the shaders keep
//   using the Wii size (the TEV programs read sizes from the BP registers). Palette wildcards ("_$_") match too.
// * Dumping (NICECRASH_TEXPACK_DUMP=1): each texture the game uses is written once as
//   %LOCALAPPDATA%\NiceCrash\dump\textures\REXE01\<name>.png, the starting point for a pack.
// EFB copies (reflections, shadows, the sky map) are never replaced or dumped: they are drawn every frame.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <utility>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "env.h"
#include "portconfig.h"
#include "sdk.h"
#include "settings.h"
#include "texpack.h"

namespace {

// ---- XXH64 (the public xxHash algorithm, seed 0 as Dolphin uses it) ----
const u64 P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full, P3 = 0x165667B19E3779F9ull,
          P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;
inline u64 Rotl(u64 x, int r) { return (x << r) | (x >> (64 - r)); }
inline u64 Read64(const u8* p) { u64 v; memcpy(&v, p, 8); return v; }  // little-endian host
inline u32 Read32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
inline u64 Round(u64 acc, u64 in) { return Rotl(acc + in * P2, 31) * P1; }
inline u64 Merge(u64 acc, u64 v) { return (acc ^ Round(0, v)) * P1 + P4; }

u64 XXH64(const u8* p, size_t len, u64 seed) {
    const u8* end = p + len;
    u64 h;
    if (len >= 32) {
        u64 v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        const u8* limit = end - 32;
        do {
            v1 = Round(v1, Read64(p));
            v2 = Round(v2, Read64(p + 8));
            v3 = Round(v3, Read64(p + 16));
            v4 = Round(v4, Read64(p + 24));
            p += 32;
        } while (p <= limit);
        h = Rotl(v1, 1) + Rotl(v2, 7) + Rotl(v3, 12) + Rotl(v4, 18);
        h = Merge(h, v1);
        h = Merge(h, v2);
        h = Merge(h, v3);
        h = Merge(h, v4);
    } else {
        h = seed + P5;
    }
    h += (u64)len;
    while (p + 8 <= end) {
        h ^= Round(0, Read64(p));
        h = Rotl(h, 27) * P1 + P4;
        p += 8;
    }
    if (p + 4 <= end) {
        h ^= (u64)Read32(p) * P1;
        h = Rotl(h, 23) * P2 + P3;
        p += 4;
    }
    while (p < end) {
        h ^= (u64)*p++ * P5;
        h = Rotl(h, 11) * P1;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}

bool SelfTest() {  // known XXH64 values (seed 0)
    return XXH64((const u8*)"", 0, 0) == 0xEF46DB3751D8E999ull && XXH64((const u8*)"abc", 3, 0) == 0x44BC2CF5AD770999ull;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ---- the pack index ----
std::unordered_map<std::string, std::string> g_index;  // lower-case name without .png -> path
bool g_indexed, g_loading;

void Scan(const std::string& dir) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            Scan(dir + "\\" + n);
        } else if (n.size() > 4 && Lower(n.substr(n.size() - 4)) == ".png") {
            g_index[Lower(n.substr(0, n.size() - 4))] = dir + "\\" + n;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void Index() {
    if (g_indexed) return;
    g_indexed = true;
    g_loading = Settings_GetInt("graphics.enhanced_assets") != 0;
    if (!g_loading) return;
    std::string dir = Settings_FindData("\\textures\\REXE01");
    Scan(dir);
    if (!SelfTest()) {
        fprintf(stderr, "[texpack] XXH64 self-test failed: texture packs off\n");
        g_index.clear();
    }
    if (!g_index.empty()) fprintf(stderr, "[texpack] %zu replacement textures in %s\n", g_index.size(), dir.c_str());
}

IWICImagingFactory* Wic() {
    static IWICImagingFactory* f;
    static bool tried;
    if (!tried) {
        tried = true;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                    (void**)&f)))
            f = nullptr;
    }
    return f;
}

std::wstring Wide(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}

bool LoadPng(const std::string& path, std::vector<u8>& rgba, u32* w, u32* h) {
    IWICImagingFactory* f = Wic();
    if (!f) return false;
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(f->CreateDecoderFromFilename(Wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                            &dec)))
        return false;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* cv = nullptr;
    bool ok = SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(fr->GetSize(w, h)) &&
              SUCCEEDED(f->CreateFormatConverter(&cv)) &&
              SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                       WICBitmapPaletteTypeCustom));
    if (ok) {
        rgba.resize((size_t)*w * *h * 4);
        ok = SUCCEEDED(cv->CopyPixels(nullptr, *w * 4, (UINT)rgba.size(), rgba.data()));
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    dec->Release();
    return ok;
}

bool SavePng(const std::string& path, const std::vector<u8>& rgba, u32 w, u32 h) {
    IWICImagingFactory* f = Wic();
    if (!f) return false;
    IWICStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    bool ok = SUCCEEDED(f->CreateStream(&st)) &&
              SUCCEEDED(st->InitializeFromFilename(Wide(path).c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) &&
              SUCCEEDED(fr->SetSize(w, h));
    std::vector<u8> bgra(rgba);  // the PNG encoder takes BGRA
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    if (ok) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(fr->SetPixelFormat(&pf)) && IsEqualGUID(pf, GUID_WICPixelFormat32bppBGRA) &&
             SUCCEEDED(fr->WritePixels(h, w * 4, (UINT)bgra.size(), bgra.data())) &&
             SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    return ok;
}

}  // namespace

bool host_save_png(const char* path, const u8* rgba, int w, int h) {
    std::vector<u8> v(rgba, rgba + (size_t)w * h * 4);
    return SavePng(path, v, (u32)w, (u32)h);
}

std::string texpack_name(int fmt, int w, int h, bool mips, const u8* data, size_t bytes, const u8* tlut) {
    // The palette range the texture's indices use (Dolphin hashes only that much of the TLUT).
    size_t pal = fmt == 8 ? 16 * 2 : fmt == 9 ? 256 * 2 : fmt == 10 ? 16384 * 2 : 0;
    u32 lo = 0xFFFF, hi = 0;
    if (pal == 32)
        for (size_t i = 0; i < bytes; i++) {
            u32 a = data[i] & 15, b = data[i] >> 4;
            lo = a < lo ? a : lo, lo = b < lo ? b : lo, hi = a > hi ? a : hi, hi = b > hi ? b : hi;
        }
    else if (pal == 512)
        for (size_t i = 0; i < bytes; i++) lo = data[i] < lo ? data[i] : lo, hi = data[i] > hi ? data[i] : hi;
    else if (pal)  // C14X2: Dolphin byte-swaps each byte as a 16-bit value (swap16 of one byte), kept as written
        for (size_t i = 0; i < bytes; i += 2) {
            u32 v = (u32)((u16)data[i] << 8) & 0x3FFF;
            lo = v < lo ? v : lo, hi = v > hi ? v : hi;
        }
    char b[96];
    int n = snprintf(b, sizeof b, "tex1_%dx%d%s_%016llx", w, h, mips ? "_m" : "",
                     (unsigned long long)XXH64(data, bytes, 0));
    if (pal && tlut && hi >= lo)
        n += snprintf(b + n, sizeof b - n, "_%016llx", (unsigned long long)XXH64(tlut + 2 * lo, 2 * (hi + 1 - lo), 0));
    snprintf(b + n, sizeof b - n, "_%d", fmt);
    return b;
}

bool texpack_active() {
    Index();
    static const bool dump = NC_ENV("NICECRASH_TEXPACK_DUMP") != nullptr;
    return !g_index.empty() || dump;
}

bool texpack_replace(const std::string& name, int fmt) {
    Index();
    if (g_index.empty()) return false;
    std::string key = Lower(name);
    auto it = g_index.find(key);
    if (it == g_index.end() && fmt >= 8 && fmt <= 10) {  // palette wildcard: tex1_WxH_hash_$_fmt
        size_t last = key.rfind('_'), prev = key.rfind('_', last - 1);
        if (prev != std::string::npos && last - prev == 17)
            it = g_index.find(key.substr(0, prev) + "_$" + key.substr(last));
    }
    if (it == g_index.end()) return false;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    if (!LoadPng(it->second, rgba, &w, &h)) {
        fprintf(stderr, "[texpack] cannot read %s\n", it->second.c_str());
        g_index.erase(it);
        return false;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    typedef void(APIENTRY * GenMip_t)(GLenum);
    static GenMip_t genMip = (GenMip_t)(void*)wglGetProcAddress("glGenerateMipmap");
    if (genMip) genMip(GL_TEXTURE_2D);
    static int told;
    if (told++ < 20) fprintf(stderr, "[texpack] %s -> %ux%u\n", name.c_str(), w, h);
    return true;
}


void texpack_dump(const std::string& name, const std::vector<u8>& rgba, int w, int h) {
    static const bool dump = NC_ENV("NICECRASH_TEXPACK_DUMP") != nullptr;
    if (!dump) return;
    static std::unordered_set<std::string> done;
    if (!done.insert(name).second) return;
    static std::string dir;
    if (dir.empty()) {
        std::string base = Settings_DataDir();
        for (const char* sub : {"\\dump", "\\textures", "\\REXE01"}) {  // each level appended in turn
            base += sub;
            CreateDirectoryA(base.c_str(), nullptr);
        }
        dir = base;
    }
    std::string path = dir + "\\" + name + ".png";
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (!SavePng(path, rgba, (u32)w, (u32)h)) fprintf(stderr, "[texpack] cannot write %s\n", path.c_str());
    else if (done.size() % 100 == 0) fprintf(stderr, "[texpack] dumped %zu textures\n", done.size());
}
