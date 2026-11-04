#!/usr/bin/env python3
"""Score static scanner output against Tier-3 coverage manifests (offline).

Ground truth is the union of every entry in the given manifests
(`entry_points_<cpu>`, schema 4) plus, optionally, the promoted seeds in
coverage/entry-points.json. A union is the right truth for every region:
manifests captured after main seeds were built simply contribute ~0
main-image entries (see HANDOFF_STATIC_SCANNER.md §2a "Ground-truth bias").

Scanner output is one or more generated `<bank>_dispatch.c` files. A
dispatch row is an address the runtime can enter compiled code at: a
function start (the row's function name ends in its own address) or an
interior resume label of a discovered body. Recall counts rows, because a
Tier-3 entry with no row falls back to the interpreter even when it lies
inside a compiled body.

Each observed entry is classified, per region bucket:
  start        a function start in the observed mode
  interior     a dispatch row (inside a discovered body) but not a start
  mode         absent in the observed mode, present in the other one
  near_body    absent; a same-mode row lies <= NEAR bytes below (a body
               that stopped early or a split point: GapFill / Merge)
  missing/...  absent. Sub-split by the manifest's caller and kind:
               caller_known_call      caller is covered; a call we did not
                                      follow (unresolved BLX/BX reg)
               caller_known_indirect  caller is covered; an indirect jump
                                      (jump table / computed target)
               caller_unknown         the caller is itself unscanned
                                      (transitive; fix upstream first)

Overlay attribution (--overlay-inputs generated/inputs): many overlays share
a load address, so an address-only match credits a row in the WRONG
overlay's bank (2026-11-04: ov016 entries were "covered" by ov017 rows,
and ran interpreted). With this flag each observed overlay-window entry is
matched against the manifests' captured code pages: only overlays whose
image bytes equal the live bytes at the entry (32 bytes, clipped to the
page) are candidates, and only dispatch files named `*ovNNN_dispatch.c` of
those overlays can cover it. An entry whose live bytes match no overlay is
`no_image` (code written at run time; no static bank can hold it) and,
like `foreign`, is excluded from game recall. Entries on pages the
manifests did not capture fall back to address-only matching (counted).

Usage:
  score_static_scan.py --cpu arm9 --dispatch diag_arm9_dispatch.c \\
      [--seeds coverage/entry-points.json] [--log finder.log] \\
      [--overlay-inputs generated/inputs] \\
      [--list-misses 20] [--json report.json] captures/*.json
"""

from __future__ import annotations

import argparse
import base64
import bisect
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROM_SHA1 = "ff761d349709f329c8ac4bd0023fdce21861e8a1"
ROW_RE = re.compile(r"\{0x([0-9A-Fa-f]+)u, ([01])u, (\w+), ")
NAME_ADDR_RE = re.compile(r"_([0-9A-Fa-f]{8})$")
NEAR = 0x40

# (name, start, end) — half-open; first match wins, "other" catches the rest.
BUCKETS = {
    "arm9": [
        ("itcm", 0x01FF8000, 0x02000000),
        ("main", 0x02000000, 0x020F4618),
        ("overlay", 0x020F4618, 0x02400000),
        ("mainram_high", 0x02400000, 0x03000000),
        ("wram", 0x03000000, 0x04000000),
    ],
    "arm7": [
        ("main", 0x02380000, 0x02380000 + 167876),
        ("mainram_copy", 0x027E0000, 0x02800000),
        ("wram", 0x037F8000, 0x03810000),
    ],
}
MODES = ("arm", "thumb")

