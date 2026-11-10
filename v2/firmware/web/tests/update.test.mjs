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
const DEVICE = { name: 'pytaichenko-a1b2c3', version: '2.0.0', ip: '192.0.2.40', rssi: -62 };
const NEWER = { current: '2.0.0', latest: '2.1.0', available: true };

function routes(extra = {}) {
  return {
    'GET api/state': reply(200, IN),
    'GET api/settings': reply(200, { settings: SETTINGS }),
    'GET api/device': reply(200, DEVICE),
    'GET api/log?after=0': reply(200, { lines: [] }),
    'POST api/logout': reply(200, { password_set: true, logged_in: false }),
    ...extra,
  };
}

async function open(extra = {}) {
  const page = makePage({ routes: routes(extra), hash: '#about' });
  await settle();
  return page;
}

function text(page, key, vars = {}, lang = 'uk') {
  return page.window.PYTAICHENKO_PAGE.t(lang, key, vars);
}

const status = (page) => page.$('[data-status="update"]');
const fileStatus = (page) => page.$('[data-status="update-file"]');
const button = (page, name) => page.$(`[data-action="update-${name}"]`);

async function check(page) {
  button(page, 'check').click();
  await settle();
}

async function confirmYes(page) {
  await settle();
  page.$('[data-confirm-ok]').click();
  await settle();
}

test('test update block shows available version', async () => {
  const page = await open({ 'GET api/update': reply(200, NEWER) });
  assert.equal(page.$('[data-update="current"]').textContent, '2.0.0');
  assert.equal(page.$('[data-update="latest"]').textContent, '—');
  assert.equal(page.visible(button(page, 'install')), false);
  await check(page);
  assert.equal(page.$('[data-update="latest"]').textContent, '2.1.0');
  assert.equal(page.visible(button(page, 'install')), true);
  assert.equal(button(page, 'install').textContent, text(page, 'update.install', { version: '2.1.0' }));
  assert.equal(status(page).textContent, text(page, 'update.available', { version: '2.1.0' }));
  assert.equal(status(page).getAttribute('data-tone'), 'ok');
  assert.equal(button(page, 'check').disabled, false);
});

test('the update block shows the version the speaker runs even before a check', async () => {
  const page = await open();
  assert.equal(page.$('[data-update="current"]').textContent, '2.0.0');
  assert.equal(page.calls.filter((c) => c.path === 'api/update').length, 0);
});

test('with nothing newer the install button stays hidden', async () => {
  const page = await open({ 'GET api/update': reply(200, { current: '2.1.0', latest: '2.1.0', available: false }) });
  await check(page);
  assert.equal(page.visible(button(page, 'install')), false);
  assert.equal(status(page).textContent, text(page, 'update.up_to_date'));
  page.routes['GET api/update'] = reply(200, { current: '2.1.0', latest: null, available: false });
  await check(page);
  assert.equal(page.$('[data-update="latest"]').textContent, '—');
  assert.equal(status(page).textContent, text(page, 'update.none'));
  assert.equal(page.visible(button(page, 'install')), false);
});

test('a check that fails says so and keeps the buttons usable', async () => {
  const page = await open({ 'GET api/update': reply(502, { error: 'update_failed', message: 'x' }) });
  await check(page);
  assert.equal(status(page).textContent, text(page, 'err.update_failed'));
  assert.equal(status(page).getAttribute('data-tone'), 'error');
  assert.equal(button(page, 'check').disabled, false);
  assert.equal(page.visible(button(page, 'install')), false);
});

test('the install button follows the page language', async () => {
  const page = await open({ 'GET api/update': reply(200, NEWER) });
  await check(page);
  page.$('[data-set-lang="en"]').click();
  assert.equal(button(page, 'install').textContent, text(page, 'update.install', { version: '2.1.0' }, 'en'));
  assert.equal(status(page).textContent, text(page, 'update.available', { version: '2.1.0' }, 'en'));
});

