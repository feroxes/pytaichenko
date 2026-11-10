import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { loadScripts, makeElement, makeEnv, readPage, readSite, siteDir } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/releases.js', 'assets/config.js', 'assets/i18n.js', 'assets/data.js'];

const REPO_URL = 'https://github.com/owner/pytaichenko';

const ROWS = [
  ['body', 'PETG-black_body.stl', 'petg_black', 1],
  ['top', 'PETG-black_top.stl', 'petg_black', 1],
  ['tray', 'PETG-black_tray.stl', 'petg_black', 1],
  ['ring_arc', 'PETG-white_ring_arc_x2.stl', 'petg_white', 2],
  ['button_cap', 'PETG-black_button_cap.stl', 'petg_black', 1],
  ['top_plug', 'TPU_top_plug_x2.stl', 'tpu', 2],
  ['foot', 'TPU_foot_x4.stl', 'tpu', 4],
  ['tray_gasket', 'TPU_tray_gasket.stl', 'tpu', 1],
  ['speaker_gasket', 'TPU_speaker_gasket.stl', 'tpu', 1],
];

const PLASTICS = ['petg_black', 'petg_white', 'tpu'];
const GROUPS = [
  ['petg_black', 'petg', ['body', 'top', 'tray', 'button_cap']],
  ['petg_white', 'arc', ['ring_arc']],
  ['tpu', 'tpu', ['top_plug', 'foot', 'tray_gasket', 'speaker_gasket']],
];
const CLEAR = /прозор|clear|transparent|translucent/i;
const OWNER_REPO = 'https://github.com/feroxes/pytaichenko';
const V1_RAW = `${OWNER_REPO}/raw/v1.0.0/case/stl`;

function readRepo(relPath) {
  return readFileSync(path.join(path.resolve(siteDir, '..'), relPath), 'utf8');
}

function unescapeHtml(text) {
  return text.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
}

function boot({ languages = ['uk-UA'], stored = null, readyState = 'complete' } = {}) {
  const tbody = makeElement({ 'data-print': '1' });
  tbody.innerHTML = '';
  const env = makeEnv({ children: [tbody], languages, stored, readyState });
  const P = loadScripts(SCRIPTS, env);
  return { tbody, env, P };
}

function groupsOf(html) {
  return [...html.matchAll(/<div class="print-group" data-plastic="(\w+)">([\s\S]*?)<\/table><\/div>/g)].map((m) => ({
    plastic: m[1],
    title: unescapeHtml(/<h3>([^<]*)<\/h3>/.exec(m[2])[1]),
    settings: unescapeHtml(/<p class="print-settings">([^<]*)<\/p>/.exec(m[2])[1]),
    heads: [...m[2].matchAll(/<th\b[^>]*>([^<]*)<\/th>/g)].map((h) => h[1]),
    rows: rowsOf(m[2]),
  }));
}

function rowsOf(html) {
  return [...html.matchAll(/<tr\b([^>]*)>([\s\S]*?)<\/tr>/g)].filter((tr) => /<td\b/.test(tr[2])).map((tr) => ({
    attrs: tr[1],
    html: tr[2],
    cells: [...tr[2].matchAll(/<td\b([^>]*)>([\s\S]*?)<\/td>/g)].map((td) => ({
      attrs: td[1],
      html: td[2],
      text: unescapeHtml(td[2].replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim()),
    })),
  }));
}

function anchorsIn(html) {
  return [...html.matchAll(/<a\b[^>]*>/g)].map((m) => m[0]);
}

function attrOf(tag, name) {
  const m = new RegExp(`\\b${name}="([^"]*)"`).exec(tag);
  return m ? unescapeHtml(m[1]) : null;
}

test('print rows are the expected files and counts, and none is optional', () => {
  const { P } = boot();
  assert.deepEqual(Array.from(P.PRINT, (r) => [r.id, r.file, r.plastic, r.qty]), ROWS);
  assert.deepEqual(Array.from(P.PRINT.filter((r) => r.optional), (r) => r.file), []);
});

