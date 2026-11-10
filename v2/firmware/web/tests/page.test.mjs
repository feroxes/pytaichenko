import test from 'node:test';
import assert from 'node:assert/strict';
import { makePage, reply, settle } from './dom.mjs';

const SETTINGS = {
  openai: {
    api_key_set: true, model: 'gpt-realtime-2.1-mini', voice: 'marin', reasoning_effort: 'low',
    vad_threshold: 0.5, silence_ms: 600, log_heard: false,
  },
  assistant: { language: 'uk', instructions: 'Відповідай коротко.' },
  timing: {
    first_turn_s: 8, follow_up_s: 10, max_speech_s: 20, response_timeout_s: 30, answer_max_s: 180,
    session_idle_s: 300, mic_stop_grace_s: 0.5, mic_holdoff_s: 0.4, ready_check_s: 2, max_follow_ups: 20,
  },
  sound: {
    volume: 0.78, wake_word: 'hey_jarvis', mic_gain_db: 12, low_cut_hz: 200, loudness_gain_db: 6, eq_db: [0, 0, 0, 0, 0],
  },
  password_set: true,
};
const IN = { password_set: true, logged_in: true };
const OUT = { password_set: true, logged_in: false };
const DEVICE = { name: 'pytaichenko-a1b2c3', version: '2.0.0', ip: '192.0.2.40', rssi: -62 };
const EXPIRED = reply(401, { error: 'login_required', message: 'log in first' });

function routes(extra = {}) {
  return {
    'GET api/state': reply(200, IN),
    'GET api/settings': reply(200, { settings: SETTINGS }),
    'GET api/device': reply(200, DEVICE),
    'GET api/log?after=0': reply(200, { lines: [] }),
    'POST api/login': reply(200, IN),
    'POST api/logout': reply(200, OUT),
    ...extra,
  };
}

async function open(extra = {}, options = {}) {
  const page = makePage({ routes: routes(extra), ...options });
  await settle();
  return page;
}

function later() {
  let done;
  const promise = new Promise((resolve) => {
    done = resolve;
  });
  return { promise, done };
}

function text(page, key, vars = {}) {
  return page.window.PYTAICHENKO_PAGE.t('uk', key, vars);
}

async function logIn(page) {
  page.type(page.$('#login-password'), 'correct horse battery');
  page.submit(page.$('[data-form="login"]'));
  await settle();
}

function statusOf(form) {
  return form.querySelector('.form-foot [data-status]');
}

function unsaved(page, name) {
  return !page.$(`[data-form="${name}"] [data-unsaved]`).hidden;
}

function puts(page) {
  return page.calls.filter((c) => c.method === 'PUT' && c.path === 'api/settings');
}

test('an expired session keeps the values typed but not saved, and forgets a typed key', async () => {
  const page = await open({}, { hash: '#about' });
  assert.equal(page.$('#openai-model').value, 'gpt-realtime-2.1-mini');
  page.type(page.$('#openai-voice'), 'cedar');
  page.type(page.$('#openai-key'), 'sk-typed-not-saved');
  page.type(page.$('#timing-follow_up_s'), '12,5');
  page.type(page.$('#assistant-instructions'), 'Новий текст.');
  page.type(page.$('#sound-eq2'), '-3');

  page.routes['GET api/device'] = EXPIRED;
  page.poll();
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(page.$('[data-login-note]').textContent, text(page, 'login.expired'));
  assert.equal(page.$('#openai-key').value, '');

  page.routes['GET api/device'] = reply(200, DEVICE);
  await logIn(page);
  assert.equal(page.visible(page.$('[data-screen="main"]')), true);
  assert.equal(page.$('#openai-voice').value, 'cedar');
  assert.equal(page.$('#timing-follow_up_s').value, '12,5');
  assert.equal(page.$('#assistant-instructions').value, 'Новий текст.');
  assert.equal(page.$('#sound-eq2').value, '-3');
  assert.equal(page.$('#openai-model').value, 'gpt-realtime-2.1-mini');
  assert.equal(page.$('#openai-key').value, '');
});

test('logging out on purpose leaves nothing typed behind', async () => {
  const page = await open();
  page.type(page.$('#openai-voice'), 'cedar');
  page.$('[data-action="logout"]').click();
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  await logIn(page);
  assert.equal(page.$('#openai-voice').value, 'marin');
});

test('settings that fail to load are reported, saving waits for them, and the page tries again', async () => {
  let answers = 0;
  const page = await open({
    'GET api/settings': () => {
      answers += 1;
      return answers === 1 ? reply(500, { error: 'internal', message: 'something went wrong' })
        : reply(200, { settings: SETTINGS });
    },
  });
  const banner = page.$('[data-banner="settings"]');
  assert.equal(page.visible(banner), true);
  const form = page.$('[data-form="openai"]');
  page.submit(form);
  await settle();
  assert.equal(form.querySelector('[data-status]').textContent, text(page, 'settings.not_loaded'));
  assert.equal(answers, 2);
  assert.equal(page.visible(banner), false);
  assert.equal(page.$('#openai-voice').value, 'marin');
  assert.equal(page.calls.filter((c) => c.method === 'PUT').length, 0);
});

test('the page keeps retrying the settings by itself on any tab', async () => {
  let answers = 0;
  const page = await open({
    'GET api/settings': () => {
      answers += 1;
      return answers < 3 ? 'network' : reply(200, { settings: SETTINGS });
    },
  }, { hash: '#timing' });
  assert.equal(page.visible(page.$('[data-banner="settings"]')), true);
  page.poll();
  await settle();
  assert.equal(page.visible(page.$('[data-banner="settings"]')), true);
  page.poll();
  await settle();
  assert.equal(answers, 3);
  assert.equal(page.visible(page.$('[data-banner="settings"]')), false);
  assert.equal(page.$('#timing-follow_up_s').value, '10');
  page.poll();
  await settle();
  assert.equal(answers, 3);
});

test('a failed logout is explained on the Access tab, wherever it was pressed', async () => {
  const page = await open({ 'POST api/logout': reply(500, { error: 'internal', message: 'something went wrong' }) });
  assert.equal(page.visible(page.$('[data-tab="conversation"]')), true);
  page.$('header [data-action="logout"]').click();
  await settle();
  assert.equal(page.window.location.hash, '#access');
  assert.equal(page.visible(page.$('[data-tab="access"]')), true);
  const status = page.$('[data-status="logout"]');
  assert.equal(page.visible(status), true);
  assert.equal(status.textContent, text(page, 'err.internal'));
});

