/*
 * nav.js — the document's table of contents, in one place.
 *
 * Loaded as a plain <script> (not fetched), so it works from file:// as well as
 * over HTTP. tools/build.mjs reads this same file to generate the static
 * sidebar baked into every page and the prev/next footer links.
 *
 * Edit here, then run:  node tools/build.mjs nav
 */
window.ZSPEC_NAV = [
  {
    group: null,
    items: [
      { file: 'preface.html', title: 'Preface' },
      { file: 'overview.html', title: 'Overview of Z-machine architecture', short: 'Overview' },
    ],
  },
  {
    group: 'Fundamentals',
    items: [
      { file: 'sect01.html', num: '1', title: 'The memory map' },
      { file: 'sect02.html', num: '2', title: 'Numbers and arithmetic' },
      { file: 'sect03.html', num: '3', title: 'How text and characters are encoded', short: 'Text and characters' },
      { file: 'sect04.html', num: '4', title: 'How instructions are encoded', short: 'Instruction encoding' },
      { file: 'sect05.html', num: '5', title: 'How routines are encoded', short: 'Routine encoding' },
      { file: 'sect06.html', num: '6', title: 'The game state: storage and routine calls', short: 'The game state' },
    ],
  },
  {
    group: 'Input/Output',
    items: [
      { file: 'sect07.html', num: '7', title: 'Output streams and file handling', short: 'Output streams' },
      { file: 'sect08.html', num: '8', title: 'The screen model' },
      { file: 'sect09.html', num: '9', title: 'Sound effects' },
      { file: 'sect10.html', num: '10', title: 'Input streams and devices', short: 'Input streams' },
    ],
  },
  {
    group: 'Tables',
    items: [
      { file: 'sect11.html', num: '11', title: 'The format of the header', short: 'The header' },
      { file: 'sect12.html', num: '12', title: 'The object table' },
      { file: 'sect13.html', num: '13', title: 'The dictionary and lexical analysis', short: 'Dictionary and lexing' },
    ],
  },
  {
    group: 'Instruction Set',
    items: [
      { file: 'sect14.html', num: '14', title: 'Complete table of opcodes', short: 'Table of opcodes',
        extra: { href: 'sect14.html#inform', text: 'with Inform assembly syntax' } },
      { file: 'sect15.html', num: '15', title: 'Dictionary of opcodes' },
    ],
  },
  {
    group: 'An Unusual Font',
    items: [
      { file: 'sect16.html', num: '16', title: 'Font 3 and character graphics', short: 'Font 3' },
    ],
  },
  {
    group: 'Appendices',
    items: [
      { file: 'appa.html', num: 'A', title: 'Error messages and debugging' },
      { file: 'appb.html', num: 'B', title: 'Conventional contents of the header', short: 'Header conventions' },
      { file: 'appc.html', num: 'C', title: 'Resources available (with WWW links)', short: 'Resources available' },
      { file: 'appd.html', num: 'D', title: 'A short history of the Z-machine', short: 'History' },
      { file: 'appe.html', num: 'E', title: 'Statistics' },
      { file: 'appf.html', num: 'F', title: 'Canonical story files' },
    ],
  },
  {
    group: 'Related standards',
    note: 'Mirrored in this repository',
    items: [
      { file: 'quetzal.html', title: 'Quetzal: saved-game format', short: 'Quetzal' },
      {
        file: 'blorb.html', title: 'Blorb: resource collection format', short: 'Blorb',
        extra: { href: 'blorb.html#bpal', text: 'with the BPal addendum' },
      },
    ],
  },
];

/* Related work that lives elsewhere - listed on the contents page only. */
window.ZSPEC_ELSEWHERE = [
  {
    href: 'http://babel.ifarchive.org/',
    title: 'The Treaty of Babel',
    note: 'Software and a standard for IF bibliography',
  },
  {
    href: 'https://www.inform-fiction.org/zmachine/standards/current/',
    title: 'The canonical copy of this standard',
    note: 'inform-fiction.org',
  },
];
