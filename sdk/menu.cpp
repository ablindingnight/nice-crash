// The Nice Crash menu: port settings over the paused game (handoff/port_refs/NICE_CRASH_UI_DESIGN.md, section 4).
// Esc (Tab with game.menu_on_tab, or the pad's Start held 0.5 s) opens it; sdk/vi.cpp then holds the game thread in
// its retrace and calls sdk_menu_frame once per screen refresh until it closes, drawing the last game picture under
// it. Every change goes straight to settings.ini (lib/settings) and applies live where the port can; rows the game
// reads only at start wear "Next start".
//
// Test hook: NICECRASH_MENUTEST=<retrace>:<cmd>,<cmd>,...  opens the menu at that retrace and runs one command every
// 8 menu frames: UP DOWN LEFT RIGHT Q E A B R ESC, SHOT=<file.bmp> (save the screen), WAIT.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "hostcfg.h"
#include "project.h"
#include "sdk.h"
#include "settings.h"

extern "C" void sdk_vi_apply_display();  // sdk/vi.cpp: window mode, size and vsync from host_cfg()
unsigned sdk_input_menu_pad();     // sdk/input.cpp
void sdk_input_swallow_held();     // sdk/input.cpp
void* sdk_vi_hwnd();
void gxr_state_dirty();
u32 sdk_vi_retrace_count();

namespace {

// ---- palette (design section 1) ----
struct Col {
    float r, g, b;
};
constexpr Col Hex(unsigned v) { return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f}; }
const Col kSkyTop = Hex(0x0E0820), kPanel = Hex(0x1A0F2E), kPanelHi = Hex(0x261845), kTrack = Hex(0x3B2B5B),
          kBoost = Hex(0xFF5A1F), kGold = Hex(0xFFC22E), kTilt = Hex(0x4CC4FF), kFg = Hex(0xF4EFFB),
          kDim = Hex(0xA99BC6), kAmber = Hex(0xFFB02E), kRed = Hex(0xFF4D5E), kHill = Hex(0x07030E);

// ---- GL entry points the fixed-function overlay needs ----
typedef void(APIENTRY* PFUseProgram)(GLuint);
typedef void(APIENTRY* PFBindFramebuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFActiveTexture)(GLenum);
typedef void(APIENTRY* PFBindVertexArray)(GLuint);
PFUseProgram p_glUseProgram;
PFBindFramebuffer p_glBindFramebuffer;
PFActiveTexture p_glActiveTexture;
PFBindVertexArray p_glBindVertexArray;
void LoadGL() {
    static bool done;
    if (done) return;
    done = true;
    p_glUseProgram = (PFUseProgram)(void*)wglGetProcAddress("glUseProgram");
    p_glBindFramebuffer = (PFBindFramebuffer)(void*)wglGetProcAddress("glBindFramebuffer");
    p_glActiveTexture = (PFActiveTexture)(void*)wglGetProcAddress("glActiveTexture");
    p_glBindVertexArray = (PFBindVertexArray)(void*)wglGetProcAddress("glBindVertexArray");
}

