# Source notes for the decomp sessions (port trial, generated)

From `port/tools/gametrial` (every src/game unit compiled for 32-bit x86 Windows with zig/clang). These are
places where the decomp source relies on MWCC-only behaviour. Each fix is meant to be matching-neutral;
please verify with the usual build before committing. Line numbers are as of the trial run.

Already handled port-side without source changes (no action needed): `__declspec(section ...)`, MWCC
intrinsics (`__fabs`, `__fabsf`, `__abs`, `__cntlzw`), `operator new(unsigned long, ...)` (rewritten to
size_t in the port copy), decomp/compat.h clang branch (the port has its own types.h), the GX FIFO macro
(0xCC008000 -> host FIFO object), and the little-endian byte-swap helpers (no-ops on an x86 host).

## Behaviour differences found at run time (port patches in port/patches; please fix in the decomp)

- `res.cpp` fn_8012A648: `lbl_8055E008->Free(AsyncResource::operator new(...))`: the argument creates the
  pool that lbl_8055E008 points to. MWCC evaluates the argument first; C++17 compilers load the (null)
  pointer first. Matching-neutral fix: `void* p = AsyncResource::operator new(...); lbl_8055E008->Free(p);`
  (check the match).
- `mrender.cpp` declares `Pool::Pool(const char*, long, long)`; pool.cpp defines it with s32. Same type on
  MWCC, different mangled names elsewhere: use s32 in mrender.cpp.

## templates-in-extern-C: 21 sites in 18 files

Templates (or template helpers) sit inside an `extern "C" {` block. MWCC allows it; standard C++ does not. Close the extern "C" block before the template (matching is unaffected: templates are C++ anyway).

- `birds.cpp`: lines 245
- `caruimod.cpp`: lines 937
- `cenvmap.cpp`: lines 185
- `drivmod.cpp`: lines 472
- `evpath.cpp`: lines 1316
- `gtext.cpp`: lines 329
- `heightfld.cpp`: lines 296
- `masslist.cpp`: lines 416, 418, 505
- `multievt.cpp`: lines 1052
- `placecar.cpp`: lines 285
- `ppsys.cpp`: lines 543
- `racerslot.cpp`: lines 130
- `ring.cpp`: lines 397
- `sphcoll.cpp`: lines 2053
- `stuntpart.cpp`: lines 378
- `tedash.cpp`: lines 565
- `tree.cpp`: lines 582
- `uistyle.cpp`: lines 137, 138

## linkage-mismatch: 106 sites in 16 files

A symbol is declared both inside and outside `extern "C"` in the same unit (usually an `extern` re-declared locally). Declare it once, with one linkage.

- `aicar.cpp`: lines 1089, 1739, 1875, 1968, 2891, 2892
- `cammgr.cpp`: lines 336, 1716
- `carbody.cpp`: lines 411, 412, 413, 1471, 1554, 2900, 2996, 3298, 3512, 3879
- `cardmg.cpp`: lines 792, 793, 864, 865, 866
- `chalpath.cpp`: lines 1071
- `garage.cpp`: lines 1113, 1114, 1115
- `gmat.cpp`: lines 706
- `interlude.cpp`: lines 2558
- `mroll.cpp`: lines 914, 917, 1229, 1232, 1233, 1234, 1235, 1238
- `multievt.cpp`: lines 775, 861, 928
- `obstacles.cpp`: lines 855, 856, 857, 858, 1326, 1327, 1533
- `playcar.cpp`: lines 1636, 1637, 1638
- `popwarn.cpp`: lines 688, 796
- `sphcoll.cpp`: lines 989, 991, 1880, 1952, 2748, 4370, 4371, 4372, 4373, 4374, 4375, 4376, 4377, 4378, 4379
- `uiapi.cpp`: lines 2440, 2441, 2447
- `unk_801BD620.cpp`: lines 2559, 2560, 2561, 2562, 2563, 2564, 2565, 2566, 2567, 2568, 2569, 2570, 2571, 2572, 2573, 2574, 2575, 2576, 2577, 2578, 2579, 2580, 2581, 2582, 2583, 2584, 2585, 2586, 2587, 2588 ...

## no-matching-call: 40 sites in 19 files

Usually a temporary/rvalue passed to a non-const reference or pointer parameter, or a call that relies on MWCC's looser conversions. See the candidate notes in the log; often `const T&` on the helper fixes it.

- `birds.cpp`: lines 267, 281
- `caruimod.cpp`: lines 975, 976
- `cenvmap.cpp`: lines 192
- `drivmod.cpp`: lines 483
- `evpath.cpp`: lines 1330, 1347
- `gtext.cpp`: lines 350
- `heightfld.cpp`: lines 329, 362
- `masslist.cpp`: lines 432, 438, 536
- `multievt.cpp`: lines 1067
- `placecar.cpp`: lines 341
- `ppsys.cpp`: lines 568
- `racerslot.cpp`: lines 246
- `ring.cpp`: lines 409
- `sphcoll.cpp`: lines 2090
- `stuntpart.cpp`: lines 411
- `tedash.cpp`: lines 578
- `tree.cpp`: lines 593
- `uistyle.cpp`: lines 293, 295, 297, 326, 327, 328, 329, 331, 332, 354, 355, 359, 360, 688, 689
- `unk_802E4D84.cpp`: lines 490, 1136

