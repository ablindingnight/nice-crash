// Nice Crash launcher: the work behind the screens. Dump inspection and search, the first-run checklist (drives
// build/import.exe and the game build), starting NiceCrashGame.exe, run history, logs and bug reports.
// Everything here reads the player's dump; nothing writes to it or copies it.
#pragma once
#include <string>
#include <vector>

namespace tasks {

std::string ExeDir();       // folder of NiceCrash.exe, with a trailing backslash
std::string RepoRoot();     // the source checkout (has tools/gametrial/link.py), or "" in a player install
std::string GameExe();      // ExeDir() + "NiceCrashGame.exe" (NICECRASH_GAME_EXE overrides, for debugging)
std::string LogDir();       // %LOCALAPPDATA%\NiceCrash\logs (created)
std::string LogPath();      // LogDir() + "\nicecrash_log.txt"

std::wstring FormatBytes(long long n);
long long FreeBytes(const std::string& path);   // free space for this user on path's drive, -1 if unknown
bool ShortcutExists(int where);                 // 0 = desktop, 1 = Start menu
bool CreateShortcut(int where);
std::string ShortHash(const std::string& sha1);   // "e516…672b"

struct DumpInfo {
    std::string path;          // as chosen (the dump root)
    std::string files, dol;    // located files\ and main.dol
    std::string sha1, gameId;
    bool found = false;        // files\ with tocres.res and a readable main.dol
    bool verified = false;     // main.dol SHA-1 is the retail USA one
    std::string why;           // what's wrong, for the player
    long long bytes = -1;      // size of the dump folder, if measured
};
DumpInfo InspectDump(const std::string& path, bool measure);  // a folder or a disc image (extracted once, cached)
bool IsDiscImage(const std::string& path);                     // .iso .rvz .wia .wbfs .ciso .gcz .nfs
std::string DiscCacheDir();                                    // <game data folder>\disc
// The game data folder (launcher.data_dir, default %USERPROFILE%\Games\Nice Crash): disc, cache, textures, dump.
bool ChangeDataDir(const std::string& to, std::string& err);  // moves what is there, then switches
void MigrateLegacyData();                                       // once: from the settings folder (older builds)

// The dump the launcher uses: launcher.dump_path, else dump.ini's disc. Re-checked on demand.
struct State {
    DumpInfo dump;
    bool haveDump = false;     // a path is configured
    bool imported = false;     // dump.ini exists and points at this dump
    bool built = false;
    std::string builtWhen;     // the game exe's date
    std::string decompRef;     // pinned decomp commit, if the source is here
};
State Refresh();

// Disc search: Downloads, Desktop, Documents, the last-used folder.
struct Candidate {
    DumpInfo info;
    int kind = 0;              // 0 ready, 1 not supported (other region/build), 2 incomplete
    std::wstring title, why;
};
Candidate JudgeDump(const std::string& path);  // inspect a folder the player chose
void StartDiscScan();
bool DiscScanRunning();
std::vector<Candidate> DiscScanResults();
std::wstring DiscScanSummary();

// The first-run checklist (design 3.3). Runs on a worker thread; the UI polls Snapshot().
enum StepState { PENDING, ACTIVE, DONE, FAILED, SKIPPED };
struct Step {
    std::wstring title, detail;
    StepState state = PENDING;
    double t0 = 0, t1 = 0;     // seconds (GetTickCount64 / 1000)
    float progress = -1;       // 0..1 for the mini bar, -1 = none
};
struct Checklist {
    std::wstring title, sub;
    std::vector<Step> steps;
    std::vector<std::string> log;
    bool running = false, finished = false, failed = false, cancelled = false;
    int kind = 0;              // 0 first run / rebuild, 1 "Enhance my game"
};
void StartChecklist(const std::string& dumpPath, bool rebuild);

// "Enhance my game" (handoff ENHANCE_MY_GAME): the intro video's frames are read from the player's own movie by
// the game (NICECRASH_VIDEODUMP), scaled (an AI upscaler when one is installed, else Windows' high-quality filter)
// and written to EnhancedDir()\uisingle.mp4 by enhance.exe; the game plays it when graphics.enhanced_assets is on.
// Runs as a checklist (kind 1) on the same screen as the first run.
void StartEnhance();
std::string EnhancedDir();          // %LOCALAPPDATA%\NiceCrash\cache\enhanced
bool HaveEnhancedVideo();           // the enhanced intro exists
long long EnhancedBytes();          // its size on disk, -1 if none
bool RemoveEnhanced();              // deletes the enhanced files and turns the setting off
void CancelChecklist();
Checklist Snapshot();

// The game.
bool StartGame(std::string& err);   // with the current settings; false + err if it couldn't start
bool GameRunning();
void StopGame();                    // ends the game process (debugging; the player quits from the game)
void SetGameExitWindow(void* hwnd, unsigned msg);  // posted when the game exits

struct Run {
    std::string when;          // local time, "2026-11-15 20:31"
    int seconds = 0;
    unsigned code = 0;
    std::string status;        // "OK", "Crashed", "Stopped"
    bool logged = false;
};
std::vector<Run> Runs();       // newest first, at most 10
bool LastRunFailed(Run* run);
std::wstring DescribeExit(unsigned code);

void LogLine(const char* fmt, ...);   // the launcher's errors and warnings, when "Save error logs" is on
std::string BugReport();              // log + settings + build info, with the user's home path and name removed
std::string Scrub(std::string text);  // removes the user's home path and name
// Writes a report folder (report.txt, the logs, settings.ini, all scrubbed) under %LOCALAPPDATA%\NiceCrash\reports;
// returns its path, or "" on failure. Nothing is sent anywhere.
std::string SaveReport();
std::string RunLogPath(int runIndex);  // the log file of Runs()[runIndex], or "" if it wasn't kept / rotated away
long long BuildDiskUse();              // bytes in the build folder; -1 while measuring
void ForgetDiskUse();                  // measure again next time (after a build)
bool CopyToClipboard(void* hwnd, const std::string& text);
void OpenPath(const std::string& path);   // Explorer / the default app

}  // namespace tasks
