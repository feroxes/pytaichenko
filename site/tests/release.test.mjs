import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { chmodSync, cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { loadScripts, siteDir } from './helpers.mjs';

const repoDir = path.resolve(siteDir, '..');
const CHECK = '.github/scripts/check-version.sh';
const NOTES = '.github/scripts/release-notes.sh';
const SITE = '.github/scripts/site-releases.sh';
const LATEST = '.github/scripts/latest-flag.sh';
const TREE = [CHECK, NOTES, 'v1/firmware/build-web.sh', 'v1/firmware/pytaichenko.yaml', 'v1/hub/pyproject.toml', 'v2/firmware/VERSION', 'CHANGELOG.md'];
const SITE_TREE = [SITE, 'v1/firmware/build-web.sh', 'v1/firmware/pytaichenko.yaml', 'v2/firmware/build-web.sh', 'v2/firmware/tools/web_image.py', 'v2/firmware/partitions.csv', 'v2/firmware/sdkconfig.defaults', 'v2/firmware/VERSION', 'site/assets/releases.js'];
const RELEASE_LINE = /^    '(\d+)': \{ version: '(\d+\.\d+\.\d+)', tag: 'v(\d+\.\d+\.\d+)', stlDir: '([\w.-]+(?:\/[\w.-]+)*\/)' \},?$/;

function readRepo(relPath) {
  return readFileSync(path.join(repoDir, relPath), 'utf8');
}

function withTree(fn, files = TREE) {
  const dir = mkdtempSync(path.join(tmpdir(), 'pytaichenko-release-'));
  try {
    for (const file of files) {
      mkdirSync(path.dirname(path.join(dir, file)), { recursive: true });
      cpSync(path.join(repoDir, file), path.join(dir, file));
    }
    return fn(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function run(root, script, arg, extraEnv = {}) {
  const env = { PATH: process.env.PATH, ...extraEnv };
  return spawnSync('bash', [path.join(root, script), ...(arg === undefined ? [] : [arg])], { env, encoding: 'utf8' });
}

const FAKE_GH = `#!/bin/sh
printf '%s\\n' "$*" >> "$FAKE_GH_LOG"
if [ "$1 $2" = "release list" ]; then
  cat "$FAKE_GH_LIST"
  exit 0
fi
if [ "$1 $2" = "release download" ]; then
  tag="$3"
  shift 3
  dir=.
  patterns=""
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --dir) dir="$2"; shift 2 ;;
      --pattern) patterns="$patterns $2"; shift 2 ;;
      *) echo "fake gh: unexpected $1" >&2; exit 9 ;;
    esac
  done
  mkdir -p "$dir"
  for pattern in $patterns; do
    if [ ! -f "$FAKE_GH_ASSETS/$tag/$pattern" ]; then
      echo "no assets match the file pattern" >&2
      exit 1
    fi
    cp "$FAKE_GH_ASSETS/$tag/$pattern" "$dir/" || exit 1
  done
  exit 0
fi
echo "fake gh: unexpected $*" >&2
exit 9
`;

function fakeGh(dir, releases, assets = {}) {
  const bin = path.join(dir, 'fake-bin');
  mkdirSync(bin, { recursive: true });
  writeFileSync(path.join(bin, 'gh'), FAKE_GH);
  chmodSync(path.join(bin, 'gh'), 0o755);
  writeFileSync(path.join(dir, 'releases.json'), JSON.stringify(releases));
  for (const [tag, files] of Object.entries(assets)) {
    mkdirSync(path.join(dir, 'assets', tag), { recursive: true });
    for (const [name, data] of Object.entries(files)) writeFileSync(path.join(dir, 'assets', tag, name), data);
  }
  return {
    PATH: `${bin}${path.delimiter}${process.env.PATH}`,
    FAKE_GH_LOG: path.join(dir, 'gh.log'),
    FAKE_GH_LIST: path.join(dir, 'releases.json'),
    FAKE_GH_ASSETS: path.join(dir, 'assets'),
  };
}

function ghLog(env) {
  return existsSync(env.FAKE_GH_LOG) ? readFileSync(env.FAKE_GH_LOG, 'utf8').trim().split('\n') : [];
}

function released(tagName, extra = {}) {
  return { tagName, isDraft: false, isPrerelease: false, ...extra };
}

function espImage(fill) {
  const image = Buffer.alloc(32);
  image.set([0xe9, 0, 2, 0x2f]);
  image[8] = 0xee;
  image[31] = fill;
  return image;
}

function tableEntry(type, subtype, offset, size, label) {
  const entry = Buffer.alloc(32);
  entry.set([0xaa, 0x50, type, subtype]);
  entry.writeUInt32LE(offset, 4);
  entry.writeUInt32LE(size, 8);
  entry.write(label, 12, 'ascii');
  return entry;
}

const V2_BOOT = espImage(0x11);
const V2_APP = espImage(0x22);
const V2_TABLE = Buffer.concat([
  tableEntry(1, 0x02, 0x9000, 0x10000, 'nvs'),
  tableEntry(1, 0x00, 0x19000, 0x2000, 'otadata'),
  tableEntry(1, 0x01, 0x1b000, 0x1000, 'phy_init'),
  tableEntry(0, 0x10, 0x20000, 0x400000, 'ota_0'),
  tableEntry(0, 0x11, 0x420000, 0x400000, 'ota_1'),
  Buffer.alloc(0xc00 - 5 * 32, 0xff),
]);

function v2Factory() {
  const head = Buffer.alloc(0x20000, 0xff);
  V2_BOOT.copy(head, 0);
  V2_TABLE.copy(head, 0x8000);
  return Buffer.concat([head, V2_APP]);
}

function git(root, ...args) {
  const done = spawnSync('git', ['-c', 'user.name=test', '-c', 'user.email=test@example.com', '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false', ...args], { cwd: root, encoding: 'utf8' });
  assert.equal(done.status, 0, done.stderr);
  return done.stdout;
}

function commitFiles(root, files, tags) {
  for (const [file, text] of Object.entries(files)) {
    mkdirSync(path.dirname(path.join(root, file)), { recursive: true });
    writeFileSync(path.join(root, file), text);
  }
  git(root, 'add', '-A', '--', ...Object.keys(files));
  git(root, 'commit', '-q', '-m', tags.join(' '));
  for (const tag of tags) git(root, 'tag', tag);
}

function withSiteRepo(fn) {
  return withTree((root) => {
    git(root, 'init', '-q');
    commitFiles(root, { 'case/stl/PETG-black_body.stl': 'solid v1.0.0' }, ['v1.0.0']);
    git(root, 'rm', '-q', '-r', 'case');
    commitFiles(root, { 'v1/case/stl/PETG-black_body.stl': 'solid v1.0.1', 'v2/case/stl/PETG-black_body.stl': 'solid v2.0.0' }, ['v1.0.1', 'v2.0.0']);
    return fn(root);
  }, SITE_TREE);
}

const V1_ASSETS = (version) => ({ [`pytaichenko-${version}.factory.bin`]: `v1 image ${version}`, [`pytaichenko-${version}-stl.zip`]: `v1 stl ${version}` });
const V2_ASSETS = (version) => ({ [`pytaichenko-${version}.factory.bin`]: v2Factory(), [`pytaichenko-${version}.ota.bin`]: V2_APP, [`pytaichenko-${version}-stl.zip`]: `v2 stl ${version}` });

function releasesJs(rows) {
  return ['window.PYTAICHENKO = window.PYTAICHENKO || {};', 'window.PYTAICHENKO.RELEASES = {', ...rows.map((row, i) => `    ${row}${i < rows.length - 1 ? ',' : ''}`), '};', ''].join('\n');
}

function filesUnder(dir) {
  if (!existsSync(dir)) return [];
  return readdirSync(dir, { recursive: true }).map(String).sort();
}

function readJson(file) {
  return JSON.parse(readFileSync(file, 'utf8'));
}

function edit(root, file, from, to) {
  const full = path.join(root, file);
  const text = readFileSync(full, 'utf8');
  assert.ok(text.includes(from), `${file} has no ${from}`);
  writeFileSync(full, text.replace(from, to));
}

function jobOf(workflow, name) {
  const lines = workflow.split('\n');
  const start = lines.findIndex((line) => line === `  ${name}:`);
  assert.ok(start >= 0, `job ${name} is missing`);
  const end = lines.findIndex((line, i) => i > start && /^  \S/.test(line));
  return lines.slice(start, end < 0 ? undefined : end).join('\n');
}

function runBlocks(workflow) {
  const lines = workflow.split('\n');
  const blocks = [];
  lines.forEach((line, i) => {
    const m = /^(\s*)(?:- )?run:\s*(.*)$/.exec(line);
    if (!m) return;
    const body = [m[2]];
    const indent = m[1].length + (line.trimStart().startsWith('- ') ? 2 : 0);
    for (const next of lines.slice(i + 1)) {
      if (next.trim() !== '' && next.search(/\S/) <= indent) break;
      body.push(next);
    }
    blocks.push(body.join('\n'));
  });
  return blocks;
}

const VERSION = loadScripts(['assets/releases.js']).RELEASES['1'].version;

test('the firmware, the server and the site carry the same version, and CHANGELOG.md has its notes', () => {
  const firmware = /^  project:\n    name: Pytaichenko\.Speaker\n    version: "([^"]+)"$/m.exec(readRepo('v1/firmware/pytaichenko.yaml'));
  const hub = /^version = "([^"]+)"$/m.exec(readRepo('v1/hub/pyproject.toml'));
  assert.ok(firmware && hub);
  assert.equal(firmware[1], VERSION);
  assert.equal(hub[1], VERSION);
  const checked = run(repoDir, CHECK, `v${VERSION}`);
  assert.equal(checked.status, 0, checked.stderr);
  const notes = run(repoDir, NOTES, VERSION);
  assert.equal(notes.status, 0, notes.stderr);
  assert.match(readRepo('CHANGELOG.md'), new RegExp(`^## \\[${VERSION.replace(/\./g, '\\.')}\\] - \\d{4}-\\d{2}-\\d{2}$`, 'm'));
});

