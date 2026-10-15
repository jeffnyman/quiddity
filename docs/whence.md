# Why "Quiddity"?

*A short note on the name of this Z-Machine and Glulx interpreter.*

---

The name carries two meanings, one from medieval philosophy and one from modern fiction. Both describe what an interpreter is for.

## 1. The Philosophical Meaning: Guarding the "Whatness"

**Quiddity** comes from the Latin *quidditas*, built from *quid*, meaning "what." Literally, it means *whatness*: the essence of a thing, the answer to the question *"What is it?"*

Scholastic philosophers, Thomas Aquinas among them (notably in *De Ente et Essentia*), used the term for what a thing fundamentally is, as opposed to the incidental features it happens to have.

That distinction sits at the heart of how this interpreter is designed. (See [*Received According to the Receiver*](meditation.md) for the longer version.) A story file has:

- **Substance:** its logic, opcodes, arithmetic, object tree, and state. This is the game's quiddity, and it must be preserved exactly.
- **Accidents:** fonts, colors, layout, and screen presentation. These may legitimately vary from one interpreter to another.

So the name isn't a claim that the interpreter *is* the essence of the game. It's a statement of purpose:

> **The interpreter exists to protect the quiddity of every story file it runs.**

Whatever terminal, window, or screen a game appears in, what it *is* should arrive intact. When there's a choice between looking right and being right, the name points to the answer.

## 2. The Literary Meaning: The Sea of Other Worlds

In Clive Barker's *Books of the Art* (*The Great and Secret Show* and *Everville*), **Quiddity** is a dream-sea lying beneath ordinary reality. Most people touch it only a handful of times in their lives. The Art is the rare power to reach it at will.

A story file is a lot like that. On disk, it's a world sitting latent and unreachable, just bytes. The interpreter is what lets you cross over: it opens the door, and the world wakes up.

## Two Readings, One Job

| Reading | What "Quiddity" means | What the interpreter does |
|---|---|---|
| Scholastic | The essence of a thing | Preserves each game's essence faithfully |
| Barkerian | A sea of other worlds | Opens the way into those worlds |

One meaning is about **fidelity** and the other is about **wonder**. A good interpreter needs both: it should be exact enough to keep a game what it is, and alive enough to let someone step inside it.