## placement-new-const: 1 sites in 1 files

`new ("tag") T` where the unit does not see the game's `operator new(size_t, const char*)`, so clang picks the standard placement new. Declare the tagged operator new (with size_t) before use.

- `widgets.cpp`: lines 3328

## switch-jump-over-init: 27 sites in 4 files

A `case` label jumps past an initialised local declared in an earlier case. Wrap that case body in `{ }`.

- `aicar.cpp`: lines 1424
- `cammgr.cpp`: lines 1554, 1558, 1561, 1568, 1572, 1575, 1578, 1581, 1584, 1587, 1590, 1593, 1601, 1604, 1607, 1610, 1613
- `dashhelper.cpp`: lines 485, 498, 502, 508, 511, 520, 523
- `interlude.cpp`: lines 2145, 2146

## conflicting-types: 19 sites in 5 files

Two different prototypes for the same function in one unit (often int vs s32/u32 or a header vs a local extern). Make them agree.

- `fonttext.cpp`: lines 82, 116
- `gart.cpp`: lines 93, 94
- `gtext.cpp`: lines 702, 773, 909, 964, 972
- `physcam.cpp`: lines 260
- `uidraw.cpp`: lines 70, 108, 189, 214, 240, 266, 279, 458, 472

## out-of-line-mismatch: 2 sites in 1 files

A member definition does not match its declaration (types differ, e.g. long vs s32).

- `uitable.cpp`: lines 259, 276

## implicit-function: 31 sites in 3 files

C file calls a function with no prototype in scope. Add the prototype.

- `unk_80116BD4.c`: lines 561, 766, 1440, 1449, 1845, 2224, 2389, 2453, 2535, 2545, 2584, 2593, 2626, 2634, 2655, 2658, 2996, 3187, 3189, 3221, 3241, 3261, 3271, 3290, 3302, 3314, 3328
- `unk_8020F3B4.c`: lines 510, 511, 522
- `unk_80262F8C.c`: lines 25

## address-of-temporary: 5 sites in 2 files

Taking the address of a temporary (MWCC allows it). Use a named local.

- `sphcoll.cpp`: lines 3588, 3590, 3610, 3621
- `trail.cpp`: lines 795

## return-type-overload: 1 sites in 1 files

Two declarations of the same function with different return types.

- `uiapi.cpp`: lines 2099

## other

- `aicar.cpp` 1979: call to 'fn_80137788' is ambiguous
- `crushmaster.cpp` 1676: cannot combine with previous 'extern' declaration specifier
- `drsurf.cpp` 1261: expected 'volatile', 'inline', 'goto', or '('
- `gmat.cpp` 749: use of undeclared identifier 'count'
- `gmat.cpp` 754: use of undeclared identifier 'data'
- `homebtn.cpp` 159: expected 'volatile', 'inline', 'goto', or '('
- `mcdialog.cpp` 65: non-constant-expression cannot be narrowed from type 'size_t' (aka 'unsigned int') to 'int' in initializer list [-Wc++11
- `pickup.cpp` 841: cannot combine with previous 'extern' declaration specifier
- `pickup.cpp` 864: cannot combine with previous 'extern' declaration specifier
- `race.cpp` 93: expected unqualified-id
- `race.cpp` 198: expected unqualified-id
- `trailer.cpp` 230: call to implicitly-deleted default constructor of 'BodyState'
- `uicontext.h` 10: unknown type name 'size_t'
- `uitable.cpp` 161: incompatible pointer types assigning to 'long *' from 's32 *' (aka 'int *')
- `uitable.cpp` 162: incompatible pointer types assigning to 'long *' from 's32 *' (aka 'int *')
- `uitable.cpp` 261: incompatible pointer types assigning to 'const long *' from 'const s32 *' (aka 'const int *')
- `unk_80116BD4.c` 1425: use of undeclared identifier 'uintptr_t'
- `unk_80116BD4.c` 1426: use of undeclared identifier 'uintptr_t'
- `unk_80116BD4.c` 1427: use of undeclared identifier 'decodeOffset'
- `unk_80116BD4.c` 1427: use of undeclared identifier 'uintptr_t'
- `unk_80116BD4.c` 1435: use of undeclared identifier 'uintptr_t'
- `unk_80116BD4.c` 1441: use of undeclared identifier 'readOffset'
- `unk_80116BD4.c` 1441: use of undeclared identifier 'uintptr_t'
- `unk_80116BD4.c` 1442: use of undeclared identifier 'readOffset'
- `unk_8020F3B4.c` 9: type name requires a specifier or qualifier
- `unk_8020F3B4.c` 9: expected ';' at end of declaration list
- `unk_8020F3B4.c` 27: type name requires a specifier or qualifier
- `unk_8020F3B4.c` 27: expected ';' at end of declaration list
- `unk_8020F3B4.c` 155: no member named 'm_entries' in 'struct Unk_804902C4'
- `unk_8020F3B4.c` 167: no member named 'm_entries' in 'struct Unk_804902C4'
- `unk_8020F3B4.c` 183: no member named 'm_entries' in 'struct Unk_804902C4'
- `unk_8020F3B4.c` 203: no member named 'm_entries' in 'struct Unk_804900AC'
- `widgets.cpp` 305: unknown type name 'size_t'
