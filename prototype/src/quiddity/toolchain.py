"""Thin wrapper around the toolchain: Dialog (dialogc, dgdebug) and the Å-machine
(aambundle, aamshow).

Each repo is built in its own src/ directory, and the resulting tools are copied
into prototype/bin/, which is where they're checked for and run from.

On Windows the binaries are built and run inside WSL. Commands go through wsl.exe
from the directory they need (a repo's src/, or bin/), which WSL maps to its
/mnt/<drive>/... path, so no Windows paths are passed to them. Elsewhere the
binaries are run directly. Cloning uses the host's own git on every platform.
"""

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]  # prototype/
PRIOR_ART = REPO_ROOT.parent / "_references"
BIN = REPO_ROOT / "bin"
USE_WSL = sys.platform == "win32"


@dataclass(frozen=True)
class Repo:
    """A source repo that provides part of the toolchain."""

    name: str
    url: str
    env: str  # environment variable that overrides the checkout location
    tools: tuple[str, ...]  # executables the build leaves in src/, copied to bin/
    targets: tuple[str, ...] = ()  # make targets; empty means the default target
    files: dict[str, str] = field(default_factory=dict)  # name -> path in checkout
    prereqs: tuple[str, ...] = ()  # programs the build needs beyond make and cc

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
    targets=("dialogc", "dgdebug"),
    files={"stdlib": "stdlib.dg"},
)

AAMACHINE = Repo(
    name="aamachine",
    url="https://github.com/Dialog-IF/aamachine.git",
    env="QUIDDITY_AAMACHINE_DIR",
    tools=("aambundle", "aamshow"),
    # aambundle embeds 6502 code, which needs these cross-assemblers.
    prereqs=("xa", "acme"),
)

REPOS = (DIALOG, AAMACHINE)
STDLIB = DIALOG.dir / DIALOG.files["stdlib"]


def _runnable(p: Path) -> bool:
    return p.is_file() and os.access(p, os.X_OK)


def _run(
    cmd: list[str], cwd: Path | None = None, capture: bool = False
) -> subprocess.CompletedProcess[str]:
    if USE_WSL:
        cmd = ["wsl", *cmd]

    return subprocess.run(cmd, cwd=cwd, capture_output=capture, text=True, check=False)


def _has_program(name: str) -> bool:
    try:
        return _run(["which", name], capture=True).returncode == 0
    except OSError:
        return False


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


def _status(repo: Repo) -> RepoStatus:
    tools = {t: _runnable(BIN / t) for t in repo.tools}

    # Prerequisites only matter if make still has to produce the tools. When
    # they're already built in src/, "quiddity build" just copies them.
    built = all(tools.values()) or all(_runnable(repo.src / t) for t in repo.tools)
    missing = [] if built else [p for p in repo.prereqs if not _has_program(p)]

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
            f"Building {repo.name} needs {' and '.join(missing)} installed{where}."
        )

    cmd = ["make", *repo.targets]

    try:
        rc = _run(cmd, cwd=repo.src).returncode
    except FileNotFoundError:
        missing_cmd = "wsl" if USE_WSL else "make"
        raise BuildError(
            f"{missing_cmd} not found. Install it and try again."
        ) from None

    # Inside WSL, a missing make comes back as the shell's "command not found".
    if USE_WSL and rc == 127:
        raise BuildError("make not found inside WSL. Install build-essential there.")

    return rc


def install(repo: Repo) -> None:
    BIN.mkdir(exist_ok=True)

    for t in repo.tools:
        built = repo.src / t

        if not built.is_file():
            raise BuildError(f"The {repo.name} build didn't produce {t}.")

        # copy2 keeps the executable bit.
        shutil.copy2(built, BIN / t)


def version() -> str | None:
    try:
        r = _run(["./dialogc", "--version"], cwd=BIN, capture=True)
    except OSError:
        return None

    # dialogc prints its version to stderr.
    lines = (r.stdout + r.stderr).strip().splitlines()

    return lines[0] if r.returncode == 0 and lines else None


def check() -> ToolStatus:
    repos = [_status(r) for r in REPOS]
    dialogc = _runnable(BIN / "dialogc")

    return ToolStatus(repos=repos, version=version() if dialogc else None)
