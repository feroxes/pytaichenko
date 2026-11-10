import vm from 'node:vm';
import { readWeb } from './helpers.mjs';

const VOID = new Set(['meta', 'link', 'input', 'br', 'img', 'hr', 'source']);
const BOOLEAN = ['hidden', 'disabled'];

class Text {
  constructor(data) {
    this.nodeType = 3;
    this.data = data;
    this.parentNode = null;
  }

  get textContent() {
    return this.data;
  }
}

class Element {
  constructor(document, tag) {
    this.nodeType = 1;
    this.ownerDocument = document;
    this.tagName = tag.toUpperCase();
    this.attributes = new Map();
    this.childNodes = [];
    this.parentNode = null;
    this.listeners = {};
    this.onclick = null;
  }

  [Symbol.for('nodejs.util.inspect.custom')]() {
    const id = this.getAttribute('id');
    return `<${this.tagName.toLowerCase()}${id ? `#${id}` : ''}>`;
  }

  get children() {
    return this.childNodes.filter((node) => node.nodeType === 1);
  }

  get firstChild() {
    return this.childNodes[0] || null;
  }

  get textContent() {
    return this.childNodes.map((node) => node.textContent).join('');
  }

  set textContent(value) {
    this.childNodes.forEach((node) => {
      node.parentNode = null;
    });
    this.childNodes = [];
    if (value !== '') this.appendChild(new Text(String(value)));
  }

