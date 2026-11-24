NICE CRASH - tester build
=========================

Nice Crash is a PC version of Excite Truck (Wii), made from a community decompilation and played with YOUR OWN copy
of the game. This download contains no game data at all: no disc files, no music, no textures, no tables from the
game. The game reads everything it needs from your own disc when it starts.

WHAT YOU NEED
  - Your own Excite Truck disc (USA, game ID REXE01), dumped from your Wii to a disc image (.iso, .rvz, .wbfs, .wia,
    .gcz, .ciso or a plain .nkit.iso) or extracted with Dolphin (Right-click > Properties > Filesystem > Extract
    Entire Disc). Dump it with your Wii and CleanRip or similar; other regions (Europe, Japan) don't work yet.
  - Windows 10 or 11, 64-bit, with any graphics card or laptop graphics from the last ten years (OpenGL 3.0),
    with its driver installed.
  - About 1 GB of free disk space for the unpacked game files (more for "Enhance my game").
  - A game controller (Xbox-style) or the keyboard. Keyboard, Wii Remote held sideways: 2 = accelerate,
    1 = brake, Left/Right (or A/D) steer, Up = turbo, Shift+Up/Down lean, Space = A, Enter = HOME menu.

FIRST RUN (about a minute)
  1. Unzip this folder somewhere with a short path, for example C:\Games\NiceCrash. Don't run it from inside the zip.
  2. Double-click "Start Nice Crash.cmd" (or build\NiceCrash.exe).
     Windows SmartScreen may warn that the app is unrecognised: click "More info", then "Run anyway".
  3. The launcher asks for your game: drag your disc image (or the extracted folder) onto the window, or pick it.
  4. It checks that it is the retail USA disc, unpacks it and indexes the files. Nothing is compiled.
  5. When it says "Ready to play", press PLAY. The launcher can put a shortcut on your desktop.

IF SOMETHING GOES WRONG
  - The launcher's Logs page lists every run. "Save report" makes a folder with the log, your settings and build
    info (your user name is removed). Nothing is sent anywhere; send that folder to whoever gave you this build.
  - An NKit image the launcher can't read: convert it with Dolphin (right-click the game > Convert File > ISO or RVZ)
    and pick that file.
  - Logs and settings live in %LOCALAPPDATA%\NiceCrash. Your unpacked disc lives in
    %USERPROFILE%\Games\Nice Crash (changeable in Settings). Delete both folders to start from scratch.

WHAT'S IN THIS FOLDER
  build\NiceCrash.exe       the launcher         build\NiceCrashGame.exe   the game (no game data inside)
  build\build_info.txt      which source it was made from (github.com/ablindingnight/nice-crash)
  build\tools\              dtk (disc reader), the optional AI upscaler
  LICENSES                  licences of everything included
