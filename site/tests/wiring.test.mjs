import test from 'node:test';
import assert from 'node:assert/strict';
import { loadScripts, makeElement, makeEnv, readPage, readSite } from './helpers.mjs';
import { openAttr } from './svg-helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js', 'assets/data.js'];

const UK_ROWS = [
  ['мікрофон VDD', '3V3', '11 см'],
  ['мікрофон GND', 'GND (ряд з 3V3)', '11,5 см'],
  ['мікрофон SD', 'D33', '6,5 см'],
  ['мікрофон WS', 'D25', '8 см'],
  ['мікрофон SCK', 'D32', '6 см'],
  ['підсилювач VIN', 'VIN', '8 см'],
  ['підсилювач GND', 'GND', '7 см'],
  ['підсилювач DIN', 'D22', '3,5 см'],
  ['підсилювач BCLK', 'D26', '7 см'],
  ['підсилювач LRC', 'D27', '7 см'],
  ['транзистор G (ліва ніжка), через 47 Ом', 'D4', '7 см'],
  ['транзистор D (середня ніжка), 2 дроти', '«−» нитки 1 і «−» нитки 2', 'по 8,5 см'],
  ['транзистор S (права ніжка)', 'GND (будь-який із двох)', '8 см'],
  ['«+» кожної нитки, через 47 Ом', 'VIN', '≈9 см, орієнтовно'],
  ['кнопка, внутрішня ніжка 1', 'D19', '8 см'],
  ['кнопка, внутрішня ніжка 2', 'GND', '8 см'],
];

const EN_ROWS = [
  ['microphone VDD', '3V3', '11 cm'],
  ['microphone GND', 'GND (row with 3V3)', '11.5 cm'],
  ['microphone SD', 'D33', '6.5 cm'],
  ['microphone WS', 'D25', '8 cm'],
  ['microphone SCK', 'D32', '6 cm'],
  ['amplifier VIN', 'VIN', '8 cm'],
  ['amplifier GND', 'GND', '7 cm'],
  ['amplifier DIN', 'D22', '3.5 cm'],
  ['amplifier BCLK', 'D26', '7 cm'],
  ['amplifier LRC', 'D27', '7 cm'],
  ['transistor G (left leg), through 47 Ω', 'D4', '7 cm'],
  ['transistor D (middle leg), 2 wires', '“−” of filament 1 and “−” of filament 2', '8.5 cm each'],
  ['transistor S (right leg)', 'GND (either of the two)', '8 cm'],
  ['“+” of each filament, through 47 Ω', 'VIN', '≈9 cm, estimate'],
  ['button, inner leg 1', 'D19', '8 cm'],
  ['button, inner leg 2', 'GND', '8 cm'],
];

const FROM_KEYS = [
  'mic_vdd', 'mic_gnd', 'mic_sd', 'mic_ws', 'mic_sck',
  'amp_vin', 'amp_gnd', 'amp_din', 'amp_bclk', 'amp_lrc',
  'ring_g', 'ring_d', 'ring_s', 'ring_plus', 'btn_1', 'btn_2',
].map((name) => `wires.from.${name}`);

const CM = [11, 11.5, 6.5, 8, 6, 8, 7, 3.5, 7, 7, 7, 8.5, 8, 9, 8, 8];

const LABEL_ROWS = {
  'steps.capacitor.w_lrc': 'amp_lrc',
  'steps.capacitor.w_bclk': 'amp_bclk',
  'steps.capacitor.w_din': 'amp_din',
  'steps.capacitor.w_gnd': 'amp_gnd',
  'steps.capacitor.w_vin': 'amp_vin',
  'steps.mic.w_vdd': 'mic_vdd',
  'steps.mic.w_gnd': 'mic_gnd',
  'steps.mic.w_sd': 'mic_sd',
  'steps.mic.w_ws': 'mic_ws',
  'steps.mic.w_sck': 'mic_sck',
  'steps.devkit.w_amp_din': 'amp_din',
  'steps.devkit.w_amp_bclk': 'amp_bclk',
  'steps.devkit.w_amp_lrc': 'amp_lrc',
  'steps.devkit.w_amp_gnd': 'amp_gnd',
  'steps.devkit.w_amp_vin': 'amp_vin',
  'steps.devkit.w_mic_vdd': 'mic_vdd',
  'steps.devkit.w_mic_gnd': 'mic_gnd',
  'steps.devkit.w_mic_sd': 'mic_sd',
  'steps.devkit.w_mic_ws': 'mic_ws',
  'steps.devkit.w_mic_sck': 'mic_sck',
  'steps.devkit.w_ring_g': 'ring_g',
  'steps.devkit.w_ring_s': 'ring_s',
  'steps.devkit.w_ring_plus': 'ring_plus',
  'steps.devkit.w_btn_d19': 'btn_1',
  'steps.devkit.w_btn_gnd': 'btn_2',
};

const PIN_LITERALS = ['3V3', 'GND', 'D33', 'D25', 'D32', 'VIN', 'D22', 'D26', 'D27', 'D4', 'D19'];

function unescapeHtml(text) {
  return text.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
}

