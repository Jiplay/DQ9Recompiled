#!/usr/bin/env python3
"""Recover overlay 29's plaintext from its ROM bytes, with no gameplay.

Overlay 29 is a self-decrypting protection module; every other code image in
YDQP is ordinary. Its bytes in the ROM are not the bytes that execute, so a
bank compiled from them is decoded noise and the module has to fall back to
the interpreter. But the cipher is deterministic and all of its inputs are in
the overlay, so the bytes that DO execute are a pure function of the ROM.

Two layers, both implemented here from the guest's own routines:

Stage 1 (load time). Five static initialisers each walk a table of
  {region + BIAS, size + BASE} pairs through 0x021D9824, an XOR over the low
  24 bits of every word whose key chains from word to word and restarts at
  each region. That leaves the module's stubs and its stage 2 routines in
  plaintext for good.

Stage 2 (every call). A stub decrypts one target through 0x021D990C (a
  variant RC4 keyed from the stub's own pool), calls it, and encrypts it again
  through 0x021D9994 under a new key. So the ciphertext of a protected
  function changes on every call, but the plaintext it shows while running
  never does. That plaintext is what a bank has to be built from.

A stub also keeps two live words in its pool: its caller's LR, and the rolling
key. Generated code reads pools through the bus, so it stays correct, but those
words must stay out of live-byte validation or the stub could never run. So
bank_inputs() reports every pool as a data range. Neighbouring regions flip
between plaintext and ciphertext independently too, so this image's bank must
not coalesce functions: each one has to validate on its own.

    ov029_unprotect.py <overlay.bin> <out.bin>

Checked against page snapshots taken while the module ran under the
interpreter: every region matched byte for byte, including stage 2 regions
caught as ciphertext under a later key, which decrypt to the same plaintext.
prepare_dq9.py is the normal caller; run this directly to disassemble the result.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

OVERLAY_ID = 29
LOAD = 0x021D8A40
SIZE = 0x1EA0
END = LOAD + SIZE              # 0x021DA8E0, also the base of the size encoding
BIAS = 0x1200                  # addresses in the tables are stored biased
SIZE_BASE = END + BIAS         # 0x021DBAE0
STAGE1_SEED = 0xF03852CB
BRANCH_DELTA = 0xFFFFFB7E      # -0x482, added to a branch's imm24
STATIC_INIT = 0x021DA8B4       # zero-terminated initialiser table

# A protected call: 0x68 bytes of stub code, then its pool.
STUB_PROLOGUE = bytes.fromhex("f0002de90f002de9f000bde8")
POOL_LR, POOL_SIZE, POOL_KEY, POOL_TARGET = 0x68, 0x6C, 0x70, 0x74
STUB_BYTES = 0x78

M32 = 0xFFFFFFFF


def word(buf, addr):
    return struct.unpack_from("<I", buf, addr - LOAD)[0]


def set_word(buf, addr, value):
    struct.pack_into("<I", buf, addr - LOAD, value & M32)


def branch_class(value: int) -> int:
    """0x021D97E8: 0 not a branch, 1 BLX imm, 2 BL, 3 B."""
    top = (value >> 24) & 0xFF
    if top & 0x0E != 0x0A:
        return 0
    if top & 0xF0 == 0xF0:
        return 1
    return 2 if top & 1 else 3


def rebase_branch(value: int) -> int:
    """Shift a branch's imm24 and flip its link bit, as both layers do."""
    imm = (value + BRANCH_DELTA) & 0x00FFFFFF
    return ((value & 0xFF000000) ^ 0x01000000) | imm


def stage1(buf: bytearray, table: int) -> list[tuple[int, int]]:
    """0x021D9824 over one {region, size} table; returns the regions."""
    regions = []
    entry = table
    while word(buf, entry) != 0:
        key = STAGE1_SEED
        size = (word(buf, entry + 4) - END - BIAS) & M32
        start = (word(buf, entry) - BIAS) & M32
        regions.append((start, size))
        for addr in range(start, start + (size & ~3), 4):
            cipher = word(buf, addr)
            kind = branch_class(cipher)
            if kind in (1, 3):
                plain = rebase_branch(cipher)
                key = (key ^ (plain >> 24)) & 0x00FFFFFF
            else:
                if kind == 2:
                    cipher ^= 0x01000000
                plain = cipher ^ key
                key = (key ^ plain ^ (plain >> 8)) & 0x00FFFFFF
            set_word(buf, addr, plain)
        entry += 8
    return regions


