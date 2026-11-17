#!/usr/bin/env python3
"""Writes src/hooks/map_names.generated.h: map id -> English place name.

The names are the game's own, from the ROM: data/map/mapname.gp2 holds one
mapname_<lang>.bin per language, a table of (map id, string) records. The
Discord activity (src/hooks/discord_presence.cpp) always shows the English
one, whatever language the game runs in, floor or room included
("Stornway Castle - L1"); only the untranslated " - ???" suffix is dropped.

The header is a gitignored build artifact, like lang.generated.h; with no
dq9.nds the table is empty and the activity shows no place.
"""
import os
import re
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ROM_PATH = os.path.join(ROOT, "dq9.nds")
OUT_PATH = os.path.join(ROOT, "src", "hooks", "map_names.generated.h")
ARCHIVE = "data/map/mapname.gp2"
ENTRY = "mapname_en.bin"


def rom_file(rom, path):
    """The bytes of `path` in the ROM's file system (FNT + FAT)."""
    fnt_off, _, fat_off, _ = struct.unpack_from("<IIII", rom, 0x40)

    def directory(dir_id):
        entry = fnt_off + 8 * (dir_id & 0xFFF)
        sub_off, first_file, _ = struct.unpack_from("<IHH", rom, entry)
        p, file_id, out = fnt_off + sub_off, first_file, {}
        while True:
            t = rom[p]
            p += 1
            if t == 0:
                return out
            name = rom[p:p + (t & 0x7F)].decode("ascii")
            p += t & 0x7F
            if t & 0x80:
                out[name] = ("dir", struct.unpack_from("<H", rom, p)[0])
                p += 2
            else:
                out[name] = ("file", file_id)
                file_id += 1

    dir_id = 0xF000
    parts = path.split("/")
    for i, part in enumerate(parts):
        kind, value = directory(dir_id)[part]
        if i == len(parts) - 1:
            start, end = struct.unpack_from("<II", rom, fat_off + 8 * value)
            return rom[start:end]
        dir_id = value


def lz_decompress(data, pos, end, size=None):
    """Level-5's LZ: a flag byte per 8 items, MSB first; a set bit is a
    back-reference of two bytes, length (high nibble) + 3, distance (low 12
    bits) + 1. Reads data[pos:end], or stops at `size` bytes out."""
    out = bytearray()
    while pos < end and (size is None or len(out) < size):
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if pos >= end or (size is not None and len(out) >= size):
                break
            if flags & (0x80 >> bit):
                b0, b1 = data[pos], data[pos + 1]
                pos += 2
                dist = ((b0 & 0x0F) << 8 | b1) + 1
                if dist > len(out):
                    raise ValueError("bad back-reference")
                for _ in range((b0 >> 4) + 3):
                    out.append(out[-dist])
            else:
                out.append(data[pos])
                pos += 1
    return bytes(out)


def is_table(head):
    """A mapname_<lang>.bin header: record count, strings offset, and the
    first record (kind 0x65)."""
    if len(head) < 20:
        return False
    count, strings = struct.unpack_from("<II", head, 0)
    return 0 < count < 0x10000 and 16 < strings < 0x40000 and         struct.unpack_from("<H", head, 16)[0] == 0x65


def gpc2_entry(archive, name):
    """One file of a GPC2 archive, decompressed: after a 0x18-byte header,
    a 12-byte entry per file (name hash; name offset << 24 | data offset / 4;
    compressed size) and a word, then the LZ-compressed, NUL-separated names
    and the files' compressed data."""
    if archive[:4] != b"GPC2":
        raise ValueError("not a GPC2 archive")
    count = struct.unpack_from("<H", archive, 6)[0]
    entries = [struct.unpack_from("<III", archive, 0x18 + 12 * i)
               for i in range(count)]
    names_pos = 0x18 + 12 * count + 4
    names = lz_decompress(archive, names_pos, len(archive), 16 * count)
    # The data starts where the names' stream ends: the first file (data
    # offset 0) is the one whose header reads as a table there.
    first = min(entries, key=lambda e: e[1] & 0xFFFFFF)
    for base in range(names_pos, names_pos + 0x80):
        try:
            head = lz_decompress(archive, base, base + first[2], 24)
        except (ValueError, IndexError):
            continue
        if is_table(head):
            break
    else:
        raise ValueError("no file data found")
    for _, packed, csize in entries:
        name_off = packed >> 24
        entry_name = names[name_off:names.index(b"\0", name_off)].decode("ascii")
        if entry_name == name:
            pos = base + 4 * (packed & 0xFFFFFF)
            return lz_decompress(archive, pos, pos + csize)
    raise KeyError(name)


def map_names(rom):
    blob = gpc2_entry(rom_file(rom, ARCHIVE), ENTRY)
    if not is_table(blob):
        raise ValueError(ENTRY + " is not a map name table")
    strings = struct.unpack_from("<I", blob, 4)[0]
    names = {}
    p = 16
    while p < strings:
        kind, argc = struct.unpack_from("<HB", blob, p)
        args = struct.unpack_from("<%dI" % argc, blob, p + 4)
        p += 4 + 4 * argc
        if kind != 0x67 or argc != 2 or args[1] == 0xFFFFFFFF:
            continue
        start = strings + args[1]
        raw = blob[start:blob.index(b"\0", start)]
        if any(b >= 0x80 for b in raw):
            continue  # untranslated placeholders
        text = raw.decode("ascii").replace("<1>", "'").replace("<,>", ",")
        # "Stornway Castle - ???": a room the translation left unnamed.
        if text.endswith(" - ???"):
            text = text[:-len(" - ???")]
        if text and text != "None" and not re.search(r"<[^>]*>", text):
            names[args[0]] = text
    return names


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    names = {}
    if os.path.exists(ROM_PATH):
        with open(ROM_PATH, "rb") as f:
            names = map_names(f.read())
    else:
        print(f"gen-map-names: {ROM_PATH} not found, writing an empty table",
              file=sys.stderr)
    lines = [
        "// Generated by scripts/gen-map-names.py from the ROM; do not edit.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace dq9 {",
        "",
        "struct MapName {",
        "    uint16_t id;",
        "    const char* name;",
        "};",
        "",
        "// Sorted by id.",
        "inline constexpr MapName kMapNames[] = {",
    ]
    lines += [f"    {{{mid}, {c_string(text)}}}," for mid, text in sorted(names.items())]
    if not names:
        lines.append("    {0, nullptr},")
    lines += ["};", "", "}  // namespace dq9", ""]
    content = "\n".join(lines)
    old = open(OUT_PATH, encoding="utf8").read() if os.path.exists(OUT_PATH) else None
    if content != old:
        with open(OUT_PATH, "w", encoding="utf8", newline="\n") as f:
            f.write(content)
    print(f"gen-map-names: {len(names)} map names")


if __name__ == "__main__":
    main()
