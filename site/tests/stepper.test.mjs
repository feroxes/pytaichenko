import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { loadScripts, makeElement, makeEnv, readPage, readSite, siteDir } from './helpers.mjs';
import { HEX, contrast, insideOf, openAttr, pageBackgrounds, paintedShapes, svgItems } from './svg-helpers.mjs';

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js', 'assets/stepper.js'];
const STEPPERS = { mic: 5, capacitor: 9, ring: 9, button: 6, devkit: 9 };
const V2_SOLDER = { v2_mics: 6, v2_amp: 5, v2_ring: 9, v2_button: 4, v2_speaker: 2, v2_devkit: 13 };
const DRAWN = { ...STEPPERS, ...V2_SOLDER };

function pinMapBlock(page) {
  const m = /<figure\b[^>]*\bdata-pinmap\b[^>]*>([\s\S]*?)<\/figure>/.exec(page);
  assert.ok(m, 'the DevKit pin map figure is missing from index.html');
  return { body: m[1] };
}

function drawingBlocks(page) {
  const blocks = Object.keys(DRAWN).map((id) => ({ id, body: stepperBlock(page, id).body }));
  blocks.push({ id: 'pinmap', body: pinMapBlock(page).body });
  return blocks;
}

function stepperBlock(page, id) {
  const m = new RegExp(`<figure\\b([^>]*\\bdata-stepper="${id}"[^>]*)>([\\s\\S]*?)</figure>`).exec(page);
  assert.ok(m, `stepper ${id} is missing from index.html`);
  return { open: m[1], body: m[2] };
}

function makeStepper({ id = 'ring', n = 8, only = [], hide = [], withLabel = true, withSizer = false } = {}) {
  const groups = [];
  for (let k = 1; k <= n; k += 1) groups.push(makeElement({ 'data-s': String(k) }));
  const onlyEls = only.map((k) => makeElement({ 'data-only': String(k) }));
  const hideEls = hide.map((k) => makeElement({ 'data-hide': String(k) }));
  const label = makeElement({ 'data-i18n': 'steps.common.g' }, 'G');
  const caption = makeElement({ 'data-stepper-caption': '' });
  const prev = makeElement({ 'data-stepper-prev': '' }, 'Назад');
  const next = makeElement({ 'data-stepper-next': '' }, 'Далі');
  const count = makeElement({ 'data-stepper-count': '' });
  const dots = [];
  for (let k = 1; k <= n; k += 1) dots.push(makeElement({}));
  const dotsEl = makeElement({ 'data-stepper-dots': '' }, '', dots);
  const sizer = makeElement({ 'data-stepper-sizer': '', 'aria-hidden': 'true' });
  sizer.innerHTML = '';
  const root = makeElement({ 'data-stepper': id, 'data-steps': String(n) }, '', [
    ...groups,
    ...onlyEls,
    ...hideEls,
    ...(withLabel ? [label] : []),
    caption,
    ...(withSizer ? [sizer] : []),
    prev,
    next,
    count,
    dotsEl,
  ]);
  return { root, groups, onlyEls, hideEls, label, caption, sizer, prev, next, count, dots };
}

function click(el) {
  el.dispatchEvent({ type: 'click' });
}

function boot(parts, envOptions = {}) {
  const env = makeEnv({ children: [parts.root], languages: ['uk-UA'], ...envOptions });
  const P = loadScripts(SCRIPTS, env);
  return { env, P };
}

test('nextStep moves one step, wraps from the last step to the first and stays on 1 going back from 1', () => {
  const { nextStep } = loadScripts(SCRIPTS);
  assert.equal(nextStep(1, 8, 1), 2);
  assert.equal(nextStep(7, 8, 1), 8);
  assert.equal(nextStep(8, 8, 1), 1);
  assert.equal(nextStep(8, 8, -1), 7);
  assert.equal(nextStep(2, 8, -1), 1);
  assert.equal(nextStep(1, 8, -1), 1);
  assert.equal(nextStep(1, 1, 1), 1);
  assert.equal(nextStep(1, 1, -1), 1);
  assert.equal(nextStep(9, 8, 1), 1);
  assert.equal(nextStep(0, 8, -1), 1);
});

test('each stepper in the page numbers its steps 1..N without gaps and has N dots and N captions in uk and en', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  for (const [id, expected] of Object.entries(DRAWN)) {
    const { open, body } = stepperBlock(page, id);
    const n = Number(openAttr(open, 'data-steps'));
    assert.equal(n, expected, `${id}: data-steps`);
    const seen = new Set();
    for (const m of body.matchAll(/\bdata-(?:s|only)="(\d+)"/g)) seen.add(Number(m[1]));
    assert.deepEqual([...seen].sort((a, b) => a - b), Array.from({ length: n }, (_, i) => i + 1), `${id}: steps are not 1..N`);
    for (const m of body.matchAll(/\bdata-hide="(\d+)"/g)) {
      assert.ok(Number(m[1]) >= 1 && Number(m[1]) <= n, `${id}: data-hide ${m[1]} is outside 1..${n}`);
    }
    assert.equal((body.match(/<span class="dot"/g) || []).length, n, `${id}: dots`);
    for (const lang of ['uk', 'en']) {
      for (let k = 1; k <= n; k += 1) {
        const value = TEXT[lang][`steps.${id}.${k}`];
        assert.ok(value && value.trim().length > 0, `${lang} caption ${id}.${k} is missing`);
      }
      assert.equal(TEXT[lang][`steps.${id}.${n + 1}`], undefined, `${lang} has a stray caption ${id}.${n + 1}`);
    }
    for (let k = 1; k <= n; k += 1) {
      assert.notEqual(TEXT.en[`steps.${id}.${k}`], TEXT.uk[`steps.${id}.${k}`], `${id}.${k} is not translated`);
    }
  }
});

test('the stepper UI strings exist in uk and en and the count template carries {n} and {m}', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  for (const lang of ['uk', 'en']) {
    for (const key of ['steps.prev', 'steps.next', 'steps.restart', 'steps.count']) {
      assert.ok(TEXT[lang][key] && TEXT[lang][key].trim(), `${lang}.${key}`);
    }
    assert.match(TEXT[lang]['steps.count'], /\{n\}/);
    assert.match(TEXT[lang]['steps.count'], /\{m\}/);
  }
  assert.equal(TEXT.uk['steps.prev'], 'Назад');
  assert.equal(TEXT.uk['steps.next'], 'Далі');
  assert.equal(TEXT.uk['steps.restart'], 'Спочатку');
  assert.notEqual(TEXT.en['steps.prev'], TEXT.uk['steps.prev']);
  assert.notEqual(TEXT.en['steps.count'], TEXT.uk['steps.count']);
});

test('every SVG <text> inside a stepper or the pin map has a data-i18n key that exists in uk and en, and the svg has a translated label', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  for (const { id, body } of drawingBlocks(page)) {
    const texts = [...body.matchAll(/<text\b([^>]*)>/g)];
    assert.ok(texts.length >= 5, `${id}: suspiciously few labels`);
    for (const t of texts) {
      const key = openAttr(t[1], 'data-i18n');
      assert.ok(key, `${id}: <text ${t[1]}> has no data-i18n`);
      assert.ok(key in TEXT.uk && key in TEXT.en, `${id}: key ${key} missing`);
    }
    const svg = /<svg\b([^>]*)>/.exec(body);
    assert.ok(svg, `${id}: no svg`);
    assert.equal(openAttr(svg[1], 'role'), 'img');
    const altKey = id === 'pinmap' ? 'solder.pinmap.alt' : `steps.${id}.alt`;
    assert.equal((openAttr(svg[1], 'data-i18n-attr') || '').replace(/^aria-label:/, ''), altKey);
    assert.ok(TEXT.uk[altKey] && TEXT.en[altKey]);
  }
});

