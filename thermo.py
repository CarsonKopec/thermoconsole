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
import re
import shutil
import subprocess
import sys
import zipfile
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

# Windows cmd.exe doesn't render ANSI escapes by default. Flip
# ENABLE_VIRTUAL_TERMINAL_PROCESSING on both output handles so colored output
# works there too. Already-VT-aware terminals (PowerShell 7, Windows Terminal,
# cmd.exe on recent Win10+) are unaffected.
if sys.platform == "win32":
    try:
        import ctypes
        _kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
        _ENABLE_VT = 0x0004
        for _handle_id in (-11, -12):  # STD_OUTPUT_HANDLE, STD_ERROR_HANDLE
            _h = _kernel32.GetStdHandle(_handle_id)
            _mode = ctypes.c_uint32()
            if _kernel32.GetConsoleMode(_h, ctypes.byref(_mode)):
                _kernel32.SetConsoleMode(_h, _mode.value | _ENABLE_VT)
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
VENDOR_DIR  = ROOT / "vendor"

# ───────────────────────────────────────────────────────────────────────────
# Vendored C/C++ dependencies
# ───────────────────────────────────────────────────────────────────────────
#
# Pinned git refs so `thermo setup` is fully reproducible — same commit
# everywhere, no vcpkg/brew/apt required. To bump a pin, edit `ref` below
# and run `thermo setup --update-deps`. Each entry is cloned to vendor/<dir>.
#
# Fallback branches are used when the pinned ref doesn't resolve (typo,
# offline, or the tag was moved) — better than failing outright.

@dataclass
class VendoredDep:
    name: str             # display name
    url: str              # git URL
    ref: str              # tag / branch / SHA (pinned)
    fallback_branch: str  # used when `ref` doesn't resolve
    dir: str              # subdirectory under vendor/


VENDORED_DEPS: list[VendoredDep] = [
    VendoredDep("imgui",     "https://github.com/ocornut/imgui",        "v1.91.9-docking",   "docking",        "imgui"),
    VendoredDep("SDL",       "https://github.com/libsdl-org/SDL",       "release-2.30.11",   "SDL2",           "SDL"),
    VendoredDep("SDL_image", "https://github.com/libsdl-org/SDL_image", "release-2.8.3",     "SDL2",           "SDL_image"),
    VendoredDep("SDL_mixer", "https://github.com/libsdl-org/SDL_mixer", "release-2.8.0",     "SDL2",           "SDL_mixer"),
    VendoredDep("lua",       "https://github.com/lua/lua",              "v5.4.7",            "master",         "lua"),
]


def _dep(name: str) -> VendoredDep:
    for d in VENDORED_DEPS:
        if d.name == name:
            return d
    raise KeyError(name)


# Legacy aliases — the old --imgui-ref flag still works and overrides the
# imgui entry only. Everything else is controlled by editing VENDORED_DEPS.
IMGUI_URL    = _dep("imgui").url
IMGUI_BRANCH = _dep("imgui").fallback_branch
IMGUI_REF    = _dep("imgui").ref

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

    # SDL2 / SDL2_image / SDL2_mixer — prefer vendored sources under vendor/;
    # fall back to "is it installed on the system?" so users with a
    # pre-vcpkg/brew/apt setup still see a pass.
    for dep_name, header in (
        ("SDL",       "include/SDL.h"),
        ("SDL_image", "SDL_image.h"),
        ("SDL_mixer", "SDL_mixer.h"),
    ):
        dep = _dep(dep_name)
        vend = VENDOR_DIR / dep.dir
        if (vend / header).exists():
            sha = _git_head_sha(vend) or "?"
            results.append(DepStatus(
                dep_name, True, detail=f"vendored at {sha[:10]}",
                hint=f"python thermo.py setup  (pin: {dep.ref})"))
        elif dep_name == "SDL":
            # For bare SDL we can probe pkg-config / vcpkg as a fallback
            found, detail, hint = _check_sdl2()
            results.append(DepStatus("SDL (dev)", found,
                                     detail=detail or "not vendored",
                                     hint=hint or f"python thermo.py setup"))
        else:
            results.append(DepStatus(
                dep_name, False, detail="not vendored",
                hint=f"python thermo.py setup  (pin: {dep.ref})"))

    # Lua — only needed by the runtime, but we still surface the status.
    lua_dep = _dep("lua")
    lua_vend = VENDOR_DIR / lua_dep.dir
    if (lua_vend / "src" / "lua.h").exists():
        sha = _git_head_sha(lua_vend) or "?"
        results.append(DepStatus("lua", True, detail=f"vendored at {sha[:10]}",
                                 hint=f"python thermo.py setup  (pin: {lua_dep.ref})"))
    else:
        results.append(DepStatus("lua", False, detail="not vendored",
                                 hint=f"python thermo.py setup  (pin: {lua_dep.ref})"))

    # ImGui — vendored into vendor/imgui, pinned to the imgui entry's ref.
    imgui_dep = _dep("imgui")
    imgui_repo = VENDOR_DIR / imgui_dep.dir
    imgui_header = imgui_repo / "imgui.h"
    branch = _git_current_branch(imgui_repo)
    sha    = _git_head_sha(imgui_repo)
    if imgui_header.exists():
        short_sha = sha[:10] if sha else "?"
        ref_label = branch if branch and branch != "HEAD" else short_sha
        on_pin = branch == imgui_dep.ref or (sha and sha.startswith(imgui_dep.ref))
        acceptable = on_pin or branch == imgui_dep.fallback_branch
        detail = f"at {ref_label}" + ("" if on_pin else f"  (pin: {imgui_dep.ref})")
    else:
        acceptable = False
        detail = "not cloned"
    results.append(DepStatus(
        "Dear ImGui (vendor/imgui)",
        acceptable,
        detail=detail,
        hint=f"python thermo.py setup  (pins to '{imgui_dep.ref}')",
    ))

    return results


