import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { chmodSync, cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { keysIn, loadScripts, makeEnv, parseFragment, readPage, readSite, sectionHtml, siteDir, tagAttr, visibleText } from './helpers.mjs';

const repoDir = path.resolve(siteDir, '..');
const script = path.join(repoDir, 'v1', 'firmware', 'build-web.sh');
const yamlPath = path.join(repoDir, 'v1', 'firmware', 'pytaichenko.yaml');

function readRepo(relPath) {
  return readFileSync(path.join(repoDir, relPath), 'utf8');
}

function withTemp(fn) {
  const dir = mkdtempSync(path.join(tmpdir(), 'pytaichenko-web-'));
  try {
    return fn(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function runScript(args, env) {
  return spawnSync('bash', [script, ...args], {
    cwd: repoDir,
    env: { ...process.env, ...env },
    encoding: 'utf8',
  });
}

const PROJECT_NAME = 'Pytaichenko.Speaker';

function firmwareProject() {
  const m = /^esphome:\n(?:  .*\n)*?  project:\n    name: (.+)\n    version: "?([^"\n]+)"?\n/m.exec(readRepo('v1/firmware/pytaichenko.yaml'));
  assert.ok(m, 'v1/firmware/pytaichenko.yaml sets esphome: project: name and version');
  return { name: m[1], version: m[2] };
}

function fakeEsphome(dir, body) {
  const file = path.join(dir, 'fake-esphome');
  writeFileSync(file, `#!/bin/sh\n${body}\n`);
  chmodSync(file, 0o755);
  return file;
}

test('the firmware manifest built by build-web.sh uses relative part paths for one ESP32 build', () => {
  withTemp((dir) => {
    const out = path.join(dir, 'out');
    const result = runScript(['--manifest-only'], { WEB_OUT_DIR: out });
    assert.equal(result.status, 0, result.stderr);

    const manifest = JSON.parse(readFileSync(path.join(out, 'manifest.json'), 'utf8'));
    const project = firmwareProject();
    assert.equal(project.name, PROJECT_NAME);
    assert.match(project.version, /^\d+\.\d+\.\d+$/);
    assert.equal(manifest.name, project.name, 'ESP Web Tools offers “Update” only when the name matches what the speaker reports');
    assert.equal(manifest.version, project.version);
    assert.equal(manifest.new_install_prompt_erase, true);
    assert.equal(manifest.new_install_improv_wait_time, 15);

    assert.equal(manifest.builds.length, 1);
    const [build] = manifest.builds;
    assert.deepEqual(Object.keys(build).sort(), ['chipFamily', 'parts']);
    assert.equal(build.chipFamily, 'ESP32');
    assert.equal(build.parts.length, 1);
    assert.deepEqual(build.parts[0], { path: 'pytaichenko.factory.bin', offset: 0 });

    for (const part of manifest.builds.flatMap((b) => b.parts)) {
      assert.ok(!part.path.startsWith('/'), `${part.path} must not be absolute`);
      assert.ok(!/^https?:/i.test(part.path), `${part.path} must not be a URL`);
      assert.ok(!part.path.includes('..'), `${part.path} must stay beside the manifest`);
    }
  });
});

test('build-web.sh --manifest-only neither compiles nor copies a binary', () => {
  withTemp((dir) => {
    const out = path.join(dir, 'out');
    const marker = path.join(dir, 'called');
    const esphome = fakeEsphome(dir, `touch "${marker}"\nexit 1`);
    const result = runScript(['--manifest-only'], { WEB_OUT_DIR: out, ESPHOME_BIN: esphome });
    assert.equal(result.status, 0, result.stderr);
    assert.ok(!existsSync(marker), 'esphome must not run');
    assert.ok(existsSync(path.join(out, 'manifest.json')));
    assert.ok(!existsSync(path.join(out, 'pytaichenko.factory.bin')));
  });
});

test('build-web.sh compiles pytaichenko.yaml and copies the factory image beside the manifest', () => {
  withTemp((dir) => {
    const out = path.join(dir, 'out');
    const factory = path.join(dir, 'build', 'firmware.factory.bin');
    const args = path.join(dir, 'args');
    const esphome = fakeEsphome(
      dir,
      `printf '%s\\n' "$@" > "${args}"\nmkdir -p "$(dirname "${factory}")"\nprintf 'image-bytes' > "${factory}"`,
    );
    const result = runScript([], { WEB_OUT_DIR: out, ESPHOME_BIN: esphome, FACTORY_BIN: factory });
    assert.equal(result.status, 0, result.stderr);
    assert.deepEqual(readFileSync(args, 'utf8').trim().split('\n'), ['compile', yamlPath]);
    assert.equal(readFileSync(path.join(out, 'pytaichenko.factory.bin'), 'utf8'), 'image-bytes');
    const manifest = JSON.parse(readFileSync(path.join(out, 'manifest.json'), 'utf8'));
    assert.equal(manifest.builds[0].parts[0].path, 'pytaichenko.factory.bin');
  });
});

test('build-web.sh fails and writes nothing when the compile leaves no factory image', () => {
  withTemp((dir) => {
    const out = path.join(dir, 'out');
    const esphome = fakeEsphome(dir, 'exit 0');
    const result = runScript([], {
      WEB_OUT_DIR: out,
      ESPHOME_BIN: esphome,
      FACTORY_BIN: path.join(dir, 'missing', 'firmware.factory.bin'),
    });
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /factory/i);
    assert.ok(!existsSync(path.join(out, 'manifest.json')));
  });
});

test('without WEB_OUT_DIR build-web.sh writes the installer into site/firmware/v1, the folder the version 1 button reads', () => {
  withTemp((dir) => {
    mkdirSync(path.join(dir, 'v1', 'firmware'), { recursive: true });
    for (const file of ['build-web.sh', 'pytaichenko.yaml']) cpSync(path.join(repoDir, 'v1', 'firmware', file), path.join(dir, 'v1', 'firmware', file));
    const env = { ...process.env };
    delete env.WEB_OUT_DIR;
    const result = spawnSync('bash', [path.join(dir, 'v1', 'firmware', 'build-web.sh'), '--manifest-only'], { cwd: dir, env, encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    const manifest = JSON.parse(readFileSync(path.join(dir, 'site', 'firmware', 'v1', 'manifest.json'), 'utf8'));
    assert.equal(manifest.name, PROJECT_NAME);
    assert.ok(!existsSync(path.join(dir, 'site', 'firmware', 'manifest.json')));
  });
});

test('build-web.sh rejects unknown arguments', () => {
  const result = runScript(['--nope'], {});
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /usage/i);
});

test('build-web.sh --manifest-only --version --out writes the manifest of a published release into the given folder', () => {
  withTemp((dir) => {
    const env = path.join(dir, 'env');
    const out = path.join(dir, 'out');
    const result = runScript(['--manifest-only', '--version', '1.4.2', '--out', out], { WEB_OUT_DIR: env });
    assert.equal(result.status, 0, result.stderr);
    const manifest = JSON.parse(readFileSync(path.join(out, 'manifest.json'), 'utf8'));
    assert.equal(manifest.name, PROJECT_NAME);
    assert.equal(manifest.version, '1.4.2');
    assert.deepEqual(manifest.builds[0].parts, [{ path: 'pytaichenko.factory.bin', offset: 0 }]);
    assert.ok(!existsSync(env), '--out wins over WEB_OUT_DIR');
    const same = runScript(['--out', out, '--version', '1.4.3', '--manifest-only'], {});
    assert.equal(same.status, 0, same.stderr);
    assert.equal(JSON.parse(readFileSync(path.join(out, 'manifest.json'), 'utf8')).version, '1.4.3');
    const plain = runScript(['--manifest-only', '--out', out], {});
    assert.equal(plain.status, 0, plain.stderr);
    assert.equal(JSON.parse(readFileSync(path.join(out, 'manifest.json'), 'utf8')).version, firmwareProject().version);
  });
});

test('build-web.sh takes --version only with --manifest-only and only as N.N.N, and writes nothing otherwise', () => {
  withTemp((dir) => {
    const out = path.join(dir, 'out');
    const esphome = fakeEsphome(dir, 'exit 1');
    for (const args of [['--version', '1.0.1'], ['--version', '1.0.1', '--out', out], ['--manifest-only', '--version'], ['--manifest-only', '--out'], ['--manifest-only', '--out', '']]) {
      const result = runScript(args, { WEB_OUT_DIR: out, ESPHOME_BIN: esphome });
      assert.equal(result.status, 2, args.join(' '));
      assert.match(result.stderr, /^usage: build-web\.sh/, args.join(' '));
    }
    for (const bad of ['1.0', 'v1.0.1', '1.0.1-dev', '']) {
      const result = runScript(['--manifest-only', '--version', bad, '--out', out], {});
      assert.equal(result.status, 1, bad);
      assert.equal(result.stderr.trim().split('\n').length, 1, bad);
    }
    assert.ok(!existsSync(out));
  });
});

test('the generated installer files and archives of every version are kept out of git', () => {
  const files = [
    'site/firmware/v1/manifest.json',
    'site/firmware/v1/pytaichenko.factory.bin',
    'site/firmware/v2/manifest.json',
    'site/firmware/v2/bootloader.bin',
    'site/firmware/v2/partition-table.bin',
    'site/firmware/v2/ota_data_initial.bin',
    'site/firmware/v2/pytaichenko.bin',
    'site/files/v1/pytaichenko-stl.zip',
    'site/files/v2/pytaichenko-stl.zip',
  ];
  const ignored = spawnSync('git', ['check-ignore', ...files], { cwd: repoDir, encoding: 'utf8' });
  assert.equal(ignored.status, 0);
  assert.deepEqual(ignored.stdout.trim().split('\n'), files);
  const tracked = execFileSync('git', ['ls-files', 'site/firmware', 'site/files'], { cwd: repoDir, encoding: 'utf8' });
  assert.equal(tracked.trim(), '');
});

function restoreKeys(yaml) {
  const lines = yaml.split('\n');
  const found = [];
  lines.forEach((line, i) => {
    const m = /^(\s*)restore-keys:\s*(.*?)\s*$/.exec(line);
    if (!m) return;
    if (m[2] && !/^[|>][-+]?$/.test(m[2])) {
      found.push(m[2]);
      return;
    }
    for (const next of lines.slice(i + 1)) {
      if (next.trim() === '') continue;
      if (next.search(/\S/) <= m[1].length) break;
      found.push(next.trim());
    }
  });
  return found;
}

test('the restore-keys reader sees inline values and every line of a block', () => {
  assert.deepEqual(restoreKeys('      restore-keys: a-\n      x: 1\n'), ['a-']);
  assert.deepEqual(restoreKeys('      restore-keys: |\n        a-\n\n        b-\n      x: 1\n'), ['a-', 'b-']);
  assert.deepEqual(restoreKeys('      restore-keys: >-\n        a-\n'), ['a-']);
});

function jobOf(workflow, name) {
  const lines = workflow.split('\n');
  const start = lines.findIndex((line) => line === `  ${name}:`);
  assert.ok(start >= 0, `job ${name} is missing`);
  const end = lines.findIndex((line, i) => i > start && /^  \S/.test(line));
  return lines.slice(start, end < 0 ? undefined : end).join('\n');
}

test('the firmware is built from a pinned ESPHome with one toolchain cache per ESPHome pin, on main and for a release', () => {
  for (const [file, job] of [['.github/workflows/ci.yml', 'build-firmware'], ['.github/workflows/release.yml', 'build-v1']]) {
    const workflow = jobOf(readRepo(file), job);
    assert.match(workflow, /actions\/setup-python@v\d+/, file);
    assert.match(workflow, /python-version:\s*"3\.12"/, file);
    assert.match(workflow, /cache:\s*pip/, file);
    assert.match(workflow, /path:\s*~\/\.cache\/esphome\s*$/m, file);
    assert.ok(!/platformio/.test(workflow), `${file}: the ESP-IDF build keeps its toolchain in ~/.cache/esphome only`);
    const keys = [...workflow.matchAll(/^\s*key:\s*(esphome-\$\{\{ runner\.os \}\}-.*)$/gm)];
    assert.equal(keys.length, 1, `${file}: the ESPHome cache is keyed per runner`);
    assert.equal(
      keys[0][1].trim(),
      "esphome-${{ runner.os }}-${{ runner.arch }}-${{ hashFiles('v1/firmware/requirements.txt') }}",
      `${file}: the toolchain depends on the ESPHome pin only, so a YAML edit reuses the same cache`,
    );
    const restores = restoreKeys(workflow);
    assert.ok(restores.length > 0, `${file}: the cache step keeps its restore-keys`);
    for (const restore of restores) {
      assert.ok(restore.includes("hashFiles('v1/firmware/requirements.txt')"), `${file}: a cache from another ESPHome pin is never restored: ${restore}`);
    }
    assert.ok(!/secrets\./.test(workflow), `${file}: the build must not need any repository secrets`);
    const build = workflow.indexOf('bash v1/firmware/build-web.sh\n');
    assert.ok(build > 0, `${file}: runs build-web.sh`);
    const install = workflow.indexOf('pip install -r v1/firmware/requirements.txt');
    assert.ok(install > 0 && install < build, `${file}: ESPHome is installed from the pin before the build`);
  }
  assert.match(jobOf(readRepo('.github/workflows/ci.yml'), 'build-firmware'), /if: github\.event_name == 'push' && github\.ref == 'refs\/heads\/main'/);
});

test('the site takes each line\'s installer from its newest release and writes its manifest with that release\'s version before the upload', () => {
  const site = jobOf(readRepo('.github/workflows/release.yml'), 'site');
  assert.ok(!/pip install|esphome compile|idf\.py|build-web\.sh/.test(site), 'the site never compiles its own firmware');
  const script = readRepo('.github/scripts/site-releases.sh');
  const download = script.indexOf('gh release download "$tag"');
  const copy = script.indexOf('cp "$work/v1/pytaichenko-$version.factory.bin" "$root/site/firmware/v1/pytaichenko.factory.bin"');
  const manifest = script.indexOf('bash "$root/v1/firmware/build-web.sh" --manifest-only --version "$version" --out "$root/site/firmware/v1"');
  const split = script.indexOf('bash "$root/v2/firmware/build-web.sh" --from-factory "$work/v2/pytaichenko-$version.factory.bin" --version "$version" --out "$root/site/firmware/v2"');
  assert.ok(download > 0 && copy > download && manifest > copy && split > download, `${download} ${copy} ${manifest} ${split}`);
  assert.ok(!script.includes('site/firmware/v2/pytaichenko.factory.bin'), 'the version 2 merged image never reaches the site');
  assert.ok(site.indexOf('bash .github/scripts/site-releases.sh') < site.indexOf('actions/upload-pages-artifact'));
  assert.match(site, /ref: main\n/);
});

test('firmware requirements pin ESPHome exactly', () => {
  assert.match(readRepo('v1/firmware/requirements.txt'), /^esphome==2026\.10\.0\s*$/m);
});

const SCRIPTS = ['assets/text.js', 'assets/config.js', 'assets/i18n.js'];
const ESP_WEB_TOOLS = /^https:\/\/unpkg\.com\/esp-web-tools@10\.\d+\.\d+\/dist\/web\/install-button\.js\?module$/;
const CH340_URL = 'https://www.wch-ic.com/downloads/CH341SER_EXE.html';
const CP210X_URL = 'https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers';
const CYRILLIC = /[Ѐ-ӿ]/;
function firmwareText() {
  return loadScripts(['assets/text.js']).TEXT;
}

test('the firmware section holds one ESP Web Tools button per version, each with its own relative manifest, loaded by one pinned module script', () => {
  const page = readPage();
  const section = sectionHtml(page, 'firmware');
  const blocks = [...section.matchAll(/<div class="section-body" data-for-v="(\d)">/g)].map((m) => [m.index, m[1]]);
  const buttons = [...section.matchAll(/<esp-web-install-button\b([^>]*)>/g)];
  assert.deepEqual(
    buttons.map((b) => [blocks.filter(([i]) => i < b.index).pop()[1], tagAttr(b[1], 'manifest')]),
    [['1', 'firmware/v1/manifest.json'], ['2', 'firmware/v2/manifest.json']],
  );
  assert.equal([...page.matchAll(/<esp-web-install-button\b/g)].length, 2);

  const modules = [...page.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/gi)].filter((m) => /\btype\s*=\s*"module"/i.test(m[1]));
  assert.equal(modules.length, 1, 'exactly one module script on the page');
  const src = tagAttr(modules[0][1], 'src');
  assert.match(src, ESP_WEB_TOOLS);
  assert.match(src, /@\d+\.\d+\.\d+\//, 'an exact version');
  assert.equal(modules[0][2].trim(), '', 'the module script has no inline code');
});

test('the install button slots carry translated texts: the activate button, unsupported and not-allowed', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const inner = /<esp-web-install-button\b[^>]*>([\s\S]*?)<\/esp-web-install-button>/.exec(section);
  assert.ok(inner, 'the install button is closed');
  const slotted = [...inner[1].matchAll(/<([a-z0-9-]+)\b([^>]*\bslot="([^"]+)"[^>]*)>/g)];
  assert.deepEqual(slotted.map((m) => m[3]), ['activate', 'unsupported', 'not-allowed']);
  const [activate, unsupported, notAllowed] = slotted;
  assert.equal(activate[1], 'button');
  assert.equal(tagAttr(activate[2], 'type'), 'button');
  assert.equal(tagAttr(activate[2], 'data-i18n'), 'firmware.button');
  assert.equal(tagAttr(unsupported[2], 'data-i18n'), 'firmware.unsupported');
  assert.equal(tagAttr(notAllowed[2], 'data-i18n'), 'firmware.not_allowed');

  const TEXT = firmwareText();
  assert.equal(TEXT.uk['firmware.button'], 'Встановити прошивку');
  assert.equal(TEXT.en['firmware.button'], 'Install the firmware');
  assert.match(TEXT.uk['firmware.unsupported'], /Chrome або Edge на комп'ютері/);
  assert.match(TEXT.en['firmware.unsupported'], /Chrome or Edge on a computer/);
  for (const lang of ['uk', 'en']) {
    assert.match(TEXT[lang]['firmware.not_allowed'], /https:\/\//, lang);
    assert.match(TEXT[lang]['firmware.not_allowed'], /localhost/, lang);
  }
});

test('switching to English leaves no Cyrillic in the firmware section, including the button slots, and Ukrainian comes back', () => {
  const fragment = parseFragment(sectionHtml(readPage(), 'firmware'));
  const env = makeEnv({ children: [fragment], languages: ['uk-UA'] });
  const P = loadScripts(SCRIPTS, env);
  const ukText = visibleText(fragment);
  assert.match(ukText, CYRILLIC);
  const translated = fragment.querySelectorAll('[data-i18n]');
  assert.ok(translated.length >= 20, `expected many translated texts, got ${translated.length}`);
  const slotTexts = translated.filter((el) => el.hasAttribute('slot'));
  assert.equal(slotTexts.length, 6, 'three slots in each version button');

  P.setLang('en');
  for (const el of translated) assert.equal(el.textContent, P.TEXT.en[el.getAttribute('data-i18n')]);
  const enText = visibleText(fragment);
  const cyr = enText.match(new RegExp(`.{0,30}${CYRILLIC.source}.{0,30}`));
  assert.equal(cyr, null, `Cyrillic left in English: ${cyr && cyr[0]}`);

  P.setLang('uk');
  assert.equal(visibleText(fragment), ukText);
});

test('the firmware section is no longer a coming-soon stub', () => {
  const page = readPage();
  const TEXT = firmwareText();
  const firmware = sectionHtml(page, 'firmware');
  assert.doesNotMatch(firmware, /soon-status|Готується/);
  assert.ok(!keysIn(firmware).includes('firmware.status'));
  for (const lang of ['uk', 'en']) {
    assert.ok(!('firmware.status' in TEXT[lang]), `${lang}.firmware.status is gone`);
    assert.ok(!('firmware.text' in TEXT[lang]), `${lang}.firmware.text is gone`);
  }
});

test('the firmware section opens with one sentence on what the button does, then the button', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const keys = keysIn(section);
  assert.deepEqual(keys.slice(0, 3), ['firmware.title', 'firmware.intro', 'firmware.button']);
  const TEXT = firmwareText();
  for (const lang of ['uk', 'en']) {
    const intro = TEXT[lang]['firmware.intro'];
    assert.equal(intro.split(/[.!?](?:\s|$)/).filter((s) => s.trim()).length, 1, `${lang} intro is one sentence`);
    assert.match(intro, lang === 'uk' ? /браузер/ : /browser/);
    assert.match(intro, /Wi-Fi/);
  }
});

test('what you need: Chrome or Edge on a computer, a USB-C cable that carries data, and on Windows a driver from the chip makers', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const TEXT = firmwareText();
  const keys = keysIn(section);
  for (const key of ['firmware.need.title', 'firmware.need.browser', 'firmware.need.cable', 'firmware.need.driver']) {
    assert.ok(keys.includes(key), `page misses ${key}`);
  }
  assert.match(TEXT.uk['firmware.need.browser'], /Chrome або Edge/);
  assert.match(TEXT.uk['firmware.need.browser'], /комп'ютер/i);
  assert.match(TEXT.en['firmware.need.browser'], /Chrome or Edge/);
  assert.match(TEXT.en['firmware.need.browser'], /computer/i);
  assert.match(TEXT.uk['firmware.need.cable'], /USB-C[\s\S]*дан/);
  assert.match(TEXT.en['firmware.need.cable'], /USB-C[\s\S]*data/);
  for (const lang of ['uk', 'en']) {
    assert.match(TEXT[lang]['firmware.need.driver'], /Windows/);
    assert.match(TEXT[lang]['firmware.need.driver'], /CH340/);
    assert.match(TEXT[lang]['firmware.need.driver'], /CP210x/);
  }
  const v1 = section.slice(0, section.indexOf('<div class="section-body" data-for-v="2">'));
  const links = [...v1.matchAll(/<a\b([^>]*)>/g)].map((m) => m[1]).filter((tag) => /^https?:/.test(tagAttr(tag, 'href') || ''));
  assert.deepEqual(links.map((tag) => tagAttr(tag, 'href')), [CH340_URL, CP210X_URL]);
  for (const tag of links) {
    assert.equal(tagAttr(tag, 'target'), '_blank');
    assert.match(tagAttr(tag, 'rel'), /\bnoopener\b/);
    assert.ok(tagAttr(tag, 'data-i18n'), 'the driver link text is translated');
  }
  assert.match(TEXT.uk['firmware.need.ch340'], /CH340/);
  assert.match(TEXT.en['firmware.need.ch340'], /CH340/);
  assert.match(TEXT.uk['firmware.need.cp210x'], /CP210x/);
  assert.match(TEXT.en['firmware.need.cp210x'], /CP210x/);
});

test('the steps go in order and name the buttons of the English installer window as it shows them', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const list = /<ol class="steps">([\s\S]*?)<\/ol>/.exec(section);
  assert.ok(list, 'the steps are an ordered list');
  const steps = [...list[1].matchAll(/<li data-i18n="([^"]+)">/g)].map((m) => m[1]);
  assert.deepEqual(steps, ['firmware.step1', 'firmware.step2', 'firmware.step3', 'firmware.step4', 'firmware.step5']);
  const TEXT = firmwareText();
  const labels = {
    'firmware.step3': [`Install ${PROJECT_NAME}`, 'Erase device', 'Next', 'Install'],
    'firmware.step4': ['Installation complete!', 'Next'],
    'firmware.step5': ['Configure Wi-Fi', 'Network', 'Password', 'Connect', 'Join other…', 'Device connected to the network!', 'Continue', 'Connect to Wi-Fi'],
  };
  for (const [key, names] of Object.entries(labels)) {
    for (const name of names) {
      assert.ok(TEXT.uk[key].includes(`«${name}»`), `uk ${key} names «${name}»`);
      assert.ok(TEXT.en[key].includes(`“${name}”`), `en ${key} names “${name}”`);
    }
  }
  assert.match(TEXT.uk['firmware.step1'], /USB-C/);
  assert.match(TEXT.uk['firmware.step1'], /ззаду/);
  assert.match(TEXT.en['firmware.step1'], /USB-C/);
  assert.match(TEXT.en['firmware.step1'], /back/);
  assert.ok(TEXT.uk['firmware.step2'].includes(`«${TEXT.uk['firmware.button']}»`));
  assert.ok(TEXT.en['firmware.step2'].includes(`“${TEXT.en['firmware.button']}”`));
  assert.match(TEXT.uk['firmware.step2'], /порт/);
  assert.match(TEXT.en['firmware.step2'], /port/);
  assert.ok(TEXT.uk['firmware.step5'].includes(`натисніть «${TEXT.uk['firmware.button']}» ще раз`));
  assert.ok(TEXT.en['firmware.step5'].includes(`press “${TEXT.en['firmware.button']}” again`));
});

test('what happens next: the ring breathes once every 4 s until the speaker reaches the server, which should come right after flashing', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const keys = keysIn(section);
  for (const key of ['firmware.next.title', 'firmware.next.ring', 'firmware.next.server']) assert.ok(keys.includes(key), key);
  const TEXT = firmwareText();
  assert.match(TEXT.uk['firmware.next.ring'], /кільце[\s\S]*раз на 4 с[\s\S]*сервер/i);
  assert.match(TEXT.en['firmware.next.ring'], /ring[\s\S]*once every 4 s[\s\S]*server/i);
  assert.match(TEXT.uk['firmware.next.server'], /одразу після прошивки[\s\S]*розділі «Сервер»/);
  assert.match(TEXT.en['firmware.next.server'], /right after flashing[\s\S]*“Server” section/);
});

test('the speaker accepts its first server connection for a day after power-on, a replug reopens it, and an adopted speaker restarts by itself', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const keys = keysIn(section);
  assert.equal(keys.indexOf('firmware.next.window'), keys.indexOf('firmware.next.server') + 1);
  const yaml = readRepo('v1/firmware/pytaichenko.yaml');
  assert.match(yaml, /^provisioning:\n  timeout: 24h$/m);
  assert.match(yaml, /^  on_timeout:\n    - if:\n        condition:\n          lambda: return api::global_api_server->get_noise_ctx\(\)\.has_psk\(\);\n        then:\n          - lambda: App\.safe_reboot\(\);$/m);
  const TEXT = firmwareText();
  assert.match(TEXT.uk['firmware.next.window'], /протягом доби після ввімкнення[\s\S]*від'єднайте[\s\S]*під'єднайте знову[\s\S]*Уже з'єднана колонка[\s\S]*перезапускається сама/);
  assert.match(TEXT.en['firmware.next.window'], /within a day of being switched on[\s\S]*unplug it and plug it in again[\s\S]*already connected[\s\S]*restarts by itself/);
});

test('updating: the same button, “Update” in the window, never “Erase device”, and the speaker keeps its key, Wi-Fi and phrase', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const keys = keysIn(section);
  const order = ['firmware.update.title', 'firmware.update.text', 'firmware.update.erase', 'firmware.trouble.title'];
  const at = order.map((key) => keys.indexOf(key));
  assert.ok(at.every((i) => i >= 0), `page has ${order.join(', ')}`);
  assert.deepEqual([...at].sort((a, b) => a - b), at, 'the update note comes before the troubleshooting list');
  assert.ok(keys.indexOf('firmware.next.window') < at[0]);

  const TEXT = firmwareText();
  const quote = { uk: (s) => `«${s}»`, en: (s) => `“${s}”` };
  for (const lang of ['uk', 'en']) {
    const q = quote[lang];
    const text = TEXT[lang]['firmware.update.text'];
    for (const name of [TEXT[lang]['firmware.button'], `Update ${PROJECT_NAME}`, 'Install', 'Installation complete!', 'Next']) {
      assert.ok(text.includes(q(name)), `${lang} update text names ${name}`);
    }
    const erase = TEXT[lang]['firmware.update.erase'];
    for (const name of [`Install ${PROJECT_NAME}`, 'Erase device', 'Erase User Data']) {
      assert.ok(erase.includes(q(name)), `${lang} erase note names ${name}`);
    }
  }
  assert.match(TEXT.uk['firmware.update.text'], /ключ сервера, мережу Wi-Fi, вибрану фразу/);
  assert.match(TEXT.en['firmware.update.text'], /server key, Wi-Fi network, chosen phrase/);
  assert.match(TEXT.uk['firmware.update.erase'], /галочку не ставте/);
  assert.match(TEXT.en['firmware.update.erase'], /leave that box unticked/);
  assert.match(TEXT.uk['firmware.update.erase'], /«Erase User Data» тоді не натискайте, якщо не хочете з'єднувати колонку із сервером заново/);
  assert.match(TEXT.en['firmware.update.erase'], /Do not press the red “Erase User Data” item then unless you want to connect the speaker to the server all over again/);
});

test('if the ESP Web Tools module never loads, a note in the install area says so after a few seconds', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const area = /<div class="fw-install">([\s\S]*?)<\/div>/.exec(section);
  assert.ok(area, 'the install area is there');
  const after = /<\/esp-web-install-button>\s*<p class="fw-note fw-missing" data-i18n="firmware\.load_failed">/.exec(area[1]);
  assert.ok(after, 'the note sits right after the install button, so the sibling selector reaches it');

  const TEXT = firmwareText();
  assert.match(TEXT.uk['firmware.load_failed'], /unpkg\.com[\s\S]*інтернет[\s\S]*оновіть сторінку/);
  assert.match(TEXT.en['firmware.load_failed'], /unpkg\.com[\s\S]*internet[\s\S]*reload the page/);

  const css = readSite('assets/style.css');
  assert.match(css, /\.fw-missing\s*\{[^}]*display:\s*none/);
  const shown = /esp-web-install-button:not\(:defined\)\s*\+\s*\.fw-missing\s*\{([^}]*)\}/.exec(css);
  assert.ok(shown, 'the note shows only while the button is still undefined');
  assert.match(shown[1], /display:\s*block/);
  assert.match(shown[1], /visibility:\s*hidden/);
  const delay = /animation:\s*([\w-]+)\s+0s\s+(\d+)s\s+forwards/.exec(shown[1]);
  assert.ok(delay, 'revealed by a zero-length animation after a delay');
  assert.ok(Number(delay[2]) >= 5, 'a normal load defines the button before the note appears');
  assert.match(css, new RegExp(`@keyframes\\s+${delay[1]}\\s*\\{\\s*to\\s*\\{\\s*visibility:\\s*visible;?\\s*\\}\\s*\\}`));
});

