#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# switch-install.sh PACKAGE.zip [OVLDATA_DIR]
#   Replaces Win64/ovldata/IgnoreRideSafety with the IgnoreRideSafety folder from PACKAGE.zip
#   (a stable, EXPERIMENTAL or DIAGNOSTIC package). The installed log is copied next to the package first. Both use the same folder name, so exactly one version
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
  local h p
  h=$(strings -a "$d/IgnoreRideSafety.dll" 2>/dev/null | grep -o -m1 'Ignore Ride Safety \(helper \)\?[^:(]*' | sed 's/Ignore Ride Safety \(helper \)\?//; s/ *$//')
  if [ -f "$d/PACKAGE-VERSION.txt" ]; then p=$(head -1 "$d/PACKAGE-VERSION.txt" | tr -d '\r' | sed 's/^package //')
  else
    case "$(sha256sum "$d/Main.ovl" | cut -c1-16)" in        # packages made before PACKAGE-VERSION.txt existed
      cdf32a64ba53ab96) p="0.2.0-exp.6 EXPERIMENTAL (identified by Main.ovl hash)" ;;
      *) p="unknown (no PACKAGE-VERSION.txt; the in-game header shows the script version)" ;;
    esac
  fi
  echo "installed package: $p"
  echo "installed helper:  ${h:-unknown} (DLL sha256 $(sha256sum "$d/IgnoreRideSafety.dll" | cut -c1-16))"
  echo "installed Main.ovl sha256 $(sha256sum "$d/Main.ovl" | cut -c1-16)"
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
if [ -f "$OV/IgnoreRideSafety/IgnoreRideSafety.log" ]; then   # keep the log of the version being replaced
  LOGDIR=${IRS_LOG_BACKUP:-$(dirname "$(realpath "$ZIP")")/installed-logs}; mkdir -p "$LOGDIR"
  cp "$OV/IgnoreRideSafety/IgnoreRideSafety.log" "$LOGDIR/IgnoreRideSafety-$(date +%Y%m%d-%H%M%S).log"
  echo "saved the previous log to $LOGDIR"
fi
rm -rf "$OV/IgnoreRideSafety"
cp -r "$T/IgnoreRideSafety" "$OV/IgnoreRideSafety"
status "$OV"
