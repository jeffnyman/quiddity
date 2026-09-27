# Inception

## Where This Started

I wanted to try my hand at creating a simplified language for interactive fiction. Basically something that would compile down to the Z-Machine or Glulx. I like Inform 7 but it can lead to a lot of issues in terms of how to express concepts. Initially, I was trying to conceptualize something where perhaps there are declarations about the world, such as "the living room is an indoor location" and then some type of rule system that would follow a Gherkin-style formatting structure. The basic idea of this format is a keyword template:

```
GIVEN <a specific pre-existing context>
WHEN  <a specific key action takes place>
THEN  <a specific result is observed>
```

This is effectively a description of a state change. The context is a starting state, the action is something that perturbs that state, and the observed result is the new state.

## The Trouble with Gherkin

Here's one example of what that might look like:

```
GIVEN the player is in the living room
AND   the locked treasure chest is present
AND   the player does not possess the silver key
WHEN  the player tries to open the treasure chest
THEN  the player is told "You need a key to unlock that."
```

However, as I started to play around with the idea, even this simple example turned out not to be quite right. The problem is the very first line. Whether the chest refuses to open has nothing to do with the living room. The chest should behave the same way wherever it happens to be. That snag turned out to be the tell that I'd mixed up two different things that look similar but aren't: a **scenario** and a **rule**.

Here's the core issue: Gherkin was built to describe one case, not a law. When someone writes `GIVEN the player is in the living room`, they're pinning down every variable of a single path so the outcome is unambiguous. That's great for *verifying* behavior, but a poor fit for *specifying* it.

The reason is that every GIVEN carries equal weight. Nothing in the format separates the conditions that actually cause the outcome from the ones that are just setting the scene. Look at the example again:

```
GIVEN the player is in the living room            <- incidental: just where this happened
AND   the locked treasure chest is present        <- essential: the chest is locked
AND   the player does not possess the silver key  <- essential: the player lacks the key that fits
```

Only two facts matter here: the chest is locked, and the player doesn't have the key that fits it. The room is incidental. So, also, is the fact that it's a *treasure* chest, and that the key happens to be *silver*. But if you read this scenario as a rule, every one of those facts becomes part of the pattern the rule has to match before it fires.

The practical upshot is that if you then need the same locking behavior for a chest in the attic, a trunk in the basement, and a jewelry box on a dresser, you either duplicate the rule for each one or go back and manually strip out the incidental details. Putting it simply, you've written a photograph of one scene when what you wanted was a physical law. A law that was operative not just of that scene but of any comparable scenes.

I started to think of it like the difference between a court transcript and a statute. "On March 3rd, in the living room, Jeff tried to open the chest without a key" is a case record. It's true of exactly one event. "Whomever attempts to open a locked container without its designated key shall be refused" is a statute. It's true of every container, everywhere, forever, as long as it satisfies the pattern. I want to be writing statutes. Gherkin, used straight, writes case records.

That's the potential issue with building on Gherkin. The GIVEN/WHEN/THEN shape is appealing because it reads naturally and maps cleanly onto state change. But if I keep that shape, the language needs a way to write statutes with it: to say which conditions define the pattern and leave everything else free.

## Quantifying Over Kinds

What I came to see is this: **The fix is to quantify over kinds instead of naming instances.** Give my world model actual types with traits, and let rules bind variables to anything matching a pattern rather than to one hardcoded object. Spitballing, here is what I came up with:

```
kind: Container
trait: locked (bool)
trait: required-key (Key, optional)

rule "locked container needs its key":
  when: player tries to open (C: Container)
  where: C.locked = true
     and C.required-key = (K: Key)
     and player does not possess K
  then: tell "You need a key to unlock that."
        stop the action
```

Notice the room never appears in all that. It's not that the idea of location is banned from rules. After all, a rule about slippery ice absolutely should mention a location trait. Rather, it's that location should only show up when it's actually part of the *pattern*, not as leftover scaffolding from the one scenario you were imagining when you wrote it. `C` is a logic variable that unifies with any object of kind `Container` at match time, the same way a Prolog clause head unifies with whatever fact it's checked against.

## An Old Problem: Universals and Particulars

As it turns out, this is a very old problem with a name. The Scholastics, the philosopher-theologians of the medieval universities (roughly the 11th through 14th centuries), spent a great deal of effort on what they called the problem of universals. A *particular* is one specific thing: *this* chest, in *this* room. A *universal* is something that can be true of many things at once: "container," "locked," "requires a key." The question they argued over was what kind of existence a universal has. Is "container" a real thing in its own right, independent of any actual container? Or is it just a name we attach to a bunch of objects that happen to look alike?

Thomas Aquinas, the 13th-century Dominican who is probably the best-known Scholastic, took a middle position that's usually called *moderate realism*, which he developed from Aristotle. He said universals are real, but they don't float free somewhere on their own. They exist *in* the particulars. Every locked container really does share something with every other locked container. The mind gets at that shared thing by abstraction, pulling the general pattern out of its encounters with individual instances.

That's almost exactly the move my rule engine needs to make. A kind like `Container` isn't just a label (the rule really does depend on it), but it's never met apart from actual objects in the world. The rule has to abstract the *kind-level* pattern ("locked container lacking its key") out of the *instance-level* details ("this chest, this room") that first prompted me to write it. The Scholastics even had a word for those details: they were *accidents*, properties a thing happens to have without them being part of what it is. A chest moved to the attic is still the same chest. In fact, "where" a thing is was one of Aristotle's standard categories of accident, and Aquinas kept it. My GIVEN about the living room was an accident that I mistook for part of the pattern.

What a thing *is*, as opposed to what it happens to be, the Scholastics called its *quidditas*, its "whatness." That's where this project's name comes from: a language whose rules speak to what things are rather than where they happen to be.

## Prior Art

There were a few pieces of prior art I looked at before I even started to commit to a grammar.

