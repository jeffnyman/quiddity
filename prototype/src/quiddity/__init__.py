"""Quiddity: a prototype interactive fiction language that compiles to Dialog."""

import argparse
import sys
from pathlib import Path

from . import toolchain


def cmd_check(_: argparse.Namespace) -> int:
    """Check that the toolchain is in place."""

    s = toolchain.check()
    hints: list[str] = []

    print(f"{'tools in':<12}: {toolchain.BIN}")

    for r in s.repos:
        name = r.repo.name
        print(f"{name:<12}: {r.repo.dir}")

        for item, found in {**r.tools, **r.files}.items():
            print(f"  {item:<10}: {'ok' if found else 'MISSING'}")

        if r.repo is toolchain.DIALOG and r.tools["dialogc"]:
            print(f"  {'version':<10}: {s.version or 'unknown'}")

        if not r.present:
            hints.append(
                f'{name} not found. "quiddity build" will clone it, '
                f"or set {r.repo.env} to an existing checkout."
            )
        elif not all(r.tools.values()):
            hints.append(f'Build the {name} tools with "quiddity build".')
        elif not all(r.files.values()):
            hints.append(f"{name} checkout looks incomplete (missing files).")

        if r.missing_prereqs:
            hints.append(f"Building {name} needs {', '.join(r.missing_prereqs)}.")

    print(f"{'build in WSL':<12}: {toolchain.USE_WSL}")

    if hints:
        print()
        print("\n".join(hints))

    return 0 if s.ok else 1


def cmd_build(a: argparse.Namespace) -> int:
    """Clone any missing toolchain repos, build their tools, and copy them to bin/."""

    for repo in toolchain.REPOS:
        try:
            if not repo.dir.exists():
                # Flush so these land before git's and make's own output.
                print(f"Cloning {repo.name} into {repo.dir}", flush=True)
                toolchain.clone(repo)

            print(f"Building {repo.name}", flush=True)
            rc = toolchain.make(repo)

            if rc != 0:
                print(f"\nBuilding {repo.name} failed (make exited with {rc}).")
                return rc

            print(f"Copying {', '.join(repo.tools)} into {toolchain.BIN}")
            toolchain.install(repo)
        except toolchain.BuildError as e:
            print(e)
            return 1

    print()
    return cmd_check(a)


def cmd_compile(a: argparse.Namespace) -> int:
    """Compile Dialog sources to a story file."""

    sources = [Path(s) for s in a.sources]
    ext = {"aa": ".aastory", "z8": ".z8", "z5": ".z5", "zblorb": ".zblorb"}[a.format]
    out = Path(a.output) if a.output else sources[0].with_suffix(ext)

    missing = [s for s in sources if not s.is_file()]

    if missing:
        print(f"Source not found: {', '.join(map(str, missing))}")
        return 1

    try:
        rc = toolchain.compile(sources, out, a.format)
    except toolchain.ToolMissing as e:
        print(e)
        return 1

    if rc == 0:
        print(f"Wrote {out}")

    return rc


def cmd_run(a: argparse.Namespace) -> int:
    """Run Dialog sources in dgdebug with scripted input and print the transcript."""

    sources = [Path(s) for s in a.sources]
    missing = [
        p for p in [*sources, *([Path(a.input)] if a.input else [])] if not p.is_file()
    ]

    if missing:
        print(f"File not found: {', '.join(map(str, missing))}")
        return 1

    commands = Path(a.input).read_text() if a.input else sys.stdin.read()

    try:
        print(toolchain.run_transcript(sources, commands), end="")
    except (toolchain.ToolMissing, toolchain.ToolFailed) as e:
        print(e)
        return 1

    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="quiddity")

    sub = p.add_subparsers(
        dest="cmd", required=True, title="command", metavar="<command>"
    )
    sub.add_parser(
        "check", help="verify the Dialog and aamachine tools are in place"
    ).set_defaults(fn=cmd_check)
    sub.add_parser(
        "build", help="clone (if needed) and build the Dialog and aamachine tools"
    ).set_defaults(fn=cmd_build)

    c = sub.add_parser("compile", help="compile Dialog sources to a story file")
    c.add_argument("sources", nargs="+")
    c.add_argument("-t", "--format", default="aa", choices=["aa", "z8", "z5", "zblorb"])
    c.add_argument("-o", "--output")
    c.set_defaults(fn=cmd_compile)

    r = sub.add_parser("run", help="run Dialog sources in dgdebug with scripted input")
    r.add_argument("sources", nargs="+")
    r.add_argument("-i", "--input", help="file of player commands (default: stdin)")
    r.set_defaults(fn=cmd_run)

    a = p.parse_args(argv)

    return a.fn(a)
