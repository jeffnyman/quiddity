# Quiddity Prototype

A prototype compiler for a new interactive fiction language.

## Development

The prototype is managed with [uv](https://docs.astral.sh/uv/). The commands below assume you're in this `prototype/` directory. To run them from the repository root instead, see [Running from the repository root](#running-from-the-repository-root).

### Setup

Create the virtual environment and install the project with its development dependencies:

```
uv sync
```

`uv run` also syncs automatically, so this step is optional, but it's a quick way to confirm everything installs.

### Dialog toolchain

Quiddity compiles to [Dialog](https://github.com/Dialog-IF/dialog), so it needs a Dialog checkout. By default it looks in `_references/dialog` at the repository root:

```
git clone https://github.com/Dialog-IF/dialog.git ../_references/dialog
```

To use a checkout somewhere else, set `QUIDDITY_DIALOG_DIR` to its path.

Building the Dialog tools needs `make` and a C compiler. On Windows, the tools are built and run inside WSL, so those need to be installed there.

### Running

Quiddity is run as `uv run quiddity <command>`. Use `uv run quiddity --help` to list the commands.

Check that the Dialog toolchain is in place:

```
uv run quiddity check
```

This reports whether `dialogc`, `dgdebug`, and the standard library (`stdlib.dg`) were found, along with the compiler's version. It exits with a nonzero status if anything is missing, and tells you what to do about it.

Build the Dialog tools from source:

```
uv run quiddity build
```

This runs `make` in the checkout's `src/` directory, then runs `check` to confirm the result.

### Testing

```
uv run pytest
```

### Linting and formatting

Check for lint issues:

```
uv run ruff check
```

Apply the fixes Ruff can make automatically:

```
uv run ruff check --fix
```

Format the code:

```
uv run ruff format
```

### Type checking

```
uv run ty check
```

### Running from the repository root

uv looks for `pyproject.toml` in the current directory and its parents, never in subdirectories, so these commands won't find the project from the repository root on their own. Add `--directory prototype` to any of them:

```
uv run --directory prototype quiddity check
uv run --directory prototype pytest
uv run --directory prototype ruff check
uv run --directory prototype ty check
```

`--directory` behaves as if you'd changed into `prototype/` first, so pytest's `testpaths` setting and any relative paths work the same way.
