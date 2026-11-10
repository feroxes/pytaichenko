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
  http_port: 8080,
  advertise_host: null,
  password_set: true,
};
const IN = { password_set: true, password_from_env: false, logged_in: true };
const OUT = { password_set: true, password_from_env: false, logged_in: false };
const KITCHEN = {
  name: 'kitchen', host: '192.0.2.10', port: 6053, key_set: true, connected: true,
  wake_words: { available: [{ id: 'okay_nabu', phrase: 'Okay Nabu' }], active: ['okay_nabu'] },
  volume: 78,
};
const EXPIRED = reply(401, { error: 'login_required', message: 'log in first' });

function routes(extra = {}) {
  return {
    'GET api/state': reply(200, IN),
    'GET api/settings': reply(200, { settings: SETTINGS, restart_needed: false }),
    'GET api/devices': reply(200, { devices: [KITCHEN] }),
    'GET api/discover': reply(200, { found: [] }),
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
  return page.window.PYTAICHENKO_HUB.t('uk', key, vars);
}

async function logIn(page) {
  page.type(page.$('#login-password'), 'correct horse battery');
  page.submit(page.$('[data-form="login"]'));
  await settle();
}

test('an expired session keeps the values typed but not saved, and forgets a typed key', async () => {
  const page = await open();
  assert.equal(page.$('#openai-model').value, 'gpt-realtime-2.1-mini');
  page.type(page.$('#openai-voice'), 'cedar');
  page.type(page.$('#openai-key'), 'sk-typed-not-saved');
  page.type(page.$('#timing-follow_up_s'), '12,5');
  page.type(page.$('#assistant-instructions'), 'Новий текст.');

  page.routes['GET api/devices'] = EXPIRED;
  page.poll();
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(page.$('[data-login-note]').textContent, text(page, 'login.expired'));
  assert.equal(page.$('#openai-key').value, '');

  page.routes['GET api/devices'] = reply(200, { devices: [KITCHEN] });
  await logIn(page);
  assert.equal(page.visible(page.$('[data-screen="main"]')), true);
  assert.equal(page.$('#openai-voice').value, 'cedar');
  assert.equal(page.$('#timing-follow_up_s').value, '12,5');
  assert.equal(page.$('#assistant-instructions').value, 'Новий текст.');
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
        : reply(200, { settings: SETTINGS, restart_needed: false });
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
      return answers < 3 ? 'network' : reply(200, { settings: SETTINGS, restart_needed: false });
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
  assert.equal(page.visible(page.$('[data-tab="speakers"]')), true);
  page.$('header [data-action="logout"]').click();
  await settle();
  assert.equal(page.window.location.hash, '#access');
  assert.equal(page.visible(page.$('[data-tab="access"]')), true);
  const status = page.$('[data-status="logout"]');
  assert.equal(page.visible(status), true);
  assert.equal(status.textContent, text(page, 'err.internal'));
  assert.equal(page.$('[data-status="mine"]').textContent, '');
});

test('polling does not unlock a speaker that is being removed', async () => {
  const gate = later();
  const page = await open({ 'DELETE api/devices/kitchen': () => gate.promise });
  const card = page.$('[data-speaker-list] .speaker');
  const [move, remove] = card.querySelectorAll('.speaker-actions button');
  remove.click();
  assert.equal(page.$('[data-confirm-text]').textContent, text(page, 'speaker.remove_confirm', { name: 'kitchen' }));
  page.$('[data-confirm-ok]').click();
  await settle();
  assert.equal(remove.disabled, true);
  page.poll();
  await settle();
  assert.equal(remove.disabled, true);
  assert.equal(move.disabled, true);
  assert.equal(card.querySelector('select').disabled, true);
  remove.click();
  page.$('[data-confirm-ok]').click();
  await settle();
  assert.equal(page.calls.filter((c) => c.method === 'DELETE').length, 1);
  gate.done(reply(200, { devices: [] }));
  await settle();
  assert.equal(page.$$('[data-speaker-list] .speaker').length, 0);
});

test('polling does not unlock a speaker whose address is being changed', async () => {
  const gate = later();
  const page = await open({ 'PUT api/devices/kitchen': () => gate.promise });
  const card = page.$('[data-speaker-list] .speaker');
  const [move, remove] = card.querySelectorAll('.speaker-actions button');
  move.click();
  const form = card.querySelector('.move-form');
  page.type(form.querySelector('[data-field="host"]'), '192.0.2.40');
  page.submit(form);
  await settle();
  const save = form.querySelector('button[type="submit"]');
  page.poll();
  await settle();
  assert.equal(save.disabled, true);
  assert.equal(remove.disabled, true);
  assert.equal(page.calls.filter((c) => c.method === 'PUT').length, 1);
  gate.done(reply(200, { devices: [{ ...KITCHEN, host: '192.0.2.40' }] }));
  await settle();
  assert.equal(save.disabled, false);
  assert.equal(remove.disabled, false);
  assert.equal(form.hidden, true);
});

test('a speaker without a key is removed with a question that matches its hint', async () => {
  const porch = { ...KITCHEN, name: 'porch', key_set: false, connected: false, wake_words: { available: [], active: [] } };
  const page = await open({ 'GET api/devices': reply(200, { devices: [porch] }) });
  const card = page.$('[data-speaker-list] .speaker');
  assert.equal(card.querySelector('.hint').textContent, text(page, 'speaker.nokey_hint'));
  card.querySelectorAll('.speaker-actions button')[1].click();
  assert.equal(page.$('[data-confirm-text]').textContent, text(page, 'speaker.remove_confirm_nokey', { name: 'porch' }));
  page.$('[data-confirm-cancel]').click();
});

test('“Enter the key” opens the Conversation tab first and then puts the cursor in the key field', async () => {
  const page = await open({
    'GET api/settings': reply(200, { settings: { ...SETTINGS, openai: { ...SETTINGS.openai, api_key_set: false } } }),
  });
  const link = page.$('[data-action="goto-key"]');
  assert.equal(page.visible(link), true);
  link.click();
  assert.equal(page.visible(page.$('[data-tab="conversation"]')), true);
  assert.equal(page.document.activeElement.id, 'openai-key');
  assert.equal(page.window.location.hash, '#conversation');
});

test('a speaker just adopted is not offered again while it is online, and the re-flash hint waits for it to drop', async () => {
  const adopted = { ...KITCHEN, name: 'speaker-a1b2c3', host: '192.0.2.30' };
  const stale = { name: 'speaker-a1b2c3', host: '192.0.2.30', port: 6053, provisionable: true, friendly_name: 'Speaker', added: true };
  const page = await open({
    'GET api/devices': reply(200, { devices: [adopted] }),
    'GET api/discover': reply(200, { found: [stale] }),
  });
  assert.equal(page.$$('[data-found-list] .found').length, 0);
  assert.equal(page.$$('[data-found-others-list] .found').length, 0);
  assert.equal(page.visible(page.$('[data-found-empty]')), true);

  page.routes['GET api/devices'] = reply(200, { devices: [{ ...adopted, connected: false }] });
  page.poll();
  await settle();
  const items = page.$$('[data-found-list] .found');
  assert.equal(items.length, 1);
  assert.equal(items[0].querySelector('.hint').textContent, text(page, 'found.reflashed'));
  const button = items[0].querySelector('button');
  assert.equal(button.textContent, text(page, 'found.adopt_again'));
  assert.doesNotMatch(button.getAttribute('class') || '', /primary/);
  assert.equal(items[0].querySelector('.pill'), null);
});

test('a new speaker is offered with the plain «Adopt» and its waiting mark', async () => {
  const fresh = { name: 'speaker-d4e5f6', host: '192.0.2.31', port: 6053, provisionable: true, friendly_name: 'Speaker', added: false };
  const page = await open({ 'GET api/discover': reply(200, { found: [fresh] }) });
  const item = page.$('[data-found-list] .found');
  const button = item.querySelector('button');
  assert.equal(button.textContent, text(page, 'found.adopt'));
  assert.match(button.getAttribute('class'), /primary/);
  assert.equal(item.querySelector('.pill').textContent, text(page, 'found.waiting'));
  assert.equal(item.querySelector('.hint'), null);
});

test('the key fields ask password managers not to fill them', async () => {
  const page = await open();
  for (const id of ['#openai-key', '#manual-key']) {
    const input = page.$(id);
    assert.equal(input.getAttribute('type'), 'password', id);
    assert.equal(input.getAttribute('autocomplete'), 'off', id);
    assert.equal(input.hasAttribute('data-1p-ignore'), true, id);
    assert.equal(input.getAttribute('data-lpignore'), 'true', id);
    assert.equal(input.hasAttribute('data-bwignore'), true, id);
    assert.equal(input.getAttribute('data-form-type'), 'other', id);
  }
});

function statusOf(form) {
  return form.querySelector('.form-foot [data-status]');
}

function unsaved(page, name) {
  return !page.$(`[data-form="${name}"] [data-unsaved]`).hidden;
}

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
      return reply(200, { settings: after, restart_needed: false });
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
  gate.done(reply(200, { settings: { ...SETTINGS, timing: { ...SETTINGS.timing, follow_up_s: 12 } }, restart_needed: false }));
  await settle();
  assert.deepEqual(page.calls.find((c) => c.method === 'PUT').body, { timing: { follow_up_s: 12 } });
  assert.equal(page.$('#timing-follow_up_s').value, '12');
  assert.equal(page.$('#timing-first_turn_s').value, '9');
  assert.equal(unsaved(page, 'timing'), true);
});