test('print files are exactly the names v1/case/build.sh writes, and a _xN suffix is the number of copies', () => {
  const { P } = boot();
  const line = readRepo('v1/case/build.sh').split('\n').find((l) => l.startsWith('PARTS='));
  const built = line.split('(')[1].replace(/\)\s*$/, '').split(/\s+/).map((e) => `${e.split(':')[1]}.stl`);
  assert.deepEqual([...built].sort(), Array.from(P.PRINT, (r) => r.file).sort());
  for (const r of P.PRINT) {
    const m = /_x(\d+)\.stl$/.exec(r.file);
    if (m) assert.equal(r.qty, Number(m[1]), `${r.file}: suffix and count`);
    assert.ok(PLASTICS.includes(r.plastic), `${r.id}: plastic`);
    assert.equal(r.settings, undefined, `${r.id}: the settings belong to the plastic group, not to the row`);
    assert.ok(r.file.startsWith(r.plastic.startsWith('petg') ? 'PETG-' : 'TPU_'), `${r.id}: file prefix follows the plastic`);
    assert.ok(Number.isInteger(r.qty) && r.qty > 0, `${r.id}: qty`);
  }
});

test('every print row, plastic group and settings key is translated in both languages, and the groups follow the plastics', () => {
  const { P } = boot();
  assert.deepEqual(Array.from(P.PRINT_GROUPS, (g) => [g.plastic, g.settings]), GROUPS.map(([plastic, settings]) => [plastic, settings]));
  for (const r of P.PRINT) {
    assert.ok(`print.name.${r.id}` in P.TEXT.uk && `print.name.${r.id}` in P.TEXT.en, `${r.id}: name`);
    assert.ok(P.PRINT_GROUPS.some((g) => g.plastic === r.plastic), `${r.id}: no group for ${r.plastic}`);
  }
  for (const g of P.PRINT_GROUPS) {
    assert.ok(`print.plastic.${g.plastic}` in P.TEXT.uk && `print.plastic.${g.plastic}` in P.TEXT.en, `${g.plastic}: plastic`);
    assert.ok(`print.settings.${g.settings}` in P.TEXT.uk && `print.settings.${g.settings}` in P.TEXT.en, `${g.settings}: settings`);
  }
  const notes = Object.keys(P.TEXT.uk).filter((k) => k.startsWith('print.note.'));
  assert.deepEqual(notes, ['print.note.body']);
  assert.ok('print.note.body' in P.TEXT.en);
  for (const key of ['print.settings.petg_body', 'print.prototype', 'print.caption', 'print.col.plastic', 'print.col.settings', 'print.col.download', 'print.download', 'print.no_link']) {
    assert.equal(P.TEXT.uk[key], undefined, key);
    assert.equal(P.TEXT.en[key], undefined, key);
  }
});

test('no row mentions clear or прозорий: the ring arcs are white PETG only', () => {
  const { tbody, P } = boot();
  for (const r of P.PRINT) for (const value of [r.id, r.file, r.plastic]) assert.doesNotMatch(String(value), CLEAR, r.id);
  for (const lang of ['uk', 'en']) {
    for (const key of Object.keys(P.TEXT[lang]).filter((k) => /^(print|need\.time)/.test(k))) {
      assert.doesNotMatch(P.TEXT[lang][key], CLEAR, `${lang} ${key}`);
    }
    P.setLang(lang);
    assert.doesNotMatch(tbody.innerHTML, CLEAR, `${lang} rendered table`);
  }
  assert.doesNotMatch(readSite('assets/data.js'), CLEAR);
  const arcs = P.PRINT.filter((r) => r.file === 'PETG-white_ring_arc_x2.stl');
  assert.equal(arcs.length, 1);
  assert.equal(arcs[0].plastic, 'petg_white');
  assert.equal(P.TEXT.uk['print.plastic.petg_white'], 'PETG білий');
  assert.equal(P.TEXT.en['print.plastic.petg_white'], 'PETG, white');
});