// ---- text: a GDI-rendered glyph atlas per font ----
const unsigned kExtra[] = {0x2605, 0x00B7, 0x2190, 0x2191, 0x2192, 0x2193, 0x2039, 0x203A, 0x2026, 0x00D7, 0x25C0, 0x25B6};
struct Glyph {
    float u0, v0, u1, v1;
    int w;  // advance in pixels
};
struct Font {
    const wchar_t* face;
    float size;  // design pixels (at 1280x720)
    int weight;
    bool italic;
    float builtScale = -1;
    GLuint tex = 0;
    int h = 0, atlasW = 1024, atlasH = 0;
    Glyph g[128 + sizeof kExtra / sizeof kExtra[0]];
    int Index(unsigned cp) const {
        if (cp >= 32 && cp < 127) return (int)cp;
        for (unsigned i = 0; i < sizeof kExtra / sizeof kExtra[0]; i++)
            if (kExtra[i] == cp) return 128 + (int)i;
        return '?';
    }
    void Build(float scale) {
        if (scale == builtScale && tex) return;
        builtScale = scale;
        int px = (int)(size * scale + 0.5f);
        if (px < 6) px = 6;
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT f = CreateFontW(-px, 0, 0, 0, weight, italic, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
        HGDIOBJ oldf = SelectObject(dc, f);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        h = tm.tmHeight;
        // Lay the glyphs out in rows.
        std::vector<unsigned> cps;
        for (unsigned c = 32; c < 127; c++) cps.push_back(c);
        for (unsigned c : kExtra) cps.push_back(c);
        int x = 0, y = 0, pad = 2 + px / 6;  // italic overhang
        std::vector<int> gx(cps.size()), gy(cps.size()), gw(cps.size());
        for (size_t i = 0; i < cps.size(); i++) {
            wchar_t ch = (wchar_t)cps[i];
            SIZE sz{};
            GetTextExtentPoint32W(dc, &ch, 1, &sz);
            int cell = sz.cx + 2 * pad;
            if (x + cell > atlasW) {
                x = 0;
                y += h + 2;
            }
            gx[i] = x;
            gy[i] = y;
            gw[i] = sz.cx;
            x += cell;
        }
        atlasH = y + h + 2;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = atlasW;
        bi.bmiHeader.biHeight = -atlasH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldb = SelectObject(dc, bmp);
        memset(bits, 0, (size_t)atlasW * atlasH * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (size_t i = 0; i < cps.size(); i++) {
            wchar_t ch = (wchar_t)cps[i];
            TextOutW(dc, gx[i] + pad, gy[i], &ch, 1);
        }
        GdiFlush();
        std::vector<unsigned> rgba((size_t)atlasW * atlasH);
        const unsigned* src = (const unsigned*)bits;
        for (size_t i = 0; i < rgba.size(); i++) {
            unsigned c = src[i];
            unsigned a = ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)) / 3;
            rgba[i] = (a << 24) | 0xFFFFFF;
        }
        for (size_t i = 0; i < cps.size(); i++) {
            int k = cps[i] < 127 ? (int)cps[i] : 128 + (int)(i - 95);
            g[k] = {(float)gx[i] / atlasW, (float)gy[i] / atlasH, (float)(gx[i] + gw[i] + 2 * pad) / atlasW,
                    (float)(gy[i] + h) / atlasH, gw[i]};
        }
        SelectObject(dc, oldb);
        SelectObject(dc, oldf);
        DeleteObject(bmp);
        DeleteObject(f);
        DeleteDC(dc);
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);  // GL_CLAMP_TO_EDGE
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, rgba.data());
        padPx = pad;
    }
    int padPx = 0;
};

Font fTitle{L"Segoe UI", 30, FW_BLACK, true};
Font fTab{L"Segoe UI", 21, FW_BOLD, false};
Font fRow{L"Segoe UI", 21, FW_SEMIBOLD, false};
Font fSmall{L"Segoe UI", 16, FW_NORMAL, false};
Font fHelp{L"Segoe UI", 18, FW_NORMAL, false};

std::vector<unsigned> Utf8(const char* s) {
    std::vector<unsigned> out;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        unsigned c = *p++;
        if (c >= 0xF0) {
            c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63);
            p += 3;
        } else if (c >= 0xE0) {
            c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63);
            p += 2;
        } else if (c >= 0xC0) {
            c = (c & 31) << 6 | (p[0] & 63);
            p += 1;
        }
        out.push_back(c);
    }
    return out;
}

// ---- drawing in design units (1280x720, scaled and centred in the window) ----
float g_s = 1, g_ox = 0, g_oy = 0;
int g_winW, g_winH;
float X(float x) { return g_ox + x * g_s; }
float Y(float y) { return g_oy + y * g_s; }

void Rect(float x, float y, float w, float h, Col c, float a = 1, float skew = 0) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(c.r, c.g, c.b, a);
    glBegin(GL_QUADS);
    glVertex2f(X(x + skew), Y(y));
    glVertex2f(X(x + w + skew), Y(y));
    glVertex2f(X(x + w), Y(y + h));
    glVertex2f(X(x), Y(y + h));
    glEnd();
}

float TextW(Font& f, const char* s) {
    f.Build(g_s);
    float w = 0;
    for (unsigned c : Utf8(s)) w += f.g[f.Index(c)].w;
    return w / g_s;
}

// Draws at design (x, y) = top-left of the line; returns the width in design units.
float Text(Font& f, const char* s, float x, float y, Col c, float a = 1) {
    f.Build(g_s);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glColor4f(c.r, c.g, c.b, a);
    float px = floorf(X(x)), py = floorf(Y(y)), start = px;
    glBegin(GL_QUADS);
    for (unsigned cp : Utf8(s)) {
        const Glyph& g = f.g[f.Index(cp)];
        float gw = (g.u1 - g.u0) * f.atlasW, gh = (float)f.h;
        float x0 = px - f.padPx;
        glTexCoord2f(g.u0, g.v0); glVertex2f(x0, py);
        glTexCoord2f(g.u1, g.v0); glVertex2f(x0 + gw, py);
        glTexCoord2f(g.u1, g.v1); glVertex2f(x0 + gw, py + gh);
        glTexCoord2f(g.u0, g.v1); glVertex2f(x0, py + gh);
        px += g.w;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    return (px - start) / g_s;
}

// Word-wrapped text; returns the height used.
float TextWrap(Font& f, const std::string& s, float x, float y, float w, Col c) {
    float lineH = f.size * 1.35f, yy = y;
    std::string line, word;
    auto flush = [&]() {
        Text(f, line.c_str(), x, yy, c);
        yy += lineH;
        line.clear();
    };
    for (size_t i = 0; i <= s.size(); i++) {
        char ch = i < s.size() ? s[i] : ' ';
        if (ch == ' ' || ch == '\n') {
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && TextW(f, trial.c_str()) > w) {
                flush();
                line = word;
            } else {
                line = trial;
            }
            word.clear();
            if (ch == '\n') flush();
        } else {
            word += ch;
        }
    }
    if (!line.empty()) flush();
    return yy - y;
}

