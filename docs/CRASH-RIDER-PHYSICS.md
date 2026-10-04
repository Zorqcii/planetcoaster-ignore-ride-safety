# Feasibility: crash passengers using the game's guest physics ("RCT-style riders")

Branch `experimental/crash-rider-physics`, from the unfinished-rides checkpoint `4e34994`. **Investigation only: no behaviour change.**
Time box: about one hour of active work. About 20 minutes were used. The investigation stopped because no credible entry point for seated
passengers emerged (see "Result"), not because time ran out. Static analysis of targeted functions only; no full decompilation.
Addresses are for game build 1.13.3.88540.

## Result
| Question | Status |
|---|---|
| 1. The existing guest-physics / recovery system | **Found** (readable code) |
| 2. The mechanism that starts a physics incident | **Partly found**: the low-level start routine is readable; the code that decides to start one is in the protected region |
| 3. Whether that mechanism can safely work for seated passengers | **Not established**: no evidence either way |
| Train-destruction code as a targeted route | **Not credible** with current evidence (see below) |

The working crash loop (0.2.0-exp.6, passengers return to the exit) is the completed milestone and stays as it is.
Owner decision (2026-10-03): continue with one observation-only diagnostic round (plan below) before any behaviour prototype.

## Diagnostics: what the zero counts do and do not show
The 0.2.0-exp.6 crash observer counted `GuestPhysicsIncidentEnded` 0, `GuestHidden` 0 and trapped guests 0 over several crashes with riders aboard.
**This is not proof that guest physics never occurs in these crashes:**
* `GuestEnteredSoSFromCrashMessage` and `GroupPhysicsRecoveryMessage` are not exposed to Lua, so two of the relevant messages could not be observed at all.
* The receiver was never validated against a known physics incident. It has never been seen to count anything, so "missed" and "did not happen"
  cannot be told apart from these numbers alone.
* The counts are consistent with the owner's observation (riders reappear at the exit, nobody seen flying), and that observation is the stronger evidence.

**Correction to the first version of this file.** It said the train-removed handler `0x1405d3fb0` purges the passengers (via `0x14081a0a0`,
the routine behind `rides:PurgeAllRideGuests`), and called the zero count confirmed. That was a static reading, not an observation.
A later revision said the exp.4/exp.5 logs contradict it. That was also too strong: the earlier hook was **inconclusive**.
* Address: validated (fingerprints and original bytes checked before patching).
* Installation: validated indirectly (all sites are applied all-or-nothing, and another site of the same batch demonstrably worked).
* Coverage: **not validated.** The hook replaced only the handler's `IsClosed` call (`0x1405d40aa`), which comes after a branch
  (`+0x439`/`+0x438` flags), and it counted only ids on the Lua list. The id the handler passes (`[train+0x160]`) was never shown to be
  of the same kind as the listed station or ride ids. The close-request log was filtered the same way.
So whether that handler (and its purge) runs in these crashes is **unknown**. **Which code moves the riders to the exit is unidentified.**

## 1. The existing physics / recovery system (found)
* Per-tick guest-physics update `0x14067de10` (readable). It ends incidents (sends `GuestPhysicsIncidentEnded` via `0x1406f0860`),
  moves stranded guests to SOS (`GuestEnteredSoSFromCrash`, after `0x140680af0`), and handles group recovery (`GroupPhysicsRecovery`).
* State on the guest-system object: incident counter `+0x288`, timer `+0x290`, incident groups in a hash map at `+0x248`.
* The trapped-guest system (`guests:GetTrappedGuestCount` / `GetNextTrappedGuest`) exists.
* The achievement script awards `CoasterCrash` from `GuestPhysicsIncidentEndedMessage.nGuestsInvolved`. So the game was built with a crash path
  in which coaster riders become a physics incident. The unfinished-track derailment observed here does not use it.