test('renderPrint draws one card per plastic: its name, its settings, then its parts with the file name and the count', () => {
  const { tbody, P } = boot();
  P.renderPrint(tbody);
  const groups = groupsOf(tbody.innerHTML);
  assert.deepEqual(groups.map((g) => g.plastic), PLASTICS);
  groups.forEach((g, i) => {
    const [plastic, settings, ids] = GROUPS[i];
    assert.equal(g.title, P.TEXT.uk[`print.plastic.${plastic}`]);
    assert.equal(g.settings, P.TEXT.uk[`print.settings.${settings}`]);
    assert.deepEqual(g.heads, [P.TEXT.uk['print.col.part'], P.TEXT.uk['print.col.qty']]);
    assert.equal(g.rows.length, ids.length, plastic);
    g.rows.forEach((row, k) => {
      const r = P.PRINT.find((x) => x.id === ids[k]);
      assert.equal(row.cells.length, 2, r.id);
      assert.ok(row.cells[0].text.startsWith(P.TEXT.uk[`print.name.${r.id}`]), `${r.id}: name`);
      assert.match(row.cells[0].html, new RegExp(`<code class="print-file">${r.file.replace(/\./g, '\\.')}</code>`));
      assert.equal(row.cells[1].text, String(r.qty), `${r.id}: qty`);
    });
  });
  assert.equal(groups[0].title, 'PETG чорний');
  assert.equal(groups[1].title, 'PETG білий');
  assert.equal(groups[1].rows[0].cells[1].text, '2');
  assert.equal(groups[2].rows[1].cells[1].text, '4');
  assert.ok(groups[0].rows[0].cells[0].text.includes(P.TEXT.uk['print.note.body']), 'the body row says where its supports go');
  assert.equal(groups.flatMap((g) => g.rows).filter((row) => row.cells[0].text.includes(P.TEXT.uk['print.note.body'])).length, 1);
});

test('renderPrint draws the English texts and the same ten rows', () => {
  const { tbody, P } = boot({ languages: ['en-US'] });
  P.renderPrint(tbody);
  const groups = groupsOf(tbody.innerHTML);
  assert.deepEqual(groups.map((g) => g.title), ['PETG, black', 'PETG, white', 'TPU']);
  groups.forEach((g, i) => assert.equal(g.settings, P.TEXT.en[`print.settings.${GROUPS[i][1]}`]));
  const rows = rowsOf(tbody.innerHTML);
  assert.equal(rows.length, ROWS.length);
  assert.ok(rows[0].cells[0].text.startsWith(P.TEXT.en['print.name.body']));
  assert.ok(rows[0].cells[0].text.includes(P.TEXT.en['print.note.body']));
});

test('no printed part is marked optional now that the microphone is glued', () => {
  const { tbody, P } = boot();
  const order = GROUPS.flatMap((g) => g[2]);
  for (const lang of ['uk', 'en']) {
    P.setLang(lang);
    const rows = rowsOf(tbody.innerHTML);
    assert.equal(rows.length, order.length);
    rows.forEach((row, i) => {
      const optional = false;
      assert.equal(row.cells[0].text.includes(P.TEXT[lang]['print.optional']), optional, `${lang} ${order[i]}`);
      assert.equal(row.attrs.includes('data-optional="true"'), optional, `${lang} ${order[i]} data-optional`);
    });
  }
  assert.match(P.TEXT.uk['print.optional'], /не обов/);
  assert.match(P.TEXT.en['print.optional'], /optional/i);
});

test('the settings are grouped: the common line over the cards, then PETG 0.16 mm, the white arcs 0.16 mm from 2 walls, TPU 0.1 mm at 100 %', () => {
  const { P } = boot();
  const uk = (key) => P.TEXT.uk[`print.settings.${key}`];
  const en = (key) => P.TEXT.en[`print.settings.${key}`];
  for (const [lang, text] of [['uk', P.TEXT.uk['print.common']], ['en', P.TEXT.en['print.common']]]) {
    assert.match(text, /X1 Carbon/, lang);
    assert.match(text, lang === 'uk' ? /сопло 0,4 мм/ : /0\.4 mm nozzle/, lang);
    assert.match(text, lang === 'uk' ? /перший шар 0,2 мм/ : /0\.2 mm first layer/, lang);
    assert.match(text, /Arachne/, lang);
  }
  assert.match(uk('petg'), /^шар 0,16 мм, стінки 4–5, без опор, крім тіла$/);
  assert.match(en('petg'), /^layer 0\.16 mm, 4–5 walls, no supports except in the body$/);
  assert.match(uk('arc'), /^шар 0,16 мм, від 2 стінок, без опор$/);
  assert.match(en('arc'), /0\.16 mm, 2 or more walls/);
  assert.match(uk('tpu'), /^шар 0,1 мм, заповнення 100 %, без опор$/);
  assert.match(en('tpu'), /0\.1 mm, 100% infill/);
  assert.match(P.TEXT.uk['print.note.body'], /опори лише під трьома бобишками всередині/);
  assert.match(P.TEXT.en['print.note.body'], /supports only under the three bosses inside/);
  for (const key of ['petg', 'arc', 'tpu']) {
    assert.doesNotMatch(uk(key), /перший шар|Arachne|0,12|0,14/, `${key}: the common settings are said once, above the cards`);
    assert.doesNotMatch(en(key), /first layer|Arachne/, key);
  }
});

