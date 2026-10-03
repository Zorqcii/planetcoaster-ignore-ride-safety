Ignore Ride Safety @VERSION@ - EXPERIMENTAL TEST BUILD (not a release)
=====================================================================

This is a diagnostic build for testing support for untested/unfinished rides.
For normal play use the stable release (0.1.0-beta):
  https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.1.0-beta

Contains the two stable options (unchanged) plus one EXPERIMENTAL option, off by default:
  "EXPERIMENTAL: allow opening untested or unfinished rides"
In this build that option only:
  * lets untested or unfinished rides be opened, and
  * records what the guest code sees, in IgnoreRideSafety.log and in a diagnostics list
    shown under the option in Options > Game.
Guests are still expected to refuse untested rides in this build.

Install with the game closed: replace Win64/ovldata/IgnoreRideSafety with the folder from this ZIP.
Only one IgnoreRideSafety folder may exist. Use a COPY of a park for testing.
Restore the stable version: replace the folder with the one from the stable release ZIP.

License: GPL-3.0-or-later (LICENSE.txt). Copyright (C) 2026 Zorqcii.
