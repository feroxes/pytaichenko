(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});
  var bodies = [];
  var partBodies = [];
  var toolLists = [];
  var costSlots = [];
  var footnotes = [];
  var printBodies = [];
  var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  var ARDUINO = 'arduino.ua';

  P.PARTS_DATE = '2026-10-31';

  P.PARTS = [
    { id: 'devkit', qty: 1, uah: 295, url: 'https://arduino.ua/prod3990-wi-fi-modyl-devkit-v1-s-esp-32', shop: 'arduino.ua' },
    { id: 'mic', qty: 1, uah: 140, url: 'https://menstudio.kyiv.ua/ua/p2712862774-vsenapravlennyj-mems-mikrofonnyj.html', shop: 'menstudio' },
    { id: 'amp', qty: 1, uah: 338, url: 'https://arduino.ua/prod3178-zvykovoi-monofonicheskii-i2s-modyl-s-3vt-ysilitelem-klassa-d-na-max98357a-ot-adafruit', shop: 'arduino.ua' },
    { id: 'speaker', qty: 1, uah: 350, url: 'https://menstudio.kyiv.ua/ua/p3163263864-dinamik-shirokopolosnyj-dyujma.html', shop: 'menstudio' },
    { id: 'filament', qty: 2, uah: null, usd: 4.89, pack: 5, url: 'https://www.aliexpress.com/item/1005007884281989.html', shop: 'aliexpress' },
    { id: 'mosfet', qty: 1, uah: 22, url: 'https://arduino.ua/prod3107-silovoi-polevoi-tranzistor-irlz44n', shop: 'arduino.ua' },
    { id: 'res47', qty: 3, uah: 2, pack: 5, url: 'https://arduino.ua/prod1972-rezistor-47-om-5-shtyk', shop: 'arduino.ua' },
    { id: 'res62k', qty: 1, uah: 20, pack: 4, url: 'https://www.rcscomponents.kiev.ua/product/6-2-kohm-5-0-5w-vyv-cr050sjtb-6k2r-hitano_182986.html', shop: 'rcs' },
    { id: 'cap', qty: 1, uah: 6, url: 'https://arduino.ua/prod6492-kondensator-elektrolitichnii-hanqxin-1000mkf-16v-lowesr', shop: 'arduino.ua' },
    { id: 'button', qty: 1, uah: 4, url: 'https://arduino.ua/prod7626-knopka-dip-12x12x7mm', shop: 'arduino.ua' },
    { id: 'inserts', qty: 1, kit: true, uah: 189, url: 'https://arduino.ua/prod6834-nabir-zakladnih-gaiok-m2m2-5m3m4m5-220sht-latyn', shop: 'arduino.ua', costAs: 'small' },
    { id: 'screws', qty: 1, kit: true, uah: 149, url: 'https://arduino.ua/prod8440-nabir-preciziinih-gvintiv-m2-ta-m2-5-500-sht-', shop: 'arduino.ua', costAs: 'small' },
    { id: 'washers', qty: 4, uah: null, url: null, shop: null, generic: true, costAs: 'small' },
    { id: 'wires', qty: 'set', uah: null, url: null, shop: null, generic: true, costAs: 'small' },
    { id: 'charger', qty: 'set', uah: null, url: null, shop: null, generic: true },
    { id: 'petg_black', qty: 1, uah: null, url: null, shop: null, generic: true, costAs: 'plastic' },
    { id: 'petg_white', qty: 1, uah: null, url: null, shop: null, generic: true, costAs: 'plastic' },
    { id: 'tpu', qty: 1, uah: null, url: null, shop: null, generic: true, costAs: 'plastic' }
  ];

  P.TOOLS = [
    { id: 'iron' },
    { id: 'solder' },
    { id: 'insert_tip' },
    { id: 'multimeter' },
    { id: 'cutters' },
    { id: 'glue_gun' },
    { id: 'feet_glue' },
    { id: 'screwdriver' },
    { id: 'flat_screwdriver' },
    { id: 'battery' },
    { id: 'marker' },
    { id: 'tape' }
  ];

  P.PRINT_DIR = 'case/stl/';

  P.PRINT_GROUPS = [
    { plastic: 'petg_black', settings: 'petg' },
    { plastic: 'petg_white', settings: 'arc' },
    { plastic: 'tpu', settings: 'tpu' }
  ];

  P.PRINT = [
    { id: 'body', file: 'PETG-black_body.stl', plastic: 'petg_black', qty: 1 },
    { id: 'top', file: 'PETG-black_top.stl', plastic: 'petg_black', qty: 1 },
    { id: 'tray', file: 'PETG-black_tray.stl', plastic: 'petg_black', qty: 1 },
    { id: 'ring_arc', file: 'PETG-white_ring_arc_x2.stl', plastic: 'petg_white', qty: 2 },
    { id: 'button_cap', file: 'PETG-black_button_cap.stl', plastic: 'petg_black', qty: 1 },
    { id: 'top_plug', file: 'TPU_top_plug_x2.stl', plastic: 'tpu', qty: 2 },
    { id: 'foot', file: 'TPU_foot_x4.stl', plastic: 'tpu', qty: 4 },
    { id: 'tray_gasket', file: 'TPU_tray_gasket.stl', plastic: 'tpu', qty: 1 },
    { id: 'speaker_gasket', file: 'TPU_speaker_gasket.stl', plastic: 'tpu', qty: 1 }
  ];

  P.WIRES = [
    { from: 'wires.from.mic_vdd', to: '3V3', cm: 11, approx: false, each: false },
    { from: 'wires.from.mic_gnd', to: 'wires.to.gnd_3v3', cm: 11.5, approx: false, each: false },
    { from: 'wires.from.mic_sd', to: 'D33', cm: 6.5, approx: false, each: false },
    { from: 'wires.from.mic_ws', to: 'D25', cm: 8, approx: false, each: false },
    { from: 'wires.from.mic_sck', to: 'D32', cm: 6, approx: false, each: false },
    { from: 'wires.from.amp_vin', to: 'VIN', cm: 8, approx: false, each: false },
    { from: 'wires.from.amp_gnd', to: 'GND', cm: 7, approx: false, each: false },
    { from: 'wires.from.amp_din', to: 'D22', cm: 3.5, approx: false, each: false },
    { from: 'wires.from.amp_bclk', to: 'D26', cm: 7, approx: false, each: false },
    { from: 'wires.from.amp_lrc', to: 'D27', cm: 7, approx: false, each: false },
    { from: 'wires.from.ring_g', to: 'D4', cm: 7, approx: false, each: false },
    { from: 'wires.from.ring_d', to: 'wires.to.fil_minus', cm: 8.5, approx: false, each: true },
    { from: 'wires.from.ring_s', to: 'wires.to.gnd_any', cm: 8, approx: false, each: false },
    { from: 'wires.from.ring_plus', to: 'VIN', cm: 9, approx: true, each: false },
    { from: 'wires.from.btn_1', to: 'D19', cm: 8, approx: false, each: false },
    { from: 'wires.from.btn_2', to: 'GND', cm: 8, approx: false, each: false }
  ];

  function has(dict, key) {
    return Object.prototype.hasOwnProperty.call(dict, key);
  }

  function escapeHtml(value) {
    return String(value).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function word(dict, key) {
    return has(dict, key) ? dict[key] : key;
  }

  function lengthText(dict, row) {
    var number = String(row.cm).replace('.', word(dict, 'wires.decimal'));
    var key = row.approx ? 'wires.len_approx' : row.each ? 'wires.len_each' : 'wires.len';
    return word(dict, key).replace('{n}', number);
  }

  function rowHtml(dict, row) {
    var attrs = row.approx ? ' data-approx="true"' : '';
    return (
      '<tr' + attrs + '>' +
      '<td>' + escapeHtml(word(dict, row.from)) + '</td>' +
      '<td>' + escapeHtml(word(dict, row.to)) + '</td>' +
      '<td class="wire-len">' + escapeHtml(lengthText(dict, row)) + '</td>' +
      '</tr>'
    );
  }

  function renderWires(tbody) {
    var dict = P.TEXT && P.TEXT[P.lang];
    if (!dict) return;
    if (bodies.indexOf(tbody) < 0) bodies.push(tbody);
    var html = '';
    for (var i = 0; i < P.WIRES.length; i++) html += rowHtml(dict, P.WIRES[i]);
    tbody.innerHTML = html;
  }

  function initWires(root) {
    var found = root.querySelectorAll('[data-wires]');
    for (var i = 0; i < found.length; i++) renderWires(found[i]);
  }

  function onLang() {
    for (var i = 0; i < bodies.length; i++) renderWires(bodies[i]);
  }

  function fill(template, values) {
    return String(template).replace(/\{(\w+)\}/g, function (all, name) {
      return Object.prototype.hasOwnProperty.call(values, name) ? String(values[name]) : all;
    });
  }

  function groupDigits(n, lang) {
    var sep = lang === 'uk' ? '\u00a0' : ',';
    return String(Math.round(n)).replace(/\B(?=(\d{3})+(?!\d))/g, sep);
  }

  function formatUah(n, lang) {
    return groupDigits(n, lang) + ' \u20b4';
  }

  function formatUsd(uah, lang) {
    var usd = uah / P.CONFIG.usdRate;
    if (usd < 1) return usd.toFixed(2);
    if (usd < 10) return usd.toFixed(1);
    return groupDigits(usd, lang);
  }

  function formatDate(iso, lang) {
    var parts = String(iso).split('-');
    if (lang === 'uk') return parts[2] + '.' + parts[1] + '.' + parts[0];
    return Number(parts[2]) + ' ' + MONTHS[Number(parts[1]) - 1] + ' ' + parts[0];
  }

  function units(part) {
    return typeof part.qty === 'number' ? Math.ceil(part.qty / (part.pack || 1)) : 1;
  }

  function priceUah(part) {
    if (typeof part.uah === 'number') return part.uah;
    if (typeof part.usd === 'number') return Math.round(part.usd * P.CONFIG.usdRate);
    return null;
  }

  function partsTotal() {
    var uah = 0;
    var missing = 0;
    for (var i = 0; i < P.PARTS.length; i++) {
      var part = P.PARTS[i];
      var price = priceUah(part);
      if (price !== null) uah += price * units(part);
      else if (!part.generic) missing += 1;
    }
    return { uah: uah, usd: Math.round(uah / P.CONFIG.usdRate), missing: missing };
  }

  function costLeft() {
    var named = [];
    var groups = [];
    for (var i = 0; i < P.PARTS.length; i++) {
      var part = P.PARTS[i];
      if (priceUah(part) !== null) continue;
      var list = part.costAs ? groups : named;
      var key = part.costAs || part.id;
      if (list.indexOf(key) < 0) list.push(key);
    }
    return named.concat(groups);
  }

  function joinNames(dict, names) {
    if (names.length < 2) return names.join('');
    var last = names[names.length - 1];
    var before = names[names.length - 2];
    var and = word(dict, /[аеєиіїоуюя]$/i.test(before) ? 'cost.and_vowel' : 'cost.and');
    return names.slice(0, -1).join(', ') + and + last;
  }

  function usableUrl(url) {
    return typeof url === 'string' && /^https:\/\/[^\s"'<>]+$/.test(url);
  }

  function fileUrl(path) {
    var base = P.CONFIG && P.CONFIG.repoUrl;
    if (typeof base !== 'string' || typeof path !== 'string') return null;
    base = base.replace(/\/+$/, '');
    if (!usableUrl(base)) return null;
    var segments = path.replace(/^\/+/, '').split('/');
    for (var i = 0; i < segments.length; i++) {
      var segment = segments[i];
      if (!segment || segment === '.' || segment === '..' || /["'<>\\`\u0000-\u001f]/.test(segment)) return null;
      segments[i] = encodeURIComponent(segment);
    }
    return base + '/raw/main/' + segments.join('/');
  }

  function twoDecimals(value, lang) {
    var text = Number(value).toFixed(2);
    return lang === 'uk' ? text.replace('.', ',') : text;
  }

  function priceHtml(dict, lang, part) {
    var uah = priceUah(part);
    if (uah === null) {
      return (
        '<span class="part-nolink"><span aria-hidden="true">\u2014</span>' +
        '<span class="visually-hidden">' + escapeHtml(word(dict, 'parts.price_missing')) + '</span></span>'
      );
    }
    var text;
    if (typeof part.uah !== 'number') {
      var usd = '$' + twoDecimals(part.usd, lang);
      text = lang === 'uk' ? '\u2248 ' + formatUah(uah, lang) + ' (' + usd + ')' : usd;
    } else {
      text = formatUah(uah, lang);
      if (lang !== 'uk') text += ' (\u2248 $' + formatUsd(uah, lang) + ')';
    }
    if (part.pack) text += ' ' + fill(word(dict, 'parts.per_pack'), { n: part.pack });
    return escapeHtml(text);
  }

  function qtyText(dict, part) {
    if (part.qty === 'set') return word(dict, 'parts.qty_set');
    if (part.kit) return fill(word(dict, 'parts.qty_kit'), { n: part.qty });
    return String(part.qty);
  }

  function linkHtml(dict, part) {
    if (usableUrl(part.url) && part.shop) {
      var shop = word(dict, 'parts.shop.' + (part.shop === ARDUINO ? 'arduino' : part.shop));
      var label = word(dict, 'parts.name.' + part.id) + ' \u2014 ' + shop;
      return (
        '<a href="' + escapeHtml(part.url) + '" target="_blank" rel="noopener" aria-label="' + escapeHtml(label) + '">' +
        escapeHtml(shop) + '</a>'
      );
    }
    var key = part.generic ? 'parts.generic' : 'parts.no_link';
    return '<span class="part-nolink">' + escapeHtml(word(dict, key)) + '</span>';
  }

  function partRowHtml(dict, lang, part) {
    var noteKey = 'parts.note.' + part.id;
    var note = has(dict, noteKey) ? '<span class="part-note">' + escapeHtml(dict[noteKey]) + '</span>' : '';
    return (
      '<tr>' +
      '<td class="part-name">' + escapeHtml(word(dict, 'parts.name.' + part.id)) + note + '</td>' +
      '<td class="part-qty" data-label="' + escapeHtml(word(dict, 'parts.col.qty')) + '">' + escapeHtml(qtyText(dict, part)) + '</td>' +
      '<td class="part-price" data-label="' + escapeHtml(word(dict, 'parts.col.price')) + '">' + priceHtml(dict, lang, part) + '</td>' +
      '<td class="part-link" data-label="' + escapeHtml(word(dict, 'parts.col.link')) + '">' + linkHtml(dict, part) + '</td>' +
      '</tr>'
    );
  }

  function printNameHtml(dict, row) {
    var name = word(dict, 'print.name.' + row.id);
    var url = fileUrl(P.PRINT_DIR + row.file);
    if (!url) return escapeHtml(name);
    var label = fill(word(dict, 'print.download_label'), { name: name, file: row.file });
    return (
      '<a href="' + escapeHtml(url) + '" target="_blank" rel="noopener" aria-label="' + escapeHtml(label) + '">' +
      escapeHtml(name) + '</a>'
    );
  }

  function printRowHtml(dict, row) {
    var optional = row.optional ? ' data-optional="true"' : '';
    var noteKey = 'print.note.' + row.id;
    var note = has(dict, noteKey) ? '<span class="part-note">' + escapeHtml(dict[noteKey]) + '</span>' : '';
    if (row.optional) note += '<span class="part-note print-optional">' + escapeHtml(word(dict, 'print.optional')) + '</span>';
    return (
      '<tr' + optional + '>' +
      '<td class="print-name">' + printNameHtml(dict, row) +
      '<span class="part-note"><code class="print-file">' + escapeHtml(row.file) + '</code></span>' + note + '</td>' +
      '<td class="print-qty">' + escapeHtml(row.qty) + '</td>' +
      '</tr>'
    );
  }

  function printGroupHtml(dict, group) {
    var rows = '';
    for (var i = 0; i < P.PRINT.length; i++) {
      if (P.PRINT[i].plastic === group.plastic) rows += printRowHtml(dict, P.PRINT[i]);
    }
    if (!rows) return '';
    return (
      '<div class="print-group" data-plastic="' + escapeHtml(group.plastic) + '">' +
      '<h3>' + escapeHtml(word(dict, 'print.plastic.' + group.plastic)) + '</h3>' +
      '<p class="print-settings">' + escapeHtml(word(dict, 'print.settings.' + group.settings)) + '</p>' +
      '<table class="print-table"><thead><tr>' +
      '<th scope="col">' + escapeHtml(word(dict, 'print.col.part')) + '</th>' +
      '<th scope="col" class="print-qty">' + escapeHtml(word(dict, 'print.col.qty')) + '</th>' +
      '</tr></thead><tbody>' + rows + '</tbody></table>' +
      '</div>'
    );
  }

  function register(list, el) {
    if (list.indexOf(el) < 0) list.push(el);
  }

  function renderParts(tbody, lang) {
    var use = lang || P.lang;
    var dict = P.TEXT && P.TEXT[use];
    if (!dict) return;
    register(partBodies, tbody);
    var html = '';
    for (var i = 0; i < P.PARTS.length; i++) html += partRowHtml(dict, use, P.PARTS[i]);
    tbody.innerHTML = html;
  }

  function renderPrint(box, lang) {
    var use = lang || P.lang;
    var dict = P.TEXT && P.TEXT[use];
    if (!dict) return;
    register(printBodies, box);
    var html = '';
    for (var i = 0; i < P.PRINT_GROUPS.length; i++) html += printGroupHtml(dict, P.PRINT_GROUPS[i]);
    box.innerHTML = html;
  }

  function renderTools(list, lang) {
    var use = lang || P.lang;
    var dict = P.TEXT && P.TEXT[use];
    if (!dict) return;
    register(toolLists, list);
    var html = '';
    for (var i = 0; i < P.TOOLS.length; i++) html += '<li>' + escapeHtml(word(dict, 'tools.item.' + P.TOOLS[i].id)) + '</li>';
    list.innerHTML = html;
  }

  function renderCost(el, lang) {
    var use = lang || P.lang;
    var dict = P.TEXT && P.TEXT[use];
    if (!dict) return;
    register(costSlots, el);
    var total = partsTotal();
    var left = costLeft();
    var names = [];
    for (var i = 0; i < left.length; i++) {
      var key = 'cost.left.' + left[i];
      names.push(has(dict, key) ? dict[key] : word(dict, 'parts.name.' + left[i]));
    }
    var values = { uah: groupDigits(total.uah, use), usd: groupDigits(total.usd, use), left: joinNames(dict, names) };
    el.textContent = fill(word(dict, left.length > 0 ? 'cost.value' : 'cost.exact'), values);
  }

  function renderFootnote(el, lang) {
    var use = lang || P.lang;
    var dict = P.TEXT && P.TEXT[use];
    if (!dict) return;
    register(footnotes, el);
    el.textContent = fill(word(dict, 'parts.footnote'), {
      date: formatDate(P.PARTS_DATE, use),
      rate: twoDecimals(P.CONFIG.usdRate, use),
      rate_date: formatDate(P.CONFIG.ratesDate, use)
    });
  }

  function initParts(root) {
    var found = root.querySelectorAll('[data-parts]');
    for (var i = 0; i < found.length; i++) renderParts(found[i]);
    found = root.querySelectorAll('[data-tools]');
    for (var j = 0; j < found.length; j++) renderTools(found[j]);
    found = root.querySelectorAll('[data-cost]');
    for (var k = 0; k < found.length; k++) renderCost(found[k]);
    found = root.querySelectorAll('[data-parts-footnote]');
    for (var m = 0; m < found.length; m++) renderFootnote(found[m]);
  }

  function initPrint(root) {
    var found = root.querySelectorAll('[data-print]');
    for (var i = 0; i < found.length; i++) renderPrint(found[i]);
  }

  function onPrintLang() {
    for (var i = 0; i < printBodies.length; i++) renderPrint(printBodies[i]);
  }

  function onPartsLang() {
    var i;
    for (i = 0; i < partBodies.length; i++) renderParts(partBodies[i]);
    for (i = 0; i < toolLists.length; i++) renderTools(toolLists[i]);
    for (i = 0; i < costSlots.length; i++) renderCost(costSlots[i]);
    for (i = 0; i < footnotes.length; i++) renderFootnote(footnotes[i]);
  }

  P.renderWires = renderWires;
  P.initWires = initWires;
  P.partsTotal = partsTotal;
  P.costLeft = costLeft;
  P.renderParts = renderParts;
  P.renderTools = renderTools;
  P.renderCost = renderCost;
  P.renderFootnote = renderFootnote;
  P.initParts = initParts;
  P.fileUrl = fileUrl;
  P.renderPrint = renderPrint;
  P.initPrint = initPrint;

  window.document.addEventListener('pytaichenko:lang', onLang);
  window.document.addEventListener('pytaichenko:lang', onPartsLang);
  window.document.addEventListener('pytaichenko:lang', onPrintLang);
  initWires(window.document);
  initParts(window.document);
  initPrint(window.document);
})(window);
