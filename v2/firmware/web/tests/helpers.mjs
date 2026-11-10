import { readFileSync } from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

export const webDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

export function readWeb(name) {
  return readFileSync(path.join(webDir, name), 'utf8');
}

export function loadPage({ stored = {}, languages = [], search } = {}) {
  const store = new Map(Object.entries(stored));
  const attrs = {};
  const document = {
    readyState: 'loading',
    listeners: [],
    addEventListener(type) {
      this.listeners.push(type);
    },
    documentElement: {
      setAttribute(name, value) {
        attrs[name] = String(value);
      },
      getAttribute(name) {
        return attrs[name] ?? null;
      },
    },
  };
  const window = {
    document,
    navigator: { languages, language: languages[0] },
    localStorage: {
      getItem: (k) => (store.has(k) ? store.get(k) : null),
      setItem: (k, v) => store.set(k, String(v)),
    },
  };
  if (search !== undefined) window.location = { search };
  window.window = window;
  vm.runInNewContext(readWeb('app.js'), { window, document, navigator: window.navigator }, { filename: 'app.js' });
  return { H: window.PYTAICHENKO_PAGE, attrs, document, store };
}
