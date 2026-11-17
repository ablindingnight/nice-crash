// EFB and XFBs as the Wii has them. The GX renderer draws into an off-screen EFB (an OpenGL framebuffer object the
// size of the window); a display copy stores the EFB into a texture for that XFB address; at each retrace the VI
// shows the XFB the game selected with VISetNextFrameBuffer (sdk/vi.cpp). So a copy the game makes but never
// displays (a black copy of a cleared EFB while loading, a second copy in one retrace) never reaches the screen,
// which removes the flashes of presenting at every copy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include "env.h"
#include "sdk.h"
#include "xfb.h"
#include "hostcfg.h"

namespace {

#define GL_FRAMEBUFFER_ 0x8D40
#define GL_RENDERBUFFER_ 0x8D41
#define GL_COLOR_ATTACHMENT0_ 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT_ 0x821A
#define GL_DEPTH24_STENCIL8_ 0x88F0
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5

typedef void(APIENTRY* GenFB)(GLsizei, GLuint*);
typedef void(APIENTRY* BindFB)(GLenum, GLuint);
typedef void(APIENTRY* FBTex)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(APIENTRY* RBStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY* FBRB)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum(APIENTRY* CheckFB)(GLenum);
GenFB p_glGenFramebuffers, p_glGenRenderbuffers;
BindFB p_glBindFramebuffer, p_glBindRenderbuffer;
FBTex p_glFramebufferTexture2D;
RBStorage p_glRenderbufferStorage;
FBRB p_glFramebufferRenderbuffer;
CheckFB p_glCheckFramebufferStatus;
typedef void(APIENTRY* RBStorageMS)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY* BlitFB)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
RBStorageMS p_glRenderbufferStorageMultisample;
BlitFB p_glBlitFramebuffer;
#define GL_READ_FRAMEBUFFER_ 0x8CA8
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9
#define GL_MAX_SAMPLES_ 0x8D57
#define GL_RGBA8_ 0x8058

int g_ok = -1;
GLuint g_fbo, g_color, g_depth;  // the EFB as the readers see it (copies, the XFB): colour texture + depth
// graphics.msaa: the game draws into a multisampled framebuffer, resolved into g_fbo before anything reads the
// EFB (xfb_resolve: texture copies, display copies).
GLuint g_msFbo, g_msColor, g_msDepth;
int g_samples;
bool g_msDirty;
int g_w, g_h;
int g_presW, g_presH;  // window size at the last presentation

struct Xfb {
    GLuint tex = 0;
    int w = 0, h = 0;
    u32 serial = 0;  // display copies so far, when this XFB was written
    int lines = 480;  // EFB lines the copy took (the game's 448): centred on the screen as a TV shows them
};
std::map<u32, Xfb> g_xfbs;
u32 g_serial, g_lastAddr;

bool Load() {
    if (g_ok >= 0) return g_ok;
    p_glGenFramebuffers = (GenFB)(void*)wglGetProcAddress("glGenFramebuffers");
    p_glGenRenderbuffers = (GenFB)(void*)wglGetProcAddress("glGenRenderbuffers");
    p_glBindFramebuffer = (BindFB)(void*)wglGetProcAddress("glBindFramebuffer");
    p_glBindRenderbuffer = (BindFB)(void*)wglGetProcAddress("glBindRenderbuffer");
    p_glFramebufferTexture2D = (FBTex)(void*)wglGetProcAddress("glFramebufferTexture2D");
    p_glRenderbufferStorage = (RBStorage)(void*)wglGetProcAddress("glRenderbufferStorage");
    p_glFramebufferRenderbuffer = (FBRB)(void*)wglGetProcAddress("glFramebufferRenderbuffer");
    p_glCheckFramebufferStatus = (CheckFB)(void*)wglGetProcAddress("glCheckFramebufferStatus");
    p_glRenderbufferStorageMultisample =
        (RBStorageMS)(void*)wglGetProcAddress("glRenderbufferStorageMultisample");
    p_glBlitFramebuffer = (BlitFB)(void*)wglGetProcAddress("glBlitFramebuffer");
    g_ok = p_glGenFramebuffers && p_glGenRenderbuffers && p_glBindFramebuffer && p_glBindRenderbuffer &&
           p_glFramebufferTexture2D && p_glRenderbufferStorage && p_glFramebufferRenderbuffer &&
           p_glCheckFramebufferStatus;
    if (!g_ok) fprintf(stderr, "[xfb] no framebuffer objects: presenting at every display copy\n");
    return g_ok;
}

}  // namespace

