#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make_diagnostic.sh VERSION  -> dist-diagnostic/IgnoreRideSafety-DIAGNOSTIC-VERSION.zip (+ .sha256)
# Observation-only diagnostic builds. Separate from make_release.sh and make_experimental.sh:
# never writes to dist/, dist-experimental/, build/release or build/experimental.
set -euo pipefail
VERSION=${1:?usage: make_diagnostic.sh VERSION   (e.g. 0.2.0-diag.1)}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
grep -q "\"$VERSION DIAGNOSTIC\"" "$ROOT/native/irs_patch.c" || { echo "helper version is not $VERSION"; exit 1; }
grep -q "sPackageVersion = \"$VERSION\"" "$ROOT/mod/IgnoreRideSafety/src/Database.IgnoreRideSafety.lua" || { echo "script version is not $VERSION"; exit 1; }
BUILD="$ROOT/build/diagnostic"
rm -rf "$BUILD"; mkdir -p "$BUILD" "$ROOT/dist-diagnostic"
"$ROOT/native/build.sh" "$BUILD/native" >/dev/null
"$ROOT/tools/build_pack.sh" "$BUILD/pack" >/dev/null
STAGE="$BUILD/zip"; mkdir -p "$STAGE/IgnoreRideSafety"
cp "$BUILD/pack/IgnoreRideSafety/Manifest.xml" "$BUILD/pack/IgnoreRideSafety/Main.ovl" "$STAGE/IgnoreRideSafety/"
cp "$BUILD/native/IgnoreRideSafety.dll" "$STAGE/IgnoreRideSafety/"
printf 'package %s DIAGNOSTIC\r\nscripts %s\r\nhelper %s (diagnostic build 1)\r\n' "$VERSION" "$VERSION" "$VERSION" > "$STAGE/IgnoreRideSafety/PACKAGE-VERSION.txt"
sed "s/@VERSION@/$VERSION/g" "$ROOT/docs/README-diagnostic.txt" > "$STAGE/README-DIAGNOSTIC.txt"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
ZIP="$ROOT/dist-diagnostic/IgnoreRideSafety-DIAGNOSTIC-$VERSION.zip"
rm -f "$ZIP"
( cd "$STAGE" && find . -exec touch -d "2026-10-03 00:00:00" {} + && zip -X -q -r "$ZIP" README-DIAGNOSTIC.txt LICENSE.txt IgnoreRideSafety )
( cd "$ROOT/dist-diagnostic" && sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP").sha256" )
unzip -l "$ZIP"; cat "$ZIP.sha256"
