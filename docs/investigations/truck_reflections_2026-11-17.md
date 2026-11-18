# Truck reflections: stepped blocks (fixed in 1371724)

Symptom: the player's truck paint showed stepped square blocks of shade instead of smooth reflections.

Cause: the paint (drsurf surface fmt 0x10 -> mode 2 -> tevbink fn_802DEFA0) reads the 256x256 envmap
(cenvmap.cpp render target, map 6) through an indirect lookup of the "imap%d.tex" offset table (map 5).
sdk/gxtev.cpp point-sampled indirect textures at a texel centre, always repeat-wrapped, so the offsets
stepped. Hardware and Dolphin (PixelShaderGen.cpp: iindtex via sampleTextureWrapper) use the normal
sampler with the texture's filter and wrap.

Fix: indirect lookups go through sampleGX with the map's mode0 wrap/filter.

Verified: A/B old vs new exe, same inputs, 15 shots (build/t3/ab_old, build/t3/ab_new): truck smooth;
intro video pixel-identical (Bink ramps are nearest-filtered); menus unchanged.

Next steps:
- Rebuild build/NiceCrashGame.exe when the user is not playing:
  `python -I tools/gametrial/link.py --nocompile` (test exe: build/t3/rx/NiceCrashTest.exe).
- Not yet checked in a scene: the other indirect users (gndtex.cpp ground layer, gx.cpp ind blend).