test('a version 1 tag must equal the firmware and the server versions, and the check names every place that differs', () => {
  withTree((root) => {
    const ok = run(root, CHECK, `v${VERSION}`);
    assert.equal(ok.status, 0, ok.stderr);
    assert.equal(ok.stderr, '');

    const next = run(root, CHECK, 'v1.8.7');
    assert.equal(next.status, 1);
    for (const place of ['v1/firmware/pytaichenko.yaml', 'v1/hub/pyproject.toml']) {
      assert.match(next.stderr, new RegExp(`${place.replace(/\./g, '\\.')} \\([^)]*\\) is ${VERSION.replace(/\./g, '\\.')}, but the tag v1\\.8\\.7 needs 1\\.8\\.7`));
    }
    assert.doesNotMatch(next.stderr, /site\/|v2\/firmware/);

    edit(root, 'v1/hub/pyproject.toml', `version = "${VERSION}"`, 'version = "1.8.7"');
    const one = run(root, CHECK, 'v1.8.7');
    assert.equal(one.status, 1);
    assert.match(one.stderr, /firmware\/pytaichenko\.yaml/);
    assert.doesNotMatch(one.stderr, /hub\/pyproject\.toml/);

    edit(root, 'v1/firmware/pytaichenko.yaml', `version: "${VERSION}"`, "version: '1.8.7' # bumped");
    const all = run(root, CHECK, 'v1.8.7');
    assert.equal(all.status, 0, all.stderr);
  });
});

