import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import {
  keysIn,
  loadScripts,
  makeElement,
  makeEnv,
  parseFragment,
  readPage,
  readSite,
  sectionHtml,
  siteDir,
  tagAttr,
  visibleText,
} from './helpers.mjs';

const repoDir = path.resolve(siteDir, '..');
const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js'];
const CYRILLIC = /[Ѐ-ӿ]/;
const OPENAI_KEYS_URL = 'https://platform.openai.com/api-keys';

function readRepo(relPath) {
  return readFileSync(path.join(repoDir, relPath), 'utf8');
}

function serverText() {
  return loadScripts(['assets/text.js']).TEXT;
}

function settingsLabels() {
  const app = readRepo('hub/src/voice_hub/web/app.js');
  const start = app.indexOf('\n  var TEXT = {\n    uk: {');
  const cut = app.indexOf('\n    en: {', start);
  const end = app.indexOf('\n  };', cut);
  assert.ok(start > 0 && cut > start && end > cut, 'app.js keeps its texts in TEXT, Ukrainian first, then English');
  const parts = { uk: app.slice(start, cut), en: app.slice(cut, end) };
  const label = (lang, key) => {
    const escaped = key.replace(/\./g, '\\.');
    const m = new RegExp(`^\\s*(?:'${escaped}'|${escaped}): (['"])(.*?)\\1,?$`, 'm').exec(parts[lang]);
    assert.ok(m, `app.js has no ${lang} text for ${key}`);
    return m[2].replace(/\\'/g, "'");
  };
  return label;
}

function codeBlocks(html) {
  return [...html.matchAll(/<pre class="cmd"[^>]*><code>([\s\S]*?)<\/code><\/pre>/g)].map((m) =>
    m[1]
      .replace(/<[^>]+>/g, '')
      .replace(/&lt;/g, '<')
      .replace(/&gt;/g, '>')
      .replace(/&amp;/g, '&')
      .replace(/&quot;/g, '"'),
  );
}

function serverKeys(TEXT, lang) {
  return Object.entries(TEXT[lang]).filter(([key]) => key.startsWith('server.'));
}

test('the server section is no longer a stub: what you need, how to start it, how to set it up and what to do if it did not work, in that order', () => {
  const section = sectionHtml(readPage(), 'server');
  assert.doesNotMatch(section, /soon-status|Готується/);
  const keys = keysIn(section);
  const heads = ['server.title', 'server.intro', 'server.need.title', 'server.run.title', 'server.setup.title', 'server.trouble.title'];
  for (const key of heads) assert.ok(keys.includes(key), `page misses ${key}`);
  const at = heads.map((key) => keys.indexOf(key));
  assert.deepEqual([...at].sort((a, b) => a - b), at, 'the parts come in order');
  assert.equal(at[0], 0);
  assert.equal(at[1], 1, 'the section opens with one sentence on what the server is');
  const TEXT = serverText();
  for (const lang of ['uk', 'en']) {
    assert.ok(!('server.status' in TEXT[lang]), `${lang}.server.status is gone`);
    assert.ok(!('server.text' in TEXT[lang]), `${lang}.server.text is gone`);
    const intro = TEXT[lang]['server.intro'];
    assert.equal(intro.split(/[.!?](?:\s|$)/).filter((s) => s.trim()).length, 1, `${lang} intro is one sentence`);
    assert.match(intro, /OpenAI/);
  }
  assert.equal(TEXT.uk['server.need.title'], 'Що потрібно');
  assert.equal(TEXT.en['server.need.title'], 'What you need');
  assert.equal(TEXT.uk['server.trouble.title'], 'Якщо не вийшло');
  assert.equal(TEXT.en['server.trouble.title'], 'If it did not work');
});

test('what you need: a Linux computer that stays on, Docker with Compose, and an OpenAI key paid at OpenAI prices with no figures that can go stale', () => {
  const section = sectionHtml(readPage(), 'server');
  const need = /data-i18n="server\.need\.title"[^>]*>[^<]*<\/h3>\s*<ul class="marks">([\s\S]*?)<\/ul>/.exec(section);
  assert.ok(need, 'what you need is a marked list right under its heading');
  const keys = keysIn(need[1]);
  for (const key of ['server.need.computer', 'server.need.docker', 'server.need.key']) assert.ok(keys.includes(key), key);
  const TEXT = serverText();
  assert.match(TEXT.uk['server.need.computer'], /Linux[\s\S]*працює постійно/);
  assert.match(TEXT.en['server.need.computer'], /Linux[\s\S]*stays on/);
  assert.match(TEXT.uk['server.need.computer'], /міні-ПК[\s\S]*Raspberry Pi 4 чи 5[\s\S]*64-бітн[\s\S]*NAS/);
  assert.match(TEXT.en['server.need.computer'], /mini PC[\s\S]*Raspberry Pi 4 or 5[\s\S]*64-bit[\s\S]*NAS/);
  assert.match(TEXT.uk['server.need.computer'], /тій самій мережі/);
  assert.match(TEXT.en['server.need.computer'], /same network/);
  for (const lang of ['uk', 'en']) {
    assert.match(TEXT[lang]['server.need.docker'], /Docker Compose/, lang);
    assert.match(TEXT[lang]['server.need.docker'], /\bgit\b/, lang);
    assert.doesNotMatch(TEXT[lang]['server.need.key'], /\d|\$|₴|USD|грн|cent|цент/i, `${lang} names no price`);
  }
  assert.match(TEXT.uk['server.need.key'], /Ключ API OpenAI[\s\S]*оплачуються[\s\S]*тарифами OpenAI/);
  assert.match(TEXT.en['server.need.key'], /OpenAI API key[\s\S]*paid[\s\S]*OpenAI prices/);
  const links = [...section.matchAll(/<a\b([^>]*)>/g)].map((m) => m[1]);
  assert.deepEqual(links.map((tag) => tagAttr(tag, 'href')), [OPENAI_KEYS_URL]);
  assert.equal(tagAttr(links[0], 'target'), '_blank');
  assert.match(tagAttr(links[0], 'rel'), /\bnoopener\b/);
  assert.equal(tagAttr(links[0], 'data-i18n'), 'server.need.key_link');
  assert.match(need[1], /server\.need\.key_link/, 'the key link sits by the key item');
});

test('the start commands are in <code>: git clone of CONFIG.repoUrl, cd into its folder, docker compose up -d', () => {
  const section = sectionHtml(readPage(), 'server');
  const { CONFIG } = loadScripts(['assets/config.js']);
  const run = /data-i18n="server\.run\.title"[\s\S]*?data-i18n="server\.setup\.title"/.exec(section);
  assert.ok(run, 'the start part sits between its heading and the setup heading');
  const blocks = codeBlocks(run[0]);
  assert.equal(blocks.length, 2, 'the three commands, then the command that adds the user to the docker group');
  assert.deepEqual(blocks[0].split('\n'), [`git clone ${CONFIG.repoUrl}`, 'cd pytaichenko', 'docker compose up -d']);
  assert.equal(blocks[1], 'sudo usermod -aG docker $USER');
  assert.ok(run[0].indexOf('server.run.perm') > run[0].indexOf('docker compose up -d'), 'the permission note comes right after the commands');
  assert.ok(run[0].indexOf('server.run.perm') < run[0].indexOf('server.run.after'));
  assert.match(run[0], /<span data-repo-url>https:\/\/github\.com\/feroxes\/pytaichenko<\/span>/);
  assert.match(run[0], /cd <span data-repo-dir>pytaichenko<\/span>/);
  const keys = keysIn(run[0]);
  for (const key of ['server.run.text', 'server.run.perm', 'server.run.after']) assert.ok(keys.includes(key), key);
  const TEXT = serverText();
  assert.match(TEXT.uk['server.run.perm'], /«permission denied»[\s\S]*групі docker[\s\S]*командою нижче[\s\S]*sudo/);
  assert.match(TEXT.en['server.run.perm'], /“permission denied”[\s\S]*docker group[\s\S]*command below[\s\S]*sudo/);
  assert.match(TEXT.uk['server.run.text'], /термінал[\s\S]*три команди/);
  assert.match(TEXT.en['server.run.text'], /terminal[\s\S]*three commands/);
  assert.match(TEXT.uk['server.run.after'], /кілька хвилин[\s\S]*сама[\s\S]*data/);
  assert.match(TEXT.en['server.run.after'], /a few minutes[\s\S]*by itself[\s\S]*data/);
});

test('commands live only in code blocks: no translated text carries a shell command', () => {
  const TEXT = serverText();
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of serverKeys(TEXT, lang)) {
      assert.doesNotMatch(value, /git clone|docker compose|\bcd \w|echo |VOICE_HUB_HOSTS=|VOICE_HUB_PORT=|usermod|ufw allow|>>/, `${lang}.${key}`);
    }
  }
  const section = sectionHtml(readPage(), 'server');
  for (const block of section.matchAll(/<pre class="cmd"[^>]*><code>([\s\S]*?)<\/code><\/pre>/g)) {
    assert.doesNotMatch(block[1], /data-i18n="(?!server\.setup\.address")/, 'a command block translates nothing but the address placeholder');
  }
  const blocks = codeBlocks(section);
  assert.ok(blocks.some((b) => b === 'docker compose ps'), 'the state check is a command block');
  assert.ok(blocks.some((b) => /^printf '\\nVOICE_HUB_HOSTS=[\w.-]+\\n' >> \.env\ndocker compose up -d$/.test(b)), 'the extra host name is a command block that starts on a new line of .env');
  assert.ok(blocks.some((b) => /^printf '\\nVOICE_HUB_PORT=\d+\\n' >> \.env\ndocker compose up -d$/.test(b)), 'the other port is a command block that starts on a new line of .env');
  assert.ok(!blocks.some((b) => /^echo .*>> \.env/m.test(b)), 'no echo that could glue onto the last line of .env');
  assert.ok(blocks.some((b) => /^sudo ufw allow from [\d./]+ to any port 8080 proto tcp$/.test(b)), 'the ufw rule is a command block');
  assert.ok(
    blocks.some((b) => /^sudo firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="[\d./]+" port port="8080" protocol="tcp" accept'\nsudo firewall-cmd --reload$/.test(b)),
    'the firewalld rule is a command block',
  );
});

