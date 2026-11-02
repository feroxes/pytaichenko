import test from 'node:test';
import assert from 'node:assert/strict';
import { loadScripts, makeElement, makeEnv, readPage, readSite } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js', 'assets/data.js'];

const PART_ROWS = [
  ['devkit', 1],
  ['mic', 1],
  ['amp', 1],
  ['speaker', 1],
  ['filament', 2],
  ['mosfet', 1],
  ['res47', 3],
  ['res62k', 1],
  ['cap', 1],
  ['button', 1],
  ['inserts', 1],
  ['screws', 1],
  ['washers', 4],
  ['wires', 'set'],
  ['charger', 'set'],
  ['petg_black', 1],
  ['petg_white', 1],
  ['tpu', 1],
];

const LINKED_IDS = ['devkit', 'mic', 'amp', 'speaker', 'filament', 'mosfet', 'res47', 'res62k', 'cap', 'button', 'inserts', 'screws'];

const TOOL_IDS = ['iron', 'solder', 'insert_tip', 'multimeter', 'cutters', 'glue_gun', 'feet_glue', 'screwdriver', 'flat_screwdriver', 'battery', 'marker', 'tape'];

const GONE_TOOLS = ['stripper', 'heat_source', 'electrical_tape', 'hex_key'];

const SPEAKER_URL = 'https://menstudio.kyiv.ua/ua/p3163263864-dinamik-shirokopolosnyj-dyujma.html';
const FILAMENT_URL = 'https://www.aliexpress.com/item/1005007884281989.html';
const DEVKIT_URL = 'https://arduino.ua/prod3990-wi-fi-modyl-devkit-v1-s-esp-32';
const MIC_URL = 'https://menstudio.kyiv.ua/ua/p2712862774-vsenapravlennyj-mems-mikrofonnyj.html';
const RES62K_URL = 'https://www.rcscomponents.kiev.ua/product/6-2-kohm-5-0-5w-vyv-cr050sjtb-6k2r-hitano_182986.html';
const INSERTS_URL = 'https://arduino.ua/prod6834-nabir-zakladnih-gaiok-m2m2-5m3m4m5-220sht-latyn';
const SCREWS_URL = 'https://arduino.ua/prod8440-nabir-preciziinih-gvintiv-m2-ta-m2-5-500-sht-';

const SHOP_HOSTS = {
  'arduino.ua': ['arduino.ua'],
  menstudio: ['menstudio.kyiv.ua'],
  aliexpress: ['www.aliexpress.com', 'aliexpress.com'],
  rcs: ['www.rcscomponents.kiev.ua'],
};

const PATH_SHAPES = {
  'arduino.ua': /^\/prod\d+-[a-z0-9-]+$/,
  menstudio: /^\/ua\/p\d+-[a-z0-9-]+\.html$/,
  aliexpress: /^\/item\/\d+\.html$/,
  rcs: /^\/product\/[a-z0-9-]+_\d+\.html$/,
};

const DASH = '\u2014';

function unescapeHtml(text) {
  return text.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
}

function boot({ languages = ['uk-UA'], stored = null, readyState = 'complete', extra = [] } = {}) {
  const tbody = makeElement({ 'data-parts': '' });
  tbody.innerHTML = '';
  const list = makeElement({ 'data-tools': '' });
  list.innerHTML = '';
  const cost = makeElement({ 'data-cost': '' }, 'Уточнюється');
  const note = makeElement({ 'data-parts-footnote': '' }, '');
  const env = makeEnv({ children: [tbody, list, cost, note, ...extra], languages, stored, readyState });
  const P = loadScripts(SCRIPTS, env);
  return { tbody, list, cost, note, env, P };
}

function rowsOf(html) {
  return [...html.matchAll(/<tr\b([^>]*)>([\s\S]*?)<\/tr>/g)].map((tr) => ({
    attrs: tr[1],
    html: tr[2],
    cells: [...tr[2].matchAll(/<td\b([^>]*)>([\s\S]*?)<\/td>/g)].map((td) => ({ attrs: td[1], html: td[2], text: unescapeHtml(td[2].replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim()) })),
  }));
}

function attrOf(tag, name) {
  const m = new RegExp(`\\b${name}="([^"]*)"`).exec(tag);
  return m ? unescapeHtml(m[1]) : null;
}

function anchorsIn(html) {
  return [...html.matchAll(/<a\b[^>]*>/g)].map((m) => m[0]);
}

function priced(uah, qty = 1, extra = {}) {
  return { id: 'devkit', qty, uah, url: 'https://arduino.ua/prod3107-x', shop: 'arduino.ua', ...extra };
}

test('data.js still loads after text.js and i18n.js and has no fetch or modules', () => {
  const code = readSite('assets/data.js');
  assert.doesNotMatch(code, /\bfetch\s*\(|XMLHttpRequest|^\s*(?:import|export)\b/m);
  assert.doesNotMatch(code, /^\s*\/\/|\/\*/m, 'data.js has code comments');
});

test('PARTS lists the 18 parts in the agreed order with their quantities, and heat-shrink is gone', () => {
  const { P } = boot();
  assert.deepEqual(Array.from(P.PARTS, (p) => [p.id, p.qty]), PART_ROWS);
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(P.TEXT[lang])) {
      assert.doesNotMatch(key, /heatshrink/, `${lang} ${key}`);
      assert.doesNotMatch(value, /термоусад|heat-shrink|heat shrink|ізолент|electrical tape/i, `${lang} ${key}`);
    }
  }
  assert.doesNotMatch(readPage(), /heatshrink|термоусад|heat-shrink|ізолент|electrical tape/i);
});

