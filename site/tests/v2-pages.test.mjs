import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { loadScripts, makeElement, makeEnv, parseFragment, readPage, readSite, sectionHtml, siteDir } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/releases.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js', 'assets/data.js'];
const BASE = 'https://example.github.io/pytaichenko/';
const SECTIONS = ['about', 'can', 'need', 'parts', 'tools', 'print'];

const PART_ROWS = [
  ['devkit', 1],
  ['mic', 2],
  ['amp', 1],
  ['speaker', 1],
  ['filament', 1],
  ['mosfet', 1],
  ['res47', 2],
  ['res62k', 1],
  ['cap', 1],
  ['button', 1],
  ['inserts', 1],
  ['screws', 1],
  ['washers', 2],
  ['wires', 'set'],
  ['charger', 'set'],
  ['petg_black', 1],
  ['petg_white', 1],
  ['tpu', 1],
];

const IMRAD_ROWS = {
  devkit: { uah: 421, url: 'https://imrad.com.ua/ua/plata-rozrobnika-esp32-s3-devkitc-n16r8' },
  speaker: { uah: 712, url: 'https://imrad.com.ua/ua/vs-frs-5-x-4' },
};

const OWN_ROWS = {
  amp: { shop: 'menstudio', uah: 75, url: 'https://menstudio.kyiv.ua/ua/p2712693157-modul-tsap-dekodera.html' },
};

const SHOP_HOSTS = {
  'arduino.ua': ['arduino.ua'],
  menstudio: ['menstudio.kyiv.ua'],
  aliexpress: ['www.aliexpress.com', 'aliexpress.com'],
  rcs: ['www.rcscomponents.kiev.ua'],
  imrad: ['imrad.com.ua'],
};

const TOOL_IDS = ['printer', 'iron', 'solder', 'insert_tip', 'multimeter', 'cutters', 'glue_gun', 'screwdriver', 'flat_screwdriver', 'battery', 'marker', 'tape'];

const GROUPS = [
  ['petg_black', 'petg'],
  ['petg_white', 'arc'],
  ['tpu', 'tpu'],
];

const V1_ONLY = /server|сервер|Docker|DevKit V1|ESPHome|voice-hub|compose|D33|D25|D32/i;

function readRepo(relPath) {
  return readFileSync(path.join(path.resolve(siteDir, '..'), relPath), 'utf8');
}

function unescapeHtml(text) {
  return text.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
}

function plain(html) {
  return unescapeHtml(html.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim());
}

function built() {
  const script = readRepo('v2/case/build.sh');
  const block = /^PARTS=\(\n([\s\S]*?)\n\)$/m.exec(script);
  assert.ok(block, 'build.sh has a PARTS list');
  return [...block[1].matchAll(/"(\w+):([\w-]+)"/g)].map((m) => [m[1], `${m[2]}.stl`]);
}

function boxes() {
  const make = (attr, text = '') => {
    const el = makeElement({ [attr]: '2' }, text);
    el.innerHTML = '';
    return el;
  };
  return {
    parts: make('data-parts'),
    tools: make('data-tools'),
    print: make('data-print'),
    cost: make('data-cost', 'Уточнюється'),
    note: make('data-parts-footnote'),
  };
}

function boot(lang = 'uk') {
  const b = boxes();
  const env = makeEnv({ children: Object.values(b), languages: [lang], href: `${BASE}?v=2&lang=${lang}` });
  const P = loadScripts(SCRIPTS, env);
  return { P, env, ...b };
}

function v2Blocks(html) {
  const out = [];
  const re = /<div\b[^>]*\bdata-for-v="2"[^>]*>/g;
  let m;
  while ((m = re.exec(html))) {
    const block = html.slice(m.index, m.index + closingOf(html.slice(m.index)));
    parseFragment(block);
    out.push({ at: m.index, html: block });
  }
  return out;
}

function closingOf(html) {
  let depth = 0;
  const re = /<(\/?)div\b[^>]*>/g;
  let m;
  while ((m = re.exec(html))) {
    depth += m[1] ? -1 : 1;
    if (depth === 0) return m.index + m[0].length;
  }
  throw new Error('unclosed div');
}

function keysOf(html) {
  const keys = [...html.matchAll(/data-i18n="([^"]+)"/g)].map((m) => m[1]);
  for (const m of html.matchAll(/data-i18n-attr="([^"]+)"/g)) {
    for (const pair of m[1].split(';')) keys.push(pair.slice(pair.indexOf(':') + 1).trim());
  }
  return keys;
}

function v2Texts(TEXT) {
  const out = [];
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) if (key.startsWith('v2.')) out.push([`${lang}.${key}`, value]);
  }
  return out;
}

