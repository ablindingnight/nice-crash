// Bink movies on the host: dumping their frames, and playing an enhanced replacement in their place.
//
// tevbink.cpp's movie draw calls port_movie_frame (patches/movie-hook) with the movie, its decoded planes and its
// rectangle on screen, every time the game draws a movie frame.
//   * NICECRASH_VIDEODUMP=<folder>: each decoded frame is written once as <folder>/<movie>_<frame>.bmp (RGB, the
//     movie's own size), the input of "Enhance my game" (tools/enhance). NICECRASH_VIDEODUMP_QUIT=1 ends the game
//     once every frame of the first movie has been written.
//   * graphics.enhanced_assets = 1 and %LOCALAPPDATA%\NiceCrash\cache\enhanced\<movie>.mp4 present: that video's
//     matching frame is drawn instead (window resolution, the 640x448 Bink frame no longer limits it). Bink keeps
//     running underneath, so the sound and the timing stay the game's own.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "env.h"
#include "settings.h"
#include "portconfig.h"
#include "sdk.h"
#include "hostcfg.h"
#include <cmath>

extern "C" void port_fifo_u8(unsigned char v);
extern "C" void port_fifo_u32(unsigned int v);
extern "C" void port_fifo_f32(float v);
u32 sdk_vi_retrace_count();  // sdk/vi.cpp

namespace {

// The game's movie texture block (kernel.cpp Video), as far as this file reads it.
struct PlaneH {
    s32 on;
    u8* buf;
    u32 width;  // the tiled texture's width (row pitch in texels)
};
struct FrameH {
    PlaneH y, u, v, a;
};
struct VideoH {
    u32 texObj[2][4][8];  // GXTexObj y/u/v/a for both buffers
    s32 count;
    u32 width, height, cwidth, cheight;
    s32 cur;
    FrameH frames[2];
};
static_assert(sizeof(u32) * 2 * 4 * 8 == 0x100, "VidTexSet x2");

// I8 texture (8x4 texel tiles of 32 bytes) -> one texel.
inline u8 TexelI8(const u8* p, u32 pitch, u32 x, u32 y) {
    static const bool tiled = NC_ENV("NICECRASH_VIDEODUMP_TILED") != nullptr;
    if (!tiled) return p[(size_t)y * pitch + x];  // what the decoder leaves at draw time: rows of `pitch` bytes
    return p[((y >> 2) * (pitch >> 3) + (x >> 3)) * 32 + (y & 3) * 8 + (x & 7)];
}

std::string BaseName(const char* name) {  // "video/uisingle.bik" -> "uisingle"
    const char* s = strrchr(name, '/');
    s = s ? s + 1 : name;
    std::string b = s;
    size_t dot = b.rfind('.');
    if (dot != std::string::npos) b.resize(dot);
    return b;
}

// Y/Cb/Cr planes -> RGB, as the game's TEV stages convert them (full range).
void ToRgb(const VideoH* v, int cur, std::vector<u8>& rgb) {
    const FrameH& f = v->frames[cur];
    u32 w = v->width, h = v->height;
    rgb.resize((size_t)w * h * 3);
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            float Y = TexelI8(f.y.buf, f.y.width, x, y);  // full range
            // Plane "u" holds Cr and "v" holds Cb (checked against the game's own picture).
            float cr = TexelI8(f.u.buf, f.u.width, x >> 1, y >> 1) - 128.0f;
            float cb = TexelI8(f.v.buf, f.v.width, x >> 1, y >> 1) - 128.0f;
            // The game's TEV stages (kernel.cpp fn_802E1DA4): JPEG-style full-range coefficients.
            float r = Y + 1.402f * cr, g = Y - 0.344f * cb - 0.714f * cr, b = Y + 1.772f * cb;
            u8* o = &rgb[((size_t)y * w + x) * 3];
            o[0] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r);
            o[1] = (u8)(g < 0 ? 0 : g > 255 ? 255 : g);
            o[2] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b);
        }
}

