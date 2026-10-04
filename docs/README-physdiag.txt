Ignore Ride Safety @VERSION@ - DIAGNOSTIC BUILD (guest-physics observation)
==========================================================================

For one focused observation session on a DISPOSABLE COPY of a park. Not for normal play.
This build applies NO gameplay patches: "Ignore ride safety", "Ignore ride nausea" and every
experimental feature are unavailable, and it contains no launch/prototype code.

One option, off by default:  "DIAGNOSTIC: log guest physics (observation only)".
When ticked it records, in IgnoreRideSafety.log, the game's own guest-physics events when a
crash or other impact knocks guests over: impact, group request, a group entering Physics (seen
through its first internal step), each guest's launch, recovery and stranded (SOS) guests; plus,
through the game's own script functions, the displayed group status, group members and the
"incident ended" message. It changes nothing in the game, but its patches run inside game code and
could still cause a crash or slowdown.
Lines marked ENTRY = a game function was entered (values as read at that moment, outcomes are
predictions); STATE = a confirmed state read through the game's script functions; SANITY = purge
(unloading) calls from scripts and station closing only - crash-time unloading is not observed.

Versions: package @VERSION@; scripts @VERSION@; helper @VERSION@ (research build 3).
Install with the game closed: replace Win64/ovldata/IgnoreRideSafety with the folder from this ZIP.
Only one IgnoreRideSafety folder may exist. Roll back by replacing it with the 0.2.0-exp.6
(or stable) folder, again with the game closed. Keep the log first.

REQUIREMENTS: Planet Coaster build 1.13.3.88540, ACSE-PlanetCoaster 0.2. Linux/Proton tested only.
License: GPL-3.0-or-later (LICENSE.txt). Copyright (C) 2026 Zorqcii.
