#!/bin/sh
# Builds build/NiceCrash.exe (the launcher, plus the phase-0 viewer behind --viewer) and build/import.exe (dump
# importer), 64-bit Windows, zig c++.
# Only Windows system libraries are linked: nothing to download.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
B="$HERE/build"
L="$HERE/lib"
Z=${ZIG:-$(command -v zig || find /c/Users/theop/AppData/Local/Microsoft/WinGet/Packages/zig.zig* -name zig.exe | head -1)}
[ -x "$Z" ] || { echo "zig not found (set ZIG=...)"; exit 1; }
mkdir -p "$B"
cxx() {
  "$Z" c++ -target x86_64-windows-gnu -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing \
    -I "$L" "$@" "$L/crc.cpp" "$L/res.cpp" "$L/texture.cpp" "$L/dumpcheck.cpp" "$L/portconfig.cpp" "$L/settings.cpp" -lbcrypt
}
cxx -o "$B/import.exe" "$HERE/tools/import/import.cpp"
echo "built $B/import.exe"
# The program icon (assets/icon): a resource object both executables embed.
(cd "$HERE/assets/icon" && MSYS_NO_PATHCONV=1 "$Z" rc /fo ../../build/nicecrash.res nicecrash.rc)
H="$HERE/host/src"
cxx -o "$B/NiceCrash.exe" "$H/main.cpp" "$H/viewer.cpp" "$H/launcher/launcher.cpp" "$H/launcher/ui.cpp"   "$H/launcher/tasks.cpp" "$B/nicecrash.res" -lopengl32 -lgdi32 -luser32 -lgdiplus -lole32 -lshell32 -lwinmm   -Wl,--subsystem,windows
echo "built $B/NiceCrash.exe"
