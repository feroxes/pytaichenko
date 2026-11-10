import test from 'node:test';
import assert from 'node:assert/strict';
import { loadPage, readWeb } from './helpers.mjs';

const { H } = loadPage();
const LANGS = ['uk', 'en'];

function keysUsedInHtml() {
  const html = readWeb('index.html');
  const keys = new Set();
  for (const m of html.matchAll(/data-(?:i18n|hint)="([^"]+)"/g)) keys.add(m[1]);
  for (const m of html.matchAll(/data-i18n-attr="([^"]+)"/g)) {
    for (const pair of m[1].split(';')) keys.add(pair.split(':')[1].trim());
  }
  return keys;
}

function keysUsedInScript() {
  const script = readWeb('app.js');
  const code = script.slice(script.indexOf('  function has('));
  const keys = new Set([...code.matchAll(/'([a-z]+\.[a-z_.]*[a-z_])'/g)].map((m) => m[1]));
  for (const m of script.matchAll(/\b(?:tr|key)\('([a-z_.]*[a-z_])'[,)]/g)) keys.add(m[1]);
  for (const m of script.matchAll(/\bt\((?:lang|state\.lang), '([a-z_.]*[a-z_])'[,)]/g)) keys.add(m[1]);
  for (const m of script.matchAll(/i18n: '([a-z_.]*[a-z_])'(?!\s*\+)/g)) keys.add(m[1]);
  for (const m of script.matchAll(/rule: \{ key: '([a-z_.]+)'/g)) keys.add(m[1]);
  return keys;
}

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

test('the script waits for the page and only sets the theme and language early', () => {
  const { attrs, document } = loadPage({ languages: ['en-GB'] });
  assert.deepEqual(document.listeners, ['DOMContentLoaded']);
  assert.equal(attrs['data-theme'], 'dark');
  assert.equal(attrs.lang, 'en');
});

test('a stored theme and language win, junk falls back', () => {
  assert.equal(loadPage({ stored: { 'pytaichenko-theme': 'light' } }).attrs['data-theme'], 'light');
  assert.equal(loadPage({ stored: { 'pytaichenko-theme': 'blue' } }).attrs['data-theme'], 'dark');
  assert.equal(loadPage({ stored: { 'pytaichenko-lang': 'uk' }, languages: ['en'] }).attrs.lang, 'uk');
  assert.equal(loadPage({ stored: { 'pytaichenko-lang': 'de' }, languages: ['uk-UA'] }).attrs.lang, 'uk');
});

test('the page language follows the browser like the project page', () => {
  assert.equal(H.pickLang('en', ['uk']), 'en');
  assert.equal(H.pickLang(null, ['uk-UA', 'en']), 'uk');
  assert.equal(H.pickLang(null, ['ru-RU']), 'uk');
  assert.equal(H.pickLang(null, ['de-DE', 'fr']), 'en');
  assert.equal(H.pickLang(null, []), 'en');
});

test('a language in the address wins over the stored one and the browser', () => {
  assert.equal(loadPage({ search: '?lang=en', stored: { 'pytaichenko-lang': 'uk' }, languages: ['uk-UA'] }).attrs.lang, 'en');
  assert.equal(loadPage({ search: '?x=1&lang=uk', languages: ['en'] }).attrs.lang, 'uk');
  assert.equal(loadPage({ search: '?lang=de', stored: { 'pytaichenko-lang': 'en' } }).attrs.lang, 'en');
  assert.equal(H.langFromSearch('?lang=en'), 'en');
  assert.equal(H.langFromSearch('?a=1&lang=uk&b=2'), 'uk');
  assert.equal(H.langFromSearch('?lang=EN'), null);
  assert.equal(H.langFromSearch('?language=en'), null);
  assert.equal(H.langFromSearch(''), null);
});

test('the address keeps everything else and carries the page language', () => {
  assert.equal(H.urlWithLang('http://192.0.2.5/', 'en'), 'http://192.0.2.5/?lang=en');
  assert.equal(H.urlWithLang('http://h/?lang=uk#conversation', 'en'), 'http://h/?lang=en#conversation');
  assert.equal(H.urlWithLang('http://h/?a=1&lang=uk&b=2', 'en'), 'http://h/?a=1&b=2&lang=en');
  assert.equal(H.urlWithLang('http://h/?#sound', 'uk'), 'http://h/?lang=uk#sound');
});

test('both languages have the same texts and none is empty', () => {
  const uk = Object.keys(H.TEXT.uk).sort();
  const en = Object.keys(H.TEXT.en).sort();
  assert.deepEqual(uk, en);
  for (const lang of LANGS) {
    for (const [k, v] of Object.entries(H.TEXT[lang])) assert.ok(v.trim(), `${lang} ${k}`);
  }
});

test('every text the page and the script ask for exists', () => {
  const missing = [...keysUsedInHtml(), ...keysUsedInScript()].filter((k) => !(k in H.TEXT.uk));
  assert.deepEqual(missing, []);
  assert.ok(keysUsedInHtml().size > 50);
  assert.ok(keysUsedInScript().size > 50);
});

test('the texts stay calm and neutral', () => {
  for (const lang of LANGS) {
    for (const [k, v] of Object.entries(H.TEXT[lang])) {
      assert.doesNotMatch(v, /home assistant|jarvis|джарвіс/i, `${lang} ${k}`);
      assert.doesNotMatch(v, /!/, `${lang} ${k}`);
    }
  }
  for (const v of Object.values(H.TEXT.uk)) assert.doesNotMatch(v, /[“”"]/, v);
  for (const v of Object.values(H.TEXT.en)) assert.doesNotMatch(v, /[«»]/, v);
});

test('the page speaks of the speaker, never of a server or a hub', () => {
  const all = [...Object.values(H.TEXT.uk), ...Object.values(H.TEXT.en)].join('\n');
  assert.doesNotMatch(all, /VOICE_HUB|сервер|\bserver\b|\bport\b|\bhub\b/i);
  const html = readWeb('index.html');
  assert.doesNotMatch(html, /data-speaker|data-found|data-form="manual"|data-form="server"|VOICE_HUB/);
});

test('every timing has a label, an explanation and a default', () => {
  const names = H.TIMING_FIELDS.map((f) => f.name);
  assert.deepEqual([...names].sort(), Object.keys(H.DEFAULTS.timing).sort());
  for (const lang of LANGS) {
    for (const name of names) {
      assert.ok(H.TEXT[lang][`timing.${name}.label`], `${lang} ${name} label`);
      assert.ok(H.TEXT[lang][`timing.${name}.hint`], `${lang} ${name} hint`);
    }
  }
  assert.ok(names.includes('ready_check_s'));
  assert.deepEqual(new Set(H.TIMING_FIELDS.map((f) => f.group)), new Set(['main', 'more']));
});

test('every sound setting has a label, an explanation and a default, and the equalizer has five bands', () => {
  const names = H.SOUND_FIELDS.map((f) => f.name);
  assert.deepEqual(plain(names), ['mic_gain_db', 'low_cut_hz', 'loudness_gain_db']);
  for (const lang of LANGS) {
    for (const name of names) {
      assert.ok(H.TEXT[lang][`sound.${name}.label`], `${lang} ${name} label`);
      assert.ok(H.TEXT[lang][`sound.${name}.hint`], `${lang} ${name} hint`);
    }
  }
  assert.deepEqual(plain(H.DEFAULTS.sound), { mic_gain_db: 12, low_cut_hz: 200, loudness_gain_db: 6, eq: 0 });
  assert.deepEqual(plain(H.EQ_HZ), [250, 600, 1500, 3500, 8000]);
  const eq = H.SPECS.sound.filter((s) => s.group === 'eq_db');
  assert.deepEqual(plain(eq.map((s) => s.index)), [0, 1, 2, 3, 4]);
});

test('every form field the page sends has its place in the page', () => {
  const html = readWeb('index.html');
  for (const spec of H.SPECS.openai) assert.match(html, new RegExp(`data-field="openai\\.${spec.name}"`));
  for (const spec of H.SPECS.assistant) assert.match(html, new RegExp(`data-field="assistant\\.${spec.name}"`));
  assert.match(html, /data-field="sound\.wake_word"/);
  assert.match(html, /data-sound-main/);
  assert.match(html, /data-sound-eq/);
  assert.match(html, /data-timing-main/);
  assert.match(html, /id="volume"/);
  for (const name of ['password', 'repeat', 'current', 'new']) {
    assert.match(html, new RegExp(`data-field="${name}"`));
  }
});

test('the wake word list has the three phrases the firmware knows', () => {
  const html = readWeb('index.html');
  const select = /<select id="wake-word"[\s\S]*?<\/select>/.exec(html)[0];
  assert.deepEqual([...select.matchAll(/<option value="([^"]+)">([^<]+)<\/option>/g)].map((m) => [m[1], m[2]]), [
    ['hey_jarvis', 'Hey Jarvis'], ['okay_nabu', 'Okay Nabu'], ['hey_mycroft', 'Hey Mycroft'],
  ]);
});

test('every field has a readable name for error messages in both languages', () => {
  const paths = [
    ...H.SPECS.openai.map((s) => `openai.${s.name}`),
    ...H.SPECS.assistant.map((s) => `assistant.${s.name}`),
    ...H.SPECS.timing.map((s) => `timing.${s.name}`),
    'sound.wake_word', 'sound.mic_gain_db', 'sound.low_cut_hz', 'sound.loudness_gain_db', 'sound.eq_db', 'sound.volume',
    'volume', 'password', 'current', 'new',
  ];
  for (const lang of LANGS) {
    for (const p of paths) assert.ok(H.fieldLabel(p, lang), `${lang} ${p}`);
  }
  assert.equal(H.fieldLabel('timing.nope', 'uk'), null);
  assert.equal(H.fieldLabel('devices[0].name', 'en'), null);
});

test('numbers are read with a comma or a point and shown the local way', () => {
  assert.equal(H.parseNumber('0,4'), 0.4);
  assert.equal(H.parseNumber(' 0.4 '), 0.4);
  assert.equal(H.parseNumber('8'), 8);
  assert.equal(H.parseNumber('.5'), 0.5);
  assert.equal(H.parseNumber('-3,5'), -3.5);
  for (const bad of ['', ' ', 'abc', '1e3', '1,2,3', '1.2.3', '0x10', 'Infinity', '5 с']) {
    assert.ok(Number.isNaN(H.parseNumber(bad)), bad);
  }
  assert.equal(H.formatNumber(0.4, 'uk'), '0,4');
  assert.equal(H.formatNumber(0.4, 'en'), '0.4');
  assert.equal(H.formatNumber(-3.5, 'uk'), '-3,5');
  assert.equal(H.formatNumber(8, 'uk'), '8');
  assert.equal(H.formatNumber(null, 'uk'), '');
  assert.equal(H.formatDuration(300, 'uk'), '5 хв');
  assert.equal(H.formatDuration(300, 'en'), '5 min');
  assert.equal(H.formatDuration(90, 'en'), '90 s');
  assert.equal(H.formatDuration(7200, 'en'), '2 h');
  assert.equal(H.formatDuration(0.5, 'uk'), '0,5 с');
  assert.equal(H.formatSetting('seconds0', 0.4, 'uk'), '0,4 с');
  assert.equal(H.formatSetting('grace', 0.5, 'en'), '0.5 s');
  assert.equal(H.formatSetting('count', 20, 'en'), '20');
  assert.equal(H.formatSetting('gain', 12, 'uk'), '12 дБ');
  assert.equal(H.formatSetting('hz', 200, 'en'), '200 Hz');
});

test('values are checked by the same rules as the speaker', () => {
  const ok = (kind, text) => assert.equal(H.checkValue(kind, text).rule, undefined, `${kind} ${text}`);
  const bad = (kind, text) => assert.ok(H.checkValue(kind, text).rule, `${kind} ${text}`);
  ok('seconds', '0,1'); bad('seconds', '0'); bad('seconds', '-1'); bad('seconds', ''); bad('seconds', 'x');
  ok('seconds0', '0'); ok('seconds0', '0,4'); bad('seconds0', '-0.1');
  ok('grace', '0,5'); ok('grace', '0.5'); ok('grace', '2'); bad('grace', '0,49'); bad('grace', '0'); bad('grace', '');
  ok('count', '0'); ok('count', '20'); bad('count', '2.5'); bad('count', '-1'); bad('count', '');
  ok('ms', '1'); bad('ms', '0'); bad('ms', '600.5');
  ok('unit', '0'); ok('unit', '1,0'); ok('unit', '0.55'); bad('unit', '1.1'); bad('unit', '-0.1');
  ok('gain', '0'); ok('gain', '30'); ok('gain', '12,5'); bad('gain', '-0.1'); bad('gain', '30.1'); bad('gain', '');
  ok('hz', '80'); ok('hz', '400'); ok('hz', '200'); bad('hz', '79'); bad('hz', '401'); bad('hz', 'x');
  ok('loud', '0'); ok('loud', '12'); bad('loud', '12.1'); bad('loud', '-1');
  ok('eq', '-12'); ok('eq', '12'); ok('eq', '0'); ok('eq', '-3,5'); bad('eq', '-12.1'); bad('eq', '12.1'); bad('eq', '');
  assert.equal(H.checkValue('count', ' 3 ').value, 3);
  assert.deepEqual(plain(H.checkValue('eq', '13').rule), { key: 'rule.number_range', vars: { low: -12, high: 12 } });
  assert.deepEqual(plain(H.checkValue('hz', '10').rule), { key: 'rule.number_range', vars: { low: 80, high: 400 } });
});

const OPENAI = {
  api_key_set: true, model: 'gpt-realtime-2.1-mini', voice: 'marin', reasoning_effort: 'low',
  vad_threshold: 0.5, silence_ms: 600, log_heard: false,
};
const FORM = {
  api_key: '', model: 'gpt-realtime-2.1-mini', voice: 'marin', reasoning_effort: 'low',
  vad_threshold: '0,5', silence_ms: '600', log_heard: false,
};

test('an untouched form sends nothing, and an empty key field never clears the key', () => {
  const built = H.buildPatch(H.SPECS.openai, OPENAI, FORM);
  assert.deepEqual(plain(built), { patch: {}, errors: [], empty: true });
  assert.ok(H.buildPatch(H.SPECS.openai, OPENAI, { ...FORM, api_key: '   ' }).empty);
});

test('only the changed fields are sent, the key only when typed', () => {
  const built = H.buildPatch(H.SPECS.openai, OPENAI, {
    ...FORM, api_key: '  sk-new  ', voice: 'cedar', reasoning_effort: '', vad_threshold: '0,6', log_heard: true,
  });
  assert.deepEqual(plain(built.patch), {
    api_key: 'sk-new', voice: 'cedar', reasoning_effort: null, vad_threshold: 0.6, log_heard: true,
  });
});

test('bad values are named and nothing is sent', () => {
  const built = H.buildPatch(H.SPECS.openai, OPENAI, { ...FORM, vad_threshold: '2', silence_ms: '0' });
  assert.deepEqual(plain(built.errors.map((e) => e.name)), ['vad_threshold', 'silence_ms']);
});

test('the model and the voice are chosen from lists, not typed', () => {
  for (const name of ['model', 'voice']) {
    assert.equal(H.SPECS.openai.find((f) => f.name === name).kind, 'choice');
  }
});

test('timings send only what changed, with zero allowed where the speaker allows it', () => {
  const loaded = { ...H.DEFAULTS.timing };
  const values = Object.fromEntries(Object.entries(loaded).map(([k, v]) => [k, H.formatNumber(v, 'uk')]));
  assert.ok(H.buildPatch(H.SPECS.timing, loaded, values).empty);
  const built = H.buildPatch(H.SPECS.timing, loaded, { ...values, mic_holdoff_s: '0', follow_up_s: '12,5' });
  assert.deepEqual(plain(built.patch), { mic_holdoff_s: 0, follow_up_s: 12.5 });
  const wrong = H.buildPatch(H.SPECS.timing, loaded, { ...values, first_turn_s: '0', max_follow_ups: '1,5' });
  assert.deepEqual(plain(wrong.errors.map((e) => e.name)), ['first_turn_s', 'max_follow_ups']);
});

test('the pause before the answer keeps at least half a second, and its hint says so', () => {
  const loaded = { ...H.DEFAULTS.timing };
  const values = Object.fromEntries(Object.entries(loaded).map(([k, v]) => [k, H.formatNumber(v, 'uk')]));
  const zero = H.buildPatch(H.SPECS.timing, loaded, { ...values, mic_stop_grace_s: '0' });
  assert.deepEqual(plain(zero.errors), [{ name: 'mic_stop_grace_s', rule: { key: 'rule.grace' } }]);
  assert.deepEqual(plain(H.buildPatch(H.SPECS.timing, loaded, { ...values, mic_stop_grace_s: '0,8' }).patch),
    { mic_stop_grace_s: 0.8 });
  assert.equal(H.MIN_STOP_GRACE, 0.5);
  assert.equal(H.TIMING_FIELDS.find((f) => f.name === 'mic_stop_grace_s').kind, 'grace');
  assert.match(H.TEXT.uk['timing.mic_stop_grace_s.hint'], /Щонайменше 0,5 с\.$/);
  assert.match(H.TEXT.en['timing.mic_stop_grace_s.hint'], /At least 0\.5 s\.$/);
});

test('the assistant sends the language alone so the speaker can switch default instructions', () => {
  const loaded = { language: 'uk', instructions: 'Текст.' };
  assert.deepEqual(plain(H.buildPatch(H.SPECS.assistant, loaded, { language: 'en', instructions: 'Текст.' }).patch),
    { language: 'en' });
  assert.deepEqual(plain(H.buildPatch(H.SPECS.assistant, loaded, { language: 'uk', instructions: ' Інший \n' }).patch),
    { instructions: ' Інший \n' });
  assert.equal(H.buildPatch(H.SPECS.assistant, loaded, { language: 'uk', instructions: ' \n ' }).errors[0].rule.key,
    'rule.empty');
});

const SOUND = {
  volume: 0.78, wake_word: 'hey_jarvis', mic_gain_db: 12, low_cut_hz: 200, loudness_gain_db: 6, eq_db: [0, 0, 0, 0, 0],
};
const SOUND_FORM = {
  mic_gain_db: '12', low_cut_hz: '200', loudness_gain_db: '6', eq0: '0', eq1: '0', eq2: '0', eq3: '0', eq4: '0',
};

test('the sound block sends the equalizer as one list of five, and nothing when untouched', () => {
  assert.ok(H.buildPatch(H.SPECS.sound, SOUND, SOUND_FORM).empty);
  const one = H.buildPatch(H.SPECS.sound, SOUND, { ...SOUND_FORM, eq2: '-3,5' });
  assert.deepEqual(plain(one.patch), { eq_db: [0, 0, -3.5, 0, 0] });
  const many = H.buildPatch(H.SPECS.sound, SOUND, { ...SOUND_FORM, mic_gain_db: '16', eq0: '2', eq4: '-1' });
  assert.deepEqual(plain(many.patch), { mic_gain_db: 16, eq_db: [2, 0, 0, 0, -1] });
  const later = H.buildPatch(H.SPECS.sound, { ...SOUND, eq_db: [1, 2, 3, 4, 5] }, { ...SOUND_FORM, eq0: '1', eq1: '2', eq2: '3', eq3: '4', eq4: '9' });
  assert.deepEqual(plain(later.patch), { eq_db: [1, 2, 3, 4, 9] });
});

test('the sound block names the bad values and sends nothing', () => {
  const built = H.buildPatch(H.SPECS.sound, SOUND, { ...SOUND_FORM, low_cut_hz: '30', eq3: '20', loudness_gain_db: '7' });
  assert.deepEqual(plain(built.errors.map((e) => e.name)), ['low_cut_hz', 'eq3']);
  assert.deepEqual(plain(built.patch), { loudness_gain_db: 7 });
});

test('the wake word is one choice and unsaved sound values are told apart', () => {
  assert.equal(H.SPECS.wake.length, 1);
  assert.deepEqual(plain(H.buildPatch(H.SPECS.wake, { wake_word: 'hey_jarvis' }, { wake_word: 'okay_nabu' }).patch),
    { wake_word: 'okay_nabu' });
  assert.ok(H.buildPatch(H.SPECS.wake, { wake_word: 'hey_jarvis' }, { wake_word: 'hey_jarvis' }).empty);
  assert.deepEqual(plain(H.changedValues(H.SPECS.sound, SOUND, { ...SOUND_FORM, eq1: '2', mic_gain_db: '12.0' })), { eq1: '2' });
  assert.deepEqual(plain(H.changedValues(H.SPECS.sound, SOUND, { ...SOUND_FORM, eq1: '99' })), { eq1: '99' });
});

test('a refused value is explained with the field name in both languages', () => {
  const failure = { status: 400, code: 'invalid', message: 'timing.mic_holdoff_s: invalid value' };
  const uk = H.explain(failure, 'uk');
  assert.equal(uk.field, 'timing.mic_holdoff_s');
  assert.equal(uk.text, '«Пропустити початок звуку з мікрофона»: колонка не прийняла це значення.');
  assert.equal(uk.short, 'Колонка не прийняла це значення.');
  assert.equal(uk.detail, '');
  const en = H.explain(failure, 'en');
  assert.equal(en.text, '“Skip the start of the microphone sound” was not accepted by the speaker.');
  const eq = H.explain({ code: 'invalid', message: 'sound.eq_db: invalid value' }, 'uk');
  assert.equal(eq.field, 'sound.eq_db');
  assert.equal(eq.text, '«Еквалайзер відповіді»: колонка не прийняла це значення.');
  const volume = H.explain({ code: 'invalid', message: 'volume: must be a number from 0 to 1' }, 'en');
  assert.equal(volume.text, '“Answer volume” must be a number from 0 to 1.');
  const language = H.explain({ code: 'invalid', message: 'language: must be one of uk, en' }, 'uk');
  assert.equal(language.detail, 'language: must be one of uk, en');
});

test('every rule the speaker writes has a translation', () => {
  const rules = [
    'invalid value', 'must be a number from 0 to 1', 'must be one of uk, en', 'unknown field',
    'must be 8 to 256 characters', 'must be text of at most 256 bytes',
  ];
  for (const lang of LANGS) {
    for (const rule of rules) {
      const got = H.ruleText(rule, lang);
      assert.ok(got.known, `${lang}: ${rule}`);
      assert.doesNotMatch(got.text, /\{/, `${lang}: ${rule}`);
    }
  }
  assert.equal(H.ruleText('must be one of uk, en', 'uk').text, 'потрібне одне зі значень: uk, en');
  assert.equal(H.ruleText('must be 8 to 256 characters', 'en').text, 'must be 8 to 256 characters long');
  assert.equal(H.ruleText('must be text of at most 256 bytes', 'en').text, 'must be text of at most 256 bytes');
  assert.equal(H.ruleText('something new', 'uk').known, false);
});

test('a refusal that names no known field still reads as a sentence and keeps the speaker words', () => {
  const unknownField = H.explain({ code: 'invalid', message: 'openai.colour: unknown field' }, 'uk');
  assert.equal(unknownField.text, H.TEXT.uk['err.invalid']);
  assert.equal(unknownField.detail, 'openai.colour: unknown field');
  assert.equal(unknownField.field, undefined);
  const newRule = H.explain({ code: 'invalid', message: 'openai.model: must be shiny' }, 'en');
  assert.equal(newRule.text, '“Model” was not accepted by the speaker.');
  assert.equal(newRule.detail, 'openai.model: must be shiny');
});

test('passwords: the field is named and a wrong current password reads differently', () => {
  const short = H.explain({ code: 'bad_password', message: 'new: must be 8 to 256 characters' }, 'uk');
  assert.equal(short.field, 'new');
  assert.equal(short.text, H.TEXT.uk['err.bad_password']);
  assert.equal(H.explain({ code: 'wrong_password' }, 'uk').text, H.TEXT.uk['err.wrong_password']);
  assert.equal(H.explain({ code: 'wrong_password' }, 'en', 'password').text, H.TEXT.en['err.wrong_current']);
});

test('passwords are measured in bytes like the speaker does', () => {
  assert.equal(H.utf8Length('abc'), 3);
  assert.equal(H.utf8Length('привіт'), 12);
  assert.equal(H.utf8Length('😀'), 4);
  assert.equal(H.utf8Length('a'.repeat(256)), 256);
  assert.equal(H.utf8Length('\ud800'), Infinity);
});

test('a lost connection is not an unknown error, and a wrong host explains the accepted addresses', () => {
  for (const lang of LANGS) {
    assert.match(H.explain({ status: 403, code: 'foreign_host', message: 'x' }, lang).text, /\.local/);
    assert.equal(H.explain({ status: 0, code: 'network' }, lang).text, H.TEXT[lang]['err.network']);
    assert.equal(H.explain({ status: 0, code: 'timeout' }, lang).text, H.TEXT[lang]['err.timeout']);
  }
});

test('a failed save keeps the speaker reason as a detail, other errors do not need one', () => {
  const saved = H.explain({ status: 500, code: 'save_failed', message: 'the settings could not be saved' }, 'uk');
  assert.equal(saved.text, H.TEXT.uk['err.save_failed']);
  assert.match(saved.detail, /could not be saved/);
  const slow = H.explain({ status: 429, code: 'slow_down', message: 'too many password checks' }, 'en');
  assert.equal(slow.text, H.TEXT.en['err.slow_down']);
  assert.equal(slow.detail, '');
});

test('an unknown error is a sentence plus the speaker words, never the bare code', () => {
  const got = H.explain({ status: 418, code: 'teapot', message: 'short and stout' }, 'uk');
  assert.equal(got.text, H.TEXT.uk['err.http']);
  assert.equal(got.detail, '418 short and stout');
  assert.equal(H.explain({ status: 500 }, 'en').text, H.TEXT.en['err.http']);
});

test('every known error code reads as a sentence in both languages', () => {
  assert.ok(H.ERROR_CODES.length >= 18);
  for (const code of ['foreign_host', 'foreign_origin', 'login_required', 'json_required', 'bad_json', 'too_large', 'internal',
    'save_failed', 'password_set', 'no_password', 'bad_password', 'wrong_password', 'slow_down', 'not_found',
    'method_not_allowed', 'invalid', 'network', 'timeout']) {
    assert.ok(H.ERROR_CODES.includes(code), code);
  }
  for (const lang of LANGS) {
    for (const code of H.ERROR_CODES) {
      const got = H.explain({ status: 409, code, message: '' }, lang);
      assert.notEqual(got.text, code);
      assert.ok(got.text.split(' ').length >= 4, `${lang} ${code}`);
    }
  }
});

test('in Ukrainian a refused value reads right after any label, feminine and plural ones too', () => {
  const ruleKeys = Object.keys(H.TEXT.uk).filter((k) => k.startsWith('rule.'));
  assert.ok(ruleKeys.length >= 10);
  assert.equal(H.TEXT.uk['field.message'], '«{field}»: {rule}.');
  for (const k of ruleKeys) {
    assert.doesNotMatch(H.TEXT.uk[k], /^(має|мають|не може|невідом|обов'язков|уже має)/, k);
  }
  const model = H.explain({ code: 'invalid', message: 'openai.model: invalid value' }, 'uk');
  assert.equal(model.text, '«Модель»: колонка не прийняла це значення.');
  const mic = H.explain({ code: 'invalid', message: 'sound.mic_gain_db: invalid value' }, 'uk');
  assert.equal(mic.text, '«Підсилення мікрофона»: колонка не прийняла це значення.');
});

test('wording: unsaved marks, OpenAI costs, the log section and the lost password', () => {
  assert.equal(H.TEXT.uk['form.unsaved'], 'Є незбережені зміни');
  assert.equal(H.TEXT.en['form.unsaved'], 'Unsaved changes');
  assert.match(H.TEXT.uk['openai.log_heard_hint'], /витрати на OpenAI/);
  assert.match(H.TEXT.uk['openai.log_heard_hint'], /«Журнал»/);
  assert.match(H.TEXT.en['openai.log_heard_hint'], /“Log”/);
  assert.match(H.TEXT.uk['openai.key_hint'], /окремий проєкт з обмеженням витрат/);
  assert.match(H.TEXT.en['openai.key_hint'], /separate project/);
  assert.match(H.TEXT.uk['password.forgot'], /кнопку зверху[\s\S]*10 секунд[\s\S]*п'ять разів[\s\S]*стиранням/);
  assert.match(H.TEXT.en['password.forgot'], /top button[\s\S]*10 seconds[\s\S]*five times[\s\S]*erase/);
});

test('unsaved values are told apart from the loaded ones, and a typed key is never kept', () => {
  const loaded = { ...OPENAI };
  assert.deepEqual(plain(H.changedValues(H.SPECS.openai, loaded, { ...FORM, api_key: 'sk-typed' })), {});
  assert.deepEqual(plain(H.changedValues(H.SPECS.openai, loaded, { ...FORM, voice: ' cedar ', vad_threshold: '2', log_heard: true })),
    { voice: ' cedar ', vad_threshold: '2', log_heard: true });
  const timing = { ...H.DEFAULTS.timing };
  const values = Object.fromEntries(Object.entries(timing).map(([k, v]) => [k, H.formatNumber(v, 'uk')]));
  assert.deepEqual(plain(H.changedValues(H.SPECS.timing, timing, { ...values, mic_holdoff_s: '0.4', follow_up_s: '12' })),
    { follow_up_s: '12' });
});

test('the log time is the uptime, or the clock when the speaker knows the date', () => {
  assert.equal(H.formatClock(0), '0:00:00');
  assert.equal(H.formatClock(75), '0:01:15');
  assert.equal(H.formatClock(3725), '1:02:05');
  assert.equal(H.formatClock(90000), '25:00:00');
  assert.match(H.formatClock(1762180000), /^\d\d:\d\d:\d\d$/);
  assert.equal(H.formatClock(-1), '');
  assert.equal(H.formatClock('x'), '');
});

test('the Wi-Fi signal is told in plain words', () => {
  assert.equal(H.signalQuality(-45), 'about.q_great');
  assert.equal(H.signalQuality(-60), 'about.q_great');
  assert.equal(H.signalQuality(-65), 'about.q_good');
  assert.equal(H.signalQuality(-75), 'about.q_weak');
  assert.equal(H.signalQuality(-90), 'about.q_bad');
  assert.equal(H.signalQuality(0), null);
  assert.equal(H.signalQuality(null), null);
});

test('the volume is shown in whole percent', () => {
  assert.equal(H.volumePercent(0.78), 78);
  assert.equal(H.volumePercent(0.7), 70);
  assert.equal(H.volumePercent(0), 0);
  assert.equal(H.volumePercent(1), 100);
  assert.equal(H.volumePercent(1.4), 100);
  assert.equal(H.volumePercent(null), null);
  assert.equal(H.volumePercent('0.5'), null);
});
