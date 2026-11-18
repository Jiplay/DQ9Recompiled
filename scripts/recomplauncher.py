"""Stage recomplauncher.json with this platform's data directory.

The runner keeps settings, saves and savestates in a per-user folder
(ndsrecomp/runner/src/player_launch.h), or next to the executable with a
portable.txt. The manifest names that folder {data}; this fills it in.
"""
import json
from pathlib import Path

APP = "DQ9Recompiled"


def stage_manifest(destination, is_windows, is_macos=False):
    source = Path(__file__).resolve().parent.parent / "recomplauncher.json"
    if is_windows:
        data = "{documents}/" + APP
    elif is_macos:
        data = "{home}/Library/Application Support/" + APP
    else:
        # $XDG_DATA_HOME, default ~/.local/share.
        data = "{local}/" + APP
    manifest = json.loads(source.read_text(encoding="utf-8").replace("{data}", data))
    if is_macos:
        # The manifest sits in the signed .app's Contents/MacOS, which must
        # stay unchanged: the prepared copy and the logs go to the data
        # directory instead.
        content = "{install}/assets/recomplauncher"
        text = json.dumps(manifest).replace(content, data + "/assets/recomplauncher")
        manifest = json.loads(text)
        manifest["folders"]["logs"] = [{"path": data + "/logs"}]
    else:
        # Portable installs keep everything next to the executable.
        manifest["saves"] += [p.replace(data, "{install}") for p in manifest["saves"]]
    Path(destination, "recomplauncher.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
