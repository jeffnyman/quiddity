# Quiddity surface syntax, version 0

Decisions made while writing the first story (`tests/fixtures/chest.qd`). Everything here is provisional, but the compiler will implement exactly this until a decision is revisited.

## Shape of a file

Four block types, all introduced by a header line and continued by indented `property: value` lines. Blank lines and `#` comments are ignored.

| Block | Header | Purpose |
| --- | --- | --- |
| story | `story "Title":` | Metadata, intro text, starting room |
| kind | `kind Name extends Parent` | Single-inheritance type declaration; no body |
| declaration | `The <name> is a <Kind> [in <place>].` | Creates one object |
| rule | `rule <id>:` | A `when` / `where` / `then` rule |

Actions (`action open (C: Container, A: Actor):` with `requires` and `effect`) and traits with bodies are in the spec but not yet needed by a test story, so they are not in version 0.

## Declarations

- The header is a fixed template, not natural language. Only these shapes parse: `The X is a K.` and `The X is a K in Y.` (`in` may be `on` later).
- The name after `The` is both the display name and the identifier. The identifier is the name lowercased with spaces turned to hyphens (`The treasure chest` becomes `treasure-chest`). References to objects elsewhere always use the same prose form, `the treasure chest`.
- Properties are one per line. Values are a quoted string, `yes`/`no`, a reference (`the hallway`), a comma-separated list of references, or a capitalised kind/trait name list (`traits: Lockable, Openable`).
- Direction properties (`north:`, `south:`, ...) on a Room declare exits.
- Consistency rules the compiler enforces: `locked: yes` implies closed, and `locked: yes` on something without the Lockable trait is an error.

## Rules

```
rule <id>:
  when: the player tries to <action> (<Var>: <Kind>)
  where: <condition> [and <condition>]...
  then: <statement>
        <statement>
```

- `(C: Kind)` binds a logic variable to any object of that kind, walking the single-parent kind chain.
- `tries to` is the pre-action stage: the rule runs before the action's own requirements are checked. Other stages (`after`, and the action's own `requires`) will be added when a story needs them.
- Statements in version 0: `tell "..."` and `stop`. `stop` cancels the action.
- Dispatch order is computed structurally (more specific kind wins) and emitted as Dialog source order; ties fall back to file order with a warning.

## Mapping to Dialog

- Kinds and traits both become Dialog trait predicates, e.g. `(cursedchest $)`, with parent kinds emitted as implication rules.
- Declarations become `#identifier` object blocks with `(name *)`, `(descr *)`, `(* is #in ...)` and the stdlib flags (`(container *)`, `(lockable *)`, ...).
- `rule ... tries to open` becomes `(prevent [open $C])` guarded by the kind predicate. Rules are emitted before the standard library so they are tried first, in computed specificity order.
