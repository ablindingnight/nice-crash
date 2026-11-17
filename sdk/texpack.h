// Texture packs by content (sdk/texpack.cpp): Dolphin-compatible names, replacement loading, dumping.
#pragma once
#include <string>
#include <vector>
#include "sdk.h"

bool texpack_active();  // a pack is installed (and enhanced files are on) or dumping is on
// Dolphin's custom-texture name. data/bytes: the level-0 image as the GPU reads it (whole blocks); tlut: TMEM at the
// palette (paletted formats 8-10), else nullptr.
std::string texpack_name(int fmt, int w, int h, bool mips, const u8* data, size_t bytes, const u8* tlut);
// Uploads the replacement for `name` into the bound GL texture (with mip levels); false when the pack has none.
bool texpack_replace(const std::string& name, int fmt);
void texpack_dump(const std::string& name, const std::vector<u8>& rgba, int w, int h);  // NICECRASH_TEXPACK_DUMP
// A PNG file (WIC) from RGBA rows, top row first; the screenshot key (sdk/vi.cpp) uses it too.
bool host_save_png(const char* path, const u8* rgba, int w, int h);
