(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});
  var STORAGE_KEY = 'pytaichenko-lang';
  var LANGS = ['uk', 'en'];

  function has(dict, key) {
    return Object.prototype.hasOwnProperty.call(dict, key);
  }

  function pickLang(saved, navLangs) {
    if (LANGS.indexOf(saved) >= 0) return saved;
    var list = navLangs && navLangs.length ? Array.prototype.slice.call(navLangs) : [];
    for (var i = 0; i < list.length; i++) {
      var primary = String(list[i]).toLowerCase().split('-')[0];
      if (primary === 'uk' || primary === 'ru') return 'uk';
    }
    return 'en';
  }

  function langFromSearch(search) {
    var found = /[?&]lang=([^&#]*)/.exec(String(search || ''));
    return found && LANGS.indexOf(found[1]) >= 0 ? found[1] : null;
  }

  function urlWithParams(href, values) {
    var names = Object.keys(values);
    var cut = href.indexOf('#');
    var hash = cut >= 0 ? href.slice(cut) : '';
    var rest = cut >= 0 ? href.slice(0, cut) : href;
    var q = rest.indexOf('?');
    var base = q >= 0 ? rest.slice(0, q) : rest;
    var params = (q >= 0 ? rest.slice(q + 1).split('&') : []).filter(function (part) {
      return part && names.indexOf(part.split('=')[0]) < 0;
    });
    names.forEach(function (name) {
      params.push(name + '=' + encodeURIComponent(values[name]));
    });
    return base + '?' + params.join('&') + hash;
  }

  function urlWithLang(href, lang) {
    return urlWithParams(href, { lang: lang });
  }

  function markUrl(lang) {
    var history = window.history;
    if (!window.location || !history || typeof history.replaceState !== 'function') return;
    var values = { lang: lang };
    if (P.VERSIONS && P.VERSIONS.indexOf(P.version) >= 0) values.v = P.version;
    try {
      history.replaceState(history.state, '', urlWithParams(window.location.href, values));
    } catch (e) {}
  }

  function applyText(root, lang) {
    var dict = P.TEXT && P.TEXT[lang];
    if (!dict) return;
    var texts = root.querySelectorAll('[data-i18n]');
    for (var i = 0; i < texts.length; i++) {
      var key = texts[i].getAttribute('data-i18n');
      if (has(dict, key)) texts[i].textContent = dict[key];
    }
    var attrs = root.querySelectorAll('[data-i18n-attr]');
    for (var j = 0; j < attrs.length; j++) {
      var pairs = attrs[j].getAttribute('data-i18n-attr').split(';');
      for (var k = 0; k < pairs.length; k++) {
        var cut = pairs[k].indexOf(':');
        if (cut < 1) continue;
        var name = pairs[k].slice(0, cut).trim();
        var attrKey = pairs[k].slice(cut + 1).trim();
        if (has(dict, attrKey)) attrs[j].setAttribute(name, dict[attrKey]);
      }
    }
  }

  function markSwitch(doc, lang) {
    var buttons = doc.querySelectorAll('[data-set-lang]');
    for (var i = 0; i < buttons.length; i++) {
      var on = buttons[i].getAttribute('data-set-lang') === lang;
      buttons[i].setAttribute('aria-pressed', on ? 'true' : 'false');
    }
  }

  function show(lang) {
    var doc = window.document;
    P.lang = lang;
    applyText(doc.documentElement, lang);
    doc.documentElement.setAttribute('lang', lang);
    markSwitch(doc, lang);
    markUrl(lang);
    doc.dispatchEvent(new window.CustomEvent('pytaichenko:lang', { detail: { lang: lang } }));
  }

  function setLang(lang) {
    if (LANGS.indexOf(lang) < 0) return;
    show(lang);
    try {
      window.localStorage.setItem(STORAGE_KEY, lang);
    } catch (e) {}
  }

  function readSaved() {
    try {
      return window.localStorage.getItem(STORAGE_KEY);
    } catch (e) {
      return null;
    }
  }

  function navigatorLangs() {
    var nav = window.navigator || {};
    return nav.languages && nav.languages.length ? nav.languages : nav.language ? [nav.language] : [];
  }

  function ready() {
    var doc = window.document;
    var buttons = doc.querySelectorAll('[data-set-lang]');
    for (var i = 0; i < buttons.length; i++) {
      (function (button) {
        button.addEventListener('click', function () {
          setLang(button.getAttribute('data-set-lang'));
        });
      })(buttons[i]);
    }
    if (P.applyConfig) P.applyConfig(doc);
    show(P.lang);
  }

  P.pickLang = pickLang;
  P.langFromSearch = langFromSearch;
  P.urlWithParams = urlWithParams;
  P.urlWithLang = urlWithLang;
  P.applyText = applyText;
  P.setLang = setLang;

  P.lang = pickLang(langFromSearch((window.location || {}).search) || readSaved(), navigatorLangs());
  window.document.documentElement.setAttribute('lang', P.lang);

  if (window.document.readyState === 'loading') {
    window.document.addEventListener('DOMContentLoaded', ready);
  } else {
    ready();
  }
})(window);
