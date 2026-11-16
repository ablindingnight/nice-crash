// Nice Crash launcher (handoff step 7, design NICE_CRASH_UI_DESIGN.md section 3, mockups/launcher_home.html).
// Screens: Home, the disc picker, the first-run checklist and Settings (Game files, Display, Graphics, Audio,
// Controls, Language, Builds, Logs, About). Starts build/NiceCrashGame.exe with the chosen settings.
// Pad first, mouse welcome: arrows/D-pad move, Enter/A acts, Esc/B goes back, Q E / LB RB switch pages,
// Start plays, Y opens Settings.
#include "launcher.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <mmsystem.h>
#include <xinput.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include "portconfig.h"
#include "project.h"
#include "settings.h"
#include "tasks.h"
#include "truckrun.h"
#include "ui.h"

using namespace ui;
using tasks::State;

namespace {

enum Screen { HOME, DISC, FIRSTRUN, SETTINGS };
enum Page { P_FILES, P_DISPLAY, P_GRAPHICS, P_AUDIO, P_CONTROLS, P_LANGUAGE, P_BUILDS, P_LOGS, P_ABOUT, P_COUNT };

struct App {
    HWND hwnd = nullptr;
    Screen screen = HOME;
    int page = P_DISPLAY;
    State st;
    bool resetFocus = true;     // put focus on the screen's default widget this frame
    bool pendingFocus = false;  // a screen change: settle focus at the start of the next frame
    int wantFocus = -1;
    // disc picker
    std::vector<tasks::Candidate> cands, extra;   // extra: folders the player picked or dropped
    int candSel = 0;
    bool scanStarted = false;
    bool showDumpHelp = false;
    // first run
    bool showDetails = false;
    bool demoChecklist = false;
    bool checkRefreshed = false;
    bool offerShortcuts = false, scDesktop = true, scStart = true;
    // settings
    float scroll = 0, scrollWant = -1;
    // messages
    std::wstring toast;
    double toastUntil = 0;
    std::wstring playError;
    bool gameRunning = false;
    bool runsLoaded = false, lastFailed = false;  // cached run history for Home (reloaded on Go(HOME) and game exit)
    tasks::Run lastRun;
    // titlebar buttons in window pixels, for hit-testing
    RECT tbGear{}, tbMin{}, tbClose{};
    float tbBottom = 52;
    ULONGLONG t0 = 0;
} A;

const UINT WM_GAME_EXIT = WM_APP + 1;

double Now() { return (GetTickCount64() - A.t0) / 1000.0; }

bool g_padLast = false;  // the last button came from the pad: hints show pad glyphs

// Background animation (design 3.2 and display.background_animation): the Home truck run and the emblem's turning
// star ring. "auto" follows Windows' "Show animations in Windows" (SPI_GETCLIENTAREAANIMATION).
bool AnimationOn() {
    std::string v = Settings_Get("display.background_animation");
    if (v == "on") return true;
    if (v == "off") return false;
    BOOL on = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
    return on != FALSE;
}

float g_runL = 0, g_runSx = 1;  // the hills mapping the truck was last drawn with
float g_animClock = 0;      // seconds of animation shown (stands still while the animation is off or paused)
double g_animLast = -1;
float g_animAcc = 0;

// Once per frame: steps the truck run at a fixed 30 fps while Home is showing, the animation is on and the game
// isn't running (the main loop stops rendering altogether while the window is hidden or minimised).
void AdvanceAnimation() {
    double now = Now();
    float dt = g_animLast < 0 ? 0 : (float)std::min(0.1, now - g_animLast);
    g_animLast = now;
    if (!AnimationOn()) {
        truckrun::Park();
        g_animAcc = 0;
        return;
    }
    if (A.screen != HOME || tasks::GameRunning()) return;
    g_animClock += dt;
    g_animAcc += dt;
    const float frame = 1.f / 30;
    while (g_animAcc >= frame) {
        truckrun::Step(frame);
        g_animAcc -= frame;
    }
}

void Toast(const std::wstring& s) {
    A.toast = s;
    A.toastUntil = Now() + 3.5;
}

void Go(Screen s) {
    A.screen = s;
    A.pendingFocus = true;
    A.scroll = 0;
    if (s == HOME) {
        A.st = tasks::Refresh();
        A.runsLoaded = false;
    }
}

void GoPage(int p) {
    A.screen = SETTINGS;
    A.page = (p + P_COUNT) % P_COUNT;
    A.pendingFocus = true;
    A.scroll = 0;
}

// Default focus: the first widget that calls this on a frame after a screen change.
void DefaultFocus(int idx) {
    if (A.resetFocus) {
        g.focus = idx;
        A.resetFocus = false;
    }
}

std::wstring Up(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}

// ---------------------------------------------------------------- shared pieces

void TitleBar() {
    float L = g.viewL, R = g.viewR;
    Star(L + 28 + 8, 26, 8, col::gold);
    float w = Text(L"NICE CRASH", F_DISPLAY, 15, L + 52, 18, col::fg, LEFT, 0.14f);
    Text(L"launcher " LAUNCHER_VERSION, F_MONO, 12, L + 52 + w + 14, 19, col::dim);
    const char* icons[3] = {"gear", "min", "close"};
    RECT* rects[3] = {&A.tbGear, &A.tbMin, &A.tbClose};
    for (int i = 0; i < 3; i++) {
        float bx = R - 22 - 34 * (3 - i), by = 11;
        bool hov = Hover(bx, by, 34, 30);
        if (hov) FillRound(bx, by, 34, 30, 4, i == 2 ? Alpha(col::bad, 0.8f) : Alpha(col::panelHi, 0.9f));
        Icon(icons[i], bx + 10, by + 8, 14, hov ? col::fg : col::dim, 1.6f);
        rects[i]->left = (LONG)(g.ox + bx * g.scale);
        rects[i]->top = (LONG)(g.oy + by * g.scale);
        rects[i]->right = (LONG)(g.ox + (bx + 34) * g.scale);
        rects[i]->bottom = (LONG)(g.oy + (by + 30) * g.scale);
        if (Clicked(bx, by, 34, 30)) {
            if (i == 0) GoPage(A.page);
            else if (i == 1) ShowWindow(A.hwnd, SW_MINIMIZE);
            else PostMessageA(A.hwnd, WM_CLOSE, 0, 0);
        }
    }
    A.tbBottom = g.oy + 52 * g.scale;
}

void DrawToast() {
    if (A.toast.empty() || Now() > A.toastUntil) return;
    float a = (float)std::min(1.0, (A.toastUntil - Now()) / 0.4);
    float w = TextWidth(A.toast, F_BODY_SEMI, 15) + 40;
    float x = 640 - w / 2, y = 662;
    FillRound(x, y, w, 40, 6, Alpha(col::panelHi, 0.96f * a));
    StrokeRound(x, y, w, 40, 6, Alpha(col::gold, 0.5f * a), 1);
    Text(A.toast, F_BODY_SEMI, 15, 640, y + 11, Alpha(col::fg, a), CENTER);
}

void StarBullet(float x, float y, const Color& c) { Star(x + 7, y + 7, 7, c); }

// ---------------------------------------------------------------- actions

void StartChecklist(bool rebuild) {
    std::string path = A.st.dump.path;
    if (path.empty()) { Go(DISC); return; }
    A.demoChecklist = false;
    A.checkRefreshed = false;
    tasks::StartChecklist(path, rebuild);
    Go(FIRSTRUN);
}

void Play() {
    std::string err;
    A.playError.clear();
    if (!tasks::StartGame(err)) {
        A.playError = ui::W(err);
        Toast(A.playError);
        return;
    }
    A.gameRunning = true;
    tasks::LogLine("[launcher] game started");
    if (Settings_GetInt("launcher.close_on_play")) {
        // "Close": the window goes away now; the launcher stays out of sight only to note how the run ended
        // (for the crash row next time), then exits.
        ShowWindow(A.hwnd, SW_HIDE);
    } else {
        ShowWindow(A.hwnd, SW_MINIMIZE);
    }
}

// What PLAY does right now, and its label.
enum PlayAction { ACT_SELECT_DISC, ACT_CHECKLIST, ACT_BUILD, ACT_PLAY };
PlayAction CurrentAction() {
    const State& s = A.st;
    if (!s.haveDump || !s.dump.found || !s.dump.verified) return ACT_SELECT_DISC;
    if (!s.built) return ACT_BUILD;
    if (!s.imported) return ACT_CHECKLIST;
    return ACT_PLAY;
}

void DoPlayAction() {
    switch (CurrentAction()) {
    case ACT_SELECT_DISC: Go(DISC); break;
    case ACT_BUILD: StartChecklist(false); break;
    case ACT_CHECKLIST: StartChecklist(false); break;
    case ACT_PLAY: Play(); break;
    }
}

std::string PickFolder() {
    std::string out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void**)&dlg)))
        return out;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Choose your extracted Wii disc folder");
    if (SUCCEEDED(dlg->Show(A.hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
                out.resize(n ? n - 1 : 0);
                WideCharToMultiByte(CP_UTF8, 0, p, -1, &out[0], n, nullptr, nullptr);
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

void AddPickedFolder(const std::string& path) {
    if (path.empty()) return;
    tasks::Candidate c = tasks::JudgeDump(path);
    for (size_t i = 0; i < A.extra.size(); i++)
        if (!_stricmp(A.extra[i].info.path.c_str(), c.info.path.c_str())) A.extra.erase(A.extra.begin() + i);
    A.extra.insert(A.extra.begin(), c);
    A.candSel = 0;
    if (c.kind != 0) Toast(c.kind == 1 ? L"That disc isn't supported. See why under its name." : L"That folder isn't a complete disc dump.");
}

// ---------------------------------------------------------------- Home

void HomeRow(float y, float h, const Color& star, const std::wstring& key, const std::function<void(float, float)>& body) {
    StarBullet(84, y + 22, star);
    Text(Up(key), F_DISPLAY_CAPS, 14, 84 + 26, y + 19, col::dim, LEFT, 0.2f);
    body(84 + 196 + 18, y + 18);
    Line(84, y + h, 84 + 650, y + h, col::rule, 1);
}

void HomeScreen() {
    const State& s = A.st;
    PlayAction act = CurrentAction();
    tasks::Run lastRun;
    if (!A.runsLoaded) {  // the run history only changes when a run ends: read it once, not every frame
        A.lastFailed = tasks::LastRunFailed(&A.lastRun);
        A.runsLoaded = true;
    }
    bool crashed = A.lastFailed;
    lastRun = A.lastRun;

    // Hero: emblem, wordmark, tagline.
    float spin = fmodf(g_animClock * 9.f, 360.f);
    // Design v8: emblem 360 px from y 58, the Speed stack wordmark 350 px wide 22 px up into it, tagline below.
    const float heroCx = 1280 - 56 - 220, emblemTop = 58, emblemSize = 360, wmW = 350;
    Emblem(heroCx - emblemSize / 2, emblemTop, emblemSize, spin);
    float wmY = emblemTop + emblemSize - 22;
    Wordmark(heroCx - wmW / 2, wmY, wmW);
    {
        float ty = wmY + 120 * wmW / 440 + 12, cx = heroCx;
        std::wstring tag = L"YOUR DISC \u00B7 OUR ENGINE";
        float tw = TextWidth(tag, F_DISPLAY_CAPS, 12, 0.3f);
        Text(tag, F_DISPLAY_CAPS, 12, cx, ty, col::dim, CENTER, 0.3f);
        FillRect(cx - tw / 2 - 12 - 54, ty + 7, 54, 2, col::rule);
        FillRect(cx + tw / 2 + 12, ty + 7, 54, 2, col::rule);
    }

    // Left column.
    float x = 84, y = 92;
    std::wstring eyebrow, head1, head2, lede;
    if (act == ACT_PLAY) {
        eyebrow = L"Ready to race";
        head1 = L"Welcome back, ";
        head2 = L"driver.";
        lede = L"Your disc checks out and your build is current. Press Tab or Esc in game for display, sound and controls.";
    } else if (!s.haveDump) {
        eyebrow = L"First run";
        head1 = L"Let's find your ";
        head2 = L"disc.";
        lede = L"Nice Crash plays from your own Wii disc dump. Choose the folder you extracted it to.";
    } else if (act == ACT_SELECT_DISC) {
        eyebrow = L"Check your disc";
        head1 = L"Let's find your ";
        head2 = L"disc.";
        lede = s.dump.found ? L"This disc doesn't match the retail USA game (REXE01), so Nice Crash can't run it. "
                              L"Other regions aren't supported yet."
                            : L"Nice Crash can't find your disc dump where it was. Choose the folder you extracted it to.";
    } else {
        eyebrow = L"Almost ready";
        head1 = L"Almost there, ";
        head2 = L"driver.";
        lede = act == ACT_BUILD ? L"Your disc checks out. Next, Nice Crash builds the game on this PC. It takes a few minutes."
                                : L"Your disc checks out. Nice Crash needs to index its files once before you play.";
    }
    Eyebrow(eyebrow, x, y);
    float hs = FitSize(head1 + head2, F_DISPLAY, 64, 650);
    float w1 = Text(head1, F_DISPLAY, hs, x, y + 26, col::fg);
    Text(head2, F_DISPLAY, hs, x + w1, y + 26, col::gold);
    float ly = y + 26 + hs * 0.95f + 12;
    if (!crashed) ly += TextWrap(lede, F_BODY, 18, x, ly, 650, col::dim, 1.45f) + 22;
    else ly += 6;

    // Status rows.
    float ry = ly;
    Line(x, ry, x + 650, ry, col::rule, 1);
    const float rowH = crashed ? 74 : 80;
    if (crashed) {
        HomeRow(ry, rowH, col::bad, L"Last run", [&](float vx, float vy) {
            Text(L"Last run stopped unexpectedly.", F_BODY, 18, vx, vy, col::bad);
            std::wstring d = tasks::DescribeExit(lastRun.code) + L" \u00B7 " + ui::W(lastRun.when);
            if (!lastRun.logged) d = L"Turn on error logs to keep details next time.";
            Text(d, F_BODY, 14, vx, vy + 26, col::dim);
        });
        float vy = ry + 18;
        if (lastRun.logged) {
            if (Link(L"View log", 734, vy, 15, col::tilt, true, nullptr, RIGHT)) tasks::OpenPath(tasks::LogPath());
            if (Link(L"Copy report", 734, vy + 26, 15, col::tilt, true, nullptr, RIGHT) &&
                tasks::CopyToClipboard(A.hwnd, tasks::BugReport()))
                Toast(L"Report copied. Paste it wherever you send it.");
        } else {
            if (Link(L"Turn on", 734, vy, 15, col::tilt, true, nullptr, RIGHT)) {
                Settings_Set("launcher.error_log", "1");
                Toast(L"Error logs are on. The next run keeps its details.");
            }
        }
        ry += rowH;
    }
    // Your game.
    HomeRow(ry, rowH, !s.haveDump || !s.dump.verified ? (s.haveDump ? col::bad : col::warn) : col::gold, L"Your game",
            [&](float vx, float vy) {
                std::wstring v1, v2;
                Color c2 = col::dim;
                if (!s.haveDump) {
                    v1 = L"No disc selected yet";
                    v2 = L"Choose the folder you extracted your Wii disc to.";
                    float w = Text(v1, F_BODY, 18, vx, vy, col::fg);
                    Badge(L"Not found", vx + w + 10, vy + 1, col::warn, false);
                } else if (!s.dump.found) {
                    float w = Text(L"Wii disc", F_BODY, 18, vx, vy, col::fg);
                    Badge(L"Not found", vx + w + 10, vy + 1, col::bad, false);
                    v2 = ui::W(s.dump.why);
                    c2 = col::bad;
                } else if (!s.dump.verified) {
                    float w = Text(s.dump.gameId.empty() ? L"Wii disc" : L"Wii disc (" + ui::W(s.dump.gameId) + L")", F_BODY, 18, vx, vy, col::fg);
                    Badge(L"Unknown build", vx + w + 10, vy + 1, col::bad, false);
                    v2 = L"This isn't the retail USA disc. Nice Crash only runs REXE01.";
                    c2 = col::bad;
                } else {
                    float w = Text(L"Wii disc, USA (REXE01)", F_BODY, 18, vx, vy, col::fg);
                    if (s.imported) Badge(L"Verified", vx + w + 10, vy + 1, col::ok, true);
                    else Badge(L"Not checked", vx + w + 10, vy + 1, col::warn, false);
                }
                if (s.dump.found && s.dump.verified) {
                    std::wstring p = Ellipsize(ui::W(s.dump.path), F_BODY, 14, 240);
                    float w = Text(p + L"  \u00B7  ", F_BODY, 14, vx, vy + 26, col::dim);
                    Text(L"main.dol " + ui::W(tasks::ShortHash(s.dump.sha1)), F_MONO, 12.5f, vx + w, vy + 27, col::dim);
                } else if (!v2.empty()) {
                    float sz = FitSize(v2, F_BODY, 14, 330);
                    Text(v2, F_BODY, sz, vx, vy + 26, c2);
                }
            });
    if (Link(s.haveDump ? L"Change" : L"Select", 734, ry + 18, 15, col::tilt, true, nullptr, RIGHT)) Go(DISC);
    ry += rowH;
    // Your build.
    HomeRow(ry, rowH, s.built ? col::gold : col::warn, L"Your build", [&](float vx, float vy) {
        if (s.built) {
            Text(L"Nice Crash " NICECRASH_VERSION, F_BODY, 18, vx, vy, col::fg);
            float w = 0;
            if (!s.decompRef.empty()) {
                w = Text(L"decomp ", F_BODY, 14, vx, vy + 26, col::dim);
                w += Text(ui::W(s.decompRef), F_MONO, 12.5f, vx + w, vy + 27, col::dim);
                w += Text(L"  \u00B7  ", F_BODY, 14, vx + w, vy + 26, col::dim);
            }
            Text(L"built " + ui::W(s.builtWhen), F_BODY, 14, vx + w, vy + 26, col::dim);
        } else {
            float w = Text(L"Nice Crash", F_BODY, 18, vx, vy, col::fg);
            Badge(L"Missing", vx + w + 10, vy + 1, col::warn, false);
            Text(L"Not built yet. It takes about two minutes.", F_BODY, 14, vx, vy + 26, col::warn);
        }
    });
    if (Link(s.built ? L"Rebuild" : L"Build", 734, ry + 18, 15, col::tilt, true, nullptr, RIGHT)) {
        if (s.dump.verified) StartChecklist(s.built);
        else Go(DISC);
    }
    ry += rowH;
    // Newest version.
    HomeRow(ry, rowH, col::dim, L"Newest version", [&](float vx, float vy) {
        Text(L"Local build", F_BODY, 18, vx, vy, col::fg);
        Text(L"Update checks are off until Nice Crash has an update host.", F_BODY, 14, vx, vy + 26, col::dim);
    });
    if (Link(L"Builds", 734, ry + 18, 15, col::tilt, true, nullptr, RIGHT)) GoPage(P_BUILDS);

    // Bottom bar.
    float by = 720 - 46 - 74;
    float lx = 84, lw;
    if (NavLink(L"Settings", "gear", lx, by + 26, &lw)) GoPage(P_DISPLAY);
    lx += lw + 24;
    if (NavLink(L"Controls", "pad", lx, by + 26, &lw)) GoPage(P_CONTROLS);
    lx += lw + 24;
    if (NavLink(L"Logs", "logs", lx, by + 26, &lw)) GoPage(P_LOGS);
    lx += lw + 24;
    if (NavLink(L"About", "about", lx, by + 26, &lw)) GoPage(P_ABOUT);

    const wchar_t* label = act == ACT_PLAY ? L"PLAY" : act == ACT_SELECT_DISC ? L"SELECT YOUR DISC"
                         : act == ACT_BUILD ? L"BUILD NICE CRASH" : L"GET READY";
    float pw = act == ACT_PLAY ? 300 : 360;
    float px = 1280 - 84 - pw;
    {
        float kx = px - 28 - 150;
        float kw;
        Key(L"A", kx, by + 26, &kw);
        float tx = kx + kw + 6;
        tx += Text(L"/", F_BODY_SEMI, 12, tx, by + 29, col::dim) + 6;
        Key(L"ENTER", tx, by + 26, &kw);
        Text(act == ACT_PLAY ? L"play" : L"continue", F_BODY_SEMI, 12, tx + kw + 6, by + 29, col::dim);
    }
    bool running = tasks::GameRunning();
    int playIdx = g.focusCount;
    if (PlayButton(running ? L"RUNNING" : label, px, by, pw, 74, act == ACT_PLAY, false) && !running) DoPlayAction();
    DefaultFocus(playIdx);
    if (g.in.start && !g.consumed && !running) { g.consumed = true; DoPlayAction(); }
    if (g.in.settings && !g.consumed) { g.consumed = true; GoPage(P_DISPLAY); }
}

// ---------------------------------------------------------------- disc picker

void DiscScreen() {
    if (!A.scanStarted) {
        tasks::StartDiscScan();
        A.scanStarted = true;
    }
    std::vector<tasks::Candidate> cands = A.extra;
    for (auto& c : tasks::DiscScanResults()) {
        bool dup = false;
        for (auto& e : cands) dup |= !_stricmp(e.info.path.c_str(), c.info.path.c_str());
        if (!dup) cands.push_back(c);
    }
    A.cands = cands;
    if (A.candSel >= (int)cands.size()) A.candSel = 0;

    float x = 96, y = 84, colW = 1088 - 330 - 48;
    Eyebrow(A.st.haveDump ? L"Choose your disc" : L"First run \u00B7 choose your disc", x, y);
    float hs = 52;
    float w1 = Text(L"Let's find your ", F_DISPLAY, hs, x, y + 26, col::fg);
    Text(L"disc.", F_DISPLAY, hs, x + w1, y + 26, col::gold);
    float sy = y + 26 + 52 + 8;
    sy += TextWrap(cands.empty() ? L"Nice Crash plays from your own Wii disc dump. Choose the folder you extracted it to."
                                 : L"Nice Crash plays from your own Wii disc dump. We looked in the usual places and found these.",
                   F_BODY, 17, x, sy, colW, col::dim, 1.45f) + 18;
    Text(L"FOUND ON THIS PC", F_DISPLAY_CAPS, 13, x, sy, col::dim, LEFT, 0.22f);
    std::wstring sum = tasks::DiscScanSummary();
    Text(sum, F_MONO, 12, x + colW, sy + 1, Alpha(col::dim, 0.8f), RIGHT);
    if (tasks::DiscScanRunning()) Arc(x + colW - TextWidth(sum, F_MONO, 12) - 16, sy + 8, 6, (float)Now() * 400, 270, col::gold, 2);
    float cy = sy + 28;
    int shown = 0;
    int firstIdx = -1;
    for (size_t i = 0; i < cands.size() && shown < 3; i++, shown++) {
        const tasks::Candidate& c = cands[i];
        bool sel = (int)i == A.candSel;
        float ch = c.why.empty() ? 62 : 82;
        int fi = TakeFocus(x, cy, colW, ch);
        if (firstIdx < 0) firstIdx = fi;
        if (Focused(fi) && g.focusVisible) A.candSel = (int)i, sel = true;
        if (Clicked(x, cy, colW, ch) && !g.consumed) { A.candSel = (int)i; g.consumed = true; }
        if (Focused(fi) && g.in.accept && !g.consumed && c.kind == 0) {
            g.consumed = true;
            A.candSel = (int)i;
            Settings_Set("launcher.dump_path", c.info.path.c_str());
            A.st = tasks::Refresh();
            StartChecklist(false);
            return;
        }
        FillRound(x, cy, colW, ch, 6, sel ? col::panelHi : Alpha(col::panel, 0.85f));
        StrokeRound(x, cy, colW, ch, 6, sel ? Alpha(col::gold, 0.7f) : col::faint, 1);
        if (sel) {
            PushSkew(x - 12, cy + ch / 2, -14);
            FillRect(x - 14, cy + 12, 5, ch - 24, col::boost);
            Pop();
        }
        Ring(x + 28, cy + 31, 9, sel ? col::gold : col::faint, 2);
        if (sel) Circle(x + 28, cy + 31, 5, col::gold);
        Color tc = c.kind == 0 ? col::fg : col::dim;
        Text(c.title, F_BODY_SEMI, 17, x + 50, cy + 10, tc);
        std::wstring path = ui::W(c.info.path);
        if (c.info.bytes > 0) path += L"  \u00B7  " + tasks::FormatBytes(c.info.bytes);
        if (!c.info.sha1.empty() && c.kind == 0) path += L"  \u00B7  main.dol " + ui::W(tasks::ShortHash(c.info.sha1));
        float ps = FitSize(path, F_MONO, 12.5f, colW - 200);
        Text(path, F_MONO, ps, x + 50, cy + 36, col::dim);
        const wchar_t* bl = c.kind == 0 ? L"READY" : c.kind == 1 ? L"NOT SUPPORTED" : L"INCOMPLETE";
        Color bc = c.kind == 0 ? col::ok : c.kind == 1 ? col::bad : col::warn;
        Badge(bl, x + colW - 16 - BadgeWidth(bl, c.kind == 0), cy + 20, bc, c.kind == 0);
        if (!c.why.empty()) {
            float s2 = FitSize(c.why, F_BODY, 13, colW - 70);
            Text(c.why, F_BODY, s2, x + 50, cy + 58, col::dim);
        }
        if (Focused(fi) && g.focusVisible) StrokeRound(x - 3, cy - 3, colW + 6, ch + 6, 8, col::tilt, 2);
        cy += ch + 8;
    }
    if (cands.empty() && !tasks::DiscScanRunning()) {
        FillRound(x, cy, colW, 62, 6, Alpha(col::panel, 0.6f));
        StrokeRound(x, cy, colW, 62, 6, col::faint, 1);
        Text(L"No disc dumps found in Downloads, Desktop or Documents.", F_BODY_SEMI, 16, x + 20, cy + 12, col::dim);
        Text(L"Choose the folder yourself with Browse\u2026, or drop it on this window.", F_BODY, 14, x + 20, cy + 36, col::dim);
        cy += 70;
    }
    // Drop zone.
    {
        float dy = cy + 6, dh = 66;
        Gdiplus::Pen pen(Alpha(col::tilt, 0.45f), 2);
        Gdiplus::REAL dash[2] = {3, 2};
        pen.SetDashPattern(dash, 2);
        FillRound(x, dy, colW, dh, 6, col::chip);
        Gdiplus::GraphicsPath p;
        p.AddRectangle(Gdiplus::RectF(x + 1, dy + 1, colW - 2, dh - 2));
        g.g->DrawPath(&pen, &p);
        Icon("folder", x + 18, dy + 20, 26, col::tilt, 1.8f);
        Text(L"Drop your dump folder here", F_BODY_SEMI, 15, x + 60, dy + 14, col::fg);
        Text(L"Or choose it yourself. Disc images (.iso, .rvz) come later.", F_BODY, 13, x + 60, dy + 36, col::dim);
        float bw = TextWidth(L"BROWSE\u2026", F_DISPLAY, 17, 0.12f) + 52;
        if (Button2(L"Browse\u2026", x + colW - bw - 16, dy + 11)) AddPickedFolder(PickFolder());
    }
    // Aside.
    {
        float ax = 96 + colW + 48, ay = 84 + 82, aw = 330;
        FillRound(ax, ay, aw, 330, 8, Alpha(col::panel, 0.92f));
        StrokeRound(ax, ay, aw, 330, 8, col::faint, 1);
        if (!A.showDumpHelp) {
            Text(L"WHAT WE LOOK FOR", F_DISPLAY_CAPS, 12, ax + 24, ay + 22, col::dim, LEFT, 0.24f);
            const wchar_t* items[] = {L"An extracted Wii disc: a folder with sys and files inside.",
                                      L"The retail USA version. We check main.dol against its known fingerprint.",
                                      L"Your dump is only read, never changed or copied anywhere else."};
            float iy = ay + 50;
            for (auto it : items) {
                Star(ax + 31, iy + 9, 7, col::gold);
                iy += TextWrap(it, F_BODY, 14, ax + 48, iy, aw - 72, col::fg, 1.45f) + 10;
            }
            float ty = iy + 4;
            FillRound(ax + 24, ty, aw - 48, 124, 6, col::hill);
            StrokeRound(ax + 24, ty, aw - 48, 124, 6, col::faint, 1);
            const wchar_t* tree[] = {L"dump\\", L"\u251C\u2500 sys\\", L"\u2502  \u2514\u2500 main.dol", L"\u2514\u2500 files\\",
                                     L"   \u251C\u2500 tocres.res", L"   \u2514\u2500 \u2026"};
            for (int i = 0; i < 6; i++)
                Text(tree[i], F_MONO, 12, ax + 36, ty + 10 + i * 18, i == 0 || i == 1 || i == 3 ? col::fg : col::dim);
        } else {
            Text(L"HOW TO DUMP YOUR DISC", F_DISPLAY_CAPS, 12, ax + 24, ay + 22, col::dim, LEFT, 0.24f);
            TextWrap(L"1. Dump the disc with your own Wii (for example with CleanRip on a homebrew Wii).\n"
                     L"2. Open the dump in Dolphin, right-click the game, Properties, Filesystem, then "
                     L"right-click the disc and choose Extract Entire Disc.\n"
                     L"3. Pick the folder you extracted to here. It has sys and files inside.",
                     F_BODY, 14, ax + 24, ay + 50, aw - 48, col::fg, 1.45f);
        }
    }
    // Footer.
    float fy = 720 - 40 - 62;
    float lw;
    if (Link(A.showDumpHelp ? L"What we look for" : L"How do I dump my disc?", x, fy + 18, 15, col::tilt, true, &lw))
        A.showDumpHelp = !A.showDumpHelp;
    if (A.st.haveDump && A.st.dump.verified) {
        if (Link(L"Back", x + lw + 32, fy + 18, 15, col::dim, false)) Go(HOME);
    }
    bool ok = !cands.empty() && cands[A.candSel].kind == 0;
    float pbx = 96 + 1088 - 260;
    {
        float kx = pbx - 24 - 170, kw;
        Key(L"\u2191", kx, fy + 20, &kw);
        Key(L"\u2193", kx + kw + 4, fy + 20, &kw);
        Text(L"choose", F_BODY_SEMI, 12, kx + 56, fy + 23, col::dim);
        Key(g_padLast ? L"A" : L"ENTER", kx + 104, fy + 20, &kw);
        Text(L"continue", F_BODY_SEMI, 12, kx + 104 + kw + 6, fy + 23, col::dim);
    }
    int pi = g.focusCount;
    if (PlayButton(L"CONTINUE", pbx, fy, 260, 62, true, true)) {
        if (ok) {
            Settings_Set("launcher.dump_path", cands[A.candSel].info.path.c_str());
            A.st = tasks::Refresh();
            StartChecklist(false);
            return;
        }
        Toast(cands.empty() ? L"Choose your disc folder first: Browse\u2026 or drop it here."
                            : L"That folder can't be used. Pick one marked Ready.");
    }
    if (!ok) FillRound(pbx - 14, fy - 2, 290, 66, 8, Alpha(col::skyMid, 0.45f));  // dim when there's nothing to continue with
    DefaultFocus(firstIdx >= 0 ? firstIdx : pi);
    if (g.in.back && !g.consumed && A.st.haveDump && A.st.dump.verified) { g.consumed = true; Go(HOME); }
}

// ---------------------------------------------------------------- first run checklist

void MakeShortcuts() {
    if (!A.offerShortcuts) return;
    A.offerShortcuts = false;
    bool ok = true;
    if (A.scDesktop && !tasks::ShortcutExists(0)) ok &= tasks::CreateShortcut(0);
    if (A.scStart && !tasks::ShortcutExists(1)) ok &= tasks::CreateShortcut(1);
    if (!ok) Toast(L"Couldn't make a shortcut. You can still start Nice Crash from its folder.");
}

tasks::Checklist DemoChecklist() {
    tasks::Checklist c;
    c.title = L"Getting Nice Crash ready";
    c.sub = L"We read your disc dump and build the game on this PC. This takes a few minutes.";
    c.running = true;
    double n = GetTickCount64() / 1000.0;
    const wchar_t* t[] = {L"Find your disc dump", L"Check it's the retail USA disc", L"Unpack the build tools",
                          L"Get the Nice Crash source", L"Index the game files", L"Build Nice Crash",
                          L"Prepare the shaders", L"Ready to play"};
    const wchar_t* d[] = {L"D:\\Games\\Wii\\dump (extracted folder, 1.12 GB)", L"main.dol SHA-1 matches REXE01 e516\u2026672b",
                          L"Using zig and Python already on this PC (developer setup)", L"Using the local source \u00B7 decomp dc71f87",
                          L"Reading every resource set and checking its CRCs", L"", L"", L""};
    for (int i = 0; i < 8; i++) {
        tasks::Step s;
        s.title = t[i];
        s.detail = d[i];
        s.state = i < 4 ? tasks::DONE : i == 4 ? tasks::ACTIVE : tasks::PENDING;
        s.t0 = n - 18 + i * 2;
        s.t1 = n - 16 + i * 2;
        if (i == 4) s.progress = 0.62f;
        c.steps.push_back(s);
    }
    c.log = {"[import] disc files: D:\\Games\\Wii\\dump\\DATA\\files", "[import] sha1: e51685814152f377e5c76a50067d8132e25f672b",
             "[import] verified: yes (Wii (USA), REXE01)"};
    return c;
}

void FirstRunScreen() {
    tasks::Checklist c = A.demoChecklist ? DemoChecklist() : tasks::Snapshot();
    if (c.finished && !A.checkRefreshed) {
        A.st = tasks::Refresh();
        A.checkRefreshed = true;
        // Offer shortcuts once, after a first run (not a rebuild), for the ones that don't exist yet.
        bool rebuild = c.title.find(L"Rebuild") != std::wstring::npos;
        A.scDesktop = !tasks::ShortcutExists(0);
        A.scStart = !tasks::ShortcutExists(1);
        A.offerShortcuts = !c.failed && !rebuild && (A.scDesktop || A.scStart);
        A.pendingFocus = true;  // the footer changed (Cancel -> PLAY / Try again): focus its default

    }
    float x = 96, y = 84, w = 1088;
    int cur = 0, done = 0;
    for (size_t i = 0; i < c.steps.size(); i++) {
        if (c.steps[i].state == tasks::DONE || c.steps[i].state == tasks::SKIPPED) done++;
        if (c.steps[i].state == tasks::ACTIVE || c.steps[i].state == tasks::FAILED) cur = (int)i;
        else if (c.steps[i].state != tasks::PENDING) cur = std::max(cur, (int)i);
    }
    wchar_t eb[64];
    if (c.finished && !c.failed) swprintf(eb, 64, L"All set");
    else swprintf(eb, 64, L"First run \u00B7 step %d of %d", cur + 1, (int)c.steps.size());
    Eyebrow(c.failed ? (c.cancelled ? L"Cancelled" : L"Something went wrong") : eb, x, y, c.failed ? col::bad : col::gold);
    Text(c.title, F_DISPLAY, 48, x, y + 22, col::fg);
    Text(c.sub, F_BODY, 17, x, y + 22 + 56, col::dim);
    // Overall bar with boost stripes.
    float by = y + 22 + 56 + 34;
    float frac = c.steps.empty() ? 0 : (float)done / c.steps.size();
    for (auto& s : c.steps)
        if (s.state == tasks::ACTIVE) frac += (s.progress >= 0 ? s.progress : 0.5f) / c.steps.size();
    FillRound(x, by, w, 10, 5, col::faint);
    if (frac > 0) {
        float fw = std::max(10.f, w * frac);
        PushClip(x, by, fw, 10);
        FillRound(x, by, fw, 10, 5, c.failed ? col::bad : col::boost);
        if (!c.failed) {
            Gdiplus::SolidBrush b(Color(255, 0xFF, 0x7A, 0x22));
            float off = c.running ? fmodf((float)Now() * 30, 26.4f) : 0;
            for (float sx = x - 30 + off; sx < x + fw + 30; sx += 26.4f) {
                Gdiplus::PointF v[4] = {Gdiplus::PointF(sx, by), Gdiplus::PointF(sx + 13.2f, by),
                                        Gdiplus::PointF(sx + 13.2f - 4.7f, by + 10), Gdiplus::PointF(sx - 4.7f, by + 10)};
                g.g->FillPolygon(&b, v, 4);
            }
        }
        Pop();
        if (!c.failed && frac < 1) {
            FillRect(x + fw - 4, by - 4, 4, 18, col::gold);
            FillRect(x + fw - 8, by - 6, 12, 22, Alpha(col::gold, 0.15f));
        }
    }
    // Checklist panel.
    float py = by + 26;
    bool compact = A.showDetails;
    float stepH = compact ? 34 : 46;
    float ph = c.steps.size() * stepH + 16;
    FillRound(x, py, w, ph, 10, Alpha(col::panel, 0.85f));
    StrokeRound(x, py, w, ph, 10, col::faint, 1);
    double now = GetTickCount64() / 1000.0;
    for (size_t i = 0; i < c.steps.size(); i++) {
        const tasks::Step& s = c.steps[i];
        float sy = py + 8 + i * stepH, dx = x + 28 + 16, dy = sy + 16;
        bool fin = s.state == tasks::DONE || s.state == tasks::SKIPPED;
        if (i + 1 < c.steps.size()) Line(dx, dy + 18, dx, dy + stepH - 16, fin ? col::gold : col::faint, 2);
        float r = compact ? 12 : 15;
        if (fin) {
            Circle(dx, dy, r, col::gold);
            Icon("tick", dx - r * 0.55f, dy - r * 0.55f, r * 1.1f, col::onGold, 3);
        } else if (s.state == tasks::FAILED) {
            Circle(dx, dy, r, col::bad);
            Icon("cross", dx - r * 0.5f, dy - r * 0.5f, r, col::fg, 2.6f);
        } else if (s.state == tasks::ACTIVE) {
            Ring(dx, dy, r - 1, col::faint, 2);
            Arc(dx, dy, r - 1, fmodf((float)Now() * 300, 360), 90, col::boost, 2.4f);
        } else {
            Ring(dx, dy, r - 1, col::faint, 2);
        }
        bool pend = s.state == tasks::PENDING;
        float tx = x + 28 + 50;
        Text(s.title, pend ? F_BODY : F_BODY_SEMI, compact ? 16 : 18, tx, sy + (compact ? 6 : 4),
             pend ? col::dim : s.state == tasks::FAILED ? col::bad : col::fg);
        if (!compact && !s.detail.empty()) {
            float ds = FitSize(s.detail, F_BODY, 14, w - 280);
            float dw = Text(s.detail, F_BODY, ds, tx, sy + 27, s.state == tasks::FAILED ? col::fg : col::dim);
            if (s.state == tasks::ACTIVE && s.progress >= 0) Meter(tx + dw + 12, sy + 34, 160, 4, s.progress);
            else if (s.state == tasks::ACTIVE && s.progress == -1 && !A.demoChecklist) {
                // indeterminate: a gold block sliding along the mini bar
                float mx = tx + dw + 12;
                FillRound(mx, sy + 34, 160, 4, 2, col::faint);
                float p = fmodf((float)Now() * 0.8f, 1.f);
                PushClip(mx, sy + 32, 160, 8);
                FillRound(mx - 40 + p * 200, sy + 34, 40, 4, 2, col::gold);
                Pop();
            }
        }
        std::wstring tm = L"\u2013";
        if (!pend) {
            double secs = (s.state == tasks::ACTIVE ? now : s.t1) - s.t0;
            if (secs < 0) secs = 0;
            wchar_t b[16];
            swprintf(b, 16, L"%d:%02d", (int)secs / 60, (int)secs % 60);
            tm = b;
        }
        Text(tm, F_MONO, 13, x + w - 28, sy + (compact ? 8 : 7), s.state == tasks::ACTIVE ? col::gold : col::dim, RIGHT);
    }
    // Details (the live log).
    float fy = 720 - 40 - 44;
    if (A.showDetails) {
        float dy = py + ph + 12, dh = fy - 12 - dy;
        FillRound(x, dy, w, dh, 6, col::hill);
        StrokeRound(x, dy, w, dh, 6, col::faint, 1);
        int lines = (int)((dh - 16) / 19);
        int start = std::max(0, (int)c.log.size() - lines);
        PushClip(x + 8, dy + 4, w - 16, dh - 8);
        for (int i = start; i < (int)c.log.size(); i++)
            Text(ui::W(c.log[i]), F_MONO, 12, x + 14, dy + 8 + (i - start) * 19, col::dim);
        Pop();
    }
    // Footer.
    float lw;
    int first = g.focusCount;
    if (Link(A.showDetails ? L"Hide details" : L"Show details", x, fy + 10, 15, col::tilt, true, &lw))
        A.showDetails = !A.showDetails;
    float sx = x + lw + 28;
    if (A.offerShortcuts && c.finished && !c.failed) {
        // Last step of a first run (design 9): shortcuts to the launcher, both ticked; made on PLAY or Home.
        int i1 = g.focusCount;
        TakeFocus(sx, fy + 6, 190, 30);
        Checkbox(&A.scDesktop, L"Desktop shortcut", sx, fy + 9, i1);
        int i2 = g.focusCount;
        TakeFocus(sx + 220, fy + 6, 210, 30);
        Checkbox(&A.scStart, L"Start menu entry", sx + 220, fy + 9, i2);
    } else {
        Icon("shield", sx, fy + 13, 16, col::ok, 1.8f);
        Text(L"Your dump is only read, never changed. Nothing leaves this PC.", F_BODY, 14, sx + 24, fy + 12, col::dim);
    }
    float bx = x + w;
    if (c.running) {
        float bw = TextWidth(L"CANCEL", F_DISPLAY, 17, 0.12f) + 52;
        int ci = g.focusCount;
        if (Button2(L"Cancel", bx - bw, fy)) { tasks::CancelChecklist(); A.demoChecklist = false; }
        DefaultFocus(ci);
        if (g.in.back && !g.consumed) { g.consumed = true; tasks::CancelChecklist(); }
    } else if (c.failed) {
        float bw = TextWidth(L"TRY AGAIN", F_DISPLAY, 17, 0.12f) + 52, bw2;
        int ti = g.focusCount;
        if (Button2(L"Try again", bx - bw, fy)) {
            bool rebuild = c.title.find(L"Rebuild") != std::wstring::npos;
            if (c.steps[0].state == tasks::FAILED || c.steps[1].state == tasks::FAILED) Go(DISC);
            else { A.checkRefreshed = false; tasks::StartChecklist(A.st.dump.path, rebuild); }
        }
        DefaultFocus(ti);
        bw2 = TextWidth(L"BACK", F_DISPLAY, 17, 0.12f) + 52;
        if (Button2(L"Back", bx - bw - 20 - bw2, fy) || (g.in.back && !g.consumed)) { g.consumed = true; Go(HOME); }
    } else {
        // Ready to play.
        float pw = 260;
        int pi = g.focusCount;
        if (PlayButton(L"PLAY", bx - pw, fy - 18, pw, 62, true, false)) {
            MakeShortcuts();
            A.st = tasks::Refresh();
            Go(HOME);
            Play();
        }
        DefaultFocus(pi);
        float bw2 = TextWidth(L"HOME", F_DISPLAY, 17, 0.12f) + 52;
        if (Button2(L"Home", bx - pw - 30 - bw2, fy) || (g.in.back && !g.consumed)) {
            g.consumed = true;
            MakeShortcuts();
            Go(HOME);
        }
    }
    (void)first;
}

// ---------------------------------------------------------------- settings

struct Opt {
    const char* value;
    const wchar_t* label;
};

const float kMainX = 304, kMainW = 576, kCtlX = 304 + 192, kAsideX = 304 + 576 + 44, kAsideW = 300;
const float kTop = 156, kBottom = 720 - 26 - 44 - 18;
float g_rowY;
bool g_rowsClipped = false;
void EndRows() {
    if (g_rowsClipped) { Pop(); g_rowsClipped = false; }
}
std::vector<std::pair<int, std::pair<float, float>>> g_rowRects;  // focus idx -> (y, h) in content space

// A settings row: label (with a "Next start" tag and an env-override note) and help text; the caller draws the
// control at (kCtlX, returned y). Returns the row's focus index.
int Row(const std::wstring& label, const std::wstring& help, const char* key, float ctlH, float* ctlY) {
    float helpH = help.empty() ? 0 : TextWrap(help, F_BODY, 13, kCtlX, 0, kMainW - 192, col::dim, 1.4f, false) + 6;
    const char* ov = key ? Settings_OverriddenBy(key) : nullptr;
    float labelExtra = (key && Settings_NeedsRestart(key) ? 24 : 0) + (ov ? 18 : 0);
    const float kPad = 11;  // above and below each row (tight enough that the Graphics page fits at rest)
    float h = kPad + std::max(ctlH, 22 + labelExtra) + helpH + kPad;
    float y = g_rowY;
    int idx = TakeFocus(kMainX - 20, y, kMainW + 20, h);
    g_rowRects.push_back({idx, {y + A.scroll, h}});
    if (Focused(idx)) {
        FillRect(kMainX - 30, y, kMainW + 30, h, Alpha(col::panelHi, 0.35f));
        PushSkew(kMainX - 16, y + h / 2, -14);
        FillRect(kMainX - 18, y + 14, 4, h - 28, col::boost);
        Pop();
    }
    Text(label, F_BODY_SEMI, 15, kMainX, y + kPad + std::max(0.f, (ctlH - 22) / 2) - (labelExtra > 0 ? 4 : 0), col::fg);
    float ty = y + kPad + std::max(0.f, (ctlH - 22) / 2) + 20;
    if (key && Settings_NeedsRestart(key)) {
        Badge(L"Next start", kMainX, ty, col::warn, false);
        ty += 24;
    }
    if (ov) {
        float s = FitSize(L"set by " + ui::W(ov), F_MONO, 11, 170);
        Text(L"set by " + ui::W(ov), F_MONO, s, kMainX, ty, col::tilt);
    }
    if (!help.empty()) TextWrap(help, F_BODY, 13, kCtlX, y + kPad + ctlH + 6, kMainW - 192, col::dim, 1.4f);
    Line(kMainX, y + h, kMainX + kMainW, y + h, col::rule, 1);
    *ctlY = y + kPad;
    g_rowY += h;
    return idx;
}

void SubHead(const std::wstring& s) {
    g_rowY += 10;
    Text(Up(s), F_DISPLAY_CAPS, 12, kMainX, g_rowY + 8, col::dim, LEFT, 0.24f);
    g_rowY += 30;
    Line(kMainX, g_rowY, kMainX + kMainW, g_rowY, col::rule, 1);
}

int IndexOf(const std::vector<Opt>& opts, const std::string& v) {
    for (size_t i = 0; i < opts.size(); i++)
        if (v == opts[i].value) return (int)i;
    return -1;
}

void SegRow(const wchar_t* label, const char* key, const std::vector<Opt>& opts, const std::wstring& help = L"") {
    float cy;
    int idx = Row(label, help, key, 34, &cy);
    std::vector<std::wstring> labels;
    for (auto& o : opts) labels.push_back(o.label);
    int sel = IndexOf(opts, Settings_Get(key));
    if (sel < 0) sel = std::max(0, IndexOf(opts, Settings_Default(key)));
    if (Seg(labels, &sel, kCtlX, cy, idx)) Settings_Set(key, opts[sel].value);
}

void ToggleRow(const wchar_t* label, const char* key, const std::wstring& help = L"") {
    float cy;
    int idx = Row(label, help, key, 24, &cy);
    bool on = Settings_GetInt(key) != 0;
    if (Toggle(&on, kCtlX, cy, idx)) Settings_Set(key, on ? "1" : "0");
}

void SliderRow(const wchar_t* label, const char* key, int lo, int hi, int step, const wchar_t* unit,
               const std::wstring& help = L"") {
    float cy;
    int idx = Row(label, help, key, 34, &cy);
    int v = Settings_GetInt(key);
    if (Slider(&v, lo, hi, step, kCtlX + 10, cy, 300, idx, unit)) Settings_SetInt(key, v);
}

void PickerRow(const wchar_t* label, const char* key, const std::vector<std::string>& values,
               const std::vector<std::wstring>& labels, const std::wstring& help = L"") {
    float cy;
    int idx = Row(label, help, key, 36, &cy);
    int sel = 0;
    std::string v = Settings_Get(key);
    for (size_t i = 0; i < values.size(); i++)
        if (values[i] == v) sel = (int)i;
    if (Picker(labels, &sel, kCtlX, cy, 300, idx) && sel < (int)values.size()) Settings_Set(key, values[sel].c_str());
}

// Display modes and monitors, read once.
struct Modes {
    std::vector<std::string> values;
    std::vector<std::wstring> labels;
    std::vector<std::wstring> monitors;
    std::vector<std::string> audioValues;
    std::vector<std::wstring> audioLabels;
    int deskW = 1920, deskH = 1080;
    bool loaded = false;
} M;

BOOL CALLBACK MonEnum(HMONITOR m, HDC, LPRECT, LPARAM) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(m, &mi);
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof dd;
    std::wstring name = mi.szDevice;
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0) && dd.DeviceString[0]) name = dd.DeviceString;
    wchar_t b[200];
    swprintf(b, 200, L"%d \u00B7 %ls (%ld\u00D7%ld)", (int)M.monitors.size() + 1, name.c_str(),
             mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
    M.monitors.push_back(b);
    return TRUE;
}

void LoadModes() {
    if (M.loaded) return;
    M.loaded = true;
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
    M.deskW = dm.dmPelsWidth;
    M.deskH = dm.dmPelsHeight;
    wchar_t b[64];
    swprintf(b, 64, L"%d \u00D7 %d (desktop)", M.deskW, M.deskH);
    M.values.push_back("desktop");
    M.labels.push_back(b);
    std::vector<std::pair<int, int>> seen;
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++) {
        std::pair<int, int> wh((int)dm.dmPelsWidth, (int)dm.dmPelsHeight);
        if (wh.first < 640 || wh.second < 480) continue;
        if (std::find(seen.begin(), seen.end(), wh) != seen.end()) continue;
        seen.push_back(wh);
    }
    std::sort(seen.begin(), seen.end(), [](auto a, auto b) { return a.first * a.second > b.first * b.second; });
    for (auto& wh : seen) {
        char v[32];
        snprintf(v, 32, "%dx%d", wh.first, wh.second);
        swprintf(b, 64, L"%d \u00D7 %d", wh.first, wh.second);
        M.values.push_back(v);
        M.labels.push_back(b);
    }
    EnumDisplayMonitors(nullptr, nullptr, MonEnum, 0);
    M.audioValues.push_back("default");
    M.audioLabels.push_back(L"System default");
    UINT n = waveOutGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        WAVEOUTCAPSW caps;
        if (waveOutGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR) {
            int len = WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, nullptr, 0, nullptr, nullptr);
            std::string s(len ? len - 1 : 0, 0);
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, &s[0], len, nullptr, nullptr);
            M.audioValues.push_back(s);
            M.audioLabels.push_back(caps.szPname);
        }
    }
}

