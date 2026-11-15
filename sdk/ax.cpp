// Host SDK: AX/AI audio, silent for now. Voices are real objects from a pool (the game keeps pointers and reads
// fields such as the current address at VPB+0x1BC), settings are accepted and ignored, and the AX frame callback
// runs on the retrace (sdk_ax_tick) so the game's sound engine keeps advancing. Mixing to WASAPI comes later
// (the game's own MP3 decoder already runs natively; see mp3test/).
#include <cstring>
#include "sdk.h"

typedef void (*AXCallback)(void);
typedef void (*AXVoiceCallback)(void* vpb);

static const int kVoices = 96;
static const int kVpbSize = 0x400;  // larger than the SDK's AXVPB (+0x1BC is read by the game)
alignas(32) static u8 g_vpb[kVoices][kVpbSize];
static bool g_used[kVoices];
static AXCallback g_frameCb;

void sdk_ax_tick() {
    // The DSP produces a 5 ms (3 ms on some modes) audio frame; three or so per video frame. Run the callback
    // the same number of times so time-based sound code sees the right cadence.
    if (g_frameCb)
        for (int i = 0; i < 3; i++) g_frameCb();
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
            *(u32*)(g_vpb[i] + 0x0C) = prio;     // priority
            *(u32*)(g_vpb[i] + 0x14) = context;  // userContext
            *(u32*)(g_vpb[i] + 0x18) = i;        // index
            return g_vpb[i];
        }
    }
    return nullptr;
}

void AXFreeVoice(void* vpb) {
    int i = (int)(((u8*)vpb - &g_vpb[0][0]) / kVpbSize);
    if (i >= 0 && i < kVoices) g_used[i] = false;
}

void AXSetVoiceAddr(void*, const void*) {}
void AXSetVoiceAdpcm(void*, const void*) {}
void AXSetVoiceAdpcmLoop(void*, const void*) {}
void AXSetVoiceMix(void*, const void*) {}
void AXSetVoiceRmtMix(void*, const void*) {}
void AXSetVoiceRmtOn(void*, u16) {}
void AXSetVoiceSrc(void*, const void*) {}
void AXSetVoiceSrcRatio(void*, f32) {}
void AXSetVoiceSrcType(void*, u32) {}
void AXSetVoiceState(void*, u16) {}
void AXSetVoiceType(void*, u16) {}
void AXSetVoiceVe(void*, const void*) {}
s32 AXRmtGetSamples(s32, void*, s32) { return 0; }
s32 AXRmtAdvancePtr(s32, s32) { return 0; }

}  // extern "C"
