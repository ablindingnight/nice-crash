# Green flashes in races (FIXED 2026-11-17 late)

## Cause and fix
Vertex arrays the game builds with the CPU each frame were sometimes read in the wrong byte order.
`sdk/gxrender.cpp` decides a draw's array byte order from its float POSITIONS (`ArraysHostOrder`: plausible as
big-endian model data or as host-order CPU data) and applied that one answer to every array of the draw. Two cases
went wrong, both with arrays from the game's "gc:dyndata" frame ring (`gx.cpp fn_802CFF64`, 0x177000 bytes):
- Positions in the ring (drsurf.cpp skinned / deformed surfaces: `Skin2` output, the trees' morph flush) judged
  big-endian. The judgement scores only the first 12 vertices and is cached per array ADDRESS per frame; on a tie it
  reuses an older answer for that address, and ring addresses are reused by different arrays every frame. A misread
  float is mostly tiny/NaN (nothing visible), but now and then a vertex lands far away: the long flat bars, arches and
  stretched trunks converging on one point. Their flat colour is probably one texel of the tree texture stretched
  over the huge triangle (not verified; the "paint x2" match was a coincidence). Measured: ~365,000 such draws in one repro run.
- Mixed draws: the tree instance batch (`gndtex.cpp FlushBatch`) and fogged surfaces (`drsurf.cpp fn_802D5B8C`)
  take positions from the model (big-endian) but colours / texture coordinates from the ring (host order); those were
  read big-endian (instance fade alpha lost, lightmap uvs garbage). ~5.2 million draws per run, every frame.
Fix: an array that lies in the ring is always host order (`InDynRing`); every user of the ring is a CPU-built array
(drsurf skin/deform/fog outputs, gndtex batch, glare, gmat, water). Other arrays keep the position judgement.
`NICECRASH_NORINGLE=1` restores the old behaviour for comparison.

Verification (repro below, `build/t3/flashhunt/run1.sh` + `count.sh`, counting LUMLOG JUMPs at retrace >= 5000
because the detector saves at most 8 frames): fix on: 0, 0, 0 in-race jumps (3 runs; the only flagged frames are the
menu fade-ins at ~2500 / ~4350 / ~7290). Fix off (`NICECRASH_NORINGLE=1`, same build): 15 jumps, the usual bars.
A normal race frame with the fix (retrace 6200) looks right. Everything below is the history of the hunt.

## What they are
- Single frames (sometimes 2-3 in a row) with solid green bars, wedges or blocks, mostly in the Tutorial's
  "Tree Run" stretch. Bars often converge on one point (off screen or in the sky), like many triangles of a mesh
  sharing one far-away vertex.
- The green is exactly RGB (0, 225, 24) in every captured frame: the truck's paint (0, ~112, 12) doubled. No texture
  holds that colour, and no draw has it (or the paint colour) as vertex, material, TEV register or konst colour.
- Timing-dependent: 1-8 per 2400-frame run at full speed; anything that slows rendering (string logging per draw,
  per-vertex checks) makes them disappear. Not deterministic between runs.

## How to see them
`NICECRASH_LUMLOG=3900:2400` with the scripted Drive lesson (`build/t3/runcode.ps1`, inputs
`790:2,950:2,1100:2,1400:2,1600:2,1800:2,3100:2,3300:2,3500:2,3700:2,3900+1800:2`): every presented frame's flat-green
share is logged, and frames that jump are saved as `build/t3/flash_<retrace>.bmp` (sdk/vi.cpp LumLog).
`build/t3/sheet.ps1 flash "<frames>" out.png` makes a contact sheet.

## Ruled out (each 2-3 runs, flashes still present)
- Late RAM write-back of GPU EFB copies (`NICECRASH_NOCOPYRAM`), copy textures substituting RAM textures
  (`NICECRASH_NOCOPYTEX`), the synchronous CPU copy path (`NICECRASH_CPUCOPY`).
- Array byte-order guess: per-frame cache (judge every draw), 16-bit positions forced big-endian, texture-coordinate
  arrays forced big-endian.
- NVIDIA threaded driver (synchronous GL debug output on).
- Indirect texturing offsets switched off.
- FIFO interpreted at the game's cache flushes (DCFlushRange); no FIFO drain on mid-frame presents.
- Indexed matrix loads: no doubtful byte-order picks; no position or texture matrix with huge/non-finite elements.
- A second thread writing GX commands (only one writer).
- Triangles with NaN vertices dropped (they do exist every frame: img3 815228 tree draws, one NaN x).
- Long thin / big green triangles at the GPU hand-off (client arrays as drawn): none found with vertex colour green;
  but the bars' colour is not a vertex colour, so this check could not see them.

- Stale batched state (TC's idea): GL state set up again for every draw (no batching): bars still appear
  (a slow run, so speed is not strictly required either).
- Final (lit) vertex colour = paint or paint x2 (TC's check 2): only the grass tufts (img3 8abdcc, prim 98, n 4)
  match, every few frames, never on a flash frame. Index-past-batch check (TC's check 1) never fired earlier.
- glFinish once per frame before the swap (TC's buffer-rewrite test): still 3-4 flashes a run.
- Grass tufts skipped (SKIPTEX 1157b980,11570880, the only draws lit to paint x2): still 2-9 flashes a run.
- glFinish after every draw: untestable (~30,000 draws a frame; the race is never reached).

## Evening 2026-11-17
- Reliable repro: the Tutorial save (`build/t3/nand_tut`, copied), `NICECRASH_AUTOPILOT=2`, inputs in
  `build/t3/pz/fl_inputs.txt` (790:2,950:2,1150:DOWN,1250:DOWN,1350:2 then 2 every 200 frames), `NICECRASH_LUMLOG=1800:6600`:
  the autopilot drives Basics 1's Tree Run and 5-8 real flash frames appear per run (frames ~5700-7000).
- The shapes, seen clearly now: flat green acacia trunks and arches (trunk + branches), whole tree crowns, thin
  vertical bars; taller than / offset from the real trees, which draw normally in the same frame.
- Fixed on the way, flashes unchanged: LCLoadData/LCStoreData were unimplemented stubs (drsurf's batched y-morph
  deform flush never wrote its output), GX subtract blend was 0 - src instead of dst - src.
- Ruled out this evening (flashes unchanged): skipping draws with colour writes off (`NICECRASH_SKIPINVIS=1`, shadow
  volumes), skipping additive/subtractive blends (`=2`), a 4x bigger "gc:dyndata" frame ring, the trees in the water
  reflection render (water.cpp fn_802BDD8C), synchronous DVD (already synchronous).
- `NICECRASH_FLASHDRAWS` on a flash lists only legitimate near-camera geometry: the green shapes are not wild vertices
  in the frame's own draws. Next: an offscreen pass (ground lightmap / tree shadow layer, gndtex.cpp) leaking into the
  frame, or the tree instance batch (gndtex FlushBatch: 10 instances per draw, per-instance colour/uv arrays built in
  host order while positions come from the model) reading the wrong order for its colour/uv arrays.

## Not yet tried / ideas
- Find what draws with the truck's paint at 2x: the truck's own passes (reflection/shine, shadow, damage), or
  something sampling the paint texture through another stage (SKIPTEX only checks stage 0). Skip the truck's draws
  entirely and see if bars remain.
- Cheap capture: keep the last frame's GL batches in a ring (memcpy only) and dump on a JUMP, then analyse offline
  by texture of every stage, not vertex colour.
- Compare with Dolphin on the same stretch (the user reports Dolphin has no flashes).

The diagnostics used (env-gated logging in gxrender/gxinterp/gxfifo/vi/os/gxtev) are saved as
`green_flash_diagnostics_2026-11-17.patch` next to this file; apply with `git apply` to continue.