test('a version 2 tag must equal the first line of v2/firmware/VERSION exactly, so a -dev build is never released', () => {
  withTree((root) => {
    writeFileSync(path.join(root, 'v2/firmware/VERSION'), '2.0.0-dev\n');
    const dev = run(root, CHECK, 'v2.0.0');
    assert.equal(dev.status, 1);
    assert.match(dev.stderr, /v2\/firmware\/VERSION \([^)]*\) is 2\.0\.0-dev, but the tag v2\.0\.0 needs 2\.0\.0/);
    assert.doesNotMatch(dev.stderr, /v1\/|site\//);
    writeFileSync(path.join(root, 'v2/firmware/VERSION'), '2.0.0\n');
    const ok = run(root, CHECK, 'v2.0.0');
    assert.equal(ok.status, 0, ok.stderr);
    assert.equal(ok.stderr, '');
    assert.equal(run(root, CHECK, 'v2.0.1').status, 1);
    writeFileSync(path.join(root, 'v2/firmware/VERSION'), '2.0.1\r\n');
    assert.equal(run(root, CHECK, 'v2.0.1').status, 0);
    edit(root, 'v1/hub/pyproject.toml', `version = "${VERSION}"`, 'version = "0.0.1"');
    assert.equal(run(root, CHECK, 'v2.0.1').status, 0, 'a version 2 release does not look at version 1');
    rmSync(path.join(root, 'v2/firmware/VERSION'));
    const missing = run(root, CHECK, 'v2.0.1');
    assert.equal(missing.status, 1);
    assert.match(missing.stderr, /v2\/firmware\/VERSION \([^)]*\) is not set/);
  });
});

test('the version check knows only lines 1 and 2 and reads no site file', () => {
  withTree((root) => {
    for (const tag of ['v3.0.0', 'v0.1.0', 'v10.0.0']) {
      const done = run(root, CHECK, tag);
      assert.equal(done.status, 1, tag);
      assert.match(done.stderr, /only lines 1 and 2/, tag);
    }
    assert.ok(!existsSync(path.join(root, 'site')));
    assert.equal(run(root, CHECK, `v${VERSION}`).status, 0);
  });
  assert.doesNotMatch(readRepo(CHECK), /site\/|config\.js|releases\.js/);
});

