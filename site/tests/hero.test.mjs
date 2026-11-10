import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { loadScripts, readPage, readSite, siteDir } from './helpers.mjs';

function jpegSegments(bytes) {
  const markers = [];
  let i = 2;
  while (i < bytes.length && bytes[i] === 0xff) {
    const marker = bytes[i + 1];
    if (marker === 0xda) break;
    const length = bytes.readUInt16BE(i + 2);
    markers.push({ marker, at: i });
    i += 2 + length;
  }
  return markers;
}

function jpegSize(bytes) {
  for (const { marker, at } of jpegSegments(bytes)) {
    if (marker >= 0xc0 && marker <= 0xc2) return [bytes.readUInt16BE(at + 7), bytes.readUInt16BE(at + 5)];
  }
  return null;
}

test('the top of the page shows a photo of the finished speaker', () => {
  const page = readPage();
  const hero = /<section id="about" class="hero">([\s\S]*?)<\/section>/.exec(page)[1];
  assert.doesNotMatch(page, /media-placeholder|hero\.media\b|renders\/hero\.png/);
  const img = /<img\b[^>]*class="hero-media"[^>]*>/.exec(hero);
  assert.ok(img, 'the hero has an image');
  assert.match(img[0], /src="assets\/photo\.jpg"/);
  assert.match(img[0], /width="1200" height="900"/);
  assert.match(img[0], /data-i18n-attr="alt:hero\.image"/);
  const { TEXT } = loadScripts(['assets/text.js']);
  assert.match(TEXT.uk['hero.image'], /^Питайченко на столі: /);
  assert.match(TEXT.en['hero.image'], /^Pytaichenko on a desk: /);
  assert.match(readSite('assets/style.css'), /\.hero-media \{[^}]*grid-area: media;/);
});

test('the photo is 1200x900 and carries no camera or location data', () => {
  const bytes = readFileSync(path.join(siteDir, 'assets/photo.jpg'));
  assert.deepEqual([...bytes.subarray(0, 3)], [0xff, 0xd8, 0xff]);
  assert.deepEqual(jpegSize(bytes), [1200, 900]);
  const app = jpegSegments(bytes).map((s) => s.marker).filter((m) => m >= 0xe1 && m <= 0xef);
  assert.deepEqual(app, [], 'no EXIF, XMP or other application data');
  assert.ok(bytes.length < 250 * 1024);
});

test('the render that stood in for the photo is gone', () => {
  assert.ok(!existsSync(path.join(siteDir, '..', 'v1', 'case', 'hero.scad')));
  assert.doesNotMatch(readFileSync(path.join(siteDir, '..', 'v1', 'case', 'render_steps.sh'), 'utf8'), /hero/);
  assert.ok(!readdirSync(path.join(siteDir, 'assets', 'renders')).includes('hero.png'));
  for (const readme of ['README.md', 'README.uk.md']) {
    const text = readFileSync(path.join(siteDir, '..', readme), 'utf8');
    assert.match(text, /<img src="site\/assets\/photos-v2\/speaker\.jpg"/, readme);
    assert.doesNotMatch(text, /renders\/hero\.png/, readme);
  }
});

test('the page carries a link preview with the version 2 photo for chats and social networks', () => {
  const head = /<head>([\s\S]*?)<\/head>/.exec(readPage())[1];
  const meta = (key) => {
    const m = new RegExp(`<meta (?:property|name)="${key.replace(/[.:]/g, '\\$&')}" content="([^"]*)">`).exec(head);
    return m ? m[1] : null;
  };
  const base = 'https://feroxes.github.io/pytaichenko/';
  assert.equal(meta('og:type'), 'website');
  assert.equal(meta('og:url'), base);
  assert.equal(meta('og:title'), /<title[^>]*>([^<]*)<\/title>/.exec(head)[1]);
  assert.ok(meta('og:description').length > 60 && meta('og:description').length < 300);
  const image = meta('og:image');
  assert.ok(image.startsWith(base), 'an absolute address on the published site');
  const bytes = readFileSync(path.join(siteDir, image.slice(base.length)));
  assert.deepEqual(jpegSize(bytes), [Number(meta('og:image:width')), Number(meta('og:image:height'))]);
  assert.ok(meta('og:image:alt').startsWith('Питайченко на столі: '));
  assert.equal(meta('og:locale'), 'uk_UA');
  assert.equal(meta('og:locale:alternate'), 'en_US');
  assert.equal(meta('twitter:card'), 'summary_large_image');
});
