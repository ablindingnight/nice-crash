// Glue between the game and the HOME button library (tools/gametrial/hbmbuild.py).
// The game declares HBMCreateSound as a C++ function with u32 = unsigned int; the library's own u32 is unsigned
// long. Same 32 bits, different mangled names: forward one to the other.
void HBMCreateSound(const char* path, void* buf, unsigned long size);
void HBMCreateSound(const char* path, void* buf, unsigned int size) { HBMCreateSound(path, buf, (unsigned long)size); }

#include <cstdlib>
// The real HOME menu (Enter / the pad's Back). NICECRASH_NOHBM=1 turns it off (HOME is then ignored).
int port_hbm_enabled(void) {
    static int on = getenv("NICECRASH_NOHBM") == nullptr;
    return on;
}

// TODO: the HOME menu's sound effects (HomeButtonSe.brsar, a big-endian NW4R sound archive the library parses in
// place) are not converted yet, so the menu runs silent unless NICECRASH_HBMSOUND=1 (patches/hbm-portability).
extern "C" int port_hbm_sound_enabled(void) {
    static int on = getenv("NICECRASH_HBMSOUND") != nullptr;
    return on;
}
