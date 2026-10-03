#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make_release.sh VERSION  -> dist/IgnoreRideSafety-VERSION.zip (+ .sha256)
# Builds the DLL and the OVL from source and zips only what players need.
set -euo pipefail
VERSION=${1:?usage: make_release.sh VERSION}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build/release"
rm -rf "$BUILD"; mkdir -p "$BUILD" "$ROOT/dist"
"$ROOT/native/build.sh" "$BUILD/native" >/dev/null
"$ROOT/tools/build_pack.sh" "$BUILD/pack" >/dev/null
STAGE="$BUILD/zip"; mkdir -p "$STAGE/IgnoreRideSafety"
cp "$BUILD/pack/IgnoreRideSafety/Manifest.xml" "$BUILD/pack/IgnoreRideSafety/Main.ovl" "$STAGE/IgnoreRideSafety/"
cp "$BUILD/native/IgnoreRideSafety.dll" "$STAGE/IgnoreRideSafety/"
sed "s/@VERSION@/$VERSION/g" "$ROOT/docs/README-release.txt" > "$STAGE/README.txt"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
ZIP="$ROOT/dist/IgnoreRideSafety-$VERSION.zip"
rm -f "$ZIP"
( cd "$STAGE" && find . -exec touch -d "2026-10-03 00:00:00" {} + && zip -X -q -r "$ZIP" README.txt LICENSE.txt IgnoreRideSafety )
( cd "$ROOT/dist" && sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP").sha256" )
unzip -l "$ZIP"
cat "$ZIP.sha256"
