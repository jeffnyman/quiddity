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
