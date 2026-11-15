# Boot-path gaps for the matching sessions (2026-11-15)

Source: BOOT_PATH.md section 7 (game functions statically reachable from `__start` to the title screen's first
GXCopyDisp) joined with excite-truck build/REXE01/report.json at 07:00. Regenerate with the same join when
report.json moves.

What matters to the port (route A, decomp source compiled natively):
- **Functions below 100%** (list below): the source exists but may not behave exactly like retail. These are the
  real risk on the boot path. "100.0%" entries are rounding (99.95%+).
- **NonMatching units** (the table): their functions mostly match already, but the unit links asm. For the port this
  only matters if the unit's source still relies on something the matching build hides (e.g. data owned by asm,
  `asm` blocks). Linking these units (Matching) also proves their data/rodata, which the port needs.
- Units already ported to the host by hand (port/lib): res.cpp (loader), texture.cpp + drtex.cpp (decode path).
  They still need matching for the full port, but are not blocking milestone 2.

Suggested queue order: gx.cpp, texture.cpp, drtex.cpp, gtext.cpp, uiapi.cpp, widgets.cpp, res.cpp, then the rest by
bytes. gx/texture/drtex/gtext are what milestone 2 (GX to the host renderer) reads first.

Total below 100%: 32 functions, 43104 bytes, in 17 units

| unit | status | path fns | path bytes linked as asm | fns below 100% |
|---|---|---|---|---|
| game/gx.cpp | NonMatching | 82 | 17612 | 2 |
| game/res.cpp | NonMatching | 81 | 16460 | 0 |
| game/widgets.cpp | NonMatching | 72 | 16168 | 0 |
| game/uiapi.cpp | NonMatching | 21 | 12372 | 2 |
| game/unk_80116BD4.c | NonMatching | 39 | 10576 | 2 |
| game/texture.cpp | NonMatching | 45 | 9144 | 4 |
| game/gtext.cpp | NonMatching | 26 | 7992 | 6 |
| game/drtex.cpp | NonMatching | 25 | 7760 | 4 |
| game/sdmp3.cpp | NonMatching | 14 | 7724 | 1 |
| game/model.cpp | NonMatching | 30 | 5600 | 1 |
| game/drsurf.cpp | NonMatching | 12 | 4872 | 1 |
| game/unk_802E4D84.cpp | NonMatching | 17 | 4692 | 1 |
| game/tevbink.cpp | NonMatching | 15 | 3844 | 0 |
| game/loading.cpp | NonMatching | 9 | 3052 | 1 |
| game/widgetwin.cpp | NonMatching | 13 | 3016 | 1 |
| game/gxcanvas.cpp | NonMatching | 20 | 2096 | 0 |
| game/filecd.cpp | NonMatching | 23 | 2036 | 0 |
| game/inputbind.cpp | NonMatching | 8 | 1844 | 0 |
| game/swfanim.cpp | NonMatching | 4 | 1800 | 1 |
| game/namedcam.cpp | NonMatching | 3 | 1512 | 1 |
| game/soundmgr.cpp | NonMatching | 11 | 1404 | 2 |
| game/racing.cpp | NonMatching | 5 | 1296 | 0 |
| game/dashhelper.cpp | NonMatching | 2 | 1040 | 0 |
| game/musicplr.cpp | NonMatching | 4 | 1032 | 1 |
| game/sound.cpp | NonMatching | 6 | 880 | 0 |
| game/popwarn.cpp | NonMatching | 4 | 712 | 1 |
| game/tristrip.cpp | NonMatching | 4 | 472 | 0 |
| game/bonemgr.cpp | NonMatching | 2 | 384 | 0 |
| game/ovlayout.cpp | NonMatching | 3 | 320 | 0 |
| game/glare.cpp | NonMatching | 1 | 248 | 0 |
| game/cammgr.cpp | NonMatching | 1 | 124 | 0 |
| game/unk_8020F3B4.c | NonMatching | 1 | 96 | 0 |
| game/stub18.cpp | NonMatching | 2 | 8 | 0 |
| game/recordmgr.cpp | NonMatching | 1 | 4 | 0 |
| game/roadsnd.cpp | NonMatching | 1 | 4 | 0 |
| game/zone.cpp | NonMatching | 1 | 4 | 0 |

