// GX TEV as GLSL: each distinct TEV configuration (stage count, combiner inputs/ops, orders, konst selections, swap
// tables, alpha compare) becomes one generated fragment program; register and konst colours are uniforms. Covers
// the colour/alpha combiners including the compare modes, konst colours, raster/texture swap tables and the alpha
// compare with its two references and logic op. Indirect texturing and fog are not emulated yet.
// Register layouts follow public GX documentation (YAGCD, Dolphin's VideoCommon); this code is the port's own.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <unordered_map>
#include "gxinterp.h"
#include "gxtev.h"

namespace {

typedef char GLchar_;
#define GL_FRAGMENT_SHADER_ 0x8B30
#define GL_VERTEX_SHADER_ 0x8B31
#define GL_COMPILE_STATUS_ 0x8B81
#define GL_LINK_STATUS_ 0x8B82
#define GL_TEXTURE0_ 0x84C0

#define GLFUNCS(X)                                                                         \
    X(GLuint, glCreateShader, (GLenum))                                                    \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar_* const*, const GLint*))        \
    X(void, glCompileShader, (GLuint))                                                     \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*))                                       \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar_*))                     \
    X(GLuint, glCreateProgram, (void))                                                     \
    X(void, glAttachShader, (GLuint, GLuint))                                              \
    X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar_*))                        \
    X(void, glLinkProgram, (GLuint))                                                       \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*))                                      \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar_*))                    \
    X(void, glUseProgram, (GLuint))                                                        \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar_*))                               \
    X(void, glUniform1i, (GLint, GLint))                                                   \
    X(void, glUniform1iv, (GLint, GLsizei, const GLint*))                                  \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat))                                        \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat*))                                    X(void, glUniform3fv, (GLint, GLsizei, const GLfloat*))                                    X(void, glUniform2fv, (GLint, GLsizei, const GLfloat*))                                    X(void, glUniform1fv, (GLint, GLsizei, const GLfloat*))                                \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))               \
    X(void, glActiveTexture, (GLenum))                                                     \
    X(void, glMultiTexCoord2f, (GLenum, GLfloat, GLfloat))                                     X(void, glClientActiveTexture, (GLenum))                                                   X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))     X(void, glEnableVertexAttribArray, (GLuint))                                               X(void, glDisableVertexAttribArray, (GLuint))

#define DECL(ret, name, args) typedef ret(APIENTRY* name##_t) args; name##_t p_##name;
GLFUNCS(DECL)
#undef DECL

// Blend entry points (named by hand: the system gl.h may declare these names itself).
typedef void(APIENTRY* BlendFuncSep_t)(GLenum, GLenum, GLenum, GLenum);
typedef void(APIENTRY* BlendEqSep_t)(GLenum, GLenum);
typedef void(APIENTRY* BlendColor_t)(GLfloat, GLfloat, GLfloat, GLfloat);
BlendFuncSep_t p_blendFuncSep;
BlendEqSep_t p_blendEqSep;
BlendColor_t p_blendColor;

int g_ok = -1;  // -1 = not tried yet

