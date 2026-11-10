import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import { loadScripts, makeElement, makeEnv, readPage, readSite, siteDir } from './helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js', 'assets/stepper.js'];
const STAGES = [
  { id: 'asm_body', short: 'body', from: 1, n: 9, uk: 'Тіло', en: 'Body' },
  { id: 'asm_top', short: 'top', from: 10, n: 10, uk: 'Верх', en: 'Top' },
  { id: 'asm_join', short: 'join', from: 20, n: 3, uk: 'Верх на тіло', en: 'Top onto the body' },
  { id: 'asm_tray', short: 'tray', from: 23, n: 11, uk: 'Лоток', en: 'Tray' },
];
const N = 33;
const FORBIDDEN = /home assistant|\bHA\b|assist\b|говорить українською|speaks ukrainian/i;
const SERVER = /сервер|server/i;
const SWITCH_UP = 12;
const SWITCH_GLUE = 13;
const BOARD_WIRES = 30;
const EXTERNAL_WIRES = 31;
const CHECK = 32;
const TRAY_IN = 33;
const FEET = 23;

function stageOf(g) {
  const stage = STAGES.find((st) => g >= st.from && g < st.from + st.n);
  assert.ok(stage, `no stage for step ${g}`);
  return stage;
}

function cap(TEXT, lang, g) {
  const st = stageOf(g);
  return TEXT[lang][`steps.${st.id}.${g - st.from + 1}`];
}

function altOf(TEXT, lang, g) {
  const st = stageOf(g);
  return TEXT[lang][`steps.${st.id}.alt_${g - st.from + 1}`];
}

function repoPath(relPath) {
  return path.join(path.resolve(siteDir, '..'), relPath);
}

function pad(k) {
  return String(k).padStart(2, '0');
}

function renderPath(k) {
  return path.join(siteDir, 'assets', 'renders', `assembly-${pad(k)}.png`);
}

function section(page) {
  const m = /<section\b[^>]*\bid="assembly"[^>]*>([\s\S]*?)<\/section>/.exec(page);
  assert.ok(m, 'the assembly section is missing');
  const v1 = m[1].indexOf('<div class="section-body" data-for-v="1">');
  const v2 = m[1].indexOf('<div class="section-body" data-for-v="2">');
  assert.ok(v1 >= 0 && v2 > v1, 'the assembly section has a version 1 block, then a version 2 block');
  return m[1].slice(0, v2);
}

function stageBlock(page, id) {
  const m = new RegExp(`<figure\\b([^>]*\\bdata-stepper="${id}"[^>]*)>([\\s\\S]*?)</figure>`).exec(page);
  assert.ok(m, `the stepper ${id} is missing from index.html`);
  return { open: m[1], body: m[2] };
}

function framesOf(body) {
  return [...body.matchAll(/<img\b([^>]*)>/g)].map((m) => m[1]);
}

function attrOf(attrs, name) {
  const m = new RegExp(`(?:^|\\s)${name}="([^"]*)"`).exec(attrs);
  return m ? m[1] : null;
}

function unescapeHtml(text) {
  return text.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
}

function sentences(text) {
  return (text.match(/[.!?](?=\s|$)/g) || []).length;
}

test(`${N} render files exist, are PNG, 1000x750 and between 20 kB and 250 kB, and nothing else sits in renders`, () => {
  for (let k = 1; k <= N; k += 1) {
    const file = renderPath(k);
    assert.ok(existsSync(file), `assembly-${pad(k)}.png is missing`);
    const bytes = readFileSync(file);
    assert.deepEqual([...bytes.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], `assembly-${pad(k)}.png is not a PNG`);
    assert.equal(bytes.readUInt32BE(16), 1000, `assembly-${pad(k)}.png width`);
    assert.equal(bytes.readUInt32BE(20), 750, `assembly-${pad(k)}.png height`);
    assert.ok(statSync(file).size > 20 * 1024, `assembly-${pad(k)}.png is smaller than 20 kB`);
    assert.ok(statSync(file).size < 250 * 1024, `assembly-${pad(k)}.png is 250 kB or more`);
  }
  const files = readdirSync(path.join(siteDir, 'assets', 'renders')).filter((f) => f !== 'v2').sort();
  assert.deepEqual(files, [...Array.from({ length: N }, (_, i) => `assembly-${pad(i + 1)}.png`)], 'no stale render is left behind');
});

function v2Steps() {
  const readme = readFileSync(repoPath('v2/case/README.md'), 'utf8');
  const m = /^## Assembly steps\n([\s\S]*?)(?=^## |(?![\s\S]))/m.exec(readme);
  assert.ok(m, 'v2/case/README.md has an Assembly steps section');
  const count = /(\d+) steps in three stages/.exec(m[1]);
  assert.ok(count, 'the section states the number of steps');
  const rows = [...m[1].matchAll(/^\| (\d+) \| ([^|]+?) \| ([^|]+?) \|$/gm)].map((r) => Number(r[1]));
  assert.deepEqual(rows, Array.from({ length: rows.length }, (_, i) => i + 1));
  assert.equal(rows.length, Number(count[1]));
  return rows.length;
}

function pngSize(file) {
  const bytes = readFileSync(file);
  assert.deepEqual([...bytes.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], `${path.basename(file)} is not a PNG`);
  return [bytes.readUInt32BE(16), bytes.readUInt32BE(20)];
}

