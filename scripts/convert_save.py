#!/usr/bin/env python3
"""Convert a DS save wrapper (.duc Action Replay, .dsv, padded .sav...) into
the raw 64 KB image the runner reads (saves/DQ9Recompiled.sav).

The save is region independent (same layout and charset for EU/US), so the
conversion only strips whatever precedes the "DRAGON QUEST IX" marker and
keeps the 65536 bytes after it. Both slots' CRC32s are verified.

The title menu's "Import save file" row does the same conversion in game
(src/hooks/save_image.h).

usage: convert_save.py INPUT [OUTPUT]   (default OUTPUT: saves/DQ9Recompiled.sav
under Documents/DQ9Recompiled; an existing OUTPUT is backed up to OUTPUT.bak-N)
"""
import struct, sys, zlib
from pathlib import Path

MARK = b"DRAGON QUEST IX"
SIZE = 65536
SLOT = 32768


def check(img):
    ok = True
    for i in range(2):
        s = img[i * SLOT:(i + 1) * SLOT]
        if not s.startswith(MARK):
            print(f"slot {i}: no marker (empty slot?)")
            continue
        a, b = struct.unpack_from("<II", s, 16), struct.unpack_from("<I", s, 132)[0]
        good = (zlib.crc32(s[20:36]) == a[0]) and (zlib.crc32(s[136:136 + 28508]) == b)
        print(f"slot {i}: CRCs {'ok' if good else 'MISMATCH'}")
        ok &= good
    return ok


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    data = Path(sys.argv[1]).read_bytes()
    pos = data.find(MARK)
    if pos < 0:
        sys.exit("no DRAGON QUEST IX marker found")
    img = data[pos:pos + SIZE]
    if len(img) != SIZE:
        sys.exit(f"only {len(img)} bytes after marker, need {SIZE}")
    print(f"header {pos} bytes stripped, {len(data) - pos - SIZE} trailing bytes dropped")
    if not check(img):
        print("warning: checksum mismatch, writing anyway")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else \
        Path.home() / "Documents/DQ9Recompiled/saves/DQ9Recompiled.sav"
    if out.exists():
        n = 1
        while out.with_name(f"{out.name}.bak-{n}").exists():
            n += 1
        bak = out.with_name(f"{out.name}.bak-{n}")
        bak.write_bytes(out.read_bytes())
        print(f"backed up existing save to {bak}")
    out.write_bytes(img)
    print(f"wrote {out}")


main()