bool WriteBmp(const char* path, const std::vector<u8>& rgb, u32 w, u32 h) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    u32 stride = (w * 3 + 3) & ~3u, size = 54 + stride * h;
    u8 hd[54] = {'B', 'M'};
    memcpy(hd + 2, &size, 4);
    u32 off = 54, ih = 40, planes = 1 | (24 << 16);
    memcpy(hd + 10, &off, 4);
    memcpy(hd + 14, &ih, 4);
    memcpy(hd + 18, &w, 4);
    s32 negh = -(s32)h;  // top-down
    memcpy(hd + 22, &negh, 4);
    memcpy(hd + 26, &planes, 4);
    fwrite(hd, 1, 54, f);
    std::vector<u8> row(stride, 0);
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            const u8* s = &rgb[((size_t)y * w + x) * 3];
            row[x * 3] = s[2];
            row[x * 3 + 1] = s[1];
            row[x * 3 + 2] = s[0];
        }
        fwrite(row.data(), 1, stride, f);
    }
    fclose(f);
    return true;
}

// ---- dump ----
void Dump(const std::string& base, u32* bink, const VideoH* v) {
    static const char* dir = NC_ENV("NICECRASH_VIDEODUMP");
    // One movie per run: NICECRASH_VIDEODUMP_MOVIE names it (default uisingle, the intro "Enhance my game" upscales).
    // Not "the first one seen": on a fresh install another movie plays behind the save-file prompt first.
    static const std::string want = NC_ENV("NICECRASH_VIDEODUMP_MOVIE") ? NC_ENV("NICECRASH_VIDEODUMP_MOVIE") : "uisingle";
    static std::vector<bool> done;
    static u32 written;
    static std::string lastSeen;
    if (base != lastSeen) {
        fprintf(stderr, "[movie] dump: sees %s (frame %u of %u)\n", base.c_str(), bink[3], bink[2]);
        lastSeen = base;
    }
    if (base != want) return;
    u32 frames = bink[2], frame = bink[3];  // BINK: Width, Height, Frames, FrameNum (1-based)
    if (!frame || frame > frames) return;
    if (done.size() != frames) done.assign(frames, false);
    if (done[frame - 1]) return;
    static bool told;
    if (!told) {
        told = true;
        const FrameH& f = v->frames[v->cur];
        fprintf(stderr, "[movie] w %u h %u cw %u ch %u cur %d count %d y %p/%u u %p/%u v %p/%u tex0 %08x %08x %08x %08x\n",
                v->width, v->height, v->cwidth, v->cheight, v->cur, v->count, f.y.buf, f.y.width, f.u.buf, f.u.width,
                f.v.buf, f.v.width, v->texObj[v->cur][0][0], v->texObj[v->cur][0][1], v->texObj[v->cur][0][2], v->texObj[v->cur][0][3]);
    }
    std::vector<u8> rgb;
    ToRgb(v, v->cur, rgb);
    char path[MAX_PATH];
    CreateDirectoryA(dir, nullptr);
    snprintf(path, sizeof path, "%s/%s_%05u.bmp", dir, base.c_str(), frame);
    if (!WriteBmp(path, rgb, v->width, v->height)) {
        static bool warned;
        if (!warned) fprintf(stderr, "[movie] dump: can't write %s\n", path);
        warned = true;
        return;
    }
    done[frame - 1] = true;
    written++;
    if (NC_ENV("NICECRASH_VIDEODUMP_LOG")) {
        fprintf(stderr, "[movie] frame %u at retrace %u\n", frame, sdk_vi_retrace_count());
    }
    if (written % 100 == 0 || written == frames) fprintf(stderr, "[movie] dumped %u/%u frames of %s\n", written, frames, base.c_str());
    if (written == frames && NC_ENV("NICECRASH_VIDEODUMP_QUIT")) {
        fflush(stderr);
        ExitProcess(0);
    }
}

