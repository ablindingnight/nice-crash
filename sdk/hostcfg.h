// The player's settings as the game uses them (lib/settings: %LOCALAPPDATA%\NiceCrash\settings.ini, shared with the
// launcher; NICECRASH_<SECTION>_<KEY> environment variables override). Read once at start.
#pragma once

struct HostCfg {
    int windowMode;       // 0 windowed, 1 borderless (the whole monitor), 2 fullscreen (borderless too)
    int windowW, windowH; // windowed client size
    int windowX, windowY; // windowed position, or INT_MIN: centred
    int monitor;          // display.monitor
    bool vsync;
    bool widescreen;      // display.aspect: 16:9 / native = true, 4:3 = false
    bool showFps;         // frame rate in the window title
    int master;           // audio.master 0-100
    bool muteUnfocused;   // launched runs only (scripted test runs are often unfocused)
    bool pauseUnfocused;  // launched runs only
    bool rumble;
    int deadzone;         // controls.deadzone: percent of the stick's travel
    bool invertSteer;
    bool launched;        // started by the launcher (NICECRASH_LAUNCHED=1)
};
const HostCfg& host_cfg();