test('SVG labels are readable on the dark and the light page: theme variables on the page, light or dark ink matched to the part under them', () => {
  const page = readPage();
  const bg = pageBackgrounds();
  for (const { id, body } of drawingBlocks(page)) {
    const items = svgItems(body);
    const texts = items.filter((it) => it.kind === 'text');
    assert.ok(texts.length > 0);
    items.forEach((it, index) => {
      if (it.kind !== 'text') return;
      const label = `${id}: "${it.i18n}"`;
      assert.ok(it.fill, `${label} has no fill`);
      const under = paintedShapes(items.slice(0, index)).filter((s) => insideOf(s, it.x, it.y)).pop();
      const themed = /^var\(--[\w-]+\)$/.test(it.fill) || it.fill === 'currentColor';
      if (themed) {
        assert.equal(under, undefined, `${label} uses a theme colour over a drawn part (${under && under.fill})`);
        return;
      }
      assert.match(it.fill, HEX, `${label} has an unsupported fill ${it.fill}`);
      if (under) {
        assert.ok(contrast(it.fill, under.fill) >= 3, `${label}: ${it.fill} on ${under.fill} is below 3:1`);
      } else {
        assert.ok(contrast(it.fill, bg.dark) >= 3, `${label}: ${it.fill} is below 3:1 on the dark page`);
        assert.ok(contrast(it.fill, bg.light) >= 3, `${label}: ${it.fill} is below 3:1 on the light page`);
      }
    });
  }
});

test('nothing drawn in a stepper or the pin map vanishes on the dark page: strokes and parts on the page are visible or outlined', () => {
  const page = readPage();
  const bg = pageBackgrounds();
  for (const { id, body } of drawingBlocks(page)) {
    for (const m of body.matchAll(/\bstroke="(#[0-9a-fA-F]{3,6})"/g)) {
      assert.ok(contrast(m[1], bg.dark) >= 1.8, `${id}: stroke ${m[1]} vanishes on the dark page`);
    }
    const items = svgItems(body);
    const shapes = paintedShapes(items);
    items.forEach((it, index) => {
      if (!shapes.includes(it)) return;
      if (contrast(it.fill, bg.dark) >= 1.8) return;
      const cx = (it.box.x0 + it.box.x1) / 2;
      const cy = (it.box.y0 + it.box.y1) / 2;
      const under = shapes.slice(0, shapes.indexOf(it)).filter((s) => insideOf(s, cx, cy));
      if (under.length > 0) return;
      assert.ok(it.stroke && HEX.test(it.stroke) && contrast(it.stroke, bg.dark) >= 1.8, `${id}: ${it.kind} ${it.fill} at index ${index} has no visible outline on the dark page`);
    });
  }
});

test('on the light page pale parts keep a visible outline: no part or line below 1.8:1 against the page without a 3:1 edge', () => {
  const page = readPage();
  const bg = pageBackgrounds();
  for (const { id, body } of drawingBlocks(page)) {
    const items = svgItems(body);
    const shapes = paintedShapes(items);
    for (const it of shapes) {
      if (contrast(it.fill, bg.light) >= 1.8) continue;
      const cx = (it.box.x0 + it.box.x1) / 2;
      const cy = (it.box.y0 + it.box.y1) / 2;
      if (shapes.slice(0, shapes.indexOf(it)).some((s) => insideOf(s, cx, cy))) continue;
      assert.ok(it.stroke && HEX.test(it.stroke) && contrast(it.stroke, bg.light) >= 3, `${id}: ${it.kind} ${it.fill} at ${JSON.stringify(it.box)} has no outline that reads on the light page`);
    }
    for (const m of body.matchAll(/<line\b([^>]*)>/g)) {
      const stroke = openAttr(m[1], 'stroke');
      if (stroke && HEX.test(stroke)) assert.ok(contrast(stroke, bg.light) >= 1.8, `${id}: line ${stroke} fades on the light page`);
    }
  }
  assert.ok(contrast('#8C9098', bg.light) >= 3, 'the outline grey reads on the light page');
  assert.ok(contrast('#C9CCD2', bg.light) < 1.8, 'the pale grey alone does not');
});

