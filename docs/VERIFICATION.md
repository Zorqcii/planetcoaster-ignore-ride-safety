# Verification status — 0.1.0-beta

Legend: **Game** = observed in the running game · **Log** = helper log / process inspection · **Code** = inferred from reading the game's machine code · **Pending** = to be checked in the release test.

| Claim | Evidence | Status |
|---|---|---|
| Helper loads via `package.loadlib` under Proton and recognises build 1.13.3.88540 | Log line on every launch; patch bytes seen in process memory | Game + Log |
| Unsupported/unexpected code → nothing changed | Fingerprint + timestamp checks before every write | Code (not exercised: no other build available) |
| Safety option reduces "too intense" refusals | Copy of a career park, 1,100+ guests, option alone: park-wide "too intense" thoughts 32 (off) → 13 → 2 (on) | Game |
| Safety option keeps "not intense enough" | Same run: those thoughts stayed (55 → 75) | Game + Code |
| Price refusal unaffected | Same run, safety on, one ride priced ×10: "too expensive" thoughts 0 → 347 | Game |
| Queue length / needs unaffected | Patched instructions only feed the fear and nausea results | Code |
| Safety option does not touch nausea | Separate patch sites; nausea block unchanged | Code · Pending (game) |
| Nausea option removes nausea-based reluctance | Player reports with the development build and the release build ("seems to work") | Game (informal player report) |
| Turning an option off restores original code | "original code restored" log lines; byte check in helper | Log + Code |
| Guest behaviour returns to normal after turning off | Not measured (that test phase was lost when the game was closed) | Pending |
| Both options off after loading a park / restarting | Manager disables both on park load and unload; log shows the sequence | Log + Code · Pending |
| Removing the mod restores normal play | All changes are in memory only | Code · Pending |
| Checkbox labels display | Initially blank; fixed by using the game's text symbol; release build reported working by the player (labels not separately confirmed) | Game (informal) |
| Release ZIP works without development files | Clean install from the ZIP (development copy removed first): log shows the release helper loading, recognising the build, enabling both options and restoring original code | Game + Log |
| Windows without Proton | — | Not tested |

## Release test (0.1.0-beta, 2026-10-03)
The player installed from the release ZIP into a clean `IgnoreRideSafety` folder (ACSE 0.2 files unchanged), played with the options and reported
"seems to work". Helper log from that session:

```
Ignore Ride Safety 0.1.0-beta: loaded, supported game build 1.13.3.88540
fear: enabled (guests ignore 'ride too intense')
nausea: enabled (guests ignore ride nausea rating)
fear: disabled (original code restored)
nausea: disabled (original code restored)
```

Items still marked *Pending* above were not separately reported in this test and remain beta caveats.
