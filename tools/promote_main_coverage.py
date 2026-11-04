#!/usr/bin/env python3
"""Merge Tier-3 coverage manifests into main-image seeds (coverage/entry-points.json).

Only call/indirect targets inside the immutable ARM9/ARM7 main images are kept;
overlay and relocated-RAM code needs per-generation content-validated banks.
Output is a monotonic union with any existing seed file.
"""
import argparse, json
from pathlib import Path

GAME_SHA1 = "ff761d349709f329c8ac4bd0023fdce21861e8a1"
RANGES = {"arm9": (0x02000000, 0x02000000 + 0xF4618),
          "arm7": (0x02380000, 0x02380000 + 0x28FC4)}
SKIP_KINDS = {"resume"}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("manifests", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=Path("coverage/entry-points.json"))
    a = ap.parse_args()
    seeds = {"arm9": {}, "arm7": {}}
    if a.out.exists():
        old = json.loads(a.out.read_text())
        for cpu in seeds:
            for e in old.get("entry_points", {}).get(cpu, []):
                seeds[cpu][int(e["addr"], 0)] = e["mode"]
    for m in a.manifests:
        d = json.loads(m.read_text())
        if d.get("rom_sha1") != GAME_SHA1:
            raise SystemExit(f"{m}: manifest is for a different ROM")
        for cpu in seeds:
            lo, hi = RANGES[cpu]
            for e in d.get(f"entry_points_{cpu}", []):
                addr = int(e["addr"], 0)
                if lo <= addr < hi and e.get("kind") not in SKIP_KINDS:
                    seeds[cpu][addr] = e["mode"]
    out = {"schema": 1, "game_sha1": GAME_SHA1,
           "entry_points": {cpu: [{"addr": f"0x{k:08X}", "mode": v}
                                  for k, v in sorted(s.items())]
                            for cpu, s in seeds.items()}}
    a.out.write_text(json.dumps(out, indent=1) + "\n")
    print({cpu: len(s) for cpu, s in seeds.items()})

main()