test('site-releases.sh with only v1.0.0 published writes line 1 alone, its files and its manifest, and nothing for line 2', () => {
  withSiteRepo((root) => {
    const env = fakeGh(root, [released('v1.0.0')], { 'v1.0.0': V1_ASSETS('1.0.0') });
    const done = run(root, SITE, undefined, env);
    assert.equal(done.status, 0, done.stderr);
    const js = readFileSync(path.join(root, 'site/assets/releases.js'), 'utf8');
    assert.equal(js, releasesJs(["'1': { version: '1.0.0', tag: 'v1.0.0', stlDir: 'case/stl/' }"]));
    assert.equal(readFileSync(path.join(root, 'site/firmware/v1/pytaichenko.factory.bin'), 'utf8'), 'v1 image 1.0.0');
    assert.equal(readFileSync(path.join(root, 'site/files/v1/pytaichenko-stl.zip'), 'utf8'), 'v1 stl 1.0.0');
    const manifest = readJson(path.join(root, 'site/firmware/v1/manifest.json'));
    assert.equal(manifest.version, '1.0.0');
    assert.equal(manifest.name, 'Pytaichenko.Speaker');
    assert.deepEqual(filesUnder(path.join(root, 'site/firmware')), ['v1', 'v1/manifest.json', 'v1/pytaichenko.factory.bin']);
    assert.deepEqual(filesUnder(path.join(root, 'site/files')), ['v1', 'v1/pytaichenko-stl.zip']);
    const log = ghLog(env);
    assert.equal(log[0], 'release list --limit 1000 --json tagName,isDraft,isPrerelease');
    assert.deepEqual(log.slice(1), [`release download v1.0.0 --dir ${log[1].split(' ')[4]} --pattern pytaichenko-1.0.0.factory.bin --pattern pytaichenko-1.0.0-stl.zip`]);
  });
});

test('site-releases.sh with v1.0.0 and v2.0.0 writes exactly the committed releases.js and both lines, version 2 as four parts', () => {
  withSiteRepo((root) => {
    const env = fakeGh(root, [released('v2.0.0'), released('v1.0.0')], { 'v1.0.0': V1_ASSETS('1.0.0'), 'v2.0.0': V2_ASSETS('2.0.0') });
    const done = run(root, SITE, undefined, env);
    assert.equal(done.status, 0, done.stderr);
    const js = readFileSync(path.join(root, 'site/assets/releases.js'), 'utf8');
    assert.equal(js, releasesJs(["'1': { version: '1.0.0', tag: 'v1.0.0', stlDir: 'case/stl/' }", "'2': { version: '2.0.0', tag: 'v2.0.0', stlDir: 'v2/case/stl/' }"]));
    assert.equal(js, readRepo('site/assets/releases.js'));
    assert.deepEqual(filesUnder(path.join(root, 'site/firmware/v2')), ['bootloader.bin', 'manifest.json', 'ota_data_initial.bin', 'partition-table.bin', 'pytaichenko.bin']);
    assert.deepEqual(readFileSync(path.join(root, 'site/firmware/v2/bootloader.bin')), V2_BOOT);
    assert.deepEqual(readFileSync(path.join(root, 'site/firmware/v2/partition-table.bin')), V2_TABLE);
    assert.deepEqual(readFileSync(path.join(root, 'site/firmware/v2/ota_data_initial.bin')), Buffer.alloc(0x2000, 0xff));
    assert.deepEqual(readFileSync(path.join(root, 'site/firmware/v2/pytaichenko.bin')), V2_APP);
    const manifest = readJson(path.join(root, 'site/firmware/v2/manifest.json'));
    assert.equal(manifest.version, '2.0.0');
    assert.equal(manifest.builds[0].chipFamily, 'ESP32-S3');
    assert.deepEqual(manifest.builds[0].parts, [
      { path: 'bootloader.bin', offset: 0 },
      { path: 'partition-table.bin', offset: 0x8000 },
      { path: 'ota_data_initial.bin', offset: 0x19000 },
      { path: 'pytaichenko.bin', offset: 0x20000 },
    ]);
    assert.equal(readFileSync(path.join(root, 'site/files/v2/pytaichenko-stl.zip'), 'utf8'), 'v2 stl 2.0.0');
    assert.equal(readJson(path.join(root, 'site/firmware/v1/manifest.json')).version, '1.0.0');
    assert.ok(ghLog(env).some((line) => line.startsWith('release download v2.0.0 ') && line.endsWith('--pattern pytaichenko-2.0.0.factory.bin --pattern pytaichenko-2.0.0-stl.zip')));
  });
});