// ---- rows ----
enum Kind { kChoice, kSlider, kToggle, kAction, kWinSize };
enum Action { aNone, aResume, aResetAll, aOpenFolder, aQuitLauncher, aQuitGame };
struct Choice {
    const char* value;
    const char* label;
    const char* effect;  // what this value does (the description panel)
};
struct Row {
    const char* label;
    const char* key;
    Kind kind;
    const char* help;
    std::vector<Choice> choices;
    int lo = 0, hi = 100, step = 5;
    Action action = aNone;
    bool confirm = false;
};
struct Tab {
    const char* name;
    std::vector<Row> rows;
};

std::vector<Tab> g_tabs;
int g_tab, g_sel[8], g_scroll[8];
bool g_open, g_armed;  // g_armed: a confirm row was pressed once
int g_frames;
bool g_restartChanged;
bool g_padLast;  // the last input came from a pad (hint glyphs)
int g_monitors;

BOOL CALLBACK CountMon(HMONITOR, HDC, LPRECT, LPARAM p) {
    ++*(int*)p;
    return TRUE;
}

void BuildRows() {
    g_tabs.clear();
    g_monitors = 0;
    EnumDisplayMonitors(nullptr, nullptr, CountMon, (LPARAM)&g_monitors);
    Tab display{"Display", {}};
    display.rows.push_back({"Window mode", "display.window_mode", kChoice, "How the game fills your screen. Alt+Enter switches too.",
                            {{"windowed", "Windowed", "A normal window you can move and resize."},
                             {"borderless", "Borderless full screen", "Covers the whole monitor without changing its mode."}}});
    display.rows.push_back({"Window size", "", kWinSize, "The window's size in Windowed mode.", {}});
    display.rows.push_back({"Aspect ratio", "display.aspect", kChoice, "The shape of the game's picture.",
                            {{"16:9", "16:9 widescreen", "The game's own widescreen mode."},
                             {"4:3", "4:3", "The picture of a standard TV, with bars at the sides."}}});
    display.rows.push_back({"V-sync", "display.vsync", kToggle, "Wait for the monitor's refresh: no tearing.", {}});
    if (g_monitors > 1) {
        Row m{"Monitor", "display.monitor", kChoice, "The monitor the game opens on.", {}};
        static char names[8][16], vals[8][4];
        for (int i = 0; i < g_monitors && i < 8; i++) {
            snprintf(names[i], sizeof names[i], i ? "Monitor %d" : "Main monitor", i + 1);
            snprintf(vals[i], sizeof vals[i], "%d", i);
            m.choices.push_back({vals[i], names[i], ""});
        }
        display.rows.push_back(m);
    }
    Tab graphics{"Graphics", {}};
    graphics.rows.push_back({"Render resolution", "graphics.internal_scale", kChoice, "How sharp the 3D picture is drawn.",
                             {{"window", "Match the window", "Drawn at your window's full size: the sharpest."},
                              {"1", "1x (480p)", "The Wii's own resolution."},
                              {"2", "2x (960p)", "Twice the Wii's resolution."},
                              {"3", "3x (1440p)", "Three times the Wii's resolution."},
                              {"4", "4x (1920p)", "Four times the Wii's resolution: needs a strong GPU."}}});
    graphics.rows.push_back({"Texture filtering", "graphics.texture_filter", kChoice, "How textures look at a distance and at an angle.",
                             {{"game", "As the game sets it", "The Wii's own filtering."},
                              {"bilinear", "Bilinear", "Smooth, no mipmaps."},
                              {"trilinear", "Trilinear", "Smooth with mipmaps: less shimmer far away."},
                              {"aniso16", "Anisotropic 16x", "The sharpest roads and ground at an angle."}}});
    graphics.rows.push_back({"Show frame rate", "graphics.show_fps", kToggle, "Frames per second in the window's title bar (Windowed mode).", {}});
    graphics.rows.push_back({"Enhanced files", "graphics.enhanced_assets", kToggle,
                             "Use the AI-enhanced intro video and your texture packs when they are there.", {}});
    Tab audio{"Audio", {}};
    audio.rows.push_back({"Volume", "audio.master", kSlider, "The game's overall volume.", {}, 0, 100, 5});
    audio.rows.push_back({"Mute in the background", "audio.mute_unfocused", kToggle,
                          "Silence while another window is in front (games started from the launcher).", {}});
    Tab controls{"Controls", {}};
    controls.rows.push_back({"Rumble", "controls.rumble", kToggle, "Controller vibration when the truck hits and lands.", {}});
    controls.rows.push_back({"Stick dead zone", "controls.deadzone", kSlider,
                             "How far the stick moves before the truck steers. Raise it if the truck drifts.", {}, 0, 40, 5});
    controls.rows.push_back({"Invert steering", "controls.invert_steer", kToggle, "Swap left and right on the stick.", {}});
    Tab game{"Game", {}};
    game.rows.push_back({"Pause in the background", "game.pause_unfocused", kToggle,
                         "The game stops while another window is in front (games started from the launcher).", {}});
    game.rows.push_back({"Tab opens this menu", "game.menu_on_tab", kToggle, "Esc always opens it; Tab too when this is on.", {}});
    Tab sys{"System", {}};
    Row r;
    sys.rows.push_back({"Resume", "", kAction, "Back to the game.", {}, 0, 0, 0, aResume});
    sys.rows.push_back({"Reset all settings", "", kAction, "Every setting on these pages back to its default.", {}, 0, 0, 0, aResetAll, true});
    sys.rows.push_back({"Open settings folder", "", kAction, "settings.ini, logs and saves.", {}, 0, 0, 0, aOpenFolder});
    const char* l = getenv("NICECRASH_LAUNCHED");
    if (l && l[0] == '1')
        sys.rows.push_back({"Quit to launcher", "", kAction, "Close the game and go back to the Nice Crash launcher.", {}, 0, 0, 0, aQuitLauncher, true});
    sys.rows.push_back({"Quit game", "", kAction, "Close Nice Crash. Progress since the game last saved is lost.", {}, 0, 0, 0, aQuitGame, true});
    g_tabs = {display, graphics, audio, controls, game, sys};
}

