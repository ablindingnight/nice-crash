// Nice Crash launcher: drawing and immediate-mode widgets on GDI+ (a Windows system library).
// Everything is laid out on the design canvas, 1280x720 (handoff mockups/launcher_home.html), scaled to the window.
// Theme: "Dusk Rally", purple (mockups/nc_theme.css). Our own art only.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
#include <functional>
#include <string>
#include <vector>

namespace ui {

using Gdiplus::Color;
typedef Gdiplus::RectF Rect;

// nc_theme.css tokens.
namespace col {
const Color skyTop(0xFF, 0x0E, 0x08, 0x20), skyMid(0xFF, 0x1F, 0x11, 0x39), panel(0xFF, 0x1A, 0x0F, 0x2E),
    panelHi(0xFF, 0x26, 0x18, 0x45), hill(0xFF, 0x07, 0x03, 0x0E), glow(0xFF, 0xFF, 0x7A, 0x2E),
    fg(0xFF, 0xF4, 0xEF, 0xFB), dim(0xFF, 0xA9, 0x9B, 0xC6), faint(0xFF, 0x3B, 0x2B, 0x5B),
    rule(43, 0xFF, 0xC2, 0x2E), boost(0xFF, 0xFF, 0x5A, 0x1F), gold(0xFF, 0xFF, 0xC2, 0x2E),
    tilt(0xFF, 0x4C, 0xC4, 0xFF), ok(0xFF, 0x5F, 0xE3, 0x9A), warn(0xFF, 0xFF, 0xB0, 0x2E), bad(0xFF, 0xFF, 0x4D, 0x5E),
    playFg(0xFF, 0x2A, 0x10, 0x00), chip(26, 0x4C, 0xC4, 0xFF), onGold(0xFF, 0x24, 0x10, 0x00);
}
Color Alpha(const Color& c, float a);  // c with its alpha times a
Color Mix(const Color& a, const Color& b, float t);

enum Font {
    F_DISPLAY,       // Barlow Condensed 800-900 italic (headings, PLAY)
    F_DISPLAY_CAPS,  // Barlow Condensed 700 (eyebrows, small caps labels)
    F_BODY,          // Barlow 400-500
    F_BODY_SEMI,     // Barlow 600
    F_BODY_BOLD,     // Barlow 700
    F_MONO,          // JetBrains Mono
    F_COUNT
};

enum Align { LEFT, CENTER, RIGHT };

// Input gathered by the window procedure for one frame, in window pixels.
struct Input {
    float mx = -1, my = -1;   // mouse
    bool moved = false;       // the mouse moved this frame
    bool pressed = false;     // left button went down this frame
    bool released = false;    // left button went up this frame
    bool down = false;
    bool rclick = false;
    float wheel = 0;          // notches, positive = up
    // Navigation, from keyboard and pad.
    bool up = false, dn = false, left = false, right = false, accept = false, back = false;
    bool start = false, settings = false, prevTab = false, nextTab = false, reset = false;
};

struct Ctx {
    Gdiplus::Graphics* g = nullptr;
    int winW = 1280, winH = 720;
    float scale = 1, ox = 0, oy = 0;    // canvas -> window
    float viewL = 0, viewR = 1280;      // the canvas x range the window shows (wider windows show more sky)
    float time = 0;                     // seconds since start, for animation
    Input in;
    float mx = -1, my = -1;             // mouse in canvas units
    // Focus: every focusable widget takes the next index; the focused one gets keyboard/pad input.
    int focus = 0, focusCount = 0, prevFocusCount = 0;
    bool focusVisible = false;          // show the focus ring (after keyboard/pad use)
    int hotCursor = 0;                  // 1 = hand cursor wanted this frame
    bool consumed = false;              // an action ran this frame
};
extern Ctx g;

bool Init(const std::wstring& fontDir);  // GDI+ start, fonts (Barlow from fontDir if present, else system fonts)
void Shutdown();
const wchar_t* FontName(Font f);

void BeginFrame(Gdiplus::Graphics* gr, int w, int h, float t, const Input& in);
void EndFrame();

std::wstring W(const std::string& utf8);

// Drawing, canvas units.
void FillRect(float x, float y, float w, float h, const Color& c);
void FillRound(float x, float y, float w, float h, float r, const Color& c);
void StrokeRound(float x, float y, float w, float h, float r, const Color& c, float width);
void FillRoundGrad(float x, float y, float w, float h, float r, const Color& a, const Color& b, float angleDeg);
void Line(float x0, float y0, float x1, float y1, const Color& c, float width);
void Circle(float cx, float cy, float r, const Color& fill);
void Ring(float cx, float cy, float r, const Color& c, float width);
void Arc(float cx, float cy, float r, float startDeg, float sweepDeg, const Color& c, float width);
void Star(float cx, float cy, float r, const Color& c);
void PushSkew(float cx, float cy, float deg);  // CSS skewX(deg) around (cx, cy)
void PushClip(float x, float y, float w, float h);
void Pop();
// A small SVG-path subset (M L H V C S Z, absolute and relative) for icons and art.
Gdiplus::GraphicsPath* SvgPath(const char* d);
// Stroke an icon path drawn on a 16-unit grid at (x, y), size px.
void Icon(const char* name, float x, float y, float size, const Color& c, float stroke = 1.7f);

float TextWidth(const std::wstring& s, Font f, float size, float tracking = 0);
// y is the top of the line; returns the width drawn.
float Text(const std::wstring& s, Font f, float size, float x, float y, const Color& c, Align a = LEFT,
           float tracking = 0);
// Wrapped in width w; returns the height used.
float TextWrap(const std::wstring& s, Font f, float size, float x, float y, float w, const Color& c,
               float lineHeight = 1.4f, bool draw = true);
// Cuts the front off with "…" so it fits in maxW (paths keep their most telling end).
std::wstring Ellipsize(const std::wstring& s, Font f, float size, float maxW);
// Shrinks to fit in maxW (one line); returns the size used.
float FitSize(const std::wstring& s, Font f, float size, float maxW, float tracking = 0);
void TextGradient(const std::wstring& s, Font f, float size, float cx, float y, const Color& top, const Color& bottom,
                  float tracking);

// Widgets. Each focusable one takes a focus index; it returns true when activated (click or Accept).
bool Hover(float x, float y, float w, float h);
int TakeFocus(float x, float y, float w, float h);  // returns the index; mouse movement over it focuses it
bool Focused(int idx);
bool Clicked(float x, float y, float w, float h);   // mouse released inside, pressed inside
void FocusRing(float x, float y, float w, float h, float r = 4);

bool PlayButton(const std::wstring& label, float x, float y, float w, float h, bool icon = true, bool arrowAfter = false);
bool Button2(const std::wstring& label, float x, float y, float* wOut = nullptr, bool enabled = true);  // ghost
bool Link(const std::wstring& label, float x, float y, float size = 15, const Color& c = col::tilt, bool chev = true,
          float* wOut = nullptr, Align a = LEFT);
bool NavLink(const std::wstring& label, const char* icon, float x, float y, float* wOut = nullptr);
void Badge(const std::wstring& label, float x, float y, const Color& c, bool tick);
float BadgeWidth(const std::wstring& label, bool tick);
void Key(const std::wstring& label, float x, float y, float* wOut = nullptr);
void Eyebrow(const std::wstring& label, float x, float y, const Color& c = col::gold);
void Meter(float x, float y, float w, float h, float frac);

// Settings controls (value widgets). They draw at (x, y) with the given width/height and return true on change.
// focusIdx: the row's focus index (Left/Right change the value while the row is focused).
bool Seg(const std::vector<std::wstring>& labels, int* sel, float x, float y, int focusIdx, float h = 34);
bool Toggle(bool* on, float x, float y, int focusIdx);
bool Slider(int* v, int lo, int hi, int step, float x, float y, float w, int focusIdx, const wchar_t* unit);
bool Picker(const std::vector<std::wstring>& labels, int* sel, float x, float y, float w, int focusIdx);
bool Checkbox(bool* on, const std::wstring& label, float x, float y, int focusIdx);

// The window background: sky, glow, hills and the tread strip. `mid` (optional) draws between the far range and the
// front hill, with the canvas transform; the hills' x maps to canvas L + x * sx and their y to 460 + y.
void Backdrop(const std::function<void(float L, float sx)>& mid = nullptr);
// The emblem: tilt ring, star, truck (our own art, theme_sheet.html shapes). Box size s at (x, y).
void Emblem(float x, float y, float s, float spinDeg);
void Truck(float x, float y, float w, float h);  // the truck symbol fitted in a box
// The NICE CRASH wordmark, "C · Speed stack" (design section 2): its 440x120 box drawn w wide at (x, y).
void Wordmark(float x, float y, float w);

}  // namespace ui
