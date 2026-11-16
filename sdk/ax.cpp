// Host SDK: AX audio. A software mixer for the game's voices: PCM16, PCM8 and DSP-ADPCM samples in the Wii's
// memory, linear sample-rate conversion, the volume envelope (VE) and the main L/R mix, at 32 kHz stereo. Voice
// state lives in the real VPB fields (Excite Truck's older AX: parameter block at VPB+0x138), so code that reads
// a voice's current address (VPB+0x1BC) sees it advance. The game's AX frame callback runs once per 96-sample
// frame (3 ms), paced by the retrace (sdk_ax_tick). Output goes through waveOut (winmm) from a ring buffer.
// Not emulated yet: the aux (reverb) sends, surround, the low-pass filter and the Wii Remote speaker
// (AXRmtGetSamples reports no samples). NICECRASH_WAV=<file>: also write the mix to a WAV file (first 60 s).
// NICECRASH_MUTE: no output device.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sdk.h"
#include "hostcfg.h"
bool sdk_vi_has_focus();

typedef void (*AXCallback)(void);
typedef void (*AXVoiceCallback)(void* vpb);

namespace {

const int kRate = 32000, kFrame = 96;
const int kVoices = 96;
const int kVpbSize = 0x400;  // larger than Excite Truck's AXVPB (0x138 + 0x120)
alignas(32) u8 g_vpb[kVoices][kVpbSize];
bool g_used[kVoices];
AXCallback g_frameCb;

// Offsets in the VPB (Excite Truck's AX; revolution/ax/AXVPB.h and AXPB.h under GAME_REXE01).
const int PB = 0x138;
const int PB_STATE = PB + 0x10, PB_TYPE = PB + 0x12, PB_MIX = PB + 0x14, PB_VE = PB + 0x74, PB_ADDR = PB + 0x78,
          PB_ADPCM = PB + 0x88, PB_SRC = PB + 0xB0, PB_ADPCMLOOP = PB + 0xBE;

inline u16& F16(u8* v, int off) { return *(u16*)(v + off); }
u32 Addr32(u8* v, int off) { return (u32)F16(v, off) << 16 | F16(v, off + 2); }
void SetAddr32(u8* v, int off, u32 a) {
    F16(v, off) = (u16)(a >> 16);
    F16(v, off + 2) = (u16)a;
}

// Host-side per-voice state that is not in the PB.
struct Mix {
    double frac;    // position between the two samples (0..1)
    s16 prev, cur;  // linear interpolation pair
    bool primed;
};
Mix g_mix[kVoices];

// PCM16 byte order in memory: little-endian by default (the host's CPU-built PCM, e.g. the game's MP3 decoder;
// sample banks stay in the disc's order because the port neutralises the game's byte-swap helpers).
// NICECRASH_PCMBE=1 reads big-endian, as the Wii DSP does.
bool g_pcmBE = getenv("NICECRASH_PCMBE") != nullptr;

int Index(void* vpb) { return (int)(((u8*)vpb - &g_vpb[0][0]) / kVpbSize); }

// Physical sample address (bytes) -> host pointer in the mapped Wii memory (MEM1 at 0x80000000, MEM2 at 0x90000000).
const u8* Mem(u32 byte) {
    u32 phys = byte & 0x1FFFFFFF;
    if (phys < 0x01800000) return (const u8*)(uintptr_t)(0x80000000u + phys);
    if (phys >= 0x10000000 && phys < 0x14000000) return (const u8*)(uintptr_t)(0x90000000u + (phys - 0x10000000));
    return nullptr;
}

// Decode the sample at the voice's current address, then advance (loop or stop at the end as the DSP does).
bool NextSample(u8* v, s16* out) {
    u16 format = F16(v, PB_ADDR + 2);
    u32 cur = Addr32(v, PB_ADDR + 0xC), end = Addr32(v, PB_ADDR + 8), loop = Addr32(v, PB_ADDR + 4);
    s32 s = 0;
    if (format == 0) {  // DSP-ADPCM: addresses in nibbles; each 16-nibble frame starts with a header byte
        if ((cur & 15) == 0) {
            const u8* h = Mem(cur >> 1);
            if (!h) return false;
            F16(v, PB_ADPCM + 0x22) = *h;  // pred_scale
            {
                static unsigned hdrs, bad;
                hdrs++;
                if ((*h >> 4) > 7 || (*h & 15) > 12) bad++;
                if (getenv("NICECRASH_AXLOG") && (hdrs & 0xFFFF) == 0)
                    fprintf(stderr, "[ax] adpcm headers %u, out of range %u\n", hdrs, bad);
            }
            cur += 2;
        }
        const u8* p = Mem(cur >> 1);
        if (!p) return false;
        int nib = (cur & 1) ? (*p & 15) : (*p >> 4);
        if (nib >= 8) nib -= 16;
        u16 ps = F16(v, PB_ADPCM + 0x22);
        int pred = (ps >> 4) & 7, scale = ps & 15;
        s32 c1 = (s16)F16(v, PB_ADPCM + pred * 4), c2 = (s16)F16(v, PB_ADPCM + pred * 4 + 2);
        s32 yn1 = (s16)F16(v, PB_ADPCM + 0x24), yn2 = (s16)F16(v, PB_ADPCM + 0x26);
        s32 x = ((nib << scale) << 11) + c1 * yn1 + c2 * yn2;
        x = (x + 1024) >> 11;
        if (x > 32767) x = 32767;
        if (x < -32768) x = -32768;
        F16(v, PB_ADPCM + 0x26) = (u16)yn1;
        F16(v, PB_ADPCM + 0x24) = (u16)x;
        s = x;
    } else if (format == 10) {  // PCM16, big-endian, addresses in samples
        const u8* p = Mem(cur * 2);
        if (!p) return false;
        s = g_pcmBE ? (s16)(p[0] << 8 | p[1]) : (s16)(p[1] << 8 | p[0]);
    } else if (format == 25) {  // PCM8, addresses in bytes
        const u8* p = Mem(cur);
        if (!p) return false;
        s = (s8)*p * 256;
    } else {
        return false;
    }
    *out = (s16)s;
    if (cur >= end) {
        if (F16(v, PB_ADDR)) {  // loopFlag
            cur = loop;
            if (format == 0 && F16(v, PB_TYPE) == 1) {  // streams restart the predictor from the loop context
                F16(v, PB_ADPCM + 0x22) = F16(v, PB_ADPCMLOOP);
                F16(v, PB_ADPCM + 0x24) = F16(v, PB_ADPCMLOOP + 2);
                F16(v, PB_ADPCM + 0x26) = F16(v, PB_ADPCMLOOP + 4);
            }
        } else {
            F16(v, PB_STATE) = 0;
            SetAddr32(v, PB_ADDR + 0xC, cur);
            return false;
        }
    } else {
        cur++;
    }
    SetAddr32(v, PB_ADDR + 0xC, cur);
    return true;
}

void MixFrame(s32* acc, int n) {
    for (int i = 0; i < kVoices; i++) {
        if (!g_used[i]) continue;
        u8* v = g_vpb[i];
        Mix& m = g_mix[i];
        if (F16(v, PB_STATE) != 1) {
            m.primed = false;
            continue;
        }
        double step = (double)Addr32(v, PB_SRC) / 65536.0;  // ratioHi:ratioLo (16.16)
        if (step <= 0) step = 1.0;
        double vol = F16(v, PB_VE) / 32768.0;  // 0x8000 = 1.0
        double vl = F16(v, PB_MIX) / 32768.0 * vol, vr = F16(v, PB_MIX + 4) / 32768.0 * vol;
        if (!m.primed) {
            static int logged = getenv("NICECRASH_AXLOG") ? 0 : 1000;
            if (logged < 40) {
                logged++;
                fprintf(stderr, "[ax] voice %d start: fmt %u type %u loop %u cur %08x end %08x loop %08x ratio %08x ve %04x mix %04x/%04x\n", i,
                        F16(v, PB_ADDR + 2), F16(v, PB_TYPE), F16(v, PB_ADDR), Addr32(v, PB_ADDR + 0xC), Addr32(v, PB_ADDR + 8),
                        Addr32(v, PB_ADDR + 4), Addr32(v, PB_SRC), F16(v, PB_VE), F16(v, PB_MIX), F16(v, PB_MIX + 4));
            }
            m.primed = true;
            m.frac = 0;
            if (!NextSample(v, &m.prev) || !NextSample(v, &m.cur)) {
                m.primed = false;
                continue;
            }
        }
        for (int k = 0; k < n; k++) {
            double s = m.prev + (m.cur - m.prev) * m.frac;
            acc[k * 2] += (s32)(s * vl);
            acc[k * 2 + 1] += (s32)(s * vr);
            m.frac += step;
            bool alive = true;
            while (m.frac >= 1.0 && alive) {
                m.frac -= 1.0;
                m.prev = m.cur;
                alive = NextSample(v, &m.cur);
            }
            if (!alive) {
                m.primed = false;
                break;
            }
        }
    }
}

// ---- output: ring buffer -> waveOut ----
const int kRing = kRate * 2;  // 2 s of stereo frames
s16 g_ring[kRing * 2];
long g_wr, g_rd;              // stereo frames, increasing
CRITICAL_SECTION g_cs;
HWAVEOUT g_wo;
const int kBuf = 1024, kNBuf = 4;
WAVEHDR g_hdr[kNBuf];
s16 g_bufs[kNBuf][kBuf * 2];
FILE* g_wav;
u32 g_wavFrames;

void Fill(WAVEHDR* h) {
    s16* d = (s16*)h->lpData;
    // audio.master (0-100), and silence while another window has the focus (audio.mute_unfocused, launched runs).
    const HostCfg& cfg = host_cfg();
    int gain = cfg.master * 256 / 100;
    if (cfg.muteUnfocused && !sdk_vi_has_focus()) gain = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < kBuf; i++) {
        if (g_rd < g_wr) {
            long r = g_rd % kRing;
            d[i * 2] = (s16)(g_ring[r * 2] * gain >> 8);
            d[i * 2 + 1] = (s16)(g_ring[r * 2 + 1] * gain >> 8);
            g_rd++;
        } else {
            d[i * 2] = d[i * 2 + 1] = 0;  // underrun: silence
        }
    }
    LeaveCriticalSection(&g_cs);
    h->dwFlags &= ~WHDR_DONE;
    waveOutWrite(g_wo, h, sizeof *h);
}

