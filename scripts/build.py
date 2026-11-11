#!/usr/bin/env python3
"""Build DQ9Recomp end to end: venv, generated banks, then the runner.

Runs the two CMake projects from the README (the bank generator in build/ and
ndsrecomp's runner in build-runner/) with the per-platform flags each needs,
then copies the runner to the repo root as DQ9Recomp[.exe].
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys

ROM_SHA1 = "ff761d349709f329c8ac4bd0023fdce21861e8a1"
PROJECT_NAME = "DQ9Recomp"
BANK_BUILD_DIR = "build"
RUNNER_BUILD_DIR = "build-runner"
RECOMP_UI_DIR = "recomp-ui"
RECOMP_NET_DIR = "recomp-net"

# The runner always links the recompiled FreeBIOS banks, which are not part of
# DQ9_BANKS: ndsrecomp ships their configs and images, and nds_recompile (built
# with the banks above) turns them into ndsrecomp/generated/*.c.
FREEBIOS_BANKS = (
    ("freebios9", "drastic_bios_arm9.bin", "freebios_arm9"),
    ("freebios7", "drastic_bios_arm7.bin", "freebios_arm7"),
)

IS_WINDOWS = platform.system() == "Windows"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(args, check=True, **kwargs):
    print(f"+ {' '.join(str(a) for a in args)}", flush=True)
    result = subprocess.run(args, **kwargs)
    if result.returncode != 0 and check:
        sys.exit(result.returncode)
    return result


def cmake_path(path):
    # CMake wants forward slashes, and semicolons separate list items.
    return os.path.abspath(path).replace("\\", "/")


def check_deps():
    missing = [dep for dep in ("cmake", "ninja") if shutil.which(dep) is None]
    if missing:
        die(f"missing required tool(s): {', '.join(missing)}")


def find_clang():
    """Return (clang, clang++), or None to leave CMake's default compiler."""
    # Versioned binaries (clang-20, clang++-20, ...) only exist on Linux.
    if not IS_WINDOWS:
        for version in range(30, 17, -1):
            cc, cxx = f"clang-{version}", f"clang++-{version}"
            if shutil.which(cc) and shutil.which(cxx):
                return cc, cxx
    if shutil.which("clang") and shutil.which("clang++"):
        return "clang", "clang++"
    if IS_WINDOWS:
        # The banks and runner are only built with LLVM clang (MSVC target) here.
        die("no clang/clang++ found in PATH (install LLVM, e.g. `scoop install llvm`)")
    return None


def venv_python():
    if IS_WINDOWS:
        return os.path.join(".venv", "Scripts", "python.exe")
    return os.path.join(".venv", "bin", "python3")