test('every part has the documented fields and every id is translated in both languages', () => {
  const { P } = boot();
  for (const p of P.PARTS) {
    assert.ok((Number.isInteger(p.qty) && p.qty > 0) || p.qty === 'set', `${p.id}: qty is a positive whole number or a set`);
    if (p.qty === 'set') assert.equal(p.uah, null, `${p.id}: a set has no price to multiply`);
    assert.ok(p.uah === null || (typeof p.uah === 'number' && p.uah > 0), `${p.id}: uah is a positive number or null`);
    if (p.usd !== undefined) assert.ok(typeof p.usd === 'number' && p.usd > 0 && p.uah === null, `${p.id}: usd is a positive number and replaces uah`);
    if (p.kit !== undefined) assert.ok(p.kit === true && Number.isInteger(p.qty), `${p.id}: a kit is counted in whole kits`);
    assert.ok(p.url === null || typeof p.url === 'string', `${p.id}: url`);
    assert.ok(p.shop === null || p.shop in SHOP_HOSTS, `${p.id}: shop ${p.shop}`);
    assert.ok(`parts.name.${p.id}` in P.TEXT.uk && `parts.name.${p.id}` in P.TEXT.en, `${p.id}: name is not translated`);
    if (p.pack !== undefined) assert.ok(Number.isInteger(p.pack) && p.pack > 1, `${p.id}: pack`);
    if (p.generic !== undefined) assert.ok(p.generic === true && p.url === null, `${p.id}: generic is true and only for rows without a link`);
    if (p.costAs !== undefined) assert.ok(['plastic', 'small'].includes(p.costAs), `${p.id}: costAs groups rows as plastic or small parts`);
    const note = `parts.note.${p.id}`;
    assert.equal(note in P.TEXT.uk, note in P.TEXT.en, `${p.id}: note exists in one language only`);
  }
});

test('every external part url is https and on arduino.ua, menstudio.kyiv.ua, aliexpress.com or rcscomponents.kiev.ua, never a search or listing page', () => {
  const { P } = boot();
  let withUrl = 0;
  for (const p of P.PARTS) {
    if (p.url === null) continue;
    withUrl += 1;
    const u = new URL(p.url);
    assert.equal(u.protocol, 'https:', `${p.id}: not https`);
    assert.ok(SHOP_HOSTS[p.shop], `${p.id}: shop`);
    assert.ok(SHOP_HOSTS[p.shop].includes(u.hostname), `${p.id}: host ${u.hostname} does not match shop ${p.shop}`);
    assert.equal(u.search, '', `${p.id}: query string in url`);
    assert.equal(u.hash, '', `${p.id}: fragment in url`);
    assert.match(u.pathname, PATH_SHAPES[p.shop], `${p.id}: ${u.pathname} is not a direct product path`);
    assert.doesNotMatch(p.url, /prom\.ua/);
  }
  assert.equal(withUrl, P.PARTS.filter((p) => p.url !== null).length);
  for (const id of LINKED_IDS) assert.notEqual(P.PARTS.find((p) => p.id === id).url, null, `${id} lost its link`);
});

test('the arduino.ua parts link to their product pages, the speaker and the filament to the fixed links', () => {
  const { P } = boot();
  const byId = Object.fromEntries(Array.from(P.PARTS, (p) => [p.id, p]));
  assert.match(byId.mosfet.url, /^https:\/\/arduino\.ua\/prod3107-/);
  assert.match(byId.res47.url, /^https:\/\/arduino\.ua\/prod1972-/);
  assert.match(byId.cap.url, /^https:\/\/arduino\.ua\/prod6492-/);
  assert.match(byId.button.url, /^https:\/\/arduino\.ua\/prod7626-/);
  assert.equal(byId.speaker.url, SPEAKER_URL);
  assert.equal(byId.speaker.shop, 'menstudio');
  assert.equal(byId.filament.url, FILAMENT_URL);
  assert.equal(byId.filament.shop, 'aliexpress');
  assert.equal(byId.devkit.url, DEVKIT_URL);
  assert.equal(byId.devkit.shop, 'arduino.ua');
  assert.equal(byId.mic.url, MIC_URL);
  assert.equal(byId.mic.shop, 'menstudio');
  assert.equal(byId.mic.uah, 140);
  assert.equal(byId.res62k.url, RES62K_URL);
  assert.equal(byId.res62k.shop, 'rcs');
  assert.equal(byId.inserts.url, INSERTS_URL);
  assert.equal(byId.screws.url, SCREWS_URL);
  for (const id of ['inserts', 'screws']) assert.equal(byId[id].shop, 'arduino.ua', id);
});

test('the 6.2 kΩ resistor comes from RCS Components in a minimum order of 4 at 5 ₴ each, shown as «20 ₴ за 4 шт.»', () => {
  const { tbody, P } = boot();
  const res = P.PARTS.find((p) => p.id === 'res62k');
  assert.equal(res.uah, 20);
  assert.equal(res.pack, 4);
  assert.equal(res.generic, undefined);
  assert.equal(P.TEXT.uk['parts.shop.rcs'], 'RCS Components');
  assert.equal(P.TEXT.en['parts.shop.rcs'], 'RCS Components');
  const row = () => rowsOf(tbody.innerHTML)[P.PARTS.indexOf(res)];
  P.renderParts(tbody, 'uk');
  assert.equal(row().cells[2].text, '20 ₴ за 4 шт.');
  assert.equal(row().cells[3].text, 'RCS Components');
  assert.doesNotMatch(P.TEXT.uk['parts.note.res62k'], /arduino/);
  assert.doesNotMatch(P.TEXT.en['parts.note.res62k'], /arduino/);
});

test('the filaments are priced in dollars, $4.89 for a pack of 5: UA shows the hryvnias at CONFIG.usdRate first, EN the dollars only', () => {
  const { tbody, P } = boot();
  const fil = P.PARTS.find((p) => p.id === 'filament');
  assert.equal(fil.usd, 4.89);
  assert.equal(fil.pack, 5);
  assert.equal(fil.qty, 2);
  assert.equal(fil.uah, null);
  const price = (lang) => {
    P.renderParts(tbody, lang);
    return rowsOf(tbody.innerHTML)[P.PARTS.indexOf(fil)].cells[2].text;
  };
  P.CONFIG.usdRate = 45.53;
  assert.equal(price('uk'), '≈ 223 ₴ ($4,89) за 5 шт.');
  assert.equal(price('en'), '$4.89 per 5 pcs');
  P.CONFIG.usdRate = 40;
  assert.equal(price('uk'), '≈ 196 ₴ ($4,89) за 5 шт.');
  assert.equal(price('en'), '$4.89 per 5 pcs');
});

