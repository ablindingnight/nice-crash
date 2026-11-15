# Route decision (phase 0) - awaiting the user's confirmation

Goal for this decision: **boot to the intro screen soon**, then into play, without throwing work away later.

## The numbers

Whole game (build/REXE01/report.json, 2026-11-15, branch runtime-decomp):

| | value |
|---|---|
| code | 3,050,236 bytes, 83.0% byte-exact |
| functions | 13,780; **13,354 exact (96.9%)** |
| functions not exact | 426 (517 KB): 102 at 99%+, 217 at 90-99%, 104 at 50-90%, 3 below 50% |
| functions with no C at all | none found: every unit has a source file; inline `asm` only in drsurf.cpp, homebtn.cpp, unk_80116BD4.c (Bink) |
| units | 538 Matching (linked from source), 163 NonMatching (source exists, the matching build links asm) |
| paired-single intrinsics in game code | none (only the SDK's MTX/PSMTX uses them) |

The path to the intro screen: see BOOT_PATH.md (numbers copied below once that map is finished).

From BOOT_PATH.md (static call graph from `__start`, plus resolved indirect calls; report.json of 06:26):

| path to the first content frame | units | functions | code bytes | linked as asm today |
|---|---|---|---|---|
| game | 120 (84 Matching, 36 NonMatching) | 1,250 | 247,716 | 608 fns / 148,200 B |
| SDK (incl. HBM, VF, BTE) | 220 | 1,290 | 307,048 | 0 |
| MSL + runtime + sdk_mp3 | 51 | 161 | 92,716 | 0 |

- 97.4% of the game functions on the path score 100%; only **32 game functions (43 KB) are below 100%**. The other
  576 "asm" functions match already and are only waiting for their unit to link. For route A that means the C
  source for the path is essentially done.
- **What the first frame is:** on retail the logo movie (`logos.pss`) is skipped (`fn_80278A1C` is an 8-byte stub
  returning 0) and the Nintendo/Monster/Dolby logo layouts only run under `-e3_demo`. The first frame with content is
  the **title screen** (kiosk.lyt + extruck.art via MenuDoDemo -> fn_80246AE8), drawn by fn_802D0C98, the DOL's only
  `GXCopyDisp` caller. So milestone 3's target is the title screen, not a logo. (MGI_logo.tex belongs to the
  MONST_SwB.mod model in race.res, most likely a trackside banner.)
- Biggest units to bring up on the path: gx.cpp, res.cpp, widgets.cpp, uiapi.cpp, Bink (unk_80116BD4.c),
  texture.cpp, gtext.cpp, drtex.cpp, sdmp3.cpp, model.cpp. res/texture/drtex are already ported in port/lib.
- 372 static constructors run before main; 353 only register a ModuleTag.

## The three routes

### (A) Compile the decompiled C/C++ for the PC, fill the gaps another way
- Every function already has C source. "NonMatching" only means "not byte-identical with MWCC"; most of
  those functions are at 90%+ and differ in register choice or instruction order, not in behaviour.
- Work: a host build (zig/clang) of src/game + src/sdk_mp3 + a C replacement of the SDK (src/revolution is the
  real SDK source; we replace GX/VI/DVD/AX/WPAD/NAND/OS with the host layer, keep MTX/VEC math in plain C).
- Risks: functions that are near-matches but behave differently (logic bugs; the playbook has a few retail-proven
  examples), MWCC-isms (`asm` blocks in 3 files, 32-bit layouts: 4,850 raw-offset field macros), and big-endian
  assumptions. **Disc data is little-endian** (written by a 32-bit Windows tool), so on x86 the game's own
  byte-swaps of file data become no-ops: a pleasant surprise.
- The 32-bit layout problem goes away if the first build targets **32-bit x86** (`zig cc -target x86-windows-gnu`),
  exactly as the MP3 decoder test did last week. 64-bit comes later as structs replace raw offsets
  (port_audit.md).

### (B) Statically recompile the whole DOL (PPC -> C) plus the host layer
- Behaviour is exactly retail's, including bugs, with no dependence on decomp quality.
- Work: a PPC recompiler (Gekko/Broadway incl. paired singles, FPSCR/rounding, rlwinm/cr logic, indirect calls
  via function tables, exception-free), a memory model (24 MB MEM1 + 64 MB MEM2 big-endian emulated in a byte
  array, byte-swapped loads/stores everywhere), plus SDK functions trapped at their symbol addresses
  (we have every SDK symbol, so HLE by name is easy).