function rowsOf(html) {
  return [...html.matchAll(/<tr\b([^>]*)>([\s\S]*?)<\/tr>/g)].map((tr) => ({
    attrs: tr[1],
    cells: [...tr[2].matchAll(/<td\b[^>]*>([\s\S]*?)<\/td>/g)].map((td) => unescapeHtml(td[1])),
  }));
}

function bootTable({ languages = ['uk-UA'], stored = null, readyState = 'complete' } = {}) {
  const tbody = makeElement({ 'data-wires': '' });
  tbody.innerHTML = '';
  const env = makeEnv({ children: [tbody], languages, stored, readyState });
  const P = loadScripts(SCRIPTS, env);
  return { tbody, env, P };
}

function figureBlock(page, id) {
  const m = new RegExp(`<figure\\b([^>]*\\bdata-stepper="${id}"[^>]*)>([\\s\\S]*?)</figure>`).exec(page);
  assert.ok(m, `stepper ${id} is missing`);
  return m[2];
}

function groupHtml(body, attr, k) {
  const open = new RegExp(`<g\\b[^>]*\\b${attr}="${k}"[^>]*>`).exec(body);
  assert.ok(open, `no <g ${attr}="${k}">`);
  const start = open.index + open[0].length;
  const re = /<g\b[^>]*>|<\/g>/g;
  re.lastIndex = start;
  let depth = 1;
  let t;
  while ((t = re.exec(body))) {
    depth += t[0] === '</g>' ? -1 : 1;
    if (depth === 0) return body.slice(start, t.index);
  }
  assert.fail(`<g ${attr}="${k}"> is not closed`);
  return '';
}

function keysIn(html) {
  return [...html.matchAll(/data-i18n="([^"]+)"/g)].map((m) => m[1]);
}

function textsIn(html) {
  return [...html.matchAll(/<text\b([^>]*)>/g)].map((m) => ({
    key: openAttr(m[1], 'data-i18n'),
    x: Number(openAttr(m[1], 'x')),
    y: Number(openAttr(m[1], 'y')),
    anchor: openAttr(m[1], 'text-anchor') || 'start',
  }));
}

function numberText(lang, cm) {
  return lang === 'uk' ? String(cm).replace('.', ',') : String(cm);
}

test('data.js is a classic script loaded after text.js and i18n.js, with no fetch or modules', () => {
  const page = readPage();
  const srcs = [...page.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gi)].map((m) => m[1]);
  const at = (name) => srcs.indexOf(name);
  assert.ok(at('assets/data.js') >= 0, 'data.js is not loaded');
  assert.ok(at('assets/data.js') > at('assets/text.js'));
  assert.ok(at('assets/data.js') > at('assets/i18n.js'));
  const code = readSite('assets/data.js');
  assert.doesNotMatch(code, /\bfetch\s*\(|XMLHttpRequest|^\s*(?:import|export)\b/m);
});

test('WIRES has 16 rows in table order with the measured lengths; only the «+» row is an estimate and only the D row is per wire', () => {
  const { P } = bootTable();
  assert.equal(P.WIRES.length, 16);
  const column = (pick) => Array.from(P.WIRES, pick);
  assert.deepEqual(column((w) => w.from), FROM_KEYS);
  assert.deepEqual(column((w) => w.cm), CM);
  assert.deepEqual(column((w) => w.approx), CM.map((_, i) => i === 13));
  assert.deepEqual(column((w) => w.each === true), CM.map((_, i) => i === 11));
  for (const w of P.WIRES) {
    assert.ok(w.from in P.TEXT.uk && w.from in P.TEXT.en, `${w.from} is not translated`);
    const known = w.to in P.TEXT.uk ? w.to in P.TEXT.en : PIN_LITERALS.includes(w.to);
    assert.ok(known, `${w.from}: "to" ${w.to} is neither a pin nor a key in both languages`);
  }
});

test('renderWires fills the table body with 16 Ukrainian rows: from, to, length', () => {
  const { tbody, P } = bootTable();
  P.renderWires(tbody);
  const rows = rowsOf(tbody.innerHTML);
  assert.equal(rows.length, 16);
  assert.deepEqual(rows.map((r) => r.cells), UK_ROWS);
});

test('the table is in English when the page is in English, with decimal points and units', () => {
  const { tbody, P } = bootTable({ languages: ['en-US'] });
  assert.equal(P.lang, 'en');
  const rows = rowsOf(tbody.innerHTML);
  assert.equal(rows.length, 16);
  assert.deepEqual(rows.map((r) => r.cells), EN_ROWS);
});

test('the estimate row renders as «≈9 см, орієнтовно» / "≈9 cm, estimate" and is the only marked row', () => {
  const uk = bootTable();
  const ukRows = rowsOf(uk.tbody.innerHTML);
  assert.equal(ukRows[13].cells[2], '≈9 см, орієнтовно');
  assert.equal(ukRows.filter((r) => /орієнтовно/.test(r.cells[2])).length, 1);
  assert.deepEqual(ukRows.map((r) => /data-approx="true"/.test(r.attrs)), CM.map((_, i) => i === 13));
  const en = bootTable({ languages: ['en'] });
  const enRows = rowsOf(en.tbody.innerHTML);
  assert.equal(enRows[13].cells[2], '≈9 cm, estimate');
  assert.equal(enRows.filter((r) => /estimate/.test(r.cells[2])).length, 1);
});

