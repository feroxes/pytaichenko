import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

export const siteDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

export function readSite(relPath) {
  return readFileSync(path.join(siteDir, relPath), 'utf8');
}

export function readPage() {
  return readSite('index.html');
}

export function assetScripts() {
  return readdirSync(path.join(siteDir, 'assets'))
    .filter((name) => name.endsWith('.js'))
    .sort();
}

function matchesSelector(el, selector) {
  const m = /^\[([\w-]+)\]$/.exec(selector);
  if (!m) throw new Error(`fake DOM supports only [attr] selectors, got ${selector}`);
  return Object.prototype.hasOwnProperty.call(el.attrs, m[1]);
}

function collect(el, selector, out) {
  for (const child of el.children) {
    if (matchesSelector(child, selector)) out.push(child);
    collect(child, selector, out);
  }
  return out;
}

export function makeElement(attrs = {}, text = '', children = [], tagName = undefined) {
  return {
    tagName,
    attrs: { ...attrs },
    textContent: text,
    children,
    style: {},
    listeners: {},
    getAttribute(name) {
      return Object.prototype.hasOwnProperty.call(this.attrs, name) ? this.attrs[name] : null;
    },
    setAttribute(name, value) {
      this.attrs[name] = String(value);
    },
    removeAttribute(name) {
      delete this.attrs[name];
    },
    hasAttribute(name) {
      return Object.prototype.hasOwnProperty.call(this.attrs, name);
    },
    addEventListener(type, fn) {
      (this.listeners[type] = this.listeners[type] || []).push(fn);
    },
    dispatchEvent(event) {
      (this.listeners[event.type] || []).forEach((fn) => fn(event));
      return true;
    },
    querySelectorAll(selector) {
      return collect(this, selector, []);
    },
  };
}

class FakeCustomEvent {
  constructor(type, init = {}) {
    this.type = type;
    this.detail = init.detail;
  }
}

export function makeEnv({ children = [], stored = null, storedTheme = null, languages = [], throwingStorage = false, readyState = 'complete', href = null, throwingHistory = false } = {}) {
  const store = new Map();
  if (stored !== null) store.set('pytaichenko-lang', stored);
  if (storedTheme !== null) store.set('pytaichenko-theme', storedTheme);
  const root = makeElement({}, '', children);
  const document = makeElement();
  document.documentElement = root;
  document.readyState = readyState;
  document.querySelectorAll = (selector) => root.querySelectorAll(selector);
  const localStorage = {
    getItem(key) {
      if (throwingStorage) throw new Error('blocked');
      return store.has(key) ? store.get(key) : null;
    },
    setItem(key, value) {
      if (throwingStorage) throw new Error('blocked');
      store.set(key, String(value));
    },
  };
  const replaced = [];
  const location = href === null ? null : { href, search: new URL(href).search, hash: new URL(href).hash };
  const history = {
    state: null,
    replaceState(state, title, url) {
      if (throwingHistory) throw new Error('SecurityError');
      const next = new URL(url);
      Object.assign(location, { href: next.href, search: next.search, hash: next.hash });
      replaced.push(url);
    },
  };
  return { document, root, store, localStorage, navigator: { languages, language: languages[0] }, location, history, replaced };
}

export function loadScripts(files, env = makeEnv()) {
  const window = {
    document: env.document,
    localStorage: env.localStorage,
    navigator: env.navigator,
    CustomEvent: FakeCustomEvent,
  };
  if (env.location) {
    window.location = env.location;
    window.history = env.history;
  }
  window.window = window;
  const context = vm.createContext({ window, document: env.document, navigator: env.navigator, CustomEvent: FakeCustomEvent });
  for (const file of files) {
    vm.runInContext(readSite(file), context, { filename: file });
  }
  return window.PYTAICHENKO;
}

const VOID_TAGS = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'source', 'track', 'wbr']);

export function sectionHtml(page, id) {
  const m = new RegExp(`<section\\b[^>]*\\bid="${id}"[^>]*>[\\s\\S]*?</section>`).exec(page);
  assert.ok(m, `section ${id} is missing`);
  return m[0];
}

export function tagAttr(tag, name) {
  const m = new RegExp(`(?:^|\\s)${name}\\s*=\\s*"([^"]*)"`, 'i').exec(tag);
  return m ? m[1] : null;
}

export function keysIn(html) {
  return [...html.matchAll(/data-i18n="([^"]+)"/g)].map((m) => m[1]);
}

export function parseFragment(html) {
  const root = makeElement({}, '', [], '#fragment');
  root.nodes = [];
  const stack = [root];
  const token = /<!--[\s\S]*?-->|<\/([a-zA-Z][\w-]*)\s*>|<([a-zA-Z][\w-]*)([^>]*?)(\/?)>|([^<]+)/g;
  for (const m of html.matchAll(token)) {
    const top = stack[stack.length - 1];
    if (m[1]) {
      assert.equal(top.tagName, m[1].toLowerCase(), `unbalanced </${m[1]}>`);
      if (top.hasAttribute('data-i18n')) {
        assert.equal(top.children.length, 0, `${top.getAttribute('data-i18n')} must hold text only`);
        top.textContent = top.nodes.map((n) => n.text).join('');
      }
      stack.pop();
    } else if (m[2]) {
      const attrs = {};
      for (const a of m[3].matchAll(/([^\s=/]+)(?:\s*=\s*"([^"]*)")?/g)) attrs[a[1]] = a[2] === undefined ? '' : a[2];
      const el = makeElement(attrs, '', [], m[2].toLowerCase());
      el.nodes = [];
      top.children.push(el);
      top.nodes.push(el);
      if (!m[4] && !VOID_TAGS.has(el.tagName)) stack.push(el);
    } else if (m[5]) {
      top.nodes.push({ text: m[5] });
    }
  }
  assert.equal(stack.length, 1, 'unclosed tags in the fragment');
  return root;
}

export function visibleText(el) {
  const parts = Object.values(el.attrs);
  if (el.hasAttribute('data-i18n')) return [...parts, el.textContent].join(' ');
  for (const node of el.nodes) parts.push(node.text !== undefined ? node.text : visibleText(node));
  return parts.join(' ');
}