test('“Enter the key” opens the Conversation tab first and then puts the cursor in the key field', async () => {
  const page = await open({
    'GET api/settings': reply(200, { settings: { ...SETTINGS, openai: { ...SETTINGS.openai, api_key_set: false } } }),
  }, { hash: '#sound' });
  assert.equal(page.visible(page.$('[data-tab="sound"]')), true);
  const link = page.$('[data-action="goto-key"]');
  assert.equal(page.visible(link), true);
  link.click();
  assert.equal(page.visible(page.$('[data-tab="conversation"]')), true);
  assert.equal(page.document.activeElement.id, 'openai-key');
  assert.equal(page.window.location.hash, '#conversation');
});

test('the key field asks password managers not to fill it', async () => {
  const page = await open();
  const input = page.$('#openai-key');
  assert.equal(input.getAttribute('type'), 'password');
  assert.equal(input.getAttribute('autocomplete'), 'off');
  assert.equal(input.hasAttribute('data-1p-ignore'), true);
  assert.equal(input.getAttribute('data-lpignore'), 'true');
  assert.equal(input.hasAttribute('data-bwignore'), true);
  assert.equal(input.getAttribute('data-form-type'), 'other');
});

test('after the login runs out during a save, logging back in shows no stale error', async () => {
  const page = await open({}, { hash: '#timing' });
  const form = page.$('[data-form="timing"]');
  page.type(page.$('#timing-follow_up_s'), '12');
  page.routes['PUT api/settings'] = EXPIRED;
  page.submit(form);
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  await logIn(page);
  assert.equal(page.visible(page.$('[data-screen="main"]')), true);
  assert.equal(page.$('#timing-follow_up_s').value, '12');
  assert.equal(statusOf(form).textContent, '');
  assert.equal(statusOf(form).hasAttribute('data-tone'), false);
  assert.equal(form.querySelector('button[type="submit"]').disabled, false);
});

test('removing the key keeps the other unsaved choices in the card', async () => {
  const after = { ...SETTINGS, openai: { ...SETTINGS.openai, api_key_set: false } };
  const bodies = [];
  const page = await open({
    'PUT api/settings': (body) => {
      bodies.push(body);
      return reply(200, { settings: after });
    },
  }, { hash: '#conversation' });
  page.type(page.$('#openai-voice'), 'cedar');
  page.type(page.$('#openai-model'), 'gpt-realtime-2.1');
  page.type(page.$('#openai-key'), 'sk-typed');
  page.$('[data-action="clear-key"]').click();
  page.$('[data-confirm-ok]').click();
  await settle();
  assert.deepEqual(bodies, [{ openai: { api_key: null } }]);
  assert.equal(page.$('#openai-voice').value, 'cedar');
  assert.equal(page.$('#openai-model').value, 'gpt-realtime-2.1');
  assert.equal(page.$('#openai-key').value, '');
  assert.equal(statusOf(page.$('[data-form="openai"]')).textContent, text(page, 'openai.key_cleared'));
  assert.equal(unsaved(page, 'openai'), true);
});

test('a save fills in only what it sent and keeps what was typed meanwhile', async () => {
  const gate = later();
  const page = await open({ 'PUT api/settings': () => gate.promise }, { hash: '#timing' });
  page.type(page.$('#timing-follow_up_s'), '12');
  page.submit(page.$('[data-form="timing"]'));
  await settle();
  page.type(page.$('#timing-first_turn_s'), '9');
  gate.done(reply(200, { settings: { ...SETTINGS, timing: { ...SETTINGS.timing, follow_up_s: 12 } } }));
  await settle();
  assert.deepEqual(puts(page)[0].body, { timing: { follow_up_s: 12 } });
  assert.equal(page.$('#timing-follow_up_s').value, '12');
  assert.equal(page.$('#timing-first_turn_s').value, '9');
  assert.equal(unsaved(page, 'timing'), true);
});

test('saving one block keeps the unsaved fields of the other blocks on the same tab', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, sound: { ...SETTINGS.sound, ...body.sound } },
    }),
  }, { hash: '#sound' });
  page.type(page.$('#sound-eq1'), '4');
  page.type(page.$('#wake-word'), 'okay_nabu');
  page.submit(page.$('[data-form="wake"]'));
  await settle();
  assert.deepEqual(puts(page)[0].body, { sound: { wake_word: 'okay_nabu' } });
  assert.equal(page.$('#sound-eq1').value, '4');
  assert.equal(page.$('#wake-word').value, 'okay_nabu');
  assert.equal(unsaved(page, 'sound'), true);
  assert.equal(unsaved(page, 'wake'), false);
});

test('picking another conversation language puts its default instructions in the field, and picking the saved one back restores the saved text', async () => {
  const page = await open({
    'GET api/instructions?language=en': reply(200, { instructions: 'Answer briefly.' }),
    'GET api/instructions?language=pl': reply(200, { instructions: 'Answer briefly. Answer in Polish.' }),
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, assistant: { ...SETTINGS.assistant, ...body.assistant } },
    }),
  }, { hash: '#conversation' });
  const form = page.$('[data-form="assistant"]');
  const area = page.$('#assistant-instructions');
  page.type(page.$('#assistant-language'), 'en');
  await settle();
  assert.equal(area.value, 'Answer briefly.');
  assert.equal(unsaved(page, 'assistant'), true);
  assert.equal(page.$('[data-language-note]').hidden, false);
  page.type(page.$('#assistant-language'), 'uk');
  await settle();
  assert.equal(area.value, 'Відповідай коротко.');
  page.type(page.$('#assistant-language'), 'pl');
  await settle();
  assert.equal(area.value, 'Answer briefly. Answer in Polish.');
  page.submit(form);
  await settle();
  assert.deepEqual(puts(page)[0].body, { assistant: { language: 'pl', instructions: 'Answer briefly. Answer in Polish.' } });
  assert.equal(area.value, 'Answer briefly. Answer in Polish.');
  assert.equal(unsaved(page, 'assistant'), false);
  assert.equal(page.$('[data-language-note]').hidden, true);
  assert.equal(statusOf(form).textContent, text(page, 'assistant.language_switched'));
});

