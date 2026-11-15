/* Port version of revolution/os/OSFastCast.h: the Wii uses paired-single quantised loads/stores (GQRs);
   plain C conversions give the same results (truncation toward zero, saturation as psq_st does). */
#ifndef RVL_SDK_OS_FAST_CAST_H
#define RVL_SDK_OS_FAST_CAST_H
#ifdef __cplusplus
extern "C" {
#endif
#define OS_GQR_TYPE_U8 4
#define OS_GQR_TYPE_U16 5
#define OS_GQR_TYPE_S8 6
#define OS_GQR_TYPE_S16 7
static void OSInitFastCast(void) {}
static inline float __port_sat(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline f32 __OSu16tof32(const u16* arg) { return (f32)*arg; }
static inline void OSu16tof32(const u16* in, f32* out) { *out = __OSu16tof32(in); }
static inline u16 __OSf32tou16(f32 arg) { return (u16)__port_sat(arg, 0.0f, 65535.0f); }
static inline void OSf32tou16(const f32* in, u16* out) { *out = __OSf32tou16(*in); }
static inline u8 __OSf32tou8(f32 arg) { return (u8)__port_sat(arg, 0.0f, 255.0f); }
static inline void OSf32tou8(f32* in, u8* out) { *out = __OSf32tou8(*in); }
static inline s16 __OSf32tos16(f32 arg) { return (s16)__port_sat(arg, -32768.0f, 32767.0f); }
static inline void OSf32tos16(const f32* in, s16* out) { *out = __OSf32tos16(*in); }
#ifdef __cplusplus
}
#endif
#endif
