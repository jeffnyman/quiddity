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

### Running

```
uv run quiddity
```

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
uv run --directory prototype pytest
uv run --directory prototype ruff check
uv run --directory prototype ty check
```

`--directory` behaves as if you'd changed into `prototype/` first, so pytest's `testpaths` setting and any relative paths work the same way.