function pngSize(file) {
  const bytes = readFileSync(file);
  assert.deepEqual([...bytes.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  return [bytes.readUInt32BE(16), bytes.readUInt32BE(20)];
}

test('version 2 parts are the agreed rows in assembly order with their quantities', () => {
  const { P } = boot();
  assert.deepEqual(Array.from(P.DATA['2'].PARTS, (p) => [p.id, p.qty]), PART_ROWS);
});

test('the IMRAD rows carry their price and the product link verbatim, and IMRAD is an allowed shop with product pages only', () => {
  const { P } = boot();
  for (const [id, row] of Object.entries(IMRAD_ROWS)) {
    const part = P.DATA['2'].PARTS.find((p) => p.id === id);
    assert.equal(part.shop, 'imrad', id);
    assert.equal(part.uah, row.uah, id);
    assert.equal(part.url, row.url, id);
  }
  for (const p of P.DATA['2'].PARTS) {
    assert.ok(p.shop === null || p.shop in SHOP_HOSTS, `${p.id}: shop ${p.shop}`);
    if (p.url === null) continue;
    const u = new URL(p.url);
    assert.equal(u.protocol, 'https:', p.id);
    assert.ok(SHOP_HOSTS[p.shop].includes(u.hostname), `${p.id}: host ${u.hostname}`);
    assert.equal(u.search, '', `${p.id}: no query`);
    if (p.shop === 'imrad') assert.match(u.pathname, /^\/ua\/[a-z0-9-]+$/, p.id);
  }
  for (const lang of ['uk', 'en']) assert.equal(P.TEXT[lang]['parts.shop.imrad'], 'IMRAD', lang);
});

test('the version 2 amplifier is its own row: the MENstudio module at 75 ₴', () => {
  const { P } = boot();
  for (const [id, row] of Object.entries(OWN_ROWS)) {
    const part = P.DATA['2'].PARTS.find((p) => p.id === id);
    assert.deepEqual({ shop: part.shop, uah: part.uah, url: part.url }, row, id);
  }
});

test('every other row is the version 1 row with the same shop, link, price, pack and kit fields; only the count may differ', () => {
  const { P } = boot();
  const v1 = new Map(P.DATA['1'].PARTS.map((p) => [p.id, p]));
  for (const p of P.DATA['2'].PARTS) {
    if (p.id in IMRAD_ROWS || p.id in OWN_ROWS) continue;
    assert.ok(v1.has(p.id), `${p.id} is a version 1 row`);
    const { qty: a, ...mine } = p;
    const { qty: b, ...theirs } = v1.get(p.id);
    assert.deepEqual({ ...mine }, { ...theirs }, p.id);
  }
  assert.equal(P.DATA['2'].PARTS_DATE, P.DATA['1'].PARTS_DATE);
  const washers = P.DATA['2'].PARTS.find((p) => p.id === 'washers');
  assert.equal(washers.url, null);
  assert.equal(washers.generic, true);
});

test('every version 2 part has its own name, and its note in both languages or in none', () => {
  const { P } = boot();
  for (const p of P.DATA['2'].PARTS) {
    for (const lang of ['uk', 'en']) assert.ok(P.TEXT[lang][`v2.parts.name.${p.id}`], `${lang} v2.parts.name.${p.id}`);
    const note = `v2.parts.note.${p.id}`;
    assert.equal(note in P.TEXT.uk, note in P.TEXT.en, note);
  }
  assert.match(P.TEXT.uk['v2.parts.name.devkit'], /ESP32-S3-DevKitC N16R8/);
  assert.match(P.TEXT.en['v2.parts.name.speaker'], /Visaton FRS 5 X speaker, 8 Ω/);
  assert.match(P.TEXT.uk['v2.parts.name.speaker'], /Visaton FRS 5 X, 8 Ом/);
  assert.match(P.TEXT.uk['v2.parts.note.inserts'], /9 вставок/);
  assert.match(P.TEXT.en['v2.parts.note.inserts'], /9 M2\.5 inserts/);
  assert.match(P.TEXT.uk['v2.parts.note.screws'], /9 гвинтів/);
  assert.match(P.TEXT.en['v2.parts.note.screws'], /9 screws/);
  assert.match(P.TEXT.uk['v2.parts.note.screws'], /хрестов/);
  assert.match(P.TEXT.en['v2.parts.note.screws'], /Phillips/);
});

test('the version 2 table renders every row with its own names, links and prices in both languages', () => {
  for (const lang of ['uk', 'en']) {
    const { P, parts } = boot(lang);
    const rows = [...parts.innerHTML.matchAll(/<tr>([\s\S]*?)<\/tr>/g)].map((m) => m[1]);
    assert.equal(rows.length, PART_ROWS.length, lang);
    P.DATA['2'].PARTS.forEach((p, i) => {
      assert.ok(plain(rows[i]).startsWith(P.TEXT[lang][`v2.parts.name.${p.id}`]), `${lang} ${p.id}`);
      if (p.url) assert.ok(rows[i].includes(`href="${p.url}"`), `${lang} ${p.id} link`);
    });
    assert.ok(plain(rows[0]).includes(lang === 'uk' ? '421 ₴' : '421 ₴ (≈ $9.2)'), `${lang} devkit price`);
    assert.ok(rows[0].includes('>IMRAD</a>'));
    if (lang === 'en') assert.doesNotMatch(plain(parts.innerHTML), /[Ѐ-ӿ]/);
  }
});

test('the version 2 cost is the sum of its priced parts by the shared cost logic: 2 103 ₴ without the charger, small parts and plastic', () => {
  const { P, cost, note } = boot();
  const total = P.partsTotal('2');
  assert.equal(total.uah, 2103);
  assert.deepEqual(Array.from(P.costLeft('2')), ['charger', 'small', 'plastic']);
  assert.equal(cost.textContent, 'від 2 103 ₴ без зарядки, дрібниць і пластику');
  assert.notEqual(note.textContent, '');
  P.setLang('en');
  assert.equal(cost.textContent, `from 2,103 ₴ (≈ $${total.usd}), excluding the charger, small parts and plastic`);
});

test('version 2 tools follow its steps: a Phillips screwdriver, no glue for the feet and no hex key', () => {
  for (const lang of ['uk', 'en']) {
    const { P, tools } = boot(lang);
    assert.deepEqual(Array.from(P.DATA['2'].TOOLS, (t) => t.id), TOOL_IDS);
    const items = [...tools.innerHTML.matchAll(/<li>([^<]*)<\/li>/g)].map((m) => unescapeHtml(m[1]));
    assert.deepEqual(items, TOOL_IDS.map((id) => P.TEXT[lang][`v2.tools.item.${id}`]));
  }
  const { P } = boot();
  assert.match(P.TEXT.uk['v2.tools.item.screwdriver'], /Хрестова/);
  assert.match(P.TEXT.en['v2.tools.item.screwdriver'], /Phillips/);
  for (const [name, value] of v2Texts(P.TEXT)) assert.doesNotMatch(value, /шестигран|hex key|Allen/i, name);
});

test('version 2 print rows are exactly the files v2/case/build.sh writes, with the count from _xN and the plastic from the prefix', () => {
  const { P } = boot();
  const rows = P.DATA['2'].PRINT;
  assert.deepEqual(Array.from(rows, (r) => [r.id, r.file]), built());
  assert.equal(rows.length, 11);
  for (const r of rows) {
    const m = /_x(\d+)\.stl$/.exec(r.file);
    assert.equal(r.qty, m ? Number(m[1]) : 1, r.file);
    const plastic = r.file.startsWith('PETG-black_') ? 'petg_black' : r.file.startsWith('PETG-white_') ? 'petg_white' : r.file.startsWith('TPU_') ? 'tpu' : null;
    assert.equal(r.plastic, plastic, r.file);
    for (const lang of ['uk', 'en']) assert.ok(P.TEXT[lang][`v2.print.name.${r.id}`], `${lang} ${r.id}`);
  }
  assert.deepEqual(Array.from(P.DATA['2'].PRINT_GROUPS, (g) => [g.plastic, g.settings]), GROUPS);
  for (const [plastic, settings] of GROUPS) {
    for (const lang of ['uk', 'en']) {
      assert.ok(P.TEXT[lang][`v2.print.plastic.${plastic}`], `${lang} ${plastic}`);
      assert.match(P.TEXT[lang][`v2.print.settings.${settings}`], lang === 'uk' ? /шар 0,16? мм.*без опор/ : /layer 0\.16? mm.*no supports/, `${lang} ${settings}`);
    }
  }
});

test('version 2 print links point at the files inside the v2.0.0 tag, in groups by plastic', () => {
  for (const lang of ['uk', 'en']) {
    const { P, print } = boot(lang);
    const groups = [...print.innerHTML.matchAll(/<div class="print-group" data-plastic="(\w+)">/g)].map((m) => m[1]);
    assert.deepEqual(groups, GROUPS.map(([plastic]) => plastic));
    const hrefs = [...print.innerHTML.matchAll(/<a href="([^"]+)"/g)].map((m) => unescapeHtml(m[1]));
    assert.equal(hrefs.length, 11);
    for (const r of P.DATA['2'].PRINT) {
      assert.ok(hrefs.includes(`https://github.com/feroxes/pytaichenko/raw/v2.0.0/v2/case/stl/${r.file}`), r.file);
    }
    assert.ok(plain(print.innerHTML).includes(P.TEXT[lang]['v2.print.note.body']));
    if (lang === 'en') assert.doesNotMatch(plain(print.innerHTML), /[Ѐ-ӿ]/);
  }
});

test('P.DATA no longer carries a print folder: links read the folder of the release', () => {
  const { P } = boot();
  for (const v of ['1', '2']) assert.deepEqual(Object.keys(P.DATA[v]).sort(), ['PARTS', 'PARTS_DATE', 'PRINT', 'PRINT_GROUPS', 'TOOLS', 'WIRES'], v);
  assert.equal(P.PRINT_DIR, undefined);
  assert.doesNotMatch(readFileSync(path.join(siteDir, 'assets/data.js'), 'utf8'), /PRINT_DIR/);
});

test('each section from about to printing has one version 2 block, and every text key in it is a v2 key', () => {
  const html = readPage();
  for (const id of SECTIONS) {
    const section = sectionHtml(html, id);
    const blocks = v2Blocks(section);
    assert.equal(blocks.length, 1, `${id}: one version 2 block`);
    const keys = keysOf(blocks[0].html);
    assert.ok(keys.length > 0 || /data-tools="2"/.test(blocks[0].html), `${id}: the block has text or its list`);
    for (const key of keys) assert.ok(key.startsWith('v2.'), `${id}: ${key} is not a v2 key`);
  }
});

test('v2 keys exist in both languages with the same set and no empty values', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const uk = Object.keys(TEXT.uk).filter((k) => k.startsWith('v2.')).sort();
  const en = Object.keys(TEXT.en).filter((k) => k.startsWith('v2.')).sort();
  assert.ok(uk.length > 80);
  assert.deepEqual(uk, en);
  for (const [name, value] of v2Texts(TEXT)) assert.ok(value.trim(), name);
});

test('the version 2 hero has the page heading, a one-sentence lead about talking with OpenAI over Wi-Fi and the render of the closed speaker', () => {
  const html = readPage();
  const about = sectionHtml(html, 'about');
  const block = v2Blocks(about)[0].html;
  assert.match(block, /^<div class="hero-body" data-for-v="2">/);
  assert.match(block, /<h1 data-i18n="v2\.hero\.title">/);
  assert.match(block, /<p class="lead" data-i18n="v2\.hero\.lead">/);
  const img = /<img\b[^>]*>/.exec(block)[0];
  assert.match(img, /class="hero-media"/);
  assert.match(img, /src="assets\/photos-v2\/speaker\.jpg"/);
  assert.match(img, /width="1200" height="900"/);
  assert.match(img, /data-i18n-attr="alt:v2\.hero\.image"/);
  const jpg = readFileSync(path.join(siteDir, 'assets/photos-v2/speaker.jpg'));
  assert.equal(jpg.readUInt16BE(0), 0xffd8);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['v2.hero.lead'], /OpenAI/);
  assert.match(TEXT.uk['v2.hero.lead'], /через Wi-Fi/);
  assert.match(TEXT.en['v2.hero.lead'], /OpenAI/);
  assert.match(TEXT.en['v2.hero.lead'], /over Wi-Fi/);
  for (const lang of ['uk', 'en']) {
    assert.equal(TEXT[lang]['v2.hero.lead'].split(/[.:](?:\s|$)/).filter((s) => s.trim()).length, 1, `${lang}: one sentence`);
    assert.ok(TEXT[lang]['v2.hero.image'].length > 40, `${lang}: the alt describes the picture`);
  }
  assert.equal((html.match(/<h1\b/g) || []).length, 2, 'one h1 per version');
});

test('what it does lists the version 1 abilities plus the settings page on the speaker and updates from it', () => {
  const block = v2Blocks(sectionHtml(readPage(), 'can'))[0].html;
  const keys = keysOf(block);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.ok(keys.includes('v2.can.settings') && keys.includes('v2.can.update'));
  assert.match(TEXT.uk['v2.can.settings'], /сторінц/);
  assert.match(TEXT.en['v2.can.settings'], /page/);
  assert.match(TEXT.uk['v2.can.update'], /оновлю/i);
  assert.match(TEXT.en['v2.can.update'], /update/i);
  assert.match(TEXT.uk['v2.can.wake'], /Hey Jarvis, Okay Nabu або Hey Mycroft/);
});

