#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make_experimental.sh VERSION  -> dist-experimental/IgnoreRideSafety-EXPERIMENTAL-VERSION.zip (+ .sha256)
# Separate from make_release.sh: never writes to dist/ or build/release.
set -euo pipefail
VERSION=${1:?usage: make_experimental.sh VERSION   (e.g. 0.2.0-exp.1)}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build/experimental"
rm -rf "$BUILD"; mkdir -p "$BUILD" "$ROOT/dist-experimental"
"$ROOT/native/build.sh" "$BUILD/native" >/dev/null
"$ROOT/tools/build_pack.sh" "$BUILD/pack" >/dev/null
STAGE="$BUILD/zip"; mkdir -p "$STAGE/IgnoreRideSafety"
cp "$BUILD/pack/IgnoreRideSafety/Manifest.xml" "$BUILD/pack/IgnoreRideSafety/Main.ovl" "$STAGE/IgnoreRideSafety/"
cp "$BUILD/native/IgnoreRideSafety.dll" "$STAGE/IgnoreRideSafety/"
sed "s/@VERSION@/$VERSION/g" "$ROOT/docs/README-experimental.txt" > "$STAGE/README-EXPERIMENTAL.txt"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
ZIP="$ROOT/dist-experimental/IgnoreRideSafety-EXPERIMENTAL-$VERSION.zip"
rm -f "$ZIP"
( cd "$STAGE" && find . -exec touch -d "2026-10-03 00:00:00" {} + && zip -X -q -r "$ZIP" README-EXPERIMENTAL.txt LICENSE.txt IgnoreRideSafety )
( cd "$ROOT/dist-experimental" && sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP").sha256" )
unzip -l "$ZIP"; cat "$ZIP.sha256"
