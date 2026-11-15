// Nice Crash entry point: host setup, then the game's own main (kernel.cpp main, renamed GameMain).
#include <cstdio>
#include "project.h"

extern "C" int GameMain(void);
void sdk_cpu_acquire();  // sdk/thread.cpp: the main thread owns the Wii CPU from the start  // kernel.cpp main (extern "C" there), renamed by port_prefix.h

int main(int argc, char** argv) {
    printf("%s - %s: starting game main\n", PROJECT_NAME, PROJECT_TAGLINE);
    fflush(stdout);
    sdk_cpu_acquire();
    int r = GameMain();
    printf("GameMain returned %d\n", r);
    return r;
}