test('installing asks first, posts an empty object and waits for the speaker to come back', async () => {
  const page = await open({
    'GET api/update': reply(200, NEWER),
    'POST api/update': reply(200, { installing: true, version: '2.1.0' }),
  });
  await check(page);
  button(page, 'install').click();
  await settle();
  assert.equal(page.$('[data-confirm]').open, true);
  assert.equal(page.$('[data-confirm-text]').textContent, text(page, 'update.confirm', { version: '2.1.0' }));
  assert.equal(page.calls.filter((c) => c.method === 'POST' && c.path === 'api/update').length, 0);
  page.$('[data-confirm-ok]').click();
  await settle();
  assert.deepEqual(page.calls.filter((c) => c.method === 'POST' && c.path === 'api/update').map((c) => c.body), [{}]);
  assert.equal(status(page).textContent, text(page, 'update.restarting'));
  assert.equal(button(page, 'check').disabled, true);
  assert.equal(button(page, 'install').disabled, true);
  assert.equal(button(page, 'upload').disabled, true);

  const stateCalls = () => page.calls.filter((c) => c.path === 'api/state').length;
  const before = stateCalls();
  page.routes['GET api/state'] = 'network';
  page.runTimeouts();
  await settle();
  assert.equal(stateCalls(), before + 1);
  assert.equal(page.reloads.length, 0);
  page.runTimeouts();
  await settle();
  assert.equal(page.reloads.length, 0);
  page.routes['GET api/state'] = reply(200, IN);
  page.runTimeouts();
  await settle();
  assert.equal(status(page).textContent, text(page, 'update.back'));
  assert.equal(page.reloads.length, 0);
  page.runTimeouts();
  assert.equal(page.reloads.length, 1);
});

test('an answer from the old firmware right after the request does not count as the restart', async () => {
  const page = await open({
    'GET api/update': reply(200, NEWER),
    'POST api/update': reply(200, { installing: true, version: '2.1.0' }),
  });
  await check(page);
  button(page, 'install').click();
  await confirmYes(page);
  page.runTimeouts();
  await settle();
  assert.equal(page.reloads.length, 0);
  assert.equal(status(page).textContent, text(page, 'update.restarting'));
});

test('the page stops waiting after a long silence and says what to do', async () => {
  const page = await open({
    'GET api/update': reply(200, NEWER),
    'POST api/update': reply(200, { installing: true, version: '2.1.0' }),
  });
  await check(page);
  button(page, 'install').click();
  await confirmYes(page);
  page.routes['GET api/state'] = 'network';
  for (let i = 0; i < 80; i += 1) {
    page.runTimeouts();
    await settle(2);
  }
  assert.equal(status(page).textContent, text(page, 'update.restart_timeout'));
  assert.equal(status(page).getAttribute('data-tone'), 'error');
  assert.equal(button(page, 'check').disabled, false);
  assert.equal(page.reloads.length, 0);
});

test('while the speaker restarts the page does not poll the device or show the offline banner', async () => {
  const page = await open({
    'GET api/update': reply(200, NEWER),
    'POST api/update': reply(200, { installing: true, version: '2.1.0' }),
  });
  await check(page);
  button(page, 'install').click();
  await confirmYes(page);
  const deviceCalls = () => page.calls.filter((c) => c.path === 'api/device').length;
  const before = deviceCalls();
  page.routes['GET api/device'] = 'network';
  page.poll();
  await settle();
  assert.equal(deviceCalls(), before);
  const logCalls = () => page.calls.filter((c) => c.path.startsWith('api/log')).length;
  const logBefore = logCalls();
  page.routes['GET api/log?after=0'] = 'network';
  page.$('[data-tab-link="log"]').click();
  page.poll();
  await settle();
  assert.equal(logCalls(), logBefore);
  assert.equal(page.visible(page.$('[data-banner="offline"]')), false);
});

test('polling and tab reads wait while an update is on its way, then resume', async () => {
  const gate = (() => {
    let done;
    const promise = new Promise((resolve) => {
      done = resolve;
    });
    return { promise, done };
  })();
  const page = await open({
    'GET api/update': reply(200, NEWER),
    'POST api/update': () => gate.promise,
  });
  await check(page);
  button(page, 'install').click();
  await confirmYes(page);
  const deviceCalls = () => page.calls.filter((c) => c.path === 'api/device').length;
  const before = deviceCalls();
  page.routes['GET api/device'] = 'network';
  page.poll();
  await settle();
  page.$('[data-tab-link="sound"]').click();
  page.$('[data-tab-link="about"]').click();
  await settle();
  page.poll();
  await settle();
  assert.equal(deviceCalls(), before);
  assert.equal(page.visible(page.$('[data-banner="offline"]')), false);
  gate.done(reply(502, { error: 'update_failed', message: 'x' }));
  await settle();
  assert.equal(button(page, 'check').disabled, false);
  page.routes['GET api/device'] = reply(200, DEVICE);
  page.poll();
  await settle();
  assert.equal(deviceCalls(), before + 1);
});

