<h1 align="center">
Quiddity
</h1>

<div align="center">
<p><strong>A Specification-Accurate Z-Machine / Glulx Interpreter</strong><br>
<em>Aiming for Early and Late Infocom + Modern Inform</em></p>
</div>

---

<blockquote>
<em>You are standing in a repository. There is a README here.</em>
<br><br>
&gt; <strong>read the README</strong>
</blockquote><br>

And thus begins my foray into learning how to emulate the Z-Machine and Glulx. What does that mean, you ask? Well, let's head down a maze of twisty little passages.

---

<blockquote>
<em>You are standing in an open field west of a white house, with a boarded front door. You could circle the house to the north or south. There is a small mailbox here.</em>
<br><br>
&gt; <strong>open the mailbox</strong>
<br><br>
<em>Opening the small mailbox reveals a leaflet.</em>
<br><br>
&gt; <strong>read the leaflet</strong>
<br><br>
<em>"WELCOME TO ZORK, a game of adventure, danger, and low cunning. No computer should be without one!"</em>
</blockquote><br>

That is the beginning of [_Zork_](https://en.wikipedia.org/wiki/Zork), a text adventure game from the 1980s by a company called [Infocom](https://en.wikipedia.org/wiki/Infocom). It's really what started off the whole Z-Machine business and thus, indirectly, started off my interest in creating an interpreter for it.

---

The goal of the Quiddity project is to provide a working emulator of two virtual machines used for interactive fiction: the [Z-Machine](https://en.wikipedia.org/wiki/Z-machine), which runs [z-code](http://fileformats.archiveteam.org/wiki/Z-code) programs, and [Glulx](https://www.eblong.com/zarf/glulx/), its larger 32-bit successor. The interactive fiction community calls such an emulator an *interpreter*.

Quiddity strives to be specification _accurate_ rather than specification _complete_. In practice, that means:

- **The substance is non-negotiable.** For every version Quiddity supports, the execution model (opcodes, arithmetic, the object tree, and save/restore) should behave exactly as the specification requires.
- **The presentation is selective.** Features like graphics, sound, and certain display capabilities may be partially implemented or absent. Quiddity reports what it supports honestly, through the story file header for the Z-Machine and through Glk gestalt responses for Glulx, as each specification intends.

A reference implementation is the authoritative standard others are measured against, and that role demands completeness. Quiddity's aims are more modest. The project is pedagogical: it exists to understand how such an emulator is built and, through building it, to understand the Z-Machine and Glulx.

## Building

Quiddity is written in C11 and builds with CMake 3.25 or later under GCC, Clang, or MSVC. Warnings are treated as errors on every compiler. The presets in `CMakePresets.json` cover the supported toolchains, and each preset is enabled only on the platform it is for. To see which apply where you are:

```
cmake --list-presets
```

On Windows, the presets are `msvc` and `clang-cl`. Both use the Visual Studio generator, so they work from any shell once `cmake` is on the PATH; no developer command prompt is needed:

```
cmake --preset msvc
cmake --build --preset msvc
ctest --preset msvc
```

On Linux and macOS, including WSL, the presets are `gcc`, `clang`, `asan`, and `release`:

```
cmake --preset gcc
cmake --build --preset gcc
ctest --preset gcc
```

The `asan` preset builds with AddressSanitizer and UndefinedBehaviorSanitizer and is the one to run tests under when something is suspected of misbehaving.

Every build puts its executables under `build/<preset>/bin`. The Visual Studio generator adds a configuration folder, so after a `msvc` build the interpreter is at `build/msvc/bin/Debug/quiddity.exe`; after a `gcc` build it is at `build/gcc/bin/quiddity`. Run it from there:

```
build/gcc/bin/quiddity path/to/story.z3
```

The story runs on the terminal until it quits, reading commands from standard input. When standard input is a file rather than a keyboard, each command is echoed after the prompt so the transcript reads as a session would, and the game ends when the file does. A game that keeps time while waiting for input, as _Border Zone_ does, gets its timer: the clock runs until the first key of a command is pressed and stops while the command is being typed, since the terminal's own line editor has the keyboard then. The terminal is treated as a teletype: text is word-wrapped at 80 columns, the Version 1 to 3 status line is printed above each prompt, and whatever a later game draws in its upper window (a status line, a menu, a map) is printed as a block whenever it changes and the game stops for input. Text styles and colours are accepted and ignored. Every opcode of Versions 1 to 8 is implemented; a story that does something the Standard forbids stops with a message saying what.

A Version 6 story (_Zork Zero_, _Shogun_, _Arthur_, _Journey_, and the modern games written for that Version) treats the screen as eight overlapping windows measured in units. Quiddity lays the text out itself, in units of one character, and the terminal keeps the whole screen as a grid of 80 by 24 cells: the rows that have changed are printed whenever the game stops for input, and a line that scrolls out of a window before it has been seen is printed as it goes, so nothing is lost. Pictures are drawn as boxes with their numbers in them, sized as if the picture file's standard window filled the screen. The pictures themselves come from a Blorb resource file: either the story file is one (a `.zblorb`), or a file with the story's name and the extension `.blb`, `.blorb`, or `.zblorb` lies beside it, as the Infocom picture collections do; a resource file for a different story is reported and ignored. Without one the game is told there are no pictures.

The `--transcript <file>` option names a file for the game's transcript (output stream 2), which a game turns on with its own command, usually `script`; `--record <file>` likewise names a file for the commands typed (output stream 4), which Inform games turn on with `recording on`. Without the option, a game asking for the stream is told it is not available.

Saved games are written in the Quetzal format, so a game saved by Quiddity can be restored by Frotz or any other Quetzal interpreter, and theirs by Quiddity. When a game saves or restores, Quiddity asks for a file name, offering the story file's name with the extension `.sav` the first time and the name last used after that; it will not write over an existing file without a yes. Undo is kept in memory, 32 turns deep, for the games that offer an `undo` command. The Version 5 forms of `save` and `restore` that copy a table of bytes to a file of the game's own write that file in the current directory, under the game's name for it reduced to letters and digits with the extension `.AUX`.

The `--seed <number>` option seeds the game's random numbers for the session. The game is not told and sees ordinary dice, only the same dice every time the number is given, so that a play can be repeated exactly. The generator is fixed by the interpreter itself rather than by any library, and it is the same one Rezrov uses, so a seed means the same play in both.

The `--info` option prints what the story file's header says about it, including its Version, release, serial code, memory map, and whether its checksum verifies, and then exits without running it.

In VS Code with the CMake Tools extension, pick a configure preset when prompted, then use the build, run, and debug buttons on the status bar; the tracked `.vscode` files wire F5 to the `quiddity` target.

## Testing

Tests are run by CTest, which the test presets point at the right build directory:

```
ctest --preset msvc        # or gcc, clang, asan on Linux
```

This prints one line per test and shows a test's output only when it fails. Add `-V` to see every test's full output, including the per-check summary, on every run. To run a single test by name, use `-R`:

```
ctest --preset msvc -R test_check
```

Each test is also an ordinary executable in the same `bin` folder as the interpreter, so it can be run directly or under a debugger. The tests use the small harness in `tests/check.h`; a new test is one C file plus one line in `tests/CMakeLists.txt`. In VS Code, CMake Tools adds a test button to the status bar and lists the tests in the Testing sidebar.

## License

**Quiddity**, An Interactive Fiction Interpreter\
Copyright (C) 2026 [Jeff Nyman][owner]

[![GNU General Public License version 3](assets/gplv3-127x51.png)][GNU General Public License]

> This program is free software: you can redistribute it and/or modify
> it under the terms of the [GNU General Public License] as published by
> the Free Software Foundation, either version 3 of the License, or
> (at your option) any later version.
>
> This program is distributed in the hope that it will be useful,
> but WITHOUT ANY WARRANTY; without even the implied warranty of
> MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
> [GNU General Public License] for more details.

[owner]: https://github.com/jeffnyman
[GNU General Public License]: https://www.gnu.org/licenses/gpl-3.0.en.html