# Code the bootstrap firmware runs before the game (NDS_BOOTSTRAP_FIRMWARE):
# it is in no DQ9 image, so no game bank can cover it. Evidence
# (2026-11-04): ARM9 0x0232xxxx THUMB / 0x023FExxx ARM entries have
# byte-identical hit totals in boot, boot2, play1 and play2, and their bytes
# match no main/overlay image, decompressed or raw, at any offset. The ARM7
# WRAM and ITCM THUMB entries with that same boot-only invariance decode as
# neither mode of the relocated game bytes at their addresses (the game's
# copy later overwrites the firmware's).
FIRMWARE_WINDOWS = {"arm9": [(0x02300000, 0x02400000)], "arm7": []}
FIRMWARE_THUMB_BUCKETS = {"itcm", "wram"}
# Runtime spans that hold DQ9 bytes (image + proven code copies + overlay
# window). An observed entry outside all of them has no game bytes at all.
IMAGE_SPANS = {
    "arm9": [(0x01FF8000, 0x01FF8000 + 0x1740), (0x02000000, 0x020F4618),
             (0x020F4618, 0x022A2A60), (0x027E0000, 0x027E0000 + 0x61)],
    "arm7": [(0x02380000, 0x02380000 + 167876), (0x027E0000, 0x027E0000 + 0x17F5D),
             (0x037F8000, 0x037F8000 + 0x10E34)],
}
FOREIGN_CALLER_REACH = 0x200


def bucket_of(cpu: str, addr: int) -> str:
    for name, lo, hi in BUCKETS[cpu]:
        if lo <= addr < hi:
            return name
    return "other"


def load_dispatch(paths: list[Path]):
    rows = {m: set() for m in MODES}
    starts = {m: set() for m in MODES}
    for path in paths:
        for a, t, name in ROW_RE.findall(path.read_text(encoding="utf-8")):
            addr = int(a, 16)
            mode = MODES[int(t)]
            rows[mode].add(addr)
            m = NAME_ADDR_RE.search(name)
            if m and int(m.group(1), 16) == addr:
                starts[mode].add(addr)
    return rows, starts


OV_DISPATCH_RE = re.compile(r"ov(\d{3})_dispatch\.c$")
ATTR_BYTES = 32


def load_overlays(inputs: Path):
    """[(id, base, bytes)] for every overlay image below 0x02400000."""
    out = []
    for ov in json.loads((inputs / "overlays.json").read_text()):
        base = int(ov["load_address"], 16)
        if base < 0x02400000:
            out.append((ov["id"], base,
                        (inputs / "overlays" / ov["file"]).read_bytes()))
    return out


def load_pages(manifests: list[Path], cpu_num: int):
    """{manifest stem: {page addr: [snapshot bytes, ...]}}"""
    pages = {}
    for path in manifests:
        doc = json.loads(path.read_text(encoding="utf-8"))
        by = defaultdict(list)
        for e in doc.get("pages", {}).get("entries", []):
            if e.get("cpu") == cpu_num and e.get("data"):
                by[int(e["addr"], 16)].append(base64.b64decode(e["data"]))
        pages[path.stem] = by
    return pages


def resident_overlays(addr, stems, pages, overlays):
    """Overlay ids whose bytes match a captured snapshot at addr; None when
    no snapshot of that page exists in the manifests that observed it."""
    page = addr & ~0xFFF
    snaps = [d for st in stems for d in pages.get(st, {}).get(page, [])]
    if not snaps:
        return None
    off = addr - page
    hit = set()
    for d in snaps:
        live = d[off:off + ATTR_BYTES]
        for oid, base, img in overlays:
            o = addr - base
            if 0 <= o and img[o:o + len(live)] == live and len(img) - o >= len(live):
                hit.add(oid)
    return hit


def load_truth(cpu: str, manifests: list[Path], seeds: Path | None):
    truth: dict[tuple[int, str], dict] = {}
    sources = Counter()
    hits_by: dict[tuple[int, str], dict] = defaultdict(dict)
    for path in manifests:
        doc = json.loads(path.read_text(encoding="utf-8"))
        if doc.get("rom_sha1") != ROM_SHA1:
            raise SystemExit(f"{path}: rom_sha1 does not match YDQP")
        for e in doc.get(f"entry_points_{cpu}", []):
            key = (int(e["addr"], 16), e["mode"])
            hits_by[key][path.stem] = e.get("hits")
            if key not in truth:
                truth[key] = {"kind": e.get("kind", "?"),
                              "caller": e.get("caller"), "src": path.stem,
                              "stems": []}
                sources[path.stem] += 1
            truth[key]["stems"].append(path.stem)
    if seeds is not None:
        doc = json.loads(seeds.read_text(encoding="utf-8"))
        if doc.get("game_sha1") != ROM_SHA1:
            raise SystemExit(f"{seeds}: game_sha1 does not match YDQP")
        for e in doc.get("entry_points", {}).get(cpu, []):
            key = (int(e["addr"], 16), e["mode"])
            if key not in truth:
                truth[key] = {"kind": "seed", "caller": None,
                              "src": "seeds", "stems": []}
                sources["seeds"] += 1
    stems = {p.stem for p in manifests}
    for key, info in truth.items():
        h = hits_by.get(key, {})
        info["boot_invariant"] = (len(stems) > 1 and set(h) == stems
                                  and len(set(h.values())) == 1)
    return truth, sources