test('before you start in version 2: only the print time and the cost; the OpenAI key and 2.4 GHz Wi-Fi are in the firmware list', () => {
  const block = v2Blocks(sectionHtml(readPage(), 'need'))[0].html;
  const keys = keysOf(block);
  assert.doesNotMatch(block, /<ul\b|<li\b/);
  assert.deepEqual(keys, ['v2.need.time_label', 'v2.need.time_value', 'v2.need.cost_label', 'v2.need.cost_pending']);
  const firmware = keysOf(v2Blocks(sectionHtml(readPage(), 'firmware'))[0].html);
  for (const key of ['v2.firmware.need.wifi', 'v2.firmware.need.key']) assert.ok(firmware.includes(key), key);
  for (const key of ['v2.need.time_label', 'v2.need.time_value', 'v2.need.cost_label', 'v2.need.cost_pending']) {
    assert.ok(keys.includes(key), key);
  }
  assert.match(block, /<dd id="need-time-v2" data-i18n="v2\.need\.time_value">/);
  assert.match(block, /<dd id="need-cost-v2" data-cost="2" data-i18n="v2\.need\.cost_pending">/);
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const lang of ['uk', 'en']) {
    for (const gone of ['v2.need.printer', 'v2.need.solder', 'v2.need.charger', 'v2.need.key', 'v2.need.wifi']) assert.equal(TEXT[lang][gone], undefined, `${lang} ${gone}`);
  }
  assert.equal(TEXT.uk['v2.need.time_value'], 'близько 5–6 годин');
  assert.equal(TEXT.en['v2.need.time_value'], 'about 5–6 hours');
  assert.equal(TEXT.uk['v2.tools.item.printer'], '3D-принтер, що друкує PETG і TPU');
  assert.equal(TEXT.en['v2.tools.item.printer'], 'A 3D printer that prints PETG and TPU');
});

test('parts, tools and print in version 2 draw their own datasets, and the print block has its archive, note and how-to, with no slicer estimate', () => {
  const html = readPage();
  const parts = v2Blocks(sectionHtml(html, 'parts'))[0].html;
  assert.match(parts, /<tbody data-parts="2"><\/tbody>/);
  assert.deepEqual([...parts.matchAll(/<th scope="col" data-i18n="([^"]+)">/g)].map((m) => m[1]), ['v2.parts.col.part', 'v2.parts.col.qty', 'v2.parts.col.price', 'v2.parts.col.link']);
  assert.match(parts, /<p class="parts-footnote" data-parts-footnote="2"><\/p>/);
  assert.match(v2Blocks(sectionHtml(html, 'tools'))[0].html, /<ul class="marks" data-tools="2"><\/ul>/);
  const print = v2Blocks(sectionHtml(html, 'print'))[0].html;
  const order = ['v2.print.intro', 'v2.print.common', 'v2.print.archive"', 'v2.print.archive_note', 'data-print="2"', 'v2.print.how.title'].map((k) => print.indexOf(k));
  assert.ok(order.every((at) => at > 0), JSON.stringify(order));
  assert.deepEqual([...order].sort((a, b) => a - b), order);
  assert.match(print, /<a href="files\/v2\/pytaichenko-stl\.zip" download data-i18n="v2\.print\.archive">/);
  const keys = keysOf(print);
  for (const key of ['v2.print.how.orient', 'v2.print.how.seam', 'v2.print.how.supports', 'v2.print.how.layer', 'v2.print.how.walls', 'v2.print.how.tpu']) assert.ok(keys.includes(key), key);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['v2.print.archive_note'], /v2\/case\/stl/);
  assert.match(TEXT.en['v2.print.archive_note'], /v2\/case\/stl/);
  assert.match(TEXT.uk['v2.print.how.seam'], /180°/);
  assert.match(TEXT.en['v2.print.how.seam'], /180°/);
  assert.match(TEXT.uk['v2.print.how.supports'], /не потрібні/);
  assert.match(TEXT.en['v2.print.how.supports'], /No supports/);
  assert.doesNotMatch(print, /print-estimate|v2\.print\.estimate/);
  for (const lang of ['uk', 'en']) {
    assert.equal(TEXT[lang]['v2.print.estimate'], undefined, lang);
    assert.match(TEXT[lang]['v2.print.settings.tpu'], lang === 'uk' ? /^шар 0,16 мм, заповнення 100 % \(zig-zag\), без опор$/ : /^layer 0\.16 mm, 100% infill \(zig-zag\), no supports$/);
    assert.match(TEXT[lang]['v2.print.how.tpu'], lang === 'uk' ? /0,16 мм[\s\S]*100 %[\s\S]*zig-zag[\s\S]*без опор/ : /0\.16 mm[\s\S]*100%[\s\S]*zig-zag[\s\S]*without supports/);
  }
  assert.match(readRepo('v2/case/README.md'), /^\| TPU \| layer 0\.16, infill 100 % \(zig-zag\), Arachne, no supports \|$/m);
});

test('version 2 text says nothing that belongs to version 1 only, stays calm and never calls the screw posts columns', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const stepTexts = ['uk', 'en'].flatMap((lang) => Object.entries(TEXT[lang]).filter(([key]) => key.startsWith('steps.v2_')).map(([key, value]) => [`${lang}.${key}`, value]));
  for (const [name, value] of [...v2Texts(TEXT), ...stepTexts]) {
    assert.doesNotMatch(value, V1_ONLY, name);
    assert.doesNotMatch(value.replace(/«[^»]*»|“[^”]*”/g, ''), /!/, name);
    assert.doesNotMatch(value, /колон(?![кц])|column/i, name);
    assert.doesNotMatch(value, /українською|speaks Ukrainian|Home Assistant/i, name);
  }
  for (const lang of ['uk', 'en']) {
    const { parts, tools, print } = boot(lang);
    for (const box of [parts, tools, print]) assert.doesNotMatch(plain(box.innerHTML), V1_ONLY, lang);
  }
  for (const id of SECTIONS) {
    for (const block of v2Blocks(sectionHtml(readPage(), id))) {
      assert.doesNotMatch(plain(block.html), V1_ONLY, id);
    }
  }
});

test('switching to version 1 and back leaves both tables drawn, each with its own rows', () => {
  const one = makeElement({ 'data-parts': '1' });
  one.innerHTML = '';
  const two = makeElement({ 'data-parts': '2' });
  two.innerHTML = '';
  const env = makeEnv({ children: [one, two], languages: ['uk'], href: `${BASE}?v=1` });
  const P = loadScripts(SCRIPTS, env);
  assert.ok(one.innerHTML.includes(P.TEXT.uk['parts.name.devkit']));
  assert.ok(two.innerHTML.includes(P.TEXT.uk['v2.parts.name.devkit']));
  assert.ok(!two.innerHTML.includes(P.TEXT.uk['parts.name.devkit']));
  P.setVersion('2');
  P.setLang('en');
  assert.ok(two.innerHTML.includes(P.TEXT.en['v2.parts.name.devkit']));
  assert.ok(one.innerHTML.includes(P.TEXT.en['parts.name.devkit']));
});

const LATER = ['solder', 'assembly', 'firmware', 'trouble'];
const SHARED_KEYS = new Set(['firmware.button', 'firmware.unsupported', 'firmware.not_allowed', 'firmware.load_failed']);
const SOLDER_GROUPS = ['mics', 'amp', 'ring', 'button', 'speaker', 'devkit'];
const CYR = /[Ѐ-ӿ]/;

