// Basic types and explicit-endian readers for the host layer.
//
// Disc data is never overlaid with structs on the host: the Wii code casts file bytes to structs with
// 32-bit pointer fields (ResEntry::data, TexImage::tex ...), which breaks on a 64-bit target. The port
// parses each field with these readers instead and keeps pointers in host-side structs.
#pragma once
#include <cstdint>
#include <cstddef>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

// RST0 sets, TOCs and tt_texture headers are stored little-endian on the disc; the Wii code byte-swaps
// them after loading (res.cpp fn_8012A80C / fn_8012A388, texture.cpp fn_802CA57C, drtex.cpp Fix).
static inline u16 rd_le16(const u8* p) { return (u16)(p[0] | p[1] << 8); }
static inline u32 rd_le32(const u8* p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

// GX texel data is in the GPU's native big-endian layout (no swap in drtex.cpp for it).
static inline u16 rd_be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }
static inline u32 rd_be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | (u32)p[3]; }

// A FourCC as the game writes it in source ('TEX ', 'RST0'): first character in the high byte.
static constexpr u32 FOURCC(const char (&s)[5]) {
    return (u32)(u8)s[0] << 24 | (u32)(u8)s[1] << 16 | (u32)(u8)s[2] << 8 | (u32)(u8)s[3];
}
