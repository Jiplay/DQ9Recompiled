# DQ9Recomp

Static recompilation of **Dragon Quest IX: Sentinels of the Starry Skies**
(Europe, `YDQP`, SHA-1 `ff761d349709f329c8ac4bd0023fdce21861e8a1`), built on the
[ndsrecomp](https://github.com/birabittoh/ndsrecomp) framework.

**You must own the game.** This project does **not** ship any copyrighted code,
data, or assets: no ROM, BIOS, firmware or generated code. You provide your own
dump as `dq9.nds`.

The game runs as compiled code from its first instruction, with no interpreter,
on a recompiled FreeBIOS and a generated firmware.

## Features

Most of these have a switch under Game in the in-game menu (Esc); with the
switch off, the game is untouched.

* **Skip intros** (off by default): boots past the logos and the opening movie.
* **Camera zoom**: Auto (the default, follows the window size) or Manual (mouse
  wheel or right stick), plus **unlock camera rotation**.
* **UI size**: keeps text boxes and menus at a fixed size in big windows.
* **Language**: switch between English, French, German, Italian and Spanish
  without a restart.
* **Mouse-friendly menus**: hover to highlight, one click to choose.
* **60 FPS mode**: Off, Fast or Accurate.
* **Analog movement**: the left stick walks at any angle.
* **Instant text**: hold B or the right mouse button to print the rest of a box.
* **Options**: the Nintendo Wi-Fi row of the main menu and the camp menu's Misc.
  list get an Options page, and the main menu gets Import and Export save file
  rows.
* **Multiplayer**: local wireless over the LAN, and online lobbies with Netplay.

## Play

```bash
./DQ9Recomp
```

Settings and saves live in a per-user folder, or
next to the executable if a `portable.txt` file is there:

| Platform | Folder |
| --- | --- |
| Linux | `~/.local/share/DQ9Recomp` |
| Windows | `Documents\DQ9Recomp` |
| macOS | `~/Library/Application Support/DQ9Recomp` |

Logs always go to `logs/` next to the executable.

### Multiplayer

On first launch Windows asks whether to let the game through its firewall;
allow it on private networks. On Linux, open the ports yourself if a
firewall is on (`sudo ufw allow 26710:26725/udp`, or
`sudo firewall-cmd --add-port=26710-26725/udp`). If the
others still don't show up (networks that block broadcast, or players
reached through a VPN such as Tailscale), add `--local-wireless-peer <IP>`
for each of the other PCs.

**Local wireless**: games running on the same network find each other. One player
opens the Rapportal and the others join from the inn in Stornway. Allow the
runner through your firewall (UDP ports 26710-26725). If peers do not show up,
pass `--local-wireless-peer <IP>`.

**Netplay** (in progress, off by default): online lobbies through
[recomp-net](https://github.com/RetroPortingToolKit/recomp-net). Turn it on from
the Netplay section of the menu. Players see only the lobby server's relay
address.

Two copies can play on one PC: `--instance 0` and `--instance 1` (separate saves).

## Building from scratch

You need Python 3, CMake, Ninja, clang and SDL3.

```bash
git clone --recursive https://github.com/birabittoh/DQ9Recompiled
cd DQ9Recompiled
```

Put your ROM in the repository root as `dq9.nds`, then:

```bash
python3 scripts/build.py
```

If you already cloned without `--recursive`, run
`git submodule update --init --recursive` first.

This sets up a virtual environment, generates the banks from the ROM, builds the
runner and copies it to `DQ9Recomp[.exe]`. The bank compile is large (about 1 GB
of C), so keep `--jobs` at 6 or below on a small machine. `--help` lists the
other options (`--release`, `--no-online`, `--no-hooks`, ...).

## Credits

* [ndsrecomp](https://github.com/mstan/ndsrecomp)
* [recomp-net](https://github.com/RetroPortingToolKit/recomp-net)
* [melonDS](https://melonds.kuribo64.net/)

## License

The source in this repository is available under the MIT License; see
[`LICENSE`](LICENSE).

A built `nds_runner` links GPL-3.0-or-later code from melonDS, so it can only be
distributed under **GPL-3.0-or-later**; see [`LICENSE-GPL`](LICENSE-GPL). If you
distribute a build, ship `LICENSE-GPL` with it and make the corresponding source
available. The submodules carry their own licenses.
