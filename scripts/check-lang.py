#!/usr/bin/env python3
"""Checks res/lang/*.toml: key parity with English, empty values, keys the
hooks use (Tr("section", "key", ...)) that English lacks, and keys nothing
uses ([options] aside: its keys are built at run time from the runtime
menu's rows)."""
import re
import sys
import tomllib
from pathlib import Path

root = Path(__file__).resolve().parent.parent
langs = {p.stem: tomllib.loads(p.read_text(encoding="utf8"))
         for p in sorted((root / "res/lang").glob("*.toml"))}
en = langs["en"]
problems = 0


def report(msg):
    global problems
    problems += 1
    print(msg)


def flat(table):
    return {(sec, k): v for sec, body in table.items() if isinstance(body, dict)
            for k, v in body.items()}


en_flat = flat(en)
for code, table in langs.items():
    if "name" not in table:
        report(f"{code}: no name")
    mine = flat(table)
    for key in sorted(mine.keys() - en_flat.keys()):
        report(f"{code}: not in English [{key[0]}] {key[1]}")
    for key in sorted(en_flat.keys() - mine.keys()):
        report(f"{code}: missing [{key[0]}] {key[1]}")
    for key, value in sorted(mine.items()):
        if not value.strip():
            report(f"{code}: [{key[0]}] {key[1]} is empty")

used = set()
pattern = re.compile(r'\bTr\(\s*"(\w+)"\s*,\s*"(\w+)"')
for path in (root / "src").rglob("*"):
    if path.suffix in (".cpp", ".h") and not path.name.endswith(".generated.h"):
        used.update(pattern.findall(path.read_text(encoding="utf8", errors="replace")))
for key in sorted(used - en_flat.keys()):
    report(f"en: used but missing [{key[0]}] {key[1]}")
for key in sorted(en_flat.keys() - used):
    if key[0] != "options":
        report(f"unused [{key[0]}] {key[1]}")

print(f"{problems} problem(s)" if problems else "ok")
sys.exit(1 if problems else 0)