test('switching the language re-renders the table in place and keeps 16 rows', () => {
  const { tbody, P } = bootTable();
  assert.deepEqual(rowsOf(tbody.innerHTML).map((r) => r.cells), UK_ROWS);
  P.setLang('en');
  assert.deepEqual(rowsOf(tbody.innerHTML).map((r) => r.cells), EN_ROWS);
  P.setLang('uk');
  assert.deepEqual(rowsOf(tbody.innerHTML).map((r) => r.cells), UK_ROWS);
});

test('the table is drawn with the saved language while the document is still loading', () => {
  const { tbody, env, P } = bootTable({ stored: 'en', languages: ['uk-UA'], readyState: 'loading' });
  assert.equal(P.lang, 'en');
  assert.deepEqual(rowsOf(tbody.innerHTML).map((r) => r.cells), EN_ROWS);
  env.document.readyState = 'interactive';
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.deepEqual(rowsOf(tbody.innerHTML).map((r) => r.cells), EN_ROWS);
});

test('every row has its length, so the not-measured placeholder is gone', () => {
  const { P } = bootTable();
  for (const row of P.WIRES) assert.ok(typeof row.cm === 'number' && row.cm > 0, row.from);
  assert.equal(P.TEXT.uk['wires.len_missing'], undefined);
  assert.equal(P.TEXT.en['wires.len_missing'], undefined);
  assert.doesNotMatch(readSite('assets/data.js'), /len_missing/);
});

test('renderWires renders the same body twice without doubling rows', () => {
  const { tbody, P } = bootTable();
  P.renderWires(tbody);
  P.renderWires(tbody);
  assert.equal(rowsOf(tbody.innerHTML).length, 16);
});

test('the table caption says the lengths are recommended and include some spare, and what «≈» means, in the dictionary and in the page', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['wires.caption'], /^Усі довжини рекомендовані й узяті із запасом\./);
  assert.match(TEXT.en['wires.caption'], /^All lengths are recommended and include some spare\./);
  assert.match(TEXT.uk['wires.caption'], /«≈» — орієнтовна/);
  assert.match(TEXT.en['wires.caption'], /“≈” marks an approximate/);
  for (const lang of ['uk', 'en']) assert.doesNotMatch(TEXT[lang]['wires.caption'], /прототип|prototype|виміряно|measured/i, lang);
  const page = readPage();
  const caption = /<caption\b[^>]*\bdata-i18n="wires\.caption"[^>]*>([^<]*)<\/caption>/.exec(page);
  assert.ok(caption, 'the table has no translated <caption>');
  assert.equal(caption[1].trim(), TEXT.uk['wires.caption']);
});

test('the wire table sits in a scrolling wrapper in the solder section after the DevKit stepper and the pin map, with translated headers', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  const section = /<section\b[^>]*\bid="solder"[^>]*>([\s\S]*?)<\/section>/.exec(page)[1];
  const wrap = /<div class="table-wrap">\s*<table\b([^>]*)>([\s\S]*?)<\/table>\s*<\/div>/.exec(section);
  assert.ok(wrap, 'no .table-wrap table in #solder');
  assert.match(wrap[2], /<tbody\b[^>]*\bdata-wires\b[^>]*>\s*<\/tbody>/);
  const heads = [...wrap[2].matchAll(/<th\b([^>]*)>([^<]*)<\/th>/g)];
  assert.deepEqual(heads.map((h) => openAttr(h[1], 'data-i18n')), ['wires.col.from', 'wires.col.to', 'wires.col.len']);
  for (const h of heads) {
    assert.match(h[1], /\bscope="col"/);
    assert.equal(h[2].trim(), TEXT.uk[openAttr(h[1], 'data-i18n')]);
  }
  const at = (needle) => section.indexOf(needle);
  assert.ok(at('data-stepper="devkit"') >= 0);
  assert.ok(at('data-pinmap') > at('data-stepper="devkit"'), 'pin map after the DevKit stepper');
  assert.ok(at('class="table-wrap"') > at('data-pinmap'), 'table after the pin map');
  assert.ok(at('data-stepper="mic"') >= 0 && at('data-stepper="mic"') < at('data-stepper="ring"'), 'microphone stepper first');
  for (const key of ['solder.mic.title', 'solder.devkit.title', 'solder.pinmap.title', 'solder.wires.title']) {
    assert.match(section, new RegExp(`<h3\\b[^>]*\\bdata-i18n="${key.replace(/\./g, '\\.')}">`), `${key} heading`);
    assert.ok(TEXT.uk[key] && TEXT.en[key] && TEXT.uk[key] !== TEXT.en[key], `${key} in both languages`);
  }
});

