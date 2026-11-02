import test from 'node:test';
import assert from 'node:assert/strict';
import { loadScripts, makeElement, makeEnv, readPage, readSite } from './helpers.mjs';

const THEME = ['assets/theme.js'];

function luminance(hex) {
  const n = parseInt(hex.slice(1), 16);
  const [r, g, b] = [(n >> 16) & 255, (n >> 8) & 255, n & 255].map((v) => {
    const c = v / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

function block(css, selector) {
  const start = css.indexOf(`${selector} {`);
  assert.ok(start >= 0, `no ${selector} block`);
  return css.slice(start, css.indexOf('}', start));
}

test('the default theme is dark when nothing is stored, and nothing gets stored', () => {
  const env = makeEnv({ languages: ['en-US'] });
  const P = loadScripts(THEME, env);
  assert.equal(P.theme, 'dark');
  assert.equal(env.root.getAttribute('data-theme'), 'dark');
  assert.equal(env.store.has('pytaichenko-theme'), false);
});

test('the system colour scheme does not change the default: no matchMedia use', () => {
  assert.doesNotMatch(readSite('assets/theme.js'), /matchMedia|prefers-color-scheme/);
});

test('a stored light theme is applied while the document is still loading, a stored junk value falls back to dark', () => {
  const light = makeEnv({ storedTheme: 'light', readyState: 'loading' });
  const P1 = loadScripts(THEME, light);
  assert.equal(P1.theme, 'light');
  assert.equal(light.root.getAttribute('data-theme'), 'light');

  const junk = makeEnv({ storedTheme: 'blue', readyState: 'loading' });
  const P2 = loadScripts(THEME, junk);
  assert.equal(P2.theme, 'dark');
  assert.equal(junk.root.getAttribute('data-theme'), 'dark');
});

test('the toggle switches to light and back, stores the choice and marks the button', () => {
  const btn = makeElement({ 'data-theme-toggle': '', 'aria-pressed': 'false' });
  const env = makeEnv({ children: [btn] });
  const P = loadScripts(THEME, env);
  assert.equal(btn.getAttribute('aria-pressed'), 'false');
  btn.dispatchEvent({ type: 'click' });
  assert.equal(P.theme, 'light');
  assert.equal(env.root.getAttribute('data-theme'), 'light');
  assert.equal(env.store.get('pytaichenko-theme'), 'light');
  assert.equal(btn.getAttribute('aria-pressed'), 'true');
  btn.dispatchEvent({ type: 'click' });
  assert.equal(P.theme, 'dark');
  assert.equal(env.root.getAttribute('data-theme'), 'dark');
  assert.equal(env.store.get('pytaichenko-theme'), 'dark');
  assert.equal(btn.getAttribute('aria-pressed'), 'false');
});

test('the toggle binds after DOMContentLoaded and shows the stored theme as pressed', () => {
  const btn = makeElement({ 'data-theme-toggle': '', 'aria-pressed': 'false' });
  const env = makeEnv({ children: [btn], storedTheme: 'light', readyState: 'loading' });
  const P = loadScripts(THEME, env);
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.equal(btn.getAttribute('aria-pressed'), 'true');
  btn.dispatchEvent({ type: 'click' });
  assert.equal(P.theme, 'dark');
});

test('setTheme ignores unknown values and survives blocked storage', () => {
  const env = makeEnv({ throwingStorage: true });
  const P = loadScripts(THEME, env);
  assert.equal(P.theme, 'dark');
  P.setTheme('pink');
  assert.equal(P.theme, 'dark');
  assert.doesNotThrow(() => P.setTheme('light'));
  assert.equal(P.theme, 'light');
  assert.equal(env.root.getAttribute('data-theme'), 'light');
});

test('theme.js runs first in <head>, blocking, and the page starts out dark', () => {
  const page = readPage();
  const head = /<head>([\s\S]*?)<\/head>/.exec(page)[1];
  const tag = /<script\b([^>]*)\bsrc="assets\/theme\.js"([^>]*)>/.exec(head);
  assert.ok(tag, 'theme.js must be loaded from <head>');
  assert.doesNotMatch(tag[1] + tag[2], /\b(?:async|defer|type)\b/i);
  assert.ok(head.indexOf('assets/theme.js') < head.indexOf('assets/style.css'), 'theme.js before the stylesheet');
  assert.match(page, /<html\b[^>]*\bdata-theme="dark"/);
  assert.doesNotMatch(page, /<script\b(?![^>]*\bsrc=)[^>]*>/i, 'no inline scripts');
});

test('the header has a theme toggle with a translated aria-label in both languages', () => {
  const page = readPage();
  const tag = /<button\b[^>]*\bdata-theme-toggle[^>]*>/.exec(page);
  assert.ok(tag, 'theme toggle button missing');
  assert.match(tag[0], /data-i18n-attr="[^"]*aria-label:header\.theme_label/);
  assert.match(tag[0], /type="button"/);
  assert.match(tag[0], /aria-pressed="false"/);
  assert.match(tag[0], /aria-label="[^"]+"/);
  const P = loadScripts(['assets/text.js']);
  assert.ok(P.TEXT.uk['header.theme_label'].trim().length > 0);
  assert.ok(P.TEXT.en['header.theme_label'].trim().length > 0);
  assert.notEqual(P.TEXT.uk['header.theme_label'], P.TEXT.en['header.theme_label']);
});

test('css: dark palette is the default, light palette sits under data-theme="light"', () => {
  const css = readSite('assets/style.css');
  assert.doesNotMatch(css, /prefers-color-scheme/);
  const dark = block(css, ':root');
  const light = block(css, ':root[data-theme="light"]');
  const bg = (b) => /--bg:\s*(#[0-9a-f]{6})/i.exec(b)[1];
  const text = (b) => /--text-primary:\s*(#[0-9a-f]{6})/i.exec(b)[1];
  assert.ok(luminance(bg(dark)) < 0.1, 'default background must be dark');
  assert.ok(luminance(text(dark)) > 0.6, 'default text must be light');
  assert.ok(luminance(bg(light)) > 0.8, 'light background must be light');
  assert.ok(luminance(text(light)) < 0.1, 'light text must be dark');
  assert.match(dark, /color-scheme:\s*dark/);
  assert.match(light, /color-scheme:\s*light/);
});