test('the parts without a price show a dash, with the words kept for screen readers, and can be priced with one number', () => {
  const { tbody, P } = boot();
  P.renderParts(tbody);
  const rows = rowsOf(tbody.innerHTML);
  const dashed = [];
  P.PARTS.forEach((p, i) => {
    const cell = rows[i].cells[2];
    if (p.uah !== null || p.usd !== undefined) return assert.doesNotMatch(cell.html, /\u2014/, p.id);
    dashed.push(p.id);
    assert.match(cell.html, new RegExp(`<span aria-hidden="true">${DASH}</span><span class="visually-hidden">${P.TEXT.uk['parts.price_missing']}</span>`), p.id);
  });
  assert.deepEqual(dashed, ['washers', 'wires', 'charger', 'petg_black', 'petg_white', 'tpu']);
  const before = P.partsTotal().uah;
  for (const [id, uah] of [['charger', 300], ['washers', 150], ['wires', 120]]) P.PARTS.find((p) => p.id === id).uah = uah;
  assert.equal(P.partsTotal().uah, before + 300 + 150 * 4 + 120);
  assert.deepEqual(Array.from(P.costLeft()), ['plastic']);
  P.renderParts(tbody);
  const after = rowsOf(tbody.innerHTML);
  assert.equal(after[P.PARTS.findIndex((p) => p.id === 'charger')].cells[2].text, '300 ₴');
  assert.equal(after[P.PARTS.findIndex((p) => p.id === 'wires')].cells[2].text, '120 ₴');
});

test('inserts and screws are bought as one kit each, and their notes say how many of which size go where', () => {
  const { tbody, P } = boot();
  for (const id of ['inserts', 'screws']) {
    const p = P.PARTS.find((x) => x.id === id);
    assert.equal(p.qty, 1, id);
    assert.equal(p.kit, true, id);
  }
  P.renderParts(tbody, 'uk');
  const qty = (lang, id) => {
    P.renderParts(tbody, lang);
    return rowsOf(tbody.innerHTML)[P.PARTS.findIndex((p) => p.id === id)].cells[1].text;
  };
  assert.equal(qty('uk', 'inserts'), '1 набір');
  assert.equal(qty('en', 'screws'), '1 set');
  assert.equal(P.TEXT.uk['parts.name.inserts'], 'Латунні різьбові вставки M2.5');
  assert.equal(P.TEXT.en['parts.name.inserts'], 'Brass threaded inserts, M2.5');
  assert.equal(P.TEXT.uk['parts.name.screws'], 'Гвинти M2.5×8 мм з потайною головкою');
  assert.equal(P.TEXT.en['parts.name.screws'], 'Countersunk screws, M2.5×8 mm');
  assert.match(P.TEXT.uk['parts.note.inserts'], /звичайним жалом паяльника\. За посиланням продається набір M2–M5 на 220 шт\., з нього знадобляться лише M2\.5\.$/);
  assert.match(P.TEXT.en['parts.note.inserts'], /ordinary soldering-iron tip\. The link is a kit of 220 pcs, M2 to M5; only the M2\.5 ones are needed\.$/);
  assert.match(P.TEXT.uk['parts.note.screws'], /За посиланням продається набір M2 і M2\.5 на 500 шт\., з нього знадобляться лише M2\.5×8\.$/);
  assert.match(P.TEXT.en['parts.note.screws'], /The link is a kit of 500 pcs, M2 and M2\.5; only the M2\.5×8 ones are needed\.$/);
  for (const lang of ['uk', 'en']) assert.doesNotMatch(P.TEXT[lang]['parts.note.inserts'], /насадк|special tip|insert tip/);
  for (const lang of ['uk', 'en']) assert.doesNotMatch(P.TEXT[lang]['parts.note.res62k'], /Мінімальне|minimum/i);
  assert.match(P.TEXT.uk['parts.note.wires'], /кручена пара/);
  assert.match(P.TEXT.en['parts.note.wires'], /twisted-pair/);
  assert.match(P.TEXT.uk['parts.note.mic'], /круглий модуль INMP441/);
  assert.doesNotMatch(P.TEXT.uk['parts.note.mic'], /Штирі не впаюються/);
  assert.match(P.TEXT.uk['parts.note.inserts'], /11 вставок M2\.5[^:]*: 3 у тіло, 2 у стійки тіла, 4 у верх, 2 у лоток/);
  assert.match(P.TEXT.en['parts.note.inserts'], /11 M2\.5 inserts[^:]*: 3 in the body, 2 in the body posts, 4 in the top, 2 in the tray/);
  assert.match(P.TEXT.uk['parts.note.screws'], /11 гвинтів: 3 для лотка, 2 для верху, 4 для динаміка й 2 для DevKit/);
  assert.match(P.TEXT.en['parts.note.screws'], /11 screws: 3 for the tray, 2 for the top, 4 for the speaker and 2 for the DevKit/);
  for (const lang of ['uk', 'en']) {
    for (const key of ['parts.name.screws', 'parts.note.screws']) {
      assert.doesNotMatch(P.TEXT[lang][key], /шестигран|hex|ключ|key/i, `${lang} ${key}`);
    }
  }
});

test('the DevKit row links to arduino.ua with its price, and asks for a 30-pin board with a USB-C connector in the name and the note', () => {
  const { tbody, P } = boot();
  const devkit = P.PARTS.find((p) => p.id === 'devkit');
  assert.equal(devkit.url, DEVKIT_URL);
  assert.equal(devkit.shop, 'arduino.ua');
  assert.equal(devkit.uah, 295);
  for (const lang of ['uk', 'en']) {
    const name = P.TEXT[lang]['parts.name.devkit'];
    const note = P.TEXT[lang]['parts.note.devkit'];
    assert.match(name, /DevKit V1/);
    assert.match(name, /30/);
    assert.match(name, /USB-C/);
    assert.match(note, /USB-C/);
    assert.match(note, /Type-C/);
    assert.match(note, lang === 'uk' ? /проріз/ : /slot/);
    assert.doesNotMatch(note, lang === 'uk' ? /не вказано/ : /does not state/);
    assert.doesNotMatch(note, /наявност|out of stock|arduino/i);
  }
  P.renderParts(tbody);
  const row = rowsOf(tbody.innerHTML)[0];
  assert.equal(anchorsIn(row.html).length, 1);
  assert.equal(row.cells[3].text, 'arduino.ua');
  assert.equal(row.cells[2].text, '295 ₴');
});

