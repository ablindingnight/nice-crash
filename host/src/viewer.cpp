// Phase 0 viewer, reached through NiceCrash.exe's command-line flags (main.cpp; with no flags NiceCrash.exe is the
// launcher). A Win32 + OpenGL 1.1 window that mounts the
// player's own Wii (USA) disc files through the ported resource loader and shows one decoded texture
// (default: race.res/MGI_logo.tm0 with MGI_logo.tex beside it).
//
//   NiceCrash --viewer [--disc <dir>] [--dol <main.dol>] [--tex <set/name.tex>] [--screenshot <out.bmp>] [--scan]
//           [--allow-unverified]
//
// The disc folder is the extracted DATA partition's files/ directory (race.res, tocres.res ...). It
// comes from --disc, else the EXCITE_TRUCK_DISC environment variable, else "disc=" in NiceCrash.ini next
// to the executable ("dol=" there overrides <disc>/../sys/main.dol), else the dump importer's config
// (%LOCALAPPDATA%\<PROJECT_DIRNAME>\dump.ini). The dump is verified by the SHA-1
// of main.dol (dumpcheck.cpp) and refused if unknown. Nothing from the disc is copied anywhere.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "dumpcheck.h"
#include "portconfig.h"
#include "project.h"
#include "res.h"
#include "texture.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

static const u32 kTEX = FOURCC("TEX ");

struct Shown {
    std::string name;
    tex::Decoded img;
    GLuint id = 0;
};

static std::vector<Shown> g_shown;
static int g_winW = 640, g_winH = 480;  // the Wii's 4:3 frame

static std::string ExeDir() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s = p;
    return s.substr(0, s.find_last_of("\\/") + 1);
}

static std::string IniValue(const char* key) {
    FILE* f = fopen((ExeDir() + "NiceCrash.ini").c_str(), "r");
    if (!f) return "";
    char line[1024];
    std::string r;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            r = line + kl + 1;
            while (!r.empty() && (r.back() == '\n' || r.back() == '\r' || r.back() == ' ')) r.pop_back();
        }
    }
    fclose(f);
    return r;
}

static bool LoadTex(res::System& rs, const std::string& name, Shown& out) {
    u32 ver = 0;
    s32 size = 0;
    bool first = false, got = false;
    const u8* body = rs.Get(name, kTEX, &ver, &size, &first, &got);  // fn_802CA658
    out.name = name;
    if (!body) return false;
    if (!tex::Decode(body, size, ver, out.img)) {
        fprintf(stderr, "%s: %s\n", name.c_str(), out.img.error.c_str());
        return false;
    }
    printf("%-28s %4dx%-4d format 0x%02x %-14s lods %d wrap %d/%d\n", name.c_str(), out.img.w, out.img.h,
           out.img.format, out.img.gxName, out.img.hdr.lods, out.img.hdr.wrapS, out.img.hdr.wrapT);
    return true;
}

// --scan: decode every TEX resource in every set, report formats and failures.
static int Scan(res::System& rs, const std::string& disc) {
    WIN32_FIND_DATAA fd;
    std::vector<std::string> sets;
    for (const char* pat : {"*.res", "*.trk", "*.car", "*.sng"}) {
        HANDLE h = FindFirstFileA((disc + "\\" + pat).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do sets.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::map<int, int> byFormat;
    int ok = 0, bad = 0, setFail = 0;
    for (auto& sn : sets) {
        bool keep = rs.FindSet(sn) != nullptr;
        res::Set* s = rs.LoadSet(sn);
        if (!s) { setFail++; printf("SET FAIL %s\n", sn.c_str()); continue; }
        for (auto& e : s->entries) {
            if (e.type != kTEX) continue;
            u32 ver; s32 size;
            const u8* b = rs.Get(sn + "/" + e.name, kTEX, &ver, &size, nullptr, nullptr);
            tex::Decoded d;
            if (b && tex::Decode(b, size, ver, d)) { ok++; byFormat[d.format]++; }
            else { bad++; printf("TEX FAIL %s/%s: %s\n", sn.c_str(), e.name.c_str(), b ? d.error.c_str() : "get failed"); }
            e.extracted.clear();
            e.extracted.shrink_to_fit();
            if (!(e.offset + (int64_t)e.size <= s->hdr.loadSize)) { e.flags &= ~0xCu; e.body = nullptr; }
        }
        if (!keep) rs.UnloadSet(sn);
    }
    printf("\nsets %d (failed %d), TEX decoded %d, failed %d\n", (int)sets.size(), setFail, ok, bad);
    for (auto& kv : byFormat) printf("  format 0x%02x: %d\n", kv.first, kv.second);
    return bad || setFail ? 1 : 0;
}

static void Upload(Shown& s) {
    glGenTextures(1, &s.id);
    glBindTexture(GL_TEXTURE_2D, s.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.img.w, s.img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.img.rgba.data());
}

static void Quad(GLuint id, float x, float y, float w, float h) {
    glBindTexture(GL_TEXTURE_2D, id);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + w, y);
    glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1); glVertex2f(x, y + h);
    glEnd();
}

static void Draw() {
    glViewport(0, 0, g_winW, g_winH);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, g_winW, g_winH, 0, -1, 1);  // y down, like the game's 2D
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    if (g_shown.empty()) return;
    // The first texture big in the centre, the rest small along the bottom-left at native size.
    Shown& m = g_shown[0];
    float scale = (float)(g_winH - 96) / (float)m.img.h;
    if (m.img.w * scale > g_winW - 32) scale = (float)(g_winW - 32) / m.img.w;
    float w = m.img.w * scale, h = m.img.h * scale;
    Quad(m.id, (g_winW - w) / 2, (g_winH - 96 - h) / 2 + 8, w, h);
    float x = 8;
    for (size_t i = 1; i < g_shown.size(); i++) {
        float sh = g_shown[i].img.h > 80 ? 80.f : (float)g_shown[i].img.h;
        float sw = g_shown[i].img.w * sh / g_shown[i].img.h;
        Quad(g_shown[i].id, x, g_winH - sh - 8, sw, sh);
        x += sw + 8;
    }
}

