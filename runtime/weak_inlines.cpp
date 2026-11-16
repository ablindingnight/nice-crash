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
