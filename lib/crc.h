// Port of the game's CRC-32 (decomp src/game/crc.cpp fn_80132E7C).
#pragma once
#include "bytes.h"

u32 GameCrc32(const void* data, s32 len);
