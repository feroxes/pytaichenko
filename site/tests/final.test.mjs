import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { loadScripts, makeElement, makeEnv, parseFragment, readPage, siteDir } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js'];
const FOOTER_SCRIPTS = ['assets/text.js', 'assets/releases.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js'];

const LESSONS = [
  {
    name: 'mic',
    uk: { symptom: /перше слово[^.]*обрізається/i, fix: /0,4\s*с/ },
    en: { symptom: /first word[^.]*cut off/i, fix: /0\.4\s*s/ },
  },
  {
    name: 'wake',
    uk: { symptom: /фраз/i, fix: /ще раз|друг/i },
    en: { symptom: /phrase/i, fix: /again|second/i },
  },
  {
    name: 'volume',
    uk: { symptom: /гучн|тих/i, fix: /70 %[\s\S]*15 %[\s\S]*дБ/ },
    en: { symptom: /loud|quiet|volume/i, fix: /70 %[\s\S]*15 %[\s\S]*dB/ },
  },
  {
    name: 'capacitor',
    uk: { symptom: /конденсатор/i, fix: /GND/ },
    en: { symptom: /capacitor/i, fix: /GND/ },
  },
  {
    name: 'button',
    uk: { symptom: /перемикач|кнопк/i, fix: /клей|клею/i },
    en: { symptom: /switch|button/i, fix: /glue/i },
  },
  {
    name: 'arc',
    uk: { symptom: /перемичк[\s\S]*деренч/i, fix: /0,16[\s\S]*0,04 мм[\s\S]*деренчати/ },
    en: { symptom: /bridges[\s\S]*rattle/i, fix: /0\.16[\s\S]*0\.04 mm[\s\S]*rattle/ },
  },
];

function readRepo(relPath) {
  return readFileSync(path.join(path.resolve(siteDir, '..'), relPath), 'utf8');
}

function sectionOf(page, id) {
  const m = new RegExp(`<section\\b[^>]*\\bid="${id}"[^>]*>([\\s\\S]*?)</section>`).exec(page);
  assert.ok(m, `section ${id} is missing`);
  return m[1];
}

function bodyOf(page, id) {
  const m = /<div class="section-body" data-for-v="1">([\s\S]*?)<\/div>\s*$/.exec(sectionOf(page, id).trim());
  assert.ok(m, `section ${id} needs a section-body`);
  return m[1];
}

function textKeysIn(html) {
  return [...html.matchAll(/data-i18n="([^"]+)"/g)].map((m) => m[1]);
}

function boot() {
  return loadScripts(SCRIPTS, makeEnv({ languages: ['uk-UA'] }));
}

test('no section is a coming-soon stub any more: no status label on the page, in the styles or in the texts', () => {
  const page = readPage();
  const { TEXT } = boot();
  assert.doesNotMatch(page, /soon-status|Готується/);
  assert.doesNotMatch(readFileSync(path.join(siteDir, 'assets', 'style.css'), 'utf8'), /soon-status/);
  for (const lang of ['uk', 'en']) {
    for (const key of Object.keys(TEXT[lang])) assert.doesNotMatch(key, /^(firmware|server)\.(status|text)$/, `${lang}.${key}`);
    for (const value of Object.values(TEXT[lang])) assert.doesNotMatch(value, /^(Готується|In preparation)$/, lang);
  }
});

test('troubleshooting lists at least these lessons', () => {
  const page = readPage();
  const { TEXT } = boot();
  const body = bodyOf(page, 'trouble');
  assert.match(body, /<dl class="trouble">/);
  const items = [...body.matchAll(/<div class="trouble-item">([\s\S]*?)<\/div>/g)].map((m) => m[1]);
  assert.ok(items.length >= LESSONS.length, `expected at least ${LESSONS.length} items, got ${items.length}`);
  for (const item of items) {
    assert.equal([...item.matchAll(/<dt\b/g)].length, 1);
    assert.equal([...item.matchAll(/<dd\b/g)].length, 1);
    assert.ok(item.indexOf('<dt') < item.indexOf('<dd'), 'the symptom comes before what to do');
  }
  const used = textKeysIn(body);
  for (const lesson of LESSONS) {
    const sKey = `trouble.${lesson.name}.symptom`;
    const fKey = `trouble.${lesson.name}.fix`;
    assert.ok(used.includes(sKey), `page misses ${sKey}`);
    assert.ok(used.includes(fKey), `page misses ${fKey}`);
    assert.ok(used.indexOf(sKey) + 1 === used.indexOf(fKey), `${lesson.name}: the fix follows its symptom`);
    for (const lang of ['uk', 'en']) {
      assert.match(TEXT[lang][sKey], lesson[lang].symptom, `${lang}.${sKey}`);
      assert.match(TEXT[lang][fKey], lesson[lang].fix, `${lang}.${fKey}`);
    }
  }
});