test('CONFIG points at the owner repository, fileUrl is null while repoUrl is empty and builds the raw url of the given ref once it is set', () => {
  const { P } = boot();
  assert.equal(P.CONFIG.repoUrl, OWNER_REPO);
  assert.equal(P.fileUrl('case/stl/PETG-black_body.stl', 'v1.0.0'), `${V1_RAW}/PETG-black_body.stl`);
  P.CONFIG.repoUrl = '';
  assert.equal(P.fileUrl('case/stl/PETG-black_body.stl', 'v1.0.0'), null);
  assert.equal(P.fileUrl('', 'v1.0.0'), null);
  assert.equal(P.fileUrl(undefined, 'v1.0.0'), null);
  P.CONFIG.repoUrl = REPO_URL;
  assert.equal(P.fileUrl('v1/case/stl/PETG-black_body.stl', 'v1.0.1'), `${REPO_URL}/raw/v1.0.1/v1/case/stl/PETG-black_body.stl`);
  assert.equal(P.fileUrl('/v1/case/stl/x.stl', 'v1.0.1'), `${REPO_URL}/raw/v1.0.1/v1/case/stl/x.stl`);
  P.CONFIG.repoUrl = `${REPO_URL}/`;
  assert.equal(P.fileUrl('v1/case/stl/x.stl', 'main'), `${REPO_URL}/raw/main/v1/case/stl/x.stl`);
  P.CONFIG.repoUrl = `${REPO_URL}///`;
  assert.equal(P.fileUrl('v1/case/stl/x.stl', 'v2.0.0'), `${REPO_URL}/raw/v2.0.0/v1/case/stl/x.stl`);
});

test('fileUrl never returns a url from an unusable repoUrl, path or ref', () => {
  const { P } = boot();
  for (const bad of ['', '   ', '#', 'github.com/o/r', 'http://github.com/o/r', 'javascript:alert(1)', 'https://', 'https://a b', 'https://a"b', null, undefined, 5]) {
    P.CONFIG.repoUrl = bad;
    assert.equal(P.fileUrl('v1/case/stl/x.stl', 'v1.0.0'), null, `repoUrl ${String(bad)}`);
  }
  P.CONFIG.repoUrl = REPO_URL;
  for (const bad of ['../x.stl', 'case/../x.stl', 'case/./x.stl', 'case//x.stl', 'v1/case/stl/a"b.stl', "v1/case/stl/a'b.stl", 'v1/case/stl/a<b.stl', 'v1/case/stl/a\\b.stl', 'v1/case/stl/a\nb.stl', '', '///', null, 5]) {
    assert.equal(P.fileUrl(bad, 'v1.0.0'), null, `path ${String(bad)}`);
  }
  for (const bad of ['', '..', '.', 'a//b', 'v1"0', 'v1<0', 'v1\n0', '///', null, undefined, 5]) {
    assert.equal(P.fileUrl('v1/case/stl/x.stl', bad), null, `ref ${String(bad)}`);
  }
});