test('version 2 renders: one PNG per README step, 1000x750 and 20-250 kB, a 1200x900 hero, and nothing else in renders/v2', () => {
  const n = v2Steps();
  const dir = path.join(siteDir, 'assets', 'renders', 'v2');
  for (let k = 1; k <= n; k += 1) {
    const file = path.join(dir, `assembly-${pad(k)}.png`);
    assert.ok(existsSync(file), `v2/assembly-${pad(k)}.png is missing`);
    assert.deepEqual(pngSize(file), [1000, 750], `v2/assembly-${pad(k)}.png size`);
    assert.ok(statSync(file).size > 20 * 1024, `v2/assembly-${pad(k)}.png is smaller than 20 kB`);
    assert.ok(statSync(file).size < 250 * 1024, `v2/assembly-${pad(k)}.png is 250 kB or more`);
  }
  assert.deepEqual(pngSize(path.join(dir, 'hero.png')), [1200, 900], 'v2/hero.png size');
  const files = readdirSync(dir).sort();
  assert.deepEqual(files, [...Array.from({ length: n }, (_, i) => `assembly-${pad(i + 1)}.png`), 'hero.png']);
  for (let k = 1; k < n; k += 1) {
    assert.notDeepEqual(readFileSync(path.join(dir, `assembly-${pad(k)}.png`)), readFileSync(path.join(dir, `assembly-${pad(k + 1)}.png`)), `v2 renders ${k} and ${k + 1} are identical`);
  }
});

