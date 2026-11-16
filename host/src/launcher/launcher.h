// Nice Crash launcher entry points (launcher.cpp).
#pragma once
#include <string>
#include "ui.h"

int LauncherMain();
// Renders one screen offscreen to a .bmp: home, disc, firstrun, settings:<files|display|graphics|audio|controls|
// language|builds|logs|about>.
// Starts the game as PLAY does, ends it after `seconds`, prints the recorded run.
int LauncherLaunchTest(int seconds);
int LauncherShot(const std::string& which, const std::string& out, int w, int h);
