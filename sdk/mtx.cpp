// Host SDK: MTX/VEC math. The SDK's PS* versions are paired-single assembly; these are plain C with the same
// semantics (Mtx = f32[3][4] with an implied 0 0 0 1 row, Mtx44 = f32[4][4], conventions of the SDK's C_ versions).
#include <cmath>
#include "sdk.h"

typedef f32 Mtx[3][4];
typedef f32 Mtx44[4][4];
struct Vec {
    f32 x, y, z;
};

extern "C" {

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = 2.0f * tmp; m[0][1] = 0; m[0][2] = 0; m[0][3] = -(r + l) * tmp;
    tmp = 1.0f / (t - b);
    m[1][0] = 0; m[1][1] = 2.0f * tmp; m[1][2] = 0; m[1][3] = -(t + b) * tmp;
    tmp = 1.0f / (f - n);
    m[2][0] = 0; m[2][1] = 0; m[2][2] = -1.0f * tmp; m[2][3] = -f * tmp;
    m[3][0] = 0; m[3][1] = 0; m[3][2] = 0; m[3][3] = 1.0f;
}

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
    f32 angle = fovY * 0.5f * 3.14159265358979323846f / 180.0f;
    f32 cot = 1.0f / tanf(angle);
    m[0][0] = cot / aspect; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
    m[1][0] = 0; m[1][1] = cot; m[1][2] = 0; m[1][3] = 0;
    f32 tmp = 1.0f / (f - n);
    m[2][0] = 0; m[2][1] = 0; m[2][2] = -n * tmp; m[2][3] = -(f * n) * tmp;
    m[3][0] = 0; m[3][1] = 0; m[3][2] = -1.0f; m[3][3] = 0;
}

void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    Mtx t;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++)
            t[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + (c == 3 ? a[r][3] : 0.0f);
    }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) ab[r][c] = t[r][c];
}

u32 PSMTXInvXpose(const Mtx src, Mtx inv) {
    f32 det = src[0][0] * src[1][1] * src[2][2] + src[0][1] * src[1][2] * src[2][0] +
              src[0][2] * src[1][0] * src[2][1] - src[2][0] * src[1][1] * src[0][2] -
              src[1][0] * src[0][1] * src[2][2] - src[0][0] * src[2][1] * src[1][2];
    if (det == 0.0f) return 0;
    det = 1.0f / det;
    Mtx t;
    t[0][0] = (src[1][1] * src[2][2] - src[2][1] * src[1][2]) * det;
    t[0][1] = -(src[1][0] * src[2][2] - src[2][0] * src[1][2]) * det;
    t[0][2] = (src[1][0] * src[2][1] - src[2][0] * src[1][1]) * det;
    t[1][0] = -(src[0][1] * src[2][2] - src[2][1] * src[0][2]) * det;
    t[1][1] = (src[0][0] * src[2][2] - src[2][0] * src[0][2]) * det;
    t[1][2] = -(src[0][0] * src[2][1] - src[2][0] * src[0][1]) * det;
    t[2][0] = (src[0][1] * src[1][2] - src[1][1] * src[0][2]) * det;
    t[2][1] = -(src[0][0] * src[1][2] - src[1][0] * src[0][2]) * det;
    t[2][2] = (src[0][0] * src[1][1] - src[1][0] * src[0][1]) * det;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) inv[r][c] = t[r][c];
        inv[r][3] = 0.0f;
    }
    return 1;
}

void PSMTXScale(Mtx m, f32 x, f32 y, f32 z) {
    m[0][0] = x; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
    m[1][0] = 0; m[1][1] = y; m[1][2] = 0; m[1][3] = 0;
    m[2][0] = 0; m[2][1] = 0; m[2][2] = z; m[2][3] = 0;
}

void PSMTXTrans(Mtx m, f32 x, f32 y, f32 z) {
    m[0][0] = 1; m[0][1] = 0; m[0][2] = 0; m[0][3] = x;
    m[1][0] = 0; m[1][1] = 1; m[1][2] = 0; m[1][3] = y;
    m[2][0] = 0; m[2][1] = 0; m[2][2] = 1; m[2][3] = z;
}

void PSMTXTransApply(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) dst[r][c] = src[r][c];
    dst[0][3] = src[0][3] + x;
    dst[1][3] = src[1][3] + y;
    dst[2][3] = src[2][3] + z;
}

void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* out) {
    Vec t = {a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x};
    *out = t;
}
f32 PSVECDotProduct(const Vec* a, const Vec* b) { return a->x * b->x + a->y * b->y + a->z * b->z; }
f32 PSVECMag(const Vec* v) { return sqrtf(PSVECDotProduct(v, v)); }
f32 PSVECSquareDistance(const Vec* a, const Vec* b) {
    f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}
f32 PSVECDistance(const Vec* a, const Vec* b) { return sqrtf(PSVECSquareDistance(a, b)); }
void PSVECNormalize(const Vec* src, Vec* dst) {
    f32 m = PSVECMag(src);
    f32 k = m > 0 ? 1.0f / m : 0.0f;
    dst->x = src->x * k;
    dst->y = src->y * k;
    dst->z = src->z * k;
}

}  // extern "C"