- Cost: building and debugging the recompiler is weeks before the first frame. Output is unreadable and slower
  (every load/store swaps), and we would still need the same host layer. Little of it carries over to the
  final port.

### (C) Hybrid: recompiled DOL now, decompiled units swapped in one by one
- Best when the decomp is far from finished. Here 96.9% of functions are exact and all have source, so the
  recompiler would be scaffolding for the last ~3%.
- Mixing needs a shared memory model: native C++ structs (little-endian, host pointers) and the emulated
  big-endian Wii RAM do not interoperate without marshalling at every boundary. That is the hard part of a hybrid
  and it is large.

## Recommendation: (A), 32-bit x86 first

- It is the shortest road to the intro: the code exists, and the host layer is needed in every route anyway.
- 32-bit first keeps every `F32(p, 0x1C)` layout valid with no source edits; 64-bit is a later clean-up task
  driven by port_audit.md.
- Near-match functions: compile them anyway; when behaviour looks wrong, compare with the DOL in Dolphin
  (or a tiny PPC interpreter for one function) rather than recompiling everything.
- What it needs, in order:
  1. **Port build of the game units** (no edits to excite-truck): a build script in port/ that compiles
     excite-truck/src/game/*.cpp with zig for x86, a shim header replacing MWCC-specific things
     (`#pragma` noise, `__declspec(section)`, `asm` blocks: 3 files get hand-written C replacements in port/).
  2. **Host SDK** (port/host/sdk): OS (heaps, time, threads as cooperative), DVD (files from the disc folder),
     VI/GX (record display lists -> OpenGL), PAD/WPAD/KPAD (keyboard/gamepad), AX (silence first), NAND/SC (stubs,
     then a save folder). The resource loader in port/host/src is already the shape this needs.
  3. Link, run to `main`, step through the boot path in BOOT_PATH.md until the logo frame.

## Next milestones (plan)
- **Milestone 1 (done when the screenshot exists):** host window, disc mounted through the ported loader,
  MGI_logo decoded and shown.
- **Milestone 2: GX display lists to the host renderer.** Implement the GX API the boot path uses (BOOT_PATH.md
  section 3): vertex formats (GXSetVtxDesc/AttrFmt), immediate GXBegin/GXPosition*/GXEnd, display lists
  (GXCallDisplayList parsed from the FIFO format), texture objects (reuse texture.cpp decoders), TEV stages
  -> generated GLSL (start with the 2D modes the logo and menus use), matrices (GXLoadPosMtxImm, projection).
  Upgrade from GL 1.1 to GL 3.3 core (function loader written by us, no download).
- **Milestone 2.5: the launcher** (user decision 2026-11-15; spec: handoff/port_refs/LAUNCHER_SPEC_2026-11-15.md,
  mockup launcher_mockup_user_2026-11-15.webp). After the GX renderer starts, before full boot.
  - Polished, game-like UI drawn with the host renderer: hero area, slanted side panel (Play / Settings / Builds /
    Logs / About), info card (dump region, shortened hash + Verified badge, build commit, Play, Controller,
    Graphics). Our own original UI art only.
  - Game art (logo, disc banner opening.bnr, menu/track textures) loaded at run time from the player's disc
    through the milestone-1 resource loader and texture decoder (`res::System`, `tex::Decode`); a neutral
    placeholder plus a "select your disc" flow when none is configured.
  - Dump verification reuses `CheckDump` (host/src/dumpcheck.cpp, SHA-1 of main.dol vs config/REXE01/build.sha1);
    unknown builds are refused with a clear message.
  - Name: never "Excite Truck"; the project name lives only in `PROJECT_NAME` (host/src/project.h) until the user
    picks one. The game's title appears only when describing the player's dump.
  - Until then the disc path and hash check live in `et_host.ini` (`disc=`, `dol=`) and the host's console output.
  - Host design for reuse: loader, decoder and dump check are free of window/GL code, so the launcher links them
    as they are.
- **Milestone 3: real boot code to the title screen** (the first content frame, BOOT_PATH.md). Compile the boot-path units (A, step 1), host SDK
  stubs for every call in BOOT_PATH.md section 3, run `main` and reach the title screen through the game's own code.
- **Milestone 4: input.** WPAD/KPAD from keyboard and XInput/SDL-less gamepad: tilt steering from a stick
  (and gyro later), buttons per porting.md section 7. Tilt needs playtesting.
