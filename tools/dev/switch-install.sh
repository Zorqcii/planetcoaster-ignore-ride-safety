#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# switch-install.sh PACKAGE.zip [OVLDATA_DIR]
#   Replaces Win64/ovldata/IgnoreRideSafety with the IgnoreRideSafety folder from PACKAGE.zip
#   (a stable or an EXPERIMENTAL package). Both use the same folder name, so exactly one version
#   of the mod and its DLL can be installed at a time. The game must be closed.
#   switch-install.sh --status [OVLDATA_DIR]   shows what is installed.
# OVLDATA_DIR defaults to the Steam library that contains Planet Coaster.
set -euo pipefail
find_ovldata() {
  local lib vdf
  for vdf in "$HOME/.local/share/Steam/steamapps/libraryfolders.vdf" "$HOME/.steam/steam/steamapps/libraryfolders.vdf"; do
    [ -f "$vdf" ] || continue
    while read -r lib; do
      [ -d "$lib/steamapps/common/Planet Coaster/Win64/ovldata" ] && { echo "$lib/steamapps/common/Planet Coaster/Win64/ovldata"; return; }
    done < <(sed -n 's/.*"path"[[:space:]]*"\(.*\)".*/\1/p' "$vdf")
  done
  return 1
}
status() {
  local d="$1/IgnoreRideSafety"
  if [ ! -d "$d" ]; then echo "not installed"; return; fi
  local v; v=$(strings -a "$d/IgnoreRideSafety.dll" 2>/dev/null | grep -o 'Ignore Ride Safety [^:]*' | head -1 | sed 's/Ignore Ride Safety //')
  echo "installed: ${v:-unknown} (DLL sha256 $(sha256sum "$d/IgnoreRideSafety.dll" | cut -c1-16))"
  ls "$1" | grep -i -E 'ignoreride|irs' | grep -v '^IgnoreRideSafety$' && echo "WARNING: other mod copies present above - remove them" || true
}
if [ "${1:-}" = "--status" ]; then OV=${2:-$(find_ovldata)}; status "$OV"; exit; fi
ZIP=${1:?usage: switch-install.sh PACKAGE.zip [OVLDATA_DIR] | --status}
OV=${2:-$(find_ovldata)} || { echo "could not find Planet Coaster; pass the ovldata folder"; exit 1; }
if pgrep -f 'PlanetCoaster\.exe' >/dev/null; then echo "Planet Coaster is running - close it first"; exit 1; fi
unzip -l "$ZIP" | grep -q 'IgnoreRideSafety/IgnoreRideSafety.dll' || { echo "not an Ignore Ride Safety package: $ZIP"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
unzip -q "$ZIP" -d "$T"
for f in Main.ovl Manifest.xml IgnoreRideSafety.dll; do [ -f "$T/IgnoreRideSafety/$f" ] || { echo "package incomplete ($f)"; exit 1; }; done
rm -rf "$OV/IgnoreRideSafety"
cp -r "$T/IgnoreRideSafety" "$OV/IgnoreRideSafety"
status "$OV"
