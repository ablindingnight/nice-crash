// enhance.exe: "Enhance my game" for movies. Turns the frames the game dumped from the player's own movie
// (NiceCrashGame with NICECRASH_VIDEODUMP, sdk/movie.cpp) into an enhanced video the game plays instead:
//   %LOCALAPPDATA%\NiceCrash\cache\enhanced\<movie>.mp4
// Everything stays on the player's PC; only Windows components are used (WIC to read and scale images, Media
// Foundation's H.264 encoder to write the video). An AI upscaler (Real-ESRGAN / Real-CUGAN ncnn-vulkan) can be
// run on the frames first with --upscaler; without one the frames are scaled with WIC's high-quality cubic filter.
//
//   enhance.exe --frames <dir> --name uisingle [--scale 2] [--fps 29.97] [--out file.mp4]
//               [--upscaler <exe> [--model <name>]] [--bitrate <Mbit/s>]
// Prints "progress <done> <total> <stage>" lines for the launcher.
//
// Textures ("Enhance my game" for menus, HUD and fonts): the textures the game dumped while walking its menus
// (NICECRASH_TEXPACK_DUMP, sdk/texpack.cpp, Dolphin's names) become a texture pack the game loads with
// graphics.enhanced_assets on:
//   enhance.exe --textures <dump dir> --out <pack dir> [--upscaler <exe> [--model realesr-animevideov3]]
// Only 2D art is taken: textures drawn without mip levels (menus, HUD, fonts, sky cards) and small intensity
// textures (HUD digits, arrows, glows). Up to 512 pixels they are upscaled 4x, 1024 pixels 2x (nothing above
// 2048). Each result is kept inside the colour and alpha range of its 2x2 source cell (a little margin): no halos,
// no haze in clear areas, opaque art stays opaque. Textures already in the pack are skipped.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::wstring W(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}

