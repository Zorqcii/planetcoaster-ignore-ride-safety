Ignore Ride Safety @VERSION@ - PROTOTYPE BUILD (crash-rider research)
====================================================================

For one focused test on a COPY of a park. Not for normal play and not a release.
It contains everything of 0.2.0-diag.1 (the 0.2.0-exp.6 behaviour plus observation logging) and one
extra option, OFF by default:
  "PROTOTYPE (research): launch one crashed rider into guest physics"
It only works while the EXPERIMENTAL option is ticked. After a crash, once one rider has finished
unloading at the exit, that rider is put into the game's own guest physics (no position or speed
is set by the mod). One rider per crash at most; while a launched rider is being watched (180 s),
further crashes are skipped. Launch and recovery are written to IgnoreRideSafety.log.
This calls game code in a way the game itself was never seen doing. It may crash the game, or leave
a guest stuck, invisible or trapped. Do NOT save the park after testing.
Untick the PROTOTYPE option to stop further launches (a guest already launched is left to the game).
Versions: package @VERSION@; scripts @VERSION@; helper @VERSION@ (research build 2).
The options header shows all three; IgnoreRideSafety/PACKAGE-VERSION.txt lists them too.

Install with the game closed: replace Win64/ovldata/IgnoreRideSafety with the folder from this ZIP.
Only one IgnoreRideSafety folder may exist. Roll back by replacing it with the folder from the
0.2.0-exp.6 package (or the stable release), again with the game closed. Keep the log first.

REQUIREMENTS: Planet Coaster build 1.13.3.88540, ACSE-PlanetCoaster 0.2. Linux/Proton tested only.
License: GPL-3.0-or-later (LICENSE.txt). Copyright (C) 2026 Zorqcii.
