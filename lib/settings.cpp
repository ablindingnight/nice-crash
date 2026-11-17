// settings.ini: see settings.h. Windows only (CreateThread, MoveFileEx); no third-party code.
#define WIN32_LEAN_AND_MEAN
#include "settings.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "portconfig.h"
#include "project.h"

namespace {

enum { kRestart = 1 };

struct Def {
    const char* key;
    const char* def;  // nullptr = computed (see DefaultOf)
    unsigned flags;
    const char* alias;  // an older environment variable name, or nullptr
};

// Design section 5, in its order. Hidden rows (bloom, music, sfx) are known keys so their values survive.
const Def kDefs[] = {
    {"display.window_mode", "borderless", 0, nullptr},
    {"display.resolution", "desktop", 0, nullptr},
    {"display.aspect", "16:9", kRestart, "NICECRASH_ASPECT"},
    {"display.window_width", "1280", 0, nullptr},
    {"display.window_height", "720", 0, nullptr},
    {"display.window_x", "", 0, nullptr},
    {"display.window_y", "", 0, nullptr},
    {"display.vsync", "1", 0, nullptr},
    {"display.frame_rate", "59.94", 0, nullptr},
    {"display.monitor", "0", kRestart, nullptr},
    {"display.background_animation", "auto", 0, nullptr},  // launcher Home: auto | on | off
    {"graphics.internal_scale", "window", 0, nullptr},
    {"graphics.msaa", "0", kRestart, nullptr},  // 0 | 2 | 4 | 8; the driver crash at the Tutorial loading is fixed (2026-11-17, sdk/xfb.cpp xfb_resolve)
    {"graphics.texture_filter", "game", 0, nullptr},
    {"graphics.bloom", "1", 0, nullptr},
    {"graphics.show_fps", "0", 0, nullptr},
    {"graphics.renderer", "gl", kRestart, nullptr},
    // "Enhance my game" (handoff/port_refs/ENHANCE_MY_GAME_2026-11-15.md, a later milestone): reserved so the launcher
    // and the in-game menu share them from the start. Nothing reads them yet.
    {"graphics.enhanced_assets", "0", kRestart, nullptr},      // use the player's locally enhanced files when present
    {"graphics.upscale_model", "realesrgan", 0, nullptr}, // realesrgan (x4plus, best) | fast (animevideov3)
    {"graphics.upscale_scale", "2", 0, nullptr},         // 2 | 4
    {"audio.master", "80", 0, nullptr},
    {"audio.music", "100", 0, nullptr},
    {"audio.sfx", "100", 0, nullptr},
    {"audio.remote_speaker", "main", 0, nullptr},
    {"audio.mute_unfocused", "1", 0, nullptr},
    {"audio.device", "default", kRestart, nullptr},
    {"controls.device", "auto", 0, nullptr},
    {"controls.steer_source", "stick", 0, nullptr},
    {"controls.tilt_sensitivity", "100", 0, nullptr},
    {"controls.deadzone", "10", 0, nullptr},
    {"controls.steer_curve", "smooth", 0, nullptr},
    {"controls.invert_steer", "0", 0, nullptr},
    {"controls.rumble", "1", 0, nullptr},
    {"game.skip_intro", "0", kRestart, nullptr},
    {"game.language", "en", kRestart, nullptr},
    {"game.pause_unfocused", "1", 0, nullptr},
    {"game.screenshot_key", "F12", 0, nullptr},
    {"game.menu_on_tab", "1", 0, nullptr},
    {"launcher.dump_path", "", 0, nullptr},
    // Where the bulky game data lives (the extracted disc, enhanced files, texture packs and dumps); settings, logs
    // and saves stay in %LOCALAPPDATA%\NiceCrash. Environment variables are expanded.
    {"launcher.data_dir", "%USERPROFILE%\\Games\\Nice Crash", kRestart, nullptr},
    {"launcher.close_on_play", "1", 0, nullptr},
    {"launcher.check_updates", "1", 0, nullptr},
    {"launcher.channel", "stable", 0, nullptr},
    {"launcher.update_url", "", 0, nullptr},
    {"launcher.error_log", nullptr, 0, "NICECRASH_ERROR_LOG"},
    {"launcher.last_seen_version", "", 0, nullptr},
};

const Def* FindDef(const char* key) {
    for (auto& d : kDefs)
        if (!strcmp(d.key, key)) return &d;
    return nullptr;
}

const char* DefaultOf(const Def* d) {
    if (!d) return "";
    if (d->def) return d->def;
    if (!strcmp(d->key, "launcher.error_log")) return NICECRASH_KNOWN_WORKING_RELEASE ? "0" : "1";
    return "";
}

// The file as lines, so comments and unknown keys survive a rewrite.
struct Line {
    std::string section;  // the section this line belongs to ("" before the first header)
    std::string key;      // empty for comments, blanks and headers
    std::string value;
    std::string raw;      // used for everything that isn't a key line
    bool header = false;
};

struct Callback {
    std::string prefix;
    void (*cb)(const char*);
};

CRITICAL_SECTION g_lock;
bool g_init = false, g_loaded = false, g_dirty = false;
std::vector<Line> g_lines;
std::vector<Callback> g_cbs;
std::string g_path;
HANDLE g_wake = nullptr;
DWORD g_lastChange = 0;

struct Lock {
    Lock() { EnterCriticalSection(&g_lock); }
    ~Lock() { LeaveCriticalSection(&g_lock); }
};

void Init() {
    static bool once = (InitializeCriticalSection(&g_lock), true);  // thread-safe static init
    g_init = once;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

void Split(const char* key, std::string& sec, std::string& name) {
    const char* dot = strchr(key, '.');
    if (!dot) { sec.clear(); name = key; return; }
    sec.assign(key, dot - key);
    name = dot + 1;
}

void LoadLocked() {
    g_lines.clear();
    if (g_path.empty()) {
        const char* e = getenv("NICECRASH_SETTINGS_FILE");  // tests and debugging: another file
        g_path = e && *e ? e : portcfg::Dir() + "\\settings.ini";
    }
    g_loaded = true;
    FILE* f = fopen(g_path.c_str(), "rb");
    if (!f) return;
    std::string sec;
    char buf[4096];
    bool first = true;
    while (fgets(buf, sizeof buf, f)) {
        std::string s = buf;
        if (first && s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF)
            s = s.substr(3);  // UTF-8 BOM from Notepad
        first = false;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        Line l;
        std::string t = Trim(s);
        if (!t.empty() && t[0] == '[' && t.back() == ']') {
            sec = Trim(t.substr(1, t.size() - 2));
            l.header = true;
            l.section = sec;
            l.raw = s;
        } else if (!t.empty() && t[0] != '#' && t[0] != ';' && t.find('=') != std::string::npos) {
            size_t eq = t.find('=');
            l.section = sec;
            l.key = Trim(t.substr(0, eq));
            l.value = Trim(t.substr(eq + 1));
            l.raw = s;
        } else {
            l.section = sec;
            l.raw = s;
        }
        g_lines.push_back(l);
    }
    fclose(f);
}

void EnsureLoaded() {
    Init();
    Lock lk;
    if (!g_loaded) LoadLocked();
}

Line* FindLine(const std::string& sec, const std::string& name) {
    for (auto& l : g_lines)
        if (!l.header && !l.key.empty() && l.section == sec && _stricmp(l.key.c_str(), name.c_str()) == 0) return &l;
    return nullptr;
}

std::string TextLocked() {
    std::string out;
    bool any = false;
    for (auto& l : g_lines) {
        if (l.header) out += "[" + l.section + "]\n";
        else if (!l.key.empty()) out += l.key + " = " + l.value + "\n";
        else out += l.raw + "\n";
        any = true;
    }
    if (!any) out = "";
    return out;
}

bool WriteLocked() {
    std::string text = TextLocked();
    if (text.empty())
        text = std::string("# ") + PROJECT_NAME +
               " settings. Only values you changed are listed; delete a line to go back to its default.\n";
    std::string tmp = g_path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (!ok) { DeleteFileA(tmp.c_str()); return false; }
    if (!MoveFileExA(tmp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    g_dirty = false;
    return true;
}

DWORD WINAPI Writer(void*) {
    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        for (;;) {  // wait until 300 ms have passed since the last change
            DWORD since;
            {
                Lock lk;
                if (!g_dirty) break;
                since = GetTickCount() - g_lastChange;
                if (since >= 300) { WriteLocked(); break; }
            }
            Sleep(300 - since);
        }
    }
    return 0;
}

void ScheduleWrite() {  // with the lock held
    g_dirty = true;
    g_lastChange = GetTickCount();
    if (!g_wake) {
        g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    SetEvent(g_wake);
}

std::string EnvName(const char* key) {
    std::string n = "NICECRASH_";
    for (const char* p = key; *p; p++) n += *p == '.' ? '_' : (char)toupper((unsigned char)*p);
    return n;
}

thread_local std::string t_envName;

const char* OverrideOf(const char* key, const char** value) {
    t_envName = EnvName(key);
    if (const char* v = getenv(t_envName.c_str())) { *value = v; return t_envName.c_str(); }
    const Def* d = FindDef(key);
    if (d && d->alias) {
        if (const char* v = getenv(d->alias)) {
            *value = v;
            return d->alias;
        }
    }
    return nullptr;
}

void Fire(const std::vector<std::string>& keys) {
    std::vector<Callback> cbs;
    {
        Lock lk;
        cbs = g_cbs;
    }
    for (auto& k : keys)
        for (auto& c : cbs)
            if (!strncmp(k.c_str(), c.prefix.c_str(), c.prefix.size())) c.cb(k.c_str());
}

// Remove the key from the file (back on the default). Returns whether it was there.
bool EraseLocked(const std::string& sec, const std::string& name) {
    for (size_t i = 0; i < g_lines.size(); i++) {
        Line& l = g_lines[i];
        if (!l.header && !l.key.empty() && l.section == sec && _stricmp(l.key.c_str(), name.c_str()) == 0) {
            g_lines.erase(g_lines.begin() + i);
            return true;
        }
    }
    return false;
}

}  // namespace

std::string Settings_DataDir(void) {
    char out[MAX_PATH * 2] = {};
    std::string v = Settings_Get("launcher.data_dir");
    if (v.empty()) v = Settings_Default("launcher.data_dir");
    if (!ExpandEnvironmentStringsA(v.c_str(), out, sizeof out) || strchr(out, '%')) return portcfg::Dir();
    std::string d = out;
    while (d.size() > 3 && (d.back() == '\\' || d.back() == '/')) d.pop_back();
    // Create it, parent by parent (the default's Games folder may not exist yet).
    for (size_t i = 3; i <= d.size(); i++)
        if (i == d.size() || d[i] == '\\' || d[i] == '/') CreateDirectoryA(d.substr(0, i).c_str(), nullptr);
    DWORD a = GetFileAttributesA(d.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? d : portcfg::Dir();
}

const char* Settings_Path(void) {
    EnsureLoaded();
    static thread_local std::string p;
    Lock lk;
    p = g_path;
    return p.c_str();
}

std::string settings::Get(const char* key) {
    EnsureLoaded();
    const char* ov = nullptr;
    if (OverrideOf(key, &ov)) return ov;
    std::string sec, name;
    Split(key, sec, name);
    {
        Lock lk;
        if (Line* l = FindLine(sec, name)) return l->value;
    }
    return DefaultOf(FindDef(key));
}

const char* Settings_Get(const char* key) {
    static thread_local std::string ring[4];
    static thread_local int at = 0;
    std::string& s = ring[at++ & 3];  // a few calls in one expression stay valid
    s = settings::Get(key);
    return s.c_str();
}

int Settings_GetInt(const char* key) { return atoi(settings::Get(key).c_str()); }
float Settings_GetFloat(const char* key) { return (float)atof(settings::Get(key).c_str()); }

void Settings_Set(const char* key, const char* value) {
    EnsureLoaded();
    std::string sec, name, v = Trim(value ? value : "");
    Split(key, sec, name);
    bool changed = false;
    {
        Lock lk;
        const Def* d = FindDef(key);
        Line* l = FindLine(sec, name);
        if (d && v == DefaultOf(d)) {
            changed = EraseLocked(sec, name);
        } else if (l) {
            changed = l->value != v;
            l->value = v;
        } else {
            // Append to the end of the section, creating it if needed.
            size_t at = g_lines.size();
            bool found = false;
            for (size_t i = 0; i < g_lines.size(); i++) {
                if (g_lines[i].header && g_lines[i].section == sec) found = true;
                if (found && g_lines[i].section == sec) {
                    at = i + 1;
                    // keep blank lines and comments after the section's last key with the next section
                    if (!g_lines[i].header && g_lines[i].key.empty() && Trim(g_lines[i].raw).empty()) at = i;
                }
                if (found && g_lines[i].section != sec) break;
            }
            Line nl;
            nl.section = sec;
            nl.key = name;
            nl.value = v;
            if (!found) {
                if (!g_lines.empty() && !Trim(g_lines.back().raw).empty()) {
                    Line blank;
                    blank.section = g_lines.back().section;
                    g_lines.push_back(blank);
                }
                Line h;
                h.header = true;
                h.section = sec;
                g_lines.push_back(h);
                g_lines.push_back(nl);
            } else {
                g_lines.insert(g_lines.begin() + at, nl);
            }
            changed = true;
        }
        if (changed) ScheduleWrite();
    }
    if (changed) Fire({key});
}

void Settings_SetInt(const char* key, int value) { Settings_Set(key, std::to_string(value).c_str()); }

void Settings_Reset(const char* key) {
    EnsureLoaded();
    std::string sec, name;
    Split(key, sec, name);
    bool changed;
    {
        Lock lk;
        changed = EraseLocked(sec, name);
        if (changed) ScheduleWrite();
    }
    if (changed) Fire({key});
}

void Settings_ResetSection(const char* section) {
    EnsureLoaded();
    std::vector<std::string> keys;
    {
        Lock lk;
        size_t n = strlen(section);
        for (auto& d : kDefs) {
            if (strncmp(d.key, section, n) || d.key[n] != '.') continue;
            std::string sec, name;
            Split(d.key, sec, name);
            if (!strcmp(d.key, "launcher.dump_path")) continue;  // never forget the player's disc
            if (EraseLocked(sec, name)) keys.push_back(d.key);
        }
        if (!keys.empty()) ScheduleWrite();
    }
    Fire(keys);
}

void Settings_OnChange(const char* key_prefix, void (*cb)(const char* key)) {
    EnsureLoaded();
    Lock lk;
    g_cbs.push_back({key_prefix ? key_prefix : "", cb});
}

bool Settings_NeedsRestart(const char* key) {
    const Def* d = FindDef(key);
    return d && (d->flags & kRestart);
}

const char* Settings_Default(const char* key) { return DefaultOf(FindDef(key)); }
bool Settings_IsKnown(const char* key) { return FindDef(key) != nullptr; }

const char* Settings_OverriddenBy(const char* key) {
    const char* v;
    return OverrideOf(key, &v);
}

bool Settings_IsSetInFile(const char* key) {
    EnsureLoaded();
    std::string sec, name;
    Split(key, sec, name);
    Lock lk;
    return FindLine(sec, name) != nullptr;
}

void Settings_Flush(void) {
    EnsureLoaded();
    Lock lk;
    if (g_dirty) WriteLocked();
}

void Settings_Reload(void) {
    EnsureLoaded();
    Lock lk;
    if (g_dirty) WriteLocked();
    LoadLocked();
}

std::vector<std::string> settings::Keys() {
    std::vector<std::string> r;
    for (auto& d : kDefs) r.push_back(d.key);
    return r;
}

std::string settings::FileText() {
    EnsureLoaded();
    Lock lk;
    return TextLocked();
}