test('v2/case/render_steps.sh renders one camera per step into renders/v2 and the v1 script cannot clean that folder', () => {
  const n = v2Steps();
  const script = readFileSync(repoPath('v2/case/render_steps.sh'), 'utf8');
  assert.match(script, /^#!\/usr\/bin\/env bash/);
  assert.match(script, /--backend=manifold/);
  assert.match(script, /--render/);
  assert.match(script, /--colorscheme=Tomorrow/);
  assert.match(script, /--imgsize=1000,750/);
  assert.match(script, /OUT="\.\.\/\.\.\/site\/assets\/renders\/v2"/);
  assert.match(script, /rm -f "\$OUT"\/assembly-\*\.png/);
  const cameras = [...script.matchAll(/^\s+"(-?[\d.]+(?:,-?[\d.]+){6})"$/gm)].map((m) => m[1]);
  assert.equal(cameras.length, n, 'one camera per step');
  const v1 = readFileSync(repoPath('v1/case/render_steps.sh'), 'utf8');
  assert.match(v1, /OUT="\.\.\/\.\.\/site\/assets\/renders"/);
  assert.doesNotMatch(v1, /rm -r|rm -rf|\*\*|find /);
});

test('v1/case/steps.scad draws every step from the case model and v1/case/render_steps.sh renders them with manifold', () => {
  const scad = readFileSync(repoPath('v1/case/steps.scad'), 'utf8');
  assert.match(scad, /include <voice-case\.scad>/);
  assert.match(scad, /^part = "params";/m);
  assert.match(scad, /^step = \d+;/m);
  assert.match(scad, /darkorange/);
  assert.match(scad, /lightslategray/);
  assert.match(scad, /gold/);
  assert.match(scad, /arc_lift = 18;/);
  for (let k = 1; k <= N; k += 1) assert.match(scad, new RegExp(`module scene_${k}\\(`), `scene_${k}`);
  assert.doesNotMatch(scad, new RegExp(`module scene_${N + 1}\\(`));
  assert.doesNotMatch(scad, /\/\/|\/\*/, 'no comments in code');
  const script = readFileSync(repoPath('v1/case/render_steps.sh'), 'utf8');
  assert.match(script, /^#!\/usr\/bin\/env bash/);
  assert.match(script, /--backend=manifold/);
  assert.match(script, /--render/);
  assert.match(script, /--colorscheme=Tomorrow/);
  assert.match(script, /--imgsize=1000,750/);
  assert.match(script, /\.\.\/site\/assets\/renders/);
  assert.match(script, /assembly-\$\(printf '%02d'/);
  assert.doesNotMatch(script, /\/Users\/|\/home\/[a-z]|\/private\/tmp|[A-Z]:\\\\Users/);
  const cameras = [...script.matchAll(/^\s+"(-?[\d.]+(?:,-?[\d.]+){6})"$/gm)].map((m) => m[1]);
  assert.equal(cameras.length, N, 'one camera per step');
  assert.equal(cameras[5], '0,0,40,58,0,30,330', 'step 6 keeps the reference camera of the arcs scene');
});

test('the assembly is four stage steppers in this order, with these step counts, and 33 steps in all', () => {
  assert.deepEqual(STAGES.map((st) => st.id), ['asm_body', 'asm_top', 'asm_join', 'asm_tray']);
  assert.deepEqual(STAGES.map((st) => st.n), [9, 10, 3, 11]);
  assert.equal(STAGES.reduce((sum, st) => sum + st.n, 0), N);
  STAGES.forEach((st, i) => assert.equal(st.from, i === 0 ? 1 : STAGES[i - 1].from + STAGES[i - 1].n, `${st.id} starts where the previous stage ends`));
  const html = section(readPage());
  const figures = [...html.matchAll(/<figure\b[^>]*\bdata-stepper="([\w-]+)"[^>]*\bdata-steps="(\d+)"/g)].map((m) => [m[1], Number(m[2])]);
  assert.deepEqual(figures, STAGES.map((st) => [st.id, st.n]));
  assert.doesNotMatch(readPage(), /data-stepper="assembly"/, 'the single 31-step stepper is gone');
});

test('each stage has its own h3, then a small label "Stage N of 4", then its stepper, in both languages', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const html = section(readPage());
  STAGES.forEach((st, i) => {
    const re = new RegExp(`<h3\\b[^>]*\\bdata-i18n="assembly\\.${st.short}\\.title">([^<]*)</h3>\\s*<p class="stage-label" data-i18n="assembly\\.stage_${i + 1}">([^<]*)</p>\\s*<figure\\b[^>]*\\bdata-stepper="${st.id}"`);
    const m = re.exec(html);
    assert.ok(m, `${st.id}: h3, stage label and stepper in this order`);
    assert.equal(m[1], st.uk);
    assert.equal(m[2], `Етап ${i + 1} з 4`);
    assert.equal(TEXT.uk[`assembly.${st.short}.title`], st.uk);
    assert.equal(TEXT.en[`assembly.${st.short}.title`], st.en);
    assert.equal(TEXT.uk[`assembly.stage_${i + 1}`], `Етап ${i + 1} з 4`);
    assert.equal(TEXT.en[`assembly.stage_${i + 1}`], `Stage ${i + 1} of 4`);
  });
  assert.equal(TEXT.uk['assembly.stage_5'], undefined);
  assert.equal((html.match(/<h3\b/g) || []).length, 4);
  assert.equal((html.match(/class="stage-label"/g) || []).length, 4);
});

test('every stage stepper has its image frames numbered from 1, relative render paths, own alts and captions in uk and en', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  for (const st of STAGES) {
    const { open, body } = stageBlock(page, st.id);
    assert.equal(attrOf(open, 'data-steps'), String(st.n));
    assert.match(open, /\bclass="stepper"/);
    assert.doesNotMatch(body, /\bdata-only=|\bdata-hide=/);
    const frames = framesOf(body);
    assert.equal(frames.length, st.n, `${st.id}: ${st.n} <img> frames`);
    assert.deepEqual(
      frames.map((f) => Number(attrOf(f, 'data-s'))),
      Array.from({ length: st.n }, (_, i) => i + 1),
      `${st.id}: frames carry data-s 1..n in DOM order`,
    );
    frames.forEach((f, i) => {
      const k = i + 1;
      assert.equal(attrOf(f, 'src'), `assets/renders/assembly-${pad(st.from + i)}.png`, `${st.id} frame ${k} src`);
      assert.equal(attrOf(f, 'width'), '1000');
      assert.equal(attrOf(f, 'height'), '750');
      assert.equal(attrOf(f, 'loading'), 'lazy');
      assert.equal(attrOf(f, 'data-i18n-attr'), `alt:steps.${st.id}.alt_${k}`);
      const alt = unescapeHtml(attrOf(f, 'alt') || '');
      assert.ok(alt.length > 20, `${st.id} frame ${k} has an alt text`);
      assert.equal(alt, TEXT.uk[`steps.${st.id}.alt_${k}`], `${st.id} frame ${k} static alt equals the uk text`);
      assert.doesNotMatch(f, /\bsrc="(?:\/|https?:|file:)/);
      assert.doesNotMatch(f, /\baria-pressed\b|\bon\w+=/);
    });
    assert.equal((body.match(/<span class="dot"/g) || []).length, st.n, `${st.id}: dots`);
    for (const lang of ['uk', 'en']) {
      for (let k = 1; k <= st.n; k += 1) {
        const caption = TEXT[lang][`steps.${st.id}.${k}`];
        assert.ok(caption && caption.trim().length > 30, `${lang} ${st.id} caption ${k}`);
        const altText = TEXT[lang][`steps.${st.id}.alt_${k}`];
        assert.ok(altText && altText.trim().length > 20, `${lang} ${st.id} alt ${k}`);
      }
      assert.equal(TEXT[lang][`steps.${st.id}.${st.n + 1}`], undefined, `${lang} ${st.id} has a stray caption`);
      assert.equal(TEXT[lang][`steps.${st.id}.alt_${st.n + 1}`], undefined, `${lang} ${st.id} has a stray alt`);
    }
    for (let k = 1; k <= st.n; k += 1) {
      assert.notEqual(TEXT.en[`steps.${st.id}.${k}`], TEXT.uk[`steps.${st.id}.${k}`], `${st.id} caption ${k} is not translated`);
      assert.notEqual(TEXT.en[`steps.${st.id}.alt_${k}`], TEXT.uk[`steps.${st.id}.alt_${k}`], `${st.id} alt ${k} is not translated`);
    }
  }
  assert.equal(TEXT.uk['steps.assembly.1'], undefined, 'the keys of the old single stepper are gone');
});

test('in each stage only the first frame is exposed in the static markup: the rest are aria-hidden', () => {
  const page = readPage();
  for (const st of STAGES) {
    const frames = framesOf(stageBlock(page, st.id).body);
    assert.equal(attrOf(frames[0], 'aria-hidden'), null, `${st.id} frame 1 is exposed`);
    for (let k = 2; k <= st.n; k += 1) assert.equal(attrOf(frames[k - 1], 'aria-hidden'), 'true', `${st.id} frame ${k} is aria-hidden`);
  }
});

test('each stage keeps the common block: frame with one stacked box, caption, real buttons, count and dots', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  for (const st of STAGES) {
    const { body } = stageBlock(page, st.id);
    assert.match(body, /^\s*<div class="stepper-frame">\s*<div class="stepper-stack">\s*<img\b/);
    assert.match(body, /<img\b[^>]*>\s*<\/div>\s*<\/div>\s*<div class="stepper-caption-box">\s*<div class="stepper-caption"/);
    assert.match(body, /<div class="stepper-caption-sizer" data-stepper-sizer aria-hidden="true"><\/div>\s*<\/div>\s*<div class="stepper-controls">/);
    assert.equal((body.match(/class="stepper-frame"/g) || []).length, 1);
    assert.equal((body.match(/class="stepper-stack"/g) || []).length, 1);
    const caption = /<div\b[^>]*\bdata-stepper-caption\b[^>]*>([^<]*)<\/div>/.exec(body);
    assert.ok(caption);
    assert.match(/<div\b[^>]*\bdata-stepper-caption\b[^>]*>/.exec(body)[0], /aria-live="off"/, 'quiet until the reader steps');
    assert.equal(unescapeHtml(caption[1].trim()), TEXT.uk[`steps.${st.id}.1`]);
    const count = /<span\b[^>]*\bdata-stepper-count\b[^>]*>([^<]*)<\/span>/.exec(body);
    assert.equal(count[1].trim(), TEXT.uk['steps.count'].replace('{n}', '1').replace('{m}', String(st.n)));
    const prev = /<button\b([^>]*\bdata-stepper-prev\b[^>]*)>([^<]*)<\/button>/.exec(body);
    const next = /<button\b([^>]*\bdata-stepper-next\b[^>]*)>([^<]*)<\/button>/.exec(body);
    assert.match(prev[1], /\btype="button"/);
    assert.match(next[1], /\btype="button"/);
    assert.match(prev[1], /\bdisabled\b/);
    assert.equal(prev[2].trim(), TEXT.uk['steps.prev']);
    assert.equal(next[2].trim(), TEXT.uk['steps.next']);
    assert.doesNotMatch(body, /aria-pressed/);
  }
});

test('the intro comes before the first stage and explains the 3D model views, orange, gray, the yellow filament and its orange exceptions and the four stages', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const html = section(readPage());
  const h2 = html.indexOf('data-i18n="assembly.title"');
  const intro = html.indexOf('data-i18n="assembly.intro"');
  const first = html.indexOf('data-i18n="assembly.body.title"');
  assert.ok(h2 >= 0 && intro > h2 && first > intro, 'heading, intro, first stage in this order');
  const uk = TEXT.uk['assembly.intro'];
  const en = TEXT.en['assembly.intro'];
  assert.match(uk, /помаранчев/);
  assert.match(uk, /сір/);
  assert.match(uk, /нитки жовті/);
  assert.match(uk, /нитка сама є новою деталлю, вона помаранчева/);
  assert.match(uk, /знімки 3D-моделі корпусу/);
  assert.doesNotMatch(uk, /ревізі|прототип/i);
  assert.match(uk, /чотирма етапами/);
  assert.match(uk, /спаяні з дротами за розділом «Пайка»; дроти на DevKit паяють на етапі 4/);
  assert.match(en, /wires on the DevKit are soldered in stage 4/);
  assert.match(en, /orange/i);
  assert.match(en, /gray/i);
  assert.match(en, /filament is yellow/i);
  assert.match(en, /filament itself is the new part and is orange/i);
  assert.match(en, /views of the 3D model of the case/);
  assert.doesNotMatch(en, /revision|prototype/i);
  assert.match(en, /four stages/i);
  const introUk = /<p\b[^>]*data-i18n="assembly\.intro"[^>]*>([^<]+)<\/p>/.exec(html);
  assert.ok(introUk);
  assert.equal(unescapeHtml(introUk[1].trim()), uk);
});

