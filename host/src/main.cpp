// NiceCrash.exe: the Nice Crash launcher (host/src/launcher). With no arguments it opens the launcher window;
// the phase-0 texture viewer is still here for debugging (viewer.cpp):
//
//   NiceCrash                                   the launcher
//   NiceCrash --ui-shot <screen> <out.bmp> [w h]  render a launcher screen to a .bmp (launcher.h lists them)
//   NiceCrash --setup <disc> [--rebuild]         the first-run checklist (import + build) without the window
//   NiceCrash --launch-game [seconds]            start the game as PLAY does, end it after N s, print the run
//   NiceCrash --viewer [--disc ...] [--tex ...]   the phase-0 viewer; --disc, --tex, --dol, --scan and
//                                               --screenshot also select it
//
// Built for the Windows subsystem, so the launcher opens without a console. The viewer and --ui-shot attach to the
// console they were started from, for their printed output.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "launcher/launcher.h"

int ViewerMain(int argc, char** argv);

static void AttachParentConsole() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;  // already redirected
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--ui-shot") {
        AttachParentConsole();
        int w = argc >= 6 ? atoi(argv[4]) : 1280, h = argc >= 6 ? atoi(argv[5]) : 720;
        return LauncherShot(argv[2], argv[3], w, h);
    }
    if (argc >= 2 && std::string(argv[1]) == "--enhance") {
        AttachParentConsole();
        int LauncherEnhanceTest();
        return LauncherEnhanceTest();
    }
    if (argc >= 3 && std::string(argv[1]) == "--judge") {  // what the launcher makes of a folder or disc image
        AttachParentConsole();
        int LauncherJudgeTest(const char* path);
        return LauncherJudgeTest(argv[2]);
    }
    if (argc >= 3 && std::string(argv[1]) == "--setup") {  // the first-run checklist without the window
        AttachParentConsole();
        int LauncherSetupTest(const char* path, bool rebuild);
        return LauncherSetupTest(argv[2], argc >= 4 && std::string(argv[3]) == "--rebuild");
    }
    if (argc >= 2 && std::string(argv[1]) == "--launch-game") {
        AttachParentConsole();
        return LauncherLaunchTest(argc >= 3 ? atoi(argv[2]) : 20);
    }
    if (argc > 1) {
        AttachParentConsole();
        return ViewerMain(argc, argv);
    }
    return LauncherMain();
}