## 2. The mechanism that starts an incident (partly found)
* **Low-level start routine `0x14067d450` (readable).** It adds a `PhysicsRigidBody` with a `PhysicsConvexHullShape` in `/MainPhysicsWorld`
  (surface group `Character`, surface `Flying`) to the guest entity. It then increments the incident counter and sets a 5 s timer.
  * `rcx` = guest-system object; `rdx` = pointer to the guest entity id; `r9` unused (its home slot is reused as a local).
  * `r8` = pointer to a record. `+0`: 32-bit incident-group key, which finds or creates a group (`0x1406e9110`). A group has room for
    about **6 guests** (one car); capacity is **not** checked here.
  * `+4..+0x10`: four 32-bit values copied into the guest's group slot. In readable code they are only ever written; their meaning is unknown.
  * It sets **no position and no velocity**, and has **no seat or ride code**: it does not detach a guest from a vehicle.
* **Decision code (protected).** The routine's only caller is at `0x14408ee64`, inside protected function `0x14408ee20`. Readable code reaches it
  only through a jump stub at `0x1406de9e0`, which has **no** static references (no call, pointer or RVA). The GuestPhysics manager's
  method table `0x1419e8c80` also points into the protected region. Following the trigger further means decompiling protected code,
  which is out of scope.
* The guest-system object can be captured at the entry of `0x14067de10` (aligned, `mov rax,rsp ; push rbp ; push rbx`). The update uses the same
  group map (`+0x248`) and `+0x198` field as the start routine.

## 3. Seated passengers (not established)
* The start routine does not detach a guest from a seat or train. Whatever the game does for `CoasterCrash` riders (detach, place, then start)
  happens in code that was not found, most likely the protected caller.
* Calling the start routine on a guest still attached to a train that is being destroyed is high risk (dangling seat or vehicle references).
* Calling it after the riders reach the exit is plausible but only launches them from the exit; that is not the requested behaviour.
* Passenger ids are available (seat occupants, `rides:GetGuestsOnRide`). Crash position is plausibly available; crash velocity is not established.

## Diagnostic round 1 plan (observation only)
Owner report (2026-10-03): the exp.6 milestone checks all passed (crash/reset loop, disabling the experimental option, safety and
nausea options). Objective unchanged: passenger physics at the crash site.

Three routines, each observed by an **unfiltered entry hook** (every call is recorded, whatever the ids), with return address,
time, thread and the ids each routine itself reads at entry:

| Routine | Why it is relevant | What its results distinguish |
|---|---|---|
| Train-removed handler `0x1405d3fb0` | Static candidate for crashed-train cleanup; contains two purge calls | Called at the crash or not; which caller; early-exit flags (`+0x438/+0x439`) say whether it can reach its purge calls; the ids it uses (`[train+0x160]`, `[train+0x2e0]`) |
| Purge `0x14081a0a0` (`rides:PurgeAllRideGuests`) | The only known routine that moves riders off a ride to the exit | Whether riders are moved by this routine at the crash, and from which caller (handler, script, or one of two unexplained callers `0x14081bb0f` / `0x140856a86`) |
| Guest-physics start `0x14067d450` | The routine that makes a guest a flying physics body | Whether any guest enters physics at the crash; if so, whether the guest was a passenger of the listed ride or a bystander |

Controls:
* **Installation:** after enabling, all hook bytes are read back and the result is logged. Build checks cover the bytes after each hook.
* **Positive control (known event):** the ride info panel's "move entrance"/"move exit" buttons call `rides:PurgeAllRideGuests`
  (game script), which calls the purge routine from `0x14046c099`. Clicking one must produce a purge event with that caller.
* **Crash anchor:** the existing crash-close hook (`0x140815460` called from the crash handler) records a timestamped event per crash.
* **Riders:** every 3 s the scripts send the guest ids of riders on open untested rides (`rides:GetGuestsOnRide`), so a physics
  start can be classed as passenger or not, and the log shows the rider count before and after the crash.

Limits (stated before the test):
* There is no known in-game event that starts guest physics, so the physics-start hook has no positive control. A zero count rests on the
  read-back check and on the same hook mechanism working for the purge control.
