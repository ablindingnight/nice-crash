#include "hostcfg.h"
#include <climits>
#include <cstdlib>
#include <cstring>
#include "settings.h"

static HostCfg Load() {
    HostCfg c{};
    const char* m = Settings_Get("display.window_mode");
    c.windowMode = !strcmp(m, "windowed") ? 0 : !strcmp(m, "fullscreen") ? 2 : 1;
    c.windowW = Settings_GetInt("display.window_width");
    c.windowH = Settings_GetInt("display.window_height");
    if (c.windowW < 320) c.windowW = 1280;
    if (c.windowH < 240) c.windowH = 720;
    const char* x = Settings_Get("display.window_x");
    const char* y = Settings_Get("display.window_y");
    c.windowX = x[0] ? atoi(x) : INT_MIN;
    c.windowY = y[0] ? atoi(y) : INT_MIN;
    c.monitor = Settings_GetInt("display.monitor");
    c.vsync = Settings_GetInt("display.vsync") != 0;
    c.widescreen = strcmp(Settings_Get("display.aspect"), "4:3") != 0;
    c.showFps = Settings_GetInt("graphics.show_fps") != 0;
    c.master = Settings_GetInt("audio.master");
    if (c.master < 0) c.master = 0;
    if (c.master > 100) c.master = 100;
    const char* l = getenv("NICECRASH_LAUNCHED");
    c.launched = l && l[0] == '1';
    c.muteUnfocused = c.launched && Settings_GetInt("audio.mute_unfocused") != 0;
    c.pauseUnfocused = c.launched && Settings_GetInt("game.pause_unfocused") != 0;
    c.rumble = Settings_GetInt("controls.rumble") != 0;
    c.deadzone = Settings_GetInt("controls.deadzone");
    if (c.deadzone < 0) c.deadzone = 0;
    if (c.deadzone > 50) c.deadzone = 50;
    c.invertSteer = Settings_GetInt("controls.invert_steer") != 0;
    c.msaa = Settings_GetInt("graphics.msaa");
    if (c.msaa != 2 && c.msaa != 4 && c.msaa != 8) c.msaa = 0;
    const char* is = Settings_Get("graphics.internal_scale");
    c.internalScale = !strcmp(is, "window") ? 0 : atoi(is);
    if (c.internalScale < 0 || c.internalScale > 4) c.internalScale = 0;
    const char* tf = Settings_Get("graphics.texture_filter");
    c.texFilter = !strcmp(tf, "bilinear") ? 1 : !strcmp(tf, "trilinear") ? 2 : !strcmp(tf, "aniso16") ? 3 : 0;
    return c;
}

const HostCfg& host_cfg() {
    static HostCfg c = Load();
    return c;
}