test('stepper markup: figure with caption, real buttons without aria-pressed, unique ids, no dead url() references', () => {
  const page = readPage();
  for (const id of Object.keys(DRAWN)) {
    const { body } = stepperBlock(page, id);
    assert.match(body, /\bdata-stepper-caption\b/);
    assert.match(body, /<div class="stepper-caption" data-stepper-caption aria-live="off">/);
    assert.match(body, /<button\b[^>]*\btype="button"[^>]*\bdata-stepper-prev\b/);
    assert.match(body, /<button\b[^>]*\btype="button"[^>]*\bdata-stepper-next\b/);
    assert.match(body, /\bdata-stepper-count\b/);
    assert.match(body, /\bdata-stepper-dots\b/);
    assert.doesNotMatch(body, /aria-pressed/);
    assert.doesNotMatch(body, /\bon(?:click|load)\s*=/i);
  }
  const ids = [...page.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]);
  assert.equal(new Set(ids).size, ids.length, 'duplicate ids in index.html');
  for (const m of page.matchAll(/url\(#([^)]+)\)/g)) assert.ok(ids.includes(m[1]), `url(#${m[1]}) points nowhere`);
});

test('the steppers sit inside the solder section in the page order, after its heading', () => {
  const page = readPage();
  const section = /<section\b[^>]*\bid="solder"[^>]*>([\s\S]*?)<\/section>/.exec(page);
  assert.ok(section);
  for (const id of Object.keys(STEPPERS)) assert.match(section[1], new RegExp(`data-stepper="${id}"`));
  const order = Object.keys(STEPPERS).map((id) => section[1].indexOf(`data-stepper="${id}"`));
  assert.deepEqual([...order].sort((a, b) => a - b), order, 'mic, capacitor, ring, button, devkit order');
});

test('the soldering steppers follow the spec order: microphone, amplifier capacitor, ring, button, DevKit, each right after its own heading', () => {
  assert.deepEqual(Object.keys(STEPPERS), ['mic', 'capacitor', 'ring', 'button', 'devkit']);
  const whole = /<section\b[^>]*\bid="solder"[^>]*>([\s\S]*?)<\/section>/.exec(readPage())[1];
  const section = whole.slice(0, whole.indexOf('data-for-v="2"'));
  const headings = [...section.matchAll(/<h3\b[^>]*\bdata-i18n="solder\.(\w+)\.title">/g)].map((m) => m[1]);
  assert.deepEqual(headings, ['mic', 'capacitor', 'ring', 'button', 'devkit', 'pinmap', 'wires']);
  const figures = [...section.matchAll(/<figure\b[^>]*\bdata-stepper="(\w+)"/g)].map((m) => m[1]);
  assert.deepEqual(figures, ['mic', 'capacitor', 'ring', 'button', 'devkit']);
  for (const id of figures) {
    assert.match(section, new RegExp(`<h3\\b[^>]*\\bdata-i18n="solder\\.${id}\\.title">[^<]*</h3>\\s*<figure\\b[^>]*\\bdata-stepper="${id}"`), `${id}: heading right before its stepper`);
  }
});

test('the static markup already shows step 1 in Ukrainian before any script runs', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  for (const [id, n] of Object.entries(DRAWN)) {
    const { body } = stepperBlock(page, id);
    const caption = /<div\b[^>]*\bdata-stepper-caption\b[^>]*>([^<]*)<\/div>/.exec(body);
    assert.ok(caption, `${id}: no caption box`);
    assert.equal(caption[1].trim(), TEXT.uk[`steps.${id}.1`]);
    const count = /<span\b[^>]*\bdata-stepper-count\b[^>]*>([^<]*)<\/span>/.exec(body);
    assert.equal(count[1].trim(), TEXT.uk['steps.count'].replace('{n}', '1').replace('{m}', String(n)));
    const prev = /<button\b[^>]*\bdata-stepper-prev\b[^>]*>([^<]*)<\/button>/.exec(body);
    const next = /<button\b[^>]*\bdata-stepper-next\b[^>]*>([^<]*)<\/button>/.exec(body);
    assert.equal(prev[1].trim(), TEXT.uk['steps.prev']);
    assert.equal(next[1].trim(), TEXT.uk['steps.next']);
    assert.match(/<button\b[^>]*\bdata-stepper-prev\b[^>]*>/.exec(body)[0], /\bdisabled\b/);
  }
});

test('stepper.js is a classic script loaded after i18n.js and uses no fetch or modules', () => {
  const page = readPage();
  const srcs = [...page.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gi)].map((m) => m[1]);
  assert.ok(srcs.indexOf('assets/stepper.js') > srcs.indexOf('assets/i18n.js'), 'stepper.js after i18n.js');
  const code = readSite('assets/stepper.js');
  assert.doesNotMatch(code, /\bfetch\s*\(|XMLHttpRequest|^\s*(?:import|export)\b/m);
  assert.doesNotMatch(code, /aria-pressed/);
});

test('a stepper starts on step 1: only the first group is shown, Back is disabled and the caption is in the page language', () => {
  const parts = makeStepper({ n: 8 });
  const { P } = boot(parts);
  assert.equal(parts.root.getAttribute('data-cur'), '1');
  assert.equal(parts.caption.textContent, P.TEXT.uk['steps.ring.1']);
  assert.equal(parts.count.textContent, 'Крок 1 з 8');
  assert.equal(parts.prev.disabled, true);
  assert.equal(parts.next.textContent, 'Далі');
  assert.equal(parts.prev.textContent, 'Назад');
  assert.deepEqual(parts.groups.map((g) => g.style.opacity), [1, 0, 0, 0, 0, 0, 0, 0]);
  assert.deepEqual(parts.dots.map((d) => d.getAttribute('data-on')), ['true', 'false', 'false', 'false', 'false', 'false', 'false', 'false']);
});

test('Next and Back walk the steps, groups appear cumulatively, the last step offers Start over and wraps to 1', () => {
  const parts = makeStepper({ n: 8 });
  const { P } = boot(parts);
  click(parts.next);
  click(parts.next);
  click(parts.next);
  assert.equal(parts.root.getAttribute('data-cur'), '4');
  assert.equal(parts.caption.textContent, P.TEXT.uk['steps.ring.4']);
  assert.equal(parts.count.textContent, 'Крок 4 з 8');
  assert.deepEqual(parts.groups.map((g) => g.style.opacity), [1, 1, 1, 1, 0, 0, 0, 0]);
  assert.deepEqual(parts.dots.map((d) => d.getAttribute('data-on')), ['true', 'true', 'true', 'true', 'false', 'false', 'false', 'false']);
  assert.equal(parts.prev.disabled, false);
  click(parts.prev);
  assert.equal(parts.root.getAttribute('data-cur'), '3');
  for (let i = 0; i < 5; i += 1) click(parts.next);
  assert.equal(parts.root.getAttribute('data-cur'), '8');
  assert.equal(parts.next.textContent, 'Спочатку');
  assert.deepEqual(parts.groups.map((g) => g.style.opacity), [1, 1, 1, 1, 1, 1, 1, 1]);
  click(parts.next);
  assert.equal(parts.root.getAttribute('data-cur'), '1');
  assert.equal(parts.next.textContent, 'Далі');
  assert.equal(parts.prev.disabled, true);
  click(parts.prev);
  assert.equal(parts.root.getAttribute('data-cur'), '1');
});

test('when Back disables itself while it has the focus, the focus moves to Next so the keyboard keeps working', () => {
  const parts = makeStepper({ n: 4 });
  const { env } = boot(parts);
  let moved = 0;
  parts.next.focus = () => {
    moved += 1;
    env.document.activeElement = parts.next;
  };
  click(parts.next);
  assert.equal(parts.prev.disabled, false);
  env.document.activeElement = parts.prev;
  click(parts.prev);
  assert.equal(parts.prev.disabled, true);
  assert.equal(moved, 1);
  assert.equal(env.document.activeElement, parts.next);
  click(parts.next);
  click(parts.next);
  assert.equal(moved, 1, 'no focus move while Back stays enabled');
  env.document.activeElement = parts.next;
  click(parts.next);
  click(parts.next);
  assert.equal(parts.root.getAttribute('data-cur'), '1');
  assert.equal(moved, 1, 'a focused Next is left alone when Back disables itself');
});

test('smooth scrolling is on only for readers who have not asked for reduced motion', () => {
  const css = readSite('assets/style.css');
  const root = /\nhtml\s*\{([^}]*)\}/.exec(css);
  assert.ok(root, 'html rule');
  assert.doesNotMatch(root[1], /scroll-behavior/);
  assert.match(css, /@media \(prefers-reduced-motion: no-preference\)\s*\{\s*html\s*\{\s*scroll-behavior:\s*smooth;\s*\}\s*\}/);
});

test('data-only shows a group on its own step only and data-hide fades a group to 0.12 from its step on', () => {
  const parts = makeStepper({ id: 'button', n: 6, only: [2, 3], hide: [3] });
  boot(parts);
  const [only2, only3] = parts.onlyEls;
  const [hide3] = parts.hideEls;
  assert.deepEqual([only2.style.opacity, only3.style.opacity, hide3.style.opacity], [0, 0, 1]);
  click(parts.next);
  assert.deepEqual([only2.style.opacity, only3.style.opacity, hide3.style.opacity], [1, 0, 1]);
  click(parts.next);
  assert.deepEqual([only2.style.opacity, only3.style.opacity, hide3.style.opacity], [0, 1, 0.12]);
  click(parts.next);
  assert.deepEqual([only2.style.opacity, only3.style.opacity, hide3.style.opacity], [0, 0, 0.12]);
  click(parts.prev);
  click(parts.prev);
  assert.deepEqual([only2.style.opacity, only3.style.opacity, hide3.style.opacity], [1, 0, 1]);
});