void AsideTitle(const wchar_t* s, float y) { Text(Up(s), F_DISPLAY_CAPS, 12, kAsideX, y, col::dim, LEFT, 0.24f); }

void Note(const std::wstring& s, float y) {
    float h = TextWrap(s, F_BODY, 13, kAsideX + 14, 0, kAsideW - 26, col::fg, 1.45f, false) + 20;
    FillRect(kAsideX, y, kAsideW, h, col::chip);
    FillRect(kAsideX, y, 3, h, col::tilt);
    TextWrap(s, F_BODY, 13, kAsideX + 14, y + 10, kAsideW - 26, col::fg, 1.45f);
}

int Gcd(int a, int b) { return b ? Gcd(b, a % b) : a; }

void AspectPreview(float y) {
    AsideTitle(L"Preview", y);
    float bx = kAsideX, by = y + 26, bw = 300, bh = 169;
    FillRound(bx, by, bw, bh, 6, col::hill);
    std::string a = Settings_Get("display.aspect");
    float ar = a == "4:3" ? 4.f / 3 : a == "native" ? (float)M.deskW / M.deskH : 16.f / 9;
    float pw = std::min(bw - 4, (bh - 4) * ar), ph = std::min(bh - 4, pw / ar);
    float px = bx + (bw - pw) / 2, py = by + (bh - ph) / 2;
    Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(0, py), Gdiplus::PointF(0, py + ph * 1.4f), Color(255, 0x3A, 0x1F, 0x66), col::glow);
    g.g->FillRectangle(&lg, px, py, pw, ph);
    float tw = pw * 0.46f;
    Truck(px + (pw - tw) / 2, py + (ph - tw * 140 / 230) / 2, tw, tw * 140 / 230);
    StrokeRound(bx, by, bw, bh, 6, col::faint, 2);
    wchar_t tag[64];
    if (a == "4:3") swprintf(tag, 64, L"4:3 \u00B7 640\u00D7480 look");
    else if (a == "native") swprintf(tag, 64, L"window \u00B7 %d:%d here", M.deskW / std::max(1, Gcd(M.deskW, M.deskH)), M.deskH / std::max(1, Gcd(M.deskW, M.deskH)));
    else swprintf(tag, 64, L"16:9 \u00B7 Wii widescreen");
    Text(tag, F_MONO, 11, bx + bw - 8, by + bh - 18, col::dim, RIGHT);
}

