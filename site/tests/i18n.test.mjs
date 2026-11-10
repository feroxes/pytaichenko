import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { assetScripts, loadScripts, makeElement, makeEnv, readPage, readSite, siteDir } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js'];

const ESP_WEB_TOOLS = /^https:\/\/unpkg\.com\/esp-web-tools@10\.\d+\.\d+\/dist\/web\/install-button\.js\?module$/;

const FORBIDDEN = [
  'Home Assistant',
  'HA',
  'Assist',
  'українською',
  'speaks Ukrainian',
];

function forbiddenRegex(phrase) {
  const escaped = phrase.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return new RegExp(`(?<![\\p{L}\\p{N}_])${escaped}(?![\\p{L}\\p{N}_])`, 'iu');
}

function findForbidden(text) {
  return FORBIDDEN.filter((phrase) => forbiddenRegex(phrase).test(text));
}

function pageKeys(page) {
  const keys = new Set();
  for (const m of page.matchAll(/data-i18n="([^"]+)"/g)) keys.add(m[1]);
  for (const m of page.matchAll(/data-i18n-attr="([^"]+)"/g)) {
    for (const pair of m[1].split(';')) {
      const i = pair.indexOf(':');
      assert.ok(i > 0, `bad data-i18n-attr pair "${pair}"`);
      keys.add(pair.slice(i + 1).trim());
    }
  }
  return keys;
}

function anchors(page) {
  return [...page.matchAll(/<a\b([^>]*)>/gi)].map((m) => m[1]);
}

function attr(tagAttrs, name) {
  const m = new RegExp(`(?:^|\\s)${name}\\s*=\\s*"([^"]*)"`, 'i').exec(tagAttrs);
  return m ? m[1] : null;
}

test('every data-i18n key in index.html exists in uk and en', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const keys = pageKeys(readPage());
  assert.ok(keys.size > 20, 'the page should use many text keys');
  for (const key of keys) {
    assert.ok(key in TEXT.uk, `uk is missing "${key}"`);
    assert.ok(key in TEXT.en, `en is missing "${key}"`);
  }
});

test('uk and en have the same key set and no empty values', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.deepEqual(Object.keys(TEXT).sort(), ['en', 'uk']);
  assert.deepEqual(Object.keys(TEXT.uk).sort(), Object.keys(TEXT.en).sort());
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      assert.equal(typeof value, 'string', `${lang}.${key} must be a string`);
      assert.ok(value.trim().length > 0, `${lang}.${key} is empty`);
    }
  }
});

test('forbidden matcher is word based and case-insensitive', () => {
  assert.deepEqual(findForbidden('a voice assistant'), []);
  assert.deepEqual(findForbidden('Hash, hats and what'), []);
  assert.deepEqual(findForbidden('На плані'), []);
  assert.deepEqual(findForbidden('the HA setup'), ['HA']);
  assert.deepEqual(findForbidden('works with home assistant'), ['Home Assistant']);
  assert.deepEqual(findForbidden('Assist is here'), ['Assist']);
  assert.deepEqual(findForbidden('Говорить Українською.'), ['українською']);
  assert.deepEqual(findForbidden('Speaks Ukrainian well'), ['speaks Ukrainian']);
});

test('page text has no forbidden phrases', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const sources = [['index.html', readPage()]];
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) sources.push([`${lang}.${key}`, value]);
  }
  for (const [name, text] of sources) {
    assert.deepEqual(findForbidden(text), [], `${name} contains a forbidden phrase`);
  }
});

