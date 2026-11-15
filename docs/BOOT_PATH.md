# Excite Truck (REXE01) boot path: DOL entry to first rendered intro frame

Generated 2026-11-15 from the matching repo `excite-truck` (branch `runtime-decomp`), read-only:
`config/REXE01/symbols.txt`, `config/REXE01/splits.txt`, `configure.py` (Object status),
`build/REXE01/report.json` (objdiff, built 2026-11-15 06:26), a disassembly of `build/REXE01/main.elf`
(06:22 build) with `build/binutils/powerpc-eabi-objdump.exe`, and `src/game/*.cpp`.

Status terms:
- **Matching** = the unit is `Object(Matching, ...)` in configure.py: built from C/C++ source and linked.
- **NonMatching** = `Object(NonMatching, ...)`: the linked DOL still uses the dtk asm for that whole unit,
  even when report.json scores most of its functions 100% (source exists for every NonMatching unit on this
  path, but the unit does not yet link from source). For a port, the C++ source of these units exists but is
  not proven by linking.
- "%" = report.json `fuzzy_match_percent` per function (shown only when below 100).

## 0. Headline findings (read first)

1. **There is no Monster Games logo on the retail boot path.**
   - `PlayAttractMovie` (app.cpp:163, 0x800049C0) is the last step of `AppInit`. It plays `logos.pss` and then
     one of ten `*siz.pss` attract movies through `fn_80278A1C`. In this build `fn_80278A1C`
     (0x80278A1C, carlayout.cpp:79) is a stub, `li r3,0; blr`, so no movie plays and nothing is drawn.
   - The logo layouts `LogoNint.lyt`, `LogoMnst.lyt` and `LogoDlby.lyt` (`fn_80246DE8`/`DF4`/`E00`,
     popwarn.cpp:264-268) are shown 2.5 s each by `fn_80246CB8`. Their only caller is `fn_80247210`
     (popwarn.cpp:411), the kiosk loop. That loop is reached only from `fn_8023B670` (mmain.cpp), only when
     `OptE3Demo()` is set (cmdline.cpp:126, bit 0x400 from `-e3_demo`).
   - `main` passes a zero-filled 0x100 buffer to `ParseCommandLine` (kernel.cpp `main`; disassembly at
     0x802E2BB4..0x802E2BD4), so no option is ever set on retail. The retail flow is therefore
     `fn_8023B640` → `MenuDoDemo` (popwarn.cpp:550).
   - `MGI_logo.tex` in `race.res` belongs to `MONST_SwB.mod` (a model in race.res, per `extracted/INDEX.tsv`
     line 6693/6694). The string `MGI_logo`/`MONST` does not appear in main.dol, so that texture is reached
     through model/track data. It is probably a trackside banner, not boot UI (an inference, not verified).
2. **The first VI frame-buffer swap happens inside GX init**, `fn_802CF600` (gx.cpp:802) → `fn_802CFF10`
   → `fn_802D0C98` ("gx: did flip"). It is shown black: `lbl_8055FD48 = 3` makes `fn_802D0C98` call
   `VISetBlack(1)`, and later frames would count it down. At the end of `fn_802CF600` the code calls
   `VISetBlack(0); VIFlush()`.
3. **The first frame with content on a normal boot is the title screen.** `MenuDoDemo` →
   `fn_80246AE8` ("kiosk.lyt" with the `extruck.art` MenuArt widget, popwarn.cpp:202) →
   `fn_802146BC` (UIDoMappedDialog) → `fn_802168F0` (window loop) → `fn_802166B8` (draw one frame) →
   `fn_802C2B48` (vidsetup.cpp:111) → `fn_802CFF10` → `fn_802D0C98` → `GXCopyDisp` + `VISetNextFrameBuffer`.
   `fn_802D0C98` (gx.cpp:1445) is the **only** caller of `GXCopyDisp` in the DOL.
   - Exception 1: on a console with no or corrupt save data, `MemCardSystemLoad` (inside `fn_8023B618`, before
     `MenuDoDemo`) can go to `fn_802E6FBC` → rmemcard. That path can raise memory-card dialogs through the
     callbacks `AppInit` registers (`fn_8023E7A8`... in mcdialog.cpp). Those dialogs draw through the same
     `fn_802146BC` pipeline, so the first visible frame would be a save-data dialog. Not verified at runtime.
   - Exception 2: during `AppInit`, `fn_80127464` (the "yield" called between load steps) only draws a frame
     (`fn_802ECC20` → `LoadingFrame`/clear → `fn_802CFF10`) while the HOME-menu modal flag
     `lbl_805600EB` is set (homebtn.cpp:336). On a normal boot nothing is drawn during the resident
     resource loads.
4. **Size of the path.** Static call-graph closure from the boot roots (below), plus the indirect targets I
   resolved:

| Side | Units | Functions | Code bytes | Still asm (NonMatching units) | Asm bytes |
|---|---|---|---|---|---|
| Game (`game/*`) | 120 (84 Matching, 36 NonMatching) | 1,250 | 247,716 | 608 functions | **148,200 (59.8%)** |
| RVL SDK (`revolution/`, `RVL_SDK/`, incl. HBM, VF, BTE, WPAD) | 220 | 1,290 | 307,048 | 0 | 0 |
| MSL C | 39 | 89 | 49,344 | 0 | 0 |
| Runtime.PPCEABI.H | 8 | 22 | 3,232 | 0 | 0 |
| sdk_mp3 | 4 | 50 | 40,140 | 0 | 0 |

   - Of the 608 asm game functions, 576 (105,096 bytes) already score 100% in report.json. They are blocked only
     because their unit does not link yet. Only **32 functions (43,104 bytes) are below 100%** (list in §5).
   - Function-wise the game side is 642/1,250 (51.4%) linked from source and 1,218/1,250 (97.4%) at 100%
     fuzzy. Byte-wise it is 99,516/247,716 (40.2%) linked and 204,612 (82.6%) at 100% fuzzy.
   - Without the resolved indirect targets (pure `bl` closure) the figures are 1,160 game functions,
     225,928 bytes and 547 asm functions / 132,192 asm bytes.
   - Static constructors (§4) add 353 game `__sinit` entries: 197 Matching, 156 NonMatching, 12,568 asm bytes.
   - The biggest asm blockers on the path, by bytes: `gx.cpp` (17.6 KB, the whole GX driver incl. the frame
     swap), `res.cpp` (16.5 KB, resource loader incl. `fn_8012C370`), `widgets.cpp` (16.2 KB), `uiapi.cpp`
     (12.4 KB, menu runner), `unk_80116BD4.c` (10.6 KB, RAD Bink middleware), `texture.cpp` (9.1 KB),
     `gtext.cpp` (8.0 KB fonts), `drtex.cpp` (7.8 KB), `sdmp3.cpp` (7.7 KB) and `model.cpp` (5.6 KB).

How "on the path" was computed: depth-first closure over direct `bl`/tail-`b` targets in the disassembly,
from these roots:
- `__start`'s callees, `main`'s callees and the full `AppInit`.
- The `AppMain` mode tests.
- The prefix of `fn_8023B6B0` up to `MenuDoDemo`.
- The prefix of `MenuDoDemo` up to and including `fn_80246AE8` (the title screen).
- The resolved indirect targets listed in §2.6.

Not included:
- The `-e3_demo`, `-gx_test`, `-model_test`, `-pa_test` and `-direct_to_track` branches.
- `AppShutdown`, and anything after the title screen returns.
- `.ctors` (counted separately in §4).

The closure over-approximates by following error and rare branches, such as `OSFatal` and the HBM `VPanic`.
It under-approximates through the 807 `bctrl`/`blrl` sites it cannot follow, except where §2.6 resolves them.

## 1. Summary numbers

See the table in §0.4. Per-unit numbers for every game unit on the path are in §6. The complete
per-function list (name, address, size, % when below 100, `[indN]` = N unresolved indirect call sites) is
in §7. SDK units are all Matching (§3 and §8).

## 2. Ordered call path (execution order, the spine)

Format: `name` (address, unit, status). Indented entries are callees in call order. "M" = Matching
(linked from source). "NM" = NonMatching (asm linked; the % is shown when report.json is below 100).

### 2.1 Reset to main: `__start` (0x80004170, RVL_SDK/os/__start.c, M; source libs/msc/src/RVL_SDK/os/__start.c:29)
1. `__init_registers` (0x800042DC, M): sets SP, r2 and r13 (SDA bases).
2. `__init_hardware` (0x80004414, M): HID and FPU setup.
3. `__init_data` (0x8000436C, M): copies ROM sections and clears .bss/.sbss from `_rom_copy_info`/`_bss_init_info`.
4. Reads BI2/OS globals (0x80000044, 0x800000F4, 0x800030E8). On a debug flag it calls `__set_debug_bba`
   (0x8000415C, M). It then calls `InitMetroTRK` (0x80030104) **indirectly via `blrl`**: the address is built with lis/addi
   at 0x80004208-0x80004210 (resolved by reading the disassembly).
5. Copies argv from the BI2 area (0x800000F4 + 8).
6. `DBInit` (0x80034064, M), then `OSInit` (0x8004ACB4, revolution/os/OS.c, M). OSInit brings up the heap
   arenas, EXI, SI, IPC, the cache and the interrupt/alarm/thread system. Its internals are SDK (§3).
7. `__check_pad3` (0x80004134, M) when the bootinfo bit is set. Then `__get_debug_bba`, and
   `InitMetroTRK_BBA` (0x800557DC) only on debug units.
8. `__init_user` (0x800560B0, M) → `__init_cpp` (0x800560D0, M). This walks `_ctors` (0x802ED060) **indirectly**
   (§4: 372 constructors).
9. `main(argc, argv)` (0x802E2BA8). Afterwards it does `b exit`.

### 2.2 `main` (0x802E2BA8, game/kernel.cpp, M)
`main` zero-fills `char cmd[0x100]` and ignores argc/argv.
1. `ParseCommandLine__FPCc` (0x80004D64, app.cpp:297, M) → `ParseOptions__FPCc` (cmdline.cpp, M). The string
   is empty, so all option bits stay 0.
2. `fn_802E2A5C` (0x802E2A5C, kernel.cpp:625, M), system init, in order:
   - `OSInit` (second call; OSInit guards itself), `VIInit`, `LCEnable`.
   - `fn_802E77A8` (callhook.cpp, M, empty).
   - `fn_802E3498` (file.cpp, M): file-id table. It calls `fn_802E3F94` (filecd.cpp, **NM**), which builds
     `FileDisk`; its ctor calls **`DVDInit`** + `OSReport("filecd: started DVD services")` (filecd.cpp:68-72).
   - `fn_801293B4` (scratch.cpp, M): 16 KB scratch buffer.
   - `fn_802E302C` (log.cpp, M): debug log.
   - `fn_8012820C(0x2000000)` (mem.cpp, M): MemAlloc heaps over the MEM1/MEM2 arenas. It uses `fn_802E3DF8`
     (memarena.cpp, M) → `OSGetMEM1/2ArenaLo/Hi`.
   - `fn_802E98D0` (sysconfig.cpp, M): `SCInit`, `SCCheckStatus`, `SCGetAspectRatio`,
     `SCGetProgressiveMode`, `SCGetSoundMode`.
   - `fn_802E7318` (strmfile.cpp, M), `fn_802E46B0` (keybd.cpp, M), `fn_80127C8C` (bg.cpp, M).
   - `fn_802E8CA8` (rmemcard.cpp, M): `NANDInit`. It registers the "memcard" bg task `fn_802E9010`.
   - `fn_80127538` (prof.cpp, M): registers the "ProfUpdate" bg task.
   - `fn_80129790` (joy.cpp, M): registers the "JoyUpdate" bg task `fn_80129400`. It reaches `fn_802E7C64`
     (rjoy.cpp, M): `PADInit`, `KPADInit`, `WPADRegisterAllocator`, `WPADSetConnectCallback`, `WPADGetStatus`.
   - `fn_802E5478` (unk_802E4D84.cpp, **NM**; upstream import), the sound hardware: `AIInit`, `AXInit`,
     `AXSetMode`, `AXAcquireVoice`..., `AXRegisterCallback(fn_802E4F98)`, `OSCreateAlarm`, and
     `OSSetPeriodicAlarm(.., 6.67 ms, fn_802E52EC)` (unk_802E4D84.cpp:495-498). The 6.67 ms alarm is the
     Wii Remote speaker stream: `WENCGetEncodeData`, `WPADSendStreamData`.
   - `fn_802E7578` (strmbank.cpp, M).
   - `fn_802EBE2C` (sdmp3.cpp, **NM**): SD-card MP3 player. `OSCreateThread(fn_802EAB28)` plus
     `OSResumeThread` (sdmp3.cpp:993), the "mp3" bg task `fn_802E9EB8`, and VF via `fn_802E9CF0`
     (`VFInit`, `VFAttachDrive`, `VFSetMediaCallback`).
   - **`fn_802CF600` (gx.cpp:802, NM): video/GX init.** In order:
     - `VIInit`; `fn_802CEAC4` (render-mode choice: `VIGetTvFormat`, `VIGetDTVStatus`, `GXAdjustForOverscan`);
       `VIConfigure`; `fn_802CEA34` (XFB alloc).
     - `new "gc:dyndata"` (0x177000), `new "local_flist"`; `fn_802D8D1C` (drtex.cpp, NM, texture table).
     - `new "fifo_buf"` (0x300000); **`GXInit`**; `fn_802D6810` (drsurf.cpp, NM).
     - `fn_802CF850` (GX default state: viewport, scissor, pixel format, copy clear, gamma,
       `VISetNextFrameBuffer`, `VIFlush`, `VIWaitForRetrace`).
     - `fn_802D0BD8` (before-render: viewport jitter, `VIGetNextField`).
     - **`fn_802CFF10` → `fn_802D0C98`: first GXCopyDisp/VISetNextFrameBuffer (shown black).**
     - `fn_802CF5A8`; `fn_802DACD4` (romfont.cpp, M: `OSInitFont`, `OSGetFontTexture`).
     - `fn_802D9C18` (discerr.cpp, M: `VISetPostRetraceCallback(fn_802D992C)`, `OSSetPowerCallback`).
     - `VISetPreRetraceCallback(fn_802CF10C)`; `fn_802DA2F4` (bonemgr.cpp, NM).
     - `GXGetTexBufferSize`/`GXInitTexObj` (shadow alpha); `GXSetDrawSyncCallback(fn_802CECF4)`,
       `GXSetDrawDoneCallback(fn_802CEC44)`.
     - `fn_802D37E4`; `fn_802DD4FC` (glare.cpp, NM); bg task "drIdle" `fn_802D9A50`; `fn_802DB168` (romfont, M).
     - `VISetBlack(0)`; `VIFlush`.
   - `fn_802EC614` (homebtn.cpp, M): HOME-button menu setup, `SCGetLanguage`, `HBMCreate`.
   - `fn_802E2CFC` (date via `OSGetTime`/`OSTicksToCalendarTime`); `fn_802E30C8` (debug print, compiled out);
     `stack_check`; `fn_80127C90(fn_802E2A1C, "check_stack")`.
3. `AppMain__Fv` (0x80004E20, app.cpp:318, M).
4. (After return) `fn_802E2B34` (shutdown) and `OSPanic("kernel.cpp", 210, "END")`. These are not on the boot path.

### 2.3 `AppMain` (app.cpp:318, M)
1. `fn_80265D48` (stub16.cpp, M, empty).
2. `fn_80129BD4` (sysinit.cpp:15, M) → `fn_80129C24` (random.cpp, M, seeds) and `fn_8012BE90` (res.cpp,
   **NM**, resource system init).
3. **`AppInit__Fv`** (0x80004B5C, app.cpp:207, M). In order:
   - `fn_802E2C20` (kernel.cpp:691, M): millisecond tick base via `OSGetTime`.
   - `LoadResidentRes` (app.cpp:191, M) → `fn_8012DDA8` (res.cpp:1517, **NM**) for each of `genesis.res`,
     `ResolveResName("winky")` → `fn_80130390` (locale.cpp, M: language archive, e.g. usenglish.res),
     `common.res`, `race.res`, `uibig.res`, `ghostcar.res`, `hobbyset.res`, `part.res` and `mcbanner.res`.
     `fn_8012DDA8` queues an async load (`AsyncSetQueue::Process`, `TocSizer::LoadSize`; all res.cpp, NM).
   - `fn_8012E81C("genesis.res")` (res.cpp:1791, **NM**): blocks until the set is loaded. It loops on
     `AsyncResourceSet::Status`, `fn_80127464` (yield) and `fn_802E2D74` (sleep). Reads go through the
     `FileDisk`/`FileCD` vtables (§2.6), i.e. `DVDOpen`, `DVDReadPrio`/`DVDReadAsyncPrio`.
   - The per-type resource loader `fn_8012C370` (res.cpp, **NM**) is reached from `fn_8012ACA0`,
     `fn_8012BF28`, `fn_8012D908`, `TocSizer::LoadSize`, `AsyncSetQueue::FindNodeFor` and the type
     consumers `fn_8012F150` (strtab), `fn_8013111C` (locale), `fn_80131588` (valtable) and
     `fn_80133BA0` (options).
   - `fn_80133024` (options.cpp:90, M): options/settings.
   - `fn_802C2AA8(2)` (vidsetup.cpp:90, M): video mode 2 (640x480), `fn_802CA0E4` (texture table),
     `fn_802C83CC` (mrender), clear colour, `fn_801E6978` (animstream).
   - `fn_80127464` ×2 (yield → `fn_802ECC74`).
   - `fn_802062E8` (sound.cpp:201, **NM**): SoundManager on the chosen mixer (`fn_80208D4C`, mixer.cpp, M).
   - `fn_80230090` (uidraw.cpp:73, M): UI start-up.
   - `fn_801303A8` (locale.cpp:308, M): localisation tables.
   - `fn_80245118` (menusel.cpp, M); `fn_802E712C` (mcsys.cpp, M, empty).
   - `LoadTimerInit` and `LoadTimerBegin("app_begin", ...)` (loadtimer.cpp, M): bookkeeping only, no drawing.
   - `fn_8012E81C("common.res")` (NM, blocking wait).
   - `fn_80232230` (gtext.cpp:598, **NM**): bitmap fonts; 11 indirect calls.
   - `fn_80137B20` (league.cpp, M); `DriverInit` (driver.cpp:150, M); `LoadTimerProgress` ×2; yields.
   - `fn_801E3C04` (dbgdraw.cpp, M); `RaceInit` (race.cpp:108, M: tracks.val); `IdentInit`; `StatsInit`;
     `AchievementsInit`; `FlagsInit` (all M).
   - `fn_80141454` (zone.cpp, NM, 4 bytes); `fn_8027DAF4` (pathcareer.cpp, M); `fn_8021031C` (unk_8020F3B4.c, NM);
     `fn_801AED54` (recordmgr.cpp, NM, 4 bytes); `RepInit`, `UnlocksInit` (M); `fn_802789A8` (stub18, NM);
     `fn_8020CA30` (roadsnd, NM, 4 bytes).
   - `LoadingInitHook` (loading.cpp:844, NM, 4 bytes); `fn_80211F44` (uiapi.cpp, NM).
   - `fn_802E986C..fn_802E9888` (rmemcard.cpp, M): register 8 memory-card dialog callbacks `fn_8023E7A8`,
     `fn_8023E5B4`, `fn_8023F0E8`, `fn_8023F4B8`, `fn_8023F694`, `fn_8023EEF8`, `fn_8023F2C4` and
     `fn_8023ED30` (mcdialog.cpp, all NM, 83-100%). They are not run unless a save error occurs.
   - `fn_802789AC` (stub18, NM); yield; `fn_802E47E8` (keybd.cpp, M).
   - `PlayAttractMovie` (app.cpp:163, M) → `fn_802E2CFC`, `fn_80129C80`/`fn_80129D18` (random.cpp, M) →
     `fn_80278A1C("logos.pss")`, which **returns 0 (stub)**. No movie plays.
4. `fn_802E30C8("AppMain is 0x%x")`, then the mode tests `OptGxTest`, `OptModelTest`, `OptPaTest` and
   `OptDirectToTrack` (cmdline.cpp, M). All are false on retail.
5. `fn_8023B6B0` (mmain.cpp, M).

### 2.4 `fn_8023B6B0` (mmain.cpp, M) to the title screen
1. `fn_80129AEC(0)` (joy.cpp, M); `fn_802E47E8` (keybd, M); `fn_80127A24` (eventq.cpp, M: clears the event ring).
2. `fn_8023B618` (mmain, M):
   - `fn_8023B5EC` → `fn_802E712C`, `fn_802E6CA4` (empty), and **`MemCardSystemLoad`** (mcsys.cpp:92, M).
     MemCardSystemLoad reads the NAND save via `fn_802E8D8C` (rmemcard, M: `NANDOpen`, `NANDRead`, `NANDClose`)
     and checks the CRC (`fn_80132E7C`, crc.cpp). On failure it may go to `fn_802E6FBC` →
     `fn_802E967C` / `fn_802E9254` (`NANDCreateAsync`/`NANDWriteAsync`) and the dialog callbacks.
   - `fn_8023D7CC` and `fn_8023D70C` (controls.cpp, M): control bindings.