test('each card says when it has unsaved changes, and the mark goes after saving', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, assistant: { ...SETTINGS.assistant, ...body.assistant } },
    }),
  }, { hash: '#conversation' });
  for (const name of ['openai', 'assistant', 'timing', 'sound', 'wake']) {
    const mark = page.$(`[data-form="${name}"] .form-foot [data-unsaved]`);
    assert.ok(mark, name);
    assert.equal(mark.textContent, text(page, 'form.unsaved'), name);
    assert.equal(mark.hidden, true, name);
  }
  page.type(page.$('#openai-key'), 'sk-proj-abc');
  assert.equal(unsaved(page, 'openai'), true);
  assert.equal(unsaved(page, 'assistant'), false);
  page.type(page.$('#assistant-instructions'), 'Відповідай дуже коротко.');
  assert.equal(unsaved(page, 'assistant'), true);
  page.submit(page.$('[data-form="assistant"]'));
  await settle();
  assert.equal(unsaved(page, 'assistant'), false);
  assert.equal(unsaved(page, 'openai'), true);
  page.type(page.$('#openai-key'), '');
  assert.equal(unsaved(page, 'openai'), false);

  page.type(page.$('#timing-follow_up_s'), '12');
  assert.equal(unsaved(page, 'timing'), true);
  page.type(page.$('#timing-follow_up_s'), '10');
  assert.equal(unsaved(page, 'timing'), false);
  page.type(page.$('#timing-follow_up_s'), '12');
  page.$('[data-action="timing-defaults"]').click();
  assert.equal(unsaved(page, 'timing'), false);

  page.type(page.$('#sound-mic_gain_db'), '14');
  assert.equal(unsaved(page, 'sound'), true);
  page.type(page.$('#sound-mic_gain_db'), '12');
  assert.equal(unsaved(page, 'sound'), false);
  page.type(page.$('#sound-eq0'), '2');
  assert.equal(unsaved(page, 'sound'), true);
  page.$('[data-action="sound-defaults"]').click();
  assert.equal(unsaved(page, 'sound'), false);
  page.type(page.$('#wake-word'), 'hey_mycroft');
  assert.equal(unsaved(page, 'wake'), true);
});

test('leaving the page asks first only while a card has unsaved changes', async () => {
  const page = await open({}, { hash: '#timing' });
  assert.equal(page.leave().defaultPrevented, false);
  page.type(page.$('#timing-follow_up_s'), '12');
  const asked = page.leave();
  assert.equal(asked.defaultPrevented, true);
  assert.equal(asked.returnValue, true);
  page.type(page.$('#timing-follow_up_s'), '10');
  assert.equal(page.leave().defaultPrevented, false);

  page.type(page.$('#sound-eq3'), '2');
  assert.equal(page.leave().defaultPrevented, true);
  page.type(page.$('#sound-eq3'), '0');

  page.type(page.$('#timing-follow_up_s'), '12');
  page.routes['GET api/settings'] = EXPIRED;
  page.$('[data-action="reload-settings"]').click();
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(page.leave().defaultPrevented, false);

  page.routes['GET api/settings'] = reply(200, { settings: SETTINGS });
  await logIn(page);
  assert.equal(page.$('#timing-follow_up_s').value, '12');
  assert.equal(page.leave().defaultPrevented, true);
  page.$('header [data-action="logout"]').click();
  await settle();
  assert.equal(page.leave().defaultPrevented, false);
});

test('restoring defaults and removing the key wait until the settings have loaded', async () => {
  let answers = 0;
  const page = await open({
    'GET api/settings': () => {
      answers += 1;
      return answers === 1 ? reply(500, { error: 'internal', message: 'x' }) : reply(200, { settings: SETTINGS });
    },
  }, { hash: '#conversation' });
  const buttons = ['reset-instructions', 'timing-defaults', 'sound-defaults', 'clear-key']
    .map((name) => page.$(`[data-action="${name}"]`));
  for (const button of buttons) assert.equal(button.disabled, true, button.getAttribute('data-action'));
  assert.equal(page.$('#volume').disabled, true);
  buttons[0].click();
  assert.notEqual(page.$('[data-confirm]').open, true);
  assert.equal(page.calls.filter((c) => c.method === 'PUT').length, 0);
  page.poll();
  await settle();
  assert.equal(answers, 2);
  for (const button of buttons) assert.equal(button.disabled, false, button.getAttribute('data-action'));
  assert.equal(page.$('#volume').disabled, false);
});

test('logging out during a save leaves the page usable after logging back in', async () => {
  const gate = later();
  const page = await open({ 'PUT api/settings': () => gate.promise }, { hash: '#conversation' });
  const form = page.$('[data-form="assistant"]');
  page.type(page.$('#assistant-language'), 'en');
  page.submit(form);
  await settle();
  page.$('header [data-action="logout"]').click();
  await settle();
  gate.done(reply(200, { settings: { ...SETTINGS, assistant: { language: 'en', instructions: 'X' } } }));
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(page.$('#assistant-instructions').value, '');
  await logIn(page);
  assert.equal(form.querySelector('button[type="submit"]').disabled, false);
  assert.equal(form.querySelector('[data-action="reset-instructions"]').disabled, false);
  assert.equal(statusOf(form).textContent, '');
  assert.equal(page.$('#assistant-instructions').value, SETTINGS.assistant.instructions);
});

test('the no-connection banner goes away on any tab once the speaker answers', async () => {
  const page = await open({}, { hash: '#about' });
  page.routes['GET api/device'] = 'network';
  page.poll();
  await settle();
  const banner = page.$('[data-banner="offline"]');
  assert.equal(page.visible(banner), true);
  page.$('[data-tab-link="timing"]').click();
  await settle();
  page.poll();
  await settle();
  assert.equal(page.visible(banner), true);
  page.routes['GET api/device'] = reply(200, DEVICE);
  page.poll();
  await settle();
  assert.equal(page.visible(banner), false);
  const polled = page.calls.length;
  page.poll();
  await settle();
  assert.equal(page.calls.length, polled);
});

test('any answer from the speaker clears the no-connection banner', async () => {
  const page = await open({ 'PUT api/settings': reply(200, { settings: SETTINGS }) }, { hash: '#about' });
  page.routes['GET api/device'] = 'network';
  page.poll();
  await settle();
  assert.equal(page.visible(page.$('[data-banner="offline"]')), true);
  page.$('[data-tab-link="timing"]').click();
  await settle();
  page.type(page.$('#timing-follow_up_s'), '12');
  page.submit(page.$('[data-form="timing"]'));
  await settle();
  assert.equal(page.visible(page.$('[data-banner="offline"]')), false);
});

test('the settings keep loading after a script error on the way', async () => {
  let answers = 0;
  const page = await open({
    'GET api/settings': () => {
      answers += 1;
      return answers === 1 ? reply(200, {}) : reply(200, { settings: SETTINGS });
    },
  }, { hash: '#timing' });
  assert.equal(answers, 1);
  page.poll();
  await settle();
  assert.equal(answers, 2);
  assert.equal(page.$('#timing-follow_up_s').value, '10');
  assert.equal(page.visible(page.$('[data-banner="settings"]')), false);
});

test('the address carries the page language, and switching it changes the address', async () => {
  const page = await open({}, { search: '?lang=en', hash: '#conversation' });
  assert.equal(page.document.documentElement.getAttribute('lang'), 'en');
  assert.equal(page.window.location.search, '?lang=en');
  page.$('[data-set-lang="uk"]').click();
  assert.equal(page.window.location.search, '?lang=uk');
  assert.equal(page.window.location.hash, '#conversation');
  assert.equal(page.document.documentElement.getAttribute('lang'), 'uk');
});