test('pickLang prefers saved value, then uk for uk/ru navigator languages, else en', () => {
  const { pickLang } = loadScripts(SCRIPTS);
  assert.equal(pickLang('en', ['uk-UA']), 'en');
  assert.equal(pickLang('uk', ['en-US']), 'uk');
  assert.equal(pickLang(null, ['uk-UA', 'en']), 'uk');
  assert.equal(pickLang(null, ['uk']), 'uk');
  assert.equal(pickLang(null, ['ru-RU']), 'uk');
  assert.equal(pickLang(null, ['en-US', 'ru']), 'uk');
  assert.equal(pickLang(null, ['en-US']), 'en');
  assert.equal(pickLang(null, ['de-DE', 'fr']), 'en');
  assert.equal(pickLang(null, []), 'en');
  assert.equal(pickLang(null, null), 'en');
  assert.equal(pickLang('fr', ['uk']), 'uk');
  assert.equal(pickLang('', ['de']), 'en');
});

test('applyText fills data-i18n text and data-i18n-attr attributes', () => {
  const P = loadScripts(SCRIPTS);
  P.TEXT.uk.t_probe = 'Привіт';
  P.TEXT.en.t_probe = 'Hello';
  P.TEXT.uk.t_label = 'Мітка';
  P.TEXT.en.t_label = 'Label';
  const textEl = makeElement({ 'data-i18n': 't_probe' }, 'old');
  const attrEl = makeElement({ 'data-i18n-attr': 'aria-label:t_label;title:t_probe' });
  const root = makeElement({}, '', [textEl, makeElement({}, '', [attrEl])]);
  P.applyText(root, 'en');
  assert.equal(textEl.textContent, 'Hello');
  assert.equal(attrEl.getAttribute('aria-label'), 'Label');
  assert.equal(attrEl.getAttribute('title'), 'Hello');
  P.applyText(root, 'uk');
  assert.equal(textEl.textContent, 'Привіт');
  assert.equal(attrEl.getAttribute('aria-label'), 'Мітка');
});

test('applyText leaves an element alone when its key is unknown', () => {
  const P = loadScripts(SCRIPTS);
  const el = makeElement({ 'data-i18n': 'no_such_key' }, 'keep');
  P.applyText(makeElement({}, '', [el]), 'en');
  assert.equal(el.textContent, 'keep');
});

test('setLang applies the text, stores the choice and fires pytaichenko:lang', () => {
  const textEl = makeElement({ 'data-i18n': 'hero.title' }, '');
  const ukBtn = makeElement({ 'data-set-lang': 'uk' });
  const enBtn = makeElement({ 'data-set-lang': 'en' });
  const env = makeEnv({ children: [textEl, ukBtn, enBtn] });
  const P = loadScripts(SCRIPTS, env);
  const seen = [];
  env.document.addEventListener('pytaichenko:lang', (e) => seen.push(e.detail.lang));
  P.setLang('en');
  assert.equal(textEl.textContent, P.TEXT.en['hero.title']);
  assert.equal(env.store.get('pytaichenko-lang'), 'en');
  assert.equal(env.root.getAttribute('lang'), 'en');
  assert.equal(P.lang, 'en');
  assert.equal(enBtn.getAttribute('aria-pressed'), 'true');
  assert.equal(ukBtn.getAttribute('aria-pressed'), 'false');
  P.setLang('uk');
  assert.equal(textEl.textContent, P.TEXT.uk['hero.title']);
  assert.equal(env.store.get('pytaichenko-lang'), 'uk');
  assert.deepEqual(seen, ['en', 'uk']);
});

test('setLang ignores unknown languages and survives blocked storage', () => {
  const env = makeEnv({ throwingStorage: true });
  const P = loadScripts(SCRIPTS, env);
  const seen = [];
  env.document.addEventListener('pytaichenko:lang', (e) => seen.push(e.detail.lang));
  P.setLang('de');
  assert.deepEqual(seen, []);
  assert.doesNotThrow(() => P.setLang('en'));
  assert.deepEqual(seen, ['en']);
});