void PageFiles() {
    const State& s = A.st;
    float cy;
    {
        int idx = Row(L"Disc folder", L"", nullptr, 36, &cy);
        std::wstring p = s.haveDump ? Ellipsize(ui::W(s.dump.path), F_MONO, 12.5f, 240) : L"(none chosen yet)";
        Text(p, F_MONO, 12.5f, kCtlX, cy + 10, s.haveDump ? col::fg : col::dim);
        float bw = TextWidth(L"CHANGE\u2026", F_DISPLAY, 17, 0.12f) + 52;
        g.focusCount--;  // the button shares the row's focus slot
        if (Button2(L"Change\u2026", kMainX + kMainW - bw, cy - 4) || (Focused(idx) && g.in.accept && !g.consumed)) {
            g.consumed = true;
            Go(DISC);
        }
    }
    {
        Row(L"Status", L"", nullptr, 50, &cy);
        if (!s.haveDump) Badge(L"No disc", kCtlX, cy, col::warn, false);
        else if (!s.dump.found) Badge(L"Not found", kCtlX, cy, col::bad, false);
        else if (!s.dump.verified) Badge(L"Unknown build", kCtlX, cy, col::bad, false);
        else Badge(L"Verified \u00B7 Wii (USA), REXE01", kCtlX, cy, col::ok, true);
        std::wstring h = s.dump.sha1.empty() ? ui::W(s.dump.why) : L"main.dol SHA-1 " + ui::W(s.dump.sha1);
        float sz = FitSize(h, F_MONO, 12, kMainW - 192);
        Text(h, F_MONO, sz, kCtlX, cy + 28, col::dim);
    }
    {
        portcfg::Values v;
        portcfg::Read(portcfg::DumpIniPath(), v);
        Row(L"Index", L"", nullptr, 22, &cy);
        std::wstring t = s.imported ? ui::W(v["sets"] + " resource sets, " + v["resources"] + " resources \u00B7 " + v["imported"])
                                    : L"Not indexed yet";
        Text(t, F_BODY, 14, kCtlX, cy + 2, s.imported ? col::fg : col::warn);
    }
    g_rowY += 22;
    float bx = kMainX, bw;
    if (Button2(L"Check again", bx, g_rowY, &bw)) {
        A.st = tasks::Refresh();
        Toast(A.st.dump.verified ? L"Your disc checks out." : L"Your disc didn't check out. See Status.");
    }
    bx += bw + 24;
    if (Button2(L"Open folder", bx, g_rowY, &bw, s.dump.found)) tasks::OpenPath(s.dump.path);
    bx += bw + 24;
    if (Button2(L"Index again", bx, g_rowY, &bw, s.dump.verified)) StartChecklist(false);
    g_rowY += 60;

    EndRows();
    float ay = kTop;
    AsideTitle(L"Your disc", ay);
    TextWrap(L"Nice Crash plays from your own Wii disc dump. It's only read, never changed or copied anywhere else. "
             L"The game's files stay where you extracted them.",
             F_BODY, 13, kAsideX, ay + 26, kAsideW, col::dim, 1.5f);
    Note(L"Later: show your disc's own banner art on the home screen, read from your dump.", ay + 120);
}