def ensure_venv():
    # The bank CMakeLists picks .venv's Python up by itself when it exists;
    # tools/prepare_dq9.py needs ndspy from requirements.txt.
    py = venv_python()
    if not os.path.exists(py):
        run([sys.executable, "-m", "venv", ".venv"])
    if run([py, "-c", "import ndspy, PIL"], check=False,
           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        run([py, "-m", "pip", "install", "-r", "requirements.txt"])
    return py


def default_sdl3_dir():
    if os.environ.get("SDL3_DIR"):
        return os.environ["SDL3_DIR"]
    # On Windows there is no system SDL3; reuse EternalSonataReprise's static one.
    candidate = os.path.join("..", "EternalSonataReprise", "sdk", "cmake")
    if IS_WINDOWS and os.path.exists(os.path.join(candidate, "SDL3Config.cmake")):
        return candidate
    return None


def compiler_args(compilers):
    if compilers is None:
        return []
    cc, cxx = compilers
    return [f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}"]


def build_banks(args, compilers, python):
    configure = [
        "cmake", "-G", "Ninja", "-S", ".", "-B", BANK_BUILD_DIR,
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        f"-DDQ9_PYTHON={cmake_path(python)}",
        f"-DNDSRECOMP_COMPILE_JOBS={args.jobs}",
        f"-DDQ9_HOOKS={'OFF' if args.no_hooks else 'ON'}",
        f"-DDQ9_FIRMWARE_BANKS={'ON' if args.firmware_banks else 'OFF'}",
    ] + compiler_args(compilers)
    run(configure)
    run(["cmake", "--build", BANK_BUILD_DIR, "--parallel", str(args.jobs),
         "--target", "dq9_romcheck", "dq9_generate_banks"])


def ensure_freebios_banks():
    """Recompile the FreeBIOS images the runner links, if they are missing."""
    recompiler = os.path.join(BANK_BUILD_DIR, "ndsrecomp-recompiler",
                              f"nds_recompile{EXE_SUFFIX}")
    out = os.path.join("ndsrecomp", "generated")
    for config, image, bank in FREEBIOS_BANKS:
        if all(os.path.exists(os.path.join(out, f"{bank}{suffix}"))
               for suffix in (".c", "_dispatch.c", ".h")):
            continue
        run([
            recompiler,
            "--config", os.path.join("ndsrecomp", "bios", f"{config}.toml"),
            "--bin", os.path.join("ndsrecomp", "third_party", "freebios", image),
            "--out", out, "--bank", bank,
        ])


def build_runner(args, compilers):
    configure = [
        "cmake", "-G", "Ninja", "-S", os.path.join("ndsrecomp", "runner"),
        "-B", RUNNER_BUILD_DIR,
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        "-DNDS_BOOTSTRAP_FIRMWARE=ON",
        f"-DNDS_TITLE_BANK_DIR={cmake_path(os.path.join('generated', 'recomp'))}",
        f"-DNDS_TITLE_ROM_SHA1={ROM_SHA1}",
        f"-DNDS_TITLE_CONFIG={cmake_path('game.toml')}",
        f"-DNDS_INTERPRETER={'ON' if args.interpreter else 'OFF'}",
        f"-DNDSRECOMP_COMPILE_JOBS={args.jobs}",
        # game.toml boots on FreeBIOS with a generated firmware, so the retail
        # BIOS banks (recompiled from the user's dumps) are never used, and
        # without them neither the build nor the game needs any dump.
        "-DNDS_RETAIL_BIOS_BANKS=OFF",
        "-DNDS_EMBED_DUMPS_DIR=",
    ] + compiler_args(compilers)

    # Hooks are baked into the banks unless --no-hooks, and then the runner
    # must link their handlers (a missing one fails the link).
    if not args.no_hooks:
        # The hooks' own text (res/lang/*.toml -> src/hooks/lang.generated.h).
        run([sys.executable, os.path.join("scripts", "gen-lang.py")])
        hooks_dir = os.path.join("src", "hooks")
        hooks = sorted(cmake_path(os.path.join(hooks_dir, f))
                       for f in os.listdir(hooks_dir) if f.endswith(".cpp"))
        configure.append(f"-DNDS_TITLE_HOOK_SOURCES={';'.join(hooks)}")
    else:
        configure.append("-DNDS_TITLE_HOOK_SOURCES=")

    sdl3_dir = args.sdl3_dir or default_sdl3_dir()
    if sdl3_dir:
        configure.append(f"-DSDL3_DIR={cmake_path(sdl3_dir)}")
    elif IS_WINDOWS:
        die("no SDL3 found: pass --sdl3-dir <dir with SDL3Config.cmake> or set SDL3_DIR")

    # Point CMake at the submodule explicitly: its own fallback is a hardcoded
    # path from the author's machine, and a cached value could also win.
    recomp_ui = args.recomp_ui or RECOMP_UI_DIR
    configure.append(f"-DNDS_RECOMP_UI_ROOT={cmake_path(recomp_ui)}")

    # The cartridge banner icon becomes the Windows .exe icon.
    rom = os.path.abspath("dq9.nds")
    if os.path.exists(rom):
        configure.append(f"-DNDS_EXE_ICON_ROM={cmake_path(rom)}")

    # Netplay is on unless --no-online; like recomp-ui, recomp-net comes from
    # its submodule unless --recomp-net points elsewhere.
    configure.append(f"-DNDS_ONLINE={'ON' if args.online else 'OFF'}")
    if args.online:
        recomp_net = args.recomp_net or RECOMP_NET_DIR
        configure.append(f"-DNDS_RECOMP_NET_ROOT={cmake_path(recomp_net)}")

    if IS_WINDOWS:
        configure += [
            # C++ only, so the generated C banks are unaffected.
            "-DCMAKE_CXX_FLAGS=-DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -D__WIN32__",
            "-DCMAKE_EXE_LINKER_FLAGS=-ld3d12 -ldxgi",
        ]

    run(configure)
    run(["cmake", "--build", RUNNER_BUILD_DIR, "--parallel", str(args.jobs),
         "--target", "nds_runner"])

    src = os.path.join(RUNNER_BUILD_DIR, f"nds_runner{EXE_SUFFIX}")
    dst = f"{PROJECT_NAME}{EXE_SUFFIX}"
    print(f"+ cp {src} {dst}")
    try:
        shutil.copy2(src, dst)
    except PermissionError:
        die(f"cannot overwrite {dst}: close any running copy and run again")


def do_package(name):
    import tarfile
    import zipfile

    # game.toml is embedded in the executable and it needs no dumps, so it
    # ships on its own with the licenses.
    exe = f"{PROJECT_NAME}{EXE_SUFFIX}"
    if not os.path.isfile(exe):
        die(f"{exe} not found: build first")
    files = [exe, "LICENSE", "LICENSE-GPL", "README.md"]

    if IS_WINDOWS:
        archive_path = f"{name}.zip"
        print(f"+ zip {archive_path}")
        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in files:
                zf.write(f, f)
    else:
        archive_path = f"{name}.tar.gz"
        print(f"+ tar {archive_path}")
        with tarfile.open(archive_path, "w:gz") as tf:
            for f in files:
                tf.add(f, arcname=f)

    github_env = os.environ.get("GITHUB_ENV")
    if github_env:
        with open(github_env, "a") as fh:
            fh.write(f"ARTIFACT_PATH={archive_path}\n")
            fh.write(f"ARTIFACT_NAME={name}\n")


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--package", metavar="NAME",
                   help="Package the built executable into NAME.zip (Windows) or NAME.tar.gz; skips the build")
    p.add_argument("--release", dest="build_type", action="store_const",
                   const="Release", default="RelWithDebInfo",
                   help="Build an optimized release without debug symbols; default is RelWithDebInfo")
    p.add_argument("--jobs", "-j", type=int, default=min(os.cpu_count() or 1, 6),
                   help="Parallel compile jobs (default: min(CPUs, 6); the bank C is ~1 GB, "
                        "so keep it low on hosts with little RAM)")
    p.add_argument("--runner-only", action="store_true",
                   help="Skip the bank build and only rebuild the runner")
    p.add_argument("--no-hooks", action="store_true",
                   help="Generate banks without config/hooks and link no hook handlers")
    p.add_argument("--firmware-banks", action="store_true",
                   help="Also build the firmware boot banks (DQ9_FIRMWARE_BANKS=ON)")
    p.add_argument("--interpreter", action="store_true",
                   help="Keep the interpreter in the runner as a fallback")
    p.add_argument("--sdl3-dir", help="Directory with SDL3Config.cmake (default: $SDL3_DIR, "
                                      "or ../EternalSonataReprise/sdk/cmake on Windows)")
    p.add_argument("--recomp-ui", help="Path to a recomp-ui checkout to use instead "
                                       f"of the {RECOMP_UI_DIR}/ submodule")
    p.add_argument("--online", action=argparse.BooleanOptionalAction, default=True,
                   help="Build Netplay (online lobbies, NDS_ONLINE=ON; default); "
                        "--no-online builds the local-wireless-only runner")
    p.add_argument("--recomp-net", help="Path to a recomp-net checkout to use instead "
                                        f"of the {RECOMP_NET_DIR}/ submodule")
    return p.parse_args()


def main():
    args = parse_args()

    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    os.chdir(root)

    if args.package:
        do_package(args.package)
        return

    submodules = [
        os.path.join("ndsrecomp", "runner", "CMakeLists.txt"),
        os.path.join(RECOMP_UI_DIR, "src", "recomp_runtime_ui.h"),
    ]
    if args.online and not args.recomp_net:
        submodules.append(os.path.join(RECOMP_NET_DIR, "include", "recomp_net", "auth.h"))
    if not all(os.path.exists(path) for path in submodules):
        die("submodules missing: run `git submodule update --init --recursive`")

    check_deps()
    compilers = find_clang()

    if not args.runner_only:
        if not os.path.exists("dq9.nds"):
            die("dq9.nds not found in the repo root: place your Europe (YDQP) ROM there")
        python = ensure_venv()
        build_banks(args, compilers, python)
        ensure_freebios_banks()
    elif not os.path.isdir(os.path.join("generated", "recomp")):
        die("generated/recomp missing: run without --runner-only first")

    build_runner(args, compilers)


if __name__ == "__main__":
    main()
