"""Thin wrapper around the toolchain: Dialog (dialogc, dgdebug) and the Å-machine
(aambundle, aamshow).

Each repo is built in its own src/ directory, and the resulting tools are copied
into prototype/bin/, which is where they're checked for and run from.

On Windows the build runs inside WSL, which cross-compiles native .exe tools with
MinGW. Build commands go through wsl.exe from the repo's src/ directory, which WSL
maps to its /mnt/<drive>/... path, so no Windows paths are passed to them. The
.exe tools in bin/ then run directly on Windows, without WSL. Cloning uses the
host's own git on every platform.
"""

import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]  # prototype/
PRIOR_ART = REPO_ROOT.parent / "_references"
BIN = REPO_ROOT / "bin"
USE_WSL = sys.platform == "win32"
EXE = ".exe" if USE_WSL else ""

# The MinGW cross-compiler both repos' Makefiles use for their .exe targets.
MINGW = "i686-w64-mingw32-gcc"


@dataclass(frozen=True)
class Repo:
    """A source repo that provides part of the toolchain."""

    name: str
    url: str
    env: str  # environment variable that overrides the checkout location
    tools: tuple[str, ...]  # executables the build leaves in src/, copied to bin/
    files: dict[str, str] = field(default_factory=dict)  # name -> path in checkout
    prereqs: tuple[str, ...] = ()  # programs the build needs beyond make and cc

    # Each inner tuple is one make run, in order; () means the default target.
    builds: tuple[tuple[str, ...], ...] = ((),)
    windows_builds: tuple[tuple[str, ...], ...] = ((),)

    @property
    def dir(self) -> Path:
        return Path(os.environ.get(self.env, PRIOR_ART / self.name))

    @property
    def src(self) -> Path:
        return self.dir / "src"


DIALOG = Repo(
    name="dialog",
    url="https://github.com/Dialog-IF/dialog.git",
    env="QUIDDITY_DIALOG_DIR",
    tools=("dialogc", "dgdebug"),
    files={"stdlib": "stdlib.dg"},
    builds=(("dialogc", "dgdebug"),),
    windows_builds=(("dialogc.exe", "dgdebug.exe"),),
)

AAMACHINE = Repo(
    name="aamachine",
    url="https://github.com/Dialog-IF/aamachine.git",
    env="QUIDDITY_AAMACHINE_DIR",
    tools=("aambundle", "aamshow"),
    # aambundle embeds 6502 code, which needs these cross-assemblers.
    prereqs=("xa", "acme"),
    # The Makefile's own "windows" target also builds aamrun.exe, which needs
    # Node's pkg, so build the 6502 side first and then just the two tools.
    windows_builds=(("6502",), ("aambundle.exe", "aamshow.exe")),
)

REPOS = (DIALOG, AAMACHINE)
STDLIB = DIALOG.dir / DIALOG.files["stdlib"]


def _exe(tool: str) -> str:
    return tool + EXE


def _runnable(p: Path) -> bool:
    return p.is_file() and os.access(p, os.X_OK)


def _run_build(
    cmd: list[str], cwd: Path | None = None, capture: bool = False
) -> subprocess.CompletedProcess[str]:
    if USE_WSL:
        cmd = ["wsl", *cmd]

    return subprocess.run(cmd, cwd=cwd, capture_output=capture, text=True, check=False)


def _has_program(name: str) -> bool:
    try:
        return _run_build(["which", name], capture=True).returncode == 0
    except OSError:
        return False


def _normalize_transcript(raw: str) -> str:
    """Collapse dgdebug's tagged-line output into a plain, diffable transcript."""

    out: list[str] = []

    for line in raw.replace("\0", "").splitlines():
        line = line.removeprefix("  ")

        if line.strip() == ">":  # bare prompt echo before each input line
            continue

        out.append(line.rstrip())

    text = "\n".join(out).strip("\n") + "\n"

    return re.sub(r"\n{3,}", "\n\n", text)


@dataclass
class RepoStatus:
    """Status of one toolchain repo."""

    repo: Repo
    present: bool
    tools: dict[str, bool]
    files: dict[str, bool]
    missing_prereqs: list[str]  # only checked while tools still need building

    @property
    def ok(self) -> bool:
        return self.present and all(self.tools.values()) and all(self.files.values())


@dataclass
class ToolStatus:
    """Status of the whole toolchain."""

    repos: list[RepoStatus]
    version: str | None

    @property
    def ok(self) -> bool:
        """Return True if the toolchain is usable."""
        return all(r.ok for r in self.repos)


class BuildError(Exception):
    pass


class ToolMissing(Exception):
    pass


class ToolFailed(Exception):
    pass


