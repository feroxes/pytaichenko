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

    function fit() {
      if (wide) box.open = wide.matches;
    }

    links.forEach(function (link) {
      var section = doc.getElementById(link.getAttribute('href').slice(1));
      if (!section || section.hidden) {
        link.parentNode.hidden = true;
        return;
      }
      entries.push({ link: link, section: section, seen: false });
      link.addEventListener('click', function () {
        if (wide && !wide.matches) box.open = false;
      });
    });

    fit();
    if (wide && wide.addEventListener) wide.addEventListener('change', fit);

    if (!window.IntersectionObserver) return;
    var observer = new window.IntersectionObserver(function (changes) {
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
      if (!current) return;
      entries.forEach(function (entry) {
        if (entry === current) entry.link.setAttribute('aria-current', 'location');
        else entry.link.removeAttribute('aria-current');
      });
    }, { rootMargin: '-80px 0px -55% 0px' });
    entries.forEach(function (entry) {
      observer.observe(entry.section);
    });
  }

  if (window.document.readyState === 'loading') {
    window.document.addEventListener('DOMContentLoaded', ready);
  } else {
    ready();
  }
})(window);