test('microphone stepper text: chip side, no pins, jumper first, tails flush on the sound-hole side, never poke the hole', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const uk = (k) => TEXT.uk[`steps.mic.${k}`];
  const en = (k) => TEXT.en[`steps.mic.${k}`];
  assert.match(uk(1), /без впаяних штирів/);
  assert.match(uk(1), /з боку чипа/);
  assert.match(uk(1), /отвором для звуку лишається рівним/);
  assert.match(en(1), /without pins/);
  assert.match(en(1), /chip side/);
  assert.match(en(1), /sound hole, stays flat/);
  assert.match(uk(2), /L\/R із GND/);
  assert.match(uk(2), /з боку чипа/);
  assert.match(uk(2), /Не робіть цього краплею припою на боці з отвором/);
  assert.match(en(2), /L\/R to GND with a short piece of wire/);
  assert.match(en(2), /blob of solder on the sound-hole side/);
  assert.match(uk(3), /GND і VDD/);
  assert.match(uk(4), /SD, WS і SCK/);
  assert.match(en(3), /GND and VDD/);
  assert.match(en(4), /SD, WS and SCK/);
  assert.match(uk(5), /врівень з платою/);
  assert.match(uk(5), /Нічим не тицяйте в отвір/);
  assert.match(uk(5), /мембран/);
  assert.match(en(5), /flush with the board/);
  assert.match(en(5), /Do not poke anything into the hole/);
  assert.match(en(5), /membrane/);
});

test('microphone drawing: jumper in step 2, GND and VDD in step 3, SD, WS and SCK in step 4, the sound-hole side in step 5, a note to solder by the labels on the board', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const body = figureBlock(readPage(), 'mic');
  assert.ok(keysIn(groupHtml(body, 'data-s', 1)).includes('steps.mic.order_note'));
  assert.ok(keysIn(groupHtml(body, 'data-s', 1)).includes('steps.mic.chip'));
  assert.deepEqual(keysIn(groupHtml(body, 'data-s', 2)).filter((k) => k.startsWith('steps.mic.')), ['steps.mic.jumper']);
  assert.deepEqual(keysIn(groupHtml(body, 'data-s', 3)).filter((k) => k.startsWith('steps.mic.w_')), ['steps.mic.w_vdd', 'steps.mic.w_gnd']);
  assert.deepEqual(keysIn(groupHtml(body, 'data-s', 4)).filter((k) => k.startsWith('steps.mic.w_')), ['steps.mic.w_ws', 'steps.mic.w_sck', 'steps.mic.w_sd']);
  const flat = keysIn(groupHtml(body, 'data-s', 5));
  for (const key of ['steps.mic.flat_title', 'steps.mic.flat_hole', 'steps.mic.flat_trim', 'steps.mic.flat_poke']) assert.ok(flat.includes(key), `${key} in step 5`);
  assert.match(TEXT.uk['steps.mic.order_note'], /паяйте за написами на своїй/);
  assert.match(TEXT.en['steps.mic.order_note'], /solder by the labels on your board/);
  const all = new Set(keysIn(body));
  for (const pad of ['vdd', 'gnd', 'lr', 'ws', 'sck', 'sd']) assert.ok(all.has(`steps.common.${pad}`), `pad label ${pad}`);
});

test('devkit stepper text: tip soldering first, marker on the black header, turn over, the four wire groups, two continuity checks', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const uk = (k) => TEXT.uk[`steps.devkit.${k}`];
  const en = (k) => TEXT.en[`steps.devkit.${k}`];
  assert.match(uk(1), /кінчика штиря/);
  assert.match(uk(1), /відразу відводьте дріт убік/);
  assert.match(uk(1), /лише 5 мм/);
  assert.match(en(1), /tip of the pin/);
  assert.match(en(1), /sideways/);
  assert.match(en(1), /only 5 mm/);
  for (const pin of ['D4', 'D19', 'D22', 'D25', 'D26', 'D27', 'D32', 'D33', 'VIN', '3V3', 'обидва GND']) assert.ok(uk(2).includes(pin), `uk step 2 names ${pin}`);
  for (const pin of ['D4', 'D19', 'D22', 'D25', 'D26', 'D27', 'D32', 'D33', 'VIN', '3V3', 'both GND']) assert.ok(en(2).includes(pin), `en step 2 names ${pin}`);
  assert.match(uk(2), /Написи й модуль ESP32 лежать з одного боку плати, а чорна пластмасова гребінка штирів — з іншого/);
  assert.match(uk(2), /Переверніть плату гребінкою догори, антеною до себе/);
  assert.match(uk(2), /маркером/);
  assert.match(uk(2), /чорній гребінці/);
  assert.match(uk(2), /ряд з D22, D4, D19 і 3V3 лишається праворуч, але порядок штирів у кожному ряду тепер іде з іншого кінця: VIN і 3V3 найдалі від вас, біля USB-C/);
  assert.match(en(2), /The lettering and the ESP32 module are on one side of the board, and the black plastic pin header is on the other/);
  assert.match(en(2), /Turn the board over, header up and antenna towards you/);
  assert.match(en(2), /marker/);
  assert.match(en(2), /black header/);
  assert.match(en(2), /the row with D22, D4, D19 and 3V3 stays on the right, but the pins in each row now run from the other end: VIN and 3V3 are furthest from you, by the USB-C/);
  assert.match(uk(3), /штирями вгору/);
  assert.match(uk(3), /ряд з D22, D4, D19 і 3V3 з боку підсилювача, ряд з D26, D27 і VIN з боку транзистора/);
  assert.match(en(3), /the row with D22, D4, D19 and 3V3 is on the amplifier side, the row with D26, D27 and VIN is on the transistor side/);
  assert.match(uk(4), /DIN на D22, BCLK на D26, LRC на D27/);
  assert.match(uk(5), /VDD на 3V3, GND на штир GND поруч із 3V3, SD на D33, WS на D25, SCK на D32/);
  assert.match(uk(6), /D4/);
  assert.match(uk(6), /будь-який із двох/);
  assert.match(uk(6), /орієнтовні/);
  assert.match(uk(7), /D19/);
  assert.match(uk(8), /VIN і GND/);
  assert.match(uk(8), /Має бути тиша; короткий писк на самому початку — теж нормально/);
  assert.match(uk(8), /конденсатор/);
  assert.match(uk(8), /Безперервний писк означає замикання/);
  assert.match(uk(9), /3V3 і GND/);
  assert.match(uk(9), /тиша/);
  assert.match(en(8), /VIN and GND/);
  assert.match(en(8), /It should stay silent; a short beep at the very start is fine too/);
  assert.match(en(8), /continuous beep means a short circuit/);
  assert.match(en(9), /3V3 and GND/);
  assert.match(en(9), /silence/);
});