// ---- replacement playback (Media Foundation) ----
// A decoder thread reads the video in order into a small ring of RGB frames, a few ahead of the frame the game asked
// for, so the game thread only uploads a ready frame (decoding and colour conversion on the game thread cost ~10 ms
// per video frame at 1280x896, every other game frame). The movie looping back (the game asks for an earlier frame
// than the ring holds) rewinds the reader on the decoder thread.
struct Slot {
    long idx = -1;              // video frame in `px`, -1 = empty
    std::vector<u8> px;         // BGRA rows, top first
};
const int kSlots = 4;
struct Replacement {
    IMFSourceReader* reader = nullptr;
    GLuint tex = 0;
    u32 w = 0, h = 0;
    LONG stride = 0;
    double fps = 29.97;
    long have = -1;  // index of the frame in tex
    long next = 0;   // index of the next frame the reader returns (decoder thread)
    bool failed = false;
    // Shared with the decoder thread (under cs).
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    Slot slot[kSlots];
    long want = 0;       // the frame the game wants now
    bool rewind = false; // the game went back: start the reader over
    bool ended = false;  // the reader hit the end (or failed) and waits for a rewind
    int busy = -1;       // slot being uploaded by the game thread (not to be overwritten)
};
std::unordered_map<std::string, Replacement> g_repl;

std::wstring Wide(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}

bool Open(Replacement& r, const std::string& path) {
    static bool started;
    if (!started) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
        started = true;
    }
    IMFAttributes* at = nullptr;
    MFCreateAttributes(&at, 1);
    at->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    HRESULT hr = MFCreateSourceReaderFromURL(Wide(path).c_str(), at, &r.reader);
    at->Release();
    if (FAILED(hr)) return false;
    r.reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    r.reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    IMFMediaType* mt = nullptr;
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = r.reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt);
    mt->Release();
    if (FAILED(hr)) return false;
    IMFMediaType* cur = nullptr;
    if (FAILED(r.reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) return false;
    UINT32 w = 0, h = 0, num = 0, den = 0;
    MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
    if (SUCCEEDED(MFGetAttributeRatio(cur, MF_MT_FRAME_RATE, &num, &den)) && den) r.fps = (double)num / den;
    UINT32 st = 0;
    r.stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) ? (LONG)st : (LONG)(w * 4);
    cur->Release();
    r.w = w;
    r.h = h;
    glGenTextures(1, &r.tex);
    glBindTexture(GL_TEXTURE_2D, r.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);  // clamp to edge
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, 0x80E1 /* GL_BGRA */, GL_UNSIGNED_BYTE, nullptr);
    fprintf(stderr, "[movie] replacement %s: %ux%u at %.2f fps\n", path.c_str(), w, h, r.fps);
    return true;
}