test('image frames use the same data-s rule and only the frame of the current step stays exposed (aria-hidden on the rest)', () => {
  const parts = makeStepper({ id: 'ring', n: 3 });
  const frames = [1, 2, 3].map((k) => makeElement({ 'data-s': String(k), src: `s${k}.png` }, '', [], 'IMG'));
  parts.root.children.push(...frames);
  boot(parts);
  const hidden = () => frames.map((f) => f.getAttribute('aria-hidden'));
  assert.deepEqual(frames.map((f) => f.style.opacity), [1, 0, 0]);
  assert.deepEqual(hidden(), [null, 'true', 'true']);
  click(parts.next);
  assert.deepEqual(frames.map((f) => f.style.opacity), [1, 1, 0]);
  assert.deepEqual(hidden(), ['true', null, 'true']);
  click(parts.next);
  assert.deepEqual(hidden(), ['true', 'true', null]);
  click(parts.prev);
  assert.deepEqual(hidden(), ['true', null, 'true']);
});

test('switching the language in the middle of a stepper keeps the step and translates caption, buttons, count and SVG labels', () => {
  const parts = makeStepper({ n: 8 });
  const { env, P } = boot(parts);
  click(parts.next);
  click(parts.next);
  click(parts.next);
  assert.equal(parts.caption.textContent, P.TEXT.uk['steps.ring.4']);
  P.setLang('en');
  assert.equal(parts.root.getAttribute('data-cur'), '4');
  assert.equal(parts.caption.textContent, P.TEXT.en['steps.ring.4']);
  assert.equal(parts.count.textContent, 'Step 4 of 8');
  assert.equal(parts.prev.textContent, P.TEXT.en['steps.prev']);
  assert.equal(parts.next.textContent, P.TEXT.en['steps.next']);
  assert.deepEqual(parts.groups.map((g) => g.style.opacity), [1, 1, 1, 1, 0, 0, 0, 0]);
  click(parts.prev);
  assert.equal(parts.caption.textContent, P.TEXT.en['steps.ring.3']);
  for (let i = 0; i < 5; i += 1) click(parts.next);
  assert.equal(parts.next.textContent, P.TEXT.en['steps.restart']);
  P.setLang('uk');
  assert.equal(parts.root.getAttribute('data-cur'), '8');
  assert.equal(parts.next.textContent, P.TEXT.uk['steps.restart']);
  assert.equal(parts.caption.textContent, P.TEXT.uk['steps.ring.8']);
  assert.equal(parts.count.textContent, 'Крок 8 з 8');
  env.document.dispatchEvent({ type: 'pytaichenko:lang', detail: { lang: 'en' } });
  assert.equal(parts.caption.textContent, P.TEXT.en['steps.ring.8']);
  assert.equal(parts.root.getAttribute('data-cur'), '8');
});

test('the SVG labels follow the language through the global text pass', () => {
  const parts = makeStepper({ n: 8 });
  const { P } = boot(parts, { languages: ['en-US'] });
  assert.equal(parts.label.textContent, P.TEXT.en['steps.common.g']);
  P.setLang('uk');
  assert.equal(parts.label.textContent, P.TEXT.uk['steps.common.g']);
});

test('the first render uses PYTAICHENKO.lang while the document is still loading, and the later language event keeps the step', () => {
  const parts = makeStepper({ n: 8 });
  const env = makeEnv({ children: [parts.root], stored: 'en', languages: ['uk-UA'], readyState: 'loading' });
  const P = loadScripts(SCRIPTS, env);
  assert.equal(P.lang, 'en');
  assert.equal(parts.caption.textContent, P.TEXT.en['steps.ring.1']);
  assert.equal(parts.count.textContent, 'Step 1 of 8');
  click(parts.next);
  env.document.readyState = 'interactive';
  env.document.dispatchEvent({ type: 'DOMContentLoaded' });
  assert.equal(parts.root.getAttribute('data-cur'), '2');
  assert.equal(parts.caption.textContent, P.TEXT.en['steps.ring.2']);
});

test('initSteppers is idempotent: a second call does not bind a second click handler', () => {
  const parts = makeStepper({ n: 8 });
  const { env, P } = boot(parts);
  P.initSteppers(env.document);
  P.initSteppers(env.document);
  click(parts.next);
  assert.equal(parts.root.getAttribute('data-cur'), '2');
});

