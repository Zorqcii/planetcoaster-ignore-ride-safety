# Feasibility: crash passengers using the game's guest physics ("RCT-style riders")

Branch `experimental/crash-rider-physics`, from the unfinished-rides checkpoint `4e34994`. **Investigation only: no behaviour change.**
Bounded to about one hour of active work; about 6 minutes were used. Addresses are for game build 1.13.3.88540.

## Diagnostics re-checked: is "0 physics incidents" real?
* **Confirmed real.** When a crashed train is removed, the train-removed handler (`0x1405d3fb0`) calls the engine's "purge all guests from a ride"
  routine `0x14081a0a0` twice (`0x1405d403c` ride, `0x1405d4083` station). This is the same routine behind the script call `rides:PurgeAllRideGuests`.
  Passengers are moved straight to the exit; they never enter guest physics. The zero counts and the owner's observation agree.
* `GuestEnteredSoSFromCrashMessage` and `GroupPhysicsRecoveryMessage` exist natively but are not exposed to Lua, so the observer could not count them.
  Statically they are **sent** by the guest system's physics update (`0x14067de10`), which only acts on guests already in physics.

## The game's own "flung guest" mechanism
* **Entry point (credible):** `0x14067d450`. It adds to a guest a `PhysicsRigidBody` with a `PhysicsConvexHullShape` in `/MainPhysicsWorld`,
  surface group `Character`, surface `Flying`. It then counts the incident (`+0x288`) and sets a 5 s timer (`+0x290`, 5.0f).
* **Inputs (from its code):** `rcx` = guest-system object (the one holding `+0x288/+0x290`); `rdx` = pointer to the guest entity id;
  `r8` = pointer to a small record: a 32-bit value at `+0` (used as a lookup key) and four 32-bit values at `+4..+0x10`, copied into the guest's
  physics slot (likely launch motion; not confirmed).
* **Caller:** only one, at `0x14408ee64`, **inside the executable's protected (obfuscated) region**. The game's own decision to fling a guest,
  and how it fills the record, cannot be read with this project's targeted method.
* **Recovery / stranding:** handled in plain code by the per-tick update `0x14067de10`. It ends the incident (sends `GuestPhysicsIncidentEnded`
  via `0x1406f0860`) or moves a stranded guest to SOS (`GuestEnteredSoSFromCrash`, after `0x140680af0`). The trapped-guest system
  (`guests:GetTrappedGuestCount`) exists.
* Several methods of the `GuestPhysics` component manager (method table `0x1419e8c80`) are also in the protected region.

## Availability at crash time
* Passenger identities: yes (seat occupants; `rides:GetGuestsOnRide`; the purge routine iterates them).
* Crash position: plausibly yes (car/seat world transforms before the purge). Crash motion (velocity): not established.
* Guest-system object: not directly available in the train handler. It is passed as `rcx` to the guest update `0x14067de10` every tick
  (an aligned entry, `mov rax,rsp ; push rbp ; push rbx` = 5 bytes), so it could be captured there.

## Interactions
* Purge first, then fling: the guest is detached and valid but stands at the exit. It would be flung from the exit unless repositioned first;
  no safe reposition method is established.
* Fling before purge: the guest is still attached to a seat on a train being destroyed. High risk.
* After a fling, the game's own update would land, recover or strand the guest. The 32-bit key and four values in the record are not fully understood.

## Assessment
Plausible but uncertain. A credible entry point exists with partly understood inputs, but:
(1) the game's own caller is obfuscated, so correct argument values are inferred, not observed;
(2) passengers are already at the exit when they become safe to fling, and moving them back to the crash site is unsolved;
(3) the effects of the record's unknown fields, and of flinging guests that never came from the game's own trigger, are untested.
