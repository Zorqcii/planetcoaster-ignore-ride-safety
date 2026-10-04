# EXPERIMENTAL: untested / unfinished rides

Branch `experimental/unfinished-rides`. Not part of any release. The stable release stays
[v0.1.0-beta](https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.1.0-beta).

## Goal
Let guests board and ride incomplete or untested roller coasters, without changing the two stable options.

## Milestones
1. The native helper gets reliable information about which rides are untested. **Done (0.2.0-exp.1), see results below.**
2. Find out whether guests can board an untested or incomplete ride, and whether it dispatches with them. **Done (0.2.0-exp.3).**
3. Operation after a crash: the ride stays open and trains respawn for the next riders. **Done (0.2.0-exp.5).**
4. Riders stay at the crash site and walk back, as in RollerCoaster Tycoon. **Optional extension; separate branch `experimental/crash-rider-physics`.**
   Status 2026-10-03: unresolved within the agreed scope (not disproven); live testing closed; see `CRASH-RIDER-PHYSICS.md`.

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

## Stage 2 result (0.2.0-exp.2, manual test 2026-10-03)
* Log: the ride was listed while open and guests got the assumed ratings ("guests now see assumed ratings for a listed ride").
* Guests showed the thought "I want to go on Black Falcon 1" and walked to it. **The destination choice now works.**
* With the train held at the station (600 s minimum wait), every guest **turned around at the entrance**, and "not until it's been tested" thoughts rose.
  The ride closed itself after the empty train crashed.

## Stage 3 (0.2.0-exp.3): the entrance check
* Two more guest functions test the same "rated" bits: `0x1406a2990` and `0x1406a3470`, both called from `0x1406a6d90`.
  They are the "can I join this ride now?" checks and record thought 0x2B on failure.
  They take the assessment record as their 5th argument, read only non-rating fields after the test, and do not keep the pointer.
* Their flag reads straddle 8-byte boundaries, so they cannot be patched atomically. Both functions start on aligned addresses, so the
  hook is at the function entry instead (`jmp` to a wrapper; 6 and 5 bytes). The wrapper replaces the 5th argument with a per-thread copy
  that has the rated bits set (listed rides only), then runs the original prologue bytes from a trampoline and continues in the function.
* The four experimental sites and eight fingerprints are checked before applying. All-or-nothing with rollback.
* Question: do guests now pass the entrance, queue, board, and dispatch with the train?

## Stage 3 result (0.2.0-exp.3, manual test 2026-10-03)
* **Guests entered the queue, boarded the unfinished coaster and dispatched with the train. Milestone 2 achieved.**
* The trains left the track, crashed and exploded, and riders were placed at the exit. The ride then went to **closed**.

## Stage 4 (0.2.0-exp.4): keep the ride open after a crash
* When a crashed train is removed, the game's train-removed handler (`0x1405d3fb0`) asks `IsClosed` and, if the ride is not closed,
  closes it (then runs a close follow-up). Its `call IsClosed` (`0x1405d40aa`) is redirected: for rides on the open-untested list it answers
  "closed", so the handler takes the game's own already-closed path. All other rides are answered by the game's `IsClosed`.
  The earlier pre-release attempt at this relied on the broken ride-table lookup; this version uses the working Lua list.
* Diagnostic, log only: the close-request function (`0x140537510`) is entered through a hook that records the calling address for
  listed rides. It never blocks a close, so the player can always close the ride. If the ride still closes, the log names the path.
* Question: does the ride stay open and keep cycling (train respawns at the station, next riders board)?

## Stage 4 result (0.2.0-exp.4, manual test 2026-10-03)
* The ride still closed after the crash. The log showed the train-removed handler hook never fired; the only close request for the listed
  ride came from `0x140815559`, i.e. from `0x140815460` ("close every station of a ride").
* `0x140815460` has two callers: a Lua binding (`rides:CloseRide`, used only by UI/editor/DLC scripts) and the native crash handler
  `0x1409a9270` (reached from `0x1409e0048`). That handler, under a lock, looks up the ride, checks `0x1408151c0`, adjusts a ride field,
  and calls `0x140815460(system, &ride id)`; it does not use the result.