test('fileUrl encodes each path segment, so names with spaces become links instead of being refused', () => {
  const { P } = boot();
  P.CONFIG.repoUrl = REPO_URL;
  assert.equal(P.fileUrl('v1/case/stl/PETG black tuned.3mf', 'v1.0.0'), `${REPO_URL}/raw/v1.0.0/v1/case/stl/PETG%20black%20tuned.3mf`);
  assert.equal(P.fileUrl('v1/case/stl/a b.stl', 'v1.0.0'), `${REPO_URL}/raw/v1.0.0/v1/case/stl/a%20b.stl`);
  assert.equal(P.fileUrl('v1/case/stl/a#b?.stl', 'v1.0.0'), `${REPO_URL}/raw/v1.0.0/v1/case/stl/a%23b%3F.stl`);
  assert.equal(P.fileUrl('v1/case/stl/x.stl', 'a#b'), `${REPO_URL}/raw/a%23b/v1/case/stl/x.stl`);
  assert.equal(P.fileUrl('v1/case/stl/PETG-black_body.stl', 'v1.0.0'), `${REPO_URL}/raw/v1.0.0/v1/case/stl/PETG-black_body.stl`, 'plain names stay as they are');
  P.CONFIG.repoUrl = 'http://github.com/o/r';
  assert.equal(P.fileUrl('v1/case/stl/a b.stl', 'v1.0.0'), null, 'the base still has to be https');
});

test('without a usable repoUrl the part names stay plain text, never a dead link', () => {
  const { tbody, P } = boot();
  P.CONFIG.repoUrl = '';
  P.renderPrint(tbody);
  assert.equal(anchorsIn(tbody.innerHTML).length, 0);
  assert.doesNotMatch(tbody.innerHTML, /href=""|href="#"|href='#'|href=''/);
  const order = GROUPS.flatMap((g) => g[2]);
  rowsOf(tbody.innerHTML).forEach((row, i) => assert.ok(row.cells[0].text.startsWith(P.TEXT.uk[`print.name.${order[i]}`])));
  P.setLang('en');
  assert.equal(anchorsIn(tbody.innerHTML).length, 0);
});

test('every part name is the link to its own STL file in the repository, opening in a new tab with noopener', () => {
  const { tbody, P } = boot();
  const order = GROUPS.flatMap((g) => g[2]);
  for (const lang of ['uk', 'en']) {
    P.setLang(lang);
    const rows = rowsOf(tbody.innerHTML);
    assert.equal(rows.length, ROWS.length);
    rows.forEach((row, i) => {
      const r = P.PRINT.find((x) => x.id === order[i]);
      const anchors = anchorsIn(row.cells[0].html);
      assert.equal(anchors.length, 1, r.file);
      assert.equal(attrOf(anchors[0], 'href'), `${V1_RAW}/${r.file}`);
      assert.equal(attrOf(anchors[0], 'href'), P.fileUrl(P.RELEASES['1'].stlDir + r.file, P.RELEASES['1'].tag));
      assert.equal(attrOf(anchors[0], 'target'), '_blank');
      assert.match(attrOf(anchors[0], 'rel'), /\bnoopener\b/);
      const name = P.TEXT[lang][`print.name.${r.id}`];
      assert.match(row.cells[0].html, new RegExp(`<a\\b[^>]*>${name.replace(/[()]/g, '\\$&')}</a>`), `${r.id}: the link text is the part name`);
      const label = attrOf(anchors[0], 'aria-label');
      assert.ok(label.startsWith(name) && label.includes(r.file), `${r.file}: aria-label "${label}"`);
    });
  }
  assert.doesNotMatch(tbody.innerHTML, /href=""|href="#"/);
  assert.doesNotMatch(tbody.innerHTML, /print-link|завантажити STL|download STL/);
});

test('the links follow repoUrl when the table is drawn again', () => {
  const { tbody, P } = boot();
  assert.equal(anchorsIn(tbody.innerHTML).length, ROWS.length);
  P.CONFIG.repoUrl = '';
  P.setLang('en');
  assert.equal(anchorsIn(tbody.innerHTML).length, 0);
  P.CONFIG.repoUrl = REPO_URL;
  P.setLang('uk');
  assert.equal(anchorsIn(tbody.innerHTML).length, ROWS.length);
  assert.ok(tbody.innerHTML.includes(`${REPO_URL}/raw/v1.0.0/case/stl/PETG-black_body.stl`));
});