def _capture_version(cmd: Sequence[str]) -> str:
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=False)
        first = (out.stdout or out.stderr or "").splitlines()
        return first[0].strip() if first else ""
    except FileNotFoundError:
        return ""


def _check_sdl2() -> tuple[bool, str, str]:
    """Return (found, detail, hint) for SDL2+SDL2_image.

    Preferred path: vendored clone at vendor/SDL. Fallback: pkg-config
    (Linux/Homebrew) or vcpkg (Windows).
    """
    # Vendored sources win — that's what CMake prefers too.
    if (VENDOR_DIR / "SDL" / "include" / "SDL.h").exists() and \
       (VENDOR_DIR / "SDL_image" / "SDL_image.h").exists():
        return True, "vendored (vendor/SDL, vendor/SDL_image)", ""

    # pkg-config covers Linux distros and Homebrew on macOS
    if have("pkg-config"):
        pkgs = ["sdl2", "SDL2_image"]
        r = subprocess.run(["pkg-config", "--exists", *pkgs],
                           capture_output=True, text=True, check=False)
        if r.returncode == 0:
            ver = subprocess.run(["pkg-config", "--modversion", "sdl2"],
                                 capture_output=True, text=True, check=False)
            return True, f"sdl2 {ver.stdout.strip() or '?'} (pkg-config)", ""

    # vcpkg on Windows — look in $VCPKG_ROOT/installed/*/include/SDL2/SDL.h
    if IS_WINDOWS:
        vcpkg_root = os.environ.get("VCPKG_ROOT")
        if vcpkg_root:
            installed = Path(vcpkg_root) / "installed"
            if installed.exists():
                for triplet in installed.iterdir():
                    if (triplet / "include" / "SDL2" / "SDL.h").exists():
                        return True, f"vcpkg ({triplet.name})", ""

    # Not found anywhere — point at the vendoring path first.
    return False, "not detected", "python thermo.py setup  (clones SDL/SDL_image under vendor/)"


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

    # Windows — vendored deps handle the C/C++ libraries. All the user
    # needs is the MSVC toolchain, CMake, and Git, installed manually.
    if IS_WINDOWS:
        info("No package install needed — all C/C++ deps are vendored into "
             "vendor/ by the next step.")
        missing = [x for x in ("cmake", "git") if not have(x)]
        if not any(have(c) for c in ("cl", "clang")):
            missing.append("MSVC toolchain")
        if missing:
            warn(f"Still missing: {', '.join(missing)}")
            print(
                "    Install manually:\n"
                "      - CMake:            https://cmake.org/download/\n"
                "      - Git:              https://git-scm.com/\n"
                "      - MSVC Build Tools: https://aka.ms/vs/17/release/vs_BuildTools.exe\n"
                "                          (tick 'Desktop development with C++')"
            )
        return

    warn(f"Unknown platform {sys.platform}; skipping system deps.")


def _git_head_sha(repo: Path) -> Optional[str]:
    r = subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"],
                       capture_output=True, text=True, check=False)
    return r.stdout.strip() if r.returncode == 0 else None


