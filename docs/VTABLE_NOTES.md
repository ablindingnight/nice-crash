# Vtable destructor-slot mismatches (port finding, tools/gametrial/vslots.py)

On MWCC a virtual destructor takes ONE vtable slot; on the Itanium C++ ABI (the PC port) it takes TWO (complete +
deleting). Where one unit declares a class with `virtual ~X()` and another unit declares a placeholder virtual
(vf08, Init, ...) in the same position, both agree on the Wii but not on the PC: a virtual call from the
placeholder unit lands in the destructor. Found the hard way: bg.cpp BgObject{vf08, Update} vs res/soundmgr
{~BgObject, Update} made the background-task Update() call run ~SoundManager (port patch: vtable-dtor).

Fix in the decomp: declare the slot as the virtual destructor in every unit (no code change on MWCC). Candidates
(the scanner does not see inherited virtuals, so check each):

```
Arena                    slot 2: destructor in ringchal.cpp; plain virtual in aidriver.cpp(vf10), tutorial.cpp(vf10)
BoxShape                 slot 0: destructor in obstacles.cpp,trackobj.cpp; plain virtual in betarace.cpp(vf10)
CarBody                  slot 0: destructor in carbody.cpp; plain virtual in netcar.cpp(Init), springcar.cpp(Init), trailer.cpp(Init)
Chunks                   slot 0: destructor in chunks.cpp,sparks.cpp; plain virtual in partemit.cpp(vf08)
CollVolume               slot 0: destructor in carbody.cpp,phob.cpp; plain virtual in collvol.cpp(Init), mutphob.cpp(Init), panelvol.cpp(Init), wheel.cpp(Init)
GameMode                 slot 0: destructor in amaster.cpp,betarace.cpp,crushmaster.cpp,modeinl.cpp,ringchal.cpp,zone.cpp; plain virtual in actseq.cpp(vf08), cardmg.cpp(v08)
GameUI                   slot 0: destructor in amaster.cpp,crushmaster.cpp,ringchal.cpp,zone.cpp; plain virtual in crashmode.cpp(vf08), overlay.cpp(vf08), racing.cpp(vf08)
LineSet                  slot 0: destructor in ilresmgr.cpp; plain virtual in orbit.cpp(vf08)
MutPhob                  slot 0: destructor in phob.cpp; plain virtual in mutphob.cpp(Init)
Phob                     slot 1: destructor in ball.cpp,corners.cpp,phob.cpp,rigid.cpp; plain virtual in physcol.cpp(vf0C), sol.cpp(vf0C)
RacingOverlay            slot 0: destructor in racing.cpp; plain virtual in overlay.cpp(Update)
RigidBody                slot 0: destructor in phob.cpp; plain virtual in ball.cpp(Init), corners.cpp(OnHit), mutphob.cpp(Init), panelvol.cpp(Init), rigid.cpp(Init), wheel.cpp(Init)
StatBar                  slot 0: destructor in garage.cpp; plain virtual in mroll.cpp(v08)
VR                       slot 0: destructor in evpath.cpp; plain virtual in carbody.cpp(v08)
```
