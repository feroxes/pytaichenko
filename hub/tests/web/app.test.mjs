import test from 'node:test';
import assert from 'node:assert/strict';
import { loadHub, readWeb } from './helpers.mjs';

const { H } = loadHub();
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

test('the script waits for the page and only sets the theme and language early', () => {
  const { attrs, document } = loadHub({ languages: ['en-GB'] });
  assert.deepEqual(document.listeners, ['DOMContentLoaded']);
  assert.equal(attrs['data-theme'], 'dark');
  assert.equal(attrs.lang, 'en');
});

test('a stored theme and language win, junk falls back', () => {
  assert.equal(loadHub({ stored: { 'pytaichenko-theme': 'light' } }).attrs['data-theme'], 'light');
  assert.equal(loadHub({ stored: { 'pytaichenko-theme': 'blue' } }).attrs['data-theme'], 'dark');
  assert.equal(loadHub({ stored: { 'pytaichenko-lang': 'uk' }, languages: ['en'] }).attrs.lang, 'uk');
  assert.equal(loadHub({ stored: { 'pytaichenko-lang': 'de' }, languages: ['uk-UA'] }).attrs.lang, 'uk');
});

test('the page language follows the browser like the project page', () => {
  assert.equal(H.pickLang('en', ['uk']), 'en');
  assert.equal(H.pickLang(null, ['uk-UA', 'en']), 'uk');
  assert.equal(H.pickLang(null, ['ru-RU']), 'uk');
  assert.equal(H.pickLang(null, ['de-DE', 'fr']), 'en');
  assert.equal(H.pickLang(null, []), 'en');
});

test('a language in the address wins over the stored one and the browser', () => {
  assert.equal(loadHub({ search: '?lang=en', stored: { 'pytaichenko-lang': 'uk' }, languages: ['uk-UA'] }).attrs.lang, 'en');
  assert.equal(loadHub({ search: '?x=1&lang=uk', languages: ['en'] }).attrs.lang, 'uk');
  assert.equal(loadHub({ search: '?lang=de', stored: { 'pytaichenko-lang': 'en' } }).attrs.lang, 'en');
  assert.equal(H.langFromSearch('?lang=en'), 'en');
  assert.equal(H.langFromSearch('?a=1&lang=uk&b=2'), 'uk');
  assert.equal(H.langFromSearch('?lang=EN'), null);
  assert.equal(H.langFromSearch('?language=en'), null);
  assert.equal(H.langFromSearch(''), null);
});