  getAttribute(name) {
    return this.attributes.has(name) ? this.attributes.get(name) : null;
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  hasAttribute(name) {
    return this.attributes.has(name);
  }

  removeAttribute(name) {
    this.attributes.delete(name);
  }

  get id() {
    return this.getAttribute('id') || '';
  }

  get type() {
    if (this.tagName === 'SELECT') return 'select-one';
    if (this.tagName === 'TEXTAREA') return 'textarea';
    return (this.getAttribute('type') || (this.tagName === 'BUTTON' ? 'submit' : 'text')).toLowerCase();
  }

  get options() {
    return this.querySelectorAll('option');
  }

  get value() {
    if (this.tagName === 'SELECT') {
      const options = this.options.map((option) => option.value);
      if (this._value === undefined) return options[0] ?? '';
      return options.includes(this._value) ? this._value : '';
    }
    if (this.tagName === 'OPTION') return this.getAttribute('value') ?? this.textContent;
    if (this._value === undefined) return this.tagName === 'TEXTAREA' ? this.textContent : this.getAttribute('value') ?? '';
    return this._value;
  }

  set value(value) {
    if (this.tagName === 'SELECT') {
      const wanted = String(value);
      this._value = this.options.some((option) => option.value === wanted) ? wanted : null;
    } else this._value = String(value);
  }

  get checked() {
    return this._checked === undefined ? this.hasAttribute('checked') : this._checked;
  }

  set checked(value) {
    this._checked = !!value;
  }

  appendChild(node) {
    return this.insertBefore(node, null);
  }

  insertBefore(node, before) {
    if (node.parentNode) node.parentNode.removeChild(node);
    const at = before ? this.childNodes.indexOf(before) : -1;
    if (at < 0) this.childNodes.push(node);
    else this.childNodes.splice(at, 0, node);
    node.parentNode = this;
    return node;
  }

  removeChild(node) {
    const at = this.childNodes.indexOf(node);
    if (at >= 0) this.childNodes.splice(at, 1);
    node.parentNode = null;
    return node;
  }

  descendants() {
    const out = [];
    const walk = (node) => {
      for (const child of node.children) {
        out.push(child);
        walk(child);
      }
    };
    walk(this);
    return out;
  }

  querySelectorAll(selector) {
    const groups = parseSelector(selector);
    return this.descendants().filter((el) => groups.some((chain) => matchesChain(el, chain, this)));
  }

  querySelector(selector) {
    return this.querySelectorAll(selector)[0] || null;
  }

  matches(selector) {
    return parseSelector(selector).some((chain) => matchesChain(this, chain, null));
  }

  closest(selector) {
    let node = this;
    while (node && node.nodeType === 1) {
      if (node.matches(selector)) return node;
      node = node.parentNode;
    }
    return null;
  }

  addEventListener(type, fn) {
    (this.listeners[type] = this.listeners[type] || []).push(fn);
  }

  dispatchEvent(event) {
    event.target = event.target || this;
    let node = this;
    while (node && !event.stopped) {
      (node.listeners[event.type] || []).slice().forEach((fn) => fn.call(node, event));
      if (event.type === 'click' && node === this && typeof node.onclick === 'function') node.onclick(event);
      node = node.parentNode || (node === this.ownerDocument.documentElement ? this.ownerDocument : null);
    }
    return !event.defaultPrevented;
  }

  click() {
    if (this.disabled) return;
    const event = makeEvent('click');
    this.dispatchEvent(event);
    if (event.defaultPrevented) return;
    const href = this.tagName === 'A' ? this.getAttribute('href') : null;
    if (href && href.startsWith('#')) this.ownerDocument.defaultView.location.hash = href;
    else if (this.tagName === 'BUTTON' && this.type === 'submit') {
      const form = this.closest('form');
      if (form) form.dispatchEvent(makeEvent('submit'));
    }
  }

  focus() {
    this.ownerDocument.activeElement = this;
  }

  scrollIntoView() {}

  reset() {
    for (const el of this.querySelectorAll('input, textarea, select')) {
      el._value = undefined;
      el._checked = undefined;
    }
  }

  showModal() {
    this.open = true;
  }

  close() {
    this.open = false;
  }
}

for (const name of BOOLEAN) {
  Object.defineProperty(Element.prototype, name, {
    get() {
      return this.hasAttribute(name);
    },
    set(on) {
      if (on) this.setAttribute(name, '');
      else this.removeAttribute(name);
    },
  });
}

function makeEvent(type, extra = {}) {
  return {
    type,
    target: null,
    defaultPrevented: false,
    stopped: false,
    preventDefault() {
      this.defaultPrevented = true;
    },
    stopPropagation() {
      this.stopped = true;
    },
    ...extra,
  };
}

function parseCompound(text) {
  const parts = { tag: null, ids: [], classes: [], attrs: [] };
  const re = /^([a-z][a-z0-9-]*)|#([\w-]+)|\.([\w-]+)|\[([\w-]+)(?:="([^"]*)")?\]/gi;
  let m;
  let used = 0;
  while ((m = re.exec(text)) !== null) {
    if (m.index !== used) throw new Error(`unsupported selector: ${text}`);
    used = re.lastIndex;
    if (m[1]) parts.tag = m[1].toUpperCase();
    else if (m[2]) parts.ids.push(m[2]);
    else if (m[3]) parts.classes.push(m[3]);
    else parts.attrs.push([m[4], m[5]]);
    if (m[0] === '') break;
  }
  if (used !== text.length) throw new Error(`unsupported selector: ${text}`);
  return parts;
}

function parseSelector(selector) {
  return selector.split(',').map((group) => group.trim().split(/\s+/).map(parseCompound));
}

function matchesCompound(el, parts) {
  if (parts.tag && el.tagName !== parts.tag) return false;
  if (parts.ids.some((id) => el.getAttribute('id') !== id)) return false;
  const classes = (el.getAttribute('class') || '').split(/\s+/);
  if (parts.classes.some((name) => !classes.includes(name))) return false;
  return parts.attrs.every(([name, value]) => (value === undefined ? el.hasAttribute(name) : el.getAttribute(name) === value));
}

function matchesChain(el, chain, root) {
  if (!matchesCompound(el, chain[chain.length - 1])) return false;
  let at = chain.length - 2;
  let node = el.parentNode;
  while (at >= 0 && node && node.nodeType === 1 && node !== root) {
    if (matchesCompound(node, chain[at])) at -= 1;
    node = node.parentNode;
  }
  return at < 0;
}

function parseHtml(document, html) {
  const top = new Element(document, '#root');
  const stack = [top];
  const re = /<!--[\s\S]*?-->|<!doctype[^>]*>|<\/([a-z0-9]+)\s*>|<([a-z0-9]+)((?:\s+[^\s=/>]+(?:\s*=\s*"[^"]*")?)*)\s*(\/?)>|([^<]+)/gi;
  let m;
  while ((m = re.exec(html)) !== null) {
    const current = stack[stack.length - 1];
    if (m[1]) {
      const tag = m[1].toUpperCase();
      while (stack.length > 1 && stack[stack.length - 1].tagName !== tag) stack.pop();
      if (stack.length > 1) stack.pop();
    } else if (m[2]) {
      const el = new Element(document, m[2]);
      for (const a of m[3].matchAll(/([^\s=/>]+)(?:\s*=\s*"([^"]*)")?/g)) el.setAttribute(a[1], a[2] ?? '');
      current.appendChild(el);
      if (!VOID.has(m[2].toLowerCase()) && !m[4]) stack.push(el);
    } else if (m[5] !== undefined) current.appendChild(new Text(m[5]));
  }
  return top.removeChild(top.children[0]);
}

export function settle(rounds = 8) {
  let chain = Promise.resolve();
  for (let i = 0; i < rounds; i += 1) chain = chain.then(() => new Promise((resolve) => setImmediate(resolve)));
  return chain;
}

export function reply(status, body) {
  return { status, body };
}

export function makePage({ routes = {}, hash = '', host = '192.0.2.5:8080', search = '', languages = ['uk-UA'] } = {}) {
  const document = {
    readyState: 'complete',
    visibilityState: 'visible',
    listeners: {},
    activeElement: null,
    addEventListener(type, fn) {
      (this.listeners[type] = this.listeners[type] || []).push(fn);
    },
    dispatchEvent(event) {
      (this.listeners[event.type] || []).slice().forEach((fn) => fn(event));
    },
    createElement(tag) {
      return new Element(document, tag);
    },
    createTextNode(data) {
      return new Text(data);
    },
    querySelector(selector) {
      return this.documentElement.matches(selector) ? this.documentElement : this.documentElement.querySelector(selector);
    },
    querySelectorAll(selector) {
      return this.documentElement.querySelectorAll(selector);
    },
  };
  document.documentElement = parseHtml(document, readWeb('index.html'));
  document.body = document.documentElement.querySelector('body');
  document.activeElement = document.body;

  const calls = [];
  const timers = { intervals: [], timeouts: [] };
  const windowListeners = {};
  let currentHash = hash;
  let currentSearch = search;
  const replaced = [];
  const location = {
    host,
    hostname: host.split(':')[0],
    get search() {
      return currentSearch;
    },
    get href() {
      return `http://${host}/${currentSearch}${currentHash}`;
    },
    get hash() {
      return currentHash;
    },
    set hash(value) {
      const next = value && !value.startsWith('#') ? `#${value}` : value;
      if (next === currentHash) return;
      currentHash = next;
      (windowListeners.hashchange || []).forEach((fn) => fn(makeEvent('hashchange')));
    },
  };
  const window = {
    document,
    location,
    navigator: { languages, language: languages[0] },
    localStorage: { getItem: () => null, setItem() {} },
    history: {
      state: null,
      pushState(state, title, url) {
        currentHash = url;
      },
      replaceState(state, title, url) {
        const parsed = new URL(url);
        currentSearch = parsed.search;
        currentHash = parsed.hash;
        replaced.push(url);
      },
    },
    scrollTo() {},
    confirm: () => true,
    addEventListener(type, fn) {
      (windowListeners[type] = windowListeners[type] || []).push(fn);
    },
    setInterval(fn) {
      timers.intervals.push(fn);
      return timers.intervals.length;
    },
    clearInterval(id) {
      if (id) timers.intervals[id - 1] = null;
    },
    setTimeout(fn) {
      timers.timeouts.push(fn);
      return timers.timeouts.length;
    },
    clearTimeout(id) {
      if (id) timers.timeouts[id - 1] = null;
    },
    fetch(path, options) {
      const method = options.method || 'GET';
      const body = options.body ? JSON.parse(options.body) : undefined;
      calls.push({ method, path, body });
      const handler = routes[`${method} ${path}`];
      if (handler === undefined) return Promise.resolve(response(404, { error: 'not_found', message: 'Not Found' }));
      const answer = typeof handler === 'function' ? handler(body) : handler;
      if (answer instanceof Promise) return answer.then((got) => response(got.status, got.body));
      if (answer === 'network') return Promise.reject(new TypeError('Failed to fetch'));
      return Promise.resolve(response(answer.status, answer.body));
    },
  };
  window.window = window;
  document.defaultView = window;

  function response(status, body) {
    const fresh = body === undefined ? undefined : JSON.parse(JSON.stringify(body));
    return { ok: status >= 200 && status < 300, status, json: () => Promise.resolve(fresh) };
  }

  vm.runInNewContext(readWeb('app.js'), { window, document, navigator: window.navigator }, { filename: 'app.js' });

  const $ = (selector) => document.querySelector(selector);
  return {
    window,
    document,
    calls,
    replaced,
    routes,
    $,
    $$: (selector) => document.querySelectorAll(selector),
    poll() {
      timers.intervals.filter(Boolean).forEach((fn) => fn());
    },
    submit(form) {
      form.dispatchEvent(makeEvent('submit'));
    },
    fire(el, type, extra = {}) {
      const event = makeEvent(type, extra);
      el.dispatchEvent(event);
      return event;
    },
    leave() {
      const event = makeEvent('beforeunload', { returnValue: undefined });
      (windowListeners.beforeunload || []).forEach((fn) => fn(event));
      return event;
    },
    type(input, value) {
      input.value = value;
      input.dispatchEvent(makeEvent('input'));
    },
    visible(el) {
      let node = el;
      while (node && node.nodeType === 1) {
        if (node.hidden) return false;
        node = node.parentNode;
      }
      return true;
    },
  };
}