test('site-releases.sh takes the newest published release of each line by number, skips drafts and pre-releases, and finds the STL folder inside the tag', () => {
  withSiteRepo((root) => {
    const releases = [
      released('v2.1.0', { isDraft: true }),
      released('v1.0.1'),
      released('v1.1.0', { isPrerelease: true }),
      released('v2.0.0'),
      released('v1.0.0'),
      released('v3.0.0'),
      released('v2.0.0-rc1'),
      released('nightly'),
    ];
    const env = fakeGh(root, releases, { 'v1.0.1': V1_ASSETS('1.0.1'), 'v2.0.0': V2_ASSETS('2.0.0') });
    const done = run(root, SITE, undefined, env);
    assert.equal(done.status, 0, done.stderr);
    const js = readFileSync(path.join(root, 'site/assets/releases.js'), 'utf8');
    assert.equal(js, releasesJs(["'1': { version: '1.0.1', tag: 'v1.0.1', stlDir: 'v1/case/stl/' }", "'2': { version: '2.0.0', tag: 'v2.0.0', stlDir: 'v2/case/stl/' }"]));
    for (const line of js.trimEnd().split('\n').slice(2, -1)) assert.match(line, RELEASE_LINE);
    assert.equal(readJson(path.join(root, 'site/firmware/v1/manifest.json')).version, '1.0.1');
    assert.equal(readFileSync(path.join(root, 'site/firmware/v1/pytaichenko.factory.bin'), 'utf8'), 'v1 image 1.0.1');
    assert.equal(readFileSync(path.join(root, 'site/files/v1/pytaichenko-stl.zip'), 'utf8'), 'v1 stl 1.0.1');
  });
  withSiteRepo((root) => {
    git(root, 'tag', 'v1.0.9', 'v1.0.0');
    git(root, 'tag', 'v1.0.10', 'v1.0.1');
    const env = fakeGh(root, [released('v1.0.9'), released('v1.0.10'), released('v1.0.1')], { 'v1.0.10': V1_ASSETS('1.0.10') });
    const done = run(root, SITE, undefined, env);
    assert.equal(done.status, 0, done.stderr);
    assert.equal(readFileSync(path.join(root, 'site/assets/releases.js'), 'utf8'), releasesJs(["'1': { version: '1.0.10', tag: 'v1.0.10', stlDir: 'v1/case/stl/' }"]));
  });
});

test('site-releases.sh fails and leaves releases.js alone with no release, a tag missing from the checkout, a missing asset, or a broken version 2 image', () => {
  const cases = [
    [[], {}, /no published release/],
    [[released('v1.0.0', { isDraft: true })], {}, /no published release/],
    [[released('v1.0.2')], { 'v1.0.2': V1_ASSETS('1.0.2') }, /v1\.0\.2 is not in this checkout/],
    [[released('v1.0.0')], { 'v1.0.0': { 'pytaichenko-1.0.0.factory.bin': 'x' } }, /^no assets match the file pattern$/m],
    [[released('v2.0.0')], { 'v2.0.0': { ...V2_ASSETS('2.0.0'), 'pytaichenko-2.0.0.factory.bin': 'not an image' } }, /bootloader/],
  ];
  for (const [releases, assets, words] of cases) {
    withSiteRepo((root) => {
      const before = readFileSync(path.join(root, 'site/assets/releases.js'), 'utf8');
      const env = fakeGh(root, releases, assets);
      const done = run(root, SITE, undefined, env);
      assert.notEqual(done.status, 0, JSON.stringify(releases));
      assert.match(done.stderr, words);
      assert.equal(readFileSync(path.join(root, 'site/assets/releases.js'), 'utf8'), before);
    });
  }
  withSiteRepo((root) => {
    const env = fakeGh(root, []);
    const extra = spawnSync('bash', [path.join(root, SITE), 'v1.0.0'], { env: { ...env }, encoding: 'utf8' });
    assert.equal(extra.status, 2);
    assert.match(extra.stderr, /^usage: /);
  });
});