bool xfb_active() { return Load(); }

void xfb_bind_efb(int w, int h) {
    if (!Load()) return;
    if (!g_fbo || w != g_w || h != g_h) {
        if (!g_fbo) {
            p_glGenFramebuffers(1, &g_fbo);
            glGenTextures(1, &g_color);
            p_glGenRenderbuffers(1, &g_depth);
        }
        g_w = w;
        g_h = h;
        glBindTexture(GL_TEXTURE_2D, g_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        p_glBindRenderbuffer(GL_RENDERBUFFER_, g_depth);
        p_glRenderbufferStorage(GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, w, h);
        p_glBindFramebuffer(GL_FRAMEBUFFER_, g_fbo);
        p_glFramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, g_color, 0);
        p_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_, g_depth);
        if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER_) != GL_FRAMEBUFFER_COMPLETE_) {
            fprintf(stderr, "[xfb] EFB framebuffer incomplete: presenting at every display copy\n");
            p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
            g_ok = 0;
            return;
        }
        // The multisampled drawing target (graphics.msaa), at the card's limit at most.
        g_samples = host_cfg().msaa;
        if (g_samples > 1 && p_glRenderbufferStorageMultisample && p_glBlitFramebuffer) {
            GLint maxS = 0;
            glGetIntegerv(GL_MAX_SAMPLES_, &maxS);
            if (g_samples > maxS) g_samples = maxS;
        } else {
            g_samples = 0;
        }
        if (g_samples > 1) {
            if (!g_msFbo) {
                p_glGenFramebuffers(1, &g_msFbo);
                p_glGenRenderbuffers(1, &g_msColor);
                p_glGenRenderbuffers(1, &g_msDepth);
            }
            p_glBindRenderbuffer(GL_RENDERBUFFER_, g_msColor);
            p_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, g_samples, GL_RGBA8_, w, h);
            p_glBindRenderbuffer(GL_RENDERBUFFER_, g_msDepth);
            p_glRenderbufferStorageMultisample(GL_RENDERBUFFER_, g_samples, GL_DEPTH24_STENCIL8_, w, h);
            p_glBindFramebuffer(GL_FRAMEBUFFER_, g_msFbo);
            p_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, g_msColor);
            p_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_, g_msDepth);
            if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER_) != GL_FRAMEBUFFER_COMPLETE_) {
                fprintf(stderr, "[xfb] %dx MSAA framebuffer incomplete: anti-aliasing off\n", g_samples);
                g_samples = 0;
            } else {
                static bool told;
                if (!told) fprintf(stderr, "[xfb] EFB %dx%d, %dx MSAA\n", w, h, g_samples);
                told = true;
            }
        }
    }
    g_msDirty = g_samples > 1;  // drawing follows
    p_glBindFramebuffer(GL_FRAMEBUFFER_, g_samples > 1 ? g_msFbo : g_fbo);
}

void xfb_resolve(bool depth) {
    if (g_samples <= 1 || !g_msDirty) {
        if (g_fbo) p_glBindFramebuffer(GL_FRAMEBUFFER_, g_fbo);
        return;
    }
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER_, g_msFbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER_, g_fbo);
    p_glBlitFramebuffer(0, 0, g_w, g_h, 0, 0, g_w, g_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    // Depth only when a depth copy reads it (resolving the multisampled depth/stencil at every copy crashed the
    // driver: 0xC0000409 fast fails at the Tutorial's loading, about half the runs with 4x MSAA).
    if (depth) p_glBlitFramebuffer(0, 0, g_w, g_h, 0, 0, g_w, g_h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, g_fbo);
    g_msDirty = false;
}