test('switching the conversation language shows the new default instructions after saving', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, assistant: { language: body.assistant.language, instructions: 'Answer briefly.' } },
      restart_needed: false,
    }),
  }, { hash: '#conversation' });
  const form = page.$('[data-form="assistant"]');
  page.type(page.$('#assistant-language'), 'en');
  page.submit(form);
  await settle();
  assert.deepEqual(page.calls.find((c) => c.method === 'PUT').body, { assistant: { language: 'en' } });
  assert.equal(page.$('#assistant-instructions').value, 'Answer briefly.');
  assert.equal(statusOf(form).textContent, text(page, 'assistant.language_switched'));
  assert.equal(unsaved(page, 'assistant'), false);
});

test('each card says when it has unsaved changes, and the mark goes after saving', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, assistant: { ...SETTINGS.assistant, ...body.assistant } }, restart_needed: false,
    }),
  }, { hash: '#conversation' });
  for (const name of ['openai', 'assistant', 'timing', 'server']) {
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
  page.type(page.$('#server-advertise'), 'hub.lan');
  assert.equal(unsaved(page, 'server'), true);
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

  page.type(page.$('#timing-follow_up_s'), '12');
  page.routes['GET api/settings'] = EXPIRED;
  page.$('[data-action="reload-settings"]').click();
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(page.leave().defaultPrevented, false);

  page.routes['GET api/settings'] = reply(200, { settings: SETTINGS, restart_needed: false });
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
      return answers === 1 ? reply(500, { error: 'internal', message: 'x' }) : reply(200, { settings: SETTINGS, restart_needed: false });
    },
  }, { hash: '#conversation' });
  const buttons = ['reset-instructions', 'timing-defaults', 'clear-key'].map((name) => page.$(`[data-action="${name}"]`));
  for (const button of buttons) assert.equal(button.disabled, true, button.getAttribute('data-action'));
  buttons[0].click();
  assert.notEqual(page.$('[data-confirm]').open, true);
  assert.equal(page.calls.filter((c) => c.method === 'PUT').length, 0);
  page.poll();
  await settle();
  assert.equal(answers, 2);
  for (const button of buttons) assert.equal(button.disabled, false, button.getAttribute('data-action'));
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
  gate.done(reply(200, { settings: { ...SETTINGS, assistant: { language: 'en', instructions: 'X' } }, restart_needed: false }));
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
  assert.equal(page.$('#assistant-instructions').value, '');
  await logIn(page);
  assert.equal(form.querySelector('button[type="submit"]').disabled, false);
  assert.equal(form.querySelector('[data-action="reset-instructions"]').disabled, false);
  assert.equal(statusOf(form).textContent, '');
  assert.equal(page.$('#assistant-instructions').value, SETTINGS.assistant.instructions);
});