DWORD WINAPI Feeder(void*) {
    for (;;) {
        for (int i = 0; i < kNBuf; i++)
            if (g_hdr[i].dwFlags & WHDR_DONE) Fill(&g_hdr[i]);
        Sleep(4);
    }
}

void WavHeader(FILE* f, u32 frames) {
    u32 data = frames * 4, v;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    v = 36 + data;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    u16 fmt[2] = {1, 2};
    fwrite(fmt, 2, 2, f);
    v = kRate;
    fwrite(&v, 4, 1, f);
    v = kRate * 4;
    fwrite(&v, 4, 1, f);
    u16 ba[2] = {4, 16};
    fwrite(ba, 2, 2, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
}

void OpenOutput() {
    static bool done;
    if (done) return;
    done = true;
    InitializeCriticalSection(&g_cs);
    if (const char* w = getenv("NICECRASH_WAV")) {
        g_wav = fopen(w, "wb");
        if (g_wav) WavHeader(g_wav, 0);
    }
    if (getenv("NICECRASH_MUTE")) return;
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = kRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = kRate * 4;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        sdk_log("ax: no audio output device");
        g_wo = nullptr;
        return;
    }
    for (int i = 0; i < kNBuf; i++) {
        g_hdr[i].lpData = (LPSTR)g_bufs[i];
        g_hdr[i].dwBufferLength = sizeof g_bufs[i];
        waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof g_hdr[i]);
        g_hdr[i].dwFlags |= WHDR_DONE;
    }
    CreateThread(nullptr, 0, Feeder, nullptr, 0, nullptr);
}

