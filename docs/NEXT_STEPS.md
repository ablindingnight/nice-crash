# Next steps (written 2026-11-17, end of night)

## Where things stand
- Port master is at `3c8b68f` (nothing pushed); decomp pin `cf6990eea`.
- Live tester zip: `NiceCrash-2026-11-17-9e9d7ae.zip` on mossywilliams.com behind the hidden "byte" link
  (beneath.html). The brother has downloaded it; his gameplay report is pending.
- Commits AFTER the tester zip, not yet in `build/NiceCrashGame.exe` or any zip:
  - `1371724`: truck paint reflections (indirect texture lookups use the map's own filter and wrap).
  - `aba7d75`: smoother menus and HUD. A live "Sharp" upscale (the default) plus a "Menus and HUD" AI step under
    Enhance my game.
  - `874b61a`: the Tutorial autopilot now passes Basics 1 and 2 up to Terrain Morph.

## To do, in order
1. **DONE 2026-11-17 late** (build/NiceCrashGame.exe + launcher rebuilt at 91d7e87): Sharp menus are crisper than
   "game" at 1920x1080, the truck paint is smooth (no blocks), and 33 lesson starts + 3 HOME opens in one 400 s run ran
   with no crash or PANIC. Still worth a look: ground ripples/overlays (indirect lookups) and an Excite Race with AI.
   Original step: **Rebuild and check the three commits above together.** Full `sh tools/gametrial/link.sh` and `sh build.sh`,
   then:
   - the truck from behind in the Tree Run: smooth reflections like the user's reference
     (`build/t3/reflhunt/reference_truck_reflections.webp`);
   - the ground ripples and overlay effects that use indirect lookups (see
     `docs/investigations/truck_reflections_2026-11-17.md`);
   - the menus and HUD at 1920x1080 with Sharp on and off (`docs/investigations/ui_textures.md`);
   - 3+ race loads in one run plus HOME open/close, with no PANIC.
2. **DONE 2026-11-23** (3980be1): pinned cdc03e26b6; patches follow the decomp's renames; nanscan debug patch removed;
   6 min play-test clean. Original step: **Re-pin the decomp** (ask TC for the latest good commit). Pending, all PC-safe:
   - `2c00b4efd`: fn_801DCEC0 prototype;
   - `17b044b8c`: StackAlloc string;
   - `08dc91f44` + `8570465f0`: the MP3 tables are now generated at build time (the port doesn't build sdk_mp3);
   - `64d28092c`: davmap `% 0` guard.
   After pinning: build, play-test, and check that every port patch still applies once.
3. **New tester zip** from that commit (`tools/package/make_package.py --zip`, then the scan). Get the user's OK
   before it goes online (TC uploads through the Hostinger connector). Test boots run windowed and off-screen.
4. **Autopilot:** steer to the Power Item, then the remaining lessons
   (`docs/investigations/tutorial_autopilot_2026-11-17.md`). Testing tool only.
5. **DONE 2026-11-23**: launcher Graphics page has the "Sharpen videos" row. Original: add the "Sharpen videos" row (`graphics.video_sharpen`) to the launcher's Graphics page (it's only
   in the in-game menu today).
6. **Gameplay smoothing beyond menus:** the HUD and gameplay textures via the same pack system (ui_textures.md
   covers the next steps).
7. **DONE 2026-11-23 (653277e)**: playback verified by recording; choosing it from the Options "Track Music" menu is
   not yet checked by hand. Folder: audio.music_folder, default %USERPROFILE%\Music\Nice Crash (an EXCITE TRUCK
   subfolder first, like the SD card). FOR THE NEXT TESTER ZIP: include the decomp's src/sdk_mp3 sources and
   tools/mp3/gentables.py (the package excluded sdk_mp3 while it carried DOL tables; they're generated locally now),
   then rescan. Original: **MP3 custom soundtrack** (the user wants it). The game's SD-card MP3 player becomes a music folder on PC:
   implement the TMCCMP3*/VF* stubs, build the decomp's sdk_mp3 with its generated tables (prep must run the
   decomp's `tools/mp3/gentables.py` like datagen), and turn the SD flag on when the folder has songs. Background:
   G7's notes, `handoff/dossier/port_support/G7_STUBS_TUTORIAL.md` in the matching repo.
8. **Remaining stubs** (49) that G7 ranked medium/low: alarms that never fire (OSSetAlarm), the WPAD simple-sync
   (Connect screen), and HOME's dry reverb. See the same G7 doc.

9. **VERY LAST (user 2026-11-23): a Triforce-style setup exe.** One small NiceCrashSetup.exe (like
   git.triforceproject.com/triforce's TriforceSetup.exe): installs to the user folder without admin, makes shortcuts,
   downloads the build tools (zig, Python, dtk, optional Real-ESRGAN) from their official sources on first run (confirm
   the sources with the user), then the launcher does disc + build as today; "Welcome back -> Update" from GitHub
   releases (github.com/ablindingnight/nice-crash), keeping saves and settings. Game files stay on the player's PC.

## Reminders
- Never edit the matching repo `excite-truck`. Never commit Nintendo data. Ask before downloading. Don't push
  unless asked.
- Bash sees a stale sandboxed copy of `%LOCALAPPDATA%\NiceCrash`. Read the user's real logs with PowerShell.
- Bash heredocs mangle backslashes (`\n` in C strings): use the Edit tool for those lines.
- Test games run hidden: `build/t3/runcode.ps1` (offscreen), scratch saves via NC_NAND/NC_SETTINGS. Copy
  `build/t3/nand_tut` for a profile with the Tutorial. Never rebuild `build/NiceCrashGame.exe` while the user is
  playing.
- Reaching a port stub exits with code 99 (`runtime/unimpl.cpp`); NICECRASH_UNIMPL=continue lists them.
- Debug switches: NICECRASH_LUMLOG (green-flash detector), NICECRASH_SKIPINVIS, NICECRASH_NORINGLE,
  NICECRASH_PAUSETEST / NICECRASH_NOPAUSECLOCK, NICECRASH_ENHANCE_PLAYTEST (launcher --enhance),
  NICECRASH_VIDEODUMP_MOVIE, NICECRASH_AUTOPILOT=1|2.

## Drop the build-on-the-tester's-PC step (no zig/Python in the package), started 2026-11-23
Why: the package ships source + zig + Python because NiceCrashGame.exe compiled in tables made from the player's
main.dol. Goal: one prebuilt NiceCrashGame.exe with no Nintendo data, package ~156 MB -> a few MB, first run in seconds.

What read main.dol at build time, and what it does now:
- DUMP_DATA tables (decomp tools/datagen.py; 46 in the game, 5 in the HOME menu library) and the MP3 decoder's 407
  tables (decomp tools/mp3/gentables.py): **DONE 2026-11-23.** tools/gametrial/dumptables.py rewrites each definition
  into a view at its Wii address (`#define lbl_X (*(T (*)[N])0x8xxxxxxx)`, with a sizeof check); runtime/dolload.cpp
  already loads main.dol's data sections there, and the multi-byte fields' swap runs (from the marker types) go into
  port_dolfix_table via build/dumptables/*.tsv. Checked: every 16-byte window of those tables, in both byte orders,
  searched in the exe: the old exe held 7239, the new one 0 (4 hits are a plain 00 01 02 .. ramp); 110 s Tutorial
  autopilot run clean (profile keyboard, lesson, HUD); a custom-soundtrack MP3 recorded sample-identical to the old
  exe (and louder than a control run without it).
- link.py's retail-vtable and pointer-word recognition: still reads main.dol, but only at OUR build, and it writes
  host addresses, not DOL bytes (today: 0 vtables, 5 data-pointer words). Fine for a prebuilt exe.
  (gentables.py still needs a main.dol on the build machine too; only its names and shapes are kept.)

Next, in order:
1. scan_package.py: run check 3 over the game exe in both byte orders (the old exe held the tables little-endian,
   which the big-endian run check couldn't see). The old exe must fail it, the new one pass.
2. make_package.py `--prebuilt`: ship build/NiceCrash.exe, build/NiceCrashGame.exe (+ import.exe, enhance.exe), dtk,
   assets, licences and the README; no source, zig or Python. Stamp the game exe with the port commit + decomp pin.
3. Launcher: when a prebuilt NiceCrashGame.exe is next to it, the first-run checklist is disc check + extract + import
   only (no build). Keep the build path for source checkouts.
4. Package scan, then a clean-machine style test (fresh %LOCALAPPDATA%, offscreen), then the user's OK before upload.
   Repo is public since 2026-11-22: github.com/ablindingnight/nice-crash (CC0). Releases: attach the tester zip.