test('devkit drawing: step 2 shows two views, the labels side with the printed names and no black header, and the header side with the same 12 pins ringed and named by hand', () => {
  const body = figureBlock(readPage(), 'devkit');
  const two = groupHtml(body, 'data-only', 2);
  const tray = groupHtml(body, 'data-s', 3);
  const inRange = (cx, from, to) => cx >= from && cx <= to;
  const rings = (html, from, to) =>
    (html.match(/<circle\b[^>]*>/g) || []).filter((c) => /\br="8"/.test(c) && /\bstroke="#E24B4A"/.test(c) && inRange(Number(openAttr(c, 'cx')), from, to)).length;
  const labelsFrom = 0;
  const labelsTo = 200;
  const headerFrom = 200;
  const headerTo = 400;
  assert.equal(rings(two, labelsFrom, labelsTo), 12, 'labels side');
  assert.equal(rings(two, headerFrom, headerTo), 12, 'header side');
  assert.equal(rings(tray, 0, 680), 12);
  const pinName = (key) => key.replace(/^pins\./, '');
  const names = (html, from, to, middle, left) =>
    textsIn(html)
      .filter((t) => t.key && t.key.startsWith('pins.') && inRange(t.x, from, to) && (left ? t.x < middle : t.x > middle))
      .sort((a, b) => a.y - b.y)
      .map((t) => pinName(t.key));
  assert.deepEqual(names(two, labelsFrom, labelsTo, 112, true), ['D32', 'D33', 'D25', 'D26', 'D27', 'GND', 'VIN']);
  assert.deepEqual(names(two, labelsFrom, labelsTo, 112, false), ['D22', 'D19', 'D4', 'GND', '3V3']);
  assert.deepEqual(names(two, headerFrom, headerTo, 328, true), ['VIN', 'GND', 'D27', 'D26', 'D25', 'D33', 'D32']);
  assert.deepEqual(names(two, headerFrom, headerTo, 328, false), ['3V3', 'GND', 'D4', 'D19', 'D22']);
  const right = (tag) => {
    const name = /^<(\w+)/.exec(tag)[1];
    if (name === 'circle') return Number(openAttr(tag, 'cx')) + Number(openAttr(tag, 'r'));
    if (name === 'rect') return Number(openAttr(tag, 'x')) + Number(openAttr(tag, 'width'));
    if (name === 'path') return Math.max(...(openAttr(tag, 'd').match(/[\d.]+/g) || []).filter((_, i) => i % 2 === 0).map(Number));
    return Number(openAttr(tag, 'x'));
  };
  const shapes = two.match(/<(?:circle|rect|path|text)\b[^>]*>/g) || [];
  assert.ok(shapes.length > 50);
  for (const tag of shapes) assert.ok(right(tag) <= 400, `step 2 stays in the part a 360 px phone shows without scrolling: ${tag}`);
  const modules = (two.match(/<rect\b[^>]*stroke-dasharray[^>]*>/g) || []).map((r) => `${openAttr(r, 'width')}x${openAttr(r, 'height')}`);
  assert.deepEqual(modules, ['100x26', '100x26'], 'the module outline is the same size in both views');
  const trayModule = (tray.match(/<rect\b[^>]*stroke-dasharray[^>]*>/g) || []).map((r) => `${openAttr(r, 'width')}x${openAttr(r, 'height')}`);
  assert.deepEqual(trayModule, ['100x26'], 'and the same as in the tray view');
  const blackBars = (two.match(/<rect\b[^>]*fill="#111111"[^>]*>/g) || []).map((r) => Number(openAttr(r, 'x')));
  assert.equal(blackBars.length, 2, 'only the header side has the black plastic');
  assert.ok(blackBars.every((x) => x > headerFrom), 'no black header on the labels side');
  const twoKeys = new Set(keysIn(two));
  for (const key of ['steps.devkit.t_labels_up', 'steps.devkit.t_header_up', 'steps.devkit.t_flip', 'steps.devkit.t_marker']) {
    assert.ok(twoKeys.has(key), `${key} in step 2`);
  }
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['steps.devkit.t_header_up'], /гребінкою догори/);
  assert.match(TEXT.en['steps.devkit.t_header_up'], /header up/);
  assert.match(TEXT.uk['steps.devkit.t_flip'], /переверніть/);
  assert.match(TEXT.en['steps.devkit.t_flip'], /turn over/);
  const trayNames = (left) =>
    textsIn(tray)
      .filter((t) => t.key && t.key.startsWith('pins.') && (left ? t.x < 340 : t.x > 340))
      .sort((a, b) => a.y - b.y)
      .map((t) => pinName(t.key));
  assert.deepEqual(trayNames(true), ['VIN', 'GND', 'D27', 'D26', 'D25', 'D33', 'D32']);
  assert.deepEqual(trayNames(false), ['3V3', 'GND', 'D4', 'D19', 'D22']);
  const keys = new Set(keysIn(tray));
  for (const key of ['steps.devkit.t_side_tr', 'steps.devkit.t_row_tr', 'steps.devkit.t_side_amp', 'steps.devkit.t_row_amp', 'steps.devkit.t_usb_back', 'steps.devkit.t_antenna_front']) {
    assert.ok(keys.has(key), `${key} in the tray view`);
  }
});