class Rc4:
    """The module's RC4 variant (state at 0x021DA3CC, stream at 0x021DA458)."""

    def __init__(self, key: bytes):
        self.acc = 0xAA
        self.i = 0
        self.j = 0
        self.s = list(range(256))
        s = self.s
        j = 0
        k = 0
        for i in range(255, -1, -1):
            j = (j + key[k] + s[i]) & 0xFF
            k += 1
            if k >= len(key):
                k = 0
            s[i], s[j] = s[j], s[i]

    def next(self) -> int:
        s = self.s
        i = (self.i + 1 + self.acc) & 0xFF
        si = s[i]
        j = (si + self.j + self.acc) & 0xFF
        sj = s[j]
        self.i, self.j = i, j
        s[j], s[i] = si, sj
        return s[(si + sj) & 0xFF]


def stage2_key(key_value: int, size: int) -> bytes:
    """Key schedule shared by 0x021D990C and 0x021D9994."""
    v = key_value
    ror = lambda x, n: ((x >> n) | (x << (32 - n))) & M32
    words = (v ^ size, size ^ ror(v, 24), size ^ ror(v, 16), size ^ ror(v, 8))
    return struct.pack("<4I", *(w & M32 for w in words))


def stage2_decrypt(buf: bytearray, start: int, size: int, key_value: int) -> None:
    """0x021DA5FC in place. Byte 2 goes through the fixed table ~x."""
    rc4 = Rc4(stage2_key(key_value, size))
    for addr in range(start, start + size, 4):
        w = word(buf, addr)
        kind = branch_class(w)
        if kind in (1, 3):
            rc4.acc = (rc4.acc + (w >> 24)) & M32
            set_word(buf, addr, rebase_branch(w))
            continue
        b0, b1, b2, b3 = w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, w >> 24
        p0 = b0 ^ rc4.next()
        rc4.acc = b0
        p1 = b1 ^ rc4.next()
        rc4.acc = b1
        rc4.acc = (rc4.acc * b2 - b3) & M32
        plain = p0 | (p1 << 8) | ((~b2 & 0xFF) << 16) | (b3 << 24)
        if kind == 2:
            plain ^= 0x01000000
        set_word(buf, addr, plain)


def find_stubs(buf: bytes) -> list[int]:
    """Every protected-call stub, once stage 1 has exposed them."""
    found = []
    at = buf.find(STUB_PROLOGUE)
    while at >= 0:
        found.append(LOAD + at)
        at = buf.find(STUB_PROLOGUE, at + 4)
    return found


def unprotect(rom_bytes: bytes) -> tuple[bytes, dict]:
    if len(rom_bytes) != SIZE:
        raise SystemExit(f"overlay 29 is {len(rom_bytes)} bytes, expected {SIZE}")
    buf = bytearray(rom_bytes)

    # Stage 1, in the order the guest's static-init table runs. An initialiser
    # is `stmdb sp!,{lr}; add r0,pc,#4; bl 0x021D9824; ldm sp!,{pc}` and its
    # table starts right after, at +0x10.
    stage1_regions = []
    initialisers = []
    entry = STATIC_INIT
    while word(buf, entry) != 0:
        init = word(buf, entry)
        initialisers.append(init)
        stage1_regions += stage1(buf, init + 0x10)
        entry += 4

    # Stage 2: each stub's pool names its target, and the ROM pool key is the
    # one the ROM ciphertext was made with.
    stubs = []
    targets = {}
    for stub in find_stubs(buf):
        key = word(buf, stub + POOL_KEY)
        target = (word(buf, stub + POOL_TARGET) - BIAS) & M32
        size = (word(buf, stub + POOL_SIZE) - SIZE_BASE) & M32
        stubs.append({"stub": stub, "target": target, "size": size, "key": key})
        targets.setdefault(target, (size, key))
    for target, (size, key) in sorted(targets.items()):
        stage2_decrypt(buf, target, size, (key - SIZE_BASE) & M32)

    report = {
        "load": LOAD,
        "size": SIZE,
        "initialisers": initialisers,
        "stage1_regions": stage1_regions,
        "stubs": stubs,
        "targets": sorted(targets),
    }
    return bytes(buf), report


def bank_inputs(report: dict) -> tuple[list[int], list[tuple[int, int]]]:
    """Entry points and data ranges for a bank built from the plaintext.

    Stubs and their targets are only ever reached through computed transfers,
    and the crypt routines are the stage 1 regions that are not stubs. A
    stub's pool is data: its LR and key words change on every call, so they
    must not land inside a validated function body.
    """
    stubs = {stub["stub"] for stub in report["stubs"]}
    entries = set(stubs) | set(report["targets"])
    entries |= {start for start, _ in report["stage1_regions"]}
    pools = [(stub + POOL_LR, stub + STUB_BYTES) for stub in sorted(stubs)]
    return sorted(entries), pools


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("overlay", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    plain, report = unprotect(args.overlay.read_bytes())
    args.out.write_bytes(plain)
    print(f"[ov029] {len(report['stage1_regions'])} stage 1 regions, "
          f"{len(report['stubs'])} stubs, {len(report['targets'])} stage 2 targets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
