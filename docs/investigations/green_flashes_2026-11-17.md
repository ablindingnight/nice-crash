# Green flashes in races (open, 2026-11-17)

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
- glFinish after every draw: untestable (~30,000 draws a frame; the race is never reached).

## Not yet tried / ideas
- Find what draws with the truck's paint at 2x: the truck's own passes (reflection/shine, shadow, damage), or
  something sampling the paint texture through another stage (SKIPTEX only checks stage 0). Skip the truck's draws
  entirely and see if bars remain.
- Cheap capture: keep the last frame's GL batches in a ring (memcpy only) and dump on a JUMP, then analyse offline
  by texture of every stage, not vertex colour.
- Compare with Dolphin on the same stretch (the user reports Dolphin has no flashes).

The diagnostics used (env-gated logging in gxrender/gxinterp/gxfifo/vi/os/gxtev) are saved as
`green_flash_diagnostics_2026-11-17.patch` next to this file; apply with `git apply` to continue.
