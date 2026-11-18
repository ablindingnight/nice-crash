# Menu/HUD smoothing (port commit aba7d75, not pushed)

Comparison shots live outside the repo (shared side-session notes, ui_smoothing_shots/): game screenshots, never in git.

## What's there
- **Live tier**: `graphics.ui_filter` = game | smooth | sharp, **default sharp** (restart setting). The rows are in the
  in-game menu and in the launcher's Graphics page. sdk/uiscale.cpp: non-mip textures (menus, HUD, fonts, sky/hill cards)
  are upscaled at upload: 4x up to 256 px, 2x up to 512 px, and 1024 px atlases only get mips. Catmull-Rom is clamped to
  the 2x2 cell, and "sharp" tightens hard edges. Every upscaled texture gets mips and LOD bias -0.5.
  Tuning env: NICECRASH_UISCALE_GAIN (0.75), _BIAS (-0.5), _LOG.
  Cost: ~160 uploads, ~2.8 s CPU over a menus+race session, worst 47 ms, all at load time.
- **Key finding**: the 1024 px menu font atlases are drawn at ~0.45x at 1080p. The original's blockiness there is
  minification without mips, not magnification. Mips + bias fix that, and texpack replacements of non-"_m" art now get the
  same bias.
- **AI tier**: launcher Graphics > Enhance my game > "Menus and HUD" (tasks::StartEnhanceUi, same checklist screen
  as the intro). The game runs hidden on a copy of the player's NAND with NICECRASH_TEXPACK_DUMP, enhanced assets off, and
  NICECRASH_AUTOPILOT=1. It walks the title, profile 1, Tutorial, Options (never near Erase Data), Records and an Excite
  Race (difficulty, level, track, truck, race). NICECRASH_QUIT_AT=7200 (new, sdk/vi.cpp) ends it. That takes about 2m20s
  and dumps ~407 textures. Then `enhance.exe --textures <dump> --out <data>\textures\REXE01\enhanced` runs
  realesr-animevideov3 (x4 up to 512 px, x2 for 1024 px). It clamps to the source cell range (no halos, exact
  alpha 0/255) and skips files already in the pack. 254 textures take 18 s and give 89 MB.
  realesrgan-x4plus-anime bloats small glyphs, so animevideov3 is used even when "Best quality" is picked.
- Remove deletes only `<data>\textures\REXE01\enhanced`.

## Verified (isolated settings/data/NAND in scratch, test exe linked privately, hidden windows)
- 1920x1080 shots of the title, profiles, main menu, Tutorial, Basics 2 and race HUD for game/smooth/sharp/AI: nothing broken.
- The launcher builds warning-free and its Graphics page renders (--ui-shot). The dump walk with the exact launcher env
  exits 0 at 7200, and enhance.exe from the launcher build made the pack the game loaded (254 replacements).
- Not run: the launcher's own Enhance button end to end. `build/NiceCrashGame.exe`, `build/NiceCrash.exe` and
  `build/enhance.exe` were NOT rebuilt (the user may be playing), so a rebuild is needed to ship.
- With a save that has no profile, the walk only reaches the title and the profile/name screens.

## Seen in passing
- Truck select screen renders a black background (walk shots f4200-4500). Not investigated.
