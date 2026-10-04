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
* On **the entry path found here** (contact handler → impact event → two receivers → enter-Physics), the game launches **guests on
  foot** (whole groups) and recovers them **as a group**. **On this path**, riders are excluded: OnRide and Queueing are not accepted
  behaviours, and the prototype rider's group after the purge (Lost/Idle) would also have been refused. This exclusion applies only
  to the path found. Other entry paths, for example in the protected code, are neither found nor ruled out.
* The prototype bypassed all of this: it called the start routine directly without the group state, component or group-wide entry,
  and with an invented ID. That explains the stuck guest as well as the crash, though the stuck guest is an inference.
* **Boundary reached:** the remaining links are in the excluded protected code or are not identified (see "Unresolved" below).
* No live test was done.

### Unresolved (each keeps the prototype blocked)
| Item | Status |
|---|---|
| Entry checks: group record `+8`, `+9`; the reason-2 check `0x1406cdf90` with `record+0xd8`; source id vs system `+0x9a50` | Location known, **meaning not identified** |
| Pairing of the update's group message (`0x14067ebd7`) with receiver `0x1406a8ed0` | **Inferred** from matching layout and the crash; the type registration is in protected code |
| Pairing of the impact event and the group request with their receivers (`0x1406b58b0`, `0x1406a8bc0`) | **Inferred** from field layout and the shared prerequisites; type registration not read |
| "GuestPhysics" component added → physics start routine (launch) | **Hidden** (protected caller `0x14408ee20`); the link is inferred from the identical data layout |
| "GuestPhysics" component removed → teardown (rigid body, physics maps) | **Hidden**; not established |
| Per-member condition before the group recovery message | Readable but **not analysed** |
| Which bodies the three contact source checks accept | Readable but **not analysed** |
| Other entry paths (e.g. in protected code) | **Unknown** |
Prototype status: **blocked** (owner decision, 2026-10-03). The stop condition is unchanged.

### Bystander observation, session 1 (2026-10-03, about 22:50–23:10): **successful knockdown and recovery, observed with exp.6 enabled**
**Conditions:** these differed from the proposal, and the result is weighed accordingly.
* The owner's **main park** was used, not a disposable copy. It was not saved: no save file changed after 21:50 apart from Steam's
  `steam_autocloud.vdf` bookkeeping.
* The mod log shows **"Ignore ride safety", "Ignore ride nausea" and EXPERIMENTAL all ticked** for the session, so the coaster ran
  as an open untested ride with riders (the exp.6 crash loop), not as an empty test run. The exp.6 patch sites are the open gate, the
  rating evaluator, the two join checks, the IsClosed call and two close routines. Statically, none of them is in the contact,
  impact or physics code above. That is a static statement, not a verified one.
* Only one helper session appears in the log; all options were switched off at shutdown.

**Observed (owner report):**
* Riders of the crashed train reappeared at the exit (known exp.6 behaviour).
* **At least one bystander was hit by the crash and fell.** The owner identified the guest in the guest panel and read that guest's group
  there: 3 other members. **Whether the other members also fell was not reported, so no whole-group claim is made.**
* The guest **got up again** afterwards. **Recovery time was not measured**: the owner's estimate ("about 10 minutes ago, maybe") refers to
  when they got up relative to the report, not to how long they were down.
* Status line in the guest panel: **not recorded.** Stranded/trapped guests: **none reported.**
* Internal messages: **no logged evidence.** exp.6's crash observer shows its counts only in the options menu, the game is now closed and
  they were not read, so no claim is made that `GuestPhysicsIncidentEnded` or any other message fired.

**Result (owner-confirmed classification): a successful bystander knockdown and recovery, observed with exp.6 enabled.** In this game
build, crash debris knocked down a bystander on foot, and that guest later recovered on their own.
This is consistent with the statically found contact → impact → enter-Physics → recovery path, but it **does not prove** that path was
taken (no status line, no logging). Nothing here changes the "Unresolved" table. The prototype stays blocked.

**Still open from the proposal:** the displayed status, group-wide behaviour (members checked), recovery duration, and any stranded guests.
These would need a repeat under the original conditions: disposable copy, all options off, empty test run.