test('latest-flag.sh makes a release Latest only when no published release of its line is numerically higher, and a version 1 release never once version 2 is published', () => {
  const cases = [
    ['v2.0.0', [], 'true'],
    ['v2.0.1', [released('v2.0.0'), released('v1.0.0')], 'true'],
    ['v1.0.1', [released('v1.0.0')], 'true'],
    ['v1.0.1', [released('v1.0.0'), released('v2.0.0')], 'false'],
    ['v1.0.1', [released('v2.1.0', { isDraft: true }), released('v1.0.0')], 'true'],
    ['v1.0.1', [released('v2.0.0', { isPrerelease: true })], 'true'],
    ['v1.0.1', [released('v20.0.0'), released('v2.0.0-rc1')], 'true'],
    ['v2.0.0', [released('v1.5.0')], 'true'],
    ['v2.0.1', [released('v2.1.0'), released('v2.0.0')], 'false'],
    ['v2.0.1', [released('v2.1.0', { isDraft: true }), released('v2.0.0')], 'true'],
    ['v2.0.1', [released('v2.1.0', { isPrerelease: true })], 'true'],
    ['v2.0.1', [released('v2.0.1'), released('v2.0.0')], 'true'],
    ['v2.0.10', [released('v2.0.9')], 'true'],
    ['v2.0.9', [released('v2.0.10')], 'false'],
    ['v2.9.0', [released('v2.10.0')], 'false'],
    ['v2.1.0', [released('v2.1.0-rc1'), released('v20.0.0')], 'true'],
    ['v1.0.1', [released('v1.0.2')], 'false'],
    ['v1.0.10', [released('v1.0.9')], 'true'],
    ['v1.0.1', [released('v1.1.0', { isDraft: true })], 'true'],
  ];
  withTree((root) => {
    for (const [tag, releases, flag] of cases) {
      const env = fakeGh(root, releases);
      rmSync(env.FAKE_GH_LOG, { force: true });
      const done = run(root, LATEST, tag, env);
      assert.equal(done.status, 0, `${tag} ${done.stderr}`);
      assert.equal(done.stdout, `${flag}\n`, `${tag} with ${JSON.stringify(releases)}`);
      assert.deepEqual(ghLog(env), ['release list --limit 1000 --json tagName,isDraft,isPrerelease']);
    }
    const env = fakeGh(root, []);
    for (const tag of ['v3.0.0', 'v1.0', 'latest']) {
      const done = run(root, LATEST, tag, env);
      assert.equal(done.status, 1, tag);
      assert.equal(done.stdout, '', tag);
    }
    assert.equal(run(root, LATEST, undefined, env).status, 2);
    const broken = { ...env, FAKE_GH_LIST: path.join(root, 'missing.json') };
    writeFileSync(path.join(root, 'missing.json'), 'not json');
    for (const tag of ['v1.0.1', 'v2.0.1']) {
      const failed = run(root, LATEST, tag, broken);
      assert.notEqual(failed.status, 0, tag);
      assert.equal(failed.stdout, '', tag);
    }
  }, [LATEST]);
});

test('the version check refuses tags that are not v<major>.<minor>.<patch> and asks for exactly one tag', () => {
  withTree((root) => {
    for (const tag of [VERSION, `v${VERSION}-rc1`, 'v1.0', 'v1.0.0.0', 'latest', '']) {
      const done = run(root, CHECK, tag);
      assert.equal(done.status, 1, tag);
      assert.match(done.stderr, /must look like v1\.2\.3/, tag);
    }
    const none = spawnSync('bash', [path.join(root, CHECK)], { encoding: 'utf8' });
    assert.equal(none.status, 2);
    assert.match(none.stderr, /^usage: /);
  });
});

test('the release notes are the body of one CHANGELOG section, without its heading, the next section or the link list', () => {
  withTree((root) => {
    writeFileSync(path.join(root, 'CHANGELOG.md'), [
      '# Changelog',
      '',
      'Intro.',
      '',
      '## [1.1.0] - 2026-12-01',
      '',
      'Newer.',
      '',
      '## [1.0.10] - 2026-11-20',
      '',
      'Not this one.',
      '',
      '## [1.0.1] - 2026-11-10',
      '',
      '',
      'Fixed one thing.',
      '',
      '### Українською',
      '',
      'Виправлено одне.',
      '',
      '',
      '[1.1.0]: https://example.com/1.1.0',
      '[1.0.1]: https://example.com/1.0.1',
      '',
    ].join('\n'));
    const done = run(root, NOTES, '1.0.1');
    assert.equal(done.status, 0, done.stderr);
    assert.equal(done.stdout, 'Fixed one thing.\n\n### Українською\n\nВиправлено одне.\n');
    assert.equal(run(root, NOTES, '1.1.0').stdout, 'Newer.\n');
    const missing = run(root, NOTES, '1.0.2');
    assert.equal(missing.status, 1);
    assert.match(missing.stderr, /CHANGELOG\.md has no '## \[1\.0\.2\] - <date>' section/);
    assert.equal(run(root, NOTES, 'v1.0.1').status, 2);
  });
});