function mdTable(md, heading) {
  const start = md.indexOf(`\n## ${heading}\n`);
  assert.ok(start >= 0, `WIRING.md has a "${heading}" section`);
  const rest = md.slice(start + heading.length + 5);
  const end = rest.search(/\n## /);
  const lines = (end < 0 ? rest : rest.slice(0, end)).split('\n').filter((l) => l.startsWith('|'));
  return lines.slice(2).map((l) => l.slice(1, -1).split('|').map((c) => c.trim()));
}

function wiringMd() {
  return readRepo('v2/case/WIRING.md');
}

function v2Block(id) {
  const blocks = v2Blocks(sectionHtml(readPage(), id));
  assert.equal(blocks.length, 1, `${id}: one version 2 block`);
  return blocks[0].html;
}

function cells(rowHtml) {
  return [...rowHtml.matchAll(/<td\b[^>]*>([\s\S]*?)<\/td>/g)].map((m) => unescapeHtml(m[1]));
}

function wiresBoot(lang) {
  const one = makeElement({ 'data-wires': '1' });
  one.innerHTML = '';
  const two = makeElement({ 'data-wires': '2' });
  two.innerHTML = '';
  const env = makeEnv({ children: [one, two], languages: [lang], href: `${BASE}?v=2&lang=${lang}` });
  const P = loadScripts(SCRIPTS, env);
  const rows = (el) => [...el.innerHTML.matchAll(/<tr\b[^>]*>([\s\S]*?)<\/tr>/g)].map((m) => cells(m[1]));
  return { P, one: rows(one), two: rows(two) };
}

function pageLabel(key) {
  const src = readRepo('v2/firmware/web/app.js');
  const uk = src.indexOf('\n    uk: {');
  const en = src.indexOf('\n    en: {');
  assert.ok(uk > 0 && en > uk, 'the settings page has uk and en dictionaries');
  const bare = /^\w+$/.test(key) ? `(?:'${key}'|${key})` : `'${key.replace(/\./g, '\\.')}'`;
  const re = new RegExp(`\\n\\s+${bare}: (?:'([^']*)'|"([^"]*)")`);
  const pick = (part) => {
    const m = re.exec(part);
    assert.ok(m, `the settings page has ${key}`);
    return m[1] ?? m[2];
  };
  return { uk: pick(src.slice(uk, en)), en: pick(src.slice(en)) };
}

function quoted(lang, label) {
  return lang === 'uk' ? `«${label}»` : `“${label}”`;
}

test('version 2 wires are the rows of the WIRING.md table: number, ends, length, colour and where they run', () => {
  const { P } = boot();
  const md = mdTable(wiringMd(), 'Wires');
  const wires = P.DATA['2'].WIRES;
  assert.equal(md.length, 23);
  assert.deepEqual(Array.from(wires, (w) => w.id), md.map((r) => r[0]));
  wires.forEach((w, i) => {
    const [id, from, to, len, colour, joins] = md[i];
    assert.equal(w.cm, Number(/^([\d.]+) cm$/.exec(len)[1]), id);
    assert.equal(w.colour, colour, id);
    assert.equal(w.joins, joins.replace(' ↔ ', '_'), id);
    for (const word of from.replace(/^mic /, 'microphone ').replace(/^amp /, 'amplifier ').split(/\s+/)) {
      assert.ok(P.TEXT.en[w.from].includes(word), `${id}: “${P.TEXT.en[w.from]}” names ${word}`);
    }
    const pin = /^DevKitC (\S+) \(row (\d+), pin (\d+) from the antenna\)(?: \((.+)\))?$/.exec(to);
    if (pin) {
      assert.deepEqual({ ...w.to }, { label: pin[1], row: Number(pin[2]), pin: Number(pin[3]) }, id);
      assert.equal(Boolean(w.via), Boolean(pin[4]), `${id}: a note on the far end`);
      if (pin[4]) assert.equal(P.TEXT.en[w.via], pin[4], id);
    } else {
      assert.equal(typeof w.to, 'string', id);
      assert.ok(P.TEXT.en[w.to].startsWith(to.replace(/^amp /, 'amplifier ')), `${id}: ${P.TEXT.en[w.to]}`);
      assert.equal(w.via, undefined, id);
    }
    for (const lang of ['uk', 'en']) {
      for (const key of [w.from, typeof w.to === 'string' ? w.to : null, w.via].filter(Boolean)) assert.ok(P.TEXT[lang][key], `${lang} ${key}`);
      assert.ok(P.TEXT[lang][`v2.wires.colour.${w.colour}`], `${lang} colour ${w.colour}`);
    }
  });
});

test('the version 2 wire table is gone from the page: no container, no column texts, no card styles, and renderWires still draws the version 1 table', () => {
  const uk = wiresBoot('uk');
  assert.equal(uk.one.length, 16);
  for (const row of uk.one) assert.equal(row.length, 3);
  assert.deepEqual(uk.one[0], [uk.P.TEXT.uk['wires.from.mic_vdd'], '3V3', '11 см']);
  assert.doesNotMatch(readPage(), /data-wires="2"|wire-cards/);
  assert.doesNotMatch(readSite('assets/style.css'), /wire-cards/);
  for (const lang of ['uk', 'en']) {
    const gone = Object.keys(uk.P.TEXT[lang]).filter((k) => /^v2\.wires\.(?:caption|pin|col\.|joins\.)|^v2\.solder\.wires\.|^v2\.solder\.pinmap\.check$/.test(k));
    assert.deepEqual(gone, [], lang);
  }
});

test('the version 2 pin map table is the DevKitC pins table of WIRING.md, and every wire to a pin lands where the pin map says', () => {
  const { P } = boot();
  const md = mdTable(wiringMd(), 'DevKitC pins');
  const block = v2Block('solder');
  const table = /<table class="pin-table">([\s\S]*?)<\/table>/.exec(block);
  assert.ok(table, 'the pin map is a table');
  const rows = [...table[1].matchAll(/<tr>\s*(<td[\s\S]*?)<\/tr>/g)].map((m) => [...m[1].matchAll(/<td\b([^>]*)>([^<]*)<\/td>/g)].map((c) => [c[1], unescapeHtml(c[2])]));
  assert.equal(rows.length, md.length);
  const ids = (text) => text.match(/\b[A-Z]\d+\b/g) || [];
  rows.forEach((row, i) => {
    const [r, n, label, what, wires] = md[i];
    assert.deepEqual(row.slice(0, 3).map((c) => c[1]), [r, n, label], `row ${i + 1}`);
    const fnKey = /data-i18n="([^"]+)"/.exec(row[3][0])[1];
    assert.ok(fnKey.startsWith('v2.solder.pinmap.fn.'), fnKey);
    assert.equal(row[3][1], P.TEXT.uk[fnKey]);
    for (const signal of what.match(/\(([A-Z]+)\)/g) || []) assert.ok(P.TEXT.en[fnKey].includes(signal), `${fnKey} names ${signal}`);
    const wKey = /data-i18n="([^"]+)"/.exec(row[4][0]);
    assert.ok(wKey, `row ${i + 1}: the wires cell is translated`);
    for (const lang of ['uk', 'en']) {
      const words = ids(wires).map((id) => P.TEXT[lang][`steps.v2_from.${id}`].replace(/ · [^·]+$/, ''));
      assert.equal(P.TEXT[lang][wKey[1]], words.join('; '), `${lang} row ${i + 1} wires`);
      assert.deepEqual(ids(P.TEXT[lang][wKey[1]]), [], `${lang} row ${i + 1}: no wire ids`);
    }
  });
  for (const w of P.DATA['2'].WIRES) {
    if (typeof w.to === 'string') continue;
    const at = md.find((r) => Number(r[0]) === w.to.row && Number(r[1]) === w.to.pin);
    assert.ok(at, `${w.id}: pin ${w.to.row}/${w.to.pin} is on the map`);
    assert.equal(at[2], w.to.label, w.id);
    assert.ok(ids(at[4]).includes(w.id), `${w.id} is listed at its pin`);
  }
});

test('version 2 soldering: the intro, one stepper per job right after its heading, then the pin map table', () => {
  const block = v2Block('solder');
  const keys = keysOf(block);
  assert.match(block, /^<div class="section-body" data-for-v="2">\s*<p class="solder-intro" data-i18n="v2\.solder\.intro">/);
  const figures = [...block.matchAll(/<figure class="stepper" data-stepper="(\w+)"/g)].map((m) => m[1]);
  assert.deepEqual(figures, SOLDER_GROUPS.map((g) => `v2_${g}`));
  for (const g of SOLDER_GROUPS) {
    assert.match(block, new RegExp(`<h3 id="h-v2-${g}" data-i18n="v2\\.solder\\.${g}\\.title">[^<]*</h3>\\s*<figure class="stepper" data-stepper="v2_${g}"[^>]*aria-labelledby="h-v2-${g}"`), g);
  }
  assert.ok(block.indexOf('data-stepper="v2_devkit"') < block.indexOf('v2.solder.pinmap.title'), 'the pin map comes after the steppers');
  assert.match(block, /<table class="pin-table">/);
  assert.doesNotMatch(block, /<ol class="steps">|data-wires/);
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const lang of ['uk', 'en']) {
    for (const g of SOLDER_GROUPS) assert.equal(Object.keys(TEXT[lang]).filter((k) => k.startsWith(`v2.solder.${g}.`)).join(), `v2.solder.${g}.title`, `${lang} ${g}: only the heading is left of the old list`);
  }
  for (const [name, value] of v2Texts(TEXT)) {
    if (name.includes('.v2.solder.')) assert.doesNotMatch(value, /фото|photo|згодом|later/i, name);
  }
  assert.ok(keys.every((k) => k.startsWith('v2.') || k.startsWith('steps.v2_') || k === 'steps.scroll_hint'), keys.find((k) => !k.startsWith('v2.')));
});