test('logging out during an adoption keeps the page busy until it ends and never adopts twice', async () => {
  const gate = later();
  const fresh = { name: 'speaker-a1', host: '192.0.2.30', port: 6053, provisionable: true, friendly_name: 'Speaker', added: false };
  const page = await open({ 'GET api/discover': reply(200, { found: [fresh] }), 'POST api/devices/adopt': () => gate.promise });
  page.$('[data-found-list] button').click();
  await settle();
  page.$('header [data-action="logout"]').click();
  await settle();
  await logIn(page);
  const add = page.$('[data-form="manual"] button[type="submit"]');
  assert.equal(page.visible(page.$('[data-progress]')), true);
  assert.equal(add.disabled, true);
  assert.equal(page.$('[data-found-list] button').disabled, true);
  page.$('[data-found-list] button').click();
  await settle();
  assert.equal(page.calls.filter((c) => c.path === 'api/devices/adopt').length, 1);
  const adopted = { ...KITCHEN, name: 'speaker-a1', host: '192.0.2.30' };
  page.routes['GET api/devices'] = reply(200, { devices: [KITCHEN, adopted] });
  page.routes['GET api/discover'] = reply(200, { found: [{ ...fresh, added: true, provisionable: false }] });
  gate.done(reply(200, { name: 'speaker-a1', devices: [KITCHEN, adopted] }));
  await settle();
  assert.equal(page.visible(page.$('[data-progress]')), false);
  assert.equal(add.disabled, false);
  assert.equal(page.$('[data-status="found"]').textContent, text(page, 'adopt.done', { name: 'speaker-a1' }));
});