test('two steppers on one page keep separate steps', () => {
  const a = makeStepper({ id: 'ring', n: 8 });
  const b = makeStepper({ id: 'capacitor', n: 5 });
  const env = makeEnv({ children: [a.root, b.root], languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  click(a.next);
  click(a.next);
  click(b.next);
  assert.equal(a.root.getAttribute('data-cur'), '3');
  assert.equal(b.root.getAttribute('data-cur'), '2');
  P.setLang('en');
  assert.equal(a.caption.textContent, P.TEXT.en['steps.ring.3']);
  assert.equal(b.caption.textContent, P.TEXT.en['steps.capacitor.2']);
});

test('every stepper drawing sits in its own .stepper-frame; caption and controls stay outside it', () => {
  const page = readPage();
  const FRAME = {
    svg: { opening: /^\s*<div class="stepper-frame">\s*<svg\b/, closing: /<\/svg>\s*<\/div>\s*<div class="stepper-caption-box">\s*<div class="stepper-caption"/ },
    image: { opening: /^\s*<div class="stepper-frame">\s*<div class="stepper-stack">\s*<img\b/, closing: /<img\b[^>]*>\s*<\/div>\s*<\/div>\s*<div class="stepper-caption-box">\s*<div class="stepper-caption"/ },
  };
  const kinds = { ...Object.fromEntries(Object.keys(DRAWN).map((id) => [id, 'svg'])), asm_body: 'image', asm_top: 'image', asm_join: 'image', asm_tray: 'image', v2_asm_tray: 'image', v2_asm_body: 'image', v2_asm_top: 'image' };
  for (const [id, kind] of Object.entries(kinds)) {
    const { body } = stepperBlock(page, id);
    assert.match(body, FRAME[kind].opening, `${id}: the ${kind} must be the content of a .stepper-frame`);
    assert.match(body, FRAME[kind].closing, `${id}: the frame must close before the caption`);
    assert.equal((body.match(/<div class="stepper-frame"/g) || []).length, 1);
    assert.equal(kind === 'image' ? 1 : 0, (body.match(/<div class="stepper-stack"/g) || []).length, `${id}: stack box only for image steppers`);
    assert.equal(kind === 'image' ? 0 : 1, (body.match(/<svg\b/g) || []).length, `${id}: exactly one svg unless the frames are images`);
  }
});

test('style.css: below 600px the drawing keeps min-width 560px inside a sideways-scrolling frame, and nothing relies on overflow: visible', () => {
  const css = readSite('assets/style.css');
  const media = /@media \(max-width:\s*599px\)\s*\{([\s\S]*?)\n\}/.exec(css);
  assert.ok(media, 'no @media (max-width: 599px) block');
  assert.match(media[1], /\.stepper-frame\s*\{[^}]*overflow-x:\s*auto/);
  assert.match(media[1], /\.stepper-frame svg\s*\{[^}]*min-width:\s*560px/);
  assert.doesNotMatch(css, /overflow:\s*visible/);
  assert.doesNotMatch(css.replace(media[0], ''), /\.stepper-frame[^{]*\{[^}]*(?:overflow-x|min-width)/, 'the frame scrolls and the drawing is wide only inside the media query');
});

test('no SVG label starts left of the viewBox or ends right of it, in either language (estimated text width)', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  for (const { id, body } of drawingBlocks(page)) {
    const box = /<svg\b[^>]*\bviewBox="(-?[\d.]+) (-?[\d.]+) ([\d.]+) ([\d.]+)"/.exec(body);
    assert.ok(box, `${id}: no viewBox`);
    const [minX, width] = [Number(box[1]), Number(box[3])];
    for (const m of body.matchAll(/<text\b([^>]*)>/g)) {
      const attrs = m[1];
      const key = openAttr(attrs, 'data-i18n');
      const x = Number(openAttr(attrs, 'x'));
      const style = openAttr(attrs, 'style') || '';
      const size = Number(/font-size:\s*([\d.]+)px/.exec(style)[1]);
      const anchor = openAttr(attrs, 'text-anchor') || 'start';
      for (const lang of ['uk', 'en']) {
        const w = TEXT[lang][key].length * size * 0.5;
        const left = anchor === 'middle' ? x - w / 2 : anchor === 'end' ? x - w : x;
        const right = left + w;
        assert.ok(left >= minX, `${id} "${TEXT[lang][key]}" (${lang}) starts at ${Math.round(left)}, left of the viewBox (${minX})`);
        assert.ok(right <= minX + width, `${id} "${TEXT[lang][key]}" (${lang}) ends at ${Math.round(right)}, right of the viewBox (${minX + width})`);
      }
    }
  }
});

test('capacitor step 7: legs bent apart, rests on its legs on its side, not above the pin tips, no glue', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  const { body } = stepperBlock(page, 'capacitor');
  assert.match(TEXT.uk['steps.capacitor.7'], /не торкалися одна одної/);
  assert.match(TEXT.uk['steps.capacitor.7'], /набік/);
  assert.match(TEXT.uk['steps.capacitor.7'], /на ніжках/);
  assert.match(TEXT.uk['steps.capacitor.7'], /не вище кінчиків штирів/);
  assert.match(TEXT.uk['steps.capacitor.7'], /[Кк]лей не потрібен/);
  assert.match(TEXT.en['steps.capacitor.7'], /do not touch each other/);
  assert.match(TEXT.en['steps.capacitor.7'], /on its side/);
  assert.match(TEXT.en['steps.capacitor.7'], /rests on its legs/);
  assert.match(TEXT.en['steps.capacitor.7'], /no higher than the pin tips/);
  assert.match(TEXT.en['steps.capacitor.7'], /[Nn]o glue is needed/);
  for (const lang of ['uk', 'en']) {
    assert.equal(TEXT[lang]['steps.capacitor.glue'], undefined, `${lang}: the glue label key must be gone`);
    assert.ok(TEXT[lang]['steps.capacitor.on_side'], `${lang}: steps.capacitor.on_side`);
    assert.doesNotMatch(TEXT[lang]['steps.capacitor.7'], /термоклей|краплею|hot glue|tack/i);
  }
  assert.doesNotMatch(body, /steps\.capacitor\.glue|<ellipse/);
  assert.match(body, /<g data-s="7">[\s\S]*?<path d="M142 250[^"]*"[\s\S]*?<path d="M158 274[^"]*"[\s\S]*?steps\.capacitor\.on_side[\s\S]*?<\/g>/, 'step 7 draws both legs bent');
  const straight = [...body.matchAll(/<line\b[^>]*\bdata-hide="7"[^>]*>/g)];
  assert.equal(straight.length, 2, 'the two straight legs of step 6 fade out when the bent legs appear');
});

test('button text guides the reader when the inner pair does not behave, keeps steps 3-5 on the right legs, and ends with a flat joint beside the speaker flange', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const uk = (k) => TEXT.uk[`steps.button.${k}`];
  const en = (k) => TEXT.en[`steps.button.${k}`];
  assert.match(uk(2), /Якщо пищить завжди або не пищить ніколи, нічого не обрізайте: знайдіть мультиметром пару ніжок, яка замикається лише під час натискання, і далі працюйте з нею\./);
  assert.match(en(2), /If it beeps all the time or never beeps, do not cut anything: use the multimeter to find the pair of legs that connects only while the button is pressed, and keep working with that pair\./);
  for (const k of [3, 4, 5]) {
    assert.match(uk(k), /(?<![а-яіїєґ])пар[а-яіїєґ]{1,3}(?![а-яіїєґ])/i, `uk step ${k} must refer to the pair`);
    assert.match(en(k), /\bpair\b/, `en step ${k} must refer to the pair`);
  }
  assert.match(uk(3), /^Кусачками обріжте дві ніжки, які не входять до цієї пари, до 0,5 мм або коротше\. Зазвичай це зовнішні ніжки, як на малюнку\. Пара, що замикається, лишається цілою\./);
  assert.doesNotMatch(uk(3), /поза парою/);
  assert.match(en(3), /^With cutters, trim the two legs that are not part of this pair to 0\.5 mm or shorter\. Usually these are the outer legs, as in the picture\. The pair that connects stays whole\./);
  assert.match(uk(3), /Якщо замикається пара зовнішніх ніжок, нічого не обрізайте: розверніть перемикач на 180°, щоб ці ніжки стали внутрішніми, і перевірте ще раз\./);
  assert.match(en(3), /If the pair that connects is the two outer legs, do not cut anything: turn the switch 180° so those legs become the inner ones, and test again\./);
  assert.doesNotMatch(uk(3), /запитайте|обговор|питань|проєкту/);
  assert.doesNotMatch(en(3), /ask in|issues|discussions|project/i);
  assert.match(uk(3), /Якщо замикаються внутрішня й зовнішня ніжки, обріжте дві інші, а зовнішню ніжку з пари вкоротіть, наскільки дозволяє пайка: поруч металевий край фланця \(крок 6\)\./);
  assert.match(en(3), /If one inner and one outer leg connect, cut the other two and shorten the outer leg of the pair as far as the joint allows: the metal edge of the flange is right beside it \(step 6\)\./);
  assert.match(uk(4), /зазвичай внутрішньої/);
  assert.match(en(4), /usually an inner leg/);
  assert.match(uk(6), /маленькою й плоскою/);
  assert.match(uk(6), /поруч із ніжками, лише за 1,15 мм/);
  assert.match(en(6), /small and flat/);
  assert.match(en(6), /right beside the legs, only 1\.15 mm/);
  for (const lang of ['uk', 'en']) {
    assert.doesNotMatch(TEXT[lang]['steps.button.6'], /над ніжками|тримач|до упору|above the legs|holder|all the way/i, `${lang}: the switch goes into the top only in assembly stage 2`);
    assert.equal(TEXT[lang]['steps.button.holder'], undefined);
  }
  assert.match(uk('flange'), /1,15 мм/);
  assert.match(en('flange'), /1\.15 mm/);
  const six = /<g data-s="6">([\s\S]*?)<\/g>/.exec(stepperBlock(readPage(), 'button').body)[1];
  assert.match(six, /data-i18n="steps\.button\.flange"/);
  assert.doesNotMatch(six, /steps\.button\.holder/);
  for (const [lang, dict] of [['uk', TEXT.uk], ['en', TEXT.en]]) {
    for (const [key, value] of Object.entries(dict)) {
      if (!key.startsWith('steps.button.') && !key.startsWith('solder.button.')) continue;
      assert.doesNotMatch(value, /клацан|click|напишіть мені|write to me/i, `${lang}.${key}`);
    }
  }
});