void Push(const s32* acc, int n) {
    s16 out[kFrame * 2];
    for (int i = 0; i < n * 2; i++) out[i] = (s16)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
    if (g_wav && g_wavFrames < (u32)kRate * 60) {
        fwrite(out, 4, n, g_wav);
        g_wavFrames += n;
        if (g_wavFrames % (kRate / 2) < (u32)n) {  // keep the header current (runs are often killed)
            long pos = ftell(g_wav);
            WavHeader(g_wav, g_wavFrames);
            fseek(g_wav, pos, SEEK_SET);
            fflush(g_wav);
        }
    }
    if (!g_wo) return;
    EnterCriticalSection(&g_cs);
    if (g_wr - g_rd > kRing - n) g_rd = g_wr - (kRing - n);  // overrun: drop the oldest
    for (int i = 0; i < n; i++) {
        long w = (g_wr + i) % kRing;
        g_ring[w * 2] = out[i * 2];
        g_ring[w * 2 + 1] = out[i * 2 + 1];
    }
    g_wr += n;
    LeaveCriticalSection(&g_cs);
}

double g_produced;  // frames mixed so far
u32 g_ticks;

}  // namespace

// Called on every emulated retrace (59.94 Hz): run AX frames until the mix has caught up with that much time.
void sdk_ax_tick() {
    OpenOutput();
    g_ticks++;
    double target = g_ticks * (double)kRate / 59.94;
    while (g_produced + kFrame <= target) {
        if (g_frameCb) g_frameCb();
        s32 acc[kFrame * 2] = {};
        MixFrame(acc, kFrame);
        Push(acc, kFrame);
        g_produced += kFrame;
    }
}