test('every step is one action: one or two sentences, short, no forbidden words', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const lang of ['uk', 'en']) {
    for (let g = 1; g <= N; g += 1) {
      const caption = cap(TEXT, lang, g);
      assert.ok(sentences(caption) >= 1 && sentences(caption) <= 2, `${lang} step ${g} has ${sentences(caption)} sentences`);
      assert.ok(caption.length <= 420, `${lang} step ${g} is ${caption.length} characters`);
      assert.doesNotMatch(caption, FORBIDDEN, `${lang} caption ${g}`);
      assert.doesNotMatch(altOf(TEXT, lang, g), FORBIDDEN, `${lang} alt ${g}`);
    }
  }
});

test('the screw posts of the body are never called columns: that word is left to the speaker, which is called колонка', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (let g = 1; g <= N; g += 1) {
    assert.doesNotMatch(cap(TEXT, 'uk', g), /колонк/i, `uk caption ${g}`);
    assert.doesNotMatch(altOf(TEXT, 'uk', g), /колонк/i, `uk alt ${g}`);
    assert.doesNotMatch(cap(TEXT, 'en', g), /column/i, `en caption ${g}`);
    assert.doesNotMatch(altOf(TEXT, 'en', g), /column/i, `en alt ${g}`);
  }
  assert.doesNotMatch(TEXT.uk['assembly.intro'], /колонк/i);
  assert.doesNotMatch(TEXT.en['assembly.intro'], /column/i);
  assert.match(cap(TEXT, 'uk', 2), /стійк/);
  assert.match(cap(TEXT, 'en', 2), /post/);
  assert.match(cap(TEXT, 'uk', 21), /стійок/);
  assert.match(cap(TEXT, 'en', 21), /posts/);
  assert.doesNotMatch(TEXT.uk['parts.note.inserts'], /колонки/);
  assert.doesNotMatch(TEXT.en['parts.note.inserts'], /columns/);
});

