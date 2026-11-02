(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});

  P.CONFIG = {
    repoUrl: 'https://github.com/feroxes/pytaichenko',
    version: '1.0.0',
    usdRate: 45.53,
    ratesDate: '2026-10-31',
    donate: {
      buyMeACoffee: '',
      monobank: ''
    }
  };

  function usable(url) {
    return typeof url === 'string' && /^https:\/\/[^\s"'<>]+$/.test(url);
  }

  function link(el, url, readyAttr) {
    if (usable(url)) {
      el.setAttribute('href', url);
      el.setAttribute(readyAttr, 'true');
    } else {
      el.removeAttribute('href');
      el.setAttribute(readyAttr, 'false');
    }
  }

  function repoDir(url) {
    return url.replace(/\/+$/, '').split('/').pop().replace(/\.git$/, '');
  }

  function fill(nodes, text) {
    for (var i = 0; i < nodes.length; i++) nodes[i].textContent = text;
  }

  function repoPage(path) {
    var base = P.CONFIG.repoUrl;
    if (!usable(base) || typeof path !== 'string' || !/^\/[\w./-]*$/.test(path)) return '';
    return base.replace(/\/+$/, '') + path;
  }

  P.applyConfig = function (doc) {
    var links = doc.querySelectorAll('[data-repo-link]');
    for (var i = 0; i < links.length; i++) link(links[i], P.CONFIG.repoUrl, 'data-repo-ready');
    var pages = doc.querySelectorAll('[data-repo-path]');
    for (var n = 0; n < pages.length; n++) link(pages[n], repoPage(pages[n].getAttribute('data-repo-path')), 'data-repo-ready');
    if (usable(P.CONFIG.repoUrl)) {
      fill(doc.querySelectorAll('[data-repo-url]'), P.CONFIG.repoUrl);
      fill(doc.querySelectorAll('[data-repo-dir]'), repoDir(P.CONFIG.repoUrl));
    }
    if (typeof P.CONFIG.version === 'string' && /^\d+\.\d+\.\d+$/.test(P.CONFIG.version)) {
      fill(doc.querySelectorAll('[data-version]'), P.CONFIG.version);
    }
    var donate = doc.querySelectorAll('[data-donate-link]');
    var donateReady = false;
    for (var j = 0; j < donate.length; j++) {
      var name = donate[j].getAttribute('data-donate-link');
      var urls = P.CONFIG.donate || {};
      link(donate[j], typeof urls[name] === 'string' ? urls[name] : '', 'data-link-ready');
      if (donate[j].getAttribute('data-link-ready') === 'true') {
        donateReady = true;
        donate[j].removeAttribute('hidden');
      } else {
        donate[j].setAttribute('hidden', '');
      }
    }
    var support = doc.querySelectorAll('[data-support]');
    for (var k = 0; k < support.length; k++) {
      if (donateReady) support[k].removeAttribute('hidden');
      else support[k].setAttribute('hidden', '');
    }
  };
})(window);