def _status(repo: Repo) -> RepoStatus:
    tools = {t: _runnable(BIN / _exe(t)) for t in repo.tools}

    # Prerequisites only matter if make still has to produce the tools. When
    # they're already built in src/, "quiddity build" just copies them.
    built = all(tools.values()) or all(
        _runnable(repo.src / _exe(t)) for t in repo.tools
    )
    needed = (*repo.prereqs, MINGW) if USE_WSL else repo.prereqs
    missing = [] if built else [p for p in needed if not _has_program(p)]

    return RepoStatus(
        repo=repo,
        present=repo.dir.is_dir(),
        tools=tools,
        files={n: (repo.dir / p).is_file() for n, p in repo.files.items()},
        missing_prereqs=missing,
    )


def clone(repo: Repo) -> None:
    repo.dir.parent.mkdir(parents=True, exist_ok=True)

    # Keep LF line endings even when git on Windows is set to convert them: the
    # checkout is built inside WSL, where CRLF breaks make and shell scripts.
    cmd = ["git", "clone", "-c", "core.autocrlf=false", repo.url, str(repo.dir)]

    try:
        rc = subprocess.run(cmd, check=False).returncode
    except FileNotFoundError:
        raise BuildError("git not found. Install it and try again.") from None

    if rc != 0:
        raise BuildError(f"Cloning {repo.name} failed (git exited with {rc}).")


def make(repo: Repo) -> int:
    if not repo.src.is_dir():
        raise BuildError(
            f"{repo.name} source not found at {repo.src}. The checkout looks incomplete."
        )

    missing = _status(repo).missing_prereqs

    if missing:
        where = " inside WSL" if USE_WSL else ""
        raise BuildError(
            f"Building {repo.name} needs {', '.join(missing)} installed{where}."
        )

    for targets in repo.windows_builds if USE_WSL else repo.builds:
        try:
            rc = _run_build(["make", *targets], cwd=repo.src).returncode
        except FileNotFoundError:
            missing_cmd = "wsl" if USE_WSL else "make"
            raise BuildError(
                f"{missing_cmd} not found. Install it and try again."
            ) from None

        # Inside WSL, a missing make comes back as the shell's "command not found".
        if USE_WSL and rc == 127:
            raise BuildError(
                "make not found inside WSL. Install build-essential there."
            )

        if rc != 0:
            return rc

    return 0


def install(repo: Repo) -> None:
    BIN.mkdir(exist_ok=True)

    for t in repo.tools:
        built = repo.src / _exe(t)

        if not built.is_file():
            raise BuildError(f"The {repo.name} build didn't produce {built.name}.")

        # copy2 keeps the executable bit.
        shutil.copy2(built, BIN / built.name)

        # Earlier Windows builds copied Linux binaries with no extension.
        if USE_WSL:
            (BIN / t).unlink(missing_ok=True)


def version() -> str | None:
    cmd = [str(BIN / _exe("dialogc")), "--version"]

    try:
        r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except OSError:
        return None

    # dialogc prints its version to stderr.
    lines = (r.stdout + r.stderr).strip().splitlines()

    return lines[0] if r.returncode == 0 and lines else None


def check() -> ToolStatus:
    repos = [_status(r) for r in REPOS]
    dialogc = _runnable(BIN / _exe("dialogc"))

    return ToolStatus(repos=repos, version=version() if dialogc else None)


def compile(
    sources: list[Path], out: Path, fmt: str = "aa", stdlib: bool = True
) -> int:
    """Compile Dialog sources into a story file. fmt is one of z5, z8, zblorb, aa.

    dialogc's own messages go straight to the terminal. Returns its exit code.
    """

    dialogc = BIN / _exe("dialogc")

    if not _runnable(dialogc):
        raise ToolMissing(f'{dialogc} not found. Run "quiddity build" first.')

    # The standard library goes last, after the story's own sources.
    files = [*sources, STDLIB] if stdlib else sources
    cmd = [str(dialogc), "-t", fmt, "-o", str(out), *map(str, files)]

    return subprocess.run(cmd, check=False).returncode


def run_transcript(sources: list[Path], commands: str, stdlib: bool = True) -> str:
    """Run Dialog sources in dgdebug, feeding player commands on stdin. Returns the transcript."""

    dgdebug = BIN / _exe("dgdebug")

    if not _runnable(dgdebug):
        raise ToolMissing(f'{dgdebug} not found. Run "quiddity build" first.')

    files = [*sources, STDLIB] if stdlib else sources
    cmd = [str(dgdebug), "-u", "-T", "--no-links", *map(str, files)]
    r = subprocess.run(cmd, input=commands, capture_output=True, text=True, check=False)

    # dgdebug reports source errors on stdout, so a failure can still have output.
    if r.returncode != 0:
        raise ToolFailed((_normalize_transcript(r.stdout) + r.stderr).rstrip())

    return _normalize_transcript(r.stdout)