def _clone_or_update_repo(dep: VendoredDep, ref_override: Optional[str],
                          update: bool) -> None:
    """Ensure `dep` is cloned into vendor/<dir> at `ref_override` (if given)
    or `dep.ref`. Falls back to `dep.fallback_branch` when the pinned ref
    doesn't resolve. Idempotent — safe to call on every setup."""
    target_ref = ref_override or dep.ref
    target     = VENDOR_DIR / dep.dir

    info(_c("1", f"  {dep.name}  @ {target_ref}"))
    VENDOR_DIR.mkdir(parents=True, exist_ok=True)

    def _fresh_clone() -> None:
        if target.exists():
            shutil.rmtree(target)
        r = subprocess.run(
            ["git", "clone", "--depth", "1", "-b", target_ref,
             dep.url, str(target)],
            check=False,
        )
        if r.returncode != 0:
            warn(f"    '{target_ref}' not found; falling back to '{dep.fallback_branch}'.")
            if target.exists():
                shutil.rmtree(target)
            run(["git", "clone", "--depth", "1", "-b", dep.fallback_branch,
                 dep.url, str(target)])
        sha = _git_head_sha(target) or "?"
        ok(f"    {dep.name} {sha[:10]}")

    if not (target / ".git").exists():
        _fresh_clone()
        return

    current_branch = _git_current_branch(target)
    current_sha    = _git_head_sha(target) or ""
    at_pin = current_branch == target_ref or current_sha.startswith(target_ref)

    if at_pin and not update:
        ok(f"    {dep.name} already at {target_ref} ({current_sha[:10]})")
        return

    # Shallow clones don't have arbitrary refs — fetch the exact one, then
    # check it out. Re-clone on any failure so we don't leave a half-moved repo.
    fetched = subprocess.run(
        ["git", "-C", str(target), "fetch", "--depth", "1", "origin", target_ref],
        check=False,
    )
    if fetched.returncode == 0:
        checkout = subprocess.run(
            ["git", "-C", str(target), "checkout", "--detach", "FETCH_HEAD"],
            check=False,
        )
        if checkout.returncode == 0:
            sha = _git_head_sha(target) or "?"
            ok(f"    {dep.name} → {target_ref} ({sha[:10]})")
            return

    warn(f"    {dep.name} fetch/checkout failed — re-cloning.")
    _fresh_clone()


def clone_or_update_vendored_deps(
    imgui_ref: Optional[str] = None,
    update: bool = False,
    only: Optional[Iterable[str]] = None,
) -> None:
    """Clone (or update) every vendored dep into vendor/. Idempotent.

    `imgui_ref` overrides the imgui pin (legacy --imgui-ref flag).
    `only` restricts to a subset of dep names.
    `update` forces re-fetch even if already at the pin.
    """
    if not have("git"):
        err("git is required. Install it and re-run.")
        raise SystemExit(1)

    # One-time migration: old layout had editor/vendor/imgui; new layout is
    # vendor/imgui at repo root. Clean up the old path so it doesn't shadow.
    legacy_imgui = EDITOR_DIR / "vendor" / "imgui"
    if legacy_imgui.exists():
        warn(f"Removing legacy {legacy_imgui.relative_to(ROOT)} (now at vendor/imgui)")
        shutil.rmtree(legacy_imgui)
        legacy_parent = EDITOR_DIR / "vendor"
        try:
            legacy_parent.rmdir()  # only succeeds if empty
        except OSError:
            pass

    step(f"Fetching vendored dependencies into {VENDOR_DIR.relative_to(ROOT)}/")
    only_set = set(only) if only else None
    for dep in VENDORED_DEPS:
        if only_set and dep.name not in only_set:
            continue
        override = imgui_ref if dep.name == "imgui" else None
        _clone_or_update_repo(dep, override, update)


# Back-compat shim — old callers (and the --imgui-ref setup flag) land here.
def clone_or_update_imgui(ref: Optional[str] = None, update: bool = False) -> None:
    clone_or_update_vendored_deps(imgui_ref=ref, update=update, only=["imgui"])


# Header files we probe to decide if a vendored dep is "present enough" to
# skip fetching. These are paths under vendor/<dir>/.
_VENDOR_PROBES: dict[str, str] = {
    "imgui":     "imgui.h",
    "SDL":       "include/SDL.h",
    "SDL_image": "SDL_image.h",
    "SDL_mixer": "SDL_mixer.h",
    "lua":       "src/lua.h",
}