test('devkit drawing: wire flags come in groups per step (amplifier 5, microphone 5, ring 3, button 2), the side view is step 1, the multimeter is on steps 8 and 9', () => {
  const body = figureBlock(readPage(), 'devkit');
  const flagKeys = (k) => keysIn(groupHtml(body, 'data-s', k)).filter((key) => key.startsWith('steps.devkit.w_'));
  assert.deepEqual(flagKeys(4), ['steps.devkit.w_amp_din', 'steps.devkit.w_amp_bclk', 'steps.devkit.w_amp_lrc', 'steps.devkit.w_amp_gnd', 'steps.devkit.w_amp_vin']);
  assert.deepEqual(flagKeys(5), ['steps.devkit.w_mic_vdd', 'steps.devkit.w_mic_gnd', 'steps.devkit.w_mic_sd', 'steps.devkit.w_mic_ws', 'steps.devkit.w_mic_sck']);
  assert.deepEqual(flagKeys(6), ['steps.devkit.w_ring_g', 'steps.devkit.w_ring_s', 'steps.devkit.w_ring_plus']);
  assert.deepEqual(flagKeys(7), ['steps.devkit.w_btn_d19', 'steps.devkit.w_btn_gnd']);
  const side = keysIn(groupHtml(body, 'data-only', 1));
  for (const key of ['steps.devkit.t_speaker', 'steps.devkit.t_gap', 'steps.devkit.t_tip', 'steps.devkit.t_wire', 'steps.devkit.t_wrong', 'steps.devkit.t_board', 'steps.devkit.t_tray']) {
    assert.ok(side.includes(key), `${key} in the side view`);
  }
  assert.deepEqual(keysIn(groupHtml(body, 'data-only', 8)), ['steps.devkit.silence', 'steps.devkit.beep_ok']);
  assert.equal(loadScripts(['assets/text.js']).TEXT.uk['steps.devkit.silence'], 'тиша');
  assert.equal(loadScripts(['assets/text.js']).TEXT.uk['steps.devkit.beep_ok'], 'короткий пік на початку — теж норма');
  assert.equal(loadScripts(['assets/text.js']).TEXT.en['steps.devkit.beep_ok'], 'a short beep at first is fine too');
  assert.ok(keysIn(groupHtml(body, 'data-only', 9)).includes('steps.devkit.silence'));
  assert.match(body, /<g data-hide="8">/, 'wire flags fade while the multimeter is shown');
});

test('every wire length written in a picture equals the table value, in both languages', () => {
  const { TEXT, WIRES } = bootTable().P;
  const page = readPage();
  for (const lang of ['uk', 'en']) {
    const unit = lang === 'uk' ? 'см' : 'cm';
    for (const [labelKey, name] of Object.entries(LABEL_ROWS)) {
      const row = WIRES.find((w) => w.from === `wires.from.${name}`);
      assert.ok(row, `no wire ${name}`);
      const expected = `${row.approx ? '≈' : ''}${numberText(lang, row.cm)} ${unit}`;
      assert.ok(TEXT[lang][labelKey].includes(expected), `${lang} ${labelKey} "${TEXT[lang][labelKey]}" should contain "${expected}"`);
      assert.match(page, new RegExp(`data-i18n="${labelKey.replace(/\./g, '\\.')}"`), `${labelKey} is not used in the page`);
    }
  }
  const used = new Set(Object.values(LABEL_ROWS));
  for (const w of WIRES) {
    if (w.from === 'wires.from.ring_d') continue;
    assert.ok(used.has(w.from.replace('wires.from.', '')), `${w.from} has no label in a picture`);
  }
});