test('a confirmation answered after logging out sends nothing', async () => {
  const page = await open({ 'GET api/update': reply(200, NEWER) });
  await check(page);
  button(page, 'install').click();
  await settle();
  page.$('[data-action="logout"]').click();
  await settle();
  page.$('[data-confirm-ok]').click();
  await settle();
  assert.equal(page.calls.filter((c) => c.method === 'POST' && c.path === 'api/update').length, 0);
  assert.equal(button(page, 'check').disabled, false);
  assert.equal(status(page).textContent, '');
});

test('a cancelled confirmation sends nothing', async () => {
  const page = await open({ 'GET api/update': reply(200, NEWER) });
  await check(page);
  button(page, 'install').click();
  await settle();
  page.$('[data-confirm-cancel]').click();
  await settle();
  assert.equal(page.calls.filter((c) => c.method === 'POST' && c.path === 'api/update').length, 0);
  assert.equal(button(page, 'check').disabled, false);
});

test('install refusals are explained and the buttons come back', async () => {
  const page = await open({ 'GET api/update': reply(200, NEWER) });
  await check(page);
  for (const [code, httpStatus] of [['update_busy', 409], ['update_not_ours', 400], ['update_failed', 502], ['no_release', 404]]) {
    page.routes['POST api/update'] = reply(httpStatus, { error: code, message: 'x' });
    button(page, 'install').click();
    await confirmYes(page);
    const detail = code === 'update_busy' ? 'x' : '';
    assert.equal(status(page).textContent, text(page, `err.${code}`) + detail, code);
    assert.equal(status(page).getAttribute('data-tone'), 'error');
    assert.equal(button(page, 'check').disabled, false);
    assert.equal(button(page, 'install').disabled, false);
  }
  assert.equal(page.reloads.length, 0);
});

class FakeXhr {
  static last = null;

  constructor() {
    this.headers = {};
    this.upload = {};
    FakeXhr.last = this;
  }

  open(method, url) {
    this.method = method;
    this.url = url;
  }

  setRequestHeader(name, value) {
    this.headers[name] = value;
  }

  send(body) {
    this.body = body;
  }

  finish(status, data) {
    this.status = status;
    this.responseText = typeof data === 'string' ? data : JSON.stringify(data);
    this.onload();
  }
}

function pickFile(page, name, size) {
  page.$('#update-file').files = [{ name, size }];
  page.window.XMLHttpRequest = FakeXhr;
  FakeXhr.last = null;
}

test('a firmware file is sent as one octet-stream post with visible progress', async () => {
  const page = await open();
  const file = { name: 'pytaichenko-2.1.0.ota.bin', size: 1500000 };
  page.$('#update-file').files = [file];
  page.window.XMLHttpRequest = FakeXhr;
  button(page, 'upload').click();
  await settle();
  assert.equal(page.$('[data-confirm-text]').textContent, text(page, 'update.file_confirm', { name: file.name }));
  assert.equal(FakeXhr.last, null);
  page.$('[data-confirm-ok]').click();
  await settle();
  const xhr = FakeXhr.last;
  assert.equal(xhr.method, 'POST');
  assert.equal(xhr.url, 'api/update/upload');
  assert.equal(xhr.headers['Content-Type'], 'application/octet-stream');
  assert.equal(xhr.body, file);
  assert.equal(button(page, 'upload').disabled, true);
  assert.equal(fileStatus(page).textContent, text(page, 'update.uploading', { n: 0 }));
  xhr.upload.onprogress({ lengthComputable: true, loaded: 750000, total: 1500000 });
  assert.equal(fileStatus(page).textContent, text(page, 'update.uploading', { n: 50 }));
  xhr.upload.onprogress({ lengthComputable: true, loaded: 1500000, total: 1500000 });
  assert.equal(fileStatus(page).textContent, text(page, 'update.upload_done'));
  xhr.finish(200, { installing: true });
  await settle();
  assert.equal(fileStatus(page).textContent, text(page, 'update.restarting'));
  page.routes['GET api/state'] = 'network';
  page.runTimeouts();
  await settle();
  page.routes['GET api/state'] = reply(200, IN);
  page.runTimeouts();
  await settle();
  page.runTimeouts();
  assert.equal(page.reloads.length, 1);
});

