#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build_pack.sh [OUT_DIR]
# Packs mod/IgnoreRideSafety/src/*.lua into an OVL content pack (Main.ovl) with cobra-tools.
#   COBRA_TOOLS  path to a cobra-tools checkout (required)
#   COBRA_PY     Python interpreter with cobra-tools' requirements (default: $COBRA_TOOLS/.venv/bin/python)
# Tested with cobra-tools 2026.09.28 (commit a7f596c8c) on Python 3.11.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/mod/IgnoreRideSafety"
OUT=$(realpath -m "${1:-$ROOT/build}")
: "${COBRA_TOOLS:?set COBRA_TOOLS to a cobra-tools checkout}"
PY=${COBRA_PY:-$COBRA_TOOLS/.venv/bin/python}
# fixed staging path: cobra-tools output depends on the input folder name
STAGE="$OUT/.stage/IgnoreRideSafety"; rm -rf "$OUT/.stage"; mkdir -p "$STAGE"
VER=$(mktemp -d); trap 'rm -rf "$OUT/.stage" "$VER"' EXIT
for f in "$SRC"/src/*.lua; do
  b=$(basename "$f" | tr 'A-Z' 'a-z')          # the game stores Lua module names in lower case
  tr -d '\r' < "$f" > "$STAGE/$b"
  if command -v luac >/dev/null; then luac -p "$STAGE/$b"; fi
done
mkdir -p "$OUT/IgnoreRideSafety"
cp "$SRC/Manifest.xml" "$OUT/IgnoreRideSafety/Manifest.xml"
( cd "$COBRA_TOOLS" && PYTHONHASHSEED=0 "$PY" ovl_tool_cmd.py new -g "Planet Coaster" -c ZLIB -i "$STAGE" -o "$OUT/IgnoreRideSafety/Main.ovl" -f ) > "$VER/new.log" 2>&1 \
  || { cat "$VER/new.log"; exit 1; }
# round-trip check: the packed scripts must extract byte-identical
( cd "$COBRA_TOOLS" && "$PY" ovl_tool_cmd.py extract -g "Planet Coaster" -o "$VER/x" "$OUT/IgnoreRideSafety/Main.ovl" ) > "$VER/x.log" 2>&1
for f in "$STAGE"/*.lua; do cmp -s "$f" "$VER/x/$(basename "$f")" || { echo "round-trip mismatch: $(basename "$f")"; exit 1; }; done
echo "built $OUT/IgnoreRideSafety/Main.ovl ($(ls "$STAGE" | wc -l) scripts)"
