(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});

  P.CONFIG = {
    repoUrl: 'https://github.com/feroxes/pytaichenko',
    usdRate: 45.53,
    ratesDate: '2026-10-31',
    donate: {
      donatello: 'https://donatello.to/feroxes'
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

  function releaseNumber() {
    var releases = P.RELEASES;
    if (!releases || typeof P.version !== 'string' || !Object.prototype.hasOwnProperty.call(releases, P.version)) return '';
    var release = releases[P.version];
    var number = release && release.version;
    return typeof number === 'string' && /^\d+\.\d+\.\d+$/.test(number) ? number : '';
  }

  function showVersion(doc) {
    var number = releaseNumber();
    fill(doc.querySelectorAll('[data-version]'), number);
    var boxes = doc.querySelectorAll('[data-version-box]');
    for (var i = 0; i < boxes.length; i++) {
      if (number) boxes[i].removeAttribute('hidden');
      else boxes[i].setAttribute('hidden', '');
    }
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
    showVersion(doc);
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

  var doc = window.document;
  if (doc && typeof doc.addEventListener === 'function') {
    doc.addEventListener('pytaichenko:version', function () {
      showVersion(doc);
    });
    doc.addEventListener('pytaichenko:lang', function () {
      showVersion(doc);
    });
  }
})(window);
