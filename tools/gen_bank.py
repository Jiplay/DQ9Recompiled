#!/usr/bin/env python3
"""Generate one recompiled bank behind a content-keyed stamp.

    gen_bank.py --recompiler <nds_recompile> --out <recomp dir> --bank <name>
                --stamp <file> -- <nds_recompile args except --out/--bank>

The stamp holds a SHA-256 over the recompiler binary, every file named on the
nds_recompile command line (config, image, candidates, ...), and the argument
list itself. If the stored key matches and every listed output still exists,
nothing runs and nothing is touched (CMake's Ninja generator restats custom
command outputs, so dependants stay up to date as well).

Otherwise the bank is generated into a scratch directory and copied into
<out> file by file, rewriting only files whose bytes changed. With
--stable-address-shards an edit that moves a few functions therefore
recompiles only the affected shards instead of the whole bank. Shards the new
run no longer produces are deleted, so the runner's glob never picks up a
stale body.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

STAMP_VERSION = "dq9-gen-bank-1"


def file_digest(h, path: Path) -> None:
    h.update(str(path).encode() + b"\0")
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)


def compute_key(recompiler: Path, bank: str, rest: list[str]) -> str:
    h = hashlib.sha256()
    h.update(STAMP_VERSION.encode() + b"\0" + bank.encode() + b"\0")
    file_digest(h, recompiler)
    for a in rest:
        h.update(a.encode() + b"\0")
        p = Path(a)
        if p.is_file():
            file_digest(h, p)
    return h.hexdigest()


def read_stamp(stamp: Path) -> tuple[str, list[str]]:
    try:
        lines = stamp.read_text().splitlines()
    except OSError:
        return "", []
    if not lines or not lines[0].startswith("key "):
        return "", []
    return lines[0][4:], [l for l in lines[1:] if l]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--recompiler", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--bank", required=True)
    ap.add_argument("--stamp", type=Path, required=True)
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    rest = args.rest[1:] if args.rest[:1] == ["--"] else args.rest
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    key = compute_key(args.recompiler.resolve(), args.bank, rest)
    old_key, old_files = read_stamp(args.stamp)
    if old_key == key and old_files and all((out / f).exists() for f in old_files):
        print(f"[gen-bank] {args.bank}: up to date ({len(old_files)} files)")
        return 0

    work = out / f".work-{args.bank}"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    cmd = [str(args.recompiler), *rest, "--out", str(work), "--bank", args.bank]
    log = work / "recompile.log"
    with open(log, "w") as lf:
        rc = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT).returncode
    if rc != 0:
        sys.stdout.write(log.read_text()[-4000:])
        print(f"[gen-bank] {args.bank}: nds_recompile failed ({rc}); log {log}")
        return rc

    produced = sorted(p.name for p in work.iterdir()
                      if p.is_file() and p.name != "recompile.log")
    if not any(n == f"{args.bank}_dispatch.c" for n in produced):
        print(f"[gen-bank] {args.bank}: no dispatch table produced")
        return 1
    changed = 0
    for name in produced:
        src, dst = work / name, out / name
        if dst.exists() and dst.read_bytes() == src.read_bytes():
            continue
        os.replace(src, dst)
        changed += 1
    body = re.compile(re.escape(args.bank) + r"(_\d\d)?\.c$")
    removed = 0
    for p in out.iterdir():
        if body.fullmatch(p.name) and p.name not in produced:
            p.unlink()
            removed += 1
    shutil.copyfile(log, out / f"{args.bank}.log")
    shutil.rmtree(work, ignore_errors=True)

    tmp = args.stamp.with_suffix(args.stamp.suffix + ".tmp")
    tmp.write_text(f"key {key}\n" + "".join(f"{n}\n" for n in produced))
    tmp.replace(args.stamp)
    print(f"[gen-bank] {args.bank}: {len(produced)} files, {changed} rewritten, "
          f"{removed} stale removed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
