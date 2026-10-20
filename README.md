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

Every preset but `asan` also builds `quiddity-sdl`, the windowed front end, which is drawn with [SDL3](https://www.libsdl.org/). CMake downloads pinned releases of SDL3, SDL_ttf, SDL_image, and FreeType when a preset is first configured, checks each against its hash, and builds them into the program, so nothing need be installed beforehand and the result is one executable with no libraries beside it. On Windows the C runtime is built in as well. On Linux, SDL looks for X11, Wayland, and the sound servers when the program runs, but can only do that if their development headers were present when it was configured; [SDL's notes on Linux](https://wiki.libsdl.org/SDL3/README-linux) list the packages. To build without the front end, and so without the download, set `QUIDDITY_SDL` to `OFF`; to build it on the system's own SDL3 libraries instead of the pinned ones, set `QUIDDITY_SYSTEM_SDL` to `ON`.

The windowed front end plays a story in a window of its own, given the story file as the terminal is, or asking for one with the system's file dialog when it is started without, as it is when opened from the desktop:

```
build/gcc/bin/quiddity-sdl path/to/story.z5
```

It is at an early stage, and for now plays Versions 1 to 5 as the Standard lays out their screen: the status line and the upper window above, the lower window scrolling beneath them and pausing with `[MORE]` before it scrolls away anything not yet read, all in one fixed-pitch font on a screen of 80 by 24 characters. A command is typed on a line of its own, edited with the cursor keys, Home, End, and Escape, and earlier commands come back with the up and down arrows. A single key is read as it is pressed; timed input runs while the game waits, and a command half-typed when the time runs out is taken up where it was left. Saved games and the files a game asks the player to name are chosen with the system's dialogs. Reverse video is shown; bold, italic, colours, pictures, and sound are not yet, and the game is told so. A Version 6 story gets its text in the order it was printed, as on a teletype. The `--seed` option works as it does for the terminal. The font, Noto Sans Mono, is built into the program; it is under the SIL Open Font License, whose text is in `assets/fonts`.

Every build puts its executables under `build/<preset>/bin`. The Visual Studio generator adds a configuration folder, so after a `msvc` build the interpreter is at `build/msvc/bin/Debug/quiddity.exe`; after a `gcc` build it is at `build/gcc/bin/quiddity`. Run it from there:

```
build/gcc/bin/quiddity path/to/story.z3
```

The story runs on the terminal until it quits, reading commands from standard input. When standard input is a file rather than a keyboard, each command is echoed after the prompt so the transcript reads as a session would, and the game ends when the file does. A game that keeps time while waiting for input, as _Border Zone_ does, gets its timer: the clock runs until the first key of a command is pressed and stops while the command is being typed, since the terminal's own line editor has the keyboard then. A single key a game asks for is read as it is pressed, without a return after it; Escape, the cursor keys, and backspace reach the game by their ZSCII codes. The terminal is treated as a teletype: text is word-wrapped at 80 columns, the Version 1 to 3 status line is printed above each prompt, and whatever a later game draws in its upper window (a status line, a menu, a map) is printed as a block whenever it changes and the game stops for input. Text styles and colours are accepted and ignored. Every opcode of Versions 1 to 8 is implemented; a story that does something the Standard forbids stops with a message saying what.

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

### Acceptance scripts

The unit tests check the machine piece by piece; the acceptance scripts check that whole games play as they did. A script is a small text file under `acceptance/` naming a story, a seed, and the commands to type, and beside it lies a recording of the play, with the extension `.expected`, that the runner made the first time and compares against ever after:

```
build/gcc/bin/quiddity --accept acceptance/cutthroats-r23-s840809.accept --games ../entharion
```

The play is shown as it happens, and the verdict comes at the end: passed, or the first line that differs and the line of the script that was being played. When a change in the play is meant, `--update` records it afresh; while a script is being written, `--resume` plays it and then hands the game over at the keyboard, so that the next commands can be tried before they go into the file. What is typed there is kept at the end of the script under a fence labelled `tried`, which the runner never plays: move the lines that worked above the fence, delete the rest, and the next run plays them for real. The seed is what makes this work: the game's dice come from the interpreter's own generator, which is pinned by its source and shared with Rezrov, so the same seed rolls the same dice in every build and a script written for either interpreter plays on the other.

A new script begins with `--create`, which writes the file naming the story given by `--game`, seeded with 1 and with no commands yet, and then hands the game over at the keyboard as `--resume` would, with the session kept under the same `tried` fence:

```
build/gcc/bin/quiddity --accept --create acceptance/checkers/cloak-plus.accept --game zcode-checkers/cloak-plus.z5 --games ../entharion
```

The story is named as it will be looked up, relative to the games directory, and the file is written with LF line endings on every platform. A script that exists already is left alone, and a story that cannot be found leaves no file behind.

The play is recorded through the plainest front end there is, a stream of text in which the lower window is everything: no wrapping, no status line, and no upper window unless the script says `! UPPER=yes`, when whatever the game has drawn there is written into the stream as a block of rows each time it pauses for input with the window changed. The game is told as much through its header. That keeps a recording about what the game said rather than how a screen arranged it. A script's directives (`GAME`, `SEED`, `BLORB`, `INTERPRETER`, `TANDY`, `UPPER`, `HEIGHT`, `PICTURES`) and its command format are described in `src/quiddity/script.h`; a line beginning with `!` is a directive, `#` a comment, three backticks fence off a part not to be played, and anything else is typed into the game, with a key that is not a character written as `[code]` or by name, such as `<down>`.

The games themselves are not in the repository. A script names its game relative to itself, and when the file is not there the runner looks under a games directory instead, given by `--games`, by the `QUIDDITY_GAMES` environment variable, or, for the CTest runs, by the CMake cache variable `QUIDDITY_GAMES_DIR`, which defaults to an `entharion` directory beside the repository. Every script under `acceptance/` is a CTest test; one whose game cannot be found is reported as skipped rather than failed, so the suite passes on a machine without the corpus. Scripts may be grouped in subdirectories, and a test is named after the script's path under `acceptance/` with the slashes turned to dots, so `checkers/czech-z3.accept` is the test `accept_checkers.czech-z3` and `ctest --preset gcc -R checkers` runs the whole group.

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