test('the ring arcs are white PETG only: no "clear" or «прозорий» anywhere in the parts texts', () => {
  const { P } = boot();
  assert.equal(P.TEXT.uk['parts.name.petg_white'], 'PETG білий');
  assert.equal(P.TEXT.en['parts.name.petg_white'], 'PETG, white');
  assert.equal(P.TEXT.uk['parts.note.petg_white'], 'Дві дуги кільця.');
  assert.equal(P.TEXT.en['parts.note.petg_white'], 'The two ring arcs.');
  for (const lang of ['uk', 'en']) {
    for (const key of Object.keys(P.TEXT[lang]).filter((k) => /^(parts|tools|cost)\./.test(k))) {
      assert.doesNotMatch(P.TEXT[lang][key], /прозор|clear|transparent|translucent/i, `${lang} ${key}`);
    }
  }
});

test('a part with a price always has a link, a part without a link has no shop, and nothing links to the generic filament rows', () => {
  const { P } = boot();
  for (const p of P.PARTS) {
    if (p.uah !== null || p.usd !== undefined) assert.notEqual(p.url, null, `${p.id}: a price needs the page it was read from`);
    if (p.url === null) assert.equal(p.shop, null, `${p.id}: shop without a link`);
    else assert.notEqual(p.shop, null, `${p.id}: link without a shop`);
  }
  for (const id of ['petg_black', 'petg_white', 'tpu']) {
    const p = P.PARTS.find((x) => x.id === id);
    assert.equal(p.url, null);
    assert.equal(p.uah, null);
    assert.equal(p.generic, true);
  }
});

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function ukDate(iso) {
  const [y, m, d] = iso.split('-');
  return `${d}.${m}.${y}`;
}

function enDate(iso) {
  const [y, m, d] = iso.split('-');
  return `${Number(d)} ${MONTHS[Number(m) - 1]} ${y}`;
}

test('the date the prices were read is stated right under the table in both languages', () => {
  const { note, P } = boot();
  assert.match(P.PARTS_DATE, /^\d{4}-\d{2}-\d{2}$/);
  assert.ok(!Number.isNaN(Date.parse(P.PARTS_DATE)));
  assert.ok(note.textContent.includes(ukDate(P.PARTS_DATE)), note.textContent);
  P.setLang('en');
  assert.ok(note.textContent.includes(enDate(P.PARTS_DATE)), note.textContent);
});

test('both notes state the dollar rate and its date from CONFIG, the Ukrainian one with a decimal comma', () => {
  const { note, P } = boot();
  P.CONFIG.usdRate = 45.5293;
  P.CONFIG.ratesDate = '2026-10-31';
  P.renderFootnote(note);
  assert.ok(note.textContent.includes('1 $ = 45,53 ₴'), note.textContent);
  assert.ok(note.textContent.includes(ukDate('2026-10-31')), note.textContent);
  assert.match(note.textContent, /НБУ/);
  P.setLang('en');
  assert.ok(note.textContent.includes('1 $ = 45.53 ₴'), note.textContent);
  assert.ok(note.textContent.includes(enDate('2026-10-31')), note.textContent);
  P.CONFIG.usdRate = 50;
  P.renderFootnote(note);
  assert.ok(note.textContent.includes('1 $ = 50.00 ₴'), note.textContent);
  P.setLang('uk');
  assert.ok(note.textContent.includes('1 $ = 50,00 ₴'), note.textContent);
});

test('renderParts draws one row per part in Ukrainian: name, quantity, price, link', () => {
  const { tbody, P } = boot();
  P.renderParts(tbody);
  const rows = rowsOf(tbody.innerHTML);
  assert.equal(rows.length, P.PARTS.length);
  rows.forEach((row, i) => {
    assert.equal(row.cells.length, 4);
    assert.ok(row.cells[0].text.startsWith(P.TEXT.uk[`parts.name.${P.PARTS[i].id}`]), `row ${i} name`);
    const part = P.PARTS[i];
    assert.equal(row.cells[1].text, part.qty === 'set' ? P.TEXT.uk['parts.qty_set'] : part.kit ? `${part.qty} набір` : String(part.qty));
    assert.ok(row.cells[2].text.length > 0, `row ${i} price cell is empty`);
    assert.ok(row.cells[3].text.length > 0, `row ${i} link cell is empty`);
  });
});

test('the quantity, price and link cells carry their column name as data-label in the language they are drawn in', () => {
  const { tbody, P } = boot();
  P.renderParts(tbody);
  for (const [lang, name] of [['uk', undefined], ['en', 'en']]) {
    if (name) P.renderParts(tbody, name);
    const dict = P.TEXT[lang];
    const rows = rowsOf(tbody.innerHTML);
    assert.ok(rows.length > 0);
    for (const row of rows) {
      assert.equal(attrOf(row.cells[0].attrs, 'data-label'), null, 'the name cell needs no label');
      assert.equal(attrOf(row.cells[1].attrs, 'data-label'), dict['parts.col.qty']);
      assert.equal(attrOf(row.cells[2].attrs, 'data-label'), dict['parts.col.price']);
      assert.equal(attrOf(row.cells[3].attrs, 'data-label'), dict['parts.col.link']);
    }
  }
});