test('on load the saved language wins, otherwise the navigator picks, and nothing is stored', () => {
  const titleEl = makeElement({ 'data-i18n': 'hero.title' }, '');
  const saved = makeEnv({ children: [titleEl], stored: 'en', languages: ['uk-UA'] });
  const P1 = loadScripts(SCRIPTS, saved);
  assert.equal(P1.lang, 'en');
  assert.equal(titleEl.textContent, P1.TEXT.en['hero.title']);

  const titleEl2 = makeElement({ 'data-i18n': 'hero.title' }, '');
  const fresh = makeEnv({ children: [titleEl2], languages: ['uk-UA'] });
  const P2 = loadScripts(SCRIPTS, fresh);
  assert.equal(P2.lang, 'uk');
  assert.equal(titleEl2.textContent, P2.TEXT.uk['hero.title']);
  assert.equal(fresh.store.has('pytaichenko-lang'), false);
});

test('a language in the address wins on load and is not stored', () => {
  const env = makeEnv({ stored: 'uk', languages: ['uk-UA'], href: 'https://example.github.io/pytaichenko/?lang=en#server' });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.lang, 'en');
  assert.equal(env.root.getAttribute('lang'), 'en');
  assert.equal(env.location.href, 'https://example.github.io/pytaichenko/?lang=en&v=2#server');
  assert.equal(env.store.get('pytaichenko-lang'), 'uk');
});

test('the address always shows the language and the version, and switching the language changes only the language', () => {
  const enBtn = makeElement({ 'data-set-lang': 'en' });
  const env = makeEnv({ children: [enBtn], languages: ['uk-UA'], href: 'https://example.github.io/pytaichenko/#parts' });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(env.location.href, 'https://example.github.io/pytaichenko/?lang=uk&v=2#parts');
  enBtn.dispatchEvent({ type: 'click' });
  assert.equal(P.lang, 'en');
  assert.equal(env.location.href, 'https://example.github.io/pytaichenko/?lang=en&v=2#parts');
  assert.equal(env.store.get('pytaichenko-lang'), 'en');
});

test('a bad language in the address is ignored, and a refused address change does no harm', () => {
  const env = makeEnv({ stored: 'en', href: 'file:///site/index.html?lang=de', throwingHistory: true });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.lang, 'en');
  assert.doesNotThrow(() => P.setLang('uk'));
  assert.equal(P.lang, 'uk');
});

test('langFromSearch and urlWithLang read and write only the lang parameter', () => {
  const P = loadScripts(SCRIPTS);
  assert.equal(P.langFromSearch('?lang=en'), 'en');
  assert.equal(P.langFromSearch('?a=1&lang=uk'), 'uk');
  assert.equal(P.langFromSearch('?lang=EN'), null);
  assert.equal(P.langFromSearch('?xlang=en'), null);
  assert.equal(P.langFromSearch(''), null);
  assert.equal(P.urlWithLang('https://h/p/', 'en'), 'https://h/p/?lang=en');
  assert.equal(P.urlWithLang('https://h/p/?lang=uk#x', 'en'), 'https://h/p/?lang=en#x');
  assert.equal(P.urlWithLang('https://h/p/?a=1&lang=uk&b=2', 'en'), 'https://h/p/?a=1&b=2&lang=en');
});

test('clicking a language button switches the language', () => {
  const titleEl = makeElement({ 'data-i18n': 'hero.title' }, '');
  const enBtn = makeElement({ 'data-set-lang': 'en' });
  const env = makeEnv({ children: [titleEl, enBtn], languages: ['uk'] });
  const P = loadScripts(SCRIPTS, env);
  enBtn.dispatchEvent({ type: 'click' });
  assert.equal(P.lang, 'en');
  assert.equal(titleEl.textContent, P.TEXT.en['hero.title']);
});

test('CONFIG has the owner repoUrl, a USD rate, a dated rate and the Donatello link, and leaves release numbers to releases.js', () => {
  const { CONFIG } = loadScripts(['assets/config.js']);
  assert.equal(CONFIG.repoUrl, 'https://github.com/feroxes/pytaichenko');
  assert.equal('version' in CONFIG, false);
  assert.deepEqual({ ...CONFIG.donate }, { donatello: 'https://donatello.to/feroxes' });
  assert.equal(typeof CONFIG.usdRate, 'number');
  assert.ok(CONFIG.usdRate > 0);
  assert.match(CONFIG.ratesDate, /^\d{4}-\d{2}-\d{2}$/);
  assert.ok(!Number.isNaN(Date.parse(CONFIG.ratesDate)));
});