3. `fn_8027B690` (eventpath.cpp:432, M); `fn_8012742C`/`fn_80127434` (M); `OptE3Demo` (false).
4. `fn_8023B640` (mmain, M) → `fn_802E30C8("mmain: demo mode!")` → **`MenuDoDemo`** (0x802473A4,
   popwarn.cpp:550, **NM**):
   - `fn_8020A600` (musicplr.cpp, **NM**): menu music cue. The "music" bg task `fn_8020A100` (97.4%) was
     registered earlier, from `AppInit`'s `fn_80211F44` → `fn_80211690` (uiapi.cpp, NM) → `fn_8020A47C`
     (musicplr.cpp, NM).
   - `fn_802ECEC4` (movieinfo.cpp, M).
   - **`fn_80246AE8`** (popwarn.cpp:202, **NM 97.8%**): title screen. It builds a `MenuArt` ("extruck.art"
     via `fn_8023512C`, gart.cpp) and a `MenuDesc{"kiosk.lyt"}`, then calls:
     - **`fn_802146BC`** (uiapi.cpp:1584, NM; UIDoMappedDialog):
       - `fn_802141A0` (uiapi.cpp:1449, NM 98.7%): builds the window from the layout.
         - `fn_8022E0EC`/`fn_8022E4C0` (autolyt.cpp, M): loads `.lyt`, through `fn_8012C370` (res).
         - `Window::Window` (window.cpp, M); `gxGetImage` (image.cpp, M).
         - `fn_802123B8` (uiapi.cpp, NM 99.8%, 7.6 KB): maps the widgets.
       - **`fn_802168F0`** (widgetwin.cpp:375, NM): window run loop, once per frame:
         - Input: `fn_802E46F0`, `fn_802E47B8` (keybd), `fn_80127A84` (eventq).
         - Time: `fn_8023660C`, `fn_80236674` (gtime).
         - Animation: `fn_80236780` (uianime).
         - Window update: `fn_8021734C`, `fn_80217734` (window.cpp, indirect widget methods).
         - **`fn_802166B8`** (widgetwin.cpp:300, NM): draws one frame:
           - `fn_802C8530` (mrender.cpp:296, M): begin 2D/3D.
           - `fn_80216244` (widgetwin.cpp:202, **NM 80.4%**): draws the window widgets via virtual
             `Draw`, e.g. `MenuArt::Draw` (0x80248AF0, popwarn.cpp, NM) and the `gText` and `gArt` draws.
           - `fn_802160AC`, `fn_80211F54`.
           - `fn_802C855C` (mrender.cpp:302, M): end; it reaches `fn_802CFDE0` (gx, NM: `GXSetDrawSync`, `GXFlush`).
           - **`fn_802C2B48`** (vidsetup.cpp:111, M):
             - **`fn_802CFF10`** (gx.cpp:1083, NM) → `fn_802CF5D0` → **`fn_802D0C98`** (gx.cpp:1445, NM).
               `fn_802D0C98` calls `GXSetZMode`, `GXSetColorUpdate`, `fn_802D09E8`, `GXSetDispCopyYScale`,
               `GXSetDispCopySrc/Dst`, `GXSetCopyFilter`, **`GXCopyDisp`**, **`VISetNextFrameBuffer`**,
               `VISetBlack` and `VIFlush`.
               This is the first title-screen frame. `fn_802CFF10` then calls `fn_802D0BD8` (next-frame setup).
             - `fn_80127468` (bg tasks), `fn_801279B8` (prof), `fn_802E4C18` (vmem).

### 2.5 The "yield" used everywhere during loading
`fn_80127464` → `fn_80127440` → `fn_802ECC74` (homebtn.cpp:336, M) → `fn_80127F94` (bg.cpp, M). The bg runner
calls up to ten registered per-frame tasks **through function pointers** (bg.cpp). On this path they are:
`check_stack`, `drIdle`, `JoyUpdate`, `ProfUpdate`, `memcard`, `mp3` and `music`. The GX-init variant
`fn_80127468` uses `fn_80127F70` instead. `fn_802ECC74` draws (`fn_802ECC20` → `LoadingFrame` (loading.cpp, NM)
or a clear via `fn_802D68C4`, then `fn_802CFF10`) only while the HOME-menu flags `lbl_805600EB` and
`lbl_805600E8` are set.

### 2.6 Indirect calls: what I resolved and how
| Site | Mechanism | Resolved to | How |
|---|---|---|---|
| `__start` 0x80004210 `blrl` | address in r6 | `InitMetroTRK` 0x80030104 | lis/addi in the disassembly |
| `__init_cpp` | `_ctors` table | 372 ctors (§4) | `.ctors` section dump of main.elf |
| `FileSystem`/`File` virtuals (res loads) | vtable | `__vt__8FileDisk` 0x8034D098 → Create/Open/OpenWrite/OpenAppend/Close/Print/Finder 0x802E4030..0x802E33D0; `__vt__6FileCD` 0x8034D130 → Read 0x802E4440, Write, ReadAsync, IsDone, Size, Tell, Seek, Skip, Flush, Disk, dtor | `.data` dump, symbols.txt |
| bg tasks `fn_80127C90(fn, name)` | function-pointer list | `fn_802E2A1C` check_stack (kernel.cpp:652), `fn_802D9A50` drIdle (gx.cpp:857), `fn_80129400` JoyUpdate (joy.cpp:180), `fn_80127968` ProfUpdate (prof.cpp:111), `fn_802E9010` memcard (rmemcard.cpp:348), `fn_802E9EB8` mp3 (sdmp3.cpp:998), `fn_8020A100` music (musicplr.cpp:205) | grep of registration sites; all registrars are on the path |
| `OSCreateThread` | thread entry | `fn_802EAB28` (sdmp3.cpp, NM 89.3%), the MP3 decoder thread | sdmp3.cpp:993 |
| `OSSetPeriodicAlarm` / `AXRegisterCallback` | callbacks | `fn_802E52EC` (6.67 ms alarm, Wii Remote speaker), `fn_802E4F98` (AX frame callback) | unk_802E4D84.cpp:495-498 |
| VI pre/post retrace | callbacks | `fn_802CF10C` (gx, NM 94.1%), `fn_802D992C` (discerr.cpp, M: reset/disc monitor) | gx.cpp:841, discerr.cpp:163 |
| GX draw sync/done | callbacks | `fn_802CECF4`, `fn_802CEC44` (gx, NM) | gx.cpp:851-852 |
| Title widget | vtable `__vt__7MenuArt` 0x80343780 | `Enter`/`Update`/`Draw__7MenuArtFv` 0x80248A68/A74/AF0 | symbols.txt |
| `PlayAttractMovie`'s movie player | direct, but a stub | `fn_80278A1C` returns 0 | disassembly 0x80278A1C |

**Unresolved:** 807 `bctrl`/`blrl` sites remain in path functions (232 functions, 127 of them game). The
largest are `fn_80236A5C` (uianime, 52), `fn_8012FAB8` (locale, 37), `ParseTrack` (eve, 30), `fn_8022C520`
(uistyle, 21), `fn_8011770C` (Bink, 16), `AchLoad` (12), `fn_8022CDE4` (12), `fn_80232230` (gtext, 11),
`fn_80205268` (difficulty, 11), `LoadVehicle` (9), `fn_80208E90` (mixer, 8), `ParseEveTable` (8) and
`fn_80216244` (widget Draw dispatch). Most are .val parsers dispatching on value types, and widget
virtuals. The widget `Draw`/`Update` virtuals of the other kiosk.lyt widgets (beyond MenuArt) were not
enumerated.

## 3. SDK / hardware calls on the path (called directly from game code)

Format: `SDK function` <- game caller(s). The header counts are SDK functions called directly from game code, and all functions of that library reachable on the path, including SDK-internal calls. **Every SDK, MSL, Runtime and sdk_mp3 unit on the path is Matching (linked from source); 0 asm bytes.** Groups: OS, GX, DVD, VI, PAD/WPAD/KPAD, AX/AI/DSP, NAND, SC, VF (SD card), HBM/HOMEBUTTONLIB, MTX and MSL. SDK-internal-only groups on the path: IPC, FS, EXI, SI, BTE, WUD, BASE, MEM, ARC, DB, AXFX, WENC and DSP (see §8).

Notes:
- `__OSInitAudioSystem`/DSP are reached inside `OSInit`/`AIInit`/`AXInit`, not from game code.
- The HOME button menu: `HBMCreate` runs at boot (`fn_802EC614`). `HBMInit`/`HBMCalc`/`HBMDraw` run in `fn_802EC884`/`fn_802ECA48`, which are only active when HOME is pressed.
- The SD MP3 player (VF + sdk_mp3) starts a thread at boot.
- `DVDOpen`/`DVDReadPrio`/`DVDReadAsyncPrio` are reached only through the FileDisk/FileCD vtables (§2.6).

### AI (direct from game: 1 fns; total AI fns reachable on path: 6)
- `AIInit` <- fn_802E5478
### AX (direct from game: 18 fns; total AX fns reachable on path: 58)
- `AXAcquireVoice` <- fn_802E5478
- `AXInit` <- fn_802E5478
- `AXRegisterCallback` <- fn_802E5478
- `AXRmtAdvancePtr` <- fn_802E52EC
- `AXRmtGetSamples` <- fn_802E52EC
- `AXSetMode` <- fn_802E5478, fn_802E6264
- `AXSetVoiceAddr` <- fn_802E6264
- `AXSetVoiceAdpcm` <- fn_802E6264
- `AXSetVoiceAdpcmLoop` <- fn_802E4F98
- `AXSetVoiceMix` <- fn_802E6264
- `AXSetVoiceRmtMix` <- fn_802E6264
- `AXSetVoiceRmtOn` <- fn_802E6264
- `AXSetVoiceSrc` <- fn_802E6264
- `AXSetVoiceSrcRatio` <- fn_802E6264
- `AXSetVoiceSrcType` <- fn_802E6264
- `AXSetVoiceState` <- fn_802E5478, fn_802E6264
- `AXSetVoiceType` <- fn_802E5478, fn_802E6264
- `AXSetVoiceVe` <- fn_802E6264
### DVD (direct from game: 8 fns; total DVD fns reachable on path: 50)
- `DVDCancelAll` <- fn_802E2E38
- `DVDClose` <- __dt__6FileCDFv
- `DVDGetCommandBlockStatus` <- IsDone__6FileCDFv, Read__6FileCDFPvl
- `DVDGetDriveStatus` <- Read__6FileCDFPvl, fn_802D992C
- `DVDInit` <- fn_802E3F94
- `DVDOpen` <- OpenAppend__8FileDiskFPCc, Open__8FileDiskFPCc
- `DVDReadAsyncPrio` <- ReadAsync__6FileCDFPvl, Read__6FileCDFPvl
- `DVDReadPrio` <- Read__6FileCDFPvl
### GX (direct from game: 96 fns; total GX fns reachable on path: 157)
- `GXAbortFrame` <- fn_802CFD74
- `GXAdjustForOverscan` <- fn_802CEAC4
- `GXBegin` <- fn_802D2218, fn_802D68C4
- `GXBeginDisplayList` <- fn_802D62FC
- `GXClearVtxDesc` <- fn_802D2218, fn_802D5DE8, fn_802D68C4, fn_802ECA48
- `GXCopyDisp` <- fn_802D0C98
- `GXDisableBreakPt` <- fn_802CFDE0, fn_802D3DAC
- `GXDrawDone` <- fn_802D2F78
- `GXEnableBreakPt` <- fn_802CFCA0
- `GXEndDisplayList` <- fn_802D62FC
- `GXFlush` <- fn_802CFCA0, fn_802CFDE0
- `GXGetCPUFifo` <- fn_802CFCA0, fn_802CFD74
- `GXGetFifoBase` <- fn_802CFD74
- `GXGetFifoPtrs` <- fn_802CFCA0
- `GXGetFifoSize` <- fn_802CFD74
- `GXGetGPStatus` <- fn_802CED78, fn_802CEF90
- `GXGetOverflowCount` <- fn_802CEF90, fn_802D992C
- `GXGetTexBufferSize` <- fn_802CF600, fn_802D7B5C, fn_802D7D98, fn_802D8A84, fn_802D8C00
- `GXGetTexObjHeight` <- fn_802D7564
- `GXGetTexObjWidth` <- fn_802D7564
- `GXGetTlutObjNumEntries` <- fn_802D9340
- `GXGetYScaleFactor` <- fn_802D0C98
- `GXInit` <- fn_802CF600, fn_802CFD74
- `GXInitLightAttn` <- fn_802D17BC
- `GXInitLightColor` <- fn_802D0EEC, fn_802D17BC
- `GXInitLightDistAttn` <- fn_802D0EEC
- `GXInitLightPos` <- fn_802D0EEC
- `GXInitSpecularDirHA` <- fn_802D17BC
- `GXInitTexObj` <- create_generic_texture, fn_802CF600, fn_802D7D98, fn_802D8A84, fn_802D8C00, fn_802DD4FC (+1)
- `GXInitTexObjCI` <- fn_802D7B5C
- `GXInitTexObjFilter` <- fn_802D7780, fn_802DD4FC
- `GXInitTexObjLOD` <- create_generic_texture, fn_802D7D98, fn_802D8A84, fn_802D8C00, fn_802E18D4
- `GXInitTexObjWrapMode` <- fn_802D7780, fn_802D92B8
- `GXInitTlutObj` <- fn_802D7B5C
- `GXInvalidateTexAll` <- create_generic_texture, fn_802D0BD8, fn_802D7B5C, fn_802D7D98, fn_802D8A84, fn_802D9340
- `GXInvalidateVtxCache` <- fn_802CFCA0, fn_802D0BD8
- `GXLoadLightObjImm` <- fn_802D0EEC, fn_802D18E8
- `GXLoadNrmMtxImm` <- fn_802D1C04
- `GXLoadPosMtxImm` <- fn_802D1C04, fn_802D2218
- `GXLoadTexMtxImm` <- fn_802D1B58, fn_802D1C04
- `GXLoadTexObj` <- fn_802D9160, fn_802D9230, fn_802D92B8, fn_802DEFA0, fn_802DF300, fn_802DF4A8 (+1)
- `GXLoadTlut` <- fn_802D9160
- `GXPeekZ` <- fn_802D3900
- `GXReadXfRasMetric` <- fn_802CED78
- `GXResetOverflowCount` <- fn_802D3DAC
- `GXSetAlphaCompare` <- fn_802CF5D4, fn_802D0EEC
- `GXSetAlphaUpdate` <- fn_802D0EEC, fn_802D2218
- `GXSetBlendMode` <- fn_802CF850, fn_802D0EEC, fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802ECA48
- `GXSetChanAmbColor` <- fn_802D0EEC
- `GXSetChanCtrl` <- fn_802D0EEC, fn_802D18E8
- `GXSetChanMatColor` <- fn_802D0EEC
- `GXSetColorUpdate` <- fn_802D0C98, fn_802D0EEC, fn_802D2218
- `GXSetCopyClear` <- fn_802CF850
- `GXSetCopyFilter` <- fn_802D0C98
- `GXSetCullMode` <- fn_802D08CC, fn_802D2218
- `GXSetCurrentMtx` <- fn_802D1C04, fn_802D3360, fn_802ECA48
- `GXSetDispCopyDst` <- fn_802D0C98
- `GXSetDispCopyGamma` <- fn_802CF850
- `GXSetDispCopySrc` <- fn_802D0C98
- `GXSetDispCopyYScale` <- fn_802D0C98
- `GXSetDither` <- fn_802CF850
- `GXSetDrawDone` <- fn_802CFCA0
- `GXSetDrawDoneCallback` <- fn_802CF600
- `GXSetDrawSync` <- fn_802CFCA0, fn_802CFDE0
- `GXSetDrawSyncCallback` <- fn_802CF600
- `GXSetDstAlpha` <- fn_802D0EEC, fn_802D2218
- `GXSetFog` <- fn_802D0EEC, fn_802D2218
- `GXSetGPMetric` <- fn_802CF048
- `GXSetIndTexMtx` <- fn_802D307C
- `GXSetIndTexOrder` <- fn_802D2684, fn_802DEFA0, fn_802DF4A8, fn_802DF6D0
- `GXSetNumChans` <- fn_802CF850, fn_802D0EEC, fn_802D18E8, fn_802ECA48
- `GXSetNumIndStages` <- fn_802D2640, fn_802D2684, fn_802DEFA0, fn_802DF4A8, fn_802DF6D0
- `GXSetNumTevStages` <- fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802DF21C, fn_802DF300, fn_802DF4A8 (+4)
- `GXSetNumTexGens` <- fn_802CF850, fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802DF21C, fn_802DF300 (+5)
- `GXSetPixelFmt` <- fn_802CF850
- `GXSetProjection` <- fn_802D14D8, fn_802D2218, fn_802ECA48
- `GXSetScissor` <- fn_802CF850, fn_802D1678, fn_802ECA48
- `GXSetTevAlphaIn` <- fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802DF21C, fn_802DF300, fn_802DF4A8 (+3)
- `GXSetTevAlphaOp` <- fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802DF21C, fn_802DF300, fn_802DF4A8 (+3)
- `GXSetTevColor` <- fn_802D007C, fn_802D0EEC, fn_802D2218
- `GXSetTevColorIn` <- fn_802CF850, fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802DF21C, fn_802DF300 (+4)
- `GXSetTevColorOp` <- fn_802CF850, fn_802D2218, fn_802D2684, fn_802DEFA0, fn_802DF21C, fn_802DF300 (+4)
- `GXSetTevDirect` <- fn_802D2640
- `GXSetTevIndirect` <- fn_802D2684, fn_802DEFA0, fn_802DF4A8, fn_802DF6D0
- `GXSetTevOp` <- fn_802CF850, fn_802D2218, fn_802D2684, fn_802D3360, fn_802ECA48
- `GXSetTevOrder` <- fn_802CF850, fn_802D2218, fn_802D2684, fn_802D3360, fn_802DEFA0, fn_802DF21C (+6)
- `GXSetTevSwapMode` <- fn_802DEFA0, fn_802DF21C, fn_802DF300, fn_802DF4A8, fn_802DF6D0, fn_802DF994 (+1)
- `GXSetTevSwapModeTable` <- fn_802DEFA0
- `GXSetTexCoordGen2` <- fn_802D2684, fn_802D3360, fn_802DEFA0, fn_802DF21C, fn_802DF300, fn_802DF4A8 (+3)
- `GXSetViewport` <- fn_802CF850, fn_802D0BD8, fn_802D1678, fn_802ECA48
- `GXSetViewportJitter` <- fn_802D0BD8, fn_802D1678
- `GXSetVtxAttrFmt` <- fn_802D2218, fn_802D68C4, fn_802ECA48
- `GXSetVtxDesc` <- fn_802D2218, fn_802D68C4, fn_802ECA48
- `GXSetVtxDescv` <- fn_802D5DE8
- `GXSetZCompLoc` <- fn_802CF850
- `GXSetZMode` <- fn_802D0C98, fn_802D0EEC, fn_802D2218, fn_802ECA48
### HOMEBUTTONLIB (direct from game: 10 fns; total HOMEBUTTONLIB fns reachable on path: 68)
- `HBMCalc` <- fn_802EC884
- `HBMCreate` <- fn_802EC614
- `HBMCreateSound__FPCcPvUl` <- fn_802EC884
- `HBMDeleteSound` <- fn_802EC884
- `HBMDraw` <- fn_802ECA48
- `HBMGetSelectBtnNum` <- fn_802EC884
- `HBMInit` <- fn_802EC884
- `HBMIsReassignedControllers` <- fn_802EC884
- `HBMSetAdjustFlag__Fb` <- fn_802EC884
- `HBMUpdateSound` <- fn_802EC884
### KPAD (direct from game: 4 fns; total KPAD fns reachable on path: 23)
- `KPADDisableDPD` <- fn_802E7FD0
- `KPADEnableDPD` <- fn_802E7FD0
- `KPADInit` <- fn_802E7C64
- `KPADRead` <- fn_802E7FD0
### MSL (direct from game: 29 fns; total MSL fns reachable on path: 89)
- `abs` <- fn_802123B8
- `atan` <- fn_802C974C, fn_802D14D8
- `atan2` <- fn_802E7DDC
- `atof` <- fn_80133960, fn_80234EF4
- `atoi` <- fn_80133844, fn_801338D8
- `cos` <- Mat3RotX__FP4Mat3f, Mat3RotY__FP4Mat3f, Mat3RotZ__FP4Mat3f, fn_802C974C
- `fmod` <- fn_80129E34, fn_80230DA4
- `free` <- fn_80120084
- `malloc` <- fn_8011FFC4
- `memcmp` <- fn_802D2B0C
- `memmove` <- Add__12SoundManagerFP9GameSound
- `qsort` <- LoadTracks__Fv, SortEves__Fv, fn_80265DCC
- `sin` <- Mat3RotX__FP4Mat3f, Mat3RotY__FP4Mat3f, Mat3RotZ__FP4Mat3f, fn_802C974C
- `sprintf` <- Alloc__Q217@unnamed@mem_cpp@9StackHeapFlPCci, ParseTrack__FP5TrackiP9ValueNodePCc, fn_8012B2C4, fn_8013323C, fn_80133348, fn_80137A18 (+11)
- `sqrt` <- fn_80276098
- `sscanf` <- ParseColor__FPCc, fn_80133BA0, fn_8022CD0C, fn_8022CDE4
- `strcasecmp` <- AchFindCategory__FPCc, AchLoad__Fv, FindHighScoreList__FiPCcli, FindTrack__FPCc, LoadChassisUnlocks__Fv, LoadDriverList__FP10ValueTable (+50)
- `strcat` <- fn_80232578
- `strchr` <- fn_80130568, fn_80133BA0, fn_8022ACFC, fn_802CA7F4, fn_802CA824
- `strcmp` <- FindEventType__FPCc, FindGrade__FPCc, LoadUnlockables__Fv, fn_802141A0, fn_8022E4C0, fn_8027B2F8 (+1)
- `strcpy` <- Alloc__Q217@unnamed@mem_cpp@9StackHeapFlPCci, OpenAppend__8FileDiskFPCc, Open__8FileDiskFPCc, __ct__8NameListFPPCcii, fn_800122B4, fn_80133460 (+11)
- `strlwr` <- LoadSize__8TocSizerFPCc, fn_8012ACA0, fn_8012D908
- `strncasecmp` <- fn_80133E10, fn_8023293C, fn_8023400C, fn_802346C8, fn_80236A5C
- `strncmp` <- FindOption__FPCcPCc, OpenAppend__8FileDiskFPCc, Open__8FileDiskFPCc, ParseCommandLine__FPCc, fn_80208E90, fn_802E321C (+2)
- `strncpy` <- LoadTimerBegin__FPCciif, LoadTimerEnd__FPCc, LoadTimerMark__FPCcf, ParseTrack__FP5TrackiP9ValueNodePCc, Set__7UITableFiiPCcc, __ct__9GameSoundFPCci (+13)
- `strrchr` <- LoadSize__8TocSizerFPCc, LoadVehicle__FP9ValueNode, fn_8012ACA0, fn_8012B3D4, fn_8012D908, fn_8021203C (+6)
- `strstr` <- IsEveAllowed__FP9ValueNode, __ct__6WindowFPCclllll, fn_8012BA1C, fn_802123B8, fn_802141A0, fn_8022A7D4 (+2)
- `tan` <- fn_802C974C, fn_802D0368, fn_802D14D8
- `vsprintf` <- fn_801345D8
### MTX (direct from game: 10 fns; total MTX fns reachable on path: 11)
- `C_MTXOrtho` <- fn_802D2218, fn_802ECA48
- `C_MTXPerspective` <- fn_802D14D8
- `PSMTXConcat` <- fn_802D1C04
- `PSMTXInvXpose` <- fn_802D1C04
- `PSMTXScale` <- fn_802D1C04
- `PSMTXTrans` <- fn_802D1C04
- `PSVECCrossProduct` <- fn_802C8588, fn_802D03F4
- `PSVECMag` <- fn_802D03F4, fn_802D17BC, fn_802D18E8
- `PSVECNormalize` <- fn_802C8588
- `PSVECSquareDistance` <- fn_80276098
### NAND (direct from game: 11 fns; total NAND fns reachable on path: 32)
- `NANDCheck` <- fn_802E8920
- `NANDClose` <- fn_802E8AE8, fn_802E8D8C, fn_802E9254, test_banner_ok
- `NANDCloseAsync` <- fn_802E9254
- `NANDCreateAsync` <- fn_802E9254
- `NANDDelete` <- fn_802E8B34
- `NANDGetLength` <- test_banner_ok
- `NANDInit` <- fn_802E8CA8
- `NANDOpen` <- fn_802E8AE8, fn_802E8D8C, test_banner_ok
- `NANDOpenAsync` <- fn_802E9254
- `NANDRead` <- fn_802E8D8C
- `NANDWriteAsync` <- fn_802E9254
### OS (direct from game: 41 fns; total OS fns reachable on path: 184)
- `DCFlushRange` <- create_generic_texture, fn_802CF10C, fn_802CFDE0, fn_802D2FD8, fn_802D7B5C, fn_802D7D98 (+7)
- `DCInvalidateRange` <- fn_802CF10C, fn_802D6810, fn_802E5478
- `DCStoreRangeNoSync` <- fn_802C6B44
- `LCEnable` <- fn_802E2A5C
- `LCLoadData` <- fn_802D6C78
- `LCQueueWait` <- fn_802D6C78
- `LCStoreData` <- fn_802D6C78
- `OSCancelAlarm` <- fn_8011C364
- `OSCreateAlarm` <- fn_802E5478
- `OSCreateThread` <- fn_802EBE2C
- `OSDisableInterrupts` <- fn_8011BEC8, fn_8011BF4C, fn_8011C06C, fn_8011C120, fn_8011C1E0, fn_8011C2EC (+4)
- `OSEnableInterrupts` <- fn_802D9A50, fn_802E4F98, fn_802E6058
- `OSGetConsoleType` <- fn_802E769C
- `OSGetFontEncode` <- fn_802DACD4
- `OSGetFontTexture` <- fn_802DACD4, fn_802DAE64
- `OSGetFontWidth` <- fn_802DAE64
- `OSGetMEM1ArenaHi` <- fn_802E3DF8
- `OSGetMEM1ArenaLo` <- fn_802E3DF8
- `OSGetMEM2ArenaHi` <- fn_802E3DF8
- `OSGetMEM2ArenaLo` <- fn_802E3DF8
- `OSGetResetButtonState` <- fn_802D992C
- `OSGetTime` <- fn_80116D30, fn_8011C120, fn_802E2C20, fn_802E2C9C, fn_802E2CFC, fn_802E5478
- `OSInit` <- fn_802E2A5C
- `OSInitFont` <- fn_802DACD4
- `OSInitMutex` <- fn_802EBE2C
- `OSLockMutex` <- fn_802EAB28
- `OSPanic` <- fn_802CEF90
- `OSReport` <- Print__8FileDiskFPCc, Read__6FileCDFPvl, fn_802CED78, fn_802CEF90, fn_802CF10C, fn_802CFDE0 (+5)
- `OSRestart` <- fn_802E2E38
- `OSRestoreInterrupts` <- fn_8011BEC8, fn_8011BF4C, fn_8011C06C, fn_8011C120, fn_8011C1E0, fn_8011C2EC (+3)
- `OSResumeThread` <- fn_802EBE2C
- `OSReturnToMenu` <- fn_802E2E38
- `OSSetPeriodicAlarm` <- fn_8011C120, fn_802E5478
- `OSSetPowerCallback` <- fn_802D9C18
- `OSShutdownSystem` <- fn_802E2E38
- `OSSleepTicks` <- fn_802EA834, fn_802EAB28
- `OSTicksToCalendarTime` <- fn_802E2CFC
- `OSTryLockMutex` <- fn_802E9EB8
- `OSUnlockMutex` <- fn_802E9EB8, fn_802EAB28
- `OSYieldThread` <- fn_8011C0E0, fn_8011C274
- `exit` <- fn_802E317C
### PAD (direct from game: 1 fns; total PAD fns reachable on path: 3)
- `PADInit` <- fn_802E7C64
### Runtime.PPCEABI.H: compiler helpers (_savegpr/_restgpr, __cvt_*, __div2i), plus memcpy, memset and strlen, called from most game functions (omitted)
### SC (direct from game: 8 fns; total SC fns reachable on path: 29)
- `SCCheckStatus` <- fn_802E98D0
- `SCGetAspectRatio` <- fn_802E98D0
- `SCGetLanguage` <- fn_802EC614
- `SCGetProgressiveMode` <- fn_802E98D0
- `SCGetSoundMode` <- fn_802E98D0
- `SCGetWpadMotorMode` <- fn_802E99AC
- `SCGetWpadSpeakerVolume` <- fn_802E99AC
- `SCInit` <- fn_802E98D0
### VF (direct from game: 8 fns; total VF fns reachable on path: 261)
- `VFAttachDrive` <- fn_802E9CF0
- `VFCloseFile` <- fn_802E9A6C
- `VFFindFirst` <- fn_802EA5D4
- `VFFindNext` <- fn_802EA5D4
- `VFGetFileInfo` <- fn_802E9A6C
- `VFInit` <- fn_802E9CF0
- `VFOpenFile` <- fn_802E9A6C
- `VFSetMediaCallback` <- fn_802E9CF0
### VI (direct from game: 11 fns; total VI fns reachable on path: 23)
- `VIConfigure` <- fn_802CF600, fn_802CFD74
- `VIFlush` <- Read__6FileCDFPvl, fn_802CF600, fn_802CF850, fn_802D0C98, fn_802D2F78, fn_802D2FD8
- `VIGetDTVStatus` <- fn_802CEAC4
- `VIGetNextField` <- fn_802D0BD8, fn_802D1678
- `VIGetTvFormat` <- fn_802CEAC4
- `VIInit` <- fn_802CF600, fn_802E2A5C
- `VISetBlack` <- fn_802CF600, fn_802D0C98, fn_802D2FD8
- `VISetNextFrameBuffer` <- fn_802CF850, fn_802D0C98, fn_802D2FD8
- `VISetPostRetraceCallback` <- fn_802D9C18
- `VISetPreRetraceCallback` <- fn_802CF600
- `VIWaitForRetrace` <- Read__6FileCDFPvl, fn_802CF850, fn_802D2FD8
### WENC (direct from game: 1 fns; total WENC fns reachable on path: 1)
- `WENCGetEncodeData` <- fn_802E52EC
### WPAD (direct from game: 10 fns; total WPAD fns reachable on path: 33)
- `WPADCanSendStreamData` <- fn_802E52EC
- `WPADControlMotor` <- fn_802E7920, fn_802EC884
- `WPADGetInfoAsync` <- fn_802E7FD0
- `WPADGetRadioSensitivity` <- fn_802E7FD0
- `WPADGetStatus` <- fn_802E2E38, fn_802E7C64
- `WPADProbe` <- fn_802E7FD0
- `WPADRegisterAllocator` <- fn_802E7C64
- `WPADSendStreamData` <- fn_802E52EC
- `WPADSetConnectCallback` <- fn_802E7C64
- `__WPADShutdown` <- fn_802E2E38
### sdk_mp3 (direct from game: 4 fns; total sdk_mp3 fns reachable on path: 50)
- `MP3_Delete` <- fn_802EAB28
- `TMCCMP3Decode` <- fn_802EAB28
- `TMCCMP3GetInfo` <- fn_802EAB28
- `TMCCMP3Init` <- fn_802EAB28