test('the address keeps everything else and carries the page language', () => {
  assert.equal(H.urlWithLang('http://192.0.2.5:8080/', 'en'), 'http://192.0.2.5:8080/?lang=en');
  assert.equal(H.urlWithLang('http://h/?lang=uk#conversation', 'en'), 'http://h/?lang=en#conversation');
  assert.equal(H.urlWithLang('http://h/?a=1&lang=uk&b=2', 'en'), 'http://h/?a=1&b=2&lang=en');
  assert.equal(H.urlWithLang('http://h/?#speakers', 'uk'), 'http://h/?lang=uk#speakers');
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
  assert.ok(keysUsedInHtml().size > 60);
  assert.ok(keysUsedInScript().size > 60);
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

test('every timing has a label, an explanation and a default', () => {
  const names = H.TIMING_FIELDS.map((f) => f.name);
  assert.deepEqual([...names].sort(), Object.keys(H.DEFAULTS.timing).sort());
  for (const lang of LANGS) {
    for (const name of names) {
      assert.ok(H.TEXT[lang][`timing.${name}.label`], `${lang} ${name} label`);
      assert.ok(H.TEXT[lang][`timing.${name}.hint`], `${lang} ${name} hint`);
    }
  }
  assert.deepEqual(new Set(H.TIMING_FIELDS.map((f) => f.group)), new Set(['main', 'more']));
});

test('every form field the page sends has its place in the page', () => {
  const html = readWeb('index.html');
  for (const spec of H.SPECS.openai) assert.match(html, new RegExp(`data-field="openai\\.${spec.name}"`));
  for (const spec of H.SPECS.assistant) assert.match(html, new RegExp(`data-field="assistant\\.${spec.name}"`));
  for (const spec of H.SPECS.server) assert.match(html, new RegExp(`data-field="${spec.name}"`));
  for (const name of ['host', 'port', 'key', 'password', 'repeat', 'current', 'new']) {
    assert.match(html, new RegExp(`data-field="${name}"`));
  }
});

test('every field has a readable name for error messages in both languages', () => {
  const paths = [
    ...H.SPECS.openai.map((s) => `openai.${s.name}`),
    ...H.SPECS.assistant.map((s) => `assistant.${s.name}`),
    ...H.SPECS.timing.map((s) => `timing.${s.name}`),
    ...H.SPECS.server.map((s) => s.name),
    'host', 'port', 'key', 'password', 'current', 'new', 'wake_word', 'volume',
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
  for (const bad of ['', ' ', 'abc', '1e3', '1,2,3', '1.2.3', '0x10', 'Infinity', '5 с']) {
    assert.ok(Number.isNaN(H.parseNumber(bad)), bad);
  }
  assert.equal(H.formatNumber(0.4, 'uk'), '0,4');
  assert.equal(H.formatNumber(0.4, 'en'), '0.4');
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
});

test('values are checked by the same rules as the service', () => {
  const ok = (kind, text) => assert.equal(H.checkValue(kind, text).rule, undefined, `${kind} ${text}`);
  const bad = (kind, text) => assert.ok(H.checkValue(kind, text).rule, `${kind} ${text}`);
  ok('seconds', '0,1'); bad('seconds', '0'); bad('seconds', '-1'); bad('seconds', ''); bad('seconds', 'x');
  ok('seconds0', '0'); ok('seconds0', '0,4'); bad('seconds0', '-0.1');
  ok('grace', '0,5'); ok('grace', '0.5'); ok('grace', '2'); bad('grace', '0,49'); bad('grace', '0'); bad('grace', '');
  ok('count', '0'); ok('count', '20'); bad('count', '2.5'); bad('count', '-1'); bad('count', '');
  ok('ms', '1'); bad('ms', '0'); bad('ms', '600.5');
  ok('unit', '0'); ok('unit', '1,0'); ok('unit', '0.55'); bad('unit', '1.1'); bad('unit', '-0.1');
  ok('port', '1'); ok('port', '65535'); bad('port', '0'); bad('port', '65536'); bad('port', '80.5');
  ok('server_port', '1024'); ok('server_port', '8080'); ok('server_port', '65535');
  bad('server_port', '1023'); bad('server_port', '80'); bad('server_port', '65536'); bad('server_port', '8080.5');
  assert.equal(H.checkValue('count', ' 3 ').value, 3);
  assert.deepEqual(JSON.parse(JSON.stringify(H.checkValue('port', '0').rule)),
    { key: 'rule.int_range', vars: { low: 1, high: 65535 } });
  assert.deepEqual(JSON.parse(JSON.stringify(H.checkValue('server_port', '80').rule)),
    { key: 'rule.int_range', vars: { low: 1024, high: 65535 } });
  assert.equal(H.SPECS.server.find((s) => s.name === 'http_port').kind, 'server_port');
});

test('hosts are a name or an IP address, without a scheme, a port or a path', () => {
  for (const host of ['192.0.2.10', ' 192.0.2.10 ', 'hub', 'hub.lan', 'hub.lan.', 'my_hub-2.home.arpa', 'fe80::1',
    'fe80::1%eth0', '::1', '::ffff:192.0.2.1', '1:2:3:4:5:6:7:8', 'x'.repeat(63)]) {
    assert.equal(H.isHost(host), true, host);
  }
  for (const host of ['', ' ', 'http://192.0.2.10', '192.0.2.10:8080', 'hub.lan/audio', 'hub lan', '[::1]', '::1/64',
    'hub..lan', '.hub', 'hub:8080', '1::2::3', 'g::1', 'user@hub', 'hub?x', 'x'.repeat(64), 'fe80::1%', 'хаб.lan',
    '192.168.1', '1.2.3', '192.0.2.300', '12345', 'hub.123', 'fe80::1%a b']) {
    assert.equal(H.isHost(host), false, host);
  }
  assert.equal(H.isHost('123.lan'), true);
  assert.equal(H.isHost('fe80::1%eth0', true), false);
  assert.equal(H.isHost('fe80::1', true), true);
});

const OPENAI = {
  api_key_set: true, model: 'gpt-realtime-2.1-mini', voice: 'marin', reasoning_effort: 'low',
  vad_threshold: 0.5, silence_ms: 600, log_heard: false,
};
const FORM = {
  api_key: '', model: 'gpt-realtime-2.1-mini', voice: 'marin', reasoning_effort: 'low',
  vad_threshold: '0,5', silence_ms: '600', log_heard: false,
};

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

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

test('timings send only what changed, with zero allowed where the service allows it', () => {
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
  for (const lang of LANGS) assert.doesNotMatch(H.TEXT[lang]['timing.mic_stop_grace_s.hint'], /\b0 —|0 means/);
});

test('the assistant sends the language alone so the service can switch default instructions', () => {
  const loaded = { language: 'uk', instructions: 'Текст.' };
  assert.deepEqual(plain(H.buildPatch(H.SPECS.assistant, loaded, { language: 'en', instructions: 'Текст.' }).patch),
    { language: 'en' });
  assert.deepEqual(plain(H.buildPatch(H.SPECS.assistant, loaded, { language: 'uk', instructions: ' Інший \n' }).patch),
    { instructions: ' Інший \n' });
  assert.equal(H.buildPatch(H.SPECS.assistant, loaded, { language: 'uk', instructions: ' \n ' }).errors[0].rule.key,
    'rule.empty');
});

test('the server address: empty means "find it yourself"', () => {
  const loaded = { http_port: 8080, advertise_host: null };
  assert.ok(H.buildPatch(H.SPECS.server, loaded, { http_port: '8080', advertise_host: '  ' }).empty);
  assert.deepEqual(plain(H.buildPatch(H.SPECS.server, loaded, { http_port: '8081', advertise_host: ' hub.lan ' }).patch),
    { http_port: 8081, advertise_host: 'hub.lan' });
  assert.deepEqual(plain(H.buildPatch(H.SPECS.server, { http_port: 8080, advertise_host: 'hub.lan' },
    { http_port: '8080', advertise_host: '' }).patch), { advertise_host: null });
  const wrong = H.buildPatch(H.SPECS.server, loaded, { http_port: '80', advertise_host: 'http://192.0.2.5:8080' });
  assert.deepEqual(plain(wrong.errors), [
    { name: 'http_port', rule: { key: 'rule.int_range', vars: { low: 1024, high: 65535 } } },
    { name: 'advertise_host', rule: { key: 'rule.host' } },
  ]);
});

test('a refused value is explained with the field name in both languages', () => {
  const failure = { status: 400, code: 'invalid', message: 'timing.mic_holdoff_s: must be 0 or more seconds' };
  const uk = H.explain(failure, 'uk');
  assert.equal(uk.field, 'timing.mic_holdoff_s');
  assert.equal(uk.text, '«Пропустити початок звуку з мікрофона»: потрібне число секунд, 0 або більше.');
  assert.equal(uk.short, 'Потрібне число секунд, 0 або більше.');
  assert.equal(uk.detail, '');
  const en = H.explain(failure, 'en');
  assert.equal(en.text, '“Skip the start of the microphone sound” must be a number of seconds, 0 or more.');
  const vad = H.explain({ code: 'invalid', message: 'openai.vad_threshold: must be a number from 0 to 1' }, 'uk');
  assert.equal(vad.text, '«Чутливість до голосу»: потрібне число від 0 до 1.');
  const port = H.explain({ code: 'invalid', message: 'port: must be a whole number from 1 to 65535' }, 'en');
  assert.equal(port.text, '“Port” must be a whole number from 1 to 65535.');
  assert.equal(port.field, 'port');
  const rule = 'must be a host name or an IP address, without http://, a port or a path';
  const host = H.explain({ code: 'invalid', message: `advertise_host: ${rule}` }, 'en');
  assert.equal(host.field, 'advertise_host');
  assert.equal(host.text, `“Server address for the speakers” ${rule}.`);
  assert.equal(host.detail, '');
  assert.equal(H.explain({ code: 'invalid', message: `host: ${rule}` }, 'uk').text,
    `«Адреса колонки»: ${H.TEXT.uk['rule.host']}.`);
});

test('every rule the service writes has a translation', () => {
  const rules = [
    'must be a non-empty string', 'must be a non-empty string or null', 'must be valid UTF-8 text',
    'must be a whole number from 1 to 65535', 'must be a whole number from 1024 to 65535',
    'must be a host name or an IP address, without http://, a port or a path',
    'must be a whole number of at least 0', 'must be a number from 0 to 1',
    'must be true or false', 'must be one of uk, en', 'must be 0 or more seconds', 'must be 0.5 or more seconds',
    'must be a number of seconds above 0',
    'must be base64 of 32 bytes', 'must be base64 of 32 bytes or null', 'unknown setting', 'unknown field', 'required',
    'expected an object', 'expected a list', 'must be text of at most 1024 characters', 'must be 8 to 1024 characters',
    "another speaker is already called 'kitchen'",
  ];
  for (const lang of LANGS) {
    for (const rule of rules) {
      const got = H.ruleText(rule, lang);
      assert.ok(got.known, `${lang}: ${rule}`);
      assert.doesNotMatch(got.text, /\{/, `${lang}: ${rule}`);
    }
  }
  assert.equal(H.ruleText('must be one of uk, en', 'uk').text, 'потрібне одне зі значень: uk, en');
  assert.equal(H.ruleText('must be 0.5 or more seconds', 'uk').text, 'потрібне число секунд, 0,5 або більше');
  assert.equal(H.ruleText('must be 0.5 or more seconds', 'en').text, 'must be a number of seconds, 0.5 or more');
  assert.equal(H.ruleText('must be 8 to 1024 characters', 'en').text, 'must be 8 to 1024 characters long');
  assert.equal(H.ruleText('something new', 'uk').known, false);
});

test('a refusal that names no known field still reads as a sentence and keeps the server words', () => {
  const unknownField = H.explain({ code: 'invalid', message: 'openai.colour: unknown setting' }, 'uk');
  assert.equal(unknownField.text, H.TEXT.uk['err.invalid']);
  assert.equal(unknownField.detail, 'openai.colour: unknown setting');
  assert.equal(unknownField.field, undefined);
  const newRule = H.explain({ code: 'invalid', message: 'openai.model: must be shiny' }, 'en');
  assert.equal(newRule.text, '“Model” was not accepted by the server.');
  assert.equal(newRule.detail, 'openai.model: must be shiny');
});

test('passwords: the field is named and a wrong current password reads differently', () => {
  const short = H.explain({ code: 'bad_password', message: 'new: must be 8 to 1024 characters' }, 'uk');
  assert.equal(short.field, 'new');
  assert.equal(short.text, H.TEXT.uk['err.bad_password']);
  assert.equal(H.explain({ code: 'wrong_password' }, 'uk').text, H.TEXT.uk['err.wrong_password']);
  assert.equal(H.explain({ code: 'wrong_password' }, 'en', 'password').text, H.TEXT.en['err.wrong_current']);
});

test('the host guard explains VOICE_HUB_HOSTS and a lost connection is not an unknown error', () => {
  for (const lang of LANGS) {
    assert.match(H.explain({ status: 403, code: 'foreign_host', message: 'x' }, lang).text, /VOICE_HUB_HOSTS/);
    assert.equal(H.explain({ status: 0, code: 'network' }, lang).text, H.TEXT[lang]['err.network']);
    assert.equal(H.explain({ status: 0, code: 'timeout' }, lang).text, H.TEXT[lang]['err.timeout']);
  }
});

test('a failed save keeps the server reason as a detail, other speaker errors do not need one', () => {
  const saved = H.explain({ status: 500, code: 'save_failed', message: '/data/settings.json: cannot be saved (No space left on device)' }, 'uk');
  assert.equal(saved.text, H.TEXT.uk['err.save_failed']);
  assert.match(saved.detail, /No space left/);
  const taken = H.explain({ status: 409, code: 'has_key', message: '192.0.2.30 already has a key' }, 'en');
  assert.equal(taken.text, H.TEXT.en['err.has_key']);
  assert.equal(taken.detail, '');
  assert.match(H.TEXT.uk['err.has_key'], /erase/);
  assert.match(H.TEXT.en['speaker.remove_confirm'], /erase/);
});

test('an unknown error is a sentence plus the server words, never the bare code', () => {
  const got = H.explain({ status: 418, code: 'teapot', message: 'short and stout' }, 'uk');
  assert.equal(got.text, H.TEXT.uk['err.http']);
  assert.equal(got.detail, '418 short and stout');
  assert.equal(H.explain({ status: 500 }, 'en').text, H.TEXT.en['err.http']);
});

test('every known error code reads as a sentence in both languages', () => {
  assert.ok(H.ERROR_CODES.length >= 30);
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
  assert.ok(ruleKeys.length >= 17);
  assert.equal(H.TEXT.uk['field.message'], '«{field}»: {rule}.');
  for (const k of ruleKeys) {
    assert.doesNotMatch(H.TEXT.uk[k], /^(має|мають|не може|невідом|обов'язков|уже має)/, k);
  }
  const model = H.explain({ code: 'invalid', message: 'openai.model: must be a non-empty string' }, 'uk');
  assert.equal(model.text, '«Модель»: значення не може бути порожнім.');
  const instructions = H.explain({ code: 'invalid', message: 'assistant.instructions: must be a non-empty string' }, 'uk');
  assert.equal(instructions.text, '«Інструкції»: значення не може бути порожнім.');
  const language = H.explain({ code: 'invalid', message: 'assistant.language: must be one of uk, en' }, 'uk');
  assert.equal(language.text, '«Мова розмови»: потрібне одне зі значень: uk, en.');
  assert.equal(language.short, 'Потрібне одне зі значень: uk, en.');
});

test('wording: in the list, OpenAI costs, the speaker’s firmware', () => {
  assert.equal(H.TEXT.uk['found.reflashed'],
    "Ця колонка вже додана, але зараз не на зв'язку. Якщо її перепрошили зі стиранням, заберіть її знову.");
  assert.equal(H.TEXT.uk['found.adopt_again'], 'Забрати знову');
  assert.equal(H.TEXT.uk['form.unsaved'], 'Є незбережені зміни');
  assert.equal(H.TEXT.en['form.unsaved'], 'Unsaved changes');
  assert.doesNotMatch(Object.values(H.TEXT.uk).join('\n'), /[аеєиіїоуюяь] у списку/);
  assert.match(H.TEXT.uk['openai.log_heard_hint'], /витрати на OpenAI/);
  assert.doesNotMatch(H.TEXT.uk['openai.log_heard_hint'], /оплату OpenAI/);
  assert.match(H.TEXT.en['err.unsupported'], /^This speaker's firmware/);
  assert.match(H.TEXT.en['err.wrong_key'], /this speaker's firmware\.$/);
  assert.doesNotMatch(Object.values(H.TEXT.en).join('\n'), /speaker firmware/);
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
  assert.deepEqual(plain(H.changedValues(H.SPECS.server, { http_port: 8080, advertise_host: null },
    { http_port: '8080', advertise_host: ' ' })), {});
});

const ONLINE = { name: 'speaker-a1b2c3', key_set: true, connected: true };

test('a found speaker that is saved and online is not offered for adoption again', () => {
  const stale = { name: 'speaker-a1b2c3', host: '192.0.2.30', port: 6053, provisionable: true, added: true };
  const fresh = { name: 'speaker-d4e5f6', host: '192.0.2.31', port: 6053, provisionable: true, added: false };
  const keyed = { name: 'lamp', host: '192.0.2.32', port: 6053, provisionable: false, added: false };
  const sorted = plain(H.sortFound([stale, keyed, fresh], [ONLINE]));
  assert.deepEqual(sorted.waiting.map((e) => [e.found.name, e.adopt, e.reflashed]), [['speaker-d4e5f6', true, false]]);
  assert.deepEqual(sorted.others.map((e) => [e.found.name, e.adopt]), [['lamp', false]]);
  const offline = plain(H.sortFound([stale], [{ ...ONLINE, connected: false }]));
  assert.deepEqual(offline.waiting.map((e) => [e.found.name, e.adopt, e.reflashed]), [['speaker-a1b2c3', true, true]]);
  assert.deepEqual(plain(H.sortFound([stale], [])).waiting.map((e) => e.reflashed), [true]);
});

test('a speaker card stays locked while it is being removed or moved, whatever the poll brings', () => {
  const device = { ...ONLINE, wake_words: { available: [{ id: 'okay_nabu' }], active: ['okay_nabu'] }, volume: 78 };
  const idle = { saving: false, removing: false, moving: false };
  const open = { wake: false, move: false, remove: false, volume: false };
  const shut = { wake: true, move: true, remove: true, volume: true };
  assert.deepEqual(plain(H.speakerLocks(device, idle, null)), open);
  for (const card of [{ ...idle, removing: true }, { ...idle, moving: true }]) {
    assert.deepEqual(plain(H.speakerLocks(device, card, null)), shut);
  }
  assert.deepEqual(plain(H.speakerLocks(device, idle, { kind: 'adopt' })), shut);
  assert.equal(H.speakerLocks({ ...device, connected: false }, idle, null).wake, true);
  assert.equal(H.speakerLocks({ ...device, wake_words: { available: [], active: [] } }, idle, null).wake, true);
  assert.equal(H.speakerLocks({ ...device, wake_words: { available: [], active: [] } }, idle, null).volume, false);
  assert.equal(H.speakerLocks(device, { ...idle, saving: true }, null).volume, false);
  for (const off of [{ connected: false }, { volume: null }, { volume: undefined }, { volume: '78' }]) {
    assert.equal(H.speakerLocks({ ...device, ...off }, idle, null).volume, true, JSON.stringify(off));
  }
});

test('volume errors read as volume errors, not wake word ones', () => {
  for (const lang of LANGS) {
    assert.equal(H.explain({ code: 'offline' }, lang, 'volume').text, H.TEXT[lang]['err.volume_offline']);
    assert.equal(H.explain({ code: 'failed' }, lang, 'volume').text, H.TEXT[lang]['err.volume_failed']);
    assert.equal(H.explain({ code: 'offline' }, lang).text, H.TEXT[lang]['err.offline']);
  }
  assert.match(H.TEXT.uk['err.volume_offline'], /[Гг]учність/);
  assert.match(H.TEXT.en['err.volume_failed'], /volume/);
  const refused = H.explain({ code: 'invalid', message: 'volume: must be a whole number from 0 to 100' }, 'uk', 'volume');
  assert.equal(refused.text, '«Гучність»: ' + H.t('uk', 'rule.int_range', { low: '0', high: '100' }) + '.');
});

test('removing a speaker without a key does not threaten a re-flash, and its hint agrees', () => {
  assert.equal(H.removeQuestion({ key_set: true }), 'speaker.remove_confirm');
  assert.equal(H.removeQuestion({ key_set: false }), 'speaker.remove_confirm_nokey');
  assert.equal(H.removeQuestion(undefined), 'speaker.remove_confirm');
  for (const lang of LANGS) {
    assert.doesNotMatch(H.TEXT[lang]['speaker.remove_confirm_nokey'], /erase|перепрош|re-flash/);
    assert.doesNotMatch(H.TEXT[lang]['speaker.nokey_hint'], /erase|перепрош|re-flash/);
  }
  assert.match(H.TEXT.uk['speaker.nokey_hint'], /заберіть[\s\S]*вручну з її ключем/);
  assert.match(H.TEXT.uk['speaker.remove_confirm_nokey'], /заберіть[\s\S]*вручну з її ключем/);
});
