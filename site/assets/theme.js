(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});
  var STORAGE_KEY = 'pytaichenko-theme';
  var THEMES = ['dark', 'light'];

  function readSaved() {
    try {
      return window.localStorage.getItem(STORAGE_KEY);
    } catch (e) {
      return null;
    }
  }

  function markToggles() {
    var buttons = window.document.querySelectorAll('[data-theme-toggle]');
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].setAttribute('aria-pressed', P.theme === 'light' ? 'true' : 'false');
    }
  }

  function applyTheme(theme) {
    P.theme = theme;
    window.document.documentElement.setAttribute('data-theme', theme);
    markToggles();
  }

  function setTheme(theme) {
    if (THEMES.indexOf(theme) < 0) return;
    applyTheme(theme);
    try {
      window.localStorage.setItem(STORAGE_KEY, theme);
    } catch (e) {}
  }

  function ready() {
    var buttons = window.document.querySelectorAll('[data-theme-toggle]');
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].addEventListener('click', function () {
        setTheme(P.theme === 'light' ? 'dark' : 'light');
      });
    }
    markToggles();
  }

  P.setTheme = setTheme;

  var saved = readSaved();
  applyTheme(THEMES.indexOf(saved) >= 0 ? saved : 'dark');

  if (window.document.readyState === 'loading') {
    window.document.addEventListener('DOMContentLoaded', ready);
  } else {
    ready();
  }
})(window);
