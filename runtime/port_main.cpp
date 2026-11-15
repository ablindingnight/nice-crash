// Nice Crash entry point: host setup, then the game's own main (kernel.cpp main, renamed GameMain).
#include <cstdio>
#include "project.h"

extern "C" int GameMain(void);  // kernel.cpp main (extern "C" there), renamed by port_prefix.h

int main(int argc, char** argv) {
    printf("%s - %s: starting game main\n", PROJECT_NAME, PROJECT_TAGLINE);
    fflush(stdout);
    int r = GameMain();
    printf("GameMain returned %d\n", r);
    return r;
}