## Stage 5 (0.2.0-exp.5): skip the crash close
* Entry hook on `0x140815460` (7 bytes, aligned: `mov rax,rsp ; mov [rax+0x10],rbx` → `jmp ; nop`). If the call returns to the crash
  handler (`0x1409a934b`) **and** the ride id is listed, the routine returns immediately. Every other call, including the player's own
  close and editors, runs the original code via a trampoline.
* Lua now also sends the listed rides' **ride ids** (the crash handler identifies rides, not stations).
* Question: does the ride stay open, and do trains respawn for the next riders (a continuous loop)?

## Stage 5 result (0.2.0-exp.5, manual test 2026-10-03)
* **The ride runs, crashes and stays open; the loop continues.** Owner report: "It works now. The ride runs, crashes, and stays open."
* Log: list `221 219` (station id and ride id after this park load); "skipped the crash close of a listed ride" 6 + 6 + 6 times;
  no other close requests for the listed ride.
* Owner confirmed (2026-10-03): the ride's own close button still closes it; unticking the experimental option mid-loop returns the
  ride to normal behaviour; with the experimental option off, the safety and nausea options behave exactly as in 0.1.0-beta.

## Phase 4: RollerCoaster Tycoon-style riders (investigation)
Published so far: [v0.2.0-exp.5](https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.2.0-exp.5) (separate experimental prerelease).

Findings (static):
* Coaster crashes are "guest physics incidents" in this game: the achievement system counts `nGuestsInvolved` from `GuestPhysicsIncidentEndedMessage`.
* The game has `GuestEnteredSoSFromCrashMessage` and `GroupPhysicsRecoveryMessage`, guest state components `GuestPhysics`, `GuestSOS` and `GuestLost`,
  and a trapped-guest system (`guests:GetTrappedGuestCount` / `GetNextTrappedGuest`). So the engine already has a path for crash victims who are
  stranded and must recover. The owner observes riders reappearing at the exit instead.

0.2.0-exp.6 (Lua only; native helper unchanged and byte-identical to 0.2.0-exp.5): while the experimental option is on, counts those crash messages,
records the fields of the first of each kind, and shows them plus the trapped-guest count in the diagnostics list. No behaviour change.
Question: in this crash type, do riders go through the physics-incident / SOS path, or are they moved straight to the exit?

## Milestone checkpoint: unfinished rides (0.2.0-exp.6, 2026-10-03)
**Owner acceptance (2026-10-03): the installed 0.2.0-exp.6 package passed the manual checks** (crash/reset loop with the ride staying open,
disabling the experimental option, and the safety and nausea options). The milestone is complete.

The unfinished-ride objective is met: guests board an incomplete coaster and it dispatches repeatedly. When a train is destroyed,
passengers return to the ride exit (not the crash site).

Evidence (owner's game, Planet Coaster 1.13.3.88540 / Steam build 14428432, Proton Experimental `experimental-11.0-20261001`):
* In-game diagnostics with 0.2.0-exp.6: the unfinished coaster (station 221) was UNTESTED with 2 open track ends, **open**, with **21 riders**
  on board, **165 departures** and no "not until tested" thoughts. "Guest code reached one of them: YES".
* Helper log (0.2.0-exp.5): "skipped the crash close of a listed ride" 18 times across repeated crashes; no other close requests.
* Owner checks: the ride's own Close button closes it; unticking the option mid-loop restores normal behaviour; with the option off the
  safety and nausea options behave as in 0.1.0-beta.
* Crash observer (0.2.0-exp.6): GuestPhysicsIncidentEnded 0, GuestHidden 0, trapped guests 0 across several crashes with riders aboard;
  "EnteredSoSFromCrash" and "GroupPhysicsRecovery" are not exposed to Lua in this game. Owner: riders appear at the exit.
  These zeros alone do not prove that no guest physics occurs (two messages are unobservable and the receiver was never validated against
  a known incident); see `CRASH-RIDER-PHYSICS.md`. Which game code moves the riders to the exit is unidentified.

Note: 0.2.0-exp.6 changed only Lua; its `IgnoreRideSafety.dll` is byte-identical to 0.2.0-exp.5 and its log header (and `switch-install.sh --status`)
says "0.2.0-exp.5". Before any publication the diagnostics must show package and component versions separately (see `CRASH-RIDER-PHYSICS.md`).