### diag.2 revision 2 (built and statically verified 2026-10-04; NOT installed, awaiting approval)
Superseding the plan below after the owner's review: (1) unproven-concurrency reads removed, (2) only single-instruction patch points.

**Patch points (9):** each replaces ONE instruction with ONE instruction of the same length at the same address, in one atomic
aligned 8- or 16-byte compare-exchange (`lock cmpxchg` / `lock cmpxchg16b`). No thread can be part-way through a patched span; it
either executes the old or the new complete instruction, or has passed it.

| # | Point | Kind | Unit |
|---|---|---|---|
| P1 | `0x1406f1fa4` jmp → impact receiver | redirect (dispatcher jump) | 16 |
| P2 | `0x1406f0a00` post group request, first instruction | entry | 8 |
| P3 | `0x1406f2634` jmp → request receiver | redirect | 16 |
| P4 | `0x14067d450` physics start, first instruction | entry | 8 |
| P5 | `0x140680af0` SOS step, first instruction | entry | 8 |
| P6 | `0x1406f2594` jmp → recovery receiver | redirect | 16 |
| P7 | `0x14069cec0` exit behaviour, first instruction | entry | 8 |
| P8 | `0x14046c099` call purge (script binding) | redirect | 8 |
| P9 | `0x140856a86` call purge (station purge) | redirect | 16 |

The three receivers are reached only through these dispatcher jumps, so coverage is complete. Not patchable this way, and dropped:
* enter-Physics: its entry is multi-instruction and its single call site straddles a 16-byte block. Entry is evidenced by its first
  step, the unconditional call to exit-behaviour (P7, return address `0x14069c8d1`);
* the crash-time purge call (`0x14081bb0f`, straddles a 16-byte block). The unloading sanity check covers only script and station purges;
* the per-tick physics-update hook (removed with its reads; see below).

**Read policy:** hook bodies read only:
* register values;
* memory on the current thread's own stack, checked at run time against the thread's stack bounds;
* game memory that the observed routine itself reads on that call **before any call, lock-prefixed instruction, xchg-with-memory or
  syscall** on its own path. This was verified offline for every mirrored read: impact receiver 27 reads, request receiver 12,
  recovery receiver 8, exit behaviour 2, purge 3. The physics-start group key is read by its first callee's first memory instruction
  after a straight-line path. Only **message 0** of a batch is examined, because later messages are read by the game after calls.
**Confirmed STATE** comes only from the game's public script functions on the script thread: displayed group behaviour, group
members, guest→group id, the trapped count, and the `GuestPhysicsIncidentEnded` message.

**No longer confirmable (accepted loss):**
* per-guest presence in the physics guest map and incident-group presence/removal; incident timer and counter values;
* previous behaviour and member list at the moment of enter-Physics (the scripts sample every 0.5 s and may miss short states);
* request reason/source at the post step; the motion vectors; messages after the first in a batch;
* source and pending checks when the reason-2 extra check applies;
* guest ids at launch or SOS when the id pointer is not on the calling thread's stack (logged as "not read");
* crash-time unloading.

**Checks run against the final package** (DLL `3d98c82c…`, ZIP sha256 `4a16d073…`):
* the 9 points: original bytes, single instruction, inside one aligned unit, no RIP-relative addressing, redirect targets correct;
* 61 build-check ranges match;
* no real branch, function-table entry or absolute pointer into any patched instruction's interior;
* RAX/R10/R11/XMM4/XMM5/flags are never read before being written in any of the 8 observed routines (all paths, including the
  exit-behaviour switch);
* all mirrored reads lie in the pre-call/pre-sync region (above);
* the 9 wrappers match the template and use their own trampoline slot; `pd_swap` uses `lock cmpxchg16b` and `lock cmpxchg`;
* the stub-page builder, run natively: stubs, entry trampolines, redirect slots (= original targets) and patch instructions all correct;
* the read-only lookups give 480/480 identical results to the game's code in an emulator;
* the package: a fresh build is byte-identical; no experimental, diag.1 or prototype exports or strings; the site table inside the DLL
  matches; the scripts in `Main.ovl` equal the source; the versions read 0.2.0-diag.2.

