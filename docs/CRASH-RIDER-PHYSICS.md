# Feasibility: crash passengers using the game's guest physics ("RCT-style riders")

Branch `experimental/crash-rider-physics`, from the unfinished-rides checkpoint `4e34994`. **Investigation only: no behaviour change.**
Scope: targeted static analysis plus owner-run tests on a copied park; no full decompilation and no deobfuscation of the protected code.
Addresses are for game build 1.13.3.88540. Later sections are kept in the order the work happened; this summary is current.

## Current status (2026-10-03): unresolved within the agreed scope, not disproven
**Live testing is closed for now. 0.2.0-exp.6 stays installed** (crash loop works; riders return to the exit).

| Question | Status |
|---|---|
| Existing guest-physics / recovery system | **Found** (readable per-tick update `0x14067de10`) |
| Code that takes riders off a crashed train | **Identified in game:** destroyed-vehicle listener → purge routine → riders placed at the exit |
| Train-removed handler `0x1405d3fb0` | **Not involved** in this crash type (observed: 0 calls) |
| Routine that starts a physics incident | **Found** (`0x14067d450`). When called from the update context with a fresh group key, the game accepted it (incident started, timer ran, incident-ended message sent) |
| Correct group key | **Not established in game.** Strong static evidence: the guest's group index (`GetGuestGroupID`) |
| Group state and recovery sequence the game expects | **Unknown.** Recovery acts only on group records in state `0x0b` (very likely "Physics"); what sets it, and in what order, is in the protected caller |
| Riders at the crash site | **Not investigated** (out of scope so far) |