test('the notes of this version come in English first, then in Ukrainian, with no line wrapped inside a paragraph', () => {
  const done = run(repoDir, NOTES, VERSION);
  const notes = done.stdout;
  assert.ok(notes.indexOf('### Українською') > 0);
  assert.doesNotMatch(notes.slice(0, notes.indexOf('### Українською')), /[А-Яа-яІіЇїЄєҐґ]/);
  assert.doesNotMatch(notes, /^## /m);
  assert.doesNotMatch(notes, /!/);
  const lines = notes.split('\n');
  lines.forEach((line, i) => {
    if (i === 0 || line.trim() === '' || lines[i - 1].trim() === '') return;
    assert.match(line, /^(#|- )/, `a release page shows every newline, so this line continues the one before it: ${line}`);
  });
});

test('CI runs the server, site and firmware tests on every push and pull request, and the release reuses it', () => {
  const ci = readRepo('.github/workflows/ci.yml');
  assert.match(ci, /^on:\n  push:\n    branches: \["\*\*"\]\n  pull_request:\n  workflow_call:\n/m);
  assert.match(ci, /^permissions:\n  contents: read$/m);
  const test = jobOf(ci, 'test');
  assert.ok(test.indexOf('actions/setup-node@v4') < test.indexOf('pytest -q -W error'), 'the server tests need node for the settings page tests');
  assert.match(test, /working-directory: v1\/hub/);
  assert.match(test, /node --test site\/tests\/\*\.test\.mjs/);
  assert.match(jobOf(ci, 'test-firmware'), /pytest -q v1\/firmware\/tests/);
  assert.doesNotMatch(ci, /secrets\./);
  const release = readRepo('.github/workflows/release.yml');
  assert.match(jobOf(release, 'test'), /uses: \.\/\.github\/workflows\/ci\.yml/);
  for (const [file, text] of [['ci.yml', ci], ['release.yml', release]]) {
    for (const action of text.matchAll(/uses: (actions\/[\w-]+)@(\S+)/g)) {
      const pinned = { checkout: 'v4', 'setup-python': 'v5', 'setup-node': 'v4', cache: 'v4', 'upload-artifact': 'v4', 'download-artifact': 'v4', 'upload-pages-artifact': 'v3', 'deploy-pages': 'v4' };
      assert.equal(action[2], pinned[action[1].split('/')[1]], `${file}: ${action[0]}`);
    }
  }
});

function inOrder(text, steps, label) {
  const at = steps.map((step) => text.indexOf(step));
  assert.ok(at.every((i, n) => i > 0 && (n === 0 || i > at[n - 1])), `${label}: steps missing or out of order: ${steps.map((step, n) => `${at[n]} ${step}`).join(' | ')}`);
}

test('a release checks the tag of its line first and tells the later jobs which line it is', () => {
  const yml = readRepo('.github/workflows/release.yml');
  const check = jobOf(yml, 'check');
  assert.match(check, /bash \.github\/scripts\/check-version\.sh "\$TAG"/);
  assert.match(check, /bash \.github\/scripts\/release-notes\.sh "\$\{TAG#v\}"/);
  assert.match(check, /line: \$\{\{ steps\.version\.outputs\.line \}\}/);
  assert.match(check, /\{\n\s+echo "tag=\$TAG"\n\s+echo "version=\$version"\n\s+echo "line=\$\{version%%\.\*\}"\n\s+\} >> "\$GITHUB_OUTPUT"/);
  assert.match(jobOf(yml, 'test'), /needs: check/);
  assert.match(yml, /^  push:\n    tags: \["v\*\.\*\.\*"\]$/m);
});

test('a version 1 tag builds the ESPHome image and a version 2 tag builds in the pinned ESP-IDF container, each after the tests', () => {
  const yml = readRepo('.github/workflows/release.yml');
  const v1 = jobOf(yml, 'build-v1');
  assert.match(v1, /needs: \[check, test\]/);
  assert.match(v1, /if: github\.event_name == 'push' && needs\.check\.outputs\.line == '1'/);
  inOrder(v1, ['pip install -r v1/firmware/requirements.txt', 'bash v1/firmware/build-web.sh\n', 'cp site/firmware/v1/pytaichenko.factory.bin "dist/pytaichenko-$VERSION.factory.bin"', 'actions/upload-artifact@v4'], 'build-v1');
  assert.match(v1, /name: firmware\n/);
  const v2 = jobOf(yml, 'build-v2');
  assert.match(v2, /needs: \[check, test\]/);
  assert.match(v2, /if: github\.event_name == 'push' && needs\.check\.outputs\.line == '2'/);
  const idf = readRepo('v2/firmware/IDF_VERSION').trim();
  assert.match(v2, new RegExp(`container: espressif/idf:${idf.replace(/\./g, '\\.')}\n`));
  assert.match(jobOf(readRepo('.github/workflows/ci.yml'), 'build-v2-firmware'), new RegExp(`container: espressif/idf:${idf.replace(/\./g, '\\.')}\n`));
  inOrder(v2, [
    'test "$(idf.py --version)" = "ESP-IDF $(cat v2/firmware/IDF_VERSION)"',
    'idf.py -C v2/firmware build',
    'bash v2/firmware/build-web.sh --version "$VERSION" --out dist/web --dist dist/image',
    'cp dist/image/pytaichenko.factory.bin "dist/pytaichenko-$VERSION.factory.bin"',
    'cp dist/image/pytaichenko.ota.bin "dist/pytaichenko-$VERSION.ota.bin"',
    'actions/upload-artifact@v4',
  ], 'build-v2');
  assert.match(v2, /name: firmware\n/);
  assert.match(v2, /path: dist\/pytaichenko-\*\.bin\n/);
  for (const job of [v1, v2]) assert.match(job, /VERSION: \$\{\{ needs\.check\.outputs\.version \}\}/);
  for (const block of runBlocks(v2)) assert.ok(!/idf\.py|build-web/.test(block) || block.includes('. "$IDF_PATH/export.sh"'), block);
});

test('the release job publishes the files of the tag line with the Latest flag from latest-flag.sh', () => {
  const yml = readRepo('.github/workflows/release.yml');
  const release = jobOf(yml, 'release');
  assert.match(release, /needs: \[check, build-v1, build-v2\]/);
  assert.match(release, /!cancelled\(\) && github\.event_name == 'push'/);
  assert.match(release, /needs\.build-v1\.result == 'success' \|\| needs\.build-v2\.result == 'success'/);
  assert.match(release, /contents: write/);
  assert.match(release, /GH_TOKEN: \$\{\{ github\.token \}\}/);
  assert.match(release, /LINE: \$\{\{ needs\.check\.outputs\.line \}\}/);
  inOrder(release, [
    'actions/download-artifact@v4',
    'zip -j "dist/pytaichenko-$VERSION-stl.zip" "v$LINE/case/stl/"*.stl LICENSE-hardware',
    'release-notes.sh "$VERSION" > dist/notes.md',
    'latest="$(bash .github/scripts/latest-flag.sh "$TAG")"',
    'files=("dist/pytaichenko-$VERSION.factory.bin")',
    'if [ "$LINE" = 2 ]; then files+=("dist/pytaichenko-$VERSION.ota.bin"); fi',
    'files+=("dist/pytaichenko-$VERSION-stl.zip")',
    'gh release create "$TAG" "${files[@]}"',
  ], 'release');
  assert.match(release, /name: firmware\n\s+path: dist\n/);
  assert.match(release, /--verify-tag/);
  assert.match(release, /--title "Pytaichenko \$VERSION"/);
  assert.match(release, /--notes-file dist\/notes\.md/);
  assert.match(release, /--latest="\$latest"/);
  assert.doesNotMatch(yml, /uses: (?!actions\/|\.\/\.github\/workflows\/)/, 'no third-party actions');
});

test('the site is built from main with the newest release of each line, whichever line was released, and the latest-release guard is gone', () => {
  const yml = readRepo('.github/workflows/release.yml');
  const site = jobOf(yml, 'site');
  assert.match(site, /needs: \[check, release\]/);
  assert.match(site, /needs\.release\.result == 'success'/);
  assert.match(site, /github\.event_name == 'workflow_dispatch' && needs\.release\.result == 'skipped'/);
  assert.match(site, /- uses: actions\/checkout@v4\n\s+with:\n\s+ref: main\n\s+fetch-depth: 0\n/);
  assert.doesNotMatch(site, /refs\/tags|needs\.check\.outputs\.tag/);
  assert.doesNotMatch(yml, /gh release view|Publish only the latest release/);
  inOrder(site, ['bash .github/scripts/site-releases.sh\n', 'rm -rf site/tests', 'actions/upload-pages-artifact@v3', 'actions/deploy-pages@v4'], 'site');
  assert.match(site, /GH_TOKEN: \$\{\{ github\.token \}\}/);
  assert.match(site, /GH_REPO: \$\{\{ github\.repository \}\}/);
  const script = readRepo(SITE);
  assert.match(script, /gh release list --limit 1000 --json tagName,isDraft,isPrerelease/);
  assert.match(script, /git -C "\$root" cat-file -e "\$tag:v1\/case\/stl"/);
});

test('workflow values reach the shell through the environment, never by expanding an expression inside a script', () => {
  for (const file of ['.github/workflows/ci.yml', '.github/workflows/release.yml']) {
    const blocks = runBlocks(readRepo(file));
    assert.ok(blocks.length > 3, file);
    for (const block of blocks) assert.doesNotMatch(block, /\$\{\{/, `${file}: ${block}`);
  }
});

test('the scripts the workflows run are in the repository and have no comments', () => {
  for (const file of [CHECK, NOTES, SITE, LATEST]) {
    assert.ok(existsSync(path.join(repoDir, file)), file);
    const lines = readRepo(file).split('\n').slice(1);
    assert.ok(!lines.some((line) => /^\s*#/.test(line)), `${file} has a comment line`);
  }
});