const TOPICS = [
  [1, [/вставк|вставц/, /трьох бобишок/, /знизу|низом/, /паяльник/, /рівня краю/], [/insert/i, /three bosses/i, /bottom up/i, /soldering iron/i, /level with the edge/i]],
  [2, [/вставк|вставц/, /дві стійки/, /зверху/], [/insert/i, /two posts/i, /from above/i]],
  [3, [/мікрофон/, /ложе/, /боком без чипа до отвору/, /отвір плати має збігтися/], [/microphone/i, /seat/i, /without the chip towards the hole/i, /line up/i]],
  [4, [/термокле/, /лише по краю/, /під плату й до отвору клей потрапити не має/i, /шлях звуку/], [/hot glue/i, /only along its edge/i, /must not get under the board or into the hole/i, /block the sound/i]],
  [5, [/дуг/, /нитк/, /уже припаяними дротами/, /бортиком і пазом догори/, /до клацання/, /13 мм/, /5 мм контакту/], [/arc/i, /filament/i, /wires already soldered on/i, /flange and the groove up/i, /clicks/i, /13 mm/, /5 mm of the contact/]],
  [6, [/переверніть дугу/i, /бортик іде всередину стінки/, /плаский бік дивиться вгору/, /«−»[^.]*передн/, /«\+»[^.]*задн/, /До етапу 3 дуги лише лежать на обідку/, /не штовхайте тіло й не перевертайте його/], [/turn the arc/i, /flange goes inside the wall/i, /flat side faces up/i, /“−”[^.]*front/, /“\+”[^.]*back/, /Until stage 3 the arcs only lie loose on the rim/, /do not push the body or turn it over/]],
  [7, [/хвіст/, /канал/, /разом із дротом/, /не менше 5 мм/, /чотирма кінцями/, /ніколи не гніть/], [/tail/i, /channel/i, /together with its wire/i, /at least 5 mm/i, /all four ends/i, /never bend/i]],
  [8, [/[Пп]роведіть дроти/, /контакт/, /чотирьох каналів/, /жолоб, відкритий у бік центру/, /сині[^.]*«−»[^.]*передн/, /червоні[^.]*«\+»[^.]*задн/], [/route the wires/i, /contact/i, /four channels/i, /groove that is open towards the center/i, /blue[^.]*“−”[^.]*front/, /red[^.]*“\+”[^.]*back/]],
  [9, [/краплею термоклею/, /дріт і метал контакту/, /ніколи на світну частину нитки/], [/drop of hot glue/i, /wire and the metal of the contact/i, /never on the lit part/i]],
  [10, [/верх лицем донизу/, /чотири вставки/, /бобишки/], [/top face down/i, /four inserts/i, /bosses/i]],
  [11, [/ковпачок/, /тильного боку/], [/button cap/i, /from the back/i]],
  [12, [/перемикач/, /до упору/, /без натиску тиша/, /ще рано/], [/switch/i, /all the way/i, /silent without pressing/i, /too early to glue/i]],
  [13, [/приклейте перемикач краплею термоклею/i, /штовхач/, /щілину ковпачка/, /застигне/, /кілька разів/, /лише під час натиску/], [/glue the switch with a drop of hot glue/i, /pusher/i, /gap of the cap/i, /has set/i, /hard a few times/i, /only while it is pressed/i]],
  [14, [/TPU-прокладк[уа] динаміка/, /гніздо/], [/TPU speaker gasket/i, /seat/i]],
  [15, [/динамік лицем донизу/, /клемами навпроти кнопки/, /0,8 мм/, /відцентруйте/], [/speaker face down/i, /terminals opposite the button/i, /0\.8 mm/, /center/i]],
  [16, [/шайб/, /чотирьох кутових отворів/, /DIN 125/], [/washer/i, /four corner holes/i, /DIN 125/]],
  [17, [/[Хх]рестовою викруткою/, /чотири гвинти/, /навхрест/, /дифузор/, /підвіс/], [/Phillips screwdriver/, /four M2\.5 × 8 screws/i, /crosswise/i, /cone/i, /surround/i]],
  [18, [/виміряйте відстань від клем динаміка/, /клемника підсилювача в лотку/, /додайте близько 3 см і відріжте дроти такої довжини/, /[Пп]рипаяйте по дроту/], [/measure the distance from the speaker terminals/i, /terminal block in the tray/i, /add about 3 cm and cut the wires to that length/i, /solder one wire/i]],
  [19, [/валиком/, /периметру фланця/, /силіконом/, /розрізати/], [/bead/i, /edge of the flange/i, /silicone/i, /cut/i]],
  [20, [/верх/, /дуги/, /кнопкою на боці мікрофона/, /дроти динаміка й кнопки/], [/top/i, /arcs/i, /button on the microphone side/i, /speaker and button wires/i]],
  [21, [/два гвинти/, /зверху/, /хрестовою викруткою/, /стійок/], [/two M2\.5 × 8 screws/i, /from above/i, /Phillips screwdriver/, /posts/i]],
  [22, [/TPU-заглушки/, /фаскою вниз/, /врівень з лицем/], [/TPU plugs/i, /chamfer down/i, /flush with the face/i]],
  [23, [/чотири TPU-ніжки/, /ніші/, /дном догори/], [/four TPU feet/i, /niches/i, /bottom up/i]],
  [24, [/дві вставки/, /стійки/, /антенного кінця/], [/two inserts/i, /posts/i, /antenna end/i]],
  [25, [/DevKit/, /позначено штирі \(крок 2 «Дроти на DevKit»\)/, /штирями вгору/, /USB-C до заднього краю лотка, де буде проріз/, /до клацання/], [/DevKit/, /pins already marked \(step 2 of “Wires on the DevKit”\)/, /pins up/i, /USB-C towards the back edge of the tray where the slot will be/i, /latches click/i]],
  [26, [/[Хх]рестовою викруткою/, /два гвинти/, /отвори плати біля антени/, /помірно, щоб не тріснула плата/], [/Phillips screwdriver/, /two M2\.5 × 8 screws/i, /near the antenna/i, /moderately so the board does not crack/i]],
  [27, [/підсилювач/, /напаяним конденсатором/, /клемником до стінки/, /фіксатор/], [/amplifier/i, /capacitor and wires soldered on/i, /terminal block towards the wall/i, /latches/i]],
  [28, [/транзистор/, /термоклеєм/, /до дна лотка ліворуч від DevKit, уздовж нього/], [/transistor/i, /hot glue/i, /floor of the tray/i, /on the left of the DevKit and along it/]],
  [29, [/конденсатор/, /напаяний на столі/, /GND і VIN/, /не вище кінчиків штирів/], [/capacitor/i, /on the table/i, /GND and VIN/, /no higher than the pin tips/i]],
  [30, [/[Пп]рипаяйте вільні кінці дротів/, /кінчиків штирів DevKit/, /кроках 1 і 3–7/, /«Дроти на DevKit»/], [/free ends of the wires/i, /tips of the DevKit pins/i, /steps 1 and 3–7/, /“Wires on the DevKit”/]],
  [31, [/дроти динаміка/, /гвинтовий клемник підсилювача/, /викруткою/], [/speaker wires/i, /screw terminal of the amplifier/i, /screwdriver/i]],
  [32, [/мультиметром/, /кроків 8 і 9/, /«Дроти на DevKit»/, /пайк/, /USB-C/, /лежить поруч із тілом/], [/multimeter/i, /steps 8 and 9/, /“Wires on the DevKit”/, /joint/i, /USB-C/, /beside the body/i]],
  [33, [/TPU-прокладк/, /хрестовою викруткою три гвинти/, /USB-C/, /проти прорізу/, /до упору/], [/TPU gasket/i, /three screws with a Phillips screwdriver/i, /USB-C/, /slot/i, /all the way/i]],
];

