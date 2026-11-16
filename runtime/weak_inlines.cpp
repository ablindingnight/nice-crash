// Out-of-line copies of small inline members that retail emitted as weak functions in one unit and called from
// others. On the host the defining unit inlines them and emits no copy, so the other units' calls would land on
// "unimplemented" stubs (a float getter stub leaves the x87 stack empty: NaN). Bodies follow retail's code.
// Symbol names are the units' Itanium names (i386 COFF: leading underscore), the object passed as the first argument.

// RaceCarRec::F2690 (retail 0x8014AEC8: lfs f1, 0x2690(r3)): crashmode.cpp / stunts.cpp
extern "C" float port_RaceCarRec_F2690(const unsigned char* p) __asm__("__ZN10RaceCarRec5F2690Ev");
extern "C" float port_RaceCarRec_F2690(const unsigned char* p) { return *(const float*)(p + 0x2690); }

// Vec2::SetXZ(const Vec3&) (retail 0x8016029C: x = v.x, z = v.z): xlobj.cpp
extern "C" void port_Vec2_SetXZ(float* d, const float* v) __asm__("__ZN4Vec25SetXZERK4Vec3");
extern "C" void port_Vec2_SetXZ(float* d, const float* v) {
    d[0] = v[0];
    d[1] = v[2];
}

// Vec2::Vec2() (retail 0x801602B0: blr)
extern "C" void port_Vec2_ct(void* p) __asm__("__ZN4Vec2C1Ev");
extern "C" void port_Vec2_ct(void* p) { (void)p; }
extern "C" void port_Vec2_ct_mw(void* p) __asm__("___ct__4Vec2Fv");
extern "C" void port_Vec2_ct_mw(void* p) { (void)p; }

// Spot::From(const Vec2&) (aiobst.cpp inline, retail 0x80160230: pos = (v.x, 0, v.y)): ball.cpp / boostman.cpp
extern "C" void port_Spot_From(float* pos, const float* v) __asm__("__ZN4Spot4FromERK4Vec2");
extern "C" void port_Spot_From(float* pos, const float* v) {
    pos[0] = v[0];
    pos[1] = 0.0f;
    pos[2] = v[1];
}

// GameUI's in-class virtuals (overlay.cpp / racing.cpp inline; retail weak copies 0x80006244-0x80007974,
// 0x800151D4): amaster / crushmaster / ringchal / zone declare them out of line.
extern "C" int port_GameUI_vf58(void* p) __asm__("__ZN6GameUI4vf58Ev");
extern "C" int port_GameUI_vf58(void* p) { (void)p; return 1; }
extern "C" int port_GameUI_vf5C(const void* p, int player) __asm__("__ZNK6GameUI4vf5CEi");
extern "C" int port_GameUI_vf5C(const void* p, int player) { (void)p; (void)player; return 0; }
extern "C" void port_GameUI_vf60(void* p, int player) __asm__("__ZN6GameUI4vf60Ei");
extern "C" void port_GameUI_vf60(void* p, int player) { (void)p; (void)player; }
extern "C" int port_GameUI_vf64(void* p) __asm__("__ZN6GameUI4vf64Ev");
extern "C" int port_GameUI_vf64(void* p) { (void)p; return 0; }
extern "C" int port_GameUI_vf68(const void* p) __asm__("__ZNK6GameUI4vf68Ev");
extern "C" int port_GameUI_vf68(const void* p) { (void)p; return 1; }
extern "C" void port_GameUI_vfF8(void* p) __asm__("__ZN6GameUI4vfF8Ev");
extern "C" void port_GameUI_vfF8(void* p) { (void)p; }
extern "C" void port_GameUI_vfFC(void* p) __asm__("__ZN6GameUI4vfFCEv");
extern "C" void port_GameUI_vfFC(void* p) { (void)p; }

// zone.cpp's inline Sound::Active / Sound::Play, called by ringchal.cpp by their MWCC names (retail weak copies
// 0x80141FA0 / 0x80142B60): active = byte 0x19; Play sets play (0x49) = 1, b4B = b4A = 0.
extern "C" int port_Sound_Active(const unsigned char* s) __asm__("_Active__5SoundFv");
extern "C" int port_Sound_Active(const unsigned char* s) { return s[0x19]; }
extern "C" void port_Sound_Play(unsigned char* s) __asm__("_Play__5SoundFv");
extern "C" void port_Sound_Play(unsigned char* s) {
    s[0x49] = 1;
    s[0x4B] = 0;
    s[0x4A] = 0;
}
