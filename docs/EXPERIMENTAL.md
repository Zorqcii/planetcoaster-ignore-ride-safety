# EXPERIMENTAL: untested / unfinished rides

Branch `experimental/unfinished-rides`. Not part of any release. The stable release stays
[v0.1.0-beta](https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.1.0-beta).

## Goal
Let guests board and ride incomplete or untested roller coasters, without changing the two stable options.

## Milestones
1. The native helper gets reliable information about which rides are untested. **Done (0.2.0-exp.1), see results below.**
2. Find out whether guests can board an untested or incomplete ride, and whether it dispatches with them. **Stage 0.2.0-exp.2 (current).**
3. Operation after a crash. Investigated separately, only after milestone 2.

## Earlier findings (from the pre-release experiment), re-checked
| Finding | Status |
|---|---|
| Opening is blocked by the attraction's "tested / has ratings" flag. One `je` in the open gate makes it openable. | Confirmed earlier in game: untested and unfinished rides opened. Reused. |
| Guests skip destinations whose rating data is missing ("not going on until it's been tested"). | Confirmed by player observation. Not changed in this stage. |
| `bound=false`: the helper could not obtain the game's internal ride table through the Lua attractions object. | Confirmed (pointer was null). **Approach abandoned.** Replaced by the Lua → helper id channel below. |
| Ride closes after a crash despite the earlier hook. | Unexplained. Deferred to milestone 3. The old hook is not included. |

## What 0.2.0-exp.1 does when the experimental option is ticked
* Applies the open-gate patch (untested and unfinished rides can be opened; everything else still required).
* Every 3 s, uses the game's public script functions to list tracked rides that are not tested, and sends their station ids
  to the helper via a channel made only of separate functions (`irs_exp_begin`/`bit0`/`bit1`/`push`/`commit`).
  No Lua internals are read.
* Observe-only hook on the guest evaluator's "has ratings" read (`0x1406a051b`). It records the destination ids
  that guests skip as unrated. It changes no decision and no register other than the one the original instruction sets.
* Writes both lists to `IgnoreRideSafety.log` and shows a diagnostics list in Options → Game.

Unticking restores both patched locations. When the option is off, no experimental patch is applied.
The experimental code has its own fingerprint checks; if they fail, only the experimental option is unavailable.

## Question this stage answers
Are the ids the guest code skips the same station ids the Lua side reports? If yes, the next stage can apply
assumed ratings to exactly those rides (and nothing else, so shops and normal rides are untouched). It also records
whether an opened untested/unfinished ride dispatches, and whether any guest boards it without further changes.

## Local switching between stable and experimental
`tools/dev/switch-install.sh PACKAGE.zip` (game closed) replaces `Win64/ovldata/IgnoreRideSafety` with the package's folder.
Both packages use the same folder name, so only one version and one DLL can be loaded. `--status` shows what is installed.

## Stage 1 result (0.2.0-exp.1, manual test 2026-10-03)
* Lua sent the untested rides' station ids (`206 250`) to the helper. **The `bound=false` problem no longer applies.**
* The guest code skipped destination `206` hundreds of times with all rating bits clear (`flags & 0xF == 0`), matching the Lua list
  ("=ride from Lua"). **Guest destination id == station id: confirmed.**
* As expected for an observe-only build, guests kept saying "not until it's been tested", and Black Falcon closed itself after crashing.

## Stage 2 (0.2.0-exp.2)
* Lua sends only rides that are **open and untested**.
* For a destination on that list, the guest evaluator is given a private per-thread copy of the guest's assessment record with assumed
  ratings (Excitement 8, Fear 8, Nausea 4, prestige 300) and the rated bits set (the values chosen by the project owner). The game's record
  is never written. The evaluator's returned "chosen destination" still comes from the original record (`[rbp-0x40]`), so the copy cannot escape.
  Destinations not on the list (shops, rated rides) are never affected.
* Disabling first stops handing out copies and clears the list, then restores the code.
* Question: do guests now queue and board, and does the train dispatch with riders? Closing after a crash is still expected (milestone 3).