test('if it did not work: a cable or a driver, the BOOT button behind the three tray screws, and Change Wi-Fi with the same button', () => {
  const section = sectionHtml(readPage(), 'firmware');
  const list = /<dl class="trouble">([\s\S]*?)<\/dl>/.exec(section);
  assert.ok(list, 'the troubleshooting list looks like the one in “If something goes wrong”');
  assert.deepEqual(keysIn(list[1]), [
    'firmware.trouble.port.symptom',
    'firmware.trouble.port.fix',
    'firmware.trouble.boot.symptom',
    'firmware.trouble.boot.fix',
    'firmware.trouble.wifi.symptom',
    'firmware.trouble.wifi.fix',
  ]);
  const TEXT = firmwareText();
  assert.match(TEXT.uk['firmware.trouble.port.fix'], /кабель[\s\S]*драйвер/);
  assert.match(TEXT.en['firmware.trouble.port.fix'], /cable[\s\S]*driver/);
  const port = /firmware\.trouble\.port\.fix[\s\S]*?<\/dd>/.exec(list[1])[0];
  assert.match(port, /<pre class="cmd" tabindex="0"><code>sudo usermod -aG dialout \$USER<\/code><\/pre>/);
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (key.startsWith('firmware.')) assert.doesNotMatch(value, /sudo|usermod/, `${lang}.${key}`);
    }
  }
  assert.match(TEXT.uk['firmware.trouble.port.fix'], /dialout командою нижче/);
  assert.match(TEXT.en['firmware.trouble.port.fix'], /dialout group with the command below/);
  assert.match(TEXT.uk['firmware.trouble.boot.fix'], /три гвинти лотка[\s\S]*BOOT/);
  assert.match(TEXT.en['firmware.trouble.boot.fix'], /three screws of the tray[\s\S]*BOOT/);
  assert.ok(TEXT.uk['firmware.trouble.wifi.fix'].includes('«Change Wi-Fi»'));
  assert.ok(TEXT.en['firmware.trouble.wifi.fix'].includes('“Change Wi-Fi”'));
  assert.ok(TEXT.uk['firmware.trouble.wifi.fix'].includes(`«${TEXT.uk['firmware.button']}»`));
  assert.ok(TEXT.en['firmware.trouble.wifi.fix'].includes(`“${TEXT.en['firmware.button']}”`));
});