* Passenger matching compares the physics routine's guest id with the ids from `rides:GetGuestsOnRide`. That binding pushes the game's own
  64-bit ids as integers (static reading of `0x140464d10`/`0x1403a4ad0`), so both sides should be the same kind of id, but this has not
  been observed in game. A physics start that matches no rider is therefore still ambiguous (bystander, rider of another ride, or id mismatch).
* If neither the handler nor the purge runs at the crash, the code that moves riders stays unidentified. That is a blocker for this
  route, and this round stops there.
* A purge at the crash, followed by riders reaching the exit, is timing evidence. It is strong but not proof that it is the only mover.
* Observation hooks run inside game code and can still cause crashes or slowdowns. This build is not risk-free.

## Diagnostic build 0.2.0-diag.1 (built, not yet tested)
Package `IgnoreRideSafety-DIAGNOSTIC-0.2.0-diag.1.zip` (built by `tools/make_diagnostic.sh`; not published). Behaviour is that of
0.2.0-exp.6. Added only while the EXPERIMENTAL option is on:
* entry hooks at `0x1405d3fb0`, `0x14081a0a0`, `0x14067d450` (sites 7-9, `native/irs_diag.c`). Each saves the argument registers,
  reads the fields named above, writes one event to a 256-entry ring buffer and continues into the original code. No logging and no
  allocation inside a hook. The log is written from the script thread every 3 s (at most 48 event lines per report, 3000 in total;
  dropped or lost events are counted);
* a crash anchor event in the existing crash-close hook;
* rider ids for open untested rides (with a marker giving the number of riders the script found), every 3 s.
Build checks: the 10 sites plus 24 fingerprints, verified against the executable. Hooks are applied all-or-nothing and read back
after enabling ("diag: observation hooks installed and read back OK").
Version labels: package / scripts / helper each say 0.2.0-diag.1. The options header shows
"0.2.0-diag.1 DIAGNOSTIC (scripts 0.2.0-diag.1, helper diag.1)" and flags a mismatching helper.
`IgnoreRideSafety/PACKAGE-VERSION.txt` lists all three. `tools/dev/switch-install.sh --status` prints package and helper separately,
and the switch saves the installed log before replacing the folder.

Log lines to read after the test:
* `diag: observation hooks installed and read back OK` (installation);
* `PURGE from 0x14046c09e (script rides:PurgeAllRideGuests (positive control))` (known-event control, with the ride's id);
* around each `CRASH-CLOSE ... listed`: any `TRAIN-REMOVED`, `PURGE` and `PHYSICS-START` events, their callers and times;
* `riders on open untested rides: N` before and after the crash.

## Earlier proposal (superseded by the plan above)
An **observe-only** helper build: log-only entry hooks on the purge routine `0x14081a0a0`, the train-removed handler `0x1405d3fb0` and the start
routine `0x14067d450`, each recording its return address. During one crash on a copied park it would show which of them runs, which code moves
riders to the exit, and whether any incident is started. It changes no behaviour, so detachment, exit unloading and recovery stay as the game
does them. Recovery: untick the option, or switch back to exp.6 with the game closed. A behaviour prototype (detach, place at the crash site,
start an incident, then let the game recover the guests) should only be considered if that log reveals readable detach code.

## Version labels (0.2.0-exp.6 milestone)
The package and its Lua scripts are **0.2.0-exp.6**. The native helper `IgnoreRideSafety.dll` is unchanged from 0.2.0-exp.5 (byte-identical,
sha256 `4f37f8de…`), so its log header says "Ignore Ride Safety 0.2.0-exp.5". `tools/dev/switch-install.sh --status` reads the DLL and also
prints "0.2.0-exp.5". The in-game Settings heading shows the Lua (package) version.
**Before any future publication:** the diagnostics must name both parts, e.g. "package 0.2.0-exp.6 (scripts 0.2.0-exp.6, helper 0.2.0-exp.5)".
This applies to the log header, the in-game diagnostics list and `--status` (which should also read the package version).