void PageDisplay() {
    LoadModes();
    SegRow(L"Window mode", "display.window_mode",
           {{"windowed", L"Windowed"}, {"borderless", L"Borderless"}, {"fullscreen", L"Fullscreen"}});
    std::string mode = Settings_Get("display.window_mode");
    PickerRow(L"Resolution", "display.resolution", M.values, M.labels,
              mode == "borderless" ? L"Borderless always uses the desktop size." : L"");
    std::string a = Settings_Get("display.aspect");
    SegRow(L"Aspect ratio", "display.aspect", {{"4:3", L"4:3 Retail"}, {"16:9", L"16:9 Widescreen"}, {"native", L"Match window"}},
           a == "4:3" ? L"4:3 is how the game looks on a standard TV. Takes effect the next time the game starts."
           : a == "native" ? L"Match window widens the view to fill any window shape. Menus and the HUD keep their "
                             L"proportions. Takes effect the next time the game starts."
                           : L"16:9 uses the game's own widescreen mode. Takes effect the next time the game starts.");
    ToggleRow(L"V-sync", "display.vsync");
    SegRow(L"Frame rate", "display.frame_rate", {{"59.94", L"59.94 Hz (Wii)"}, {"60", L"60 Hz"}},
           L"The game runs one step per TV frame, so it can't go faster without speeding up the race.");
    if (M.monitors.size() > 1) {
        std::vector<std::string> mv;
        for (size_t i = 0; i < M.monitors.size(); i++) mv.push_back(std::to_string(i));
        PickerRow(L"Monitor", "display.monitor", mv, M.monitors);
    }
    SegRow(L"Home animation", "display.background_animation", {{"auto", L"Auto"}, {"on", L"On"}, {"off", L"Off"}},
           L"The truck run and the turning stars on Home. Auto follows Windows' animation setting.");
    EndRows();
    AspectPreview(kTop);
    TextWrap(L"Black bars fill whatever the picture doesn't cover. Menus and the HUD follow the same shape.", F_BODY, 13,
             kAsideX, kTop + 26 + 169 + 12, kAsideW, col::dim, 1.5f);
    Note(L"You can change these during a race too: press Tab or Esc.", kTop + 26 + 169 + 70);
}