test('the pin map is a labelled component-side figure with 15 pins per row, 12 highlighted, and a caption that says which side the rows are on in the tray', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  const figure = /<figure\b[^>]*\bdata-pinmap\b[^>]*>([\s\S]*?)<\/figure>/.exec(page);
  assert.ok(figure);
  const body = figure[1];
  assert.match(body, /<div class="stepper-frame">\s*<svg\b/);
  assert.match(body, /<figcaption\b[^>]*\bdata-i18n="solder\.pinmap\.caption"/);
  const board = textsIn(body).filter((t) => t.key && t.key.startsWith('pins.'));
  const left = board.filter((t) => t.x < 340).sort((a, b) => a.y - b.y).map((t) => t.key.replace('pins.', ''));
  const right = board.filter((t) => t.x > 340).sort((a, b) => a.y - b.y).map((t) => t.key.replace('pins.', ''));
  assert.deepEqual(left, ['EN', 'VP', 'VN', 'D34', 'D35', 'D32', 'D33', 'D25', 'D26', 'D27', 'D14', 'D12', 'D13', 'GND', 'VIN']);
  assert.deepEqual(right, ['D23', 'D22', 'TX0', 'RX0', 'D21', 'D19', 'D18', 'D5', 'TX2', 'RX2', 'D4', 'D2', 'D15', 'GND', '3V3']);
  assert.equal((body.match(/<rect\b[^>]*\bfill="#0F6E56"/g) || []).length, 12);
  for (const lang of ['uk', 'en']) {
    for (const key of Object.keys(TEXT[lang]).filter((k) => k.startsWith('pins.'))) assert.equal(TEXT[lang][key], key.replace('pins.', ''));
  }
  assert.match(TEXT.uk['solder.pinmap.caption'], /Вигляд з боку написів \(деталей\), антена вгорі/);
  assert.match(TEXT.uk['solder.pinmap.caption'], /ряд з D22, D4, D19 і 3V3 лежить з боку підсилювача, ряд з D26, D27 і VIN з боку транзистора/);
  assert.match(TEXT.en['solder.pinmap.caption'], /Labels \(component\) side up, antenna at the top/);
  assert.match(TEXT.en['solder.pinmap.caption'], /the row with D22, D4, D19 and 3V3 is on the amplifier side, the row with D26, D27 and VIN is on the transistor side/);
  assert.match(TEXT.uk['solder.pinmap.caption'], /Два штирі GND приймають чотири дроти, на VIN сходяться три/);
  assert.match(TEXT.en['solder.pinmap.caption'], /The two GND pins take four wires, three wires meet on VIN/);
});

test('the wire table styles keep numbers on one line and the estimate quiet', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /\.wire-len\s*\{[^}]*white-space:\s*nowrap/);
  assert.match(css, /tr\[data-approx="true"\]/);
  assert.match(css, /\.pinmap svg\s*\{[^}]*width:\s*100%/);
  assert.match(css, /caption\s*\{[^}]*text-align:\s*left/);
});

test('the solder section opens with one sentence: the parts are soldered on the table with free wire ends, the DevKit wires in assembly stage 4', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const section = /<section\b[^>]*\bid="solder"[^>]*>([\s\S]*?)<\/section>/.exec(readPage())[1];
  assert.match(section, /^\s*<h2\b[^>]*>[^<]*<\/h2>\s*<div class="section-body">\s*<p\b[^>]*\bdata-i18n="solder\.intro"[^>]*>[^<]+<\/p>\s*<h3\b[^>]*\bdata-i18n="solder\.mic\.title">/);
  const uk = TEXT.uk['solder.intro'];
  const en = TEXT.en['solder.intro'];
  for (const word of [/Мікрофон/, /підсилювач із конденсатором/, /джгут транзистора/, /кнопку/, /на столі/, /другі кінці дротів вільними/, /дроти на DevKit паяють уже на етапі 4 збирання/]) assert.match(uk, word);
  for (const word of [/microphone/, /amplifier with its capacitor/, /transistor harness/, /button/, /on the table/, /far ends of their wires left free/, /wires on the DevKit are soldered in assembly stage 4/]) assert.match(en, word);
  for (const text of [uk, en]) assert.equal((text.match(/[.!?](?=\s|$)/g) || []).length, 1, 'one sentence');
});

