# Specifications

This directory holds reference copies of the specifications that Quiddity
implements. They are here so that citations in source comments, written as
`Spec §3.5.3`, resolve to a file in the checkout, and so the documents can be
read offline. A citation `§N.N.N` refers to the clause with id `N.N.N` in the
corresponding `sectNN.html`.

Nothing in this directory is covered by Quiddity's GPL. Each subdirectory
carries its own LICENSE and NOTICE, and `specs/` is marked `export-ignore`
in `.gitattributes`, so release archives built with `git archive` omit it.

## zmachine/

The Z-Machine Standards Document, version 1.1 (24 February 2014), by Graham
Nelson, with revisions by Kevin Bracey, Jason C. Penney, and David Fillmore.
The canonical copy is at
<https://www.inform-fiction.org/zmachine/standards/current/>.

The files here are the built output of a restyled edition maintained at
<https://github.com/jeffnyman/z-machine-standard> and published at
<https://jeffnyman.github.io/z-machine-standard/>. That edition adds
navigation, search, a permalink for every clause, and editor's notes. It
corrects a small number of clause-numbering defects in the 2014 source and
alters no normative text. The corrections are listed in the revision note at
the end of `preface.html`. Quiddity's citations follow this edition's
numbering, which differs from inform-fiction.org only in §7.6.1, §8.8.3.2,
and §13.3.

Also included, because the Standard refers to them: `quetzal.html`
(Quetzal 1.4, Martin Frost) and `blorb.html` (Blorb 2.0.4, Andrew Plotkin,
with the unofficial BPal addendum by Chris Spiegel).

Copied from upstream commit `d019507` (17 August 2026). To refresh, check out
the upstream repository and copy its root `*.html` files, `assets/`, `images/`,
`LICENSE`, and `NOTICE` over this directory. The generator in `tools/`, the
pristine 2014 sources in `source/`, and the editor's note sources in
`content/` are deliberately not copied.

The specification texts carry no explicit licence and are reproduced without
any claim of licence or ownership. The stylesheet, scripts, editor's notes,
and SVG title letters are the upstream editor's work under the MIT licence in
`zmachine/LICENSE`. See `zmachine/NOTICE` for full attribution.