test('an adoption that ends while logged out leaves nothing behind', async () => {
  const gate = later();
  const fresh = { name: 'speaker-a1', host: '192.0.2.30', port: 6053, provisionable: true, friendly_name: 'Speaker', added: false };
  const page = await open({ 'GET api/discover': reply(200, { found: [fresh] }), 'POST api/devices/adopt': () => gate.promise });
  page.$('[data-found-list] button').click();
  await settle();
  page.$('header [data-action="logout"]').click();
  await settle();
  gate.done(reply(200, { name: 'speaker-a1', devices: [KITCHEN, { ...KITCHEN, name: 'speaker-a1' }] }));
  await settle();
  assert.equal(page.$$('[data-speaker-list] .speaker').length, 0);
  assert.equal(page.$('[data-status="found"]').textContent, '');
  await logIn(page);
  assert.equal(page.visible(page.$('[data-progress]')), false);
  assert.equal(page.$('[data-form="manual"] button[type="submit"]').disabled, false);
});

test('the no-connection banner goes away on any tab once the server answers', async () => {
  const page = await open();
  page.routes['GET api/devices'] = 'network';
  page.routes['GET api/discover'] = 'network';
  page.poll();
  await settle();
  const banner = page.$('[data-banner="offline"]');
  assert.equal(page.visible(banner), true);
  page.$('[data-tab-link="timing"]').click();
  await settle();
  page.poll();
  await settle();
  assert.equal(page.visible(banner), true);
  page.routes['GET api/devices'] = reply(200, { devices: [KITCHEN] });
  page.poll();
  await settle();
  assert.equal(page.visible(banner), false);
  const polled = page.calls.length;
  page.poll();
  await settle();
  assert.equal(page.calls.length, polled);
});

