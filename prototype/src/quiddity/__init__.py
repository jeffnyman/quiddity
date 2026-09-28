"""Quiddity: a prototype interactive fiction language that compiles to Dialog."""

import argparse

from . import toolchain


def cmd_check(_: argparse.Namespace) -> int:
    """Check that the Dialog toolchain is reachable."""

    s = toolchain.check()
    print(f"dialog dir : {toolchain.DIALOG_DIR}")

    for name, found in {**s.tools, **s.files}.items():
        print(f"{name:<11}: {'ok' if found else 'MISSING'}")

    if s.tools["dialogc"]:
        print(f"version    : {s.version or 'unknown'}")

    print(f"via WSL    : {toolchain.USE_WSL}")

    if not s.dialog_dir:
        print("Dialog not found. Clone it or set QUIDDITY_DIALOG_DIR.")
    elif not all(s.tools.values()):
        print('\nBuild the tools with "quiddity build".')
    elif not all(s.files.values()):
        print("\nDialog checkout looks incomplete (missing library files).")

    return 0 if s.ok else 1


def cmd_build(a: argparse.Namespace) -> int:
    """Build the Dialog tools from source."""

    try:
        rc = toolchain.build()
    except toolchain.BuildError as e:
        print(e)
        return 1

    if rc != 0:
        print(f"\nBuild failed (make exited with {rc}).")
        return rc

    print()
    return cmd_check(a)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="quiddity")

    sub = p.add_subparsers(
        dest="cmd", required=True, title="command", metavar="<command>"
    )
    sub.add_parser(
        "check", help="verify the Dialog toolchain is reachable"
    ).set_defaults(fn=cmd_check)
    sub.add_parser("build", help="build the Dialog tools from source").set_defaults(
        fn=cmd_build
    )

    a = p.parse_args(argv)

    return a.fn(a)