test('the links point at the newest release of the line, and follow RELEASES when the table is drawn again', () => {
  const { tbody, P } = boot();
  assert.ok(tbody.innerHTML.includes(`${V1_RAW}/PETG-black_body.stl`));
  assert.doesNotMatch(tbody.innerHTML, /\/raw\/main\//);
  P.RELEASES['1'] = { version: '1.0.1', tag: 'v1.0.1', stlDir: 'v1/case/stl/' };
  P.setLang('en');
  const hrefs = anchorsIn(tbody.innerHTML).map((a) => attrOf(a, 'href'));
  assert.equal(hrefs.length, ROWS.length);
  for (const href of hrefs) assert.match(href, /^https:\/\/github\.com\/feroxes\/pytaichenko\/raw\/v1\.0\.1\/v1\/case\/stl\/[\w.-]+\.stl$/);
  delete P.RELEASES['1'];
  P.setLang('uk');
  assert.equal(anchorsIn(tbody.innerHTML).length, 0, 'without a release the names stay plain text');
  assert.equal(rowsOf(tbody.innerHTML).length, ROWS.length);
});

test('switching the language re-renders the print table in place without doubling rows', () => {
  const { tbody, P } = boot();
  assert.equal(rowsOf(tbody.innerHTML).length, ROWS.length);
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.uk['print.name.body']));
  P.setLang('en');
  assert.equal(rowsOf(tbody.innerHTML).length, ROWS.length);
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.en['print.name.body']));
  P.setLang('uk');
  P.renderPrint(tbody);
  P.renderPrint(tbody);
  assert.equal(rowsOf(tbody.innerHTML).length, ROWS.length);
  assert.equal(groupsOf(tbody.innerHTML).length, 3);
});

test('the table is drawn with the saved language while the document is still loading', () => {
  const { tbody, env, P } = boot({ stored: 'en', languages: ['uk-UA'], readyState: 'loading' });
  assert.equal(P.lang, 'en');
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.en['print.name.body']));
  env.document.readyState = 'interactive';
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.ok(rowsOf(tbody.innerHTML)[0].cells[0].text.startsWith(P.TEXT.en['print.name.body']));
});

test('renderPrint escapes what it writes', () => {
  const { tbody, P } = boot();
  P.CONFIG.repoUrl = REPO_URL;
  P.PRINT.splice(0, P.PRINT.length, { id: 'body', file: 'PETG-black_body.stl', plastic: 'petg_black', qty: '<b>1</b>' });
  P.renderPrint(tbody);
  assert.doesNotMatch(tbody.innerHTML, /<b>1<\/b>/);
  assert.match(tbody.innerHTML, /&lt;b&gt;1&lt;\/b&gt;/);
});

