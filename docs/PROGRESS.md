# Port progress log

## 2026-11-15
- Created `port/` (own git repo, nothing pushed). `.gitignore` keeps game data and screenshots of game assets out.
- Host toolchain: zig 0.17.1 (`zig c++ -target x86_64-windows-gnu`), Win32 + OpenGL 1.1 from the system
  `opengl32.dll`. No downloads needed.
- Ported from the decomp, as host-side code (no struct overlays on file data, 64-bit clean):
  - `lib/crc.cpp`: CRC-32 (crc.cpp fn_80132E7C); table generated from the polynomial.
  - `lib/res.cpp`: RST0 sets, the tocres.res TOC library, resident load, on-demand extraction,
    ResourceGet (res.cpp fn_8012AFE0, fn_8012AA60, fn_8012ACA0, fn_8012A470, fn_8012B534, fn_8012B6FC,
    fn_8012B85C, fn_8012C370). CRCs are checked like the game does.
  - `lib/texture.cpp`: tt_texture v15 header (texture.cpp fn_802CA658, fn_802CA57C, fn_802CA3F4) and
    drtex.cpp's format dispatch (drCreateTexture): CMPR, CMPR+I4 alpha, C4/C8 with RGB565/IA8 TLUTs, I4, I8,
    IA4, IA8, RGB565, RGB5A3, RGBA8.
- MGI_logo: `.tex` = 128x128 format 0x42 (CMPR via fn_802D8A84), 5 LODs; `.tm0` = 256x256 mip-0 stream
  (6 LODs). File sizes match 0x80 + the CMPR mip chain exactly.
- Coordinator/user additions (2026-11-15): the launcher is milestone 2.5 (ROUTE.md); the project name lives only in
  `PROJECT_NAME` (lib/project.h); the **dump importer** comes first in phase 0.
- Layout: `lib/` (shared: loader, decoder, CRC, dump check, config), `tools/import/` (importer CLI),
  `host/src/` (window). `sh build.sh` builds `build/import.exe` and `build/et_host.exe`.
- **Dump importer done** (`build/import.exe <extracted disc folder>`): finds DATA\files / files\ / the files folder,
  SHA-1 of main.dol vs build.sha1 (Windows bcrypt), rejects unknown builds and disc images (.iso/.rvz: planned;
  needs a Wii disc reader), mounts every set through the ported loader with all CRCs checked, writes
  `%LOCALAPPDATA%\ProjectPlaceholder\dump.ini` + `sets.tsv`. On the user's dump: verified, 114 sets, 8,138
  resources (same count as resunpack.py), 0 failures. Tested rejections: wrong DOL hash (exit 3), .iso (exit 2),
  folder without tocres.res (exit 2).
- **Milestone 1 done.** `build/et_host.exe` reads the importer's config, re-verifies the dump, mounts tocres.res +
  race.res, extracts MGI_logo.tm0 (non-resident) and shows it with MGI_logo.tex on an NVIDIA GL context.
  Screenshot: `docs/screenshots/milestone1_mgi_logo.png` (git-ignored: it shows a game asset).
- `et_host --scan`: all 2,733 TEX resources on the disc decode (0x40 C4 20, 0x41 C8 25, 0x42 CMPR 1053,
  0x43 CMPR 75, 0x44 CMPR+I4 alpha 1091, 0x46 RGB5A3 169, 0x48 I4 230, 0x49 I8 7, 0x4A IA4 7, 0x4B IA8 56).
  Visual spot checks of C4, C8, RGB5A3, IA8, I4 and CMPR+alpha look right.

## 2026-11-15 (later): route A confirmed (user, via the coordinator)
- docs/BOOT_GAPS.md for the matching queue: 32 boot-path functions below 100% in 17 units, plus 36 NonMatching units.
- **Compile trial** (`python tools/gametrial/trial.py --obj`): every src/game unit compiled for 32-bit x86 Windows
  with zig. **310 of 361 compile as they are.**
  - Port-side shims, no decomp edits: `shim/include/types.h` (the decomp's compat.h blanks `__attribute__`/`asm`
    for clang editors and broke the system headers); u32/s32 as `int` (same size, matches x86 size_t: 310 clean vs
    305 with `long`); `port_prefix.h` (NONMATCHING, `__declspec(section)`, `__fabs`/`__fabsf`/`__abs`/`__cntlzw`);
    `tools/gametrial/prep.py` copies src + include into build/gamesrc with mechanical rewrites only
    (`operator new(unsigned long` -> size_t, 15 sites).
  - The other 51 units need real source changes: docs/SOURCE_NOTES.md lists each by cause and file:line
    (templates inside extern "C" 18 files, extern "C"/C++ linkage mismatches 16, rvalue-to-reference calls 19,
    switch jumps over initialised locals 4, conflicting prototypes 5, Bink/unk_8020F3B4 C files ...). 21 of them are
    on the boot path.
  - Tooling note: run zig with a working directory outside %LOCALAPPDATA%'s parent. From C:\Users\theop\AppData it
    writes relative cache paths ("Local\zig\...") and fails with "failed to rename ... FileNotFound" (30 s/unit);
    from port/ a unit takes 0.1-0.5 s.

## 2026-11-15: named **Nice Crash** (user)
- PROJECT_NAME = "Nice Crash", PROJECT_DIRNAME = "NiceCrash" (lib/project.h). Host exe is now build/NiceCrash.exe,
  its optional ini NiceCrash.ini; the importer config moved to %LOCALAPPDATA%\NiceCrash (re-imported, old
  ProjectPlaceholder folder removed).