test('on a phone the parts table stacks: the name on top, then quantity, price and link with their labels, so no column hides behind a scroll', () => {
  const css = readSite('assets/style.css');
  const wide = /\.parts-table\s*\{\s*min-width:\s*(\d+)px;\s*\}/.exec(css);
  assert.ok(wide, 'the full table keeps a minimum width');
  const gutters = 2 * 16;
  const scrollbar = 20;
  assert.ok(Number(wide[1]) + gutters + scrollbar <= 600, `the full table (${wide[1]} px) fits a 600 px window with its gutters and a scrollbar, so nothing is clipped above the phone layout`);
  assert.ok(Number(wide[1]) >= 500, 'the full table stays readable');
  const blocks = [...css.matchAll(/@media \(max-width:\s*599px\)\s*\{([\s\S]*?)\n\}/g)].map((m) => m[1]);
  const phone = blocks.find((b) => /\.parts-table/.test(b));
  assert.ok(phone, 'a phone block for the parts table');
  assert.match(phone, /\.parts-table\s*\{[^}]*min-width:\s*0/);
  assert.match(phone, /\.parts-table,\s*\.parts-table tbody,\s*\.parts-table caption\s*\{[^}]*display:\s*block/);
  assert.match(phone, /\.parts-table thead\s*\{[^}]*position:\s*absolute[^}]*clip:/);
  assert.match(phone, /\.parts-table tr\s*\{[^}]*display:\s*flex[^}]*flex-wrap:\s*wrap/);
  assert.match(phone, /\.parts-table \.part-name\s*\{[^}]*flex:\s*1 1 100%/);
  assert.match(phone, /\.parts-table td\[data-label\]::before\s*\{[^}]*content:\s*attr\(data-label\)/);
});

test('renderParts takes the language as an argument and does not depend on the page language', () => {
  const { tbody, P } = boot();
  P.renderParts(tbody, 'en');
  const rows = rowsOf(tbody.innerHTML);
  rows.forEach((row, i) => {
    assert.ok(row.cells[0].text.startsWith(P.TEXT.en[`parts.name.${P.PARTS[i].id}`]), `row ${i} English name`);
  });
});

test('parts without a link render a visible placeholder text, not a link', () => {
  const { tbody, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, priced(10), { id: 'res62k', qty: 1, uah: null, url: null, shop: null });
  P.renderParts(tbody);
  const rows = rowsOf(tbody.innerHTML);
  assert.equal(anchorsIn(rows[0].html).length, 1);
  assert.equal(anchorsIn(rows[1].html).length, 0);
  assert.equal(rows[1].cells[3].text, P.TEXT.uk['parts.no_link']);
  assert.ok(P.TEXT.uk['parts.no_link'].trim().length > 0);
  assert.doesNotMatch(tbody.innerHTML, /href=""|href="#"|href=''/);
  P.renderParts(tbody, 'en');
  assert.equal(rowsOf(tbody.innerHTML)[1].cells[3].text, P.TEXT.en['parts.no_link']);
});

test('rows marked generic show «будь-який постачальник» / "any supplier" instead of a link or the link placeholder', () => {
  const { tbody, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, { id: 'petg_black', qty: 1, uah: null, url: null, shop: null, generic: true });
  P.renderParts(tbody);
  let row = rowsOf(tbody.innerHTML)[0];
  assert.equal(anchorsIn(row.html).length, 0);
  assert.equal(row.cells[3].text, P.TEXT.uk['parts.generic']);
  P.renderParts(tbody, 'en');
  row = rowsOf(tbody.innerHTML)[0];
  assert.equal(row.cells[3].text, P.TEXT.en['parts.generic']);
  assert.notEqual(P.TEXT.uk['parts.generic'], P.TEXT.uk['parts.no_link']);
  assert.ok(P.TEXT.uk['parts.generic'].trim().length > 0 && P.TEXT.en['parts.generic'].trim().length > 0);
});

test('an unusable url never becomes a link: empty, hash, relative, http and javascript values show the placeholder', () => {
  const { tbody, P } = boot();
  for (const bad of ['', '#', '/prod1', 'http://arduino.ua/prod1-x', 'javascript:alert(1)']) {
    P.PARTS.splice(0, P.PARTS.length, priced(10, 1, { url: bad }));
    P.renderParts(tbody);
    const rows = rowsOf(tbody.innerHTML);
    assert.equal(anchorsIn(rows[0].html).length, 0, `"${bad}" became a link`);
    assert.equal(rows[0].cells[3].text, P.TEXT.uk['parts.no_link']);
  }
});

test('rendered links open in a new tab with rel noopener, carry the shop name and a label with the part name', () => {
  const { tbody, P } = boot();
  P.renderParts(tbody);
  const rows = rowsOf(tbody.innerHTML);
  let links = 0;
  rows.forEach((row, i) => {
    const part = P.PARTS[i];
    const anchors = anchorsIn(row.html);
    if (part.url === null) return assert.equal(anchors.length, 0);
    assert.equal(anchors.length, 1);
    links += 1;
    assert.equal(attrOf(anchors[0], 'href'), part.url);
    assert.equal(attrOf(anchors[0], 'target'), '_blank');
    assert.match(attrOf(anchors[0], 'rel'), /\bnoopener\b/);
    assert.ok(attrOf(anchors[0], 'aria-label').includes(P.TEXT.uk[`parts.name.${part.id}`]));
    assert.equal(row.cells[3].text, P.TEXT.uk[`parts.shop.${part.shop === 'arduino.ua' ? 'arduino' : part.shop}`]);
  });
  assert.equal(links, P.PARTS.filter((p) => p.url !== null).length);
  assert.ok(links >= LINKED_IDS.length);
});

test('urls and names are escaped when rendered', () => {
  const { tbody, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, priced(10, 1, { url: 'https://arduino.ua/prod1-a"onmouseover="x' }));
  P.renderParts(tbody);
  assert.doesNotMatch(tbody.innerHTML, /"onmouseover="/);
  assert.doesNotMatch(tbody.innerHTML, /<script/i);
  P.PARTS.splice(0, P.PARTS.length, priced(10, 1, { url: 'https://arduino.ua/prod1-a?x=1&y=2' }));
  P.renderParts(tbody);
  assert.ok(tbody.innerHTML.includes('x=1&amp;y=2'), 'an ampersand in the url is escaped');
  assert.equal(attrOf(anchorsIn(tbody.innerHTML)[0], 'href'), 'https://arduino.ua/prod1-a?x=1&y=2');
});

test('prices render in hryvnias with a thousands separator, nothing in dollars in Ukrainian', () => {
  const { tbody, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, priced(1234), priced(350), priced(22));
  P.renderParts(tbody);
  const prices = rowsOf(tbody.innerHTML).map((r) => r.cells[2].text);
  assert.deepEqual(prices, ['1 234 ₴', '350 ₴', '22 ₴']);
  assert.ok(tbody.innerHTML.includes('1\u00a0234'), 'the thousands separator is a no-break space');
  assert.doesNotMatch(tbody.innerHTML, /\$/);
});

