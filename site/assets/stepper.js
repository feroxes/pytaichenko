(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});
  var registry = [];

  function nextStep(cur, n, dir) {
    if (dir > 0 && cur >= n) return 1;
    return Math.min(n, Math.max(1, cur + dir));
  }

  function has(dict, key) {
    return Object.prototype.hasOwnProperty.call(dict, key);
  }

  function text(lang, key) {
    var dict = P.TEXT && P.TEXT[lang];
    return dict && has(dict, key) ? dict[key] : null;
  }

  function isImage(el) {
    return String(el.tagName || '').toLowerCase() === 'img';
  }

  function expose(el, visible) {
    if (!isImage(el)) return;
    if (visible) el.removeAttribute('aria-hidden');
    else el.setAttribute('aria-hidden', 'true');
  }

  function first(el, selector) {
    return el.querySelectorAll(selector)[0] || null;
  }

  function stepsOf(el) {
    var declared = parseInt(el.getAttribute('data-steps'), 10);
    if (declared > 0) return declared;
    var max = 1;
    var names = ['data-s', 'data-only'];
    for (var j = 0; j < names.length; j++) {
      var groups = el.querySelectorAll('[' + names[j] + ']');
      for (var i = 0; i < groups.length; i++) {
        var k = parseInt(groups[i].getAttribute(names[j]), 10);
        if (k > max) max = k;
      }
    }
    return max;
  }

  function currentOf(el, n) {
    var cur = parseInt(el.getAttribute('data-cur'), 10);
    if (!(cur >= 1)) return 1;
    return Math.min(cur, n);
  }

  function escapeHtml(value) {
    return String(value).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function fillSizer(stepper, id, lang) {
    var sizer = first(stepper.el, '[data-stepper-sizer]');
    if (!sizer || stepper.sizedLang === lang) return;
    var html = '';
    for (var k = 1; k <= stepper.n; k++) {
      var value = text(lang, 'steps.' + id + '.' + k);
      if (value !== null) html += '<span>' + escapeHtml(value) + '</span>';
    }
    sizer.innerHTML = html;
    stepper.sizedLang = lang;
  }

  function render(stepper, lang) {
    var el = stepper.el;
    var n = stepper.n;
    var cur = currentOf(el, n);
    el.setAttribute('data-cur', String(cur));

    var i;
    var shown = el.querySelectorAll('[data-s]');
    for (i = 0; i < shown.length; i++) {
      var shownAt = parseInt(shown[i].getAttribute('data-s'), 10);
      shown[i].style.opacity = shownAt <= cur ? 1 : 0;
      expose(shown[i], shownAt === cur);
    }
    var only = el.querySelectorAll('[data-only]');
    for (i = 0; i < only.length; i++) {
      var onlyAt = parseInt(only[i].getAttribute('data-only'), 10);
      only[i].style.opacity = onlyAt === cur ? 1 : 0;
      expose(only[i], onlyAt === cur);
    }
    var fading = el.querySelectorAll('[data-hide]');
    for (i = 0; i < fading.length; i++) {
      fading[i].style.opacity = cur >= parseInt(fading[i].getAttribute('data-hide'), 10) ? 0.12 : 1;
    }

    var id = el.getAttribute('data-stepper');
    var caption = first(el, '[data-stepper-caption]');
    var captionText = text(lang, 'steps.' + id + '.' + cur);
    if (caption && captionText !== null) caption.textContent = captionText;
    fillSizer(stepper, id, lang);

    var count = first(el, '[data-stepper-count]');
    var template = text(lang, 'steps.count');
    if (count && template !== null) count.textContent = template.replace('{n}', String(cur)).replace('{m}', String(n));

    var prev = first(el, '[data-stepper-prev]');
    if (prev) {
      var prevText = text(lang, 'steps.prev');
      if (prevText !== null) prev.textContent = prevText;
      prev.disabled = cur === 1;
    }
    var next = first(el, '[data-stepper-next]');
    if (prev && next && prev.disabled && window.document.activeElement === prev && typeof next.focus === 'function') next.focus();
    if (next) {
      var nextText = text(lang, cur === n ? 'steps.restart' : 'steps.next');
      if (nextText !== null) next.textContent = nextText;
    }

    var dotsBox = first(el, '[data-stepper-dots]');
    if (dotsBox) {
      for (i = 0; i < dotsBox.children.length; i++) {
        dotsBox.children[i].setAttribute('data-on', i < cur ? 'true' : 'false');
      }
    }
  }

  function live(stepper, on) {
    var caption = first(stepper.el, '[data-stepper-caption]');
    if (caption) caption.setAttribute('aria-live', on ? 'polite' : 'off');
  }

  function go(stepper, dir) {
    for (var i = 0; i < registry.length; i++) live(registry[i], registry[i] === stepper);
    var cur = currentOf(stepper.el, stepper.n);
    stepper.el.setAttribute('data-cur', String(nextStep(cur, stepper.n, dir)));
    render(stepper, P.lang);
  }

  function bind(el) {
    if (el.getAttribute('data-stepper-ready') === 'true') return;
    el.setAttribute('data-stepper-ready', 'true');
    var stepper = { el: el, n: stepsOf(el) };
    var prev = first(el, '[data-stepper-prev]');
    var next = first(el, '[data-stepper-next]');
    if (prev) {
      prev.addEventListener('click', function () {
        go(stepper, -1);
      });
    }
    if (next) {
      next.addEventListener('click', function () {
        go(stepper, 1);
      });
    }
    registry.push(stepper);
    live(stepper, false);
    if (P.applyText && P.lang) P.applyText(el, P.lang);
    render(stepper, P.lang);
  }

  function initSteppers(root) {
    var found = root.querySelectorAll('[data-stepper]');
    for (var i = 0; i < found.length; i++) bind(found[i]);
  }

  function onLang(event) {
    var lang = (event && event.detail && event.detail.lang) || P.lang;
    for (var i = 0; i < registry.length; i++) {
      live(registry[i], false);
      render(registry[i], lang);
    }
  }

  P.nextStep = nextStep;
  P.initSteppers = initSteppers;

  window.document.addEventListener('pytaichenko:lang', onLang);
  initSteppers(window.document);
})(window);
