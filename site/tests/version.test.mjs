import test from 'node:test';
import assert from 'node:assert/strict';
import { loadScripts, makeElement, makeEnv, parseFragment, readPage, readSite, sectionHtml } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js'];
const BASE = 'https://example.github.io/pytaichenko/';

function page({ href = BASE, stored = null, lang = 'uk' } = {}) {
  const v1 = makeElement({ 'data-for-v': '1' }, '', [makeElement({ id: 'h-mic' })]);
  const v2 = makeElement({ 'data-for-v': '2' }, '', [makeElement({ id: 'h-s3' })]);
  const server = makeElement({ id: 'server', 'data-for-v': '1' });
  const parts = makeElement({ id: 'parts' });
  const b2 = makeElement({ 'data-set-version': '2', 'aria-pressed': 'true' });
  const b1 = makeElement({ 'data-set-version': '1', 'aria-pressed': 'false' });
  const en = makeElement({ 'data-set-lang': 'en' });
  const env = makeEnv({ children: [b2, b1, en, parts, v1, v2, server], languages: [lang], href });
  if (stored !== null) env.store.set('pytaichenko-version', stored);
  return { env, v1, v2, server, b1, b2, en };
}

function shown(el) {
  return !el.hasAttribute('hidden');
}

test('the version list is newest first and the address beats storage, which beats the newest', () => {
  const P = loadScripts(SCRIPTS, makeEnv());
  assert.deepEqual([...P.VERSIONS], ['2', '1']);
  assert.equal(P.pickVersion('1', '2'), '1');
  assert.equal(P.pickVersion('2', '1'), '2');
  assert.equal(P.pickVersion(null, '1'), '1');
  assert.equal(P.pickVersion(null, null), '2');
  assert.equal(P.pickVersion('7', '1'), '1');
  assert.equal(P.pickVersion('7', 'x'), '2');
  assert.equal(P.pickVersion(undefined, ''), '2');
});

const ONLY_V1 = () => ({ RELEASES: { '1': { version: '1.0.0', tag: 'v1.0.0', stlDir: 'case/stl/' } } });

test('without a version in the address or storage the newest released line is the default, and an explicit choice still wins', () => {
  const P = loadScripts(SCRIPTS, makeEnv(), ONLY_V1());
  assert.equal(P.pickVersion(null, null), '1');
  assert.equal(P.pickVersion('7', 'x'), '1');
  assert.equal(P.pickVersion(null, '2'), '2');
  assert.equal(P.pickVersion('2', '1'), '2');
  P.RELEASES['2'] = { version: '2.0.0', tag: 'v2.0.0', stlDir: 'v2/case/stl/' };
  assert.equal(P.pickVersion(null, null), '2');
  P.RELEASES = {};
  assert.equal(P.pickVersion(null, null), '2', 'no released line: the newest line');
  delete P.RELEASES;
  assert.equal(P.pickVersion(null, null), '2', 'no release list: the newest line');

  const fresh = page({ href: BASE });
  const one = loadScripts(SCRIPTS, fresh.env, ONLY_V1());
  assert.equal(one.version, '1');
  assert.ok(shown(fresh.v1) && !shown(fresh.v2));
  assert.equal(fresh.env.location.href, `${BASE}?lang=uk&v=1`);

  const hash = page({ href: `${BASE}#h-s3` });
  assert.equal(loadScripts(SCRIPTS, hash.env, ONLY_V1()).version, '2', 'a hash inside a version 2 block still opens version 2');

  const saved = page({ href: BASE, stored: '2' });
  assert.equal(loadScripts(SCRIPTS, saved.env, ONLY_V1()).version, '2');
  const asked = page({ href: `${BASE}?v=2` });
  assert.equal(loadScripts(SCRIPTS, asked.env, ONLY_V1()).version, '2');
});