void PageGraphics() {
    SegRow(L"Internal resolution", "graphics.internal_scale",
           {{"1", L"1\u00D7"}, {"2", L"2\u00D7"}, {"3", L"3\u00D7"}, {"4", L"4\u00D7"}, {"window", L"Window"}},
           L"How sharp the 3D is drawn. 1× is the Wii's own 640×480.");
    SegRow(L"Anti-aliasing", "graphics.msaa", {{"0", L"Off"}, {"2", L"2\u00D7"}, {"4", L"4\u00D7"}, {"8", L"8\u00D7"}},
           L"Smooths jagged edges. Higher costs more GPU time.");
    SegRow(L"Texture filtering", "graphics.texture_filter",
           {{"game", L"Game"}, {"bilinear", L"Bilinear"}, {"trilinear", L"Trilinear"}, {"aniso16", L"Aniso 16\u00D7"}},
           L"Game keeps the original. The others sharpen angled textures.");
    ToggleRow(L"Show FPS", "graphics.show_fps");
    // Reserved for "Enhance my game" (ENHANCE_MY_GAME_2026-11-15.md): local AI upscaling of the player's own intro
    // video and textures into the cache, never touching the originals. Model/scale picker, a checklist with Cancel,
    // disk space and "Remove enhanced files" come with the feature (keys graphics.enhanced_assets,
    // upscale_model, upscale_scale are already in settings.cpp).
    SubHead(L"Enhance my game");
    {
        float cy;
        Row(L"AI upscaling", L"Sharpens your own intro video, menus, HUD and truck paint on this PC. Your files are "
                            L"never changed. Smoother edges come from the settings above.",
            nullptr, 34, &cy);
        Badge(L"Later", kCtlX, cy + 7, col::tilt, false);
        g.focusCount--;  // the button shares the row's focus slot
        Button2(L"Enhance…", kMainX + kMainW - (TextWidth(L"ENHANCE…", F_DISPLAY, 17, 0.12f) + 52), cy, nullptr,
                false);
    }
    EndRows();
    AsideTitle(L"Graphics", kTop);
    TextWrap(L"These change how the picture is drawn, never how the game plays. Rows marked Next start apply when "
             L"the game starts again.",
             F_BODY, 13, kAsideX, kTop + 26, kAsideW, col::dim, 1.5f);
    Note(L"Renderer: OpenGL. The setting lives under Builds.", kTop + 110);
}

void PageAudio() {
    LoadModes();
    SliderRow(L"Master volume", "audio.master", 0, 100, 5, L"%", L"The final level of everything the game plays.");
    SegRow(L"Wii Remote speaker", "audio.remote_speaker", {{"off", L"Off"}, {"main", L"Main output"}},
           L"Sounds the Wii plays from the remote's speaker are mixed into your main output.");
    ToggleRow(L"Mute in background", "audio.mute_unfocused", L"Silence the game while another window has focus.");
    PickerRow(L"Output device", "audio.device", M.audioValues, M.audioLabels);
    EndRows();
    AsideTitle(L"Audio", kTop);
    TextWrap(L"Music and effects get their own sliders once the port can tell them apart.", F_BODY, 13, kAsideX,
             kTop + 26, kAsideW, col::dim, 1.5f);
}

void PageControls() {
    SegRow(L"Device", "controls.device", {{"auto", L"Auto"}, {"keyboard", L"Keyboard"}, {"gamepad", L"Gamepad"}},
           L"Auto uses whichever you touched last.");
    SegRow(L"Steer with", "controls.steer_source", {{"stick", L"Stick"}, {"keys", L"Keys"}},
           Settings_Get("controls.steer_source") == std::string("keys")
               ? L"Shift + \u2190 / \u2192 steers at full lock. The plain arrows stay the remote's D-pad."
               : L"The left stick tilts the remote to steer.");
    SliderRow(L"Tilt sensitivity", "controls.tilt_sensitivity", 0, 200, 10, L"%");
    SliderRow(L"Dead zone", "controls.deadzone", 0, 40, 2, L"%");
    SegRow(L"Steering curve", "controls.steer_curve", {{"linear", L"Linear"}, {"smooth", L"Smooth"}, {"sharp", L"Sharp"}});
    ToggleRow(L"Invert steering", "controls.invert_steer");
    ToggleRow(L"Rumble", "controls.rumble");
    EndRows();
    AsideTitle(L"Default buttons", kTop);
    const wchar_t* rows[][3] = {{L"Wii", L"Keyboard", L"Pad"},       {L"2", L"2", L"A"},
                                {L"1", L"1", L"B"},                  {L"D-pad", L"Arrows", L"D-pad"},
                                {L"Tilt", L"Shift + \u2190\u2192", L"Stick X"}, {L"+", L"", L"Start"},
                                {L"\u2212", L"", L"LB"},             {L"HOME", L"Enter", L"Back"},
                                {L"Menu", L"Esc, Tab", L"Hold Start"}};
    float y = kTop + 30;
    for (int i = 0; i < 9; i++) {
        Color c = i == 0 ? col::dim : col::fg;
        Font f = i == 0 ? F_DISPLAY_CAPS : F_BODY;
        Text(rows[i][0], f, 13, kAsideX, y, c);
        Text(rows[i][1], f, 13, kAsideX + 90, y, c);
        Text(rows[i][2], f, 13, kAsideX + 200, y, c);
        y += 24;
        if (i == 0) Line(kAsideX, y - 3, kAsideX + kAsideW, y - 3, col::rule, 1);
    }
    Note(L"Remapping comes in a later version.", y + 12);
}

void PageLanguage() {
    PickerRow(L"Language", "game.language", {"en"}, {L"English (from your disc)"},
              L"The languages your disc has. A USA disc has English.");
    SubHead(L"Game");
    ToggleRow(L"Skip intro movies", "game.skip_intro", L"Skips the opening movies only. The title screen stays.");
    ToggleRow(L"Pause in background", "game.pause_unfocused", L"Opens the Nice Crash menu when the game loses focus.");
    ToggleRow(L"Open menu with Tab", "game.menu_on_tab", L"Esc always opens it.");
    SegRow(L"Screenshot key", "game.screenshot_key",
           {{"F12", L"F12"}, {"F11", L"F11"}, {"PrintScreen", L"Print Screen"}, {"none", L"None"}},
           L"Saves a PNG in Pictures\\Nice Crash.");
    EndRows();
    AsideTitle(L"Language", kTop);
    TextWrap(L"The game's text comes from your disc. Changes here apply the next time the game starts.", F_BODY, 13,
             kAsideX, kTop + 26, kAsideW, col::dim, 1.5f);
}

