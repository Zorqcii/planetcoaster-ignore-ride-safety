# Third-party notices

This project is licensed under GPL-3.0-or-later (see LICENSE). This repository and the release ZIP contain only this project's own source code and build output. No third-party code,
game scripts, game executables or game assets are included.

| Component | Relationship | License |
|---|---|---|
| ACSE-PlanetCoaster by EvanMad (<https://github.com/EvanMad/ACSE-PlanetCoaster>) | Required at runtime; installed separately by the player; **not redistributed** | No license declared by the repository. It is a port of OpenNaja's ACSE (GPL-3.0). Because redistribution terms are unclear, it is linked rather than bundled. |
| cobra-tools by OpenNaja (<https://github.com/OpenNaja/cobra-tools>) | Build tool used to create `Main.ovl`; not redistributed | GPL-3.0 |
| LLVM / clang | Build tool; not redistributed | Apache-2.0 with LLVM exceptions |

Planet Coaster is a trademark of Frontier Developments plc. This project is an unofficial fan modification and is not affiliated with or endorsed by Frontier.
The native helper modifies a few bytes of the game's code in memory at runtime to provide the options described in the README. It does not include or distribute any part of the game.