test('style.css styles the stepper: caption, dots, controls and an opacity transition, with no aria-pressed look', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /\.stepper-caption\b/);
  assert.match(css, /\.stepper-controls\b/);
  assert.match(css, /\.dot\[data-on="true"\]/);
  assert.match(css, /\.stepper[^{]*\[data-s\][^{]*\{[^}]*transition:\s*opacity/);
  assert.doesNotMatch(css, /\.stepper[^{]*aria-pressed/);
});

test('the ring stepper starts with the filament polarity: a CR2032 lights the filament one way only, and its «+» end gets a mark', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['steps.ring.1'], /CR2032/);
  assert.match(TEXT.uk['steps.ring.1'], /світиться лише в одному положенні/);
  assert.match(TEXT.uk['steps.ring.1'], /позначте маркером «\+»/);
  assert.match(TEXT.en['steps.ring.1'], /CR2032/);
  assert.match(TEXT.en['steps.ring.1'], /lights only one way round/);
  assert.match(TEXT.en['steps.ring.1'], /mark the end that touches the “\+” side/);
  assert.match(TEXT.uk['steps.ring.2'], /^Візьміть транзистор/);
  const { body } = stepperBlock(readPage(), 'ring');
  const first = /<g data-only="1">([\s\S]*?)<\/g>/.exec(body);
  assert.ok(first, 'the polarity drawing shows on step 1 only');
  for (const key of ['steps.ring.battery', 'steps.ring.lights', 'steps.ring.mark_plus', 'steps.common.plus']) assert.match(first[1], new RegExp(`data-i18n="${key.replace(/\./g, '\\.')}"`), key);
  assert.ok(body.indexOf('<g data-only="1">') < body.indexOf('<g data-s="2">'), 'the transistor appears from step 2 on');
  assert.doesNotMatch(body, /<g data-s="1">/);
  assert.match(TEXT.uk['steps.asm_body.6'], /позначені кінці «\+»[^.]*біля задньої/);
  assert.match(TEXT.en['steps.asm_body.6'], /ends marked “\+”[^.]*by the back one/);
});

test('the amplifier stepper first solders the header and the terminal from below and trims the tails, then the capacitor, then the wires, then the check', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const uk = (k) => TEXT.uk[`steps.capacitor.${k}`];
  const en = (k) => TEXT.en[`steps.capacitor.${k}`];
  assert.match(uk(1), /Семипінову гребінку/);
  assert.match(uk(1), /краю з написами LRC … VIN/);
  assert.match(uk(1), /чорний пластик лежить на платі, довгі штирі дивляться вгору/);
  assert.match(uk(1), /знизу/);
  assert.match(en(1), /7-pin header/);
  assert.match(en(1), /edge with the LRC … VIN labels/);
  assert.match(en(1), /black plastic rests on the board and the long pins point up/);
  assert.match(en(1), /from below/);
  assert.match(uk(2), /клемник[^.]*той самий верхній бік[^.]*протилежного краю[^.]*знизу/);
  assert.match(en(2), /screw terminal[^.]*same top side[^.]*opposite edge[^.]*from below/);
  assert.match(uk(3), /врівень/);
  assert.match(uk(3), /ложе рівно/);
  assert.match(en(3), /flush/);
  assert.match(en(3), /lie flat in its seat/);
  assert.match(uk(4), /знайдіть GND і VIN/);
  assert.match(uk(8), /LRC, BCLK, DIN, GND і VIN/);
  assert.match(uk(8), /вільними до етапу 4 збирання/);
  assert.match(en(8), /free until assembly stage 4/);
  assert.match(uk(9), /^Перевірка: прозвонка на VIN і GND/);
  assert.equal(TEXT.uk['solder.capacitor.title'], 'Підсилювач і конденсатор');
  assert.equal(TEXT.en['solder.capacitor.title'], 'Amplifier and capacitor');
  const { body } = stepperBlock(readPage(), 'capacitor');
  const side = (k) => {
    const m = new RegExp(`<g data-only="${k}">([\\s\\S]*?)</g>`).exec(body);
    assert.ok(m, `side view ${k} shows on its own step`);
    return m[1];
  };
  for (const k of [1, 2, 3]) {
    assert.match(side(k), /data-i18n="steps\.capacitor\.side"/);
    assert.match(side(k), /data-i18n="steps\.capacitor\.header"/);
  }
  assert.doesNotMatch(side(1), /steps\.capacitor\.terminal/);
  assert.match(side(2), /steps\.capacitor\.terminal/);
  assert.match(side(3), /steps\.capacitor\.trim/);
  assert.doesNotMatch(side(3), /steps\.capacitor\.solder_below/);
  assert.match(body, /<g data-s="4">[\s\S]*?data-i18n="steps\.common\.max"/, 'the top view starts on step 4');
  const wires = /<g data-s="8">([\s\S]*?)<\/g>/.exec(body)[1];
  assert.deepEqual([...wires.matchAll(/data-i18n="(steps\.capacitor\.w_\w+)"/g)].map((m) => m[1]), ['lrc', 'bclk', 'din', 'gnd', 'vin'].map((w) => `steps.capacitor.w_${w}`));
  assert.equal((wires.match(/<path\b/g) || []).length, 5);
  assert.match(body, /<g data-only="9">[\s\S]*?steps\.capacitor\.silence[\s\S]*?steps\.capacitor\.beep_ok[\s\S]*?<\/g>/, 'the multimeter check is the last step: silence, and a note that a short beep at first is fine');
  assert.equal(TEXT.uk['steps.capacitor.silence'], 'тиша');
  assert.equal(TEXT.en['steps.capacitor.silence'], 'silence');
  assert.equal(TEXT.uk['steps.capacitor.beep_ok'], 'короткий пік на початку — теж норма');
  assert.match(uk(9), /Має бути тиша; короткий писк на самому початку — теж нормально, так заряджається конденсатор\. Безперервний писк означає замикання\./);
  assert.match(en(9), /It should stay silent; a short beep at the very start is fine too, that is the capacitor charging\. A continuous beep means a short circuit\./);
  for (const lang of ['uk', 'en']) {
    for (const id of ['capacitor', 'devkit']) assert.equal(TEXT[lang][`steps.${id}.beep_silence`], undefined, `${lang} ${id}`);
  }
});

test('every stepper figure is named by the h3 right before it, so the Back and Next pairs can be told apart', () => {
  const page = readPage();
  const ids = ['mic', 'capacitor', 'ring', 'button', 'devkit', ...Object.keys(V2_SOLDER), 'asm_body', 'asm_top', 'asm_join', 'asm_tray', 'v2_asm_tray', 'v2_asm_body', 'v2_asm_top'];
  const figures = [...page.matchAll(/<figure\b[^>]*\bdata-stepper="(\w+)"[^>]*>/g)];
  assert.deepEqual(figures.map((m) => m[1]), ids);
  for (const m of figures) {
    const label = openAttr(m[0].slice(7), 'aria-labelledby');
    assert.ok(label, `${m[1]}: no aria-labelledby`);
    const before = page.slice(0, m.index);
    const h3 = [...before.matchAll(/<h3\b([^>]*)>/g)].pop();
    assert.equal(openAttr(h3[1], 'id'), label, `${m[1]}: labelled by the nearest heading`);
  }
  const labels = figures.map((m) => openAttr(m[0].slice(7), 'aria-labelledby'));
  assert.equal(new Set(labels).size, labels.length);
});

