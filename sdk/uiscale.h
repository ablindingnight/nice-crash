// Live smoothing for the game's 2D art (graphics.ui_filter, sdk/uiscale.cpp).
#pragma once
#include "sdk.h"

// Upload `rgba` (w x h, the decoded Wii texture) into the bound GL texture at a higher resolution, with mip levels,
// when graphics.ui_filter wants it for this texture: one the game samples without mip levels (menus, HUD, fonts,
// the sky and backdrop cards). Returns false when it does nothing (the caller uploads as usual).
bool uiscale_upload(const u8* rgba, int w, int h, u32 mode0, int fmt);