test('troubleshooting keeps a calm tone: no alarm, no blame, no selling', () => {
  const { TEXT } = boot();
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (!key.startsWith('trouble.') || key === 'trouble.title') continue;
      assert.doesNotMatch(value, /!|небезпечн|терміново|dangerous|urgent/i, `${lang}.${key}`);
      assert.ok(value.length <= 420, `${lang}.${key} is too long for a quick answer (${value.length})`);
    }
  }
});

test('switching the language rewrites the troubleshooting text', () => {
  const keys = [];
  for (const lesson of LESSONS) keys.push(`trouble.${lesson.name}.symptom`, `trouble.${lesson.name}.fix`);
  const nodes = keys.map((key) => makeElement({ 'data-i18n': key }, 'static'));
  const env = makeEnv({ children: nodes, languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  P.applyText(env.root, 'en');
  nodes.forEach((node, i) => assert.equal(node.textContent, P.TEXT.en[keys[i]], keys[i]));
  P.applyText(env.root, 'uk');
  nodes.forEach((node, i) => assert.equal(node.textContent, P.TEXT.uk[keys[i]], keys[i]));
});

function shown(el) {
  if (el.hasAttribute('data-i18n') || el.hasAttribute('data-version')) return el.textContent;
  return el.nodes.map((node) => (node.text !== undefined ? node.text : shown(node))).join('');
}

function footerLine(fragment) {
  return shown(fragment).replace(/\s+/g, ' ').trim();
}

test('the footer names both licenses and the release of the shown version, links them into the repository and reads right in both languages', () => {
  const page = readPage();
  const footer = /<footer\b[\s\S]*?<\/footer>/.exec(page)[0];
  assert.doesNotMatch(footer, /license-placeholder|footer\.license/);
  assert.doesNotMatch(readFileSync(path.join(siteDir, 'assets', 'style.css'), 'utf8'), /license-placeholder/);
  const fragment = parseFragment(footer);
  const env = makeEnv({ children: [fragment], languages: ['uk-UA'] });
  const P = loadScripts(FOOTER_SCRIPTS, env);
  const links = fragment.querySelectorAll('[data-repo-path]');
  assert.deepEqual(links.map((a) => a.getAttribute('href')), [
    'https://github.com/feroxes/pytaichenko/blob/main/LICENSE',
    'https://github.com/feroxes/pytaichenko/blob/main/LICENSE-hardware',
    'https://github.com/feroxes/pytaichenko/releases',
  ]);
  for (const a of links) {
    assert.equal(a.getAttribute('target'), '_blank');
    assert.match(a.getAttribute('rel'), /\bnoopener\b/);
  }
  assert.equal(P.version, '2');
  assert.equal(footerLine(fragment), 'Код — MIT, корпус і тексти — CC BY-SA 4.0 · Версія 2.0.0');
  P.setVersion('1');
  assert.equal(footerLine(fragment), 'Код — MIT, корпус і тексти — CC BY-SA 4.0 · Версія 1.0.0');
  P.setLang('en');
  assert.equal(footerLine(fragment), 'Code — MIT, case and texts — CC BY-SA 4.0 · Version 1.0.0');
  P.RELEASES['1'].version = '1.2.3';
  P.applyConfig(env.document);
  assert.match(footerLine(fragment), /· Version 1\.2\.3$/);
  assert.equal(/<span data-version>([^<]*)<\/span>/.exec(footer)[1], '', 'before scripts run the page names no release');
  assert.match(footer, /<span data-version-box hidden>·\s*<a data-repo-path="\/releases"/, 'before scripts run the release link is hidden');
  for (const file of ['LICENSE', 'LICENSE-hardware']) assert.ok(existsSync(path.join(path.resolve(siteDir, '..'), file)), `${file} is missing`);
});

test('the release workflow publishes the site/ folder to Pages, built from main and the newest release of each line', () => {
  const file = path.join(path.resolve(siteDir, '..'), '.github', 'workflows', 'release.yml');
  assert.ok(existsSync(file), '.github/workflows/release.yml is missing at the repo root');
  assert.ok(!existsSync(path.join(path.dirname(file), 'pages.yml')), 'Pages are published only from releases');
  const yml = readRepo('.github/workflows/release.yml');
  assert.match(yml, /uses:\s*actions\/upload-pages-artifact@v\d+/);
  assert.match(yml, /uses:\s*actions\/deploy-pages@v\d+/);
  assert.match(yml, /uses:\s*actions\/upload-pages-artifact@v\d+\s*\n\s*with:\s*\n\s*path:\s*site\s*(\n|$)/);
  assert.match(yml, /^\s*pages:\s*write\s*$/m);
  assert.match(yml, /^\s*id-token:\s*write\s*$/m);
  assert.match(yml, /name:\s*github-pages/);
  assert.match(yml, /concurrency:\s*\n\s*group:\s*pages\s*\n\s*cancel-in-progress:\s*false/);
  assert.match(yml, /^  push:\n    tags: \["v\*\.\*\.\*"\]$/m);
  assert.match(yml, /workflow_dispatch:\s*\n\s*inputs:\s*\n\s*tag:/);
  assert.ok(yml.indexOf('rm -rf site/tests') < yml.indexOf('upload-pages-artifact'), 'the test sources stay out of the published site');
  assert.ok(yml.indexOf('ref: main') < yml.indexOf('rm -rf site/tests'), 'the published site is the one on main');
  assert.doesNotMatch(yml, /path:\s*(\.|\/|\.\/)\s*$/m, 'only the site/ folder is published');
  assert.doesNotMatch(yml, /secrets\./);
});

test('the workflow runs actions in order: upload the artifact, then deploy it', () => {
  const yml = readRepo('.github/workflows/release.yml');
  assert.ok(yml.indexOf('upload-pages-artifact') < yml.indexOf('deploy-pages'));
  assert.match(yml, /id:\s*deployment/);
  assert.match(yml, /steps\.deployment\.outputs\.page_url/);
});

test('troubleshooting says what a builder hears and matches the print notes: a clipped first word, a gap and a rattle, a scale linear in dB', () => {
  const { TEXT } = boot();
  assert.match(TEXT.uk['trouble.mic.fix'], /слухаю/);
  assert.match(TEXT.en['trouble.mic.fix'], /listening/);
  for (const lang of ['uk', 'en']) {
    assert.doesNotMatch(TEXT[lang]['trouble.volume.fix'], /логарифм|logarithm/i, lang);
  }
  assert.match(TEXT.uk['trouble.volume.fix'], /лінійна в децибелах/);
  assert.match(TEXT.en['trouble.volume.fix'], /linear in decibels/);
  for (const lang of ['uk', 'en']) {
    assert.doesNotMatch(TEXT[lang]['trouble.volume.fix'], /0[,.]78|1[,.]0\b/, `${lang}: no fixed calibrated levels`);
  }
  assert.match(TEXT.uk['trouble.volume.fix'], /гучність 80 відсотків/);
  assert.match(TEXT.en['trouble.volume.fix'], /set the volume to 80 percent/);
  assert.match(TEXT.uk['trouble.arc.fix'], /0,12 мм/);
  assert.match(TEXT.en['trouble.arc.fix'], /0\.12 mm/);
  assert.match(TEXT.uk['trouble.arc.fix'], /нижчими за перемички на 0,04 мм/);
  assert.match(TEXT.en['trouble.arc.fix'], /0\.04 mm lower than the bridges/);
  assert.match(TEXT.uk['print.how.layer'], /нижчими за перемички на 0,04 мм/);
  assert.match(TEXT.en['print.how.layer'], /0\.04 mm lower than the bridges/);
  assert.match(TEXT.uk['print.how.layer'], /0,12 мм[\s\S]*перемички/);
  assert.match(TEXT.en['print.how.layer'], /0\.12 mm[\s\S]*bridges/);
});

test('one English word per part: bridges, never webs; no microphone socket or boot; the pin map side named the same way in both languages', () => {
  const { TEXT } = boot();
  for (const [key, value] of Object.entries(TEXT.en)) {
    assert.doesNotMatch(value, /\bwebs?\b/i, key);
    if (/mic|print\.optional|parts\.note\.tpu/.test(key)) assert.doesNotMatch(value, /\bsocket/i, key);
  }
  assert.ok(!('print.name.mic_boot' in TEXT.en) && !('print.name.mic_boot' in TEXT.uk));
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) assert.doesNotMatch(value, /microphone boot|mic boot|гніздо мікрофона|гнізда мікрофона/i, `${lang} ${key}`);
  }
  assert.match(TEXT.uk['solder.pinmap.caption'], /^Вигляд з боку написів \(деталей\), антена вгорі\./);
  assert.match(TEXT.en['solder.pinmap.caption'], /^Labels \(component\) side up, antenna at the top\./);
  assert.match(TEXT.uk['solder.pinmap.alt'], /вигляд з боку написів \(деталей\)/);
  assert.match(TEXT.en['solder.pinmap.alt'], /labels \(component\) side up/);
});

