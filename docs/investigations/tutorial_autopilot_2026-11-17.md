# Tutorial autopilot (testing tool, 2026-11-17 night)

`NICECRASH_AUTOPILOT` (patches/autopilot.patch.txt + sdk/input.cpp) drives the player's truck for scripted test runs.
Goal: get scripted runs through the whole Tutorial. Status: every Basics 1 and Basics 2 lesson passes **except Power
Item** (out of time; see below).

## Why it failed before
- The patch took "airborne" from the Hybrid driver's byte 0x24, which is not that flag in the decomp pin cf6990eea.
  It read 1 on every frame. As a result the player's driver always had the truck, the AI never steered (steering
  stayed 0), turbo was held all the time (OVERHEAT, then a 6.25 s lockout), and after 300 "air" frames the gas was
  released (the truck parked at ~5 mph). Now: airborne = no wheel on the ground (CarBody 0x2620 == 0).
- In menus, the truck behind the lesson list still runs its driver and reads as "airborne" at speed ~0.5. The
  autopilot held 2 (gas) and the D-pad's up (turbo) there. The lesson list's Up wraps from Drive to Exit, so the
  cursor walked to Exit and 2 picked it: the runs looped Tutorial menu -> Basics 1 -> Exit. Now the autopilot lets go
  when the truck is airborne below speed 2.

## How the Tutorial menus behave (decomp tutorial.cpp / tutmenu.cpp)
- Lesson list (`ChalList`): it opens on Drive (`sel = 1`). After the current lesson is passed, the cursor moves to the
  newly unlocked lesson; after the last lesson of a set it moves to Exit. After a fail it stays on the lesson.
  Locked rows are skipped, and the list wraps both ways.
- Progress is the profile pref `tutorial_level` (1 + lessons passed across all sets). A set unlocks when every
  lesson before it is passed. Basics 1 is "mandatory": with it done, a new profile goes to the main menu (Excite
  Race, Challenge, Versus, Tutorial, ...) instead of straight into the Tutorial.
- So pressing 2 every 200 frames walks through all lessons in order, as long as the autopilot stays off in the
  menus.
- The scratch save (build/t3/nand_tut) is never written by these runs (the game only saves progress at a proper
  shutdown), so each run starts from a new profile.

## Switches
- `NICECRASH_AUTOPILOT=1` (recommended for the Tutorial): the AI drives on the ground with its own (cautious) turbo.
  The player's driver takes the truck in the air (turbo tapped on air frames 2-10, AI steering kept).
- `=2`: three ground frames in four go to the player's driver, with gas, the AI's last steering, and turbo by heat
  (off at 95%, on below 85%; released on air frames 1-2 so a press right after takeoff counts as a turbo jump).
  Faster, but it overshoots Short Jump and fails Air Turning. Used by the green-flash repro (Tree Run).
- `=3`: the AI drives on the ground, with turbo by heat (aicar.cpp hook, `port_ai_turbo`). Faster than 1, but it
  keeps the engine hot, and Turbo Jump then runs out of time.
- `NICECRASH_AUTOPILOT_LEAN`: unset or `auto` = in the air, keep the truck level with its flight path (lean = 6 x
  dot(car up 0x48, velocity direction 0x118)). This is what makes Short Jump pass: it noses down after the ledge and
  doesn't overshoot (the user's tip: turbo to the jump, tilt down and right, turbo again after landing). `up` /
  `down` force a lean; `none` turns it off.
- `NICECRASH_TUTLEVEL=<n>`: the profile's Tutorial progress reads as at least lesson n (1 Drive, 2 Turbo, 3 Jump,
  4 Short Jump, 5 Air Turning, 6 Cooling Down, 7 Turbo Jump, 8 Landing Boost, 9 Terrain Morph, 10 Power Item),
  and the lesson list opens on the current lesson.
  - With n <= 5 the profile goes straight into Basics 1 (the script's 2 presses work unchanged).
  - With n >= 6 it goes to the main menu: press DOWN twice (Tutorial), then 2.
- `NICECRASH_APLOG=1`: one line per game frame with the car's heat, overheat timer, turbo held/active, airtime,
  wheels, speed, steering and pitch error.
- `NICECRASH_LINK_OUT=<exe>` (tools/gametrial/link.py): link a test build to another exe, because the launcher's
  build/NiceCrashGame.exe is locked while it runs. build/t3/runcode.ps1 runs `$env:NC_EXE` when it is set.

## Results (build/t3/aptut, contact sheets per run)
- Mode 1 + auto lean, fresh profile (`full`): Drive, Turbo, Jump, Short Jump, Air Turning, Cooling Down, Turbo
  Jump, Landing Boost and Terrain Morph all "Good Job!". Then Power Item repeats.
- Power Item: "Try Again, you ran out of time" (20.2 s) in modes 1, 2 and 3. The truck follows the AI's racing line
  and is still short of the end line when time runs out; turbo (mode 3) doesn't change where. The lesson text ("if
  you hit a tree while powered...") suggests the item is meant to be picked up for a route through the trees. The
  AI's line probably misses it. Next step: steer to the item (pickup list in betarace.cpp, `pow_item`), or ask the
  user which way the lesson is meant to be driven.

## Running it
`build/t3/aptut/tut.sh <tag> <inputs> <secs> [VAR=value ...]` copies the Tutorial save, runs the hidden game with
screenshots every 150 frames, and keeps the log; `build/t3/aptut/sheet.ps1 <tag> <from> <to> <step>` makes a
contact sheet. Inputs: `790:2,950:2,1150:DOWN,1250:DOWN,1350:2` then `2` every 200 frames.
