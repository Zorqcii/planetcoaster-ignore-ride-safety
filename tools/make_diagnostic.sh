#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make_diagnostic.sh VERSION  -> dist-diagnostic/IgnoreRideSafety-<LABEL>-VERSION.zip (+ .sha256)
# Research builds: *-diag.N = DIAGNOSTIC (observation only), *-proto.N = PROTOTYPE. Separate from make_release.sh and make_experimental.sh:
# never writes to dist/, dist-experimental/, build/release or build/experimental.
set -euo pipefail
VERSION=${1:?usage: make_diagnostic.sh VERSION   (e.g. 0.2.0-diag.1 or 0.2.0-proto.1)}
case "$VERSION" in *-diag.*) LABEL=DIAGNOSTIC; README=README-diagnostic.txt; BUILDNO=1 ;; *-proto.*) LABEL=PROTOTYPE; README=README-prototype.txt; BUILDNO=2 ;;
  *) echo "version must contain -diag. or -proto."; exit 1 ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
grep -q "\"$VERSION $LABEL\"" "$ROOT/native/irs_patch.c" || { echo "helper version is not $VERSION $LABEL"; exit 1; }
grep -q "#define DX_BUILD $BUILDNO " "$ROOT/native/irs_diag.c" || { echo "helper build number is not $BUILDNO"; exit 1; }
grep -q "sPackageVersion = \"$VERSION\"" "$ROOT/mod/IgnoreRideSafety/src/Database.IgnoreRideSafety.lua" || { echo "script version is not $VERSION"; exit 1; }
BUILD="$ROOT/build/diagnostic"
rm -rf "$BUILD"; mkdir -p "$BUILD" "$ROOT/dist-diagnostic"
"$ROOT/native/build.sh" "$BUILD/native" >/dev/null
"$ROOT/tools/build_pack.sh" "$BUILD/pack" >/dev/null
STAGE="$BUILD/zip"; mkdir -p "$STAGE/IgnoreRideSafety"
cp "$BUILD/pack/IgnoreRideSafety/Manifest.xml" "$BUILD/pack/IgnoreRideSafety/Main.ovl" "$STAGE/IgnoreRideSafety/"
cp "$BUILD/native/IgnoreRideSafety.dll" "$STAGE/IgnoreRideSafety/"
printf 'package %s %s\r\nscripts %s\r\nhelper %s (research build %s)\r\n' "$VERSION" "$LABEL" "$VERSION" "$VERSION" "$BUILDNO" > "$STAGE/IgnoreRideSafety/PACKAGE-VERSION.txt"
sed "s/@VERSION@/$VERSION/g" "$ROOT/docs/$README" > "$STAGE/README-$LABEL.txt"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
ZIP="$ROOT/dist-diagnostic/IgnoreRideSafety-$LABEL-$VERSION.zip"
rm -f "$ZIP"
( cd "$STAGE" && find . -exec touch -d "2026-10-03 00:00:00" {} + && zip -X -q -r "$ZIP" "README-$LABEL.txt" LICENSE.txt IgnoreRideSafety )
( cd "$ROOT/dist-diagnostic" && sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP").sha256" )
unzip -l "$ZIP"; cat "$ZIP.sha256"
