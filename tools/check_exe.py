#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""check_exe.py PATH/TO/PlanetCoaster.exe

Maintainer tool: checks that a local PlanetCoaster.exe matches every fingerprint and patch site
declared in native/irs_patch.c (PE timestamp + exact original bytes). Players do not need this;
the DLL performs the same checks in-game and refuses to patch on any mismatch.
Requires: pip install pefile
"""
import os
import re
import sys

import pefile

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "native", "irs_patch.c")


def main(exe):
    src = open(SRC).read()
    pe = pefile.PE(exe, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    raw = open(exe, "rb").read()

    def read(va, n):
        rva = va - base
        for s in pe.sections:
            if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                off = s.PointerToRawData + (rva - s.VirtualAddress)
                return raw[off:off + n]
        return b""

    ok = True
    ts = int(re.search(r"#define PE_TIMESTAMP (0x[0-9a-f]+)u", src).group(1), 16)
    good = pe.FILE_HEADER.TimeDateStamp == ts and base == 0x140000000
    print(f"timestamp {pe.FILE_HEADER.TimeDateStamp:#x} image base {base:#x}: {'OK' if good else 'MISMATCH'}")
    ok &= good
    for m in re.finditer(r"\{(0x[0-9a-f]+)ULL, (\d+), \{([^}]*)\}\}", src):
        va, n = int(m.group(1), 16), int(m.group(2))
        want = bytes(int(x, 16) for x in m.group(3).replace("\n", " ").split(",") if x.strip())
        good = len(want) == n and read(va, n) == want
        print(f"fingerprint {va:#x} ({n} bytes): {'OK' if good else 'MISMATCH'}")
        ok &= good
    for m in re.finditer(r"\{(0x[0-9a-f]+)ULL, (\d+), \{([^}]*)\}, \{([^}]*)\}\}", src):
        va, n = int(m.group(1), 16), int(m.group(2))
        orig = bytes(int(x, 16) for x in m.group(3).split(","))
        patch = bytes(int(x, 16) for x in m.group(4).split(","))
        good = read(va, n) == orig and len(orig) == len(patch) == n and (va & 7) + n <= 8
        print(f"patch site {va:#x}: {orig.hex()} -> {patch.hex()}: {'OK' if good else 'MISMATCH'}")
        ok &= good
    print("ALL OK" if ok else "NOT SUPPORTED")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