test('no step drives a screw with a hex key: the M2.5 screws take a Phillips screwdriver, the terminal block a small flat one', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (let g = 1; g <= N; g += 1) {
    assert.doesNotMatch(cap(TEXT, 'uk', g), /шестигран|(?<![а-яіїєґ])ключ(?:ем)?(?![а-яіїєґ])/i, `uk step ${g}`);
    assert.doesNotMatch(cap(TEXT, 'en', g), /\bhex\b|\bkey\b/i, `en step ${g}`);
  }
  assert.match(cap(TEXT, 'uk', EXTERNAL_WIRES), /маленькою пласкою викруткою/);
  assert.match(cap(TEXT, 'en', EXTERNAL_WIRES), /small flat screwdriver/);
  assert.match(altOf(TEXT, 'uk', 28), /ліворуч від DevKit, уздовж нього/);
  assert.match(altOf(TEXT, 'en', 28), /to the left of the DevKit and along it/);
});

test('each step caption says what its assembly step is about, in both languages', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TOPICS.length, N);
  const misses = [];
  for (const [g, uk, en] of TOPICS) {
    for (const re of uk) if (!re.test(cap(TEXT, 'uk', g))) misses.push(`uk step ${g}: ${re}`);
    for (const re of en) if (!re.test(cap(TEXT, 'en', g))) misses.push(`en step ${g}: ${re}`);
  }
  assert.deepEqual(misses, []);
});

test('the switch has two steps (push up and meter, then glue and press test); the tray wiring is the DevKit stepper, then the speaker terminal', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(SWITCH_GLUE, SWITCH_UP + 1);
  assert.equal(EXTERNAL_WIRES, BOARD_WIRES + 1);
  for (const lang of ['uk', 'en']) {
    assert.doesNotMatch(cap(TEXT, lang, SWITCH_UP), /застигне|has set|кілька разів|hard a few/i, `${lang}: no press test while pushing up`);
    assert.doesNotMatch(cap(TEXT, lang, SWITCH_GLUE), /упору|all the way/i, `${lang}: no pushing up while gluing`);
    assert.doesNotMatch(cap(TEXT, lang, BOARD_WIRES), /5 мм|5 mm|убік|sideways|D22|D26|3V3/i, `${lang}: the DevKit wiring is not written out again`);
    assert.doesNotMatch(cap(TEXT, lang, EXTERNAL_WIRES), /DevKit|штир|pin/i, `${lang}: the speaker step does not repeat the DevKit wires`);
  }
});

test('the boards come to the tray already soldered: the tray stage points to the DevKit stepper and repeats neither soldering nor the checks', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const tray = STAGES.find((st) => st.id === 'asm_tray');
  for (const lang of ['uk', 'en']) {
    for (let g = tray.from; g < tray.from + tray.n; g += 1) {
      const text = cap(TEXT, lang, g);
      assert.doesNotMatch(text, /писк|тиша|beep|silence/i, `${lang} step ${g}: the check text lives only in the DevKit stepper`);
      assert.doesNotMatch(text, /[Нн]апаяйте конденсатор|[Ss]older the capacitor/, `${lang} step ${g}: the capacitor is soldered on the table`);
    }
    for (let g = 1; g <= N; g += 1) assert.doesNotMatch(cap(TEXT, lang, g), /^Якщо дроти не|^If the wires were not/, `${lang} step ${g}: no step for a choice the page never offers`);
  }
  assert.match(TEXT.uk['solder.devkit.title'], /етап 4 збирання/);
  assert.match(TEXT.en['solder.devkit.title'], /assembly stage 4/);
  for (const g of [BOARD_WIRES, CHECK]) {
    assert.ok(cap(TEXT, 'uk', g).includes(`«${TEXT.uk['solder.devkit.title'].split(' (')[0]}»`), `uk step ${g} names the DevKit stepper`);
    assert.ok(cap(TEXT, 'en', g).includes(`“${TEXT.en['solder.devkit.title'].split(' (')[0]}”`), `en step ${g} names the DevKit stepper`);
  }
  assert.match(TEXT.uk['steps.devkit.1'], /етапі 4 збирання/);
  assert.match(TEXT.en['steps.devkit.1'], /assembly stage 4/);
  assert.match(TEXT.uk['steps.devkit.2'], /^Ще до лотка/);
  assert.match(TEXT.en['steps.devkit.2'], /^Before the board goes into the tray/);
  assert.match(TEXT.uk['steps.devkit.8'], /VIN і GND/);
  assert.match(TEXT.uk['steps.devkit.9'], /3V3 і GND/);
});