def is_firmware(cpu, bucket, addr, mode, info):
    if any(lo <= addr < hi for lo, hi in FIRMWARE_WINDOWS.get(cpu, [])):
        return True
    if not any(lo <= addr < hi for lo, hi in IMAGE_SPANS.get(cpu, [])):
        return True
    return (mode == "thumb" and bucket in FIRMWARE_THUMB_BUCKETS
            and info.get("boot_invariant", False))


def classify(addr, mode, info, rows, starts, sorted_rows, covered_any):
    other = MODES[1 - MODES.index(mode)]
    if addr in starts[mode]:
        return "start"
    if addr in rows[mode]:
        return "interior"
    if addr in rows[other]:
        return "mode"
    lst = sorted_rows[mode]
    i = bisect.bisect_left(lst, addr)
    if i > 0 and addr - lst[i - 1] <= NEAR:
        return "near_body"
    caller = info.get("caller")
    if caller and int(caller, 16) in covered_any:
        if info["kind"] == "indirect":
            return "caller_known_indirect"
        return "caller_known_call"
    return "caller_unknown"


CATS = ("start", "interior", "foreign", "no_image", "mode", "near_body", "caller_known_call",
        "caller_known_indirect", "caller_unknown")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cpu", choices=sorted(BUCKETS), required=True)
    ap.add_argument("--dispatch", type=Path, action="append", required=True)
    ap.add_argument("--seeds", type=Path, default=None,
                    help="coverage/entry-points.json (promoted seeds)")
    ap.add_argument("--log", type=Path, default=None,
                    help="nds_recompile output; [finder]/[reloc-scan] lines are echoed")
    ap.add_argument("--list-misses", type=int, default=0, metavar="N",
                    help="print up to N misses per bucket and category")
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--overlay-inputs", type=Path, default=None,
                    help="prepare_dq9 output dir: attribute overlay-window "
                         "entries to the resident overlay (see docstring)")
    ap.add_argument("manifests", type=Path, nargs="+")
    args = ap.parse_args()

    rows, starts = load_dispatch(args.dispatch)
    sorted_rows = {m: sorted(rows[m]) for m in MODES}
    covered_any = rows["arm"] | rows["thumb"]
    truth, sources = load_truth(args.cpu, args.manifests, args.seeds)
    attr = None
    attr_stats = Counter()
    if args.overlay_inputs and args.cpu == "arm9":
        overlays = load_overlays(args.overlay_inputs)
        pages = load_pages(args.manifests, 9)
        ov_rows = {}
        for path in args.dispatch:
            m = OV_DISPATCH_RE.search(path.name)
            if m:
                r, st = load_dispatch([path])
                ov_rows[int(m.group(1))] = (r, st)
        attr = (overlays, pages, ov_rows)

    table: dict[str, Counter] = defaultdict(Counter)
    misses: dict[tuple[str, str], list] = defaultdict(list)
    cats: dict[tuple[int, str], tuple[str, str]] = {}
    for (addr, mode), info in sorted(truth.items()):
        b = bucket_of(args.cpu, addr)
        cat = None
        if attr is not None and b == "overlay" and not is_firmware(
                args.cpu, b, addr, mode, info):
            overlays, pages, ov_rows = attr
            # Judge each manifest's observation on its own resident set:
            # the entry is covered only if every observation is.
            per = {st: resident_overlays(addr, [st], pages, overlays)
                   for st in dict.fromkeys(info["stems"])}
            seen = [r for r in per.values() if r is not None]
            if not seen:
                attr_stats["unattributed"] += 1
            elif not any(seen):
                attr_stats["no_image"] += 1
                cat = "no_image"
            else:
                attr_stats["attributed"] += 1
                info["resident"] = sorted(set().union(*seen))
                for res in seen:
                    if not res:
                        continue
                    r = {m: set() for m in MODES}
                    st = {m: set() for m in MODES}
                    for oid in res:
                        if oid in ov_rows:
                            for m in MODES:
                                r[m] |= ov_rows[oid][0][m]
                                st[m] |= ov_rows[oid][1][m]
                    c = classify(addr, mode, info, r, st,
                                 {m: sorted(r[m]) for m in MODES},
                                 r["arm"] | r["thumb"])
                    if cat is None or (c not in ("start", "interior")):
                        cat = c
                    if c not in ("start", "interior"):
                        break
        if cat is None:
            cat = classify(addr, mode, info, rows, starts, sorted_rows,
                           covered_any)
        if cat not in ("start", "interior") and is_firmware(
                args.cpu, b, addr, mode, info):
            cat = "foreign"
        cats[(addr, mode)] = (b, cat)
    # Transitive: a boot-only miss called from inside foreign code is
    # foreign too (the firmware's own ARM helpers in the same window).
    changed = True
    while changed:
        changed = False
        foreign = sorted(a for (a, _), (_, c) in cats.items() if c == "foreign")
        for key, (b, cat) in cats.items():
            if cat in ("start", "interior", "foreign"):
                continue
            info = truth[key]
            caller = info.get("caller")
            if not caller or not info.get("boot_invariant"):
                continue
            c = int(caller, 16)
            i = bisect.bisect_right(foreign, c) - 1
            if i >= 0 and c - foreign[i] <= FOREIGN_CALLER_REACH:
                cats[key] = (b, "foreign")
                changed = True
    for (addr, mode), (b, cat) in sorted(cats.items()):
        info = truth[(addr, mode)]
        table[b]["observed"] += 1
        table[b][cat] += 1
        if cat not in ("start", "interior"):
            misses[(b, cat)].append((addr, mode, info))

    print(f"cpu={args.cpu} truth={len(truth)} "
          + " ".join(f"{k}={v}" for k, v in sorted(sources.items())))
    print(f"scanner: arm_rows={len(rows['arm'])} thumb_rows={len(rows['thumb'])} "
          f"arm_starts={len(starts['arm'])} thumb_starts={len(starts['thumb'])}")
    if args.log:
        for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("[finder]") or (line.startswith("[reloc-scan]")
                                               and "steps=" in line):
                print("  " + line)
    if attr is not None:
        print("overlay attribution: " + " ".join(
            f"{k}={v}" for k, v in sorted(attr_stats.items())))
    hdr = f"{'bucket':<13}{'observed':>9}{'recall':>8}{'game':>8}" + "".join(
        f"{c[:12]:>13}" for c in CATS)
    print(hdr)
    order = [n for n, _, _ in BUCKETS[args.cpu]] + ["other"]
    report = {"cpu": args.cpu, "truth": len(truth), "sources": dict(sources),
              "buckets": {}}
    for b in order:
        c = table.get(b)
        if not c:
            continue
        hit = c["start"] + c["interior"]
        recall = hit / c["observed"]
        game = c["observed"] - c["foreign"] - c["no_image"]
        game_recall = hit / game if game else 1.0
        print(f"{b:<13}{c['observed']:>9}{recall:>8.1%}{game_recall:>8.1%}"
              + "".join(f"{c[k]:>13}" for k in CATS))
        report["buckets"][b] = {"observed": c["observed"], "recall": recall,
                                "game_recall": game_recall,
                                **{k: c[k] for k in CATS}}
    if args.list_misses:
        for b in order:
            for cat in CATS[3:]:
                items = misses.get((b, cat), [])
                if not items:
                    continue
                print(f"\n[{b} / {cat}] {len(items)}")
                for addr, mode, info in items[:args.list_misses]:
                    res = info.get("resident")
                    print(f"  0x{addr:08X} {mode:<5} {info['kind']:<8} "
                          f"caller={info.get('caller') or '-'} src={info['src']}"
                          + (f" resident=ov{','.join(map(str, res))}" if res else ""))
    if args.json:
        report["misses"] = {f"{b}/{cat}": [
            {"addr": f"0x{a:08X}", "mode": m, **info} for a, m, info in items]
            for (b, cat), items in sorted(misses.items())}
        args.json.write_text(json.dumps(report, indent=2) + "\n",
                             encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
