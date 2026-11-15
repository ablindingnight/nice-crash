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

## 2026-11-15: first link and first run of the game's own code
- `python tools/gametrial/link.py` compiles every game unit (-O1, no UBSan/exceptions), links the 310 that compile
  with the port runtime and host SDK into build/NiceCrashGame.exe (32-bit, /LARGEADDRESSAWARE, /DYNAMICBASE:NO).
  - Two-pass link: pass 1 without stubs asks lld which symbols nothing defines; build/stubs.s then defines exactly
    those (972 functions -> `port_unimpl("name (unit, address)")`, 73 asm-only data labels -> zeroed storage of
    their symbols.txt size). `/force:multiple` because the decomp spells MWCC's weak inline copies as plain
    duplicate definitions (lld-link run directly with zig's own CRT libs, learned from a `--verbose-link` probe).
- runtime/: port_main (calls the game's `main`, renamed GameMain), unimpl (NICECRASH_UNIMPL=continue returns 0 and
  lists each stub once), crash (symbolised x86 stack via Windows dbghelp + the PDB), **wiimem: MEM1 mapped at
  0x80000000 and MEM2 at 0x90000000**, the real Wii addresses (a 32-bit LAA process owns the top 2 GB on 64-bit
  Windows), with the OS globals the game reads (bus clock at 0x800000F8 ...).
- sdk/: first host SDK pieces: OS (init, arenas into the mapped MEM1/MEM2, OSReport/OSPanic, OSGetTime from the
  performance counter, caches/interrupts as no-ops), SC (US console defaults), NAND/VI/DVD init, WPAD/PAD/KPAD
  (stack "set up", no remotes).
- **Run:** static constructors run; the game's main runs kernel init through OS/VI/DVD/arenas/SC/NAND, the game's own
  heaps (mem.cpp) work in the mapped arenas, "filecd: started DVD services" prints, input init, VF (virtual FS)
  attach, then stops at OSCreateThread (sdmp3's decoder thread; a failed create is fatal by design).
- Lessons: the game's global operator new/new[] replace the host's (runtime code must not allocate with C++
  new before the game heap exists); unk_802E4D84.cpp (file layer on the boot path) still needs its compile fixes.
- Next: OSThread/OSMutex/OSMessageQueue on Windows threads (Wii scheduling: priority, run until block), then the
  DVD layer on lib/res, then VI/GX.

## 2026-11-15: threads, DVD, GX FIFO; the game's own resource system runs
- sdk/thread.cpp: OSThread/OSMutex on Windows threads with one "CPU" lock (Wii single-core semantics; a thread
  yields only where a Wii thread blocks). The MP3 decoder thread starts.
- sdk/dvd.cpp: DVDOpen/Read/ReadAsync/Close from the imported disc folder (dump.ini).
- GX FIFO: prep.py rewrites game code's `(*(volatile WGPipe*)0xCC008000)` to a host object (shim/include/
  port_fifo.h) that appends big-endian bytes to sdk/gxfifo.cpp's buffer. Plan for milestone 2: compile the decomp's
  matched SDK GX source for the host too, so all GX calls produce the real FIFO stream, and write a FIFO
  interpreter (BP/CP/XF + draws -> OpenGL), the way Dolphin's backends work.
- Port patches (bg.cpp vtable patch dropped 2026-11-15: fixed in the decomp ef19dbe4d) (patches/*.patch.txt, exact-text, must match once): res.cpp evaluation order (fixed in the decomp at 3ba5ef67c, patch removed; MWCC evaluates call
  arguments before the object pointer), debug log printing (port-only), allocator boundary (port-only: untagged
  host `new` -> malloc, delete of non-MEM1/MEM2 pointers -> free).
- **Endianness:** the 9 little-endian byte-swap helpers (36 definitions) are emptied in the port copy: the disc's
  LE data is already native on x86. Byte-level reads of swapped words (e.g. drtex palette unions) still need
  case-by-case fixes as they are found.
- **Run now:** kernel init, heaps ("stackmem: base == 0x80004000 ..."), mp3 thread, "gx: NTSC 480i ... did flip",
  `res: loading tocres.res`, the 9 startup sets queued, a chunk read, "texture: loaded lightmap" (the game's own
  texture.cpp), sound mixer; stops in model.cpp (debug-draw model setup in AppInit).

## 2026-11-15: AppInit done; the boot reaches the memory-card dialog (first interactive screen)
- model.cpp: mrModelFixup decided "already fixed up" when the counts looked small (true for unswapped LE data on
  the BE Wii); on an LE host they always look small. Port patch: test whether `elems` is still an offset.
- **Arity bugs** (new class): mrender.cpp declared `fn_802CAD74(void)` and relied on r3 still holding the texture
  id (PPC register ABI); on x86 the callee read garbage. Port patch passes it. tools/gametrial/arity.py finds every
  address-named function declared with fewer parameters than its definition: **107 call sites, 83 functions**
  (docs/ARITY.md, for the decomp sessions). The 240 sites declaring extra parameters are harmless with cdecl.
- **Run now:** AppInit completes (options, stats, achievements, chunks), AppMain -> menu main -> MemCardSystemLoad:
  no save data, "rmemcard: new user, missing either banner or memcard", then the memory-card dialog loop
  (MemCardDialogBg::RunChoices) waits for input. Nothing is drawn yet (milestone 2) and gtext.cpp (fonts, on the
  boot path, does not compile yet) is stubbed.
- Next: milestone 2 (GX: compile the decomp's SDK GX for the host + FIFO interpreter + window), input (KPADRead from
  keyboard), VI retrace pacing.

## 2026-11-15: milestone 2 groundwork: real SDK GX on the host, FIFO interpreter, 60 fps frame loop
- **The decomp's own SDK GX library runs on the host** (tools/gametrial/sdkbuild.py: all 15 libs/msc RVL_SDK/gx
  files). Prep rewrites every `WGPIPE.x = v` to the port FIFO, replaces the 7 paired-single asm helpers
  (GXTransform matrix/projection writers, GXLight WriteLightObj) with C that emits the same words, PPC intrinsics
  (__rlwimi/__rlwinm/__cntlzw) in a prefix, -fgnu89-inline for MWCC-style `inline`, host overrides for the
  interrupt-driven sync (GXDrawDone/Flush/sync callbacks/breakpoints/FIFO pointers).
- **Wii address space:** MEM1/MEM2 mapped twice (cached 0x8/0x9 and uncached 0xC/0xD views of one section),
  hardware register pages 0xCC000000/0xCD000000 and the locked cache 0xE0000000 as plain memory.
- **DOL data at real addresses:** runtime/dolload.cpp loads main.dol's data sections into MEM1 at their addresses;
  the asm-only data labels become absolute symbols there (scalars byte-swapped once from a generated table), so
  string pools indexed past their label work. The MEM1 arena now starts above the DOL image (0x80570000).
- **GX FIFO interpreter (sdk/gxinterp.cpp):** CP/XF/BP state, vertex sizes from VCD/VAT, display lists, draws,
  copies; fires the draw-sync/draw-done callbacks at the token/finish BP registers, writes the PE token register,
  and **honours GP breakpoints** (the game keeps the GPU a frame behind with GXEnableBreakPt; without it the
  flip waited forever). tools/gametrial/fifoparse.py replays NICECRASH_FIFODUMP captures offline.
- **VI:** window + the retrace as a polled "interrupt" (from OSGetTime, which every game wait calls): present,
  window messages, AX tick, pre/post retrace callbacks every 1/59.94 s. Silent AX (sdk/ax.cpp) with real voice
  objects. MWCC runtime names (__nw__FUlPCc ...) in runtime/mwcc_rt.cpp.
- Compile: prep now normalises 32-bit `long` -> `int` and gives column-0 templates explicit C++ linkage:
  **334 of 361 game units compile** (was 310). gtext (fonts), race, rmemcard, drsurf, homebtn, the file/sound
  layer unk_802E4D84 (port patches: asm GQR setup, Bink lwbrx, fn-pointer casts) now build.
- Tools: NICECRASH_STACKDUMP=<s> prints the main thread's stack after s seconds (find spins).
- **Run now:** boot -> AppInit -> memory-card dialog ("new user"), which runs at 60 fps: ~550 draws, ~3,300
  vertices, ~3.3 EFB copies per frame, 0 unknown FIFO opcodes. Nothing is rasterised yet; input not mapped.
- Next: rasterise (vertex decode -> GL, textures via lib/texture decoders, TEV approximation, EFB copy -> XFB
  texture -> window), then keyboard -> WPAD/KPAD to press A.

## 2026-11-15: MILESTONE 2 first frame: the game's memory-card dialog rendered through its own code
- sdk/gxrender.cpp: fixed-function GL rasteriser fed by the FIFO interpreter: vertex decode (direct + indexed
  arrays, fixed-point fractions, all colour formats), XF position matrix/projection/viewport on the CPU, texgen
  with texture matrices, textures from BP image registers (lib/texture.cpp GX decoders + C4/C8/C14X2 with TLUTs
  loaded into a host TMEM), blend/depth/alpha-test/scissor from BP, TEV approximated as colour x texture,
  EFB clears on copy, display copy = present. Screenshot: docs/screenshots/milestone2_first_frame_memcard_dialog.png
  (git-ignored). Tools: NICECRASH_SHOT=<frame>:<bmp>, NICECRASH_DRAWLOG=<frame>, NICECRASH_TEXDUMP=1.
- sdk/mtx.cpp: the SDK's paired-single MTX/VEC functions in C (Concat, InvXpose, Scale, Trans, Ortho, Perspective,
  vector ops).
- **MWCC-name bridges** (link.py): units that call a C++ function by its MWCC-mangled or plain C name
  (Mat3Mul__FP4Mat3PC4Mat3PC4Mat3, used by gx.cpp) get a jump to the host definition: 26 bridges.
- More LE byte-swap helpers neutralised after a body scan (SwapUV in gtext scrambled the glyph UVs, PSwap16/32,
  SwapN, GSwap32, Swap2, SwapName): 16 helper names in total; value swaps (Swap, SwapItems) untouched.
- The decomp adopted the ARITY fixes (2bf8689a7, 21a31da6b): the port's mrender patch is gone.
- Visible issues: dialog lines overlap (line advance too small; to check), NAND is stubbed so the save reads as
  "corrupted", TEV/lighting approximate, no input yet.

## 2026-11-15: input, the dialogs answered, "demo mode", and three new bug classes
- **Input (sdk/input.cpp):** remote 0 is a connected core remote. Keyboard: Enter = 2 (confirm/accelerate: the
  remote is held sideways), Backspace = 1, A/B, +/-/Home, arrows = D-pad (rotated). NICECRASH_INPUT scripts
  presses by frame. With "200:2,420:2" the boot answers both memory-card dialogs ("cannot be used ... corrupted",
  "You will not be able to save ... Is this okay? Yes") and reaches **"mmain: demo mode!"** (the title-screen
  path) when MenuDoDemo/popwarn was still stubbed.
- **The decomp's batch-1 portability fixes** (8c7a664af) + port **linkage fixes** (tools/gametrial/linkagefix.py:
  the later C++-scope declaration of a name an extern "C" block declared gets `extern "C"`, 106 sites):
  **359 of 361 game units compile**. Obsolete port patches removed (homebtn asm, unk_802E4D84 casts, arity).
  Response-file link (command line > 32K).
- **New bug class: __thiscall stubs.** i386 MinGW member functions are thiscall (callee pops its stack args); the
  generated stubs now `ret N` with N parsed from the Itanium mangled parameters (link.py thiscall_pop). A stubbed
  UITable::Set had left 16 bytes on the stack and the caller returned into the heap.
- **New bug class: vtable destructor slots.** MWCC: a virtual destructor is one slot; Itanium: two. bg.cpp declared
  BgObject {vf08, Update}, other units {~BgObject, Update}: the background-task Update() ran ~SoundManager. Port
  patch for bg.cpp; tools/gametrial/vslots.py lists 14 more candidate classes -> docs/VTABLE_NOTES.md.
- Intrinsics __mulhwu/__mulhw/__frsqrte/__fres; crash handler walks EBP / scans the stack when EIP is wild;
  game units built with frame pointers.
- **Run now:** AppInit completes (music tracks load), the memory-card dialog now runs through the real UI system
  (MemCardDialogMgr -> uiapi -> window.cpp) and crashes in uiapi fn_80215AA4 reading a string as a pointer. Next.
- Open: dialog line spacing (20-unit step vs 42-unit glyphs; same in Show() which matches 99%: needs a retail
  reference image).
- **All 361 of 361 game units compile** (2026-11-15): the decomp's SOURCE_NOTES batches (8c7a664af, ac52fb77c) plus
  port items the coordinator handed back: unk_8020F3B4.c compiled as C++ (it uses `#pragma cplusplus on`),
  trailer.cpp TMtx() {} (patch), loading.cpp fn_80011280 arity with retail h = 140/160 (patch), and boostman.cpp
  explicit specialisations declared before first use (patch). 376 objects link, 42 MWCC-name bridges.

## 2026-11-15: cdecl methods (Cygwin triple), retail vtables and DOL pointers translated, first layout dialog runs
- **Calling convention:** game and SDK code now compile with clang's i686 *Cygwin* triple (MinGW headers/libs kept,
  `_WIN32`/`__MINGW32__` re-defined): C++ methods are cdecl (MinGW's default is thiscall), so `this` is the first
  stack argument as it is the first register argument on PowerPC. That keeps hand-written vtables of free functions
  and per-unit virtual declarations with differing parameter lists working. Bonus: GCC-style bitfield packing (as
  MWCC), not MS-style. Thiscall stubs (`ret N`) switched off. `#pragma clang attribute cdecl` is not supported.
- **Hand-written vtables (uiapi.cpp, 7):** prep rewrites them to Itanium layout (two destructor thunks, vptr at
  slot 0). Placeholder dispatch classes VfObj/Vf4Obj/Callback get a destructor in slot 0 (port patch).
- **Retail vtables in asm-only data (75):** link.py reads them from the player's DOL and builds host tables: each
  entry resolved to the host function by name (C names, or MWCC `Method__<n>ClassF` -> Itanium), destructor -> D1/D0
  thunks; the label points at the host table. All 75 resolved.
- **DOL data pointers:** words in asm-only data that point into retail code become the host function (52), data
  pointers are byte-swapped (4) (runtime/dolload.cpp, port_dolptr_table). The memcard MenuDesc's poll callback was
  the case found.
- The Microsoft Store Python cannot see %LOCALAPPDATA%\NiceCrash (filesystem virtualisation):
  tools/gametrial/link.sh passes the DOL path in NICECRASH_DOL.
- **Run now:** the layout-driven dialog mc_dialog.lyt runs its loop through the real UI (UIDoMappedDialog); the
  scripted press chose "delete" (rmemcard: deleting foo.bar / banner.bin); the next dialog crashes in a virtual
  call where HitTest lands in a Bounds function (widgets.cpp fn_80229AE4 -> fn_80222AFC): a class whose
  declarations disagree about the destructor slot. widgets.cpp has ~115 hand-written dispatch helpers: next step is
  a systematic slot audit (extend vslots.py to dispatch structs and translated tables) rather than crash-by-crash.

## 2026-11-15: MILESTONE 3 (first pass): the title screen, through the game's own boot code
- Hand-written MWCC vtables are rewritten to Itanium layout in every unit (11: uiapi 7, widgets 3, collisionsound
  1). The decomp took the uiapi VfObj/Vf4Obj/Callback destructor fix (9ad9bdea8): that port patch is gone.
- uiapi fn_80215828's local `Obj` and fn_8021583C's `Local` declared their destructor slot as a placeholder:
  infinite recursion on the PC (port patch). docs/SLOT0_PLACEHOLDERS.md lists all 98 classes whose first virtual
  is a positional placeholder, for a retail-checked pass by the decomp (known non-destructor slot 0: PathSet, sol
  Phob, CollVolume).
- Crash handler reserves stack (SetThreadStackGuarantee) so stack overflows are reported.
- **Run (scripted presses 200/420/640: 2):** both memory-card dialogs (layout-driven), "demo profile", then the
  **title screen**: "Press [2] to Start" banner and "(c)2006 Nintendo/Developed by Monster Games", rendered by the
  game. Screenshot docs/screenshots/milestone3_title_screen.png (git-ignored).
- Missing on it: the background video (Bink: video/uisingle.bik; DVDFastOpen is a stub), the button icon in the
  banner, black boxes behind some text (TEV approximation), audio.

## 2026-11-15 (later): the title's Bink video plays; TEV and indirect texturing as shaders
- **DVD:** DVDConvertPathToEntrynum/DVDFastOpen/DVDCancel; open files are tagged inside DVDFileInfo so copies of
  the struct (Bink reads through one) still find their file.
- **Bink decode (unk_80116BD4.c), four port-only defects, all reported to the decomp:**
  - PowerPC `srw` gives 0 for a shift count of 32; x86 keeps the value. `0xFFFFFFFF >> (32 - n)` masks with n == 0
    (one-symbol Huffman trees) -> prep rule ppc-srw-mask (port_srw, 34 sites).
  - Huffman lookup table 0 (lbl_802F65C0) is bytes written as big-endian u32 words (patch).
  - The pattern-mask macros address lbl_802F65C0 + 0x3E0.. (past a 16-byte label; retail .rodata order only):
    they are lbl_802F6990 + 0x10.. (patch).
  - The dequantisation tables lbl_802F6A40/lbl_802F7A40 are u8 initialisers read as s32 (prep WORD_TABLES).
- **DOL label swaps typed by the source:** link.py reads each extern declaration's element type; a label is
  swapped per element (u8 tables no longer reversed as if they were f64, big u32/f32 tables now swapped).
- **Past-label string pools:** `lbl_X + N` past a source-defined byte array (inputbind's 56 LocStrings) is
  redirected to the retail address, where the DOL's data is loaded (prep rule past-label-retail;
  tools/gametrial/pastlabel.py lists all candidates).
- **TEV as GLSL (sdk/gxtev.cpp):** colour/alpha combiners incl. compare modes, konst, swap tables, alpha compare,
  both colour channels, every texture map on its own unit; indirect texturing (stages, matrices + scale, wrap,
  add-previous, dynamic S/T). NICECRASH_TEVLOG=1/2 (programs / per-draw use), NICECRASH_TEVDBG=<stage>,
  NICECRASH_NOTEV (fixed-function fallback). The black boxes behind text are gone.
- **Big-endian GX data built by the CPU:** drtex's font palettes (u16 stores, patch gx-stores) and the SDK's
  `*(u32*)&GXColor` / GXColorS10 type-puns (sdkbuild.py colour_puns) -> konst/material/TEV colours now right.
- Crash reporter: registers + code bytes at EIP; port_watch(addr) / port_watch_value / port_watch_range hardware
  write watches (DR0) for "who overwrote this" bugs.
- **Run:** the memcard dialogs and the title screen with the uisingle.bik video in colour
  (docs/screenshots/m3_title_bink.png).
- `gArtGet(extruck.art)` / `blinker*.img` "not found": no resource archive on the disc has them (searched the
  unpacked RST0 archives); retail logs the same miss. Not a port bug.
- Still open on the title: the [2] button icon in the banner, a one-pixel dashed seam across the video (indirect
  coordinate rounding), audio.

## 2026-11-15 (afternoon): the full title screen (logo, sparkle, [2] icon)
- **Int/float parameter order (tools/gametrial/regorder.py, docs/REGORDER.md):** on PowerPC ints and floats go
  in separate register files, so `f(int, u32, float...)` and `f(int, float..., u32)` are the same call; on x86
  the callee reads the wrong stack slots. 52 declarations of 34 functions differ from their C definitions
  (fn_802354A0, the art drawer, in 12 units: widgets' colour -1 landed in x as NaN, so all gart art vanished).
  prep turns each into a static inline adapter that forwards to the definition's symbol in its order (49).
- **Weak inline helpers:** units that declare `Dot`/`Max`/... without the body called MWCC's out-of-line weak
  copy; clang only emits inline functions where it does not inline them, so the call was stubbed (a float stub
  returns NaN: the logo sparkle's matrix). prep rule inline-emit adds `__attribute__((used))` (680 sites).
- **Itanium bridges:** C++ declarations whose types differ from the definition (mcsys fn_802E6BD8 as
  achieve/ident declare it) have another mangled name; link.py now binds them to the single definition of that
  name and scope (stubs 399 -> 309).
- **fn_802C8E00 (mrender) forwards its caller's vertex** to tristrip fn_801E4F3C by leaving the registers alone
  (patch arity-forwarders): the [2] icon in "Press [2] to Start" and every immediate-mode quad.
- The game's stack-overrun check budgets 29K (patch host-stack: 256K on the PC).
- FIFO: the GP breakpoint moves with the data when the buffer restarts at 0.
- NICECRASH_SHOT / NICECRASH_DRAWLOG count retrace frames now (menus present many copies per retrace).
- Boot flow seen: NAND "corrupted" dialog (NAND is stubbed), save-file dialog, **Choose a Profile** (over its
  background movie), then the title with logo, sparkle, the [2] icon and the uisingle.bik video
  (docs/screenshots/m3_title_full.png, retrace 1300 with presses 200:2, 420:2, 640:2, 900:1).
- Open: a 4-row band of the video (screen rows 204-207) takes the wrong indirect ramp offset (cosmetic); a faint
  rectangle behind the logo; NAND saves; audio.

## 2026-11-15 (evening): pinned decomp, NAND saves, framework transition
- **Pinned decomp:** tools/gametrial/decomp_ref.txt + pin.sh; tools read a `git archive` snapshot in build/decomp/<commit>
  (read-only on the matching repo), so other sessions' work in progress no longer breaks the port mid-day.
  Pinned at 25ff62004 (UI framework step 1 included). 9 regorder adapters left (the decomp fixed the rest).
- **Framework transition (real UIWidget classes):** link.py binds MWCC-named member calls from C code to the
  Itanium definitions (overloads by parameter encoding), binds old fn_<addr> names to whatever symbols.txt now
  calls that address, and gives ctor/dtor bridges a thunk that returns `this` (MWCC ctors return it).
  `-fno-strict-return`: UICustom::Slot68 falls off the end of an int function (PPC returns r3).
- **NAND saves (sdk/nand.cpp):** the game's NAND home = %LOCALAPPDATA%\NiceCrash\nand. First boot: new user,
  banner.bin + foo.bar written; next boot: "banner is ok", straight to the title.
- **Faint box behind the logo:** the logo's alpha palette gives its transparent pixels alpha 15 (~6%), built
  from the game's own palette bytes as on the Wii; not changed (no hardware reference to compare).
- **Video seam (open):** a dashed 4-row band in the uisingle.bik picture near screen row 204. Ruled out: the
  decoded frame (clean), bilinear filtering (still there with nearest), sampler rounding of the ramp lookups (now
  fixed point as the hardware). The final luma coordinates follow the same ramp phase as the good rows; next: dump
  the Y plane of the exact frame shown and compare texel by texel.
- Debug aids: NICECRASH_TEVDBG=[c|b|i]<stage> (final/base texel coords, indirect values), NICECRASH_NEAREST,
  NICECRASH_NANTRAP=<n> (stack of NaN floats sent to the GPU), crash reports dump memory + vptr symbol for
  registers pointing into Wii memory.

## 2026-11-15 (night): audio
- **Host AX mixer (sdk/ax.cpp):** PCM16 / PCM8 / DSP-ADPCM voices read from the Wii memory image, linear SRC,
  volume envelope and main L/R mix at 32 kHz; voice state in the real VPB fields (Excite Truck's older AX: PB at
  VPB+0x138, current address at VPB+0x1BC as the game reads it); one AX frame callback per 96 samples, paced by the
  retrace; waveOut (winmm, part of Windows) through a 2 s ring buffer. Not yet: aux/reverb sends, surround, LPF,
  the Wii Remote speaker.
- **DSP-ADPCM headers are big-endian** inside the little-endian SFX0 resources (Nintendo tool output embedded in
  Monster Games' format): swapped once on load and for streamed samples (patch dsp-adpcm-header). Before that the
  loop/end addresses were garbage and the title played noise.
- PCM16 is read in host order (the port does not run the game's byte swaps; the MP3 player's PCM is CPU-built);
  NICECRASH_PCMBE flips it if a big-endian source turns up.
- Verified from a capture (NICECRASH_WAV): title music, an ADPCM stereo loop; sample autocorrelation 0.93, L/R
  correlation 0.93, peaks ~17k. NICECRASH_AXLOG lists voice setups. Listened to on speakers 2026-11-15: sounds right (user).

## 2026-11-15 (late): compared with Dolphin; colours, 16:9, flicker, icon
- **Palette byte order:** image palettes are little-endian on disc and the Wii loader swaps them; the port now
  reverses each word in drtex fn_802D78D4 (logo orange/yellow/silver instead of pink; the profile star's blue box
  and the faint box behind the logo were the alpha read from red).
- **16:9:** SCGetAspectRatio reports widescreen (NICECRASH_ASPECT=4:3), window 854x480, the 448-line picture
  centred with bars above and below as on a TV (and in Dolphin).
- **__float_epsilon was a stub function:** classify.py stripped every leading underscore from C names, so MSL's
  __float_epsilon/__float_max/_current_locale were stubbed as code; NearZero compared against code bytes. With the
  data back, the widescreen camera widens its field of view and the UI matches Dolphin's 16:9 layout.
- **Display = Wii VI:** EFB in an FBO, display copies into per-address XFB textures (sdk/xfb.cpp), one present per
  retrace of the XFB the game selected; frame pacing (the first GXFlush after a display copy waits for the retrace;
  deferring the FIFO instead deadlocks on the game's draw-sync busy-wait); empty copies are not shown. 0 black
  frames in 1800 (was ~1 in 5 around the title).
- **Video strip:** Dolphin shows it too unless "manual texture sampling" is on; the port now samples indirect
  stages manually (1/128 texel coordinates, 1/16 bilinear weights, texel-centre fetches): gone.
- **Keys like Dolphin:** 1/2 = remote 1/2, Enter = HOME. Pressing 2 on the title opens Choose a Profile.
- **Icon:** the user's X artwork -> assets/icon (transparent PNG + .ico via tools/makeicon.py), embedded in both
  exes, used for the window/taskbar.
- Open: HOME menu (HBM library is stubbed), controller (XInput), profile slots on Choose a Profile (compare with
  Dolphin).
