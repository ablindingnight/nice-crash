// The Home truck run (see truckrun.h). A port of the mockup's script (launcher_home v11): same constants, same order.
#include "truckrun.h"
#include <cmath>
#include <vector>
#include "ui.h"

using namespace Gdiplus;

namespace truckrun {
namespace {

const float K = 0.27f, V0 = 40, VLIP = 230, KICK = 90, SPIN = 390, G = 420;  // scale, px/s, px/s, px/s, deg/s, px/s^2
const float GAP = 30;                                                          // seconds off screen (launcher)
const float kPi = 3.14159265f;

struct Pt { float s, x, y; };
std::vector<Pt> table;  // the drive line, flattened: arc length, x, y
float L = 0;

void Build() {
    if (!table.empty()) return;
    GraphicsPath* p = ui::SvgPath("M-90 130 L0 128 C 120 116, 220 136, 340 122 S 560 106, 700 120 C 800 128, 880 128, "
                                  "960 118 C 1000 116, 1050 108, 1080 92");
    p->Flatten(nullptr, 0.1f);
    int n = p->GetPointCount();
    std::vector<PointF> pts(n);
    p->GetPathPoints(pts.data(), n);
    delete p;
    float s = 0;
    for (int i = 0; i < n; i++) {
        if (i) s += hypotf(pts[i].X - pts[i - 1].X, pts[i].Y - pts[i - 1].Y);
        table.push_back({s, pts[i].X, pts[i].Y});
    }
    L = s;
}

PointF At(float s) {  // like SVG getPointAtLength
    s = std::max(0.f, std::min(L, s));
    for (size_t i = 1; i < table.size(); i++)
        if (table[i].s >= s) {
            const Pt &a = table[i - 1], &b = table[i];
            float t = (s - a.s) / std::max(1e-6f, b.s - a.s);
            return PointF(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        }
    return PointF(table.back().x, table.back().y);
}

void Ground(float x, float* y, float* s) {  // ground height and arc length at x
    for (size_t i = 1; i < table.size(); i++)
        if (table[i].x >= x) {
            const Pt &a = table[i - 1], &b = table[i];
            float t = (x - a.x) / ((b.x - a.x) != 0 ? (b.x - a.x) : 1);
            *y = a.y + (b.y - a.y) * t;
            *s = a.s + (b.s - a.s) * t;
            return;
        }
    *y = table.back().y;
    *s = table.back().s;
}

enum Mode { DRIVE, AIR, GAPM, ENTER, PARKED };
struct State {
    Mode mode = DRIVE;
    float s = 0, v = V0, x = 0, y = 0, vx = 0, vy = 0, rot = 0, spin = 0, wait = 0, t = 0, bob = 0;
} st;
float wheel = 0, trail = 0;

struct Puff { float x, y, vx, vy, r, life; };
std::vector<Puff> puffs;
unsigned rng = 12345;
float Rand() {  // 0..1
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / 16777216.f;
}

void AddPuff(float x, float y, int n = 7, float spread = 12, bool back = false) {
    for (int i = 0; i < n; i++)
        puffs.push_back({x + (Rand() * 2 - 1) * spread, y - 2, back ? -30 - Rand() * 30 : (Rand() * 2 - 1) * 60,
                         -20 - Rand() * 30, 2 + Rand() * 3, 0.7f});
}

void SpinWheels(float v, float dt) { wheel += v * dt / (32 * K) * 180 / kPi; }

void Place(float s) {
    PointF p = At(s), p2 = At(std::min(L, s + 6)), p0 = At(std::max(0.f, s - 6));
    st.x = p.X;
    st.y = p.Y;
    st.rot = atan2f(p2.Y - p0.Y, p2.X - p0.X) * 180 / kPi;
}

}  // namespace

void Step(float dt) {
    Build();
    if (st.mode == PARKED) st = State();  // the animation was switched back on
    st.t += dt;
    if (st.mode == DRIVE) {
        // Build up speed: constant acceleration chosen so the truck reaches VLIP exactly at the lip, from wherever
        // this lap started (the left edge, or the landing spot after flying back in).
        float left = std::max(1.f, L - st.s), a = std::max(0.f, (VLIP * VLIP - st.v * st.v) / (2 * left));
        st.v = std::min(VLIP, st.v + a * dt);
        st.s += st.v * dt;
        SpinWheels(st.v, dt);
        trail += dt * std::max(0.f, st.v - 110) / 40;  // a faint dust trail that grows with speed
        if (trail >= 1) { trail = 0; AddPuff(st.x - 10, st.y, 1, 4, true); }
        if (st.s >= L) {  // off the lip: launch from the real speed along the lip's tangent, plus a kick
            PointF e = At(L), e0 = At(L - 8);
            float ang = atan2f(e.Y - e0.Y, e.X - e0.X), k = st.v / VLIP;
            st.mode = AIR;
            st.x = e.X;
            st.y = e.Y;
            st.vx = st.v * cosf(ang);
            st.vy = st.v * sinf(ang) - KICK * k;
            st.spin = -SPIN * k;  // a faster arrival flips harder
            AddPuff(e.X, e.Y);
        } else {
            Place(st.s);
            st.bob = sinf(st.t * (8 + st.v / 20)) * (3 + st.v / 40);
        }
    }
    if (st.mode == AIR || st.mode == ENTER) {
        st.vy += G * dt;
        st.x += st.vx * dt;
        st.y += st.vy * dt;
        st.bob = 0;
        if (st.mode == AIR) st.rot += st.spin * dt;
        else st.rot += ((atan2f(st.vy, st.vx) * 180 / kPi) * 0.5f - st.rot) * std::min(1.f, dt * 6);
        if (st.mode == AIR && st.x > 1360) { st.mode = GAPM; st.wait = GAP; }
        if (st.mode == ENTER) {
            float gy, gs;
            Ground(st.x, &gy, &gs);
            if (st.y >= gy && st.vy > 0) {
                st.y = gy;
                if (st.vy > 90) { AddPuff(st.x, st.y); st.vy = -st.vy * 0.28f; }
                else { st.mode = DRIVE; st.s = gs; st.v = V0; }  // back to the slow start speed
            }
        }
        if (st.mode == AIR || st.mode == ENTER) SpinWheels(st.mode == AIR ? 120.f : 60.f, dt);
    }
    if (st.mode == GAPM) {
        st.wait -= dt;
        if (st.wait <= 0) { st.mode = ENTER; st.x = -70; st.y = 18; st.vx = 170; st.vy = -40; st.rot = 8; }
    }
    for (size_t i = puffs.size(); i-- > 0;) {
        Puff& f = puffs[i];
        f.life -= dt;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        f.vy += 60 * dt;
        if (f.life <= 0) puffs.erase(puffs.begin() + i);
    }
}

void Park() {
    Build();
    if (st.mode == PARKED) return;
    float gy, gs;
    Ground(300, &gy, &gs);
    st = State();
    st.mode = PARKED;
    Place(gs);
    puffs.clear();
}

bool Bounds(float Lc, float sx, float* x0, float* y0, float* x1, float* y1) {
    Build();
    bool any = false;
    auto add = [&](float cx, float cy, float r) {
        if (!any) { *x0 = cx - r; *y0 = cy - r; *x1 = cx + r; *y1 = cy + r; any = true; return; }
        *x0 = std::min(*x0, cx - r); *y0 = std::min(*y0, cy - r);
        *x1 = std::max(*x1, cx + r); *y1 = std::max(*y1, cy + r);
    };
    // The truck spans about 62x38 canvas units around a point 19 above its anchor; any rotation fits in radius 44.
    if (st.mode != GAPM) {
        float a = st.rot * kPi / 180;
        add(Lc + st.x * sx + 19 * sinf(a), 460 + st.y - 19 * cosf(a), 44);
    }
    for (auto& f : puffs) add(Lc + f.x * sx, 460 + f.y, f.r * 1.6f + 2);
    return any;
}

float Speed() { return st.v; }
const char* Mode() {
    static const char* n[] = {"drive", "air", "gap", "enter", "parked"};
    return n[st.mode];
}

void Draw(float Lc, float sx) {
    Build();
    if (st.mode == DRIVE && st.t == 0) Place(st.s);  // first frame
    Graphics* gr = ui::g.g;
    // Dust (behind the truck), in hills coordinates mapped to the canvas.
    for (auto& f : puffs) {
        float r = f.r * (1.6f - f.life), a = f.life / 0.7f * 0.8f;
        ui::Circle(Lc + f.x * sx, 460 + f.y, r, ui::Alpha(ui::col::hill, a));
    }
    if (st.mode == GAPM) return;
    // The truck: our emblem truck as one solid silhouette, unrotated in its symbol, anchored at the wheels' bottom
    // centre (207,294), scale K. Drawn unstretched even when the hills are stretched to a wide window.
    static GraphicsPath* body = ui::SvgPath("M104 250 L118 218 L170 212 L192 178 L256 176 L280 210 L306 216 L310 252 Z");
    static GraphicsPath* win = ui::SvgPath("M198 186 L250 185 L266 208 L190 210 Z");
    GraphicsState s0 = gr->Save();
    gr->TranslateTransform(Lc + st.x * sx, 460 + st.y);
    gr->RotateTransform(st.rot);
    gr->ScaleTransform(K, K);
    gr->TranslateTransform(-207, -294);
    SolidBrush dark(ui::col::hill), glass(Color(71, 0xFF, 0x7A, 0x2E));
    GraphicsState s1 = gr->Save();
    gr->TranslateTransform(0, st.bob);
    gr->FillPath(&dark, body);
    gr->FillPath(&glass, win);
    gr->Restore(s1);
    Pen spoke(Color(89, 0xFF, 0x7A, 0x2E), 5);
    for (float cx : {142.f, 272.f}) {
        gr->FillEllipse(&dark, cx - 32, 262 - 32.f, 64.f, 64.f);
        GraphicsState s2 = gr->Save();
        gr->TranslateTransform(cx, 262);
        gr->RotateTransform(fmodf(wheel, 360));
        gr->DrawLine(&spoke, 0.f, -18.f, 0.f, 18.f);
        gr->DrawLine(&spoke, -18.f, 0.f, 18.f, 0.f);
        gr->Restore(s2);
    }
    gr->Restore(s0);
}

}  // namespace truckrun