void xfb_copy(u32 addr, int lines) {
    if (!Load() || !g_fbo) return;
    addr &= 0x1FFFFFFF;
    Xfb& x = g_xfbs[addr];
    if (NC_ENV("NICECRASH_XFBLOG")) fprintf(stderr, "[xfb] copy %08x lines %d%c", addr, lines, 10);
    if (!x.tex) glGenTextures(1, &x.tex);
    xfb_resolve();  // binds g_fbo
    glBindTexture(GL_TEXTURE_2D, x.tex);
    if (x.w != g_w || x.h != g_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_w, g_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        x.w = g_w;
        x.h = g_h;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_w, g_h);
    x.serial = ++g_serial;
    x.lines = lines > 0 && lines <= 480 ? lines : 480;
    g_lastAddr = addr;
}

bool xfb_present(u32 addr, bool black, int ww, int wh) {
    if (!Load() || !g_fbo) return false;
    addr &= 0x1FFFFFFF;
    auto it = g_xfbs.find(addr);
    if (NC_ENV("NICECRASH_XFBLOG"))
        fprintf(stderr, "[xfb] present %08x %s serial %u (latest copy %08x serial %u)%c", addr,
                it == g_xfbs.end() ? "MISSING" : "ok", it == g_xfbs.end() ? 0 : it->second.serial, g_lastAddr, g_serial, 10);
    if (it == g_xfbs.end()) it = g_xfbs.find(g_lastAddr);  // the game never named one: show the latest copy
    if (it == g_xfbs.end() && !black) return false;
    // The same picture as the last presentation (the game draws a new frame every second or third retrace): the
    // window already shows it, so skip the draw and the swap (NICECRASH_PRESENTALL=1 presents every retrace).
    static u32 lastAddr = ~0u, lastSerial;
    static bool lastBlack, lastValid;
    static const bool all = getenv("NICECRASH_PRESENTALL") != nullptr;
    u32 serial = it != g_xfbs.end() ? it->second.serial : 0, shown = it != g_xfbs.end() ? it->first : 0;
    if (!all && lastValid && shown == lastAddr && serial == lastSerial && black == lastBlack && ww == g_presW && wh == g_presH)
        return false;
    lastValid = true;
    lastAddr = shown;
    lastSerial = serial;
    lastBlack = black;
    g_presW = ww;
    g_presH = wh;
    p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
    glViewport(0, 0, ww, wh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glColorMask(1, 1, 1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!black && it != g_xfbs.end()) {
        // The picture keeps its shape (16:9 or 4:3) in any window: centred, black bars outside.
        float want = sdk_widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f, have = (float)ww / (float)(wh ? wh : 1);
        int vw = ww, vh = wh;
        if (have > want * 1.01f) vw = (int)(wh * want + 0.5f);
        else if (have < want / 1.01f) vh = (int)(ww / want + 0.5f);
        glViewport((ww - vw) / 2, (wh - vh) / 2, vw, vh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, 1, 0, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, it->second.tex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        // The EFB's first `lines` rows (the top of the texture) fill the middle of the screen, black above and below.
        float used = it->second.lines / 480.0f, v0 = 1.0f - used, y0 = (1.0f - used) * 0.5f, y1 = y0 + used;
        glBegin(GL_QUADS);
        glTexCoord2f(0, v0); glVertex2f(0, y0);
        glTexCoord2f(1, v0); glVertex2f(1, y0);
        glTexCoord2f(1, 1); glVertex2f(1, y1);
        glTexCoord2f(0, 1); glVertex2f(0, y1);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    return true;
}

void xfb_rebind_efb() {
    if (g_ok > 0 && g_fbo) p_glBindFramebuffer(GL_FRAMEBUFFER_, g_samples > 1 ? g_msFbo : g_fbo);
}

unsigned xfb_efb_color() { return g_color; }

bool xfb_target_tex(unsigned tex, int w, int h) {
    if (!Load() || !g_fbo) return false;
    static GLuint fbo;
    if (!fbo) p_glGenFramebuffers(1, &fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, tex, 0);
    static bool checked;  // RGBA8 colour-only targets: one completeness check is enough
    if (!checked) {
        if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER_) != GL_FRAMEBUFFER_COMPLETE_) {
            p_glBindFramebuffer(GL_FRAMEBUFFER_, g_fbo);
            return false;
        }
        checked = true;
    }
    glViewport(0, 0, w, h);
    return true;
}