// Decoder thread: keeps the ring filled with the frames from r.want on, in order.
DWORD WINAPI Decoder(LPVOID arg) {
    Replacement& r = *(Replacement*)arg;
    const size_t frameBytes = (size_t)r.w * r.h * 4;
    for (;;) {
        EnterCriticalSection(&r.cs);
        int free = -1;
        for (;;) {
            if (r.rewind) break;
            // A slot is free when it is empty or holds a frame the game has gone past (and is not being uploaded).
            free = -1;
            int ahead = 0;
            for (int i = 0; i < kSlots; i++) {
                if (i == r.busy) continue;
                if (r.slot[i].idx < 0 || r.slot[i].idx < r.want) {
                    if (free < 0) free = i;
                } else {
                    ahead++;
                }
            }
            if (free >= 0 && !r.ended && ahead < kSlots) break;
            SleepConditionVariableCS(&r.cv, &r.cs, 100);
        }
        bool rewind = r.rewind;
        if (rewind) {
            r.rewind = false;
            r.ended = false;
            for (int i = 0; i < kSlots; i++)
                if (i != r.busy) r.slot[i].idx = -1;
        }
        LeaveCriticalSection(&r.cs);
        if (rewind) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            pv.vt = VT_I8;
            pv.hVal.QuadPart = 0;
            r.reader->SetCurrentPosition(GUID{}, pv);  // GUID_NULL: 100-ns units
            r.next = 0;
            continue;
        }
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample* s = nullptr;
        HRESULT hr = r.reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &s);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (s) s->Release();
            EnterCriticalSection(&r.cs);
            r.ended = true;  // until the game goes back to an earlier frame
            LeaveCriticalSection(&r.cs);
            continue;
        }
        if (!s) continue;
        long idx = (long)((double)ts * r.fps / 1e7 + 0.5);
        r.next = idx + 1;
        IMFMediaBuffer* mb = nullptr;
        bool got = false;
        Slot& sl = r.slot[free];
        if (sl.px.size() != frameBytes) sl.px.resize(frameBytes);
        if (SUCCEEDED(s->ConvertToContiguousBuffer(&mb))) {
            BYTE* p = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(mb->Lock(&p, nullptr, &len))) {
                LONG st = r.stride;
                size_t row = (size_t)r.w * 4, pitch = (size_t)(st < 0 ? -st : st);
                if (pitch >= row && pitch * r.h <= len) {
                    for (u32 y = 0; y < r.h; y++)  // top row first whichever way the decoder stored them
                        memcpy(&sl.px[(size_t)y * row], p + (st > 0 ? y : r.h - 1 - y) * pitch, row);
                    got = true;
                }
                mb->Unlock();
            }
            mb->Release();
        }
        s->Release();
        EnterCriticalSection(&r.cs);
        sl.idx = got ? idx : -1;
        LeaveCriticalSection(&r.cs);
        WakeAllConditionVariable(&r.cv);
    }
    return 0;
}

void StartDecoder(Replacement& r) {
    InitializeCriticalSection(&r.cs);
    InitializeConditionVariable(&r.cv);
    if (HANDLE h = CreateThread(nullptr, 0, Decoder, &r, 0, nullptr)) {
        SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(h);
    } else {
        r.failed = true;
    }
}

// Bring the newest decoded frame not after `want` (0-based) into r.tex. Never waits for decoding, except up to
// 100 ms for the very first frame. False when nothing has been shown yet.
bool Seek(Replacement& r, long want) {
    EnterCriticalSection(&r.cs);
    if (want < r.want || want + 1 < r.have) {  // the movie looped: the decoder starts over
        bool inRing = false;
        for (int i = 0; i < kSlots; i++) inRing |= r.slot[i].idx == want;
        if (!inRing) r.rewind = true;
    }
    r.want = want;
    WakeAllConditionVariable(&r.cv);
    for (int tries = 0;; tries++) {
        int best = -1;
        for (int i = 0; i < kSlots; i++)
            if (r.slot[i].idx >= 0 && r.slot[i].idx <= want && (best < 0 || r.slot[i].idx > r.slot[best].idx)) best = i;
        if (best >= 0 && r.slot[best].idx != r.have) {
            r.busy = best;
            long idx = r.slot[best].idx;
            LeaveCriticalSection(&r.cs);
            glBindTexture(GL_TEXTURE_2D, r.tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.w, r.h, 0x80E1 /* GL_BGRA */, GL_UNSIGNED_BYTE, r.slot[best].px.data());
            EnterCriticalSection(&r.cs);
            r.busy = -1;
            r.have = idx;
            WakeAllConditionVariable(&r.cv);
            break;
        }
        if (r.have >= 0 || tries >= 10) break;
        SleepConditionVariableCS(&r.cv, &r.cs, 10);  // the first frame only
    }
    bool shown = r.have >= 0;
    LeaveCriticalSection(&r.cs);
    return shown;
}