## 4. Static initialisers before main (`.ctors`, run by `__init_cpp` from `__init_user`)

The `.ctors` table at 0x802ED060 (`_ctors`) has **372 entries** plus a 0 terminator (dump of main.elf `.ctors`).

| Owner | Count | Of which Matching | NonMatching (asm) | Code bytes | Asm bytes |
|---|---|---|---|---|---|
| Game units | 353 | 197 | 156 | 25,548 | 12,568 |
| SDK/Runtime (incl. `__init_cpp_exceptions` first, HBM nw4hbm snd/ut statics) | 19 | 19 | 0 | 648 | 0 |

- 353 of the 372 call `RegisterModuleTag__FP9ModuleTag` (0x8000D838, version.cpp). Every game unit
  registers its `ModuleTag`. 145 constructors are exactly 8 bytes: `addi r3,r13,X; b RegisterModuleTag`.
- Other work done by ctors: `__register_global_object` (45, static objects with dtors), `__construct_array` (25),
  `LocString::LocString` (6), and nw4hbm `BasicSound`/`StrmPlayer`/`LinkListImpl` statics (HBM).
- Three game ctors are not named `__sinit_*`: `fn_80181854`, `fn_8018FACC` (cardmg.cpp, NM) and `fn_8022A594`.
- The static-call closure of all 372 constructors is 577 functions: 211 game Matching, 157 game NonMatching,
  209 SDK.
- The NonMatching ctors are listed below. Examples: `__sinit_\overlay_cpp` 87.5%,
  `__sinit_\racing_cpp` **17.2%**, `__sinit_\crushmaster_cpp` 65.7%, `__sinit_\ball_cpp` 67.9%,
  `__sinit_\trackobj_cpp` 67.3%, `__sinit_\inputbind_cpp` 95.4%, and several ~82-83% (`gameloop`, `raceload`,
  `escape`, `aistub`, `modeinl`, `terchunk`).
- For a port, ctor order matters only for the ModuleTag list (version.cpp) and for the `__register_global_object`
  dtor chain.


NonMatching (asm) constructors (156): __sinit_overlay_cpp 80007D54 0x40 (87.5%); __sinit_replay_cpp 80009CEC 0x84; __sinit_raceload_cpp 8000AC40 0x58 (83.1%); __sinit_gameloop_cpp 8000C774 0x58 (83.1%); __sinit_escape_cpp 8000D780 0x58 (82.2%); __sinit_dashhelper_cpp 80012314 0x70; __sinit_racing_cpp 8001629C 0xB8 (17.2%); __sinit_trackmap_cpp 8001B92C 0xA8; __sinit_stdoverlay_cpp 8001EEE8 0x50; __sinit_loading_cpp 800218FC 0x4C; __sinit_res_cpp 8012F148 0x8; __sinit_aidriver_cpp 80135270 0x8; __sinit_idealline_cpp 801368C4 0x34; __sinit_proxer_cpp 8013790C 0x44; __sinit_idealres_cpp 80138BB0 0x34; __sinit_arenabnd_cpp 8013A1E0 0x8; __sinit_aistub_cpp 8013A20C 0x58 (82.2%); __sinit_zone_cpp 8014449C 0x68 (82.7%); __sinit_modeinl_cpp 80144DE0 0x58 (82.2%); __sinit_ovlayout_cpp 801490E0 0xD8; __sinit_betarace_cpp 80156128 0x60; __sinit_stunts_cpp 8015EB98 0x60; __sinit_aiobst_cpp 801606BC 0x60; __sinit_tutorial_cpp 80165910 0x60; __sinit_pickup_cpp 8016A040 0x6C; __sinit_ringchal_cpp 80171648 0x50; __sinit_techdemo_cpp 80171CE4 0x2C; __sinit_crushmaster_cpp 80176404 0x5C (65.7%); __sinit_phob_cpp 80179CBC 0x4C; __sinit_collvol_cpp 8017B0A8 0x8; __sinit_rigid_cpp 8017D370 0x8; __sinit_ball_cpp 8017EB0C 0x5C (67.9%); __sinit_carbody_cpp 8018BF6C 0x34; fn_8018FACC 8018FACC 0x4C; __sinit_playcar_cpp 8019306C 0x30; __sinit_aicar_cpp 80198EAC 0x68; __sinit_sphcoll_cpp 801A51DC 0x30; __sinit_springcar_cpp 801A6344 0x2C; __sinit_netcar_cpp 801A75AC 0x8; __sinit_inputbind_cpp 801A937C 0x2E4 (95.4%); __sinit_obstacles_cpp 801ADAC8 0x2C; __sinit_recordmgr_cpp 801AFAC0 0x8; __sinit_deity_cpp 801B329C 0x2C; __sinit_collisionsound_cpp 801B45D8 0x8; __sinit_placecar_cpp 801B81D8 0x8; __sinit_trailer_cpp 801BA05C 0x2C; __sinit_unk_801BD620_cpp 801BD620 0x8; __sinit_masslist_cpp 801BF1C4 0x4C; __sinit_interlude_cpp 801CB2A4 0x34; __sinit_awards_cpp 801CEC78 0x8; __sinit_pmesh_cpp 801D0284 0x2C; __sinit_soevent_cpp 801D3264 0x64; __sinit_actseq_cpp 801D8FF8 0x98; __sinit_path_cpp 801DA6E4 0x8; __sinit_heightfld_cpp 801DDE70 0x8; __sinit_crashmode_cpp 801E06E4 0x2C; __sinit_boostman_cpp 801E186C 0x8; __sinit_tristrip_cpp 801E5924 0x8; __sinit_boneset_cpp 801E68CC 0x8; __sinit_world_cpp 801E9C18 0x68; __sinit_graf_cpp 801EC1A0 0x34; __sinit_carmodel_cpp 801EEF7C 0x4C; __sinit_trackmod_cpp 801F4CA4 0x34; __sinit_fabric_cpp 801F7A04 0x8; __sinit_trackobj_cpp 801FC85C 0x4C (67.3%); __sinit_drone_cpp 801FFADC 0x2C; __sinit_drivmod_cpp 802021DC 0x3C; __sinit_terchunk_cpp 80202B8C 0x58 (82.2%); __sinit_cenvmap_cpp 80203F10 0x78; __sinit_terrpick_cpp 802042FC 0x34; __sinit_sound_cpp 80207958 0x8; __sinit_soundmgr_cpp 80208A10 0x8; __sinit_musicplr_cpp 8020A6A4 0x8; __sinit_menumus_cpp 8020CA28 0x8; __sinit_roadsnd_cpp 8020CF60 0x8; __sinit_unk_8020CF68_cpp 8020DE24 0x50; __sinit_unk_8020DE74_cpp 8020F3AC 0x8; __sinit_unk_8020F3B4_c 802114F4 0xB8; __sinit_uiapi_cpp 80215CF4 0x174; __sinit_widgetwin_cpp 80216CBC 0x58 (82.2%); fn_8022A594 8022A594 0x50; __sinit_gtext_cpp 80234BD0 0xC4; __sinit_swfanim_cpp 8023990C 0x50; __sinit_uioptions_cpp 8023B590 0x50; __sinit_opsound_cpp 8023D470 0x48; __sinit_mcdialog_cpp 8023FA00 0x50; __sinit_nameent_cpp 8023FDC8 0x58 (84.5%); __sinit_mroll_cpp 80243410 0x50; __sinit_keyboard_cpp 802445A4 0x58 (84.5%); __sinit_unlkmenu_cpp 80245030 0x50; __sinit_mcstatus_cpp 802468F0 0x48; __sinit_popwarn_cpp 80249468 0x90; __sinit_multievt_cpp 8024BD8C 0x50; __sinit_modeltest_cpp 8024C068 0x8; __sinit_profile_cpp 8024DA88 0x50; __sinit_garage_cpp 802524D0 0x210; __sinit_minworld_cpp 80253210 0x44; __sinit_caruimod_cpp 80257214 0x70 (83.3%); __sinit_tunemenu_cpp 80257F70 0x48; __sinit_achview_cpp 8025BFD0 0x58; __sinit_recscreen_cpp 8025CE28 0x50; __sinit_frname_cpp 8025DFE4 0x50; __sinit_carpick_cpp 8025FE3C 0x50; __sinit_chalpath_cpp 80262EF8 0x94; __sinit_contmenu_cpp 80263EE0 0x50; __sinit_cammgr_cpp 8026A584 0x7C (74.2%); __sinit_racerslot_cpp 8026B0D4 0x44; __sinit_unk_8026B844_cpp 8026E224 0x8; __sinit_camfov_cpp 80270A84 0x64; __sinit_physcam_cpp 802739B8 0xD0; __sinit_camstate_cpp 80276060 0x8; __sinit_namedcam_cpp 80278168 0xA4; __sinit_stub18_cpp 802789B4 0x68 (87.0%); __sinit_evpath_cpp 802874C8 0xF0; __sinit_efxman_cpp 80288894 0x44 (52.9%); __sinit_sparks_cpp 8028CED0 0x8; __sinit_skid_cpp 8028DF24 0x34; __sinit_partman_cpp 80291728 0x5C; __sinit_chunks_cpp 80294918 0x54; __sinit_carlights_cpp 80296014 0x44; __sinit_flipbook_cpp 802971A8 0x8; __sinit_fire_cpp 8029C1BC 0x8; __sinit_waterpool_cpp 8029EF68 0x8; __sinit_partemit_cpp 802A086C 0x7C; __sinit_sfline_cpp 802A1588 0x8; __sinit_trail_cpp 802A4A48 0x8; __sinit_precip_cpp 802A7B74 0x64; __sinit_birds_cpp 802A8F34 0x60; __sinit_zonelite_cpp 802A95B4 0xCC; __sinit_stuntpart_cpp 802AC0BC 0x2C; __sinit_artanim_cpp 802AE2B4 0x2C; __sinit_groundvis_cpp 802B05F4 0x34; __sinit_gmat_cpp 802B1C88 0x58; __sinit_objplace_cpp 802B3F14 0x58; __sinit_skagbag_cpp 802B4FA8 0x8; __sinit_tedash_cpp 802B8CA4 0xFC; __sinit_xlobj_cpp 802BB734 0x98; __sinit_lweight_cpp 802BC320 0x8; __sinit_tree_cpp 802BE2B4 0x58; __sinit_faninfo_cpp 802BECDC 0x8; __sinit_water_cpp 802C0D44 0x2C; __sinit_treehit_cpp 802C1BB4 0x74; __sinit_swlmap_cpp 802C28D8 0x3C (86.7%); __sinit_gxcanvas_cpp 802C5028 0x108 (3.3%); __sinit_model_cpp 802C8238 0x88; __sinit_texture_cpp 802CCB84 0x3C; __sinit_mlist_cpp 802CE308 0x3C; __sinit_gx_cpp 802D3ED4 0x5C; __sinit_drsurf_cpp 802D755C 0x8; __sinit_drtex_cpp 802D98BC 0x3C; __sinit_bonemgr_cpp 802DAC70 0x64; __sinit_glare_cpp 802DEA90 0x34; __sinit_tevbink_cpp 802E1720 0x8; __sinit_filecd_cpp 802E46A4 0x8; __sinit_unk_802E4D84_cpp 802E6B48 0x8; __sinit_sdmp3_cpp 802EC464 0x58


## 5. Open questions / not resolved

1. **Intro definition.**
   - Retail boot shows no Monster Games logo screen from code: the logo layouts are kiosk/E3 only and
     `logos.pss` playback is a stubbed function.
   - If the real hardware boot does show logos, they must come from data. Candidates: a `kiosk.lyt` widget,
     or an `.lyt` loaded by a widget virtual I did not enumerate. Another possibility is that this DOL's
     `fn_80278A1C` is not the function a later revision uses.
   - Should be confirmed by running the retail disc in Dolphin with a breakpoint on 0x802D0C98 / 0x802146BC.
2. **First visible frame on a fresh console** (no save) is probably a memory-card dialog. The order is
   `MemCardSystemLoad` → `fn_802E6FBC` → rmemcard → the registered mcdialog callbacks
   (`fn_8023E7A8`..., NM 83-100%) → `fn_802146BC`. This was not traced through the rmemcard state machine.
   Those 8 dialog functions (≈4.6 KB, NM) belong to the boot path for that case. They are in the NM unit
   `mcdialog.cpp` and are not counted in §0.4.
3. **807 unresolved indirect call sites** (§2.6). The biggest gaps are the `.val` value-type dispatchers
   (eve/locale/uistyle/uianime) and widget virtuals. The kiosk.lyt widget set is built from data at run
   time by `fn_802123B8`, so the exact set of `Draw` methods on the first frame needs a runtime trace.
4. **RAD Bink** (`unk_80116BD4.c`, NM, 39 functions, 10.6 KB on path) is statically reachable, via
   `tevbink.cpp`/`gxcanvas.cpp` and the `fn_8011C120` alarm. Whether any Bink code runs before the title
   frame is unclear: no movie plays. Reference-only material; see the n100f note in project memory.
5. The **res.cpp async pipeline** (`AsyncSetQueue::Process`/`Update`) runs from `fn_8012E81C`'s wait loop, and
   possibly also from a bg task. I did not find which bg task, if any, pumps it; the pure `bl` graph links it
   via `Process__13AsyncSetQueueFv`.
6. The pure `bl` closure follows rare branches (`OSFatal`, HBM `VPanic`, `fn_802E2E38` reset/power-off →
   `OSReturnToMenu`/`OSShutdownSystem`, `DVDCancelAll`). The function counts are therefore an upper bound for
   the direct-call part and a lower bound overall.
7. Unit names, and many function names, are the project's guesses (see each unit's header comment). Status
   reflects configure.py and report.json at 2026-11-15 06:26. Other sessions are actively matching, so the
   numbers drift.
8. I took no runtime measurements. "Execution order" comes from source order in kernel.cpp/app.cpp/mmain.cpp/
   popwarn.cpp/uiapi.cpp/widgetwin.cpp and the disassembly.


## 6. Game units on the path

Sorted: NonMatching first (by bytes), then Matching. "Unit report %" = report.json unit fuzzy %. "What it is" = the unit's header comment.