// ---- values ----
const struct {
    int w, h;
} kSizes[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

int ChoiceIndex(const Row& r) {
    std::string v = settings::Get(r.key);
    for (size_t i = 0; i < r.choices.size(); i++)
        if (v == r.choices[i].value) return (int)i;
    return 0;
}
int SizeIndex() {
    const HostCfg& c = host_cfg();
    for (int i = 0; i < 5; i++)
        if (kSizes[i].w == c.windowW && kSizes[i].h == c.windowH) return i;
    return -1;
}

void Applied(const char* key) {
    host_cfg_reload();
    if (key && Settings_NeedsRestart(key)) g_restartChanged = true;
    if (!key || !strncmp(key, "display.", 8)) sdk_vi_apply_display();
}

void Change(Row& r, int dir) {
    switch (r.kind) {
    case kChoice: {
        int n = (int)r.choices.size(), i = (ChoiceIndex(r) + dir + n) % n;
        Settings_Set(r.key, r.choices[i].value);
        Applied(r.key);
        break;
    }
    case kToggle:
        Settings_SetInt(r.key, Settings_GetInt(r.key) ? 0 : 1);
        Applied(r.key);
        break;
    case kSlider: {
        int v = Settings_GetInt(r.key) + dir * r.step;
        v = v < r.lo ? r.lo : v > r.hi ? r.hi : v;
        Settings_SetInt(r.key, v);
        Applied(r.key);
        break;
    }
    case kWinSize: {
        int i = SizeIndex();
        i = i < 0 ? 0 : (i + dir + 5) % 5;
        Settings_SetInt("display.window_width", kSizes[i].w);
        Settings_SetInt("display.window_height", kSizes[i].h);
        Applied("display.window_width");
        break;
    }
    default:
        break;
    }
}

void ResetRow(Row& r) {
    if (r.kind == kWinSize) {
        Settings_Reset("display.window_width");
        Settings_Reset("display.window_height");
        Applied("display.window_width");
    } else if (r.key && r.key[0]) {
        Settings_Reset(r.key);
        Applied(r.key);
    }
}

void Close() { g_open = false; }

void Activate(Row& r) {
    if (r.kind != kAction) {
        Change(r, 1);
        return;
    }
    if (r.confirm && !g_armed) {
        g_armed = true;
        return;
    }
    g_armed = false;
    switch (r.action) {
    case aResume:
        Close();
        break;
    case aResetAll:
        for (const char* s : {"display", "graphics", "audio", "controls", "game"}) Settings_ResetSection(s);
        Applied(nullptr);
        break;
    case aOpenFolder: {
        std::string dir = Settings_Path();
        size_t k = dir.find_last_of("\\/");
        if (k != std::string::npos) dir.resize(k);
        ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case aQuitLauncher: {
        Settings_Flush();
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, sizeof exe);
        std::string dir = exe;
        dir.resize(dir.find_last_of("\\/") + 1);
        std::string launcher = dir + "NiceCrash.exe";
        ShellExecuteA(nullptr, "open", launcher.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
        ExitProcess(0);
    }
    case aQuitGame:
        Settings_Flush();
        ExitProcess(0);
    default:
        break;
    }
}

// ---- input ----
std::vector<unsigned> g_keys;  // WM_KEYDOWN while open (auto-repeat included)
enum Cmd { cNone, cUp, cDown, cLeft, cRight, cTabPrev, cTabNext, cActivate, cBack, cDefault };

Cmd KeyCmd(unsigned vk) {
    switch (vk) {
    case VK_UP: case 'W': return cUp;
    case VK_DOWN: case 'S': return cDown;
    case VK_LEFT: case 'A': return cLeft;
    case VK_RIGHT: case 'D': return cRight;
    case 'Q': return cTabPrev;
    case 'E': return cTabNext;
    case VK_RETURN: case VK_SPACE: case '2': return cActivate;
    case VK_ESCAPE: case VK_BACK: case '1': return cBack;
    case VK_TAB: return host_cfg().menuOnTab ? cBack : cNone;
    case 'R': return cDefault;
    }
    return cNone;
}

void Run(Cmd c) {
    Tab& t = g_tabs[g_tab];
    int n = (int)t.rows.size();
    int& sel = g_sel[g_tab];
    if (c != cActivate && c != cNone) g_armed = false;
    switch (c) {
    case cUp: sel = (sel + n - 1) % n; break;
    case cDown: sel = (sel + 1) % n; break;
    case cLeft: if (t.rows[sel].kind != kAction) Change(t.rows[sel], -1); break;
    case cRight: if (t.rows[sel].kind != kAction) Change(t.rows[sel], 1); break;
    case cTabPrev: g_tab = (g_tab + (int)g_tabs.size() - 1) % (int)g_tabs.size(); break;
    case cTabNext: g_tab = (g_tab + 1) % (int)g_tabs.size(); break;
    case cActivate: Activate(t.rows[sel]); break;
    case cBack: Close(); break;
    case cDefault: ResetRow(t.rows[sel]); break;
    default: break;
    }
}

// Pad: edges, with repeat for the directions (0.4 s, then every 90 ms).
unsigned g_padPrev, g_padIgnore;
DWORD g_repeatAt;
void PadInput() {
    unsigned b = sdk_input_menu_pad();
    b &= ~g_padIgnore;  // the buttons held when the menu opened, until let go
    g_padIgnore &= sdk_input_menu_pad();
    unsigned edge = b & ~g_padPrev;
    unsigned dirs = b & (0x1 | 0x2 | 0x4 | 0x8 | 0x10000 | 0x20000 | 0x40000 | 0x80000);
    DWORD now = GetTickCount();
    if (edge) {
        g_padLast = true;
        g_repeatAt = now + 400;
    } else if (dirs && now >= g_repeatAt) {
        edge |= dirs;
        g_repeatAt = now + 90;
    }
    g_padPrev = b;
    if (edge & (0x1 | 0x10000)) Run(cUp);
    if (edge & (0x2 | 0x20000)) Run(cDown);
    if (edge & (0x4 | 0x40000)) Run(cLeft);
    if (edge & (0x8 | 0x80000)) Run(cRight);
    if (edge & (0x100 | 0x100000)) Run(cTabPrev);
    if (edge & (0x200 | 0x200000)) Run(cTabNext);
    if (edge & 0x1000) Run(cActivate);
    if (edge & (0x2000 | 0x10)) Run(cBack);
    if (edge & 0x8000) Run(cDefault);
}

// Mouse: click a tab, click a row (selects it), click the left/right half of a value; wheel scrolls.
bool g_mousePrev;
struct Hit {
    float x, y, w, h;
    int tab, row, side;  // side: -1 left half of the value, 1 right half, 0 the label
};
std::vector<Hit> g_hits;
void MouseInput() {
    HWND hw = (HWND)sdk_vi_hwnd();
    bool down = GetForegroundWindow() == hw && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    bool click = down && !g_mousePrev;
    g_mousePrev = down;
    if (!click) return;
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(hw, &p);
    float mx = (p.x - g_ox) / g_s, my = (p.y - g_oy) / g_s;
    for (const Hit& h : g_hits)
        if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h) {
            g_padLast = false;
            if (h.row < 0) {
                g_tab = h.tab;
            } else {
                bool same = g_sel[g_tab] == h.row;
                g_sel[g_tab] = h.row;
                Row& r = g_tabs[g_tab].rows[h.row];
                if (h.side) Change(r, h.side);
                else if (r.kind == kAction && (same || !r.confirm)) Activate(r);
            }
            return;
        }
}

// ---- test script ----
struct TestCmd {
    std::string cmd;
};
std::vector<std::string> g_test;
size_t g_testAt;
int g_testOpenAt = -1;
void LoadTest() {
    static bool done;
    if (done) return;
    done = true;
    const char* s = getenv("NICECRASH_MENUTEST");
    if (!s) return;
    g_testOpenAt = atoi(s);
    const char* c = strchr(s, ':');
    if (!c) return;
    std::string rest = c + 1, cur;
    for (char ch : rest + ",") {
        if (ch == ',') {
            if (!cur.empty()) g_test.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
}

void SaveShot(const char* path) {
    std::vector<unsigned char> px((size_t)g_winW * g_winH * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, g_winW, g_winH, GL_BGR_EXT, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return;
    int stride = (g_winW * 3 + 3) & ~3;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + stride * g_winH;
    ih.biSize = sizeof ih;
    ih.biWidth = g_winW;
    ih.biHeight = g_winH;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    std::vector<unsigned char> row(stride);
    for (int y = 0; y < g_winH; y++) {
        memcpy(row.data(), &px[(size_t)y * g_winW * 3], g_winW * 3);
        fwrite(row.data(), stride, 1, f);
    }
    fclose(f);
    fprintf(stderr, "[menu] saved %s\n", path);
}

const char* g_pendingShot;
std::string g_shotPath;
void TestStep() {
    if (g_testAt >= g_test.size() || g_frames % 8 != 7) return;
    const std::string& c = g_test[g_testAt++];
    if (c.rfind("SHOT=", 0) == 0) {
        g_shotPath = c.substr(5);
        g_pendingShot = g_shotPath.c_str();
        return;
    }
    static const struct {
        const char* n;
        Cmd c;
    } map[] = {{"UP", cUp}, {"DOWN", cDown}, {"LEFT", cLeft}, {"RIGHT", cRight}, {"Q", cTabPrev}, {"E", cTabNext},
               {"A", cActivate}, {"B", cBack}, {"ESC", cBack}, {"R", cDefault}};
    for (auto& m : map)
        if (c == m.n) Run(m.c);
}

// ---- drawing ----
void DrawValue(Row& r, float x, float y, float w, float h, bool sel) {
    float cy = y + (h - fRow.size * 1.33f) / 2;
    Col arrow = sel ? kGold : kDim;
    if (r.kind == kChoice || r.kind == kWinSize) {
        char buf[64];
        const char* label;
        if (r.kind == kWinSize) {
            snprintf(buf, sizeof buf, "%d \xC3\x97 %d", host_cfg().windowW, host_cfg().windowH);
            label = buf;
        } else {
            label = r.choices[ChoiceIndex(r)].label;
        }
        Text(fRow, "\xE2\x97\x80", x, cy + 2, arrow, sel ? 1 : 0.5f);
        Text(fRow, "\xE2\x96\xB6", x + w - 18, cy + 2, arrow, sel ? 1 : 0.5f);
        float tw = TextW(fRow, label);
        Text(fRow, label, x + (w - tw) / 2, cy, kFg);
    } else if (r.kind == kToggle) {
        bool on = Settings_GetInt(r.key) != 0;
        float pw = 92, px = x + w - pw;
        Rect(px, y + 12, pw, h - 24, on ? kGold : kTrack, on ? 1 : 0.9f, 0);
        const char* t = on ? "On" : "Off";
        Text(fRow, t, px + (pw - TextW(fRow, t)) / 2, cy, on ? kHill : kDim);
    } else if (r.kind == kSlider) {
        int v = Settings_GetInt(r.key);
        float bw = w - 70, frac = (float)(v - r.lo) / (float)(r.hi - r.lo);
        Rect(x, y + h / 2 - 4, bw, 8, kTrack);
        Rect(x, y + h / 2 - 4, bw * frac, 8, sel ? kGold : kTilt);
        Rect(x + bw * frac - 5, y + h / 2 - 12, 10, 24, kFg);
        char buf[16];
        snprintf(buf, sizeof buf, r.key && strstr(r.key, "deadzone") ? "%d%%" : "%d", v);
        Text(fRow, buf, x + w - TextW(fRow, buf), cy, kFg);
    }
}

void Draw() {
    g_hits.clear();
    // Scrim: Night Plum, 94% at the left fading to 72% at the right, over the whole window.
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor4f(kSkyTop.r, kSkyTop.g, kSkyTop.b, 0.94f);
    glVertex2f(0, 0);
    glColor4f(kSkyTop.r, kSkyTop.g, kSkyTop.b, 0.72f);
    glVertex2f((float)g_winW, 0);
    glVertex2f((float)g_winW, (float)g_winH);
    glColor4f(kSkyTop.r, kSkyTop.g, kSkyTop.b, 0.94f);
    glVertex2f(0, (float)g_winH);
    glEnd();

    // Top bar.
    float tx = Text(fTitle, "\xE2\x98\x85 ", 72, 18, kGold);
    tx += Text(fTitle, "NICE CRASH", 72 + tx, 18, kFg);
    Text(fTitle, "  \xC2\xB7  Paused", 72 + tx, 18, kDim);
    float x = 72, ty = 70;
    const char* prevKey = g_padLast ? "LB" : "Q";
    const char* nextKey = g_padLast ? "RB" : "E";
    x += Text(fSmall, prevKey, x, ty + 6, kDim) + 18;
    for (size_t i = 0; i < g_tabs.size(); i++) {
        float w = TextW(fTab, g_tabs[i].name) + 36;
        bool act = (int)i == g_tab;
        if (act) {
            Rect(x, ty - 4, w, 40, kPanelHi, 1, 8);
            Rect(x + 6, ty + 34, w - 6, 3, kGold);
        }
        Text(fTab, g_tabs[i].name, x + 18 + (act ? 4 : 0), ty + 2, act ? kFg : kDim);
        g_hits.push_back({x, ty - 4, w, 40, (int)i, -1, 0});
        x += w + 6;
    }
    Text(fSmall, nextKey, x + 12, ty + 6, kDim);

    // Rows.
    Tab& t = g_tabs[g_tab];
    int n = (int)t.rows.size(), sel = g_sel[g_tab];
    if (sel >= n) sel = g_sel[g_tab] = n - 1;
    const int kVisible = 8;
    int& sc = g_scroll[g_tab];
    if (sel < sc) sc = sel;
    if (sel >= sc + kVisible) sc = sel - kVisible + 1;
    float ly = 132;
    for (int i = sc; i < n && i < sc + kVisible; i++) {
        Row& r = t.rows[i];
        bool s = i == sel;
        float rh = 54;
        bool danger = s && g_armed && r.confirm;
        Rect(72, ly, 700, rh, danger ? kRed : s ? kPanelHi : kPanel, s ? 1 : 0.85f);
        if (s) Rect(72, ly, 5, rh, danger ? kFg : kBoost);
        const char* label = danger ? (g_padLast ? "Press A again to confirm" : "Press Enter again to confirm") : r.label;
        float lw = Text(fRow, label, 96, ly + (rh - fRow.size * 1.33f) / 2, s ? (danger ? kFg : kGold) : kFg);
        if (r.key && r.key[0] && Settings_NeedsRestart(r.key)) {
            float bx = 96 + lw + 12;
            Rect(bx, ly + 17, TextW(fSmall, "Next start") + 14, 22, kAmber, 0.18f);
            Text(fSmall, "Next start", bx + 7, ly + 18, kAmber);
        }
        if (r.kind != kAction) {
            DrawValue(r, 72 + 700 - 330 - 18, ly, 330, rh, s);
            g_hits.push_back({72, ly, 700 - 330 - 18, rh, g_tab, i, 0});
            g_hits.push_back({72 + 700 - 330 - 18, ly, 165, rh, g_tab, i, -1});
            g_hits.push_back({72 + 700 - 165 - 18, ly, 183, rh, g_tab, i, 1});
        } else {
            g_hits.push_back({72, ly, 700, rh, g_tab, i, 0});
        }
        ly += rh + 8;
    }
    if (n > kVisible) {  // the scroll thumb
        float trackH = kVisible * 62.0f - 8, th = trackH * kVisible / n;
        Rect(62, 132 + trackH * sc / n, 3, th, kGold);
    }

    // Description panel.
    Row& r = t.rows[sel];
    float px = 820, pw = 384, py = 132;
    Rect(px, py, pw, 440, kPanel, 0.85f);
    Text(fRow, r.label, px + 22, py + 18, kGold);
    float yy = py + 56;
    yy += TextWrap(fHelp, r.help, px + 22, yy, pw - 44, kFg) + 10;
    if (r.kind == kChoice) {
        const Choice& c = r.choices[ChoiceIndex(r)];
        if (c.effect && c.effect[0]) yy += TextWrap(fHelp, std::string(c.label) + ": " + c.effect, px + 22, yy, pw - 44, kTilt) + 10;
    }
    if (r.key && r.key[0]) {
        std::string def = Settings_Default(r.key);
        if (r.kind == kToggle) def = def == "1" ? "On" : "Off";
        if (r.kind == kChoice)
            for (auto& c : r.choices)
                if (def == c.value) def = c.label;
        Text(fSmall, ("Default: " + def).c_str(), px + 22, yy, kDim);
        yy += 24;
        bool restart = Settings_NeedsRestart(r.key);
        Text(fSmall, restart ? "Takes effect next start" : "Applies now", px + 22, yy, restart ? kAmber : kDim);
        yy += 24;
        if (const char* env = Settings_OverriddenBy(r.key)) {
            yy += TextWrap(fSmall, std::string("Overridden for this run by ") + env, px + 22, yy, pw - 44, kRed);
        }
    }
    if (g_restartChanged) {
        Rect(px, py + 452, pw, 40, kAmber, 0.15f);
        Text(fSmall, "Some changes take effect when you next start the game.", px + 14, py + 463, kAmber);
    }

    // Hint bar.
    Rect(0, 656, 1280, 64, kHill, 0.6f);
    Text(fSmall, "Changes save to settings.ini as you make them", 72, 678, kDim);
    const char* hints = g_padLast ? "\xE2\x86\x91\xE2\x86\x93 Select    \xE2\x86\x90\xE2\x86\x92 Change    LB RB Category    Y Default    B Resume"
                                  : "\xE2\x86\x91\xE2\x86\x93 Select    \xE2\x86\x90\xE2\x86\x92 Change    Q E Category    R Default    Esc Resume";
    Text(fSmall, hints, 1280 - 72 - TextW(fSmall, hints), 678, kFg);
}

}  // namespace

bool sdk_menu_open() { return g_open; }

void sdk_menu_key(unsigned vk) { g_keys.push_back(vk); }

// sdk/vi.cpp, at each retrace: the test script's open time.
bool sdk_menu_test_wants_open() {
    LoadTest();
    if (g_testOpenAt >= 0 && (int)sdk_vi_retrace_count() >= g_testOpenAt) {
        g_testOpenAt = -1;
        return true;
    }
    return false;
}

void sdk_menu_begin() {
    LoadGL();
    if (g_tabs.empty()) BuildRows();
    g_open = true;
    g_armed = false;
    g_frames = 0;
    g_keys.clear();
    g_padIgnore = sdk_input_menu_pad();  // the Start hold (or a held A) that opened it
    g_padPrev = 0;
    g_mousePrev = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    // Swallow the key-up / repeat of the key that opened the menu.
    MSG m;
    while (PeekMessageA(&m, nullptr, WM_KEYDOWN, WM_KEYUP, PM_REMOVE)) {
    }
    fprintf(stderr, "[menu] open\n");
}

void sdk_menu_end() {
    Settings_Flush();
    sdk_input_swallow_held();
    gxr_state_dirty();
    fprintf(stderr, "[menu] closed\n");
}

// One menu frame over the presented game picture (the default framebuffer is bound, sdk/vi.cpp swaps after).
// False once the menu has closed.
bool sdk_menu_frame(int w, int h) {
    g_winW = w;
    g_winH = h;
    g_s = fminf(w / 1280.0f, h / 720.0f);
    g_ox = (w - 1280.0f * g_s) / 2;
    g_oy = (h - 720.0f * g_s) / 2;
    std::vector<unsigned> keys;
    keys.swap(g_keys);
    for (unsigned vk : keys) {
        g_padLast = false;
        Run(KeyCmd(vk));
    }
    if (g_open) PadInput();
    if (g_open) MouseInput();
    if (g_open) TestStep();
    if (!g_open) return false;
    g_frames++;

    if (p_glBindFramebuffer) p_glBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, 0);
    if (p_glUseProgram) p_glUseProgram(0);
    if (p_glBindVertexArray) p_glBindVertexArray(0);
    if (p_glActiveTexture) p_glActiveTexture(0x84C0 /* GL_TEXTURE0 */);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_COLOR_LOGIC_OP);
    glColorMask(1, 1, 1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Draw();
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
    if (g_pendingShot) {
        SaveShot(g_pendingShot);
        g_pendingShot = nullptr;
    }
    return true;
}