Replacement* Find(const std::string& base) {
    static const bool on = Settings_GetInt("graphics.enhanced_assets") != 0;
    if (!on) return nullptr;
    auto it = g_repl.find(base);
    if (it == g_repl.end()) {
        Replacement& r = g_repl[base];
        std::string path = Settings_FindData("\\cache\\enhanced\\" + base + ".mp4");
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES || !Open(r, path)) r.failed = true;
        else StartDecoder(r);
        it = g_repl.find(base);
    }
    return it->second.failed ? nullptr : &it->second;
}

// ---- live sharpening (graphics.video_sharpen, no enhanced video) ----
// The game's own frame, converted on the CPU once per movie frame: Y/Cb/Cr to RGB with the half-size chroma planes
// interpolated (the game's TEV samples them bilinear too), then contrast-adaptive sharpening on luma (after AMD's
// FidelityFX CAS: a 5-tap unsharp mask weakened where the neighbourhood already has contrast, so edges don't ring
// and noise isn't boosted). gxr_host_quad then draws it bicubic at the window's size. ~2-3 ms per movie frame
// (every other game frame at 30 fps).
struct Live {
    GLuint tex = 0;
    u32 w = 0, h = 0, frame = ~0u;
    int cur = -1;
    std::vector<float> y;
    std::vector<u8> px;  // BGRA
};
std::unordered_map<std::string, Live> g_live;

inline float Chroma(const PlaneH& p, u32 cw, u32 ch, float x, float y) {  // bilinear in a half-size plane
    x = x * 0.5f - 0.25f;
    y = y * 0.5f - 0.25f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    u32 x0 = (u32)x, y0 = (u32)y;
    u32 x1 = x0 + 1 < cw ? x0 + 1 : x0, y1 = y0 + 1 < ch ? y0 + 1 : y0;
    float fx = x - x0, fy = y - y0;
    float a = TexelI8(p.buf, p.width, x0, y0), b = TexelI8(p.buf, p.width, x1, y0);
    float c = TexelI8(p.buf, p.width, x0, y1), d = TexelI8(p.buf, p.width, x1, y1);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

void LiveConvert(Live& L, const VideoH* v) {
    const FrameH& f = v->frames[v->cur];
    u32 w = v->width, h = v->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    L.y.resize((size_t)w * h);
    L.px.resize((size_t)w * h * 4);
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) L.y[(size_t)y * w + x] = TexelI8(f.y.buf, f.y.width, x, y) / 255.0f;
    const float sharp = 0.6f;  // CAS sharpness 0..1
    const float peak = -1.0f / (8.0f - 3.0f * sharp);
    for (u32 y = 0; y < h; y++) {
        const float* r0 = &L.y[(size_t)(y ? y - 1 : 0) * w];
        const float* r1 = &L.y[(size_t)y * w];
        const float* r2 = &L.y[(size_t)(y + 1 < h ? y + 1 : y) * w];
        u8* o = &L.px[(size_t)y * w * 4];
        for (u32 x = 0; x < w; x++, o += 4) {
            u32 xl = x ? x - 1 : 0, xr = x + 1 < w ? x + 1 : x;
            float c = r1[x], n = r0[x], s = r2[x], e = r1[xr], wv = r1[xl];
            float mn = fminf(c, fminf(fminf(n, s), fminf(e, wv))), mx = fmaxf(c, fmaxf(fmaxf(n, s), fmaxf(e, wv)));
            float amp = mx > 0 ? fminf(mn, 1.0f - mx) / mx : 0;
            amp = sqrtf(fmaxf(0.0f, fminf(1.0f, amp)));
            float wt = amp * peak;
            float Y = ((n + s + e + wv) * wt + c) / (1.0f + 4.0f * wt) * 255.0f;
            // Plane "u" holds Cr and "v" holds Cb (as ToRgb).
            float cr = Chroma(f.u, cw, ch, (float)x, (float)y) - 128.0f;
            float cb = Chroma(f.v, cw, ch, (float)x, (float)y) - 128.0f;
            float R = Y + 1.402f * cr, G = Y - 0.344f * cb - 0.714f * cr, B = Y + 1.772f * cb;
            o[0] = (u8)(B < 0 ? 0 : B > 255 ? 255 : B + 0.5f);
            o[1] = (u8)(G < 0 ? 0 : G > 255 ? 255 : G + 0.5f);
            o[2] = (u8)(R < 0 ? 0 : R > 255 ? 255 : R + 0.5f);
            o[3] = 255;
        }
    }
}

