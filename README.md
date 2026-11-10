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
- Banks: ARM9 main, ARM7 and 31 ARM9 overlays (`DQ9_BANKS` in
  `CMakeLists.txt`), plus the firmware's boot code with `-DDQ9_FIRMWARE_BANKS=ON`. Every row is gated by the ROM SHA-1 and by
  live-byte validation.
- No interpreter: the game runs as compiled code from its first
  instruction, and the runner below does not contain one. See
  [Without the interpreter](#without-the-interpreter).
- Save: 64 KiB EEPROM (`[cartridge]` in `game.toml`), raw 65,536-byte
  `.sav` images, interchangeable with emulator saves that have no footer.
  No dumps are needed, to build or to play: `game.toml` boots on the
  recompiled FreeBIOS (`[system] freebios`) with a generated firmware image
  (`generated_firmware`), and the runner is built without the retail BIOS
  banks (`-DNDS_RETAIL_BIOS_BANKS=OFF`).

## Build
    python3 scripts/build.py

does everything below on Linux, macOS and Windows: it creates `.venv`,
generates the banks, builds the runner (with clang and, on Windows, the
extra flags it needs) and copies it to `DQ9Recomp[.exe]` in the repo root.
`--help` lists the options (`--release`, `--jobs`, `--runner-only`, `--no-hooks`,
`--interpreter`, `--sdl3-dir`, `--package NAME`...). The manual steps:

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
        -DNDS_RETAIL_BIOS_BANKS=OFF \
        "-DNDS_TITLE_HOOK_SOURCES=$PWD/src/hooks/skip_intros.cpp;$PWD/src/hooks/local_wireless_diag.cpp;$PWD/src/hooks/netplay_host.cpp;$PWD/src/hooks/camera_zoom.cpp"
    cmake --build build-runner -j6 --target nds_runner

The bank compile is large (about 1 GB of C); keep `-j` at 6 or below on a
7 GB host. The banks come from the static scan alone: there is no list of
entry points recorded from gameplay to maintain.

### Hooks
A bank with a manifest at `config/hooks/<bank>.toml` is generated with those
mid-instruction hooks baked in (schema and semantics:
`ndsrecomp/docs/midasm-hooks.md`). Their handlers live in `src/hooks/` and go
to the runner configure as an absolute, semicolon-separated
`NDS_TITLE_HOOK_SOURCES` list. A manifest whose name is not a bank fails the
configure, and a missing handler fails the runner link.

Shipped hooks:

- **Skip intros** (`skip_intros.cpp`; `dq9_arm9_ov020.toml`, `dq9_arm9.toml`):
  boots past the Nintendo / Mobiclip / Square Enix / Level-5 logos and the
  opening movie by steering two of the game's own branches onto the paths it
  takes after a soft reset. The game proper starts about 0.9e9 guest cycles
  sooner, plus the whole movie.
- **Camera distance** (`camera_zoom.cpp`; `dq9_arm9.toml`): the runtime
  menu's Game > Camera distance, 50% to 400% (`[title] camera_distance` in
  `settings.toml`). Moves the scene camera along its line of sight before
  the game builds the view matrix, so it applies to the field, battles and
  cutscenes alike. 100% (the default) leaves the game untouched; the far
  plane does not move, so distant scenery can clip sooner when zoomed out.
  More hooks reshape the map's hardware fog for the moved camera (the
  field's time-of-day fog and the dungeons' fixed fog, which is then
  re-sent every frame), so the fog sits at the same distances as at 100%.