test('the repo link points at repoUrl and stays a visible placeholder without href while repoUrl is empty or unusable', () => {
  const link = makeElement({ 'data-repo-link': '' });
  const env = makeEnv({ children: [link] });
  const P = loadScripts(['assets/config.js'], env);
  P.applyConfig(env.document);
  assert.equal(link.getAttribute('href'), 'https://github.com/feroxes/pytaichenko');
  assert.equal(link.getAttribute('data-repo-ready'), 'true');
  for (const bad of ['', 'javascript:alert(1)', 'http://github.com/o/r', 'https://a"b']) {
    P.CONFIG.repoUrl = bad;
    P.applyConfig(env.document);
    assert.equal(link.hasAttribute('href'), false, bad);
    assert.equal(link.getAttribute('data-repo-ready'), 'false', bad);
  }
  P.CONFIG.repoUrl = 'https://github.com/example/pytaichenko';
  P.applyConfig(env.document);
  assert.equal(link.getAttribute('href'), 'https://github.com/example/pytaichenko');
  assert.equal(link.getAttribute('data-repo-ready'), 'true');
});

test('repository page links follow repoUrl and stay plain text without a usable repoUrl or path', () => {
  const license = makeElement({ 'data-repo-path': '/blob/main/LICENSE' });
  const odd = makeElement({ 'data-repo-path': 'javascript:alert(1)' });
  const env = makeEnv({ children: [license, odd] });
  const P = loadScripts(['assets/config.js'], env);
  P.applyConfig(env.document);
  assert.equal(license.getAttribute('href'), 'https://github.com/feroxes/pytaichenko/blob/main/LICENSE');
  assert.equal(license.getAttribute('data-repo-ready'), 'true');
  assert.equal(odd.hasAttribute('href'), false);
  assert.equal(odd.getAttribute('data-repo-ready'), 'false');
  P.CONFIG.repoUrl = 'https://github.com/example/pytaichenko/';
  P.applyConfig(env.document);
  assert.equal(license.getAttribute('href'), 'https://github.com/example/pytaichenko/blob/main/LICENSE');
  for (const bad of ['', 'javascript:alert(1)', 'http://github.com/o/r']) {
    P.CONFIG.repoUrl = bad;
    P.applyConfig(env.document);
    assert.equal(license.hasAttribute('href'), false, bad);
    assert.equal(license.getAttribute('data-repo-ready'), 'false', bad);
  }
});

test('the version slot shows the release number of the shown version and is emptied and hidden when there is no release number for it', () => {
  const slot = makeElement({ 'data-version': '' }, '1.0.0');
  const box = makeElement({ 'data-version-box': '', hidden: '' }, '', [slot]);
  const env = makeEnv({ children: [box] });
  const P = loadScripts(['assets/releases.js', 'assets/config.js'], env);
  P.applyConfig(env.document);
  assert.equal(slot.textContent, '', 'no version chosen yet: no number');
  assert.ok(box.hasAttribute('hidden'));
  P.version = '2';
  P.RELEASES['2'].version = '2.1.0';
  P.applyConfig(env.document);
  assert.equal(slot.textContent, '2.1.0');
  assert.equal(box.hasAttribute('hidden'), false);
  for (const bad of ['', '2.1', '<b>2.1.0</b>', undefined]) {
    P.RELEASES['2'].version = '2.1.0';
    P.applyConfig(env.document);
    P.RELEASES['2'].version = bad;
    P.applyConfig(env.document);
    assert.equal(slot.textContent, '', String(bad));
    assert.ok(box.hasAttribute('hidden'), String(bad));
  }
  P.RELEASES['2'].version = '2.1.0';
  P.version = '7';
  P.applyConfig(env.document);
  assert.equal(slot.textContent, '');
  assert.ok(box.hasAttribute('hidden'));
  P.version = 'constructor';
  assert.doesNotThrow(() => P.applyConfig(env.document));
  assert.equal(slot.textContent, '');
  delete P.RELEASES['2'];
  P.version = '2';
  P.applyConfig(env.document);
  assert.equal(slot.textContent, '');
  assert.ok(box.hasAttribute('hidden'));
});