test('a file that is not a firmware is explained and nothing restarts', async () => {
  const page = await open();
  pickFile(page, 'notes.bin', 1000);
  button(page, 'upload').click();
  await confirmYes(page);
  FakeXhr.last.finish(400, { error: 'update_not_ours', message: 'x' });
  await settle();
  assert.equal(fileStatus(page).textContent, text(page, 'err.update_not_ours'));
  assert.equal(fileStatus(page).getAttribute('data-tone'), 'error');
  assert.equal(button(page, 'upload').disabled, false);
  assert.equal(page.reloads.length, 0);
});

test('upload without a file, or with a file over 4 MB, is refused before anything is sent', async () => {
  const page = await open();
  page.window.XMLHttpRequest = FakeXhr;
  FakeXhr.last = null;
  button(page, 'upload').click();
  await settle();
  assert.equal(fileStatus(page).textContent, text(page, 'update.no_file'));
  pickFile(page, 'big.bin', 4 * 1024 * 1024 + 1);
  button(page, 'upload').click();
  await settle();
  assert.equal(fileStatus(page).textContent, text(page, 'update.file_too_big'));
  assert.equal(FakeXhr.last, null);
  assert.equal(page.$('[data-confirm]').open, undefined);
});

test('the speaker saying the file is too large reads as a firmware size problem', async () => {
  const page = await open();
  pickFile(page, 'x.bin', 1000);
  button(page, 'upload').click();
  await confirmYes(page);
  FakeXhr.last.finish(413, { error: 'too_large', message: 'x' });
  await settle();
  assert.equal(fileStatus(page).textContent, text(page, 'err.update_too_large'));
});

test('a dropped upload connection is reported and the buttons come back', async () => {
  const page = await open();
  pickFile(page, 'x.bin', 1000);
  button(page, 'upload').click();
  await confirmYes(page);
  FakeXhr.last.onerror();
  await settle();
  assert.equal(fileStatus(page).textContent, text(page, 'err.network'));
  assert.equal(button(page, 'upload').disabled, false);
  assert.equal(button(page, 'check').disabled, false);
});

test('an upload answered with an expired login sends the person to the login screen', async () => {
  const page = await open();
  pickFile(page, 'x.bin', 1000);
  button(page, 'upload').click();
  await confirmYes(page);
  FakeXhr.last.finish(401, { error: 'login_required', message: 'log in first' });
  await settle();
  assert.equal(page.visible(page.$('[data-screen="login"]')), true);
});

test('logging out wipes the update block', async () => {
  const page = await open({ 'GET api/update': reply(200, NEWER) });
  await check(page);
  page.$('[data-action="logout"]').click();
  await settle();
  assert.equal(status(page).textContent, '');
  assert.equal(page.visible(button(page, 'install')), false);
  assert.equal(page.$('[data-update="latest"]').textContent, '—');
  assert.equal(button(page, 'check').disabled, false);
});

test('every update text exists in both languages and the new error codes are known', async () => {
  const page = await open();
  const H = page.window.PYTAICHENKO_PAGE;
  const keys = Object.keys(H.TEXT.uk).filter((k) => k.startsWith('update.'));
  assert.ok(keys.length >= 20);
  for (const lang of H.LANGS) {
    for (const key of keys) assert.ok(H.TEXT[lang][key], `${lang} ${key}`);
    for (const code of ['update_not_ours', 'update_failed', 'update_busy', 'update_too_large', 'no_release']) {
      assert.ok(H.ERROR_CODES.includes(code), code);
      assert.ok(H.t(lang, `err.${code}`).split(' ').length >= 4, `${lang} ${code}`);
    }
  }
});
