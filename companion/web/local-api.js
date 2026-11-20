'use strict';

// ---------------------------------------------------------------------------
// Standalone app backend (Android WebView).
//
// The normal companion is a local HTTP server. Inside the Android app there is
// no Node process; this script installs a fetch()/EventSource shim that answers
// the same /api/... routes from the Java bridge (window.PicoAndroid). The UI,
// theme parser and UF2 inspector are the exact same files the desktop uses.
// ---------------------------------------------------------------------------

(function () {
  if (!window.PicoAndroid) return;

  window.PicoPadNative = true;
  const CFG_KEY = 'picopad.companion.config';
  const listeners = new Set();
  const pending = new Map();
  let seq = 0;

  const cfg = Object.assign(
    { sdUri: '', sdLabel: '', espHost: '', espPort: 2323, serialBaud: 115200 },
    readCfg()
  );
  const device = {
    connected: false,
    transport: null,
    info: null,
    history: [],
  };

  function readCfg() {
    try {
      return JSON.parse(localStorage.getItem(CFG_KEY) || '{}') || {};
    } catch {
      return {};
    }
  }

  function saveCfg() {
    try {
      localStorage.setItem(CFG_KEY, JSON.stringify(cfg));
    } catch {
      /* ignore */
    }
  }

  // --- bridge -------------------------------------------------------------

  window.__picoResult = function (id, json) {
    const p = pending.get(id);
    if (!p) return;
    pending.delete(id);
    let msg;
    try {
      msg = JSON.parse(json);
    } catch {
      p.reject(new Error('Bad native reply'));
      return;
    }
    if (msg.ok) p.resolve(msg.data);
    else p.reject(new Error(msg.error || 'Native call failed'));
  };

  window.__picoEvent = function (name, json) {
    let data = {};
    try {
      data = JSON.parse(json);
    } catch {
      /* ignore */
    }
    if (name === 'line' && typeof data.line === 'string') {
      device.history.push(data.line);
      if (device.history.length > 400) device.history.shift();
    } else if (name === 'state') {
      device.connected = !!data.connected;
      device.transport = data.transport || null;
      device.info = data.info || null;
    }
    for (const es of listeners) es.emit(name, data);
  };

  function call(method, args) {
    return new Promise((resolve, reject) => {
      const id = 'n' + ++seq;
      pending.set(id, { resolve, reject });
      try {
        window.PicoAndroid.call(id, method, JSON.stringify(args || {}));
      } catch (e) {
        pending.delete(id);
        reject(e);
      }
    });
  }

  function needTree() {
    if (!cfg.sdUri) throw new Error('Choose the SD card folder first');
    return cfg.sdUri;
  }

  function publicConfig() {
    return {
      sdPath: cfg.sdLabel || null,
      espHost: cfg.espHost || '',
      espPort: Number(cfg.espPort) || 2323,
      serialPort: null,
      serialBaud: Number(cfg.serialBaud) || 115200,
    };
  }

  function deviceState() {
    return {
      connected: device.connected,
      transport: device.transport,
      info: device.info,
      serialAvailable: true,
      serialError: null,
      history: device.history,
    };
  }

  // --- base64 / response helpers -----------------------------------------

  function bytesToB64(bytes) {
    let s = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      s += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }
    return btoa(s);
  }

  function b64ToBytes(b64) {
    const bin = atob(b64 || '');
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  function jsonResponse(obj, status) {
    return new Response(JSON.stringify(obj), {
      status: status || 200,
      headers: { 'Content-Type': 'application/json; charset=utf-8' },
    });
  }

  function errorResponse(message, status) {
    return jsonResponse({ error: message }, status || 400);
  }

  async function bodyBytes(init) {
    const b = init && init.body;
    if (b == null) return null;
    if (b instanceof ArrayBuffer) return new Uint8Array(b);
    if (ArrayBuffer.isView(b)) return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
    if (typeof b === 'string') return new TextEncoder().encode(b);
    if (typeof b.arrayBuffer === 'function') return new Uint8Array(await b.arrayBuffer());
    return null;
  }

  async function readBodyJson(init) {
    const bytes = await bodyBytes(init);
    if (!bytes || !bytes.length) return {};
    return JSON.parse(new TextDecoder().decode(bytes));
  }

  function mimeFor(name) {
    const ext = String(name).toLowerCase().match(/\.[a-z0-9]+$/);
    return (
      {
        '.uf2': 'application/octet-stream',
        '.bin': 'application/octet-stream',
        '.bmp': 'image/bmp',
        '.png': 'image/png',
        '.jpg': 'image/jpeg',
        '.gif': 'image/gif',
        '.wav': 'audio/wav',
        '.mp3': 'audio/mpeg',
        '.thm': 'text/plain',
        '.txt': 'text/plain',
        '.md': 'text/plain',
        '.zip': 'application/zip',
      }[(ext && ext[0]) || ''] || 'application/octet-stream'
    );
  }

  // --- SD helpers ---------------------------------------------------------

  async function sdList(rel) {
    return call('list', { tree: needTree(), rel: rel || '' });
  }

  async function sdReadText(rel) {
    return call('readText', { tree: needTree(), rel });
  }

  async function sdReadBytes(rel) {
    return b64ToBytes(await call('readBytes', { tree: needTree(), rel }));
  }

  async function sdWriteBytes(rel, bytes) {
    return call('writeBytes', { tree: needTree(), rel, base64: bytesToB64(bytes) });
  }

  async function sdWriteText(rel, text) {
    return call('writeText', { tree: needTree(), rel, text: String(text) });
  }

  async function sdExists(rel) {
    try {
      return !!(await call('exists', { tree: needTree(), rel }));
    } catch {
      return false;
    }
  }

  async function themesRel() {
    // Cards written by older launchers may use /Themes.
    try {
      if (await sdExists('themes')) return 'themes';
      if (await sdExists('Themes')) return 'Themes';
    } catch {
      /* fall through */
    }
    return 'themes';
  }

  async function themeFileSource(name) {
    const dir = await themesRel();
    const rel = `${dir}/${name}`;
    if (await sdExists(rel)) return rel;
    return `themes/${name}`;
  }

  // --- API routes ---------------------------------------------------------

  async function route(url, init) {
    const p = url.pathname;
    const q = url.searchParams;
    const method = (init && init.method) || 'GET';

    if (p === '/api/health') return jsonResponse({ ok: true, version: 1, native: true });

    if (p === '/api/state') {
      return jsonResponse({
        sd: cfg.sdUri
          ? { mounted: true, path: cfg.sdLabel || 'SD', label: cfg.sdLabel || 'SD', free: null, total: null }
          : { mounted: false },
        detect: { path: cfg.sdLabel || '', candidates: [] },
        device: deviceState(),
        serial: { available: true, error: null },
        config: publicConfig(),
      });
    }

    if (p === '/api/config') {
      if (method === 'GET') return jsonResponse(publicConfig());
      const body = await readBodyJson(init);
      if (typeof body.espHost === 'string') cfg.espHost = body.espHost.trim();
      if (body.espPort != null) cfg.espPort = Number(body.espPort) || 2323;
      if (body.serialBaud != null) cfg.serialBaud = Number(body.serialBaud) || 115200;
      saveCfg();
      return jsonResponse(publicConfig());
    }

    // --- SD ---------------------------------------------------------------
    if (p === '/api/sd') {
      return jsonResponse(
        cfg.sdUri
          ? { mounted: true, path: cfg.sdLabel || 'SD', label: cfg.sdLabel || 'SD', free: null, total: null }
          : { mounted: false }
      );
    }

    if (p === '/api/sd/list') {
      const entries = await sdList(q.get('path') || '');
      return jsonResponse({ path: q.get('path') || '', entries });
    }

    if (p === '/api/sd/download') {
      const rel = q.get('path') || '';
      const bytes = await sdReadBytes(rel);
      return new Response(bytes, {
        status: 200,
        headers: {
          'Content-Type': mimeFor(rel),
          'Content-Length': String(bytes.length),
        },
      });
    }

    if (p === '/api/sd/upload') {
      const rel = q.get('path');
      const bytes = await bodyBytes(init);
      const r = await sdWriteBytes(rel, bytes || new Uint8Array());
      return jsonResponse(r);
    }

    if (p === '/api/sd/mkdir') {
      const b = await readBodyJson(init);
      return jsonResponse(await call('mkdir', { tree: needTree(), rel: b.path }));
    }

    if (p === '/api/sd/delete') {
      const b = await readBodyJson(init);
      return jsonResponse(await call('delete', { tree: needTree(), rel: b.path }));
    }

    if (p === '/api/sd/rename') {
      const b = await readBodyJson(init);
      const i = String(b.from || '').lastIndexOf('/');
      const dir = i >= 0 ? String(b.from).slice(0, i) : '';
      const base = String(b.to || '').slice(String(b.to).lastIndexOf('/') + 1);
      return jsonResponse(await call('rename', { tree: needTree(), rel: b.from, dir, name: base }));
    }

    if (p === '/api/sd/restore') {
      if (!cfg.sdUri) return errorResponse('Choose the SD card folder first', 409);
      const bytes = await bodyBytes(init);
      const cache = await call('writeCache', {
        name: 'restore-' + Date.now() + '.zip',
        base64: bytesToB64(bytes || new Uint8Array()),
      });
      return jsonResponse(await call('restore', { tree: cfg.sdUri, path: cache.path }));
    }

    // --- themes -----------------------------------------------------------
    if (p === '/api/themes') {
      const source = q.get('source') || 'sd';
      if (source === 'repo') {
        const files = await call('assetList', { dir: 'themes' });
        return jsonResponse({
          source: 'repo',
          active: null,
          themes: files
            .filter((f) => /\.(thm|ini)$/i.test(f.name))
            .sort((a, b) => a.name.localeCompare(b.name)),
        });
      }
      if (!cfg.sdUri) return jsonResponse({ source: 'sd', themes: [], active: null });
      const dir = await themesRel();
      const entries = await sdList(dir);
      let active = null;
      try {
        active = (await sdReadText(`${dir}/active.cfg`)).trim() || null;
      } catch {
        /* no active theme */
      }
      const themes = entries
        .filter((e) => !e.dir && /\.(thm|ini)$/i.test(e.name))
        .sort((a, b) => a.name.localeCompare(b.name))
        .map((e) => ({ name: e.name, size: e.size, mtime: e.mtime }));
      return jsonResponse({ source: 'sd', active, themes });
    }

    if (p === '/api/themes/file') {
      const source = q.get('source') || 'sd';
      const name = q.get('name');
      if (source === 'repo') {
        if (method === 'GET') {
          const text = await call('assetText', { path: `themes/${name}` });
          return jsonResponse({ name, source, size: text.length, text, mtime: 0 });
        }
        return errorResponse('Examples are read-only', 403);
      }
      if (method === 'GET') {
        const rel = await themeFileSource(name);
        const text = await sdReadText(rel);
        const st = await call('stat', { tree: needTree(), rel });
        return jsonResponse({ name, source, size: st.size, text, mtime: st.mtime });
      }
      if (method === 'PUT') {
        const body = await readBodyJson(init);
        const dir = await themesRel();
        const r = await sdWriteText(`${dir}/${name}`, body.text == null ? '' : body.text);
        return jsonResponse({ name, source, size: r.size, mtime: Date.now() });
      }
      return errorResponse('Method not allowed', 405);
    }

    if (p === '/api/themes/active') {
      const body = await readBodyJson(init);
      if (!cfg.sdUri) return errorResponse('Choose the SD card folder first', 409);
      const dir = await themesRel();
      const rel = `${dir}/active.cfg`;
      if (body.name) await sdWriteText(rel, String(body.name).trim() + '\n');
      else await call('delete', { tree: cfg.sdUri, rel }).catch(() => {});
      return jsonResponse({ active: body.name || null });
    }

    if (p === '/api/themes/parse') {
      const body = await readBodyJson(init);
      return jsonResponse({ theme: window.PicoTheme.parse(String(body.text == null ? '' : body.text)) });
    }

    // --- updates ----------------------------------------------------------
    if (p === '/api/uf2/inspect') {
      const bytes = await bodyBytes(init);
      return jsonResponse({ report: window.PicoUf2.inspect(bytes || new Uint8Array()) });
    }

    if (p === '/api/uf2/stage') {
      const bytes = await bodyBytes(init);
      const report = window.PicoUf2.inspect(bytes || new Uint8Array());
      if (!report.valid) return errorResponse('Not a valid UF2 image');
      const had = await sdExists('update.uf2');
      if (had) await sdWriteBytes('update.uf2.bak', await sdReadBytes('update.uf2'));
      await sdWriteBytes('update.uf2', bytes);
      return jsonResponse({
        report,
        staged: { size: bytes.length, backup: had, name: 'update.uf2' },
      });
    }

    if (p === '/api/uf2/local') return jsonResponse({ local: null });
    if (p === '/api/uf2/local/file') return errorResponse('Not available in the app', 404);

    // --- device -----------------------------------------------------------
    if (p === '/api/device/ports') {
      const list = await call('usbList', {});
      return jsonResponse({
        serialAvailable: true,
        serialError: null,
        ports: list.map((d) => ({
          path: 'usb:' + d.id,
          manufacturer: d.label || 'USB device',
        })),
      });
    }

    if (p === '/api/device/state') return jsonResponse(deviceState());

    if (p === '/api/device/connect') {
      const body = await readBodyJson(init);
      if (body.transport === 'wifi') {
        const host = body.host || cfg.espHost;
        const port = Number(body.port || cfg.espPort) || 2323;
        cfg.espHost = host;
        cfg.espPort = port;
        saveCfg();
        const st = await call('tcpConnect', { host, port });
        return jsonResponse(st);
      }
      const id = Number(String(body.path || '').replace(/^usb:/, ''));
      if (!id) return errorResponse('Pick a USB device first');
      const baud = Number(body.baud || cfg.serialBaud) || 115200;
      cfg.serialBaud = baud;
      saveCfg();
      return jsonResponse(await call('usbConnect', { id, baud }));
    }

    if (p === '/api/device/disconnect') {
      await call('disconnect', {});
      return jsonResponse(deviceState());
    }

    if (p === '/api/device/send') {
      const body = await readBodyJson(init);
      await call('send', { line: body.line, target: body.target || 'pico' });
      return jsonResponse({ sent: body.line });
    }

    return errorResponse('Unknown API endpoint', 404);
  }

  // --- fetch / EventSource shims -----------------------------------------

  const realFetch = window.fetch ? window.fetch.bind(window) : null;

  window.fetch = function (input, init) {
    let url;
    try {
      url = new URL(typeof input === 'string' ? input : input.url, 'http://local/');
    } catch {
      return realFetch ? realFetch(input, init) : Promise.reject(new Error('Bad URL'));
    }
    if (!url.pathname.startsWith('/api/')) {
      return realFetch ? realFetch(input, init) : Promise.reject(new Error('Offline'));
    }
    return route(url, init || {}).catch((e) => errorResponse(e.message, 400));
  };

  class LocalEventSource {
    constructor() {
      this._l = Object.create(null);
      listeners.add(this);
      const self = this;
      setTimeout(() => self.emit('state', deviceState()), 0);
    }
    addEventListener(name, fn) {
      (this._l[name] || (this._l[name] = [])).push(fn);
    }
    emit(name, data) {
      const ev = { data: JSON.stringify(data) };
      for (const fn of this._l[name] || []) {
        try {
          fn(ev);
        } catch {
          /* ignore */
        }
      }
    }
    close() {
      listeners.delete(this);
    }
  }

  window.EventSource = LocalEventSource;

  // --- small API used by app.js ------------------------------------------

  window.PicoPadNative = {
    sdLabel: () => cfg.sdLabel || '',
    async chooseSdFolder() {
      const r = await call('pickTree', {});
      cfg.sdUri = r.uri;
      cfg.sdLabel = r.label || 'SD card';
      saveCfg();
      return r;
    },
    async saveSdFile(rel, name) {
      const base64 = await call('readBytes', { tree: needTree(), rel });
      return call('saveFile', { name: name || rel.split('/').pop(), mime: mimeFor(name || rel), base64 });
    },
    async backupSd() {
      return call('backup', { tree: needTree() });
    },
  };

  document.addEventListener('DOMContentLoaded', () => {
    document.body.classList.add('native-app');
  });
})();