test('applyConfig puts CONFIG.repoUrl and its folder into the clone commands, and leaves the static text when repoUrl is unusable', () => {
  const url = makeElement({ 'data-repo-url': '' }, 'static-url');
  const dir = makeElement({ 'data-repo-dir': '' }, 'static-dir');
  const env = makeEnv({ children: [url, dir] });
  const P = loadScripts(['assets/config.js'], env);
  P.applyConfig(env.document);
  assert.equal(url.textContent, 'https://github.com/feroxes/pytaichenko');
  assert.equal(dir.textContent, 'pytaichenko');
  P.CONFIG.repoUrl = 'https://github.com/example/voice-box.git';
  P.applyConfig(env.document);
  assert.equal(url.textContent, 'https://github.com/example/voice-box.git');
  assert.equal(dir.textContent, 'voice-box');
  P.CONFIG.repoUrl = 'https://github.com/example/other/';
  P.applyConfig(env.document);
  assert.equal(dir.textContent, 'other');
  for (const bad of ['', 'javascript:alert(1)', 'http://github.com/o/r']) {
    P.CONFIG.repoUrl = bad;
    P.applyConfig(env.document);
    assert.equal(url.textContent, 'https://github.com/example/other/', bad);
    assert.equal(dir.textContent, 'other', bad);
  }
});