test('an address without a language gets the one the page picked', async () => {
  const page = await open({}, { languages: ['en-US'] });
  assert.equal(page.window.location.search, '?lang=en');
});

test('switching the page language never touches the conversation language', async () => {
  const page = await open({}, { search: '?lang=uk' });
  page.$('[data-set-lang="en"]').click();
  await settle();
  assert.equal(page.$('#assistant-language').value, 'uk');
  assert.ok(!page.calls.some((call) => call.method === 'PUT'));
});

test('switching the page language rewrites the labels, hints and numbers of the generated fields', async () => {
  const page = await open({}, { search: '?lang=uk', hash: '#sound' });
  assert.equal(page.$('#sound-mic_gain_db').value, '12');
  assert.equal(page.$('[data-eq-label="1500"]').textContent, '1500 Гц');
  assert.match(page.$('[data-sound-hint="low_cut_hz"]').textContent, /Типово: 200 Гц\.$/);
  page.$('[data-set-lang="en"]').click();
  assert.equal(page.$('[data-eq-label="1500"]').textContent, '1500 Hz');
  assert.match(page.$('[data-sound-hint="low_cut_hz"]').textContent, /Default: 200 Hz\.$/);
  assert.equal(page.$('label[for="sound-mic_gain_db"]').textContent, 'Microphone gain');
  assert.equal(page.$('#timing-mic_holdoff_s').value, '0.4');
});

test('the equalizer has five fields with their frequencies as names', async () => {
  const page = await open({}, { hash: '#sound' });
  const labels = page.$$('[data-sound-eq] label').map((label) => label.textContent);
  assert.deepEqual(labels, ['250 Гц', '600 Гц', '1500 Гц', '3500 Гц', '8000 Гц']);
  const inputs = page.$$('[data-sound-eq] input');
  assert.equal(inputs.length, 5);
  for (const input of inputs) assert.equal(input.value, '0');
});

test('creating the password gives the speaker the page language for the conversation', async () => {
  const fresh = { password_set: false, logged_in: false };
  const page = await open({ 'GET api/state': reply(200, fresh), 'POST api/setup': reply(200, IN) }, { search: '?lang=en' });
  assert.ok(page.visible(page.$('[data-screen="setup"]')));
  assert.ok(page.visible(page.$('[data-setup-language]')));
  assert.match(page.$('[data-setup-language]').textContent, /English/);
  page.type(page.$('#setup-password'), 'correct horse battery');
  page.type(page.$('#setup-repeat'), 'correct horse battery');
  page.submit(page.$('[data-form="setup"]'));
  await settle();
  const setup = page.calls.find((call) => call.path === 'api/setup');
  assert.deepEqual(setup.body, { password: 'correct horse battery', language: 'en' });
  assert.ok(page.visible(page.$('[data-screen="main"]')));
});

test('a password that is too short, too long or not repeated is refused before sending', async () => {
  const fresh = { password_set: false, logged_in: false };
  const page = await open({ 'GET api/state': reply(200, fresh), 'POST api/setup': reply(200, IN) });
  const form = page.$('[data-form="setup"]');
  page.type(page.$('#setup-password'), 'short');
  page.type(page.$('#setup-repeat'), 'short');
  page.submit(form);
  await settle();
  assert.equal(page.$('#setup-password').getAttribute('aria-invalid'), 'true');
  assert.equal(statusOf(form).textContent, text(page, 'form.check'));
  const long = 'я'.repeat(129);
  page.type(page.$('#setup-password'), long);
  page.type(page.$('#setup-repeat'), long);
  page.submit(form);
  await settle();
  assert.equal(page.$('.field-error').textContent, text(page, 'password.long'));
  page.type(page.$('#setup-password'), 'correct horse battery');
  page.type(page.$('#setup-repeat'), 'correct horse');
  page.submit(form);
  await settle();
  assert.equal(page.calls.filter((c) => c.method === 'POST').length, 0);
});

test('restoring the default instructions shows the text the speaker chose and leaves nothing unsaved', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, assistant: { language: body.assistant.language, instructions: 'Типові інструкції.' } },
    }),
  }, { hash: '#conversation' });
  const form = page.$('[data-form="assistant"]');
  page.$('[data-action="reset-instructions"]').click();
  await settle();
  const confirm = page.$('[data-confirm]');
  if (confirm && confirm.open) {
    page.$('[data-confirm] [data-confirm-ok]').click();
    await settle();
  }
  assert.deepEqual(puts(page)[0].body, { assistant: { language: 'uk', instructions: null } });
  assert.equal(page.$('#assistant-instructions').value, 'Типові інструкції.');
  assert.equal(form.querySelector('[data-unsaved]').hidden, true);
  assert.equal(page.leave().defaultPrevented, false);
});

test('a wrong host name shows the screen that explains which addresses work', async () => {
  const page = await open({ 'GET api/state': reply(403, { error: 'foreign_host', message: 'this server name is not accepted' }) },
    { host: 'evil.example' });
  assert.equal(page.visible(page.$('[data-screen="host"]')), true);
  assert.match(page.$('[data-host-opened]').textContent, /evil\.example/);
  assert.match(page.$('[data-screen="host"]').textContent, /\.local/);
});

test('a wrong password on login is explained on the field', async () => {
  const page = await open({
    'GET api/state': reply(200, OUT),
    'POST api/login': reply(401, { error: 'wrong_password', message: 'wrong password' }),
  });
  await logIn(page);
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(statusOf(page.$('[data-form="login"]')).textContent, text(page, 'err.wrong_password'));
  assert.equal(page.$('#login-password').value, '');
});

test('changing the password: a wrong current one is named, a good change keeps the page', async () => {
  const wrong = reply(403, { error: 'wrong_password', message: 'the current password is wrong' });
  const page = await open({ 'POST api/password': wrong }, { hash: '#access' });
  const form = page.$('[data-form="password"]');
  page.type(page.$('#password-current'), 'old password');
  page.type(page.$('#password-new'), 'new password 1');
  page.type(page.$('#password-repeat'), 'new password 1');
  page.submit(form);
  await settle();
  assert.equal(statusOf(form).textContent, text(page, 'err.wrong_current'));
  assert.equal(page.$('#password-current').getAttribute('aria-invalid'), 'true');
  page.routes['POST api/password'] = reply(200, IN);
  page.type(page.$('#password-current'), 'old password');
  page.submit(form);
  await settle();
  assert.equal(statusOf(form).textContent, text(page, 'password.changed'));
  assert.deepEqual(page.calls.filter((c) => c.path === 'api/password').pop().body, { current: 'old password', new: 'new password 1' });
});

