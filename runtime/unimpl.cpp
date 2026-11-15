// Called by every auto-generated stub (build/stubs.s) for a symbol the port does not provide yet.
// NICECRASH_UNIMPL=continue makes stubs return 0 instead of stopping (to see how far the boot gets).
// No C++ allocation here: the game's own operator new (mem.cpp) replaces the host's, and its heap may not
// exist yet when a stub runs.
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int port_unimpl(const char* name) {
    static const char* seen[4096];
    static int nseen;
    static int mode = -1;
    if (mode < 0) {
        const char* m = getenv("NICECRASH_UNIMPL");
        mode = m && !strcmp(m, "continue");
    }
    bool first = true;
    for (int i = 0; i < nseen; i++)
        if (seen[i] == name) first = false;  // stubs pass the same string pointer every time
    if (first && nseen < 4096) seen[nseen++] = name;
    if (first || !mode) {
        fprintf(stderr, "[unimpl] %s%s\n", name, mode ? " (returning 0)" : "");
        fflush(stderr);
    }
    if (!mode) exit(99);
    return 0;
}