bool Load() {
    if (g_ok >= 0) return g_ok;
    g_ok = 1;
#define LOAD(ret, name, args)                                         \
    p_##name = (name##_t)(void*)wglGetProcAddress(#name);              \
    if (!p_##name) {                                                   \
        fprintf(stderr, "[tev] %s missing: TEV shaders off\n", #name); \
        g_ok = 0;                                                      \
    }
    GLFUNCS(LOAD)
#undef LOAD
    p_blendFuncSep = (BlendFuncSep_t)(void*)wglGetProcAddress("glBlendFuncSeparate");
    p_blendEqSep = (BlendEqSep_t)(void*)wglGetProcAddress("glBlendEquationSeparate");
    p_blendColor = (BlendColor_t)(void*)wglGetProcAddress("glBlendColor");
    if (getenv("NICECRASH_NOTEV")) g_ok = 0;
    return g_ok;
}

struct Prog {
    GLuint id;
    GLint reg, kon, aref, tex, tdim, imtx, ishift;
};
std::unordered_map<std::string, Prog> g_progs;

const char* kVS =
    "#version 120\n"
    "attribute vec4 col1in;\n"
    "varying vec4 col1;\n"
    "void main() {\n"
    "  gl_Position = ftransform();\n"
    "  gl_FrontColor = gl_Color;\n"
    "  col1 = col1in;\n"
    "  gl_TexCoord[0] = gl_MultiTexCoord0; gl_TexCoord[1] = gl_MultiTexCoord1;\n"
    "  gl_TexCoord[2] = gl_MultiTexCoord2; gl_TexCoord[3] = gl_MultiTexCoord3;\n"
    "  gl_TexCoord[4] = gl_MultiTexCoord4; gl_TexCoord[5] = gl_MultiTexCoord5;\n"
    "  gl_TexCoord[6] = gl_MultiTexCoord6; gl_TexCoord[7] = gl_MultiTexCoord7;\n"
    "}\n";

const char* kCIn[16] = {"r0.rgb", "r0.aaa", "r1.rgb", "r1.aaa", "r2.rgb", "r2.aaa", "r3.rgb", "r3.aaa",
                        "tx.rgb", "tx.aaa", "ras.rgb", "ras.aaa", "vec3(1.0)", "vec3(0.5)", "kc", "vec3(0.0)"};
const char* kAIn[8] = {"r0.a", "r1.a", "r2.a", "r3.a", "tx.a", "ras.a", "ka", "0.0"};
const char* kReg[4] = {"r0", "r1", "r2", "r3"};

std::string Swizzle(int table) {
    u32 k0 = g_gx.bp[0xF6 + table * 2], k1 = g_gx.bp[0xF7 + table * 2];
    const char* c = "rgba";
    std::string s = ".";
    s += c[k0 & 3];
    s += c[(k0 >> 2) & 3];
    s += c[k1 & 3];
    s += c[(k1 >> 2) & 3];
    return s;
}

std::string KColor(int sel) {
    if (sel < 8) {
        char b[32];
        snprintf(b, sizeof b, "vec3(%.6f)", (8 - sel) / 8.0);
        return b;
    }
    if (sel < 12) return "vec3(1.0)";  // reserved
    if (sel < 16) return std::string("kon[") + char('0' + (sel - 12)) + "].rgb";
    const char* comp = "rgba";
    return std::string("kon[") + char('0' + (sel & 3)) + "]." + std::string(3, comp[(sel - 16) / 4]);
}

std::string KAlpha(int sel) {
    if (sel < 8) {
        char b[32];
        snprintf(b, sizeof b, "%.6f", (8 - sel) / 8.0);
        return b;
    }
    if (sel < 16) return "1.0";  // reserved
    const char* comp = "rgba";
    return std::string("kon[") + char('0' + (sel & 3)) + "]." + comp[(sel - 16) / 4];
}

const char* kBias[3] = {"", " + 0.5", " - 0.5"};
const char* kScale[4] = {"1.0", "2.0", "4.0", "0.5"};

// Packed comparison operands of the compare modes (8-bit integer values).
std::string Packed(const std::string& v, int mode) {
    switch (mode >> 1) {
    case 0: return "q8(" + v + ".r)";
    case 1: return "(q8(" + v + ".g) * 256.0 + q8(" + v + ".r))";
    default: return "(q8(" + v + ".b) * 65536.0 + q8(" + v + ".g) * 256.0 + q8(" + v + ".r))";
    }
}

std::string BuildFS(int nStages, const bool texOk[8]) {
    std::string s =
        "#version 120\n"
        "uniform sampler2D tex[8];\n"
        "uniform vec4 reg[4];\n"
        "uniform vec4 kon[4];\n"
        "uniform vec2 aref;\n"
        "uniform vec2 tdim[8];\n"
        "uniform vec3 imtx[6];\n"
        "uniform float ishift[3];\n"
        "varying vec4 col1;\n"
        "float q8(float x) { return floor(clamp(x, 0.0, 1.0) * 255.0 + 0.5); }\n"
        "vec3 q8(vec3 x) { return floor(clamp(x, 0.0, 1.0) * 255.0 + 0.5); }\n"
        // Manual texture sampling, as the hardware (and Dolphin's \"manual texture sampling\"): the coordinate in
        // 1/128 texel, the texel index wrapped by the texture's own mode (0 clamp, 1 repeat, 2 mirror), bilinear
        // weights in 1/16, every fetch at an exact texel centre. The GL sampler's rounding drew a band of shifted
        // blocks across the Bink title video.
        "float wrapc(float i, float n, float m) {\n"
        "  if (m == 1.0) return mod(i, n);\n"
        "  if (m == 2.0) { float k = mod(i, 2.0 * n); return k < n ? k : 2.0 * n - 1.0 - k; }\n"
        "  return clamp(i, 0.0, n - 1.0);\n"
        "}\n"
        "vec4 fetch(sampler2D t, vec2 i, vec2 n, vec2 m) {\n"
        "  return texture2D(t, (vec2(wrapc(i.x, n.x, m.x), wrapc(i.y, n.y, m.y)) + 0.5) / n);\n"
        "}\n"
        "vec4 sampleGX(sampler2D t, vec2 tc, vec2 n, vec2 m, float lin) {\n"
        "  if (lin < 0.5) return fetch(t, floor(tc / 128.0), n, m);\n"
        "  vec2 c = tc - 64.0;\n"
        "  vec2 i0 = floor(c / 128.0);\n"
        "  vec2 f = floor((c - i0 * 128.0) / 8.0) / 16.0;\n"
        "  vec4 a = fetch(t, i0, n, m), b = fetch(t, i0 + vec2(1.0, 0.0), n, m);\n"
        "  vec4 d = fetch(t, i0 + vec2(0.0, 1.0), n, m), e = fetch(t, i0 + vec2(1.0, 1.0), n, m);\n"
        "  return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);\n"
        "}\n"
        "void main() {\n"
        "  vec4 r0 = reg[0], r1 = reg[1], r2 = reg[2], r3 = reg[3];\n"
        "  vec4 tx = vec4(0.0), ras = vec4(0.0);\n"
        "  vec3 kc = vec3(0.0); float ka = 0.0;\n"
        "  vec3 ca, cb, cc, cd; float aa, ab, ac, ad;\n"
        "  vec2 tc = vec2(0.0), prevtc = vec2(0.0), itr; vec3 ic;\n";
    int lastC = 0, lastA = 0;
    char b[512];
    // Indirect stages (BP 0x27 order, 0x25/0x26 coordinate scales): each looks up its texture once; the 8-bit values
    // (alpha, blue, green = s, t, u) feed the TEV stages that reference the stage.
    int nInd = (g_gx.bp[0x00] >> 16) & 7;
    for (int i = 0; i < nInd && i < 4; i++) {
        u32 iref = g_gx.bp[0x27] >> (6 * i);
        int imap = iref & 7, icoord = (iref >> 3) & 7;
        u32 ss = g_gx.bp[0x25 + i / 2] >> ((i & 1) * 8);
        int sS = ss & 15, sT = (ss >> 4) & 15;
        if (texOk[imap]) {
            // Fixed point as the hardware: the coordinate in 1/128 texels, the texel index wrapped by repeat, then
            // a texel-centre sample (the GL sampler's own rounding put a row of the Bink ramp lookups one row off).
            snprintf(b, sizeof b,
                     "  vec2 ic%d = floor(floor(gl_TexCoord[%d].st * tdim[%d] * 128.0 / vec2(%d.0, %d.0)) / 128.0);\n"
                     "  vec3 ind%d = floor(texture2D(tex[%d], (mod(ic%d, tdim[%d]) + 0.5) / tdim[%d]).abg * 255.0 + 0.5);\n",
                     i, icoord, imap, 1 << sS, 1 << sT, i, imap, i, imap, imap);
        } else {
            snprintf(b, sizeof b, "  vec3 ind%d = vec3(0.0);\n", i);
        }
        s += b;
    }
    for (int st = 0; st < nStages; st++) {
        u32 ord = g_gx.bp[0x28 + st / 2] >> ((st & 1) * 12);
        int map = ord & 7, coord = (ord >> 3) & 7, en = (ord >> 6) & 1, chan = (ord >> 7) & 7;
        u32 cenv = g_gx.bp[0xC0 + st * 2], aenv = g_gx.bp[0xC1 + st * 2];
        u32 ksel = g_gx.bp[0xF6 + st / 2] >> ((st & 1) * 10);
        int kcs = (ksel >> 4) & 31, kas = (ksel >> 9) & 31;
        int rswap = aenv & 3, tswap = (aenv >> 2) & 3;
        snprintf(b, sizeof b, "  // stage %d: map %d coord %d en %d chan %d cenv %06x aenv %06x\n", st, map, coord, en, chan,
                 cenv, aenv);
        s += b;
        // Indirect texturing for this stage (BP 0x10 + stage): coordinates in 1/128 texel units, as the hardware.
        u32 ind = g_gx.bp[0x10 + st];
        int bt = ind & 3, ifmt = (ind >> 2) & 3, ibias = (ind >> 4) & 7, mid = (ind >> 9) & 15;
        int sw = (ind >> 13) & 7, tw = (ind >> 16) & 7, addprev = (ind >> 20) & 1;
        bool indOn = nInd > 0 && (mid || sw || tw || addprev);
        if (indOn) {
            static const int kWrap[8] = {0, 256, 128, 64, 32, 16, 0, 0};
            snprintf(b, sizeof b, "  tc = floor(gl_TexCoord[%d].st * tdim[%d] * 128.0);\n", coord, map);
            s += b;
            if (sw) s += sw == 6 ? "  tc.x = 0.0;\n" : "  tc.x = mod(tc.x, " + std::to_string(kWrap[sw] * 128) + ".0);\n";
            if (tw) s += tw == 6 ? "  tc.y = 0.0;\n" : "  tc.y = mod(tc.y, " + std::to_string(kWrap[tw] * 128) + ".0);\n";
            if (mid && bt < nInd) {
                static const int kBits[4] = {256, 32, 16, 8};
                snprintf(b, sizeof b, "  ic = mod(ind%d, %d.0);\n", bt, kBits[ifmt]);
                s += b;
                const char* comp = "xyz";
                for (int k = 0; k < 3; k++)
                    if ((ibias >> k) & 1) s += std::string("  ic.") + comp[k] + (ifmt == 0 ? " -= 128.0;\n" : " += 1.0;\n");
                int m = (mid & 3) - 1;
                if (m >= 0) {
                    if (mid < 4) {
                        snprintf(b, sizeof b, "  itr = floor(vec2(dot(imtx[%d], ic), dot(imtx[%d], ic)) / 8.0);\n", 2 * m, 2 * m + 1);
                    } else {  // dynamic S/T matrices: the coordinate times one indirect component
                        snprintf(b, sizeof b, "  itr = floor(tc * ic.%c / 256.0);\n", mid < 8 ? 'x' : 'y');
                    }
                    s += b;
                    snprintf(b, sizeof b, "  itr = ishift[%d] >= 0.0 ? floor(itr / exp2(ishift[%d])) : itr * exp2(-ishift[%d]);\n", m,
                             m, m);
                    s += b;
                    s += "  tc += itr;\n";
                }
            }
            if (addprev) s += "  tc += prevtc;\n";
            s += "  prevtc = tc;\n";
        } else {
            s += "  prevtc = vec2(0.0);\n";
        }
        if (en && texOk[map]) {
            if (indOn)
            {
                u32 mode0 = g_gx.bp[(map < 4 ? 0x80 : 0xA0) + (map & 3)];
                static const int kWrapMode[4] = {0, 1, 2, 1};
                snprintf(b, sizeof b, "  tx = sampleGX(tex[%d], tc, tdim[%d], vec2(%d.0, %d.0), %d.0)%s;\n", map, map,
                         kWrapMode[mode0 & 3], kWrapMode[(mode0 >> 2) & 3], (int)((mode0 >> 4) & 1), Swizzle(tswap).c_str());
            }
            else
                snprintf(b, sizeof b, "  tx = texture2D(tex[%d], gl_TexCoord[%d].st)%s;\n", map, coord, Swizzle(tswap).c_str());
            s += b;
        } else {
            s += "  tx = vec4(0.0);\n";
        }
        if (const char* dbg = getenv("NICECRASH_TEVDBG"))  // debugging: show this stage's texture sample (and coords)
            if (atoi(dbg + (*dbg >= 'a')) == st && nStages > 3) {
                // c<stage>: final texel coordinate (X low in r, X high in g, Y mod 256 in b); b<stage>: the same
                // before the indirect offset; i<stage>: the biased indirect values + 128; else: sample + coords.
                if (*dbg == 'c' || *dbg == 'b')
                    s += std::string("  { vec2 t = floor((prevtc") + (*dbg == 'b' ? " - itr" : "") +
                         ") / 128.0); gl_FragColor = vec4(mod(t.x, 256.0) / 255.0, floor(t.x / 256.0) / 255.0, "
                         "mod(t.y, 256.0) / 255.0, 1.0); return; }\n";
                else if (*dbg == 'i')
                    s += "  gl_FragColor = vec4((ic + 128.0) / 255.0, 1.0); return;\n";
                else
                    s += "  gl_FragColor = vec4(tx.r, fract(prevtc / 128.0 / 64.0), 1.0); return;\n";
            }
        if (chan == 0) s += "  ras = gl_Color" + Swizzle(rswap) + ";\n";
        else if (chan == 1) s += "  ras = col1" + Swizzle(rswap) + ";\n";
        else s += "  ras = vec4(0.0);\n";
        s += "  kc = " + KColor(kcs) + "; ka = " + KAlpha(kas) + ";\n";
        // Colour combiner.
        int cd = cenv & 15, cc = (cenv >> 4) & 15, cb = (cenv >> 8) & 15, ca = (cenv >> 12) & 15;
        int cbias = (cenv >> 16) & 3, cop = (cenv >> 18) & 1, cclamp = (cenv >> 19) & 1, cscale = (cenv >> 20) & 3;
        int cdest = (cenv >> 22) & 3;
        s += std::string("  ca = clamp(") + kCIn[ca] + ", 0.0, 1.0); cb = clamp(" + kCIn[cb] + ", 0.0, 1.0); cc = clamp(" +
             kCIn[cc] + ", 0.0, 1.0); cd = " + kCIn[cd] + ";\n";
        // Alpha combiner.
        int ad = (aenv >> 4) & 7, ac = (aenv >> 7) & 7, ab = (aenv >> 10) & 7, aa = (aenv >> 13) & 7;
        int abias = (aenv >> 16) & 3, aop = (aenv >> 18) & 1, aclamp = (aenv >> 19) & 1, ascale = (aenv >> 20) & 3;
        int adest = (aenv >> 22) & 3;
        s += std::string("  aa = clamp(") + kAIn[aa] + ", 0.0, 1.0); ab = clamp(" + kAIn[ab] + ", 0.0, 1.0); ac = clamp(" +
             kAIn[ac] + ", 0.0, 1.0); ad = " + kAIn[ad] + ";\n";
        std::string cres, ares;
        if (cbias != 3) {
            cres = std::string("(cd ") + (cop ? "-" : "+") + " mix(ca, cb, cc)" + kBias[cbias] + ") * " + kScale[cscale];
        } else {
            int mode = cscale << 1 | cop;
            if (mode >= 6) {
                cres = mode == 6 ? "cd + cc * vec3(greaterThan(q8(ca), q8(cb)))" : "cd + cc * vec3(equal(q8(ca), q8(cb)))";
            } else {
                std::string pa = Packed("ca", mode), pb = Packed("cb", mode);
                cres = "cd + cc * ((" + pa + (mode & 1 ? " == " : " > ") + pb + ") ? 1.0 : 0.0)";
            }
        }
        if (abias != 3) {
            ares = std::string("(ad ") + (aop ? "-" : "+") + " mix(aa, ab, ac)" + kBias[abias] + ") * " + kScale[ascale];
        } else {
            int mode = ascale << 1 | aop;
            if (mode >= 6) {
                ares = std::string("ad + ac * ((q8(aa)") + (mode == 6 ? " > " : " == ") + "q8(ab)) ? 1.0 : 0.0)";
            } else {  // packed compares take the colour inputs of the stage
                std::string pa = Packed("ca", mode), pb = Packed("cb", mode);
                ares = "ad + ac * ((" + pa + (mode & 1 ? " == " : " > ") + pb + ") ? 1.0 : 0.0)";
            }
        }
        s += std::string("  ") + kReg[cdest] + ".rgb = " +
             (cclamp ? "clamp(" + cres + ", 0.0, 1.0)" : "clamp(" + cres + ", -4.0, 4.0)") + ";\n";
        s += std::string("  ") + kReg[adest] + ".a = " +
             (aclamp ? "clamp(" + ares + ", 0.0, 1.0)" : "clamp(" + ares + ", -4.0, 4.0)") + ";\n";
        lastC = cdest;
        lastA = adest;
    }
    s += std::string("  vec4 outc = clamp(vec4(") + kReg[lastC] + ".rgb, " + kReg[lastA] + ".a), 0.0, 1.0);\n";
    // Alpha compare (BP 0xF3): two comparisons combined by a logic op; refs are 8-bit.
    u32 acmp = g_gx.bp[0xF3];
    int comp0 = (acmp >> 16) & 7, comp1 = (acmp >> 19) & 7, logic = (acmp >> 22) & 3;
    static const char* kCmp[8] = {"false", "(a8 < aref.x)", "(a8 == aref.x)", "(a8 <= aref.x)",
                                  "(a8 > aref.x)", "(a8 != aref.x)", "(a8 >= aref.x)", "true"};
    if (!(comp0 == 7 && comp1 == 7 && logic == 0)) {
        std::string c1 = kCmp[comp1];
        for (size_t i; (i = c1.find("aref.x")) != std::string::npos;) c1.replace(i, 6, "aref.y");
        static const char* kLogic[4] = {" && ", " || ", " != ", " == "};
        s += "  float a8 = q8(outc.a);\n";
        s += std::string("  if (!(") + kCmp[comp0] + kLogic[logic] + c1 + ")) discard;\n";
    }
    s += "  gl_FragColor = outc;\n}\n";
    return s;
}

GLuint Compile(GLenum type, const std::string& src) {
    GLuint sh = p_glCreateShader(type);
    const GLchar_* p = src.c_str();
    p_glShaderSource(sh, 1, &p, nullptr);
    p_glCompileShader(sh);
    GLint ok = 0;
    p_glGetShaderiv(sh, GL_COMPILE_STATUS_, &ok);
    if (!ok) {
        char log[2048];
        p_glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        fprintf(stderr, "[tev] shader compile failed:\n%s\n%s\n", log, src.c_str());
        return 0;
    }
    return sh;
}

GLuint g_vs;
Prog* g_cur;

float S11(u32 v) {  // 11-bit signed TEV register component -> float (255 = 1.0)
    int x = (int)(v & 0x7FF);
    if (x & 0x400) x -= 0x800;
    return x / 255.0f;
}

}  // namespace