test('the page has a small favicon file linked from <head>, an svg with nothing to load', () => {
  const page = readPage();
  const head = /<head>([\s\S]*?)<\/head>/.exec(page)[1];
  const link = /<link\b([^>]*\brel="icon"[^>]*)>/.exec(head);
  assert.ok(link, 'no <link rel="icon"> in <head>');
  assert.match(link[1], /\bhref="assets\/favicon\.svg"/);
  assert.match(link[1], /\btype="image\/svg\+xml"/);
  const file = path.join(siteDir, 'assets', 'favicon.svg');
  assert.ok(existsSync(file));
  const svg = readFileSync(file, 'utf8');
  assert.match(svg, /^<svg\b[^>]*\bxmlns="http:\/\/www\.w3\.org\/2000\/svg"[^>]*\bviewBox="0 0 64 64"/);
  assert.doesNotMatch(svg.replace('http://www.w3.org/2000/svg', ''), /https?:|<script|<image|href=|<!--/);
  assert.ok(svg.length < 1024, 'small');
  assert.doesNotMatch(head, /data:image/, 'not inlined');
});

const README_GUIDE = 'https://feroxes.github.io/pytaichenko/';
const README_PAGES = {
  2: ['parts', 'print', 'solder', 'assembly', 'firmware', 'trouble'],
  1: ['parts', 'print', 'solder', 'assembly', 'firmware', 'server', 'trouble'],
};

