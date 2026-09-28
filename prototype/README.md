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

### Toolchain

Quiddity compiles to [Dialog](https://github.com/Dialog-IF/dialog), and uses the [Å-machine](https://github.com/Dialog-IF/aamachine) tools to package stories. Both are built from source, and the resulting tools end up in `bin/` (which is gitignored):

| Tool | From | What it does |
|---|---|---|
| `dialogc` | Dialog | The Dialog compiler |
| `dgdebug` | Dialog | The Dialog interactive debugger |
| `aambundle` | aamachine | Bundles an `.aastory` into a web player or disk images |
| `aamshow` | aamachine | Inspects `.aastory` files |

You don't need to clone anything by hand. `quiddity build` clones whichever repo is missing into `_references/` at the repository root. To use an existing checkout somewhere else, set `QUIDDITY_DIALOG_DIR` or `QUIDDITY_AAMACHINE_DIR` to its path.

#### Build prerequisites

Building needs `git`, `make`, and a C compiler, plus the `xa` and `acme` 6502 assemblers that aamachine uses.

On macOS (`make` and the compiler come with the Xcode Command Line Tools):

```
xcode-select --install
brew install xa acme
```

On Linux (Debian/Ubuntu):

```
sudo apt install build-essential xa65 acme
```

On Windows, the build runs inside [WSL](https://learn.microsoft.com/windows/wsl/install), which cross-compiles native `.exe` tools with MinGW. Inside WSL, install:

```
sudo apt install build-essential xa65 acme gcc-mingw-w64-i686
```

Cloning uses the Windows `git`, so that needs to be installed on the Windows side. The finished `.exe` tools in `bin\` run directly on Windows, without WSL.

### Running

Quiddity is run as `uv run quiddity <command>`. Use `uv run quiddity --help` to list the commands.

Set up the toolchain:

```
uv run quiddity build
```

This clones Dialog and aamachine if they aren't there yet, builds their tools, copies them into `bin/`, and then runs `check` to confirm the result. It's safe to run again: repos that are already cloned aren't cloned over, and the tools are just rebuilt and copied again.

Check that the toolchain is in place:

```
uv run quiddity check
```

This reports, for each repo, whether its tools are in `bin/` and whether required files such as Dialog's standard library (`stdlib.dg`) are present, along with the compiler's version. It exits with a nonzero status if anything is missing, and tells you what to do about it, including any build prerequisites that aren't installed yet.

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
