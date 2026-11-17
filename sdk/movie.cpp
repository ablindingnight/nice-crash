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
    static std::string first;
    static std::vector<bool> done;
    static u32 written;
    if (first.empty()) first = base;
    if (base != first) return;  // one movie per run
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
    if (!WriteBmp(path, rgb, v->width, v->height)) return;
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
struct Replacement {
    IMFSourceReader* reader = nullptr;
    GLuint tex = 0;
    u32 w = 0, h = 0;
    LONG stride = 0;
    double fps = 29.97;
    long have = -1;  // index of the frame in tex
    long next = 0;   // index of the next frame the reader returns
    bool failed = false;
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

// Bring frame `want` (0-based) into r.tex. False when the video has no such frame.
bool Seek(Replacement& r, long want) {
    if (want == r.have) return true;
    if (want < r.next) {  // the movie looped: back to the start
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = 0;
        r.reader->SetCurrentPosition(GUID{}, pv);  // GUID_NULL: 100-ns units
        r.next = 0;
    }
    while (r.next <= want) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample* s = nullptr;
        if (FAILED(r.reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &s))) return false;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (s) s->Release();
            return false;
        }
        if (!s) continue;
        long idx = (long)((double)ts * r.fps / 1e7 + 0.5);
        r.next = idx + 1;
        if (idx >= want) {
            IMFMediaBuffer* b = nullptr;
            if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
                BYTE* p = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(b->Lock(&p, nullptr, &len))) {
                    glBindTexture(GL_TEXTURE_2D, r.tex);
                    LONG stride = r.stride;
                    if (stride > 0 && (DWORD)stride * r.h <= len) {  // top-down rows
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.w, r.h, 0x80E1, GL_UNSIGNED_BYTE, p);
                    } else if (stride < 0 && (DWORD)(-stride) * r.h <= len) {  // bottom-up: upload row by row
                        for (u32 y = 0; y < r.h; y++)
                            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, r.w, 1, 0x80E1, GL_UNSIGNED_BYTE,
                                            p + (size_t)(r.h - 1 - y) * (size_t)(-stride));
                    }
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    b->Unlock();
                }
                b->Release();
            }
            r.have = idx;
        }
        s->Release();
    }
    return r.have == want || r.have >= 0;
}

Replacement* Find(const std::string& base) {
    static const bool on = Settings_GetInt("graphics.enhanced_assets") != 0;
    if (!on) return nullptr;
    auto it = g_repl.find(base);
    if (it == g_repl.end()) {
        Replacement& r = g_repl[base];
        std::string path = Settings_DataDir() + "\\cache\\enhanced\\" + base + ".mp4";
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES || !Open(r, path)) r.failed = true;
        it = g_repl.find(base);
    }
    return it->second.failed ? nullptr : &it->second;
}

}  // namespace

// tevbink.cpp (patches/movie-hook): the movie `name` is about to draw its current frame into the rectangle
// (x, y, w, h) in EFB pixels. Returns 1 when the replacement was drawn (skip the game's own draw).
extern "C" int port_movie_frame(const char* name, u32* bink, void* video, float x, float y, float w, float h) {
    if (!bink || !video) return 0;
    std::string base = BaseName(name);
    if (NC_ENV("NICECRASH_VIDEODUMP")) Dump(base, bink, (const VideoH*)video);
    Replacement* r = Find(base);
    if (!r || !bink[3]) return 0;
    if (!Seek(*r, (long)bink[3] - 1)) return 0;
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
