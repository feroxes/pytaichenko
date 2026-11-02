import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { loadScripts, siteDir } from './helpers.mjs';

const repoDir = path.resolve(siteDir, '..');
const CHECK = '.github/scripts/check-version.sh';
const NOTES = '.github/scripts/release-notes.sh';
const TREE = [CHECK, NOTES, 'firmware/build-web.sh', 'firmware/pytaichenko.yaml', 'hub/pyproject.toml', 'site/assets/config.js', 'CHANGELOG.md'];

function readRepo(relPath) {
  return readFileSync(path.join(repoDir, relPath), 'utf8');
}

function withTree(fn) {
  const dir = mkdtempSync(path.join(tmpdir(), 'pytaichenko-release-'));
  try {
    for (const file of TREE) {
      mkdirSync(path.dirname(path.join(dir, file)), { recursive: true });
      cpSync(path.join(repoDir, file), path.join(dir, file));
    }
    return fn(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

function run(root, script, arg) {
  const env = { PATH: process.env.PATH };
  return spawnSync('bash', [path.join(root, script), arg], { env, encoding: 'utf8' });
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

const VERSION = loadScripts(['assets/config.js']).CONFIG.version;

test('the firmware, the server and the site carry the same version, and CHANGELOG.md has its notes', () => {
  const firmware = /^  project:\n    name: Pytaichenko\.Speaker\n    version: "([^"]+)"$/m.exec(readRepo('firmware/pytaichenko.yaml'));
  const hub = /^version = "([^"]+)"$/m.exec(readRepo('hub/pyproject.toml'));
  assert.ok(firmware && hub);
  assert.equal(firmware[1], VERSION);
  assert.equal(hub[1], VERSION);
  const checked = run(repoDir, CHECK, `v${VERSION}`);
  assert.equal(checked.status, 0, checked.stderr);
  const notes = run(repoDir, NOTES, VERSION);
  assert.equal(notes.status, 0, notes.stderr);
  assert.match(readRepo('CHANGELOG.md'), new RegExp(`^## \\[${VERSION.replace(/\./g, '\\.')}\\] - \\d{4}-\\d{2}-\\d{2}$`, 'm'));
});

test('the version check passes for the matching tag and names every place that differs from another tag', () => {
  withTree((root) => {
    const ok = run(root, CHECK, `v${VERSION}`);
    assert.equal(ok.status, 0, ok.stderr);
    assert.equal(ok.stderr, '');

    const next = run(root, CHECK, 'v9.8.7');
    assert.equal(next.status, 1);
    for (const place of ['firmware/pytaichenko.yaml', 'hub/pyproject.toml', 'site/assets/config.js']) {
      assert.match(next.stderr, new RegExp(`${place.replace(/\./g, '\\.')} \\([^)]*\\) is ${VERSION.replace(/\./g, '\\.')}, but the tag v9\\.8\\.7 needs 9\\.8\\.7`));
    }

    edit(root, 'hub/pyproject.toml', `version = "${VERSION}"`, 'version = "9.8.7"');
    edit(root, 'site/assets/config.js', `version: '${VERSION}'`, "version: '9.8.7'");
    const one = run(root, CHECK, 'v9.8.7');
    assert.equal(one.status, 1);
    assert.match(one.stderr, /firmware\/pytaichenko\.yaml/);
    assert.doesNotMatch(one.stderr, /hub\/pyproject\.toml|site\/assets\/config\.js/);

    edit(root, 'firmware/pytaichenko.yaml', `version: "${VERSION}"`, "version: '9.8.7' # bumped");
    const all = run(root, CHECK, 'v9.8.7');
    assert.equal(all.status, 0, all.stderr);
  });
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
  assert.match(test, /working-directory: hub/);
  assert.match(test, /node --test site\/tests\/\*\.test\.mjs/);
  assert.match(jobOf(ci, 'test-firmware'), /pytest -q firmware\/tests/);
  assert.doesNotMatch(ci, /secrets\./);
  const release = readRepo('.github/workflows/release.yml');
  assert.match(jobOf(release, 'test'), /uses: \.\/\.github\/workflows\/ci\.yml/);
  for (const [file, text] of [['ci.yml', ci], ['release.yml', release]]) {
    for (const action of text.matchAll(/uses: (actions\/[\w-]+)@(\S+)/g)) {
      const pinned = { checkout: 'v4', 'setup-python': 'v5', 'setup-node': 'v4', cache: 'v4', 'upload-pages-artifact': 'v3', 'deploy-pages': 'v4' };
      assert.equal(action[2], pinned[action[1].split('/')[1]], `${file}: ${action[0]}`);
    }
  }
});

test('a release checks the tag first, tests, builds, publishes the release, then the site from that release', () => {
  const yml = readRepo('.github/workflows/release.yml');
  const check = jobOf(yml, 'check');
  assert.match(check, /bash \.github\/scripts\/check-version\.sh "\$TAG"/);
  assert.match(check, /bash \.github\/scripts\/release-notes\.sh "\$\{TAG#v\}"/);
  assert.match(jobOf(yml, 'test'), /needs: check/);
  const release = jobOf(yml, 'release');
  assert.match(release, /needs: \[check, test\]/);
  assert.match(release, /contents: write/);
  const steps = ['bash firmware/build-web.sh\n', 'cp site/firmware/pytaichenko.factory.bin "dist/pytaichenko-$VERSION.factory.bin"', 'release-notes.sh "$VERSION" > dist/notes.md', 'gh release create "$TAG"'];
  const at = steps.map((step) => release.indexOf(step));
  assert.ok(at.every((i, n) => i > 0 && (n === 0 || i > at[n - 1])), `release steps out of order: ${at}`);
  assert.match(release, /"dist\/pytaichenko-\$VERSION\.factory\.bin"\s+"dist\/pytaichenko-\$VERSION-stl\.zip"/);
  assert.match(release, /--title "Pytaichenko \$VERSION"/);
  assert.match(release, /--notes-file dist\/notes\.md/);
  assert.match(release, /GH_TOKEN: \$\{\{ github\.token \}\}/);
  const site = jobOf(yml, 'site');
  assert.match(site, /needs: \[check, release\]/);
  assert.match(site, /needs\.release\.result == 'success'/);
  assert.match(site, /github\.event_name == 'workflow_dispatch' && needs\.release\.result == 'skipped'/);
  assert.ok(site.indexOf('gh release view --json tagName') < site.indexOf('gh release download'), 'only the latest release reaches the site');
  assert.doesNotMatch(yml, /uses: (?!actions\/|\.\/\.github\/workflows\/)/, 'no third-party actions');
});

test('workflow values reach the shell through the environment, never by expanding an expression inside a script', () => {
  for (const file of ['.github/workflows/ci.yml', '.github/workflows/release.yml']) {
    const blocks = runBlocks(readRepo(file));
    assert.ok(blocks.length > 3, file);
    for (const block of blocks) assert.doesNotMatch(block, /\$\{\{/, `${file}: ${block}`);
  }
});

test('the scripts the workflows run are in the repository and have no comments', () => {
  for (const file of [CHECK, NOTES]) {
    assert.ok(existsSync(path.join(repoDir, file)), file);
    const lines = readRepo(file).split('\n').slice(1);
    assert.ok(!lines.some((line) => /^\s*#/.test(line)), `${file} has a comment line`);
  }
});
