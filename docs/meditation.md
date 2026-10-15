# Received According to the Receiver

*A small meditation on a scholastic axiom and what it can teach someone building an interactive fiction interpreter.*

---

## The Axiom

> **Quidquid recipitur ad modum recipientis recipitur.**\
> *Whatever is received is received according to the mode of the receiver.*

This is an old principle from medieval scholastic philosophy. Thomas Aquinas uses it in several places, including the *Summa Theologiae* (I, q. 75, a. 5). The idea is simple once you hear it:

- Water poured into a square jar takes a square shape. Poured into a round jar, it takes a round one. The water is the same; the jar decides the shape.
- A sermon heard by a child and a sermon heard by a theologian are the same words, but they are not received the same way.
- Sunlight through stained glass comes out colored. The light didn't change its nature, but the glass gave it a particular manifestation.

The thing received is real and has its own nature. But *how* it shows up is bounded, shaped, and sometimes limited by whatever receives it.

This turns out to be an unusually good lens for thinking about virtual machines.

---

## The Setup: Story Files and Interpreters

Classic interactive fiction (*Zork* and its descendants) runs on virtual machines. The two most common are:

- **The Z-Machine**, created by Infocom in 1979.
- **Glulx**, a more modern 32-bit successor designed by Andrew Plotkin.

A game is compiled into a **story file**. That story file doesn't run on its own. It needs an **interpreter**, a program that implements the virtual machine and actually executes the game.

So in the language of the axiom:

| Role | In the axiom | In interactive fiction |
|---|---|---|
| What is received | The form | The story file |
| The receiver | The vessel | The interpreter |
| The mode | How the vessel can hold things | What the interpreter supports |

The same story file run in a bare terminal, a polished desktop app, a web browser, and a screen reader will be *one game* manifesting in *four modes*.

---

## The Spec Already Knows This

The axiom isn't something we're forcing onto these machines. They were designed with it.

### The Z-Machine: the receiver declares its mode

When an interpreter loads a Z-Machine story file, it writes information about itself **into the story file's header**. That information includes:

- whether colors are available
- whether bold, italic, and fixed-width fonts are available
- whether timed input is supported
- the screen's width and height
- the interpreter's own number and version

A well-written game reads these values and adapts. In effect, the receiver announces its mode, and the form adjusts its own manifestation accordingly.

### Glulx: the receiver is asked directly

Glulx goes further. All input and output is delegated to a separate layer called **Glk**. The game asks the interpreter questions through *gestalt* calls:

> "Do you support graphics? Hyperlinks? Sound? Unicode?"

Glk was designed from the start around the expectation that one story file would be received by very different hosts. It's the axiom turned into an API.

---

## Substance and Accidents

Here's where the scholastic toolkit earns its keep. The axiom says the receiver shapes what is received. But it does **not** say the receiver gets to change *everything*.

Thomist philosophy distinguishes between:

- **Substance**: what a thing fundamentally *is*.
- **Accidents**: properties a thing *has* that could vary without it becoming something else (color, size, position).

An interpreter author can draw exactly this line.

### Substance: must be received identically

These belong to the game itself. Every correct interpreter must agree on them exactly:

- what each opcode does
- 16-bit arithmetic (including overflow behavior)
- the object tree and how objects move within it
- branching and control flow
- save and restore state
- the random number generator when the game asks for predictable mode

If two interpreters disagree here, the receiver has no legitimate "mode" to appeal to. **One of them is simply wrong.**

### Accidents: may vary by receiver

These belong to the presentation. The spec deliberately leaves them to the interpreter:

- fonts and typefaces
- colors
- window layout
- how the status line looks
- whether sound actually plays
- how text wraps

A good interpreter **preserves the substance exactly while letting the accidents vary**.

> *A curious aside for the theologically inclined:* this is the reverse of Aquinas's account of the Eucharist, in which the accidents remain while the substance changes. In an interpreter, a bug that changes the substance while leaving the accidents looking normal is exactly the kind of failure you most need to catch.

---

## The Axiom Runs Backward Through History

Receivers don't only receive. Over time, they shape the forms themselves.

Infocom's games were written against Infocom's own interpreters, quirks and all. Some later games came to depend on how particular interpreters happened to behave. The **Z-Machine Standards Document** (and its later 1.1 revision) exists partly to separate *what the machine is* from *what one particular receiver happened to do*.

The result is a strange inheritance: some modern interpreters have to deliberately emulate old quirks in order to receive old games faithfully. The mode of past receivers became part of the form.

---

## What This Means When You Build One

If you're writing your own Z-Machine or Glulx interpreter, the axiom suggests a few principles.

### 1. Describe yourself honestly

The header flags and gestalt responses are your interpreter's self-description. Advertise only what you actually support. If you claim a capability you don't have, the game will make choices based on a mode you don't possess, and the result will be received badly.

### 2. Test along the substance/accident line

When you write acceptance tests against real games:

- **Assert on substance:** game state, object locations, variable values, the *content* of the transcript.
- **Don't assert on accidents:** exact layout, styling, or how the screen happens to be drawn.

This way, a change to your screen model can't masquerade as a logic regression, and a logic bug can't hide behind "well, it just looks different."

### 3. Architect along the same line

Structure the interpreter the way the axiom structures reality:

```
┌──────────────────────────────────────┐
│   Story file       (what is received)│
└──────────────────┬───────────────────┘
                   │
┌──────────────────▼───────────────────┐
│   VM core          (substance)       │
│   pure, deterministic, identical     │
│   across every frontend              │
└──────────────────┬───────────────────┘
                   │
┌──────────────────▼───────────────────┐
│   I/O layer         (accidents)      │
│   terminal │ TUI │ GUI │ web │ etc.  │
└──────────────────────────────────────┘
```

Glk already models this split for Glulx. You can impose the same discipline on your Z-Machine side. Then adding a new frontend changes only the **mode of reception**, never **what is received**.

---

## A Closing Thought

Philosophical axioms can feel distant from code. But good design principles often turn out to be very old ideas wearing new clothes. A medieval friar thinking about how the intellect receives knowledge and a programmer thinking about how a terminal receives a story file are, in a real sense, asking the same question:

> *What belongs to the thing itself, and what belongs to the one receiving it?*

Get that distinction right and an interpreter becomes clearer to build, easier to test, and more faithful to the games it exists to serve.