test('the support buttons are visible but have no href while their CONFIG.donate links are empty, and link once a link is set', () => {
  const bmc = makeElement({ 'data-donate-link': 'buyMeACoffee' });
  const mono = makeElement({ 'data-donate-link': 'monobank' });
  const odd = makeElement({ 'data-donate-link': 'constructor' });
  const env = makeEnv({ children: [bmc, mono, odd] });
  const P = loadScripts(['assets/config.js'], env);
  P.applyConfig(env.document);
  for (const el of [bmc, mono, odd]) {
    assert.equal(el.hasAttribute('href'), false);
    assert.equal(el.getAttribute('data-link-ready'), 'false');
  }
  P.CONFIG.donate.monobank = 'https://send.monobank.ua/jar/example';
  P.CONFIG.donate.buyMeACoffee = 'javascript:alert(1)';
  P.applyConfig(env.document);
  assert.equal(mono.getAttribute('href'), 'https://send.monobank.ua/jar/example');
  assert.equal(mono.getAttribute('data-link-ready'), 'true');
  assert.equal(bmc.hasAttribute('href'), false);
  P.CONFIG.donate.monobank = '';
  P.applyConfig(env.document);
  assert.equal(mono.hasAttribute('href'), false);
});

test('the support section is hidden while both CONFIG.donate links are empty or unusable, and shows once one link is usable', () => {
  const section = makeElement({ 'data-support': '', hidden: '' });
  const bmc = makeElement({ 'data-donate-link': 'buyMeACoffee' });
  const mono = makeElement({ 'data-donate-link': 'monobank' });
  const env = makeEnv({ children: [section, bmc, mono] });
  const P = loadScripts(['assets/config.js'], env);
  P.applyConfig(env.document);
  assert.equal(section.hasAttribute('hidden'), true);
  P.CONFIG.donate.buyMeACoffee = 'javascript:alert(1)';
  P.applyConfig(env.document);
  assert.equal(section.hasAttribute('hidden'), true, 'an unusable link does not show the block');
  P.CONFIG.donate.monobank = 'https://send.monobank.ua/jar/example';
  P.applyConfig(env.document);
  assert.equal(section.hasAttribute('hidden'), false);
  assert.equal(mono.hasAttribute('hidden'), false, 'the usable button shows');
  assert.equal(bmc.hasAttribute('hidden'), true, 'a button without a usable link is hidden, not left as an empty box');
  P.CONFIG.donate.buyMeACoffee = 'https://www.buymeacoffee.com/example';
  P.applyConfig(env.document);
  assert.equal(bmc.hasAttribute('hidden'), false);
  P.CONFIG.donate.monobank = '';
  P.CONFIG.donate.buyMeACoffee = '';
  P.applyConfig(env.document);
  assert.equal(section.hasAttribute('hidden'), true);
  assert.equal(mono.hasAttribute('hidden'), true);
  assert.equal(bmc.hasAttribute('hidden'), true);
});