- **Unlock camera rotation** (`camera_rotation.cpp`; `dq9_arm9.toml`):
  the runtime menu's Game > Unlock camera rotation (`[title]
  unlock_camera_rotation`). L and R turn the field camera all the way round
  instead of stopping 45 degrees either side of the default; L+R still
  resets it. Off (the default) leaves the game untouched.
- **Local wireless diagnostics** (`local_wireless_diag.cpp`;
  `dq9_arm7.toml`): log only. Prints a `[dq9-wm]` line to stderr when DQ9's
  ARM7 Wi-Fi code drops an MP link on its own (with the WM reason code) and
  whenever its WL layer raises a status flag (with the code path). These are
  rare events; they explained the online lobby join failures
  (`HANDOFF_ONLINE_LOBBY.md`).

`-DDQ9_HOOKS=OFF` regenerates every bank without hooks (then drop
`NDS_TITLE_HOOK_SOURCES` from the runner configure); that is the reference
build to compare a hook against.

## Without the interpreter
Everything the game runs is compiled ahead of time, including the two things
that look like they could not be:

- **Overlay 29** is a self-decrypting protection module, encrypted in the ROM and
  decrypted again around every protected call. Its plaintext is a pure function
  of the ROM, so `tools/ov029_unprotect.py` recovers it during
  `dq9_prepare` and the bank is built from the bytes that actually execute.
- **The firmware's boot code** (only with `-DDQ9_FIRMWARE_BANKS=ON`, for
  `boot = "lle"` with `generated_firmware = false` in `game.toml`) only exists
  in RAM at run time, but it is the output of a fixed decode.
  `ndsrecomp/tools/fw_extract.py` runs that decode on `DQ9_FIRMWARE` and
  `DQ9_BIOS7` (by default the dumps in `ndsrecomp/bios`) when the project is
  configured, so the dumps have to be there at that point. The default
  generated firmware boots the cartridge directly and needs neither.

`-DNDS_INTERPRETER=OFF` above leaves the interpreter out of the binary. Code with
no live bank then stops both CPUs with `[no-interpreter] no live bank for
pc=...`. Two things will do that:

- booting a firmware dump without its banks, or other than the one they were
  built from (reconfigure with `DQ9_FIRMWARE_BANKS` and the right
  `DQ9_FIRMWARE`);
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
`saves/DQ9Recomp.sav`, with savestates alongside it. Logs (the console
output, performance logs and coverage manifests) always go to `logs/` next to
the executable. The game only asks for the ROM: it runs on FreeBIOS with a
generated firmware, so there are no BIOS or firmware dumps to find.

Any other command line (`nds_runner <bios dir> --rom ...`) behaves as before
and ignores `settings.toml`.

### Multiplayer
Local wireless is on (`[local_wireless]` in `game.toml`): runners on the same
local network are in radio range of each other, so one player opens the
Rapportal and the others join from the inn in Stornway, as on real DS units.
Discovery is a UDP broadcast on ports 26710-26725, sent on every Ethernet
and Wi-Fi network the PC is on. On first launch Windows asks whether to let
the runner through its firewall; allow it on private networks. On Linux,
open the ports yourself if a firewall is on (`sudo ufw allow
26710:26725/udp`, or `sudo firewall-cmd --add-port=26710-26725/udp`). If the
others still don't show up (networks that block broadcast, or players
reached through a VPN such as Tailscale), add `--local-wireless-peer <IP>`
for each of the other PCs. Two copies on one PC: `--instance 0` and
`--instance 1` (separate saves).

#### Netplay (in progress)
Built with `python3 scripts/build.py --online` (needs a
[recomp-net](https://github.com/RetroPortingToolKit/recomp-net) checkout
next to this repo), the runner can put players on different networks in
radio range of each other through an online lobby. The in-game menu
(Esc) has a Netplay section. With Netplay on, only the players of your
lobby are in range, not the LAN. First choose "Sign in with Discord" and
authorize in the browser that opens (the public lobby server needs it,
once). The key it leaves in the data folder (`netplay_secret`, a password:
don't share it) signs you in from then on. To host, open the Rapportal:
a lobby opens on the server with a seat for every free party slot, named
after the "Lobby name" setting ("<your name>'s lobby" when empty). To
join, pick a lobby under Netplay > Lobbies, then walk to the inn in
Stornway and join as usual. The menu saves Netplay and the lobby name in
`settings.toml` (`[online]`). From the command line: `--netplay on`,
`--netplay-sign-in on`, `--netplay-join first`. The game's traffic goes
only through the server's relay, never straight to the other players.
The lobby server itself still sees your address and, in its current
version, lists the host's address to everyone browsing for lobbies.

Recall can be measured offline with
`tools/static_scan_dq9.py --out <dir>` followed by
`tools/score_static_scan.py` on `<dir>/<image>/gen/scan_<image>_dispatch.c`.
