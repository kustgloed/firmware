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
  it('finds the eight pages that have one', () => {
    assert.strictEqual(helpPages.length, 8, helpPages.join(', '));
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

// Evaluates the page's translation table and compares the NL and EN keys.
function assertSameKeys(html) {
  const T = vm.runInNewContext('(' + html.match(/const T = (\{[\s\S]*?\n\t+\});/)[1] + ')');
  assert.deepStrictEqual(Object.keys(T.en).sort(), Object.keys(T.nl).sort());
}

describe('welcome page', () => {
  const html = read('welcome.htm');

  it('is self-contained Kustgloed copy in both languages', () => {
    assert.match(html, /<script src="common\.js">/);
    assert.match(html, /Welkom bij Kustgloed/);
    assert.match(html, /Welcome to Kustgloed/);
    assert.doesNotMatch(html, /wled\.me|data:image|WLED/i);
  });

  it('links the hub tiles, the lamp manual and the test button', () => {
    assert.doesNotMatch(html, /\.\/settings/);
    assert.match(html, /data-t="wifiT"/);
    assert.match(html, /href="\.\/update"/);
    assert.match(html, /data-site="status-lamp\/"/);
    assert.match(html, /kiteLinks\(\)/);
    assert.match(html, /kiteI18n\(T/);
    assert.match(html, /kiteLampTest/);
    assert.match(html, /\/json\/info/);
  });

  it('shows the welcome screen only until the start button was used', () => {
    assert.match(html, /kite-welcomed/);
    assert.match(html, /id="start"/);
  });

  it('has the same keys in both languages', () => {
    assertSameKeys(html);
  });
});

describe('update page', () => {
  const html = read('update.htm');

  it('keeps the upload form fields and the lamp manual link', () => {
    assert.match(html, /name="OP"/);
    assert.match(html, /name="update"/);
    assert.match(html, /name="skipValidation"/);
    assert.match(html, /data-site="status-lamp\/"/);
    assert.match(html, /data-site="firmware-update\/"/);
  });

  it('has the same keys in both languages', () => {
    assertSameKeys(html);
  });

  it('shows progress, waits for the reboot and has a rollback hint', () => {
    assert.match(html, /upload\.onprogress/);
    assert.match(html, /waitBack/);
    assert.match(html, /rbT/);
  });
});

describe('site links', () => {
  it('add the firmware version and board to each site link', async () => {
    const { ctx } = loadCommon();
    const links = [{ href: '', getAttribute: () => 'status-lamp/' }];
    ctx.document.querySelectorAll = () => links;
    ctx.fetch = () => Promise.resolve({ ok: true, json: () => ({ version: '1.2.3+abc', board: 'esp32c3' }) });
    ctx.kiteLinks();
    assert.strictEqual(links[0].href, 'https://kustgloed.nl/status-lamp/');
    await new Promise((r) => setTimeout(r, 0));
    assert.strictEqual(links[0].href, 'https://kustgloed.nl/status-lamp/?fw=1.2.3&hw=esp32c3');
  });
});

describe('local lamp test', () => {
  it('the usermod reads kiteLampTest from the JSON state', () => {
    const cpp = fs.readFileSync(path.join(__dirname, '..', 'usermods', 'kite_wind', 'kite_wind.cpp'), 'utf8');
    assert.match(cpp, /readFromJsonState\(JsonObject& root\) override/);
    assert.match(cpp, /root\["kiteLampTest"\]/);
  });
});

describe('setup pages', () => {
  const wifi = read('settings_wifi.htm');

  it('Wi-Fi page keeps to the station form fields and the controller script hooks', () => {
    assert.match(wifi, /function resetWiFi/);
    assert.match(wifi, /function addWiFi\(ssid/);
    assert.match(wifi, /name="D0"/);
    assert.match(wifi, /class="sip"/);
    assert.match(wifi, /name="Sf"/);
    assert.doesNotMatch(wifi, /name="ET|name="EA|name="EI/);
    assert.match(wifi, /N\(\);\s*\n\t\t\}/, 'scans on load');
  });

  it('Wi-Fi and PIN pages are translated and escape network values', () => {
    assertSameKeys(wifi);
    assert.match(wifi, /esc\(ssid\)/);
    const pin = read('settings_pin.htm');
    assert.match(pin, /name="PIN"/);
    assert.match(pin, /kiteI18n/);
  });

  it('message page shows both languages and a countdown for the Wi-Fi result', () => {
    const msg = read('msg.htm');
    assert.match(msg, /kiteI18n/);
    assert.match(msg, /getElementById\("cd"\)/);
    const cpp = fs.readFileSync(path.join(__dirname, '..', 'wled00', 'wled_server.cpp'), 'utf8');
    assert.match(cpp, /RESULT_WINDOW_MS \/ 1000/);
    assert.match(cpp, /body, 255\)/);
  });

  it('keeps the setup AP up for two minutes while a phone is connected', () => {
    const h = fs.readFileSync(path.join(__dirname, '..', 'wled00', 'kite_access.h'), 'utf8');
    assert.match(h, /RESULT_WINDOW_MS = 120000;/);
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
    assert.match(read('update.htm'), /kiteVersion\(\)/);
    assert.match(read('common.js'), /\(j\.version \|\| ""\)\.split\("\+"\)\[0\]/);
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
