(function (window) {
  'use strict';

  var P = (window.PYTAICHENKO = window.PYTAICHENKO || {});
  var STORAGE_KEY = 'pytaichenko-version';
  var VERSIONS = ['2', '1'];

  function known(version) {
    return VERSIONS.indexOf(version) >= 0;
  }

  function released() {
    var releases = P.RELEASES;
    if (!releases || typeof releases !== 'object') return VERSIONS[0];
    for (var i = 0; i < VERSIONS.length; i++) {
      if (Object.prototype.hasOwnProperty.call(releases, VERSIONS[i]) && releases[VERSIONS[i]]) return VERSIONS[i];
    }
    return VERSIONS[0];
  }

  function pickVersion(fromUrl, saved) {
    if (known(fromUrl)) return fromUrl;
    if (known(saved)) return saved;
    return released();
  }

  function versionFromSearch(search) {
    var found = /[?&]v=([^&#]*)/.exec(String(search || ''));
    return found && known(found[1]) ? found[1] : null;
  }

  function hashId(hash) {
    var id = String(hash || '').replace(/^#/, '');
    try {
      return decodeURIComponent(id);
    } catch (e) {
      return id;
    }
  }

  function holds(block, id) {
    if (block.getAttribute('id') === id) return true;
    var inner = block.querySelectorAll('[id]');
    for (var i = 0; i < inner.length; i++) {
      if (inner[i].getAttribute('id') === id) return true;
    }
    return false;
  }

  function versionForHash(doc, hash, fallback) {
    var id = hashId(hash);
    if (!id) return fallback;
    var owners = [];
    var blocks = doc.querySelectorAll('[data-for-v]');
    for (var i = 0; i < blocks.length; i++) {
      if (holds(blocks[i], id)) owners.push(blocks[i].getAttribute('data-for-v'));
    }
    if (!owners.length || owners.indexOf(fallback) >= 0) return fallback;
    for (var j = 0; j < VERSIONS.length; j++) {
      if (owners.indexOf(VERSIONS[j]) >= 0) return VERSIONS[j];
    }
    return fallback;
  }

  function applyVersion(doc, version) {
    var blocks = doc.querySelectorAll('[data-for-v]');
    for (var i = 0; i < blocks.length; i++) {
      if (blocks[i].getAttribute('data-for-v') === version) blocks[i].removeAttribute('hidden');
      else blocks[i].setAttribute('hidden', '');
    }
    var buttons = doc.querySelectorAll('[data-set-version]');
    for (var j = 0; j < buttons.length; j++) {
      var on = buttons[j].getAttribute('data-set-version') === version;
      buttons[j].setAttribute('aria-pressed', on ? 'true' : 'false');
    }
  }

  function markUrl(version) {
    var history = window.history;
    if (!window.location || !history || typeof history.replaceState !== 'function' || !P.urlWithParams) return;
    var params = {};
    if (P.lang) params.lang = P.lang;
    params.v = version;
    try {
      history.replaceState(history.state, '', P.urlWithParams(window.location.href, params));
    } catch (e) {}
  }

  function setVersion(version) {
    if (!known(version)) return;
    var doc = window.document;
    var changed = version !== P.version;
    P.version = version;
    applyVersion(doc, version);
    markUrl(version);
    try {
      window.localStorage.setItem(STORAGE_KEY, version);
    } catch (e) {}
    if (changed) doc.dispatchEvent(new window.CustomEvent('pytaichenko:version', { detail: { version: version } }));
  }

  function readSaved() {
    try {
      return window.localStorage.getItem(STORAGE_KEY);
    } catch (e) {
      return null;
    }
  }

  P.VERSIONS = VERSIONS;
  P.pickVersion = pickVersion;
  P.versionFromSearch = versionFromSearch;
  P.applyVersion = applyVersion;
  P.setVersion = setVersion;

  var doc = window.document;
  var place = window.location || {};
  P.version = versionFromSearch(place.search) || versionForHash(doc, place.hash, pickVersion(null, readSaved()));
  applyVersion(doc, P.version);
  var buttons = doc.querySelectorAll('[data-set-version]');
  for (var i = 0; i < buttons.length; i++) {
    (function (button) {
      button.addEventListener('click', function () {
        setVersion(button.getAttribute('data-set-version'));
      });
    })(buttons[i]);
  }
})(window);