test('no devices section: the page has no speaker list, discovery, adoption, address or port', async () => {
  const page = await open({}, { hash: '#about' });
  page.poll();
  await settle();
  const html = page.document.documentElement;
  for (const selector of ['[data-speaker-list]', '[data-found-list]', '[data-form="manual"]', '[data-form="server"]',
    '[data-progress]', '#server-port', '#manual-host', '[data-tab="speakers"]', '[data-tab-link="speakers"]']) {
    assert.equal(html.querySelector(selector), null, selector);
  }
  assert.deepEqual(page.$$('[data-tab-link]').map((a) => a.getAttribute('data-tab-link')),
    ['conversation', 'sound', 'timing', 'log', 'about', 'access']);
  const paths = new Set(page.calls.map((c) => c.path.split('?')[0]));
  for (const path of paths) assert.ok(!/devices|discover|adopt|wake-word/.test(path), path);
});

test('the Sound tab shows the saved values, and the volume slider starts from the saved volume', async () => {
  const page = await open({}, { hash: '#sound' });
  assert.equal(page.$('#sound-mic_gain_db').value, '12');
  assert.equal(page.$('#sound-low_cut_hz').value, '200');
  assert.equal(page.$('#sound-loudness_gain_db').value, '6');
  assert.equal(page.$('#wake-word').value, 'hey_jarvis');
  assert.equal(page.$('#volume').value, '78');
  assert.equal(page.$('[data-volume-value]').textContent, '78\u00a0%');
  assert.equal(page.$('#volume').getAttribute('aria-valuetext'), '78\u00a0%');
  assert.equal(page.$('label[for="volume"]').textContent, 'Гучність відповіді');
  page.$('[data-set-lang="en"]').click();
  assert.equal(page.$('label[for="volume"]').textContent, 'Answer volume');
  assert.equal(page.$('[data-volume-value]').textContent, '78\u00a0%');
});

test('sound block saves volume and eq', async () => {
  const page = await open({
    'PUT api/volume': (body) => reply(200, { volume: body.volume }),
    'PUT api/settings': (body) => reply(200, { settings: { ...SETTINGS, sound: { ...SETTINGS.sound, volume: 0.65, ...body.sound } } }),
  }, { hash: '#sound' });
  const slider = page.$('#volume');
  page.type(slider, '65');
  page.fire(slider, 'change');
  await settle();
  assert.deepEqual(page.calls.find((c) => c.path === 'api/volume').body, { volume: 0.65 });

  page.type(page.$('#sound-mic_gain_db'), '16,5');
  page.type(page.$('#sound-eq0'), '2');
  page.type(page.$('#sound-eq4'), '-1,5');
  page.submit(page.$('[data-form="sound"]'));
  await settle();
  assert.deepEqual(puts(page)[0].body, { sound: { mic_gain_db: 16.5, eq_db: [2, 0, 0, 0, -1.5] } });
  assert.equal(page.$('#sound-mic_gain_db').value, '16,5');
  assert.equal(page.$('#sound-eq4').value, '-1,5');
  assert.equal(unsaved(page, 'sound'), false);
  assert.equal(statusOf(page.$('[data-form="sound"]')).textContent, text(page, 'saved_plain'));
  assert.equal(slider.value, '65');
});

test('the sound block refuses values out of range on the field and sends nothing', async () => {
  const page = await open({}, { hash: '#sound' });
  const form = page.$('[data-form="sound"]');
  page.type(page.$('#sound-low_cut_hz'), '30');
  page.type(page.$('#sound-eq2'), '13');
  page.submit(form);
  await settle();
  assert.equal(page.$('#sound-low_cut_hz').getAttribute('aria-invalid'), 'true');
  assert.equal(page.$('#sound-eq2').getAttribute('aria-invalid'), 'true');
  assert.equal(page.$('#sound-eq2').closest('.field').querySelector('.field-error').textContent,
    'Потрібне число від -12 до 12.');
  assert.equal(statusOf(form).textContent, text(page, 'form.check'));
  assert.equal(puts(page).length, 0);
});

test('a refused equalizer from the speaker is shown on the first band', async () => {
  const page = await open({ 'PUT api/settings': reply(400, { error: 'invalid', message: 'sound.eq_db: invalid value' }) }, { hash: '#sound' });
  page.type(page.$('#sound-eq3'), '5');
  const form = page.$('[data-form="sound"]');
  page.submit(form);
  await settle();
  assert.equal(statusOf(form).textContent, text(page, 'field.message', {
    field: text(page, 'sound.eq.title'), rule: text(page, 'rule.invalid_value'),
  }));
  assert.equal(page.$('#sound-eq0').getAttribute('aria-invalid'), 'true');
});

test('the sound defaults fill in the fields and wait for Save', async () => {
  const page = await open({}, { hash: '#sound' });
  page.type(page.$('#sound-mic_gain_db'), '20');
  page.type(page.$('#sound-eq1'), '5');
  page.$('[data-action="sound-defaults"]').click();
  assert.equal(page.$('#sound-mic_gain_db').value, '12');
  assert.equal(page.$('#sound-low_cut_hz').value, '200');
  assert.equal(page.$('#sound-eq1').value, '0');
  assert.equal(statusOf(page.$('[data-form="sound"]')).textContent, text(page, 'form.defaults_done'));
  assert.equal(puts(page).length, 0);
});

test('wake word choice saves', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, { settings: { ...SETTINGS, sound: { ...SETTINGS.sound, ...body.sound } } }),
  }, { hash: '#sound' });
  const form = page.$('[data-form="wake"]');
  page.submit(form);
  await settle();
  assert.equal(puts(page).length, 0);
  assert.equal(statusOf(form).textContent, text(page, 'nothing'));
  page.type(page.$('#wake-word'), 'hey_mycroft');
  assert.equal(unsaved(page, 'wake'), true);
  page.submit(form);
  await settle();
  assert.deepEqual(puts(page)[0].body, { sound: { wake_word: 'hey_mycroft' } });
  assert.equal(page.$('#wake-word').value, 'hey_mycroft');
  assert.equal(unsaved(page, 'wake'), false);
  assert.equal(statusOf(form).textContent, text(page, 'saved_plain'));
});

test('a wake word the speaker refuses is explained', async () => {
  const page = await open({ 'PUT api/settings': reply(400, { error: 'invalid', message: 'sound.wake_word: invalid value' }) }, { hash: '#sound' });
  page.type(page.$('#wake-word'), 'okay_nabu');
  const form = page.$('[data-form="wake"]');
  page.submit(form);
  await settle();
  assert.equal(statusOf(form).textContent, text(page, 'field.message', {
    field: text(page, 'wake.title'), rule: text(page, 'rule.invalid_value'),
  }));
  assert.equal(page.$('#wake-word').getAttribute('aria-invalid'), 'true');
});