| Unit | Status | Unit report % | Fns on path | Bytes on path | Asm bytes on path | What it is |
|---|---|---|---|---|---|---|
| game/gx.cpp | NonMatching | 99.43 | 82 | 17612 | 17612 | GX driver core (0x802CE9FC-0x802D3F34, "gx.cpp"): video and GX init/shutdown, the render mode choice, |
| game/res.cpp | NonMatching | 100.0 | 81 | 16460 | 16460 | Resource sets (0x8012A388-0x8012F150, "res:" in its messages): .res archives of named, typed, |
| game/widgets.cpp | NonMatching | 99.99 | 72 | 16168 | 16168 | Anonymous widget-class fragment; no recovered TU or data ownership claim. |
| game/uiapi.cpp | NonMatching | 99.84 | 21 | 12372 | 12372 | UI API layer (0x8021165C-0x802160AC): UIShowGroup/UIHideGroup/UIMoveGroup/UIGetMenuIndex... on the |
| game/unk_80116BD4.c | NonMatching | 98.15 | 39 | 10576 | 10576 | Code 0x80116BD4-0x801274C0, imported verbatim from the upstream project (github.com/orangedude27/excite-truck, |
| game/texture.cpp | NonMatching | 99.53 | 45 | 9144 | 9144 | Textures (0x802CA0E4-0x802CCBC0, texture.cpp): the 512-slot texture table (reference-counted "TEX " |
| game/gtext.cpp | NonMatching | 98.61 | 26 | 7992 | 7992 | gText (0x80231504-0x80234D44): bitmap fonts ("GTEX" files, byte-swapped when built big-endian), a |
| game/drtex.cpp | NonMatching | 95.53 | 25 | 7760 | 7760 | GX texture driver (0x802D7564-0x802D98F8, "drgc/tex"): the 450-entry GX texture table |
| game/sdmp3.cpp | NonMatching | 96.57 | 14 | 7724 | 7724 | SD card MP3 player (0x802E9A3C-0x802EC4BC): scans the SD card for .mp3 files (EXCITE TRUCK folder |
| game/model.cpp | NonMatching | 97.27 | 30 | 5600 | 5600 | Models (0x802C5390-0x802C82C0, model.cpp): loading and byte-swapping "MODL" ModelInfo resources |
| game/drsurf.cpp | NonMatching | 94.85 | 12 | 4872 | 4872 | Surface drawing (0x802D3F34-0x802D7564, "drgc/surf"): draws NDL2 surfaces through their display lists |
| game/unk_802E4D84.cpp | NonMatching | 99.55 | 17 | 4692 | 4692 | Code 0x802E4D84-0x802E6B48, imported verbatim from the upstream project (github.com/orangedude27/excite-truck, |
| game/tevbink.cpp | NonMatching | 99.89 | 15 | 3844 | 3844 | TEV presets and Bink movies (0x802DEAC4-0x802E1728): the fixed texture-environment setups (and their |
| game/loading.cpp | NonMatching | 99.73 | 9 | 3052 | 3052 | Loading screen (0x8001FD7C-0x8002194C): the tips camera flythrough from camera3.val, the track |
| game/widgetwin.cpp | NonMatching | 95.43 | 13 | 3016 | 3016 | Widget windows (0x802160AC-0x80216D14, "WidgetWindow"): the table of up to 16 open menu windows, |
| game/gxcanvas.cpp | NonMatching | 96.51 | 20 | 2096 | 2096 | gx canvases (0x802C2CAC-0x802C5130): software pixel buffers in 16-bit (1 = ARGB4444, 3 = RGB555, |
| game/filecd.cpp | NonMatching | 100.0 | 23 | 2036 | 2036 | DVD file system (0x802E3EB0-0x802E46AC, filecd): the disc as a read-only file system and its files, |
| game/inputbind.cpp | NonMatching | 96.47 | 8 | 1844 | 1844 | Input bindings (0x801A75B4-0x801A9660): named controls ("Joystick Up", "Key_SPACE", ...) for the |
| game/swfanim.cpp | NonMatching | 98.91 | 4 | 1800 | 1800 | SW2 vector animations (0x80238BF4-0x8023995C): a "SWF2" resource of textured quads/triangles |
| game/namedcam.cpp | NonMatching | 97.35 | 3 | 1512 | 1512 | Named cameras (0x80276068-0x80278214): the named camera sets ("namedcam", "rddefault", "raceday", |
| game/soundmgr.cpp | NonMatching | 99.65 | 11 | 1404 | 1404 | Sound manager (0x80207960-0x80208A18, soundmgr.cpp): every live sound sorted by sound class, the |
| game/racing.cpp | NonMatching | 97.9 | 5 | 1296 | 1296 | Racing HUD (0x80013BD0-0x80016354): the per-player RacingOverlay drawn during a race - lap counter, |
| game/dashhelper.cpp | NonMatching | 99.81 | 2 | 1040 | 1040 | Dash helper (0x8000DFA8-0x800123EC, "dashhelper" in its warning): the race HUD toolkit - the left, |
| game/musicplr.cpp | NonMatching | 99.06 | 4 | 1032 | 1032 | Menu music (0x80209FFC-0x8020A6AC): the two streamed menu songs (mus_ui and mus_credits), the |
| game/sound.cpp | NonMatching | 99.86 | 6 | 880 | 880 | Sounds (0x802062E8-0x80207960): starting and stopping the sound system, the game sound classes |
| game/popwarn.cpp | NonMatching | 99.93 | 4 | 712 | 712 | Front-end popups (0x80246938-0x8024958C): the wireless and caution warnings ("pop_warn.lyt", |
| game/tristrip.cpp | NonMatching | 88.1 | 4 | 472 | 472 | Triangle strip batcher (0x801E4E44-0x801E592C): a 60-vertex / 174-index buffer flushed to the GPU |
| game/bonemgr.cpp | NonMatching | 99.08 | 2 | 384 | 384 | Bone manager (0x802D9C5C-0x802DACD4): bone pose commands built in a 100K stack allocator ("StackAlloc", |
| game/ovlayout.cpp | NonMatching | 99.84 | 3 | 320 | 320 | Overlay layouts (0x80144F40-0x80149588): the .val-driven 2D layout sets used by every HUD/overlay - |
| game/glare.cpp | NonMatching | 96.87 | 1 | 248 | 248 | Screen glare (0x802DD4FC-0x802DEAC4): copies the frame into a half-size "blur_tex", then redraws it |
| game/cammgr.cpp | NonMatching | 98.27 | 1 | 124 | 124 | Camera manager (0x80265DD8-0x8026A600): the per-player camera records, camera type dispatch |
| game/unk_8020F3B4.c | NonMatching | 97.29 | 1 | 96 | 96 | Code 0x8020F3B4-0x802114F4, imported verbatim from the upstream project (github.com/orangedude27/excite-truck, |
| game/stub18.cpp | NonMatching | 88.34 | 2 | 8 | 8 | A stripped unit (0x802789A8-0x80278A1C): 3 empty functions and its statics. Names are guesses. |
| game/zone.cpp | NonMatching | 99.44 | 1 | 4 | 4 | Gate challenge ("zone") mode (0x8013CB10-0x801444C4): ZoneMaster drives the timed ring/gate |
| game/recordmgr.cpp | NonMatching | 89.81 | 1 | 4 | 4 | Race results (0x801AE600-0x801AFAC8): the per-car result table ("RaceResultTable": finish time, place, |
| game/roadsnd.cpp | NonMatching | 99.72 | 1 | 4 | 4 | Road sounds (0x8020CA30-0x8020CF68): per-surface rolling sounds from roadsnd.val, blended by |
| game/eventpath.cpp | Matching | 100.0 | 37 | 6920 | 0 | Event paths (0x8027AD4C-0x8027D304): the career and challenge progressions. Each save slot holds three |
| game/uistyle.cpp | Matching | 100.0 | 18 | 6500 | 0 | Unified research draft. Four bodies remain provisional; no Matching promotion. |
| game/window.cpp | Matching | 100.0 | 28 | 5632 | 0 | Menu window (0x80216D14-0x80218E98): a window holds up to 256 widgets, its background (".img" or |
| game/locale.cpp | Matching | 100.0 | 19 | 5532 | 0 | Localization (0x8012FAB8-0x8013144C): language string tables (lang.lng inside usenglish.res / |
| game/uianime.cpp | Matching | 100.0 | 13 | 4892 | 0 | UI animation sets (0x80236728-0x8023875C, uianime.cpp): a stack of animation states for menu screens. |
| game/eve.cpp | Matching | 100.0 | 15 | 4672 | 0 | Event ("eve") registry (0x800126CC-0x80013B90): loads eves.val / demo_eves.val for the current game |
| game/rmemcard.cpp | Matching | 100.0 | 36 | 4364 | 0 | Save data on NAND ("rmemcard", 0x802E86D4-0x802E98C8): reads the save file, checks the banner, asks the |
| game/options.cpp | Matching | 100.0 | 21 | 3564 | 0 | Options (0x80133010-0x8013420C): named typed settings grouped in sections ([GLOBAL], [GX], ..., |
| game/kernel.cpp | Matching | 100.0 | 22 | 3404 | 0 | Kernel (0x802E1728-0x802E3028): the program entry, system init/shutdown order, stack overrun guard, |
| game/driver.cpp | Matching | 100.0 | 9 | 2960 | 0 | Drivers and vehicles (0x80016354-0x8001786C): the DriverInfoList / VehicleInfoList built from |
| game/gart.cpp | Matching | 100.0 | 13 | 2748 | 0 | gArt (0x80234D44-0x80236550): reference-counted 2D art sets. An art set is a texture plus a ".tab" |
| game/rjoy.cpp | Matching | 100.0 | 14 | 2708 | 0 | Remote input ("rjoy", 0x802E77B8-0x802E86D4): per-socket Wii Remote state on top of KPAD/WPAD. It polls |
| game/stats.cpp | Matching | 100.0 | 19 | 2336 | 0 | Stats and records (0x8001BA68-0x8001D7D8): per-identity statistics (best jump, drift, air spin...) |
| game/mrender.cpp | Matching | 100.0 | 24 | 2280 | 0 | Render state ("mr", 0x802C82C0-0x802CA0A4): the display mode (normal, widescreen, letterbox), the |
| game/autolyt.cpp | Matching | 100.0 | 4 | 2220 | 0 | Layout files (0x8022E0EC-0x8022E9E0): loads .lyt layouts (byte-swapping them when needed) and |
| game/mem.cpp | Matching | 100.0 | 22 | 2208 | 0 | Memory (0x801281A0-0x801292CC): MemAlloc/MemFree and the global operator new/delete on top of two |
| game/file.cpp | Matching | 100.0 | 23 | 2088 | 0 | Files (0x802E321C-0x802E3DF8, file.cpp): the file id table over the file systems (the disc, and |
| game/mcsys.cpp | Matching | 100.0 | 9 | 1880 | 0 | Memory card system (0x802E6B50-0x802E72B8): the save file built from registered data chunks (each |
| game/homebtn.cpp | Matching | 100.0 | 11 | 1864 | 0 | HOME button menu (0x802EC4BC-0x802ECCE4): loads the HOME menu files for the console language, opens the |
| game/uitable.cpp | Matching | 100.0 | 12 | 1740 | 0 | Menu data helpers (0x8022D228-0x8022E0EC): a fixed string list, the UI table text buffer (rows of |
| game/mixer.cpp | Matching | 100.0 | 11 | 1400 | 0 | Mixer (0x80208A18-0x80209448): the list of sound devices ("mixers"), the effects volume setting and |
| game/romfont.cpp | Matching | 100.0 | 4 | 1348 | 0 | ROM font text (0x802DACD4-0x802DB2A0): loads the console's ROM font and draws boot-screen |
| game/uidraw.cpp | Matching | 100.0 | 14 | 1268 | 0 | UI drawing helpers (0x80230090-0x80231504): UI start-up and shut-down, screen-wrapped rectangles and |
| game/controls.cpp | Matching | 100.0 | 6 | 1252 | 0 | Track B2 full-body draft from original assembly; data qualification incomplete. External declarations identify |
| game/random.cpp | Matching | 100.0 | 7 | 1180 | 0 | Random numbers (0x80129C24-0x8012A13C): seven independent Marsaglia "RANMAR" generators (the |
| game/uictx.cpp | Matching | 100.0 | 14 | 1164 | 0 | UI contexts (0x8022E9E0-0x8022F248): the stack of UI drawing contexts (a 2D transform plus fade |
| game/joy.cpp | Matching | 100.0 | 10 | 1128 | 0 | Joystick input (0x80129400-0x80129BD4): polls the four pads as the "JoyUpdate" background task and |
| game/ident.cpp | Matching | 100.0 | 8 | 1120 | 0 | Player profile slots ("IdentInfo", 0x8000D884-0x8000DFA8). The block is registered with the |
| game/valtable.cpp | Matching | 100.0 | 5 | 1068 | 0 | Value tables (0x80131588-0x80132E7C): 'VALT' resources of named nodes holding key/value string pairs, |
| game/achieve.cpp | Matching | 100.0 | 5 | 1028 | 0 | Achievements (0x8001EF38-0x8001FD7C): eight categories with level schedules from |
| game/difficulty.cpp | Matching | 100.0 | 5 | 920 | 0 | Difficulty (0x80204AA4-0x802057A8): the easy/medium/hard levels and their values from |
| game/race.cpp | Matching | 100.0 | 8 | 900 | 0 | Race/track registry (0x80007D94-0x80008390): reads the track list from tracks.val, maps event |
| game/app.cpp | Matching | 100.0 | 5 | 872 | 0 | Excite Truck's application layer (0x800049C0-0x800053CC): startup and shutdown of every |
| game/discerr.cpp | Matching | 100.0 | 5 | 800 | 0 | Disc and reset monitor (0x802D98F8-0x802D9C5C): watches the DVD drive state and the reset and |
| game/keybd.cpp | Matching | 100.0 | 8 | 792 | 0 | Debug keyboard (0x802E46AC-0x802E4C14): a small queue of key presses, the last 32 keys for |
| game/league.cpp | Matching | 100.0 | 9 | 776 | 0 | AI leagues (0x80137950-0x80137D58): the four AI driver leagues (Dirt, Modified, Truck, Cup), each |
| game/cmdline.cpp | Matching | 100.0 | 8 | 748 | 0 | Command-line switches (0x80019898-0x80019BBC): each "-name" found sets a bit in the option word, |
| game/unlocks.cpp | Matching | 100.0 | 3 | 724 | 0 | Unlockables (0x80018B70-0x80019864): the "schedule" of things unlocked by Excite points and the |
| game/strmfile.cpp | Matching | 100.0 | 8 | 660 | 0 | Stream files (0x802E72B8-0x802E7578): a table of up to 256 named stream files laid out on a virtual |
| game/carclass.cpp | Matching | 100.0 | 8 | 640 | 0 | Car classes (0x8013C748-0x8013C9F4): the SUV/Truck/Featherweight/Heavyweight/Extreme classes, each |
| game/bg.cpp | Matching | 100.0 | 6 | 596 | 0 | Background tasks (0x80127B5C-0x801281A0, "bg:" in its messages): up to ten per-frame hooks, each a |
| game/movieinfo.cpp | Matching | 100.0 | 6 | 520 | 0 | Movie info (0x802ECCE4-0x802ED050): a stack of opened movies (the current one on top) with a "missing" |
| game/vidsetup.cpp | Matching | 100.0 | 6 | 464 | 0 | Video setup (0x802C2A48-0x802C2CAC): picks the video mode, brings up and tears down the |
| game/grades.cpp | Matching | 100.0 | 5 | 460 | 0 | Race grades S-D (0x8001D7D8-0x8001DA5C): names, display names, and per-grade score thresholds |
| game/pool.cpp | Matching | 100.0 | 5 | 460 | 0 | Fixed-size object pool (0x8012A13C-0x8012A388): a free list carved from one MemAlloc block (or a |
| game/strtab.cpp | Matching | 100.0 | 4 | 452 | 0 | String tables (0x8012F150-0x8012F31C): 'STAB' resources holding fixed-stride rows of strings with one |
| game/log.cpp | Matching | 100.0 | 5 | 436 | 0 | Debug log (0x802E3028-0x802E321C): the log file, the debug print (compiled out in this build), and the |
| game/loadtimer.cpp | Matching | 100.0 | 5 | 428 | 0 | Loading-stage timer and full-screen fade (0x800123EC-0x800126B4). AppInit brackets its steps with |
| game/crc.cpp | Matching | 100.0 | 1 | 396 | 0 | CRC-32 (0x80132E7C-0x80133010): MSB-first, polynomial 0x04C11DB7, with the first four bytes taken |
| game/gtime.cpp | Matching | 100.0 | 6 | 392 | 0 | Global game time (0x80236550-0x80236728): tick bookkeeping with pause/resume, a seconds accessor and an |
| game/fonttext.cpp | Matching | 100.0 | 5 | 384 | 0 | Bitmap font text (0x801E4A2C-0x801E4E44): string widths, line heights and aligned, formatted |
| game/sysconfig.cpp | Matching | 100.0 | 8 | 360 | 0 | System settings (0x802E98C8-0x802E9A3C): reads the console settings (aspect ratio, sound mode, |
| game/image.cpp | Matching | 100.0 | 5 | 332 | 0 | Image helpers (0x802C5130-0x802C5390): null-safe wrappers around the gx image module. |
| game/panelvol.cpp | Matching | 100.0 | 1 | 328 | 0 | Panel volumes (0x801BF214-0x801C109C): loose body panels ("panelvol"). A pool of rigid bodies, each a |
| game/eventq.cpp | Matching | 100.0 | 5 | 304 | 0 | Event queue (0x80127A24-0x80127B5C): a 16-entry ring of 0x14-byte events filled elsewhere (the |
| game/unk_80127398.cpp | Matching | 100.0 | 7 | 296 | 0 | Code 0x80127398-0x801274C8: the callback lists run from the Bink/streaming alarms and the static |
| game/strlist.cpp | Matching | 100.0 | 3 | 264 | 0 | Double-null-terminated string lists (0x8022D0D8-0x8022D228): clear, append and count. Names are guesses. |
| game/lightset.cpp | Matching | 100.0 | 2 | 232 | 0 | Light sets (0x80279370-0x80279464): four sets of four light presets, reset to their defaults. |
| game/prof.cpp | Matching | 100.0 | 5 | 228 | 0 | Profiler (0x801274C8-0x80127A24, "prof:" in its messages). Most of it was stripped from the release |
| game/strfunc.cpp | Matching | 100.0 | 1 | 220 | 0 | String helpers (0x801345D8-0x8013471C, "strfunc:" in its message): a bounded sprintf that warns on |
| game/animstream.cpp | Matching | 100.0 | 2 | 196 | 0 | Skeletal animation streams (0x801E68D4-0x801E76A8): "Anim" .an0 resources of packed 64-bit |
| game/memarena.cpp | Matching | 100.0 | 1 | 176 | 0 | Memory arenas (0x802E3DF8-0x802E3EB0): the usable MEM1/MEM2 range, start aligned to 128 bytes. |
| game/joint.cpp | Matching | 100.0 | 2 | 172 | 0 | Joints (0x801E2C1C-0x801E3640): named joints from a model's MRJT chunks, pooled joint instances |
| game/dbgdraw.cpp | Matching | 100.0 | 1 | 132 | 0 | Debug drawing (0x801E3640-0x801E45CC): a basis from a direction, wire boxes along a frame, arrow |
| game/uitext.cpp | Matching | 100.0 | 1 | 132 | 0 | UI text overlay (0x8023875C-0x80238BF4): screen-space text and rectangles in pixel coordinates, |
| game/strmbank.cpp | Matching | 100.0 | 2 | 120 | 0 | Stream banks (0x802E7578-0x802E7770): the global stream-bank manager and its forwarders; this build only |
| game/vidmode.cpp | Matching | 100.0 | 1 | 88 | 0 | Video modes (0x802CE344-0x802CE3A4): the table of supported display modes and its lookup. Names are |
| game/unk_8020A6AC.cpp | Matching | 100.0 | 2 | 88 | 0 | Code 0x8020A6AC-0x8020BCE4: the voice sound mixer (streamed voice/sample sounds) and its sounds, plus the |
| game/carsetup.cpp | Matching | 100.0 | 1 | 88 | 0 | Car setup (0x80279CE4-0x8027AA08): a car's physical parameters from its .car/physics.val |
| game/mmain.cpp | Matching | 100.0 | 2 | 84 | 0 | Main-mode startup (0x8023B5EC-0x8023B764): brings up the subsystems and enters the demo or E3 demo flow. |
| game/gback.cpp | Matching | 100.0 | 1 | 76 | 0 | Menu backgrounds (0x8022F248-0x80230090, "gBack"): textured quads drawn in camera space behind the |
| game/pathcareer.cpp | Matching | 100.0 | 1 | 76 | 0 | Career path classes and the career runner (0x8027D304-0x8027E908): the concrete Path / Bracket / Contest |
| game/vmem.cpp | Matching | 100.0 | 2 | 56 | 0 | Virtual memory buffer (0x802E4C14-0x802E4D84): a heap block handed out by offset, plus a bounded string |
| game/sysinit.cpp | Matching | 100.0 | 1 | 36 | 0 | System init/shutdown pair (0x80129BD4-0x80129C24): brings two subsystems up and down. Names are guesses. |
| game/cartype.cpp | Matching | 100.0 | 1 | 36 | 0 | Car type helpers (0x80279464-0x8027953C): subsystem start/stop and CarTypeIsReal. Names are guesses apart |
| game/rep.cpp | Matching | 100.0 | 1 | 32 | 0 | Registers a 12-byte block under the four-character code 'Rep0' (0x80019864-0x80019898). What |
| game/scratch.cpp | Matching | 100.0 | 1 | 16 | 0 | 16K scratch buffer (0x801293B4-0x80129400): a shared save/restore area. Names are guesses. |
| game/version.cpp | Matching | 100.0 | 1 | 12 | 0 | Build stamp and the ModuleTag list (0x8000D82C-0x8000D884). |
| game/menusel.cpp | Matching | 100.0 | 1 | 12 | 0 | Menu selection hook (0x80245080-0x802451DC): the memory card dialog's selection check and retry |
| game/flags.cpp | Matching | 100.0 | 1 | 12 | 0 | Global game flags word (0x8000D7D8-0x8000D7F0). DirectToTrack (app.cpp) saves and sets bit 0 of |
| game/callhook.cpp | Matching | 100.0 | 2 | 8 | 0 | A small unit (0x802E7770-0x802E77B8): runs an optional callback, plus empty hooks. Names are guesses. |
| game/carlayout.cpp | Matching | 100.0 | 1 | 8 | 0 | Car layouts (0x80278A1C-0x80279370): each car's damage bones, bone links, wheel bones and |
| game/stub16.cpp | Matching | 100.0 | 1 | 4 | 0 | A stripped unit (0x80265D38-0x80265DCC): 21 empty virtual stubs and its statics. Names are guesses. |
| game/sortwrap.cpp | Matching | 100.0 | 1 | 4 | 0 | A one-function unit (0x80265DCC-0x80265DD8): forwards to qsort. Names are guesses. |

## 7. Every game function on the path, by unit

Format: `name address size`, plus `(fuzzy %)` when below 100 and `[indN]` = N indirect call sites.

**game/achieve.cpp** (Matching): AchievementsInit__Fv 8001EF38 0x24; AchLoad__Fv 8001EFF0 0x2D8 [ind12]; AchFindCategory__FPCc 8001F41C 0x74; AchRegister__Fv 8001F528 0x50; AchClearSession__Fv 8001F578 0x44

**game/animstream.cpp** (Matching): fn_801E6978 801E6978 0x74; fn_801E69EC 801E69EC 0x50

**game/app.cpp** (Matching): PlayAttractMovie__Fv 800049C0 0xF8; ResolveResName__FPCc 80004AB8 0x14; LoadResidentRes__Fv 80004ACC 0x48; AppInit__Fv 80004B5C 0x158; ParseCommandLine__FPCc 80004D64 0xBC

**game/autolyt.cpp** (Matching): fn_8022E0EC 8022E0EC 0x3D0; fn_8022E4BC 8022E4BC 0x4; fn_8022E4C0 8022E4C0 0x4A0; fn_8022E960 8022E960 0x38

**game/bg.cpp** (Matching): fn_80127B5C 80127B5C 0x130 [ind2]; fn_80127C8C 80127C8C 0x4; fn_80127C90 80127C90 0x64; fn_80127CF4 80127CF4 0x64; fn_80127F70 80127F70 0x24; fn_80127F94 80127F94 0x34

**game/bonemgr.cpp** (NonMatching): fn_802DA2F4 802DA2F4 0x15C; fn_802DA52C 802DA52C 0x24

**game/callhook.cpp** (Matching): fn_802E7770 802E7770 0x4; fn_802E77A8 802E77A8 0x4

**game/cammgr.cpp** (NonMatching): fn_8026A4A0 8026A4A0 0x7C

**game/carclass.cpp** (Matching): fn_8013C748 8013C748 0x28; fn_8013C770 8013C770 0x60; fn_8013C7D0 8013C7D0 0x30; fn_8013C800 8013C800 0x4; fn_8013C804 8013C804 0xB4 [ind2]; fn_8013C8B8 8013C8B8 0x30; fn_8013C8E8 8013C8E8 0xBC [ind2]; fn_8013C9A4 8013C9A4 0x24

**game/carlayout.cpp** (Matching): fn_80278A1C 80278A1C 0x8

**game/carsetup.cpp** (Matching): fn_80279CE4 80279CE4 0x58

**game/cartype.cpp** (Matching): fn_80279464 80279464 0x24

**game/cmdline.cpp** (Matching): FindOption__FPCcPCc 80019898 0x120; ParseOptions__FPCc 800199B8 0x184 [ind1]; OptDirectToTrack__Fv 80019B3C 0xC; OptModelTest__Fv 80019B48 0xC; OptPaTest__Fv 80019B54 0xC; OptGxTest__Fv 80019B6C 0xC; OptWidescreen__Fv 80019B9C 0xC; OptE3Demo__Fv 80019BA8 0xC

**game/controls.cpp** (Matching): fn_8023D6FC 8023D6FC 0x8; fn_8023D70C 8023D70C 0xC0; fn_8023D7CC 8023D7CC 0x260; fn_8023DA2C 8023DA2C 0x38; fn_8023DA64 8023DA64 0x104; fn_8023DCCC 8023DCCC 0x80

**game/crc.cpp** (Matching): fn_80132E7C 80132E7C 0x18C

**game/dashhelper.cpp** (NonMatching): fn_80011280 80011280 0x3B0; fn_800122B4 800122B4 0x60

**game/dbgdraw.cpp** (Matching): fn_801E3C04 801E3C04 0x84

**game/difficulty.cpp** (Matching): fn_80204AA4 80204AA4 0x74; fn_80204B18 80204B18 0x14; fn_80204B2C 80204B2C 0x88; fn_80204BB4 80204BB4 0x4; fn_80205268 80205268 0x284 [ind11]

**game/discerr.cpp** (Matching): fn_802D992C 802D992C 0x124; fn_802D9A50 802D9A50 0x1A0; fn_802D9BF0 802D9BF0 0x14; fn_802D9C04 802D9C04 0x14; fn_802D9C18 802D9C18 0x34

**game/driver.cpp** (Matching): DriverInit__Fv 80016354 0x9C; GetNumPlayers__Fv 800167F4 0x8; LoadDriverLists__Fv 80016C1C 0xF8; LoadDriverList__FP10ValueTable 80016D14 0xC4 [ind3]; ParseColor__FPCc 80016DD8 0xFC; LoadDriver__FP9ValueNode 80016ED4 0x13C [ind3]; LoadVehicle__FP9ValueNode 80017010 0x548 [ind9]; CheckDuplicateNumbers__Fv 80017558 0x114; ResetPlayer__Fi 8001766C 0x9C

**game/drsurf.cpp** (NonMatching): fn_802D5DE8 802D5DE8 0x1A0; fn_802D62E4 802D62E4 0x18; fn_802D62FC 802D62FC 0x100; fn_802D63FC 802D63FC 0x180; fn_802D657C 802D657C 0x18; fn_802D6594 802D6594 0x64; fn_802D65F8 802D65F8 0x218; fn_802D6810 802D6810 0x50; fn_802D68C4 802D68C4 0x244; fn_802D6B80 802D6B80 0xC4; fn_802D6C68 802D6C68 0x10; fn_802D6C78 802D6C78 0x8D4 (96.3%)

**game/drtex.cpp** (NonMatching): fn_802D7564 802D7564 0x9C; fn_802D7600 802D7600 0x8C; fn_802D768C 802D768C 0x50; fn_802D76DC 802D76DC 0xA4; fn_802D7780 802D7780 0x154; fn_802D78D4 802D78D4 0x288 (65.5%); fn_802D7B5C 802D7B5C 0x23C; fn_802D7D98 802D7D98 0x208; create_generic_texture 802D7FA0 0x224; fn_802D81C4 802D81C4 0x4F8 (92.9%); fn_802D86BC 802D86BC 0x3C8 (95.4%); fn_802D8A84 802D8A84 0x17C; fn_802D8C00 802D8C00 0x11C; fn_802D8D1C 802D8D1C 0x84; fn_802D8DCC 802D8DCC 0xD0 (96.7%); drCreateTexture 802D8E9C 0x118; fn_802D8FB4 802D8FB4 0xB4; fn_802D9068 802D9068 0xEC; fn_802D9154 802D9154 0x8; fn_802D915C 802D915C 0x4; fn_802D9160 802D9160 0xD0; fn_802D9230 802D9230 0x80; fn_802D92B0 802D92B0 0x8; fn_802D92B8 802D92B8 0x88; fn_802D9340 802D9340 0xA0

**game/eve.cpp** (Matching): GetModeInfo__Fi 800126CC 0xAC; LoadArcadeEves__Fv 80012778 0x8; LoadModeEves__Fi 80012790 0x4; UnloadEves__Fv 80012794 0x7C; GetEveMode__Fv 80012810 0x8; GetNumEvents__Fv 80012854 0x8; GetEvent__Fi 8001285C 0x14; LoadEves__Fi 80012984 0x130; IsEveAllowed__FP9ValueNode 80012AB4 0x9C [ind1]; ParseTrack__FP5TrackiP9ValueNodePCc 80012B50 0x794 [ind30]; ParseEveTable__FP10ValueTablePCc 8001335C 0x250 [ind8]; CheckNoField__FPCcP9ValueNodePCc 800135AC 0x6C [ind1]; LoadEveFile__FPCc 80013618 0x68; SortEves__Fv 80013724 0x310; FixupMyoEves__Fv 80013A34 0x154

**game/eventpath.cpp** (Matching): fn_8027AD4C 8027AD4C 0x238 [ind6]; fn_8027AFA0 8027AFA0 0x2E0 [ind6]; fn_8027B2F8 8027B2F8 0x314 [ind2]; fn_8027B690 8027B690 0xEC; fn_8027C014 8027C014 0xBC [ind2]; fn_8027C0D0 8027C0D0 0x50 [ind1]; fn_8027C120 8027C120 0xA8; fn_8027C1C8 8027C1C8 0x90; fn_8027C258 8027C258 0x14; fn_8027C26C 8027C26C 0x4C; fn_8027C2B8 8027C2B8 0x164 [ind4]; fn_8027C41C 8027C41C 0x98; fn_8027C4B4 8027C4B4 0xE8; fn_8027C59C 8027C59C 0x74; fn_8027C610 8027C610 0x40; fn_8027C650 8027C650 0xCC [ind1]; fn_8027C71C 8027C71C 0x6C; fn_8027C788 8027C788 0xC8 [ind3]; fn_8027C850 8027C850 0x60; fn_8027C8B0 8027C8B0 0x8; fn_8027C8B8 8027C8B8 0x164 [ind1]; fn_8027CA1C 8027CA1C 0x98 [ind1]; fn_8027CAB4 8027CAB4 0x6C; fn_8027CB20 8027CB20 0x8; fn_8027CB28 8027CB28 0xC8 [ind3]; fn_8027CBF0 8027CBF0 0x8; fn_8027CBF8 8027CBF8 0xBC; fn_8027CCB4 8027CCB4 0xCC; fn_8027CD80 8027CD80 0xCC; fn_8027CE4C 8027CE4C 0x78; fn_8027CEC4 8027CEC4 0x78; fn_8027CF3C 8027CF3C 0x78; fn_8027CFB4 8027CFB4 0xB4 [ind1]; fn_8027D068 8027D068 0x78; fn_8027D0E0 8027D0E0 0x78; fn_8027D158 8027D158 0x9C; fn_8027D1F4 8027D1F4 0x10

**game/eventq.cpp** (Matching): fn_80127A24 80127A24 0x2C; fn_80127A50 80127A50 0x34; fn_80127A84 80127A84 0x1C; fn_80127AA0 80127AA0 0x18; fn_80127AB8 80127AB8 0x9C

**game/file.cpp** (Matching): fn_802E321C 802E321C 0x54; fn_802E3270 802E3270 0x54; fn_802E32C4 802E32C4 0x78; fn_802E333C 802E333C 0x30; fn_802E336C 802E336C 0x30; Finder__10FileSystemFv 802E33D0 0x8; fn_802E33D8 802E33D8 0xC0; fn_802E3498 802E3498 0x44; fn_802E35C0 802E35C0 0xBC; fn_802E367C 802E367C 0x5C [ind1]; fn_802E36D8 802E36D8 0x5C [ind1]; fn_802E3734 802E3734 0x90 [ind2]; fn_802E37C4 802E37C4 0x40 [ind1]; fn_802E3804 802E3804 0x94; fn_802E3898 802E3898 0x98 [ind1]; fn_802E3930 802E3930 0x60 [ind1]; fn_802E3990 802E3990 0x40 [ind1]; fn_802E39D0 802E39D0 0x44; fn_802E3A14 802E3A14 0x50 [ind1]; fn_802E3A64 802E3A64 0x6C [ind1]; fn_802E3D00 802E3D00 0x4; fn_802E3D68 802E3D68 0x64 [ind1]; fn_802E3DCC 802E3DCC 0x24

**game/filecd.cpp** (NonMatching): __nw__8FileDiskFUlPCc 802E3EB0 0x8; __dl__8FileDiskFPv 802E3EB8 0x4; __nw__6FileCDFUlPCc 802E3EBC 0x7C; __dl__6FileCDFPv 802E3F38 0x5C; fn_802E3F94 802E3F94 0x9C [ind1]; __dt__8FileDiskFv 802E4030 0x40; Create__8FileDiskFPCc 802E4070 0x30; Open__8FileDiskFPCc 802E40A0 0x124 [ind1]; __dt__6FileCDFv 802E41C4 0x8C; OpenWrite__8FileDiskFPCc 802E4250 0x30; OpenAppend__8FileDiskFPCc 802E4280 0x158 [ind3]; Seek__6FileCDFl 802E43D8 0x28; Size__6FileCDFv 802E4400 0x8; Close__8FileDiskFP4File 802E4408 0x24; Print__8FileDiskFPCc 802E442C 0xC; Disk__6FileCDFv 802E4438 0x8; Read__6FileCDFPvl 802E4440 0x16C; ReadAsync__6FileCDFPvl 802E45AC 0x70; IsDone__6FileCDFv 802E461C 0x2C; Write__6FileCDFPCvl 802E4648 0x38; Tell__6FileCDFv 802E4680 0x8; Skip__6FileCDFl 802E4688 0x18; Flush__6FileCDFv 802E46A0 0x4

**game/flags.cpp** (Matching): FlagsInit__Fv 8000D7D8 0xC

**game/fonttext.cpp** (Matching): fn_801E4A6C 801E4A6C 0x44; fn_801E4AB0 801E4AB0 0x50; fn_801E4B00 801E4B00 0x48; fn_801E4B48 801E4B48 0x60; fn_801E4BA8 801E4BA8 0x44

**game/gart.cpp** (Matching): fn_80234D44 80234D44 0xA0; fn_80234DE4 80234DE4 0xA4; fn_80234E88 80234E88 0x6C; fn_80234EF4 80234EF4 0x168; fn_8023505C 8023505C 0x40; fn_8023509C 8023509C 0x20; fn_8023512C 8023512C 0x254; fn_80235380 80235380 0x74; fn_802353F4 802353F4 0xAC; fn_802354A0 802354A0 0x2B8; fn_80235EF0 80235EF0 0x74; fn_80235F64 80235F64 0x74; fn_80235FD8 80235FD8 0x30

**game/gback.cpp** (Matching): fn_8022F248 8022F248 0x4C

**game/glare.cpp** (NonMatching): fn_802DD4FC 802DD4FC 0xF8

**game/grades.cpp** (Matching): GetGradeDisplayName__Fi 8001D7D8 0x14; FindGrade__FPCc 8001D7EC 0x7C; __ct__6GradesFP9ValueNode 8001D884 0x30; Load__6GradesFP9ValueNode 8001D8B4 0x100 [ind2]; GetThreshold__6GradesFi 8001DA48 0xC

**game/gtext.cpp** (NonMatching): fn_8023150C 8023150C 0x80; fn_8023158C 8023158C 0x70; fn_80231714 80231714 0x4; fn_80231718 80231718 0x18; fn_80231730 80231730 0x50; fn_80231780 80231780 0x35C (92.2%); fn_80231ADC 80231ADC 0x14; fn_80232024 80232024 0x1C; fn_80232040 80232040 0x198 [ind1]; fn_802321D8 802321D8 0x58; fn_80232230 80232230 0x20C [ind11]; fn_80232578 80232578 0x35C; fn_8023293C 8023293C 0x234 (99.1%); fn_80232B70 80232B70 0x8; fn_80232B78 80232B78 0x8; fn_80232BA4 80232BA4 0xAC; fn_80233010 80233010 0x344; fn_80233BF0 80233BF0 0x1E8 (99.5%); fn_80233DD8 80233DD8 0x5C; fn_80233E34 80233E34 0x4C; fn_80233EBC 80233EBC 0x4; fn_8023400C 8023400C 0x230 (99.5%); fn_8023423C 8023423C 0x1E0 (99.5%); fn_802344CC 802344CC 0x188; fn_80234654 80234654 0x74; fn_802346C8 802346C8 0x424 (91.2%)

**game/gtime.cpp** (Matching): fn_80236550 80236550 0x4C; fn_8023659C 8023659C 0x3C; fn_802365D8 802365D8 0x28; fn_80236600 80236600 0xC; fn_8023660C 8023660C 0x68; fn_80236674 80236674 0x64

**game/gx.cpp** (NonMatching): fn_802CE9FC 802CE9FC 0x20; fn_802CEA1C 802CEA1C 0x18; fn_802CEA34 802CEA34 0x90; fn_802CEAC4 802CEAC4 0x140; fn_802CEC44 802CEC44 0xC; fn_802CEC50 802CEC50 0xA4; fn_802CECF4 802CECF4 0x5C; fn_802CED50 802CED50 0x28; fn_802CED78 802CED78 0x218; fn_802CEF90 802CEF90 0xB8; fn_802CF048 802CF048 0xBC; fn_802CF104 802CF104 0x8; fn_802CF10C 802CF10C 0x49C (94.1%); fn_802CF5A8 802CF5A8 0x28; fn_802CF5D0 802CF5D0 0x4; fn_802CF5D4 802CF5D4 0x24; fn_802CF5F8 802CF5F8 0x4; fn_802CF5FC 802CF5FC 0x4; fn_802CF600 802CF600 0x230; fn_802CF834 802CF834 0x1C; fn_802CF850 802CF850 0x358; fn_802CFCA0 802CFCA0 0xD4; fn_802CFD74 802CFD74 0x6C; fn_802CFDE0 802CFDE0 0x10C; fn_802CFEEC 802CFEEC 0x24; fn_802CFF10 802CFF10 0x40; fn_802CFF50 802CFF50 0xC; fn_802CFF5C 802CFF5C 0x8; fn_802CFFA4 802CFFA4 0x8; fn_802CFFAC 802CFFAC 0x68; fn_802D0014 802D0014 0x68; fn_802D007C 802D007C 0xE4; fn_802D0160 802D0160 0x28; fn_802D0188 802D0188 0x28; fn_802D01B0 802D01B0 0x30; fn_802D01E0 802D01E0 0x118; fn_802D02F8 802D02F8 0x70; fn_802D0368 802D0368 0x8C; fn_802D03F4 802D03F4 0x118; fn_802D0514 802D0514 0xFC; fn_802D0610 802D0610 0x1D4; fn_802D07E4 802D07E4 0xD0; fn_802D08C4 802D08C4 0x8; fn_802D08CC 802D08CC 0x44; fn_802D0910 802D0910 0x4; fn_802D0914 802D0914 0xD0; fn_802D09E4 802D09E4 0x4; fn_802D09E8 802D09E8 0x1F0; fn_802D0BD8 802D0BD8 0xC0; fn_802D0C98 802D0C98 0x13C; fn_802D0DD4 802D0DD4 0x74; fn_802D0EC8 802D0EC8 0x24; fn_802D0EEC 802D0EEC 0x5EC; fn_802D14D8 802D14D8 0x1A0; fn_802D1678 802D1678 0x144; fn_802D17BC 802D17BC 0x12C; fn_802D18E8 802D18E8 0x270; fn_802D1B58 802D1B58 0xAC; fn_802D1C04 802D1C04 0x614 (96.5%); fn_802D2218 802D2218 0x3FC; fn_802D2614 802D2614 0x2C; fn_802D2640 802D2640 0x44; fn_802D2684 802D2684 0x440; fn_802D2B0C 802D2B0C 0x1F8 [ind1]; fn_802D2D14 802D2D14 0x8; fn_802D2F78 802D2F78 0x60; fn_802D2FD8 802D2FD8 0x78; fn_802D307C 802D307C 0x60; fn_802D30E4 802D30E4 0xC; fn_802D30F0 802D30F0 0xC; fn_802D30FC 802D30FC 0x4; fn_802D3100 802D3100 0x4; fn_802D3104 802D3104 0x28; fn_802D312C 802D312C 0x4; fn_802D3130 802D3130 0x4; fn_802D3134 802D3134 0x4; fn_802D3138 802D3138 0xC; fn_802D3360 802D3360 0xC0; fn_802D37E4 802D37E4 0xB0; fn_802D3900 802D3900 0xA4; fn_802D3DAC 802D3DAC 0x4C; fn_802D3E9C 802D3E9C 0x20

**game/gxcanvas.cpp** (NonMatching): fn_802C2CDC 802C2CDC 0xC; fn_802C2CE8 802C2CE8 0xAC; fn_802C2D94 802C2D94 0x40; fn_802C2DD4 802C2DD4 0x1F0; fn_802C2FC4 802C2FC4 0x4; fn_802C3014 802C3014 0x20; fn_802C3034 802C3034 0x10; fn_802C3044 802C3044 0x1C; fn_802C3060 802C3060 0x1C; fn_802C3130 802C3130 0x40; fn_802C32B8 802C32B8 0x68; fn_802C3320 802C3320 0x88; fn_802C33A8 802C33A8 0x7C; fn_802C49E8 802C49E8 0x298; fn_802C4C80 802C4C80 0x10; fn_802C4EE4 802C4EE4 0x18; fn_802C4EFC 802C4EFC 0x18; fn_802C4F2C 802C4F2C 0x2C; fn_802C4FF8 802C4FF8 0x24; fn_802C501C 802C501C 0x8

**game/homebtn.cpp** (Matching): fn_802EC4BC 802EC4BC 0x2C; fn_802EC4E8 802EC4E8 0xB0; fn_802EC598 802EC598 0x7C; fn_802EC614 802EC614 0x198; fn_802EC884 802EC884 0x1C4; fn_802ECA48 802ECA48 0x1B0; fn_802ECBF8 802ECBF8 0x18; fn_802ECC10 802ECC10 0x8; fn_802ECC18 802ECC18 0x8; fn_802ECC20 802ECC20 0x54; fn_802ECC74 802ECC74 0x68

**game/ident.cpp** (Matching): IdentRegisterField__FllPPvUlPFv_v 8000D884 0x44; IdentInit__Fv 8000D8C8 0x84; IdentSelectSlot__Fi 8000D984 0x8C; IdentGetCurrentSlot__Fv 8000DA10 0xC; IdentClearSlot__Fv 8000DA80 0x98 [ind1]; IdentRegisterCallback__FPFii_v 8000DBC8 0x60; IdentResetAll__Fv 8000DD98 0x194; IdentNotify__Fi 8000DF2C 0x74 [ind1]

**game/image.cpp** (Matching): gxGetImage 802C5130 0x6C; fn_802C519C 802C519C 0x10; fn_802C51C0 802C51C0 0x60; fn_802C5220 802C5220 0x60; fn_802C5310 802C5310 0x10

**game/inputbind.cpp** (NonMatching): fn_801A75B4 801A75B4 0x74; fn_801A7628 801A7628 0x21C; fn_801A7974 801A7974 0x94; fn_801A7E54 801A7E54 0x138; fn_801A90A4 801A90A4 0x6C; fn_801A9110 801A9110 0xA0; fn_801A91B0 801A91B0 0x84; fn_801A9234 801A9234 0x148

**game/joint.cpp** (Matching): fn_801E2F00 801E2F00 0x68; fn_801E2F68 801E2F68 0x44 [ind1]

**game/joy.cpp** (Matching): fn_80129400 80129400 0x58; fn_80129458 80129458 0xA4; fn_801294FC 801294FC 0xC; fn_80129508 80129508 0x104; fn_8012960C 8012960C 0x84; fn_80129690 80129690 0x100; fn_80129790 80129790 0x64; fn_80129AEC 80129AEC 0x8; fn_80129AFC 80129AFC 0x64; fn_80129BC4 80129BC4 0x8

**game/kernel.cpp** (Matching): fn_802E1728 802E1728 0x44; fn_802E176C 802E176C 0x14; fn_802E1780 802E1780 0x84; fn_802E1804 802E1804 0xD0; fn_802E18D4 802E18D4 0x80; fn_802E1954 802E1954 0xC; fn_802E1960 802E1960 0x20; fn_802E1980 802E1980 0xB8; fn_802E1A38 802E1A38 0x64; fn_802E1A9C 802E1A9C 0x118; fn_802E1BB4 802E1BB4 0x1F0; stack_check 802E27F0 0x1BC; fn_802E2A1C 802E2A1C 0x38; fn_802E2A54 802E2A54 0x4; fn_802E2A5C 802E2A5C 0xD8; fn_802E2C20 802E2C20 0x7C; fn_802E2C9C 802E2C9C 0x60; fn_802E2CFC 802E2CFC 0x78; fn_802E2D74 802E2D74 0x98; fn_802E2E0C 802E2E0C 0x2C; fn_802E2E38 802E2E38 0x1D4 [ind2]; fn_802E300C 802E300C 0x14

**game/keybd.cpp** (Matching): fn_802E46AC 802E46AC 0x4; fn_802E46B0 802E46B0 0x34; fn_802E46F0 802E46F0 0xC8; fn_802E47B8 802E47B8 0x30; fn_802E47E8 802E47E8 0x10; fn_802E47F8 802E47F8 0x8; fn_802E4800 802E4800 0x19C; fn_802E4A2C 802E4A2C 0x34

**game/league.cpp** (Matching): fn_80137950 80137950 0x7C; fn_801379CC 801379CC 0x4C; fn_80137A18 80137A18 0xF4 [ind4]; fn_80137B0C 80137B0C 0x8; fn_80137B14 80137B14 0xC; fn_80137B20 80137B20 0xC4; fn_80137C54 80137C54 0x20; fn_80137C84 80137C84 0x14; fn_80137D10 80137D10 0x40

**game/lightset.cpp** (Matching): fn_80279370 80279370 0x3C; fn_802793B0 802793B0 0xAC

**game/loading.cpp** (NonMatching): LoadingInit__Fv 8001FD7C 0xA4; DrawLoadingText__FUl 8001FF34 0x9C; DrawLoadingBackground__Fi 80020860 0x90; GetRecordString__FUlPci 800208F0 0xB0; DrawLoadingScreen__Fif 800209A0 0x8B8 (99.1%); DrawLoadingFull__Fv 80021680 0x38; DrawLoadingTextOnly__Fv 800216C8 0x8; LoadingFrame__Fv 8002173C 0x70; LoadingInitHook__Fv 800218F4 0x4

**game/loadtimer.cpp** (Matching): LoadTimerInit__Fv 80012478 0x64; LoadTimerBegin__FPCciif 800124DC 0x80; LoadTimerMark__FPCcf 8001255C 0x68; LoadTimerProgress__Fv 800125C4 0x10; LoadTimerEnd__FPCc 800125D4 0x50

**game/locale.cpp** (Matching): fn_8012FAB8 8012FAB8 0x728 [ind37]; fn_801301E0 801301E0 0xAC; fn_8013028C 8013028C 0x54; fn_801302E0 801302E0 0xB0; fn_80130390 80130390 0x18; fn_801303A8 801303A8 0x170; fn_80130568 80130568 0x5C; fn_801305C4 801305C4 0x168; fn_801308E4 801308E4 0x10; fn_80130978 80130978 0x8; fn_80130980 80130980 0x64C; fn_80130FCC 80130FCC 0x14; fn_80130FE0 80130FE0 0xD4; fn_801310B4 801310B4 0x68; fn_8013111C 8013111C 0x118; fn_80131234 80131234 0x30; __ct__9LocStringFPCc 801312EC 0x1C; fn_80131308 80131308 0x10; fn_80131318 80131318 0x50

**game/log.cpp** (Matching): fn_802E3028 802E3028 0x4; fn_802E302C 802E302C 0x64; fn_802E30C8 802E30C8 0x88; fn_802E3150 802E3150 0x2C; fn_802E317C 802E317C 0x98

**game/mcsys.cpp** (Matching): fn_802E6B50 802E6B50 0x44; fn_802E6B98 802E6B98 0x40; fn_802E6BD8 802E6BD8 0xCC; fn_802E6CA4 802E6CA4 0x4; MemCardSystemLoad 802E6CA8 0x314 [ind3]; fn_802E6FBC 802E6FBC 0x16C [ind2]; fn_802E712C 802E712C 0x4; fn_802E7130 802E7130 0xEC; fn_802E721C 802E721C 0x94

**game/mem.cpp** (Matching): fn_801281A0 801281A0 0x54; fn_801281F4 801281F4 0x14; fn_80128208 80128208 0x4; fn_8012820C 8012820C 0x94; fn_801282E8 801282E8 0x4; fn_801282EC 801282EC 0x8; fn_801282F4 801282F4 0x20; fn_80128314 80128314 0x38; fn_8012834C 8012834C 0x19C [ind2]; fn_801284E8 801284E8 0x8; fn_801284F0 801284F0 0x8; __nw__FUlPCc 801284F8 0x14; __nwa__FUlPCc 8012850C 0x14; MemFree 80128584 0xD4; fn_80128658 80128658 0x8; fn_80128660 80128660 0x8; __dl__FPv 80128668 0x8; __dla__FPv 80128670 0x8; Init__Q217@unnamed@mem_cpp@9StackHeapFPvUl 80128714 0xBC; FindFree__Q217@unnamed@mem_cpp@9StackHeapFUl 801288A4 0x8; Alloc__Q217@unnamed@mem_cpp@9StackHeapFlPCci 801288AC 0x1F8 [ind1]; Free__Q217@unnamed@mem_cpp@9StackHeapFPvi 80128AA4 0x1B8

**game/memarena.cpp** (Matching): fn_802E3DF8 802E3DF8 0xB0

**game/menusel.cpp** (Matching): fn_80245118 80245118 0xC

**game/mixer.cpp** (Matching): fn_80208A18 80208A18 0x38; fn_80208A50 80208A50 0x28; fn_80208A78 80208A78 0x50; fn_80208B8C 80208B8C 0x170; fn_80208D4C 80208D4C 0x14; fn_80208D70 80208D70 0x10; fn_80208D80 80208D80 0x110 [ind1]; fn_80208E90 80208E90 0x198 [ind8]; fn_80209028 80209028 0x8; fn_80209030 80209030 0x60; fn_80209090 80209090 0x24

**game/mmain.cpp** (Matching): fn_8023B5EC 8023B5EC 0x2C; fn_8023B618 8023B618 0x28

**game/model.cpp** (NonMatching): fn_802C5390 802C5390 0x18; fn_802C53A8 802C53A8 0x1C; fn_802C595C 802C595C 0xF4; fn_802C5A50 802C5A50 0x10; fn_802C5A60 802C5A60 0x140; fn_802C5BEC 802C5BEC 0x54; fn_802C5C40 802C5C40 0x84; fn_802C5CC4 802C5CC4 0x12C; fn_802C5DF0 802C5DF0 0x7C [ind1]; fn_802C5E7C 802C5E7C 0x3C; fn_802C5EB8 802C5EB8 0x44; fn_802C5F48 802C5F48 0x120; mrModelEnd 802C6080 0x13C; mrModelFixup 802C61BC 0x8C8 (85.5%); Relocate<5MElem>__FRP5MElemUl_v 802C6A84 0x18; Relocate<5MPrim>__FRP5MPrimUl_v 802C6A9C 0x18; Relocate<P6MExtra>__FRPP6MExtraUl_v 802C6AB4 0x18; Relocate<6MExtra>__FRP6MExtraUl_v 802C6ACC 0x18; Relocate<P9MSurfList>__FRPP9MSurfListUl_v 802C6AE4 0x18; Relocate<9MSurfList>__FRP9MSurfListUl_v 802C6AFC 0x18; Relocate<P5MSurf>__FRPP5MSurfUl_v 802C6B14 0x18; Relocate<5MSurf>__FRP5MSurfUl_v 802C6B2C 0x18; fn_802C6B44 802C6B44 0xDC; fn_802C6D5C 802C6D5C 0x68; fn_802C6DC4 802C6DC4 0x1E4; fn_802C6FC0 802C6FC0 0x7C; fn_802C7734 802C7734 0x24; fn_802C7758 802C7758 0x8; fn_802C7760 802C7760 0x90; fn_802C7A10 802C7A10 0x24

**game/movieinfo.cpp** (Matching): fn_802ECCE4 802ECCE4 0x84; fn_802ECD68 802ECD68 0x8C; fn_802ECDF4 802ECDF4 0x54; fn_802ECE48 802ECE48 0x78; fn_802ECEC0 802ECEC0 0x4; fn_802ECEC4 802ECEC4 0x28

**game/mrender.cpp** (Matching): fn_802C82C4 802C82C4 0x100; fn_802C83CC 802C83CC 0x120; fn_802C84EC 802C84EC 0x44; fn_802C8530 802C8530 0x2C; fn_802C855C 802C855C 0x2C; fn_802C8588 802C8588 0xB0; fn_802C8638 802C8638 0xA4; fn_802C86E8 802C86E8 0x8; fn_802C87B4 802C87B4 0x108; fn_802C88BC 802C88BC 0x10; fn_802C8B70 802C8B70 0x28; fn_802C8B98 802C8B98 0x38; fn_802C8BD0 802C8BD0 0x34; fn_802C8D74 802C8D74 0x44; fn_802C8DB8 802C8DB8 0x4; fn_802C8DBC 802C8DBC 0x44; fn_802C8E00 802C8E00 0x48; fn_802C8E48 802C8E48 0x28; fn_802C90E0 802C90E0 0x10; fn_802C974C 802C974C 0x190; fn_802C990C 802C990C 0x44; fn_802C9A58 802C9A58 0x8; fn_802C9A60 802C9A60 0x8; fn_802C9C58 802C9C58 0x34

**game/musicplr.cpp** (NonMatching): fn_8020A100 8020A100 0x268 (97.4%); fn_8020A368 8020A368 0x98; fn_8020A47C 8020A47C 0xD4; fn_8020A600 8020A600 0x34

**game/namedcam.cpp** (NonMatching): fn_80276098 80276098 0x4D4 (98.0%); fn_80276A44 80276A44 0x88; fn_80276D3C 80276D3C 0x8C

**game/options.cpp** (Matching): fn_80133010 80133010 0x14; fn_80133024 80133024 0xC4; fn_801331C4 801331C4 0x78; fn_8013323C 8013323C 0x94; fn_801332D0 801332D0 0x78; fn_80133348 80133348 0xA0; fn_801333E8 801333E8 0x78; fn_80133460 80133460 0x94; fn_801334F4 801334F4 0x98; fn_8013358C 8013358C 0xD0; fn_8013365C 8013365C 0x84; fn_801336E0 801336E0 0xA0; fn_801337E4 801337E4 0x60; fn_80133844 80133844 0x94; fn_801338D8 801338D8 0x88; fn_80133960 80133960 0xA8; fn_80133A08 80133A08 0xD0; fn_80133AD8 80133AD8 0x4C; fn_80133B24 80133B24 0x7C; fn_80133BA0 80133BA0 0x270; fn_80133E10 80133E10 0x12C

**game/ovlayout.cpp** (NonMatching): fn_801451DC 801451DC 0xC4; fn_801452B4 801452B4 0x14; fn_80148134 80148134 0x68

**game/panelvol.cpp** (Matching): fn_801BFD34 801BFD34 0x148

**game/pathcareer.cpp** (Matching): fn_8027DAF4 8027DAF4 0x4C

**game/pool.cpp** (Matching): __ct__4PoolFPCcll 8012A13C 0x40; Init__4PoolFPCcllPv 8012A17C 0x9C; Reset__4PoolFv 8012A218 0x48; Alloc__4PoolFv 8012A2D8 0x8C; Free__4PoolFPv 8012A364 0x1C

**game/popwarn.cpp** (NonMatching): fn_80246AE8 80246AE8 0x160 (97.8%); Enter__7MenuArtFv 80248A68 0xC; Update__7MenuArtFv 80248A74 0x7C; Draw__7MenuArtFv 80248AF0 0xE0

**game/prof.cpp** (Matching): fn_80127538 80127538 0x74; fn_80127954 80127954 0x4; fn_80127964 80127964 0x4; fn_80127968 80127968 0x50; fn_801279B8 801279B8 0x18

**game/race.cpp** (Matching): LoadTracks__Fv 80007DA0 0x164 [ind5]; RaceInit__Fv 80007F40 0x28; GetEventTypeName__FUl 80007F90 0x60; GetEventTypeDisplayName__FUl 80007FF0 0x60; FindEventType__FPCc 80008050 0x88; GetTrackCount__Fv 800080D8 0x10; FindTrack__FPCc 800080E8 0x70; GetTrackName__Fi 80008158 0x30

**game/racing.cpp** (NonMatching): Mat3FromEuler__FP4Mat3PC4Vec3 80015A20 0x80; Mat3RotZ__FP4Mat3f 80015AA0 0x80; Mat3Mul__FP4Mat3PC4Mat3PC4Mat3 80015B20 0x310; Mat3RotX__FP4Mat3f 80015E30 0x80; Mat3RotY__FP4Mat3f 80015EB0 0x80

**game/random.cpp** (Matching): fn_80129C24 80129C24 0x3C; fn_80129C64 80129C64 0x1C; fn_80129C80 80129C80 0x98; fn_80129D18 80129D18 0x78; fn_80129D90 80129D90 0x34; fn_80129E34 80129E34 0x24C; fn_8012A080 8012A080 0xB4

**game/recordmgr.cpp** (NonMatching): fn_801AED54 801AED54 0x4

**game/rep.cpp** (Matching): RepInit__Fv 80019864 0x20

**game/res.cpp** (NonMatching): fn_8012A388 8012A388 0xE4; fn_8012A46C 8012A46C 0x4; fn_8012A470 8012A470 0x100; __nw__13AsyncResourceFUlPCc 8012A570 0x78; __dl__13AsyncResourceFPv 8012A5E8 0x60 [ind1]; fn_8012A648 8012A648 0x34; fn_8012A6C8 8012A6C8 0x134; fn_8012A7FC 8012A7FC 0x10; fn_8012A80C 8012A80C 0x254; fn_8012AA60 8012AA60 0xD4; fn_8012AB34 8012AB34 0x7C; fn_8012ABB0 8012ABB0 0x78; fn_8012AC28 8012AC28 0x70; fn_8012AC98 8012AC98 0x8; fn_8012ACA0 8012ACA0 0x340; fn_8012AFE0 8012AFE0 0x2E4; fn_8012B2C4 8012B2C4 0x50; fn_8012B314 8012B314 0x10; fn_8012B324 8012B324 0x4; fn_8012B328 8012B328 0x4; fn_8012B32C 8012B32C 0xA8; fn_8012B3D4 8012B3D4 0x110; fn_8012B4E4 8012B4E4 0x28; fn_8012B50C 8012B50C 0x28; fn_8012B534 8012B534 0x1C8; fn_8012B6FC 8012B6FC 0x160; fn_8012B85C 8012B85C 0x1C0; fn_8012BA1C 8012BA1C 0xFC; fn_8012BB18 8012BB18 0x8; fn_8012BB20 8012BB20 0x104; fn_8012BC24 8012BC24 0x2C; fn_8012BC50 8012BC50 0xEC [ind1]; fn_8012BD3C 8012BD3C 0x5C; fn_8012BD98 8012BD98 0xF8; fn_8012BE90 8012BE90 0x60; fn_8012BEF0 8012BEF0 0x34; fn_8012BF24 8012BF24 0x4; fn_8012C33C 8012C33C 0x34; fn_8012C370 8012C370 0xAC; fn_8012C4D8 8012C4D8 0xC; fn_8012C58C 8012C58C 0x2C; ResourceGetStream 8012C5B8 0x138; Span__13AsyncResourceFv 8012C6F0 0x30; Load__13AsyncResourceFPUcll 8012C720 0xDC; Poll__13AsyncResourceFPl 8012C7FC 0x110; Cancel__13AsyncResourceFv 8012C90C 0x4C; fn_8012C958 8012C958 0x70; __nw__16AsyncResourceSetFUlPCc 8012C9C8 0x78; __dl__16AsyncResourceSetFPv 8012CA40 0x60 [ind1]; fn_8012CAA0 8012CAA0 0xA4; __dt__11ResourceSetFv 8012CB44 0xDC; Restart__16AsyncResourceSetFPCc 8012CC24 0x400; StartLoad__16AsyncResourceSetFPCc 8012D024 0x184; Reset__16AsyncResourceSetFv 8012D1A8 0x194; CopyHeader__16AsyncResourceSetFP12ResSetHeaderPC12ResSetHeader 8012D33C 0x94; Ready__16AsyncResourceSetFv 8012D3D0 0x8; Finish__16AsyncResourceSetFv 8012D3D8 0x34; Update__16AsyncResourceSetFv 8012D40C 0x42C; Status__16AsyncResourceSetFv 8012D838 0x70; fn_8012D8A8 8012D8A8 0x60; fn_8012D908 8012D908 0x2C8; fn_8012DBD0 8012DBD0 0xF0; fn_8012DDA8 8012DDA8 0x13C; Process__13AsyncSetQueueFv 8012DEE4 0xE8; Append__8NodeListFP9QueueNode 8012DFCC 0x1C; Init__9QueueNodeFPCcl 8012DFE8 0x78; LoadSize__8TocSizerFPCc 8012E060 0x26C; Flush__13AsyncSetQueueFv 8012E2CC 0x108; PopFront__8NodeListFv 8012E3D4 0x38; Reload__16AsyncResourceSetFPCc 8012E40C 0x54; Alloc__8NodePoolFv 8012E460 0x30; First__8NodeListFv 8012E490 0x8; fn_8012E498 8012E498 0x14C; Free__8NodePoolFP9QueueNode 8012E5E4 0x4C; Free__9QueueNodeFv 8012E630 0x44; Used__8NodePoolFv 8012E674 0x8; Wait__13AsyncSetQueueFPCc 8012E67C 0x1A0; fn_8012E81C 8012E81C 0x11C; fn_8012E938 8012E938 0x34; fn_8012EC90 8012EC90 0x90; fn_8012ED20 8012ED20 0x88

**game/rjoy.cpp** (Matching): fn_802E77B8 802E77B8 0x8; fn_802E77C0 802E77C0 0x64; fn_802E7824 802E7824 0x80; fn_802E78A4 802E78A4 0x7C; fn_802E7920 802E7920 0xF4; fn_802E7C40 802E7C40 0x24; fn_802E7C64 802E7C64 0xA8; fn_802E7DDC 802E7DDC 0x1F4; fn_802E7FD0 802E7FD0 0x33C; fn_802E830C 802E830C 0x58; fn_802E8364 802E8364 0x1A0; fn_802E8664 802E8664 0x4; fn_802E8668 802E8668 0x28; fn_802E86B4 802E86B4 0x18

**game/rmemcard.cpp** (Matching): fn_802E86D4 802E86D4 0x38 [ind1]; fn_802E870C 802E870C 0x38 [ind1]; fn_802E8744 802E8744 0xF4 [ind1]; fn_802E883C 802E883C 0x38; fn_802E8874 802E8874 0x18; fn_802E888C 802E888C 0x4C; fn_802E88D8 802E88D8 0x40; fn_802E8918 802E8918 0x8; fn_802E8920 802E8920 0xF8; test_banner_ok 802E8A18 0xD0; fn_802E8AE8 802E8AE8 0x4C; fn_802E8B34 802E8B34 0x64; fn_802E8B98 802E8B98 0x4C; fn_802E8BE4 802E8BE4 0x48; fn_802E8C2C 802E8C2C 0x40; fn_802E8CA8 802E8CA8 0x84; fn_802E8D7C 802E8D7C 0x8; fn_802E8D8C 802E8D8C 0x284; fn_802E9010 802E9010 0x108; fn_802E9118 802E9118 0x48; fn_802E9160 802E9160 0x4C; fn_802E91AC 802E91AC 0x10; fn_802E91BC 802E91BC 0x1C; fn_802E91D8 802E91D8 0x7C; fn_802E9254 802E9254 0x378; fn_802E95E0 802E95E0 0x9C [ind1]; fn_802E967C 802E967C 0x138; fn_802E97B4 802E97B4 0xB8; fn_802E986C 802E986C 0x4; fn_802E9870 802E9870 0x4; fn_802E9874 802E9874 0x4; fn_802E9878 802E9878 0x4; fn_802E987C 802E987C 0x4; fn_802E9880 802E9880 0x4; fn_802E9884 802E9884 0x4; fn_802E9888 802E9888 0x4

**game/roadsnd.cpp** (NonMatching): fn_8020CA30 8020CA30 0x4

**game/romfont.cpp** (Matching): fn_802DACD4 802DACD4 0x144; fn_802DAE64 802DAE64 0x1D8; fn_802DB03C 802DB03C 0x12C; fn_802DB168 802DB168 0xFC

**game/scratch.cpp** (Matching): fn_801293B4 801293B4 0x10

**game/sdmp3.cpp** (NonMatching): fn_802E9A3C 802E9A3C 0x30; fn_802E9A6C 802E9A6C 0xE8 [ind1]; fn_802E9BCC 802E9BCC 0x20; fn_802E9BEC 802E9BEC 0x7C; fn_802E9CF0 802E9CF0 0xEC; fn_802E9E54 802E9E54 0x64; fn_802E9EB8 802E9EB8 0x71C; fn_802EA5D4 802EA5D4 0x20C; fn_802EA7E0 802EA7E0 0x54; fn_802EA834 802EA834 0x1D4 [ind2]; fn_802EAA08 802EAA08 0x50; fn_802EAA58 802EAA58 0xD0; fn_802EAB28 802EAB28 0xCAC (89.3%) [ind5]; fn_802EBE2C 802EBE2C 0x20C

**game/sortwrap.cpp** (Matching): fn_80265DCC 80265DCC 0x4

**game/sound.cpp** (NonMatching): fn_802062E8 802062E8 0xF0 [ind2]; fn_80206430 80206430 0x14; __ct__9GameSoundFPCci 802065F8 0xD0; __ct__7Sound2DFPCci 802072E4 0xC0 [ind1]; fn_80207848 80207848 0x84 [ind1]; fn_802078CC 802078CC 0x58

**game/soundmgr.cpp** (NonMatching): fn_80207960 80207960 0x4; fn_80207964 80207964 0x1C; __ct__9SoundListFv 80207980 0x10; __ct__12SoundManagerFP5Mixer 80207990 0x1D0 (98.0%) [ind1]; SetListener__12SoundManagerFRC8Listeneri 80207C50 0x24; Check__12SoundManagerFv 80208404 0x4; Add__12SoundManagerFP9GameSound 80208408 0x1E0 (98.8%); Add2__12SoundManagerFP9GameSound 802085E8 0x4; IsPositional__12SoundManagerFi 802085F0 0x30; Create__12SoundManagerFPvi 80208858 0x13C [ind4]; Create2__12SoundManagerFPvi 80208994 0x4

**game/stats.cpp** (Matching): StatsGetLabel__FUl 8001BA6C 0x4; StatsFormatValue__FUlUlPci 8001BA70 0x4; StatsInit__Fv 8001BA78 0xC; StatsGetValue__FiUlliUl 8001BA90 0x7C; StatsGetHighScore__Filli 8001BB58 0x5C; MatchContestName__FPCcPCc 8001C200 0x7C; Matches__9StatEntryCFUlPCc 8001C27C 0x50; FindStat__FiUlPCc 8001C2CC 0x90; StatsCheck__Fv 8001C4D4 0x4; StatsCheck2__Fv 8001C4D8 0x4; StatsCheck3__Fv 8001C4DC 0x4; GetStatTypeInfo__Fl 8001C4E0 0x60; FindHighScoreList__FiPCcli 8001C540 0x1D4; GetStatLabel__FUl 8001C714 0x28; FormatStatValue__FUllPci 8001C73C 0x224; StatsInitImpl__Fv 8001C9C4 0x134; GetStatValue__FiUlPCciUl 8001CDB8 0x7C; GetHighScore__FiPCcli 8001CE74 0x60; GetContestName__Fl 8001D25C 0x3C [ind1]

**game/strfunc.cpp** (Matching): fn_801345D8 801345D8 0xDC

**game/strlist.cpp** (Matching): fn_8022D0D8 8022D0D8 0x10; fn_8022D0E8 8022D0E8 0xCC; fn_8022D1B4 8022D1B4 0x2C

**game/strmbank.cpp** (Matching): fn_802E7578 802E7578 0x24; fn_802E769C 802E769C 0x54

**game/strmfile.cpp** (Matching): fn_802E72B8 802E72B8 0x10; fn_802E72C8 802E72C8 0x50; fn_802E7318 802E7318 0x18; fn_802E7354 802E7354 0x118; fn_802E746C 802E746C 0x20; fn_802E748C 802E748C 0xB8; fn_802E7544 802E7544 0x4; fn_802E7548 802E7548 0x28

**game/strtab.cpp** (Matching): fn_8012F150 8012F150 0x194; fn_8012F2E4 8012F2E4 0x4; fn_8012F2E8 8012F2E8 0x8; fn_8012F2F0 8012F2F0 0x24

**game/stub16.cpp** (Matching): fn_80265D48 80265D48 0x4

**game/stub18.cpp** (NonMatching): fn_802789A8 802789A8 0x4; fn_802789AC 802789AC 0x4

**game/swfanim.cpp** (NonMatching): Reset__7SwfAnimFv 8023905C 0x58; LoadFile__7SwfAnimFPCc 802390B4 0x598 (97.4%); __ct__7SwfAnimFv 8023964C 0x30; Load__7SwfAnimFPCc 80239784 0xE8

**game/sysconfig.cpp** (Matching): fn_802E98C8 802E98C8 0x8; fn_802E98D0 802E98D0 0xD8; fn_802E99AC 802E99AC 0x60; fn_802E9A0C 802E9A0C 0x8; fn_802E9A14 802E9A14 0x8; fn_802E9A1C 802E9A1C 0x8; fn_802E9A24 802E9A24 0x8; fn_802E9A2C 802E9A2C 0x8

**game/sysinit.cpp** (Matching): fn_80129BD4 80129BD4 0x24

**game/tevbink.cpp** (NonMatching): fn_802DEFA0 802DEFA0 0x214; fn_802DF21C 802DF21C 0xD4; fn_802DF300 802DF300 0x16C; fn_802DF4A8 802DF4A8 0x1DC; fn_802DF6D0 802DF6D0 0x268; fn_802DF994 802DF994 0x160; fn_802E09EC 802E09EC 0x17C; fn_802E0BA4 802E0BA4 0x4; fn_802E0BA8 802E0BA8 0x38; fn_802E0BE0 802E0BE0 0x24; fn_802E0E60 802E0E60 0x4C; fn_802E0EAC 802E0EAC 0xEC; fn_802E0F98 802E0F98 0xC0; fn_802E11A4 802E11A4 0xEC; fn_802E1290 802E1290 0x4C

**game/texture.cpp** (NonMatching): fn_802CA0E4 802CA0E4 0xFC; fn_802CA1E0 802CA1E0 0xB4; fn_802CA2A8 802CA2A8 0xD8; fn_802CA380 802CA380 0x74; fn_802CA3F4 802CA3F4 0xEC; fn_802CA4E0 802CA4E0 0x9C; fn_802CA57C 802CA57C 0xDC; fn_802CA658 802CA658 0xD4; fn_802CA7D4 802CA7D4 0x8; fn_802CA7DC 802CA7DC 0x18; fn_802CA7F4 802CA7F4 0x30; fn_802CA824 802CA824 0x74; fn_802CA898 802CA898 0x390; fn_802CAC28 802CAC28 0xC; fn_802CAC34 802CAC34 0x10; fn_802CAC44 802CAC44 0x10; gxForgetTexture 802CAC54 0xD4; fn_802CAD28 802CAD28 0x40; fn_802CAD74 802CAD74 0x5C; fn_802CAF8C 802CAF8C 0x3C; fn_802CAFC8 802CAFC8 0x28; fn_802CAFF0 802CAFF0 0x28; fn_802CB38C 802CB38C 0x68; fn_802CB3F4 802CB3F4 0x4; fn_802CB3F8 802CB3F8 0x4; fn_802CB3FC 802CB3FC 0x28; fn_802CB424 802CB424 0x290; fn_802CB6B4 802CB6B4 0x178; fn_802CB82C 802CB82C 0x370 (97.8%); fn_802CBB9C 802CBB9C 0x4C; fn_802CBBE8 802CBBE8 0x54; fn_802CBC3C 802CBC3C 0xF4; fn_802CBD30 802CBD30 0x4C; fn_802CBD7C 802CBD7C 0x54; fn_802CBDD0 802CBDD0 0x1D0; fn_802CBFA0 802CBFA0 0x2C; fn_802CBFCC 802CBFCC 0x70; fn_802CC03C 802CC03C 0xD0; fn_802CC10C 802CC10C 0x1BC (97.6%); Alloc__10SimpleHeapFl 802CC2C8 0x13C (97.9%); fn_802CC404 802CC404 0xC; fn_802CC410 802CC410 0x108; fn_802CC518 802CC518 0x38; fn_802CC60C 802CC60C 0x130; fn_802CC770 802CC770 0x414 (98.6%)

**game/tristrip.cpp** (NonMatching): fn_801E4E44 801E4E44 0xDC; fn_801E4F20 801E4F20 0x1C; fn_801E4F3C 801E4F3C 0xB8; fn_801E4FF4 801E4FF4 0x28

**game/uianime.cpp** (Matching): fn_80236728 80236728 0x4C; fn_80236780 80236780 0xC8; fn_80236848 80236848 0x6C; fn_802368B4 802368B4 0x1C; fn_802368D0 802368D0 0x10; fn_80236900 80236900 0x10; fn_80236918 80236918 0x38; fn_80236950 80236950 0x4; fn_802369A8 802369A8 0xB4; fn_80236A5C 80236A5C 0xF0C [ind52]; fn_80237A9C 80237A9C 0x4; fn_80237C40 80237C40 0xC0; fn_80237D00 80237D00 0xA0 [ind3]

**game/uiapi.cpp** (NonMatching): fn_8021165C 8021165C 0x4; fn_80211678__FPUc 80211678 0x18; fn_80211690 80211690 0x694; fn_80211F44 80211F44 0x8; fn_80211F54 80211F54 0x10; fn_80211F64 80211F64 0x8C; fn_8021203C 8021203C 0x198; fn_80212214 80212214 0x34; fn_80212248 80212248 0x34; fn_8021227C 8021227C 0xBC; fn_80212340 80212340 0x78; fn_802123B8 802123B8 0x1DE8 (99.8%); fn_802141A0 802141A0 0x51C (98.7%); fn_802146BC 802146BC 0x160; fn_80214D48 80214D48 0x8; fn_80214D50 80214D50 0xC0; fn_80215228 80215228 0x34; fn_8021525C 8021525C 0x8; fn_80215264 80215264 0xC; fn_80215270 80215270 0x28; fn_80215298 80215298 0x30

**game/uictx.cpp** (Matching): __nw__5cnodeFUlPCc 8022E9E0 0x78; __dl__5cnodeFPv 8022EA58 0x60 [ind1]; __nw__9UIContextFUlPCc 8022EAB8 0x78; __dl__9UIContextFPv 8022EB30 0x60 [ind1]; UIContextBegin__Fv 8022EB90 0x60; fn_8022EC7C 8022EC7C 0x44; fn_8022ECC0 8022ECC0 0x40; fn_8022ED00 8022ED00 0x50; fn_8022ED50 8022ED50 0x68; fn_8022EDB8 8022EDB8 0xC; fn_8022EDC4 8022EDC4 0x20; fn_8022EDE4 8022EDE4 0xB4; fn_8022EE98 8022EE98 0x4; fn_8022EE9C 8022EE9C 0x5C

**game/uidraw.cpp** (Matching): fn_80230090 80230090 0x84; fn_80230A68 80230A68 0x50; fn_80230AB8 80230AB8 0x20; fn_80230AD8 80230AD8 0x60; fn_80230B38 80230B38 0x24; fn_80230B5C 80230B5C 0x40; fn_80230B9C 80230B9C 0x94; fn_80230C30 80230C30 0x6C; fn_80230DA0 80230DA0 0x4; fn_80230DA4 80230DA4 0x50; fn_80230E90 80230E90 0x64; fn_8023108C 8023108C 0x8; fn_80231220 80231220 0x160; fn_80231380 80231380 0x1C

**game/uistyle.cpp** (Matching): fn_8022A5E4 8022A5E4 0x1EC; fn_8022A7D4 8022A7D4 0x30; fn_8022A804 8022A804 0x30; fn_8022A834 8022A834 0x24; fn_8022A858 8022A858 0x14; fn_8022A86C 8022A86C 0x78; fn_8022AB94 8022AB94 0x34; fn_8022ABC8 8022ABC8 0x54; fn_8022AC1C 8022AC1C 0xE0; fn_8022ACFC 8022ACFC 0x5D8; fn_8022B498 8022B498 0x188; fn_8022B620 8022B620 0x2FC; fn_8022C520 8022C520 0x4B0 [ind21]; fn_8022CA2C 8022CA2C 0x14; fn_8022CAA8 8022CAA8 0x1E4; fn_8022CC8C 8022CC8C 0x80; fn_8022CD0C 8022CD0C 0xD8; fn_8022CDE4 8022CDE4 0x2A4 [ind12]

**game/uitable.cpp** (Matching): fn_8022D228 8022D228 0x94; fn_8022D350 8022D350 0xA8; Get__10StringListCFiPi 8022D3F8 0x60; __ct__7UITableFiiPCi 8022D458 0x314; Clear__7UITableFv 8022D76C 0x58; At__7UITableFii 8022D82C 0x24; Set__7UITableFiiPCcc 8022D894 0x90; SetVisible__7UITableFi 8022D924 0x28; SetPage__7UITableFi 8022D94C 0x14; SetSel__7UITableFi 8022D960 0x1C; __ct__9CycleListFPPCcPClii 8022DB50 0x44; __ct__8NameListFPPCcii 8022DCC4 0x74

**game/uitext.cpp** (Matching): fn_8023889C 8023889C 0x84

**game/unk_80116BD4.c** (NonMatching): fn_80116BD4 80116BD4 0xF0 (96.6%); fn_80116D30 80116D30 0xAC; fn_80116DDC 80116DDC 0x50; fn_80116E2C 80116E2C 0x28; fn_80116E54 80116E54 0x90 [ind1]; fn_80116EE4 80116EE4 0xA8; fn_80117074 80117074 0x2BC [ind3]; fn_80117380 80117380 0x130 [ind1]; fn_801174B0 801174B0 0xE8; fn_80117618 80117618 0xE4; fn_801176FC 801176FC 0x10; fn_8011770C 8011770C 0xFAC (100.0%) [ind16]; fn_80118C1C 80118C1C 0x28; fn_80118DC0 80118DC0 0x15C [ind2]; fn_80118F1C 80118F1C 0x5C; fn_80118F78 80118F78 0x138 [ind1]; fn_80119214 80119214 0x4; fn_8011B584 8011B584 0x3C8; fn_8011BC48 8011BC48 0x4; fn_8011BC4C 8011BC4C 0x60; fn_8011BE48 8011BE48 0x80; fn_8011BEC8 8011BEC8 0x84; fn_8011BF4C 8011BF4C 0xB0; fn_8011BFFC 8011BFFC 0x70; fn_8011C06C 8011C06C 0x74; fn_8011C0E0 8011C0E0 0x40; fn_8011C120 8011C120 0xC0; fn_8011C1E0 8011C1E0 0x94; fn_8011C274 8011C274 0x78; fn_8011C2EC 8011C2EC 0x78; fn_8011C364 8011C364 0x44; fn_8011DF68 8011DF68 0x44; fn_8011DFAC 8011DFAC 0x108; fn_8011FE20 8011FE20 0x104; fn_8011FF24 8011FF24 0x8; fn_8011FF2C 8011FF2C 0x8C; fn_8011FFB8 8011FFB8 0xC; fn_8011FFC4 8011FFC4 0xC0 [ind1]; fn_80120084 80120084 0x38

**game/unk_80127398.cpp** (Matching): fn_80127398 80127398 0x94 [ind1]; fn_8012742C 8012742C 0x8; fn_80127434 80127434 0xC; fn_80127440 80127440 0x24; fn_80127464 80127464 0x4; fn_80127468 80127468 0x24; fn_8012748C 8012748C 0x34

**game/unk_8020A6AC.cpp** (Matching): fn_8020A72C 8020A72C 0x40; __ct__15VoiceSoundMixerFv 8020A860 0x18

**game/unk_8020F3B4.c** (NonMatching): fn_8021031C 8021031C 0x60

**game/unk_802E4D84.cpp** (NonMatching): fn_802E4D84 802E4D84 0x44; fn_802E4F98 802E4F98 0x1D0 [ind2]; fn_802E5168 802E5168 0x68; fn_802E52EC 802E52EC 0x120; fn_802E5438 802E5438 0x40; fn_802E5478 802E5478 0x278; fn_802E5DD8 802E5DD8 0x20C; fn_802E6058 802E6058 0xE0; fn_802E61BC 802E61BC 0x1C; fn_802E61D8 802E61D8 0xC; fn_802E61E4 802E61E4 0x8; fn_802E61EC 802E61EC 0x20; fn_802E620C 802E620C 0x20; fn_802E622C 802E622C 0x8; fn_802E6248 802E6248 0x14; fn_802E6264 802E6264 0x858 (98.4%) [ind2]; fn_802E6B04 802E6B04 0x30

**game/unlocks.cpp** (Matching): LoadUnlockables__Fv 80018BA4 0x128 [ind5]; LoadChassisUnlocks__Fv 80018CCC 0x170 [ind6]; UnlocksInit__Fv 80018F40 0x3C

**game/valtable.cpp** (Matching): fn_80131588 80131588 0x268 [ind2]; fn_80131808 80131808 0x8; fn_80131810 80131810 0x8; fn_80131818 80131818 0x4; __ct__13ValueFileNodeFP9ValueFile 80131960 0x1B0

**game/version.cpp** (Matching): GetBuildString__Fv 8000D82C 0xC

**game/vidmode.cpp** (Matching): fn_802CE344 802CE344 0x58

**game/vidsetup.cpp** (Matching): fn_802C2A48 802C2A48 0x60; fn_802C2AA8 802C2AA8 0x70; fn_802C2B18 802C2B18 0x30; fn_802C2B48 802C2B48 0x30; fn_802C2B78 802C2B78 0x40; fn_802C2C00 802C2C00 0x60

**game/vmem.cpp** (Matching): fn_802E4C18 802E4C18 0x4; fn_802E4CA8 802E4CA8 0x34

**game/widgets.cpp** (NonMatching): fn_80218F34 80218F34 0x4C; fn_80218F80 80218F80 0x6C; fn_80218FEC 80218FEC 0x58; fn_80219284 80219284 0x1C; fn_80219440 80219440 0xB4; fn_80219568 80219568 0x74; fn_80219718 80219718 0x158 [ind3]; fn_80219870 80219870 0x90; fn_8021995C 8021995C 0x74; fn_80219A28 80219A28 0x8C; fn_80219AB4 80219AB4 0x390 [ind3]; fn_8021A5B8 8021A5B8 0x98; fn_8021A77C 8021A77C 0x88; fn_8021A808 8021A808 0xA0; fn_8021AC24 8021AC24 0x9C; fn_8021AF0C 8021AF0C 0x90; fn_8021B0CC 8021B0CC 0x68; fn_8021B1C4 8021B1C4 0x60; fn_8021B2B0 8021B2B0 0x7C; fn_8021B460 8021B460 0x44C; fn_8021B9AC 8021B9AC 0xB8; fn_8021BA64 8021BA64 0x13C; fn_8021BD98 8021BD98 0xB0; fn_8021BE48 8021BE48 0x74; fn_8021BEC0 8021BEC0 0xD0 [ind1]; fn_8021C12C 8021C12C 0x60 [ind1]; fn_8021C344 8021C344 0x134 [ind1]; fn_8021C6D0 8021C6D0 0x60 [ind1]; fn_8021C97C 8021C97C 0x5C; fn_8021CA0C 8021CA0C 0xC8 [ind1]; fn_8021CEEC 8021CEEC 0xB0; fn_8021CF9C 8021CF9C 0xA4; fn_8021D044 8021D044 0x110; fn_8021D61C 8021D61C 0x84; fn_8021D6A0 8021D6A0 0xB8; fn_8021D7C8 8021D7C8 0x9C; fn_8021E0C0 8021E0C0 0x108; fn_8021E7D4 8021E7D4 0xB0; fn_8021EA64 8021EA64 0x138 [ind1]; fn_8021F394 8021F394 0x98; fn_8021F678 8021F678 0x174 [ind2]; fn_8021FF34 8021FF34 0x108; fn_80220778 80220778 0xFC [ind1]; fn_80220E48 80220E48 0x148 [ind2]; fn_80222340 80222340 0x84 [ind1]; fn_802224B4 802224B4 0x7C; fn_80222530 80222530 0x54; fn_802226D4 802226D4 0x44; fn_802228B0 802228B0 0xE8; fn_80222BDC 80222BDC 0xB0; fn_802230A4 802230A4 0x164; fn_8022375C 8022375C 0x4; fn_80223800 80223800 0x80; fn_80223AB4 80223AB4 0xE0; fn_80223CF0 80223CF0 0x2BC [ind3]; fn_802241C8 802241C8 0xDC; fn_8022435C 8022435C 0x16C; fn_80224B54 80224B54 0x1EC; fn_80224DDC 80224DDC 0x98; fn_8022553C 8022553C 0x2EC; fn_80226630 80226630 0x80; fn_802269A8 802269A8 0x128 [ind1]; fn_80226CBC 80226CBC 0x244 [ind1]; fn_80227044 80227044 0x98; fn_802270DC 802270DC 0x168 [ind1]; fn_80227D00 80227D00 0x64; fn_80227E84 80227E84 0xD8; fn_80228418 80228418 0xA4; fn_8022855C 8022855C 0x5C; fn_802286E4 802286E4 0x148; fn_8022886C 8022886C 0x74; fn_80228F40 80228F40 0xC

**game/widgetwin.cpp** (NonMatching): fn_802160AC 802160AC 0xE8; fn_80216194 80216194 0x38; fn_802161CC 802161CC 0x78; fn_80216244 80216244 0x294 (80.4%) [ind1]; fn_802164D8 802164D8 0x154; fn_8021662C 8021662C 0x8C [ind1]; fn_802166B8 802166B8 0x50; fn_80216708 80216708 0x8; fn_80216710 80216710 0x5C; fn_80216770 80216770 0xCC; fn_8021683C 8021683C 0x80; fn_802168EC 802168EC 0x4; fn_802168F0 802168F0 0x3B8 [ind1]

**game/window.cpp** (Matching): fn_80216D14 80216D14 0x14; fn_80216D28 80216D28 0xC; fn_80216D34 80216D34 0xC; __ct__6WindowFPCclllll 80216D40 0x530; __dt__6WindowFv 80217270 0xDC [ind1]; fn_8021734C 8021734C 0xAC [ind2]; fn_802173F8 802173F8 0x80 [ind2]; fn_80217478 80217478 0x4; fn_8021747C 8021747C 0x20; fn_8021749C 8021749C 0xF4 [ind2]; fn_80217590 80217590 0x60 [ind1]; fn_802175F0 802175F0 0xE4 [ind2]; fn_802176D4 802176D4 0x60 [ind1]; fn_80217734 80217734 0x100 [ind3]; fn_80217834 80217834 0x20; fn_80217854 80217854 0x20; fn_80217874 80217874 0x12C [ind2]; fn_80217D84 80217D84 0x13C; fn_80218198 80218198 0x7C [ind2]; fn_80218214 80218214 0xDC [ind1]; fn_8021832C 8021832C 0x44; fn_80218370 80218370 0xF4 [ind1]; fn_80218464 80218464 0x6C [ind1]; fn_802184D0 802184D0 0xA4 [ind1]; fn_80218574 80218574 0x2E8 [ind3]; fn_80218C98 80218C98 0xBC [ind2]; fn_80218D54 80218D54 0xB4 [ind2]; fn_80218E08 80218E08 0x40

**game/zone.cpp** (NonMatching): fn_80141454 80141454 0x4

## 8. SDK / library units on the path

| Unit | Lib | Status | Fns on path | Bytes |
|---|---|---|---|---|
| MSL/alloc.c | MSL | Matching | 10 | 3584 |
| MSL/ansi_fp.c | MSL | Matching | 10 | 6512 |
| MSL/arith.c | MSL | Matching | 1 | 16 |
| MSL/e_atan2.c | MSL | Matching | 1 | 624 |
| MSL/e_fmod.c | MSL | Matching | 1 | 816 |
| MSL/e_pow.c | MSL | Matching | 1 | 2120 |
| MSL/e_rem_pio2.c | MSL | Matching | 1 | 920 |
| MSL/e_sqrt.c | MSL | Matching | 1 | 584 |
| MSL/extras.c | MSL | Matching | 3 | 260 |
| MSL/file_io.c | MSL | Matching | 1 | 192 |
| MSL/k_cos.c | MSL | Matching | 1 | 272 |
| MSL/k_rem_pio2.c | MSL | Matching | 1 | 5900 |
| MSL/k_sin.c | MSL | Matching | 1 | 192 |
| MSL/k_tan.c | MSL | Matching | 1 | 600 |
| MSL/math_api.c | MSL | Matching | 2 | 152 |
| MSL/math_ppc.c | MSL | Matching | 1 | 4 |
| MSL/math_sun.c | MSL | Matching | 1 | 64 |
| MSL/mbstring.c | MSL | Matching | 2 | 208 |
| MSL/mem.c | MSL | Matching | 3 | 324 |
| MSL/mem_funcs.c | MSL | Matching | 4 | 720 |
| MSL/printf.c | MSL | Matching | 11 | 8748 |
| MSL/qsort.c | MSL | Matching | 1 | 368 |
| MSL/s_atan.c | MSL | Matching | 1 | 576 |
| MSL/s_copysign.c | MSL | Matching | 1 | 44 |
| MSL/s_cos.c | MSL | Matching | 1 | 212 |
| MSL/s_floor.c | MSL | Matching | 1 | 324 |
| MSL/s_frexp.c | MSL | Matching | 1 | 136 |
| MSL/s_ldexp.c | MSL | Matching | 1 | 364 |
| MSL/s_sin.c | MSL | Matching | 1 | 216 |
| MSL/s_tan.c | MSL | Matching | 1 | 120 |
| MSL/scanf.c | MSL | Matching | 3 | 5340 |
| MSL/string.c | MSL | Matching | 9 | 956 |
| MSL/strtold.c | MSL | Matching | 2 | 5240 |
| MSL/strtoul.c | MSL | Matching | 3 | 2488 |
| MSL/w_atan2.c | MSL | Matching | 1 | 4 |
| MSL/w_fmod.c | MSL | Matching | 1 | 4 |
| MSL/w_pow.c | MSL | Matching | 1 | 4 |
| MSL/w_sqrt.c | MSL | Matching | 1 | 4 |
| MSL/wchar_io.c | MSL | Matching | 1 | 132 |
| RVL_SDK/arc/arc.c | SDK | Matching | 5 | 276 |
| RVL_SDK/ax/AX.c | SDK | Matching | 1 | 64 |
| RVL_SDK/ax/AXAlloc.c | SDK | Matching | 5 | 844 |
| RVL_SDK/ax/AXAux.c | SDK | Matching | 21 | 796 |
| RVL_SDK/ax/AXCL.c | SDK | Matching | 9 | 2592 |
| RVL_SDK/ax/AXOut.c | SDK | Matching | 5 | 1088 |
| RVL_SDK/ax/AXSPB.c | SDK | Matching | 2 | 100 |
| RVL_SDK/ax/AXVPB.c | SDK | Matching | 15 | 3060 |
| RVL_SDK/axfx/AXFXHooks.c | SDK | Matching | 2 | 32 |
| RVL_SDK/axfx/AXFXReverbHiExp.c | SDK | Matching | 6 | 2240 |
| RVL_SDK/bte/bta_dm_api.c | SDK | Matching | 3 | 200 |
| RVL_SDK/bte/bta_hh_api.c | SDK | Matching | 1 | 100 |
| RVL_SDK/bte/bte_hcisu.c | SDK | Matching | 2 | 132 |
| RVL_SDK/bte/bte_init.c | SDK | Matching | 1 | 44 |
| RVL_SDK/bte/bte_main.c | SDK | Matching | 1 | 244 |
| RVL_SDK/bte/btm_acl.c | SDK | Matching | 1 | 28 |
| RVL_SDK/bte/btm_devctl.c | SDK | Matching | 2 | 188 |
| RVL_SDK/bte/btm_discovery.c | SDK | Matching | 1 | 120 |
| RVL_SDK/bte/btm_inq.c | SDK | Matching | 1 | 4 |
| RVL_SDK/bte/btm_main.c | SDK | Matching | 1 | 92 |
| RVL_SDK/bte/btm_sco.c | SDK | Matching | 1 | 68 |
| RVL_SDK/bte/btm_sec.c | SDK | Matching | 2 | 580 |
| RVL_SDK/bte/btu_hcif.c | SDK | Matching | 1 | 396 |
| RVL_SDK/bte/btu_init.c | SDK | Matching | 2 | 120 |
| RVL_SDK/bte/btu_task1.c | SDK | Matching | 2 | 176 |
| RVL_SDK/bte/gap_api.c | SDK | Matching | 1 | 96 |
| RVL_SDK/bte/gap_conn.c | SDK | Matching | 1 | 112 |
| RVL_SDK/bte/gki_buffer.c | SDK | Matching | 8 | 3164 |
| RVL_SDK/bte/gki_ppc.c | SDK | Matching | 10 | 440 |
| RVL_SDK/bte/hcicmds.c | SDK | Matching | 1 | 92 |
| RVL_SDK/bte/hidd_api.c | SDK | Matching | 1 | 104 |
| RVL_SDK/bte/hidh_api.c | SDK | Matching | 1 | 68 |
| RVL_SDK/bte/l2c_api.c | SDK | Matching | 2 | 440 |
| RVL_SDK/bte/l2c_main.c | SDK | Matching | 1 | 276 |
| RVL_SDK/bte/l2c_utils.c | SDK | Matching | 2 | 260 |
| RVL_SDK/bte/port_api.c | SDK | Matching | 1 | 80 |
| RVL_SDK/bte/rfc_l2cap_if.c | SDK | Matching | 1 | 124 |
| RVL_SDK/bte/sdp_api.c | SDK | Matching | 2 | 924 |
| RVL_SDK/bte/sdp_main.c | SDK | Matching | 1 | 416 |
| RVL_SDK/bte/sdp_utils.c | SDK | Matching | 1 | 80 |
| RVL_SDK/dvd/dvd.c | SDK | Matching | 16 | 5564 |
| RVL_SDK/dvd/dvd_broadway.c | SDK | Matching | 18 | 5884 |
| RVL_SDK/dvd/dvdfs.c | SDK | Matching | 8 | 2600 |
| RVL_SDK/exi/EXIBios.c | SDK | Matching | 14 | 5192 |
| RVL_SDK/fs/fs.c | SDK | Matching | 18 | 3328 |
| RVL_SDK/gx/GXLight.c | SDK | Matching | 10 | 1064 |
| RVL_SDK/gx/GXMisc.c | SDK | Matching | 19 | 1756 |
| RVL_SDK/gx/GXTransform.c | SDK | Matching | 14 | 1212 |
| RVL_SDK/hbm/HBMFrameController.cpp | SDK | Matching | 2 | 88 |
| RVL_SDK/hbm/nw4hbm/db/db_assert.cpp | SDK | Matching | 5 | 1468 |
| RVL_SDK/hbm/nw4hbm/db/db_console.cpp | SDK | Matching | 6 | 2304 |
| RVL_SDK/hbm/nw4hbm/db/db_mapFile.cpp | SDK | Matching | 7 | 2056 |
| RVL_SDK/hbm/nw4hbm/lyt/lyt_animation.cpp | SDK | Matching | 1 | 12 |
| RVL_SDK/hbm/nw4hbm/lyt/lyt_resourceAccessor.cpp | SDK | Matching | 1 | 16 |
| RVL_SDK/hbm/nw4hbm/math/math_triangular.cpp | SDK | Matching | 1 | 424 |
| RVL_SDK/hbm/nw4hbm/snd/snd_AxManager.cpp | SDK | Matching | 7 | 2972 |
| RVL_SDK/hbm/nw4hbm/snd/snd_AxVoice.cpp | SDK | Matching | 2 | 448 |
| RVL_SDK/hbm/nw4hbm/snd/snd_BasicSound.cpp | SDK | Matching | 1 | 20 |
| RVL_SDK/hbm/nw4hbm/snd/snd_DisposeCallbackManager.cpp | SDK | Matching | 3 | 404 |
| RVL_SDK/hbm/nw4hbm/snd/snd_DvdSoundArchive.cpp | SDK | Matching | 1 | 64 |
| RVL_SDK/hbm/nw4hbm/snd/snd_ExternalSoundPlayer.cpp | SDK | Matching | 2 | 352 |
| RVL_SDK/hbm/nw4hbm/snd/snd_FrameHeap.cpp | SDK | Matching | 4 | 1692 |
| RVL_SDK/hbm/nw4hbm/snd/snd_InstancePool.cpp | SDK | Matching | 3 | 616 |
| RVL_SDK/hbm/nw4hbm/snd/snd_MemorySoundArchive.cpp | SDK | Matching | 3 | 288 |
| RVL_SDK/hbm/nw4hbm/snd/snd_MmlSeqTrackAllocator.cpp | SDK | Matching | 2 | 20 |
| RVL_SDK/hbm/nw4hbm/snd/snd_NandSoundArchive.cpp | SDK | Matching | 1 | 116 |
| RVL_SDK/hbm/nw4hbm/snd/snd_RemoteSpeaker.cpp | SDK | Matching | 2 | 248 |
| RVL_SDK/hbm/nw4hbm/snd/snd_RemoteSpeakerManager.cpp | SDK | Matching | 3 | 424 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SeqSound.cpp | SDK | Matching | 1 | 100 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundArchive.cpp | SDK | Matching | 11 | 844 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundArchiveFile.cpp | SDK | Matching | 9 | 1912 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundArchiveLoader.cpp | SDK | Matching | 3 | 1656 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundArchivePlayer.cpp | SDK | Matching | 10 | 5536 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundHandle.cpp | SDK | Matching | 1 | 80 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundHeap.cpp | SDK | Matching | 3 | 88 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundPlayer.cpp | SDK | Matching | 10 | 2380 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundStartable.cpp | SDK | Matching | 1 | 108 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundSystem.cpp | SDK | Matching | 2 | 484 |
| RVL_SDK/hbm/nw4hbm/snd/snd_SoundThread.cpp | SDK | Matching | 3 | 672 |
| RVL_SDK/hbm/nw4hbm/snd/snd_StrmChannel.cpp | SDK | Matching | 2 | 236 |
| RVL_SDK/hbm/nw4hbm/snd/snd_StrmSound.cpp | SDK | Matching | 1 | 92 |
| RVL_SDK/hbm/nw4hbm/snd/snd_TaskThread.cpp | SDK | Matching | 3 | 444 |
| RVL_SDK/hbm/nw4hbm/snd/snd_Util.cpp | SDK | Matching | 1 | 92 |
| RVL_SDK/hbm/nw4hbm/snd/snd_WaveSound.cpp | SDK | Matching | 1 | 92 |
| RVL_SDK/hbm/nw4hbm/ut/ut_LinkList.cpp | SDK | Matching | 5 | 1388 |
| RVL_SDK/hbm/nw4hbm/ut/ut_list.cpp | SDK | Matching | 1 | 108 |
| RVL_SDK/nand/NANDCore.c | SDK | Matching | 13 | 2984 |
| RVL_SDK/nand/NANDOpenClose.c | SDK | Matching | 6 | 832 |
| RVL_SDK/os/OSExec.c | SDK | Matching | 10 | 4888 |
| RVL_SDK/os/OSIpc.c | SDK | Matching | 3 | 40 |
| RVL_SDK/os/OSLink.c | SDK | Matching | 1 | 24 |
| RVL_SDK/os/OSReboot.c | SDK | Matching | 2 | 116 |
| RVL_SDK/os/__start.c | SDK | Matching | 5 | 372 |
| RVL_SDK/vf/nand_drv.c | SDK | Matching | 1 | 288 |
| RVL_SDK/vf/pdm_bpb.c | SDK | Matching | 3 | 1260 |
| RVL_SDK/vf/pdm_disk.c | SDK | Matching | 14 | 2216 |
| RVL_SDK/vf/pdm_dskmng.c | SDK | Matching | 3 | 116 |
| RVL_SDK/vf/pdm_mbr.c | SDK | Matching | 2 | 644 |
| RVL_SDK/vf/pdm_partition.c | SDK | Matching | 16 | 2916 |
| RVL_SDK/vf/pf_api_util.c | SDK | Matching | 3 | 248 |
| RVL_SDK/vf/pf_attach.c | SDK | Matching | 1 | 176 |
| RVL_SDK/vf/pf_cache.c | SDK | Matching | 31 | 6804 |
| RVL_SDK/vf/pf_clib.c | SDK | Matching | 7 | 760 |
| RVL_SDK/vf/pf_cluster.c | SDK | Matching | 1 | 324 |
| RVL_SDK/vf/pf_code.c | SDK | Matching | 1 | 16 |
| RVL_SDK/vf/pf_dir.c | SDK | Matching | 5 | 1536 |
| RVL_SDK/vf/pf_driver.c | SDK | Matching | 12 | 1368 |
| RVL_SDK/vf/pf_entry.c | SDK | Matching | 18 | 6084 |
| RVL_SDK/vf/pf_entry_iterator.c | SDK | Matching | 10 | 5860 |
| RVL_SDK/vf/pf_fat.c | SDK | Matching | 27 | 10480 |
| RVL_SDK/vf/pf_fat12.c | SDK | Matching | 4 | 2164 |
| RVL_SDK/vf/pf_fat16.c | SDK | Matching | 4 | 1092 |
| RVL_SDK/vf/pf_fat32.c | SDK | Matching | 4 | 1264 |
| RVL_SDK/vf/pf_fatfs.c | SDK | Matching | 1 | 4 |
| RVL_SDK/vf/pf_fclose.c | SDK | Matching | 1 | 36 |
| RVL_SDK/vf/pf_file.c | SDK | Matching | 8 | 4220 |
| RVL_SDK/vf/pf_filelock.c | SDK | Matching | 5 | 408 |
| RVL_SDK/vf/pf_finfo.c | SDK | Matching | 1 | 36 |
| RVL_SDK/vf/pf_fopen.c | SDK | Matching | 1 | 156 |
| RVL_SDK/vf/pf_fsfirst.c | SDK | Matching | 1 | 116 |
| RVL_SDK/vf/pf_fsnext.c | SDK | Matching | 1 | 36 |
| RVL_SDK/vf/pf_path.c | SDK | Matching | 27 | 11880 |
| RVL_SDK/vf/pf_sector.c | SDK | Matching | 4 | 1832 |
| RVL_SDK/vf/pf_service.c | SDK | Matching | 2 | 312 |
| RVL_SDK/vf/pf_str.c | SDK | Matching | 11 | 1380 |
| RVL_SDK/vf/pf_system.c | SDK | Matching | 3 | 124 |
| RVL_SDK/vf/pf_volume.c | SDK | Matching | 8 | 3712 |
| RVL_SDK/vf/pfk.c | SDK | Matching | 5 | 480 |
| RVL_SDK/vf/sd_drv.c | SDK | Matching | 3 | 448 |
| RVL_SDK/vf/vf_et_api.c | SDK | Matching | 8 | 984 |
| RVL_SDK/vi/i2c.c | SDK | Matching | 3 | 2588 |
| RVL_SDK/vi/vi.c | SDK | Matching | 17 | 6216 |
| RVL_SDK/wenc/wenc.c | SDK | Matching | 1 | 728 |
| RVL_SDK/wpad/debug_msg.c | SDK | Matching | 1 | 80 |
| RVL_SDK/wud/debug_msg.c | SDK | Matching | 1 | 80 |
| Runtime.PPCEABI.H/GCN_mem_alloc.c | Runtime | Matching | 2 | 368 |
| Runtime.PPCEABI.H/NMWException.cp | Runtime | Matching | 3 | 628 |
| Runtime.PPCEABI.H/__mem.c | Runtime | Matching | 3 | 308 |
| Runtime.PPCEABI.H/__va_arg.c | Runtime | Matching | 1 | 200 |
| Runtime.PPCEABI.H/global_destructor_chain.c | Runtime | Matching | 1 | 24 |
| Runtime.PPCEABI.H/runtime.c | Runtime | Matching | 10 | 1672 |
| Runtime.PPCEABI.H/strlen.c | Runtime | Matching | 1 | 28 |
| Runtime.PPCEABI.H/trk_stubs.c | Runtime | Matching | 1 | 4 |
| revolution/BASE/PPCArch.c | SDK | Matching | 24 | 368 |
| revolution/BTE/bta/sys/bta_sys_main.c | SDK | Matching | 3 | 136 |
| revolution/BTE/bta/sys/ptim.c | SDK | Matching | 1 | 76 |
| revolution/BTE/gki/common/gki_time.c | SDK | Matching | 4 | 736 |
| revolution/BTE/main/bte_logmsg.c | SDK | Matching | 6 | 308 |
| revolution/DB/db.c | SDK | Matching | 3 | 144 |
| revolution/DSP/dsp_task.c | SDK | Matching | 2 | 552 |
| revolution/DVD/dvdFatal.c | SDK | Matching | 1 | 92 |
| revolution/DVD/dvderror.c | SDK | Matching | 1 | 164 |
| revolution/DVD/dvdqueue.c | SDK | Matching | 6 | 600 |
| revolution/EXI/EXIUart.c | SDK | Matching | 2 | 820 |
| revolution/GX/GXBump.c | SDK | Matching | 9 | 1100 |
| revolution/GX/GXDisplayList.c | SDK | Matching | 2 | 348 |
| revolution/GX/GXGeometry.c | SDK | Matching | 7 | 1220 |
| revolution/GX/GXPixel.c | SDK | Matching | 12 | 1540 |
| revolution/IPC/ipcMain.c | SDK | Matching | 6 | 132 |
| revolution/IPC/ipcclt.c | SDK | Matching | 18 | 4616 |
| revolution/MEM/mem_allocator.c | SDK | Matching | 3 | 72 |
| revolution/MEM/mem_frameHeap.c | SDK | Matching | 4 | 628 |
| revolution/MEM/mem_list.c | SDK | Matching | 4 | 276 |
| revolution/MTX/mtx44.c | SDK | Matching | 2 | 388 |
| revolution/OS/OSAlarm.c | SDK | Matching | 8 | 1236 |
| revolution/OS/OSAlloc.c | SDK | Matching | 6 | 776 |
| revolution/OS/OSArena.c | SDK | Matching | 13 | 148 |
| revolution/OS/OSAudioSystem.c | SDK | Matching | 3 | 1204 |
| revolution/OS/OSCache.c | SDK | Matching | 19 | 1404 |
| revolution/OS/OSContext.c | SDK | Matching | 8 | 752 |
| revolution/OS/OSFont.c | SDK | Matching | 8 | 3216 |
| revolution/OS/OSInterrupt.c | SDK | Matching | 9 | 1168 |
| revolution/OS/OSMemory.c | SDK | Matching | 6 | 572 |
| revolution/OS/OSMutex.c | SDK | Matching | 5 | 772 |
| revolution/OS/OSPlayRecord.c | SDK | Matching | 3 | 1740 |
| revolution/OS/OSStateFlags.c | SDK | Matching | 2 | 504 |
| revolution/OS/OSStateTM.c | SDK | Matching | 6 | 996 |
| revolution/OS/__ppc_eabi_init.c | SDK | Matching | 5 | 268 |
| revolution/SC/scapi.c | SDK | Matching | 13 | 960 |
| revolution/SC/scapi_prdinfo.c | SDK | Matching | 2 | 496 |
| revolution/SC/scsystem.c | SDK | Matching | 14 | 4420 |
| revolution/SI/SISamplingRate.c | SDK | Matching | 2 | 224 |
| revolution/VF/pf_init_prfile2.c | SDK | Matching | 1 | 64 |
| revolution/VF/pf_w_clib.c | SDK | Matching | 3 | 148 |
| revolution/ai/ai.c | SDK | Matching | 6 | 1056 |
| revolution/axfx/AXFXReverbHi.c | SDK | Matching | 2 | 136 |
| revolution/dsp/dsp.c | SDK | Matching | 7 | 384 |
| revolution/dsp/dsp_debug.c | SDK | Matching | 1 | 80 |
| revolution/gx/GXAttr.c | SDK | Matching | 12 | 3328 |
| revolution/gx/GXFifo.c | SDK | Matching | 19 | 2860 |
| revolution/gx/GXFrameBuf.c | SDK | Matching | 12 | 2244 |
| revolution/gx/GXInit.c | SDK | Matching | 3 | 3908 |
| revolution/gx/GXPerf.c | SDK | Matching | 3 | 2272 |
| revolution/gx/GXTev.c | SDK | Matching | 14 | 1472 |
| revolution/gx/GXTexture.c | SDK | Matching | 21 | 3564 |
| revolution/homebuttonLib/HBMAnmController.cpp | SDK | Matching | 2 | 224 |
| revolution/homebuttonLib/HBMBase.cpp | SDK | Matching | 28 | 21988 |
| revolution/homebuttonLib/HBMController.cpp | SDK | Matching | 19 | 2000 |
| revolution/homebuttonLib/HBMRemoteSpk.cpp | SDK | Matching | 7 | 688 |
| revolution/homebuttonLib/nw4hbm/db/db_directPrint.cpp | SDK | Matching | 4 | 952 |
| revolution/homebuttonLib/nw4hbm/lyt/lyt_arcResourceAccessor.cpp | SDK | Matching | 2 | 320 |
| revolution/homebuttonLib/nw4hbm/lyt/lyt_common.cpp | SDK | Matching | 1 | 44 |
| revolution/homebuttonLib/nw4hbm/lyt/lyt_drawInfo.cpp | SDK | Matching | 1 | 116 |
| revolution/homebuttonLib/nw4hbm/lyt/lyt_group.cpp | SDK | Matching | 1 | 212 |
| revolution/homebuttonLib/nw4hbm/lyt/lyt_layout.cpp | SDK | Matching | 3 | 756 |
| revolution/ipc/ipcProfile.c | SDK | Matching | 2 | 312 |
| revolution/ipc/memory.c | SDK | Matching | 4 | 1316 |
| revolution/kpad/KPAD.c | SDK | Matching | 23 | 12624 |
| revolution/mem/mem_expHeap.c | SDK | Matching | 1 | 176 |
| revolution/mem/mem_heapCommon.c | SDK | Matching | 3 | 1132 |
| revolution/mtx/mtx.c | SDK | Matching | 5 | 540 |
| revolution/mtx/vec.c | SDK | Matching | 4 | 236 |
| revolution/nand/NANDCheck.c | SDK | Matching | 2 | 452 |
| revolution/nand/nand.c | SDK | Matching | 11 | 1716 |
| revolution/os/OS.c | SDK | Matching | 15 | 4516 |
| revolution/os/OSError.c | SDK | Matching | 4 | 1092 |
| revolution/os/OSMessage.c | SDK | Matching | 1 | 200 |
| revolution/os/OSReset.c | SDK | Matching | 7 | 1200 |
| revolution/os/OSRtc.c | SDK | Matching | 6 | 2112 |
| revolution/os/OSSemaphore.c | SDK | Matching | 3 | 296 |
| revolution/os/OSSync.c | SDK | Matching | 1 | 96 |
| revolution/os/OSThread.c | SDK | Matching | 19 | 5224 |
| revolution/os/OSTime.c | SDK | Matching | 6 | 1044 |
| revolution/pad/Pad.c | SDK | Matching | 3 | 704 |
| revolution/si/SIBios.c | SDK | Matching | 7 | 1896 |
| revolution/vi/vi3in1.c | SDK | Matching | 3 | 1636 |
| revolution/wpad/WPAD.c | SDK | Matching | 32 | 14032 |
| revolution/wud/WUD.c | SDK | Matching | 15 | 2576 |
| sdk_mp3/mp3decode.c | mp3 | Matching | 3 | 4348 |
| sdk_mp3/mp3dequant.c | mp3 | Matching | 1 | 5112 |
| sdk_mp3/mp3frame.c | mp3 | Matching | 6 | 6676 |
| sdk_mp3/mp3layer3.c | mp3 | Matching | 40 | 24004 |