test('version 2 soldering steps follow WIRING.md: the L/R jumper per mic, the capacitor on Vin and GND, the ring circuit, the order at the DevKitC and the checks', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const all = (lang, g) => Object.entries(TEXT[lang]).filter(([k]) => k.startsWith(`steps.v2_${g}.`) && /\.\d+$/.test(k)).map(([, v]) => v).join(' ');
  assert.match(all('uk', 'mics'), /мікрофон[іа]? L[\s\S]*L\/R[\s\S]*GND[\s\S]*мікрофон[іа]? R[\s\S]*VDD/);
  assert.match(all('en', 'mics'), /microphone L[\s\S]*L\/R[\s\S]*GND[\s\S]*microphone R[\s\S]*VDD/);
  assert.match(all('uk', 'mics'), /не тицяйте/);
  assert.match(all('uk', 'mics'), /«L»[\s\S]*«R»/);
  assert.match(all('uk', 'amp'), /LRC, BCLK, DIN, GAIN, SD, GND, Vin/);
  assert.match(all('uk', 'amp'), /«\+»[\s\S]*Vin[\s\S]*«−»[\s\S]*GND/);
  assert.match(all('en', 'amp'), /GAIN and SD stay free/);
  assert.match(all('uk', 'ring'), /CR2032/);
  assert.match(all('uk', 'ring'), /«−» припаяйте дріт, а до контакту «\+» — резистор 47 Ом/);
  assert.match(all('uk', 'ring'), /припаяйте дріт, він піде на 5Vin DevKitC[\s\S]*термоусадку або обмотайте їх скотчем/);
  assert.match(all('en', 'ring'), /heat-shrink over the resistor and the joint, or wrap them in tape/);
  assert.match(all('uk', 'ring'), /6,2 кОм[\s\S]*G і S/);
  assert.match(all('en', 'ring'), /6\.2 kΩ[\s\S]*G and S/);
  assert.match(all('uk', 'ring'), /дріт, він піде на GPIO18 DevKitC[\s\S]*дріт, що піде на GND/);
  assert.match(TEXT.uk['steps.v2_ring.9'], /5Vin DevKitC → дріт → 47 Ом → нитка → дріт → транзистор/);
  assert.match(all('uk', 'button'), /дріт, що піде на GPIO8 DevKitC[\s\S]*дріт, що піде на GND/);
  assert.match(all('uk', 'speaker'), /надруковано «\+» і «−»[\s\S]*клему «\+» підсилювача[\s\S]*позначкою «\+»[\s\S]*позначкою «−»/);
  assert.match(all('en', 'speaker'), /“\+” and “−” printed[\s\S]*lug marked “\+”[\s\S]*lug marked “−”/);
  assert.match(all('en', 'speaker'), /speaker driver/);
  assert.match(all('uk', 'devkit'), /Порядок такий: дроти підсилювача, два дроти на 5Vin, дроти транзистора, мікрофонів і кнопки, а наостанок динамік у клемник/);
  assert.match(all('en', 'devkit'), /The order is: the amplifier wires, the two wires on 5Vin, the transistor, microphone and button wires, and last the speaker driver into the terminal block/);
  assert.match(TEXT.uk['steps.v2_devkit.6'], /від Vin підсилювача й від «\+» нитки через її резистор 47 Ом. Скрутіть/);
  assert.match(all('uk', 'devkit'), /7 мм/);
  assert.match(all('en', 'devkit'), /7 mm/);
  assert.match(all('uk', 'devkit'), /5Vin і GND[\s\S]*3V3 і GND/);
  assert.match(all('uk', 'devkit'), /пласкою викруткою/);
  assert.match(all('en', 'devkit'), /twist/);
  assert.doesNotMatch(all('uk', 'devkit'), /плазом|над платою/);
  const md = wiringMd();
  assert.match(md, /in this order: A1–A4, A5 and R4 twisted together on 5Vin, R1, R2, then M1–M10, B1 and B2/);
  assert.match(md, /a 47 Ω resistor to the "\+" contact in line with it[\s\S]*heat-shrink or tape/);
  assert.match(md, /\| 2 \| 21 \| 5Vin \| 5 V for the amp and the filament \| A5, R4 \|/);
  assert.match(md, /\| R4 \| filament \+ \(left end, 47 Ω soldered on it\) \| DevKitC 5Vin \(row 2, pin 21 from the antenna\) \| 6\.0 cm \|/);
});

test('every wire is drawn on the slides by what it is, never by its id or colour name: where it goes and its length on the parts, where it comes from and its length on the DevKitC, in its own colour', () => {
  const { P } = boot();
  const block = v2Block('solder');
  const used = [...block.matchAll(/data-i18n="steps\.v2_wire\.(\w+)"/g)].map((m) => m[1]);
  assert.deepEqual([...used].sort(), Array.from(P.DATA['2'].WIRES, (w) => w.id).sort());
  const special = {
    S1: ['у клему «+» підсилювача', 'into amplifier terminal “+”'],
    S2: ['у клему «−» підсилювача', 'into amplifier terminal “−”'],
    R3: ['на ніжку D транзистора', 'to transistor leg D'],
  };
  const devkit = /<figure class="stepper" data-stepper="v2_devkit"[\s\S]*?<\/figure>/.exec(block)[0];
  for (const w of P.DATA['2'].WIRES) {
    ['uk', 'en'].forEach((lang, i) => {
      const len = lang === 'uk' ? `${String(w.cm).replace('.', ',')} см` : `${w.cm} cm`;
      const to = typeof w.to === 'string' ? special[w.id][i] : lang === 'uk' ? `на ${w.to.label} DevKitC` : `to DevKitC ${w.to.label}`;
      assert.equal(P.TEXT[lang][`steps.v2_wire.${w.id}`], `${to} · ${len}`, `${lang} ${w.id}`);
      if (typeof w.to !== 'string' || w.id[0] === 'S') {
        const from = P.TEXT[lang][`steps.v2_from.${w.id}`];
        assert.ok(from && from.endsWith(` · ${len}`), `${lang} ${w.id}: ${from}`);
        assert.match(devkit, new RegExp(`data-i18n="steps\\.v2_from\\.${w.id}"`), `${w.id} is in the DevKitC slides`);
      }
    });
  }
  assert.match(P.TEXT.uk['steps.v2_from.M1'], /^мікрофон L · VDD · 4,5 см$/);
  assert.match(P.TEXT.uk['steps.v2_from.A1'], /^підсилювач · LRC · 5 см$/);
  assert.match(P.TEXT.uk['steps.v2_from.R1'], /^транзистор · G \(через 47 Ом\) · 4,5 см$/);
  assert.match(P.TEXT.uk['steps.v2_from.R4'], /^нитка «\+» \(через 47 Ом\) · 6 см$/);
  assert.match(P.TEXT.uk['steps.v2_from.B1'], /^кнопка · 14 см$/);
  assert.deepEqual(Array.from(P.DATA['2'].WIRES, (w) => w.colour).filter((c) => c === 'orange'), [], 'orange is no longer used');
  for (const w of P.DATA['2'].WIRES) {
    if (typeof w.to === 'string') continue;
    if (/^(?:3V3|5Vin)$/.test(w.to.label)) assert.equal(w.colour, 'red', `${w.id} is a power wire`);
    if (w.to.label === 'GND') assert.equal(w.colour, 'brown', `${w.id} is a ground wire`);
  }
  for (const lang of ['uk', 'en']) assert.equal(P.TEXT[lang]['v2.wires.colour.brown'], lang === 'uk' ? 'коричневий' : 'brown');
  const owner = { M: 'v2_mics', A: 'v2_amp', R: 'v2_ring', B: 'v2_button', S: 'v2_speaker' };
  const stroke = { brown: '#9C6B3C', black: '#6E6E6A', yellow: '#E3B505', green: '#1D9E75', blue: '#378ADD', red: '#E24B4A', white: '#F4F4F2', grey: '#9A9EA6', purple: '#9B6BD6' };
  const strokeBefore = (html, key) => {
    const at = html.indexOf(`data-i18n="${key}"`);
    const strokes = [...html.slice(0, at).matchAll(/<path\b[^>]*\bstroke="(#[0-9A-F]{6})"/g)];
    return strokes.length ? strokes[strokes.length - 1][1] : null;
  };
  for (const w of P.DATA['2'].WIRES) {
    const fig = new RegExp(`<figure class="stepper" data-stepper="${owner[w.id[0]]}"[\\s\\S]*?</figure>`).exec(block)[0];
    assert.match(fig, new RegExp(`data-i18n="steps\\.v2_wire\\.${w.id}"`), `${w.id} is labelled where its first end is soldered`);
    assert.equal(strokeBefore(fig, `steps.v2_wire.${w.id}`), stroke[w.colour], `${w.id} is drawn ${w.colour} on its part`);
    if (devkit.includes(`steps.v2_from.${w.id}"`)) assert.equal(strokeBefore(devkit, `steps.v2_from.${w.id}`), stroke[w.colour], `${w.id} is drawn ${w.colour} on the DevKitC`);
  }
  const ID = /\b(?:M(?:10|[1-9])|A[1-5]|R[1-4]|B[12]|S[12])\b(?!\.\d)/;
  const COLOUR = {
    uk: /(?<![а-яіїєґ])(?:червон|чорн|коричнев|жовт|зелен|помаранчев|фіолетов|сір)[а-яіїєґ]*|(?<![а-яіїєґ])(?:біл(?:ий|ого|ому|им|а|і|ої|у|ими|их)|син(?:ій|ього|ьому|ім|я|і|ю|іх|іми))(?![а-яіїєґ])/i,
    en: /\b(?:red|black|brown|white|grey|gray|yellow|green|blue|orange|purple)\b/i,
  };
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(P.TEXT[lang])) {
      if (!/^(?:steps\.v2_(?:mics|amp|ring|button|speaker|devkit|wire|from)\.|v2\.solder\.|steps\.v2_asm_\w+\.\d+$)/.test(key)) continue;
      assert.doesNotMatch(value, COLOUR[lang], `${lang} ${key} names a wire colour`);
    }
  }
  assert.doesNotMatch(plain(block), COLOUR.uk, 'the uk soldering slides name no colour');
  assert.doesNotMatch(plain(block), ID, 'the uk page shows no wire ids');
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(P.TEXT[lang])) {
      if (/^(?:steps\.v2_(?:mics|amp|ring|button|speaker|devkit|wire|from|asm_\w+)\.|v2\.solder\.|v2\.assembly\.)/.test(key)) assert.doesNotMatch(value, ID, `${lang} ${key}`);
    }
  }
});

test('the DevKitC stepper checks pin 21 of row 1 first, in plain words, with a fallback for the amplifier ground wire', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const uk = TEXT.uk['steps.v2_devkit.3'];
  const en = TEXT.en['steps.v2_devkit.3'];
  assert.match(uk, /режим прозвонки, той, що пищить/);
  assert.match(uk, /21-го штиря правого ряду, рахуючи від антени[\s\S]*останнього штиря лівого ряду з написом GND/);
  assert.match(uk, /Має пищати[\s\S]*Якщо не пищить, цей штир не використовуйте: дріт землі підсилювача відріжте довжиною 8 см/);
  assert.match(en, /continuity mode, the one that beeps/);
  assert.match(en, /pin 21 of the right-hand row, counting from the antenna[\s\S]*last pin of the left-hand row, labelled GND/);
  assert.match(en, /If it does not beep, do not use that pin: cut the amplifier ground wire to 8 cm/);
  assert.match(TEXT.uk['steps.v2_devkit.1'], /Праворуч ряд 1[\s\S]*ліворуч ряд 2/);
  const fig = /<figure class="stepper" data-stepper="v2_devkit"[\s\S]*?<\/figure>/.exec(readPage())[0];
  const two = /<g data-only="3">([\s\S]*?)<\/g>/.exec(fig)[1];
  assert.match(two, /steps\.v2_devkit\.pin21/);
  assert.match(two, /steps\.v2_devkit\.pin22/);
  const md = mdTable(wiringMd(), 'DevKitC pins');
  assert.deepEqual(md.find((r) => r[0] === '1' && r[1] === '21').slice(2, 3), ['GND']);
  assert.deepEqual(md.find((r) => r[0] === '2' && r[1] === '22').slice(2, 3), ['GND']);
});