test('the footer hides the release link of a version that has no release and shows it again for one that has', () => {
  const footer = parseFragment(/<footer\b[\s\S]*?<\/footer>/.exec(readPage())[0]);
  const b2 = makeElement({ 'data-set-version': '2', 'aria-pressed': 'true' });
  const b1 = makeElement({ 'data-set-version': '1', 'aria-pressed': 'false' });
  const env = makeEnv({ children: [b2, b1, footer], languages: ['uk'], href: `${BASE}?v=2` });
  const P = loadScripts(['assets/text.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js'], env, ONLY_V1());
  const slot = footer.querySelectorAll('[data-version]')[0];
  const box = footer.querySelectorAll('[data-version-box]')[0];
  assert.equal(P.version, '2');
  assert.equal(slot.textContent, '');
  assert.ok(box.hasAttribute('hidden'));
  b1.dispatchEvent({ type: 'click' });
  assert.equal(slot.textContent, '1.0.0');
  assert.equal(box.hasAttribute('hidden'), false);
  b2.dispatchEvent({ type: 'click' });
  assert.equal(slot.textContent, '', 'the number of version 1 does not stay under version 2');
  assert.ok(box.hasAttribute('hidden'));
});

test('versionFromSearch reads only a known v parameter', () => {
  const P = loadScripts(SCRIPTS, makeEnv());
  assert.equal(P.versionFromSearch('?v=1'), '1');
  assert.equal(P.versionFromSearch('?lang=en&v=2'), '2');
  assert.equal(P.versionFromSearch('?v=7'), null);
  assert.equal(P.versionFromSearch('?v='), null);
  assert.equal(P.versionFromSearch('?vv=1'), null);
  assert.equal(P.versionFromSearch(''), null);
  assert.equal(P.versionFromSearch(undefined), null);
});

test('urlWithParams sets each given parameter once and keeps the others and the hash', () => {
  const P = loadScripts(SCRIPTS, makeEnv());
  assert.equal(P.urlWithParams('https://h/p/', { lang: 'uk', v: '2' }), 'https://h/p/?lang=uk&v=2');
  assert.equal(P.urlWithParams('https://h/p/?lang=uk&v=1#x', { v: '2' }), 'https://h/p/?lang=uk&v=2#x');
  assert.equal(P.urlWithParams('https://h/p/?a=1&v=1&v=7&lang=uk&b=2#s', { lang: 'en', v: '1' }), 'https://h/p/?a=1&b=2&lang=en&v=1#s');
  assert.equal(P.urlWithLang('https://h/p/?v=1#x', 'en'), 'https://h/p/?v=1&lang=en#x');
});

test('a version in the address shows only its blocks, presses its button and lands in the address with the language', () => {
  const { env, v1, v2, server, b1, b2 } = page({ href: `${BASE}?v=1` });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.version, '1');
  assert.ok(shown(v1) && shown(server));
  assert.ok(!shown(v2));
  assert.equal(b1.getAttribute('aria-pressed'), 'true');
  assert.equal(b2.getAttribute('aria-pressed'), 'false');
  assert.equal(env.location.href, `${BASE}?lang=uk&v=1`);
  assert.equal(env.store.has('pytaichenko-version'), false);
});

test('without a version in the address the newest one is shown and written to the address', () => {
  const { env, v1, v2, server } = page({ href: `${BASE}#parts` });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.version, '2');
  assert.ok(shown(v2));
  assert.ok(!shown(v1) && !shown(server));
  assert.equal(env.location.href, `${BASE}?lang=uk&v=2#parts`);
});

test('a saved version is used when the address has none or a bad one, and the address is corrected', () => {
  const saved = page({ href: `${BASE}?lang=en`, stored: '1' });
  assert.equal(loadScripts(SCRIPTS, saved.env).version, '1');
  assert.equal(saved.env.location.href, `${BASE}?lang=en&v=1`);

  const garbage = page({ href: `${BASE}?v=7&lang=en`, stored: '1' });
  assert.equal(loadScripts(SCRIPTS, garbage.env).version, '1');
  assert.equal(garbage.env.location.href, `${BASE}?lang=en&v=1`);

  const fresh = page({ href: `${BASE}?v=7` });
  assert.equal(loadScripts(SCRIPTS, fresh.env).version, '2');
  assert.equal(fresh.env.location.href, `${BASE}?lang=uk&v=2`);
});

test('an old link whose hash points into a block of the other version opens that version', () => {
  const server = page({ href: `${BASE}?lang=en#server` });
  const P = loadScripts(SCRIPTS, server.env);
  assert.equal(P.version, '1');
  assert.ok(shown(server.server));
  assert.equal(server.env.location.href, `${BASE}?lang=en&v=1#server`);
  assert.equal(server.env.store.has('pytaichenko-version'), false);

  const nested = page({ href: `${BASE}#h-mic`, stored: '2' });
  assert.equal(loadScripts(SCRIPTS, nested.env).version, '1');

  const newer = page({ href: `${BASE}#h-s3`, stored: '1' });
  assert.equal(loadScripts(SCRIPTS, newer.env).version, '2');

  const shared = page({ href: `${BASE}#parts`, stored: '1' });
  assert.equal(loadScripts(SCRIPTS, shared.env).version, '1');

  const explicit = page({ href: `${BASE}?v=2#server` });
  assert.equal(loadScripts(SCRIPTS, explicit.env).version, '2');
});