test('any answer from the server clears the no-connection banner', async () => {
  const page = await open({ 'PUT api/settings': reply(200, { settings: SETTINGS, restart_needed: false }) });
  page.routes['GET api/devices'] = 'network';
  page.routes['GET api/discover'] = 'network';
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

const WORDS = {
  available: [
    { id: 'hey_jarvis', phrase: 'Hey Jarvis' }, { id: 'okay_nabu', phrase: 'Okay Nabu' }, { id: 'hey_mycroft', phrase: 'Hey Mycroft' },
  ],
  active: ['hey_jarvis'],
};

function pick(page, select, value, how) {
  if (how === 'mouse') page.fire(select, 'mousedown');
  else page.fire(select, 'keydown', { key: 'ArrowDown' });
  select.value = value;
  page.fire(select, 'change');
}

test('arrowing through the wake words sends nothing until Enter or leaving the list', async () => {
  const sent = [];
  const page = await open({
    'GET api/devices': reply(200, { devices: [{ ...KITCHEN, wake_words: WORDS }] }),
    'PUT api/devices/kitchen/wake-word': (body) => {
      sent.push(body.wake_word);
      return reply(200, { devices: [{ ...KITCHEN, wake_words: { ...WORDS, active: [body.wake_word] } }] });
    },
  });
  const select = page.$('[data-speaker-list] select');
  select.focus();
  pick(page, select, 'okay_nabu', 'keys');
  pick(page, select, 'hey_mycroft', 'keys');
  await settle();
  assert.deepEqual(sent, []);
  page.fire(select, 'keydown', { key: 'Enter' });
  await settle();
  assert.deepEqual(sent, ['hey_mycroft']);
  assert.equal(page.document.activeElement, select);

  pick(page, select, 'okay_nabu', 'keys');
  await settle();
  assert.deepEqual(sent, ['hey_mycroft']);
  page.fire(select, 'blur');
  await settle();
  assert.deepEqual(sent, ['hey_mycroft', 'okay_nabu']);
  page.fire(select, 'blur');
  await settle();
  assert.deepEqual(sent, ['hey_mycroft', 'okay_nabu']);

  pick(page, select, 'hey_jarvis', 'mouse');
  await settle();
  assert.deepEqual(sent, ['hey_mycroft', 'okay_nabu', 'hey_jarvis']);
});

test('the adoption progress is announced once, and the seconds tick outside the announcement', async () => {
  const gate = later();
  const fresh = { name: 'speaker-a1', host: '192.0.2.30', port: 6053, provisionable: true, friendly_name: 'Speaker', added: false };
  const page = await open({ 'GET api/discover': reply(200, { found: [fresh] }), 'POST api/devices/adopt': () => gate.promise });
  page.$('[data-found-list] button').click();
  await settle();
  const region = page.$('[data-progress]');
  assert.equal(region.getAttribute('aria-live'), 'polite');
  const said = region.querySelector('[data-progress-text]');
  const first = said.childNodes[0];
  assert.equal(said.textContent, text(page, 'adopt.progress', { host: '192.0.2.30' }));
  const time = region.querySelector('[data-progress-time]');
  assert.equal(time.getAttribute('aria-hidden'), 'true');
  assert.equal(time.textContent, text(page, 'adopt.elapsed', { s: 0 }));
  page.poll();
  page.poll();
  await settle();
  assert.equal(said.childNodes[0], first);
  page.$('[data-set-lang="en"]').click();
  assert.equal(said.textContent, page.window.PYTAICHENKO_HUB.t('en', 'adopt.progress', { host: '192.0.2.30' }));
  gate.done(reply(200, { name: 'speaker-a1', devices: [KITCHEN] }));
  await settle();
});

test('the settings keep loading after a script error on the way', async () => {
  let answers = 0;
  const page = await open({
    'GET api/settings': () => {
      answers += 1;
      return answers === 1 ? reply(200, { restart_needed: false }) : reply(200, { settings: SETTINGS, restart_needed: false });
    },
  }, { hash: '#timing' });
  assert.equal(answers, 1);
  page.poll();
  await settle();
  assert.equal(answers, 2);
  assert.equal(page.$('#timing-follow_up_s').value, '10');
  assert.equal(page.visible(page.$('[data-banner="settings"]')), false);
});

test('addresses with http://, a port or a path are refused before sending', async () => {
  const page = await open({ 'PUT api/settings': reply(200, { settings: { ...SETTINGS, advertise_host: 'hub.lan' }, restart_needed: true }) });
  const manual = page.$('[data-form="manual"]');
  page.type(page.$('#manual-host'), 'http://192.0.2.30');
  page.submit(manual);
  await settle();
  const host = page.$('#manual-host');
  assert.equal(host.getAttribute('aria-invalid'), 'true');
  assert.equal(manual.querySelector('.field-error').textContent, `${text(page, 'rule.host').charAt(0).toUpperCase()}${text(page, 'rule.host').slice(1)}.`);

  const card = page.$('[data-speaker-list] .speaker');
  card.querySelectorAll('.speaker-actions button')[0].click();
  const move = card.querySelector('.move-form');
  page.type(move.querySelector('[data-field="host"]'), '192.0.2.40:6053');
  page.submit(move);
  await settle();
  assert.equal(move.querySelector('[data-field="host"]').getAttribute('aria-invalid'), 'true');

  const server = page.$('[data-form="server"]');
  page.type(page.$('#server-advertise'), 'http://192.0.2.5:8080');
  page.submit(server);
  await settle();
  assert.equal(page.$('#server-advertise').getAttribute('aria-invalid'), 'true');
  assert.equal(page.calls.filter((c) => c.method !== 'GET').length, 0);

  page.type(page.$('#server-advertise'), 'hub.lan');
  page.submit(server);
  await settle();
  assert.deepEqual(page.calls.find((c) => c.method === 'PUT').body, { advertise_host: 'hub.lan' });
});

test('a refused address from the server is shown on its field', async () => {
  const message = 'host: must be a host name or an IP address, without http://, a port or a path';
  const page = await open({ 'POST api/devices': reply(400, { error: 'invalid', message }) });
  const manual = page.$('[data-form="manual"]');
  page.type(page.$('#manual-host'), 'speaker.lan');
  page.submit(manual);
  await settle();
  assert.equal(page.$('#manual-host').getAttribute('aria-invalid'), 'true');
  assert.equal(statusOf(manual).textContent, text(page, 'field.message', { field: text(page, 'manual.host'), rule: text(page, 'rule.host') }));
});

test('a port set by VOICE_HUB_PORT is shown read-only with a note and never sent', async () => {
  const fromEnv = { ...SETTINGS, http_port: 9000, http_port_from_env: true };
  const page = await open({
    'GET api/settings': reply(200, { settings: fromEnv, restart_needed: false }),
    'PUT api/settings': reply(200, { settings: { ...fromEnv, advertise_host: 'hub.lan' }, restart_needed: true }),
  }, { hash: '#access' });
  const port = page.$('#server-port');
  assert.equal(port.value, '9000');
  assert.equal(port.hasAttribute('readonly'), true);
  assert.equal(page.$('#server-port-hint').textContent, text(page, 'server.port_env'));
  page.type(port, '9001');
  assert.equal(unsaved(page, 'server'), false);
  page.type(page.$('#server-advertise'), 'hub.lan');
  page.submit(page.$('[data-form="server"]'));
  await settle();
  assert.deepEqual(page.calls.find((c) => c.method === 'PUT').body, { advertise_host: 'hub.lan' });
  page.$('[data-set-lang="en"]').click();
  assert.equal(page.$('#server-port-hint').textContent, page.window.PYTAICHENKO_HUB.t('en', 'server.port_env'));
});

test('a port from the settings file can be changed, from 1024 up', async () => {
  const page = await open({
    'GET api/settings': reply(200, { settings: { ...SETTINGS, http_port_from_env: false }, restart_needed: false }),
    'PUT api/settings': reply(200, { settings: { ...SETTINGS, http_port: 8081, http_port_from_env: false }, restart_needed: true }),
  }, { hash: '#access' });
  const port = page.$('#server-port');
  assert.equal(port.hasAttribute('readonly'), false);
  assert.equal(page.$('#server-port-hint').textContent, text(page, 'server.port_hint', { v: 8080 }));
  page.type(port, '80');
  page.submit(page.$('[data-form="server"]'));
  await settle();
  assert.equal(port.getAttribute('aria-invalid'), 'true');
  assert.equal(page.calls.filter((c) => c.method === 'PUT').length, 0);
  page.type(port, '8081');
  page.submit(page.$('[data-form="server"]'));
  await settle();
  assert.deepEqual(page.calls.find((c) => c.method === 'PUT').body, { http_port: 8081 });
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
  await logIn(page);
  page.$('[data-set-lang="en"]').click();
  await settle();
  assert.equal(page.$('#assistant-language').value, 'uk');
  assert.ok(!page.calls.some((call) => call.method === 'PUT'));
});

test('creating the password gives the server the page language for the conversation', async () => {
  const fresh = { password_set: false, password_from_env: false, logged_in: false };
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
});

test('restoring the default instructions shows the text the server chose and leaves nothing unsaved', async () => {
  const page = await open({
    'PUT api/settings': (body) => reply(200, {
      settings: { ...SETTINGS, assistant: { language: body.assistant.language, instructions: 'Типові інструкції.' } },
      restart_needed: false,
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
  const put = page.calls.find((c) => c.method === 'PUT');
  assert.deepEqual(put.body, { assistant: { language: 'uk', instructions: null } });
  assert.equal(page.$('#assistant-instructions').value, 'Типові інструкції.');
  assert.equal(form.querySelector('[data-unsaved]').hidden, true);
  assert.equal(page.leave().defaultPrevented, false);
});

test('saving the server port keeps an unsaved edit of the empty server address field', async () => {
  const gate = later();
  const page = await open({ 'PUT api/settings': () => gate.promise }, { hash: '#access' });
  const form = page.$('[data-form="server"]');
  const port = form.querySelector('[data-field="http_port"]');
  const host = form.querySelector('[data-field="advertise_host"]');
  page.type(port, '8090');
  page.submit(form);
  await settle();
  page.type(host, '192.0.2.7');
  gate.done(reply(200, { settings: { ...SETTINGS, http_port: 8090 }, restart_needed: true }));
  await settle();
  assert.equal(host.value, '192.0.2.7');
});

function cardOf(page, name) {
  return page.$$('[data-speaker-list] .speaker').find((card) => card.querySelector('.speaker-name').textContent === name);
}

function volumeIn(card) {
  return { slider: card.querySelector('input[type="range"]'), label: card.querySelector('.volume-value') };
}

function cardStatus(card) {
  return card.children[card.children.length - 1];
}

function volumePuts(page) {
  return page.calls.filter((c) => c.method === 'PUT' && c.path === 'api/devices/kitchen/volume').map((c) => c.body);
}

function slide(page, slider, value) {
  page.type(slider, value);
}

function release(page, slider, value) {
  page.type(slider, value);
  page.fire(slider, 'change');
}

function answerVolume(body) {
  return reply(200, { devices: [{ ...KITCHEN, volume: body.volume }] });
}

test('each speaker card shows its volume, and a dash when the speaker is offline or has no volume', async () => {
  const hall = { ...KITCHEN, name: 'hall', host: '192.0.2.11', connected: false, volume: null, wake_words: { available: [], active: [] } };
  const porch = { ...KITCHEN, name: 'porch', host: '192.0.2.12', volume: null };
  const page = await open({ 'GET api/devices': reply(200, { devices: [KITCHEN, hall, porch] }) });
  const card = cardOf(page, 'kitchen');
  const { slider, label } = volumeIn(card);
  assert.deepEqual(['min', 'max', 'step'].map((name) => slider.getAttribute(name)), ['0', '100', '1']);
  assert.equal(slider.value, '78');
  assert.equal(slider.disabled, false);
  assert.equal(label.textContent, '78 %');
  assert.equal(slider.getAttribute('aria-valuetext'), '78 %');
  const name = card.querySelector(`label[for="${slider.id}"]`);
  assert.equal(name.textContent, 'Гучність');
  assert.ok(card.querySelectorAll('.hint').some((hint) => hint.textContent === 'Голосом теж можна: «тихіше», «гучніше».'));
  for (const other of ['hall', 'porch']) {
    const shown = volumeIn(cardOf(page, other));
    assert.equal(shown.slider.disabled, true, other);
    assert.equal(shown.label.textContent, '—', other);
  }
  page.$('[data-set-lang="en"]').click();
  assert.equal(name.textContent, 'Volume');
  assert.equal(label.textContent, '78 %');
  assert.ok(card.querySelectorAll('.hint').some((hint) => hint.textContent === 'You can also say “quieter” or “louder”.'));
});

test('the label follows the slider while dragging, and the volume is sent once on release', async () => {
  const page = await open({ 'PUT api/devices/kitchen/volume': answerVolume });
  const card = cardOf(page, 'kitchen');
  const { slider, label } = volumeIn(card);
  slide(page, slider, '60');
  assert.equal(label.textContent, '60 %');
  assert.equal(slider.getAttribute('aria-valuetext'), '60 %');
  slide(page, slider, '65');
  assert.equal(label.textContent, '65 %');
  await settle();
  assert.deepEqual(volumePuts(page), []);
  page.fire(slider, 'change');
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 65 }]);
  assert.equal(slider.value, '65');
  assert.equal(label.textContent, '65 %');
  assert.equal(cardStatus(card).textContent, '');
  page.fire(slider, 'change');
  await settle();
  assert.equal(volumePuts(page).length, 1);
});

test('polling leaves the slider alone while it is held, and shows a change made by voice after that', async () => {
  const page = await open({ 'PUT api/devices/kitchen/volume': answerVolume });
  const { slider, label } = volumeIn(cardOf(page, 'kitchen'));
  slide(page, slider, '40');
  page.routes['GET api/devices'] = reply(200, { devices: [{ ...KITCHEN, volume: 55 }] });
  page.poll();
  await settle();
  assert.equal(slider.value, '40');
  assert.equal(label.textContent, '40 %');
  page.fire(slider, 'change');
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 40 }]);
  page.poll();
  await settle();
  assert.equal(slider.value, '55');
  assert.equal(label.textContent, '55 %');
});

