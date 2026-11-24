# Content crash sweep (2026-11-23)

Goal: play as much of the game as scripts + `NICECRASH_AUTOPILOT` can reach before the tester does, and catch crashes
(`[crash]`), panics (PANIC/ABEND), `[unimpl]` stub exits (code 99), hangs, and visual breakage.

## Setup
- Test exe: `build/NiceCrashTest.exe` (`NICECRASH_LINK_OUT`, port master 3d202f1, decomp pin cdc03e26b6). The
  launcher's exe is never rebuilt.
- Runner: `build/t3/sweep/sw.sh <tag> <inputs> <secs> [VAR=value ...]`. It runs the game hidden and windowed (runcode.ps1)
  from a copy of the scratch save `build/t3/nand_tut`, with scratch settings and screenshots every 150 frames. It
  prints the exit status and any crash, panic or unimpl lines. Contact sheets: `build/t3/sweep/sheet.ps1 <tag> <from> <to> <step>`.
- A new profile with `NICECRASH_TUTLEVEL=6` has the mandatory Basics 1 done, so it opens on the main menu: Excite
  Race, Challenge, Versus (greyed with one remote), Tutorial, Options, Records.
- Menu inputs: `790:2,950:2,1150:DOWN,1250:DOWN,1350:2`, then 2 every 200 frames until ~3800 (profile name), then
  the route below.
- Custom music: `build/t3/sweep/music/track1.mp3` (one MP3 copied from the user's Music folder), `NICECRASH_MUSIC_DIR`.

## Coverage and findings
No crash, panic, unimpl exit or hang in any run so far. Every run ended by the time limit with the game still
running.

| Run | Content | Result |
|---|---|---|
| er1, er2 | Excite Race, Bronze cup: Mexico "Greenhorn Gulch" (1st, grade B), Fiji "Atoll Hop", later cup races (forest track, 3 laps) | ok: race, results, grade, New Achievement Points, colour unlock ("Victory Red"), Next/Retry menu |
| se1 | Excite Race, Super Excite / Silver cup | locked for a new profile (expected); cup screen ok |
| ch1, ring2, crush2 | Challenge, Normal: Gate Canada, Gate China | ok: gates, MISSED, OUT OF TIME, time bonus, grade, high scores, Next/Retry/View Replay |
| vs1 | Versus with a scripted remote 2 (`NICECRASH_INPUT2`, new) | ok: player 2 truck/colour select, 2-player split-screen race |
| sv1, reload | save during play, then boot from that save | ok after the fix below: "Wrote 344064b to foo.bar"; the reload skips profile creation and races |
| Tutorial | all lessons (see tutorial_autopilot_2026-11-17.md) | ok, except Power Item (autopilot route, not a bug) |
| er3 | Excite Race on the scratch profile: Scotland "Sylvan Glen" (3 laps) | ok |
| rp1 | Excite Race Fiji "Atoll Hop" (grade C), then View Replay | ok: replay with camera/pause/restart/exit overlay, back to Next/Retry/View Replay |
| opt3, mus2 | Options: Music Volume, SFX Volume, Race Music (All Default / All Random), Erase Data (not pressed) | ok; the values change; backing out returns to the main menu / profile select |
| rec2 | Records: Achievements, Best Stunts, Best Times | ok (Best Stunts opened) |
| snd1 | truck select: Boulder, Firefly, Wolf; Summit and Serpent locked | ok |
| ring4, crush4 | Select Challenge below Gate China | Ring/Crush rows don't take the cursor on this profile (locked until Gate progress, it seems) |
| mus1/2, snd1 | custom soundtrack with `NICECRASH_MUSIC_DIR` = one MP3 | the folder is found and scanned ("VF: music folder ... found", `mp3: 1 tracks`, `0: "track1.mp3"`, `mp3: starting`). NOT verified: choosing it in a track's "+ Sound Options" (the PLUS press came before the track info screen) |

### Found and fixed
- **Saves "never written" was a test-script bug, not a game bug.** The scripts passed `cygpath -w $PWD/...`
  unquoted. The path has a space, so NICECRASH_NAND held two lines and every NAND call failed. The game's
  stdout said `rmemcard: NANDCreateAsync failed (-1)` and nothing reached stderr. sdk/nand.cpp now logs the folder
  and failed creates/opens (aa91e8b). Runs with the right path write and reload the save.
- Scripted remote 2 for Versus (aa91e8b).

### Noted, not chased (game's own debug prints, every race)
`gArtGet(blinker*.img / bar*.art): resource not found`, `texture: missing texture LTube*.tex`,
`artanim: unmapped string`, `betarace: can't open opt*_kenya.val`, `res: kenya.trk: never referenced ...`.
These look like retail leftovers (nothing visibly missing on screen). Compare with Dolphin's log if a visual gap
turns up.

### Not covered yet (next steps)
- Track "+ Sound Options" / choosing the custom MP3: route = main menu 2, difficulty 2, cup 2, truck 2, then PLUS
  on the track info screen. Find the exact frame with dense shots first.
- Super Excite, Silver/Gold/Platinum cups, other locations (Kenya, Finland, China ... as races), Super challenges, Ring
  and Crush challenges: locked on the available profiles. These need a profile with progress: play the cup through
  (the autopilot gets grade B in Mexico but misses Fiji's 120 stars), or a save from a real playthrough.
- Erase Data (scratch save only), HOME menu during a race, pause menu Restart/Exit, long sessions (20+ races in one
  process, for leaks).

### Script notes
- With working saves, `build/t3/nand_tut` boots to Choose a Profile with one profile. The main menu is at ~frame
  1100 with `790:2,950:2`. Main menu order: Excite Race, Challenge, (Versus: skipped with one remote),
  Tutorial, Options, Records.
- Challenge: difficulty (Normal/Super), truck colour, Select Challenge list (Gate Canada/China, Ring Finland/Fiji,
  Crush Mexico/Scotland).
- Run at most 3 test games at once: 4 drop each to ~35 fps.