function volumePuts(page) {
  return page.calls.filter((c) => c.method === 'PUT' && c.path === 'api/volume').map((c) => c.body);
}

function release(page, slider, value) {
  page.type(slider, value);
  page.fire(slider, 'change');
}

function answerVolume(body) {
  return reply(200, { volume: body.volume });
}

test('the label follows the slider while dragging, and the volume is sent once on release', async () => {
  const page = await open({ 'PUT api/volume': answerVolume }, { hash: '#sound' });
  const slider = page.$('#volume');
  const label = page.$('[data-volume-value]');
  page.type(slider, '60');
  assert.equal(label.textContent, '60\u00a0%');
  assert.equal(slider.getAttribute('aria-valuetext'), '60\u00a0%');
  page.type(slider, '65');
  assert.equal(label.textContent, '65\u00a0%');
  await settle();
  assert.deepEqual(volumePuts(page), []);
  page.fire(slider, 'change');
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 0.65 }]);
  assert.equal(slider.value, '65');
  assert.equal(page.$('[data-status="volume"]').textContent, '');
  page.fire(slider, 'change');
  await settle();
  assert.equal(volumePuts(page).length, 1);
});

test('polling leaves the slider alone while it is held, and shows a change made by voice after that', async () => {
  const page = await open({ 'PUT api/volume': answerVolume }, { hash: '#sound' });
  const slider = page.$('#volume');
  page.type(slider, '40');
  page.routes['GET api/device'] = reply(200, { ...DEVICE, volume: 0.55 });
  page.poll();
  await settle();
  assert.equal(slider.value, '40');
  assert.equal(page.$('[data-volume-value]').textContent, '40\u00a0%');
  page.fire(slider, 'change');
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 0.4 }]);
  page.poll();
  await settle();
  assert.equal(slider.value, '55');
  assert.equal(page.$('[data-volume-value]').textContent, '55\u00a0%');
});

test('the volume slider is kept current from the device reading, not from the full settings', async () => {
  const page = await open({}, { hash: '#sound' });
  const settingsReads = () => page.calls.filter((c) => c.method === 'GET' && c.path === 'api/settings').length;
  const deviceReads = () => page.calls.filter((c) => c.path === 'api/device').length;
  const before = settingsReads();
  const reads = deviceReads();
  page.routes['GET api/device'] = reply(200, { ...DEVICE, volume: 0.31 });
  page.poll();
  await settle();
  page.poll();
  await settle();
  assert.equal(page.$('#volume').value, '31');
  assert.equal(deviceReads(), reads + 2);
  assert.equal(settingsReads(), before);
  page.routes['GET api/device'] = reply(200, DEVICE);
  page.poll();
  await settle();
  assert.equal(page.$('#volume').value, '31');
});

test('letting go of the slider without a change lets polling move it again', async () => {
  const page = await open({}, { hash: '#sound' });
  const slider = page.$('#volume');
  page.type(slider, '80');
  page.fire(slider, 'pointerup');
  page.routes['GET api/device'] = reply(200, { ...DEVICE, volume: 0.5 });
  page.poll();
  await settle();
  assert.equal(slider.value, '50');
  assert.equal(page.$('[data-volume-value]').textContent, '50\u00a0%');
  assert.deepEqual(volumePuts(page), []);
});

test('a volume chosen while a save is on its way is sent after it, and only the latest one', async () => {
  const gates = [];
  const page = await open({
    'PUT api/volume': () => {
      const gate = later();
      gates.push(gate);
      return gate.promise;
    },
  }, { hash: '#sound' });
  const slider = page.$('#volume');
  release(page, slider, '30');
  await settle();
  page.poll();
  await settle();
  assert.equal(slider.value, '30');
  release(page, slider, '40');
  release(page, slider, '50');
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 0.3 }]);
  gates[0].done(reply(200, { volume: 0.3 }));
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 0.3 }, { volume: 0.5 }]);
  assert.equal(slider.value, '50');
  assert.equal(page.$('[data-volume-value]').textContent, '50\u00a0%');
  gates[1].done(reply(200, { volume: 0.5 }));
  await settle();
  assert.equal(slider.value, '50');
  assert.equal(volumePuts(page).length, 2);
});

test('a volume the speaker did not take is explained, and the slider goes back', async () => {
  const page = await open({ 'PUT api/volume': reply(500, { error: 'save_failed', message: 'the settings could not be saved' }) }, { hash: '#sound' });
  const slider = page.$('#volume');
  const status = page.$('[data-status="volume"]');
  release(page, slider, '30');
  await settle();
  assert.equal(status.textContent.startsWith(text(page, 'err.save_failed')), true);
  assert.equal(status.getAttribute('data-tone'), 'error');
  assert.equal(slider.value, '78');
  assert.equal(page.$('[data-volume-value]').textContent, '78\u00a0%');
  page.routes['PUT api/volume'] = answerVolume;
  release(page, slider, '35');
  await settle();
  assert.equal(status.textContent, '');
  assert.equal(page.$('[data-volume-value]').textContent, '35\u00a0%');
});

test('a volume change survives a settings save in the same tab', async () => {
  const page = await open({
    'PUT api/volume': answerVolume,
    'PUT api/settings': (body) => reply(200, { settings: { ...SETTINGS, sound: { ...SETTINGS.sound, ...body.sound, volume: 0.35 } } }),
  }, { hash: '#sound' });
  release(page, page.$('#volume'), '35');
  await settle();
  page.type(page.$('#sound-eq0'), '1');
  page.submit(page.$('[data-form="sound"]'));
  await settle();
  assert.equal(page.$('#volume').value, '35');
  assert.equal(page.$('[data-volume-value]').textContent, '35\u00a0%');
});

test('the About tab shows the speaker, its version, address and the signal in plain words', async () => {
  const page = await open({}, { hash: '#about' });
  assert.equal(page.$('[data-about="name"]').textContent, 'pytaichenko-a1b2c3');
  assert.equal(page.$('[data-about="version"]').textContent, '2.0.0');
  assert.equal(page.$('[data-about="ip"]').textContent, '192.0.2.40');
  assert.equal(page.$('[data-about="signal"]').textContent, 'добрий, -62 дБм');
  page.$('[data-set-lang="en"]').click();
  assert.equal(page.$('[data-about="signal"]').textContent, 'good, -62 dBm');
  page.routes['GET api/device'] = reply(200, { ...DEVICE, ip: '', rssi: 0 });
  page.poll();
  await settle();
  assert.equal(page.$('[data-about="ip"]').textContent, '—');
  assert.equal(page.$('[data-about="signal"]').textContent, '—');
});