test('ring step 9 only warns that the metal tab is connected to D: no heat-shrink, no tape, a dashed mark on the tab', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.equal(TEXT.uk['steps.ring.9'], "Металева пластина транзистора з'єднана з D, тож вона не має торкатися інших пайок і ніжок.");
  assert.equal(TEXT.en['steps.ring.9'], 'The metal tab of the transistor is connected to D, so it must not touch other joints or legs.');
  for (const lang of ['uk', 'en']) {
    assert.equal(TEXT[lang]['steps.ring.tape'], undefined, lang);
    for (let k = 1; k <= STEPPERS.ring; k += 1) assert.doesNotMatch(TEXT[lang][`steps.ring.${k}`], /термоусад|ізолент|heat-shrink|tape/i, `${lang} ring ${k}`);
  }
  const { body } = stepperBlock(readPage(), 'ring');
  const nine = /<g data-s="9">([\s\S]*?)<\/g>/.exec(body)[1];
  assert.deepEqual([...nine.matchAll(/data-i18n="([^"]+)"/g)].map((m) => m[1]), ['steps.ring.tab']);
  assert.match(nine, /<rect\b[^>]*fill="none"[^>]*stroke-dasharray/);
  assert.equal((nine.match(/<rect\b/g) || []).length, 1, 'no heat-shrink sleeves on the legs');
  assert.doesNotMatch(body, /steps\.ring\.tape/);
});

test('every stepper keeps its caption box as high as its longest caption: a hidden sizer stacks all captions in the same grid cell', () => {
  const page = readPage();
  for (const id of [...Object.keys(DRAWN), 'asm_body', 'asm_top', 'asm_join', 'asm_tray']) {
    const { body } = stepperBlock(page, id);
    assert.match(
      body,
      /<div class="stepper-caption-box">\s*<div class="stepper-caption" data-stepper-caption aria-live="off">[^<]*<\/div>\s*<div class="stepper-caption-sizer" data-stepper-sizer aria-hidden="true"><\/div>\s*<\/div>\s*<div class="stepper-controls">/,
      id,
    );
  }
  const css = readSite('assets/style.css');
  assert.match(css, /\.stepper-caption-box\s*\{[^}]*display:\s*grid[^}]*grid-template-columns:\s*minmax\(0,\s*1fr\)/);
  assert.match(css, /\.stepper-caption-box > \*,\s*\.stepper-caption-sizer > \*\s*\{[^}]*grid-area:\s*1 \/ 1/);
  const sizer = /\.stepper-caption-sizer\s*\{([^}]*)\}/.exec(css)[1];
  assert.match(sizer, /display:\s*grid/);
  assert.match(sizer, /visibility:\s*hidden/);
  const caption = /\n\.stepper-caption\s*\{([^}]*)\}/.exec(css)[1];
  for (const prop of ['font-size', 'line-height']) {
    assert.equal(new RegExp(`${prop}:\\s*([^;]+);`).exec(sizer)[1], new RegExp(`${prop}:\\s*([^;]+);`).exec(caption)[1], `the sizer has the caption ${prop}`);
  }
  assert.doesNotMatch(caption, /margin/, 'the margins sit on the box, so the sizer and the caption share one cell');
});

