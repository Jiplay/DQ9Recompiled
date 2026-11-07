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
- Static whole-ROM analysis (`tools/static_scan_dq9.py`, ~10 s, no gameplay)
  finds 100% of the observed game entry points in ARM9 main and ITCM, 99.9%
  in the overlays (scored against the resident overlay's own bank), and
  99.7% on ARM7, starting from the ROM entry alone. On a 1.5G-cycle no-input
  boot this cuts interpreted ARM9 instructions by 63% (53.9M → 20.1M). See
  `HANDOFF_STATIC_SCANNER.md` §11-12.
- Banks: ARM9 main, ARM7, 31 ARM9 overlays and the firmware's boot code
  (`DQ9_BANKS` in `CMakeLists.txt`). Every row is gated by the ROM SHA-1 and by
  live-byte validation.
- No interpreter: the game runs as compiled code from the first firmware
  instruction, and the runner below does not contain one. See
  [Without the interpreter](#without-the-interpreter).
- Save: 64 KiB EEPROM (`[cartridge]` in `game.toml`), raw 65,536-byte
  `.sav` images, interchangeable with emulator saves that have no footer.
  BIOS/firmware dumps go in `ndsrecomp/bios`.

## Build
    python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
    cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target dq9_romcheck dq9_generate_banks -j3

`dq9_generate_banks` runs `dq9_prepare` (ROM -> `generated/inputs/`), then
`dq9_analysis` (static scan -> `generated/analysis/`), then one
`nds_recompile` per bank through `tools/gen_bank.py`. Each bank's stamp is a
key over the recompiler binary, config, candidates, image and flags: an
unchanged input set skips regeneration, and a changed one rewrites only the
shard files whose bytes differ.

Then build the runner against the bank directory:

    cmake -G Ninja -S ndsrecomp/runner -B build-runner -DCMAKE_BUILD_TYPE=Release \
        -DNDS_BOOTSTRAP_FIRMWARE=ON -DNDS_TITLE_BANK_DIR=$PWD/generated/recomp \
        -DNDS_TITLE_ROM_SHA1=ff761d349709f329c8ac4bd0023fdce21861e8a1 \
        -DNDS_TITLE_CONFIG=$PWD/game.toml -DNDS_INTERPRETER=OFF \
        -DNDS_EMBED_DUMPS_DIR=$PWD/ndsrecomp/bios
    cmake --build build-runner -j6 --target nds_runner

The bank compile is large (about 1 GB of C); keep `-j` at 6 or below on a
7 GB host. Tier-3 coverage manifests can still be merged into
`coverage/entry-points.json` (`tools/promote_main_coverage.py`); the bank
build keeps those seeds on top of the static scan.

## Without the interpreter
Everything the game runs is compiled ahead of time, including the two things
that look like they could not be:

- **Overlay 29** is a self-decrypting protection module, encrypted in the ROM and
  decrypted again around every protected call. Its plaintext is a pure function
  of the ROM, so `tools/ov029_unprotect.py` recovers it during
  `dq9_prepare` and the bank is built from the bytes that actually execute.
- **The firmware's boot code** only exists in RAM at run time, but it is the
  output of a fixed decode. `ndsrecomp/tools/fw_extract.py` runs that decode
  on `DQ9_FIRMWARE` and `DQ9_BIOS7` (by default the dumps in `ndsrecomp/bios`)
  when the project is configured, so the dumps have to be there at that point.

`-DNDS_INTERPRETER=OFF` above leaves the interpreter out of the binary. Code with
no live bank then stops both CPUs with `[no-interpreter] no live bank for
pc=...`. Two things will do that:

- booting a firmware dump other than the one the banks were built from
  (reconfigure with the right `DQ9_FIRMWARE`);
- the firmware menu, which is not compiled: keep `startup_mode = "automatic"`.

Dropping the flag brings the interpreter back as a fallback, and a relink is all
that takes. That build also reports anything still interpreted in its coverage
manifests.

## Play
    ./build-runner/nds_runner

The first launch asks for the ROM, then remembers it. `game.toml` is built
into the executable, so it runs from any directory. Everything else lives in
one folder:

| Platform | Folder |
| --- | --- |
| Linux | `~/.local/share/DQ9Recomp` (`$XDG_DATA_HOME`) |
| Windows | `Documents\DQ9Recomp` |
| macOS | `~/Library/Application Support/DQ9Recomp` |
| Any | next to the executable, if a `portable.txt` file is there |

`settings.toml` holds the settings and is meant to be edited by hand; the Esc
menu writes its changes back to it, keeping your comments. The save is
`saves/DQ9Recomp.sav`, and savestates, performance logs and coverage manifests
sit alongside it. With `-DNDS_EMBED_DUMPS_DIR` the BIOS and firmware dumps are
built into the executable and the game only asks for the ROM. Without it, they
are found in `bios/` there, in `bios/` next to the executable, or in
`ndsrecomp/bios`; otherwise the game asks for the folder.

Any other command line (`nds_runner <bios dir> --rom ...`) behaves as before
and ignores `settings.toml`.

Recall can be measured offline with
`tools/static_scan_dq9.py --out <dir>` (seeds stripped) followed by
`tools/score_static_scan.py` on `<dir>/<image>/gen/scan_<image>_dispatch.c`.