// The live texture for this movie frame, or 0 (off, or the frame isn't decoded yet).
GLuint LiveFrame(const std::string& base, u32* bink, const VideoH* v) {
    if (!host_cfg().videoSharpen || !bink[3] || !v->width || !v->height || v->cur < 0 || v->cur > 1) return 0;
    const FrameH& f = v->frames[v->cur];
    if (!f.y.buf || !f.u.buf || !f.v.buf) return 0;
    Live& L = g_live[base];
    if (!L.tex || L.w != v->width || L.h != v->height) {
        if (!L.tex) glGenTextures(1, &L.tex);
        L.w = v->width;
        L.h = v->height;
        glBindTexture(GL_TEXTURE_2D, L.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);  // clamp to edge
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, L.w, L.h, 0, 0x80E1 /* GL_BGRA */, GL_UNSIGNED_BYTE, nullptr);
        L.frame = ~0u;
    }
    if (L.frame != bink[3] || L.cur != v->cur) {
        LiveConvert(L, v);
        glBindTexture(GL_TEXTURE_2D, L.tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, L.w, L.h, 0x80E1 /* GL_BGRA */, GL_UNSIGNED_BYTE, L.px.data());
        L.frame = bink[3];
        L.cur = v->cur;
    }
    return L.tex;
}

}  // namespace

// tevbink.cpp (patches/movie-hook): the movie `name` is about to draw its current frame into the rectangle
// (x, y, w, h) in EFB pixels. Returns 1 when the replacement was drawn (skip the game's own draw).
extern "C" int port_movie_frame(const char* name, u32* bink, void* video, float x, float y, float w, float h) {
    if (!bink || !video) return 0;
    std::string base = BaseName(name);
    if (NC_ENV("NICECRASH_VIDEODUMP")) Dump(base, bink, (const VideoH*)video);
    Replacement* r = Find(base);
    if (!r) {  // no enhanced video: the game's own frame, sharpened and upscaled live
        GLuint t = LiveFrame(base, bink, (const VideoH*)video);
        if (!t) return 0;
        port_fifo_u8(0xC8);
        port_fifo_f32(x);
        port_fifo_f32(y);
        port_fifo_f32(x + w);
        port_fifo_f32(y + h);
        port_fifo_u32(t);
        return 1;
    }
    if (!bink[3]) return 0;
    static const bool timeIt = NC_ENV("NICECRASH_MOVIETIME") != nullptr;  // cost of each replacement frame
    LARGE_INTEGER t0, t1, fq;
    if (timeIt) QueryPerformanceCounter(&t0);
    bool ok = Seek(*r, (long)bink[3] - 1);
    if (timeIt) {
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&fq);
        fprintf(stderr, "[movietime] frame %u %.2f ms\n", bink[3], (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart);
    }
    if (!ok) return 0;
    // A port command in the GX stream (sdk/gxinterp.cpp, 0xC8): draw texture r->tex over this EFB rectangle, in
    // order with the game's own draws.
    port_fifo_u8(0xC8);
    port_fifo_f32(x);
    port_fifo_f32(y);
    port_fifo_f32(x + w);
    port_fifo_f32(y + h);
    port_fifo_u32(r->tex);
    return 1;
}