### game/gx.cpp (NonMatching)
- `fn_802CF10C` 0x802CF10C size 0x49C — 94.1%
- `fn_802D1C04` 0x802D1C04 size 0x614 — 96.5%

### game/uiapi.cpp (NonMatching)
- `fn_802141A0` 0x802141A0 size 0x51C — 98.7%
- `fn_802123B8` 0x802123B8 size 0x1DE8 — 99.8%

### game/unk_80116BD4.c (NonMatching)
- `fn_80116BD4` 0x80116BD4 size 0xF0 — 96.6%
- `fn_8011770C` 0x8011770C size 0xFAC — 100.0%

### game/texture.cpp (NonMatching)
- `fn_802CC10C` 0x802CC10C size 0x1BC — 97.6%
- `fn_802CB82C` 0x802CB82C size 0x370 — 97.8%
- `Alloc__10SimpleHeapFl` 0x802CC2C8 size 0x13C — 97.9%
- `fn_802CC770` 0x802CC770 size 0x414 — 98.6%

### game/gtext.cpp (NonMatching)
- `fn_802346C8` 0x802346C8 size 0x424 — 91.2%
- `fn_80231780` 0x80231780 size 0x35C — 92.2%
- `fn_8023293C` 0x8023293C size 0x234 — 99.1%
- `fn_8023423C` 0x8023423C size 0x1E0 — 99.5%
- `fn_80233BF0` 0x80233BF0 size 0x1E8 — 99.5%
- `fn_8023400C` 0x8023400C size 0x230 — 99.5%

### game/drtex.cpp (NonMatching)
- `fn_802D78D4` 0x802D78D4 size 0x288 — 65.5%
- `fn_802D81C4` 0x802D81C4 size 0x4F8 — 92.9%
- `fn_802D86BC` 0x802D86BC size 0x3C8 — 95.4%
- `fn_802D8DCC` 0x802D8DCC size 0xD0 — 96.7%

### game/sdmp3.cpp (NonMatching)
- `fn_802EAB28` 0x802EAB28 size 0xCAC — 89.3%

### game/model.cpp (NonMatching)
- `mrModelFixup` 0x802C61BC size 0x8C8 — 85.5%

### game/drsurf.cpp (NonMatching)
- `fn_802D6C78` 0x802D6C78 size 0x8D4 — 96.3%

### game/unk_802E4D84.cpp (NonMatching)
- `fn_802E6264` 0x802E6264 size 0x858 — 98.4%

### game/loading.cpp (NonMatching)
- `DrawLoadingScreen__Fif` 0x800209A0 size 0x8B8 — 99.1%

### game/widgetwin.cpp (NonMatching)
- `fn_80216244` 0x80216244 size 0x294 — 80.4%

### game/swfanim.cpp (NonMatching)
- `LoadFile__7SwfAnimFPCc` 0x802390B4 size 0x598 — 97.4%

### game/namedcam.cpp (NonMatching)
- `fn_80276098` 0x80276098 size 0x4D4 — 98.0%

### game/soundmgr.cpp (NonMatching)
- `__ct__12SoundManagerFP5Mixer` 0x80207990 size 0x1D0 — 98.0%
- `Add__12SoundManagerFP9GameSound` 0x80208408 size 0x1E0 — 98.8%

### game/musicplr.cpp (NonMatching)
- `fn_8020A100` 0x8020A100 size 0x268 — 97.4%

### game/popwarn.cpp (NonMatching)
- `fn_80246AE8` 0x80246AE8 size 0x160 — 97.8%