test('clicking a version button switches blocks, stores the choice, keeps the language in the address and tells the page', () => {
  const { env, v1, v2, server, b1, b2 } = page({ href: `${BASE}?lang=en#parts` });
  const P = loadScripts(SCRIPTS, env);
  const seen = [];
  env.document.addEventListener('pytaichenko:version', (e) => seen.push(e.detail.version));
  b1.dispatchEvent({ type: 'click' });
  assert.equal(P.version, '1');
  assert.ok(shown(v1) && shown(server));
  assert.ok(!shown(v2));
  assert.equal(b1.getAttribute('aria-pressed'), 'true');
  assert.equal(b2.getAttribute('aria-pressed'), 'false');
  assert.equal(env.store.get('pytaichenko-version'), '1');
  assert.equal(env.location.href, `${BASE}?lang=en&v=1#parts`);
  assert.deepEqual(seen, ['1']);
  b2.dispatchEvent({ type: 'click' });
  assert.equal(P.version, '2');
  assert.ok(!shown(v1) && !shown(server) && shown(v2));
  assert.equal(env.location.href, `${BASE}?lang=en&v=2#parts`);
  assert.deepEqual(seen, ['1', '2']);
});

test('switching the language keeps the version in the address', () => {
  const { env, en } = page({ href: `${BASE}?v=1#server` });
  const P = loadScripts(SCRIPTS, env);
  en.dispatchEvent({ type: 'click' });
  assert.equal(P.lang, 'en');
  assert.equal(P.version, '1');
  assert.equal(env.location.href, `${BASE}?lang=en&v=1#server`);
});

test('setVersion ignores unknown versions and survives blocked storage and a refused address change', () => {
  const env = makeEnv({ throwingStorage: true, throwingHistory: true, href: `${BASE}?v=1` });
  const P = loadScripts(SCRIPTS, env);
  const seen = [];
  env.document.addEventListener('pytaichenko:version', (e) => seen.push(e.detail.version));
  P.setVersion('3');
  assert.equal(P.version, '1');
  assert.deepEqual(seen, []);
  assert.doesNotThrow(() => P.setVersion('2'));
  assert.equal(P.version, '2');
  assert.deepEqual(seen, ['2']);
});

test('applyVersion hides the blocks of the other version and presses the right button', () => {
  const { env, v1, v2, server, b1, b2 } = page();
  const P = loadScripts(SCRIPTS, env);
  P.applyVersion(env.document, '1');
  assert.ok(shown(v1) && shown(server) && !shown(v2));
  assert.equal(b1.getAttribute('aria-pressed'), 'true');
  P.applyVersion(env.document, '2');
  assert.ok(!shown(v1) && !shown(server) && shown(v2));
  assert.equal(b2.getAttribute('aria-pressed'), 'true');
});

test('the version is applied before the first text pass even while the document is still loading', () => {
  const { env, v1, v2 } = page({ href: `${BASE}?v=1` });
  env.document.readyState = 'loading';
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.version, '1');
  assert.ok(shown(v1) && !shown(v2));
});

test('the header has a labelled version switch, newest first, next to the language switch', () => {
  const html = readPage();
  const group = /<div class="version-switch"[^>]*>([\s\S]*?)<\/div>/.exec(html);
  assert.ok(group, 'the header has a version switch');
  assert.match(group[0], /role="group"/);
  assert.match(group[0], /aria-label="Версія колонки"/);
  assert.match(group[0], /data-i18n-attr="aria-label:header\.version_label"/);
  const buttons = [...group[1].matchAll(/<button type="button" data-set-version="(\d)" aria-pressed="(true|false)" data-i18n="([^"]+)">([^<]+)<\/button>/g)];
  assert.deepEqual(buttons.map((b) => [b[1], b[2], b[3], b[4]]), [
    ['2', 'true', 'header.version_2', 'Версія 2'],
    ['1', 'false', 'header.version_1', 'Версія 1'],
  ]);
  const header = /<header\b[\s\S]*?<\/header>/.exec(html)[0];
  assert.ok(header.includes('class="version-switch"') && header.indexOf('class="version-switch"') < header.indexOf('class="lang-switch"'));
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TEXT.uk['header.version_2'], 'Версія 2');
  assert.equal(TEXT.uk['header.version_1'], 'Версія 1');
  assert.equal(TEXT.uk['header.version_label'], 'Версія колонки');
  assert.equal(TEXT.en['header.version_2'], 'Version 2');
  assert.equal(TEXT.en['header.version_1'], 'Version 1');
  assert.equal(TEXT.en['header.version_label'], 'Speaker version');
});