test('the firmware texts stay calm: no exclamation marks outside the quoted installer labels, nothing urgent', () => {
  const TEXT = firmwareText();
  for (const lang of ['uk', 'en']) {
    for (const [key, value] of Object.entries(TEXT[lang])) {
      if (!key.startsWith('firmware.')) continue;
      const own = value.replace(/«[^»]*»|“[^”]*”/g, '');
      assert.doesNotMatch(own, /!/, `${lang}.${key}`);
      assert.doesNotMatch(value, /терміново|негайно|обов'язково|urgent|immediately|must\b/i, `${lang}.${key}`);
    }
  }
});

test('the install button stays hidden until ESP Web Tools defines it, and the steps are numbered', () => {
  const css = readSite('assets/style.css');
  assert.match(css, /esp-web-install-button:not\(:defined\)\s*\{[^}]*display:\s*none/);
  assert.match(css, /\.steps\s*\{[^}]*counter-reset:\s*step/);
  assert.match(css, /\.steps li::before\s*\{[^}]*content:\s*counter\(step\)/);
});

function ancestorsHidden(root, target) {
  const walk = (el, hidden) => {
    if (el === target) return hidden;
    for (const child of el.children) {
      const found = walk(child, hidden || child.hasAttribute('hidden'));
      if (found !== undefined) return found;
    }
    return undefined;
  };
  return walk(root, root.hasAttribute('hidden'));
}

test('only the install button of the shown version is reachable: the other one sits under a hidden version block', () => {
  const fragment = parseFragment(sectionHtml(readPage(), 'firmware'));
  const env = makeEnv({ children: [fragment], languages: ['uk-UA'], href: 'https://example.github.io/pytaichenko/?v=2' });
  const P = loadScripts(['assets/text.js', 'assets/releases.js', 'assets/config.js', 'assets/version.js', 'assets/i18n.js'], env);
  const buttons = [];
  const find = (el) => {
    if (el.tagName === 'esp-web-install-button') buttons.push(el);
    el.children.forEach(find);
  };
  find(fragment);
  assert.deepEqual(buttons.map((b) => b.getAttribute('manifest')), ['firmware/v1/manifest.json', 'firmware/v2/manifest.json']);
  assert.deepEqual(buttons.map((b) => ancestorsHidden(fragment, b)), [true, false]);
  P.setVersion('1');
  assert.deepEqual(buttons.map((b) => ancestorsHidden(fragment, b)), [false, true]);
  P.setVersion('2');
  assert.deepEqual(buttons.map((b) => ancestorsHidden(fragment, b)), [true, false]);
  assert.match(readSite('assets/style.css'), /\[data-for-v\]\[hidden\]\s*\{[^}]*display:\s*none\s*!important/);
});