test('the facts the page states match compose.yaml and the service defaults: build from ./hub, host network, data in ./data, restart unless stopped, port 8080', () => {
  const compose = readRepo('compose.yaml');
  assert.match(compose, /^\s*build: \.\/hub\s*$/m);
  assert.match(compose, /^\s*network_mode: host\s*$/m);
  assert.match(compose, /^\s*- \.\/data:\/data\s*$/m);
  assert.match(compose, /^\s*restart: unless-stopped\s*$/m);
  assert.match(compose, /VOICE_HUB_HOSTS/);
  assert.match(compose, /VOICE_HUB_PORT/);
  assert.match(readRepo('hub/src/voice_hub/settings.py'), /http_port: int = 8080/);
  assert.ok(codeBlocks(sectionHtml(readPage(), 'server')).includes('http://<адреса сервера>:8080'));
});

test('the setup steps go in order and quote the settings page labels as it shows them in each language', () => {
  const section = sectionHtml(readPage(), 'server');
  const list = /<ol class="steps">([\s\S]*?)<\/ol>/.exec(section);
  assert.ok(list, 'the setup steps are a numbered list');
  const steps = [...list[1].matchAll(/<li(?: data-i18n="([^"]+)">|>\s*<span data-i18n="([^"]+)">)/g)].map((m) => m[1] || m[2]);
  assert.deepEqual(steps, [
    'server.setup.open',
    'server.setup.password',
    'server.setup.key',
    'server.setup.adopt',
    'server.setup.wake',
    'server.setup.language',
  ]);
  assert.equal([...list[1].matchAll(/<li\b/g)].length, steps.length);
  assert.match(list[1], /<pre class="cmd" tabindex="0"><code>http:\/\/&lt;<span data-i18n="server\.setup\.address">[^<]+<\/span>&gt;:8080<\/code><\/pre>/);

  const TEXT = serverText();
  const label = settingsLabels();
  const quoted = {
    'server.setup.password': ['setup.submit'],
    'server.setup.key': ['nav.conversation', 'openai.key', 'save'],
    'server.setup.adopt': ['nav.speakers', 'found.title', 'found.adopt'],
    'server.setup.wake': ['speaker.wake'],
    'server.setup.language': ['nav.conversation', 'assistant.language', 'save'],
    'server.trouble.found.symptom': ['found.title'],
    'server.trouble.found.fix': ['manual.title', 'manual.host', 'manual.submit'],
    'server.trouble.desktop.after': ['nav.access', 'server.advertise', 'save'],
    'server.env.password': ['nav.conversation'],
    'server.trouble.hosts.symptom': ['host.title'],
    'server.trouble.ring.fix': ['nav.speakers', 'speaker.move'],
    'server.trouble.removed.fix': ['found.title'],
  };
  for (const [key, labels] of Object.entries(quoted)) {
    for (const name of labels) {
      assert.ok(TEXT.uk[key].includes(`«${label('uk', name)}»`), `uk ${key} quotes «${label('uk', name)}»`);
      assert.ok(TEXT.en[key].includes(`“${label('en', name)}”`), `en ${key} quotes “${label('en', name)}”`);
    }
  }
  assert.equal(TEXT.uk['server.setup.address'], 'адреса сервера');
  assert.equal(TEXT.en['server.setup.address'], 'server address');
  assert.match(TEXT.uk['server.setup.open'], /роутер/);
  assert.match(TEXT.en['server.setup.open'], /router/);
  assert.match(TEXT.uk['server.setup.key'], /назад не показує/);
  assert.match(TEXT.en['server.setup.key'], /never shows it back/);
  assert.match(TEXT.uk['server.setup.language'], /слухає й відповідає мовою, якою була сторінка налаштувань, коли ви створили пароль/);
  assert.match(TEXT.en['server.setup.language'], /listens and answers in the language the settings page was in when you created the password/);
});

