// Host SDK: system settings (SC), NAND, VI and DVD init stubs with retail defaults. Real implementations replace
// these as the boot needs them (DVD next: files from the player's disc through lib/).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include "sdk.h"

void sdk_log(const char* fmt, ...) {
    static int on = -1;
    if (on < 0) on = getenv("NICECRASH_SDKLOG") != nullptr;
    if (!on) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sdk] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

bool sdk_widescreen() {
    static int w = -1;
    if (w < 0) {
        const char* a = getenv("NICECRASH_ASPECT");
        w = !(a && a[0] == '4');
    }
    return w != 0;
}

extern "C" {

// SC: the console's settings (sc.h). Values: a US console, 16:9 (NICECRASH_ASPECT=4:3 for 4:3), stereo, no
// progressive, rumble on.
void SCInit(void) { sdk_log("SCInit"); }
s32 SCCheckStatus(void) { return 0; }  // SC_STATUS_READY
u8 SCGetAspectRatio(void) { return sdk_widescreen() ? 1 : 0; }  // SC_ASPECT_RATIO_16x9 / 4x3
u8 SCGetSoundMode(void) { return 1; }  // SC_SOUND_MODE_STEREO
u8 SCGetProgressiveMode(void) { return 0; }
u8 SCGetEuRgb60Mode(void) { return 0; }
u8 SCGetLanguage(void) { return 1; }  // SC_LANG_ENGLISH
u8 SCGetScreenSaverMode(void) { return 1; }
u8 SCGetWpadMotorMode(void) { return 1; }
u8 SCGetWpadSpeakerVolume(void) { return 0x58; }
u8 SCGetWpadSensorBarPosition(void) { return 0; }

// NAND: saves (a per-user folder later).
s32 NANDInit(void) {
    sdk_log("NANDInit");
    return 0;  // NAND_RESULT_OK
}

// VI / DVD init: nothing to set up yet.
void DVDInit(void) { sdk_log("DVDInit"); }

}  // extern "C"