void PageBuilds() {
    const State& s = A.st;
    float cy;
    {
        int idx = Row(L"Your build", L"", nullptr, 44, &cy);
        Text(s.built ? L"Nice Crash " NICECRASH_VERSION : L"Not built yet", F_BODY_SEMI, 15, kCtlX, cy, s.built ? col::fg : col::warn);
        std::wstring d = s.built ? (s.decompRef.empty() ? L"" : L"decomp " + ui::W(s.decompRef) + L" \u00B7 ") + L"built " + ui::W(s.builtWhen)
                                 : L"It takes about two minutes.";
        Text(d, F_BODY, 13, kCtlX, cy + 22, col::dim);
        float bw = TextWidth(s.built ? L"REBUILD" : L"BUILD", F_DISPLAY, 17, 0.12f) + 52;
        g.focusCount--;
        if (Button2(s.built ? L"Rebuild" : L"Build", kMainX + kMainW - bw, cy, nullptr, s.dump.verified) ||
            (Focused(idx) && g.in.accept && !g.consumed && s.dump.verified)) {
            g.consumed = true;
            StartChecklist(s.built);
        }
    }
    {
        Row(L"Disk use", L"The game, its build files and the compiler's cache. Rebuilds reuse the cache, so they're faster.",
            nullptr, 22, &cy);
        long long n = tasks::BuildDiskUse();
        Text(n < 0 ? L"Measuring…" : tasks::FormatBytes(n), F_BODY_SEMI, 15, kCtlX, cy + 2, n < 0 ? col::dim : col::fg);
    }
    {
        Row(L"Updates", L"Nice Crash has no update host yet, so nothing is checked. When it has one, updates "
                        L"arrive only with your consent and rebuild on this PC.",
            nullptr, 22, &cy);
        Text(L"Local build", F_BODY_SEMI, 15, kCtlX, cy + 2, col::fg);
    }
    ToggleRow(L"Check for updates", "launcher.check_updates", L"Only once Nice Crash has an update host.");
    SegRow(L"Channel", "launcher.channel", {{"stable", L"Stable"}, {"nightly", L"Nightly"}},
           Settings_Get("launcher.channel") == std::string("nightly") ? L"Untested. Back up your saves first." : L"");
    SegRow(L"When the game starts", "launcher.close_on_play", {{"1", L"Close the launcher"}, {"0", L"Minimise it"}},
           L"Minimise brings the launcher back when the game exits.");
    SubHead(L"Advanced");
    PickerRow(L"Renderer", "graphics.renderer", {"gl"}, {L"OpenGL"}, L"Only one backend for now.");
    EndRows();
    AsideTitle(L"Builds", kTop);
    TextWrap(L"Nice Crash is built on this PC from the decomp's source and your own disc. No game exe is downloaded.",
             F_BODY, 13, kAsideX, kTop + 26, kAsideW, col::dim, 1.5f);
    std::string root = tasks::RepoRoot();
    if (!root.empty()) Note(L"Source: this PC's checkout, " + ui::W(root), kTop + 110);
}

void PageLogs() {
    float y = g_rowY;
    int idx = g.focusCount;
    bool on = Settings_GetInt("launcher.error_log") != 0;
    TakeFocus(kMainX - 20, y, kMainW + 20, 70);
    if (Checkbox(&on, L"Save error logs", kMainX, y + 6, idx)) Settings_Set("launcher.error_log", on ? "1" : "0");
    TextWrap(L"Writes errors and warnings from the launcher and the game to a log file, to help track down problems. "
             L"Applies from the next time the game starts.",
             F_BODY, 13, kMainX + 36, y + 36, kMainW - 36, col::dim, 1.4f);
    float ny = y + 78;
    if (const char* ov = Settings_OverriddenBy("launcher.error_log")) {
        Text(L"Set by " + ui::W(ov) + L" for this session.", F_MONO, 11, kMainX + 36, ny - 6, col::tilt);
        ny += 14;
    }
    g_rowY = ny;
    Line(kMainX, g_rowY, kMainX + kMainW, g_rowY, col::rule, 1);
    g_rowY += 14;
    Text(L"LAST RUNS", F_DISPLAY_CAPS, 12, kMainX, g_rowY, col::dim, LEFT, 0.24f);
    g_rowY += 26;
    std::vector<tasks::Run> runs = tasks::Runs();
    if (runs.empty()) {
        Text(L"No runs yet. They show up here after you play.", F_BODY, 14, kMainX, g_rowY, col::dim);
        g_rowY += 30;
    }
    for (size_t i = 0; i < runs.size(); i++) {
        const tasks::Run& r = runs[i];
        Text(ui::W(r.when), F_MONO, 12.5f, kMainX, g_rowY + 2, col::fg);
        wchar_t len[32];
        swprintf(len, 32, L"%d:%02d", r.seconds / 60, r.seconds % 60);
        Text(len, F_MONO, 12.5f, kMainX + 170, g_rowY + 2, col::dim);
        Color c = r.status == "OK" ? col::ok : r.status == "Crashed" ? col::bad : col::warn;
        float dsz = FitSize(tasks::DescribeExit(r.code), F_BODY_SEMI, 14, kMainW - 240 - 70);
        Text(tasks::DescribeExit(r.code), F_BODY_SEMI, dsz, kMainX + 240, g_rowY, c);
        std::string log = tasks::RunLogPath((int)i);
        if (!log.empty()) {
            if (Link(L"Open", kMainX + kMainW, g_rowY - 1, 14, col::tilt, false, nullptr, RIGHT)) tasks::OpenPath(log);
        } else {
            Text(r.logged ? L"rotated out" : L"no log", F_BODY, 13, kMainX + kMainW, g_rowY + 1, col::dim, RIGHT);
        }
        g_rowY += 26;
    }
    g_rowY += 12;
    float bx = kMainX, bw;
    if (Button2(L"Open folder", bx, g_rowY, &bw)) tasks::OpenPath(tasks::LogDir());
    bx += bw + 24;
    if (Button2(L"Copy report", bx, g_rowY, &bw)) {
        if (tasks::CopyToClipboard(A.hwnd, tasks::BugReport())) Toast(L"Report copied. Paste it wherever you send it.");
    }
    bx += bw + 24;
    if (Button2(L"Save report", bx, g_rowY, &bw)) {
        std::string dir = tasks::SaveReport();
        if (dir.empty()) Toast(L"Couldn't write the report folder.");
        else {
            tasks::OpenPath(dir);
            Toast(L"Report saved. Nothing was sent; attach the folder wherever you report the problem.");
        }
    }
    g_rowY += 60;
    EndRows();
    AsideTitle(L"Logs", kTop);
    TextWrap(L"The log keeps the last five runs: nicecrash_log.txt and .1 to .4. A report holds the log, your settings "
             L"and build info, with your home folder and name removed. Nothing is uploaded; you choose where to send it.",
             F_BODY, 13, kAsideX, kTop + 26, kAsideW, col::dim, 1.5f);
    std::wstring p = ui::W(tasks::LogDir());
    if (const char* la = getenv("LOCALAPPDATA")) {
        std::wstring l = ui::W(la);
        if (p.compare(0, l.size(), l) == 0) p = L"%LOCALAPPDATA%" + p.substr(l.size());
    }
    Note(p, kTop + 160);
}

void PageAbout() {
    float y = g_rowY;
    Star(kMainX + 14, y + 22, 14, col::gold);
    Text(L"Nice Crash", F_DISPLAY, 40, kMainX + 40, y, col::fg);
    Text(L"version " NICECRASH_VERSION L" \u00B7 launcher " LAUNCHER_VERSION, F_MONO, 12.5f, kMainX + 40, y + 50, col::dim);
    y += 84;
    y += TextWrap(L"Nice Crash uses your own copy of the game. No game data is included.", F_BODY_SEMI, 16, kMainX, y,
                  kMainW, col::fg, 1.45f) + 14;
    struct Para { const wchar_t* head; std::wstring text; };
    std::wstring fonts = std::wstring(L"Interface fonts: ") + FontName(F_DISPLAY) + L", " + FontName(F_BODY) + L", " +
                         FontName(F_MONO) + L". Barlow, Barlow Condensed and JetBrains Mono (SIL OFL 1.1) are used when "
                                            L"bundled in assets\\fonts.";
    Para paras[] = {
        {L"Credits", L"Built from the community decompilation of the game on your disc, and its contributors' matching "
                     L"source. The launcher's art (star, tilt ring, truck) is our own."},
        {L"Licence", L"Unpublished. The licence is chosen before the first release."},
        {L"Fonts", fonts},
        {L"What Nice Crash connects to", L"Nothing, for now. Once an update host exists, the launcher fetches only its "
                                         L"signed manifest and source archives, and nothing is uploaded. Turn the check "
                                         L"off with launcher.check_updates = 0."},
    };
    for (auto& p : paras) {
        Text(Up(p.head), F_DISPLAY_CAPS, 12, kMainX, y, col::dim, LEFT, 0.24f);
        y += 22;
        y += TextWrap(p.text, F_BODY, 14, kMainX, y, kMainW, col::fg, 1.45f) + 14;
    }
    g_rowY = y;
    float bw;
    if (Button2(L"Open settings folder", kMainX, g_rowY, &bw)) tasks::OpenPath(portcfg::Dir());
    g_rowY += 60;
    EndRows();
    AsideTitle(L"Your settings", kTop);
    std::wstring p = ui::W(Settings_Path());
    if (const char* la = getenv("LOCALAPPDATA")) {
        std::wstring l = ui::W(la);
        if (p.compare(0, l.size(), l) == 0) p = L"%LOCALAPPDATA%" + p.substr(l.size());
    }
    TextWrap(L"The launcher and the in-game menu edit the same file. Only values you changed are written.", F_BODY, 13,
             kAsideX, kTop + 26, kAsideW, col::dim, 1.5f);
    Note(p, kTop + 90);
    // The emblem, small.
    Emblem(kAsideX + 50, kTop + 170, 200, fmodf(g_animClock * 9.f, 360.f));
}

const char* kPageSection[P_COUNT] = {nullptr, "display", "graphics", "audio", "controls", "game", nullptr, nullptr, nullptr};

