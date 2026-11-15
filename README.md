# Project Placeholder (working name) - PC port, phase 0

Unpublished work in progress. **No game files are included.** You supply your own Wii (USA, REXE01) disc,
extracted to a folder; everything is read from it at run time.

## Build
`sh build.sh` (needs zig 0.17+; links only Windows system libraries).

## Use
1. `build/import.exe "<your extracted disc folder>"` - verifies the dump (main.dol SHA-1) and indexes its
   resource sets. Writes `%LOCALAPPDATA%\ProjectPlaceholder\dump.ini`.
2. `build/et_host.exe` - opens a window with the boot logo loaded from your disc.
   Options: `--tex set.res/name.tex`, `--screenshot out.bmp`, `--scan` (decode every texture), `--disc`, `--dol`.

## Layout
- `lib/` shared code ported from the decomp: resource loader (res.cpp), textures (texture.cpp, drtex.cpp),
  CRC (crc.cpp), dump verification, config. Renaming the project: `lib/project.h`.
- `tools/import/` dump importer. `host/src/` window + renderer.
- `docs/` BOOT_PATH.md, ROUTE.md, PROGRESS.md.