test('the continuity check comes before the tray goes into the body, both in the tray stage, and talks about nothing else than the multimeter and the joints', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.ok(CHECK < TRAY_IN, 'checks first, tray into the body last');
  assert.equal(stageOf(CHECK).id, 'asm_tray');
  assert.equal(stageOf(TRAY_IN).id, 'asm_tray');
  assert.equal(TRAY_IN, STAGES[3].from + STAGES[3].n - 1, 'the tray going in is the last step of the page');
  for (const lang of ['uk', 'en']) {
    const check = cap(TEXT, lang, CHECK);
    assert.doesNotMatch(check, SERVER);
    assert.doesNotMatch(altOf(TEXT, lang, CHECK), SERVER);
    assert.doesNotMatch(check, /ввімкн|power on|switch on/i);
  }
  assert.match(altOf(TEXT, 'uk', CHECK), /VIN, GND і 3V3/);
  assert.match(altOf(TEXT, 'en', CHECK), /VIN, GND and 3V3/);
  assert.match(cap(TEXT, 'uk', TRAY_IN), /вставте лоток знизу в тіло/);
  assert.match(cap(TEXT, 'en', TRAY_IN), /push the tray into the body/);
  assert.match(altOf(TEXT, 'uk', TRAY_IN), /прокладк/);
  assert.match(altOf(TEXT, 'uk', TRAY_IN), /три помаранчеві гвинти/);
  assert.match(altOf(TEXT, 'en', TRAY_IN), /gasket/);
  assert.match(altOf(TEXT, 'en', TRAY_IN), /three orange screws/);
  assert.match(cap(TEXT, 'uk', FEET), /ніжк/);
  assert.match(cap(TEXT, 'en', FEET), /feet/);
});

test('consecutive renders are different pictures', () => {
  for (let k = 1; k < N; k += 1) {
    assert.notDeepEqual(readFileSync(renderPath(k)), readFileSync(renderPath(k + 1)), `renders ${k} and ${k + 1} are identical`);
  }
});

function makeImageStepper(id, n) {
  const frames = [];
  for (let k = 1; k <= n; k += 1) {
    frames.push(makeElement({ 'data-s': String(k), src: `assets/renders/assembly-${pad(k)}.png`, 'data-i18n-attr': `alt:steps.${id}.alt_${k}` }, '', [], 'IMG'));
  }
  const caption = makeElement({ 'data-stepper-caption': '' });
  const prev = makeElement({ 'data-stepper-prev': '' }, 'Назад');
  const next = makeElement({ 'data-stepper-next': '' }, 'Далі');
  const count = makeElement({ 'data-stepper-count': '' });
  const dots = [];
  for (let k = 1; k <= n; k += 1) dots.push(makeElement({}));
  const dotsEl = makeElement({ 'data-stepper-dots': '' }, '', dots);
  const root = makeElement({ 'data-stepper': id, 'data-steps': String(n) }, '', [...frames, caption, prev, next, count, dotsEl]);
  return { root, frames, caption, prev, next, count, dots, n };
}

function click(el) {
  el.dispatchEvent({ type: 'click' });
}

function exposed(parts) {
  return parts.frames.map((f, i) => (f.getAttribute('aria-hidden') === null ? i + 1 : 0)).filter(Boolean);
}

test('in every stage the frames fade in one over the other, Next and Back walk 1..n, the last step offers Start over', () => {
  for (const st of STAGES) {
    const parts = makeImageStepper(st.id, st.n);
    const env = makeEnv({ children: [parts.root], languages: ['uk-UA'] });
    const P = loadScripts(SCRIPTS, env);
    const start = Array.from({ length: st.n }, (_, i) => (i === 0 ? 1 : 0));
    assert.deepEqual(parts.frames.map((f) => f.style.opacity), start, `${st.id} start`);
    assert.equal(parts.caption.textContent, P.TEXT.uk[`steps.${st.id}.1`]);
    for (let k = 2; k <= st.n; k += 1) {
      click(parts.next);
      assert.equal(parts.root.getAttribute('data-cur'), String(k));
      assert.deepEqual(parts.frames.map((f) => f.style.opacity), parts.frames.map((_, i) => (i + 1 <= k ? 1 : 0)));
      assert.equal(parts.caption.textContent, P.TEXT.uk[`steps.${st.id}.${k}`]);
      assert.equal(parts.count.textContent, `Крок ${k} з ${st.n}`);
    }
    assert.equal(parts.next.textContent, 'Спочатку');
    click(parts.next);
    assert.equal(parts.root.getAttribute('data-cur'), '1');
    assert.deepEqual(parts.frames.map((f) => f.style.opacity), start);
  }
});

test('in every stage only the frame of the current step is exposed to assistive technology, going forward, back and around', () => {
  for (const st of STAGES) {
    const parts = makeImageStepper(st.id, st.n);
    const env = makeEnv({ children: [parts.root], languages: ['uk-UA'] });
    loadScripts(SCRIPTS, env);
    assert.deepEqual(exposed(parts), [1]);
    for (let k = 2; k <= st.n; k += 1) {
      click(parts.next);
      assert.deepEqual(exposed(parts), [k], `${st.id} step ${k}`);
      assert.equal(parts.frames[0].getAttribute('aria-hidden'), 'true');
    }
    click(parts.next);
    assert.deepEqual(exposed(parts), [1], `${st.id} after Start over`);
    click(parts.next);
    click(parts.next);
    click(parts.prev);
    assert.deepEqual(exposed(parts), [2], `${st.id} after Back`);
  }
});