**Remaining limits:**
* Cross-modifying code: other cores are not forced to serialise. They may briefly keep executing the old instruction, which is valid,
  before seeing the new one. The patching thread serialises (`cpuid`). Intel/AMD document serialisation as the guaranteed protocol,
  so non-tearing of an unserialised fetch of a single aligned instruction is relied on (common hot-patching practice), not proven here.
* Same-thread equivalence: mirrored reads happen under the same conditions as the game's own unsynchronised reads; if the game itself
  races, so would these reads. No synchronisation of the game's structures was established beyond the game's own pattern.
* Patch-time `VirtualProtect`/`FlushInstructionCache` run without suspending other threads. No thread suspension is used.
* Predictions are logged as predictions; message pairings remain inferred; launch and teardown internals stay hidden.

**Test park:** the archived pre-test backup (2026-10-03 05:10) shows that 12 named parks in the live save folder are byte-identical to
it, so they are known pre-mod and pre-prototype. The main park ("ahhhh") and the two "IRS Test" copies are not in that backup and are
not used. The test uses a Save-As copy of one verified park; the original and the main park are re-checked by hash afterwards.

### Plan: logging-only diagnostic 0.2.0-diag.2 for natural bystander physics (proposal, not built)
Goal: connect one natural impact to its group request, Physics entry, per-guest launch and recovery, using real guest and group IDs.
**No injected launch, no state change, no bypassed check.** The rider prototype stays blocked, and its code (`irs_proto.c`) is **not part of
this build**.

**Activation:** a new checkbox "DIAGNOSTIC: log guest physics (observation only)", off by default. It installs **only** the logging hooks
below. It does **not** apply any gameplay patch: the open gate, rating evaluator, join checks, IsClosed and close hooks stay unpatched,
and safety, nausea and EXPERIMENTAL stay unticked. Hooks are applied all-or-nothing, with build checks, read back after install, and
removed on untick.

**Hooks** (entry hooks with the existing wrapper; each reads only what the hooked routine itself reads at that point; events go to the
existing ring buffer and are logged from the script thread). All entries are function starts, 8-byte aligned, with relocatable first
bytes inside one aligned word:

| # | Routine | Logged |
|---|---|---|
| D1 | impact receiver `0x1406b58b0` (new) | per event: guest id, reason, source id. Group index by a read-only lookup in its guest→group map (`+0x218`; same hash and node layout as the emulator-verified lookups). **Entry-check values:** group `+8`, `+9`, behaviour `+0x1a`, `+0xd8`, source id vs `+0x9a50`, per-group entry match, request already pending. The rule's outcome is logged as *predicted* accept/reject. |
| D2 | post group request `0x1406f0a00` (new) | group index, reason, source id, caller (impact receiver or `0x1406b4c30`) |
| D3 | request receiver `0x1406a8bc0` (new) | per request: group index and the same entry-check values, predicted accept/reject |
| D4 | enter-Physics `0x14069c8a0` (new) | group index, previous behaviour, reason, vec3, **member list** (guest ids of `[rec+0]..[rec+4]`, at most 16) |
| D5 | physics start `0x14067d450` (**reused**, diag.1 site 9) | guest id, group key, caller (expected: protected `0x14408ee69`) |
| D6 | guest-physics update `0x14067de10` (**reused** from proto.1, launch code removed) | for guests seen in D5: per-tick membership in the physics guest map and presence of their group key (emulator-verified read-only lookups), timer; transitions are logged with times |
| D7 | SOS step `0x140680af0` (new) | guest id (`[r9]`) when a guest goes to the stranded state |
| D8 | recovery receiver `0x1406a8ed0` (new) | group index, behaviour, `+8`, `+9`, predicted proceed/skip |
| D9 | exit-behaviour `0x14069cec0` (new) | only when the group's behaviour is Physics: group index and member guest ids (GuestPhysics removal) |
| D10 | purge `0x14081a0a0` (**reused**, diag.1 site 8) | crash anchor (destroyed-vehicle listener) and the known-event control |

**Script side (no game state changes):**
* register a receiver for `GuestPhysicsIncidentEnded` and log `nGuestsInvolved`; this is the only claim of a message firing, and it rests on the log;
* for the guests and groups seen in D4 (passed to the scripts through a small bit-wise read channel), log `GetGuestGroupID` and
  `GetGroupDecisionState().sBehaviour` every 0.5 s while active. This cross-checks the native group index against the script group ID
  and records the status the guest panel shows.
