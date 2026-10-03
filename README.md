# Ignore Ride Safety — Planet Coaster (2016) mod · **beta**

Two in-game options for the **original Planet Coaster (2016)** — not Planet Coaster 2:

| Option | What it does | What it does **not** change |
|---|---|---|
| **Ignore ride safety concerns** | Guests no longer refuse a ride because it is *too intense / too scary* for them (the ride's Fear rating is above their personal tolerance). | Price, queue length, needs, *not intense enough*, nausea, excitement. |
| **Ignore ride nausea** | A ride's *nausea rating* no longer reduces how much guests want to ride it, and no longer makes them refuse it. | Guests who already feel sick still avoid rides and can still vomit. Fear, price, queues, needs. |

Each option is independent: turning on one does not turn on the other.
The options do **not** force guests onto rides; they only remove those particular reasons for saying no.

> **Beta.** Tested only on Linux with Proton (details below). Windows is **untested**.
> This release does **not** support opening incomplete/untested tracks or keeping rides open after crashes.

## Requirements

* Planet Coaster (2016), **game build 1.13.3.88540** (Steam build 14428432, executable dated 2024-05-08).
  The version is shown at the bottom-left of the main menu. Other versions are refused safely (see *Unsupported versions*).
* **ACSE for Planet Coaster** 0.2 by EvanMad: <https://github.com/EvanMad/ACSE-PlanetCoaster>
  (download from its [0.2 release](https://github.com/EvanMad/ACSE-PlanetCoaster/releases/tag/0.2), folder `ACSE`).
  ACSE is not bundled here because it has no declared license.

## Installation

1. Close Planet Coaster.
2. Open the game folder: in Steam, right-click *Planet Coaster* → *Manage* → *Browse local files*.
   Then open `Win64/ovldata`.
3. Install ACSE: copy the `ACSE` folder (containing `Main.ovl` and `Manifest.xml`) into `Win64/ovldata`.
4. Install this mod: extract the release ZIP and copy its `IgnoreRideSafety` folder into `Win64/ovldata`.
   It must contain `Main.ovl`, `Manifest.xml` and `IgnoreRideSafety.dll`.
5. Start the game normally. The main menu should say **ACSE LOADED** under the logo.

No launch options, DLL overrides or extra programs are needed. On Linux/Proton the helper is loaded by the
game's own Lua scripts from the mod folder, so `WINEDLLOVERRIDES` is not required.

## Use

1. Load a park.
2. Press **Esc** (or the gear icon) → **Settings** → **Game** tab.
3. At the bottom, under **Mod: Ignore Ride Safety**, tick **Ignore ride safety concerns** and/or **Ignore ride nausea**.
4. Click **Apply**.

* Both options start **off** every time a park is loaded (including after a game restart). They are not saved in the park.
* Unticking and applying restores the game's original behaviour immediately; no ride has to be closed and no reload is needed.
* Thoughts guests already have ("this ride looks too intense") fade normally over time, so changes show up gradually.
  Guests already queuing or riding continue as usual.

## Known limitations (beta)

* **One game build only** (1.13.3.88540). Anything else: the checkboxes appear greyed out with an explanation, and nothing is changed.
* After riding something above their tolerance, guests may still *comment* that it was too intense or that they feel sick. That is a remark, not a refusal.
* Rides within a guest's preferred range still get the game's normal small "this is just my kind of ride" bonus; very scary or very nauseating rides do not get that bonus. They are no longer refused, but guests may still pick a ride that fits them better.
* *Ignore ride nausea* ignores the ride's nausea rating entirely (including the mild "a bit too nauseating for me" preference), not just the hard refusal.
* Extreme coasters with **no excitement** are still unpopular: low excitement is a separate reason and is untouched.
* If you have used the game's built-in guest-rename cheat that makes guests ignore fear and nausea, that cheat stays active for the session; this mod cannot switch it off.
* If another mod changes the same game code, these options refuse to apply (shown as unavailable) instead of conflicting.
* The checkboxes only appear in **Options → Game while a park is loaded**, not on the main menu.
* Windows (without Proton) has not been tested.

## What the mod changes

* **Files:** only what you copy into `Win64/ovldata/IgnoreRideSafety`. While the game runs, the helper writes a short log, `IgnoreRideSafety.log`, in that folder.
  No game files, saves or settings are modified.
* **In memory, only while an option is on:** a few bytes of the game's guest ride-assessment code
  (3 bytes in two places for the safety option, 2 bytes in one place for nausea). Turning the option off writes the original bytes back.
  The helper first checks the game's version stamp and the exact code around every location, and refuses to change anything if they don't match.
  See [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| No "ACSE LOADED" on the main menu | ACSE is not installed in `Win64/ovldata/ACSE`. |
| No *Mod: Ignore Ride Safety* section in Options → Game | No park loaded, ACSE missing, or the `IgnoreRideSafety` folder is not directly inside `Win64/ovldata`. |
| Checkboxes greyed out | Hover for the reason. *Unsupported game version*: your game build is not 1.13.3.88540. *DLL missing*: `IgnoreRideSafety.dll` is not in the mod folder. Check `IgnoreRideSafety.log`. |
| Checkbox labels blank | Report it with your game version; the labels use the game's own text system. |

## Removal

1. Close the game.
2. Delete `Win64/ovldata/IgnoreRideSafety`.
3. Optionally delete `Win64/ovldata/ACSE` if no other mod needs it.

The mod changes nothing permanently, so your parks load and play normally afterwards.
Steam's *Verify integrity of game files* does not remove extra folders in `ovldata`; delete them manually.

## Tested environment

| | |
|---|---|
| Game | Planet Coaster 1.13.3.88540 (Steam build 14428432, `PlanetCoaster.exe` sha256 `a1a1465df3710faebebd4058d6b3cddf6b207ccdecc2d570dba4f04cb033d913`) |
| Loader | ACSE-PlanetCoaster 0.2 (`ACSE/Main.ovl` sha256 `7391eaee8902064cd7152979da65423ae97df86a1f86f3f6730f622f75033fd0`) |
| Compatibility layer | Proton Experimental `experimental-11.0-20261001` (prefix created by 11.0-100) |
| OS | Manjaro Linux, kernel 6.18, KDE Plasma (Wayland), NVIDIA RTX 5090 |
| Windows | **not tested** |

What has been verified and how is listed in [docs/VERIFICATION.md](docs/VERIFICATION.md).

## Building from source

See [docs/BUILDING.md](docs/BUILDING.md). Players do not need to build anything.

## Credits

* ACSE-PlanetCoaster by EvanMad, a port of ACSE by the OpenNaja team.
* cobra-tools by the OpenNaja team (used at build time to create the `.ovl` file).
* Planet Coaster is a trademark of Frontier Developments plc. This is an unofficial fan mod, not affiliated with or endorsed by Frontier.

## License

Copyright (C) 2026 Zorqcii.

This project is free software under the **GNU General Public License v3.0 or later** ([LICENSE](LICENSE)).
You may use, modify and redistribute it, including in other mods, as long as distributed versions stay open source under the same license.
The license covers this project's own code only. It grants no rights in Planet Coaster or its code and assets, or in third-party software such as ACSE-PlanetCoaster.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
