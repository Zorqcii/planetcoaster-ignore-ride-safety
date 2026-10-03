#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds IgnoreRideSafety.dll (x86-64 Windows DLL, no C runtime) on Linux.
# Requires: clang, lld-link and llvm-dlltool (LLVM). Tested with LLVM/clang 22.1.8.
set -euo pipefail
cd "$(dirname "$0")"
OUT=${1:-build}
mkdir -p "$OUT"
llvm-dlltool -m i386:x86-64 -d kernel32.def -l "$OUT/kernel32.lib"
clang --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector \
      -mno-stack-arg-probe -Wall -Wextra -Werror -c irs_patch.c -o "$OUT/irs_patch.obj"
rm -f "$OUT/IgnoreRideSafety.dll"
lld-link /nologo /dll /entry:DllMain /nodefaultlib /subsystem:windows /brepro \
         /out:"$OUT/IgnoreRideSafety.dll" "$OUT/irs_patch.obj" "$OUT/kernel32.lib"
echo "built $OUT/IgnoreRideSafety.dll"
