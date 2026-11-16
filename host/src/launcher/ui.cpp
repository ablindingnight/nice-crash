// Nice Crash launcher drawing (see ui.h). GDI+ only; shapes follow mockups/launcher_home.html and nc_theme.css.
#include "ui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

using namespace Gdiplus;

namespace ui {

Ctx g;

static ULONG_PTR s_token = 0;
static PrivateFontCollection* s_private = nullptr;
struct FontPick {
    FontFamily* fam = nullptr;
    int style = FontStyleRegular;
    std::wstring name;
};
static FontPick s_fonts[F_COUNT];
static std::map<long long, std::unique_ptr<Gdiplus::Font>> s_cache;
static std::vector<GraphicsState> s_states;
static float s_pressX = -1, s_pressY = -1;
static int s_drag = -1;  // focus index of the slider being dragged

static const float kPi = 3.14159265f;

Color Alpha(const Color& c, float a) {
    int al = (int)(c.GetA() * a + 0.5f);
    return Color((BYTE)std::max(0, std::min(255, al)), c.GetR(), c.GetG(), c.GetB());
}
Color Mix(const Color& a, const Color& b, float t) {
    auto l = [t](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5f); };
    return Color(l(a.GetA(), b.GetA()), l(a.GetR(), b.GetR()), l(a.GetG(), b.GetG()), l(a.GetB(), b.GetB()));
}

std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// ---------------------------------------------------------------- fonts

static bool TryFamily(FontCollection* coll, const wchar_t* name, int style, FontPick& out) {
    FontFamily* f = new FontFamily(name, coll);
    if (f->GetLastStatus() != Ok) { delete f; return false; }
    out.fam = f;
    out.style = style;
    out.name = name;
    return true;
}

