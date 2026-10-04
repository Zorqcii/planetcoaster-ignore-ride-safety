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

Recommendation: keep the working crash loop (0.2.0-exp.6) with passengers returning to the exit as the completed experimental milestone.

## Diagnostics: what the zero counts do and do not show
The 0.2.0-exp.6 crash observer counted `GuestPhysicsIncidentEnded` 0, `GuestHidden` 0 and trapped guests 0 over several crashes with riders aboard.
**This is not proof that guest physics never occurs in these crashes:**
* `GuestEnteredSoSFromCrashMessage` and `GroupPhysicsRecoveryMessage` are not exposed to Lua, so two of the relevant messages could not be observed at all.
* The receiver was never validated against a known physics incident. It has never been seen to count anything, so "missed" and "did not happen"
  cannot be told apart from these numbers alone.
* The counts are consistent with the owner's observation (riders reappear at the exit, nobody seen flying), and that observation is the stronger evidence.

**Correction to the first version of this file.** It said the train-removed handler `0x1405d3fb0` purges the passengers (via `0x14081a0a0`,
the routine behind `rides:PurgeAllRideGuests`), and called the zero count confirmed. The 0.2.0-exp.4/exp.5 logs contradict that this handler ran in
these crashes. Its purge calls (`0x1405d403c`, `0x1405d4083`) are on one straight path to its `IsClosed` call (`0x1405d40aa`), which exp.4/exp.5
hooked, yet across 18 crash closes:
* the hook never reported "kept a listed ride open" (exp.5 listed both the station id and the ride id);
* no close request came from the handler's own close call (only from the close-all routine `0x140815460`, called by the crash handler).
Possible explanations: the handler is not called for this crash type, or its early exit (`+0x439`/`+0x438` flags → `0x1405d4199`) skips the purge,
or the id it checks is of a type the list did not contain. **Which code moves the riders to the exit in this crash type is unidentified.**

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

## Smallest next experiment (only if this is revisited; not built)
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
