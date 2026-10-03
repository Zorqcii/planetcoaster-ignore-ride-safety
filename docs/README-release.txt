Ignore Ride Safety @VERSION@ (beta) - mod for Planet Coaster (2016)
====================================================================

Two in-game options:
  * Ignore ride safety concerns - guests no longer refuse rides for being too intense/scary.
  * Ignore ride nausea          - a ride's nausea rating no longer puts guests off it.
Price, queues, needs and "not intense enough" still apply. Options start OFF on every park load.

REQUIREMENTS
  * Planet Coaster game build 1.13.3.88540 (shown bottom-left of the main menu).
    Other builds: the options appear greyed out and nothing is changed.
  * ACSE for Planet Coaster 0.2: https://github.com/EvanMad/ACSE-PlanetCoaster/releases/tag/0.2

INSTALL
  1. Close the game. In Steam: Planet Coaster > Manage > Browse local files > Win64 > ovldata
  2. Copy the ACSE folder (from ACSE-PlanetCoaster) into ovldata.
  3. Copy the IgnoreRideSafety folder from this ZIP into ovldata.
     It must contain Main.ovl, Manifest.xml and IgnoreRideSafety.dll.
  4. Start the game normally (no launch options needed). The menu shows "ACSE LOADED".

USE
  Load a park > Esc > Settings > Game > bottom of the list, "Mod: Ignore Ride Safety".
  Tick an option and click Apply. Untick + Apply to turn it off.

REMOVE
  Close the game and delete Win64/ovldata/IgnoreRideSafety. Nothing else is changed.

Beta: tested on Linux with Proton Experimental only; Windows is untested.
License: GPL-3.0-or-later (LICENSE.txt). Copyright (C) 2026 Zorqcii.
Does NOT support incomplete/untested tracks or keeping rides open after crashes.
Documentation, limitations and source: https://github.com/Zorqcii/planetcoaster-ignore-ride-safety