test('the DevKitC stepper ends with every wire at once, each in a box that says where it comes from', () => {
  const { P } = boot();
  const fig = /<figure class="stepper" data-stepper="v2_devkit"[\s\S]*?<\/figure>/.exec(readPage())[0];
  const n = Number(/data-steps="(\d+)"/.exec(fig)[1]);
  const last = new RegExp(`<g data-only="${n}">([\\s\\S]*?)</g>`).exec(fig);
  assert.ok(last, 'the overview slide is its own step');
  assert.match(last[1], /steps\.v2_devkit\.all/);
  const cumulative = [...fig.matchAll(/<g data-s="(\d+)">([\s\S]*?)<\/g>/g)];
  const boxed = cumulative.flatMap((m) => [...m[2].matchAll(/data-i18n="steps\.v2_from\.(\w+)"/g)].map((b) => b[1]));
  const onPins = Array.from(P.DATA['2'].WIRES).filter((w) => typeof w.to !== 'string').map((w) => w.id);
  assert.deepEqual([...boxed].sort(), [...onPins].sort(), 'every wire on a pin has a box that stays to the last slide');
  for (const m of cumulative) {
    for (const box of m[2].matchAll(/<rect\b[^>]*width="218" height="18"[^>]*fill="(#[0-9A-F]{6})"/g)) assert.ok(['#0F6E56', '#B8431F', '#6A5FC1', '#8F6A00'].includes(box[1]), box[1]);
  }
});

test('version 2 blocks from soldering to troubleshooting: one per section, only version 2 keys apart from the shared install button texts, calm and without version 1 words', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const id of LATER) {
    const block = v2Block(id);
    for (const key of keysOf(block)) {
      const ok = key.startsWith('v2.') || key.startsWith('steps.v2_') || (id === 'firmware' && SHARED_KEYS.has(key)) || (id === 'solder' && key === 'steps.scroll_hint');
      assert.ok(ok, `${id}: ${key}`);
      for (const lang of ['uk', 'en']) assert.ok(TEXT[lang][key], `${lang} ${key}`);
    }
    const text = plain(block);
    assert.doesNotMatch(text, V1_ONLY, id);
    assert.doesNotMatch(text.replace(/«[^»]*»/g, ''), /!/, id);
  }
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (!key.startsWith('steps.v2_')) continue;
      assert.doesNotMatch(value, V1_ONLY, `${lang} ${key}`);
      assert.doesNotMatch(value, /\blid\b|шестигран|hex key|Allen/i, `${lang} ${key}`);
    }
  }
});

test('the version 2 firmware block: its own button, the steps from the UART port to the settings page, and the labels the settings page really shows', () => {
  const block = v2Block('firmware');
  assert.match(block, /<esp-web-install-button manifest="firmware\/v2\/manifest\.json">/);
  const keys = keysOf(block);
  const order = ['v2.firmware.intro', 'firmware.button', 'v2.firmware.need.title', 'v2.firmware.steps.title', 'v2.firmware.setup.title', 'v2.firmware.first.title', 'v2.firmware.ring.caption', 'v2.firmware.update.title', 'v2.firmware.trouble.title'].map((k) => keys.indexOf(k));
  assert.ok(order.every((at) => at >= 0), JSON.stringify(order));
  assert.deepEqual([...order].sort((a, b) => a - b), order);
  assert.match(block, /<pre class="cmd" tabindex="0"><code>http:\/\/pytaichenko-xxxxxx\.local<\/code><\/pre>/);
  const { TEXT } = loadScripts(['assets/text.js']);
  const steps = [...block.matchAll(/<li data-i18n="(v2\.firmware\.step\d)">/g)].map((m) => m[1]);
  assert.deepEqual(steps, ['v2.firmware.step1', 'v2.firmware.step2', 'v2.firmware.step3', 'v2.firmware.step4', 'v2.firmware.step5', 'v2.firmware.step6']);
  const installer = {
    'v2.firmware.step3': ['Install Pytaichenko.Speaker', 'Erase device', 'Next', 'Install'],
    'v2.firmware.step4': ['Installation complete!', 'Next'],
    'v2.firmware.step5': ['Configure Wi-Fi', 'Network', 'Password', 'Connect', 'Device connected to the network!', 'Continue'],
    'v2.firmware.step6': ['Continue', 'Visit Device'],
    'v2.firmware.update.usb': ['Update Pytaichenko.Speaker', 'Install', 'Install Pytaichenko.Speaker', 'Erase device'],
  };
  for (const [key, names] of Object.entries(installer)) {
    for (const lang of ['uk', 'en']) for (const name of names) assert.ok(TEXT[lang][key].includes(quoted(lang, name)), `${lang} ${key} names ${name}`);
  }
  for (const lang of ['uk', 'en']) {
    assert.match(TEXT[lang]['v2.firmware.step1'], /UART[\s\S]*COM/);
    assert.ok(TEXT[lang]['v2.firmware.step2'].includes(quoted(lang, TEXT[lang]['firmware.button'])));
    assert.match(TEXT[lang]['v2.firmware.step3'], lang === 'uk' ? /першого встановлення/ : /first install/);
    assert.match(TEXT[lang]['v2.firmware.need.browser'], /Chrome/);
    assert.match(TEXT[lang]['v2.firmware.step6'], /pytaichenko-xxxxxx\.local/);
  }
  const labels = {
    'v2.firmware.setup.password': ['setup.submit'],
    'v2.firmware.setup.key': ['nav.conversation', 'openai.key', 'save'],
    'v2.firmware.setup.check': ['openai.check'],
    'v2.firmware.setup.wake': ['nav.sound', 'wake.label', 'save'],
    'v2.firmware.setup.language': ['nav.conversation', 'assistant.language', 'save'],
    'v2.firmware.update.page': ['nav.about', 'update.title', 'update.check'],
    'v2.firmware.update.file': ['update.file', 'update.upload'],
    'v2.firmware.first.volume': ['volume.label', 'nav.sound'],
  };
  for (const [key, names] of Object.entries(labels)) {
    for (const name of names) {
      const label = pageLabel(name);
      for (const lang of ['uk', 'en']) assert.ok(TEXT[lang][key].includes(quoted(lang, label[lang])), `${lang} ${key} quotes ${label[lang]}`);
    }
  }
  const check = pageLabel('openai.check_ok');
  assert.ok(TEXT.uk['v2.firmware.setup.check'].includes(`«${check.uk.split('.')[0]}»`));
  assert.ok(TEXT.en['v2.firmware.setup.check'].includes(`“${check.en.split('.')[0]}”`));
  for (const lang of ['uk', 'en']) assert.match(TEXT[lang]['v2.firmware.setup.wake'], /Hey Jarvis, Okay Nabu (?:або|or) Hey Mycroft/);
  const wakeWords = readRepo('v2/firmware/components/pt_core/src/pt_settings.c');
  assert.match(wakeWords, /WAKE_WORDS\[\] = \{"hey_jarvis", "okay_nabu", "hey_mycroft"\}/);
});