test('stage steppers on one page do not disturb each other: own step, own exposed frame', () => {
  const a = makeImageStepper('asm_body', 9);
  const b = makeImageStepper('asm_top', 10);
  const env = makeEnv({ children: [a.root, b.root], languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  click(a.next);
  click(a.next);
  click(b.next);
  assert.equal(a.root.getAttribute('data-cur'), '3');
  assert.equal(b.root.getAttribute('data-cur'), '2');
  assert.deepEqual(exposed(a), [3]);
  assert.deepEqual(exposed(b), [2]);
  P.setLang('en');
  assert.equal(a.caption.textContent, P.TEXT.en['steps.asm_body.3']);
  assert.equal(b.caption.textContent, P.TEXT.en['steps.asm_top.2']);
  assert.equal(a.count.textContent, 'Step 3 of 9');
  assert.equal(b.count.textContent, 'Step 2 of 10');
  assert.deepEqual(exposed(a), [3]);
  assert.deepEqual(exposed(b), [2]);
});

test('aria-hidden is touched on images only: svg groups and other elements keep their attributes', () => {
  const group = makeElement({ 'data-s': '2' }, '', [], 'g');
  const plain = makeElement({ 'data-s': '3' });
  const root = makeElement({ 'data-stepper': 'ring', 'data-steps': '3' }, '', [group, plain,
    makeElement({ 'data-stepper-prev': '' }), makeElement({ 'data-stepper-next': '' }), makeElement({ 'data-stepper-count': '' })]);
  const env = makeEnv({ children: [root], languages: ['uk-UA'] });
  loadScripts(SCRIPTS, env);
  click(root.querySelectorAll('[data-stepper-next]')[0]);
  assert.equal(group.getAttribute('aria-hidden'), null);
  assert.equal(plain.getAttribute('aria-hidden'), null);
});

test('switching the language in the middle keeps the step and the exposed frame, and translates the caption and every frame alt', () => {
  const parts = makeImageStepper('asm_tray', 11);
  const env = makeEnv({ children: [parts.root], languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  for (let i = 0; i < 9; i += 1) click(parts.next);
  P.setLang('en');
  assert.equal(parts.root.getAttribute('data-cur'), '10');
  assert.deepEqual(exposed(parts), [10]);
  assert.equal(parts.caption.textContent, P.TEXT.en['steps.asm_tray.10']);
  assert.equal(parts.count.textContent, 'Step 10 of 11');
  parts.frames.forEach((f, i) => assert.equal(f.getAttribute('alt'), P.TEXT.en[`steps.asm_tray.alt_${i + 1}`]));
  P.setLang('uk');
  assert.equal(parts.root.getAttribute('data-cur'), '10');
  assert.deepEqual(exposed(parts), [10]);
  parts.frames.forEach((f, i) => assert.equal(f.getAttribute('alt'), P.TEXT.uk[`steps.asm_tray.alt_${i + 1}`]));
});

test('style.css stacks the frames in one grid cell, scales them to the column and rounds the light render on the dark page', () => {
  const css = readSite('assets/style.css');
  const stack = /\.stepper-stack\s*\{([^}]*)\}/.exec(css);
  assert.ok(stack, '.stepper-stack rule');
  assert.match(stack[1], /display:\s*grid/);
  assert.match(stack[1], /border-radius:\s*var\(--radius/);
  assert.match(stack[1], /overflow:\s*hidden/);
  assert.match(stack[1], /border:/);
  const img = /\.stepper-stack\s*>\s*img\s*\{([^}]*)\}/.exec(css);
  assert.ok(img, '.stepper-stack > img rule');
  assert.match(img[1], /grid-area:\s*1\s*\/\s*1/);
  assert.match(img[1], /width:\s*100%/);
  assert.match(img[1], /height:\s*auto/);
  assert.match(img[1], /display:\s*block/);
});

test('the stage label is small secondary text, and the progress dots wrap instead of running off a phone screen', () => {
  const css = readSite('assets/style.css');
  const label = /\.stage-label\s*\{([^}]*)\}/.exec(css);
  assert.ok(label, '.stage-label rule');
  assert.match(label[1], /font-size:\s*1[23]px/);
  assert.match(label[1], /color:\s*var\(--text-secondary\)/);
  const dots = /\.stepper-dots\s*\{([^}]*)\}/.exec(css);
  assert.ok(dots, '.stepper-dots rule');
  assert.match(dots[1], /display:\s*flex/);
  assert.match(dots[1], /flex-wrap:\s*wrap/);
});

test('the longest stage has at most 11 dots, so they fit on one line on a phone', () => {
  assert.ok(Math.max(...STAGES.map((st) => st.n)) <= 11);
  assert.ok(11 * 8 + 10 * 6 < 343, 'eleven dots of 8 px with 6 px gaps fit into a 343 px column');
});

test('the assembly section uses no external image, no script and no dead link', () => {
  const html = section(readPage());
  assert.doesNotMatch(html, /<script\b|<iframe\b|<a\b[^>]*href=""|href="#"/);
  assert.doesNotMatch(html, /(?:src|href)="(?:https?:)?\/\//);
  assert.doesNotMatch(html, /\/Users\/|\/home\/[a-z]|\/private\/tmp|[A-Z]:\\\\Users/);
});
