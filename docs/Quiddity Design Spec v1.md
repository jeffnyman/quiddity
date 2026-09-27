# Interactive Fiction Language Design Spec

Sep 22, 2026 · @Jeff Nyman

## Overview and Motivation

Inform 7 is expressive but its natural-language parsing invites ambiguity, and its rulebook system can make it unclear which rule actually governs a given situation once several rules could plausibly apply. This spec sketches a language that keeps Inform 7's readable declarative surface for describing the world, but borrows a more disciplined execution model underneath from two other traditions:

- **Dialog** (Linus Åkesson's IF language): a Prolog-style unification engine, where rules match against typed logic variables rather than hardcoded instances.
- **PDDL/STRIPS** (the AI-planning tradition): actions modeled as operators with explicit preconditions ("can this happen?") and effects ("what changes as a result?"), kept as separate concerns rather than blurred together the way Inform 7's "Instead" and "Carry out" rules can blur them.

Think of it as a car: Inform 7 supplies the dashboard and controls (what the author writes and reads), Dialog supplies the transmission (how conditions actually get matched and rules dispatched), and PDDL supplies the engineering spec sheet (a forced discipline about what must be true before an action, and what becomes true after).

The language compiles down to the Z-Machine or Glulx. Inform 7 targets both; Dialog targets the Z-Machine (alongside its own Å-machine).

A guiding design principle throughout: the language should make it easy to do the right thing and harder to do the wrong thing, where "right" and "wrong" are defined by the language's own opinions about good IF-authoring practice: a *pit of success*, not just a feature list.

## Layer 1 — World Declarations

The outermost layer keeps Inform 7's genuine strength: readable, near-English declarations for the objects, rooms, and properties that make up the world. This layer compiles down to a typed fact base.

```
kind Room
kind Container extends Thing
kind Key extends Thing

The living room is a Room.
The treasure chest is a Container in the living room.
The treasure chest is locked.
The silver key is a Key.
The treasure chest requires the silver key.
```

**`kind` is single inheritance, deliberately.** Every kind has exactly one parent, forming a strict tree rather than a lattice. `CursedChest extends Container` is allowed; a kind with two parent kinds is not. This is what keeps the specificity ordering used by the rule engine (Layer 3) computable without ambiguity. See the Traits section for why cross-cutting concerns (lockable, flammable, magical) are handled separately instead of by adding parents to `kind`.

Cross-cutting properties that don't belong in the `kind` chain (like lockable, openable, edible) are modeled as **traits**, not as additional base kinds. This mirrors how Inform 7 itself actually works under the hood: "a locked openable container" reads like multiple inheritance in prose, but it's one `kind` (`container`) plus independent trait flags that never enter the type lattice as ancestor nodes. See the Traits section for the full composition model.

## Layer 2 — Actions (PDDL-Style Operators)

Actions are modeled as operators with an explicit **precondition** (what must be true for the action to be attempted) and an explicit **effect** (what becomes true as a result). Keeping these two questions separate fixes a real ambiguity in Inform 7, where a "Carry out" rule can quietly check conditions and an "Instead" rule can quietly cause side effects. The categories leak into each other.

```
action open (C: Container, A: Actor):
  precondition:
    A is in same-room-as C
    C.locked = false
  effect:
    C.state := open
    tell A "You open " + C.name + "."
```

A locked container simply never satisfies `open`'s precondition. No separate rule is needed to intercept and block the attempt. The refusal message (why the action failed) is handled as a different concern, in Layer 3.

**Effects are absolute, not suggestive.** An effect assigns a fact; it does not merely nudge the world toward a state. This closed-world, atomic-write discipline (borrowed directly from STRIPS) prevents the classic bug where two rules fire on the same event and leave the world contradictory: a chest simultaneously open and locked. Every action produces exactly one consistent write, which doubles as a clean audit trail for debugging and for acceptance testing against reference games.

## Layer 3 — Rules (Dialog-Style Unification)

Actions-with-preconditions (Layer 2) handle "can this happen." A separate rule layer handles everything reactive and pattern-matched: refusal messages, ambient description, chained inferences ("if the room is dark and the player has no light source, describe darkness instead").

Rules use a `when` / `where` / `then` shape. Critically, the variables in a rule (`C`, `A`, `K`) are **logic variables that unify against any object of a matching kind at dispatch time** (the same mechanism Dialog and Prolog use) not names of specific instances baked into the rule:

```
rule refuse-locked-open:
  when: player tries to open (C: Container)
  where: C.locked = true
     and C.required-key = (K: Key)
     and player does not possess K
  then: tell "You need a key to unlock that."
        stop the action
```

This rule fires for *any* locked container anywhere in the game, not just one hardcoded room and object, which is the core problem with writing rules in raw Gherkin (`Given the player is in the living room...`). Gherkin was built to pin down every variable of one test scenario, which is exactly wrong for a rule meant to generalize: naming the room turns a general law into a photograph of one case.

`where` clauses use **negation-as-failure**, not classical logical negation: `player does not possess K` means "no fact `possesses(player, K)` is currently derivable," which is cheap and decidable for a closed, fully-known game world. This is the same approach Prolog and Dialog both take, rather than attempting a more expensive proof of falsehood. It is the *closed-world assumption* applied at the level of individual checks: anything not known to be true is false.

## Rule Specificity and Dispatch

When multiple rules could match the same event, resolution is **structural**: the rule matching the more specific pattern wins, computed automatically from the `kind` tree (and, more generally, from how narrow a rule's conditions are; see Defeasible Rules and Exceptions) rather than declared by the author as an explicit priority number.

```
rule refuse-locked-open:              # generic, kind: Container
rule refuse-cursed-chest-open:        # specific, kind: CursedChest
  when: try-action(open, C, A)
  where: C is-a CursedChest
  then: tell A "The chest radiates malice and will not budge."
```

`CursedChest` is a subtype of `Container`, so its rule wins whenever both match. This is the same principle behind CSS's `.locked` losing to a more specific selector, or an overridden method beating its superclass's version.

**Why structural, not explicit priority numbers:** an explicit-priority scheme requires the author to hold every existing rule in mind each time a new one is added. This is an ongoing maintenance cost that grows with the rule count and rots silently (six months later, nobody remembers *why* rule 12 outranks rule 31). Structural resolution computes the ordering as a side effect of the `kind` declarations the author already had to write, rather than storing it as a separate decision.

**Ties still happen, and need a disclosed fallback.** Two rules at equal specificity with no ancestor relationship between their patterns (e.g. unrelated sibling kinds, or two unrelated trait conditions) cannot be resolved structurally. There is no computable fact that breaks the tie. The fallback, borrowed from both Dialog (whose documentation states this plainly) and CSS's cascade, is **source order**. The two disagree on direction, though: in CSS the last declaration wins at equal specificity, while in Dialog the first matching rule wins (which is why Dialog authors put specific rules first). Which direction this language uses is still open (see Open Design Questions). The compiler should warn when a genuine tie is detected, so the author knows they're relying on file order rather than discovering it by surprise during a playtest.

This two-tier resolution (compute what can be computed structurally, fall back to something deterministic and disclosed for the rest) is not unique to this language. CSS's cascade and 1980s production-rule systems both rank by specificity and then fall back on something deterministic. Not every system makes the same choice about ties, though: C++ overload resolution reports an ambiguity error, and Rust's coherence rules forbid overlapping trait impls outright. Resolving ties by source order but warning about them puts this language deliberately in the middle: deterministic like Dialog, but not silent, with room to promote the warning to an error later if ties turn out to signal a badly modeled world.

## Traits and Composition

`kind` is strictly single-inheritance (Layer 1). Cross-cutting concerns (lockable, flammable, magical) are **traits**: flat, composable mixins that sit outside the `kind` tree entirely.

```
trait Breakable
trait Precious
trait Grammatical

trait Fragile extends Breakable, Precious
```

**Why not just give `kind` multiple parents, as TADS does?** TADS's multiple inheritance is genuinely expressive in that an object can inherit from several classes at once but it resolves conflicts with a specific, documented left-to-right depth-first search over the superclass list, and understanding what a method call actually does sometimes requires tracing that order, the same friction as debugging a diamond in Python's method resolution order (which Python computes with C3 linearization) or an ambiguous base class in C++ (which C++ resolves with virtual inheritance). Inform 7 avoids this by keeping `kind` a clean single-parent chain and handling cross-cutting properties as independent flags that never enter the inheritance graph as separate ancestor nodes.

This language follows Inform 7's approach on purpose: `kind` membership (`is-a Container`) walks the single-parent chain and produces an unambiguous specificity depth. No diamonds, ever, by construction. `has-trait Lockable` is a flat, unordered membership check: traits never become ancestors in the `kind` tree, so they never create diamonds. They do still count as conditions, though. A rule that adds a trait condition narrows its match, so `is-a Container and has-trait Lockable` is more specific than `is-a Container` alone (see Defeasible Rules and Exceptions). Two rules matching *different* traits on the same object have no such relationship and are a genuine tie, resolved by the same source-order fallback used elsewhere.

This captures most of what multiple inheritance is actually used for in practice (combining independent behaviors on one object) without importing TADS's resolution-order complexity into the one part of the system (rule dispatch) that specifically needs to stay predictable.

Letting traits extend other traits (as `Fragile` does above) sits uneasily with keeping traits flat. If a `Fragile` object satisfies `has-trait Breakable`, traits form a graph after all, and diamonds become possible again. This is unresolved; see Open Design Questions.

## Defeasible Rules and Exceptions

"Breakable objects shatter on impact: unless enchanted, unless padded" is an instance of **defeasible reasoning**: a default that holds *ut in pluribus* (for the most part), overridden by a more specific case. Exceptions are handled as rules whose match-set is a strict subset of the base rule's match-set, following the principle (from Poole and Touretzky's work on default reasoning) that more specific information beats more general information. This is the same specificity mechanism used everywhere else, generalized from `kind` depth to arbitrary `where`-clause conditions:

```
trait Breakable:
  rule breakable-shatters:
    when: (O: Breakable) receives-impact
    then: O.state := destroyed

exception EnchantedBreakable of Breakable
  when: has-trait Enchanted
  overrides: breakable-shatters
  rule: tell "The vase shimmers and holds."
```

Checking that one rule's conditions contain another's stays tractable because `where` clauses are restricted to conjunctions of simple conditions. Specificity is then a syntactic containment check rather than a general question about whether one arbitrary predicate implies another.

**Preemption, not negation.** The base rule does not check `not enchanted`. That check would need to grow every time a new exception is added. Instead the more specific rule wins dispatch, runs first, and explicitly stops the base rule from firing (`cancel-action` / `overrides`). This follows the DOM event model (`stopPropagation()`): the general rule still runs unless the more specific one explicitly stops it. That default, rather than Express-style middleware where nothing downstream runs unless a handler calls `next()`, lets an exception *add* to the general behavior without re-invoking it, and makes stopping the general rule a visible, deliberate act.

**Unrelated exceptions tie.** An object that is both enchanted and padded matches two exceptions, each more specific than the base rule but neither more specific than the other. Source order decides, and the compiler should warn.

**Exception nesting is capped at one level, by construction, in the grammar itself.** An `exception` block may override a trait's base rule, but it may not declare `exception`s of its own; that production simply doesn't exist. Exceptions are dedicated blocks rather than ordinary rules precisely so the grammar can recognize them and enforce this. If an author is tempted to write "enchanted, unless the enchantment was cast by a novice," the language gives them no syntax for it and forces the real question instead: is a novice-enchantment a distinct, nameable trait? If yes, it becomes `trait WeakEnchantment`: a first-class, reusable, documented concept. If the author can't name it, that's diagnostic too: it usually means a specific scenario is being patched rather than a real category being described, the same instance-binding trap that made raw Gherkin unsuitable for rules in the first place. This is deliberately stricter than the domain checker's warnings. A wall at the language level, rather than a style rule left to author discipline, is what reliably pushes toward naming the abstraction.

## Domains and Cohesion Checking

Composing unrelated traits (`trait Weird extends Breakable, Grammatical`) compiles fine but is a **category mistake**. This is Gilbert Ryle's term for treating something as belonging to a logical category it doesn't. A compiler can check contradiction (two rules assigning conflicting effects) fairly easily, at least with `where` clauses kept to simple conjunctions; it cannot check incoherence on its own, since incoherence is a judgment about meaning, not a formal property. The practical proxy: a **`domain` taxonomy**, a single-inheritance tree separate from `kind`, that every `trait` is tagged with.

```
domain Thing
  domain Physicality
    domain Breakability
    domain Weight
  domain Magic
    domain Enchantment
    domain Curse
  domain Social
  domain Linguistic
```

Warning severity scales with **tree distance to the nearest common ancestor** (the *least common subsumer*, a standard ingredient in the semantic-similarity measures used over description-logic ontologies) rather than a flat same/different check: `Breakability` and `Weight` share a close ancestor (`Physicality`) and warn not at all; `Breakability` and `Social` share only the root and warn strongly. This distance metric is what makes a warning system tunable between the competing goals of catching every real incoherence (recall) and not flagging things that are actually fine (precision). This is a single adjustable threshold rather than an all-or-nothing rule. Some taxonomically distant pairs are common and expected in IF (magic affecting physical objects is the premise of half the genre) and can be pre-registered as exceptions to the exception-detector itself.

**Core domains are sealed; author extensions attach to the tree rather than floating free.** A new domain must declare its parent (`domain Necromancy extends Magic.Curse`) the same way a new `kind` must extend an existing one. The compiler never needs to understand what "Necromancy" *means*, only where it sits in the graph, and the distance math does the rest. This is a deliberate departure from Dialog, which encourages overriding even standard-library predicates by placing the author's rules ahead of the library's; that freedom is well suited to a single disciplined author, but it would undermine a domain checker whose guarantees depend on the taxonomy staying stable ground.

An explicit escape hatch (e.g. a `#cross-domain-intentional` annotation) lets an author silence a warning for a deliberate, unusual combination without the compiler ever hard-blocking it. Friction for the accidental case, none for the deliberate one.

## Kind vs. Domain: Two Orthogonal Axes

`kind` and `domain` are kept as two separate single-inheritance trees rather than collapsed into one, because they answer different questions: **`kind` answers "what is this?"** (classification, is-a); **`domain` answers "what conversation is this behavior part of?"** (a cross-cutting concern, about-what). A `CursedChest` doesn't stop being a `Container` because its interesting behavior sits in the `Magic` domain, and `Magic` isn't a *kind* of anything; it has no instances the way `Container` does.

This is a well-worn move. Library science has had **faceted classification** since S. R. Ranganathan in the 1930s, and ontology engineers keep independent classification axes in separate trees for the same reason (Alan Rector's "normalization" of ontologies is the standard reference): real-world things get sorted along independent axes at once (a library book is classified by subject *and*, separately, by format, such as hardcover vs. paperback, and neither hierarchy needs to know about the other). Forcing `kind` and `domain` into one hierarchy would itself be a category mistake in the other direction: treating a classification axis and a concern axis as the same kind of thing.

## Appendix: Intellectual Lineage

A capsule note on each idea this design draws on, roughly in the order it entered the thinking.

| Idea | Origin | What it contributes here |
| --- | --- | --- |
| Rulebooks, kind hierarchy | Inform 7 | Readable world declarations; the model for keeping cross-cutting properties out of the type lattice |
| Prolog-style unification | Dialog (Linus Åkesson) | Rules match logic variables against kinds, not hardcoded instances; source-order tiebreak for ties |
| Typed operators, precondition/effect split | PDDL / STRIPS (AI planning) | Actions separate "can this happen" from "what changes"; effects as atomic, closed-world writes |
| Cascade specificity, last-rule-wins tiebreak | CSS | Model for structural-first, source-order-fallback conflict resolution |
| Specificity-based conflict resolution | 1980s production-rule systems (OPS5, CLIPS) | Specificity as a conflict-resolution criterion (OPS5 ranks recency first; CLIPS adds explicit *salience*, the same escape hatch weighed here) — production-rule theory that predates and parallels this design |
| Default logic, non-monotonic reasoning | Reiter (1980) | Formal starting point for "defaults with exceptions" (breakable-unless-enchanted); the "birds fly, unless penguin" pattern |
| Specificity preference in default reasoning | Poole (1985), Touretzky (1986) | "More specific information beats more general"; the direct formal ancestor of exceptions as more specific rules |
| *Lex specialis* | Legal interpretation | "More specific overrides more general" as a default interpretive principle, not a ranking anyone has to state |
| Category mistake | Gilbert Ryle | Naming why an incoherent trait combination (Breakable + Grammatical) is wrong without being false |
| Refactoring nested conditionals (Decompose Conditional, Replace Conditional with Polymorphism) | Martin Fowler, *Refactoring* | Justifies capping exception nesting at one level and forcing a named abstraction instead |
| Cohesion | Yourdon & Constantine (structured design) | Formal ancestor of the domain-distance cohesion warning — low cohesion as an automatable proxy for "this doesn't belong together" |
| Genus-species classification | Porphyry's Tree, via Boethius | Structural ancestor of the domain taxonomy tree; graded distance between concepts, not flat same/different |
| Defeasible general precepts | Aquinas, *Summa Theologiae* I-II q.94 a.4; *epikeia*, II-II q.120 a.1 | General rules hold *ut in pluribus* ("for the most part"); more specific circumstances override — same shape as rule exceptions |
| Multiple inheritance and its resolution order | TADS | The road not taken for `kind`; motivates single inheritance plus flat traits instead |
| Faceted classification, ontology normalization | Ranganathan (library science); Alan Rector (ontology design) | Why `kind` and `domain` stay separate trees instead of one tangled hierarchy |
| Description logic, subsumption, least common subsumer | OWL / Semantic Web tooling | Formal machinery behind domain-distance checking; a fallback if hand-rolled tree distance hits its limits |
| Pit of success | Rico Mariani | The organizing design principle: make the right path the easy one, structurally, not by convention |

The broader pattern worth naming: this design converges, from an IF-authoring problem, on an architecture close to a classic expert system (typed facts + specificity-ranked inference) crossed with modern ontology tooling (a taxonomy plus a knowledge graph). That convergence from an unrelated starting point is mild evidence that "typed facts, specificity-ranked rules, graded taxonomic distance" is closer to a general shape for rule-governed reasoning than to domain-specific machinery.

## Open Design Questions

- **Compilation target.** How `kind`, `trait`, and `rule` declarations lower into Z-Machine object attributes/properties and routines, versus Glulx's more flexible object model. Likely needs its own design pass once the surface language stabilizes.
- **Compiler architecture.** The current lean is Rust as the host language and a pipeline of discrete stages rather than a monolithic pass, with code generation kept as a strictly separate, swappable stage over an intermediate representation. Still open: whether that final stage emits assembly text for a separate assembler (as ZILF does with ZAPF and Glazer) or writes bytes directly.
- **Tiebreak direction.** Source order breaks ties, but CSS (last wins) and Dialog (first wins) disagree on direction, and this language hasn't chosen.
- **Trait composition.** Whether traits may extend other traits (`trait Fragile extends Breakable, Precious`). If so, does a `Fragile` object satisfy `has-trait Breakable`, and is a rule on `Fragile` more specific than one on `Breakable`? Either answer being yes makes traits a graph and reopens the diamond problem; treating a composed trait as shorthand for its component conditions may be enough, but hasn't been worked through.
- **Domain extension conflicts.** The core domain tree is sealed and extensions attach to it (see Domains and Cohesion Checking). What remains open is how conflicts get resolved if two extensions attach similarly-named domains at different points in the tree.
- **Domain warnings in exception blocks.** Whether domain-crossing warnings apply only when composing traits, or also when an `exception` block's condition reaches into a distant domain (a `Breakable` exception triggered by a `Grammatical` condition).
- **Shared-condition-across-exceptions detection.** Static analysis that flags when several unrelated `exception` blocks share the same condition (e.g. three different traits all special-casing "unless magically protected") as a signal that a shared trait is missing; genuinely novel among IF languages if built.
- **Domain distance in the presence of future lattice extensions.** Single-parent domains are a provisional decision, made by analogy to `kind` rather than confirmed on their own terms; something like "magic affecting physical objects" arguably belongs under both `Magic` and `Physicality`. If `domain` or `kind` ever need multiple parents for some unanticipated reason, tree-distance math would need to become proper lattice-based subsumption (as in full description-logic reasoners) rather than simple tree distance.
