Ignore Ride Safety @VERSION@ - EXPERIMENTAL BUILD
=================================================

Experimental prerelease. For normal play use the stable release (0.1.0-beta):
  https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.1.0-beta

Contains the two stable options (unchanged) plus one EXPERIMENTAL option, off by default:
  "EXPERIMENTAL: allow opening untested or unfinished rides"
When ticked:
  * untested or unfinished coasters can be opened (entrance, exit and queue still required);
  * while such a ride is OPEN, guests treat it as Excitement 8 / Fear 8 / Nausea 4 and will queue
    and board (the ride's own stats panel still shows no ratings);
  * when a train runs off an unfinished track and crashes, the ride stays open and the trains respawn
    for the next riders (crash closes are skipped only for these rides; your own Close button works);
  * a diagnostics list appears under the option in Options > Game, and IgnoreRideSafety.log records events.
Tick "Ignore ride safety concerns" too, or timid guests refuse because of the assumed Fear 8.
Unticking restores normal behaviour. All options reset to off when a park is loaded.

Known: when a train is destroyed in a crash, its passengers reappear at the ride exit (not at the crash site).
The helper's log header reports 0.2.0-exp.5 (the native helper is unchanged since that build).

REQUIREMENTS: Planet Coaster build 1.13.3.88540, ACSE-PlanetCoaster 0.2.
Tested on Linux with Proton Experimental only; Windows is untested.

Install with the game closed: replace Win64/ovldata/IgnoreRideSafety with the folder from this ZIP.
Only one IgnoreRideSafety folder may exist. Try it on a COPY of a park.
Restore the stable version: replace the folder with the one from the stable release ZIP.

License: GPL-3.0-or-later (LICENSE.txt). Copyright (C) 2026 Zorqcii.