void SettingsScreen() {
    // Navigation.
    FillRect(g.viewL, 52, 248 - g.viewL, 668, Alpha(col::hill, 0.55f));
    Line(248, 52, 248, 720, col::faint, 1);
    struct NavItem { int page; const wchar_t* label; const char* icon; };
    NavItem items[] = {{P_FILES, L"Game files", "disc"},     {P_DISPLAY, L"Display", "display"}, {P_GRAPHICS, L"Graphics", "graphics"},
                       {P_AUDIO, L"Audio", "audio"},         {P_CONTROLS, L"Controls", "pad"},   {P_LANGUAGE, L"Language", "language"},
                       {P_BUILDS, L"Builds", "builds"},      {P_LOGS, L"Logs", "logs"},          {P_ABOUT, L"About", "about"}};
    float ny = 52 + 26;
    for (int i = 0; i < P_COUNT; i++) {
        if (i == 0 || i == 6) {
            Text(i == 0 ? L"GAME" : L"NICE CRASH", F_DISPLAY_CAPS, 11, 28, ny + 14, Alpha(col::dim, 0.75f), LEFT, 0.26f);
            ny += 34;
        }
        bool on = A.page == items[i].page;
        bool hov = Hover(0, ny, 248, 38);
        if (on) {
            Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(0, 0), Gdiplus::PointF(248, 0), Alpha(col::gold, 0.14f), Alpha(col::gold, 0));
            g.g->FillRectangle(&lg, 0.f, ny, 248.f, 38.f);
            PushSkew(12, ny + 19, -14);
            FillRect(10, ny + 8, 5, 22, col::gold);
            Pop();
        }
        Color c = on || hov ? col::fg : col::dim;
        Icon(items[i].icon, 28, ny + 11, 16, c, 1.7f);
        Text(items[i].label, F_BODY_SEMI, 15, 56, ny + 10, c);
        if (hov) g.hotCursor = 1;
        if (Clicked(0, ny, 248, 38) && !g.consumed) { g.consumed = true; GoPage(items[i].page); }
        ny += 38;
    }
    if (Link(L"Back to home", 28, 720 - 48, 15, col::tilt, false)) Go(HOME);
    Icon("back", 12, 720 - 44, 12, col::tilt, 2.4f);

    // Page title.
    const wchar_t* titles[P_COUNT] = {L"Game files", L"Display", L"Graphics", L"Audio", L"Controls", L"Language",
                                      L"Builds", L"Logs", L"About"};
    Text(titles[A.page], F_DISPLAY, 42, kMainX, 52 + 26 + 10, col::fg);
    {
        // Keyboard or pad glyphs, by the last device used.
        const wchar_t* k1 = g_padLast ? L"LB" : L"Q";
        const wchar_t* k2 = g_padLast ? L"RB" : L"E";
        float w2 = std::max(24.f, TextWidth(k2, F_DISPLAY_CAPS, 12, 0.06f) + 12);
        float w1 = std::max(24.f, TextWidth(k1, F_DISPLAY_CAPS, 12, 0.06f) + 12);
        float kx = kMainX + kMainW, kw;
        Key(k2, kx - w2, 52 + 26 + 22, &kw);
        Key(k1, kx - w2 - 4 - w1, 52 + 26 + 22, &kw);
        Text(L"page", F_BODY_SEMI, 12, kx - w2 - 4 - w1 - 12, 52 + 26 + 25, col::dim, RIGHT);
    }

    // Rows (scrolling).
    g_rowRects.clear();
    // Rows are clipped to the scroll area; each page draws its rows first, then calls EndRows() for its aside.
    PushClip(kMainX - 40, kTop - 2, kMainW + 44, kBottom - kTop + 4);
    g_rowsClipped = true;
    g_rowY = kTop - A.scroll;
    if (A.page != P_LOGS && A.page != P_ABOUT) Line(kMainX, g_rowY, kMainX + kMainW, g_rowY, col::rule, 1);
    int firstIdx = g.focusCount;
    switch (A.page) {
    case P_FILES: PageFiles(); break;
    case P_DISPLAY: PageDisplay(); break;
    case P_GRAPHICS: PageGraphics(); break;
    case P_AUDIO: PageAudio(); break;
    case P_CONTROLS: PageControls(); break;
    case P_LANGUAGE: PageLanguage(); break;
    case P_BUILDS: PageBuilds(); break;
    case P_LOGS: PageLogs(); break;
    case P_ABOUT: PageAbout(); break;
    }
    EndRows();
    if (g.focusCount > firstIdx) DefaultFocus(firstIdx);
    float contentH = g_rowY + A.scroll - kTop;
    float maxScroll = std::max(0.f, contentH - (kBottom - kTop));
    // Keep the focused row visible; the wheel scrolls (it never changes a value).
    if (g.focusVisible)
        for (auto& r : g_rowRects)
            if (r.first == g.focus) {
                float ry = r.second.first - kTop, rh = r.second.second;
                if (ry < A.scroll) A.scroll = ry;
                if (ry + rh > A.scroll + (kBottom - kTop)) A.scroll = ry + rh - (kBottom - kTop);
            }
    if (g.in.wheel != 0 && g.mx > 248) A.scroll -= g.in.wheel * 60;
    A.scroll = std::max(0.f, std::min(maxScroll, A.scroll));
    if (maxScroll > 0) {
        float th = (kBottom - kTop) * (kBottom - kTop) / contentH;
        float ty = kTop + (kBottom - kTop - th) * (A.scroll / maxScroll);
        FillRound(kMainX - 30, ty, 3, th, 1.5f, col::gold);
    }

    // Footer.
    float fy = 720 - 26 - 44, bw;
    float doneW = TextWidth(L"DONE", F_DISPLAY, 17, 0.12f) + 52;
    float doneX = 1280 - 56 - doneW;
    if (kPageSection[A.page]) {
        if (Link(L"Reset this page", doneX - 24, fy + 10, 15, col::tilt, false, nullptr, RIGHT)) {
            Settings_ResetSection(kPageSection[A.page]);
            if (A.page == P_LANGUAGE) Settings_Reset("game.language");
            Toast(L"Back to the defaults for this page.");
        }
    }
    if (Button2(L"Done", doneX, fy, &bw)) Go(HOME);
    Text(L"Changes save to settings.ini as you make them.", F_BODY, 13, kMainX, fy + 13, col::dim);

    if (g.in.prevTab && !g.consumed) { g.consumed = true; GoPage(A.page - 1); }
    if (g.in.nextTab && !g.consumed) { g.consumed = true; GoPage(A.page + 1); }
    if ((g.in.back || g.in.rclick) && !g.consumed) { g.consumed = true; Go(HOME); }
}

// ---------------------------------------------------------------- frame and window

long long g_tStart, g_tBack, g_tScreen;
void Frame(Gdiplus::Graphics* gr, int w, int h, const Input& in) {
    LARGE_INTEGER qs; QueryPerformanceCounter(&qs); g_tStart = qs.QuadPart;
    BeginFrame(gr, w, h, (float)Now(), in);
    gr->ResetTransform();
    {
        Gdiplus::SolidBrush b(col::skyMid);
        gr->FillRectangle(&b, 0, 0, w, h);
    }
    gr->TranslateTransform(g.ox, g.oy);
    gr->ScaleTransform(g.scale, g.scale);
    if (A.screen == HOME) Backdrop([](float L, float sx) {
        g_runL = L;
        g_runSx = sx;
        truckrun::Draw(L, sx);
    });
    else Backdrop();
    LARGE_INTEGER qa; QueryPerformanceCounter(&qa); g_tBack = qa.QuadPart;
    TitleBar();
    switch (A.screen) {
    case HOME: HomeScreen(); break;
    case DISC: DiscScreen(); break;
    case FIRSTRUN: FirstRunScreen(); break;
    case SETTINGS: SettingsScreen(); break;
    }
    LARGE_INTEGER qb; QueryPerformanceCounter(&qb); g_tScreen = qb.QuadPart;
    DrawToast();
    if (A.wantFocus >= 0) { g.focus = A.wantFocus; A.wantFocus = -1; }
    EndFrame();
}

// Back buffer: a 32-bit DIB that GDI+ draws into.
struct BackBuffer {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    void* bits = nullptr;
    int w = 0, h = 0;
    void Ensure(HDC ref, int nw, int nh) {
        if (nw == w && nh == h && dc) return;
        Free();
        w = nw;
        h = nh;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(ref);
        bmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        old = (HBITMAP)SelectObject(dc, bmp);
    }
    void Free() {
        if (dc) { SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); }
        dc = nullptr;
        bmp = nullptr;
    }
} BB;

Input g_in;           // mouse state, accumulated between frames
bool g_mouseTracked = false;

// Keyboard and pad presses are queued and handed to the UI one per frame, so presses that arrive during a slow
// frame stay separate and in order.
enum Nav { N_UP, N_DN, N_LEFT, N_RIGHT, N_ACCEPT, N_BACK, N_START, N_SETTINGS, N_RESET, N_PREV, N_NEXT };
std::vector<int> g_nav;
void PushNav(int n) {
    if (g_nav.size() < 16) g_nav.push_back(n);
}
void ApplyNav(Input& in, int n) {
    switch (n) {
    case N_UP: in.up = true; break;
    case N_DN: in.dn = true; break;
    case N_LEFT: in.left = true; break;
    case N_RIGHT: in.right = true; break;
    case N_ACCEPT: in.accept = true; break;
    case N_BACK: in.back = true; break;
    case N_START: in.start = true; break;
    case N_SETTINGS: in.settings = true; break;
    case N_RESET: in.reset = true; break;
    case N_PREV: in.prevTab = true; break;
    case N_NEXT: in.nextTab = true; break;
    }
}

bool g_dirty = true;  // something changed (input, size, a message): the next frame repaints everything
double g_nextFull = 0;

// Screens that change on their own every frame (spinners, timers, a fading toast) repaint fully while they do.
bool ContinuousFull() {
    if (A.screen == FIRSTRUN) return true;
    if (A.screen == DISC && tasks::DiscScanRunning()) return true;
    if (!A.toast.empty() && Now() < A.toastUntil + 0.1) return true;
    if (A.screen == SETTINGS && A.page == P_BUILDS && tasks::BuildDiskUse() < 0) return true;
    return false;
}

// Window-pixel rectangle from canvas units, padded by 2 px.
RECT CanvasToWindow(float x0, float y0, float x1, float y1) {
    RECT r;
    r.left = (LONG)floorf(g.ox + x0 * g.scale) - 2;
    r.top = (LONG)floorf(g.oy + y0 * g.scale) - 2;
    r.right = (LONG)ceilf(g.ox + x1 * g.scale) + 2;
    r.bottom = (LONG)ceilf(g.oy + y1 * g.scale) + 2;
    return r;
}

// What moves on Home right now: the truck and its dust, and the emblem's turning star ring.
std::vector<RECT> MovingAreas() {
    std::vector<RECT> r;
    float x0, y0, x1, y1;
    if (truckrun::Bounds(g_runL, g_runSx, &x0, &y0, &x1, &y1)) r.push_back(CanvasToWindow(x0, y0, x1, y1));
    // The star ring turns 9 degrees a second: repainting it 10 times a second looks the same as 30 and costs a third.
    static int tick = 0;
    if (tick++ % 3 == 0) {
        const float heroCx = 1280 - 56 - 220;
        r.push_back(CanvasToWindow(heroCx - 182, 56, heroCx + 182, 58 + 362));
    }
    return r;
}

// full = repaint the whole window; otherwise only the moving areas (Home animation), before and after this step.
void Render(HWND hw, bool full = true) {
    RECT rc;
    GetClientRect(hw, &rc);
    int w = std::max(1, (int)rc.right), h = std::max(1, (int)rc.bottom);
    HDC wdc = GetDC(hw);
    if (BB.w != w || BB.h != h) full = true;
    BB.Ensure(wdc, w, h);
    std::vector<RECT> dirty;
    if (!full) dirty = MovingAreas();  // where things were
    AdvanceAnimation();
    if (!full) {
        std::vector<RECT> now = MovingAreas();  // where they are now
        dirty.insert(dirty.end(), now.begin(), now.end());
    }
    {
        Input in = g_in;
        // A screen that just opened gets one frame without key input to put its default focus in place.
        // Applied at frame start, so the rest of the old screen's frame can't take the new screen's focus.
        bool settling = A.resetFocus || A.pendingFocus;
        A.resetFocus = settling;
        A.pendingFocus = false;
        if (!settling && !g_nav.empty()) {
            ApplyNav(in, g_nav.front());
            g_nav.erase(g_nav.begin());
        }
        Gdiplus::Graphics gr(BB.dc);
        if (!full) {
            Gdiplus::Region clip;
            clip.MakeEmpty();
            for (auto& r : dirty) clip.Union(Gdiplus::Rect(r.left, r.top, r.right - r.left, r.bottom - r.top));
            gr.SetClip(&clip);
        }
        LARGE_INTEGER q0, q1, fq;
        QueryPerformanceCounter(&q0);
        Frame(&gr, w, h, in);
        QueryPerformanceCounter(&q1);
        QueryPerformanceFrequency(&fq);
        static double tSum = 0;
        static int tN = 0;
        static double tB = 0, tS = 0;
        if (!full && getenv("NICECRASH_FRAMETIME")) {  // debugging: average partial-frame time
            tSum += (double)(q1.QuadPart - q0.QuadPart) * 1000 / fq.QuadPart;
            tB += (double)(g_tBack - g_tStart) * 1000 / fq.QuadPart;
            tS += (double)(g_tScreen - g_tBack) * 1000 / fq.QuadPart;
            if (++tN % 100 == 0) {
                if (FILE* df = fopen(getenv("NICECRASH_FRAMETIME"), "a")) {
                    fprintf(df, "partial frame %.2f ms (backdrop %.2f, screen %.2f; %d frames)\n", tSum / tN, tB / tN,
                            tS / tN, tN);
                    fclose(df);
                }
            }
        }
        if (settling) A.resetFocus = false;  // even if the screen had nothing to focus
    }
    if (full) {
        BitBlt(wdc, 0, 0, w, h, BB.dc, 0, 0, SRCCOPY);
    } else {
        for (auto& r : dirty) {
            RECT c;
            RECT all{0, 0, w, h};
            if (IntersectRect(&c, &r, &all))
                BitBlt(wdc, c.left, c.top, c.right - c.left, c.bottom - c.top, BB.dc, c.left, c.top, SRCCOPY);
        }
    }
    ReleaseDC(hw, wdc);
    g_dirty = false;
    // Clear the one-frame events.
    Input keep;
    keep.mx = g_in.mx;
    keep.my = g_in.my;
    keep.down = g_in.down;
    g_in = keep;
}

// XInput, loaded at run time (xinput1_4.dll ships with Windows 10+).
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XINPUT_STATE*);
XInputGetStateFn g_xget = nullptr;
WORD g_padPrev = 0;
float g_stickRepeat = 0;
int g_stickDir = 0;