test('the adopt step answers the firmware section: right after flashing, the speaker gets its own key from the server and the ring goes dark', () => {
  const TEXT = serverText();
  assert.match(TEXT.uk['firmware.next.server'], /розділі «Сервер»/);
  assert.equal(TEXT.uk['server.title'], 'Сервер');
  assert.equal(TEXT.en['server.title'], 'Server');
  assert.match(TEXT.uk['server.setup.adopt'], /^Одразу після прошивки[\s\S]*власний ключ шифрування[\s\S]*кільце гасне й колонка чекає ключову фразу/);
  assert.match(TEXT.en['server.setup.adopt'], /^Right after flashing[\s\S]*its own encryption key[\s\S]*ring goes dark and the speaker waits for the wake word/);
});

test('the wake phrase step and “What it does” name the three phrases the shared firmware carries', () => {
  const models = [...readRepo('firmware/packages/wake_word.yaml').matchAll(/^\s*- model: \S*?(\w+)\.json/gm)].map((m) => m[1]);
  assert.deepEqual(models, ['hey_jarvis', 'okay_nabu', 'hey_mycroft']);
  const phrases = models.map((id) => id.split('_').map((w) => w[0].toUpperCase() + w.slice(1)).join(' '));
  const TEXT = serverText();
  const page = readPage();
  const can = sectionHtml(page, 'can');
  const items = [...can.matchAll(/<li data-i18n="([^"]+)">/g)].map((m) => m[1]);
  assert.equal(items[0], 'can.item1');
  assert.equal(items[1], 'can.wake', 'the phrase choice sits right after “starts with a wake phrase”');
  const joins = { uk: 'або', en: 'or' };
  for (const lang of ['uk', 'en']) {
    for (const key of ['can.wake', 'server.setup.wake']) {
      const text = TEXT[lang][key];
      assert.ok(text.includes(`${phrases[0]}, ${phrases[1]} ${joins[lang]} ${phrases[2]}`), `${lang}.${key} lists ${phrases.join(', ')}`);
    }
  }
  assert.match(TEXT.uk['can.wake'], /^Ключову фразу можна обрати з трьох/);
  assert.match(TEXT.en['can.wake'], /^The wake phrase can be one of three/);
});