test('letting go of the slider without a change lets polling move it again', async () => {
  const page = await open();
  const { slider, label } = volumeIn(cardOf(page, 'kitchen'));
  slide(page, slider, '80');
  page.fire(slider, 'pointerup');
  page.routes['GET api/devices'] = reply(200, { devices: [{ ...KITCHEN, volume: 50 }] });
  page.poll();
  await settle();
  assert.equal(slider.value, '50');
  assert.equal(label.textContent, '50 %');
  assert.deepEqual(volumePuts(page), []);
});

test('polling leaves the slider alone while a volume save is on its way', async () => {
  const gate = later();
  const page = await open({ 'PUT api/devices/kitchen/volume': () => gate.promise });
  const { slider, label } = volumeIn(cardOf(page, 'kitchen'));
  release(page, slider, '30');
  await settle();
  page.poll();
  await settle();
  assert.equal(slider.value, '30');
  assert.equal(label.textContent, '30 %');
  assert.equal(slider.disabled, false);
  gate.done(reply(200, { devices: [{ ...KITCHEN, volume: 30 }] }));
  await settle();
  assert.equal(slider.value, '30');
  page.routes['GET api/devices'] = reply(200, { devices: [{ ...KITCHEN, volume: 45 }] });
  page.poll();
  await settle();
  assert.equal(slider.value, '45');
  assert.equal(label.textContent, '45 %');
});

