// SPDX-License-Identifier: EUPL-1.2
// Copyright (c) 2026 Chura IT Consultancy (Kustgloed)
'use strict';

const assert = require('node:assert');
const { describe, it } = require('node:test');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const dataDir = path.join(__dirname, '..', 'wled00', 'data');
const read = (file) => fs.readFileSync(path.join(dataDir, file), 'utf8');

// Runs common.js in a sandbox with a fake document, storage and window.
function loadCommon({ stored, storageThrows, readyState = 'complete' } = {}) {
  const listeners = [];
  const store = new Map(stored ? [['kite-lang', stored]] : []);
  const opened = [];
  const makeButton = () => ({ setAttribute(k, v) { this[k] = v; } });
  const buttons = [makeButton(), makeButton()];
  const ctx = {
    document: { readyState, querySelectorAll: () => buttons, addEventListener: (ev, fn) => listeners.push([ev, fn]) },
    window: { open: (...args) => opened.push(args) },
    localStorage: {
      getItem: (k) => { if (storageThrows) throw new Error('blocked'); return store.has(k) ? store.get(k) : null; },
      setItem: (k, v) => { if (storageThrows) throw new Error('blocked'); store.set(k, v); },
    },
  };
  vm.createContext(ctx);
  vm.runInContext(read('common.js'), ctx);
  return { ctx, buttons, opened, store, listeners };
}

describe('common.js language helpers', () => {
  it('defaults to Dutch', () => {
    const { ctx } = loadCommon();
    assert.strictEqual(ctx.kiteLang(), 'nl');
    assert.strictEqual(ctx.kiteSite(), 'https://kustgloed.nl/');
    assert.strictEqual(ctx.kiteSite('status-lamp/'), 'https://kustgloed.nl/status-lamp/');
  });

  it('uses the stored English choice', () => {
    const { ctx } = loadCommon({ stored: 'en' });
    assert.strictEqual(ctx.kiteLang(), 'en');
    assert.strictEqual(ctx.kiteSite('firmware-update/'), 'https://kustgloed.nl/en/firmware-update/');
  });

  it('ignores an unknown stored value', () => {
    assert.strictEqual(loadCommon({ stored: 'de' }).ctx.kiteLang(), 'nl');
  });

  it('falls back to Dutch when storage is blocked', () => {
    const { ctx } = loadCommon({ storageThrows: true });
    assert.strictEqual(ctx.kiteLang(), 'nl');
    ctx.kiteSetLang('en'); // must not throw
  });

  it('stores the chosen language', () => {
    const { ctx, store } = loadCommon();
    ctx.kiteSetLang('en');
    assert.strictEqual(store.get('kite-lang'), 'en');
  });

  it('no longer carries the upstream documentation helpers', () => {
    const { ctx } = loadCommon();
    assert.strictEqual(typeof ctx.H, 'undefined');
    assert.strictEqual(typeof ctx.GH, 'undefined');
  });
});

describe('common.js home icon', () => {
  it('fills every help button with an icon and a Dutch label', () => {
    const { buttons, opened } = loadCommon();
    for (const b of buttons) {
      assert.match(b.innerHTML, /<svg/);
      assert.strictEqual(b.title, 'Kustgloed-website');
      assert.strictEqual(b['aria-label'], 'Kustgloed-website');
    }
    buttons[0].onclick();
    assert.deepStrictEqual(opened[0], ['https://kustgloed.nl/', '_blank', 'noopener,noreferrer']);
  });

  it('uses the English label and site', () => {
    const { buttons, opened } = loadCommon({ stored: 'en' });
    assert.strictEqual(buttons[0].title, 'Kustgloed website');
    buttons[0].onclick();
    assert.strictEqual(opened[0][0], 'https://kustgloed.nl/en/');
  });

  it('waits for DOMContentLoaded while the page is still loading', () => {
    const { buttons, listeners } = loadCommon({ readyState: 'loading' });
    assert.strictEqual(buttons[0].innerHTML, undefined);
    assert.strictEqual(listeners[0][0], 'DOMContentLoaded');
    listeners[0][1]();
    assert.match(buttons[0].innerHTML, /<svg/);
  });
});

const helpPages = fs.readdirSync(dataDir).filter((f) => f.endsWith('.htm') && read(f).includes('class="helpB"'));

describe('help buttons', () => {
  it('finds the ten pages that have one', () => {
    assert.strictEqual(helpPages.length, 10, helpPages.join(', '));
  });

  for (const page of helpPages) {
    it(`${page} uses the shared home icon`, () => {
      const html = read(page);
      assert.ok(html.includes('<div class="helpB"><button type="button"></button></div>'), 'icon markup');
      assert.doesNotMatch(html, /\bHW?\(/, 'no upstream documentation call');
      assert.match(html, /(l\.src\s*=\s*|src=)["']common\.js["']/, 'loads common.js');
    });
  }
});

describe('welcome page', () => {
  const html = read('welcome.htm');

  it('is self-contained Kustgloed copy in both languages', () => {
    assert.match(html, /<script src="common\.js">/);
    assert.match(html, /Welkom bij Kustgloed/);
    assert.match(html, /Welcome to Kustgloed/);
    assert.doesNotMatch(html, /wled\.me|data:image|WLED/i);
  });

  it('links the settings page and both guides through the language helpers', () => {
    assert.match(html, /\.\/settings/);
    assert.match(html, /kiteSite\('firmware-update\/'\)/);
    assert.match(html, /kiteSite\('status-lamp\/'\)/);
    assert.match(html, /kiteSetLang/);
  });
});

describe('server root route', () => {
  it('serves the welcome page instead of redirecting', () => {
    const cpp = fs.readFileSync(path.join(__dirname, '..', 'wled00', 'wled_server.cpp'), 'utf8');
    const block = cpp.slice(cpp.indexOf('#ifdef KITE_WIND_SETTINGS_HOME'), cpp.indexOf('#else', cpp.indexOf('#ifdef KITE_WIND_SETTINGS_HOME')));
    assert.match(block, /PAGE_welcome/);
    assert.doesNotMatch(block, /302/);
  });
});

describe('installed version', () => {
  it('update page strips the build suffix', () => {
    assert.match(read('update.htm'), /\(d\.version \|\| 'Unknown'\)\.split\('\+'\)\[0\]/);
  });

  it('security page reads /kite-version instead of the upstream version', () => {
    const html = read('settings_sec.htm');
    assert.match(html, /\/kite-version/);
    assert.match(html, /split\('\+'\)\[0\]/);
    assert.doesNotMatch(html, /<span class="sip"[^>]*>WLED ##VERSION##/);
  });
});

describe('C3 build flags', () => {
  it('kite_c3 serves the welcome page at the root path', () => {
    const ini = fs.readFileSync(path.join(__dirname, '..', 'platformio_override.sample.ini'), 'utf8');
    const start = ini.indexOf('[env:kite_c3]');
    const end = ini.indexOf('\n[', start + 1);
    assert.match(ini.slice(start, end < 0 ? undefined : end), /-D KITE_WIND_SETTINGS_HOME/);
  });
});