- **Inform 7's rulebooks** already do a version of what I'm talking about. "Instead of opening a locked container" is a kind-matched rule, not a room-matched one. Where I7 gets messy isn't the rule concept, it's (a) natural-language parsing ambiguity, and (b) implicit specificity ordering when multiple rules could match the same action. You often can't tell which rule "wins" without tracing I7's internal rulebook machinery.
- **Dialog** (Linus Åkesson's IF language) is literally built on Prolog-style unification and was designed by someone who hit this exact frustration with I7. It's probably the closest existing language to what I'm thinking about, and I found it worth reading even if I wasn't going to adopt its syntax.
- **PDDL/STRIPS** (from the AI-planning world) models actions as operators with preconditions and effects over typed variables. Useful if you want a "when/where/then" structure to also support NPC planning or solver-assisted testing later.

## When Rules Collide

One design decision I knew I needed to make early that all of the above systems had to deal with: when two rules could both match the same action (say, a general "locked container" rule and a more specific "the cursed chest resists all keys" rule), how does my engine pick a winner? Most-specific-pattern-wins and explicit-priority-number are the two common answers, and I knew it was going to be much easier to bake that into the grammar now than to retrofit it later once I had got [Rezrov-style acceptance tests](https://github.com/jeffnyman/rezrov/tree/main/acceptance) depending on rule order.

## Dividing the Labor

So, where did this leave me? I decided to start specifying a language that matches what Dialog does and PDDL/STRIPS does, with a little of Inform 7's declarative nature. That felt like a sensible synthesis, and, at least in my first pass, I found that the three pieces actually divide labor cleanly if you let each one own the part it's best at:

- **Inform 7**: surface syntax for declaring the world (readable, prose-like)
- **Dialog**: the underlying rule/inference engine (unification, backtracking)
- **PDDL/STRIPS**: the discipline for actions (explicit preconditions and effects, typed)

The analogy that sprang to mind was a car: I7 is the dashboard and controls (what the driver interacts with), Dialog is the transmission (how motion actually gets transmitted and matched to conditions), and PDDL is the engineering spec sheet that makes sure every action has a clearly stated "what has to be true before" and "what becomes true after." With this, I felt I could at least sketch a layered spec.

## The Layers

### Layer 1: World declarations (Inform 7's territory)

Keep the readable, near-English declarations. This is I7's genuine strength, and there's no reason I can see to lose it. So, for example:

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

This compiles down to typed facts. These declarations essentially form a knowledge base, the same shape as PDDL's `(object-type room)` declarations or Dialog's `#room{living_room}.` facts. The point of this layer is purely ergonomic: humans read and write prose better than they read `requires(treasure_chest, silver_key)`.

### Layer 2: Actions as PDDL-style operators

This is where I would borrow PDDL most directly, because it fixes what I consider to be a real weakness in I7: the murkiness around what a rule actually *changes*. PDDL forces you to separate the question "can this happen?" from "what happens as a result?", which is a distinction I7 blurs. I say it blurs because a "Carry out" rule can quietly check conditions as well (which it's not really supposed to do, that being what a "Check" rule is for) and an "Instead" rule can quietly cause side effects. The categories leak into each other.

My initial thought broke down like this:

```
action open (C: Container, A: Actor):
  precondition:
    A is in same-room-as C
    C.locked = false
  effect:
    C.state := open
    tell A "You open " + C.name + "."
```

Locked containers simply never satisfy `open`'s precondition. You don't need a separate rule to intercept and block them. The *refusal message* is a different concern from the *action itself*, so it lives in its own layer.

### Layer 3: Dialog-style inference rules for everything else

Actions-with-preconditions, like the above, handle the "doing" side. But you still need general-purpose logical rules for things like "if the room is dark and the player has no light source, describe darkness instead of the room." These are reactive, pattern-matched, and potentially chained inferences. This is where Dialog's unification comes in, and where my Gherkin instinct was pointing, minus the instance-binding problem from before.

A darkness rule would take the same shape, but the more pressing example is the one Layer 2 set aside: the refusal message for a locked container. My initial thought here was this:

```
rule refuse-locked-open:
  when: try-action(open, C, A)
  where: C.locked = true
  then: tell A "You need a key to unlock that."
        C.required-key = K
        A does not possess K
        cancel-action
```

The `when/where/then` shape might look descended from Gherkin, but it isn't quite the same thing, and the differences are deliberate. Gherkin's order is context, then action, then outcome. Here the trigger comes first: `when` names the action being attempted, much like Gherkin's WHEN. The context moves second and changes role: `where` isn't a scene being set up the way GIVEN is, but a guard that any match has to pass. And `then` is no longer an observation to check but an effect to carry out. I decided to keep the shape because it still reads well and maps cleanly onto guard-clause logic. The key change is that `C` and `A` are now logic variables unifying against *whatever* container and actor triggered the attempt, not a scenario that was invented while writing a test of the condition.

## The Specificity Problem

I know I'll want a resolution order when multiple rules match the same event. The cleanest option, and the one I came to lean towards, is **most-specific-pattern-wins**, borrowed from how CSS resolves conflicting selectors or how object-oriented method dispatch picks the most-derived override:

```
rule refuse-locked-open:              # generic, kind: Container
rule refuse-cursed-chest-open:        # specific, kind: CursedChest
  when: try-action(open, C, A)
  where: C is-a CursedChest
  then: tell A "The chest radiates malice and will not budge."
```

`CursedChest` is a more specific kind than `Container`, so its rule wins when both match. This is the same principle as `#treasure-chest.locked` beating `.locked` in a stylesheet, or a subclass's overridden method beating the superclass's. This needs to be a first-class part of the spec, not folklore convention, or I'll get exactly the "why did this rule fire instead of that one" debugging pain that I7 is notorious for.

## Effect Axioms as Closed-World Updates

Picking a single winner matters for more than which message the player sees. It's also what makes it possible to keep the world itself consistent, as long as I'm strict about how the winner is allowed to change it.

PDDL effects are absolute (they *set* facts, they don't merely suggest them) which sidesteps a classic bug in naive rule systems: two rules firing on the same event and leaving the world in a contradictory state (chest is simultaneously open and locked). If effects are the *only* way state changes, and every effect is a flat assignment applied atomically after the winning rule resolves, you get something like an event-sourcing guarantee: each action produces exactly one consistent write, and the sequence of writes is your full audit trail. This is genuinely useful for debugging IF logic, and for those acceptance tests I'm already writing against Rezrov.

## Structural or Explicit Priority?

This effect model raises the stakes on the question from "When Rules Collide." If only the winning rule gets to write, then how the winner is chosen decides what the world looks like afterward, not just what the player is told. So it's a question worth settling before I go much further: Do I want **rule priority to be entirely structural** (most-specific-kind-wins, computed automatically from the type hierarchy), or do I want an **explicit priority number** as an escape hatch for cases the type system can't express (a rule that should fire "early" for pacing reasons, unrelated to specificity)? Dialog sidesteps the question with plain source order (authors simply put more specific rules first), but it's a truism that most game-scripting DSLs end up needing some kind of escape hatch eventually. I felt this was worth deciding now, since the decision would change how much of the type hierarchy I need before writing my first real rule.

### Why Structural?

I found it interesting to consider why structural is often chosen. One perhaps obvious reason is because it would seem to help the author *not* have to pick out a priority. After all, so I reasoned, without structural, wouldn't you have to decide priorities for everything and then update them if anything changes?

And, indeed, that's the real cost of explicit priorities: it's not a one-time decision. Instead, it's an ongoing maintenance burden that scales with your rule count. When you add rule #47, you have to hold rules #1 through #46 in your head and ask "where does this slot in?" That's O(n) mental overhead per addition, and it silently rots. Six months later you perhaps don't remember *why* rule #12 was set above rule #31, just that it was, and now you're afraid to touch it. Structural specificity sidesteps this because the ordering isn't stored anywhere as a decision. Instead, it's *computed* from the type hierarchy you already had to build anyway. You get the ordering for free as a side effect of describing your world accurately.

I came to realize that there's a nice way to see why this works and it's the same reason case law and civil law both converge on "more specific overrides more general" (the principle of *lex specialis*) as a *default* interpretive principle, rather than requiring judges to rank every prior ruling against every other. `CursedChest` overriding `Container` isn't a preference anyone stated. It falls out of the fact that a cursed chest *is-a* container plus something more, so a rule written specifically for it is, by construction, talking about a narrower slice of the world. This would mean that the specificity ordering was implicit in my `kind` declarations from the start. That would further mean that I'm not inventing new information when I compute it, just surfacing something that was already there.

Which all sounds great but, and this was the part worth being honest with myself about before I built anything, structural resolution doesn't fully escape the problem. It just narrows it to a smaller, harder case: **ties**. What happens when two rules match the same event, at the same specificity, with no subtype relationship between them at all? Let's say that a `CursedChest` and a `TrappedChest` are different, unrelated subtypes of `Container`, and a particular object happens to be both:

```
rule refuse-cursed-open:  where: C is-a CursedChest
rule refuse-trapped-open: where: C is-a TrappedChest
```

Neither is more specific than the other in the type lattice. They're siblings, not ancestor/descendant. Structural ordering genuinely can't resolve this; there's no computable fact that breaks the tie. This is exactly the situation CSS runs into with equal-specificity selectors, and CSS's answer is a fallback rule: source order, last-declared-wins. It's not principled, it's just deterministic. Which turns out to be the actually important property. The goal isn't "the system picks the philosophically correct rule." Rather, it's "the system's choice is *predictable* so you can debug it."

For reference, Dialog's real solution, since I would likely be borrowing from it directly, is to make this explicit rather than silent: if two clauses could both match, the *textual order* in the source file decides, full stop, and the language documentation tells you so up front. This all means that the honest spec for my language probably wants **structural specificity first, source-order-in-file as the tiebreaker**, with maybe a compiler warning when a genuine tie is detected, so the author at least *knows* they're relying on file order rather than discovering it by surprise during a playtest. That arguably gets you 95% of the "I never have to think about priority" benefit, while keeping the last 5% honest instead of magical.

### Compute What You Can, Then Fall Back

At this point I started to think: "What I want is sort of like Dialog along with CSS specificity." And, yes, that's a good compression of it, but it's worth being precise about which piece each one contributes, because they're solving *different* halves of the same problem.

- **Dialog** gives you the *fallback mechanism*: when nothing else can decide, fall back to textual order, and be upfront that that's what's happening.
- **CSS** gives you the *primary mechanism*: a computable notion of specificity (for CSS, count of ID/class/element selectors; for me, depth in the `kind` hierarchy) that resolves the vast majority of cases *before* you ever need the fallback.

It's worth pointing out that CSS actually needs the Dialog-style fallback as well. Consider `.foo { color: red }` and `.bar { color: blue }` on the same element. These are the same specificity, no ancestor relationship, and are resolved by cascade order (last rule in the stylesheet wins). This is the exact same "structural first, source-order as tiebreaker" move. Given that, it's less "Dialog *plus* CSS" as two separate borrowed ideas, and more that I'm just independently arriving at a general pattern that any specificity-based dispatch system seems to converge on: compute what you can, fall back to something deterministic (and disclosed) for the rest.

Not every system makes the same choice about that last step, though. C++ overload resolution reports an ambiguity error when two candidates tie, and Rust's coherence rules forbid overlapping trait `impls` in the first place. Those systems handle ties by *rejecting* them at compile time. So, there's really a spectrum: resolve ties silently by source order (Dialog, the CSS cascade), resolve them by source order but warn the author, or refuse to compile them at all (C++, Rust). The compiler warning I suggested above puts my putative rule engine deliberately in the middle, deterministic like Dialog but not silent about it, and it leaves room to promote that warning to an error later if ties turn out to be a sign of a badly modeled world.

## One Parent, Many Traits

There was another thing I found worth flagging while it was cheap to decide, rather than after I've written fifty rules: **multiple inheritance in my `kind` hierarchy makes the "compute what you can" part harder**, not just the tiebreaker part. If `CursedChest` and `TrappedChest` are unrelated siblings, that's a clean tie: no ancestor relationship, easy to detect. But if I ever allow a kind to have *two parents* (say `MagicalContainer` is a `Container` that's *also* a `MagicalThing`) then two rules can each be "more specific than `Container`" along genuinely different branches of the lattice, with no clean answer to which one is "more specific overall."

This is the classic diamond problem: the one Python solves with C3 linearization to compute its method resolution order (MRO), and the one C++ leaves as an ambiguity error unless you reach for virtual inheritance. It doesn't mean I shouldn't allow multiple kinds. After all, traits/mixins are genuinely useful for IF (a chest that's both `Lockable` and `Flammable`). But it does mean I'll want to decide *now* whether `kind` is strictly single-inheritance (simple, no diamonds, occasionally awkward) or allows multiple parents (more expressive, needs a linearization algorithm to stay deterministic).

I knew TADS (another IF language) allows multiple inheritance, while Inform 7 does not. And studying some of the history of how people interacted with both systems led me to a recommendation to myself on this design: **single inheritance for `kind`, with traits/mixins as a separate, explicitly-ordered composition mechanism**. The TADS-vs-Inform contrast was actually the evidence for why.

TADS's multiple inheritance is real, and it costs real complexity. TADS objects can inherit from several classes at once, and TADS resolves conflicts with a specific, documented left-to-right depth-first search order over the superclass list. That's not a criticism. It's an expressive mechanism, and experienced TADS authors learn to wield it deliberately, often to great simulational benefit. But it means understanding what a method call actually *does* on a given object sometimes requires tracing the class list order, same as debugging Python's MRO when a diamond shows up. That's an acceptable cost for TADS's target audience, which are authors already comfortable thinking in class hierarchies, but it's friction you don't have to accept if you don't need it.

Inform 7 sidesteps this almost entirely, and I think that's the more instructive data point. I7 does have a `kind` hierarchy, but it's a clean single-inheritance tree (a "container" is a "thing" is an "object"; one line up, no branching), and *cross-cutting properties* like lockable, edible, wearable, and scenery, to name just a few, aren't modeled as multiple base classes at all. They're closer to **traits/mixins attached to an object**: "The treasure chest is a locked openable container" reads like multiple inheritance in prose, but under the hood it's one `kind` (`container`) plus independent boolean/enumerated *properties* (`locked`, `openable`) that don't participate in the specificity lattice as separate ancestor types. That's precisely why I7 doesn't hit the diamond problem: properties aren't nodes in the inheritance graph, so there's no graph to have diamonds in.

All of this maps well onto my rule-dispatch problem specifically, because I don't actually need multiple inheritance's full power. What I do need is *composability of independent concerns* (lockable, flammable, magical) without those concerns fighting over specificity ranking against each other. So I started to draw the line like this:

```
kind Container extends Thing     # single-parent chain; this is the specificity lattice
kind CursedChest extends Container

trait Lockable
trait Flammable
trait Magical

the treasure chest: kind Container, traits [Lockable, Flammable]
```

For rule matching: `is-a Container` walks the single-parent `kind` chain (unambiguous depth, clean specificity ordering, no diamonds, ever, by construction). `has-trait Lockable` is a flat, unordered membership check: true or false, no notion of "more specific," so it simply never enters the specificity computation at all. A rule that matches on `kind` alone is more specific than one on a bare trait; two rules matching on *different* traits on the same object are a **tie**, resolved the Dialog/CSS way (source order). This is because they genuinely are unrelated, and pretending otherwise would just be re-inventing a fake specificity number to avoid admitting the tie exists.

This gets you most of what TADS's multiple inheritance is actually used for in practice (combining independent behaviors on one object) without importing its resolution-order complexity into the one part of the system (rule dispatch) where you specifically want dispatch to stay predictable.

## Defaults and Exceptions

With all that forming in my mind, what I found worth deciding next was this: Should traits be allowed to carry their *own* rules (e.g., `Flammable` brings a generic "catches fire near flame" rule that attaches to any object with that trait), or should traits be pure tags with all behavior living in the top-level rule set? That's basically the "interfaces with default methods vs. marker interfaces" question from OOP language design, and I knew the answer to this would shape how much logic ends up living in trait definitions versus the global rulebook.

As I was going through these ideas, I started to realize that traits having rules would be powerful, because then you could specify the rules for something that is `Breakable` or `Flammable` and then allow for exceptions. The breakable thing isn't breakable if it has a magic spell cast on it. Or if it's inside a box packed with stuffing.

In fact, what I was circling around has a name in a field that's been wrestling with exactly this since the 1970s: **defeasible reasoning**, or non-monotonic logic. The classic textbook example is "birds fly ... unless it's a penguin," and it extends naturally: "... unless the penguin is on a rocket sled." My `Breakable` case is the same shape: "breakable objects break on impact ... unless enchanted ... unless packed in stuffing." The AI literature calls the base rule a *default* and the override an *exception*, Reiter's default logic (1980) was the formal starting point for reasoning with defaults without the whole rule system collapsing into contradictions. Reiter's system on its own doesn't say which default wins when two conflict, though. The specific idea I'm relying on, that more specific information should beat more general information, was worked out shortly after, notably in David Poole's "On the Comparison of Theories: Preferring the Most Specific Explanation" (IJCAI 1985) and David Touretzky's *The Mathematics of Inheritance Systems* (1986).

The mechanism that makes it work is one I've actually already built (at least conceptually above), without quite naming it: **exceptions are just more specific rules**, in the same specificity sense as before. I'm just widening what "specific" means from "deeper in the `kind` tree" to "matches a strict superset of conditions." This is what I was thinking:

```
trait Breakable:
  rule breakable-shatters:
    when: (O: Breakable) receives-impact
    then: O.state := destroyed

rule enchanted-resists-breaking:
  when: (O: Breakable) receives-impact
  where: O has-trait Enchanted
  then: tell "The vase shimmers and holds." 
        cancel-action

rule padding-protects:
  when: (O: Breakable) receives-impact
  where: O is-inside (C: Container) and C has-trait Padded
  then: tell "The stuffing absorbs the blow."
        cancel-action
```

Here `enchanted-resists-breaking` matches everything `breakable-shatters` matches, *plus* one more condition (`has-trait Enchanted`). Its match-set is a strict subset of the general rule's match-set, by which I mean that anything that satisfies it necessarily satisfies the general rule too, but not vice versa. That subset relationship *is* specificity, generalized: `kind` depth was really just a special case of "more conditions narrow the match," and now I would be applying the same principle to arbitrary `where`-clause predicates, not just the type lattice.

This does revise something I said in the previous section, where I claimed `has-trait` never enters the specificity computation at all. That was true of the *kind* lattice, and it still is: traits never become ancestors, so they never create diamonds. But once specificity means "narrower match," a trait used as a condition narrows a rule's match like any other condition. `is-a Container and has-trait Lockable` is more specific than `is-a Container` alone, which is also what an author would intuitively expect.

There is a limit I need to be honest about here, though. "Anything that satisfies *this* rule also satisfies *that* one" is, in general, a hard question to answer about arbitrary predicates, and once negation and disjunction get involved it can't be answered mechanically at all. What I think keeps it tractable is restricting `where` clauses to conjunctions of simple conditions. Then specificity becomes a syntactic check: does one rule's set of conditions contain the other's? That's essentially what Inform 7 does when it ranks rules. Rules that introduce extra variables, like `padding-protects` binding a container `C`, make even that check a bit more involved, but it stays computable.

This also means the tiebreaker from before still has a job. `enchanted-resists-breaking` and `padding-protects` are each more specific than `breakable-shatters`, but neither is more specific than the other. Their conditions are unrelated. So for an enchanted vase packed in a padded box, both exceptions match, and source order decides which message the player sees. Both cancel the break, so the world ends up the same either way, but the case is a good concrete example of a tie the compiler should warn about.

And that answers the question I started this section with. Traits should carry rules, but only their *defaults*: `breakable-shatters` lives inside `Breakable` because it's what being breakable means. The exceptions live in the top-level rule set, because they're about other circumstances (enchantment, packing) that the trait itself shouldn't have to know about. Specificity is what lets the two meet without either one needing to reference the other.

## Making Exceptions Work

Given the approach I was moving towards, I found there were three implementation details worth locking down now, because I felt they would bite me later if I didn't.

**1. Preemption, not negation.** Don't make the general rule check `not enchanted`. That gets combinatorial fast (soon it needs to check *every* exception that might exist). Instead, let the more specific rule win the dispatch, run first, and explicitly `cancel-action` to stop the general rule from firing at all. This is the same move as `event.stopPropagation()` in DOM event handling: the most-specific handler gets first crack, and the event keeps flowing to more general handlers unless that handler explicitly stops it. Express middleware makes the opposite choice, where nothing downstream runs unless a handler calls `next()`. I think the DOM default is the right one here. It lets an exception *add* to the general behavior without having to re-invoke it, and it means stopping the general rule is always a visible, deliberate act rather than something that happens by omission. I already know this pattern cold from thirty years of building systems; I'm just applying it to rule dispatch instead of HTTP requests.

**2. Negation-as-failure for the guard checks themselves.** When a rule's `where` clause says `O does not possess K`, I need to decide: is that "provably false" (true logical negation, expensive, sometimes undecidable) or "not currently derivable from what we know" (negation-as-failure, what Prolog and Dialog both actually use)? For a game world with a closed, fully-known state, negation-as-failure is almost certainly the right call. If the engine can't find a fact `possesses(player, key)`, treat it as false. This is cheap, decidable, and matches how I7 and Dialog both actually behave under the hood. It's also just the *closed-world assumption* stated at the level of individual checks: anything not known to be true is false.

**3. Simultaneous exceptions are still just ties.** Enchanted *and* padded at once: neither subsumes the other, same as the `CursedChest`/`TrappedChest` sibling case. Same fallback: source order, disclosed, done.

I like cross-disciplinary framing and, with this, I realized that expert systems from the 1980s solved this identical "which rule fires" problem. Forward-chaining production systems like OPS5 (used to build XCON, which configured DEC's VAX computers) and later NASA's CLIPS had to choose among many matching rules on every cycle, and one of their standard conflict-resolution criteria was literally called **specificity**: the rule with more conditions wins. The details differ from what I'm planning, though. OPS5 ranks recency first and uses specificity as a tiebreaker, and CLIPS lets authors assign an explicit priority, called *salience*, that outranks everything else. That's the same escape hatch I weighed earlier. So, again, I've independently arrived at forty-year-old production-rule-system theory, which is a good sign it's the right shape rather than a coincidence.

I happen to have a Thomist orientation and this is close to Aquinas's own account of practical reasoning in the *Summa* (I-II, q.94, a.4): general moral precepts hold *ut in pluribus*, "for the most part," and admit exceptions the further you descend into particular circumstances. "Return what is entrusted to you" is a sound general rule ... until the particular case is a madman asking for his sword back. (That example is older than Aquinas, going back to Plato's *Republic*, and Aquinas uses it himself in II-II, q.120, a.1, where he names the virtue of recognizing such exceptions *epikeia*, or equity.)

The structure is identical to mine: a default that's true in the general case, overridden by a more specific case that captures a circumstance the general rule didn't anticipate. It's the same logical shape showing up in moral theology, expert systems, and CSS specificity, which is a nice small sign that "more specific overrides general, by default" is close to a structural fact about how rule-governed reasoning works, not an artifact of any one field.

Given all this, I wanted to sketch how deep this exception-nesting should be allowed to go. A key question reared its head: should an exception-to-an-exception be expressible directly in the rule syntax, or is that a smell that the *trait* itself needs refining (e.g., splitting `Breakable` into `Breakable` and a separate `Shatterproof`-when-enchanted trait)?

## Easy to Do the Right Thing

I feel like these kinds of languages need to make it easy for someone to do the right thing and harder to do the wrong thing. In the convention-over-configuration sense, the opinionated nature of the language decides what's "right" or "wrong." In this case, it does feel like if there's too much exception nesting, it would be better to encourage thinking of abstractions that match the behavior that is being sought.

That instinct is the "pit of success" design principle. This was Rico Mariani's phrase for languages/APIs shaped so that the natural, easiest path is also the correct one, and the wrong path requires *deliberately climbing out* of the pit to reach it. You don't ban the mistake, you just make correctness the path of least resistance. Rust is the canonical modern example. Safe Rust won't let you write a dangling reference at all, but the language doesn't pretend low-level pointer work is never needed: you can still do it inside an `unsafe` block. Writing safe code is the easy path, and the risky path still exists, but you have to announce that you're taking it.

My intuition ("too much exception nesting should push the author toward a better abstraction") is really a variant of a much older piece of advice: Martin Fowler's treatment of nested conditionals in *Refactoring*. When you catch yourself writing an `if` inside an `if` inside an `if`, that's not a moral failing, it's a *signal* that the branches represent a concept your model doesn't have a name for yet. Fowler's answer is to extract: pull the nested condition out into its own named thing (his "Decompose Conditional" refactoring), or, when the branches really are different kinds of object, turn them into types ("Replace Conditional with Polymorphism"). My framing does the same move one level up: an exception-to-an-exception isn't wrong, it's *evidence* that you're modeling a distinct kind or trait you haven't named yet.

Given all this, I wanted to come up with a concrete way to make the language itself enforce this, rather than leaving it as author discipline (which tends not to hold under deadline pressure; I know this from thirty years of watching "we'll clean it up later" never happen). The overall concept I'm landing on is this: **Structurally cap exception depth at one level, by construction, in the grammar itself.**

```
trait Breakable:
  rule breakable-shatters: ...

exception EnchantedBreakable of Breakable
  when: has-trait Enchanted
  overrides: breakable-shatters
  rule: tell "The vase shimmers and holds."
```

An `exception` block is allowed to override a base trait's rule. But an `exception` block is **not** itself allowed to declare `exception`s of its own. That production simply doesn't exist in the grammar. If you're tempted to write "enchanted, *unless* the enchantment was cast by a novice," the language doesn't give you syntax for that. It forces the real question instead: *is a novice-enchantment a distinct, nameable thing?* If yes, then `trait WeakEnchantment` is now a first-class citizen with its own identity: discoverable, documented, reusable elsewhere. If you can't name it, that's diagnostic too. It usually means you're patching a specific scenario rather than describing a real category, the same trap as the Gherkin instance-binding problem that I started from. The depth limit doesn't just prevent messy code, it forces the same discipline at every level: *stop and name the abstraction* instead of *bolt on a special case*.

I'll admit this quietly changes the mechanism from the previous section. There, exceptions were just ordinary top-level rules that happened to be more specific than the default, and specificity sorted them out. Here they've become dedicated `exception` blocks that name what they override. I think the change is necessary rather than cosmetic. As long as an exception is just another rule, the grammar has no way to know it's an exception, and so no way to stop an exception from having exceptions of its own. Making exceptions their own construct is what gives the depth limit something to attach to.

This is in the spirit of Niklaus Wirth's languages, which got steadily stricter about control flow: Pascal kept `goto` but fenced it in with declared labels, and Modula-2 and Oberon dropped it entirely. It's also the preference behind Python's "flat is better than nested," which isn't a claim that nesting is *never* correct. The difference is that Python states that preference as a principle, and what I want is to put it into the grammar. A wall placed at the *language* level (rather than left to a style guide nobody reads under deadline) reliably produces better-factored programs, because the friction of working around the wall exceeds the friction of just doing it right.

Given all this, there are two design knobs worth pinning down together, since they interact.

1. **Can a `trait` itself compose other traits?** (`trait Fragile extends Breakable, Precious`; layering abstractions without layering *exceptions*, which is the escape valve that keeps a hard depth-cap from feeling suffocating.)

2. **What does the compiler do when it detects the same pattern repeated across multiple `exception` blocks**; e.g., three different traits all special-case "unless magically protected"? That's a *convergent* signal (independent evidence pointing at the same missing abstraction) that you could plausibly have the compiler flag: "these three exceptions share a condition; did you mean to define a shared trait?" That's static analysis doing the abstraction-hunting *for* the author, which is a genuinely distinctive feature no existing IF language has, at least to my knowledge.

I should be honest that the first knob pulls against something I settled earlier. In "One Parent, Many Traits," traits were deliberately flat: plain membership checks, with no hierarchy and so no diamonds. Letting `Fragile` extend `Breakable` and `Precious` gives traits parents, and that raises questions I'd have to answer. Does a `Fragile` object satisfy `has-trait Breakable`? Is a rule on `Fragile` more specific than a rule on `Breakable`? If so, traits form a graph after all, and the diamond is back (say, if `Breakable` and `Precious` both extended some shared `Physical`). The generalized notion of specificity from "Defaults and Exceptions," where more conditions means a narrower match, may be enough to handle it, if `Fragile` is treated as shorthand for the conditions it's built from. But I haven't worked that through, so it's a gap I'm flagging rather than one I've closed.

What I was coming around to was that it seems like composition for traits is a good idea. Granted, it could go too far, but, with my above example, if someone was extending "Fragile" with something that had nothing to do with the idea of breakability, that should be a warning sign. And I really like that idea of asking about whether a trait was desired.

Even while framing this thought, I realized my thinking was a bit sloppy. I had put my finger on something that needed to be split into two separate questions, because they're actually different kinds of problem: one is *structural* (does the compiler have any hope of checking this automatically?) and one is *semantic* (does this combination make sense at all?). Combining `Fragile` with something unrelated to breakability isn't a bug the type system can catch the way it catches a type mismatch. That's because there's nothing formally *wrong* with `trait Fragile extends Breakable, Grammatical`. It compiles. It's just nonsense. That's worth naming precisely, because philosophy already has a precise term for it: Gilbert Ryle called this a **category mistake**, which is treating something as if it belonged to a logical category it doesn't. His own example is a visitor to Oxford who is shown the colleges, libraries, and laboratories and then asks, "But where is the University?", as if the university were one more building alongside them rather than the way all of them are organized. `Breakable` and `Grammatical` aren't opposed or contradictory. They're just from *different universes of discourse*, and combining them isn't false, it's incoherent in a way falseness isn't.

The reason this matters for language design specifically is that a compiler can check *contradiction* (two rules that assign conflicting effects to the same event) fairly easily, at least with `where` clauses kept to the simple conjunctions from earlier. That's just logic. But it can't check *incoherence* on its own, because incoherence isn't a formal property, it's a judgment about what the trait *means*. So I can't fully automate my instinct. But you can get most of the way there with a cheap mechanism that makes the compiler *suspicious* without requiring it to actually understand meaning. Here's an idea of where my head was going:

```
trait Breakable:
  domain: physicality
  rule breakable-shatters: ...

trait Precious:
  domain: physicality
  rule ...

trait Grammatical:
  domain: linguistics
  rule ...

trait Fragile extends Breakable, Precious    # same domain; fine, no warning
trait Weird extends Breakable, Grammatical   # different domains; compiler warns
```

Each `trait` declares a `domain` tag, by which is meant a loose classification (physicality, magic, social-standing, linguistics, whatever taxonomy you settle on). In this context, composing traits from mismatched domains triggers a *warning*, not a hard error. That distinction matters and connects straight back to the opinionated-language point I opened this section with, because opinionated doesn't have to mean tyrannical: sometimes a weird combination is exactly what an author wants (a sentient, grammatically-obsessed breakable vase is a perfectly cromulent joke NPC in IF), so you don't want to *forbid* it. You want to make the *accidental* version require a moment's pause, while the *deliberate* version stays fully available with one explicit acknowledgment:

```
trait Weird extends Breakable, Grammatical
  #cross-domain-intentional
```

That's the same shape as Rust's `#[allow(dead_code)]` or `unsafe` blocks. The compiler doesn't trust your judgment by default, but it also doesn't override it; it just requires you to *say so out loud*, which is cheap for the case where you mean it and a useful speed bump for the case where you don't. Pit of success again: the accidental path (forgetting you tacked on an unrelated trait, or copy-pasting a composition you didn't fully think through) now has slightly more friction than the deliberate one.

There's a tension in how I've put this that I want to own. A few paragraphs back I called `Breakable` plus `Grammatical` "just nonsense," and here I'm defending it as a legitimate joke. I think both are true. The combination is nonsense almost every time it shows up, which is exactly why it deserves a warning. On the rare occasion it isn't, the author knows it and can say so. The warning is aimed at the common case, and the acknowledgment exists for the rare one.

It's worth naming the software-engineering ancestor of this too, since it's the same idea under a different name: **cohesion**, from Yourdon and Constantine's structured design work in the '70s: the measure of how closely the responsibilities inside one module relate to each other. A `Fragile` trait built from `Breakable` + `Precious` is *highly cohesive*: everything in it is in service of one coherent idea ("this object matters and can be lost"). A trait built from `Breakable` + `Grammatical` is *low cohesion*: two unrelated jobs stapled together. Yourdon and Constantine's name for the worst case was *coincidental cohesion*, the code equivalent of a junk drawer. Domain-tag mismatch is really just a cheap, automatable *proxy* for detecting low cohesion, the same way cyclomatic complexity is a cheap proxy for "this function is trying to do too much" without the compiler actually understanding what the function *means*.

## Domains as a Taxonomy

On these topics, I knew there were two things worth pinning down before this all got built.

1. **Should `domain` be a closed, curated enum you define upfront** (physicality, magic, social, linguistic; as in, a fixed vocabulary, consistent across every game built in the language), **or an open tag authors invent per-project?** Closed gives you a real, checkable taxonomy but constrains what stories the language comfortably expresses. Open is more flexible but means the warning system is only as good as whatever tags an author happens to remember to use consistently.

2. **Should domain-crossing warnings be visible only at `trait` composition time, or also flow through to `exception` blocks**? The idea here is that an exception is really just "a trait's rule, overridden under a condition," and an exception condition that reaches into a wildly different domain (a `Breakable` exception triggered by a `Grammatical` condition) is arguably the same smell, just showing up one level down.

### Sealing the Core

This, however, led me down another path. I feel like with the domain, the compiler would also come with a library and that library would be defined upfront. But that library, as in most IF systems, could be extended. So the idea is that someone shouldn't change the core library (I know Dialog actually encourages this) but they can extend it. This means the core could always be checkable but whatever authors extend becomes up to them, unless I ever built a way to check their additional taxonomy. This means that those domain-crossing warnings should be as broad as is possible without giving false alarms too often.

As I started to work through what all this meant, I realized that what I've described there is actually two separate ideas that are easy to conflate but most definitely shouldn't be conflated: **who's allowed to change something** (core sealed, extensions open) and **what gets checked** (core verified, extensions currently trust-based). They don't have to move together, and I think keeping them separate is exactly the design that solves my "as broad as possible without false alarms" concern.

Even with this idea in place, I knew there was a major decision point here: **sealing the core**. This is a real fork in the road, and Dialog took the other branch deliberately. Dialog comes out of a Prolog lineage where *overriding is the whole point*. There's no privileged "system" layer, just predicates that happen to ship with the library, and you're trusted to override anything, including things the standard library defines, by putting your own rules ahead of the library's. That works beautifully for a single disciplined author who wants total control. But it has a cost that matters specifically for my use case: if the compiler's domain-crossing checker is calibrated against a taxonomy that any author could silently redefine, the checker guarantees rot the moment someone touches it. A warning system is only as trustworthy as the ground truth it's measuring against. If `physicality` itself could mean something different in every project, "these two domains are far apart" stops being a fact and becomes a guess. Sealing the core isn't about distrust of the author generally; it's about preserving the one piece of *stable ground* the checker needs in order to say anything meaningful at all.

### A Tree of Domains

**On making the broad-but-not-noisy warning actually work**, here's the mechanism I was reaching for: stop treating `domain` as a flat tag and make it a **taxonomy tree**, with my core library shipping a real hierarchy rather than a bag of unrelated labels:

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

This is, not incidentally, exactly the structure Porphyry's Tree gives you in classical logic: the genus-species classification scholastics like Aquinas inherited from Boethius' commentary on Porphyry, where you don't just say "these two things are the same kind or not," you can say *how far apart* two things are by counting steps up to their nearest common genus. "Man" and "horse" meet at "animal," so they're closer than "man" and "rock," which don't meet until "body," further up the tree. That graded distance, not a binary same/different, is what actually lets you tune a warning system, because it directly answers my "broad but not too noisy" requirement: **warning severity scales with tree distance, not with a flat mismatch check.**

```
Breakability & Weight      -> common ancestor 1 step up (Physicality)  -> no warning
Breakability & Social      -> common ancestor is Thing (the root)      -> strong warning
Breakability & Enchantment -> common ancestor is Thing                 -> warning, but maybe milder,
                              since "magic affecting physical objects" is a common IF pattern
```

That last line matters! I'll want some pairs pre-registered as *expected* cross-domain combinations (magic-affects-physicality is the entire premise of half of IF) even though they're taxonomically distant, so the tree gives you distance as a *default* signal, with room for explicit exceptions to the exception-detector itself.

This is what information retrieval (and, these days, machine learning) calls the **precision/recall tradeoff**: "broad" is asking for high recall (catch every real case of incoherence), "without false alarms" is asking for high precision (don't flag things that are fine), and those two pull against each other by nature. A flat same/different check forces you to pick one point on that curve for the whole language. A distance metric lets you set the *threshold*, such as warn only past N steps, or only when the nearest common ancestor is the root itself. This is a single tunable knob instead of an all-or-nothing rule, and one you can loosen or tighten after watching real authors hit false positives.

I came to realize something important: **This is what solves my extension problem without needing semantic understanding at all.** An author doesn't get to invent a free-floating tag. Instead, they *attach* their new domain somewhere in the existing tree (`domain Necromancy extends Magic.Curse`), the same way a new `kind` has to extend an existing one rather than appearing out of nowhere. The compiler never needs to understand what "Necromancy" *means*; it just needs to know where it sits in the graph, and tree-distance math does the rest. You get checkability for extensions "for free," without ever building the harder thing (an actual semantic verifier for author-invented taxonomy), because the check was never really about meaning. It was about *distance in a graph you already forced them to declare honestly.*

Once this settled in my mind, I found one more thing worth deciding before this all went too much further: **Does an extension domain have to attach to exactly one parent, or should it be allowed multiple parents**. This is clearly the same single-vs-multiple-inheritance question I already answered for `kind`, showing up again one level up in the domain taxonomy. Given the diamond-problem reasoning I settled on earlier, my initial instinct was that I want the same answer here, but I knew it was worth confirming that it's not a case where the tradeoffs actually cut differently for domains than they did for kinds.

### An Ontology by Another Name

It was at this point that I wondered if I was skirting the edges of ontologies and knowledge graphs here. I found it worth being precise about *which* part of that field I had backed into (if, in fact, I did so), because "ontology" gets used loosely and the specific corner I'm in has decades of formal tooling behind it.

What I've built is, structurally, a small fragment of a **description logic**: the formal system behind OWL (the Web Ontology Language) and the backbone of things like the Gene Ontology in biology or SNOMED CT in medicine. Description logics exist precisely to answer the questions I've been asking by instinct: given a hierarchy of classes (my `kind`s and `domain`s) and properties attached to them (my `trait`s), what can you *infer*? Is X a subtype of Y, do these two classes share an ancestor, is this combination of properties even coherent? The "distance to nearest common ancestor" move I just landed on has a formal name in that world too: it's the **least common subsumer**, and it's a standard ingredient in the semantic-similarity measures used over ontologies (Wu–Palmer and Resnik similarity are two well-known ones), for instance to estimate how closely "myocardial infarction" and "cardiac arrest" are related in SNOMED CT without a human manually deciding.

The reason this connection is worth taking seriously rather than treating as a cute parallel is that it means I don't have to invent my consistency-checking algorithms from scratch. Subsumption checking, taxonomy classification, detecting when a newly declared class creates an unsatisfiable combination of inherited properties: this is what OWL reasoners (Pellet, HermiT, ELK) do all day, and the underlying math (subsumption, satisfiability checking in description logic) is published, implemented, and battle-tested.

This being said, I knew that I likely don't want to *depend* on a full OWL reasoner inside a game compiler. That's some heavyweight machinery built for much larger, more adversarial ontologies than a single IF game's trait tree. However, knowing the field exists means that if my hand-rolled tree-distance heuristic ever hits a wall (say, if I ever allow multiple-parent domains and need proper lattice-based subsumption instead of simple tree distance), there's forty years of formal semantics to borrow from instead of re-deriving it by trial and error.

There's also a second branch of that same field worth flagging, because it's closer to my *rule* layer than my *taxonomy* layer: **knowledge graphs and their query/inference layer** (think RDF and SPARQL, or, from the older symbolic-AI world, Cyc's inference engine) are essentially what my `when/where/then` rules plus my `kind`/`trait` facts *are*, once compiled. Essentially, a graph of typed nodes and relations, with rules that fire based on graph patterns. Dialog's unification engine is, not coincidentally, structurally close to how SPARQL pattern-matches against a graph.

So, as I kept thinking things through, my whole language is converging on **ontology (the taxonomy/ is-a structure) + knowledge graph (the fact base) + production rules (the inference layer)**. This is, interestingly, close to the classic architecture of *expert systems* from the '80s (the OPS5 and CLIPS lineage again) crossed with modern Semantic Web tooling. So, perhaps I'm not skirting the edge of that field, but rather I'm basically reconstructing its core architecture bottom-up, from an IF-authoring problem instead of a medical-diagnosis or web-metadata problem. Which is itself a nice small piece of evidence that this architecture isn't domain-specific machinery. It's closer to a *general shape* that any system needing "typed facts + specificity-ranked inference over them" tends to converge on, IF included.

### Why Kind and Domain Stay Separate

Thus, my initial thought was, "Great! Single-parent domains, consistent with `kind`, locks down one of my last decisions." But I had to be intellectually honest here and recognize that "locks down" is doing more work than I've earned. Earlier I said it was worth confirming that the tradeoffs don't cut differently for domains than they did for kinds, and then I went off into ontologies instead of actually doing that. I'm going with single-parent mostly by analogy to `kind`, and the ontology detour even left the door open to multiple-parent domains later. There's at least one reason the answer might differ: something like "magic affecting physical objects" arguably belongs under both `Magic` and `Physicality`, and that's exactly the kind of case that pushes real ontologies toward multiple parents. For now, the pre-registered cross-domain pairs are what cover that case. So this is a provisional decision, not a confirmed one.

Taking that as settled for now, I had two parallel single-inheritance trees governing my language (`kind` and `domain`), with the rule-specificity ordering riding on top of both. All of which raises a question worth asking before I go further: should `kind` and `domain` actually be the *same* hierarchy wearing two hats, or are they deliberately orthogonal axes (an object's *type* vs. an object's *area of concern*)? Right now, as I am conceptualizing things, they're separate trees, which is probably right: `CursedChest` is a `kind` but its exceptions live in the `Magic` `domain`. However, it's worth stating explicitly why I'm keeping them apart, since someone reading my spec later (including future me!) might wonder why there are two hierarchies instead of one.

The reason for separate trees is that **`kind` answers "what is this?" and `domain` answers "what conversation is this behavior part of?"** Those are genuinely different axes, not two views of the same fact. A `CursedChest` doesn't stop being a `Container` just because its interesting behavior lives in the `Magic` domain. Conversely, `Magic` isn't a *kind* of anything, it doesn't have instances the way `Container` does. Collapsing them would be a category mistake in the other direction: forcing a classification axis (is-a) and a concern axis (about-what) into one hierarchy, the same way it would be a mistake to make "Physics" a subclass of "Textbook" just because physics textbooks exist.

This also happens to be a well-worn move, in ontology design and well before it. Library science has had **faceted classification** since S. R. Ranganathan in the 1930s, and ontology engineers make the same move when they keep independent classification axes in separate trees rather than tangling them into one (Alan Rector's "normalization" of ontologies is the standard reference), precisely because real-world things get sorted two independent ways at once (a book is classified by *subject* and separately by *format*, such as hardcover vs. paperback, and neither hierarchy should have to know about the other). My `kind`/`domain` split is that same faceted structure, arrived at from an IF angle instead of a library-science one.

One loose end before I move on. I opened this section with two questions, and I've only really answered the first: the sealed core with an extensible tree is my answer to closed versus open domains. The second, whether domain-crossing warnings should also flow through to `exception` blocks, never came back up. My instinct is that the tree-distance machinery would apply there unchanged, since an exception's condition names traits that live in domains just like a composition does. But I haven't worked out what that looks like in practice, or whether it would turn out too noisy, so it stays open.

## Starting to Spec!

At this point I felt that I had actually got the skeleton of a fairly complete language spec in mind: world declarations, typed actions with PDDL-style preconditions and effects, Dialog-style unification-based rule matching, structural specificity with source-order tiebreak, trait composition with domain-cohesion warnings, and a two-axis classification system. That's a real design document's worth of decisions. The next goal was to pull this into an actual structured spec so I had a single reference.

## Language Choice

There was the practical consideration on hand now: What language do I write this in? My thoughts were C or Python.

I immediately realized it was worth separating two different questions buried in "C or Python," because they pull toward different answers: **what should the compiler be written in** (a tool you run once per project, transforming source into a story file) versus **what should the language's own runtime be**. And for me, the second question barely exists. I'm not writing a new virtual machine; I'm targeting the Z-Machine and Glulx, which already have mature, cross-platform interpreters (Frotz, Gargoyle, Lectrote, and so on). So the entire cross-platform question collapses to: **what do I ship so that an author on Mac, Windows, or Linux can turn `.qd` files into a `.z8` or `.ulx` file?** That's a compiler-distribution problem, not a runtime-design problem. It's closer to "how do I ship `gcc`" than "how do I ship a game engine."

From working on other projects where I made similar decisions, I knew that the precedent is worth knowing before I decided, because both existing tools I'm drawing from already answered this question. Inform's compiler (and Dialog's `dialogc`) are both written in C. That's not incidental. It's the direct consequence of the distribution problem: a C compiler produces one small, dependency-free static binary per platform, which an author downloads and runs, with zero expectation they have any particular runtime installed.

Python can't give you that as cleanly. A `.py`-based compiler either requires the end user to have a compatible Python installed (fragile: which 3.x, which packages), or you package it with something like PyInstaller/Nuitka, which bundles an entire interpreter into a binary many times the size of an equivalent C tool, for what's conceptually a small program. Neither is fatal, but it's real friction for exactly the audience I'm building for: IF authors, who are used to Inform's "download one file, double-click, write your game" experience and won't tolerate much else.

Where Python genuinely wins, though, is implementation speed for the parts of this system that are algorithmically the hardest. My design, as worked out above, has three pieces that are unusually gnarly to hand-roll in C specifically: a **unification engine** (matching logic variables against a fact base, the Dialog-style rule dispatch), a **taxonomy walker** (kind/domain tree traversal, least-common-subsumer distance), and a **recursive-descent or PEG parser** for the surface syntax. All three are the kind of code that's maybe 200 lines in Python (using dictionaries, recursion, and garbage collection for free) and maybe 2,000 lines in C, where you're manually managing memory for backtracking search trees and building your own hash tables and string interning from scratch. This is a case where, in my view, the language's convenience directly trades against the tool's distributability. There isn't a free lunch.

### C or Rust?

That tension is exactly what a third option is built to resolve, and I started to seriously weigh it before defaulting to the historical C precedent: Rust. This is a language that, in the past, I have tried and struggled to come to grips with. But I knew the following:

- **Compiles to a single static binary per platform**, same distribution story as C. An author still just downloads one file.
- **Algebraic data types and pattern matching** map almost embarrassingly well onto what I'm building: an AST is a natural `enum`, a `kind` tree is a natural recursive `enum` or tree structure, and matching a rule's `when` clause against a fact is a `match` expression doing exactly the job Python's dict-lookups would do, but checked at compile time.
- **Memory safety without a garbage collector** matters more than it might sound here, because a unification engine's core operation of backtracking through partial variable bindings and undoing them on failure is precisely the kind of manual-memory-lifecycle code where C invites use-after-free and double-free bugs. Rust's borrow checker is annoying to fight at first, but it is specifically good at catching bugs in "structure with pointers that gets partially undone and retried" code, which is what a Prolog-style solver *is*.
- There's existing prior art worth a look: unification/logic-engine crates already exist in the Rust ecosystem, and I wouldn't be starting from zero on that piece.

The honest tradeoff against Rust is my own ramp-up time. I know C well from decades of systems work, and Rust's ownership model has a real learning curve even for experienced C programmers, though (so I'm told) usually a matter of weeks, not months, for someone with my background. I have to weigh that against Python's near-zero ramp-up but real distribution cost, and C's familiar-but-labor-intensive implementation of the unification/backtracking logic.

I thus started to lean this way: C for fidelity to the tools I'm directly descended from and lowest implementation risk given my existing fluency with it, Rust if I'm willing to spend a couple of weeks on the learning curve in exchange for a codebase that's dramatically safer to write the unification engine in, and Python only if this is explicitly a prototype I intend to throw away. The latter being genuinely useful for validating the rule-dispatch and specificity logic on paper before committing to a systems language, the same way I would sketch a proof before formalizing it.

That actually settled it pretty cleanly for me: the moment other authors are the audience, distribution stops being a minor concern and becomes close to the dominant constraint, since my users are writers who chose Inform and Dialog precisely because "download one file, run it, done" is the barrier to entry they expect. Python is effectively out on its own merits at that point. Not because it's a bad language, but because every packaging path (bundled interpreter, "please install Python 3.11+ first," a pip package) adds friction that Inform and Dialog authors have never had to tolerate.

Given that, the real decision is C vs. Rust, and for a tool meant to last and to accumulate outside contributors over time (which is the implicit trajectory once I'm distributing to a community, the way Inform and Dialog both eventually did) I kept leaning toward Rust. And this was for a reason beyond what I said earlier: community contribution surface. If this project succeeds the way Inform or Dialog did, I would eventually get pull requests, bug reports about edge cases in the unification engine, and other people reading and modifying my backtracking/rule-dispatch code. A memory-safety bug in that code in C is the kind of bug that's genuinely hard to track down. A rule fails to backtrack cleanly, corrupts a binding, and the symptom shows up three function calls later in an unrelated part of the compiler. And it's exactly the kind of bug an outside contributor is most likely to introduce by accident, since backtracking-with-manual-memory is subtle even for experienced C programmers. Rust's compiler catches a large fraction of that class of bug *before* it ships, which matters disproportionately once I'm not the only person touching the code.

The concrete costs here were worth being honest about:

- **My ramp-up time**. Real, but bounded (weeks, given my background, not months), and front-loaded rather than recurring.
- **Compile times** are slower than C's, which mostly matters to me as the maintainer, not to end users who only ever touch the finished binary.
- **Fewer IF-adjacent examples to crib from**. Inform and Dialog are both C, so I won't find as much directly-relevant prior art in Rust for things like Z-Machine bit-packing. I would be translating C-shaped solutions rather than finding Rust ones ready-made.

None of those, I felt, outweighed the safety and maintainability case once "other people will read and edit this code" is a real design constraint rather than a hypothetical.

### Pipeline and Backends

Two things worth deciding next, now that the host language was roughly settled: do I want a single monolithic binary (parse → typecheck → unify-and-compile rules → emit bytecode, all in one pass structure) or a pipeline of discrete stages with an intermediate representation between them (closer to how a traditional compiler, or Inform 7 emitting Inform 6 as an intermediate step, is structured, which makes debugging and testing each stage independently much easier)? And separately: do I want to target the Z-Machine first, Glulx first, or design the intermediate representation to be backend-agnostic from day one so both are equally supported?

In thinking about this, I felt the pipeline makes sense. On the idea of supporting one or both, that also brought up another interesting question: ZIL. Or, rather, ZILF. This is written in C# and it's a compiler that started by just generating Z-Machine and then added support for Glulx.

In fact, ZILF is a genuinely good data point to bring in here, and it's even more interesting than "started Z-Machine, added Glulx" suggests once you look at the actual timeline: ZILF shipped in 2009, targeted the Z-Machine exclusively, and Glulx support only arrived as *experimental* around ZILF 1.0, roughly fifteen years later. And it didn't stop there: version 1.8 added a third target, Cornerstone (the virtual machine behind Infocom's 1985 database product), via yet another separate assembler (Chisel). So this isn't really a story of "backend #2 slotted in cleanly." It's a story of a single-backend compiler running for a decade and a half before the second backend showed up, which tells me the retrofit wasn't trivial. (Or perhaps just wasn't as desired. I don't know.)

As I thought about all that, I realized there's a second, even more pointed precedent sitting right next to it: Inform 6 has almost the identical arc, but with an extra twist. Inform 6 also originally targeted the Z-Machine only. Glulx support didn't come from the original compiler team (mainly Graham Nelson) retrofitting their own code. It came from Andrew Plotkin building an unofficial forked version of Inform 6 that retargeted it to his newly-designed Glulx VM, and that fork only got merged back into the official compiler in version 6.3, years later. The abstraction boundary that made dual-backend support possible wasn't designed in advance by the original authors; it was reverse-engineered by an outside developer who had to cut through the existing code generator to find it. Inform 7, by contrast, got Glulx support within a few months of its own 2006 release. But only because by the time I7 existed, it was compiling down to I6 as an intermediate step, and I6 by then already had the abstraction sorted out. I7 inherited backend-flexibility for free by sitting on top of a layer that had already paid the retrofit cost.

That's the actual lesson worth taking from both histories, and it's more specific than "design for two backends from day one." Which is good advice in principle but hard to execute correctly on the first try, since even Plotkin needed a fork to find the right seams. The concrete, actionable version is (or seems to be) this: **whatever you build first, keep code generation as a strictly separate pass operating over your own intermediate representation, never interleaved with the semantic analysis or rule-compilation logic.**

Both ZILF's slow retrofit and Inform 6's fork-and-merge-back suggest the original pain wasn't "we didn't think about Glulx" necessarily. It was that target-specific instruction emission had crept into the parts of the compiler that were supposed to be about *meaning*, not *encoding*, so pulling them apart later meant touching code far from the code generator itself. I already landed on a pipeline architecture instead of a monolithic one, so this is the argument for taking that one step further: make the final "lower this IR into VM-specific bytecode" stage genuinely swappable, even if I only write one implementation of it at first. I get most of the benefit of designing for two backends without having to correctly guess the abstraction boundary before I've built anything real, which is the part that actually broke for the people before me.

There is one more detail worth noting from ZILF and Inform 6 both, since it's a concrete design option for my own final pipeline stage: neither compiler goes straight from semantics to raw bytes. ZILF emits Z-Machine or Glulx *assembly text*, then hands that off to a separate assembler binary (ZAPF, or Glazer) to produce the actual story file. Inform 6 keeps a similar split internally, with its code generator feeding a built-in assembler, though without a text format in between. That's a deliberate IR choice (assembly-as-text as the boundary between "decide what the code should do" and "encode it as bytes") and it buys you something real: you can eyeball, diff, and unit-test the codegen stage's output as readable text rather than a binary blob. That matters a lot to me for a compiler that I intend to have outside contributors debugging. The cost is an extra format to design and an extra tool in the pipeline. Thus, it's worth deciding now whether I want that separation or a single-stage emitter that writes bytes straight out.

Finally, also worth flagging, since it bears directly on all of this: ZILF is written in C#, and has traditionally needed the .NET runtime or Mono to run. That's a live, real example of exactly the distribution friction I decided to avoid by ruling out Python. .NET's NativeAOT publishing can now produce a standalone native binary, which removes most of that friction. It hasn't stopped ZILF from having a real user base, so it's not disqualifying in practice. But it's a useful confirmation that the C/Rust call wasn't overly cautious: the fix for .NET's distribution story was, in effect, to make it behave the way C and Rust binaries already do.
