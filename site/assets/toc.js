(function (window) {
  'use strict';

  var WIDE = '(min-width: 1280px)';

  function ready() {
    var doc = window.document;
    var box = doc.querySelector('[data-toc]');
    if (!box) return;
    var wide = window.matchMedia ? window.matchMedia(WIDE) : null;
    var links = Array.prototype.slice.call(box.querySelectorAll('a[href^="#"]'));
    var entries = [];
    var observer = null;

    function fit() {
      if (wide) box.open = wide.matches;
    }

    function mark(current) {
      links.forEach(function (link) {
        if (link === current.link) link.setAttribute('aria-current', 'location');
        else link.removeAttribute('aria-current');
      });
    }

    function watch() {
      if (observer && observer.disconnect) observer.disconnect();
      observer = null;
      if (!window.IntersectionObserver) return;
      observer = new window.IntersectionObserver(function (changes) {
        changes.forEach(function (change) {
          entries.forEach(function (entry) {
            if (entry.section === change.target) entry.seen = change.isIntersecting;
          });
        });
        var current = null;
        for (var i = 0; i < entries.length; i++) {
          if (entries[i].seen) {
            current = entries[i];
            break;
          }
        }
        if (current) mark(current);
      }, { rootMargin: '-80px 0px -55% 0px' });
      entries.forEach(function (entry) {
        observer.observe(entry.section);
      });
    }

    function build() {
      entries = [];
      links.forEach(function (link) {
        var section = doc.getElementById(link.getAttribute('href').slice(1));
        var shown = !!section && !section.hidden;
        link.parentNode.hidden = !shown;
        if (shown) entries.push({ link: link, section: section, seen: false });
        else link.removeAttribute('aria-current');
      });
      watch();
    }

    links.forEach(function (link) {
      link.addEventListener('click', function () {
        if (wide && !wide.matches) box.open = false;
      });
    });

    build();
    fit();
    if (wide && wide.addEventListener) wide.addEventListener('change', fit);
    doc.addEventListener('pytaichenko:version', build);
  }

  if (window.document.readyState === 'loading') {
    window.document.addEventListener('DOMContentLoaded', ready);
  } else {
    ready();
  }
})(window);