test('EN shows approximate USD from CONFIG.usdRate next to each price', () => {
  const { tbody, P } = boot({ languages: ['en-US'] });
  P.CONFIG.usdRate = 45;
  P.PARTS.splice(0, P.PARTS.length, priced(350), priced(2250), priced(9));
  P.renderParts(tbody);
  const prices = rowsOf(tbody.innerHTML).map((r) => r.cells[2].text);
  assert.deepEqual(prices, ['350 ₴ (≈ $7.8)', '2,250 ₴ (≈ $50)', '9 ₴ (≈ $0.20)']);
  P.CONFIG.usdRate = 50;
  P.renderParts(tbody);
  assert.deepEqual(rowsOf(tbody.innerHTML).map((r) => r.cells[2].text), ['350 ₴ (≈ $7.0)', '2,250 ₴ (≈ $45)', '9 ₴ (≈ $0.18)']);
});

test('a part sold in packs shows the pack size, and a missing price shows a visible placeholder in both languages', () => {
  const { tbody, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, priced(2, 3, { pack: 5 }), { id: 'wires', qty: 1, uah: null, url: null, shop: null });
  P.renderParts(tbody);
  let rows = rowsOf(tbody.innerHTML);
  assert.equal(rows[0].cells[2].text, '2 ₴ за 5 шт.');
  assert.equal(rows[1].cells[2].text, `${DASH} ${P.TEXT.uk['parts.price_missing']}`);
  P.renderParts(tbody, 'en');
  rows = rowsOf(tbody.innerHTML);
  assert.match(rows[0].cells[2].text, /^2 ₴ \(≈ \$0\.\d+\) per 5 pcs$/);
  assert.equal(rows[1].cells[2].text, `${DASH} ${P.TEXT.en['parts.price_missing']}`);
  assert.ok(P.TEXT.uk['parts.price_missing'].trim().length > 0 && P.TEXT.en['parts.price_missing'].trim().length > 0);
});

test('a note under a part name shows when the dictionary has one', () => {
  const { tbody, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, priced(10, 1, { id: 'petg_white' }));
  assert.ok('parts.note.petg_white' in P.TEXT.uk);
  P.renderParts(tbody);
  const first = rowsOf(tbody.innerHTML)[0].cells[0];
  assert.ok(first.text.includes(P.TEXT.uk['parts.note.petg_white']));
  P.renderParts(tbody, 'en');
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.includes(P.TEXT.en['parts.note.petg_white']));
});

test('partsTotal sums uah × qty and counts as missing only the parts without a price that are not generic', () => {
  const { P } = boot();
  P.CONFIG.usdRate = 50;
  P.PARTS.splice(
    0,
    P.PARTS.length,
    priced(100, 2),
    priced(22, 1),
    priced(2, 3, { pack: 5 }),
    priced(10, 11, { pack: 20 }),
    { id: 'tpu', qty: 1, uah: null, url: null, shop: null, generic: true, costAs: 'plastic' },
    { id: 'wires', qty: 4, uah: null, url: null, shop: null, generic: true, costAs: 'small' },
    { id: 'mic', qty: 1, uah: null, url: null, shop: null },
    { id: 'devkit', qty: 1, uah: null, url: null, shop: null },
  );
  assert.deepEqual({ ...P.partsTotal() }, { uah: 100 * 2 + 22 + 2 + 10, usd: Math.round(234 / 50), missing: 2 });
  P.PARTS.splice(0, P.PARTS.length, priced(100, 2));
  assert.deepEqual({ ...P.partsTotal() }, { uah: 200, usd: 4, missing: 0 });
  P.PARTS.splice(0, P.PARTS.length);
  assert.deepEqual({ ...P.partsTotal() }, { uah: 0, usd: 0, missing: 0 });
});

test('partsTotal over the real list counts exactly the unpriced rows that are not generic and matches the sum of the priced ones', () => {
  const { P } = boot();
  const total = P.partsTotal();
  const uahOf = (p) => (p.uah !== null ? p.uah : p.usd !== undefined ? Math.round(p.usd * P.CONFIG.usdRate) : null);
  const unpriced = P.PARTS.filter((p) => uahOf(p) === null && !p.generic).length;
  const sum = P.PARTS.filter((p) => uahOf(p) !== null).reduce((acc, p) => acc + uahOf(p) * Math.ceil(p.qty / (p.pack || 1)), 0);
  assert.ok(sum >= Math.round(4.89 * P.CONFIG.usdRate), 'the filament pack is in the sum');
  assert.equal(total.missing, unpriced);
  assert.equal(total.uah, sum);
  assert.equal(total.usd, Math.round(sum / P.CONFIG.usdRate));
  assert.ok(total.uah > 500, 'the priced part of the list is more than a few hundred hryvnias');
});

test('the cost fact names what the total leaves out instead of counting rows: parts without a price one by one, generic rows as plastic and small parts', () => {
  const { cost, P } = boot();
  const nbsp = '\u00a0';
  const read = () => cost.textContent.replace(new RegExp(nbsp, 'g'), ' ');
  const row = (id, extra = {}) => ({ id, qty: 1, uah: null, url: null, shop: null, ...extra });
  P.PARTS.splice(0, P.PARTS.length, priced(125, 2), row('devkit'));
  P.renderCost(cost);
  assert.equal(read(), 'від 250 ₴ без плати DevKit');
  P.PARTS.splice(0, P.PARTS.length, priced(125, 2), row('devkit'), row('mic'));
  P.renderCost(cost);
  assert.equal(read(), 'від 250 ₴ без плати DevKit і мікрофона');
  P.PARTS.splice(0, P.PARTS.length, priced(125, 2), row('devkit'), row('petg_black', { generic: true, costAs: 'plastic' }), row('mic'), row('tpu', { generic: true, costAs: 'plastic' }), row('filament'));
  P.renderCost(cost);
  assert.equal(read(), 'від 250 ₴ без плати DevKit, мікрофона, ниток і пластику');
  P.PARTS.push(row('screws', { generic: true, costAs: 'small' }), row('washers', { generic: true, costAs: 'small' }));
  P.renderCost(cost);
  assert.equal(read(), 'від 250 ₴ без плати DevKit, мікрофона, ниток, пластику й дрібниць');
  assert.deepEqual(Array.from(P.costLeft()), ['devkit', 'mic', 'filament', 'plastic', 'small']);
  assert.doesNotMatch(read(), /\d+ детал/);
});