test('a volume chosen while a save is on its way is sent after it, and only the latest one', async () => {
  const gates = [];
  const page = await open({
    'PUT api/devices/kitchen/volume': () => {
      const gate = later();
      gates.push(gate);
      return gate.promise;
    },
  });
  const { slider, label } = volumeIn(cardOf(page, 'kitchen'));
  release(page, slider, '30');
  await settle();
  release(page, slider, '40');
  release(page, slider, '50');
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 30 }]);
  gates[0].done(reply(200, { devices: [{ ...KITCHEN, volume: 30 }] }));
  await settle();
  assert.deepEqual(volumePuts(page), [{ volume: 30 }, { volume: 50 }]);
  assert.equal(slider.value, '50');
  assert.equal(label.textContent, '50 %');
  gates[1].done(reply(200, { devices: [{ ...KITCHEN, volume: 50 }] }));
  await settle();
  assert.equal(slider.value, '50');
  assert.equal(volumePuts(page).length, 2);
});

test('a volume the speaker did not take is explained in the card, and the slider goes back', async () => {
  for (const [status, code, said] of [[409, 'offline', 'err.volume_offline'], [502, 'failed', 'err.volume_failed']]) {
    const page = await open({ 'PUT api/devices/kitchen/volume': reply(status, { error: code, message: 'kitchen: ' + code }) });
    const card = cardOf(page, 'kitchen');
    const { slider, label } = volumeIn(card);
    const lists = () => page.calls.filter((c) => c.method === 'GET' && c.path === 'api/devices').length;
    const before = lists();
    release(page, slider, '30');
    await settle();
    assert.equal(cardStatus(card).textContent, text(page, said), code);
    assert.equal(cardStatus(card).getAttribute('data-tone'), 'error', code);
    assert.equal(slider.value, '78', code);
    assert.equal(label.textContent, '78 %', code);
    assert.equal(lists() - before, code === 'offline' ? 1 : 0, code);
    page.routes['PUT api/devices/kitchen/volume'] = answerVolume;
    release(page, slider, '35');
    await settle();
    assert.equal(cardStatus(card).textContent, '', code);
    assert.equal(label.textContent, '35 %', code);
  }
});

