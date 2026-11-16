# Nice Crash - PC port, phase 0

Unpublished work in progress. **No game files are included.** You supply your own Wii (USA, REXE01) disc,
extracted to a folder; everything is read from it at run time.

## Build
`sh build.sh` (needs zig 0.17+; links only Windows system libraries).

## Use
`build/NiceCrash.exe` is the launcher: it finds your disc dump (or lets you pick it), checks it, indexes it with
the importer, builds the game when needed, holds the settings, and PLAY starts `build/NiceCrashGame.exe`.

- Settings: `%LOCALAPPDATA%\NiceCrash\settings.ini` (only values you changed; `lib/settings.cpp`).
  `NICECRASH_<SECTION>_<KEY>` environment variables override it (also `NICECRASH_ASPECT`, `NICECRASH_ERROR_LOG`).
- Logs: `%LOCALAPPDATA%\NiceCrash\logs\` (`nicecrash_log.txt` + 4 older runs when "Save error logs" is on, and
  `runs.tsv`, the last 10 runs).
- Debugging: `NiceCrash --ui-shot <home|disc|firstrun|settings:<page>> out.bmp [w h]` renders a screen;
  `NiceCrash --launch-game [s]` starts the game as PLAY does; `NICECRASH_GAME_EXE` and `NICECRASH_SETTINGS_FILE`
  point at another game exe or settings file.

By hand: `build/import.exe "<your extracted disc folder>"` verifies the dump (main.dol SHA-1) and indexes its
resource sets into `%LOCALAPPDATA%\NiceCrash\dump.ini`. `build/NiceCrash.exe --viewer` is the phase-0 viewer (the
boot logo from your disc); options `--tex set.res/name.tex`, `--screenshot out.bmp`, `--scan`, `--disc`, `--dol`.

## Layout
- `lib/` shared code ported from the decomp: resource loader (res.cpp), textures (texture.cpp, drtex.cpp),
  CRC (crc.cpp), dump verification, config. Renaming the project: `lib/project.h`.
- `tools/import/` dump importer. `host/src/launcher/` the launcher (GDI+); `host/src/viewer.cpp` phase-0 viewer.
- `docs/` BOOT_PATH.md, ROUTE.md, PROGRESS.md.