extern "C" {

void AIInit(void* stack) {}
void AIReset(void) {}
void AXInit(void) { sdk_log("AXInit"); }
void AXQuit(void) {}
void AXSetMode(u32 mode) {}
AXCallback AXRegisterCallback(AXCallback cb) {
    AXCallback o = g_frameCb;
    g_frameCb = cb;
    return o;
}

void* AXAcquireVoice(u32 prio, AXVoiceCallback cb, u32 context) {
    for (int i = 0; i < kVoices; i++) {
        if (!g_used[i]) {
            g_used[i] = true;
            memset(g_vpb[i], 0, kVpbSize);
            memset(&g_mix[i], 0, sizeof g_mix[i]);
            *(u32*)(g_vpb[i] + 0x0C) = prio;     // priority
            *(void**)(g_vpb[i] + 0x10) = (void*)cb;
            *(u32*)(g_vpb[i] + 0x14) = context;  // userContext
            *(u32*)(g_vpb[i] + 0x18) = i;        // index
            SetAddr32(g_vpb[i], PB_SRC, 0x10000);  // ratio 1.0
            return g_vpb[i];
        }
    }
    return nullptr;
}

void AXFreeVoice(void* vpb) {
    int i = Index(vpb);
    if (i >= 0 && i < kVoices) {
        g_used[i] = false;
        F16(g_vpb[i], PB_STATE) = 0;
    }
}

void AXSetVoiceAddr(void* vpb, const void* addr) {
    memcpy((u8*)vpb + PB_ADDR, addr, 0x10);
    int i = Index(vpb);
    if (i >= 0 && i < kVoices) g_mix[i].primed = false;
}
void AXSetVoiceAdpcm(void* vpb, const void* adpcm) { memcpy((u8*)vpb + PB_ADPCM, adpcm, 0x28); }
void AXSetVoiceAdpcmLoop(void* vpb, const void* loop) { memcpy((u8*)vpb + PB_ADPCMLOOP, loop, 6); }
void AXSetVoiceMix(void* vpb, const void* mix) { memcpy((u8*)vpb + PB_MIX, mix, 0x30); }
void AXSetVoiceRmtMix(void*, const void*) {}
void AXSetVoiceRmtOn(void*, u16) {}
void AXSetVoiceSrc(void* vpb, const void* src) { memcpy((u8*)vpb + PB_SRC, src, 0xE); }
void AXSetVoiceSrcRatio(void* vpb, f32 ratio) { SetAddr32((u8*)vpb, PB_SRC, (u32)(ratio * 65536.0f)); }
void AXSetVoiceSrcType(void*, u32) {}
void AXSetVoiceState(void* vpb, u16 state) { F16((u8*)vpb, PB_STATE) = state; }
void AXSetVoiceType(void* vpb, u16 type) { F16((u8*)vpb, PB_TYPE) = type; }
void AXSetVoiceVe(void* vpb, const void* ve) { memcpy((u8*)vpb + PB_VE, ve, 4); }
s32 AXRmtGetSamples(s32, void*, s32) { return 0; }
s32 AXRmtAdvancePtr(s32, s32) { return 0; }

}  // extern "C"
