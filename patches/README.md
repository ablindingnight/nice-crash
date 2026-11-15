# Port patches

Exact-text replacements applied by tools/gametrial/prep.py to the port's COPY of the decomp source
(build/gamesrc). Each one fixes behaviour that differs between MWCC and clang/x86. Every patch must match exactly
once, or the build stops (so a decomp change that already fixed it is noticed). Each is also written up in
docs/SOURCE_NOTES.md for the decomp sessions; once the decomp source carries the fix, delete the patch.

Format (patches/*.patch.txt): blocks of
    @@@ <file relative to src/game> | <one-line reason>
    <<<
    old text
    ===
    new text
    >>>
