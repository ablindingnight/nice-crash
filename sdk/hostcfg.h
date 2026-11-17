// The player's settings as the game uses them (lib/settings: %LOCALAPPDATA%\NiceCrash\settings.ini, shared with the
// launcher; NICECRASH_<SECTION>_<KEY> environment variables override). Read once at start.
#pragma once

struct HostCfg {
    int windowMode;       // 0 windowed, 1 borderless (the whole monitor), 2 fullscreen (borderless too)
    int windowW, windowH; // windowed client size
    int windowX, windowY; // windowed position, or INT_MIN: centred
    int monitor;          // display.monitor
    bool vsync;
    bool frame60;         // display.frame_rate: 60 Hz retraces instead of the Wii's 59.94
    bool widescreen;      // display.aspect: 16:9 / native = true, 4:3 = false
    bool showFps;         // frame rate in the window title
    int master;           // audio.master 0-100
    bool muteUnfocused;   // launched runs only (scripted test runs are often unfocused)
    bool pauseUnfocused;  // launched runs only
    bool rumble;
    int deadzone;         // controls.deadzone: percent of the stick's travel
    bool invertSteer;
    int steerCurve;       // controls.steer_curve: 0 smooth (the original), 1 linear (keys turn at once), 2 sharp
    float tiltSens;       // controls.tilt_sensitivity: 0..2 (percent / 100), scales steering
    bool launched;        // started by the launcher (NICECRASH_LAUNCHED=1)
    int msaa;             // graphics.msaa: 0/1 off, 2, 4, 8 samples
    int internalScale;    // graphics.internal_scale: 0 = the window's size, 1-4 = 480 lines x n
    int texFilter;
    bool menuOnTab;       // game.menu_on_tab: Tab opens the Nice Crash menu too (Esc always does)        // graphics.texture_filter: 0 game, 1 bilinear, 2 trilinear, 3 anisotropic 16x
};
const HostCfg& host_cfg();
void host_cfg_reload();  // re-read after the in-game menu changed a setting