test('the support block sits at the end of the page, above the license: a heading, one calm line and a heart Donate button, in both languages', () => {
  const page = readPage();
  const { TEXT } = loadScripts(['assets/text.js']);
  const m = /<section id="support" class="block" data-support hidden>([\s\S]*?)<\/section>\s*<\/main>\s*<footer\b/.exec(page);
  assert.ok(m, 'the support section is the last one in main, right before the footer');
  const body = m[1];
  const keys = [...body.matchAll(/data-i18n="([^"]+)"/g)].map((k) => k[1]);
  assert.deepEqual(keys, ['support.title', 'support.text', 'support.donate']);
  const links = [...body.matchAll(/<a\b([^>]*)>([\s\S]*?)<\/a>/g)];
  assert.deepEqual(links.map((l) => /data-donate-link="(\w+)"/.exec(l[1])[1]), ['donatello']);
  assert.match(links[0][2], /class="donate-heart"/);
  for (const l of links) {
    assert.doesNotMatch(l[1], /\bhref=/, 'no href in the static markup');
    assert.match(l[1], /target="_blank"/);
    assert.match(l[1], /rel="noopener noreferrer"/);
  }
  assert.equal(TEXT.uk['support.title'], 'Підтримати проєкт');
  assert.equal(TEXT.en['support.title'], 'Support the project');
  assert.equal(TEXT.uk['support.donate'], 'Donate');
  assert.equal(TEXT.en['support.donate'], 'Donate');
  assert.equal(TEXT.uk['support.text'], 'Якщо проєкт став у пригоді, можна підтримати автора.');
  for (const lang of ['uk', 'en']) assert.doesNotMatch(TEXT[lang]['support.text'], /!|будь ласка|please|негайно|now/i, lang);
  assert.doesNotMatch(body, /<script|<iframe|buymeacoffee\.com|monobank\.ua/i, 'no widgets or scripts of the services');
  const css = readSite('assets/style.css');
  assert.match(css, /\.support-link:not\(\[href\]\)\s*\{[^}]*border-style:\s*dashed[^}]*color:\s*var\(--text-secondary\)/);
  assert.match(css, /\.support-link\[hidden\]\s*\{\s*display:\s*none;?\s*\}/, 'a hidden button stays hidden although .support-link sets display');
});

test('sections come in the agreed order with headings', () => {
  const page = readPage();
  const ids = [...page.matchAll(/<section\b[^>]*\bid="([^"]+)"/g)].map((m) => m[1]);
  assert.deepEqual(ids, ['about', 'can', 'need', 'parts', 'tools', 'print', 'solder', 'assembly', 'firmware', 'server', 'trouble', 'support']);
  for (const id of ids) {
    const block = new RegExp(`<section\\b[^>]*\\bid="${id}"[^>]*>\\s*(?:<div\\b[^>]*\\bdata-for-v="\\d"[^>]*>\\s*)?<h[12]\\b[^>]*data-i18n="[^"]+"`).exec(page);
    assert.ok(block, `section ${id} needs a translated heading right at its start`);
  }
  assert.match(page, /<header\b/);
  assert.match(page, /<footer\b/);
  assert.match(page, /<meta name="viewport"/);
  assert.match(page, /data-set-lang="uk"/);
  assert.match(page, /data-set-lang="en"/);
  assert.match(page, /data-repo-link/);
});

