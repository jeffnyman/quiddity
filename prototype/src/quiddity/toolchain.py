"""Thin wrapper around the Dialog toolchain (dialogc, dgdebug).

On Windows the binaries are built and run inside WSL. Commands go through wsl.exe
from the Dialog src/ directory, which WSL maps to its /mnt/<drive>/... path, so no
Windows paths are passed to them. Elsewhere the binaries are run directly.
"""

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]  # prototype/
PRIOR_ART = REPO_ROOT.parent / "_references"
DIALOG_DIR = Path(os.environ.get("QUIDDITY_DIALOG_DIR", PRIOR_ART / "dialog"))
SRC = DIALOG_DIR / "src"
STDLIB = DIALOG_DIR / "stdlib.dg"
USE_WSL = sys.platform == "win32"

# name -> path; tool names double as make targets
TOOLS = {"dialogc": SRC / "dialogc", "dgdebug": SRC / "dgdebug"}
FILES = {"stdlib": STDLIB}


def _runnable(p: Path) -> bool:
    return p.is_file() and os.access(p, os.X_OK)


@dataclass
class ToolStatus:
    """Status of the Dialog toolchain."""

    dialog_dir: bool
    tools: dict[str, bool]
    files: dict[str, bool]
    version: str | None

    @property
    def ok(self) -> bool:
        """Return True if the toolchain is usable."""
        return self.dialog_dir and all(self.tools.values()) and all(self.files.values())


class BuildError(Exception):
    pass


def build() -> int:
    if not SRC.is_dir():
        raise BuildError(
            f"Dialog source not found at {SRC}. Clone it or set QUIDDITY_DIALOG_DIR."
        )

    cmd = ["make", *TOOLS]

    if USE_WSL:
        cmd = ["wsl", *cmd]

    try:
        rc = subprocess.run(cmd, cwd=SRC, check=False).returncode
    except FileNotFoundError:
        raise BuildError(f"{cmd[0]} not found. Install it and try again.") from None

    # Inside WSL, a missing make comes back as the shell's "command not found".
    if USE_WSL and rc == 127:
        raise BuildError("make not found inside WSL. Install build-essential there.")

    return rc


def version() -> str | None:
    cmd = ["./dialogc", "--version"]

    if USE_WSL:
        cmd = ["wsl", *cmd]

    try:
        r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True, check=False)
    except OSError:
        return None

    # dialogc prints its version to stderr.
    lines = (r.stdout + r.stderr).strip().splitlines()

    return lines[0] if r.returncode == 0 and lines else None


def check() -> ToolStatus:
    tools = {n: _runnable(p) for n, p in TOOLS.items()}

    return ToolStatus(
        dialog_dir=DIALOG_DIR.is_dir(),
        tools=tools,
        files={n: p.is_file() for n, p in FILES.items()},
        version=version() if tools["dialogc"] else None,
    )
