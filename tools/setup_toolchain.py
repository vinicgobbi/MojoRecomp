"""Prepare the MojoRecomp recompilation toolchain on Windows x64 and Linux x64.

Checks the pinned third-party submodules, mirrors the ones MojoRecomp patches
into thirdparty/work/, applies the maintained patches there, and builds the
dependencies into thirdparty/build/. The submodules themselves stay pristine.

Usage:
  setup_toolchain.py [--cmake PATH] [--llvm-root DIR] [--vcvars PATH] [--jobs N]
  setup_toolchain.py apply-patch {xenon,xenos,extract-xiso} [CHECKOUT]
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import stat
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
THIRD = ROOT / "thirdparty"
BUILD = THIRD / "build"
WORK = THIRD / "work"
PATCHES = ROOT / "patches"

IS_WINDOWS = sys.platform == "win32"
IS_LINUX = sys.platform.startswith("linux")

SUBMODULES = (
    "XenonRecomp",
    "XenosRecomp",
    "FFmpeg",
    "SDL",
    "Vulkan-Headers",
    "extract-xiso",
)


@dataclass(frozen=True)
class PatchedSource:
    key: str
    name: str
    patch: str

    @property
    def source(self) -> Path:
        return THIRD / self.name

    @property
    def work(self) -> Path:
        return WORK / self.name


PATCHED_SOURCES = (
    PatchedSource("xenon", "XenonRecomp", "xenonrecomp-mojorecomp-runtime.patch"),
    PatchedSource("xenos", "XenosRecomp", "xenosrecomp-mojorecomp-runtime.patch"),
    PatchedSource("extract-xiso", "extract-xiso", "extract-xiso-mojorecomp-no-ftp.patch"),
)
PATCHED_BY_KEY = {source.key: source for source in PATCHED_SOURCES}


@dataclass(frozen=True)
class Component:
    name: str
    source: Path
    build: Path
    args: tuple[str, ...] = ()
    target: str | None = None
    # Work package that ports the build recipe to Linux, when it is still
    # Windows-only.
    linux_pending: str | None = None
    # Shown when the build fails on Linux, usually for missing system packages.
    linux_hint: str | None = None


COMPONENTS = (
    Component("XenonRecomp", WORK / "XenonRecomp", BUILD / "XenonRecomp"),
    Component(
        "XenosRecomp",
        WORK / "XenosRecomp",
        BUILD / "XenosRecomp",
        ("-DZSTD_BUILD_SHARED=OFF", "-DZSTD_BUILD_PROGRAMS=OFF"),
    ),
    Component(
        "FFmpeg",
        ROOT / "tools" / "ffmpeg-rexglue",
        BUILD / "FFmpeg",
        (f"-DFFMPEG_ROOT:PATH={(THIRD / 'FFmpeg').as_posix()}",),
        linux_pending="RP-16",
    ),
    Component(
        "SDL3",
        THIRD / "SDL",
        BUILD / "SDL",
        ("-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF"),
        target="SDL3-static",
        linux_hint="Install SDL's Linux build dependencies, listed in "
                   "https://wiki.libsdl.org/SDL3/README-linux#build-dependencies",
    ),
    Component(
        "extract-xiso",
        ROOT / "tools" / "extract-xiso",
        BUILD / "extract-xiso",
        (f"-DEXTRACT_XISO_ROOT:PATH={(WORK / 'extract-xiso').as_posix()}",),
        linux_pending="RP-18",
    ),
)


class SetupError(RuntimeError):
    pass


@dataclass
class Toolchain:
    cmake: str
    configure_args: list[str]
    env: dict[str, str] = field(default_factory=lambda: dict(os.environ))


def log(message: str) -> None:
    print(f"[setup] {message}", flush=True)


def run(command: list[str], *, cwd: Path | None = None,
        env: dict[str, str] | None = None) -> None:
    result = subprocess.run(command, cwd=cwd, env=env)
    if result.returncode != 0:
        raise SetupError(f"Command failed with exit code {result.returncode}: "
                         + " ".join(command))


def capture(command: list[str], *, cwd: Path | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SetupError(f"Command failed with exit code {result.returncode}: "
                         + " ".join(command) + "\n" + result.stderr.strip())
    return result.stdout


def require_host() -> None:
    machine = platform.machine().lower()
    if not (IS_WINDOWS or IS_LINUX) or machine not in ("amd64", "x86_64"):
        raise SetupError(
            f"Unsupported host {sys.platform}/{machine}. "
            "MojoRecomp supports Windows x64 and Linux x64.")


def require_tool(path: str | Path | None, label: str) -> str:
    if not path or not Path(path).exists():
        raise SetupError(f"Required tool is missing: {label}"
                         + (f" ({path})" if path else ""))
    return str(path)


def visual_studio_env(vcvars: Path | None) -> dict[str, str]:
    """Returns the environment of an x64 Visual Studio developer prompt."""
    if os.environ.get("VSCMD_ARG_TGT_ARCH") == "x64":
        return dict(os.environ)

    if vcvars is None:
        program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        require_tool(vswhere, "vswhere.exe (Visual Studio Installer)")
        install = capture([
            str(vswhere), "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ]).strip()
        if not install:
            raise SetupError("No Visual Studio installation with the x64 C++ tools was found.")
        vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    require_tool(vcvars, "vcvars64.bat")

    # cmd.exe receives the line verbatim; a list would be re-quoted by Python.
    result = subprocess.run(f'cmd /d /s /c ""{vcvars}" >nul && set"',
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise SetupError(f"Could not initialize the Visual Studio environment: {vcvars}")
    env: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            env[key] = value
    return env


def find_toolchain(args: argparse.Namespace) -> Toolchain:
    if IS_WINDOWS:
        env = visual_studio_env(args.vcvars)
        llvm_bin = (args.llvm_root or THIRD / "llvm22") / "bin"
        clang_cl = require_tool(llvm_bin / "clang-cl.exe", "clang-cl")
        lld_link = require_tool(llvm_bin / "lld-link.exe", "lld-link")
        compiler_args = [
            f"-DCMAKE_C_COMPILER={clang_cl}",
            f"-DCMAKE_CXX_COMPILER={clang_cl}",
            f"-DCMAKE_LINKER={lld_link}",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
        ]
    else:
        env = dict(os.environ)
        if args.llvm_root:
            clang = require_tool(args.llvm_root / "bin" / "clang", "clang")
            clangxx = require_tool(args.llvm_root / "bin" / "clang++", "clang++")
        else:
            clang = require_tool(shutil.which("clang"), "clang")
            clangxx = require_tool(shutil.which("clang++"), "clang++")
        compiler_args = [f"-DCMAKE_C_COMPILER={clang}", f"-DCMAKE_CXX_COMPILER={clangxx}"]

    search_path = env.get("PATH") or env.get("Path")
    cmake = require_tool(args.cmake or shutil.which("cmake", path=search_path), "cmake")
    require_tool(shutil.which("ninja", path=search_path), "ninja")
    return Toolchain(
        cmake=cmake,
        configure_args=["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", *compiler_args],
        env=env,
    )


def pinned_commit(name: str) -> str:
    # The superproject index records the gitlink that `submodule update` checks out.
    entry = capture(["git", "-C", str(ROOT), "ls-files", "--stage", f"thirdparty/{name}"])
    fields = entry.split()
    if len(fields) < 2 or fields[0] != "160000":
        raise SetupError(f"thirdparty/{name} is not a registered submodule.")
    return fields[1]


def check_submodules() -> None:
    log("Initializing third-party submodules")
    try:
        run(["git", "-C", str(ROOT), "submodule", "update", "--init", "--recursive"])
    except SetupError as error:
        raise SetupError("Could not initialize third-party source submodules.") from error

    for name in SUBMODULES:
        path = THIRD / name
        if not (path / ".git").exists():
            raise SetupError(f"{name} submodule is missing: {path}\n"
                             "Run: git submodule update --init --recursive")
        expected = pinned_commit(name)
        actual = capture(["git", "-C", str(path), "rev-parse", "HEAD"]).strip()
        if actual.lower() != expected.lower():
            raise SetupError(f"{name} is at {actual}, expected {expected}.\n"
                             "Run: git submodule update --init --recursive")
        if capture(["git", "-C", str(path), "status", "--porcelain"]).strip():
            raise SetupError(f"{name} submodule has local changes. "
                             "Keep upstream submodules pristine.")


def remove_tree(path: Path) -> None:
    def make_writable_and_retry(function, target, _info) -> None:
        os.chmod(target, stat.S_IWRITE)
        function(target)

    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=make_writable_and_retry)
    else:
        shutil.rmtree(path, onerror=make_writable_and_retry)


def sync_source(source: PatchedSource) -> None:
    log(f"Preparing the {source.name} patch work tree")
    if source.work.exists():
        remove_tree(source.work)
    try:
        shutil.copytree(source.source, source.work, symlinks=True,
                        ignore=shutil.ignore_patterns(".git"))
    except OSError as error:
        raise SetupError(f"Could not prepare {source.name} patch work tree: {error}") from error
    # A repository of its own keeps `git apply` paths relative to the work tree
    # instead of the enclosing MojoRecomp checkout.
    run(["git", "-C", str(source.work), "init", "-q"])


def apply_patch(source: PatchedSource, checkout: Path) -> None:
    patch = PATCHES / source.patch
    if not checkout.is_dir():
        raise SetupError(f"{source.name} checkout is missing: {checkout}")
    log(f"Applying {patch.name}")
    check = subprocess.run(["git", "apply", "--check", str(patch)], cwd=checkout)
    if check.returncode != 0:
        raise SetupError(f"MojoRecomp {source.name} patch conflicts with this checkout: {checkout}")
    run(["git", "apply", str(patch)], cwd=checkout)


def build_component(component: Component, toolchain: Toolchain, jobs: int) -> None:
    log(f"Building {component.name}")
    command = [toolchain.cmake, "--build", str(component.build), "--parallel", str(jobs)]
    if component.target:
        command += ["--target", component.target]
    try:
        run([toolchain.cmake, "-S", str(component.source), "-B", str(component.build),
             *toolchain.configure_args, *component.args], env=toolchain.env)
        run(command, env=toolchain.env)
    except SetupError as error:
        if IS_LINUX and component.linux_hint:
            raise SetupError(f"{error}\n{component.linux_hint}") from error
        raise


def setup(args: argparse.Namespace) -> None:
    toolchain = find_toolchain(args)
    check_submodules()

    BUILD.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    for source in PATCHED_SOURCES:
        sync_source(source)
    for source in PATCHED_SOURCES:
        apply_patch(source, source.work)

    skipped = []
    for component in COMPONENTS:
        if IS_LINUX and component.linux_pending:
            skipped.append(component)
            continue
        build_component(component, toolchain, args.jobs)

    for component in skipped:
        log(f"Skipped {component.name}: its build recipe is Windows-only until "
            f"{component.linux_pending}.")
    print("MojoRecomp recompilation toolchain is ready.")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare the MojoRecomp recompilation toolchain.")
    parser.add_argument("--cmake", type=Path,
                        help="CMake executable (default: found on PATH)")
    parser.add_argument("--llvm-root", type=Path,
                        help="LLVM installation to build with (default: thirdparty/llvm22 "
                             "on Windows, clang on PATH on Linux)")
    parser.add_argument("--vcvars", type=Path,
                        help="Windows only: vcvars64.bat to use instead of locating "
                             "Visual Studio with vswhere")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                        help="Parallel build jobs")
    commands = parser.add_subparsers(dest="command")
    patch_parser = commands.add_parser(
        "apply-patch", help="Apply one maintained patch to an existing checkout")
    patch_parser.add_argument("source", choices=sorted(PATCHED_BY_KEY))
    patch_parser.add_argument("checkout", type=Path, nargs="?",
                              help="Checkout to patch (default: its thirdparty/work tree)")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        require_host()
        if args.command == "apply-patch":
            source = PATCHED_BY_KEY[args.source]
            apply_patch(source, (args.checkout or source.work).resolve())
        else:
            setup(args)
    except SetupError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