test('the root READMEs describe both versions and link each one to its own pages of the guide', () => {
  for (const [file, lang] of [['README.md', 'en'], ['README.uk.md', 'uk']]) {
    const text = readRepo(file);
    const intro = text.split('\n## ', 1)[0];
    for (const v of ['2', '1']) assert.ok(intro.includes(`${README_GUIDE}?lang=${lang}&v=${v}"`) || intro.includes(`${README_GUIDE}?lang=${lang}&v=${v})`), `${file} intro links to version ${v}`);
    assert.ok(intro.indexOf('&v=2') < intro.indexOf('&v=1'), `${file} puts version 2 first`);
    assert.match(intro, /ESP32-S3/, `${file} names the version 2 board`);
    assert.match(intro, /Docker/, `${file} says version 1 needs Docker`);
    for (const [v, anchors] of Object.entries(README_PAGES)) {
      for (const anchor of anchors) assert.ok(text.includes(`${README_GUIDE}?lang=${lang}&v=${v}#${anchor}`), `${file} links version ${v} #${anchor}`);
    }
    const links = [...text.matchAll(/https:\/\/feroxes\.github\.io\/pytaichenko\/[^)"\s]*/g)].map((m) => m[0]);
    assert.ok(links.length > 0);
    for (const link of links) assert.match(link, new RegExp(`^https://feroxes\\.github\\.io/pytaichenko/\\?lang=${lang}&v=[12](#[a-z]+)?$`), `${file}: ${link}`);
    assert.ok(!text.includes('&v=2#server'), `${file}: version 2 has no server`);
    assert.match(text, /releases\/latest/, `${file} keeps the latest release badge`);
    const releases = text.split(lang === 'en' ? '\n## Versions and releases\n' : '\n## Версії й випуски\n')[1].split('\n## ')[0];
    for (const name of ['`v1.x.y`', '`v2.x.y`', '`pytaichenko-1.Y.Z.factory.bin`', '`pytaichenko-1.Y.Z-stl.zip`', '`pytaichenko-2.Y.Z.factory.bin`', '`pytaichenko-2.Y.Z.ota.bin`', '`pytaichenko-2.Y.Z-stl.zip`']) {
      assert.ok(releases.includes(name), `${file} releases name ${name}`);
    }
    assert.match(releases, lang === 'en' ? /Latest[^\n]*version 2/ : /Останній випуск[^\n]*версії 2/, `${file} says Latest shows version 2`);
    const license = text.split(lang === 'en' ? '\n## License\n' : '\n## Ліцензія\n')[1].split('\n## ')[0];
    for (const p of ['`v1/case/*.scad`', '`v2/case/*.scad`', '`v1/case/stl`', '`v2/case/stl`', '`site/assets/renders`']) assert.ok(license.includes(p), `${file} license covers ${p}`);
  }
});

function lockedComponents() {
  const lock = readRepo('v2/firmware/dependencies.lock');
  const block = lock.split('\ndirect_dependencies:')[0].replace(/^dependencies:\n/, '');
  const out = [];
  let name = null;
  for (const line of block.split('\n')) {
    const key = /^ {2}([\w./-]+):$/.exec(line);
    if (key) name = key[1];
    const version = /^ {4}version: (\S+)$/.exec(line);
    if (version && name) out.push([name, version[1]]);
  }
  return out;
}

test('THIRD_PARTY.md names every component of the version 2 firmware with the version in dependencies.lock and its license', () => {
  const notes = readRepo('THIRD_PARTY.md');
  const section = notes.split('\n## Version 2 firmware\n')[1];
  assert.ok(section, 'a Version 2 firmware section');
  const components = lockedComponents();
  assert.ok(components.length >= 10, 'every locked component is read');
  for (const [name, version] of components) {
    const shown = name === 'idf' ? 'ESP-IDF' : name;
    const row = section.split('\n').find((line) => line.startsWith('|') && line.includes(name === 'idf' ? '| ESP-IDF |' : `\`${shown}\``));
    assert.ok(row, `${shown} has a row`);
    assert.ok(row.includes(version), `${shown} ${version}`);
    assert.match(row, /Apache-2\.0|MIT|BSD/, `${shown} has a license`);
  }
  const commit = /^COMMIT=([0-9a-f]{40})$/m.exec(readRepo('v2/firmware/models/fetch.sh'))[1];
  assert.ok(section.includes(commit), 'the models commit');
  assert.ok(section.includes('v2/firmware/models/LICENSE'));
  for (const word of ['ESPRESSIF MIT License', 'TensorFlow Lite Micro', 'kissfft', 'cJSON', 'Mbed TLS', 'Unity', 'libsoxr', 'LGPL-2.1']) assert.ok(section.includes(word), word);
  const webTools = /unpkg\.com\/esp-web-tools@([\d.]+)\//.exec(readPage())[1];
  assert.ok(notes.includes(`ESP Web Tools`) && notes.includes(webTools), `ESP Web Tools ${webTools}`);
  assert.doesNotMatch(section, /Open question/);
  assert.match(section, /not in the repository/);
  assert.match(section, /`say`/);
  assert.ok(section.includes('v2/firmware/tests/host/golden/'), 'the path of the clips');
  assert.ok(notes.includes('\n## ESPHome\n') && notes.includes('`v1/firmware/packages/wake_word.yaml`'), 'the version 1 sections stay');
});

test('LICENSE-hardware covers the version 2 case and renders', () => {
  const text = readRepo('LICENSE-hardware');
  for (const p of ['v1/case/*.scad', 'v1/case/stl/*', 'v2/case/*.scad', 'v2/case/stl/*', 'site/assets/renders/*', 'site/assets/renders/v2/*']) assert.ok(text.includes(p), p);
});
