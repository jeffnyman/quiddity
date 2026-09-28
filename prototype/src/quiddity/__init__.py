"""Quiddity: a prototype interactive fiction language that compiles to Dialog."""

import argparse

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

    a = p.parse_args(argv)

    return a.fn(a)
