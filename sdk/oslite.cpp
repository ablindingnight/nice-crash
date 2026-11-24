// SDK functions a normal run reaches that have nothing to do on a PC (hardware registers, interrupts, the
// Wii's NAND font and power button, the SD card's file system, the remote's speaker), defined so a player's run
// does not stop at the "not written yet" stubs. Each returns what the auto-generated stub returned when the game
// was brought up with NICECRASH_UNIMPL=continue (0), except OSTicksToCalendarTime, which gives the PC's local time.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "sdk.h"

extern "C" {

// ---- OS ----
struct OSCalendarTime {
    s32 sec, min, hour, mday, month, year, wday, yday, msec, usec;  // month 0-11, yday 0-365
};
// The game asks for "now" (save dates, the HOME menu clock): the PC's local time, whatever the tick value.
void OSTicksToCalendarTime(s64 ticks, OSCalendarTime* t) {
    (void)ticks;
    if (!t) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const int kDays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (st.wYear % 4 == 0 && st.wYear % 100 != 0) || st.wYear % 400 == 0;
    t->sec = st.wSecond;
    t->min = st.wMinute;
    t->hour = st.wHour;
    t->mday = st.wDay;
    t->month = st.wMonth - 1;
    t->year = st.wYear;
    t->wday = st.wDayOfWeek;
    t->yday = kDays[st.wMonth - 1] + st.wDay - 1 + (leap && st.wMonth > 2 ? 1 : 0);
    t->msec = st.wMilliseconds;
    t->usec = 0;
}
u32 OSGetConsoleType(void) { return 0; }
void OSRegisterVersion(const char* id) { (void)id; }
void OSRegisterShutdownFunction(void* info) { (void)info; }
void* OSSetPowerCallback(void* cb) { (void)cb; return nullptr; }
u32 OSGetResetButtonState(void) { return 0; }
// Alarms: the game's periodic alarms are not needed on the host (retrace and audio run on their own timers).
void OSCreateAlarm(void* alarm) { (void)alarm; }
void OSSetPeriodicAlarm(void* alarm, s64 start, s64 period, void* handler) {
    (void)alarm; (void)start; (void)period; (void)handler;
}
void OSInitThreadQueue(void* queue) { (void)queue; }
void* OSGetCurrentThread(void) { return nullptr; }
// The NAND system font: the game's own fonts are used instead.
u32 OSInitFont(void* header) { (void)header; return 0; }
u16 OSGetFontEncode(void) { return 0; }
void* OSGetFontTexture(const char* str, void** image, s32* x, s32* y, s32* width) {
    (void)str; (void)image; (void)x; (void)y; (void)width;
    return nullptr;
}
// Interrupts.
void* __OSSetInterruptHandler(s16 irq, void* handler) { (void)irq; (void)handler; return nullptr; }
u32 __OSUnmaskInterrupts(u32 mask) { (void)mask; return 0; }

// ---- PowerPC special registers ----
u32 PPCMfhid2(void) { return 0; }
void PPCMthid2(u32 v) { (void)v; }
u32 PPCMfwpar(void) { return 0; }
void PPCMtwpar(u32 v) { (void)v; }
void PPCSync(void) {}

// ---- GX ----
u32 GXGetOverflowCount(void) { return 0; }

// ---- VF: the SD card's file system is the music folder (sdk/vfmusic.cpp) ----

// ---- WPAD: the remote's speaker is not emulated ----
s32 WPADControlSpeaker(s32 chan, u32 command, void* cb) { (void)chan; (void)command; (void)cb; return 0; }

}  // extern "C"