test('the version 2 ring table names every state the firmware shows, with the timings and levels of pt_ring.c', () => {
  const ring = readRepo('v2/firmware/components/pt_core/src/pt_ring.c');
  const states = readRepo('v2/firmware/components/pt_core/include/pt_ring.h');
  const ui = readRepo('v2/firmware/components/pt_ui/pt_ui.c');
  for (const s of ['NOT_READY', 'LISTENING', 'FOLLOW_UP', 'SPEECH', 'THINKING', 'ANSWERING', 'ERROR', 'RESET']) assert.match(states, new RegExp(`PT_RING_${s}`));
  assert.match(ring, /#define BREATH_HALF_MS 2000u/);
  assert.match(ring, /#define ERROR_FLASHES 3u/);
  assert.match(ring, /#define RESET_FLASHES 5u/);
  assert.match(ring, /case PT_RING_FOLLOW_UP:\n\s+return ramp\(r->from, 0\.4f, dt\);/);
  assert.match(ring, /case PT_RING_ANSWERING:\n\s+return ramp\(r->from, 0\.7f, dt\);/);
  assert.match(ui, /#define LONG_PRESS_MS 10000/);
  const block = v2Block('firmware');
  const rows = [...block.matchAll(/<tr><td data-i18n="(v2\.firmware\.ring\.r\d\.look)">[^<]*<\/td><td data-i18n="(v2\.firmware\.ring\.r\d\.mean)">/g)];
  assert.equal(rows.length, 8);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['v2.firmware.ring.r1.look'], /раз на 4 с/);
  assert.match(TEXT.en['v2.firmware.ring.r1.look'], /once every 4 s/);
  assert.match(TEXT.uk['v2.firmware.ring.r1.mean'], /Wi-Fi[\s\S]*ключа OpenAI/);
  assert.match(TEXT.en['v2.firmware.ring.r1.mean'], /Wi-Fi[\s\S]*OpenAI key/);
  assert.match(TEXT.uk['v2.firmware.ring.r7.look'], /три/);
  assert.match(TEXT.uk['v2.firmware.ring.r8.look'], /п'ять/);
  assert.match(TEXT.en['v2.firmware.ring.r8.look'], /five/);
  assert.match(TEXT.uk['v2.firmware.ring.r8.mean'], /пароль/);
});

test('version 2 troubleshooting: no sound, the wake phrase, Wi-Fi and .local, a forgotten password, a breathing ring and a file that does not fit', () => {
  const block = v2Block('trouble');
  const items = ['sound', 'wake', 'wifi', 'password', 'ring', 'update'];
  assert.deepEqual(keysOf(block), items.flatMap((i) => [`v2.trouble.${i}.symptom`, `v2.trouble.${i}.fix`]));
  const { TEXT } = loadScripts(['assets/text.js']);
  const notOurs = pageLabel('err.update_not_ours');
  for (const lang of ['uk', 'en']) {
    const t = (k) => TEXT[lang][`v2.trouble.${k}`];
    assert.ok(t('update.symptom').includes(quoted(lang, notOurs[lang].split(':')[0])), `${lang} the page wording of the refused file`);
    assert.match(t('update.fix'), /\.ota\.bin[\s\S]*\.factory\.bin/);
    assert.ok(t('sound.fix').includes(quoted(lang, pageLabel('volume.label')[lang])));
    assert.match(t('sound.fix'), /GPIO15[\s\S]*GPIO16[\s\S]*GPIO17|GPIO16[\s\S]*GPIO15[\s\S]*GPIO17/);
    assert.match(t('wake.fix'), /L\/R[\s\S]*GND[\s\S]*VDD/);
    assert.match(t('wake.fix'), /raw microphones/);
    assert.match(t('wake.fix'), /Hey Jarvis, Okay Nabu (?:або|or) Hey Mycroft/);
    assert.match(t('wifi.fix'), /\.local/);
    assert.match(t('wifi.fix'), /2,4 ГГц|2\.4 GHz/);
    assert.ok(t('wifi.fix').includes(quoted(lang, pageLabel('host.title')[lang])));
    assert.match(t('password.fix'), /10/);
    assert.ok(t('password.fix').includes(quoted(lang, 'Erase device')));
    assert.ok(t('ring.fix').includes(quoted(lang, pageLabel('banner.nokey')[lang])));
    assert.match(t('ring.fix'), /not ready: no Wi-Fi[\s\S]*not ready: the OpenAI key is not set/);
  }
  assert.match(TEXT.uk['v2.trouble.wake.fix'], /клей/);
  assert.match(TEXT.en['v2.trouble.wake.fix'], /glue/);
  assert.match(TEXT.uk['v2.trouble.password.fix'], /п'ять/);
  const app = readRepo('v2/firmware/components/pt_app/pt_app.c');
  assert.match(app, /pt_shell_log\("not ready: no Wi-Fi"\)/);
  assert.match(app, /pt_shell_log\("not ready: the OpenAI key is not set"\)/);
  assert.match(app, /pt_shell_log\("raw microphones: left %s, right %s", left, right\)/);
});

test('version 1 keeps its firmware intro word for word', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TEXT.uk['firmware.intro'], 'Кнопка нижче записує прошивку в колонку прямо з браузера через кабель USB, а в тому ж вікні ви передаєте колонці назву й пароль своєї мережі Wi-Fi.');
  assert.match(readPage(), /<p data-i18n="firmware\.intro">Кнопка нижче записує прошивку в колонку прямо з браузера через кабель USB, а в тому ж вікні ви передаєте колонці назву й пароль своєї мережі Wi-Fi\.<\/p>/);
});

test('version 2 assembly says where left is, puts the parts in with their wires on and closes the case the way the wire lengths assume: tray first, the body tipped over onto it along the front-left edge, the top last', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['v2.assembly.intro'], /Ліве й праве тут — якщо дивитися ззаду, з боку USB-C/);
  assert.match(TEXT.en['v2.assembly.intro'], /Left and right here are as seen from behind, from the USB-C side/);
  assert.match(TEXT.uk['v2.assembly.intro'], /разом із дротами на DevKitC/);
  assert.match(TEXT.uk['v2.assembly.intro'], /трьома етапами: спершу лоток, потім тіло на лоток, а верх закриває все наостанок\.$/);
  assert.match(TEXT.en['v2.assembly.intro'], /three stages: the tray first, then the body onto the tray, and the top closes everything last\.$/);
  assert.match(TEXT.uk['steps.v2_asm_body.8'], /Динамік і кнопку[\s\S]*крізь тіло[\s\S]*Покладіть тіло на бік біля переднього лівого краю лотка[\s\S]*перекиньте його на лоток уздовж цього краю[\s\S]*вздовж стінки, подалі від прокладки/);
  assert.match(TEXT.en['steps.v2_asm_body.8'], /speaker driver and the button[\s\S]*through the body[\s\S]*Lay the body on its side by the front-left edge of the tray[\s\S]*tip it over onto the tray along that edge[\s\S]*along the wall and away from the gasket/);
  assert.match(TEXT.uk['steps.v2_asm_body.9'], /гвинти лотка/);
  assert.match(TEXT.uk['steps.v2_asm_tray.4'], /прокладку лотка[\s\S]*мультиметром/);
  assert.doesNotMatch(TEXT.uk['steps.v2_asm_tray.4'], /динамік|прошит/);
  assert.match(TEXT.uk['steps.v2_asm_top.9'], /клемнику підсилювача[\s\S]*перевірити кільце та звук[\s\S]*покладіть верх на тіло/);
  assert.match(TEXT.en['steps.v2_asm_top.9'], /amplifier terminal block[\s\S]*check the ring and the sound[\s\S]*set the top on the body/);
  const readme = readRepo('v2/case/README.md');
  assert.match(readme, /^26 steps in three stages: Tray 1–4, Body onto the tray 5–15, Lid 16–26\./m);
  assert.match(readme, /^\| 12 \| Body onto the tray \| Pass the speaker and the button up through the body, lay the body on its side by the front-left edge of the tray and tip it over onto the tray along that edge, keeping the extra wire along the wall and off the gasket\. \|$/m);
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (!/^steps\.v2_asm_|^v2\.assembly\./.test(key)) continue;
      assert.doesNotMatch(value, /припаяйте|паяйте|\bsolder\b|етапі 4|in (?:assembly )?stage 4|червон|red mark/i, `${lang} ${key}`);
    }
    for (const k of [1, 2, 3]) assert.match(TEXT[lang][`steps.v2_asm_tray.${k}`], lang === 'uk' ? /^(?:Якщо|Так само)/ : /^(?:If|The same)/, `${lang} tray ${k}`);
    assert.equal(TEXT[lang]['v2.assembly.join.title'], undefined, `${lang}: no separate stage for the top onto the body`);
    assert.equal(TEXT[lang]['v2.assembly.stage_4'], undefined, lang);
  }
  assert.match(TEXT.en['v2.solder.intro'], /Everything is soldered on the table before assembly[\s\S]*Tip: if you first snap the DevKitC and the amplifier into the tray/);
  assert.match(TEXT.uk['v2.solder.intro'], /Усе паяють на столі, ще до збирання[\s\S]*Порада[\s\S]*етап «Лоток» розділу «Збирання», кроки 1–3/);
  assert.match(TEXT.uk['steps.v2_devkit.10'], /на етапі «Верх» збирання/);
  assert.equal(TEXT.en['v2.solder.speaker.title'], 'Speaker driver');
  const order = wiringMd().slice(wiringMd().indexOf('## Soldering order'));
  assert.match(order, /Everything is soldered on the bench first, the DevKitC wires included/);
  assert.match(order, /steps 1–3 of the assembly/);
  const at = (label) => order.indexOf(label);
  assert.ok(at('At the DevKitC') < at('4. Tray:') && at('4. Tray:') < at('5. Body:') && at('5. Body:') < at('6. Body onto the tray:') && at('6. Body onto the tray:') < at('7. Lid last:'), 'tray, body, body onto the tray, lid last');
});

test('a speaker that started only in part is explained with the exact log line of the firmware', () => {
  const app = readRepo('v2/firmware/components/pt_app/pt_app.c');
  const m = /pt_shell_log\("(the speaker could not start: )%s(; the ring shows the error and the button can still reset the password)",/.exec(app);
  assert.ok(m, 'the firmware logs the failed start');
  const line = `${m[1]}…${m[2]}`;
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const lang of ['uk', 'en']) assert.ok(TEXT[lang]['v2.trouble.ring.fix'].includes(line), lang);
});

test('the version 2 button note says only that two wires are enough, without the board BOOT button', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TEXT.uk['v2.solder.pinmap.button'], 'Кнопка працює через внутрішню підтяжку чипа, тож їй потрібні лише два дроти: на GPIO8 і на GND.');
  assert.equal(TEXT.en['v2.solder.pinmap.button'], 'The button uses the chip\'s internal pull-up, so it needs only two wires: to GPIO8 and to GND.');
  assert.doesNotMatch(v2Block('solder'), /BOOT/);
});

