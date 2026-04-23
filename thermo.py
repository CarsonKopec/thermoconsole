#!/usr/bin/env python3
"""
thermo.py — one script to rule the ThermoConsole project.

Cross-platform orchestrator for the whole repo: install dependencies, clone
GitHub deps (ImGui docking branch), build the runtime and editor, clean,
run the editor or a game, and wrap the SDK tools. Python stdlib only, so
it works anywhere Python 3.8+ runs.

Examples
--------
    python thermo.py setup                   # first-time setup on this machine
    python thermo.py build                   # build everything
    python thermo.py build editor --debug    # debug build of just the editor
    python thermo.py run editor games/space_dodge
    python thermo.py run game space_dodge
    python thermo.py pack games/space_dodge
    python thermo.py new my_game
    python thermo.py status                  # what's built, what's missing
    python thermo.py doctor                  # tell me why nothing works
    python thermo.py clean all
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional, Sequence

# Windows consoles (cmd.exe, PowerShell 5.1) default to cp1252 and choke on
# the Unicode glyphs we print. Force UTF-8 on stdout/stderr so the script
# runs the same everywhere. Safe no-op on Python < 3.7 / non-reconfigurable streams.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
    except Exception:
        pass

# ───────────────────────────────────────────────────────────────────────────
# Pretty printing (degrades cleanly when stdout isn't a TTY)
# ───────────────────────────────────────────────────────────────────────────

_USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def _c(code: str, s: str) -> str:
    return f"\033[{code}m{s}\033[0m" if _USE_COLOR else s


def info(msg: str) -> None:    print(_c("96", "• ") + msg)
def ok(msg: str) -> None:      print(_c("92", "✓ ") + msg)
def warn(msg: str) -> None:    print(_c("93", "! ") + msg, file=sys.stderr)
def err(msg: str) -> None:     print(_c("91", "✗ ") + msg, file=sys.stderr)
def step(msg: str) -> None:    print(_c("1;94", f"\n▌ {msg}"))


# ───────────────────────────────────────────────────────────────────────────
# Paths & constants
# ───────────────────────────────────────────────────────────────────────────

ROOT = Path(__file__).resolve().parent
EDITOR_DIR  = ROOT / "editor"
RUNTIME_DIR = ROOT / "runtime"
SDK_DIR     = ROOT / "sdk"
GAMES_DIR   = ROOT / "games"
PICO_DIR    = ROOT / "pico-controller"

IMGUI_URL    = "https://github.com/ocornut/imgui"
IMGUI_BRANCH = "docking"

IS_WINDOWS = sys.platform == "win32"
IS_MACOS   = sys.platform == "darwin"
IS_LINUX   = sys.platform.startswith("linux")

EXE_SUFFIX = ".exe" if IS_WINDOWS else ""


# ───────────────────────────────────────────────────────────────────────────
# Shell helper — one place to run a subprocess with consistent logging
# ───────────────────────────────────────────────────────────────────────────

def run(
    cmd: Sequence[str],
    *,
    cwd: Optional[Path] = None,
    check: bool = True,
    env: Optional[dict] = None,
    capture: bool = False,
) -> subprocess.CompletedProcess:
    printable = " ".join(str(c) for c in cmd)
    info(_c("90", f"$ {printable}" + (f"  (in {cwd})" if cwd else "")))
    try:
        return subprocess.run(
            [str(c) for c in cmd],
            cwd=str(cwd) if cwd else None,
            check=check,
            env=env,
            text=True,
            capture_output=capture,
        )
    except FileNotFoundError:
        err(f"Command not found: {cmd[0]}")
        raise
    except subprocess.CalledProcessError as e:
        err(f"Command failed (exit {e.returncode}): {printable}")
        if capture:
            if e.stdout: print(e.stdout)
            if e.stderr: print(e.stderr, file=sys.stderr)
        raise


def have(binary: str) -> bool:
    return shutil.which(binary) is not None


# ───────────────────────────────────────────────────────────────────────────
# Dependency detection
# ───────────────────────────────────────────────────────────────────────────

@dataclass
class DepStatus:
    name: str
    found: bool
    detail: str = ""
    hint: str = ""


def detect_linux_pkg_mgr() -> Optional[str]:
    for mgr in ("apt-get", "dnf", "pacman", "zypper"):
        if have(mgr):
            return mgr
    return None


def check_deps() -> list[DepStatus]:
    results: list[DepStatus] = []

    # Build tools
    results.append(DepStatus(
        "cmake", have("cmake"),
        detail=_capture_version(["cmake", "--version"]),
        hint="Install CMake ≥ 3.16 from https://cmake.org or your package manager",
    ))

    # C/C++ compiler — not strictly required here (CMake finds it), but
    # users appreciate the signal.
    cc_found = any(have(c) for c in ("cl", "cc", "gcc", "clang"))
    results.append(DepStatus(
        "C/C++ compiler", cc_found,
        hint=("Install Visual Studio Build Tools on Windows, Xcode CLT on macOS, "
              "or build-essential on Linux"),
    ))

    # Git — needed to clone ImGui
    results.append(DepStatus(
        "git", have("git"),
        detail=_capture_version(["git", "--version"]),
        hint="Install git from https://git-scm.com",
    ))

    # SDL2 is platform-specific to verify. We rely on CMake's find_package
    # at build time; here we just surface the best-effort signal.
    sdl_hint = ""
    if IS_LINUX:
        mgr = detect_linux_pkg_mgr()
        if mgr == "apt-get":
            sdl_hint = "sudo apt-get install -y libsdl2-dev libsdl2-image-dev"
        elif mgr == "dnf":
            sdl_hint = "sudo dnf install -y SDL2-devel SDL2_image-devel"
        elif mgr == "pacman":
            sdl_hint = "sudo pacman -S sdl2 sdl2_image"
    elif IS_MACOS:
        sdl_hint = "brew install sdl2 sdl2_image"
    else:  # Windows
        sdl_hint = "vcpkg install sdl2 sdl2-image  (and set CMAKE_TOOLCHAIN_FILE)"

    results.append(DepStatus(
        "SDL2 (dev)", True,   # can't cheaply verify — defer to CMake
        detail="verified at build time by CMake find_package",
        hint=sdl_hint,
    ))

    # ImGui docking branch (vendored into editor/vendor/imgui)
    imgui_header = EDITOR_DIR / "vendor" / "imgui" / "imgui.h"
    branch = _git_current_branch(EDITOR_DIR / "vendor" / "imgui")
    detail = f"branch={branch}" if branch else "not cloned"
    on_right_branch = branch == IMGUI_BRANCH
    results.append(DepStatus(
        "Dear ImGui (editor/vendor/imgui)",
        imgui_header.exists() and on_right_branch,
        detail=detail,
        hint=f"python thermo.py setup  (clones the '{IMGUI_BRANCH}' branch)",
    ))

    return results


def _capture_version(cmd: Sequence[str]) -> str:
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=False)
        first = (out.stdout or out.stderr or "").splitlines()
        return first[0].strip() if first else ""
    except FileNotFoundError:
        return ""


def _git_current_branch(repo: Path) -> Optional[str]:
    if not (repo / ".git").exists():
        return None
    r = subprocess.run(
        ["git", "-C", str(repo), "rev-parse", "--abbrev-ref", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    return r.stdout.strip() if r.returncode == 0 else None


# ───────────────────────────────────────────────────────────────────────────
# Dependency installation
# ───────────────────────────────────────────────────────────────────────────

def install_system_deps(assume_yes: bool) -> None:
    """Best-effort system dep install. Platform-aware; no-op where we don't
    know the right recipe — we just print guidance in that case."""
    step("Installing system dependencies")

    if IS_LINUX:
        mgr = detect_linux_pkg_mgr()
        if mgr is None:
            warn("Unknown Linux package manager; install SDL2/CMake/git manually.")
            return
        confirm = ["-y"] if assume_yes or mgr in ("apt-get", "dnf") else []
        sudo = ["sudo"] if os.geteuid() != 0 else []  # type: ignore[attr-defined]
        if mgr == "apt-get":
            run([*sudo, "apt-get", "update", *confirm])
            run([*sudo, "apt-get", "install", *confirm,
                 "build-essential", "cmake", "git", "pkg-config",
                 "libsdl2-dev", "libsdl2-image-dev"])
        elif mgr == "dnf":
            run([*sudo, "dnf", "install", *confirm,
                 "gcc", "gcc-c++", "cmake", "git", "pkgconf-pkg-config",
                 "SDL2-devel", "SDL2_image-devel"])
        elif mgr == "pacman":
            run([*sudo, "pacman", "-S", "--needed", "--noconfirm",
                 "base-devel", "cmake", "git", "sdl2", "sdl2_image"])
        elif mgr == "zypper":
            run([*sudo, "zypper", "install", "-y",
                 "gcc", "gcc-c++", "cmake", "git", "pkg-config",
                 "SDL2-devel", "SDL2_image-devel"])
        ok(f"System deps installed via {mgr}.")
        return

    if IS_MACOS:
        if not have("brew"):
            warn("Homebrew not found. Install from https://brew.sh then re-run.")
            return
        run(["brew", "install", "cmake", "git", "sdl2", "sdl2_image"])
        ok("System deps installed via Homebrew.")
        return

    # Windows
    if IS_WINDOWS:
        warn("Automatic install on Windows requires vcpkg.")
        print(
            "    Recommended flow:\n"
            "      1. git clone https://github.com/microsoft/vcpkg C:/vcpkg\n"
            "      2. C:/vcpkg/bootstrap-vcpkg.bat\n"
            "      3. C:/vcpkg/vcpkg install sdl2 sdl2-image\n"
            "      4. set VCPKG_ROOT=C:/vcpkg  (or pass "
            "-DCMAKE_TOOLCHAIN_FILE=... to CMake)\n"
            "    CMake and Git are expected from https://cmake.org + "
            "https://git-scm.com"
        )
        return

    warn(f"Unknown platform {sys.platform}; skipping system deps.")


def clone_or_update_imgui() -> None:
    """Ensure editor/vendor/imgui is a clone of the docking branch."""
    step(f"Fetching Dear ImGui ({IMGUI_BRANCH} branch)")

    if not have("git"):
        err("git is required. Install it and re-run.")
        raise SystemExit(1)

    vendor = EDITOR_DIR / "vendor"
    target = vendor / "imgui"
    vendor.mkdir(parents=True, exist_ok=True)

    if not (target / ".git").exists():
        info(f"Cloning {IMGUI_URL} @ {IMGUI_BRANCH} into {target.relative_to(ROOT)}")
        run(["git", "clone", "--depth", "1",
             "-b", IMGUI_BRANCH, IMGUI_URL, str(target)])
        ok("ImGui clone complete.")
        return

    current = _git_current_branch(target)
    if current == IMGUI_BRANCH:
        info(f"ImGui already on '{IMGUI_BRANCH}'. Fetching updates…")
        run(["git", "-C", str(target), "fetch", "--depth", "1",
             "origin", IMGUI_BRANCH], check=False)
        run(["git", "-C", str(target), "reset", "--hard",
             f"origin/{IMGUI_BRANCH}"], check=False)
        ok("ImGui up to date.")
        return

    # Wrong branch — rewire. --depth clones don't carry other branches, so
    # fetch the right one explicitly or re-clone as a fallback.
    warn(f"ImGui clone is on '{current}', switching to '{IMGUI_BRANCH}'.")
    fetched = subprocess.run(
        ["git", "-C", str(target), "fetch", "--depth", "1", "origin",
         f"{IMGUI_BRANCH}:{IMGUI_BRANCH}"],
        check=False,
    )
    if fetched.returncode != 0:
        warn("Fetch failed — removing and re-cloning.")
        shutil.rmtree(target)
        run(["git", "clone", "--depth", "1",
             "-b", IMGUI_BRANCH, IMGUI_URL, str(target)])
    else:
        run(["git", "-C", str(target), "checkout", IMGUI_BRANCH])
    ok(f"ImGui switched to '{IMGUI_BRANCH}'.")


# ───────────────────────────────────────────────────────────────────────────
# Build
# ───────────────────────────────────────────────────────────────────────────

@dataclass
class BuildTarget:
    name: str
    source_dir: Path
    build_dir: Path
    requires_imgui: bool = False
    extra_cmake_args: list[str] = field(default_factory=list)


TARGETS: dict[str, BuildTarget] = {
    "runtime": BuildTarget(
        name="runtime",
        source_dir=RUNTIME_DIR,
        build_dir=RUNTIME_DIR / "build",
    ),
    "editor": BuildTarget(
        name="editor",
        source_dir=EDITOR_DIR,
        build_dir=EDITOR_DIR / "build",
        requires_imgui=True,
    ),
}


def _cmake_toolchain_args() -> list[str]:
    """Pass vcpkg toolchain automatically on Windows if VCPKG_ROOT is set
    and the user hasn't already specified a toolchain file."""
    if not IS_WINDOWS:
        return []
    if os.environ.get("CMAKE_TOOLCHAIN_FILE"):
        return []
    vcpkg_root = os.environ.get("VCPKG_ROOT")
    if not vcpkg_root:
        return []
    toolchain = Path(vcpkg_root) / "scripts" / "buildsystems" / "vcpkg.cmake"
    if toolchain.exists():
        return [f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"]
    return []


def build_target(name: str, *, debug: bool, clean: bool, jobs: Optional[int]) -> None:
    if name not in TARGETS:
        err(f"Unknown build target '{name}'. Known: {', '.join(TARGETS)}")
        raise SystemExit(2)
    t = TARGETS[name]

    if not t.source_dir.exists():
        err(f"Missing source directory: {t.source_dir}")
        raise SystemExit(1)

    if not have("cmake"):
        err("CMake not found. Run: python thermo.py setup")
        raise SystemExit(1)

    if t.requires_imgui and not (EDITOR_DIR / "vendor" / "imgui" / "imgui.h").exists():
        warn("ImGui missing; fetching it now.")
        clone_or_update_imgui()

    step(f"Building {t.name} ({'Debug' if debug else 'Release'})")

    if clean and t.build_dir.exists():
        info(f"Removing {t.build_dir.relative_to(ROOT)}")
        shutil.rmtree(t.build_dir)

    t.build_dir.mkdir(parents=True, exist_ok=True)

    build_type = "Debug" if debug else "Release"
    configure = [
        "cmake",
        "-S", str(t.source_dir),
        "-B", str(t.build_dir),
        f"-DCMAKE_BUILD_TYPE={build_type}",
        *t.extra_cmake_args,
        *_cmake_toolchain_args(),
    ]
    run(configure)

    build = ["cmake", "--build", str(t.build_dir), "--config", build_type]
    if jobs:
        build += ["--parallel", str(jobs)]
    else:
        build += ["--parallel"]
    run(build)
    ok(f"Built {t.name}.")


def clean_target(name: str) -> None:
    if name == "all":
        for t in TARGETS.values():
            _rm(t.build_dir)
        return
    if name not in TARGETS:
        err(f"Unknown clean target '{name}'. Known: {', '.join(TARGETS)}, all")
        raise SystemExit(2)
    _rm(TARGETS[name].build_dir)


def _rm(path: Path) -> None:
    if path.exists():
        info(f"Removing {path.relative_to(ROOT)}")
        shutil.rmtree(path)
        ok(f"Removed {path.name}/")
    else:
        info(f"Nothing to clean at {path.relative_to(ROOT)}")


# ───────────────────────────────────────────────────────────────────────────
# Locate built binaries (mirrors launch.py's search order)
# ───────────────────────────────────────────────────────────────────────────

def _first_existing(paths: Iterable[Path]) -> Optional[Path]:
    for p in paths:
        if p.exists() and p.is_file():
            return p
    return None


def find_runtime_bin() -> Optional[Path]:
    names = [f"thermoconsole{EXE_SUFFIX}"]
    return _first_existing(
        RUNTIME_DIR / "build" / sub / n
        for sub in ("", "Release", "Debug", "RelWithDebInfo")
        for n in names
    ) or _first_existing(
        [RUNTIME_DIR / "build" / n for n in names]
    )


def find_editor_bin() -> Optional[Path]:
    names = [f"thermo_editor{EXE_SUFFIX}"]
    return _first_existing(
        EDITOR_DIR / "build" / sub / n
        for sub in ("", "Release", "Debug", "RelWithDebInfo")
        for n in names
    ) or _first_existing(
        [EDITOR_DIR / "build" / n for n in names]
    )


# ───────────────────────────────────────────────────────────────────────────
# Run commands
# ───────────────────────────────────────────────────────────────────────────

def run_editor(project: Optional[str]) -> int:
    bin_path = find_editor_bin()
    if not bin_path:
        err("Editor not built. Run: python thermo.py build editor")
        return 1
    cmd: list[str] = [str(bin_path)]
    if project:
        cmd.append(str(Path(project).resolve()))
    step(f"Launching editor: {bin_path.relative_to(ROOT)}")
    # Use Popen so the user's Ctrl+C forwards cleanly
    try:
        return subprocess.call(cmd, cwd=str(bin_path.parent))
    except KeyboardInterrupt:
        return 0


def run_game(name_or_path: str) -> int:
    bin_path = find_runtime_bin()
    if not bin_path:
        err("Runtime not built. Run: python thermo.py build runtime")
        return 1

    candidate = Path(name_or_path)
    if not candidate.is_absolute():
        candidate = (ROOT / candidate).resolve()
    if not candidate.exists():
        # Treat as short name — look under games/
        alt = GAMES_DIR / name_or_path
        if alt.exists():
            candidate = alt
        else:
            err(f"Game not found: {name_or_path}")
            info(f"Tried: {candidate}  and  {alt}")
            return 1

    step(f"Launching {candidate.name}")
    try:
        return subprocess.call([str(bin_path), str(candidate)],
                               cwd=str(bin_path.parent))
    except KeyboardInterrupt:
        return 0


def run_launcher() -> int:
    launcher = ROOT / "launch.py"
    if not launcher.exists():
        err(f"Missing {launcher}")
        return 1
    return subprocess.call([sys.executable, str(launcher)])


# ───────────────────────────────────────────────────────────────────────────
# SDK wrappers — small, but one consistent entrypoint beats remembering paths
# ───────────────────────────────────────────────────────────────────────────

def sdk_passthrough(script: str, args: list[str]) -> int:
    path = SDK_DIR / script
    if not path.exists():
        err(f"Missing SDK script: {path}")
        return 1
    return subprocess.call([sys.executable, str(path), *args])


# ───────────────────────────────────────────────────────────────────────────
# Status & doctor
# ───────────────────────────────────────────────────────────────────────────

def cmd_status() -> int:
    step("ThermoConsole — status")
    print(f"  Repo root : {ROOT}")
    print(f"  Platform  : {platform.platform()}")
    print(f"  Python    : {sys.version.split()[0]}")

    print("\n  Build artifacts:")
    for label, finder in [("runtime", find_runtime_bin), ("editor", find_editor_bin)]:
        b = finder()
        print(f"    {label:8} : {b.relative_to(ROOT) if b else _c('90', '(not built)')}")

    print("\n  Dependencies:")
    for d in check_deps():
        mark = _c("92", "✓") if d.found else _c("91", "✗")
        extra = f" — {d.detail}" if d.detail else ""
        print(f"    {mark} {d.name}{extra}")

    print("\n  Games:")
    if GAMES_DIR.exists():
        games = sorted(p.name for p in GAMES_DIR.iterdir()
                       if p.is_dir() and (p / "manifest.json").exists())
        if games:
            for g in games:
                print(f"    • {g}")
        else:
            print(_c("90", "    (none)"))
    else:
        print(_c("90", "    (games/ not found)"))
    return 0


def cmd_doctor() -> int:
    step("ThermoConsole — doctor")
    deps = check_deps()
    missing = [d for d in deps if not d.found]
    if not missing:
        ok("All prerequisites satisfied.")
        return 0
    for d in missing:
        err(f"Missing: {d.name}")
        if d.hint:
            print(f"    hint: {d.hint}")
    print()
    info("Quick fix: python thermo.py setup")
    return 1


# ───────────────────────────────────────────────────────────────────────────
# CLI
# ───────────────────────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="thermo",
        description="ThermoConsole project orchestrator.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="command", required=True, metavar="command")

    ps = sub.add_parser("setup", help="Install deps and clone ImGui.")
    ps.add_argument("--yes", "-y", action="store_true",
                    help="Skip interactive confirmations during package install.")
    ps.add_argument("--skip-system", action="store_true",
                    help="Skip OS package installation (ImGui only).")

    pb = sub.add_parser("build", help="Build runtime, editor, or both.")
    pb.add_argument("target", nargs="?", default="all",
                    choices=["all", "editor", "runtime"])
    pb.add_argument("--debug", action="store_true", help="Debug build.")
    pb.add_argument("--clean", action="store_true",
                    help="Wipe build dir before configuring.")
    pb.add_argument("-j", "--jobs", type=int,
                    help="Parallel build jobs (default: all cores).")

    pc = sub.add_parser("clean", help="Remove build artifacts.")
    pc.add_argument("target", nargs="?", default="all",
                    choices=["all", "editor", "runtime"])

    pr = sub.add_parser("run", help="Run the editor, a game, or the launcher.")
    pr_sub = pr.add_subparsers(dest="what", required=True, metavar="what")
    pre = pr_sub.add_parser("editor", help="Run the editor.")
    pre.add_argument("project", nargs="?", help="Optional project path.")
    prg = pr_sub.add_parser("game", help="Run a game via the runtime.")
    prg.add_argument("game", help="Game name under games/ or path to folder/.tcr.")
    pr_sub.add_parser("launcher", help="Open the interactive launcher.")

    pp = sub.add_parser("pack", help="Pack a game folder into a .tcr ROM.")
    pp.add_argument("args", nargs=argparse.REMAINDER,
                    help="Arguments forwarded to sdk/pack_rom.py")

    pn = sub.add_parser("new", help="Scaffold a new game (wraps sdk/new_game.py).")
    pn.add_argument("args", nargs=argparse.REMAINDER,
                    help="Arguments forwarded to sdk/new_game.py")

    pu = sub.add_parser("unpack", help="Unpack a .tcr ROM.")
    pu.add_argument("args", nargs=argparse.REMAINDER,
                    help="Arguments forwarded to sdk/unpack_rom.py")

    sub.add_parser("status", help="Show build + dependency state.")
    sub.add_parser("doctor", help="Diagnose missing prerequisites.")

    return p


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "setup":
            if not args.skip_system:
                install_system_deps(assume_yes=args.yes)
            clone_or_update_imgui()
            ok("Setup complete. Try: python thermo.py build")
            return 0

        if args.command == "build":
            if args.target == "all":
                build_target("runtime", debug=args.debug, clean=args.clean, jobs=args.jobs)
                build_target("editor",  debug=args.debug, clean=args.clean, jobs=args.jobs)
            else:
                build_target(args.target, debug=args.debug, clean=args.clean, jobs=args.jobs)
            return 0

        if args.command == "clean":
            clean_target(args.target)
            return 0

        if args.command == "run":
            if args.what == "editor":
                return run_editor(args.project)
            if args.what == "game":
                return run_game(args.game)
            if args.what == "launcher":
                return run_launcher()

        if args.command == "pack":
            return sdk_passthrough("pack_rom.py", args.args)
        if args.command == "unpack":
            return sdk_passthrough("unpack_rom.py", args.args)
        if args.command == "new":
            return sdk_passthrough("new_game.py", args.args)

        if args.command == "status":
            return cmd_status()
        if args.command == "doctor":
            return cmd_doctor()

        err(f"Unknown command: {args.command}")
        return 2

    except subprocess.CalledProcessError as e:
        return e.returncode or 1
    except KeyboardInterrupt:
        warn("Interrupted.")
        return 130


if __name__ == "__main__":
    sys.exit(main())