test('the About tab is read when it is opened, and again by polling while it stays open', async () => {
  const page = await open();
  const reads = () => page.calls.filter((c) => c.path === 'api/device').length;
  assert.equal(reads(), 0);
  page.$('[data-tab-link="about"]').click();
  await settle();
  assert.equal(reads(), 1);
  page.poll();
  await settle();
  assert.equal(reads(), 2);
  page.$('[data-tab-link="timing"]').click();
  await settle();
  page.poll();
  await settle();
  assert.equal(reads(), 2);
});

function logLines(from, to) {
  const lines = [];
  for (let seq = from; seq <= to; seq += 1) lines.push({ seq, t: seq * 10, text: `line ${seq}` });
  return lines;
}

function logItems(page) {
  return page.$$('[data-log-list] li').map((li) => [li.querySelector('.log-time').textContent, li.querySelector('.log-text').textContent]);
}

function logCalls(page) {
  return page.calls.filter((c) => c.path.startsWith('api/log')).map((c) => c.path);
}

test('log polls after last seq', async () => {
  const page = await open({
    'GET api/log?after=0': reply(200, { lines: [{ seq: 1, t: 5, text: 'ready' }, { seq: 2, t: 75, text: 'heard on kitchen: <b>привіт</b>' }] }),
    'GET api/log?after=2': reply(200, { lines: [{ seq: 3, t: 3725, text: 'ended by model' }] }),
    'GET api/log?after=3': reply(200, { lines: [] }),
  }, { hash: '#log' });
  assert.deepEqual(logCalls(page), ['api/log?after=0']);
  assert.deepEqual(logItems(page), [['0:00:05', 'ready'], ['0:01:15', 'heard on kitchen: <b>привіт</b>']]);
  assert.equal(page.$('[data-log-list] li').querySelectorAll('b').length, 0);
  assert.equal(page.visible(page.$('[data-log-box]')), true);
  assert.equal(page.visible(page.$('[data-log-empty]')), false);
  page.poll();
  await settle();
  assert.deepEqual(logCalls(page), ['api/log?after=0', 'api/log?after=2']);
  assert.equal(logItems(page).length, 3);
  assert.deepEqual(logItems(page)[2], ['1:02:05', 'ended by model']);
  page.poll();
  await settle();
  assert.deepEqual(logCalls(page), ['api/log?after=0', 'api/log?after=2', 'api/log?after=3']);
  assert.equal(logItems(page).length, 3);
});

test('an empty log says so, and a line the page already has is not added twice', async () => {
  const page = await open({
    'GET api/log?after=0': reply(200, { lines: [] }),
  }, { hash: '#log' });
  assert.equal(page.visible(page.$('[data-log-empty]')), true);
  assert.equal(page.visible(page.$('[data-log-box]')), false);
  page.routes['GET api/log?after=0'] = reply(200, { lines: logLines(1, 2) });
  page.poll();
  await settle();
  assert.equal(logItems(page).length, 2);
  page.routes['GET api/log?after=2'] = reply(200, { lines: logLines(1, 3) });
  page.poll();
  await settle();
  assert.deepEqual(logItems(page).map((item) => item[1]), ['line 1', 'line 2', 'line 3']);
});

test('the log keeps the latest four hundred lines', async () => {
  const page = await open({ 'GET api/log?after=0': reply(200, { lines: logLines(1, 450) }) }, { hash: '#log' });
  const items = logItems(page);
  assert.equal(items.length, 400);
  assert.equal(items[0][1], 'line 51');
  assert.equal(items[399][1], 'line 450');
});

test('the log is read only while its tab is open, and is wiped at logout', async () => {
  const page = await open({ 'GET api/log?after=0': reply(200, { lines: logLines(1, 2) }) });
  page.poll();
  await settle();
  assert.deepEqual(logCalls(page), []);
  page.$('[data-tab-link="log"]').click();
  await settle();
  assert.deepEqual(logCalls(page), ['api/log?after=0']);
  assert.equal(logItems(page).length, 2);
  page.$('header [data-action="logout"]').click();
  await settle();
  assert.equal(logItems(page).length, 0);
  assert.equal(page.$('[data-log-empty]').hidden, false);
  assert.equal(page.$('[data-log-box]').hidden, true);
  page.routes['GET api/state'] = reply(200, OUT);
  page.$('[data-tab-link="timing"]').click();
  await logIn(page);
  page.$('[data-tab-link="log"]').click();
  await settle();
  assert.equal(logCalls(page).pop(), 'api/log?after=0');
});

test('a login that runs out while the log is open sends the person to the login screen', async () => {
  const page = await open({}, { hash: '#log' });
  page.routes['GET api/log?after=0'] = EXPIRED;
  page.poll();
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
});

function checkPosts(page) {
  return page.calls.filter((c) => c.method === 'POST' && c.path === 'api/openai/test');
}

test('test openai check shows result', async () => {
  const page = await open({
    'POST api/openai/test': reply(200, { ok: true, model: 'gpt-realtime-2.1-mini', voice: 'marin' }),
  }, { hash: '#conversation' });
  const status = page.$('[data-status="openai-check"]');
  assert.equal(status.textContent, '');
  page.$('[data-action="check-openai"]').click();
  await settle();
  assert.deepEqual(checkPosts(page).map((c) => c.body), [{}]);
  assert.equal(status.textContent, text(page, 'openai.check_ok', { model: 'gpt-realtime-2.1-mini', voice: 'marin' }));
  assert.match(status.textContent, /gpt-realtime-2\.1-mini/);
  assert.match(status.textContent, /marin/);
  assert.equal(status.getAttribute('data-tone'), 'ok');
  assert.equal(page.$('[data-action="check-openai"]').disabled, false);
});

test('test openai check shows bad key text', async () => {
  const page = await open({
    'POST api/openai/test': reply(502, { error: 'openai_bad_key', message: 'OpenAI refused the key' }),
  }, { hash: '#conversation' });
  page.$('[data-action="check-openai"]').click();
  await settle();
  const status = page.$('[data-status="openai-check"]');
  assert.equal(status.textContent, text(page, 'err.openai_bad_key'));
  assert.equal(status.getAttribute('data-tone'), 'error');
  assert.equal(page.$('[data-action="check-openai"]').disabled, false);
  page.window.PYTAICHENKO_PAGE.LANGS.forEach((lang) => {
    for (const code of ['openai_bad_key', 'openai_unreachable', 'openai_timeout', 'openai_rejected', 'openai_no_key', 'busy']) {
      assert.ok(page.window.PYTAICHENKO_PAGE.t(lang, `err.${code}`).split(' ').length >= 4, `${lang} ${code}`);
    }
  });
});