static std::vector<std::string> Frames(const std::string& dir, const std::string& name, const char* ext) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\" + name + "_*." + ext).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do out.push_back(dir + "\\" + fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// One image -> BGRA rows (top-down) at w x h (scaled when the image differs).
static bool Load(IWICImagingFactory* f, const std::string& path, UINT w, UINT h, std::vector<BYTE>& bgra, UINT* iw,
                 UINT* ih) {
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(f->CreateDecoderFromFilename(W(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return false;
    IWICBitmapFrameDecode* fr = nullptr;
    dec->GetFrame(0, &fr);
    UINT sw = 0, sh = 0;
    fr->GetSize(&sw, &sh);
    if (iw) *iw = sw;
    if (ih) *ih = sh;
    if (!w) w = sw;
    if (!h) h = sh;
    IWICBitmapSource* src = fr;
    IWICBitmapScaler* sc = nullptr;
    if (sw != w || sh != h) {
        f->CreateBitmapScaler(&sc);
        sc->Initialize(fr, w, h, (WICBitmapInterpolationMode)4);  // HighQualityCubic (Windows 10+)
        src = sc;
    }
    IWICFormatConverter* cv = nullptr;
    f->CreateFormatConverter(&cv);
    HRESULT hr = cv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom);
    bgra.resize((size_t)w * h * 4);
    if (SUCCEEDED(hr)) hr = cv->CopyPixels(nullptr, w * 4, (UINT)bgra.size(), bgra.data());
    cv->Release();
    if (sc) sc->Release();
    fr->Release();
    dec->Release();
    return SUCCEEDED(hr);
}

// Runs the upscaler from its own folder (it finds models\ there); prints progress from the PNGs it has written.
static int RunUpscaler(const std::string& cmd, const std::string& cwd, const std::string& outDir,
                       const std::string& name, size_t total, size_t before) {
    STARTUPINFOA si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::string c = cmd;
    if (!CreateProcessA(nullptr, &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi))
        return -1;
    size_t last = (size_t)-1;
    while (WaitForSingleObject(pi.hProcess, 1000) == WAIT_TIMEOUT) {
        size_t n = Frames(outDir, name, "png").size();
        if (n != last) {
            printf("progress %zu %zu upscale\n", before + n, total);
            fflush(stdout);
            last = n;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static bool SavePng(IWICImagingFactory* f, const std::string& path, const std::vector<BYTE>& bgra, UINT w, UINT h) {
    IWICStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    bool ok = SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(W(path).c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
              SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h));
    if (ok) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(fr->SetPixelFormat(&pf)) && IsEqualGUID(pf, GUID_WICPixelFormat32bppBGRA) &&
             SUCCEEDED(fr->WritePixels(h, w * 4, (UINT)bgra.size(), (BYTE*)bgra.data())) && SUCCEEDED(fr->Commit()) &&
             SUCCEEDED(enc->Commit());
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    return ok;
}

// The 2D art among the dumped textures (tex1_<w>x<h>[_m]_<hash>[_<tlut hash>]_<fmt>.png): the scale to enhance it
// at (4 or 2), or 0 to leave it out.
static int TextureScale(const std::string& leaf) {
    int w = 0, h = 0;
    if (sscanf(leaf.c_str(), "tex1_%dx%d", &w, &h) != 2 || w < 4 || h < 4) return 0;
    bool mips = leaf.find("_m_") != std::string::npos;
    size_t dot = leaf.rfind('.'), us = leaf.rfind('_', dot);
    int fmt = atoi(leaf.substr(us + 1, dot - us - 1).c_str());
    int big = w > h ? w : h;
    if (mips && !(fmt <= 3 && big <= 256)) return 0;  // a 3D world texture (small intensity HUD art stays in)
    return big <= 512 ? 4 : big <= 1024 ? 2 : 0;
}

static std::vector<std::string> Pngs(const std::string& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// Keep each upscaled pixel inside the range of the source's 2x2 cell around it (plus a margin): the AI keeps its
// sharper edges but cannot add halos, haze in clear areas or holes in opaque art.
static void ClampToSource(const std::vector<BYTE>& s, UINT sw, UINT sh, std::vector<BYTE>& d, UINT dw, UINT dh) {
    const int margin = 6;
    for (UINT y = 0; y < dh; y++) {
        float sy = (y + 0.5f) * sh / dh - 0.5f;
        int y0 = sy < 0 ? 0 : (int)sy, y1 = y0 + 1 < (int)sh ? y0 + 1 : y0;
        for (UINT x = 0; x < dw; x++) {
            float sx = (x + 0.5f) * sw / dw - 0.5f;
            int x0 = sx < 0 ? 0 : (int)sx, x1 = x0 + 1 < (int)sw ? x0 + 1 : x0;
            const BYTE* p[4] = {&s[((size_t)y0 * sw + x0) * 4], &s[((size_t)y0 * sw + x1) * 4],
                                &s[((size_t)y1 * sw + x0) * 4], &s[((size_t)y1 * sw + x1) * 4]};
            BYTE* o = &d[((size_t)y * dw + x) * 4];
            for (int c = 0; c < 4; c++) {
                int lo = 255, hi = 0;
                for (auto q : p) lo = q[c] < lo ? q[c] : lo, hi = q[c] > hi ? q[c] : hi;
                int m = c == 3 && (lo == 0 || hi == 255) ? 0 : margin;  // fully clear / fully opaque stays exact
                lo = lo - m < 0 ? 0 : lo - m;
                hi = hi + m > 255 ? 255 : hi + m;
                o[c] = (BYTE)(o[c] < lo ? lo : o[c] > hi ? hi : o[c]);
            }
        }
    }
}

static int Textures(const std::string& dump, const std::string& out, const std::string& upscaler, std::string model) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return 6;
    CreateDirectoryA(out.c_str(), nullptr);
    std::vector<std::string> have = Pngs(out), todo[2];  // [0] 4x, [1] 2x
    for (const std::string& leaf : Pngs(dump)) {
        int k = TextureScale(leaf);
        if (k && !std::binary_search(have.begin(), have.end(), leaf)) todo[k == 4 ? 0 : 1].push_back(leaf);
    }
    size_t total = todo[0].size() + todo[1].size(), done = 0;
    printf("textures: %zu to enhance (%zu already in the pack)\n", total, have.size());
    printf("progress 0 %zu upscale\n", total);
    fflush(stdout);
    // The photo model (realesrgan-x4plus, best for the intro video) smears flat UI art: the anime video model keeps
    // text and outlines faithful.
    if (model.empty() || model == "realesrgan-x4plus") model = "realesr-animevideov3";
    std::string updir = upscaler.substr(0, upscaler.find_last_of("\\/") + 1);
    std::string bin = out + "\\_in", bout = out + "\\_up";
    int failed = 0;
    for (int g = 0; g < 2; g++) {
        if (todo[g].empty()) continue;
        int k = g ? 2 : 4;
        for (const std::string& d : {bin, bout}) {
            for (auto& f : Pngs(d)) DeleteFileA((d + "\\" + f).c_str());
            CreateDirectoryA(d.c_str(), nullptr);
        }
        for (const std::string& leaf : todo[g])
            if (!CreateHardLinkA((bin + "\\" + leaf).c_str(), (dump + "\\" + leaf).c_str(), nullptr))
                CopyFileA((dump + "\\" + leaf).c_str(), (bin + "\\" + leaf).c_str(), FALSE);
        bool x4only = model.find("x4plus") != std::string::npos;
        int run = x4only ? 4 : k;
        if (!upscaler.empty()) {
            std::string cmd = "\"" + upscaler + "\" -i \"" + bin + "\" -o \"" + bout + "\" -s " + std::to_string(run) +
                              " -n " + model + (x4only ? "" : "-x" + std::to_string(run)) + " -f png";
            STARTUPINFOA si{sizeof si};
            PROCESS_INFORMATION pi{};
            std::string c = cmd;
            if (!CreateProcessA(nullptr, &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, updir.c_str(), &si,
                                &pi)) {
                fprintf(stderr, "enhance: cannot start %s\n", cmd.c_str());
                return 4;
            }
            size_t last = (size_t)-1;
            while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT) {
                size_t n = Pngs(bout).size();
                if (n != last) printf("progress %zu %zu upscale\n", done + n, total), fflush(stdout), last = n;
            }
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code) fprintf(stderr, "enhance: the upscaler exited %lu: %s\n", code, cmd.c_str());
        }
        for (const std::string& leaf : todo[g]) {
            std::vector<BYTE> src, dst;
            UINT sw = 0, sh = 0;
            std::string up = bout + "\\" + leaf;
            if (!Load(wic, dump + "\\" + leaf, 0, 0, src, &sw, &sh)) {
                failed++;
                continue;
            }
            // The AI's image at k x the size (scaled when the model only does 4x); without an upscaler, WIC's cubic.
            bool ai = !upscaler.empty() && GetFileAttributesA(up.c_str()) != INVALID_FILE_ATTRIBUTES;
            if (!upscaler.empty() && !ai) {
                failed++;
                continue;
            }
            if (!Load(wic, ai ? up : dump + "\\" + leaf, sw * k, sh * k, dst, nullptr, nullptr)) {
                failed++;
                continue;
            }
            ClampToSource(src, sw, sh, dst, sw * k, sh * k);
            std::string tmp = out + "\\" + leaf + ".part";
            if (!SavePng(wic, tmp, dst, sw * k, sh * k) ||
                !MoveFileExA(tmp.c_str(), (out + "\\" + leaf).c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileA(tmp.c_str());
                failed++;
                continue;
            }
            done++;
            printf("progress %zu %zu upscale\n", done, total);
            fflush(stdout);
        }
    }
    for (const std::string& d : {bin, bout}) {
        for (auto& f : Pngs(d)) DeleteFileA((d + "\\" + f).c_str());
        RemoveDirectoryA(d.c_str());
    }
    wic->Release();
    printf("wrote %zu textures to %s (%d failed)\n", done, out.c_str(), failed);
    return failed && !done ? 5 : 0;
}

int main(int argc, char** argv) {
    std::string frames, name = "uisingle", out, upscaler, model = "realesrgan-x4plus", textures;
    int scale = 2, mbit = 0;
    double fps = 30000.0 / 1001.0;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--frames") frames = v;
        else if (k == "--name") name = v;
        else if (k == "--scale") scale = atoi(v.c_str());
        else if (k == "--fps") fps = atof(v.c_str());
        else if (k == "--out") out = v;
        else if (k == "--upscaler") upscaler = v;
        else if (k == "--model") model = v;
        else if (k == "--bitrate") mbit = atoi(v.c_str());
        else if (k == "--textures") textures = v;
    }
    auto full = [](std::string& p) {  // the upscaler runs from its own folder: every path absolute
        if (p.empty()) return;
        char b[MAX_PATH];
        if (GetFullPathNameA(p.c_str(), MAX_PATH, b, nullptr)) p = b;
    };
    full(frames);
    full(out);
    full(upscaler);
    full(textures);
    if (!textures.empty()) {
        if (out.empty()) {
            fprintf(stderr, "usage: enhance.exe --textures <dump dir> --out <pack dir> [--upscaler exe] [--model name]\n");
            return 2;
        }
        return Textures(textures, out, upscaler, model);
    }
    if (frames.empty() || scale < 1 || scale > 4) {
        fprintf(stderr, "usage: enhance.exe --frames <dir> --name <movie> [--scale 2] [--out file.mp4] [--upscaler exe]\n");
        return 2;
    }
    if (out.empty()) {
        char* la = getenv("LOCALAPPDATA");
        std::string dir = std::string(la ? la : ".") + "\\NiceCrash\\cache\\enhanced";
        CreateDirectoryA((std::string(la ? la : ".") + "\\NiceCrash\\cache").c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
        out = dir + "\\" + name + ".mp4";
    }
    std::vector<std::string> in = Frames(frames, name, "bmp");
    if (in.empty()) {
        fprintf(stderr, "enhance: no %s_*.bmp frames in %s\n", name.c_str(), frames.c_str());
        return 3;
    }
    // Optional AI upscale, in batches of 100 frames (4x PNGs of the whole movie would need ~16 GB): each batch is
    // upscaled into <frames>_up, encoded, and deleted before the next.
    const size_t kBatch = 100;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return 6;
    std::vector<BYTE> px;
    UINT bw = 0, bh = 0;
    if (!Load(wic, in[0], 0, 0, px, &bw, &bh)) return 7;
    UINT w = bw * scale, h = bh * scale;
    w &= ~1u;
    h &= ~1u;
    if (!mbit) mbit = (int)(3.0 * w * h / (640.0 * 448.0) + 0.5);  // ~3 Mbit/s per Bink-sized picture (2x: 12)
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return 8;
    IMFSinkWriter* sink = nullptr;
    DeleteFileA(out.c_str());
    IMFAttributes* sa = nullptr;
    MFCreateAttributes(&sa, 1);
    sa->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    HRESULT hr = MFCreateSinkWriterFromURL(W(out).c_str(), nullptr, sa, &sink);
    sa->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "enhance: cannot create %s (0x%08lx)\n", out.c_str(), hr);
        return 9;
    }
    UINT32 fn = 30000, fd = 1001;
    if (fps < 29.0 || fps > 31.0) {
        fn = (UINT32)(fps * 1000 + 0.5);
        fd = 1000;
    }
    IMFMediaType* ot = nullptr;
    MFCreateMediaType(&ot);
    ot->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    ot->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    ot->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)mbit * 1000000u);
    ot->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(ot, MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(ot, MF_MT_FRAME_RATE, fn, fd);
    MFSetAttributeRatio(ot, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD stream = 0;
    hr = sink->AddStream(ot, &stream);
    ot->Release();
    IMFMediaType* it = nullptr;
    MFCreateMediaType(&it);
    it->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    it->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    it->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(it, MF_MT_FRAME_SIZE, w, h);
    MFSetAttributeRatio(it, MF_MT_FRAME_RATE, fn, fd);
    MFSetAttributeRatio(it, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    it->SetUINT32(MF_MT_DEFAULT_STRIDE, w * 4);
    if (SUCCEEDED(hr)) hr = sink->SetInputMediaType(stream, it, nullptr);
    it->Release();
    if (SUCCEEDED(hr)) hr = sink->BeginWriting();
    if (FAILED(hr)) {
        fprintf(stderr, "enhance: the H.264 encoder refused %ux%u (0x%08lx)\n", w, h, hr);
        return 10;
    }
    LONGLONG dur = (LONGLONG)(10000000.0 * fd / fn);
    std::string bin = frames + "_in", bout = frames + "_up";
    int upScale = model.find("x4plus") != std::string::npos ? 4 : scale;  // x4plus only works at 4x
    std::string updir = upscaler.substr(0, upscaler.find_last_of("\\/") + 1);
    std::vector<std::string> src;  // the current batch's images, in frame order
    size_t batch0 = 0;
    for (size_t i = 0; i < in.size(); i++) {
        if (src.empty() || i == batch0 + src.size()) {  // next batch
            batch0 = i;
            size_t n = std::min(kBatch, in.size() - i);
            if (upscaler.empty()) {
                src.assign(in.begin() + i, in.begin() + i + n);
            } else {
                for (const std::string& d : {bin, bout}) {  // empty batch folders
                    for (auto& f : Frames(d, name, d == bin ? "bmp" : "png")) DeleteFileA(f.c_str());
                    CreateDirectoryA(d.c_str(), nullptr);
                }
                for (size_t k = 0; k < n; k++) {
                    std::string leaf = in[i + k].substr(in[i + k].find_last_of('\\') + 1);
                    if (!CreateHardLinkA((bin + "\\" + leaf).c_str(), in[i + k].c_str(), nullptr))
                        CopyFileA(in[i + k].c_str(), (bin + "\\" + leaf).c_str(), FALSE);
                }
                std::string cmd = "\"" + upscaler + "\" -i \"" + bin + "\" -o \"" + bout + "\" -s " +
                                  std::to_string(upScale) + " -n " + model + " -f png";
                int rc = RunUpscaler(cmd, updir, bout, name, in.size(), i);
                src = Frames(bout, name, "png");
                if (rc != 0 || src.size() != n) {
                    fprintf(stderr, "enhance: the upscaler failed (exit %d, %zu of %zu frames): %s\n", rc, src.size(),
                            n, cmd.c_str());
                    return 4;
                }
            }
        }
        const std::string& img = src[i - batch0];
        if (!Load(wic, img, w, h, px, nullptr, nullptr)) {
            fprintf(stderr, "enhance: cannot read %s\n", img.c_str());
            return 11;
        }
        IMFMediaBuffer* buf = nullptr;
        MFCreateMemoryBuffer(w * h * 4, &buf);
        BYTE* d = nullptr;
        buf->Lock(&d, nullptr, nullptr);
        memcpy(d, px.data(), (size_t)w * h * 4);  // RGB32 with a positive stride = top-down rows
        buf->Unlock();
        buf->SetCurrentLength(w * h * 4);
        IMFSample* s = nullptr;
        MFCreateSample(&s);
        s->AddBuffer(buf);
        s->SetSampleTime((LONGLONG)(i * 10000000.0 * fd / fn));
        s->SetSampleDuration(dur);
        hr = sink->WriteSample(stream, s);
        s->Release();
        buf->Release();
        if (FAILED(hr)) {
            fprintf(stderr, "enhance: encoding frame %zu failed (0x%08lx)\n", i, hr);
            return 12;
        }
        if (upscaler.empty() && (i % 25 == 0 || i + 1 == in.size())) {
            printf("progress %zu %zu encode\n", i + 1, in.size());
            fflush(stdout);
        }
    }
    hr = sink->Finalize();
    sink->Release();
    MFShutdown();
    wic->Release();
    if (!upscaler.empty()) {
        for (const std::string& d : {bin, bout}) {
            for (auto& f : Frames(d, name, d == bin ? "bmp" : "png")) DeleteFileA(f.c_str());
            RemoveDirectoryA(d.c_str());
        }
    }
    if (FAILED(hr)) return 13;
    printf("wrote %s (%ux%u, %zu frames, %d Mbit/s%s%s)\n", out.c_str(), w, h, in.size(), mbit,
           upscaler.empty() ? "" : ", ", upscaler.empty() ? "" : model.c_str());
    return 0;
}