Two separate things went wrong in the prototype test, and they must not be confused:
1. **Implementation failure (prototype's fault, explained):** the prototype supplied an **invented group ID** (`0x7a490010`) as the
   incident-group key. The game used that value as an index into its guest-group array, and the read went outside the array. That is the
   invalid lookup in the crash dump (`rax` = the invented key at the faulting instruction `0x1406a8f24`). This failure says nothing about
   whether the approach can work; it shows the key must be a real group ID.
2. **Remaining unknown (not a failure of the approach):** even before the crash, recovery did not complete. The guest stayed in physics after
   the incident-ended message, and the group never showed the "Physics" behaviour. The game evidently performs further steps around the
   start routine (at least putting the group into the Physics state). Those steps are not identified, because they are in the protected caller.

The approach is therefore **unresolved within the agreed scope, not disproven.**
**Condition for any further prototype:** no new prototype runs unless both of these have been established:
* the real group ID to pass;
* the required group-state and recovery sequence.

## Read-only research round 2 (2026-10-03, after closeout): group ID, entry and recovery sequence
Static analysis only; no build, install or live test. exp.6 stays installed. The stop condition is unchanged.

### Real group-ID source (established statically, high confidence)
* `guests:GetGroupDecisionState(groupID)` (`0x1403fbc90`) reads the record at `[guest manager + 0x3b8] + groupID * 0x250`.
  It names byte `+0x19` (sEndeavour) and byte `+0x1a` (sBehaviour) from string tables. In the sBehaviour table `0x0b` = **Physics**
  and `0x0a` = Trapped; the full table is Idle 0, Navigating 1, Lost 2, Queueing 3, OnRide 4, Exiting 5, AtShop 6, AtBench 7, AtEntertainer 8,
  Suspended 9, Trapped 0xa, Physics 0xb, AtSecurityGuard 0xc, ...
* `guests:GetGuestGroupID(guest)` (`0x1403f90a0`) returns the guest's group index: guest map `+0x390` → guest record (`+0x3b0`, 0x30 bytes)
  field `+0x28`. **The script group ID is the index into the 0x250-byte guest-group array.**
* In the game's own data flow, the incident-group key is that same index:
  * the per-tick physics update copies the incident group's key (`[node+0x10]`) into a message at `+0x18` (`0x14067ebd7`);
  * the receiver `0x1406a8ed0` reads `+0x18` and indexes the guest-group array with it. This is the access the invented ID broke;
  * the enter-Physics routine (below) stores the group index as the first field of the "GuestPhysics" component data, which has
    the same 20-byte layout as the record the physics start routine reads. The start routine therefore receives
    `{group index, 3-float vector, reason}`.
* Not proven: the message-type registration that would formally pair the sender at `0x14067ebd7` with receiver `0x1406a8ed0` is in
  the protected region. The pairing is inferred from the matching layout and from the crash.

### Entry into physics (readable except one link)
1. **Contact handler** `0x140589ea0`, called every tick from the world update (`0x1400b08c7`, next to the guest-physics update call
   `0x1400b0899`):
   * it filters physics contacts between the "Character" group and the "Flying" surface;
   * for each contact it asks three source checks (`0x140591fb0`, `0x140592290`, `0x140592490`);
   * if one accepts, it posts a per-guest **impact event**: `+0x18` guest id, `+0x20` vec3, `+0x2c` vec3, `+0x38` reason (default 1),
     `+0x40` source id.
2. **Receiver** `0x1406b58b0`:
   * maps the guest to its group (map `+0x218`) and checks the per-group entry `[+0x288][group]`;
   * then applies the **entry prerequisites** (below);
   * skips groups that already have a request pending (map `+0x238`);
   * posts a group request via `0x1406f0a00`: `+0x18` group index, `+0x1c` reason, `+0x20` vec3, `+0x2c` vec3, `+0x38` source id.
3. **Receiver** `0x1406a8bc0` re-checks the same prerequisites, then calls the **enter-Physics routine** `0x14069c8a0(system, &group,
   record, &vec3 at +0x2c, reason)`:
   * it ends the current behaviour through `0x14069cec0`, which removes that behaviour's per-guest component;
   * it gives the group a physics handle (`record+0x50`) if it has none;
   * it adds a **"GuestPhysics" component to every group member** (members are guest indices `[record+0]`..`[record+4]`) with data
     `{group index, vec3, reason}`;
   * it writes `0x010b` to `record+0x1a` (behaviour = Physics, next byte = 1).
4. **Protected link:** the reaction to the new "GuestPhysics" component that calls the start routine `0x14067d450` per guest. Its only
   caller is the protected `0x14408ee20`, and the GuestPhysics manager's method table points into the protected region. That the
   component data is what the start routine receives is inferred from the identical layout, not observed.

**Entry prerequisites** (identical in both receivers):
* group record `+8` ≠ 0 and `+9` ≠ 0 (meaning not identified);
* current behaviour is one of **Navigating, AtShop, AtEntertainer, AtSecurityGuard** (bit mask `0x1142`). **OnRide, Queueing, Lost,
  Idle and Exiting are not accepted.**
* if reason = 2 and `record+0xd8` = 1, a further check `0x1406cdf90` must fail (meaning not identified);
* the source id differs from a system value at `+0x9a50` (meaning not identified).

### Leaving physics (readable)
* Per tick, `0x14067de10` runs the incident timer (`+0x290`, 5.0 s). When it expires, it sends `GuestPhysicsIncidentEnded` (`0x14068043d`).
* Per incident group, once a per-member count equals the member count, it posts the group message with the group index.
  The per-member condition was not analysed in detail. A stranded-guest (SOS) path is at `0x140680af0`.
* Receiver `0x1406a8ed0` requires behaviour = Physics (`0x0b`) and `record+8`/`+9`. It then calls `0x14069cec0`, which for Physics
  **removes the "GuestPhysics" component from every member** (jump table: Physics → remove "GuestPhysics"; Trapped → remove "GuestSOS";
  Lost → remove "GuestLost"). The next behaviour is chosen later by the group AI.
* What removing the component tears down (rigid body, physics maps) is presumably the protected component handler's job. Not established.

### What this means
* The game's own design launches **guests on foot** (and whole groups) on contact, and recovers them **as a group**.
  Riders are never in an accepted behaviour. After the purge, the prototype's rider's group showed Lost/Idle, which is also not
  accepted, so the game's own path would have refused it.
* The prototype bypassed all of this: it called the start routine directly without the group state, component or group-wide entry,
  and with an invented ID. That explains the stuck guest as well as the crash, though the stuck guest is an inference.
* **Boundary reached:** the one remaining link (GuestPhysics component added → start routine and teardown on removal) is in the
  excluded protected code. Everything else in the entry and recovery sequence is identified statically, with the uncertainties listed.
* No live test was done. A game-native observation would be possible without any mod change (e.g. watching whether guests on a path
  that crash debris reaches are launched), but it is not planned.

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

## Diagnostic build 0.2.0-diag.1 (tested 2026-10-03, results below)
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

## Diagnostic round 1 result (0.2.0-diag.1, manual test 2026-10-03)
Owner's copied park, unfinished coaster (station 206, ride 204), about 15 minutes, 7 crashes with up to 24 riders each.
* **Installation:** "observation hooks installed and read back OK".
* **Positive control passed:** each "move entrance"/"move exit" click logged `PURGE from 0x14046c09e (rides:PurgeAllRideGuests)` with
  the ride id 204. The owner saw that ride's guests reappear at the exit. The hook method works on a known event.
* **Each crash:** six `CRASH-CLOSE` events (crash handler, ride 204, close skipped) within about 0.3 s, then about 1.8 s later **one**
  `PURGE from 0x14081bb14` with a single id that is neither the ride nor the station (354, 29224, 29722, 30104, 30533, 30985, 31391:
  a new id each crash, most likely the destroyed vehicle). The script's rider count went from 22-24 to 0 between the 3-s samples
  around it. 7 of 7 crashes followed this pattern.
* **Train-removed handler `0x1405d3fb0`: 0 calls** in the whole session (unfiltered entry hook, read back OK). It is not part of this crash.
* **Guest-physics start `0x14067d450`: 0 calls** in the whole session. No guest, passenger or bystander, entered guest physics.
* Side finding: ticking the experimental option (its attraction refresh) and editing the entrance each caused
  `PURGE from 0x140856a8b` for the station (206). Refreshing the attraction purges that station's guests, as closing a station does.

Caller `0x14081bb0f` is in `0x14081b9a0` (readable, reached through a dispatcher jump at `0x14088dcb4`). It walks a list of entities
and their sub-entities, keeps the ids that the purge system's ride/vehicle map (`+0x198`) knows, and purges those: a
"vehicle destroyed → remove its guests" listener.

### Conclusions (proportional to the evidence)
* **Detachment in this crash is identified with direct evidence:** the destroyed-vehicle listener calls the same purge routine as
  `rides:PurgeAllRideGuests`. Its timing (≈1.8 s after the crash close, every crash) matches riders leaving the ride. The positive control
  shows this routine puts guests at the exit. That is strong evidence, but it does not prove the purge is the only mover.
* **No transfer into physics happens in this crash.** The physics start routine is never called. So there is no game path to piggy-back on;
  any physics would have to be started by the mod.
* The guest-physics update `0x14067de10` is called unconditionally once per world update from `0x1400b06a0`. Its object (needed by the start
  routine) could be captured there.

### Remaining blocker
No mechanism has been observed or identified that **safely transfers a detached passenger into physics**:
1. the physics start routine has never been seen running in this game session, its own caller is protected, and the effect of calling it
   on a guest the game has just unloaded (and is walking from the exit) is unknown;
2. the purge places guests at the exit. Where in the purge that placement happens is not located, so there is no route yet to keep
   a passenger at the crash site.
Per the round's rules this diagnostic round stops here. Next step only by owner decision.

## Prototype 0.2.0-proto.1: one-rider physics launch (owner-approved, tested 2026-10-03: game crash)
Package `IgnoreRideSafety-PROTOTYPE-0.2.0-proto.1.zip` (not published). It contains everything of 0.2.0-diag.1 plus a separate option,
off by default and reset on park load: "PROTOTYPE (research): launch one crashed rider into guest physics". It works only while the
experimental option is on; turning the experimental option off also removes the prototype hook. Crash-site positioning is out of scope.

Sequence, at most one launch per crash and none while a launched rider is being watched (180 s):
1. The purge hook counts a crash when the destroyed-vehicle listener purges riders (caller `0x14081bb0f`).
2. Scripts, from 0.5 s to 10 s after the purge, check riders from the last list taken while riders were aboard. A rider qualifies only if:
   * it still exists: `GetGuestGroupID` succeeds and the guest is in `GetGuestsInGroup` of that group;
   * it has finished unloading: it is no longer in `GetGuestsOnRide`, and its group's `GetGroupDecisionState().sBehaviour` is
     not OnRide, Queueing, Physics, Trapped or AtSecurityGuard;
   * it is not leaving the park (Navigating without a via-target).
   The first qualifying rider arms the helper.
3. In the guest-physics update context: an entry hook on `0x14067de10` runs on that update's thread with its object, before
   the update body. It checks:
   * the arming is no more than 250 ms old;
   * the same physics object has been seen for at least 60 ticks;
   * the fields the start routine uses are present (`+0x198`, `+0x1a0`, `+0x1a8`, `+0x218`, non-empty maps);
   * the guest is **not** already in the physics guest map (`+0x228`);
   * the chosen group key is unused in the group map (`+0x248`).
   The two lookups re-implement the game's hash and node layout read-only. They are checked against the game's own code in an emulator
   (hash: 4000 random keys; full lookup: 400 queries on a test map; 0 mismatches) and covered by build checks on that code.
   Only then does it call `0x14067d450(system, &guest, &{key, 0,0,0,0}, 0)`. Any failed check is logged as "NOT launched: <reason>".
4. Logged after a launch:
   * the start result and the incident counter before and after;
   * when the guest leaves the physics guest map and when its group disappears (update thread, timestamped);
   * the group's behaviour changes, the guest disappearing or reappearing, the trapped count, and `GuestPhysicsIncidentEnded`
     messages (scripts).

Known limits (stated before the test):
* The guest checks are made by the scripts. The physics call happens on the next physics tick, at most 250 ms later. A guest could
  in theory change state in between.
* Group behaviour is per group, not per guest.
* The start routine has never been seen running. Calling it from the update entry, rather than from the game's own (protected) caller,
  may break assumptions about thread, locks or timing. The group key's meaning is unknown (a fresh key is used). The four motion
  values are zero.
* The 5-second timer is what the start routine sets. Whether the guest actually recovers after it is **unproven**; the log is meant
  to show it.
* Possible failures: game crash; guest stuck, frozen, invisible or permanently trapped; physics body never removed.
  Recovery: untick the prototype option; on any problem quit without saving and switch back to 0.2.0-exp.6.

## Prototype result (0.2.0-proto.1, manual test 2026-10-03)
Owner: "one stayed and then the game crashed". Log, times since the helper loaded:
* 34.1 s: crash close (6 events); 36.4 s: crash purge (destroyed-vehicle listener). Rider list before the crash: 24.
* 37.1 s: guest 443 eligible (exists, in its group, off the ride, group behaviour "Lost") → armed → **launched** in the update context
  (thread 448). The start routine returned 1, the incident counter went 0 → 1, the timer was set to 5.0, and the guest was in the
  physics guest map. The diagnostic hook logged the call as this mod's.
* Scripts: the group's behaviour alternated Lost/Idle. It **never became "Physics"**.
* 42.8 s (5.7 s after launch): `GuestPhysicsIncidentEnded`, 1 guest involved.
* Up to the last log line (48.4 s) the guest had **not** left the physics guest map and its group was still present.
* Then the game crashed (Frontier crash dump 21:47:39, kept locally and not in Git). It was an **access violation reading
  `0x11c816d8dd2` at `0x1406a8f24`, with `rax = 0x7a490010`, the prototype's invented group key**.

Crash analysis (static):
* `0x1406a8ed0` is a message receiver (reached through the dispatcher at `0x1406f2594`). For each message it takes a dword key at
  message `+0x18`, indexes an array of 0x250-byte records (`[system+0x3b8] + key*0x250`) and acts only if the record's byte `+0x1a` is
  `0x0b`, then calls `0x14069cec0`.
* `rides`/`guests:GetGuestGroupID` (`0x1403f90a0`) reads a guest's group index from the same kind of object: a per-guest array at
  `+0x3b0`, 0x30 bytes each, field `+0x28`. The record array at `+0x3b8` is therefore very likely the **guest-group** array, and the
  physics "group key" is the **guest's group index**, not a free value. This also fits the earlier findings: an incident group holds at most
  6 guests (the guest-group size limit), and the message names `GroupPhysicsRecovery` / `GuestEnteredSoSFromCrash`.
* The handler acts only on groups whose record byte `+0x1a` is `0x0b`, very likely the group's "Physics" behaviour. The start routine
  does not set it, and the scripts never saw "Physics". In the game's own path, the (protected) caller presumably puts the group into
  that state; how it does so is unknown.

Conclusions:
* **Implementation failure:** the crash came from the prototype's **invented group ID**. The game used it as an index into its guest-group array
  and read outside it. This was seen once, is explained by the prototype's input, and is not a game bug. It does not show that the approach is unworkable.
* **What worked:** the physics start routine can be entered from the update context without an immediate failure. The incident timer runs
  and the incident-ended message is sent.
* **What remains unknown:** recovery did not complete. The guest "stayed" and did not leave the physics map in ≥ 6 s after the incident
  ended, and the group never entered the Physics behaviour. Working recovery needs:
  * (a) the guest's real group ID as the key (strong static evidence; not confirmed in game);
  * (b) the group-state and recovery sequence the game performs around the start routine (at least putting the group into the
    Physics state, `0x0b`).
  (b) lives in the protected caller and is not identified. Using the real key alone would most likely avoid this crash, but the
  receiver would then skip the group (state not `0x0b`), probably leaving the guest stuck. That is a prediction, not a test result.

**Status: unresolved within the agreed scope, not disproven.** Live testing is closed. No further prototype unless (a) and (b) have been
established. The installed build was switched back to 0.2.0-exp.6 (hashes verified, and the owner confirmed it stays installed). The
prototype and diagnostic packages, the test logs and the crash dump are kept locally, outside Git.

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
