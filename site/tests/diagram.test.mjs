import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { siteDir } from './helpers.mjs';

const CYRILLIC = /[Ѐ-ӿ]/;
const DIAGRAMS = {
  en: { readme: 'README.md', labels: ['You', 'Speaker', 'voice-hub', 'OpenAI Realtime', 'At home'] },
  uk: { readme: 'README.uk.md', labels: ['Ви', 'Колонка', 'voice-hub', 'OpenAI Realtime', 'Удома'] },
};

function diagram(lang) {
  return readFileSync(path.join(siteDir, 'assets', `how-it-works-${lang}.svg`), 'utf8');
}

function texts(svg) {
  return [...svg.matchAll(/<text\b[^>]*>([^<]*)<\/text>/g)].map((m) => m[1]);
}

test('the how-it-works diagram exists in both languages and is plain SVG that GitHub shows as it is', () => {
  for (const lang of Object.keys(DIAGRAMS)) {
    const file = path.join(siteDir, 'assets', `how-it-works-${lang}.svg`);
    assert.ok(existsSync(file), file);
    const svg = diagram(lang);
    assert.match(svg, /^<svg xmlns="http:\/\/www\.w3\.org\/2000\/svg" viewBox="0 0 \d+ \d+"/, `${lang} starts with the svg element`);
    assert.doesNotMatch(svg, /<script|foreignObject|<image\b|<use\b|<style|@import|url\(|\son[a-z]+=/i, lang);
    assert.doesNotMatch(svg, /\bhref=/i, `${lang} links to nothing`);
    for (const m of svg.matchAll(/\bfont-size="([\d.]+)"/g)) assert.ok(Number(m[1]) >= 13, `${lang} text of ${m[1]} px`);
    assert.match(svg, /font-family="-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"/);
    assert.doesNotMatch(svg, /<rect\b[^>]*width="100%"/, `${lang} keeps a transparent background`);
  }
});

test('the diagram shows the four cards and the home frame in its own language', () => {
  for (const [lang, { labels }] of Object.entries(DIAGRAMS)) {
    const shown = texts(diagram(lang));
    for (const label of labels) assert.ok(shown.includes(label), `${lang} shows ${label}`);
    assert.ok(shown.length > labels.length, `${lang} has captions under the cards`);
  }
  assert.doesNotMatch(texts(diagram('en')).join(' '), CYRILLIC, 'no Cyrillic in the English diagram');
  const ukWords = texts(diagram('uk')).join(' ').replace(/voice-hub|OpenAI Realtime|OpenAI/g, '');
  assert.doesNotMatch(ukWords, /[A-Za-z]/, 'only the names stay in Latin in the Ukrainian diagram');
});

test('each README shows the diagram in its own language', () => {
  for (const [lang, { readme }] of Object.entries(DIAGRAMS)) {
    const text = readFileSync(path.join(siteDir, '..', readme), 'utf8');
    assert.match(text, new RegExp(`<img src="site/assets/how-it-works-${lang}\\.svg" width="760" alt="[^"]{60,}">`), readme);
  }
});