test('version 2 is its own product: no home server, voice-hub or version 1 in any version 2 text or block', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const OTHER = /сервер|server|voice-hub|верс[іi][яїюєі] 1(?!\d)|version 1(?!\d)/i;
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (/^(?:v2\.|steps\.v2_)/.test(key)) assert.doesNotMatch(value, OTHER, `${lang} ${key}`);
    }
  }
  const page = readPage();
  const ids = [...page.matchAll(/<section\b[^>]*\bid="(\w+)"/g)].map((m) => m[1]);
  for (const id of ids) {
    for (const block of v2Blocks(sectionHtml(page, id))) assert.doesNotMatch(plain(block.html), OTHER, id);
  }
  const hero = /<div class="hero-body" data-for-v="2">([\s\S]*?)<\/div>/.exec(page)[1];
  assert.doesNotMatch(plain(hero), OTHER);
  assert.doesNotMatch(readRepo('README.md'), /without a home server/);
  assert.doesNotMatch(readRepo('README.uk.md'), /без домашнього сервера/);
  assert.match(readRepo('README.md'), /^\*\*Version 2\*\* talks with OpenAI by itself\./m);
  assert.match(readRepo('README.uk.md'), /^\*\*Версія 2\*\* сама розмовляє з OpenAI\./m);
});

test('version 2 part notes are short: the size of the mics, the filament variant in any colour, how many inserts and screws, the wire gauge, and no notes for the resistors, the capacitor and the washers', () => {
  const { P } = boot();
  const exact = {
    mic: ['Два круглі модулі діаметром близько 14 мм, без впаяних штирів.', 'Two round modules about 14 mm across, without header pins.'],
    inserts: ['Потрібно 9 вставок M2.5 (довжина 4 мм, Ø3,5 мм).', 'You need 9 M2.5 inserts (4 mm long, Ø3.5 mm).'],
    screws: ['Потрібно 9 гвинтів під хрестову викрутку.', 'You need 9 screws for a Phillips screwdriver.'],
    wires: ["Тонкий багатожильний дріт 26–30 AWG дев'яти кольорів.", 'Thin stranded wire, 26–30 AWG, in nine colours.'],
  };
  for (const [id, [uk, en]] of Object.entries(exact)) {
    assert.equal(P.TEXT.uk[`v2.parts.note.${id}`], uk, id);
    assert.equal(P.TEXT.en[`v2.parts.note.${id}`], en, id);
  }
  assert.match(P.TEXT.uk['v2.parts.note.filament'], /^Потрібна одна нитка Ø1,5 мм у такому варіанті: 3 В, близько 185 мм, 100 мА\. Колір світіння — на ваш смак\. Резистор 47 Ом/);
  assert.match(P.TEXT.en['v2.parts.note.filament'], /^You need one Ø1\.5 mm filament in this variant: 3 V, about 185 mm, 100 mA\. The colour of the light is up to you\. The 47 Ω resistor/);
  for (const lang of ['uk', 'en']) {
    assert.doesNotMatch(P.TEXT[lang]['v2.parts.note.filament'], /теплий|warm white/);
    for (const id of ['res47', 'res62k', 'cap', 'washers']) assert.equal(P.TEXT[lang][`v2.parts.note.${id}`], undefined, `${lang} ${id}`);
    const { P: Q, parts } = boot(lang);
    const rows = [...parts.innerHTML.matchAll(/<tr>([\s\S]*?)<\/tr>/g)].map((m) => m[1]);
    for (const id of ['res47', 'res62k', 'cap', 'washers']) {
      const row = rows[Q.DATA['2'].PARTS.findIndex((p) => p.id === id)];
      assert.ok(row.includes(escapeText(Q.TEXT[lang][`v2.parts.name.${id}`])), `${lang} ${id} row`);
      assert.doesNotMatch(row, /part-note|undefined/, `${lang} ${id} has no note`);
    }
  }
});

function escapeText(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

test('both versions name the printer the parts were printed on and say that any printer for PETG and TPU will do', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const key of ['print.common', 'v2.print.common']) {
    assert.match(TEXT.uk[key], /^Деталі надруковано на Bambu Lab X1 Carbon[^:]*: сопло 0,4 мм, перший шар 0,2 мм, стінки Arachne\. Підійде будь-який принтер, що друкує PETG і TPU\.$/, key);
    assert.match(TEXT.en[key], /^The parts were printed on a Bambu Lab X1 Carbon[^:]*: 0\.4 mm nozzle, 0\.2 mm first layer, Arachne walls\. Any printer that prints PETG and TPU will do\.$/, key);
  }
  assert.match(TEXT.uk['print.common'], /стіл 256 × 256 мм/);
});

test('the speaker driver is soldered by its printed “+” and “−” marks: the drawing shows the marks by the lugs and no red mark anywhere', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const fig = /<figure class="stepper" data-stepper="v2_speaker"[\s\S]*?<\/figure>/.exec(v2Block('solder'))[0];
  const one = /<g data-s="1">([\s\S]*?)<\/g>/.exec(fig)[1];
  assert.match(one, /data-i18n="steps\.v2_speaker\.marks">позначки «\+» і «−»<\/text>/);
  assert.match(one, /<text x="393"[^>]*data-i18n="steps\.v2_speaker\.plus">\+<\/text>/);
  assert.match(one, /<text x="447"[^>]*data-i18n="steps\.v2_speaker\.minus">−<\/text>/);
  assert.doesNotMatch(one, /<circle\b[^>]*fill="#E24B4A"/);
  assert.doesNotMatch(fig, /red_dot/);
  for (const lang of ['uk', 'en']) {
    assert.equal(TEXT[lang]['steps.v2_speaker.red_dot'], undefined);
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (/^(?:v2\.|steps\.v2_)/.test(key)) assert.doesNotMatch(value, /червон[а-яіїєґ]* позначк|red mark|red dot/i, `${lang} ${key}`);
    }
  }
  assert.equal(TEXT.en['steps.v2_speaker.marks'], '“+” and “−” marks');
  assert.doesNotMatch(wiringMd(), /red dot|red mark/i);
  assert.match(wiringMd(), /S1 \(red\) to the lug marked "\+", S2 \(black\) to the lug marked "−"/);
});

test('the photo of the finished wiring stands right before the top stage, lazy, sized, with its caption and alt in both languages, and is published with the site', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const block = v2Block('assembly');
  const fig = /<figure class="photo">\s*(<img\b[^>]*>)\s*<figcaption data-i18n="v2\.assembly\.photo\.caption">([^<]*)<\/figcaption>\s*<\/figure>\s*<h3 id="h-v2-asm-top"/.exec(block);
  assert.ok(fig, 'the photo figure comes right before the top stage heading');
  const img = fig[1];
  assert.match(img, /src="assets\/photos-v2\/inside\.jpg"/);
  assert.match(img, /width="900" height="1200"/);
  assert.match(img, /loading="lazy"/);
  assert.match(img, /data-i18n-attr="alt:v2\.assembly\.photo\.alt"/);
  assert.equal(unescapeHtml(/alt="([^"]*)"/.exec(img)[1]), TEXT.uk['v2.assembly.photo.alt']);
  assert.equal(unescapeHtml(fig[2]), TEXT.uk['v2.assembly.photo.caption']);
  assert.ok(block.indexOf('h-v2-asm-body') < block.indexOf('class="photo"'), 'after the body stage');
  assert.match(TEXT.uk['v2.assembly.photo.caption'], /^Так виглядає колонка перед тим, як закрити верх: усе спаяно, плати в лотку, мікрофони приклеєні\.$/);
  for (const lang of ['uk', 'en']) {
    assert.ok(TEXT[lang]['v2.assembly.photo.alt'].length > 40, lang);
    assert.ok(TEXT[lang]['v2.assembly.photo.caption'].trim(), lang);
  }
  assert.notEqual(TEXT.en['v2.assembly.photo.caption'], TEXT.uk['v2.assembly.photo.caption']);
  const bytes = readFileSync(path.join(siteDir, 'assets', 'photos-v2', 'inside.jpg'));
  assert.deepEqual([...bytes.subarray(0, 3)], [0xff, 0xd8, 0xff], 'a JPEG');
  assert.ok(bytes.length < 400 * 1024, 'small enough for a phone');
  let at = 2;
  let size = null;
  while (at < bytes.length) {
    const marker = bytes[at + 1];
    const len = bytes.readUInt16BE(at + 2);
    if (marker >= 0xc0 && marker <= 0xc3) size = [bytes.readUInt16BE(at + 7), bytes.readUInt16BE(at + 5)];
    if (marker === 0xe1) {
      const exif = bytes.subarray(at + 4, at + 2 + len);
      assert.ok(len < 200, 'no camera metadata left in EXIF');
      assert.ok(!exif.includes(Buffer.from([0x88, 0x25])) && !exif.includes(Buffer.from([0x25, 0x88])), 'no GPS data');
    }
    if (size) break;
    at += 2 + len;
  }
  assert.deepEqual(size, [900, 1200]);
  const css = readSite('assets/style.css');
  assert.match(css, /\.photo img \{[^}]*width: 100%;[^}]*height: auto;/);
  const release = readRepo('.github/workflows/release.yml');
  assert.match(release, /upload-pages-artifact@v3\s*\n\s*with:\s*\n\s*path: site\b/);
  assert.deepEqual([...release.matchAll(/rm -rf? (\S+)/g)].map((m) => m[1]).filter((p) => p.startsWith('site')), ['site/tests']);
});
