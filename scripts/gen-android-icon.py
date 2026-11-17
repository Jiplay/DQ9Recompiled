#!/usr/bin/env python3
"""Writes the Android launcher icon from the ROM's cartridge banner icon.

usage: gen-android-icon.py [rom.nds or unpacked folder]
       (default: assets/eu, else dq9.nds, in the repo root)

The icon is the ROM's, so it is generated, never committed: this writes
android/app/icon-res/mipmap-xxxhdpi/ic_launcher_foreground.png, the layer
android/app/src/main/res/mipmap-anydpi/ic_launcher.xml points at. The 32x32
banner icon is scaled 9x (nearest neighbour) to 288 px, the 72 dp a launcher
mask shows at xxxhdpi, and centred on the 432 px (108 dp) layer; the
transparent ring around it shows the sky-blue background colour.
Decoding is ndsrecomp/tools/gen_nds_icon.py's. Needs no third-party packages.
"""
import os
import struct
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "ndsrecomp", "tools"))

from gen_nds_icon import banner_icon  # noqa: E402
from nds_folder import banner_image  # noqa: E402

OUT = os.path.join(ROOT, "android", "app", "icon-res", "mipmap-xxxhdpi",
                   "ic_launcher_foreground.png")
SCALE = 9       # 32 px -> 288 px = 72 dp at xxxhdpi (4 px per dp)
LAYER = 432     # 108 dp at xxxhdpi
MARGIN = (LAYER - 32 * SCALE) // 2


def encode_png(rows):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    side = len(rows)
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for pixel in row:
            raw += bytes(pixel)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", side, side, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) > 1:
        rom_path = sys.argv[1]
    else:
        rom_path = os.path.join(ROOT, "assets", "eu")
        if not os.path.isdir(rom_path):
            rom_path = os.path.join(ROOT, "dq9.nds")
    px = banner_icon(banner_image(rom_path))

    def source(i):
        j = (i - MARGIN) // SCALE
        return j if 0 <= i - MARGIN and j < 32 else None

    clear = (0, 0, 0, 0)
    columns = [source(x) for x in range(LAYER)]
    rows = []
    for y in range(LAYER):
        sy = source(y)
        if sy is None:
            rows.append([clear] * LAYER)
            continue
        # Colour 0 is transparent on the DS; inside the icon it is opaque.
        rows.append([clear if c is None else px[sy][c][:3] + (255,)
                     for c in columns])

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(encode_png(rows))
    print(f"Wrote {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