test('the cost fact over the real list leaves out the charger, small parts and plastic, from the data', () => {
  const { cost, P } = boot();
  const priced = (p) => p.uah !== null || p.usd !== undefined;
  const expected = [];
  for (const p of P.PARTS) {
    if (priced(p)) continue;
    const key = p.costAs || p.id;
    if (!p.costAs && !expected.includes(key)) expected.push(key);
  }
  for (const p of P.PARTS) if (!priced(p) && p.costAs && !expected.includes(p.costAs)) expected.push(p.costAs);
  assert.deepEqual(Array.from(P.costLeft()), expected);
  assert.deepEqual(expected, ['charger', 'small', 'plastic']);
  for (const key of expected) assert.ok(`cost.left.${key}` in P.TEXT.uk && `cost.left.${key}` in P.TEXT.en, `cost.left.${key}`);
  const total = P.partsTotal();
  assert.equal(cost.textContent.replace(/\u00a0/g, ' '), `від ${total.uah.toLocaleString('uk-UA').replace(/\s/g, ' ')} ₴ без зарядки, дрібниць і пластику`);
  P.setLang('en');
  assert.equal(cost.textContent, `from ${total.uah.toLocaleString('en-US')} ₴ (≈ $${total.usd}), excluding the charger, small parts and plastic`);
  for (const id of ['washers', 'petg_black', 'petg_white', 'tpu']) assert.equal(P.PARTS.find((p) => p.id === id).generic, true, `${id} is generic`);
  for (const id of ['inserts', 'screws', 'washers', 'wires']) assert.equal(P.PARTS.find((p) => p.id === id).costAs, 'small', `${id} counts as a small part`);
  for (const lang of ['uk', 'en']) for (const n of ['one', 'few', 'many']) assert.equal(P.TEXT[lang][`cost.missing.${n}`], undefined);
});

test('the cost fact has no "without" part when nothing is left out, and shows thousands with a no-break space', () => {
  const { cost, P } = boot();
  P.PARTS.splice(0, P.PARTS.length, priced(1250, 2));
  P.renderCost(cost);
  assert.equal(cost.textContent, 'близько 2\u00a0500 ₴');
  assert.doesNotMatch(cost.textContent, /без /);
});

test('in English the cost fact reads "from N ₴ (≈ $M), excluding …" and follows CONFIG.usdRate', () => {
  const { cost, P } = boot({ languages: ['en-US'] });
  P.CONFIG.usdRate = 50;
  P.PARTS.splice(0, P.PARTS.length, priced(1250, 2), { id: 'tpu', qty: 1, uah: null, url: null, shop: null, generic: true, costAs: 'plastic' });
  P.renderCost(cost);
  assert.equal(cost.textContent, 'from 2,500 ₴ (≈ $50), excluding plastic');
  P.PARTS.push({ id: 'mic', qty: 1, uah: null, url: null, shop: null });
  P.CONFIG.usdRate = 25;
  P.renderCost(cost);
  assert.equal(cost.textContent, 'from 2,500 ₴ (≈ $100), excluding the microphone and plastic');
  P.PARTS.pop();
  P.PARTS.pop();
  P.renderCost(cost);
  assert.equal(cost.textContent, 'about 2,500 ₴ (≈ $100)');
});

test('the cost fact is drawn at load for the page language and follows the language switch', () => {
  const { cost, P } = boot();
  const total = P.partsTotal();
  assert.ok(cost.textContent.startsWith('від '), cost.textContent);
  assert.ok(cost.textContent.includes('₴'));
  assert.ok(total.missing === 0 || / без /.test(cost.textContent));
  P.setLang('en');
  assert.ok(cost.textContent.startsWith('from '), cost.textContent);
  assert.match(cost.textContent, /≈ \$\d/);
  P.setLang('uk');
  assert.ok(cost.textContent.startsWith('від '));
});

test('switching the language re-renders the parts table in place without doubling rows', () => {
  const { tbody, P } = boot();
  const count = P.PARTS.length;
  assert.equal(rowsOf(tbody.innerHTML).length, count);
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.uk['parts.name.devkit']));
  P.setLang('en');
  assert.equal(rowsOf(tbody.innerHTML).length, count);
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.en['parts.name.devkit']));
  P.setLang('uk');
  assert.equal(rowsOf(tbody.innerHTML).length, count);
  P.renderParts(tbody);
  P.renderParts(tbody);
  assert.equal(rowsOf(tbody.innerHTML).length, count);
});

test('the tables are drawn with the saved language while the document is still loading', () => {
  const { tbody, cost, env, P } = boot({ stored: 'en', languages: ['uk-UA'], readyState: 'loading' });
  assert.equal(P.lang, 'en');
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.en['parts.name.devkit']));
  env.document.readyState = 'interactive';
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.en['parts.name.devkit']));
  assert.ok(cost.textContent.startsWith('from '), cost.textContent);
});

test('TOOLS lists the twelve tools and renderTools draws them as list items in both languages', () => {
  const { list, P } = boot();
  assert.deepEqual(Array.from(P.TOOLS, (t) => t.id), TOOL_IDS);
  for (const id of TOOL_IDS) assert.ok(`tools.item.${id}` in P.TEXT.uk && `tools.item.${id}` in P.TEXT.en, `${id} is not translated`);
  const items = (html) => [...html.matchAll(/<li\b[^>]*>([\s\S]*?)<\/li>/g)].map((m) => unescapeHtml(m[1]));
  assert.deepEqual(items(list.innerHTML), TOOL_IDS.map((id) => P.TEXT.uk[`tools.item.${id}`]));
  P.setLang('en');
  assert.deepEqual(items(list.innerHTML), TOOL_IDS.map((id) => P.TEXT.en[`tools.item.${id}`]));
  P.renderTools(list);
  assert.equal(items(list.innerHTML).length, TOOL_IDS.length);
  assert.doesNotMatch(list.innerHTML, /<a\b/);
  for (const id of GONE_TOOLS) {
    assert.equal(P.TEXT.uk[`tools.item.${id}`], undefined, id);
    assert.equal(P.TEXT.en[`tools.item.${id}`], undefined, id);
  }
});