test('the check explains unreachable, timeout and busy answers', async () => {
  const page = await open({}, { hash: '#conversation' });
  const status = page.$('[data-status="openai-check"]');
  for (const [code, code_status] of [['openai_unreachable', 502], ['openai_timeout', 504], ['openai_rejected', 502], ['busy', 409]]) {
    page.routes['POST api/openai/test'] = reply(code_status, { error: code, message: 'x' });
    page.$('[data-action="check-openai"]').click();
    await settle();
    assert.equal(status.textContent, text(page, `err.${code}`), code);
    assert.equal(status.getAttribute('data-tone'), 'error');
  }
  page.routes['POST api/openai/test'] = 'network';
  page.$('[data-action="check-openai"]').click();
  await settle();
  assert.equal(status.textContent, text(page, 'err.network'));
});

test('the check shows the code OpenAI gave when it did not accept the request', async () => {
  const page = await open({
    'POST api/openai/test': reply(502, { error: 'openai_rejected', message: 'OpenAI did not accept the request', openai_code: 'model_not_found' }),
  }, { hash: '#conversation' });
  page.$('[data-action="check-openai"]').click();
  await settle();
  const status = page.$('[data-status="openai-check"]');
  assert.equal(status.getAttribute('data-tone'), 'error');
  assert.ok(status.textContent.startsWith(text(page, 'err.openai_rejected')));
  assert.equal(page.$('[data-status="openai-check"] .status-detail').textContent, 'model_not_found');
});

test('the check waits visibly, cannot be started twice, and gives way to a language switch', async () => {
  const gate = later();
  const page = await open({ 'POST api/openai/test': () => gate.promise }, { hash: '#conversation' });
  const button = page.$('[data-action="check-openai"]');
  const status = page.$('[data-status="openai-check"]');
  button.click();
  assert.equal(button.disabled, true);
  assert.equal(status.textContent, text(page, 'openai.check_wait'));
  assert.equal(status.getAttribute('data-tone'), 'wait');
  button.click();
  page.$('[data-set-lang="en"]').click();
  assert.equal(status.textContent, page.window.PYTAICHENKO_PAGE.t('en', 'openai.check_wait'));
  gate.done(reply(200, { ok: true, model: 'm', voice: 'v' }));
  await settle();
  assert.equal(checkPosts(page).length, 1);
  assert.equal(button.disabled, false);
  assert.equal(status.textContent, page.window.PYTAICHENKO_PAGE.t('en', 'openai.check_ok', { model: 'm', voice: 'v' }));
});

test('without a saved key the check asks for one and sends nothing', async () => {
  const unset = { ...SETTINGS, openai: { ...SETTINGS.openai, api_key_set: false } };
  const page = await open({ 'GET api/settings': reply(200, { settings: unset }) }, { hash: '#conversation' });
  page.$('[data-action="check-openai"]').click();
  await settle();
  assert.equal(checkPosts(page).length, 0);
  const status = page.$('[data-status="openai-check"]');
  assert.equal(status.textContent, text(page, 'err.openai_no_key'));
  assert.equal(status.getAttribute('data-tone'), 'error');
});

test('the check result is wiped at logout', async () => {
  const page = await open({
    'POST api/openai/test': reply(200, { ok: true, model: 'm', voice: 'v' }),
  }, { hash: '#conversation' });
  page.$('[data-action="check-openai"]').click();
  await settle();
  assert.notEqual(page.$('[data-status="openai-check"]').textContent, '');
  page.$('[data-action="logout"]').click();
  await settle();
  assert.equal(page.$('[data-status="openai-check"]').textContent, '');
});

function soundTests(page) {
  return page.calls.filter((c) => c.method === 'POST' && c.path === 'api/sound/test');
}

test('the Sound tab has a sound test button that plays a test sound at the saved volume', async () => {
  const page = await open({ 'POST api/sound/test': reply(200, {}) }, { hash: '#sound' });
  const button = page.$('[data-card="volume"] [data-action="sound-test"]');
  assert.ok(button, 'the button sits in the volume card');
  assert.equal(button.getAttribute('type'), 'button');
  assert.equal(button.textContent, 'Тест звуку');
  const status = page.$('[data-status="sound-test"]');
  button.click();
  await settle();
  assert.deepEqual(soundTests(page).map((c) => c.body), [{}]);
  assert.equal(status.textContent, text(page, 'volume.test_ok'));
  assert.equal(status.getAttribute('data-tone'), 'ok');
  assert.equal(button.disabled, false);
  page.$('[data-set-lang="en"]').click();
  assert.equal(button.textContent, 'Test sound');
});

test('the sound test says when the speaker is busy talking', async () => {
  const page = await open({ 'POST api/sound/test': reply(409, { error: 'busy', message: 'x' }) }, { hash: '#sound' });
  page.$('[data-action="sound-test"]').click();
  await settle();
  const status = page.$('[data-status="sound-test"]');
  assert.equal(status.textContent, text(page, 'volume.test_busy'));
  assert.equal(status.getAttribute('data-tone'), 'error');
  assert.equal(page.$('[data-action="sound-test"]').disabled, false);
});

test('the sound test waits for the previous press to finish', async () => {
  let release;
  const page = await open({ 'POST api/sound/test': () => new Promise((r) => { release = () => r(reply(200, {})); }) }, { hash: '#sound' });
  const button = page.$('[data-action="sound-test"]');
  button.click();
  button.click();
  assert.equal(button.disabled, true);
  release();
  await settle();
  assert.equal(soundTests(page).length, 1);
  assert.equal(button.disabled, false);
});

test('the conversation language offers the popular world languages, each named in itself, and no Russian', async () => {
  const page = await open({}, { hash: '#conversation' });
  const options = page.$('#assistant-language').querySelectorAll('option');
  const codes = Array.from(options).map((o) => o.getAttribute('value'));
  assert.deepEqual(codes, ['uk', 'en', 'es', 'fr', 'de', 'it', 'pt', 'pl', 'nl', 'tr']);
  assert.ok(!codes.includes('ru'));
  for (const o of options) assert.equal(o.getAttribute('lang'), o.getAttribute('value'));
  assert.equal(Array.from(options).find((o) => o.getAttribute('value') === 'de').textContent, 'Deutsch');
  page.window.PYTAICHENKO_PAGE.LANGS.forEach((lang) => {
    for (const code of codes) {
      const name = page.window.PYTAICHENKO_PAGE.t(lang, `lang.${code}`);
      assert.ok(name && name !== `lang.${code}`, `${lang} lang.${code}`);
    }
  });
});

test('the header starts with a heart Donate link to Donatello that opens in a new tab', async () => {
  const page = await open();
  const link = page.$('.header-tools a.donate-link');
  assert.ok(link);
  assert.equal(link.getAttribute('href'), 'https://donatello.to/feroxes');
  assert.equal(link.getAttribute('target'), '_blank');
  assert.equal(link.getAttribute('rel'), 'noopener noreferrer');
  assert.ok(link.querySelector('svg.donate-heart'));
  assert.equal(link.textContent.trim(), 'Donate');
});