bool gxt_begin(const bool texOk[8]) {
    if (!Load()) return false;
    int nStages = ((g_gx.bp[0x00] >> 10) & 15) + 1;
    // Fast path: a numeric hash of the same register fields the string key below is made of (the string is built
    // only for a new combination; tens of thousands of draws per frame look programs up).
    u64 h = 1469598103934665603ull;
    auto mix = [&](u32 v) { h = (h ^ v) * 1099511628211ull; };
    mix((u32)nStages);
    mix(g_gx.bp[0xF3] & 0xFFFFFF);
    for (int i = 0; i < 8; i++) mix(texOk[i]);
    for (int st = 0; st < nStages; st++) {
        mix(g_gx.bp[0xC0 + st * 2] & 0xFFFFFF);
        mix(g_gx.bp[0xC1 + st * 2] & 0xFFFFFF);
        mix(g_gx.bp[0x10 + st] & 0x1FFFFF);
    }
    mix(g_gx.bp[0x00] & 0x7FFFFF);
    mix(g_gx.bp[0x25] & 0xFFFFFF);
    mix(g_gx.bp[0x26] & 0xFFFFFF);
    mix(g_gx.bp[0x27] & 0xFFFFFF);
    for (int m = 0; m < 8; m++) mix(g_gx.bp[(m < 4 ? 0x80 : 0xA0) + (m & 3)] & 0x1F);
    for (int i = 0; i < 8; i++) {
        mix(g_gx.bp[0x28 + i] & 0xFFFFFF);
        mix(g_gx.bp[0xF6 + i] & 0xFFFFFF);
    }
    static std::unordered_map<u64, Prog*> fast;
    static const bool tevlog = getenv("NICECRASH_TEVLOG") != nullptr;
    auto fit = fast.find(h);
    if (fit != fast.end() && !tevlog) {
        if (!fit->second->id) return false;
        g_cur = fit->second;
    } else {
    // Key: everything the generated code depends on.
    std::string key;
    char b[64];
    snprintf(b, sizeof b, "%d|%06x|%d%d%d%d%d%d%d%d|", nStages, g_gx.bp[0xF3] & 0xFF0000 | (g_gx.bp[0xF3] >> 16 & 0xFF) << 0,
             texOk[0], texOk[1], texOk[2], texOk[3], texOk[4], texOk[5], texOk[6], texOk[7]);
    key = b;
    key += std::to_string((g_gx.bp[0xF3] >> 16) & 0xFF) + "|";
    for (int st = 0; st < nStages; st++) {
        snprintf(b, sizeof b, "%06x%06x", g_gx.bp[0xC0 + st * 2] & 0xFFFFFF, g_gx.bp[0xC1 + st * 2] & 0xFFFFFF);
        key += b;
    }
    snprintf(b, sizeof b, "|%06x|%06x%06x%06x|", g_gx.bp[0x00] & 0x7FFFFF, g_gx.bp[0x25] & 0xFFFFFF, g_gx.bp[0x26] & 0xFFFFFF,
             g_gx.bp[0x27] & 0xFFFFFF);
    key += b;
    for (int st = 0; st < nStages; st++) {
        snprintf(b, sizeof b, "%06x", g_gx.bp[0x10 + st] & 0x1FFFFF);
        key += b;
    }
    for (int m = 0; m < 8; m++) {  // wrap and filter modes (sampleGX literals)
        snprintf(b, sizeof b, "%02x", g_gx.bp[(m < 4 ? 0x80 : 0xA0) + (m & 3)] & 0x1F);
        key += b;
    }
    for (int i = 0; i < 8; i++) {
        snprintf(b, sizeof b, "%06x", g_gx.bp[0x28 + i] & 0xFFFFFF);
        key += b;
        snprintf(b, sizeof b, "%06x", g_gx.bp[0xF6 + i] & 0xFFFFFF);
        key += b;
    }
    auto it = g_progs.find(key);
    if (it == g_progs.end()) {
        if (!g_vs) g_vs = Compile(GL_VERTEX_SHADER_, kVS);
        std::string fsrc = BuildFS(nStages, texOk);
        GLuint fs = g_vs ? Compile(GL_FRAGMENT_SHADER_, fsrc) : 0;
        Prog pr{};
        if (fs) {
            pr.id = p_glCreateProgram();
            p_glAttachShader(pr.id, g_vs);
            p_glAttachShader(pr.id, fs);
            p_glBindAttribLocation(pr.id, 6, "col1in");
            p_glLinkProgram(pr.id);
            GLint ok = 0;
            p_glGetProgramiv(pr.id, GL_LINK_STATUS_, &ok);
            if (!ok) {
                char log[2048];
                p_glGetProgramInfoLog(pr.id, sizeof log, nullptr, log);
                fprintf(stderr, "[tev] link failed: %s\n", log);
                pr.id = 0;
            } else {
                pr.reg = p_glGetUniformLocation(pr.id, "reg");
                pr.kon = p_glGetUniformLocation(pr.id, "kon");
                pr.aref = p_glGetUniformLocation(pr.id, "aref");
                pr.tex = p_glGetUniformLocation(pr.id, "tex");
                pr.tdim = p_glGetUniformLocation(pr.id, "tdim");
                pr.imtx = p_glGetUniformLocation(pr.id, "imtx");
                pr.ishift = p_glGetUniformLocation(pr.id, "ishift");
            }
        }
        if (getenv("NICECRASH_TEVLOG")) fprintf(stderr, "[tev] program %zu (%s):\n%s\n", g_progs.size(), key.c_str(), fsrc.c_str());
        it = g_progs.emplace(key, pr).first;
    }
    fast[h] = &it->second;
    if (!it->second.id) return false;
    g_cur = &it->second;
    if (getenv("NICECRASH_TEVLOG") && atoi(getenv("NICECRASH_TEVLOG")) > 1)
        fprintf(stderr, "[tev] use %.40s gen %06x F3 %06x reg %06x %06x konst %06x %06x map0 %06x/%06x map1 %06x/%06x tlut %06x %06x\n", it->first.c_str(), g_gx.bp[0] & 0xFFFFFF,
                g_gx.bp[0xF3] & 0xFFFFFF, g_gx.tevReg[2], g_gx.tevReg[3], g_gx.tevKonst[0], g_gx.tevKonst[1], g_gx.bp[0x88] & 0xFFFFFF, g_gx.bp[0x94] & 0xFFFFFF, g_gx.bp[0x89] & 0xFFFFFF, g_gx.bp[0x95] & 0xFFFFFF, g_gx.bp[0x98] & 0xFFFFFF, g_gx.bp[0x99] & 0xFFFFFF);
    }
    p_glUseProgram(g_cur->id);
    float reg[16], kon[16];
    for (int i = 0; i < 4; i++) {
        u32 ra = g_gx.tevReg[i * 2], bg = g_gx.tevReg[i * 2 + 1];
        reg[i * 4 + 0] = S11(ra);
        reg[i * 4 + 1] = S11(bg >> 12);
        reg[i * 4 + 2] = S11(bg);
        reg[i * 4 + 3] = S11(ra >> 12);
        ra = g_gx.tevKonst[i * 2];
        bg = g_gx.tevKonst[i * 2 + 1];
        kon[i * 4 + 0] = (ra & 0xFF) / 255.0f;
        kon[i * 4 + 1] = ((bg >> 12) & 0xFF) / 255.0f;
        kon[i * 4 + 2] = (bg & 0xFF) / 255.0f;
        kon[i * 4 + 3] = ((ra >> 12) & 0xFF) / 255.0f;
    }
    p_glUniform4fv(g_cur->reg, 4, reg);
    p_glUniform4fv(g_cur->kon, 4, kon);
    p_glUniform2f(g_cur->aref, (float)(g_gx.bp[0xF3] & 0xFF), (float)((g_gx.bp[0xF3] >> 8) & 0xFF));
    static const GLint units[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    p_glUniform1iv(g_cur->tex, 8, units);
    // Texture sizes (BP image registers) and the indirect matrices (BP 0x06-0x0E: raw s11 elements, scale exponent).
    float dims[16], mtx[18], shift[3];
    for (int m = 0; m < 8; m++) {
        u32 img0 = g_gx.bp[(m < 4 ? 0x88 : 0xA8) + (m & 3)];
        dims[m * 2] = (float)((img0 & 0x3FF) + 1);
        dims[m * 2 + 1] = (float)(((img0 >> 10) & 0x3FF) + 1);
    }
    auto s11 = [](u32 v) { int x = (int)(v & 0x7FF); return (float)(x & 0x400 ? x - 0x800 : x); };
    for (int m = 0; m < 3; m++) {
        u32 A = g_gx.bp[0x06 + 3 * m], B = g_gx.bp[0x07 + 3 * m], C = g_gx.bp[0x08 + 3 * m];
        float row0[3] = {s11(A), s11(B), s11(C)}, row1[3] = {s11(A >> 11), s11(B >> 11), s11(C >> 11)};
        for (int k = 0; k < 3; k++) {
            mtx[m * 6 + k] = row0[k];
            mtx[m * 6 + 3 + k] = row1[k];
        }
        int scale = (int)((A >> 22) & 3 | ((B >> 22) & 3) << 2 | ((C >> 22) & 3) << 4);
        shift[m] = (float)(17 - scale);
    }
    p_glUniform2fv(g_cur->tdim, 8, dims);
    p_glUniform3fv(g_cur->imtx, 6, mtx);
    p_glUniform1fv(g_cur->ishift, 3, shift);
    return true;
}

void gxt_end() {
    if (g_cur) p_glUseProgram(0);
    g_cur = nullptr;
}

void gxt_active_texture(int unit) {
    if (Load()) p_glActiveTexture(GL_TEXTURE0_ + unit);
}

void gxt_texcoord(int unit, float s, float t) { p_glMultiTexCoord2f(GL_TEXTURE0_ + unit, s, t); }

void gxt_color1(const u8 c[4]) { p_glVertexAttrib4f(6, c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f); }

// One draw of batched vertices (sdk/gxrender.cpp): per vertex, float4 clip position at 0, colour 0 (u8x4) at 16,
// colour 1 (u8x4, the TEV programs' col1in) at 20 and nTex float2 texture coordinates from 24.
void gxt_draw_arrays(unsigned mode, const void* base, int stride, int n, int nTex) {
    const unsigned char* b = (const unsigned char*)base;
    bool ext = Load();
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(4, GL_FLOAT, stride, b);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, b + 16);
    if (ext) {
        p_glEnableVertexAttribArray(6);
        p_glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, b + 20);
    }
    if (!ext && nTex > 1) nTex = 1;
    for (int i = 0; i < nTex; i++) {
        if (ext) p_glClientActiveTexture(GL_TEXTURE0_ + i);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, stride, b + 24 + i * 8);
    }
    glDrawArrays(mode, 0, n);
    for (int i = 0; i < nTex; i++) {
        if (ext) p_glClientActiveTexture(GL_TEXTURE0_ + i);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    if (ext) {
        p_glClientActiveTexture(GL_TEXTURE0_);
        p_glDisableVertexAttribArray(6);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// PE blending (BP 0x41 PE_CMODE0, 0x42 PE_CMODE1) and the EFB pixel format (BP 0x43): blend factors or subtract,
// logic op, colour/alpha update, the constant destination alpha and formats without alpha (destination alpha = 1).
void gxt_blend(u32 cm, u32 cm1, u32 pe) {
    static const GLenum srcF[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    static const GLenum dstF[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    static const int ab = getenv("NICECRASH_BLENDAB") ? atoi(getenv("NICECRASH_BLENDAB")) : 0;  // debugging: 1 = no dst alpha, 2 = alpha always, 4 = no logic op
    bool hasAlpha = (pe & 7) == 1 || (ab & 2);
    if (ab & 1) cm1 = 0;
    if (ab & 4) cm &= ~2u;  // RGBA6_Z24; RGB8_Z24 and RGB565_Z16 read destination alpha as 1
    auto fix = [&](GLenum f) {
        if (hasAlpha) return f;
        return f == GL_DST_ALPHA ? (GLenum)GL_ONE : f == GL_ONE_MINUS_DST_ALPHA ? (GLenum)GL_ZERO : f;
    };
    const GLenum ADD = 0x8006, REVSUB = 0x800B, CONST_ALPHA = 0x8003;
    bool blend = false;
    GLenum eq = ADD, s = GL_ONE, d = GL_ZERO;
    if (cm & 0x800) {  // subtract: dst - src
        blend = true; eq = REVSUB;
    } else if (cm & 1) {
        blend = true; s = fix(srcF[(cm >> 8) & 7]); d = fix(dstF[(cm >> 5) & 7]);
    }
    static const GLenum logicOps[16] = {GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY, GL_AND_INVERTED, GL_NOOP,
                                        GL_XOR, GL_OR, GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE,
                                        GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET};
    if (!blend && (cm & 2) && ((cm >> 12) & 15) != 3) {
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(logicOps[(cm >> 12) & 15]);
    } else {
        glDisable(GL_COLOR_LOGIC_OP);
    }
    bool dstAlpha = (cm1 >> 8) & 1;
    Load();
    if (!p_blendFuncSep || !p_blendEqSep || !p_blendColor) {  // fixed function only
        if (blend) { glEnable(GL_BLEND); glBlendFunc(s, d); } else glDisable(GL_BLEND);
    } else if (dstAlpha && hasAlpha) {  // written alpha = the constant
        glEnable(GL_BLEND);
        p_blendColor(0, 0, 0, (cm1 & 0xFF) / 255.0f);
        p_blendEqSep(eq, ADD);
        p_blendFuncSep(s, d, CONST_ALPHA, GL_ZERO);
    } else if (blend) {
        glEnable(GL_BLEND);
        p_blendEqSep(eq, eq);
        p_blendFuncSep(s, d, s, d);
    } else {
        glDisable(GL_BLEND);
    }
    glColorMask((cm >> 3) & 1, (cm >> 3) & 1, (cm >> 3) & 1, ((cm >> 4) & 1) && hasAlpha);
}

