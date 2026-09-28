# Quiddity Prototype

A prototype compiler for a new interactive fiction language. It emits Dialog source and lets Dialog's toolchain do the rest (Z-Machine and Å-machine backends, parser, standard library).

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

Compile Dialog sources to a story file:

```
uv run quiddity compile tests/fixtures/chest.dg -t aa
```

This runs `bin/dialogc` on the given sources, with Dialog's standard library added after them, and writes `tests/fixtures/chest.aastory`. You can pass several source files; the output is named after the first one unless you give `-o`.

| Option | Meaning |
|---|---|
| `-t`, `--format` | Output format: `aa` (default), `z8`, `z5`, or `zblorb` |
| `-o`, `--output` | Output file path. Defaults to the first source with the format's extension (`.aastory`, `.z8`, `.z5`, `.zblorb`) |

`dialogc`'s own messages, including compile errors with file and line numbers, are printed as-is, and the command exits with `dialogc`'s status. The `zblorb` format needs the story to declare an IFID with `(story ifid)`; `dialogc` says so and suggests one if it's missing. Story files are gitignored, so compiled output next to a source file won't show up in `git status`.

Run Dialog sources with a scripted set of player commands and print the transcript:

```
uv run quiddity run tests/fixtures/chest.dg -i tests/fixtures/walkthrough.in
```

This plays the story in `bin/dgdebug` (with the standard library added after your sources, and no compile step), feeding it one command per line from the `-i` file. Without `-i`, the commands are read from standard input, so this does the same thing:

```
uv run quiddity run tests/fixtures/chest.dg < tests/fixtures/walkthrough.in
```

The transcript is cleaned up so it's easy to read and to compare between runs: `dgdebug`'s line markers and empty prompt lines are removed, and runs of blank lines are collapsed into one. To keep a transcript, redirect it to a file:

```
uv run quiddity run tests/fixtures/chest.dg -i tests/fixtures/walkthrough.in > chest.txt
```

If the sources have an error, `dgdebug`'s message (with file and line number) is printed instead and the command exits with a nonzero status.

### Playing

There are a few ways to play a story, depending on what you have and where you want to play it. On Windows the tools are `bin\dgdebug.exe` and `bin\aambundle.exe`.

**From source, with the debugger.** `dgdebug` runs Dialog sources directly, with no compile step, so it's the quickest loop while you're working on a story. Pass the standard library last:

```
bin/dgdebug tests/fixtures/chest.dg ../_references/dialog/stdlib.dg
```

**In the terminal, with Node.** An `.aastory` file plays with the Node.js frontend that ships in the aamachine checkout. It needs [Node.js](https://nodejs.org/) but nothing else:

```
node ../_references/aamachine/src/js/nodefrontend.js tests/fixtures/chest.aastory
```

**In a browser.** `aambundle` packages an `.aastory` into a folder with a web interpreter. Open `play.html` from that folder in a browser; the same folder is what you'd put on a website to publish the story. `aambundle` creates the output folder but not its parents, and `build/` is gitignored, so:

```
mkdir build
bin/aambundle -o build/chest-web tests/fixtures/chest.aastory
```

**In a Z-machine interpreter.** A story compiled with `-t z8` or `-t z5` plays in any Z-machine interpreter, such as [Frotz](https://davidgriffith.gitlab.io/frotz/) or my own [Rezrov](https://github.com/jeffnyman/rezrov) or [Voxam](https://github.com/jeffnyman/voxam). An example of execution, assuming frotz is installed:

```
uv run quiddity compile tests/fixtures/chest.dg -t z8
frotz tests/fixtures/chest.z8
```

### Testing

```
uv run pytest
```

The smoke tests in `tests/test_smoke.py` exercise the whole Dialog pipeline. They compile `tests/fixtures/chest.dg` to `.aastory` and `.z8`, then play `walkthrough.in` in `dgdebug` and compare the transcript against the saved gold file, `tests/fixtures/chest.gold`. If the story's output changes, the test fails with a diff showing what changed. These tests need Dialog's tools in `bin/`, and they're skipped with a reminder to run `quiddity build` if those aren't there.

The walkthrough also loads `tests/fixtures/no-banner.dg`, which replaces the standard library's banner so the transcript doesn't include version strings that change whenever Dialog is updated.

When the output changes on purpose, regenerate the gold file from the current output:

```
uv run pytest --update-gold
```

A gold file that doesn't exist yet is written the same way on the first run. Either way, that test is skipped for the run that writes it. Look over the diff of `chest.gold` before committing it, since the new file becomes what later runs are compared against.

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
uv run --directory prototype quiddity compile tests/fixtures/chest.dg -t aa
uv run --directory prototype quiddity run tests/fixtures/chest.dg -i tests/fixtures/walkthrough.in
uv run --directory prototype pytest
uv run --directory prototype pytest --update-gold
uv run --directory prototype ruff check
uv run --directory prototype ty check
```

`--directory` behaves as if you'd changed into `prototype/` first, so pytest's `testpaths` setting and any relative paths work the same way.
