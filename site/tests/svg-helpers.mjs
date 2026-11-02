import assert from 'node:assert/strict';
import { readSite } from './helpers.mjs';

export function openAttr(tagAttrs, name) {
  const m = new RegExp(`(?:^|\\s)${name}\\s*=\\s*"([^"]*)"`).exec(tagAttrs);
  return m ? m[1] : null;
}

export function luminance(hex) {
  const full = hex.length === 4 ? `#${hex[1]}${hex[1]}${hex[2]}${hex[2]}${hex[3]}${hex[3]}` : hex;
  const n = parseInt(full.slice(1), 16);
  const [r, g, b] = [(n >> 16) & 255, (n >> 8) & 255, n & 255].map((v) => {
    const c = v / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

export function contrast(a, b) {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}

export function cssBlock(css, selector) {
  const start = css.indexOf(`${selector} {`);
  assert.ok(start >= 0, `no ${selector} block`);
  return css.slice(start, css.indexOf('}', start));
}

export function pageBackgrounds() {
  const css = readSite('assets/style.css');
  const pick = (b) => /--bg:\s*(#[0-9a-f]{6})/i.exec(b)[1];
  return { dark: pick(cssBlock(css, ':root')), light: pick(cssBlock(css, ':root[data-theme="light"]')) };
}

export const HEX = /^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i;

export function svgItems(svg) {
  const items = [];
  for (const m of svg.matchAll(/<(rect|circle|text|ellipse)\b([^>]*?)\/?>/g)) {
    const attrs = m[2];
    const num = (name) => {
      const v = openAttr(attrs, name);
      return v === null ? null : Number(v);
    };
    const item = { kind: m[1], attrs, x: num('x'), y: num('y'), fill: openAttr(attrs, 'fill'), stroke: openAttr(attrs, 'stroke') };
    if (m[1] === 'rect') {
      item.box = { x0: num('x'), y0: num('y'), x1: num('x') + num('width'), y1: num('y') + num('height') };
    } else if (m[1] === 'circle') {
      item.box = { x0: num('cx') - num('r'), y0: num('cy') - num('r'), x1: num('cx') + num('r'), y1: num('cy') + num('r') };
    } else if (m[1] === 'text') {
      const style = openAttr(attrs, 'style') || '';
      const f = /fill:\s*([^;"]+)/.exec(style);
      item.fill = f ? f[1].trim() : openAttr(attrs, 'fill');
      item.i18n = openAttr(attrs, 'data-i18n');
    }
    items.push(item);
  }
  return items;
}

export function paintedShapes(items) {
  return items.filter((it) => (it.kind === 'rect' || it.kind === 'circle') && it.fill && HEX.test(it.fill));
}

export function insideOf(shape, x, y) {
  return shape.box.x0 <= x && x <= shape.box.x1 && shape.box.y0 <= y && y <= shape.box.y1;
}