void PollPad(float dt) {
    if (!g_xget) return;
    XINPUT_STATE s{};
    if (g_xget(0, &s) != ERROR_SUCCESS) { g_padPrev = 0; return; }
    if (GetForegroundWindow() != A.hwnd) { g_padPrev = s.Gamepad.wButtons; return; }
    WORD b = s.Gamepad.wButtons, edge = b & ~g_padPrev;
    g_padPrev = b;
    if (edge) g_padLast = true;
    if (edge & XINPUT_GAMEPAD_DPAD_UP) PushNav(N_UP);
    if (edge & XINPUT_GAMEPAD_DPAD_DOWN) PushNav(N_DN);
    if (edge & XINPUT_GAMEPAD_DPAD_LEFT) PushNav(N_LEFT);
    if (edge & XINPUT_GAMEPAD_DPAD_RIGHT) PushNav(N_RIGHT);
    if (edge & XINPUT_GAMEPAD_A) PushNav(N_ACCEPT);
    if (edge & XINPUT_GAMEPAD_B) PushNav(N_BACK);
    if (edge & XINPUT_GAMEPAD_START) PushNav(N_START);
    if (edge & XINPUT_GAMEPAD_Y) PushNav(N_SETTINGS);
    if (edge & XINPUT_GAMEPAD_X) PushNav(N_RESET);
    if (edge & XINPUT_GAMEPAD_LEFT_SHOULDER) PushNav(N_PREV);
    if (edge & XINPUT_GAMEPAD_RIGHT_SHOULDER) PushNav(N_NEXT);
    // The stick works like the D-pad, with repeat.
    int dir = 0;
    short lx = s.Gamepad.sThumbLX, ly = s.Gamepad.sThumbLY;
    if (ly > 16000) dir = 1;
    else if (ly < -16000) dir = 2;
    else if (lx < -16000) dir = 3;
    else if (lx > 16000) dir = 4;
    if (dir != g_stickDir) { g_stickDir = dir; g_stickRepeat = 0.4f; if (dir) goto fire; }
    else if (dir && (g_stickRepeat -= dt) <= 0) { g_stickRepeat = 0.12f; goto fire; }
    return;
fire:
    g_padLast = true;
    if (dir == 1) PushNav(N_UP);
    if (dir == 2) PushNav(N_DN);
    if (dir == 3) PushNav(N_LEFT);
    if (dir == 4) PushNav(N_RIGHT);
}

// Per-monitor DPI (Windows 10 1703+), looked up at run time so older systems just get bitmap scaling.
int WindowDpi(HWND hw) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    return fn && hw ? (int)fn(hw) : 96;
}

void MakeDpiAware() {
    typedef BOOL(WINAPI * Fn)(HANDLE);
    if (Fn fn = (Fn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {  // anything that can change what's on screen asks for a full repaint
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP: case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_SIZE: case WM_DPICHANGED: case WM_DROPFILES:
    case WM_ACTIVATE: case WM_SHOWWINDOW: case WM_SETTINGCHANGE: case WM_APP + 1:
        g_dirty = true;
        break;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) {
            if (IsZoomed(hw)) {  // a maximised frameless window overhangs the screen by its frame
                auto* p = (NCCALCSIZE_PARAMS*)lp;
                int f = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += f;
                p->rgrc[0].top += f;
                p->rgrc[0].right -= f;
                p->rgrc[0].bottom -= f;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(hw, &p);
        RECT rc;
        GetClientRect(hw, &rc);
        int b = MulDiv(6, WindowDpi(hw), 96);
        if (!IsZoomed(hw)) {
            bool l = p.x < b, r = p.x >= rc.right - b, t = p.y < b, bo = p.y >= rc.bottom - b;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (bo && l) return HTBOTTOMLEFT;
            if (bo && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (bo) return HTBOTTOM;
        }
        for (RECT* r : {&A.tbGear, &A.tbMin, &A.tbClose})
            if (PtInRect(r, p)) return HTCLIENT;
        if (p.y < A.tbBottom) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        int dpi = WindowDpi(hw);
        mm->ptMinTrackSize.x = MulDiv(1024, dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(576, dpi, 96);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hw, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hw, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hw, &ps);
        EndPaint(hw, &ps);
        Render(hw);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        g_in.mx = (short)LOWORD(lp);
        g_in.my = (short)HIWORD(lp);
        g_in.moved = true;
        if (!g_mouseTracked) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hw, 0};
            TrackMouseEvent(&t);
            g_mouseTracked = true;
        }
        return 0;
    case WM_MOUSELEAVE:
        g_mouseTracked = false;
        if (!g_in.down) g_in.mx = g_in.my = -10000;
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        g_in.pressed = g_in.down = true;
        g_in.mx = (short)LOWORD(lp);
        g_in.my = (short)HIWORD(lp);
        SetCapture(hw);
        return 0;
    case WM_LBUTTONUP:
        g_in.released = true;
        g_in.down = false;
        g_in.mx = (short)LOWORD(lp);
        g_in.my = (short)HIWORD(lp);
        ReleaseCapture();
        return 0;
    case WM_RBUTTONUP:
        g_in.rclick = true;
        return 0;
    case WM_MOUSEWHEEL:
        g_in.wheel += GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, g.hotCursor ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        g_padLast = false;
        switch (wp) {
        case VK_UP: PushNav(N_UP); break;
        case VK_DOWN: PushNav(N_DN); break;
        case VK_TAB: PushNav(GetKeyState(VK_SHIFT) < 0 ? N_UP : N_DN); break;
        case VK_LEFT: PushNav(N_LEFT); break;
        case VK_RIGHT: PushNav(N_RIGHT); break;
        case VK_RETURN: case VK_SPACE: PushNav(N_ACCEPT); break;
        case VK_ESCAPE: case VK_BACK: PushNav(N_BACK); break;
        case 'Q': case VK_PRIOR: PushNav(N_PREV); break;
        case 'E': case VK_NEXT: PushNav(N_NEXT); break;
        case 'R': PushNav(N_RESET); break;
        }
        return 0;
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        wchar_t p[MAX_PATH * 2];
        if (DragQueryFileW(d, 0, p, MAX_PATH * 2)) {
            int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
            std::string s(n ? n - 1 : 0, 0);
            WideCharToMultiByte(CP_UTF8, 0, p, -1, &s[0], n, nullptr, nullptr);
            if (A.screen != DISC) Go(DISC);
            AddPickedFolder(s);
        }
        DragFinish(d);
        SetForegroundWindow(hw);
        return 0;
    }
    case WM_GAME_EXIT: {
        A.gameRunning = false;
        A.runsLoaded = false;
        if (!IsWindowVisible(hw) || Settings_GetInt("launcher.close_on_play")) {
            // "Close the launcher": the run is recorded, so leave now.
            PostQuitMessage(0);
            return 0;
        }
        Settings_Reload();  // the in-game menu may have changed settings
        Go(HOME);
        ShowWindow(hw, SW_RESTORE);
        SetForegroundWindow(hw);
        if (wp != 0) Toast(L"The game stopped unexpectedly. See Last run.");
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hw);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Writes the 32-bit back buffer to a .bmp (for --ui-shot).
bool SaveBmp(const std::string& path) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof ih;
    ih.biWidth = BB.w;
    ih.biHeight = -BB.h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biSizeImage = BB.w * BB.h * 4;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(BB.bits, ih.biSizeImage, 1, f);
    fclose(f);
    return true;
}

std::wstring FontDir() {
    std::string root = tasks::RepoRoot();
    std::string d = root.empty() ? tasks::ExeDir() + "fonts" : root + "\\assets\\fonts";
    return ui::W(d);
}

}  // namespace

// Renders one screen offscreen and saves it, for checking the layout without clicking:
//   NiceCrash --ui-shot <home|disc|firstrun|settings:<page>> <out.bmp> [width height]
int LauncherShot(const std::string& which, const std::string& out, int w, int h) {
    MakeDpiAware();
    A.t0 = GetTickCount64();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ui::Init(FontDir());
    A.st = tasks::Refresh();
    // NICECRASH_TRUCKRUN_T=<seconds>: run the Home truck run that far first (e.g. 9.4 is mid-flip over the pines).
    if (const char* t = getenv("NICECRASH_TRUCKRUN_T")) {
        float secs = (float)atof(t);
        for (float x = 0; x < secs; x += 1.f / 30) truckrun::Step(1.f / 30);
        g_animClock = secs;
        printf("truck run after %.1f s: %s, %.0f px/s\n", secs, truckrun::Mode(), truckrun::Speed());
    }
    const char* pages[] = {"files", "display", "graphics", "audio", "controls", "language", "builds", "logs", "about"};
    if (which == "home") A.screen = HOME;
    else if (which == "disc") A.screen = DISC;
    else if (which == "firstrun") { A.screen = FIRSTRUN; A.demoChecklist = true; }
    else if (which.rfind("settings:", 0) == 0) {
        A.screen = SETTINGS;
        for (int i = 0; i < P_COUNT; i++)
            if (which.substr(9) == pages[i]) A.page = i;
    } else {
        fprintf(stderr, "unknown screen %s\n", which.c_str());
        return 2;
    }
    HDC sdc = GetDC(nullptr);
    BB.Ensure(sdc, w, h);
    ReleaseDC(nullptr, sdc);
    Input in;
    for (int i = 0; i < 40; i++) {  // a few frames, so async work (the disc search) can land
        Gdiplus::Graphics gr(BB.dc);
        Frame(&gr, w, h, in);
        if (A.screen == DISC && tasks::DiscScanRunning()) Sleep(100);
        else if (i > 2) break;
    }
    bool ok = SaveBmp(out);
    printf("%s %s (fonts: %ls / %ls / %ls)\n", ok ? "saved" : "could not write", out.c_str(), FontName(F_DISPLAY),
           FontName(F_BODY), FontName(F_MONO));
    printf("dump: configured %d found %d verified %d imported %d (%s) built %d; settings %s\n", A.st.haveDump,
           A.st.dump.found, A.st.dump.verified, A.st.imported, A.st.dump.why.c_str(), A.st.built, Settings_Path());
    ui::Shutdown();
    return ok ? 0 : 1;
}

// Starts the game exactly as PLAY does, waits up to `seconds` (then ends it), and prints how the run was
// recorded. For checking the launch path without clicking: NiceCrash --launch-game [seconds]
int LauncherLaunchTest(int seconds) {
    std::string err;
    if (!tasks::StartGame(err)) {
        printf("could not start: %s\n", err.c_str());
        return 1;
    }
    printf("started %s\n", tasks::GameExe().c_str());
    ULONGLONG until = GetTickCount64() + seconds * 1000ULL;
    while (tasks::GameRunning() && GetTickCount64() < until) Sleep(100);
    if (tasks::GameRunning()) {
        printf("still running after %d s; ending it\n", seconds);
        tasks::StopGame();
        while (tasks::GameRunning()) Sleep(50);
    }
    Sleep(200);  // the wait thread records the run just before it clears the running flag
    std::vector<tasks::Run> runs = tasks::Runs();
    if (!runs.empty())
        printf("recorded: %s, %d s, %ls, log %s\n", runs[0].when.c_str(), runs[0].seconds,
               tasks::DescribeExit(runs[0].code).c_str(), runs[0].logged ? "kept" : "off");
    return 0;
}

int LauncherMain() {
    MakeDpiAware();
    A.t0 = GetTickCount64();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!ui::Init(FontDir())) {
        MessageBoxA(nullptr, "GDI+ failed to start.", PROJECT_NAME, MB_ICONERROR);
        return 1;
    }
    A.st = tasks::Refresh();
    if (!A.st.haveDump) A.screen = DISC;
    if (HMODULE x = LoadLibraryA("xinput1_4.dll")) g_xget = (XInputGetStateFn)GetProcAddress(x, "XInputGetState");

    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));  // assets/icon
    wc.lpszClassName = L"NiceCrashLauncher";
    RegisterClassW(&wc);
    // 1280x720 at 100% scaling, grown with the main monitor's DPI and kept inside its work area.
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    HDC sdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(nullptr, sdc);
    int w = MulDiv(1280, dpi, 96), h = MulDiv(720, dpi, 96);
    int maxW = wa.right - wa.left - 40, maxH = wa.bottom - wa.top - 40;
    if (w > maxW || h > maxH) {
        float k = std::min((float)maxW / w, (float)maxH / h);
        w = (int)(w * k);
        h = (int)(h * k);
    }
    int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 2;
    A.hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, wc.lpszClassName, L"Nice Crash",
                             WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU, x, y, w, h,
                             nullptr, nullptr, wc.hInstance, nullptr);
    tasks::SetGameExitWindow(A.hwnd, WM_GAME_EXIT);
    ShowWindow(A.hwnd, SW_SHOW);
    UpdateWindow(A.hwnd);

    ULONGLONG last = GetTickCount64();
    bool quit = false;
    while (!quit) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) quit = true;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (quit) break;
        ULONGLONG now = GetTickCount64();
        float dt = (now - last) / 1000.f;
        last = now;
        if (IsWindowVisible(A.hwnd) && !IsIconic(A.hwnd)) {
            PollPad(dt);
            // Repaint everything only when something changed or a screen shows live progress; while Home animates,
            // repaint just what moves; otherwise draw nothing (an idle launcher costs next to no CPU). A full
            // repaint every 2 s catches any change that arrived without a message.
            double t = Now();
            bool full = g_dirty || !g_nav.empty() || ContinuousFull() || t >= g_nextFull;
            bool moving = A.screen == HOME && AnimationOn() && !tasks::GameRunning();
            if (full) {
                Render(A.hwnd, true);
                g_nextFull = t + 2;
            } else if (moving) {
                Render(A.hwnd, false);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);
        } else {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
        }
    }
    Settings_Flush();
    BB.Free();
    ui::Shutdown();
    CoUninitialize();
    return 0;
}