test('if it did not work: add by address, Docker Desktop, VOICE_HUB_HOSTS, the ring breathing once every 4 s, and a removed speaker flashed again with erase', () => {
  const section = sectionHtml(readPage(), 'server');
  const list = /data-i18n="server\.trouble\.title"[^>]*>[^<]*<\/h3>\s*<dl class="trouble">([\s\S]*?)<\/dl>/.exec(section);
  assert.ok(list, 'the list looks like the one in the firmware section');
  const names = ['found', 'desktop', 'hosts', 'port', 'firewall', 'ring', 'removed'];
  assert.deepEqual(
    keysIn(list[1]),
    names.flatMap((n) => [`server.trouble.${n}.symptom`, `server.trouble.${n}.fix`, ...(n === 'desktop' ? ['server.trouble.desktop.after'] : [])]),
  );
  const items = [...list[1].matchAll(/<div class="trouble-item">\s*<dt\b[\s\S]*?<\/dd>\s*<\/div>/g)];
  assert.equal(items.length, names.length);
  const TEXT = serverText();
  assert.match(TEXT.uk['server.trouble.found.fix'], /роутер[\s\S]*pytaichenko[\s\S]*поле ключа залиште порожнім/);
  assert.match(TEXT.en['server.trouble.found.fix'], /router[\s\S]*pytaichenko[\s\S]*leave the key field empty/);
  assert.match(TEXT.uk['server.trouble.desktop.symptom'], /Docker Desktop[\s\S]*macOS[\s\S]*Windows/);
  assert.match(TEXT.en['server.trouble.desktop.symptom'], /Docker Desktop[\s\S]*macOS[\s\S]*Windows/);
  assert.match(TEXT.uk['server.trouble.desktop.fix'], /мережу хоста[\s\S]*пошук колонок[\s\S]*може не спрацювати[\s\S]*compose\.override\.yaml[\s\S]*compose\.yaml не міняйте/);
  assert.match(TEXT.en['server.trouble.desktop.fix'], /host networking[\s\S]*search for speakers[\s\S]*may not work[\s\S]*compose\.override\.yaml[\s\S]*leave compose\.yaml itself as it is/);
  assert.match(TEXT.uk['server.trouble.desktop.after'], /«Доступ»[\s\S]*без http:\/\/ і порту[\s\S]*перезапустіть службу[\s\S]*вручну за адресою[\s\S]*лише рядком VOICE_HUB_PORT/);
  assert.match(TEXT.en['server.trouble.desktop.after'], /“Access”[\s\S]*without http:\/\/ and the port[\s\S]*restart the service[\s\S]*by hand by its address[\s\S]*only with the VOICE_HUB_PORT line/);
  assert.match(TEXT.uk['server.trouble.hosts.fix'], /IP-адресою[\s\S]*\.local[\s\S]*\.env[\s\S]*через кому/);
  assert.match(TEXT.en['server.trouble.hosts.fix'], /IP address[\s\S]*\.local[\s\S]*\.env[\s\S]*commas/);
  assert.match(TEXT.uk['server.trouble.ring.symptom'], /раз на 4 с/);
  assert.match(TEXT.en['server.trouble.ring.symptom'], /once every 4 s/);
  assert.match(TEXT.uk['server.trouble.ring.fix'], /не під'єднаний[\s\S]*одній мережі[\s\S]*служба працює/);
  assert.match(TEXT.en['server.trouble.ring.fix'], /not connected[\s\S]*same network[\s\S]*service is running/);
  assert.match(TEXT.uk['server.trouble.removed.fix'], /забуває її ключ[\s\S]*«Прошивка»/);
  assert.match(TEXT.en['server.trouble.removed.fix'], /forgets its key[\s\S]*“Firmware”/);
  const installer = { uk: (s) => `«${s}»`, en: (s) => `“${s}”` };
  for (const lang of ['uk', 'en']) {
    const q = installer[lang];
    const text = TEXT[lang]['server.trouble.removed.fix'];
    const path = [
      TEXT[lang]['firmware.button'],
      'Erase User Data',
      'Erase User Data',
      'Update Pytaichenko.Speaker',
      'Install',
      'Next',
      'Erase User Data',
      'Install Pytaichenko.Speaker',
      'Erase device',
      'Next',
      'Install',
    ];
    let at = -1;
    for (const name of path) {
      const next = text.indexOf(q(name), at + 1);
      assert.ok(next > at, `${lang} removed.fix walks the installer in order: ${q(name)} after position ${at}`);
      at = next;
    }
    assert.ok(text.indexOf(q('Erase User Data')) < text.indexOf(q('Erase device')), `${lang} offers “Erase User Data” before “Erase device”`);
  }
  const blocks = codeBlocks(list[1]);
  assert.equal(blocks.length, 8);
  const desktop = /server\.trouble\.desktop\.fix([\s\S]*?)server\.trouble\.hosts\.symptom/.exec(list[1])[1];
  assert.deepEqual(codeBlocks(desktop), [
    'services:\n  voice-hub:\n    network_mode: bridge\n    ports:\n      - "${VOICE_HUB_PORT:-8080}:${VOICE_HUB_PORT:-8080}"',
    'docker compose up -d',
    'docker compose restart',
  ], 'the override file, the start with it, and the restart after the address is saved');
  assert.ok(desktop.indexOf('docker compose up -d') < desktop.indexOf('server.trouble.desktop.after'), 'the address step comes after the start');
  assert.match(list[1], /server\.trouble\.hosts\.fix[\s\S]*VOICE_HUB_HOSTS[\s\S]*server\.trouble\.port\.symptom/, 'the host command sits in its own item');
  assert.match(list[1], /server\.trouble\.port\.fix[\s\S]*VOICE_HUB_PORT[\s\S]*server\.trouble\.firewall\.symptom/, 'the port command sits in its own item');
  assert.match(list[1], /server\.trouble\.firewall\.fix[\s\S]*ufw allow[\s\S]*firewall-cmd --permanent[\s\S]*firewall-cmd --reload[\s\S]*server\.trouble\.ring\.symptom/, 'the firewall commands sit in their own item, ufw first');
  assert.match(TEXT.uk['server.trouble.port.symptom'], /порт 8080 зайнятий/);
  assert.match(TEXT.en['server.trouble.port.symptom'], /port 8080 is taken/);
  assert.match(TEXT.uk['server.trouble.port.fix'], /рядок VOICE_HUB_PORT[\s\S]*\.env[\s\S]*від 1024 до 65535[\s\S]*перевагу над портом, збереженим на сторінці налаштувань[\s\S]*а не додавайте ще один/);
  assert.match(TEXT.en['server.trouble.port.fix'], /VOICE_HUB_PORT line[\s\S]*\.env[\s\S]*1024 to 65535[\s\S]*precedence over the port saved on the settings page[\s\S]*instead of adding another line/);
  assert.match(TEXT.uk['server.trouble.firewall.symptom'], /на самому сервері[\s\S]*з інших пристроїв ні[\s\S]*колонка вас чує й кільце «дихає», але відповіді не чути/);
  assert.match(TEXT.en['server.trouble.firewall.symptom'], /on the server itself but not from other devices[\s\S]*the speaker hears you and the ring breathes, but no answer plays/);
  assert.match(TEXT.uk['server.trouble.firewall.fix'], /VOICE_HUB_PORT[\s\S]*колонка бере звук відповіді[\s\S]*ufw[\s\S]*firewalld[\s\S]*8080 — на ваш порт/);
  assert.match(TEXT.en['server.trouble.firewall.fix'], /VOICE_HUB_PORT[\s\S]*the speaker fetches the spoken answer[\s\S]*ufw[\s\S]*firewalld[\s\S]*8080 with your port/);
  assert.match(TEXT.uk['server.trouble.firewall.fix'], /брандмауер[\s\S]*8080[\s\S]*для домашньої мережі[\s\S]*ufw[\s\S]*Не відкривайте порт для всього інтернету/);
  assert.match(TEXT.en['server.trouble.firewall.fix'], /firewall[\s\S]*8080[\s\S]*from the home network[\s\S]*ufw[\s\S]*Do not open the port to the whole internet/);
  assert.match(list[1], /server\.trouble\.ring\.fix[\s\S]*docker compose ps[\s\S]*server\.trouble\.removed\.symptom/, 'the state command sits in its own item');
});

test('switching to English leaves no Cyrillic in the server section, and Ukrainian comes back', () => {
  const fragment = parseFragment(sectionHtml(readPage(), 'server'));
  const env = makeEnv({ children: [fragment], languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  const ukText = visibleText(fragment);
  assert.match(ukText, CYRILLIC);
  const translated = fragment.querySelectorAll('[data-i18n]');
  assert.ok(translated.length >= 25, `expected many translated texts, got ${translated.length}`);

  P.setLang('en');
  for (const el of translated) assert.equal(el.textContent, P.TEXT.en[el.getAttribute('data-i18n')]);
  const enText = visibleText(fragment);
  const cyr = enText.match(new RegExp(`.{0,30}${CYRILLIC.source}.{0,30}`));
  assert.equal(cyr, null, `Cyrillic left in English: ${cyr && cyr[0]}`);

  P.setLang('uk');
  assert.equal(visibleText(fragment), ukText);
});

test('the server texts stay calm: no exclamation marks, nothing urgent, nothing to sell', () => {
  const TEXT = serverText();
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of [...serverKeys(TEXT, lang), [`can.wake`, TEXT[lang]['can.wake']]]) {
      assert.doesNotMatch(value, /!/, `${lang}.${key}`);
      assert.doesNotMatch(value, /терміново|негайно|обов'язково|найкращ|безкоштовн|urgent|immediately|must\b|best\b|free\b/i, `${lang}.${key}`);
    }
  }
});

test('every command block can be reached with the keyboard, so it can be scrolled sideways without a mouse', () => {
  const page = readPage();
  const blocks = [...page.matchAll(/<pre\b([^>]*)>/g)].map((m) => m[1]);
  assert.ok(blocks.length >= 5);
  for (const attrs of blocks) {
    assert.match(attrs, /class="cmd"/);
    assert.equal(tagAttr(attrs, 'tabindex'), '0', attrs);
  }
});

test('extra host names: one VOICE_HUB_HOSTS line in .env, names joined by commas, no second line', () => {
  const TEXT = serverText();
  assert.match(TEXT.uk['server.trouble.hosts.fix'], /за домашнім ім'ям, наприклад із закінченням \.local чи \.lan\./);
  assert.doesNotMatch(TEXT.uk['server.trouble.hosts.fix'], /на \.local/);
  assert.match(TEXT.uk['server.trouble.hosts.fix'], /в одному рядку через кому[\s\S]*рядок VOICE_HUB_HOSTS[\s\S]*уже є[\s\S]*а не додавайте ще один/);
  assert.match(TEXT.en['server.trouble.hosts.fix'], /on one line, separated by commas[\s\S]*already has a VOICE_HUB_HOSTS line[\s\S]*instead of adding another line/);
  for (const lang of ['uk', 'en']) assert.doesNotMatch(TEXT[lang]['server.trouble.hosts.fix'], /:$/, lang);
});

test('command blocks scroll sideways inside themselves instead of widening the page, and the stub style is gone', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /\.cmd\s*\{[^}]*overflow-x:\s*auto/);
  assert.match(css, /\.cmd\s*\{[^}]*white-space:\s*pre/);
  assert.match(css, /\.cmd\s*\{[^}]*font-family:\s*ui-monospace/);
  assert.doesNotMatch(css, /\.soon-status/);
});

test('the first conversation follows the setup steps and tells what the ring, the tones and the top button do, as the firmware does', () => {
  const section = sectionHtml(readPage(), 'server');
  const keys = keysIn(section);
  const at = (key) => keys.indexOf(key);
  assert.ok(at('server.setup.language') < at('server.first.title') && at('server.first.title') < at('server.trouble.title'), 'between the setup steps and the troubleshooting');
  assert.deepEqual(keys.slice(at('server.first.title'), at('server.first.ring.caption')), [
    'server.first.title',
    'server.first.say',
    'server.first.ask',
    'server.first.more',
    'server.first.stop',
  ]);
  const table = /<table class="ring-table">([\s\S]*?)<\/table>/.exec(section);
  assert.ok(table, 'the ring table');
  assert.match(section, /<div class="table-wrap">\s*<table class="ring-table">/);
  const rows = [...table[1].matchAll(/<tr>\s*<td data-i18n="([^"]+)">[^<]*<\/td><td data-i18n="([^"]+)">[^<]*<\/td><\/tr>/g)].map((m) => [m[1], m[2]]);
  assert.deepEqual(rows, [1, 2, 3, 4, 5, 6, 7].map((n) => [`server.first.r${n}.look`, `server.first.r${n}.mean`]));
  assert.deepEqual(keysIn(table[1]).slice(0, 3), ['server.first.ring.caption', 'server.first.col.ring', 'server.first.col.meaning']);

  const TEXT = serverText();
  assert.equal(TEXT.uk['server.first.title'], 'Перша розмова');
  assert.equal(TEXT.en['server.first.title'], 'The first conversation');
  assert.match(TEXT.uk['server.first.say'], /ключову фразу або коротко натисніть кнопку зверху[\s\S]*угору[\s\S]*кільце засвітиться повністю/);
  assert.match(TEXT.en['server.first.say'], /wake phrase you chose or press the button on top[\s\S]*rises[\s\S]*ring lights up fully/);
  assert.match(TEXT.uk['server.first.ask'], /трохи після сигналу[\s\S]*«дихає»[\s\S]*колонка думає[\s\S]*трохи тьмяніше/);
  assert.match(TEXT.en['server.first.ask'], /a moment after the signal[\s\S]*“breathes”[\s\S]*thinking[\s\S]*a little dimmer/);
  assert.match(TEXT.uk['server.first.more'], /без ключової фрази[\s\S]*слабко/);
  assert.match(TEXT.en['server.first.more'], /without the wake phrase[\s\S]*faintly/);
  assert.match(TEXT.uk['server.first.stop'], /«Дякую, все»[\s\S]*униз[\s\S]*кільце згасне[\s\S]*Кнопка зверху/);
  assert.match(TEXT.uk['server.first.stop'], /а коли колонка мовчить, кнопка її починає\.$/);
  assert.doesNotMatch(TEXT.uk['server.first.stop'], /вона її починає/);
  assert.match(TEXT.en['server.first.stop'], /“Thank you, that’s all”[\s\S]*falls[\s\S]*ring goes out[\s\S]*The button on top/);
  assert.equal(TEXT.uk['can.item5'], '«Дякую, все» завершує розмову');
  assert.match(TEXT.uk['server.first.r1.look'], /раз на 4 с/);
  assert.match(TEXT.en['server.first.r1.look'], /once every 4 s/);
  assert.match(TEXT.uk['server.first.r1.mean'], /сервер не під'єднаний[\s\S]*«Якщо не вийшло»/);
  assert.match(TEXT.en['server.first.r1.mean'], /server is not connected[\s\S]*“If it did not work”/);
  assert.match(TEXT.uk['server.first.r7.look'], /три м'які спалахи/);
  assert.match(TEXT.en['server.first.r7.look'], /three soft flashes/);
  assert.ok(TEXT.uk['server.first.r1.mean'].includes(`«${TEXT.uk['server.trouble.title']}»`));
  assert.ok(TEXT.en['server.first.r1.mean'].includes(`“${TEXT.en['server.trouble.title']}”`));

  const ring = readRepo('firmware/packages/ring.yaml');
  const assistant = readRepo('firmware/packages/assistant.yaml');
  const buttons = readRepo('firmware/packages/buttons.yaml');
  assert.match(ring, /pulse: \{name: Offline, transition_length: 2s, update_interval: 2s, min_brightness: 0%, max_brightness: 70%\}/, 'breathes slowly once every 4 s');
  assert.doesNotMatch(assistant + ring, /id: ring, effect: Offline\}/, 'the breathing starts from dark, not from the last brightness');
  assert.match(assistant + ring, /id: ring, effect: Offline, brightness: 1%\}/);
  assert.match(assistant, /id: show_error[\s\S]*brightness: 100%, transition_length: 200ms[\s\S]*turn_off: \{id: ring, transition_length: 250ms\}/, 'soft flashes on an error');
  assert.match(ring, /pulse: \{name: Thinking[^}]*min_brightness: 20%[^}]*max_brightness: 80%/, 'breathes while thinking');
  assert.match(assistant, /on_listening:[\s\S]*return id\(follow_up\)[\s\S]*brightness: 40%[\s\S]*else:[\s\S]*brightness: 100%/, 'dim while waiting to go on, full while listening');
  assert.match(assistant, /on_intent_progress:[\s\S]*brightness: 70%/, 'a little dimmer while answering');
  assert.match(assistant, /id: show_error[\s\S]*repeat:\s*count: 3/, 'three flashes on an error');
  assert.match(assistant, /on_client_connected:\s*- light\.turn_off: led\s*- light\.turn_off: ring/, 'off while waiting for the phrase');
  assert.match(buttons, /top_button[\s\S]*GPIO19[\s\S]*toggle_listen/);
  assert.match(assistant, /id: toggle_listen[\s\S]*voice_assistant\.stop:[\s\S]*start_listening/, 'the button stops a running conversation and starts one when idle');
});

