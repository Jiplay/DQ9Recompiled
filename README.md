# DQ9Recomp

Bring-up scaffold applying the [ndsrecomp](https://github.com/mstan/ndsrecomp)
framework (the `ndsrecomp` submodule) to **Dragon Quest IX:
Sentinels of the Starry Skies** (Europe, `YDQP`, 256 MiB, SHA-1
`ff761d349709f329c8ac4bd0023fdce21861e8a1`). Structure mirrors
`../MetroidPrimeHuntersRecomp`. You supply your own ROM as `dq9.nds`; no ROM or
generated code is committed.

## Status
- ROM identity check, ARM9 decompress (638,216 -> 1,000,984 B), ARM7 and 35
  ARM9 overlay extraction: working (`tools/prepare_dq9.py`).
- Main closures generate and are wired in CMake. Static discovery from the entry
  point alone finds only ~52 ARM9 functions, so most execution falls to the
  framework's Tier-3 interpreter until coverage is added.
- Not done: overlay banks (need coverage seeds), runtime/coverage banks, runner
  link (`NDS_TITLE_BANK_DIR` / `NDS_TITLE_ROM_SHA1` in ndsrecomp/runner), save
  type (`[cartridge]` in `game.toml` is a TODO until confirmed), BIOS/firmware dumps in `ndsrecomp/bios`.

## Build
    python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
    cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target dq9_romcheck dq9_generate_banks
    # then: cmake --build build --target dq9recomp   (compiles banks, slow)

Next step: build the ndsrecomp runner with
`-DNDS_TITLE_BANK_DIR=$PWD/generated/recomp -DNDS_TITLE_ROM_SHA1=ff761d349709f329c8ac4bd0023fdce21861e8a1`,
play via the interpreter, harvest Tier-3 coverage, and feed it to
`coverage/entry-points.json` as MPH does.
