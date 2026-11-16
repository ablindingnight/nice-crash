// Glue between the game and the HOME button library (tools/gametrial/hbmbuild.py).
// The game declares HBMCreateSound as a C++ function with u32 = unsigned int; the library's own u32 is unsigned
// long. Same 32 bits, different mangled names: forward one to the other.
void HBMCreateSound(const char* path, void* buf, unsigned long size);
void HBMCreateSound(const char* path, void* buf, unsigned int size) { HBMCreateSound(path, buf, (unsigned long)size); }

#include <cstdlib>
// The real HOME menu runs only with NICECRASH_HBM=1 while its resource conversion is unfinished.
int port_hbm_enabled(void) {
    static int on = getenv("NICECRASH_HBM") != nullptr;
    return on;
}