test('data.js keeps the no-fetch, no-modules, no-comments rules and points at no local path', () => {
  const code = readSite('assets/data.js');
  assert.doesNotMatch(code, /\bfetch\s*\(|XMLHttpRequest|^\s*(?:import|export)\b/m);
  assert.doesNotMatch(code, /^\s*\/\/|\/\*/m, 'data.js has code comments');
  assert.doesNotMatch(code, /\/Users\/|\/home\/[a-z]|\/private\/tmp|[A-Z]:\\\\Users/);
});

test('the print section has the intro, the common settings, the archive link, the cards box, the estimate and the how-to list', () => {
  const page = readPage();
  const section = /<section id="print"[\s\S]*?<\/section>/.exec(page);
  assert.ok(section, 'print section');
  const html = section[0];
  const order = ['print.intro', 'print.common', 'print.archive', 'data-print', 'print.estimate', 'print.how.title'].map((key) => html.indexOf(key));
  assert.ok(order.every((at) => at > 0), JSON.stringify(order));
  assert.deepEqual([...order].sort((a, b) => a - b), order, 'intro, common settings, archive, cards, estimate, how-to');
  assert.match(html, /<div class="print-groups" data-print="1"><\/div>/);
  assert.doesNotMatch(html, /<table\b|print-prototype|print\.prototype|print\.caption/);
  const { TEXT } = loadScripts(['assets/text.js']);
  const keys = [...html.matchAll(/data-i18n="([^"]+)"/g)].map((m) => m[1]);
  for (const key of keys) assert.ok(key in TEXT.uk && key in TEXT.en, `${key} is missing`);
  for (const key of ['print.how.orient', 'print.how.supports', 'print.how.layer', 'print.how.walls', 'print.how.tpu']) {
    assert.ok(keys.includes(key), `${key} is not on the page`);
  }
  for (const key of ['print.intro', 'print.common', 'print.estimate']) {
    const m = new RegExp(`data-i18n="${key.replace('.', '\\.')}">([^<]*)<`).exec(html);
    assert.equal(unescapeHtml(m[1]), TEXT.uk[key], `${key}: the static text is the Ukrainian one`);
  }
});

test('a note right under the archive link says the ZIP exists only on the published site and where the files are in a clone', () => {
  const html = /<section id="print"[\s\S]*?<\/section>/.exec(readPage())[0];
  const link = html.indexOf('data-i18n="print.archive"');
  const note = html.indexOf('data-i18n="print.archive_note"');
  assert.ok(link > 0 && note > link, 'the note follows the link');
  assert.ok(note < html.indexOf('data-print'), 'and sits above the cards');
  assert.match(html, /<p class="print-archive-note" data-i18n="print\.archive_note">/);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['print.archive_note'], /^Архів збирається під час публікації сайту, тож у клонованому репозиторії його немає: там ці самі файли лежать у папці v1\/case\/stl\.$/);
  assert.match(TEXT.en['print.archive_note'], /^The archive is built when the site is published, so a cloned repository does not have it: there the same files are in the v1\/case\/stl folder\.$/);
  for (const lang of ['uk', 'en']) assert.doesNotMatch(TEXT[lang]['print.archive_note'], /[Яя]кщо ви відкрили|If you opened/, `${lang}: the note reads right on the published site too`);
  assert.equal(unescapeHtml(new RegExp('data-i18n="print\\.archive_note">([^<]*)<').exec(html)[1]), TEXT.uk['print.archive_note']);
});

test('each version has one archive link with every STL above its cards: a relative files/v<N>/pytaichenko-stl.zip that the release builds and the site copies', () => {
  const page = readPage();
  const html = /<section id="print"[\s\S]*?<\/section>/.exec(page)[0];
  const blocks = [...html.matchAll(/<div class="section-body" data-for-v="(\d)">/g)].map((m) => [m.index, m[1]]);
  const blockOf = (at) => blocks.filter(([i]) => i < at).pop()[1];
  const links = [...html.matchAll(/<a\b([^>]*)>([^<]*)<\/a>/g)];
  assert.deepEqual(links.map((m) => blockOf(m.index)), ['1', '2']);
  const { TEXT } = loadScripts(['assets/text.js']);
  links.forEach((link, i) => {
    const v = String(i + 1);
    assert.match(link[1], new RegExp(`\\bhref="files/v${v}/pytaichenko-stl\\.zip"`));
    assert.match(link[1], /\bdownload\b/);
    const key = v === '1' ? 'print.archive' : `v${v}.print.archive`;
    assert.match(link[1], new RegExp(`\\bdata-i18n="${key.replace(/\./g, '\\.')}"`));
    assert.equal(link[2], TEXT.uk[key]);
  });
  assert.ok(links[0].index < html.indexOf('data-print="1"'), 'the version 1 archive sits above its cards');
  assert.match(TEXT.uk['print.archive'], /архів/);
  assert.match(TEXT.en['print.archive'], /archive/);
  const yml = readRepo('.github/workflows/release.yml');
  const zip = yml.indexOf('zip -j "dist/pytaichenko-$VERSION-stl.zip" "v$LINE/case/stl/"*.stl LICENSE-hardware');
  assert.ok(zip > 0, 'the release packs every STL of its line with the license notice');
  assert.ok(zip < yml.indexOf('gh release create'), 'the archive is packed before the release is created');
  const script = readRepo('.github/scripts/site-releases.sh');
  const copy = script.indexOf('cp "$work/v$line/pytaichenko-$version-stl.zip" "$root/site/files/v$line/pytaichenko-stl.zip"');
  assert.ok(copy > script.indexOf('gh release download'), 'the site copies the archive of each line\'s release');
  assert.match(script, /mkdir -p "\$root\/site\/firmware\/v\$line" "\$root\/site\/files\/v\$line"\n/);
  assert.ok(yml.indexOf('bash .github/scripts/site-releases.sh') < yml.indexOf('upload-pages-artifact'), 'the archives are in place before the upload');
  assert.match(readRepo('.gitignore'), /^site\/files\/$/m);
});