test('the filaments are wired on the table in the ring stepper after the polarity check, and assembly stage 1 only lays and routes their wires', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const minus = Object.keys(TEXT.uk).filter((k) => /^steps\.ring\.\d+$/.test(k) && /ніжки D/.test(TEXT.uk[k]) && /«−»/.test(TEXT.uk[k]));
  const plus = Object.keys(TEXT.uk).filter((k) => /^steps\.ring\.\d+$/.test(k) && /червоних дротів «\+»/.test(TEXT.uk[k]));
  assert.equal(minus.length, 1);
  assert.equal(plus.length, 1);
  const stepOf = (key) => Number(key.split('.').pop());
  assert.ok(stepOf(minus[0]) > 1 && stepOf(plus[0]) > stepOf(minus[0]), 'the polarity check comes first, then «−», then «+»');
  const [m, p] = [minus[0], plus[0]];
  assert.match(TEXT.uk[m], /два сині дроти[^.]*до контактів «−» обох ниток/);
  assert.match(TEXT.en[m], /two blue wires[^.]*to the “−” contacts of both filaments/);
  assert.match(TEXT.uk[m], /лише до металевого контакту 5 мм[^.]*ніколи до світної частини[^.]*швидко, бо нитка не любить нагрівання/);
  assert.match(TEXT.en[m], /only to the 5 mm metal contact[^.]*never to the lit part[^.]*quickly, because the filament does not like heat/);
  assert.match(TEXT.uk[p], /свій резистор 47 Ом[^.]*швидко й лише до металу[^.]*до позначеного контакту «\+» своєї нитки/);
  assert.match(TEXT.en[p], /47 Ω resistor into each[^.]*quickly and only to the metal[^.]*marked “\+” contact of its filament/);
  assert.match(TEXT.uk[p], /Другі кінці обох червоних дротів разом підуть на штир VIN на платі ESP32 на етапі 4 збирання\./);
  assert.match(TEXT.en[p], /The other ends of both red wires go together to the VIN pin on the ESP32 board in assembly stage 4\./);
  for (const key of Object.keys(TEXT.uk).filter((k) => /^steps\.ring\.(?:\d+|alt)$/.test(k))) {
    assert.doesNotMatch(TEXT.uk[key], /етап\S* 1\b|крок 8|з'єднайте разом/, key);
    assert.doesNotMatch(TEXT.en[key], /stage 1\b|step 8/, key);
  }
  for (let k = 5; k <= 9; k += 1) {
    assert.doesNotMatch(TEXT.uk[`steps.asm_body.${k}`], /[Пп]рипаяйте|паяльник/, `uk asm_body ${k}`);
    assert.doesNotMatch(TEXT.en[`steps.asm_body.${k}`], /\bsolder\b|soldering iron/i, `en asm_body ${k}`);
  }
  const seven = groupHtml(figureBlock(readPage(), 'ring'), 'data-s', 7);
  assert.equal((seven.match(/<rect\b[^>]*fill="#A8ADB5"/g) || []).length, 4, 'step 7 draws the metal contacts at both ends of both filaments');
  assert.match(TEXT.uk['steps.asm_body.5'], /нитку з уже припаяними дротами/);
  assert.match(TEXT.en['steps.asm_body.5'], /filament with its wires already soldered on/);
  assert.match(TEXT.uk['steps.asm_body.7'], /разом із дротом у жолоб каналу/);
  assert.match(TEXT.en['steps.asm_body.7'], /together with its wire in the open groove of the channel/);
  assert.match(TEXT.uk['steps.asm_body.8'], /^Проведіть дроти: у кожному з чотирьох каналів дріт виходить з контакту в жолоб, відкритий у бік центру/);
  assert.match(TEXT.en['steps.asm_body.8'], /^Route the wires: in each of the four channels the wire leaves its contact into the groove that is open towards the center/);
});

test('the INMP441 pads follow the common round module: SCK · WS · L/R and SD · VDD · GND, L/R across from GND, the jumper drawn straight across', () => {
  const body = figureBlock(readPage(), 'mic');
  const first = groupHtml(body, 'data-s', 1);
  const pads = textsIn(first).filter((t) => t.key && t.key.startsWith('steps.common.'));
  const rowOf = (left) => pads.filter((t) => (left ? t.x < 230 : t.x > 230)).sort((a, b) => a.y - b.y).map((t) => t.key.replace('steps.common.', ''));
  assert.deepEqual(rowOf(true), ['sck', 'ws', 'lr']);
  assert.deepEqual(rowOf(false), ['sd', 'vdd', 'gnd']);
  const at = (key) => pads.find((t) => t.key === `steps.common.${key}`);
  assert.equal(at('lr').y, at('gnd').y, 'L/R sits across from GND');
  const centers = [...first.matchAll(/<circle cx="(\d+)" cy="(\d+)" r="9"/g)].map((m) => [Number(m[1]), Number(m[2])]);
  assert.equal(centers.length, 6);
  const near = (t) => centers.find(([x, y]) => Math.abs(y - (t.y - 4)) < 1 && Math.abs(x - t.x) < 14);
  const lr = near(at('lr'));
  const gnd = near(at('gnd'));
  const vdd = near(at('vdd'));
  assert.ok(lr && gnd && vdd);
  const jumper = /<path d="M(\d+) (\d+) C[\d ]+ [\d ]+ (\d+) (\d+)"/.exec(groupHtml(body, 'data-s', 2));
  assert.ok(jumper, 'the jumper is one curve');
  assert.deepEqual([Number(jumper[1]), Number(jumper[2])], lr);
  assert.deepEqual([Number(jumper[3]), Number(jumper[4])], gnd);
  assert.notDeepEqual(gnd, vdd);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['steps.mic.2'], /GND стоїть навпроти L\/R, в іншому ряду/);
  assert.match(TEXT.en['steps.mic.2'], /GND sits opposite L\/R, in the other row/);
});
