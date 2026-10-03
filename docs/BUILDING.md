# Building from source

Players do not need this; use the release ZIP. These steps were tested on Linux.

## Tools

| Tool | Tested version | Used for |
|---|---|---|
| clang, lld-link, llvm-dlltool (LLVM) | 22.1.8 | `IgnoreRideSafety.dll` (x86-64 Windows, no C runtime, reproducible with `/brepro`) |
| cobra-tools | 2026.09.28, commit `a7f596c8c` (<https://github.com/OpenNaja/cobra-tools>) | packing the Lua scripts into `Main.ovl` |
| Python | 3.11 for cobra-tools (its requirement) | |
| zip, sha256sum | any | release archive |
| luac (optional) | any 5.x | syntax check of the Lua sources |

cobra-tools setup, for example with `uv`:

```bash
git clone https://github.com/OpenNaja/cobra-tools && cd cobra-tools
git checkout a7f596c8c
uv venv -p 3.11 .venv
uv pip install -p .venv/bin/python "imageio>=2.26.0" "numpy>=1.26.4,<2.0.0" pillow "bitarray~=2.9.2"
```

## Build

```bash
native/build.sh                                      # -> native/build/IgnoreRideSafety.dll
COBRA_TOOLS=/path/to/cobra-tools tools/build_pack.sh # -> build/IgnoreRideSafety/{Main.ovl,Manifest.xml}
COBRA_TOOLS=/path/to/cobra-tools tools/make_release.sh 0.1.0-beta   # -> dist/IgnoreRideSafety-0.1.0-beta.zip
```

`build_pack.sh` lower-cases the script names (the game stores module names in lower case) and checks that the
packed scripts extract byte-for-byte identical.

Reproducibility: the DLL is bit-for-bit reproducible. `Main.ovl` is reproducible for a given build folder, but its
bytes change with the absolute build location (cobra-tools behaviour); its contents (the three scripts) are always identical,
which the round-trip check verifies. No build path is stored in the file.

## Checking a game executable (maintainers)

```bash
pip install pefile
python3 tools/check_exe.py "/path/to/Planet Coaster/PlanetCoaster.exe"
```