test('the sizer gets every caption of its stepper in the page language, escaped, and is rewritten only when the language changes', () => {
  const parts = makeStepper({ n: 9, withSizer: true });
  const { P } = boot(parts);
  const spans = () => [...parts.sizer.innerHTML.matchAll(/<span>([^<]*)<\/span>/g)].map((m) => m[1].replace(/&#39;|&quot;|&amp;/g, (e) => ({ '&#39;': "'", '&quot;': '"', '&amp;': '&' })[e]));
  const expected = (lang) => Array.from({ length: 9 }, (_, i) => P.TEXT[lang][`steps.ring.${i + 1}`]);
  assert.deepEqual(spans(), expected('uk'));
  assert.equal(parts.sizer.getAttribute('aria-hidden'), 'true');
  parts.sizer.innerHTML = 'kept';
  click(parts.next);
  click(parts.prev);
  assert.equal(parts.sizer.innerHTML, 'kept', 'stepping does not rebuild the sizer');
  P.setLang('en');
  assert.deepEqual(spans(), expected('en'));
  assert.equal(parts.caption.getAttribute('aria-live'), 'off', 'only the caption speaks, and not on a language switch');
  P.TEXT.en['steps.ring.2'] = '<b>x</b> & "y"';
  P.setLang('uk');
  P.setLang('en');
  assert.match(parts.sizer.innerHTML, /<span>&lt;b&gt;x&lt;\/b&gt; &amp; &quot;y&quot;<\/span>/);
});

test('only the stepper the reader is using speaks: its caption turns polite on a click, the others stay off, and a language switch stays quiet', () => {
  const a = makeStepper({ id: 'ring', n: 8 });
  const b = makeStepper({ id: 'capacitor', n: 5 });
  a.caption.setAttribute('aria-live', 'polite');
  const env = makeEnv({ children: [a.root, b.root], languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  const live = () => [a.caption.getAttribute('aria-live'), b.caption.getAttribute('aria-live')];
  assert.deepEqual(live(), ['off', 'off'], 'nothing announces on load');
  click(a.next);
  assert.deepEqual(live(), ['polite', 'off']);
  click(b.next);
  assert.deepEqual(live(), ['off', 'polite']);
  P.setLang('en');
  assert.deepEqual(live(), ['off', 'off'], 'the language switch rewrites every caption without announcing them');
  assert.equal(a.caption.textContent, P.TEXT.en['steps.ring.2']);
  assert.equal(b.caption.textContent, P.TEXT.en['steps.capacitor.2']);
  click(b.prev);
  assert.deepEqual(live(), ['off', 'polite']);
  assert.equal(b.caption.textContent, P.TEXT.en['steps.capacitor.1']);
});

test('on a phone the caption keeps room for three lines and every drawing wider than the screen says it scrolls sideways', () => {
  const css = readSite('assets/style.css');
  const media = /@media \(max-width:\s*599px\)\s*\{([\s\S]*?)\n\}/.exec(css)[1];
  const minHeight = /\.stepper \.stepper-caption\s*\{[^}]*min-height:\s*([\d.]+)em/.exec(media);
  assert.ok(minHeight, 'the phone caption has a min-height in em, with a selector that beats the base .stepper-caption rule further down');
  assert.ok(css.indexOf('.stepper-caption {') > css.indexOf(media), 'the base rule comes later, so the phone rule needs the stronger selector');
  const lineHeight = Number(/\.stepper-caption\s*\{[^}]*line-height:\s*([\d.]+)/.exec(css)[1]);
  assert.ok(Number(minHeight[1]) >= 3 * lineHeight - 0.01, 'room for three lines');
  assert.match(media, /figure\[data-scroll-hint\]::before\s*\{[^}]*content:\s*attr\(data-scroll-hint\)/);
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  const tags = [...page.matchAll(/<figure\b[^>]*>/g)].map((m) => m[0]);
  const withSvg = tags.filter((t) => /data-stepper="(?:mic|capacitor|ring|button|devkit|v2_mics|v2_amp|v2_ring|v2_button|v2_speaker|v2_devkit)"|data-pinmap/.test(t));
  assert.equal(withSvg.length, 12);
  for (const tag of withSvg) {
    assert.equal(openAttr(tag.slice(7), 'data-scroll-hint'), TEXT.uk['steps.scroll_hint'], tag);
    assert.match(openAttr(tag.slice(7), 'data-i18n-attr'), /^data-scroll-hint:steps\.scroll_hint$/);
  }
  for (const tag of tags.filter((t) => /data-stepper="asm_/.test(t))) assert.doesNotMatch(tag, /data-scroll-hint/, 'the renders scale and do not scroll');
  assert.match(TEXT.uk['steps.scroll_hint'], /вбік/);
  assert.match(TEXT.en['steps.scroll_hint'], /sideways/);
});

const V2_STAGES = [
  { id: 'v2_asm_tray', short: 'tray', readme: 'Tray', uk: 'Лоток', en: 'Tray' },
  { id: 'v2_asm_body', short: 'body', readme: 'Body onto the tray', uk: 'Тіло на лоток', en: 'Body onto the tray' },
  { id: 'v2_asm_top', short: 'top', readme: 'Lid', uk: 'Верх', en: 'Top' },
];

function v2Readme() {
  const md = readFileSync(path.join(path.resolve(siteDir, '..'), 'v2/case/README.md'), 'utf8');
  const part = md.slice(md.indexOf('## Assembly steps'));
  return [...part.matchAll(/^\| (\d+) \| ([^|]+?) \| ([^|]+?) \|$/gm)].map((m) => ({ step: Number(m[1]), stage: m[2], caption: m[3] }));
}

test('version 2 assembly: three image steppers with the README stage split, renders from renders/v2, dots, alts and captions in uk and en, headed with their stage', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const page = readPage();
  const rows = v2Readme();
  assert.equal(rows.length, 26);
  assert.deepEqual(rows.map((r) => r.step), Array.from({ length: 26 }, (_, i) => i + 1));
  const section = /<section\b[^>]*\bid="assembly"[^>]*>([\s\S]*?)<\/section>/.exec(page)[1];
  const v2 = section.slice(section.indexOf('data-for-v="2"'));
  assert.deepEqual([...v2.matchAll(/data-stepper="([\w-]+)"/g)].map((m) => m[1]), V2_STAGES.map((st) => st.id));
  let from = 1;
  V2_STAGES.forEach((st, i) => {
    const n = rows.filter((r) => r.stage === st.readme).length;
    assert.ok(n > 0 && n <= 11, `${st.id}: ${n} steps`);
    assert.deepEqual(rows.slice(from - 1, from - 1 + n).map((r) => r.stage), Array(n).fill(st.readme), `${st.id} is one run of README rows`);
    const head = new RegExp(`<h3 id="h-v2-asm-${st.short}" data-i18n="v2\\.assembly\\.${st.short}\\.title">([^<]*)</h3>\\s*<p class="stage-label" data-i18n="v2\\.assembly\\.stage_${i + 1}">([^<]*)</p>\\s*<figure class="stepper" data-stepper="${st.id}" data-steps="${n}" aria-labelledby="h-v2-asm-${st.short}">`).exec(v2);
    assert.ok(head, `${st.id}: h3, stage label and figure`);
    for (const lang of ['uk', 'en']) {
      assert.equal(TEXT[lang][`v2.assembly.${st.short}.title`], st[lang], `${lang} ${st.short} title`);
      assert.equal(TEXT[lang][`v2.assembly.stage_${i + 1}`], lang === 'uk' ? `Етап ${i + 1} з 3` : `Stage ${i + 1} of 3`, `${lang} stage ${i + 1}`);
    }
    assert.equal(head[1], TEXT.uk[`v2.assembly.${st.short}.title`]);
    assert.equal(head[2], TEXT.uk[`v2.assembly.stage_${i + 1}`]);
    const { open, body } = stepperBlock(page, st.id);
    assert.equal(Number(openAttr(open, 'data-steps')), n);
    const frames = [...body.matchAll(/<img\b([^>]*)>/g)].map((m) => m[1]);
    assert.equal(frames.length, n, `${st.id}: frames`);
    frames.forEach((f, k) => {
      assert.equal(openAttr(f, 'data-s'), String(k + 1));
      assert.equal(openAttr(f, 'src'), `assets/renders/v2/assembly-${String(from + k).padStart(2, '0')}.png`);
      assert.equal(openAttr(f, 'width'), '1000');
      assert.equal(openAttr(f, 'height'), '750');
      assert.equal(openAttr(f, 'loading'), 'lazy');
      assert.equal(openAttr(f, 'data-i18n-attr'), `alt:steps.${st.id}.alt_${k + 1}`);
      assert.equal(openAttr(f, 'alt'), TEXT.uk[`steps.${st.id}.alt_${k + 1}`]);
      assert.equal(openAttr(f, 'aria-hidden'), k === 0 ? null : 'true');
    });
    assert.equal((body.match(/<span class="dot"/g) || []).length, n);
    assert.match(body, new RegExp(`data-stepper-count>Крок 1 з ${n}<`));
    const caption = /<div class="stepper-caption" data-stepper-caption aria-live="off">([^<]*)<\/div>/.exec(body);
    assert.equal(caption[1], TEXT.uk[`steps.${st.id}.1`]);
    for (const lang of ['uk', 'en']) {
      for (let k = 1; k <= n; k += 1) {
        for (const key of [`steps.${st.id}.${k}`, `steps.${st.id}.alt_${k}`]) assert.ok(TEXT[lang][key] && TEXT[lang][key].trim(), `${lang} ${key}`);
      }
      assert.equal(TEXT[lang][`steps.${st.id}.${n + 1}`], undefined);
      assert.equal(TEXT[lang][`steps.${st.id}.alt_${n + 1}`], undefined);
    }
    for (let k = 1; k <= n; k += 1) assert.notEqual(TEXT.en[`steps.${st.id}.${k}`], TEXT.uk[`steps.${st.id}.${k}`]);
    from += n;
  });
  assert.equal(from, 27);
  const ids = [...page.matchAll(/data-stepper="([\w-]+)"/g)].map((m) => m[1]);
  assert.equal(new Set(ids).size, ids.length, 'stepper ids are unique');
});

test('version 2 assembly captions keep the site words: the top, posts, a Phillips screwdriver, the Soldering section', () => {
  const { TEXT } = loadScripts(['assets/text.js']);
  const text = (lang, id) => Object.entries(TEXT[lang]).filter(([k]) => k.startsWith(`steps.${id}.`) && /\.\d+$/.test(k)).map(([, v]) => v).join(' ');
  assert.match(text('uk', 'v2_asm_body'), /стій/);
  assert.match(text('en', 'v2_asm_body'), /posts/);
  for (const id of V2_STAGES.map((st) => st.id)) {
    for (const lang of ['uk', 'en']) assert.doesNotMatch(text(lang, id), /\blid\b|кришк|шестигран|hex/i, `${lang} ${id}`);
  }
  assert.match(text('uk', 'v2_asm_tray'), /«Пайка»/);
  assert.match(text('en', 'v2_asm_tray'), /Soldering section/);
  for (const lang of ['uk', 'en']) {
    for (const id of ['v2_asm_body', 'v2_asm_top']) assert.match(text(lang, id), lang === 'uk' ? /хрестов/ : /Phillips/, `${lang} ${id}`);
  }
});
