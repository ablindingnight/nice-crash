#!/bin/sh
# Builds build/NiceCrash.exe (phase 0 host) and build/import.exe (dump importer), 64-bit Windows, zig c++.
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
    -I "$L" "$@" "$L/crc.cpp" "$L/res.cpp" "$L/texture.cpp" "$L/dumpcheck.cpp" "$L/portconfig.cpp" -lbcrypt
}
cxx -o "$B/import.exe" "$HERE/tools/import/import.cpp"
echo "built $B/import.exe"
cxx -o "$B/NiceCrash.exe" "$HERE/host/src/main.cpp" -lopengl32 -lgdi32 -luser32
echo "built $B/NiceCrash.exe"