test('the root READMEs leave the server commands to the site, which tells about the docker group right after them', () => {
  for (const [file, lang] of [['README.md', 'en'], ['README.uk.md', 'uk']]) {
    const readme = readRepo(file);
    assert.doesNotMatch(readme, /docker compose/, `${file} has no server commands of its own`);
    assert.ok(readme.includes(`https://feroxes.github.io/pytaichenko/?lang=${lang}#server`), `${file} links to the server section`);
  }
  const run = /data-i18n="server\.run\.title"[\s\S]*?data-i18n="server\.setup\.title"/.exec(sectionHtml(readPage(), 'server'))[0];
  const commands = run.indexOf('docker compose up -d');
  const note = run.indexOf('server.run.perm');
  assert.ok(commands > 0 && note > commands && run.indexOf('sudo usermod -aG docker $USER') > note);
});

test('updating the server: its own part after the first conversation, one command that pulls, rebuilds and restarts', () => {
  const section = sectionHtml(readPage(), 'server');
  const keys = keysIn(section);
  const at = (key) => keys.indexOf(key);
  assert.ok(at('server.first.title') < at('server.update.title') && at('server.update.title') < at('server.env.title') && at('server.env.title') < at('server.trouble.title'));
  const part = /data-i18n="server\.update\.title"[\s\S]*?data-i18n="server\.env\.title"/.exec(section)[0];
  assert.deepEqual(codeBlocks(part), ['git pull && docker compose up -d --build']);
  const TEXT = serverText();
  assert.equal(TEXT.uk['server.update.title'], 'Оновлення сервера');
  assert.equal(TEXT.en['server.update.title'], 'Updating the server');
  assert.match(TEXT.uk['server.update.text'], /нову версію[\s\S]*у папці проєкту[\s\S]*збере службу заново[\s\S]*папці data/);
  assert.match(TEXT.en['server.update.text'], /new version[\s\S]*project folder[\s\S]*rebuilds the service[\s\S]*data folder/);
});

