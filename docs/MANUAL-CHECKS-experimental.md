# Manual checks: experimental unfinished-rides build

Use a **copy** of a park. Switch mod versions only with the game closed, and keep exactly one `Win64/ovldata/IgnoreRideSafety` folder.

1. **Install:** replace `Win64/ovldata/IgnoreRideSafety` with the folder from the experimental ZIP.
   The heading in Esc → Settings → Game should show `0.2.0-exp.6 EXPERIMENTAL`.
2. **Stable options, experimental off:** tick **Ignore ride safety concerns**, Apply, and play a few in-game days:
   "too intense" thoughts fade, while price, queue and not-intense-enough complaints still occur. Untick and they return.
   Repeat with **Ignore ride nausea**.
3. **Unfinished ride:** tick **EXPERIMENTAL…** and **Ignore ride safety concerns**, then Apply. Open an untested or unfinished coaster:
   guests queue and board, trains dispatch, and after a crash the ride stays **Open** and trains respawn.
   Passengers reappear at the exit.
4. **Own close:** the ride's Close button closes it.
5. **Disable:** untick EXPERIMENTAL…, Apply. The unfinished ride closes at its next crash or can no longer be opened,
   and new guests go back to "not until it's been tested".
6. **Restore published version:** close the game and replace the folder with the one from
   [v0.1.0-beta](https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.1.0-beta)
   (or [v0.2.0-exp.5](https://github.com/Zorqcii/planetcoaster-ignore-ride-safety/releases/tag/v0.2.0-exp.5)).
   The heading shows that version.