static bool SaveBmp(const std::string& path) {
    std::vector<u8> px((size_t)g_winW * g_winH * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g_winW, g_winH, GL_BGR_EXT, GL_UNSIGNED_BYTE, px.data());
    int stride = (g_winW * 3 + 3) & ~3;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof ih;
    ih.biWidth = g_winW;
    ih.biHeight = g_winH;  // bottom-up, matching glReadPixels
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biSizeImage = stride * g_winH;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    std::vector<u8> row(stride, 0);
    for (int y = 0; y < g_winH; y++) {
        memcpy(row.data(), &px[(size_t)y * g_winW * 3], g_winW * 3);
        fwrite(row.data(), stride, 1, f);
    }
    fclose(f);
    return true;
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        g_winW = LOWORD(lp) ? LOWORD(lp) : 1;
        g_winH = HIWORD(lp) ? HIWORD(lp) : 1;
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

int ViewerMain(int argc, char** argv) {
    std::string disc, dol, texName = "race.res/MGI_logo.tm0", shot;
    bool scan = false, allowUnverified = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--disc" && i + 1 < argc) disc = argv[++i];
        else if (a == "--tex" && i + 1 < argc) texName = argv[++i];
        else if (a == "--screenshot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--dol" && i + 1 < argc) dol = argv[++i];
        else if (a == "--scan") scan = true;
        else if (a == "--allow-unverified") allowUnverified = true;
        else if (a == "--viewer") continue;
        else { fprintf(stderr, "usage: NiceCrash --viewer [--disc dir] [--tex set/name.tex] [--screenshot out.bmp] [--scan]\n"); return 2; }
    }
    if (disc.empty()) {
        const char* e = getenv("EXCITE_TRUCK_DISC");
        disc = e ? e : IniValue("disc");
    }
    if (dol.empty()) dol = IniValue("dol");
    if (disc.empty()) {  // the dump importer's config (tools/import)
        portcfg::Values v;
        if (portcfg::Read(portcfg::DumpIniPath(), v)) {
            disc = v["disc"];
            if (dol.empty()) dol = v["dol"];
        }
    }
    if (disc.empty()) {
        fprintf(stderr, "No dump configured. Run tools/import <your extracted disc folder> first, or pass --disc <files dir>\n");
        return 2;
    }

    printf("%s - %s\n", PROJECT_NAME, PROJECT_TAGLINE);
    DumpCheck dump = CheckDump(disc, dol);
    printf("Dump: %s  main.dol sha1 %s  %s\n", DUMP_REGION_LABEL, dump.sha1.empty() ? "(not found)" : dump.sha1.c_str(),
           dump.verified ? "VERIFIED" : "UNVERIFIED");
    if (!dump.verified && !allowUnverified) {
        fprintf(stderr, "This dump is not the supported %s retail disc (expected main.dol sha1 %s at %s).\n"
                        "Use --dol to point at its main.dol, or --allow-unverified to try anyway.\n",
                DUMP_REGION_LABEL, DUMP_DOL_SHA1, dump.dolPath.c_str());
        return 3;
    }

    res::System rs;
    bool ok = rs.Init(disc);
    if (ok && scan) {
        int r = Scan(rs, disc);
        return r;
    }
    std::string setName = texName.substr(0, texName.find('/'));
    if (ok && texName.find('/') != std::string::npos) ok = rs.LoadSet(setName) != nullptr;
    if (ok) {
        Shown a;
        if (LoadTex(rs, texName, a)) g_shown.push_back(std::move(a));
        // Show the in-memory mip chain texture too when the big one is a .tm0 (texture.cpp fn_802CA2A8).
        size_t dot = texName.rfind('.');
        if (dot != std::string::npos && !_stricmp(texName.c_str() + dot, ".tm0")) {
            Shown b;
            if (LoadTex(rs, texName.substr(0, dot) + ".tex", b)) g_shown.push_back(std::move(b));
        }
    }
    for (auto& l : rs.log) printf("  %s\n", l.c_str());
    if (!ok || g_shown.empty()) {
        fprintf(stderr, "Could not load %s from %s\n", texName.c_str(), disc.c_str());
        return 1;
    }

    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));  // assets/icon (title bar, taskbar)
    wc.lpszClassName = "ExciteTruckHost";
    RegisterClassA(&wc);
    RECT r{0, 0, g_winW, g_winH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    std::string title = std::string(PROJECT_NAME) + " - phase 0 host - " + texName +
                        (dump.verified ? "  [dump verified]" : "  [dump UNVERIFIED]");
    HWND hw = CreateWindowA(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                            CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hw);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    printf("GL: %s / %s\n", (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER));
    for (auto& s : g_shown) Upload(s);

    int frames = 0;
    for (;;) {
        MSG m;
        bool quit = false;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) quit = true;
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        if (quit) break;
        Draw();
        if (!shot.empty() && ++frames == 3) {  // a few frames so the window has its final size
            glFinish();
            printf(SaveBmp(shot) ? "saved %s\n" : "could not write %s\n", shot.c_str());
            break;
        }
        SwapBuffers(dc);
        Sleep(16);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hw, dc);
    DestroyWindow(hw);
    return 0;
}
