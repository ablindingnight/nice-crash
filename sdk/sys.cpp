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

extern "C" {

// SC: the console's settings (sc.h). Values: a US console, 4:3, stereo, no progressive, rumble on.
void SCInit(void) { sdk_log("SCInit"); }
s32 SCCheckStatus(void) { return 0; }  // SC_STATUS_READY
u8 SCGetAspectRatio(void) { return 0; }  // SC_ASPECT_RATIO_4x3
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
