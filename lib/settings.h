// Nice Crash settings: one file, %LOCALAPPDATA%\<PROJECT_DIRNAME>\settings.ini, shared by the launcher
// (NiceCrash.exe), the game (NiceCrashGame.exe) and the importer. Design: handoff NICE_CRASH_UI_DESIGN.md,
// section 5 (every key, its default and whether it needs a restart).
//
//   [display]
//   aspect = 4:3
//
// Keys are "section.key" ("display.aspect"). Only values the player changed are stored, so a default that moves
// in a later release (launcher.error_log) reaches everyone who never touched it. Comments, unknown sections and
// unknown keys are kept when the file is rewritten. Writes are atomic (temp file + rename) and debounced 300 ms.
//
// Environment variables win over the file, for debugging: NICECRASH_<SECTION>_<KEY> (NICECRASH_DISPLAY_ASPECT),
// plus the older names NICECRASH_ASPECT (display.aspect) and NICECRASH_ERROR_LOG (launcher.error_log).
// NICECRASH_SETTINGS_FILE points at another file (tests, debugging).
//
// Plain C-style API so the 32-bit game, the 64-bit launcher and tools can all link lib/settings.cpp.
#pragma once
#include <string>
#include <vector>

// Value as the program should use it: env override, else the file, else the default. Unknown keys give "".
// The pointer stays valid until the next Settings_Get on the same thread.
const char* Settings_Get(const char* key);
int Settings_GetInt(const char* key);
float Settings_GetFloat(const char* key);
// Writes the file (debounced 300 ms) and fires the change callbacks. Setting the default value removes the
// key from the file (the player is back on the default).
void Settings_Set(const char* key, const char* value);
void Settings_SetInt(const char* key, int value);
void Settings_Reset(const char* key);              // back to the default
void Settings_ResetSection(const char* section);   // every known key in "display", "audio", ...
// cb(key) runs on the setting thread after each change whose key starts with key_prefix ("" = every key).
void Settings_OnChange(const char* key_prefix, void (*cb)(const char* key));
bool Settings_NeedsRestart(const char* key);       // the game reads it only at start (display.aspect, ...)
const char* Settings_Default(const char* key);
bool Settings_IsKnown(const char* key);
// The name of the environment variable overriding this key right now, or nullptr.
const char* Settings_OverriddenBy(const char* key);
bool Settings_IsSetInFile(const char* key);
void Settings_Flush(void);                         // write now if a debounced write is pending
void Settings_Reload(void);                        // re-read the file (another program may have changed it)
const char* Settings_Path(void);
// launcher.data_dir expanded and created (default %USERPROFILE%\Games\Nice Crash): the extracted disc, enhanced
// files, texture packs and dumps. Falls back to the settings folder when it can't be created.
std::string Settings_DataDir(void);
// `rel` ("\cache\enhanced\x.mp4", "\textures\REXE01") inside the data folder, or, when it is only in the
// settings folder (data from before launcher.data_dir; the launcher moves it there at its next start), that path.
std::string Settings_FindData(const std::string& rel);

// C++ helpers.
namespace settings {
std::string Get(const char* key);
std::vector<std::string> Keys();                    // every known key, in the design's order
std::string FileText();                             // the file as it would be written now (for bug reports)
}  // namespace settings