test('the version script loads after the config and before the language script', () => {
  const scripts = [...readPage().matchAll(/<script src="assets\/([^"]+)"/g)].map((m) => m[1]);
  assert.ok(scripts.includes('version.js'));
  assert.equal(scripts.indexOf('version.js'), scripts.indexOf('config.js') + 1);
  assert.equal(scripts.indexOf('i18n.js'), scripts.indexOf('version.js') + 1);
});

test('the version switch looks like the language switch and hidden version blocks take no space', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /\.version-switch \{[^}]*display: inline-flex;/);
  assert.match(css, /\.version-switch button \{/);
  assert.match(css, /\[data-for-v\]\[hidden\] \{[^}]*display: none !important;/);
});

test('inside every section only the heading is shared: the rest of version 1 sits in version blocks', () => {
  const html = readPage();
  const ids = [.../<main>([\s\S]*?)<\/main>/.exec(html)[1].matchAll(/<section\b[^>]*\bid="([^"]+)"/g)].map((m) => m[1]);
  assert.ok(ids.length >= 12);
  for (const id of ids.filter((x) => x !== 'support')) {
    const section = parseFragment(sectionHtml(html, id)).children[0];
    if (section.hasAttribute('data-for-v')) {
      assert.equal(section.getAttribute('data-for-v'), '1', id);
      continue;
    }
    const kids = section.children.filter((el) => el.tagName !== 'h2');
    assert.ok(kids.some((kid) => kid.getAttribute('data-for-v') === '1'), `${id}: version 1 has a block`);
    for (const kid of kids) assert.ok(['1', '2'].includes(kid.getAttribute('data-for-v')), `${id}: <${kid.tagName}> must sit in a version block`);
  }
});

test('the server section and its contents entry belong to version 1 only', () => {
  const html = readPage();
  assert.match(html, /<section id="server" class="block" data-for-v="1">/);
  assert.match(html, /<li data-for-v="1"><a href="#server" data-i18n="server\.title">/);
});

const DATA_SCRIPTS = ['assets/text.js', 'assets/releases.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js', 'assets/data.js'];
const DATA_KEYS = ['PARTS', 'PARTS_DATE', 'PRINT', 'PRINT_GROUPS', 'TOOLS', 'WIRES'];
const RELEASE_LINE = /^    '(\d+)': \{ version: '(\d+\.\d+\.\d+)', tag: 'v(\d+\.\d+\.\d+)', stlDir: '([\w.-]+(?:\/[\w.-]+)*\/)' \},?$/;

function blockOffsets(html) {
  return [...html.matchAll(/data-for-v="(\d+)"/g)].map((m) => [m.index, m[1]]);
}

function versionAt(html, index) {
  let found = null;
  for (const [at, version] of blockOffsets(html)) if (at < index) found = version;
  return found;
}

test('releases.js names the newest release of each line: an N.N.N version, the tag v plus that version and the STL folder inside that tag', () => {
  const code = readSite('assets/releases.js');
  const lines = code.trimEnd().split('\n');
  assert.equal(lines[0], 'window.PYTAICHENKO = window.PYTAICHENKO || {};');
  assert.equal(lines[1], 'window.PYTAICHENKO.RELEASES = {');
  assert.equal(lines[lines.length - 1], '};');
  const rows = lines.slice(2, -1).map((line) => RELEASE_LINE.exec(line));
  assert.ok(rows.every(Boolean), 'every line has one release on one line');
  assert.deepEqual(rows.map((m) => m[1]), ['1', '2']);
  for (const m of rows) {
    assert.equal(m[2].split('.')[0], m[1], `line ${m[1]}: the version belongs to its line`);
    assert.equal(m[3], m[2], `line ${m[1]}: the tag is v plus the version`);
  }
  const { RELEASES } = loadScripts(['assets/releases.js']);
  assert.deepEqual(Object.keys(RELEASES), ['1', '2']);
  assert.deepEqual({ ...RELEASES['1'] }, { version: '1.0.0', tag: 'v1.0.0', stlDir: 'case/stl/' });
  assert.deepEqual({ ...RELEASES['2'] }, { version: '2.0.0', tag: 'v2.0.0', stlDir: 'v2/case/stl/' });
});