test('the environment variables: the four that compose.yaml passes, one line each, in .env, then the start command', () => {
  const section = sectionHtml(readPage(), 'server');
  const part = /data-i18n="server\.env\.title"[\s\S]*?data-i18n="server\.trouble\.title"/.exec(section)[0];
  const list = /<dl class="env">([\s\S]*?)<\/dl>/.exec(part);
  assert.ok(list, 'the variables are a list');
  const pairs = [...list[1].matchAll(/<dt>([A-Z_]+)<\/dt>\s*<dd data-i18n="([^"]+)">/g)].map((m) => [m[1], m[2]]);
  const names = ['OPENAI_API_KEY', 'VOICE_HUB_PASSWORD', 'VOICE_HUB_HOSTS', 'VOICE_HUB_PORT'];
  assert.deepEqual(pairs.map((p) => p[0]), names);
  assert.deepEqual(pairs.map((p) => p[1]), ['server.env.key', 'server.env.password', 'server.env.hosts', 'server.env.port']);
  assert.equal([...list[1].matchAll(/<dt\b/g)].length, names.length);
  const compose = readRepo('compose.yaml');
  for (const name of names) assert.match(compose, new RegExp(`${name}: \\$\\{${name}:-\\}`), `compose.yaml passes ${name}`);
  assert.deepEqual(codeBlocks(part), ['docker compose up -d']);
  assert.ok(part.indexOf('server.env.restart') < part.indexOf('docker compose up -d'));
  const TEXT = serverText();
  assert.equal(TEXT.uk['server.env.title'], 'Змінні середовища');
  assert.equal(TEXT.en['server.env.title'], 'Environment variables');
  assert.match(TEXT.uk['server.env.text'], /необов'язкові[\s\S]*\.env[\s\S]*окремим рядком[\s\S]*одинарні лапки/);
  assert.match(TEXT.en['server.env.text'], /optional[\s\S]*\.env[\s\S]*one per line[\s\S]*single quotes/);
  assert.match(TEXT.uk['server.env.key'], /лише для першого запуску[\s\S]*на сторінці налаштувань/);
  assert.match(TEXT.en['server.env.key'], /first start only[\s\S]*on the settings page/);
  assert.match(TEXT.uk['server.env.password'], /не просить створити пароль[\s\S]*лише цією змінною[\s\S]*забули[\s\S]*спершу українська/);
  assert.match(TEXT.en['server.env.password'], /does not ask you to create one[\s\S]*only through this variable[\s\S]*forgot[\s\S]*starts as Ukrainian/);
  assert.ok(TEXT.uk['server.env.hosts'].includes(`«${TEXT.uk['server.trouble.title']}»`));
  assert.ok(TEXT.en['server.env.hosts'].includes(`“${TEXT.en['server.trouble.title']}”`));
  assert.match(TEXT.uk['server.env.port'], /від 1024 до 65535[\s\S]*8080 зайнятий[\s\S]*перевагу/);
  assert.match(TEXT.en['server.env.port'], /1024 to 65535[\s\S]*8080 is taken[\s\S]*precedence/);
  const css = readSite('assets/style.css');
  assert.match(css, /\.env dt\s*\{[^}]*font-family:\s*ui-monospace/);
});

test('the site is the one guide: no text points to a README', () => {
  const TEXT = serverText();
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) assert.doesNotMatch(value, /README/, `${lang}.${key}`);
  }
  assert.doesNotMatch(readPage(), /README/);
});