test('the page has the parts table in a scrolling wrapper, a dated caption, translated headers and the tools list', () => {
  const page = readPage();
  const parts = /<section id="parts"[\s\S]*?<\/section>/.exec(page);
  assert.ok(parts, 'parts section');
  const html = parts[0];
  assert.match(html, /<div class="table-wrap">\s*<table\b[^>]*class="parts-table"/);
  assert.match(html, /<caption data-i18n="parts\.caption">/);
  assert.match(html, /<p class="parts-footnote" data-parts-footnote><\/p>/);
  const heads = [...html.matchAll(/<th scope="col" data-i18n="([^"]+)">/g)].map((m) => m[1]);
  assert.deepEqual(heads, ['parts.col.part', 'parts.col.qty', 'parts.col.price', 'parts.col.link']);
  assert.match(html, /<tbody data-parts><\/tbody>/);
  const tools = /<section id="tools"[\s\S]*?<\/section>/.exec(page);
  assert.ok(tools, 'tools section');
  assert.match(tools[0], /<ul class="marks" data-tools><\/ul>/);
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const key of ['parts.col.part', 'parts.col.qty', 'parts.col.price', 'parts.col.link', 'parts.caption']) {
    assert.ok(TEXT.uk[key] && TEXT.en[key], `${key} is missing in one language`);
  }
  assert.equal(TEXT.uk['parts.col.part'], 'Деталь');
  assert.equal(TEXT.uk['parts.caption'], 'Кількість вказано на один пристрій.');
  assert.equal(TEXT.en['parts.caption'], 'Quantities are per device.');
});

test('the need section fills the cost slot from partsTotal and keeps the time slot apart from it', () => {
  const page = readPage();
  assert.match(page, /<dd id="need-cost" data-cost\b/);
  assert.match(page, /<dd id="need-time"/);
  assert.doesNotMatch(page.match(/<dd id="need-time"[^>]*>/)[0], /data-cost/);
});

test('the parts styles keep numbers and links on one line, mute the placeholder and hide an empty footnote', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /\.parts-table\b/);
  assert.match(css, /\.part-price\s*\{[^}]*white-space:\s*nowrap/);
  assert.match(css, /\.part-nolink\s*\{[^}]*color:\s*var\(--text-secondary\)/);
  assert.match(css, /\.part-note\s*\{[^}]*color:\s*var\(--text-secondary\)/);
  assert.match(css, /\.parts-footnote:empty\s*\{[^}]*display:\s*none/);
});

test('the parts and tools wording stays calm: no sales phrases and no forbidden names', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const keys = Object.keys(TEXT.uk).filter((k) => /^(parts|tools|cost)\./.test(k));
  assert.ok(keys.length > 40);
  const text = keys.map((k) => `${TEXT.uk[k]} ${TEXT.en[k]}`).join('\n');
  assert.doesNotMatch(text, /Home Assistant|\bHA\b|Assist|українською|Ukrainian|купуйте|buy now|знижк|discount|best price|найкращ/i);
});

test('wires and the charger are counted as a set, not as one piece', () => {
  const { tbody, P } = boot();
  for (const id of ['wires', 'charger']) assert.equal(P.PARTS.find((p) => p.id === id).qty, 'set', id);
  assert.equal(P.TEXT.uk['parts.qty_set'], 'набір');
  assert.equal(P.TEXT.en['parts.qty_set'], 'set');
  const qty = (lang) => {
    P.renderParts(tbody, lang);
    return Object.fromEntries(rowsOf(tbody.innerHTML).map((row, i) => [P.PARTS[i].id, row.cells[1].text]));
  };
  assert.equal(qty('uk').charger, 'набір');
  assert.equal(qty('en').wires, 'set');
  assert.equal(qty('en').washers, '4');
});

test('the charger note asks for a slim USB-C plug that passes the slot, and the filament note names the variant the resistor is sized for', () => {
  const { P } = boot();
  assert.match(P.TEXT.uk['parts.note.charger'], /не більше приблизно 10 × 6,2 мм/);
  assert.match(P.TEXT.uk['parts.note.charger'], /проріз/);
  assert.match(P.TEXT.en['parts.note.charger'], /at most about 10 × 6\.2 mm/);
  assert.match(P.TEXT.en['parts.note.charger'], /slot/);
  assert.match(P.TEXT.uk['parts.note.filament'], /теплий білий, 3 В, близько 185 мм, 100 мА/);
  assert.match(P.TEXT.uk['parts.note.filament'], /47 Ом від 5 В[^.]*3 В/);
  assert.match(P.TEXT.en['parts.note.filament'], /warm white, 3 V, about 185 mm, 100 mA/);
  assert.match(P.TEXT.en['parts.note.filament'], /47 Ω resistor from 5 V assumes a 3 V filament/);
});

test('the tools list has what the steps use: solder and flux, a Phillips screwdriver for the M2.5 screws, a flat one for the terminal block, glue or tape for the feet', () => {
  const { P } = boot();
  assert.match(P.TEXT.uk['tools.item.solder'], /Припій і флюс/);
  assert.equal(P.TEXT.uk['tools.item.screwdriver'], 'Хрестова викрутка для гвинтів M2.5');
  assert.equal(P.TEXT.en['tools.item.screwdriver'], 'Phillips screwdriver for the M2.5 screws');
  assert.match(P.TEXT.uk['tools.item.flat_screwdriver'], /пласка викрутка[^.]*клемника підсилювача/);
  assert.match(P.TEXT.en['tools.item.flat_screwdriver'], /flat screwdriver[^.]*terminal block/);
  assert.match(P.TEXT.uk['tools.item.feet_glue'], /Суперклей або двосторонній скотч/);
  assert.match(P.TEXT.en['tools.item.feet_glue'], /Superglue or double-sided tape/);
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(P.TEXT[lang])) {
      assert.doesNotMatch(value, /шестигран|hex key|ключ(ем)? 1,5|знімач ізоляції|wire stripper|термофен|heat gun/i, `${lang} ${key}`);
    }
  }
});
