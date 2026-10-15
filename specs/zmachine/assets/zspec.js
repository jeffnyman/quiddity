/*
 * zspec.js — progressive enhancement for the Z-Machine Standards Document.
 *
 * Everything here is additive: with JavaScript disabled every page still
 * renders completely, the sidebar still works, and every link still resolves.
 *
 * No dependencies, and no fetch() — the search index is pulled in by injecting
 * a <script> tag, which works from file:// where fetch() is blocked by CORS.
 */
(function () {
  'use strict';

  var $ = function (sel, root) { return (root || document).querySelector(sel); };
  var $$ = function (sel, root) {
    return Array.prototype.slice.call((root || document).querySelectorAll(sel));
  };

  /* ------------------------------------------------------------ theme */

  var store = {
    get: function (k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set: function (k, v) { try { localStorage.setItem(k, v); } catch (e) { /* file:// */ } },
  };

  function systemDark() {
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  }

  var themeToggle = $('.theme-toggle');
  if (themeToggle) {
    themeToggle.addEventListener('click', function () {
      var current = document.documentElement.dataset.theme;
      var isDark = current ? current === 'dark' : systemDark();
      var next = isDark ? 'light' : 'dark';
      document.documentElement.dataset.theme = next;
      store.set('zspec-theme', next);
      themeToggle.setAttribute('aria-label',
        next === 'dark' ? 'Switch to light theme' : 'Switch to dark theme');
    });
  }

  /* ------------------------------------------------- mobile nav drawer */

  var sidebar = $('#sidebar');
  var navToggle = $('.nav-toggle');
  if (sidebar && navToggle) {
    var setNav = function (open) {
      sidebar.classList.toggle('is-open', open);
      document.body.classList.toggle('nav-open', open);
      navToggle.setAttribute('aria-expanded', String(open));
      navToggle.setAttribute('aria-label', open ? 'Hide contents' : 'Show contents');
    };
    navToggle.addEventListener('click', function () {
      setNav(!sidebar.classList.contains('is-open'));
    });
    document.addEventListener('click', function (e) {
      if (!sidebar.classList.contains('is-open')) return;
      if (sidebar.contains(e.target) || navToggle.contains(e.target)) return;
      setNav(false);
    });
    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && sidebar.classList.contains('is-open')) setNav(false);
    });
    /* Keep the current page visible in a long sidebar. */
    var cur = $('.nav-list .is-current');
    if (cur && sidebar.scrollHeight > sidebar.clientHeight) {
      var top = cur.offsetTop - sidebar.clientHeight / 2;
      if (top > 0) sidebar.scrollTop = top;
    }
  }

  /* ------------------------------------------------------ copy links */

  document.addEventListener('click', function (e) {
    var btn = e.target.closest && e.target.closest('.copy-link');
    if (!btn) return;
    var url = location.href.split('#')[0] + '#' + btn.dataset.anchor;
    var done = function () {
      btn.classList.add('is-copied');
      btn.style.opacity = '1';
      setTimeout(function () {
        btn.classList.remove('is-copied');
        btn.style.opacity = '';
      }, 1400);
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(url).then(done, function () { location.hash = btn.dataset.anchor; });
    } else {
      location.hash = btn.dataset.anchor;
    }
  });

  /* ------------------------------------------------- editor's note toggle */

  /*
   * Editor's notes are commentary, not specification. Anyone reading for
   * implementation can switch them off, and the choice follows them across
   * pages. The control only exists where there is something to toggle.
   */
  (function () {
    if (!document.querySelector('.editor-note')) return;

    var hidden = store.get('zspec-notes') === 'off';
    document.body.classList.toggle('hide-notes', hidden);

    var btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'icon-button notes-toggle';
    btn.innerHTML =
      '<svg viewBox="0 0 20 20" aria-hidden="true">' +
      '<path d="M4 3h9l3 3v11H4z"/><path d="M13 3v3h3"/>' +
      '<path d="M7 10h6M7 13h4"/></svg>';

    var sync = function () {
      var showing = !document.body.classList.contains('hide-notes');
      btn.setAttribute('aria-pressed', String(showing));
      btn.setAttribute('aria-label', showing ? "Hide editor's notes" : "Show editor's notes");
      btn.title = showing ? "Hide editor's notes" : "Show editor's notes";
    };
    sync();

    btn.addEventListener('click', function () {
      var nowHidden = !document.body.classList.contains('hide-notes');
      document.body.classList.toggle('hide-notes', nowHidden);
      store.set('zspec-notes', nowHidden ? 'off' : 'on');
      sync();
    });

    var actions = $('.topbar-actions');
    if (actions) actions.insertBefore(btn, actions.firstChild);
  })();

  /* -------------------------------------------------- on-this-page outline */

  /*
   * The outline itself is generated at build time, so it works without
   * JavaScript. All that is added here is highlighting of the section
   * currently in view.
   */
  var outline = $('#outline');
  if (outline && 'IntersectionObserver' in window) {
    var links = [];
    $$('a[href^="#"]', outline).forEach(function (a) {
      var el = document.getElementById(decodeURIComponent(a.getAttribute('href').slice(1)));
      if (el) links.push({ li: a.parentNode, el: el });
    });

    if (links.length) {
      var active = null;
      var setActive = function (entry) {
        if (active === entry) return;
        if (active) active.li.classList.remove('is-active');
        active = entry;
        if (!active) return;
        active.li.classList.add('is-active');
        var r = active.li.getBoundingClientRect();
        var o = outline.getBoundingClientRect();
        if (r.top < o.top || r.bottom > o.bottom) {
          active.li.scrollIntoView({ block: 'nearest' });
        }
      };

      var visible = new Set();
      var io = new IntersectionObserver(function (entries) {
        entries.forEach(function (en) {
          if (en.isIntersecting) visible.add(en.target); else visible.delete(en.target);
        });
        var best = null;
        links.forEach(function (l) {
          if (visible.has(l.el) && (!best || l.el.offsetTop < best.el.offsetTop)) best = l;
        });
        if (best) setActive(best);
      }, { rootMargin: '-15% 0px -70% 0px', threshold: 0 });
      links.forEach(function (l) { io.observe(l.el); });
    }
  }

  /* ----------------------------------------------- opcode table filters */

  $$('.table-scroll').forEach(function (scroll) {
    var table = scroll.querySelector('table.opcodes');
    if (!table) return;
    var rows = $$('tbody tr', table);
    if (rows.length < 12) return;

    var bar = document.createElement('div');
    bar.className = 'table-toolbar';
    bar.setAttribute('data-generated', '');

    var cap = table.querySelector('caption');
    var name = cap ? cap.textContent.trim() : 'opcodes';

    var input = document.createElement('input');
    input.type = 'search';
    input.placeholder = 'Filter ' + name.toLowerCase() + ' by name, hex or ZIL…';
    input.setAttribute('aria-label', 'Filter ' + name);
    bar.appendChild(input);

    var versions = ['1', '2', '3', '4', '5', '6', '7', '8'];
    var activeV = null;
    var chips = versions.map(function (v) {
      var c = document.createElement('button');
      c.type = 'button';
      c.className = 'chip';
      c.textContent = 'V' + v;
      c.setAttribute('aria-pressed', 'false');
      c.addEventListener('click', function () {
        activeV = activeV === v ? null : v;
        chips.forEach(function (x) {
          x.setAttribute('aria-pressed', String(x.textContent === 'V' + activeV));
        });
        apply();
      });
      bar.appendChild(c);
      return c;
    });

    var count = document.createElement('span');
    count.className = 'filter-count';
    count.setAttribute('aria-live', 'polite');
    bar.appendChild(count);

    /* Column 5 ("V") holds the earliest version, sometimes as "5/3" or "4+". */
    function matchesVersion(row, v) {
      var cells = row.cells;
      if (cells.length < 5) return true;
      var raw = (cells[4].textContent || '').trim();
      if (!raw) return true;
      var nums = raw.match(/\d/g);
      if (!nums) return true;
      return nums.some(function (n) { return Number(v) >= Number(n); });
    }

    function apply() {
      var q = input.value.trim().toLowerCase();
      var shown = 0;
      rows.forEach(function (row) {
        if (row.classList.contains('is-rule')) {
          row.classList.toggle('is-filtered-out', !!q || !!activeV);
          return;
        }
        var text = row.textContent.toLowerCase();
        var ok = (!q || text.indexOf(q) !== -1) && (!activeV || matchesVersion(row, activeV));
        row.classList.toggle('is-filtered-out', !ok);
        if (ok) shown++;
      });
      var total = rows.filter(function (r) { return !r.classList.contains('is-rule'); }).length;
      count.textContent = (q || activeV) ? shown + ' of ' + total : total + ' opcodes';
    }

    input.addEventListener('input', apply);
    scroll.parentNode.insertBefore(bar, scroll);
    apply();
  });

  /* ------------------------------------------- §15 dictionary filter */

  var dictEntries = $$('.dict-entry');
  if (dictEntries.length > 20) {
    var azBar = $('.az-bar');
    var dbar = document.createElement('div');
    dbar.className = 'table-toolbar';
    dbar.setAttribute('data-generated', '');
    var dinput = document.createElement('input');
    dinput.type = 'search';
    dinput.placeholder = 'Filter ' + dictEntries.length + ' opcode entries…';
    dinput.setAttribute('aria-label', 'Filter opcode entries');
    var dcount = document.createElement('span');
    dcount.className = 'filter-count';
    dcount.setAttribute('aria-live', 'polite');
    dbar.appendChild(dinput);
    dbar.appendChild(dcount);
    if (azBar) azBar.parentNode.insertBefore(dbar, azBar);

    dinput.addEventListener('input', function () {
      var q = dinput.value.trim().toLowerCase();
      var shown = 0;
      dictEntries.forEach(function (s) {
        var h2 = s.querySelector('h2');
        var nameMatch = h2 && h2.textContent.toLowerCase().indexOf(q) !== -1;
        var ok = !q || nameMatch || s.textContent.toLowerCase().indexOf(q) !== -1;
        s.classList.toggle('is-filtered-out', !ok);
        if (ok) shown++;
      });
      dcount.textContent = q ? shown + ' of ' + dictEntries.length : '';
      if (azBar) azBar.style.display = q ? 'none' : '';
    });
  }

  /* --------------------------------------------------------- search */

  var dialog = null;
  var input2 = null;
  var resultsEl = null;
  var indexLoading = false;
  var indexReady = false;
  var activeIdx = -1;
  var current = [];

  function buildDialog() {
    dialog = document.createElement('dialog');
    dialog.className = 'search-dialog';
    dialog.innerHTML =
      '<div class="search-head">' +
        '<svg viewBox="0 0 20 20" aria-hidden="true"><circle cx="9" cy="9" r="6"/><path d="M13.5 13.5 18 18"/></svg>' +
        '<input id="search-input" type="search" placeholder="Search the standard…" ' +
          'autocomplete="off" spellcheck="false" aria-label="Search the standard">' +
      '</div>' +
      '<ul class="search-results" role="listbox" aria-label="Search results"></ul>' +
      '<div class="search-foot"><span><kbd>&uarr;</kbd><kbd>&darr;</kbd> navigate</span>' +
        '<span><kbd>&crarr;</kbd> open</span><span><kbd>esc</kbd> close</span>' +
        '<span style="margin-left:auto">Tip: type a clause number, e.g. 8.8.3</span></div>';
    document.body.appendChild(dialog);
    input2 = $('#search-input', dialog);
    resultsEl = $('.search-results', dialog);

    input2.addEventListener('input', function () { runSearch(input2.value); });
    input2.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
      else if (e.key === 'Enter') {
        var a = resultsEl.querySelector('.is-active a');
        if (a) { e.preventDefault(); location.href = a.href; dialog.close(); }
      }
    });
    dialog.addEventListener('click', function (e) { if (e.target === dialog) dialog.close(); });
  }

  function loadIndex(then) {
    if (indexReady) { then(); return; }
    if (indexLoading) return;
    indexLoading = true;
    /* A <script> tag, not fetch(): this must work from file:// too. */
    var s = document.createElement('script');
    s.src = 'assets/search-index.js';
    s.onload = function () { indexReady = true; indexLoading = false; then(); };
    s.onerror = function () {
      indexLoading = false;
      resultsEl.innerHTML = '<li class="search-empty">Search index not found. ' +
        'Run <code>node tools/build.mjs index</code>.</li>';
    };
    document.head.appendChild(s);
  }

  function openSearch() {
    if (!dialog) buildDialog();
    if (!dialog.open) dialog.showModal();
    input2.focus();
    input2.select();
    resultsEl.innerHTML = '<li class="search-empty">Loading index…</li>';
    loadIndex(function () { runSearch(input2.value); });
  }

  function move(delta) {
    var items = $$('li', resultsEl).filter(function (li) { return li.querySelector('a'); });
    if (!items.length) return;
    if (activeIdx >= 0 && items[activeIdx]) items[activeIdx].classList.remove('is-active');
    activeIdx = (activeIdx + delta + items.length) % items.length;
    items[activeIdx].classList.add('is-active');
    items[activeIdx].scrollIntoView({ block: 'nearest' });
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  function highlight(text, terms) {
    var out = escapeHtml(text);
    terms.forEach(function (t) {
      if (t.length < 2) return;
      var re = new RegExp('(' + t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'ig');
      out = out.replace(re, '<mark>$1</mark>');
    });
    return out;
  }

  function snippet(text, terms) {
    var lower = text.toLowerCase();
    var at = -1;
    for (var i = 0; i < terms.length; i++) {
      var k = lower.indexOf(terms[i]);
      if (k !== -1 && (at === -1 || k < at)) at = k;
    }
    if (at === -1) return text.slice(0, 160);
    var start = Math.max(0, at - 60);
    return (start ? '…' : '') + text.slice(start, start + 200);
  }

  function runSearch(query) {
    if (!indexReady) return;
    var q = query.trim();
    activeIdx = -1;
    if (!q) {
      resultsEl.innerHTML = '<li class="search-empty">Search clause numbers, opcode names, or any text.</li>';
      return;
    }
    var data = window.ZSPEC_INDEX;
    var terms = q.toLowerCase().split(/\s+/).filter(Boolean);
    var isNum = /^[\d.]+$/.test(q);
    var scored = [];

    data.records.forEach(function (r) {
      /* r = [pageIndex, id, label, text] */
      var label = (r[2] || '').toLowerCase();
      var text = (r[3] || '').toLowerCase();
      var score = 0;

      if (isNum) {
        if (label === q) score += 1000;
        else if (label.indexOf(q) === 0) score += 500;
        else if (label.indexOf(q) !== -1) score += 120;
      }
      terms.forEach(function (t) {
        if (label === t) score += 400;
        else if (label.indexOf(t) === 0) score += 160;
        else if (label.indexOf(t) !== -1) score += 60;
        var wordRe = new RegExp('\\b' + t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i');
        if (wordRe.test(text)) score += 30;
        else if (text.indexOf(t) !== -1) score += 8;
      });
      /* Every term must appear somewhere. */
      var all = terms.every(function (t) {
        return label.indexOf(t) !== -1 || text.indexOf(t) !== -1;
      });
      if (all && score > 0) scored.push([score, r]);
    });

    scored.sort(function (a, b) { return b[0] - a[0]; });
    current = scored.slice(0, 40);

    if (!current.length) {
      resultsEl.innerHTML = '<li class="search-empty">No matches for &ldquo;' +
        escapeHtml(q) + '&rdquo;.</li>';
      return;
    }

    resultsEl.innerHTML = current.map(function (pair) {
      var r = pair[1];
      var page = data.pages[r[0]];
      var href = page[0] + (r[1] ? '#' + r[1] : '');
      return '<li><a href="' + escapeHtml(href) + '">' +
        '<span class="search-meta">' +
          (r[2] ? '<span class="search-num">' + escapeHtml(r[2]) + '</span>' : '') +
          '<span>' + escapeHtml(page[1]) + '</span>' +
        '</span>' +
        '<span class="search-snippet">' + highlight(snippet(r[3] || '', terms), terms) + '</span>' +
        '</a></li>';
    }).join('');
    move(1);
  }

  $$('[data-search-open]').forEach(function (b) {
    b.addEventListener('click', openSearch);
  });

  document.addEventListener('keydown', function (e) {
    var tag = (e.target.tagName || '').toLowerCase();
    var typing = tag === 'input' || tag === 'textarea' || e.target.isContentEditable;
    if ((e.key === '/' && !typing) || ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k')) {
      e.preventDefault();
      openSearch();
    }
  });

  /* Briefly highlight the clause an incoming deep link points at. */
  function flashTarget() {
    if (!location.hash) return;
    var el = document.getElementById(decodeURIComponent(location.hash.slice(1)));
    if (!el) return;
    var section = el.closest ? (el.closest('.clause-section') || el) : el;
    section.animate
      && section.animate(
        [{ backgroundColor: 'var(--accent-soft)' }, { backgroundColor: 'transparent' }],
        { duration: 1600, easing: 'ease-out' },
      );
  }
  window.addEventListener('hashchange', flashTarget);
  flashTarget();
})();