bool Init(const std::wstring& fontDir) {
    GdiplusStartupInput in;
    if (GdiplusStartup(&s_token, &in, nullptr) != Ok) return false;
    // Barlow / Barlow Condensed / JetBrains Mono (SIL OFL) when they're bundled in assets/fonts.
    s_private = new PrivateFontCollection();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((fontDir + L"\\*.ttf").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do s_private->AddFontFile((fontDir + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (getenv("NICECRASH_FONTLOG")) {  // debugging: what the bundled fonts register as
        int n = s_private->GetFamilyCount(), found = 0;
        std::vector<FontFamily> fams(n);
        s_private->GetFamilies(n, fams.data(), &found);
        for (int i = 0; i < found; i++) {
            wchar_t name[LF_FACESIZE];
            fams[i].GetFamilyName(name);
            printf("font family '%ls': regular %d bold %d italic %d bolditalic %d\n", name,
                   fams[i].IsStyleAvailable(FontStyleRegular), fams[i].IsStyleAvailable(FontStyleBold),
                   fams[i].IsStyleAvailable(FontStyleItalic), fams[i].IsStyleAvailable(FontStyleBoldItalic));
        }
    }
    struct Cand { const wchar_t* name; int style; bool priv; };
    const int BI = FontStyleBoldItalic, B = FontStyleBold, R = FontStyleRegular, I = FontStyleItalic;
    std::vector<Cand> cands[F_COUNT] = {
        {{L"Barlow Condensed Black", I, true}, {L"Barlow Condensed ExtraBold", I, true}, {L"Barlow Condensed", BI, true},
         {L"Bahnschrift Condensed", BI, false}, {L"Bahnschrift SemiBold SemiConden", I, false},
         {L"Bahnschrift", BI, false}, {L"Arial Narrow", BI, false}, {L"Segoe UI Black", I, false}},
        {{L"Barlow Condensed", B, true}, {L"Bahnschrift SemiBold SemiConden", R, false}, {L"Bahnschrift", B, false},
         {L"Arial Narrow", B, false}, {L"Segoe UI", B, false}},
        {{L"Barlow Medium", R, true}, {L"Barlow", R, true}, {L"Segoe UI", R, false}},
        {{L"Barlow SemiBold", R, true}, {L"Barlow", B, true}, {L"Segoe UI Semibold", R, false}, {L"Segoe UI", B, false}},
        {{L"Barlow", B, true}, {L"Segoe UI", B, false}},
        {{L"JetBrains Mono", R, true}, {L"Cascadia Mono", R, false}, {L"Consolas", R, false}, {L"Courier New", R, false}},
    };
    for (int i = 0; i < F_COUNT; i++) {
        for (auto& c : cands[i])
            if (TryFamily(c.priv ? (FontCollection*)s_private : nullptr, c.name, c.style, s_fonts[i])) break;
        if (!s_fonts[i].fam) TryFamily(nullptr, L"Arial", FontStyleRegular, s_fonts[i]);
    }
    return true;
}

const wchar_t* FontName(Font f) { return s_fonts[f].name.c_str(); }

void Shutdown() {
    s_cache.clear();
    for (auto& f : s_fonts) { delete f.fam; f.fam = nullptr; }
    delete s_private;
    s_private = nullptr;
    GdiplusShutdown(s_token);
}

static Gdiplus::Font* GetFont(Font f, float size) {
    long long key = (long long)f << 32 | (long long)(size * 8 + 0.5f);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return it->second.get();
    auto* p = new Gdiplus::Font(s_fonts[f].fam, size, s_fonts[f].style, UnitPixel);
    s_cache[key].reset(p);
    return p;
}

static const StringFormat* Fmt() {
    static StringFormat* fmt = nullptr;
    if (!fmt) {
        fmt = StringFormat::GenericTypographic()->Clone();
        fmt->SetFormatFlags(fmt->GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoClip);
    }
    return fmt;
}

// Distance from the line top to the top of the glyph cell, so text sits like CSS line boxes.
static float TopPad(Font f, float size) {
    FontFamily* fam = s_fonts[f].fam;
    int st = s_fonts[f].style;
    float em = (float)fam->GetEmHeight(st);
    float asc = fam->GetCellAscent(st) / em, desc = fam->GetCellDescent(st) / em;
    // CSS centres the font's content area in a line of 1.2 em; GDI+ draws the cell from its top.
    return (1.2f - (asc + desc)) * 0.5f * size;
}

// Text measurement is the costliest part of a frame, and the same strings come back every frame: cache the widths
// (canvas units, independent of the window scale since text isn't grid-fitted).
static float Measure(const std::wstring& s, Font f, float size) {
    static std::map<std::pair<long long, std::wstring>, float> cache;
    std::pair<long long, std::wstring> key((long long)f << 32 | (long long)(size * 8 + 0.5f), s);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (cache.size() > 20000) cache.clear();
    RectF r;
    g.g->MeasureString(s.c_str(), (int)s.size(), GetFont(f, size), PointF(0, 0), Fmt(), &r);
    cache[key] = r.Width;
    return r.Width;
}

float TextWidth(const std::wstring& s, Font f, float size, float tracking) {
    if (s.empty()) return 0;
    if (tracking == 0) return Measure(s, f, size);
    float w = 0;
    for (wchar_t ch : s) w += Measure(std::wstring(1, ch), f, size) + tracking * size;
    return w - tracking * size;
}

float Text(const std::wstring& s, Font f, float size, float x, float y, const Color& c, Align a, float tracking) {
    float w = TextWidth(s, f, size, tracking);
    if (a == CENTER) x -= w / 2;
    else if (a == RIGHT) x -= w;
    // Outside the area being repainted (a partial frame): nothing to draw.
    if (!g.g->IsVisible(RectF(x - 4, y - 4, w + 8, size * 1.6f + 8))) return w;
    Gdiplus::Font* font = GetFont(f, size);
    SolidBrush br(c);
    y += TopPad(f, size);
    if (tracking == 0) {
        g.g->DrawString(s.c_str(), (int)s.size(), font, PointF(x, y), Fmt(), &br);
    } else {
        for (wchar_t ch : s) {
            g.g->DrawString(&ch, 1, font, PointF(x, y), Fmt(), &br);
            x += Measure(std::wstring(1, ch), f, size) + tracking * size;
        }
    }
    return w;
}

float TextWrap(const std::wstring& s, Font f, float size, float x, float y, float w, const Color& c, float lh,
               bool draw) {
    // Greedy word wrap, so the line height is ours (CSS-like) rather than the font's.
    std::vector<std::wstring> lines;
    std::wstring cur;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find_first_of(L" \n", i);
        if (j == std::wstring::npos) j = s.size();
        std::wstring word = s.substr(i, j - i);
        std::wstring trial = cur.empty() ? word : cur + L" " + word;
        if (!cur.empty() && TextWidth(trial, f, size) > w) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = trial;
        }
        if (j < s.size() && s[j] == L'\n') { lines.push_back(cur); cur.clear(); }
        i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    float step = size * lh;
    if (draw)
        for (size_t k = 0; k < lines.size(); k++)
            Text(lines[k], f, size, x, y + k * step + (step - size * 1.2f) / 2, c);
    return lines.size() * step;
}

std::wstring Ellipsize(const std::wstring& s, Font f, float size, float maxW) {
    if (TextWidth(s, f, size) <= maxW) return s;
    size_t lo = 0, hi = s.size();  // drop the fewest leading characters that make it fit
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (TextWidth(L"…" + s.substr(mid), f, size) <= maxW) hi = mid;
        else lo = mid + 1;
    }
    return L"…" + s.substr(lo);
}

float FitSize(const std::wstring& s, Font f, float size, float maxW, float tracking) {
    float w = TextWidth(s, f, size, tracking);
    return w > maxW ? size * maxW / w : size;
}

void TextGradient(const std::wstring& s, Font f, float size, float cx, float y, const Color& top, const Color& bottom,
                  float tracking) {
    GraphicsPath path;
    float w = TextWidth(s, f, size, tracking);
    float x = cx - w / 2;
    y += TopPad(f, size);
    Gdiplus::Font* font = GetFont(f, size);
    for (wchar_t ch : s) {
        path.AddString(&ch, 1, s_fonts[f].fam, s_fonts[f].style, size, PointF(x, y), Fmt());
        RectF r;
        g.g->MeasureString(&ch, 1, font, PointF(0, 0), Fmt(), &r);
        x += r.Width + tracking * size;
    }
    // CSS: linear-gradient(180deg, #fff 0%, gold 120%) over the line box.
    LinearGradientBrush br(PointF(0, y - 2), PointF(0, y + size * 1.2f * 1.2f), top, bottom);
    g.g->FillPath(&br, &path);
}

// ---------------------------------------------------------------- frame

void BeginFrame(Graphics* gr, int w, int h, float t, const Input& in) {
    g.g = gr;
    g.winW = w;
    g.winH = h;
    g.scale = std::min(w / 1280.f, h / 720.f);
    g.ox = (w - 1280 * g.scale) / 2;
    g.oy = (h - 720 * g.scale) / 2;
    g.viewL = -g.ox / g.scale;
    g.viewR = (w - g.ox) / g.scale;
    g.time = t;
    g.in = in;
    g.mx = (in.mx - g.ox) / g.scale;
    g.my = (in.my - g.oy) / g.scale;
    g.prevFocusCount = g.focusCount;
    g.focusCount = 0;
    g.hotCursor = 0;
    g.consumed = false;
    if (in.pressed) { s_pressX = g.mx; s_pressY = g.my; }
    if (!in.down && !in.released) s_drag = -1;
    if (in.up || in.dn || in.left || in.right || in.accept) g.focusVisible = true;
    if (in.moved || in.pressed) g.focusVisible = false;
    int n = g.prevFocusCount;
    if (n > 0) {
        if (in.up) g.focus = (g.focus - 1 + n) % n;
        if (in.dn) g.focus = (g.focus + 1) % n;
        if (g.focus >= n) g.focus = n - 1;
    }
    gr->SetSmoothingMode(SmoothingModeAntiAlias);
    gr->SetTextRenderingHint(TextRenderingHintAntiAlias);
    gr->SetPixelOffsetMode(PixelOffsetModeHalf);
    gr->SetCompositingQuality(CompositingQualityHighQuality);
    gr->ResetTransform();
    gr->TranslateTransform(g.ox, g.oy);
    gr->ScaleTransform(g.scale, g.scale);
    s_states.clear();
}

void EndFrame() {
    if (g.in.released) { s_pressX = s_pressY = -1; }
    g.g = nullptr;
}

// ---------------------------------------------------------------- shapes

static void RoundPath(GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0.01f) { p.AddRectangle(RectF(x, y, w, h)); return; }
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRect(float x, float y, float w, float h, const Color& c) {
    SolidBrush b(c);
    g.g->FillRectangle(&b, x, y, w, h);
}
void FillRound(float x, float y, float w, float h, float r, const Color& c) {
    if (!g.g->IsVisible(RectF(x - 2, y - 2, w + 4, h + 4))) return;
    GraphicsPath p;
    RoundPath(p, x, y, w, h, r);
    SolidBrush b(c);
    g.g->FillPath(&b, &p);
}
void StrokeRound(float x, float y, float w, float h, float r, const Color& c, float width) {
    if (!g.g->IsVisible(RectF(x - 2, y - 2, w + 4, h + 4))) return;
    GraphicsPath p;
    RoundPath(p, x + width / 2, y + width / 2, w - width, h - width, r);
    Pen pen(c, width);
    g.g->DrawPath(&pen, &p);
}
void FillRoundGrad(float x, float y, float w, float h, float r, const Color& a, const Color& b, float angle) {
    if (!g.g->IsVisible(RectF(x - 2, y - 2, w + 4, h + 4))) return;
    GraphicsPath p;
    RoundPath(p, x, y, w, h, r);
    LinearGradientBrush br(RectF(x - 1, y - 1, w + 2, h + 2), a, b, angle);
    g.g->FillPath(&br, &p);
}
void Line(float x0, float y0, float x1, float y1, const Color& c, float width) {
    Pen pen(c, width);
    g.g->DrawLine(&pen, x0, y0, x1, y1);
}
void Circle(float cx, float cy, float r, const Color& c) {
    if (!g.g->IsVisible(RectF(cx - r - 1, cy - r - 1, r * 2 + 2, r * 2 + 2))) return;
    SolidBrush b(c);
    g.g->FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
}
void Ring(float cx, float cy, float r, const Color& c, float width) {
    Pen pen(c, width);
    g.g->DrawEllipse(&pen, cx - r, cy - r, r * 2, r * 2);
}
void Arc(float cx, float cy, float r, float start, float sweep, const Color& c, float width) {
    Pen pen(c, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.g->DrawArc(&pen, cx - r, cy - r, r * 2, r * 2, start, sweep);
}

static void StarPath(GraphicsPath& p, float cx, float cy, float r) {
    // theme_sheet.html: five points, inner radius 0.42.
    static const float pts[] = {0, -1, 0.247f, -0.34f, 0.951f, -0.309f, 0.399f, 0.13f, 0.588f, 0.809f,
                                0, 0.42f, -0.588f, 0.809f, -0.399f, 0.13f, -0.951f, -0.309f, -0.247f, -0.34f};
    PointF v[10];
    for (int i = 0; i < 10; i++) v[i] = PointF(cx + pts[i * 2] * r, cy + pts[i * 2 + 1] * r);
    p.AddPolygon(v, 10);
}
void Star(float cx, float cy, float r, const Color& c) {
    GraphicsPath p;
    StarPath(p, cx, cy, r);
    SolidBrush b(c);
    g.g->FillPath(&b, &p);
}

void PushSkew(float cx, float cy, float deg) {
    s_states.push_back(g.g->Save());
    float k = tanf(deg * kPi / 180);
    Matrix m(1, 0, k, 1, -k * cy, 0);  // x' = x + k*(y - cy)
    g.g->MultiplyTransform(&m);
    (void)cx;
}
void PushClip(float x, float y, float w, float h) {
    s_states.push_back(g.g->Save());
    g.g->IntersectClip(RectF(x, y, w, h));
}
void Pop() {
    if (s_states.empty()) return;
    g.g->Restore(s_states.back());
    s_states.pop_back();
}

GraphicsPath* SvgPath(const char* d) {
    auto* p = new GraphicsPath();
    float cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;  // current, subpath start, last control
    char cmd = 0, prev = 0;
    const char* s = d;
    auto num = [&](float& out) -> bool {
        while (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t') s++;
        if (!*s || (isalpha((unsigned char)*s) && *s != 'e')) return false;
        char* e;
        out = strtof(s, &e);
        if (e == s) return false;
        s = e;
        return true;
    };
    for (;;) {
        while (*s == ' ' || *s == ',' || *s == '\n') s++;
        if (!*s) break;
        if (isalpha((unsigned char)*s)) cmd = *s++;
        bool rel = islower((unsigned char)cmd);
        float a, b, c, e, f, h;
        switch (toupper((unsigned char)cmd)) {
        case 'M':
            if (!num(a) || !num(b)) return p;
            if (rel) { a += cx; b += cy; }
            p->StartFigure();
            cx = sx = a; cy = sy = b;
            cmd = rel ? 'l' : 'L';  // further pairs are line-tos
            break;
        case 'L':
            if (!num(a) || !num(b)) return p;
            if (rel) { a += cx; b += cy; }
            p->AddLine(cx, cy, a, b);
            cx = a; cy = b;
            break;
        case 'H':
            if (!num(a)) return p;
            if (rel) a += cx;
            p->AddLine(cx, cy, a, cy);
            cx = a;
            break;
        case 'V':
            if (!num(a)) return p;
            if (rel) a += cy;
            p->AddLine(cx, cy, cx, a);
            cy = a;
            break;
        case 'C':
            if (!num(a) || !num(b) || !num(c) || !num(e) || !num(f) || !num(h)) return p;
            if (rel) { a += cx; b += cy; c += cx; e += cy; f += cx; h += cy; }
            p->AddBezier(cx, cy, a, b, c, e, f, h);
            lcx = c; lcy = e; cx = f; cy = h;
            break;
        case 'S': {
            if (!num(c) || !num(e) || !num(f) || !num(h)) return p;
            if (rel) { c += cx; e += cy; f += cx; h += cy; }
            char pu = (char)toupper((unsigned char)prev);
            a = (pu == 'C' || pu == 'S') ? 2 * cx - lcx : cx;
            b = (pu == 'C' || pu == 'S') ? 2 * cy - lcy : cy;
            p->AddBezier(cx, cy, a, b, c, e, f, h);
            lcx = c; lcy = e; cx = f; cy = h;
            break;
        }
        case 'Z':
            p->CloseFigure();
            cx = sx; cy = sy;
            break;
        default:
            return p;
        }
        prev = cmd;
    }
    return p;
}

// The launcher's 16-unit stroke icons (mockups/launcher_home.html).
struct IconDef { const char* name; const char* path; float circle[3]; float circle2[3]; float rect[5]; };
static const IconDef kIcons[] = {
    {"gear", "M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4",
     {8, 8, 2.3f}, {}, {}},
    {"pad", "M4.5 8h2M5.5 7v2", {}, {}, {1.5f, 5, 13, 6, 3}},
    {"logs", "M3 2.5h10v11H3zM5.5 5.5h5M5.5 8h5M5.5 10.5h3", {}, {}, {}},
    {"about", "M8 7v4M8 4.8v.1", {8, 8, 6.2f}, {}, {}},
    {"disc", "", {8, 8, 6}, {8, 8, 1.6f}, {}},
    {"display", "M5.5 14h5M8 11.5V14", {}, {}, {1.5f, 2.5f, 13, 9, 1}},
    {"graphics", "M2 13 L6 7 L9 10 L11 8 L14 13 Z", {11, 4.5f, 1.5f}, {}, {}},
    {"audio", "M2.5 6h3l4-3v10l-4-3h-3zM11.5 5.5c1 1.4 1 3.6 0 5", {}, {}, {}},
    {"language", "M2 8h12M8 2c2 2 2 10 0 12M8 2c-2 2-2 10 0 12", {8, 8, 6}, {}, {}},
    {"builds", "M8 2 L14 5 L8 8 L2 5 Z M2 8 L8 11 L14 8 M2 11 L8 14 L14 11", {}, {}, {}},
    {"min", "M3 8h10", {}, {}, {}},
    {"close", "M3.5 3.5l9 9M12.5 3.5l-9 9", {}, {}, {}},
    {"shield", "M8 1.5 L13.5 4 V8 C13.5 11 11 13.5 8 14.5 C5 13.5 2.5 11 2.5 8 V4 Z M5.5 8 L7.3 9.8 L10.5 6.3", {}, {}, {}},
    {"chev", "M4.8 1.6 L11.2 8 L4.8 14.4", {}, {}, {}},
    {"tick", "M2.7 8.7 L6.7 12.7 L13.3 4", {}, {}, {}},
    {"cross", "M4 4l8 8M12 4l-8 8", {}, {}, {}},
    {"folder", "M2 4.7h4l1.3 1.3h6.7v6.7H2z M8 11.3v-4M6 9l2-2 2 2", {}, {}, {}},
    {"back", "M11.2 1.6 L4.8 8 L11.2 14.4", {}, {}, {}},
};

void Icon(const char* name, float x, float y, float size, const Color& c, float stroke) {
    if (!g.g->IsVisible(RectF(x - 2, y - 2, size + 4, size + 4))) return;
    const IconDef* d = nullptr;
    for (auto& i : kIcons)
        if (!strcmp(i.name, name)) d = &i;
    if (!d) return;
    GraphicsState st = g.g->Save();
    g.g->TranslateTransform(x, y);
    g.g->ScaleTransform(size / 16, size / 16);
    Pen pen(c, stroke);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    if (d->path[0]) {
        GraphicsPath* p = SvgPath(d->path);
        g.g->DrawPath(&pen, p);
        delete p;
    }
    for (const float* c3 : {d->circle, d->circle2})
        if (c3[2] > 0) g.g->DrawEllipse(&pen, c3[0] - c3[2], c3[1] - c3[2], c3[2] * 2, c3[2] * 2);
    if (d->rect[2] > 0) {
        GraphicsPath p;
        RoundPath(p, d->rect[0], d->rect[1], d->rect[2], d->rect[3], d->rect[4]);
        g.g->DrawPath(&pen, &p);
    }
    g.g->Restore(st);
}

// ---------------------------------------------------------------- widgets

bool Hover(float x, float y, float w, float h) {
    return g.mx >= x && g.mx < x + w && g.my >= y && g.my < y + h;
}

int TakeFocus(float x, float y, float w, float h) {
    int idx = g.focusCount++;
    if (g.in.moved && Hover(x, y, w, h) && s_drag < 0) g.focus = idx;
    if (Hover(x, y, w, h)) g.hotCursor = 1;
    return idx;
}

bool Focused(int idx) { return idx == g.focus; }

bool Clicked(float x, float y, float w, float h) {
    return g.in.released && Hover(x, y, w, h) && s_pressX >= x && s_pressX < x + w && s_pressY >= y &&
           s_pressY < y + h;
}

static bool Activated(int idx, float x, float y, float w, float h) {
    if (g.consumed) return false;
    bool a = Clicked(x, y, w, h) || (Focused(idx) && g.in.accept);
    if (a) g.consumed = true;
    return a;
}

void FocusRing(float x, float y, float w, float h, float r) {
    if (!g.focusVisible) return;
    StrokeRound(x - 4, y - 4, w + 8, h + 8, r + 3, col::tilt, 2);
}

bool PlayButton(const std::wstring& label, float x, float y, float w, float h, bool icon, bool arrowAfter) {
    int f = TakeFocus(x, y, w, h);
    bool hov = Hover(x, y, w, h);
    float cx = x + w / 2, cy = y + h / 2;
    if (!g.g->IsVisible(RectF(x - 50, y - 30, w + 100, h + 70))) return Activated(f, x, y, w, h);  // not repainted
    PushSkew(cx, cy, -14);
    // drop-shadow(0 10px 24px boost 45%), approximated by soft layers that follow the slanted shape.
    for (int i = 8; i >= 1; i--) {
        float grow = i * 3.f;
        FillRound(x - grow, y + 10 - grow, w + grow * 2, h + grow * 2, 6 + grow, Alpha(col::boost, 0.035f));
    }
    GraphicsPath p;
    RoundPath(p, x, y, w, h, 6);
    LinearGradientBrush br(RectF(x, y, w, h), col::boost, col::gold, 10.f);
    Color cols[3] = {col::boost, Color(255, 0xFF, 0x8A, 0x1E), col::gold};
    REAL pos[3] = {0, 0.45f, 1};
    br.SetInterpolationColors(cols, pos, 3);
    g.g->FillPath(&br, &p);
    if (hov) {
        SolidBrush hb(Color(30, 255, 255, 255));
        g.g->FillPath(&hb, &p);
    }
    PushClip(x, y, w, h);
    FillRect(x + 6, y, w - 12, 2, Color(115, 255, 255, 255));
    FillRect(x + 6, y + h - 4, w - 12, 4, Color(46, 0, 0, 0));
    for (float sx = x + w - 18 - 40; sx < x + w - 18; sx += 9) FillRect(sx, y + 14, 3, h - 28, Color(70, 255, 255, 255));
    Pop();
    if (Focused(f) && g.focusVisible) StrokeRound(x - 5, y - 5, w + 10, h + 10, 9, col::fg, 3);
    Pop();
    float size = h * 36 / 74;
    float tw = TextWidth(label, F_DISPLAY, size, 0.14f);
    float iw = icon ? size * 20 / 36 : 0, gap = icon ? 12 : 0;
    float lx = cx - (tw + iw + gap) / 2;
    float ty = cy - size * 0.6f;
    auto tri = [&](float tx) {
        PointF v[3] = {PointF(tx + iw * 0.1f, cy - iw * 0.4f), PointF(tx + iw * 0.9f, cy), PointF(tx + iw * 0.1f, cy + iw * 0.4f)};
        SolidBrush b(col::playFg);
        g.g->FillPolygon(&b, v, 3);
    };
    if (icon && !arrowAfter) { tri(lx); lx += iw + gap; }
    Text(label, F_DISPLAY, size, lx, ty, col::playFg, LEFT, 0.14f);
    if (icon && arrowAfter) tri(lx + tw + gap);
    return Activated(f, x, y, w, h);
}

bool Button2(const std::wstring& label, float x, float y, float* wOut, bool enabled) {
    std::wstring up = label;
    for (auto& c : up) c = towupper(c);
    float tw = TextWidth(up, F_DISPLAY, 17, 0.12f);
    float w = tw + 52, h = 44;
    if (wOut) *wOut = w;
    int f = TakeFocus(x, y, w, h);
    bool hov = Hover(x, y, w, h) && enabled;
    PushSkew(x + w / 2, y + h / 2, -14);
    FillRound(x, y, w, h, 4, Alpha(col::panel, 0.7f));
    StrokeRound(x, y, w, h, 4, hov ? col::gold : Alpha(col::fg, enabled ? 0.35f : 0.15f), 2);
    if (Focused(f) && g.focusVisible) StrokeRound(x - 4, y - 4, w + 8, h + 8, 7, col::tilt, 2);
    Pop();
    Text(up, F_DISPLAY, 17, x + 26, y + h / 2 - 17 * 0.6f, enabled ? col::fg : col::dim, LEFT, 0.12f);
    return enabled && Activated(f, x, y, w, h);
}

bool Link(const std::wstring& label, float x, float y, float size, const Color& c, bool chev, float* wOut, Align a) {
    float tw = TextWidth(label, F_BODY_SEMI, size);
    float w = tw + (chev ? 6 + size * 0.6f : 0);
    if (a == RIGHT) x -= w;
    if (wOut) *wOut = w;
    float h = size * 1.4f;
    int f = TakeFocus(x - 2, y - 2, w + 4, h + 4);
    bool hov = Hover(x - 2, y - 2, w + 4, h + 4);
    Color cc = hov ? Mix(c, col::fg, 0.35f) : c;
    Text(label, F_BODY_SEMI, size, x, y + (h - size * 1.2f) / 2, cc);
    if (chev) Icon("chev", x + tw + 6, y + h / 2 - size * 0.3f, size * 0.6f, cc, 2.4f);
    if (Focused(f) && g.focusVisible) Line(x, y + h + 1, x + w, y + h + 1, col::gold, 2);
    return Activated(f, x - 2, y - 2, w + 4, h + 4);
}

bool NavLink(const std::wstring& label, const char* icon, float x, float y, float* wOut) {
    float tw = TextWidth(label, F_BODY_SEMI, 15);
    float w = 16 + 8 + tw, h = 22;
    if (wOut) *wOut = w;
    int f = TakeFocus(x - 4, y - 4, w + 8, h + 8);
    bool hov = Hover(x - 4, y - 4, w + 8, h + 8);
    Icon(icon, x, y + 3, 16, col::gold, 1.8f);
    Text(label, F_BODY_SEMI, 15, x + 24, y + 2, hov ? col::fg : Alpha(col::fg, 0.85f));
    if (Focused(f) && g.focusVisible) Line(x, y + h + 3, x + w, y + h + 3, col::gold, 2);
    return Activated(f, x - 4, y - 4, w + 8, h + 8);
}

float BadgeWidth(const std::wstring& label, bool tick) {
    return TextWidth(label, F_DISPLAY_CAPS, 11, 0.12f) + (tick ? 15 : 0) + 14;
}

void Badge(const std::wstring& label, float x, float y, const Color& c, bool tick) {
    float w = BadgeWidth(label, tick), h = 20;
    FillRound(x, y, w, h, 3, Alpha(c, 0.14f));
    StrokeRound(x, y, w, h, 3, Alpha(c, 0.45f), 1);
    float tx = x + 7;
    if (tick) { Icon("tick", tx - 1, y + 5, 10, c, 3); tx += 15; }
    Text(label, F_DISPLAY_CAPS, 11, tx, y + 3, c, LEFT, 0.12f);
}

void Key(const std::wstring& label, float x, float y, float* wOut) {
    float tw = TextWidth(label, F_DISPLAY_CAPS, 12, 0.06f);
    float w = std::max(24.f, tw + 12);
    FillRound(x, y, w, 22, 4, col::chip);
    StrokeRound(x, y, w, 22, 4, col::rule, 1);
    Text(label, F_DISPLAY_CAPS, 12, x + w / 2, y + 4, col::fg, CENTER, 0.06f);
    if (wOut) *wOut = w;
}

void Eyebrow(const std::wstring& label, float x, float y, const Color& c) {
    PushSkew(x + 17, y + 6, -30);
    FillRect(x, y + 5, 34, 3, col::boost);
    Pop();
    std::wstring up = label;
    for (auto& ch : up) ch = towupper(ch);
    Text(up, F_DISPLAY_CAPS, 13, x + 46, y - 1, c, LEFT, 0.24f);
}

void Meter(float x, float y, float w, float h, float frac) {
    FillRound(x, y, w, h, h / 2, col::faint);
    if (frac > 0) FillRoundGrad(x, y, std::max(h, w * std::min(1.f, frac)), h, h / 2, col::boost, col::gold, 0);
}

static bool StepValue(int idx, int* sel, int n, bool wrap) {
    if (!Focused(idx) || g.consumed) return false;
    int d = (g.in.right ? 1 : 0) - (g.in.left ? 1 : 0);
    if (!d) return false;
    int v = *sel + d;
    if (wrap) v = (v + n) % n;
    v = std::max(0, std::min(n - 1, v));
    if (v == *sel) return false;
    *sel = v;
    g.consumed = true;
    return true;
}

bool Seg(const std::vector<std::wstring>& labels, int* sel, float x, float y, int idx, float h) {
    std::vector<float> ws;
    float total = 0;
    for (auto& l : labels) { ws.push_back(TextWidth(l, F_BODY_SEMI, 14) + 32); total += ws.back(); }
    FillRound(x, y, total, h, 4, Alpha(col::hill, 0.5f));
    bool changed = StepValue(idx, sel, (int)labels.size(), false);
    float bx = x;
    for (size_t i = 0; i < labels.size(); i++) {
        bool on = (int)i == *sel;
        if (on) {
            PushClip(bx, y, ws[i], h);
            FillRound(x, y, total, h, 4, col::gold);
            Pop();
        } else if (Hover(bx, y, ws[i], h)) {
            FillRect(bx, y, ws[i], h, Alpha(col::panelHi, 0.8f));
        }
        if (i) Line(bx, y, bx, y + h, col::faint, 1);
        Text(labels[i], F_BODY_SEMI, 14, bx + ws[i] / 2, y + h / 2 - 14 * 0.6f, on ? col::onGold : col::dim, CENTER);
        if (Clicked(bx, y, ws[i], h) && !g.consumed) {
            if (*sel != (int)i) changed = true;
            *sel = (int)i;
            g.consumed = true;
        }
        bx += ws[i];
    }
    StrokeRound(x, y, total, h, 4, col::faint, 1);
    if (Focused(idx) && g.focusVisible) StrokeRound(x - 3, y - 3, total + 6, h + 6, 6, col::tilt, 2);
    return changed;
}

bool Toggle(bool* on, float x, float y, int idx) {
    bool changed = false;
    if ((Clicked(x, y, 90, 24) || (Focused(idx) && (g.in.accept || g.in.left || g.in.right))) && !g.consumed) {
        bool nv = g.in.left ? false : g.in.right ? true : !*on;
        if (nv != *on) { *on = nv; changed = true; }
        g.consumed = true;
    }
    FillRound(x, y, 46, 24, 12, *on ? col::boost : col::faint);
    Circle(*on ? x + 34 : x + 12, y + 12, 9, *on ? Color(255, 255, 255, 255) : col::dim);
    Text(*on ? L"On" : L"Off", F_BODY_SEMI, 14, x + 56, y + 3, *on ? col::fg : col::dim);
    if (Focused(idx) && g.focusVisible) StrokeRound(x - 3, y - 3, 52, 30, 15, col::tilt, 2);
    return changed;
}

bool Slider(int* v, int lo, int hi, int step, float x, float y, float w, int idx, const wchar_t* unit) {
    float tw = w - 56, ty = y + 13;
    int old = *v;
    if (g.in.pressed && Hover(x - 10, y, tw + 20, 34)) s_drag = idx;
    if (s_drag == idx && g.in.down) {
        float t = std::max(0.f, std::min(1.f, (g.mx - x) / tw));
        int nv = lo + (int)floorf(t * (hi - lo) / step + 0.5f) * step;
        *v = std::max(lo, std::min(hi, nv));
    }
    if (Focused(idx) && !g.consumed && (g.in.left || g.in.right)) {
        *v = std::max(lo, std::min(hi, *v + (g.in.right ? step : -step)));
        g.consumed = true;
    }
    float t = (float)(*v - lo) / (hi - lo);
    FillRound(x, ty, tw, 8, 4, col::faint);
    if (t > 0) FillRoundGrad(x, ty, std::max(8.f, tw * t), 8, 4, col::boost, col::gold, 0);
    float kx = x + tw * t, ky = ty + 4;
    GraphicsState st = g.g->Save();
    g.g->TranslateTransform(kx, ky);
    g.g->RotateTransform(45);
    FillRound(-12, -12, 24, 24, 5, Alpha(col::gold, 0.5f));
    FillRound(-9, -9, 18, 18, 3, Color(255, 255, 255, 255));
    g.g->Restore(st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%d%ls", *v, unit ? unit : L"");
    Text(buf, F_BODY_SEMI, 15, x + w, y + 8, col::fg, RIGHT);
    if (Focused(idx) && g.focusVisible) StrokeRound(x - 14, y - 2, w + 20, 38, 6, col::tilt, 2);
    return *v != old;
}

bool Picker(const std::vector<std::wstring>& labels, int* sel, float x, float y, float w, int idx) {
    float h = 36;
    bool changed = false;
    int n = (int)labels.size();
    if (Clicked(x, y, w, h) && !g.consumed && n > 0) {
        int d = g.mx < x + w / 2 ? -1 : 1;
        int nv = (*sel + d + n) % n;
        changed = nv != *sel;
        *sel = nv;
        g.consumed = true;
    }
    if (n > 0) changed |= StepValue(idx, sel, n, true);
    if (Focused(idx) && g.in.accept && !g.consumed && n > 0) {
        *sel = (*sel + 1) % n;
        changed = n > 1;
        g.consumed = true;
    }
    FillRound(x, y, w, h, 4, Alpha(col::hill, 0.6f));
    StrokeRound(x, y, w, h, 4, Hover(x, y, w, h) ? Alpha(col::gold, 0.6f) : col::faint, 1);
    SolidBrush gb(n > 1 ? col::gold : col::faint);
    PointF l[3] = {PointF(x + 17, y + 13), PointF(x + 11, y + 18), PointF(x + 17, y + 23)};
    PointF r[3] = {PointF(x + w - 17, y + 13), PointF(x + w - 11, y + 18), PointF(x + w - 17, y + 23)};
    g.g->FillPolygon(&gb, l, 3);
    g.g->FillPolygon(&gb, r, 3);
    std::wstring s = n ? labels[std::max(0, std::min(n - 1, *sel))] : L"(none)";
    float size = FitSize(s, F_BODY_SEMI, 15, w - 56);
    Text(s, F_BODY_SEMI, size, x + w / 2, y + h / 2 - size * 0.6f, col::fg, CENTER);
    if (Focused(idx) && g.focusVisible) StrokeRound(x - 3, y - 3, w + 6, h + 6, 6, col::tilt, 2);
    return changed;
}

bool Checkbox(bool* on, const std::wstring& label, float x, float y, int idx) {
    float tw = TextWidth(label, F_BODY_SEMI, 16);
    bool changed = false;
    if ((Clicked(x, y - 2, 34 + tw, 28) || (Focused(idx) && g.in.accept)) && !g.consumed) {
        *on = !*on;
        changed = true;
        g.consumed = true;
    }
    if (*on) {
        FillRound(x, y, 24, 24, 5, col::gold);
        Icon("tick", x + 3, y + 3, 18, col::onGold, 2.6f);
    } else {
        FillRound(x, y, 24, 24, 5, Alpha(col::hill, 0.6f));
        StrokeRound(x, y, 24, 24, 5, Hover(x, y - 2, 34 + tw, 28) ? col::gold : col::dim, 2);
    }
    Text(label, F_BODY_SEMI, 16, x + 36, y + 2, col::fg);
    if (Focused(idx) && g.focusVisible) StrokeRound(x - 4, y - 4, 32, 32, 7, col::tilt, 2);
    return changed;
}

// ---------------------------------------------------------------- art

// Design v11 pines (mockups/launcher_home.html #pines), in hills coordinates.
static const char* kPines =
    "M1132.0 84.0 L1139.6 97.6 L1136.7 97.6 L1144.6 110.8 L1140.4 110.8 L1148.8 120.8 L1133.7 120.8 L1133.7 128.0 L1130.3 128.0 L1130.3 120.8 L1115.2 120.8 L1123.6 110.8 L1119.4 110.8 L1127.3 97.6 L1124.4 97.6 Z "
    "M1158.0 96.0 L1163.7 106.2 L1161.5 106.2 L1167.5 116.1 L1164.3 116.1 L1170.6 123.6 L1159.3 123.6 L1159.3 130.0 L1156.7 130.0 L1156.7 123.6 L1145.4 123.6 L1151.7 116.1 L1148.5 116.1 L1154.5 106.2 L1152.3 106.2 Z "
    "M1214.0 82.0 L1222.7 97.6 L1219.4 97.6 L1228.5 112.8 L1223.7 112.8 L1233.3 124.3 L1215.9 124.3 L1215.9 132.0 L1212.1 132.0 L1212.1 124.3 L1194.7 124.3 L1204.3 112.8 L1199.5 112.8 L1208.6 97.6 L1205.3 97.6 Z "
    "M1246.0 93.0 L1252.4 104.6 L1250.0 104.6 L1256.7 115.8 L1253.1 115.8 L1260.3 124.3 L1247.4 124.3 L1247.4 131.0 L1244.6 131.0 L1244.6 124.3 L1231.7 124.3 L1238.9 115.8 L1235.3 115.8 L1242.0 104.6 L1239.6 104.6 Z "
    "M392.0 92.0 L398.4 103.6 L396.0 103.6 L402.7 114.8 L399.1 114.8 L406.3 123.3 L393.4 123.3 L393.4 130.0 L390.6 130.0 L390.6 123.3 L377.7 123.3 L384.9 114.8 L381.3 114.8 L388.0 103.6 L385.6 103.6 Z "
    "M414.0 99.0 L418.9 107.8 L417.1 107.8 L422.2 116.4 L419.5 116.4 L424.9 122.9 L415.1 122.9 L415.1 129.0 L412.9 129.0 L412.9 122.9 L403.1 122.9 L408.5 116.4 L405.8 116.4 L410.9 107.8 L409.1 107.8 Z ";

static void StillLayer(float L, float R, float Wd, float top, float bottom);
static void Tread(float L, float R, float Wd);

void Backdrop(const std::function<void(float, float)>& mid) {
    float L = g.viewL - 2, R = g.viewR + 2, Wd = R - L;
    float top = -g.oy / g.scale - 2, bottom = (g.winH - g.oy) / g.scale + 2;
    // The still layer (sky, glow, far range with the foothill and pines) is drawn once per window size into a
    // cached bitmap (design 3.2 cost rule); each frame blits it, then draws the truck, the front hill and the rest.
    static Bitmap* stillBmp = nullptr;
    static CachedBitmap* still = nullptr;
    static int stillW = 0, stillH = 0;
    if (!still || stillW != g.winW || stillH != g.winH) {
        delete still;
        delete stillBmp;
        stillW = g.winW;
        stillH = g.winH;
        stillBmp = new Bitmap(stillW, stillH, PixelFormat32bppPARGB);
        Graphics bg(stillBmp);
        bg.SetSmoothingMode(SmoothingModeAntiAlias);
        bg.SetPixelOffsetMode(PixelOffsetModeHalf);
        bg.SetCompositingQuality(CompositingQualityHighQuality);
        bg.TranslateTransform(g.ox, g.oy);
        bg.ScaleTransform(g.scale, g.scale);
        Graphics* keep = g.g;
        g.g = &bg;
        StillLayer(L, R, Wd, top, bottom);
        g.g = keep;
        still = new CachedBitmap(stillBmp, g.g);
    }
    {
        // Copy only the part being repainted (a partial frame clips to the moving areas), straight, with no blending.
        GraphicsState s = g.g->Save();
        g.g->ResetTransform();
        g.g->SetCompositingMode(CompositingModeSourceCopy);
        g.g->SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.g->SetPixelOffsetMode(PixelOffsetModeNone);
        Region clip;
        g.g->GetClip(&clip);
        Matrix id;
        int n = clip.GetRegionScansCount(&id);
        std::vector<Rect> rects(std::max(1, n));
        INT got = n;
        if (n > 0 && n <= 64 && clip.GetRegionScans(&id, rects.data(), &got) == Ok) {
            for (int i = 0; i < got; i++) {
                Rect r = rects[i];
                r.Intersect(Rect(0, 0, stillW, stillH));
                if (r.Width > 0 && r.Height > 0) g.g->DrawImage(stillBmp, r, r.X, r.Y, r.Width, r.Height, UnitPixel);
            }
        } else {
            g.g->DrawImage(stillBmp, Rect(0, 0, stillW, stillH), 0, 0, stillW, stillH, UnitPixel);
        }
        g.g->Restore(s);
    }
    {
        static GraphicsPath* nearHills = SvgPath("M0 214 C 180 180, 320 236, 520 204 S 880 168, 1060 206 S 1220 196, "
                                                 "1280 188 L1280 270 L0 270 Z");
        if (mid) mid(L, Wd / 1280);  // canvas transform; hills x maps to L + x * sx, y to 460 + y
        GraphicsState st = g.g->Save();
        g.g->TranslateTransform(L, 460);
        g.g->ScaleTransform(Wd / 1280, 1);
        SolidBrush f2(col::hill);
        g.g->FillPath(&f2, nearHills);
        g.g->Restore(st);
        if (bottom > 720) FillRect(L, 719, Wd, bottom - 719, col::hill);
    }
    Tread(L, R, Wd);
}

static void StillLayer(float L, float R, float Wd, float top, float bottom) {
    // Sky (nc_theme.css .frame): sky-top 0% -> sky-mid 64%.
    FillRect(L, top, Wd, bottom - top, col::skyMid);
    LinearGradientBrush sky(PointF(0, std::min(0.f, top)), PointF(0, 461), col::skyTop, col::skyMid);
    g.g->FillRectangle(&sky, L, top, Wd, 461 - top);
    // Sunrise (user, 2026-11-15: carry the glow across the whole bottom). A wide band of horizon light spanning
    // the window, brightest just above the ridgeline, plus a warmer spot behind the emblem. Both fade out to
    // transparent, so there are no edges, and run on behind the hills.
    auto glowEllipse = [&](float cx, float cy, float rx, float ry, float peak, float mid) {
        GraphicsPath p;
        p.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
        PathGradientBrush pg(&p);
        pg.SetCenterPoint(PointF(cx, cy));
        pg.SetCenterColor(Alpha(col::glow, peak));
        Color surround = Alpha(col::glow, 0);
        int one = 1;
        pg.SetSurroundColors(&surround, &one);
        Color blend[3] = {Alpha(col::glow, 0), Alpha(col::glow, mid), Alpha(col::glow, peak)};
        REAL pos[3] = {0, 0.45f, 1};
        pg.SetInterpolationColors(blend, pos, 3);
        g.g->FillPath(&pg, &p);
    };
    {
        PushClip(L, 200, Wd, bottom - 200);
        float cx = (L + R) / 2;
        glowEllipse(cx, 640, Wd * 0.78f, 330, 0.42f, 0.13f);  // the band across the whole horizon
        glowEllipse(1024, 600, 480, 250, 0.32f, 0.1f);        // warmer behind the emblem (mockup's glow)
        Pop();
    }
    // Ridgeline hills (viewBox 1280x260, stretched to the window width). Design v11: the far range, the foothill the
    // truck drives (with its knoll) and the pines are ONE shape at 55%, so overlaps never darken; then the truck run
    // (mid), then the solid front hill.
    {
        static Region* range = nullptr;
        if (!range) {
            GraphicsPath* farHills = SvgPath("M0 170 L120 120 L210 150 L330 88 L430 140 L520 110 L640 160 L760 96 L860 132 "
                                             "L980 70 L1090 128 L1180 100 L1280 140 L1280 270 L0 270 Z");
            GraphicsPath* foothill = SvgPath("M0 128 C 120 116, 220 136, 340 122 S 560 106, 700 120 C 800 128, 880 128, 960 118 "
                                             "C 1000 116, 1050 108, 1080 92 C 1086 89, 1094 90, 1100 96 C 1118 114, 1200 130, "
                                             "1280 126 L1280 270 L0 270 Z");
            GraphicsPath* pines = SvgPath(kPines);
            range = new Region(farHills);
            range->Union(foothill);
            range->Union(pines);
            delete farHills;
            delete foothill;
            delete pines;
        }
        GraphicsState st = g.g->Save();
        g.g->TranslateTransform(L, 460);
        g.g->ScaleTransform(Wd / 1280, 1);
        SolidBrush f1(Alpha(col::hill, 0.55f));
        g.g->FillRegion(&f1, range);
        g.g->Restore(st);
    }
    (void)bottom;
}

// Tyre tread strip along the bottom edge.
static void Tread(float L, float R, float Wd) {
    {
        PushClip(L, 706, Wd, 14);
        SolidBrush b(Alpha(col::gold, 0.2f));
        for (float x = L - 20; x < R + 20; x += 17.65f) {
            PointF v[4] = {PointF(x + 6.5f, 706), PointF(x + 6.5f + 6.6f, 706), PointF(x + 6.6f, 720), PointF(x, 720)};
            g.g->FillPolygon(&b, v, 4);
        }
        Pop();
    }
}

void Truck(float x, float y, float w, float h) {
    // symbol #truck, viewBox "90 160 230 140", rotate(-10 205 250).
    GraphicsState st = g.g->Save();
    g.g->TranslateTransform(x, y);
    g.g->ScaleTransform(w / 230, h / 140);
    g.g->TranslateTransform(-90, -160);
    g.g->TranslateTransform(205, 250);
    g.g->RotateTransform(-10);
    g.g->TranslateTransform(-205, -250);
    Pen fgPen(col::fg, 6);
    fgPen.SetLineJoin(LineJoinRound);
    GraphicsPath* body = SvgPath("M104 250 L118 218 L170 212 L192 178 L256 176 L280 210 L306 216 L310 252 Z");
    SolidBrush hill(col::hill);
    g.g->FillPath(&hill, body);
    g.g->DrawPath(&fgPen, body);
    delete body;
    GraphicsPath* win = SvgPath("M198 186 L250 185 L266 208 L190 210 Z");
    SolidBrush tilt(col::tilt);
    g.g->FillPath(&tilt, win);
    delete win;
    Pen stripe(col::boost, 7);
    g.g->DrawLine(&stripe, 118.f, 236.f, 300.f, 236.f);
    Pen tread(col::gold, 7);
    REAL dash[2] = {6.f / 7, 7.f / 7};
    tread.SetDashPattern(dash, 2);
    for (float cx : {142.f, 272.f}) {
        g.g->FillEllipse(&hill, cx - 32, 262 - 32.f, 64.f, 64.f);
        g.g->DrawEllipse(&fgPen, cx - 32, 262 - 32.f, 64.f, 64.f);
        g.g->DrawEllipse(&tread, cx - 22, 262 - 22.f, 44.f, 44.f);
    }
    g.g->Restore(st);
}

// Adds s to path with its baseline at y and `spacing` px between letters; returns the advance width.
static float AddTextPath(GraphicsPath& path, const std::wstring& s, Font f, float size, float x, float baseline,
                         float spacing) {
    FontFamily* fam = s_fonts[f].fam;
    int st = s_fonts[f].style;
    float top = baseline - size * fam->GetCellAscent(st) / fam->GetEmHeight(st);
    Gdiplus::Font* font = GetFont(f, size);
    float x0 = x;
    path.SetFillMode(FillModeWinding);  // overlapping glyph contours (synthesised bold) must not punch holes
    for (wchar_t ch : s) {
        path.AddString(&ch, 1, fam, st, size, PointF(x, top), Fmt());
        RectF r;
        g.g->MeasureString(&ch, 1, font, PointF(0, 0), Fmt(), &r);
        x += r.Width + spacing;
    }
    return x - x0 - spacing;
}

void Wordmark(float x, float y, float w) {
    if (!g.g->IsVisible(RectF(x - 10, y - 10, w + 20, w * 120 / 440 + 20))) return;  // letter outlines are costly
    // Design "C · Speed stack" (user's pick, 2026-11-15), drawn in a 440x120 box scaled to width w.
    GraphicsState st = g.g->Save();
    g.g->TranslateTransform(x, y);
    g.g->ScaleTransform(w / 440, w / 440);
    // 1. Speed bars.
    const float bars[3][5] = {{64, 58, 60, 6, .9f}, {54, 72, 52, 6, .6f}, {68, 86, 44, 6, .35f}};
    for (auto& b : bars) {
        PushSkew(b[0] + b[2] / 2, b[1] + b[3] / 2, -14);
        FillRect(b[0], b[1], b[2], b[3], Alpha(col::tilt, b[4]));
        Pop();
    }
    // 2. NICE.
    {
        GraphicsPath p;
        AddTextPath(p, L"NICE", F_DISPLAY, 26, 80, 36, 13);
        SolidBrush b(col::gold);
        g.g->FillPath(&b, &p);
    }
    // 3. CRASH: shadow, blue extrusion, outline, gradient face, shine.
    {
        GraphicsPath measure;
        float cw = AddTextPath(measure, L"CRASH", F_DISPLAY, 84, 0, 96, 5);
        GraphicsPath p;
        AddTextPath(p, L"CRASH", F_DISPLAY, 84, 232 - cw / 2, 96, 5);
        auto fillAt = [&](float dx, float dy, const Color& c) {
            GraphicsState s2 = g.g->Save();
            g.g->TranslateTransform(dx, dy);
            SolidBrush b(c);
            g.g->FillPath(&b, &p);
            g.g->Restore(s2);
        };
        fillAt(6, 7, Alpha(col::hill, 0.55f));
        fillAt(4, 4, Color(255, 0x2A, 0x10, 0x60));
        fillAt(3, 3, Color(255, 0x35, 0x4C, 0x95));
        fillAt(2, 2, Color(255, 0x41, 0x88, 0xCA));
        fillAt(1, 1, col::tilt);
        Pen outline(col::hill, 5);
        outline.SetLineJoin(LineJoinRound);
        g.g->DrawPath(&outline, &p);
        LinearGradientBrush face(PointF(0, 36), PointF(0, 100), Color(255, 255, 255, 255), col::gold);
        Color cols[3] = {Color(255, 255, 255, 255), Color(255, 0xFF, 0xE9, 0xA8), col::gold};
        REAL pos[3] = {0, 0.55f, 1};
        face.SetInterpolationColors(cols, pos, 3);
        face.SetWrapMode(WrapModeTileFlipXY);
        g.g->FillPath(&face, &p);
        PushClip(0, 0, 440, 64);
        SolidBrush shine(Color(77, 255, 255, 255));
        g.g->FillPath(&shine, &p);
        Pop();
    }
    // 4. Underbar and star.
    {
        PointF v[4] = {PointF(112, 108), PointF(352, 108), PointF(348, 116), PointF(108, 116)};
        SolidBrush b(col::gold);
        g.g->FillPolygon(&b, v, 4);
        Star(352 + 11, 101 + 11, 11, col::gold);
    }
    g.g->Restore(st);
}

// The emblem in its 400-unit box, in three layers: under the turning ring (the halo), the ring itself, and over it.
static void EmblemUnder() {
    {
        GraphicsPath p;
        p.AddEllipse(10.f, 15.f, 380.f, 380.f);
        PathGradientBrush pg(&p);
        pg.SetCenterPoint(PointF(200, 205));
        pg.SetCenterColor(Alpha(col::glow, 0.3f));
        Color sc = Alpha(col::glow, 0);
        int one = 1;
        pg.SetSurroundColors(&sc, &one);
        g.g->FillPath(&pg, &p);
    }
}

static void EmblemRing(float spin) {
    // Spinning dotted ring with three stars.
    {
        GraphicsState s2 = g.g->Save();
        g.g->TranslateTransform(200, 205);
        g.g->RotateTransform(spin);
        g.g->TranslateTransform(-200, -205);
        Pen dots(Alpha(col::gold, 0.35f), 2);
        REAL dash[2] = {1, 4.5f};
        dots.SetDashPattern(dash, 2);
        g.g->DrawEllipse(&dots, 32.f, 37.f, 336.f, 336.f);
        Star(200, 37, 8, col::gold);
        Star(362, 204, 6, Alpha(col::gold, 0.7f));
        Star(40, 206, 6, Alpha(col::gold, 0.7f));
        g.g->Restore(s2);
    }
}

static void EmblemOver() {
    // Tilt ring, a horizon tipped 12 degrees.
    {
        GraphicsState s2 = g.g->Save();
        g.g->TranslateTransform(200, 205);
        g.g->RotateTransform(-12);
        g.g->TranslateTransform(-200, -205);
        Ring(200, 205, 140, Alpha(col::tilt, 0.9f), 10);
        Line(60, 205, 340, 205, Alpha(col::tilt, 0.5f), 3);
        g.g->Restore(s2);
    }
    // The big star, gold to boost.
    {
        GraphicsPath p;
        StarPath(p, 200, 140, 72);
        LinearGradientBrush br(PointF(128, 68), PointF(272, 212), col::gold, col::boost);
        g.g->FillPath(&br, &p);
    }
    // Speed lines.
    {
        Pen p5(Alpha(col::fg, 0.55f), 5), p4(Alpha(col::fg, 0.55f), 4), p3(Alpha(col::fg, 0.55f), 3);
        for (Pen* p : {&p5, &p4, &p3}) { p->SetStartCap(LineCapRound); p->SetEndCap(LineCapRound); }
        g.g->DrawLine(&p5, 28.f, 236.f, 92.f, 236.f);
        g.g->DrawLine(&p4, 48.f, 258.f, 108.f, 258.f);
        g.g->DrawLine(&p3, 20.f, 280.f, 86.f, 280.f);
    }
    Truck(95, 160, 230, 140);
    // Dust.
    Color d = Alpha(col::gold, 0.8f);
    Circle(96, 318, 5, d);
    Circle(80, 306, 3.5f, d);
    Circle(112, 332, 3, d);
    Circle(70, 326, 2.5f, d);
}

// The still layers are drawn once into window-sized-pixel bitmaps (the halo's soft gradient alone takes tens of ms
// at 1080p) and blitted each frame; only the turning ring is drawn live.
void Emblem(float x, float y, float s, float spin) {
    if (!g.g->IsVisible(RectF(x, y, s, s))) return;
    struct Layers {
        Bitmap* under = nullptr;
        Bitmap* over = nullptr;
        int bx = 0, by = 0;
    };
    static std::map<std::vector<int>, Layers> cache;
    Matrix m;
    g.g->GetTransform(&m);
    REAL e[6];
    m.GetElements(e);
    PointF corners[2] = {PointF(x, y), PointF(x + s, y + s)};
    m.TransformPoints(corners, 2);
    int bx = (int)floorf(corners[0].X) - 2, by = (int)floorf(corners[0].Y) - 2;
    int bw = (int)ceilf(corners[1].X) - bx + 4, bh = (int)ceilf(corners[1].Y) - by + 4;
    std::vector<int> key = {bx, by, bw, bh, (int)(e[0] * 1000), (int)(e[3] * 1000)};
    Layers& L = cache[key];
    if (!L.under) {
        if (cache.size() > 8) {  // window resized a few times: drop the old ones
            for (auto& kv : cache)
                if (&kv.second != &L) { delete kv.second.under; delete kv.second.over; }
            Layers keep = L;
            cache.clear();
            cache[key] = keep;
        }
        Layers& C = cache[key];
        C.bx = bx;
        C.by = by;
        for (int i = 0; i < 2; i++) {
            Bitmap* b = new Bitmap(bw, bh, PixelFormat32bppPARGB);
            Graphics bg(b);
            bg.SetSmoothingMode(SmoothingModeAntiAlias);
            bg.SetPixelOffsetMode(PixelOffsetModeHalf);
            bg.SetCompositingQuality(CompositingQualityHighQuality);
            bg.SetTransform(&m);
            bg.TranslateTransform((REAL)-bx, (REAL)-by, MatrixOrderAppend);
            bg.TranslateTransform(x, y);
            bg.ScaleTransform(s / 400, s / 400);
            Graphics* keepG = g.g;
            g.g = &bg;
            if (i == 0) EmblemUnder();
            else EmblemOver();
            g.g = keepG;
            (i == 0 ? C.under : C.over) = b;
        }
    }
    Layers& C = cache[key];
    auto blit = [&](Bitmap* b) {
        GraphicsState s0 = g.g->Save();
        g.g->ResetTransform();
        g.g->SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.g->SetPixelOffsetMode(PixelOffsetModeNone);
        g.g->SetCompositingQuality(CompositingQualityHighSpeed);  // a 1:1 copy: plain blending is enough
        g.g->DrawImage(b, Rect(C.bx, C.by, (INT)b->GetWidth(), (INT)b->GetHeight()), 0, 0, (INT)b->GetWidth(),
                       (INT)b->GetHeight(), UnitPixel);
        g.g->Restore(s0);
    };
    blit(C.under);
    GraphicsState st = g.g->Save();
    g.g->TranslateTransform(x, y);
    g.g->ScaleTransform(s / 400, s / 400);
    EmblemRing(spin);
    g.g->Restore(st);
    blit(C.over);
}

}  // namespace ui