test('the page text says nothing about revisions or a prototype, in either language, in text, alt or labels', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const words = /ревізі|revision|прототип|prototype/i;
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) assert.doesNotMatch(value, words, `${lang} ${key}`);
  }
  assert.doesNotMatch(readPage(), words);
  assert.doesNotMatch(readSite('assets/style.css'), words);
});

test('the print time slot is filled with a stated estimate and says it is print time, not a pending placeholder', () => {
  const page = readPage();
  const dd = /<dd id="need-time"[^>]*>([^<]*)<\/dd>/.exec(page);
  assert.ok(dd, 'need-time slot');
  assert.match(dd[0], /data-i18n="need\.time_value"/);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(unescapeHtml(dd[1]), TEXT.uk['need.time_value']);
  assert.match(TEXT.uk['need.time_value'], /7–8 год/);
  assert.match(TEXT.en['need.time_value'], /7–8 hours/);
  assert.match(TEXT.uk['need.time_label'], /друку/);
  assert.match(TEXT.en['need.time_label'], /print/i);
  assert.doesNotMatch(TEXT.uk['need.time_value'], /уточн/i);
  assert.doesNotMatch(TEXT.en['need.time_value'], /confirm/i);
  assert.ok(!('need.time_pending' in TEXT.uk) && !('need.time_pending' in TEXT.en), 'the unused pending text is gone');
  for (const lang of ['uk', 'en']) {
    const text = TEXT[lang]['print.estimate'];
    assert.doesNotMatch(text, /ревізі|revision/i, lang);
    assert.match(text, /Bambu Studio/, lang);
    assert.match(text, /PETG/, lang);
    assert.match(text, /TPU/, lang);
    assert.match(text, /X1 Carbon/, lang);
  }
});

test('the print styles draw each plastic as a card that fits a phone: no fixed table width, counts on one line, file names in a monospace face', () => {
  const css = readSite('assets/style.css');
  assert.doesNotMatch(css, /\.print-table\s*\{[^}]*min-width/, 'the cards must not scroll sideways on a phone');
  assert.match(css, /\.print-group\s*\{[^}]*border:[^}]*border-radius:\s*var\(--radius-lg\)[^}]*background:\s*var\(--surface-1\)/);
  assert.match(css, /\.print-table \.print-qty\s*\{[^}]*white-space:\s*nowrap/);
  assert.match(css, /\.print-settings\s*\{[^}]*color:\s*var\(--text-secondary\)/);
  assert.match(css, /\.print-file\s*\{[^}]*font-family:[^;}]*monospace/);
  assert.match(css, /\.print-file\s*\{[^}]*overflow-wrap:\s*anywhere/);
  assert.match(css, /\.print-optional\s*\{[^}]*color:\s*var\(--text-secondary\)/);
  assert.doesNotMatch(css, /print-link|print-plastic|print-prototype/);
});

test('the print wording stays calm: no sales phrases, no promises, no forbidden names', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const keys = Object.keys(TEXT.uk).filter((k) => /^print\./.test(k) || /^need\.time/.test(k));
  assert.ok(keys.length > 25, `${keys.length} print keys`);
  const text = keys.map((k) => `${TEXT.uk[k]} ${TEXT.en[k]}`).join('\n');
  assert.doesNotMatch(text, /Home Assistant|\bHA\b|Assist|українською|Ukrainian|купуйте|buy now|знижк|discount|best price|найкращ|обіцяє|promise|гарантія|warranty/i);
  assert.doesNotMatch(text, /\/Users\/|\/home\/[a-z]|\/private\/tmp|[A-Z]:\\\\Users/);
});
