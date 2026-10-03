# Technical notes

All addresses refer to `PlanetCoaster.exe` for game build 1.13.3.88540 (PE timestamp `0x663b9a50`, image base `0x140000000`).

## How the mod is loaded

1. Planet Coaster loads every content pack in `Win64/ovldata`. For each pack it requires the Lua module
   `Database.<PackName>LuaDatabase`; this mod's adds `Database.IgnoreRideSafety`.
2. ACSE asks every content pack for park managers; this mod registers `Managers.IgnoreRideSafetyManager`,
   which exists while a park is loaded.
3. The manager loads `IgnoreRideSafety.dll` from the mod folder with the game's own Lua `package.loadlib`.
   The folder is derived from `package.cpath`, whose first entry is `<game folder>\?.dll`. No personal path is involved.
4. The DLL's functions never touch the game's Lua state. Lua calls them with six arguments, and each returns a count k,
   so Lua hands back the k-th-from-top argument, which is the status code. This avoids depending on the game's modified Lua internals.
5. The checkboxes are added to the in-park **Options → Game** list by wrapping `Windows.GameOptionsMenu`
   (`Init`, `GetItems`, `HandleEvent`, `ApplyChanges`) at runtime. No game script file is replaced.
   Labels use the game's `[PopUp_Rides_RideName:RIDENAME='…']` text symbol, because plain strings are not displayed.

## Guest ride assessment

The function at `0x1406a0020` scores a ride for a guest. In it:

* `tooIntense = ride.Fear > guest.MaxRideIntensity` (`seta al` at `0x1406a0a9d`) and
  `notIntenseEnough = guest.MinRideIntensity > ride.Fear` (`seta bl`). If either is true, the ride is rejected with thought
  `Assessment_RideTooIntense` or `Assessment_RideNotIntenseEnough`. A second check for "too intense" is at `0x1406a1869`.
* The appeal is multiplied by a nausea factor computed from the ride's Nausea rating, the guest's comfortable and tolerable limits,
  and a global hard cap. The factor is 0 above the cap (refusal). The block starts with `jb` at `0x1406a0ae5` and merges at `0x1406a0b65`;
  the factor starts as 1.0 (`movaps xmm6, xmm14` at `0x1406a0ac0`).

| Option | Site | Original | Patched | Effect |
|---|---|---|---|---|
| Ignore ride safety concerns | `0x1406a0a9d` | `0f 97 c0` `seta al` | `b0 00 90` `mov al,0; nop` | "too intense" never set in the main check |
| | `0x1406a1869` | `0f 97 c0` | `b0 00 90` | same for the secondary check |
| Ignore ride nausea | `0x1406a0ae5` | `0f 82` (`jb` rel32) | `eb 7e` (`jmp` short to `0x1406a0b65`) | nausea factor stays 1.0 |

Not changed: the "not intense enough" result, price, queue, needs, the post-ride remarks picker (`0x140699cf0`),
and the "fits my preferences" bonus (`0x1406a0643`).

### Relation to the built-in `IgnoreFearAndNausea` cheat

The game has a cheat (`cheats:IgnoreFearAndNausea()`, triggered by renaming a guest) that sets a flag at `+0x9A48`. The evaluator checks it in four places.
It skips **both** "too intense" and "not intense enough", clamps the nausea factor to at least 0.5, and has no off switch.
This mod does not use or set that flag:
* the safety option affects only the "too intense" result;
* the nausea option affects only the nausea factor, which it removes entirely (it is not clamped to 0.5).

## Safety checks in the DLL

* Before any change: the image base must be `0x140000000`, the PE timestamp `0x663b9a50`, the bytes at eight fingerprint ranges around the
  sites must match exactly, and every site must hold either its original or its patched bytes. Otherwise every call returns "unsupported",
  nothing is written, and a line is logged.
* Each patch lies within one aligned 8-byte word and is applied with one atomic compare-and-exchange, so game threads never see a half-written instruction.
* An option is all-or-nothing: if any of its sites cannot be changed, sites already changed are restored.
* Disabling writes the original bytes back. The manager disables both options when a park loads and when it unloads.
* Everything is in memory only. Restarting the game always starts from the unmodified executable.

Status codes: 1 on, 2 off, 3 unsupported build, 4 could not make code writable (rolled back), 5 unexpected bytes (rolled back / refused).

## Porting to another game build

Run `tools/check_exe.py path/to/PlanetCoaster.exe`. If it reports mismatches, the addresses and fingerprints in `native/irs_patch.c` must be
re-derived for that build; do not just change the timestamp.
