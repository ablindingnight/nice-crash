// Glue between the game and the HOME button library (tools/gametrial/hbmbuild.py).
// The game declares HBMCreateSound as a C++ function with u32 = unsigned int; the library's own u32 is unsigned
// long. Same 32 bits, different mangled names: forward one to the other.
void HBMCreateSound(const char* path, void* buf, unsigned long size);
// The "path" is the sound archive itself, loaded by the game: convert it to host byte order first (sdk/hbmsnd.cpp).
extern "C" void port_hbm_rsar_fix(void* p);
void HBMCreateSound(const char* path, void* buf, unsigned int size) {
    port_hbm_rsar_fix((void*)path);
    HBMCreateSound(path, buf, (unsigned long)size);
}

#include <cstdlib>
// The real HOME menu (Enter / the pad's Back). NICECRASH_NOHBM=1 turns it off (HOME is then ignored).
int port_hbm_enabled(void) {
    static int on = getenv("NICECRASH_NOHBM") == nullptr;
    return on;
}

// The HOME menu's sound effects; NICECRASH_NOHBMSOUND=1 runs the menu silent (patches/hbm-portability).
extern "C" int port_hbm_sound_enabled(void) {
    static int on = getenv("NICECRASH_NOHBMSOUND") == nullptr;
    return on;
}