test('a volume for a speaker that is gone is explained above the list', async () => {
  const page = await open({ 'PUT api/devices/kitchen/volume': reply(404, { error: 'unknown_speaker', message: 'x' }) });
  const { slider } = volumeIn(cardOf(page, 'kitchen'));
  page.routes['GET api/devices'] = reply(200, { devices: [] });
  release(page, slider, '30');
  await settle();
  assert.equal(page.$('[data-status="mine"]').textContent, text(page, 'err.unknown_speaker'));
  assert.equal(page.$$('[data-speaker-list] .speaker').length, 0);
});

test('a speaker that drops off while its slider is held shows a dash and sends nothing', async () => {
  const page = await open({ 'PUT api/devices/kitchen/volume': answerVolume });
  const { slider, label } = volumeIn(cardOf(page, 'kitchen'));
  slide(page, slider, '40');
  page.routes['GET api/devices'] = reply(200, { devices: [{ ...KITCHEN, connected: false, volume: null }] });
  page.poll();
  await settle();
  assert.equal(slider.disabled, true);
  assert.equal(label.textContent, '—');
  page.fire(slider, 'change');
  await settle();
  assert.deepEqual(volumePuts(page), []);
  page.routes['GET api/devices'] = reply(200, { devices: [{ ...KITCHEN, volume: 60 }] });
  page.poll();
  await settle();
  assert.equal(slider.disabled, false);
  assert.equal(slider.value, '60');
  assert.equal(label.textContent, '60 %');
});