test('releases.js loads right before config.js, and the config no longer carries a version of its own', () => {
  const scripts = [...readPage().matchAll(/<script src="assets\/([^"]+)"/g)].map((m) => m[1]);
  assert.equal(scripts.indexOf('releases.js'), scripts.indexOf('config.js') - 1);
  assert.ok(scripts.indexOf('releases.js') > scripts.indexOf('text.js'));
  const { CONFIG } = loadScripts(['assets/config.js']);
  assert.equal(Object.prototype.hasOwnProperty.call(CONFIG, 'version'), false);
});

test('the footer shows the release number of the shown version, follows a version switch and a language switch, and keeps its license links', () => {
  const footer = parseFragment(/<footer\b[\s\S]*?<\/footer>/.exec(readPage())[0]);
  const b2 = makeElement({ 'data-set-version': '2', 'aria-pressed': 'true' });
  const b1 = makeElement({ 'data-set-version': '1', 'aria-pressed': 'false' });
  const env = makeEnv({ children: [b2, b1, footer], languages: ['uk'], href: BASE });
  const P = loadScripts(DATA_SCRIPTS, env);
  const slot = footer.querySelectorAll('[data-version]')[0];
  const links = () => footer.querySelectorAll('[data-repo-path]').map((a) => a.getAttribute('href'));
  const before = links();
  assert.deepEqual(before, [
    'https://github.com/feroxes/pytaichenko/blob/main/LICENSE',
    'https://github.com/feroxes/pytaichenko/blob/main/LICENSE-hardware',
    'https://github.com/feroxes/pytaichenko/releases',
  ]);
  assert.equal(slot.textContent, '2.0.0');
  b1.dispatchEvent({ type: 'click' });
  assert.equal(slot.textContent, '1.0.0');
  assert.deepEqual(links(), before);
  P.RELEASES['1'].version = '1.0.1';
  P.setLang('en');
  assert.equal(slot.textContent, '1.0.1', 'a language switch draws the number again');
  b2.dispatchEvent({ type: 'click' });
  assert.equal(slot.textContent, '2.0.0');
  assert.deepEqual(links(), before);

  const old = makeEnv({ children: [parseFragment(/<footer\b[\s\S]*?<\/footer>/.exec(readPage())[0])], languages: ['uk'], href: `${BASE}?v=1` });
  loadScripts(DATA_SCRIPTS, old);
  assert.equal(old.document.querySelectorAll('[data-version]')[0].textContent, '1.0.0');
});

test('the data of each version sits in P.DATA under the same keys, version 2 has all of it, and the old globals are the version 1 data', () => {
  const P = loadScripts(DATA_SCRIPTS, makeEnv());
  assert.deepEqual(Object.keys(P.DATA), ['1', '2']);
  for (const v of ['1', '2']) assert.deepEqual(Object.keys(P.DATA[v]).sort(), DATA_KEYS, v);
  for (const key of DATA_KEYS) assert.equal(P[key], P.DATA['1'][key], key);
  assert.equal(P.DATA['1'].PARTS.length, 18);
  assert.equal(P.DATA['1'].PRINT.length, 9);
  assert.equal(P.DATA['1'].WIRES.length, 16);
  for (const key of ['PARTS', 'TOOLS', 'PRINT', 'PRINT_GROUPS', 'WIRES']) assert.ok(P.DATA['2'][key].length > 0, key);
  assert.equal(P.PRINT_DIR, undefined);
});

