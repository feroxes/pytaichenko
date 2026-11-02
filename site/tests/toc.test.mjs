import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { loadScripts, readPage, readSite } from './helpers.mjs';

function mainSections(page) {
  const main = /<main>([\s\S]*?)<\/main>/.exec(page)[1];
  return [...main.matchAll(/<section\b[^>]*\bid="([^"]+)"[^>]*>\s*(?:<h[12] data-i18n="([^"]+)")?/g)].map((m) => ({ id: m[1], key: m[2] }));
}

function tocLinks(page) {
  const nav = /<nav\b[^>]*class="toc"[^>]*>([\s\S]*?)<\/nav>/.exec(page);
  assert.ok(nav, 'the page has a contents block');
  return [...nav[1].matchAll(/<a href="#([^"]+)"[^>]*data-i18n="([^"]+)"[^>]*>/g)].map((m) => ({ id: m[1], key: m[2] }));
}

test('the contents lists every section of the page in page order, named like its heading', () => {
  const page = readPage();
  const sections = mainSections(page);
  const links = tocLinks(page);
  assert.deepEqual(links.map((l) => l.id), sections.map((s) => s.id));
  for (const link of links) {
    const section = sections.find((s) => s.id === link.id);
    if (link.id === 'about') assert.equal(link.key, 'toc.about');
    else assert.equal(link.key, section.key, link.id);
  }
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TEXT.uk['toc.title'], 'Зміст');
  assert.equal(TEXT.en['toc.title'], 'Contents');
  assert.equal(TEXT.uk['toc.about'], 'Про колонку');
  assert.equal(TEXT.en['toc.about'], 'About');
});

test('the contents is a labelled navigation that opens on its own and loads its script after the language', () => {
  const page = readPage();
  const nav = /<nav\b[^>]*class="toc"[^>]*>/.exec(page)[0];
  assert.match(nav, /aria-label="Зміст"/);
  assert.match(nav, /data-i18n-attr="aria-label:toc.title"/);
  assert.match(page, /<details class="toc-box" data-toc open>\s*<summary data-i18n="toc.title">Зміст<\/summary>/);
  const scripts = [...page.matchAll(/<script src="assets\/([^"]+)"/g)].map((m) => m[1]);
  assert.ok(scripts.indexOf('toc.js') > scripts.indexOf('i18n.js'));
  assert.ok(scripts.indexOf('toc.js') > scripts.indexOf('config.js'));
});

test('the contents sits beside the text on wide screens and behind a button on narrow ones', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /\.toc \{[^}]*position: fixed;/);
  const wide = /@media \(min-width: 1280px\) \{([\s\S]*?)\n\}/.exec(css);
  assert.ok(wide, 'a wide-screen rule exists');
  assert.match(wide[1], /\.toc summary \{[^}]*display: none;/);
  assert.match(wide[1], /\.toc \{[^}]*left: calc\(50% \+ 434px\);/);
  assert.match(css, /\.toc a\[aria-current\]/);
});

function makeToc({ wide = false, hidden = [], intersection = true } = {}) {
  const listeners = {};
  const on = (target) => (type, fn) => {
    (target.handlers[type] = target.handlers[type] || []).push(fn);
  };
  const ids = ['about', 'can', 'parts', 'support'];
  const sections = Object.fromEntries(ids.map((id) => [id, { id, hidden: hidden.includes(id) }]));
  const links = ids.map((id) => {
    const item = { hidden: false };
    const link = {
      handlers: {},
      attrs: {},
      parentNode: item,
      getAttribute: (name) => (name === 'href' ? `#${id}` : link.attrs[name] ?? null),
      setAttribute: (name, value) => {
        link.attrs[name] = String(value);
      },
      removeAttribute: (name) => {
        delete link.attrs[name];
      },
    };
    link.addEventListener = on(link);
    return link;
  });
  const box = { open: true, querySelectorAll: () => links };
  const media = { matches: wide, handlers: {} };
  media.addEventListener = on(media);
  const observers = [];
  const document = {
    readyState: 'complete',
    addEventListener(type, fn) {
      listeners[type] = fn;
    },
    querySelector: (selector) => (selector === '[data-toc]' ? box : null),
    getElementById: (id) => sections[id] || null,
  };
  const window = { document, matchMedia: () => media };
  if (intersection) {
    window.IntersectionObserver = class {
      constructor(callback, options) {
        this.callback = callback;
        this.options = options;
        this.targets = [];
        observers.push(this);
      }
      observe(target) {
        this.targets.push(target);
      }
    };
  }
  window.window = window;
  vm.runInNewContext(readSite('assets/toc.js'), { window, document }, { filename: 'toc.js' });
  return { box, links, media, sections, observers };
}

test('the contents hides the entry of a hidden section', () => {
  const { links } = makeToc({ hidden: ['support'] });
  assert.deepEqual(links.map((l) => l.parentNode.hidden), [false, false, false, true]);
});

test('on a narrow screen the contents starts closed and closes after a jump; on a wide one it stays open', () => {
  const narrow = makeToc({ wide: false });
  assert.equal(narrow.box.open, false);
  narrow.box.open = true;
  narrow.links[2].handlers.click[0]({});
  assert.equal(narrow.box.open, false);

  const wide = makeToc({ wide: true });
  assert.equal(wide.box.open, true);
  wide.links[2].handlers.click[0]({});
  assert.equal(wide.box.open, true);

  narrow.media.matches = true;
  narrow.media.handlers.change[0]({});
  assert.equal(narrow.box.open, true);
});

test('the section being read is marked in the contents', () => {
  const { links, sections, observers } = makeToc({ hidden: ['support'] });
  assert.equal(observers.length, 1);
  assert.deepEqual(observers[0].targets.map((t) => t.id), ['about', 'can', 'parts']);
  const report = (entries) => observers[0].callback(entries.map(([id, isIntersecting]) => ({ target: sections[id], isIntersecting })));
  report([['about', true]]);
  assert.deepEqual(links.map((l) => l.attrs['aria-current'] ?? null), ['location', null, null, null]);
  report([['about', false], ['can', true], ['parts', true]]);
  assert.deepEqual(links.map((l) => l.attrs['aria-current'] ?? null), [null, 'location', null, null]);
});

test('without IntersectionObserver the contents still works', () => {
  const { links, box } = makeToc({ intersection: false });
  assert.equal(box.open, false);
  assert.ok(links.every((l) => !('aria-current' in l.attrs)));
});
