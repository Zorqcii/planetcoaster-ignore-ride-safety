Ignore Ride Safety @VERSION@ - DIAGNOSTIC BUILD (crash-rider research)
=====================================================================

For one focused test only. Not for normal play and not a release.
It behaves like 0.2.0-exp.6 (unfinished rides open, guests board, the ride stays open after a crash)
and additionally RECORDS which game routines run when a train crashes. It is meant to change
nothing for passengers, trains or rides, but its hooks run inside game code and could still cause
a crash or slowdown. Use a COPY of a park.

What it records (only while the EXPERIMENTAL option is ticked), in IgnoreRideSafety.log:
  * the train-removed handler, the "purge riders" routine and the guest-physics start routine:
    every call, with time, thread, calling code and the ids those routines use;
  * a marker each time the crash handler tries to close a ride;
  * the number (and game ids) of riders on open untested rides, every 3 seconds.
Versions: package @VERSION@; scripts @VERSION@; helper @VERSION@ (diagnostic build 1).
The options header shows all three; IgnoreRideSafety/PACKAGE-VERSION.txt lists them too.

Install with the game closed: replace Win64/ovldata/IgnoreRideSafety with the folder from this ZIP.
Only one IgnoreRideSafety folder may exist. Roll back by replacing it with the folder from the
0.2.0-exp.6 package (or the stable release), again with the game closed. Keep the log first.

REQUIREMENTS: Planet Coaster build 1.13.3.88540, ACSE-PlanetCoaster 0.2. Linux/Proton tested only.
License: GPL-3.0-or-later (LICENSE.txt). Copyright (C) 2026 Zorqcii.