test('every container draws its own dataset whatever version is shown, an empty or unknown dataset draws nothing, and a language switch redraws them', () => {
  const box = (attr, v, text = '') => {
    const el = makeElement({ [attr]: v }, text);
    el.innerHTML = '';
    return el;
  };
  const kinds = ['data-parts', 'data-tools', 'data-print', 'data-wires', 'data-parts-footnote'];
  const one = Object.fromEntries(kinds.map((k) => [k, box(k, '1')]));
  const two = Object.fromEntries(kinds.filter((k) => k !== 'data-wires').map((k) => [k, box(k, '2')]));
  const odd = Object.fromEntries(kinds.map((k) => [k, box(k, '7')]));
  const cost1 = box('data-cost', '1', 'Уточнюється');
  const cost2 = box('data-cost', '2', 'Уточнюється');
  const children = [...Object.values(one), ...Object.values(two), ...Object.values(odd), cost1, cost2];
  const env = makeEnv({ children, languages: ['uk'], href: `${BASE}?v=2` });
  const P = loadScripts(DATA_SCRIPTS, env);
  assert.equal(P.version, '2');
  assert.equal((one['data-parts'].innerHTML.match(/<tr\b/g) || []).length, 18);
  assert.equal((one['data-tools'].innerHTML.match(/<li>/g) || []).length, P.DATA['1'].TOOLS.length);
  assert.equal((one['data-print'].innerHTML.match(/class="print-group"/g) || []).length, 3);
  assert.equal((one['data-wires'].innerHTML.match(/<tr\b/g) || []).length, 16);
  assert.notEqual(one['data-parts-footnote'].textContent, '');
  assert.notEqual(cost1.textContent, 'Уточнюється');
  for (const k of ['data-parts', 'data-tools', 'data-print', 'data-wires']) assert.equal(odd[k].innerHTML, '', k);
  assert.equal(odd['data-parts-footnote'].textContent, '');
  assert.equal((two['data-parts'].innerHTML.match(/<tr\b/g) || []).length, P.DATA['2'].PARTS.length);
  assert.equal((two['data-tools'].innerHTML.match(/<li>/g) || []).length, P.DATA['2'].TOOLS.length);
  assert.equal((two['data-print'].innerHTML.match(/class="print-group"/g) || []).length, 3);
  assert.notEqual(two['data-parts-footnote'].textContent, '');
  assert.notEqual(cost2.textContent, 'Уточнюється');
  assert.notEqual(cost2.textContent, cost1.textContent);
  const drawn = one['data-parts'].innerHTML;
  P.setVersion('1');
  assert.equal(one['data-parts'].innerHTML, drawn, 'a version switch only shows and hides blocks');
  P.setLang('en');
  assert.ok(one['data-parts'].innerHTML.includes(P.TEXT.en['parts.name.devkit']));
  assert.ok(one['data-print'].innerHTML.includes(P.TEXT.en['print.name.body']));
  assert.ok(two['data-parts'].innerHTML.includes(P.TEXT.en['v2.parts.name.devkit']));
  assert.ok(two['data-print'].innerHTML.includes(P.TEXT.en['v2.print.name.top']));
});

test('the page names the dataset of every container and keeps each one in the block of its own version', () => {
  const html = readPage();
  for (const [attr, versions] of [['data-parts', ['1', '2']], ['data-tools', ['1', '2']], ['data-print', ['1', '2']], ['data-wires', ['1']], ['data-cost', ['1', '2']], ['data-parts-footnote', ['1', '2']]]) {
    const found = [...html.matchAll(new RegExp(`\\s${attr}(?:="([^"]*)")?[\\s>]`, 'g'))];
    assert.deepEqual(found.map((m) => m[1]), versions, `${attr} names its dataset`);
    for (const m of found) assert.equal(versionAt(html, m.index), m[1], `${attr}="${m[1]}" sits in a block of its version`);
  }
});

test('the per-version files: version 1 links its own zip and manifest, version 2 has its placeholders in version 2 blocks', () => {
  const html = readPage();
  const zips = [...html.matchAll(/href="(files\/[^"]+)"/g)];
  assert.deepEqual(zips.map((m) => [versionAt(html, m.index), m[1]]), [
    ['1', 'files/v1/pytaichenko-stl.zip'],
    ['2', 'files/v2/pytaichenko-stl.zip'],
  ]);
  const manifests = [...html.matchAll(/<esp-web-install-button manifest="([^"]+)">/g)];
  assert.deepEqual(manifests.map((m) => [versionAt(html, m.index), m[1]]), [
    ['1', 'firmware/v1/manifest.json'],
    ['2', 'firmware/v2/manifest.json'],
  ]);
});