**Logging bounds:** event lines are capped (as in diag.1). D9 and D6 log only for groups and guests already seen in D4 and D5.

**What it could resolve:**
* the actual guest and group IDs along one natural chain (impact → request → enter-Physics → per-member launch → recovery), which tests
  the **inferred pairing** of the impact event, the request and the recovery message with their receivers through matching IDs and order;
* that the **script group ID equals the native group index** (cross-check), in practice;
* **which members** enter physics (D4 member list against D5 launches) and which leave, and when (D6, D9). This decides group-wide entry;
* whether the launch follows the GuestPhysics component add for each member with key = group index. This is observational support for the
  hidden link, though its internals stay hidden;
* the observed **values** of the entry checks for accepted and rejected impacts;
* recovery and stranding timing, and whether `GuestPhysicsIncidentEnded` fired (logged).

**What would remain:**
* the **meaning** of `+8`, `+9`, `+0xd8`, the reason-2 check and `+0x9a50` (only values are seen);
* the **internals** of the protected launch and teardown, and formal message-type registration (pairing would be supported, not proven);
* other entry paths: partially addressable, since a D5 launch without a preceding D4 would reveal one, but absence proves nothing;
* anything about riders or crash-site positioning.
**Risks:** hooks run inside hot game code (D6 every tick, D9 on every behaviour change). They can still crash or slow the game.
Recovery is to untick the option, or to switch back to 0.2.0-exp.6 with the game closed.

**Test procedure (owner, after approval and install):**
1. Load the park, then immediately **Save As** a new disposable name. That is the only save in the session; never save again.
2. In Settings → Game, tick **only** "DIAGNOSTIC: log guest physics". Leave safety, nausea and EXPERIMENTAL unticked. Check that the header
   reads 0.2.0-diag.2.
3. Known-event control: on any ride, click "move entrance" and then cancel.
4. Make sure the unfinished coaster is **closed** and its crash zone is over a busy path. Run up to **3 Test runs or 15 minutes**.
5. If a guest falls: click them and note the status line and whether the listed group members also fell, then time how long until they get up.
6. Stop on any instability. Quit **without saving**. The log is read afterwards; nothing needs to be sent.

### Proposed observation-only test of normal bystander physics (proposal; session 1 above ran with deviations)
Purpose: watch the game's **own** crash-to-bystander physics once, with no new code, to check the statically found path against reality.
* **Setup (disposable copy of a park):** an unfinished coaster whose open track end points over a busy footpath (debris lands among
  guests). The ride is **closed and in test mode**, which the base game allows and which crashes with no riders aboard. All mod options
  stay **off**, so the game behaves as unmodded (exp.6 changes nothing while its options are off). Run 2–3 test crashes and watch the path.
  Quit without saving.
* **Instrumentation:** none needed for the core result. Visual observation plus the game's own guest info panel, which shows the
  status "Physics" for a guest in that state. Optional and still without any new build: tick EXPERIMENTAL in exp.6 to use its existing
  read-only crash observer (IncidentEnded count with guests involved, trapped count) in Options > Game. This also validates that observer
  against a real incident. Ticking it applies exp.6's patches (the test-mode ride is not on its list) and its one-time station refresh.
* **What it could establish:**
  * whether crash debris launches bystanders in this game at all;
  * whether whole groups go into physics together (the group-wide entry);
  * whether launched guests recover (get up and walk on), roughly how long that takes, and whether any become trapped/SOS;
  * whether the game's `GuestPhysicsIncidentEnded` message fires (optional observer).
* **What it cannot establish:**
  * the meaning of the unidentified entry checks;
  * proof of the message pairing;
  * the hidden launch/teardown behaviour;
  * anything about riders.
  A **negative** result is inconclusive: the debris may simply not touch any guest, or the source checks may not accept coaster cars.
* **Optional later step (needs a new build, so not proposed now):** observation hooks on the contact handler, the two receivers and the
  enter-Physics routine would show the call order and the group IDs and would test the pairing. That needs owner approval to build and install.
* **Bounds:** one session of about 15 minutes, a disposable copy, at most 3 crashes. Stop on any instability.

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