test('index.html loads only classic local scripts apart from the one pinned ESP Web Tools module, and no fetch/XMLHttpRequest in assets', () => {
  const page = readPage();
  const allTags = [...page.matchAll(/<script\b([^>]*)>/gi)].map((m) => m[1]);
  const modules = allTags.filter((tag) => /\btype\s*=\s*["']?module/i.test(tag));
  assert.equal(modules.length, 1, 'only the ESP Web Tools script is a module');
  assert.match(attr(modules[0], 'src'), ESP_WEB_TOOLS);
  const scriptTags = allTags.filter((tag) => !modules.includes(tag));
  assert.ok(scriptTags.length >= 3);
  for (const tag of scriptTags) {
    assert.equal(attr(tag, 'type'), null, `classic script without a type: ${tag}`);
    const src = attr(tag, 'src');
    assert.ok(src, `inline or src-less script tag: ${tag}`);
    assert.doesNotMatch(src, /^(?:[a-z]+:)?\/\//i, `script must be local: ${src}`);
  }
  for (const m of page.matchAll(/<link\b([^>]*)>/gi)) {
    const href = attr(m[1], 'href');
    if (href && /stylesheet/i.test(m[1])) assert.doesNotMatch(href, /^(?:[a-z]+:)?\/\//i, `stylesheet must be local: ${href}`);
  }
  for (const name of assetScripts()) {
    const code = readSite(`assets/${name}`);
    assert.doesNotMatch(code, /\bfetch\s*\(/, `${name} uses fetch`);
    assert.doesNotMatch(code, /XMLHttpRequest/, `${name} uses XMLHttpRequest`);
    assert.doesNotMatch(code, /^\s*(?:import|export)\b/m, `${name} uses ES module syntax`);
    assert.doesNotMatch(code, /\bimport\s*\(/, `${name} uses dynamic import`);
  }
});

test('every local script and stylesheet named in index.html exists', () => {
  const page = readPage();
  const refs = [
    ...[...page.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gi)].map((m) => m[1]),
    ...[...page.matchAll(/<link\b[^>]*\bhref="([^"]+)"/gi)].map((m) => m[1]),
  ];
  assert.ok(refs.length >= 4);
  for (const ref of refs.filter((r) => !ESP_WEB_TOOLS.test(r))) assert.ok(existsSync(path.join(siteDir, ref)), `${ref} is missing`);
});

test('scripts are loaded in dependency order: text, config, i18n', () => {
  const page = readPage();
  const srcs = [...page.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gi)].map((m) => m[1]);
  const at = (name) => srcs.indexOf(name);
  assert.ok(at('assets/text.js') >= 0 && at('assets/config.js') >= 0 && at('assets/i18n.js') >= 0);
  assert.ok(at('assets/text.js') < at('assets/i18n.js'));
  assert.ok(at('assets/config.js') < at('assets/i18n.js'));
});

test('no dead links: no href="" or href="#" and every external link has target=_blank and rel containing noopener', () => {
  const page = readPage();
  const all = anchors(page);
  assert.ok(all.length >= 1);
  for (const tag of all) {
    const href = attr(tag, 'href');
    if (href === null) continue;
    assert.notEqual(href.trim(), '', `empty href in <a ${tag}>`);
    assert.notEqual(href.trim(), '#', `dead "#" href in <a ${tag}>`);
    if (/^(?:https?:)?\/\//i.test(href)) {
      assert.equal(attr(tag, 'target'), '_blank', `external link without target=_blank: ${href}`);
      assert.match(attr(tag, 'rel') || '', /\bnoopener\b/, `external link without rel=noopener: ${href}`);
    }
  }
  assert.ok(
    all.some((tag) => attr(tag, 'target') === '_blank' && /\bnoopener\b/.test(attr(tag, 'rel') || '')),
    'the repo link is built to open in a new tab',
  );
});

test('in-page anchors point to existing ids', () => {
  const page = readPage();
  const ids = new Set([...page.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
  for (const tag of anchors(page)) {
    const href = attr(tag, 'href');
    if (href && href.startsWith('#') && href.length > 1) assert.ok(ids.has(href.slice(1)), `${href} has no target`);
  }
});

test('the need section has visible slots for time and cost', () => {
  const page = readPage();
  assert.match(page, /id="need-time"/);
  assert.match(page, /id="need-cost"/);
});

test('style.css is dark by default, has a light palette under data-theme, a narrow column and scrolling table wrappers', () => {
  const css = readSite('assets/style.css');
  assert.doesNotMatch(css, /prefers-color-scheme/);
  assert.match(css, /:root\[data-theme="light"\]/);
  assert.match(css, /max-width:\s*820px/);
  assert.match(css, /\.table-wrap\s*\{[^}]*overflow-x:\s*auto/);
  assert.match(css, /--text-primary/);
  assert.match(css, /--border-strong/);
});

test('static HTML text equals the uk dictionary, so the page reads right before scripts run', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  let checked = 0;
  for (const m of page.matchAll(/<([a-z0-9]+)\b[^>]*\bdata-i18n="([^"]+)"[^>]*>([^<]*)<\/\1>/g)) {
    assert.equal(m[3].trim(), TEXT.uk[m[2]], `static text of ${m[2]} differs from uk`);
    checked += 1;
  }
  assert.ok(checked > 20);
  for (const m of page.matchAll(/<[a-z0-9]+\b[^>]*\bdata-i18n-attr="[^"]+"[^>]*>/g)) {
    const tag = m[0];
    for (const pair of attr(tag, 'data-i18n-attr').split(';')) {
      const i = pair.indexOf(':');
      const name = pair.slice(0, i).trim();
      const key = pair.slice(i + 1).trim();
      assert.equal(attr(tag, name), TEXT.uk[key], `static ${name} for ${key} differs from uk`);
    }
  }
});

test('P.lang is set synchronously while the document is still loading, text and event wait for DOMContentLoaded', () => {
  const titleEl = makeElement({ 'data-i18n': 'hero.title' }, 'static');
  const env = makeEnv({ children: [titleEl], stored: 'en', languages: ['uk-UA'], readyState: 'loading' });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.lang, 'en');
  assert.equal(env.root.getAttribute('lang'), 'en');
  assert.equal(titleEl.textContent, 'static');
  const seen = [];
  env.document.addEventListener('pytaichenko:lang', (e) => seen.push(e.detail.lang));
  assert.deepEqual(seen, []);
  env.document.readyState = 'interactive';
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.deepEqual(seen, ['en']);
  assert.equal(titleEl.textContent, P.TEXT.en['hero.title']);
  assert.equal(P.lang, 'en');
});

test('while loading, the navigator picks P.lang when nothing is saved and nothing is stored', () => {
  const env = makeEnv({ languages: ['uk-UA'], readyState: 'loading' });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.lang, 'uk');
  assert.equal(env.store.has('pytaichenko-lang'), false);
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.equal(P.lang, 'uk');
  assert.equal(env.store.has('pytaichenko-lang'), false);
});

test('a language button clicked after DOMContentLoaded switches the language while loading started late', () => {
  const titleEl = makeElement({ 'data-i18n': 'hero.title' }, '');
  const enBtn = makeElement({ 'data-set-lang': 'en' });
  const env = makeEnv({ children: [titleEl, enBtn], languages: ['uk'], readyState: 'loading' });
  const P = loadScripts(SCRIPTS, env);
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  enBtn.dispatchEvent({ type: 'click' });
  assert.equal(P.lang, 'en');
  assert.equal(env.store.get('pytaichenko-lang'), 'en');
});

test('no exclamation marks in the page text, except inside quoted labels of the English installer window', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      assert.doesNotMatch(value.replace(/«[^»]*»|“[^”]*”/g, ''), /!/, `${lang} ${key}`);
    }
  }
});

test('the header starts with a heart Donate link to Donatello that config.js fills in and shows', () => {
  const page = readPage();
  const m = /<div class="header-tools">\s*<a ([^>]*)>([\s\S]*?)<\/a>/.exec(page);
  assert.ok(m, 'the first item in the header tools is a link');
  assert.match(m[1], /class="donate-link"/);
  assert.match(m[1], /data-donate-link="donatello"/);
  assert.match(m[1], /\bhidden\b/);
  assert.doesNotMatch(m[1], /\bhref=/);
  assert.match(m[1], /target="_blank"/);
  assert.match(m[1], /rel="noopener noreferrer"/);
  assert.match(m[2], /class="donate-heart"/);
  assert.match(m[2], /data-i18n="header.donate"/);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TEXT.uk['header.donate'], 'Donate');
  assert.equal(TEXT.en['header.donate'], 'Donate');
  const link = makeElement({ 'data-donate-link': 'donatello', hidden: '' });
  const env = makeEnv({ children: [link] });
  const P = loadScripts(['assets/config.js'], env);
  P.applyConfig(env.document);
  assert.equal(link.getAttribute('href'), 'https://donatello.to/feroxes');
  assert.equal(link.hasAttribute('hidden'), false);
});