def _vendor_dep_present(name: str) -> bool:
    dep = _dep(name)
    probe = _VENDOR_PROBES.get(name)
    if not probe:
        return (VENDOR_DIR / dep.dir / ".git").exists()
    return (VENDOR_DIR / dep.dir / probe).exists()


# ───────────────────────────────────────────────────────────────────────────
# Build
# ───────────────────────────────────────────────────────────────────────────

@dataclass
class BuildTarget:
    name: str
    source_dir: Path
    build_dir: Path
    # Names from VENDORED_DEPS that must be present before building.
    # If any are missing, `thermo build` auto-fetches them.
    vendor_deps: list[str] = field(default_factory=list)
    requires_imgui: bool = False
    requires_pico_sdk: bool = False
    # Opt-in targets aren't built by `thermo build all` — the pico firmware
    # needs a cross-compiler toolchain most contributors don't have.
    in_all: bool = True
    extra_cmake_args: list[str] = field(default_factory=list)


TARGETS: dict[str, BuildTarget] = {
    "runtime": BuildTarget(
        name="runtime",
        source_dir=RUNTIME_DIR,
        build_dir=RUNTIME_DIR / "build",
        vendor_deps=["SDL", "SDL_image", "SDL_mixer", "lua"],
    ),
    "editor": BuildTarget(
        name="editor",
        source_dir=EDITOR_DIR,
        build_dir=EDITOR_DIR / "build",
        vendor_deps=["imgui", "SDL", "SDL_image"],
        requires_imgui=True,
    ),
    "pico": BuildTarget(
        name="pico",
        source_dir=PICO_DIR,
        build_dir=PICO_DIR / "build",
        requires_pico_sdk=True,
        in_all=False,
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

    # Auto-fetch any vendored deps the target declares as required.
    # Cheap if already present (just probes a header file).
    missing = [n for n in t.vendor_deps if not _vendor_dep_present(n)]
    if missing:
        warn(f"Vendored deps missing: {', '.join(missing)}. Fetching now.")
        clone_or_update_vendored_deps(only=missing)

    if t.requires_pico_sdk and not os.environ.get("PICO_SDK_PATH"):
        err("PICO_SDK_PATH is not set. The pico firmware needs the Raspberry "
            "Pi Pico SDK.")
        info("Install: https://github.com/raspberrypi/pico-sdk")
        info("Then: export PICO_SDK_PATH=/path/to/pico-sdk")
        raise SystemExit(1)

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


def _targets_for(name: str) -> list[str]:
    """Resolve 'all' to the default set (everything with in_all=True)."""
    if name == "all":
        return [k for k, t in TARGETS.items() if t.in_all]
    return [name]


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
# Package — bundle built binaries + SDK + games into a release zip
# ───────────────────────────────────────────────────────────────────────────

_VERSION_RE = re.compile(r"project\s*\([^)]*VERSION\s+([0-9][0-9.]*)", re.IGNORECASE)


def _read_project_version() -> str:
    """Extract VERSION from editor/CMakeLists.txt (authoritative for releases)."""
    cmake = EDITOR_DIR / "CMakeLists.txt"
    if cmake.exists():
        m = _VERSION_RE.search(cmake.read_text(encoding="utf-8", errors="ignore"))
        if m:
            return m.group(1)
    return "0.0.0"


def _platform_tag() -> str:
    arch = platform.machine().lower()
    # Normalize x86_64 aliases across platforms
    arch = {"amd64": "x64", "x86_64": "x64"}.get(arch, arch)
    if IS_WINDOWS: return f"win-{arch}"
    if IS_MACOS:   return f"macos-{arch}"
    if IS_LINUX:   return f"linux-{arch}"
    return sys.platform


def _zip_add_file(zf: zipfile.ZipFile, src: Path, arcname: str) -> None:
    info(_c("90", f"  + {arcname}"))
    zf.write(src, arcname)


def _zip_add_tree(zf: zipfile.ZipFile, src: Path, arc_root: str,
                  ignore: Iterable[str] = ()) -> None:
    if not src.exists():
        return
    ignore_set = set(ignore)
    for path in sorted(src.rglob("*")):
        if path.is_dir():
            continue
        # Skip common junk: build dirs, .git, __pycache__
        rel = path.relative_to(src)
        parts = set(rel.parts)
        if parts & {"build", ".git", "__pycache__", ".vs", ".vscode"}:
            continue
        if path.name in ignore_set:
            continue
        _zip_add_file(zf, path, f"{arc_root}/{rel.as_posix()}")


def cmd_package(output: Optional[Path]) -> int:
    step("ThermoConsole — package")

    editor_bin  = find_editor_bin()
    runtime_bin = find_runtime_bin()
    if not editor_bin and not runtime_bin:
        err("Nothing to package — neither editor nor runtime is built.")
        info("Run: python thermo.py build")
        return 1
    if not editor_bin:
        warn("Editor binary missing — packaging without it.")
    if not runtime_bin:
        warn("Runtime binary missing — packaging without it.")

    version  = _read_project_version()
    tag      = _platform_tag()
    dist_dir = ROOT / "dist"
    dist_dir.mkdir(exist_ok=True)
    zip_path = output or dist_dir / f"thermoconsole-{version}-{tag}.zip"
    if zip_path.exists():
        zip_path.unlink()

    info(f"Writing {zip_path.relative_to(ROOT)}")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        if editor_bin:
            _zip_add_file(zf, editor_bin, f"bin/{editor_bin.name}")
        if runtime_bin:
            _zip_add_file(zf, runtime_bin, f"bin/{runtime_bin.name}")

        _zip_add_tree(zf, SDK_DIR,   "sdk")
        _zip_add_tree(zf, GAMES_DIR, "games")

        for top in ("launch.py", "README.md", "LICENSE", "LICENSE.md"):
            p = ROOT / top
            if p.exists():
                _zip_add_file(zf, p, top)

    size_mb = zip_path.stat().st_size / (1024 * 1024)
    ok(f"Packaged {version} for {tag} → {zip_path.relative_to(ROOT)} ({size_mb:.1f} MB)")
    return 0


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
    pico_uf2 = next((PICO_DIR / "build").glob("*.uf2"), None) if (PICO_DIR / "build").exists() else None
    print(f"    {'pico':8} : {pico_uf2.relative_to(ROOT) if pico_uf2 else _c('90', '(not built)')}")

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

    ps = sub.add_parser("setup", help="Install deps and clone vendored libraries into vendor/.")
    ps.add_argument("--yes", "-y", action="store_true",
                    help="Skip interactive confirmations during package install.")
    ps.add_argument("--skip-system", action="store_true",
                    help="Skip OS package installation (vendor/ only — no sudo needed).")
    ps.add_argument("--only", metavar="NAME", action="append",
                    choices=[d.name for d in VENDORED_DEPS],
                    help="Fetch only the named vendored dep. Repeat for multiple.")
    ps.add_argument("--imgui-ref", metavar="REF",
                    help=f"Override the ImGui pin (default: {IMGUI_REF}). "
                         "Tag, branch, or SHA.")
    ps.add_argument("--update-deps", action="store_true",
                    help="Force re-fetch of every vendored dep, even if already "
                         "at its pin. Use this after editing a ref in the file.")
    # Legacy alias — preserved so old muscle memory still works.
    ps.add_argument("--update-imgui", action="store_true",
                    help=argparse.SUPPRESS)

    build_choices = ["all"] + list(TARGETS)
    pb = sub.add_parser("build", help="Build runtime, editor, pico, or all.")
    pb.add_argument("target", nargs="?", default="all", choices=build_choices,
                    help="Target to build. 'all' skips opt-in targets like pico.")
    pb.add_argument("--debug", action="store_true", help="Debug build.")
    pb.add_argument("--clean", action="store_true",
                    help="Wipe build dir before configuring.")
    pb.add_argument("-j", "--jobs", type=int,
                    help="Parallel build jobs (default: all cores).")

    pc = sub.add_parser("clean", help="Remove build artifacts.")
    pc.add_argument("target", nargs="?", default="all", choices=build_choices)

    pkg = sub.add_parser("package", help="Bundle built binaries + SDK + games into a release zip.")
    pkg.add_argument("-o", "--output", type=Path,
                     help="Output zip path (default: dist/thermoconsole-<ver>-<platform>.zip).")

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
            clone_or_update_vendored_deps(
                imgui_ref=args.imgui_ref,
                update=args.update_deps or args.update_imgui,
                only=args.only,
            )
            ok("Setup complete. Try: python thermo.py build")
            return 0

        if args.command == "build":
            for name in _targets_for(args.target):
                build_target(name, debug=args.debug, clean=args.clean, jobs=args.jobs)
            return 0

        if args.command == "clean":
            clean_target(args.target)
            return 0

        if args.command == "package":
            return cmd_package(args.output)

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
